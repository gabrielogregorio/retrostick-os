/* Runner audio: core callbacks, dynamic rate control, fade-in, sending to the device, statistics.
 *
 * Dynamic rate control (same as RetroArch): the TV retrace (59.94 or 60 Hz) does not match the core
 * clock (NES 60.1 Hz), so each frame's audio is stretched or shrunk by up to DRC_MAX depending on
 * whether the device queue is below or above target. Linear resampling. */
#include <string.h>
#include "plat.h"
#include "runner_internal.h"

#define DEVICE_RATE 48000 /* the device always opens at this rate; gambatte, gpsp etc. have their own */
#define STEREO 2
#define FRAME_BYTES 4
#define PENDING_MAX 4096
#define OUTPUT_MAX (PENDING_MAX * 2 + 8)
#define QUEUE_TARGET 2000 /* audio frames (~42 ms at 48 kHz); settles slightly below (proportional control) */
#define QUEUE_LOW (QUEUE_TARGET / 4)
#define DRC_MAX 0.008
#define FADE_IN_FRAMES (DEVICE_RATE * 6 / 10)
#define IDLE_CHUNK_FRAMES 512
#define IDLE_QUEUE_FRAMES 4000
#define DEVICE_RING_FRAMES 8192 /* 32 fragments of 1 KB */
#define UNKNOWN_FILL_PERCENT 50
#define MAX_CONSECUTIVE_SKIPS 2 /* 20 fps at worst, never frozen; with 1 a heavy game may not fit */
#define LATE_US 2000
#define PHASE_BITS 16 /* phase is 16.16 fixed point */
#define PHASE_ONE 65536.0
#define PHASE_FRACTION_MASK 0xffff
#define BLEND_BITS 15 /* difference x fraction must fit in an int */

static int device_open;
static int rate_control;
static double base_ratio = 1.0; /* device rate / core rate */
static uint32_t phase;          /* fractional position inside the batch, carried between frames */
static retro_audio_buffer_status_callback_t status_callback;

/* pending[0..1] = last audio frame of the previous batch, the start of the first interpolation */
static int16_t pending[(PENDING_MAX + 1) * STEREO];
static int pending_frames;
static int16_t output[OUTPUT_MAX * STEREO];

static struct {
    uint32_t ms;
    uint32_t frames_received;
    double square_sum; /* core audio level (RMS) */
    long square_count;
    int underruns;
    int dropped;
    int queue_sum;
    int queue_count;
    double ratio_sum;
} stats;

void runner_audio_open(double core_sample_rate)
{
    device_open = !plat_audio_init(DEVICE_RATE);
    if (device_open && plat_audio_rate() > 0) {
        base_ratio = (double)plat_audio_rate() / core_sample_rate;
    }
    plat_log("game: audio %d", device_open);
    plat_log("game: sample rate %d", (int)core_sample_rate);
}

void runner_audio_set_rate_control(bool enabled)
{
    rate_control = enabled;
}

void runner_audio_set_status_callback(retro_audio_buffer_status_callback_t callback)
{
    status_callback = callback;
}

void runner_audio_report_status(int delay_us, int consecutive_skips)
{
    if (!status_callback || !device_open) {
        return;
    }
    int queue = plat_audio_queue();
    int fill = queue < 0 ? UNKNOWN_FILL_PERCENT : queue * 100 / DEVICE_RING_FRAMES;
    int running_late = delay_us > LATE_US || (queue >= 0 && queue < QUEUE_LOW);
    status_callback(true, fill > 100 ? 100 : fill, consecutive_skips < MAX_CONSECUTIVE_SKIPS && running_late);
}

void runner_audio_sample(int16_t left, int16_t right)
{
    if (pending_frames < PENDING_MAX) {
        pending[STEREO + pending_frames * STEREO] = left;
        pending[STEREO + pending_frames * STEREO + 1] = right;
        pending_frames += 1;
    }
    stats.frames_received += 1;
}

size_t runner_audio_batch(const int16_t *samples, size_t frames)
{
    for (size_t sample_index = 0; sample_index < frames * STEREO; sample_index += 1) {
        stats.square_sum += (double)samples[sample_index] * samples[sample_index];
    }
    stats.square_count += frames * STEREO;
    size_t room = (size_t)(PENDING_MAX - pending_frames);
    size_t kept = frames < room ? frames : room;
    memcpy(pending + STEREO + pending_frames * STEREO, samples, kept * FRAME_BYTES);
    pending_frames += kept;
    stats.frames_received += frames;

    return frames;
}

/* Start or underrun: fills the queue with silence to avoid crackling. Returns the new queue level. */
static int plug_queue_with_silence(int queue, bool starting)
{
    static int16_t silence[QUEUE_TARGET * STEREO];
    if (!starting) {
        stats.underruns += 1;
    }
    int missing = QUEUE_TARGET - queue;
    plat_audio(silence, missing > 0 ? missing : 0);

    return plat_audio_queue();
}

/* Output frames per input frame for this batch. */
static double control_rate(void)
{
    static bool started;
    if (!rate_control) {
        return base_ratio;
    }
    int queue = plat_audio_queue();
    if (queue < 0) {
        return base_ratio;
    }
    if (!started || queue < QUEUE_LOW) {
        queue = plug_queue_with_silence(queue, !started);
        started = true;
    }
    double error = (double)(QUEUE_TARGET - queue) / QUEUE_TARGET;
    if (error > 1) {
        error = 1;
    }
    if (error < -1) {
        error = -1;
    }
    double ratio = base_ratio * (1.0 + DRC_MAX * error);
    stats.queue_sum += queue;
    stats.queue_count += 1;
    stats.ratio_sum += ratio;

    return ratio;
}

/* Fills output from pending, interpolating from pending[frame] to pending[frame + 1]. Returns its frames. */
static inline int resample(int input_frames, double ratio)
{
    if (ratio == 1.0) {
        memcpy(output, pending + STEREO, input_frames * FRAME_BYTES);

        return input_frames;
    }
    int output_frames = 0;
    uint32_t step = (uint32_t)(PHASE_ONE / ratio);
    uint32_t position = phase;
    while ((int)(position >> PHASE_BITS) < input_frames) {
        int frame = position >> PHASE_BITS;
        int fraction = (position & PHASE_FRACTION_MASK) >> 1;
        const int16_t *from = pending + frame * STEREO;
        output[output_frames * STEREO] = from[0] + (((from[2] - from[0]) * fraction) >> BLEND_BITS);
        output[output_frames * STEREO + 1] = from[1] + (((from[3] - from[1]) * fraction) >> BLEND_BITS);
        output_frames += 1;
        position += step;
    }
    phase = position - ((uint32_t)input_frames << PHASE_BITS);

    return output_frames;
}

/* Game audio rises from 0 to normal in 0.6 s: avoids a pop when certain games open. */
static inline void apply_fade_in(int output_frames)
{
    static int fade_position;
    for (int frame = 0; frame < output_frames && fade_position < FADE_IN_FRAMES; frame += 1) {
        output[frame * STEREO] = output[frame * STEREO] * fade_position / FADE_IN_FRAMES;
        output[frame * STEREO + 1] = output[frame * STEREO + 1] * fade_position / FADE_IN_FRAMES;
        fade_position += 1;
    }
}

void runner_audio_send(void)
{
    int input_frames = pending_frames;
    pending_frames = 0;
    if (!device_open || !input_frames) {
        return;
    }
    uint32_t started_ms = plat_ms();
    int output_frames = resample(input_frames, control_rate());
    pending[0] = pending[input_frames * STEREO];
    pending[1] = pending[input_frames * STEREO + 1];
    apply_fade_in(output_frames);
    int sent = plat_audio(output, output_frames);
    if (sent < output_frames) {
        stats.dropped += output_frames - sent;
    }
    stats.ms += plat_ms() - started_ms;
}

/* An empty device queue replays old leftovers. */
void runner_audio_idle(void)
{
    static int16_t silence[IDLE_CHUNK_FRAMES * STEREO];
    if (!device_open) {
        return;
    }
    int queue = plat_audio_queue();
    while (queue >= 0 && queue < IDLE_QUEUE_FRAMES) {
        int sent = plat_audio(silence, IDLE_CHUNK_FRAMES);
        if (sent <= 0) {
            break;
        }
        queue += sent;
    }
}

uint32_t runner_audio_ms(void)
{
    return stats.ms;
}

uint32_t runner_audio_frames_received(void)
{
    return stats.frames_received;
}

void runner_audio_reset_frame_stats(void)
{
    stats.ms = 0;
    stats.frames_received = 0;
}

void runner_audio_log_queue_stats(void)
{
    if (stats.queue_count) {
        plat_log("audio: average queue %d frames", stats.queue_sum / stats.queue_count);
        plat_log("audio: average ratio (x100000) %d", (int)(stats.ratio_sum / stats.queue_count * 100000));
    }
    plat_log("audio: underruns %d", stats.underruns);
}

void runner_audio_log_level_stats_and_reset(void)
{
    if (stats.square_count) {
        plat_log("audio: rms level %d", (int)__builtin_sqrt(stats.square_sum / stats.square_count));
    }
    plat_log("audio: dropped frames %d", stats.dropped);
    memset(&stats, 0, sizeof stats);
}
