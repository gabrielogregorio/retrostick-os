/* 8-bit style menu music (like the NES sound chip): 2 square waves, 1 stepped triangle and
 * shift-register noise. Original composition (adventure, C major, 150 bpm), 32 bars in a loop
 * + 2 of intro. Runs only on the PC (tools/gen_audio.c); the stick plays the file. */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "chip.h"

#define BPM 150
#define STEPS_PER_BAR 16 /* 16th notes per bar */
#define MELODY_BARS 16
#define SECTION_BARS 8
#define INTRO_BARS 2
#define LOOP_BARS 32
#define VARIATION_SECTION 1 /* A': the section whose second half drops one octave */

#define TWO_PI 6.2831853f
#define VIBRATO_HZ 5.5f
#define VIBRATO_DEPTH 0.004f
#define ENVELOPE_FLOOR 0.001f
#define VOLUME_STEPS 15     /* the NES volume has 16 levels */
#define TRIANGLE_STEPS 7.5f /* 16 levels, centered on zero */
#define LFSR_TOP_BIT 14
#define LFSR_TAP_LONG 1
#define LFSR_TAP_SHORT 6
#define LOWPASS_AMOUNT 0.45f
#define OUTPUT_SCALE 12000
#define SAMPLE_MAX 32767
#define SAMPLE_MIN -32768

enum { TOKEN_HOLD = -1, TOKEN_REST = -2 };

enum { DRUM_KICK, DRUM_SNARE, DRUM_HIHAT, DRUM_TYPES };

typedef struct {
    float phase, hz, env, decay, volume, duty, vibrato;
    int on;
} pulse_t;

typedef struct {
    float phase, hz;
    int on;
} triangle_t;

typedef struct {
    uint16_t lfsr;
    int period, countdown;
    float env, decay, volume;
    int short_mode;
} noise_t;

typedef struct {
    int period;
    float decay_secs, volume;
    int short_mode;
} drum_t;

typedef struct {
    int root, is_minor;
} chord_t;

/* melody: one token per 16th note: note ("E5"), "-" holds, "." rests */
static const char *MELODY[MELODY_BARS] = {
    "E5 - G5 - C6 - - - B5 - G5 - E5 - - -",   "F5 - E5 - D5 - C5 - D5 - E5 - G5 - - -",
    "A5 - - - G5 - E5 - C5 - - - E5 - A5 -",   "G5 - - - - - - - . . E5 - G5 - A5 -",
    "C6 - - - A5 - F5 - A5 - C6 - D6 - C6 -",  "B5 - - - G5 - D5 - G5 - B5 - D6 - - -",
    "E6 - D6 - C6 - G5 - E5 - G5 - C6 - E6 -", "D6 - - - - - - - B5 - - - G5 - - -",
    "A5 - - - C6 - A5 - F5 - - - A5 - C6 -",   "B5 - - - D6 - B5 - G5 - - - B5 - D6 -",
    "G5 - - - B5 - G5 - E5 - G5 - B5 - E6 -",  "C6 - - - - - B5 - A5 - - - E5 - - -",
    "F5 - A5 - D6 - - - C6 - A5 - F5 - - -",   "G5 - B5 - D6 - - - F6 - E6 - D6 - B5 -",
    "C6 - - - G5 - E5 - G5 - C6 - E6 - G6 -",  "E6 - - - - - - - . . . . . . . .",
};

static const chord_t CHORDS[MELODY_BARS] = {
    {48, 0}, {48, 0}, {45, 1}, {45, 1}, {41, 0}, {43, 0}, {48, 0}, {43, 0},
    {41, 0}, {43, 0}, {40, 1}, {45, 1}, {38, 1}, {43, 0}, {48, 0}, {48, 0},
};

static const drum_t DRUMS[DRUM_TYPES] = {
    [DRUM_KICK] = {40, 0.05f, 0.55f, 0},
    [DRUM_SNARE] = {8, 0.07f, 0.45f, 0},
    [DRUM_HIHAT] = {2, 0.015f, 0.22f, 1},
};

static int rate;
static double samples_per_step;
static pulse_t lead, arpeggio;
static triangle_t bass;
static noise_t drums;
static long sample_position;

static float midi_hz(int note)
{
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

static int parse_note(const char *text)
{
    static const int SEMITONES[7] = {9, 11, 0, 2, 4, 5, 7}; /* A B C D E F G */
    int note = SEMITONES[text[0] - 'A'];
    int position = 1;
    if (text[position] == '#') {
        note += 1;
        position += 1;
    }

    return note + (text[position] - '0' + 1) * 12;
}

/* Token number `index` (0..15) of a bar: the MIDI note, TOKEN_HOLD or TOKEN_REST. */
static int read_token(const char *line, int index)
{
    for (int skipped = 0; skipped < index; skipped += 1) {
        while (*line && *line != ' ') {
            line += 1;
        }
        while (*line == ' ') {
            line += 1;
        }
    }
    if (*line == '-') {
        return TOKEN_HOLD;
    }
    if (*line == '.') {
        return TOKEN_REST;
    }

    return parse_note(line);
}

static float pulse_sample(pulse_t *pulse)
{
    if (!pulse->on && pulse->env < ENVELOPE_FLOOR) {
        return 0;
    }
    float secs = sample_position / (float)rate;
    float hz = pulse->hz * (1 + pulse->vibrato * sinf(secs * TWO_PI * VIBRATO_HZ));
    pulse->phase += hz / rate;
    pulse->phase -= (int)pulse->phase;
    pulse->env *= pulse->decay;
    float stepped_env = (int)(pulse->env * VOLUME_STEPS) / (float)VOLUME_STEPS;

    return (pulse->phase < pulse->duty ? 1.0f : -1.0f) * stepped_env * pulse->volume;
}

static float triangle_sample(triangle_t *triangle)
{
    if (!triangle->on) {
        return 0;
    }
    triangle->phase += triangle->hz / rate;
    triangle->phase -= (int)triangle->phase;
    float wave = triangle->phase < 0.5f ? triangle->phase * 4 - 1 : 3 - triangle->phase * 4;

    return (int)(wave * TRIANGLE_STEPS) / TRIANGLE_STEPS * 0.9f;
}

static float noise_sample(noise_t *channel)
{
    if (channel->env < ENVELOPE_FLOOR) {
        return 0;
    }
    channel->countdown -= 1;
    if (channel->countdown <= 0) {
        channel->countdown = channel->period;
        int tap = channel->short_mode ? LFSR_TAP_SHORT : LFSR_TAP_LONG;
        int bit = (channel->lfsr ^ (channel->lfsr >> tap)) & 1;
        channel->lfsr = (channel->lfsr >> 1) | (bit << LFSR_TOP_BIT);
    }
    channel->env *= channel->decay;

    return ((channel->lfsr & 1) ? 1.0f : -1.0f) * channel->env * channel->volume;
}

static void hit_drum(int type)
{
    const drum_t *drum = &DRUMS[type];
    float sample_secs = 1.0f / rate;
    drums.env = 1;
    drums.period = drum->period;
    drums.decay = expf(-sample_secs / drum->decay_secs);
    drums.volume = drum->volume;
    drums.short_mode = drum->short_mode;
}

/* Bar of the song (0 = start of the loop) to melody bar: A A' B A, with A = bars 0-7 and B = 8-15. */
static int melody_bar(int bar)
{
    static const int SECTION_START[4] = {0, 0, 8, 0};

    return SECTION_START[(bar / SECTION_BARS) % 4] + bar % SECTION_BARS;
}

/* Root and octave in 8th notes, with a fifth on the last beat. */
static int bass_note(int root, int step)
{
    int octave = (step / 2) % 2 ? 12 : 0;
    int fifth = step >= 12 && step % 4 == 2 ? 7 : 0;

    return root - 12 + octave + fifth;
}

static void play_drums(int step)
{
    if (step == 0 || step == 10) {
        hit_drum(DRUM_KICK);
    } else if (step == 4 || step == 12) {
        hit_drum(DRUM_SNARE);
    } else if (step % 2 == 0) {
        hit_drum(DRUM_HIHAT);
    }
}

/* Thin square, very quiet. */
static void play_arpeggio(const chord_t *chord, int step)
{
    int intervals[4] = {0, chord->is_minor ? 3 : 4, 7, 12};
    arpeggio.hz = midi_hz(chord->root + 12 + intervals[step % 4]);
    arpeggio.on = 1;
    arpeggio.env = 1;
    arpeggio.decay = expf(-(1.0f / rate) / 0.06f);
    arpeggio.volume = 0.10f;
    arpeggio.duty = 0.125f;
    arpeggio.vibrato = 0;
}

static void play_lead(int bar, int token)
{
    float sample_secs = 1.0f / rate;
    if (token >= 0) {
        int is_variation = (bar / SECTION_BARS) % 4 == VARIATION_SECTION && bar % SECTION_BARS >= SECTION_BARS / 2;
        lead.hz = midi_hz(token + (is_variation ? -12 : 0));
        lead.on = 1;
        lead.env = 1;
        lead.decay = expf(-sample_secs / 0.45f);
        lead.volume = 0.22f;
        lead.duty = 0.25f;
        lead.vibrato = 0;
    } else if (token == TOKEN_HOLD && lead.on) {
        lead.vibrato = VIBRATO_DEPTH;
    } else if (token == TOKEN_REST) {
        lead.on = 0;
        lead.decay = expf(-sample_secs / 0.02f);
    }
}

static void sequence_step(int bar, int step, int intro)
{
    int section_bar = intro ? 0 : melody_bar(bar);
    const chord_t *chord = &CHORDS[section_bar];
    bass.on = 1;
    bass.hz = midi_hz(bass_note(chord->root, step));
    play_drums(step);
    if (intro) {
        return;
    }
    play_arpeggio(chord, step);
    play_lead(bar, read_token(MELODY[section_bar], step));
}

static int16_t clamp_sample(int sample)
{
    if (sample > SAMPLE_MAX) {
        return SAMPLE_MAX;
    }
    if (sample < SAMPLE_MIN) {
        return SAMPLE_MIN;
    }

    return (int16_t)sample;
}

static void render(int16_t *stereo, long frames, int intro)
{
    /* light low-pass filter (the NES had one on its output); its state survives chip_init() */
    static float lowpass;
    double step_countdown = 0;
    int bar = 0;
    int step = 0;
    for (long frame = 0; frame < frames; frame += 1) {
        if (step_countdown <= 0) {
            sequence_step(bar, step, intro);
            step += 1;
            if (step == STEPS_PER_BAR) {
                step = 0;
                bar += 1;
            }
            step_countdown += samples_per_step;
        }
        step_countdown -= 1;
        float mixed = pulse_sample(&lead) + pulse_sample(&arpeggio) + triangle_sample(&bass) * 0.30f +
                      noise_sample(&drums) * 0.5f;
        lowpass += (mixed - lowpass) * LOWPASS_AMOUNT;
        int16_t sample = clamp_sample((int)(lowpass * OUTPUT_SCALE));
        stereo[frame * 2] = sample;
        stereo[frame * 2 + 1] = sample;
        sample_position += 1;
    }
}

void chip_init(int sample_rate)
{
    rate = sample_rate;
    samples_per_step = rate * 60.0 / BPM / 4;
    memset(&lead, 0, sizeof lead);
    memset(&arpeggio, 0, sizeof arpeggio);
    memset(&bass, 0, sizeof bass);
    memset(&drums, 0, sizeof drums);
    drums.lfsr = 1;
    sample_position = 0;
}

long chip_intro_frames(void)
{
    return (long)(INTRO_BARS * STEPS_PER_BAR * samples_per_step + 0.5);
}

long chip_loop_frames(void)
{
    return (long)(LOOP_BARS * STEPS_PER_BAR * samples_per_step + 0.5);
}

void chip_intro(int16_t *stereo)
{
    render(stereo, chip_intro_frames(), 1);
}

void chip_loop(int16_t *stereo)
{
    render(stereo, chip_loop_frames(), 0);
}
