/* Menu music and sound effects, synthesized right here (no audio files, no copyright issues).
 * "Aero" style: calm lounge/bossa, 88 bpm, FM electric piano, glass bells, pad, bass,
 * light percussion and stereo echo. Original composition, 16 bars in a loop.
 * Cost: a sine table plus phase accumulators; fits easily on the ARM11 at 32 kHz. */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "music.h"

#define TAB 4096
static float sine[TAB];
static int rate = 32000;

enum { EPIANO, BELL, VIBES, PAD, BASS, KICK, SHAKER, RIM, BREATH, BUBBLE, SQUARE };

typedef struct {
    int type, active, age, dur;
    float f, phase, mod_phase, ratio, idx, idx_dec;
    float env, attack, dec, release, amp, pl, pr, echo;
    float filter, noise_a; /* filtered noise state */
} voice_t;

#define NVOICES 24
static voice_t voices[NVOICES];
static uint32_t seed = 12345;
static float noise(void) { seed = seed * 1664525u + 1013904223u; return (int32_t)seed * (1.0f / 2147483648.0f); }

static inline float sin_lookup(float phase) { return sine[(int)(phase * TAB) & (TAB - 1)]; }
static float midi_hz(int n) { return 440.0f * powf(2.0f, (n - 69) / 12.0f); }

/* stereo ping-pong echo, 3/8 of a bar */
#define ECHO_MAX 24000
static float echo_l[ECHO_MAX], echo_r[ECHO_MAX];
static int echo_pos, echo_len;

static int in_effect; /* 1 while play_voice() is called by an effect */

static void play_voice(int type, float f, float secs, float amp, float pan)
{
    int k = 0;
    for (int i = 0; i < NVOICES; i++) {
        if (!voices[i].active) { k = i; break; }
        if (voices[i].env < voices[k].env) k = i; /* steal the quietest one */
    }
    voice_t *v = &voices[k];
    memset(v, 0, sizeof *v);
    v->type = type; v->active = 1; v->f = f; v->amp = amp;
    v->dur = (int)(secs * rate);
    v->pl = sqrtf(0.5f * (1 - pan)); v->pr = sqrtf(0.5f * (1 + pan));
    float s = 1.0f / rate;
    switch (type) {
    case EPIANO: v->ratio = 1; v->idx = 1.6f; v->idx_dec = expf(-s / 0.25f); v->attack = 0.004f; v->dec = expf(-s / 1.4f); v->release = expf(-s / 0.18f); v->echo = 0.35f; break;
    case BELL:   v->ratio = 3.5f; v->idx = 2.2f; v->idx_dec = expf(-s / 0.12f); v->attack = 0.002f; v->dec = expf(-s / 0.9f); v->release = v->dec; v->echo = 0.6f; break;
    case VIBES:  v->ratio = 4; v->idx = 0.9f; v->idx_dec = expf(-s / 0.3f); v->attack = 0.003f; v->dec = expf(-s / 1.8f); v->release = expf(-s / 0.35f); v->echo = 0.5f; break;
    case PAD:    v->attack = 0.9f; v->dec = expf(-s / 6.0f); v->release = expf(-s / 0.8f); v->echo = 0.25f; break;
    case BASS:  v->attack = 0.006f; v->dec = expf(-s / 0.9f); v->release = expf(-s / 0.08f); v->echo = 0; break;
    case KICK:  v->attack = 0.001f; v->dec = expf(-s / 0.16f); v->release = v->dec; v->echo = 0; break;
    case SHAKER: v->attack = 0.002f; v->dec = expf(-s / 0.035f); v->release = v->dec; v->echo = 0.15f; break;
    case RIM:    v->attack = 0.0005f; v->dec = expf(-s / 0.03f); v->release = v->dec; v->echo = 0.3f; break;
    case SQUARE: v->attack = 0.001f; v->dec = expf(-s / 0.07f); v->release = expf(-s / 0.02f); v->echo = 0.2f; break;
    case BUBBLE:  v->attack = 0.002f; v->dec = expf(-s / 0.045f); v->release = v->dec; v->echo = 0.25f; break;
    case BREATH:  v->attack = secs * 0.4f; v->dec = expf(-s / 0.3f); v->release = expf(-s / 0.12f); v->echo = 0.4f; v->dur = (int)(secs * 0.4f * rate); break;
    }
    if (in_effect) v->echo = 0; /* dry effects: a bell echo would come back 500 ms later like a second hit */
}

/* ---------- the music ---------- */
#define BPM 88
typedef struct { int bass; int chord[4]; } bar_t;
/* D major, bossa feel: Gmaj9 F#m7 Em9 A9sus / Gmaj9 F#m7 Bm9 A9sus / Em9 A9sus Dmaj9 Bm9 / Gmaj9 F#m7 Em9 A7 */
static const bar_t HARM[16] = {
    {43, {59, 62, 66, 69}}, {42, {57, 61, 64, 69}}, {40, {55, 59, 62, 66}}, {45, {55, 59, 62, 64}},
    {43, {59, 62, 66, 69}}, {42, {57, 61, 64, 69}}, {47, {57, 61, 62, 66}}, {45, {55, 59, 62, 64}},
    {40, {55, 59, 62, 66}}, {45, {55, 59, 62, 64}}, {38, {54, 57, 61, 64}}, {47, {57, 61, 62, 66}},
    {43, {59, 62, 66, 69}}, {42, {57, 61, 64, 69}}, {40, {55, 59, 62, 66}}, {45, {55, 59, 61, 67}},
};
/* melody (vibraphone): {step in 16th notes, MIDI note, length in 16th notes}, -1 closes the bar */
static const int8_t MEL[16][6][3] = {
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

static double samples_per_step, counter;
static int no_music;
#define NPEND 4
static float pending_f[NPEND], pending_a[NPEND]; static int pending_in[NPEND]; /* scheduled notes of the 8-bit effects */
static int current_step, loop_no;
static float master, master_target = 1;

static void music_step(int p)
{
    int c = (p / 16) % 16, s = p % 16;
    const bar_t *h = &HARM[c];
    float st = 60.0f / BPM / 4; /* seconds per 16th note */
    /* pad: the whole bar */
    if (s == 0)
        for (int i = 0; i < 4; i++) play_voice(PAD, midi_hz(h->chord[i]), st * 15, 0.045f, i & 1 ? 0.5f : -0.5f);
    /* electric piano in a bossa rhythm */
    static const int8_t HIT[] = {0, 6, 10};
    static const int8_t HDUR[] = {5, 3, 4};
    for (int k = 0; k < 3; k++)
        if (s == HIT[k] && !(loop_no == 0 && c < 2))
            for (int i = 0; i < 4; i++)
                play_voice(EPIANO, midi_hz(h->chord[i]), st * HDUR[k], (k ? 0.07f : 0.09f), -0.3f + i * 0.2f);
    /* bass */
    if (!(loop_no == 0 && c < 2)) {
        if (s == 0) play_voice(BASS, midi_hz(h->bass), st * 5, 0.32f, 0);
        if (s == 7) play_voice(BASS, midi_hz(h->bass + 7), st * 2, 0.22f, 0);
        if (s == 10) play_voice(BASS, midi_hz(h->bass + 12), st * 2, 0.18f, 0);
        if (s == 14) play_voice(BASS, midi_hz(h->bass + 7), st * 2, 0.2f, 0);
    }
    /* percussion: comes in at bar 3 of the first loop */
    if (loop_no > 0 || c >= 2) {
        if (s == 0 || s == 10) play_voice(KICK, 0, 0.3f, 0.5f, 0);
        if (s == 7) play_voice(KICK, 0, 0.3f, 0.25f, 0);
        if (s % 2 == 0) play_voice(SHAKER, 0, 0.05f, s % 4 == 2 ? 0.07f : 0.04f, 0.4f);
        if (s == 4 || s == 12) play_voice(RIM, 0, 0.05f, 0.09f, -0.2f);
    }
    /* melody: from bar 5 of the first loop */
    if (loop_no > 0 || c >= 4)
        for (int i = 0; i < 6 && MEL[c][i][0] >= 0 && MEL[c][i][1]; i++)
            if (MEL[c][i][0] == s) play_voice(VIBES, midi_hz(MEL[c][i][1]), st * MEL[c][i][2], 0.13f, 0.15f);
    /* little glass bells: sparse arpeggio, one octave up */
    if ((c % 2 == 1) && (s == 3 || s == 9 || s == 13))
        play_voice(BELL, midi_hz(h->chord[(s / 4) & 3] + 12), st * 2, 0.035f, s == 9 ? -0.6f : 0.6f);
}

void music_init(int t)
{
    rate = t;
    for (int i = 0; i < TAB; i++) sine[i] = sinf(i * 6.2831853f / TAB);
    samples_per_step = rate * 60.0 / BPM / 4;
    echo_len = (int)(samples_per_step * 6);
    if (echo_len > ECHO_MAX) echo_len = ECHO_MAX;
    counter = 0; current_step = 0; loop_no = 0; master = 0;
    /* clear voices and echo: the generator records one effect after another and the echo of one must not leak into the next */
    memset(voices, 0, sizeof voices); memset(echo_l, 0, sizeof echo_l); memset(echo_r, 0, sizeof echo_r);
    echo_pos = 0; memset(pending_in, 0, sizeof pending_in);
}

void music_volume(float v) { master_target = v; }
void music_effects_only(int on) { no_music = on; if (on) master = master_target = 1; }

/* square note that starts `delay` seconds later (multi-note effects) */
static void schedule(float f, float delay, float amp)
{
    for (int i = 0; i < NPEND; i++)
        if (pending_in[i] <= 0) { pending_f[i] = f; pending_a[i] = amp; pending_in[i] = (int)(delay * rate); return; }
}

void music_effect(int which)
{
    in_effect = 1;
    switch (which) {
    case SFX_MOVE: /* vertical: short bubble "plop" rising in pitch */
        play_voice(BUBBLE, 620, 0.09f, 0.16f, 0.15f);
        break;
    case SFX_SYSTEM: { /* horizontal: 8-bit "blip-blip", two fast rising notes (E6 -> B6) */
        play_voice(SQUARE, 1318.5f, 0.045f, 0.28f, -0.2f);
        schedule(1975.5f, 0.05f, 0.28f); /* the second note starts 50 ms later */
        break;
    }
    case SFX_FAVORITE: { /* favorite: rising 8-bit item-pickup arpeggio (C6 E6 G6 C7), the last note longer */
        play_voice(SQUARE, 1046.5f, 0.05f, 0.24f, 0);
        schedule(1318.5f, 0.06f, 0.24f); schedule(1568.0f, 0.12f, 0.24f); schedule(2093.0f, 0.18f, 0.26f);
        break;
    }
    case SFX_OPEN: /* click: short 8-bit "ok" (G5 -> C6), no bell and no high tail */
        play_voice(SQUARE, 784.0f, 0.04f, 0.26f, 0);
        schedule(1046.5f, 0.045f, 0.26f);
        break;
    case SFX_BACK: /* back: two low falling notes (A5 -> E5) */
        play_voice(SQUARE, 880.0f, 0.04f, 0.24f, 0);
        schedule(659.3f, 0.05f, 0.24f);
        break;
    case SFX_UNFAVORITE: /* unfavorite: the favorite arpeggio reversed and one octave down (G5 E5 C5) */
        play_voice(SQUARE, 784.0f, 0.05f, 0.24f, 0);
        schedule(659.3f, 0.06f, 0.24f); schedule(523.3f, 0.12f, 0.24f);
        break;
    }
    in_effect = 0;
    /* the effect starts sounding in the next batch; the delay is that of the audio queue */
}

static float voice_sample(voice_t *v)
{
    float s = 0, inc = v->f / rate;
    switch (v->type) {
    case EPIANO: case BELL: case VIBES:
        s = sin_lookup(v->phase + v->idx * sin_lookup(v->mod_phase));
        v->mod_phase += inc * v->ratio;
        v->idx *= v->idx_dec;
        if (v->type == EPIANO) s = s * 0.8f + 0.2f * sin_lookup(v->phase * 2);
        break;
    case PAD:
        s = sin_lookup(v->phase) * 0.6f + sin_lookup(v->phase * 1.003f) * 0.4f + 0.15f * sin_lookup(v->phase * 2);
        break;
    case BASS: {
        float x = sin_lookup(v->phase) + 0.3f * sin_lookup(v->phase * 2);
        s = x / (1 + fabsf(x) * 0.5f);
        break;
    }
    case KICK: {
        float t = v->age / (float)rate;
        float f = 45 + 80 * expf(-t / 0.04f);
        s = sin_lookup(v->phase);
        inc = f / rate;
        break;
    }
    case SHAKER: { float r = noise(); s = r - v->filter; v->filter = r; break; }
    case RIM: s = 0.6f * sin_lookup(v->phase) + 0.4f * noise(); inc = 1700.0f / rate; break;
    case SQUARE: {
        float f = v->phase - (int)v->phase;
        s = (f < 0.25f ? 1.0f : -1.0f) * 0.6f;
        break;
    }
    case BUBBLE: {
        float t = v->age / (float)rate, k = t / 0.06f;
        s = sin_lookup(v->phase) * 0.9f + 0.1f * sin_lookup(v->phase * 3);
        inc = v->f * (1 + 1.4f * (k > 1 ? 1 : k)) / rate;
        break;
    }
    case BREATH: {
        float a = 0.02f + 0.2f * v->age / (float)(v->dur + 1);
        if (a > 0.3f) a = 0.3f;
        v->filter += a * (noise() - v->filter);
        s = v->filter * 2.5f;
        break;
    }
    }
    v->phase += inc;
    if (v->phase > 1e4f) v->phase -= 1e4f, v->mod_phase -= 1e4f * v->ratio;
    /* envelope */
    int at = (int)(v->attack * rate) + 1;
    if (v->age < at) v->env = (float)v->age / at;
    else v->env *= v->age < v->dur ? v->dec : v->release;
    v->age++;
    if (v->age > at && v->env < 0.0005f) v->active = 0;
    return s * v->env * v->amp;
}

void music_render(int16_t *out, int n)
{
    for (int k = 0; k < n; k++) {
        if (counter <= 0 && !no_music) {
            music_step(current_step++);
            if (current_step % 256 == 0) loop_no++;
            counter += samples_per_step;
        }
        counter -= 1;
        for (int i = 0; i < NPEND; i++)
            if (pending_in[i] > 0 && --pending_in[i] == 0) { in_effect = 1; play_voice(SQUARE, pending_f[i], 0.06f, pending_a[i], 0.2f); in_effect = 0; }
        float l = 0, r = 0, el = 0, er = 0;
        for (int i = 0; i < NVOICES; i++) {
            voice_t *v = &voices[i];
            if (!v->active) continue;
            float s = voice_sample(v);
            l += s * v->pl; r += s * v->pr;
            el += s * v->pl * v->echo; er += s * v->pr * v->echo;
        }
        /* ping-pong echo */
        float dl = echo_l[echo_pos], dr = echo_r[echo_pos];
        echo_l[echo_pos] = el + dr * 0.42f;
        echo_r[echo_pos] = er + dl * 0.42f;
        if (++echo_pos >= echo_len) echo_pos = 0;
        l += dl * 0.5f; r += dr * 0.5f;
        master += (master_target - master) * 0.00005f;
        l *= master * 1.8f; r *= master * 1.8f;
        /* soft limiter */
        l = l / (1 + fabsf(l)); r = r / (1 + fabsf(r));
        out[k * 2] = (int16_t)(l * 30000);
        out[k * 2 + 1] = (int16_t)(r * 30000);
    }
}
