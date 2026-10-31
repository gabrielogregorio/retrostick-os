/* Menu music and sound effects, synthesized right here (no audio files, no copyright issues).
 * "Aero" style: calm lounge/bossa, 88 bpm, FM electric piano, glass bells, pad, bass,
 * light percussion and stereo echo. Original composition, 16 bars in a loop.
 * Cost: a sine table plus phase accumulators; fits easily on the ARM11 at 32 kHz. */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "music.h"
#include "music_voice.h"

#define PENDING_COUNT 4
#define ECHO_MAX 24000 /* samples, enough for the delay at 48 kHz */
#define ECHO_STEPS 6   /* 16th notes: 3/8 of a bar */
#define ECHO_FEEDBACK 0.42f
#define ECHO_RETURN 0.5f

#define BPM 88
#define STEPS_PER_BAR 16
#define BAR_COUNT 16
#define LOOP_STEPS (BAR_COUNT * STEPS_PER_BAR)
#define SECS_PER_STEP (60.0f / BPM / 4)
#define CHORD_NOTES 4
#define MAX_NOTES_PER_BAR 6
#define BAND_ENTRY_BAR 2   /* piano, bass and percussion join here on the first loop */
#define MELODY_ENTRY_BAR 4 /* the melody joins here on the first loop */

#define MASTER_RAMP 0.00005f
#define MASTER_GAIN 1.8f
#define OUTPUT_SCALE 30000

#define HZ_C5 523.3f
#define HZ_E5 659.3f
#define HZ_G5 784.0f
#define HZ_A5 880.0f
#define HZ_C6 1046.5f
#define HZ_E6 1318.5f
#define HZ_G6 1568.0f
#define HZ_B6 1975.5f
#define HZ_C7 2093.0f
#define HZ_BUBBLE 620

#define LEVEL_SQUARE_SOFT 0.24f
#define LEVEL_SQUARE_MID 0.26f
#define LEVEL_SQUARE_LOUD 0.28f
#define LEVEL_BUBBLE 0.16f

typedef struct {
    int bass;
    int chord[CHORD_NOTES];
} bar_t;

typedef struct {
    int8_t step, midi, length;
} melody_note_t;

typedef struct {
    int type, samples_left;
    float hz, amp;
} pending_note_t;

typedef struct {
    float left, right;
} stereo_t;

/* D major, bossa feel: Gmaj9 F#m7 Em9 A9sus / Gmaj9 F#m7 Bm9 A9sus / Em9 A9sus Dmaj9 Bm9 / Gmaj9 F#m7 Em9 A7 */
static const bar_t HARMONY[BAR_COUNT] = {
    {43, {59, 62, 66, 69}}, {42, {57, 61, 64, 69}}, {40, {55, 59, 62, 66}}, {45, {55, 59, 62, 64}},
    {43, {59, 62, 66, 69}}, {42, {57, 61, 64, 69}}, {47, {57, 61, 62, 66}}, {45, {55, 59, 62, 64}},
    {40, {55, 59, 62, 66}}, {45, {55, 59, 62, 64}}, {38, {54, 57, 61, 64}}, {47, {57, 61, 62, 66}},
    {43, {59, 62, 66, 69}}, {42, {57, 61, 64, 69}}, {40, {55, 59, 62, 66}}, {45, {55, 59, 61, 67}},
};

/* melody (vibraphone): {step in 16th notes, MIDI note, length in 16th notes}, -1 closes the bar */
static const melody_note_t MELODY[BAR_COUNT][MAX_NOTES_PER_BAR] = {
    {{0, 78, 4}, {4, 81, 2}, {6, 83, 6}, {12, 81, 4}, {-1}},
    {{0, 81, 6}, {6, 76, 4}, {10, 73, 6}, {-1}},
    {{0, 74, 4}, {4, 78, 4}, {8, 83, 8}, {-1}},
    {{0, 81, 6}, {8, 79, 4}, {12, 76, 4}, {-1}},
    {{0, 78, 2}, {2, 81, 2}, {4, 86, 6}, {12, 83, 4}, {-1}},
    {{0, 85, 6}, {6, 81, 4}, {10, 76, 6}, {-1}},
    {{0, 86, 4}, {4, 85, 4}, {8, 78, 8}, {-1}},
    {{0, 76, 12}, {-1}},
    {{0, 79, 3}, {3, 78, 3}, {6, 76, 4}, {10, 83, 6}, {-1}},
    {{0, 81, 8}, {8, 86, 4}, {12, 83, 4}, {-1}},
    {{0, 85, 6}, {6, 81, 4}, {10, 88, 6}, {-1}},
    {{0, 86, 4}, {4, 85, 2}, {6, 83, 4}, {10, 78, 6}, {-1}},
    {{0, 83, 4}, {4, 86, 4}, {8, 90, 8}, {-1}},
    {{0, 88, 6}, {6, 85, 4}, {10, 81, 6}, {-1}},
    {{0, 83, 4}, {4, 79, 4}, {8, 78, 4}, {12, 76, 4}},
    {{0, 85, 8}, {10, 81, 2}, {12, 79, 4}, {-1}},
};

static float echo_left[ECHO_MAX], echo_right[ECHO_MAX];
static int echo_position, echo_length;

static double samples_per_step, step_countdown;
static int sequencer_stopped;
static pending_note_t pending[PENDING_COUNT]; /* scheduled notes of the 8-bit effects */
static int current_step, loop_count;
static float master, master_target = 1;

static float midi_hz(int note)
{
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

static int band_is_playing(int bar)
{
    return !(loop_count == 0 && bar < BAND_ENTRY_BAR);
}

static void play_pad(const bar_t *harmony, int step)
{
    if (step != 0) {
        return;
    }
    for (int note = 0; note < CHORD_NOTES; note += 1) {
        play_voice(PAD, midi_hz(harmony->chord[note]), SECS_PER_STEP * 15, 0.045f, note & 1 ? 0.5f : -0.5f);
    }
}

/* Electric piano in a bossa rhythm. */
static void play_epiano(const bar_t *harmony, int bar, int step)
{
    static const int8_t HIT_STEPS[] = {0, 6, 10};
    static const int8_t HIT_LENGTHS[] = {5, 3, 4};
    for (int hit = 0; hit < 3; hit += 1) {
        if (step != HIT_STEPS[hit] || !band_is_playing(bar)) {
            continue;
        }
        for (int note = 0; note < CHORD_NOTES; note += 1) {
            play_voice(EPIANO, midi_hz(harmony->chord[note]), SECS_PER_STEP * HIT_LENGTHS[hit], (hit ? 0.07f : 0.09f),
                       -0.3f + note * 0.2f);
        }
    }
}

static void play_bass(const bar_t *harmony, int bar, int step)
{
    if (!band_is_playing(bar)) {
        return;
    }
    if (step == 0) {
        play_voice(BASS, midi_hz(harmony->bass), SECS_PER_STEP * 5, 0.32f, 0);
    }
    if (step == 7) {
        play_voice(BASS, midi_hz(harmony->bass + 7), SECS_PER_STEP * 2, 0.22f, 0);
    }
    if (step == 10) {
        play_voice(BASS, midi_hz(harmony->bass + 12), SECS_PER_STEP * 2, 0.18f, 0);
    }
    if (step == 14) {
        play_voice(BASS, midi_hz(harmony->bass + 7), SECS_PER_STEP * 2, 0.2f, 0);
    }
}

static void play_percussion(int bar, int step)
{
    if (!band_is_playing(bar)) {
        return;
    }
    if (step == 0 || step == 10) {
        play_voice(KICK, 0, 0.3f, 0.5f, 0);
    }
    if (step == 7) {
        play_voice(KICK, 0, 0.3f, 0.25f, 0);
    }
    if (step % 2 == 0) {
        play_voice(SHAKER, 0, 0.05f, step % 4 == 2 ? 0.07f : 0.04f, 0.4f);
    }
    if (step == 4 || step == 12) {
        play_voice(RIM, 0, 0.05f, 0.09f, -0.2f);
    }
}

static void play_melody(int bar, int step)
{
    if (loop_count == 0 && bar < MELODY_ENTRY_BAR) {
        return;
    }
    for (int index = 0; index < MAX_NOTES_PER_BAR; index += 1) {
        const melody_note_t *note = &MELODY[bar][index];
        if (note->step < 0 || !note->midi) {
            return;
        }
        if (note->step == step) {
            play_voice(VIBES, midi_hz(note->midi), SECS_PER_STEP * note->length, 0.13f, 0.15f);
        }
    }
}

/* Little glass bells: sparse arpeggio, one octave up. */
static void play_bells(const bar_t *harmony, int bar, int step)
{
    if ((bar % 2 == 1) && (step == 3 || step == 9 || step == 13)) {
        play_voice(BELL, midi_hz(harmony->chord[(step / 4) & 3] + 12), SECS_PER_STEP * 2, 0.035f,
                   step == 9 ? -0.6f : 0.6f);
    }
}

static void music_step(int position)
{
    int bar = (position / STEPS_PER_BAR) % BAR_COUNT;
    int step = position % STEPS_PER_BAR;
    const bar_t *harmony = &HARMONY[bar];
    play_pad(harmony, step);
    play_epiano(harmony, bar, step);
    play_bass(harmony, bar, step);
    play_percussion(bar, step);
    play_melody(bar, step);
    play_bells(harmony, bar, step);
}

void music_init(int sample_rate)
{
    rate = sample_rate;
    for (int index = 0; index < SINE_TABLE_SIZE; index += 1) {
        sine[index] = sinf(index * TWO_PI / SINE_TABLE_SIZE);
    }
    samples_per_step = rate * 60.0 / BPM / 4;
    echo_length = (int)(samples_per_step * ECHO_STEPS);
    if (echo_length > ECHO_MAX) {
        echo_length = ECHO_MAX;
    }
    step_countdown = 0;
    current_step = 0;
    loop_count = 0;
    master = 0;
    /* the generator records one effect after another: the echo of one must not leak into the next */
    memset(voices, 0, sizeof voices);
    memset(echo_left, 0, sizeof echo_left);
    memset(echo_right, 0, sizeof echo_right);
    echo_position = 0;
    memset(pending, 0, sizeof pending);
}

void music_volume(float volume)
{
    master_target = volume;
}

void music_effects_only(int only)
{
    sequencer_stopped = only;
    if (only) {
        master_target = 1;
        master = 1;
    }
}

/* Note that starts `delay` seconds later (multi-note effects). */
static void schedule_voice(int type, float hz, float delay, float amp)
{
    for (int index = 0; index < PENDING_COUNT; index += 1) {
        if (pending[index].samples_left <= 0) {
            pending[index].type = type;
            pending[index].hz = hz;
            pending[index].amp = amp;
            pending[index].samples_left = (int)(delay * rate);

            return;
        }
    }
}

static void schedule_square(float hz, float delay, float amp)
{
    schedule_voice(SQUARE, hz, delay, amp);
}

/* The effect starts sounding in the next batch; the delay is that of the audio queue. */
void music_effect(int which)
{
    in_effect = 1;
    switch (which) {
    case SFX_MOVE: /* vertical: short bubble "plop" rising in pitch */
        play_voice(BUBBLE, HZ_BUBBLE, 0.09f, LEVEL_BUBBLE, 0.15f);
        break;
    case SFX_SYSTEM: /* horizontal: 8-bit "blip-blip", two fast rising notes */
        play_voice(SQUARE, HZ_E6, 0.045f, LEVEL_SQUARE_LOUD, -0.2f);
        schedule_square(HZ_B6, 0.05f, LEVEL_SQUARE_LOUD);
        break;
    case SFX_FAVORITE: /* rising 8-bit item-pickup arpeggio, the last note longer */
        play_voice(SQUARE, HZ_C6, 0.05f, LEVEL_SQUARE_SOFT, 0);
        schedule_square(HZ_E6, 0.06f, LEVEL_SQUARE_SOFT);
        schedule_square(HZ_G6, 0.12f, LEVEL_SQUARE_SOFT);
        schedule_square(HZ_C7, 0.18f, LEVEL_SQUARE_MID);
        break;
    case SFX_OPEN: /* click: short 8-bit "ok", no bell and no high tail */
        play_voice(SQUARE, HZ_G5, 0.04f, LEVEL_SQUARE_MID, 0);
        schedule_square(HZ_C6, 0.045f, LEVEL_SQUARE_MID);
        break;
    case SFX_BACK: /* two low falling notes */
        play_voice(SQUARE, HZ_A5, 0.04f, LEVEL_SQUARE_SOFT, 0);
        schedule_square(HZ_E5, 0.05f, LEVEL_SQUARE_SOFT);
        break;
    case SFX_ENTER: /* open the game: two soft low rising bubbles, no square wave */
        play_voice(BUBBLE, HZ_C5, 0.12f, LEVEL_BUBBLE, 0);
        schedule_voice(BUBBLE, HZ_G5, 0.09f, LEVEL_BUBBLE);
        break;
    case SFX_UNFAVORITE: /* the favorite arpeggio reversed and one octave down */
        play_voice(SQUARE, HZ_G5, 0.05f, LEVEL_SQUARE_SOFT, 0);
        schedule_square(HZ_E5, 0.06f, LEVEL_SQUARE_SOFT);
        schedule_square(HZ_C5, 0.12f, LEVEL_SQUARE_SOFT);
        break;
    }
    in_effect = 0;
}

static inline void fire_pending_notes(void)
{
    for (int index = 0; index < PENDING_COUNT; index += 1) {
        pending_note_t *note = &pending[index];
        if (note->samples_left <= 0) {
            continue;
        }
        note->samples_left -= 1;
        if (note->samples_left == 0) {
            in_effect = 1;
            play_voice(note->type, note->hz, note->type == BUBBLE ? 0.12f : 0.06f, note->amp, 0.2f);
            in_effect = 0;
        }
    }
}

static inline void advance_sequencer(void)
{
    if (step_countdown <= 0 && !sequencer_stopped) {
        music_step(current_step);
        current_step += 1;
        if (current_step % LOOP_STEPS == 0) {
            loop_count += 1;
        }
        step_countdown += samples_per_step;
    }
    step_countdown -= 1;
}

static inline void mix_voices(stereo_t *dry, stereo_t *echo_send)
{
    for (int index = 0; index < VOICE_COUNT; index += 1) {
        voice_t *voice = &voices[index];
        if (!voice->active) {
            continue;
        }
        float sample = voice_sample(voice);
        dry->left += sample * voice->pan_left;
        dry->right += sample * voice->pan_right;
        echo_send->left += sample * voice->pan_left * voice->echo;
        echo_send->right += sample * voice->pan_right * voice->echo;
    }
}

/* Each side feeds the other, so repeats bounce between left and right. */
static inline void ping_pong_echo(stereo_t *dry, const stereo_t *echo_send)
{
    float delayed_left = echo_left[echo_position];
    float delayed_right = echo_right[echo_position];
    echo_left[echo_position] = echo_send->left + delayed_right * ECHO_FEEDBACK;
    echo_right[echo_position] = echo_send->right + delayed_left * ECHO_FEEDBACK;
    echo_position += 1;
    if (echo_position >= echo_length) {
        echo_position = 0;
    }
    dry->left += delayed_left * ECHO_RETURN;
    dry->right += delayed_right * ECHO_RETURN;
}

static inline float soft_limit(float sample)
{
    return sample / (1 + fabsf(sample));
}

void music_render(int16_t *stereo, int frames)
{
    for (int frame = 0; frame < frames; frame += 1) {
        stereo_t dry = {0, 0};
        stereo_t echo_send = {0, 0};
        advance_sequencer();
        fire_pending_notes();
        mix_voices(&dry, &echo_send);
        ping_pong_echo(&dry, &echo_send);
        master += (master_target - master) * MASTER_RAMP;
        dry.left *= master * MASTER_GAIN;
        dry.right *= master * MASTER_GAIN;
        stereo[frame * 2] = (int16_t)(soft_limit(dry.left) * OUTPUT_SCALE);
        stereo[frame * 2 + 1] = (int16_t)(soft_limit(dry.right) * OUTPUT_SCALE);
    }
}
