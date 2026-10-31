/* Voice engine of src/music.c: oscillators, envelopes and voice allocation. Private to that file, and a header
 * instead of a .c because the per-sample calls must stay inlined in the audio loop. */
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

#define SINE_TABLE_SIZE 4096
#define TWO_PI 6.2831853f
#define VOICE_COUNT 24
#define PHASE_WRAP 1e4f
#define SILENCE_THRESHOLD 0.0005f
#define KICK_DROP_HZ 80
#define KICK_BASE_HZ 45
#define RIM_HZ 1700.0f

enum { EPIANO, BELL, VIBES, PAD, BASS, KICK, SHAKER, RIM, BREATH, BUBBLE, SQUARE, VOICE_TYPE_COUNT };

typedef struct {
    int type, active, age, duration;
    float hz, phase, mod_phase, mod_ratio, mod_index, mod_index_decay;
    float env, attack, decay, release, amp, pan_left, pan_right, echo;
    float filter; /* filtered noise state */
} voice_t;

/* Timbre of a voice type: times in seconds, a decay of 0 leaves the multiplier at 0. */
typedef struct {
    float mod_ratio, mod_index, mod_decay_secs;
    float attack, decay_secs, release_secs, echo;
} patch_t;

static const patch_t PATCHES[VOICE_TYPE_COUNT] = {
    [EPIANO] = {1, 1.6f, 0.25f, 0.004f, 1.4f, 0.18f, 0.35f},
    [BELL] = {3.5f, 2.2f, 0.12f, 0.002f, 0.9f, 0.9f, 0.6f},
    [VIBES] = {4, 0.9f, 0.3f, 0.003f, 1.8f, 0.35f, 0.5f},
    [PAD] = {0, 0, 0, 0.9f, 6.0f, 0.8f, 0.25f},
    [BASS] = {0, 0, 0, 0.006f, 0.9f, 0.08f, 0},
    [KICK] = {0, 0, 0, 0.001f, 0.16f, 0.16f, 0},
    [SHAKER] = {0, 0, 0, 0.002f, 0.035f, 0.035f, 0.15f},
    [RIM] = {0, 0, 0, 0.0005f, 0.03f, 0.03f, 0.3f},
    [SQUARE] = {0, 0, 0, 0.001f, 0.07f, 0.02f, 0.2f},
    [BUBBLE] = {0, 0, 0, 0.002f, 0.045f, 0.045f, 0.25f},
    [BREATH] = {0, 0, 0, 0, 0.3f, 0.12f, 0.4f},
};

static float sine[SINE_TABLE_SIZE];
static int rate = 32000;
static voice_t voices[VOICE_COUNT];
static uint32_t noise_seed = 12345;
static int in_effect; /* 1 while play_voice() is called by an effect */

static float noise(void)
{
    noise_seed = noise_seed * 1664525u + 1013904223u;

    return (int32_t)noise_seed * (1.0f / 2147483648.0f);
}

static inline float sin_lookup(float phase)
{
    return sine[(int)(phase * SINE_TABLE_SIZE) & (SINE_TABLE_SIZE - 1)];
}

/* First free voice, or the quietest one to steal. */
static int pick_voice(void)
{
    int picked = 0;
    for (int index = 0; index < VOICE_COUNT; index += 1) {
        if (!voices[index].active) {
            return index;
        }
        if (voices[index].env < voices[picked].env) {
            picked = index;
        }
    }

    return picked;
}

static void play_voice(int type, float hz, float secs, float amp, float pan)
{
    const patch_t *patch = &PATCHES[type];
    voice_t *voice = &voices[pick_voice()];
    float sample_secs = 1.0f / rate;
    memset(voice, 0, sizeof *voice);
    voice->type = type;
    voice->active = 1;
    voice->hz = hz;
    voice->amp = amp;
    voice->duration = (int)(secs * rate);
    voice->pan_left = sqrtf(0.5f * (1 - pan));
    voice->pan_right = sqrtf(0.5f * (1 + pan));
    voice->mod_ratio = patch->mod_ratio;
    voice->mod_index = patch->mod_index;
    if (patch->mod_decay_secs > 0) {
        voice->mod_index_decay = expf(-sample_secs / patch->mod_decay_secs);
    }
    voice->attack = patch->attack;
    voice->decay = expf(-sample_secs / patch->decay_secs);
    voice->release = expf(-sample_secs / patch->release_secs);
    voice->echo = patch->echo;
    if (type == BREATH) {
        voice->attack = secs * 0.4f;
        voice->duration = (int)(secs * 0.4f * rate);
    }
    if (in_effect) {
        voice->echo = 0; /* dry effects: a bell echo would come back 500 ms later like a second hit */
    }
}

static inline float fm_sample(voice_t *voice, float phase_step)
{
    float sample = sin_lookup(voice->phase + voice->mod_index * sin_lookup(voice->mod_phase));
    voice->mod_phase += phase_step * voice->mod_ratio;
    voice->mod_index *= voice->mod_index_decay;
    if (voice->type == EPIANO) {
        sample = sample * 0.8f + 0.2f * sin_lookup(voice->phase * 2);
    }

    return sample;
}

static inline float bass_sample(const voice_t *voice)
{
    float wave = sin_lookup(voice->phase) + 0.3f * sin_lookup(voice->phase * 2);

    return wave / (1 + fabsf(wave) * 0.5f);
}

static inline float shaker_sample(voice_t *voice)
{
    float white = noise();
    float sample = white - voice->filter;
    voice->filter = white;

    return sample;
}

static inline float square_sample(const voice_t *voice)
{
    float cycle = voice->phase - (int)voice->phase;

    return (cycle < 0.25f ? 1.0f : -1.0f) * 0.6f;
}

static inline float breath_sample(voice_t *voice)
{
    float coefficient = 0.02f + 0.2f * voice->age / (float)(voice->duration + 1);
    if (coefficient > 0.3f) {
        coefficient = 0.3f;
    }
    voice->filter += coefficient * (noise() - voice->filter);

    return voice->filter * 2.5f;
}

/* Kick and bubble glide in pitch, so their phase step depends on the voice age. */
static inline float kick_step(const voice_t *voice)
{
    float age_secs = voice->age / (float)rate;
    float hz = KICK_BASE_HZ + KICK_DROP_HZ * expf(-age_secs / 0.04f);

    return hz / rate;
}

static inline float bubble_step(const voice_t *voice)
{
    float age_secs = voice->age / (float)rate;
    float rise = age_secs / 0.06f;

    return voice->hz * (1 + 1.4f * (rise > 1 ? 1 : rise)) / rate;
}

static inline float oscillator_sample(voice_t *voice, float *phase_step)
{
    switch (voice->type) {
    case EPIANO:
    case BELL:
    case VIBES: return fm_sample(voice, *phase_step);
    case PAD:
        return sin_lookup(voice->phase) * 0.6f + sin_lookup(voice->phase * 1.003f) * 0.4f +
               0.15f * sin_lookup(voice->phase * 2);
    case BASS: return bass_sample(voice);
    case KICK: *phase_step = kick_step(voice); return sin_lookup(voice->phase);
    case SHAKER: return shaker_sample(voice);
    case RIM: *phase_step = RIM_HZ / rate; return 0.6f * sin_lookup(voice->phase) + 0.4f * noise();
    case SQUARE: return square_sample(voice);
    case BUBBLE:
        *phase_step = bubble_step(voice);

        return sin_lookup(voice->phase) * 0.9f + 0.1f * sin_lookup(voice->phase * 3);
    case BREATH: return breath_sample(voice);
    }

    return 0;
}

static inline void advance_envelope(voice_t *voice)
{
    int attack_samples = (int)(voice->attack * rate) + 1;
    if (voice->age < attack_samples) {
        voice->env = (float)voice->age / attack_samples;
    } else {
        voice->env *= voice->age < voice->duration ? voice->decay : voice->release;
    }
    voice->age += 1;
    if (voice->age > attack_samples && voice->env < SILENCE_THRESHOLD) {
        voice->active = 0;
    }
}

static float voice_sample(voice_t *voice)
{
    float phase_step = voice->hz / rate;
    float sample = oscillator_sample(voice, &phase_step);
    voice->phase += phase_step;
    if (voice->phase > PHASE_WRAP) {
        voice->phase -= PHASE_WRAP;
        voice->mod_phase -= PHASE_WRAP * voice->mod_ratio;
    }
    advance_envelope(voice);

    return sample * voice->env * voice->amp;
}
