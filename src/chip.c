/* 8-bit style menu music (like the NES sound chip): 2 square waves, 1 stepped triangle and
 * shift-register noise. Original composition (adventure, C major, 150 bpm), 32 bars in a loop
 * + 2 of intro. Runs only on the PC (tools/gen_audio.c); the stick plays the file. */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "chip.h"

#define BPM 150
#define STEPS_PER_BAR 16 /* 16th notes per bar */

/* melody: one token per 16th note: note ("E5"), "-" holds, "." rests */
static const char *MEL[16] = {
    "E5 - G5 - C6 - - - B5 - G5 - E5 - - -",
    "F5 - E5 - D5 - C5 - D5 - E5 - G5 - - -",
    "A5 - - - G5 - E5 - C5 - - - E5 - A5 -",
    "G5 - - - - - - - . . E5 - G5 - A5 -",
    "C6 - - - A5 - F5 - A5 - C6 - D6 - C6 -",
    "B5 - - - G5 - D5 - G5 - B5 - D6 - - -",
    "E6 - D6 - C6 - G5 - E5 - G5 - C6 - E6 -",
    "D6 - - - - - - - B5 - - - G5 - - -",
    "A5 - - - C6 - A5 - F5 - - - A5 - C6 -",
    "B5 - - - D6 - B5 - G5 - - - B5 - D6 -",
    "G5 - - - B5 - G5 - E5 - G5 - B5 - E6 -",
    "C6 - - - - - B5 - A5 - - - E5 - - -",
    "F5 - A5 - D6 - - - C6 - A5 - F5 - - -",
    "G5 - B5 - D6 - - - F6 - E6 - D6 - B5 -",
    "C6 - - - G5 - E5 - G5 - C6 - E6 - G6 -",
    "E6 - - - - - - - . . . . . . . .",
};
/* chord of each melody bar: root (MIDI) and type (0 major, 1 minor) */
static const int CHORDS[16][2] = {
    {48, 0}, {48, 0}, {45, 1}, {45, 1}, {41, 0}, {43, 0}, {48, 0}, {43, 0},
    {41, 0}, {43, 0}, {40, 1}, {45, 1}, {38, 1}, {43, 0}, {48, 0}, {48, 0},
};
/* order: intro (2 bars of bass and drums only), then A A' B A (A = bars 0-7, B = 8-15) */
#define INTRO 2

static int rate;
static double samples_per_step;

static float midi_hz(int n) { return 440.0f * powf(2.0f, (n - 69) / 12.0f); }

static int note(const char *t)
{
    static const int ofs[7] = {9, 11, 0, 2, 4, 5, 7}; /* A B C D E F G */
    int n = ofs[t[0] - 'A'], i = 1;
    if (t[i] == '#') { n++; i++; }
    return n + (t[i] - '0' + 1) * 12;
}

/* reads token k (0..15) of the bar: returns the note, -1 hold, -2 rest */
static int token(const char *line, int k)
{
    while (k--) { while (*line && *line != ' ') line++; while (*line == ' ') line++; }
    if (*line == '-') return -1;
    if (*line == '.') return -2;
    return note(line);
}

/* ---- voices ---- */
typedef struct { float phase, f, env, dec, vol, duty, vib; int on; } pulse_t;
typedef struct { float phase, f; int on; } tri_t;
typedef struct { uint16_t lfsr; int period, count; float env, dec, vol; int short_mode; } noise_t;
static pulse_t p1, p2;
static tri_t tr;
static noise_t rd;
static long sample_pos; /* global counter */

static float pulse(pulse_t *p)
{
    if (!p->on && p->env < 0.001f) return 0;
    float t = sample_pos / (float)rate;
    float f = p->f * (1 + p->vib * sinf(t * 6.2831853f * 5.5f)); /* light vibrato on long notes */
    p->phase += f / rate;
    p->phase -= (int)p->phase;
    p->env *= p->dec;
    /* volume in 16 steps, like the NES */
    float e = (int)(p->env * 15) / 15.0f;
    return (p->phase < p->duty ? 1.0f : -1.0f) * e * p->vol;
}

static float triangle(tri_t *t)
{
    if (!t->on) return 0;
    t->phase += t->f / rate;
    t->phase -= (int)t->phase;
    float v = t->phase < 0.5f ? t->phase * 4 - 1 : 3 - t->phase * 4;
    return (int)(v * 7.5f) / 7.5f * 0.9f; /* 16 steps */
}

static float noise(noise_t *r)
{
    if (r->env < 0.001f) return 0;
    if (--r->count <= 0) {
        r->count = r->period;
        int bit = (r->lfsr ^ (r->lfsr >> (r->short_mode ? 6 : 1))) & 1;
        r->lfsr = (r->lfsr >> 1) | (bit << 14);
    }
    r->env *= r->dec;
    return ((r->lfsr & 1) ? 1.0f : -1.0f) * r->env * r->vol;
}

static void drum(int type)
{
    float s = 1.0f / rate;
    rd.env = 1;
    if (type == 0) { rd.period = 40; rd.dec = expf(-s / 0.05f); rd.vol = 0.55f; rd.short_mode = 0; }  /* kick */
    if (type == 1) { rd.period = 8; rd.dec = expf(-s / 0.07f); rd.vol = 0.45f; rd.short_mode = 0; }   /* snare */
    if (type == 2) { rd.period = 2; rd.dec = expf(-s / 0.015f); rd.vol = 0.22f; rd.short_mode = 1; }  /* hi-hat */
}

/* ---- sequence ---- */
static int melody_bar(int c) /* bar c of the song (0 = start of the loop) -> melody bar */
{
    static const int ORDER[4] = {0, 0, 8, 0}; /* A A B A */
    return ORDER[(c / 8) % 4] + c % 8;
}

static void step(int c, int s, int intro)
{
    float st = 60.0f / BPM / 4, secs = 1.0f / rate;
    int cm = intro ? 0 : melody_bar(c);
    int root = CHORDS[cm][0], is_minor = CHORDS[cm][1];
    /* bass (triangle): root and octave in 8th notes */
    tr.on = 1;
    tr.f = midi_hz(root - 12 + ((s / 2) % 2 ? 12 : 0) + (s >= 12 && s % 4 == 2 ? 7 : 0));
    /* drums */
    if (s == 0 || s == 10) drum(0);
    else if (s == 4 || s == 12) drum(1);
    else if (s % 2 == 0) drum(2);
    if (intro) return;
    /* arpeggio (thin square, very quiet) */
    int arp[4] = {0, is_minor ? 3 : 4, 7, 12};
    p2.f = midi_hz(root + 12 + arp[s % 4]);
    p2.on = 1; p2.env = 1; p2.dec = expf(-secs / 0.06f); p2.vol = 0.10f; p2.duty = 0.125f; p2.vib = 0;
    /* melody (variation in A': one octave down in the second half) */
    int t = token(MEL[cm], s);
    if (t >= 0) {
        int octave = ((c / 8) % 4 == 1 && c % 8 >= 4) ? -12 : 0;
        p1.f = midi_hz(t + octave);
        p1.on = 1; p1.env = 1; p1.dec = expf(-secs / 0.45f); p1.vol = 0.22f; p1.duty = 0.25f; p1.vib = 0;
    } else if (t == -1 && p1.on) {
        p1.vib = 0.004f; /* a held note gets vibrato */
    } else if (t == -2) {
        p1.on = 0; p1.dec = expf(-secs / 0.02f);
    }
    (void)st;
}

static void mix(int16_t *out, long n, int c0, int intro)
{
    double counter = 0;
    int c = c0, s = 0;
    for (long k = 0; k < n; k++) {
        if (counter <= 0) {
            step(c, s, intro);
            if (++s == STEPS_PER_BAR) { s = 0; c++; }
            counter += samples_per_step;
        }
        counter -= 1;
        float v = pulse(&p1) + pulse(&p2) + triangle(&tr) * 0.30f + noise(&rd) * 0.5f;
        /* light low-pass filter (the NES had one on its output) */
        static float lp;
        lp += (v - lp) * 0.45f;
        int x = (int)(lp * 12000);
        out[k * 2] = out[k * 2 + 1] = x > 32767 ? 32767 : x < -32768 ? -32768 : x;
        sample_pos++;
    }
}

void chip_init(int t)
{
    rate = t;
    samples_per_step = rate * 60.0 / BPM / 4;
    memset(&p1, 0, sizeof p1); memset(&p2, 0, sizeof p2); memset(&tr, 0, sizeof tr); memset(&rd, 0, sizeof rd);
    rd.lfsr = 1; sample_pos = 0;
}

long chip_intro_frames(void) { return (long)(INTRO * STEPS_PER_BAR * samples_per_step + 0.5); }
long chip_loop_frames(void) { return (long)(32 * STEPS_PER_BAR * samples_per_step + 0.5); }

void chip_intro(int16_t *out) { mix(out, chip_intro_frames(), 0, 1); }
void chip_loop(int16_t *out) { mix(out, chip_loop_frames(), 0, 0); }
