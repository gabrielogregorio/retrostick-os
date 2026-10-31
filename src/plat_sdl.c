/* PC platform: SDL2 window (menu preview). Keyboard + any controller.
 * Keyboard: arrows, Z=A, X=B, A=X, S=Y, Q=L, W=R, right Shift=Select, Enter=Start, Esc=quit.
 * F2 toggles "controller 2 connected" just to see the indicator.
 *
 * Scripted runs (tests and the README demo), usually with SDL_VIDEODRIVER=dummy:
 *   STICK_KEYS=D,D,R,A,.,S   one key every 15 input reads, then quits
 *     (U D L R = directions, A B X Y, S = select, T = start, M = mode, anything else = no key)
 *   STICK_SHOT=file.ppm      saves the last screen on exit
 *   STICK_FRAMES=dir         saves every 2nd menu frame as dir/NNNNN.ppm
 *   STICK_LOG=file           appends plat_log lines
 *   STICK_RUNNERS=dir        where the game runners live (default out) */
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "plat.h"

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_GameController *pads[2];
static uint32_t keys, quit_flag;
static int p2_fake;
static SDL_Texture *tex565;
static int tw565, th565;
static SDL_AudioDeviceID aud;
static int aud_rate;
static const char *script;
static FILE *log_file;
static uint32_t shot[SCREEN_W * SCREEN_H];
static int have_shot, frame_count;

static void save_ppm(const char *path, const uint32_t *px)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i += 1) {
        uint8_t p[3] = {px[i] >> 16, px[i] >> 8, px[i]};
        fwrite(p, 1, 3, f);
    }
    fclose(f);
}

int plat_init(void)
{
    script = getenv("STICK_KEYS");
    if (getenv("STICK_LOG")) {
        log_file = fopen(getenv("STICK_LOG"), "a");
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO)) return 1;
    win = SDL_CreateWindow("RetroStick OS - preview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W * 2,
                           SCREEN_H * 2, SDL_WINDOW_RESIZABLE);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    return !(win && ren && tex);
}

void plat_shutdown(void)
{
    const char *path = getenv("STICK_SHOT");
    if (path && have_shot) {
        save_ppm(path, shot);
    }
    if (log_file) {
        fclose(log_file);
    }
    SDL_Quit();
}

void plat_present(const uint32_t *t)
{
    memcpy(shot, t, sizeof shot);
    have_shot = 1;
    const char *frames = getenv("STICK_FRAMES");
    if (frames && frame_count % 2 == 0) {
        char path[512];
        snprintf(path, sizeof path, "%s/%05d.ppm", frames, frame_count / 2);
        save_ppm(path, t);
    }
    frame_count += 1;
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
    SDL_UpdateTexture(tex, NULL, t, SCREEN_W * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

static uint32_t key_to_button(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP: return BTN_UP;
    case SDLK_DOWN: return BTN_DOWN;
    case SDLK_LEFT: return BTN_LEFT;
    case SDLK_RIGHT: return BTN_RIGHT;
    case SDLK_z: return BTN_A;
    case SDLK_x: return BTN_B;
    case SDLK_a: return BTN_X;
    case SDLK_s: return BTN_Y;
    case SDLK_q: return BTN_L;
    case SDLK_w: return BTN_R;
    case SDLK_RSHIFT: return BTN_SELECT;
    case SDLK_RETURN: return BTN_START;
    case SDLK_m: return BTN_MODE;
    case SDLK_ESCAPE: return BTN_QUIT;
    }
    return 0;
}

static uint32_t pad_bt(SDL_GameController *g)
{
    static const struct { int b; uint32_t m; } M[] = {
        {SDL_CONTROLLER_BUTTON_DPAD_UP, BTN_UP}, {SDL_CONTROLLER_BUTTON_DPAD_DOWN, BTN_DOWN},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, BTN_LEFT}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, BTN_RIGHT},
        {SDL_CONTROLLER_BUTTON_A, BTN_A}, {SDL_CONTROLLER_BUTTON_B, BTN_B},
        {SDL_CONTROLLER_BUTTON_X, BTN_X}, {SDL_CONTROLLER_BUTTON_Y, BTN_Y},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, BTN_L}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, BTN_R},
        {SDL_CONTROLLER_BUTTON_BACK, BTN_SELECT}, {SDL_CONTROLLER_BUTTON_START, BTN_START},
        {SDL_CONTROLLER_BUTTON_GUIDE, BTN_MODE},
    };
    uint32_t m = 0;
    for (unsigned i = 0; i < sizeof M / sizeof M[0]; i++)
        if (SDL_GameControllerGetButton(g, M[i].b)) m |= M[i].m;
    int ax = SDL_GameControllerGetAxis(g, SDL_CONTROLLER_AXIS_LEFTX);
    int ay = SDL_GameControllerGetAxis(g, SDL_CONTROLLER_AXIS_LEFTY);
    if (ax < -16000) m |= BTN_LEFT;
    if (ax > 16000) m |= BTN_RIGHT;
    if (ay < -16000) m |= BTN_UP;
    if (ay > 16000) m |= BTN_DOWN;
    return m;
}

/* same pacing as simulated_key() in plat_stick.c */
static uint32_t scripted_key(void)
{
    static int reads;
    int q = reads;
    reads += 1;
    int idx = q / 15, phase = q % 15, n = 1, k = 0;
    const char *p = script;
    for (const char *c = script; *c; c += 1) {
        n += *c == ',';
    }
    while (*p && k < idx) {
        k += *p == ',';
        p += 1;
    }
    if (!*p) {
        return q > n * 15 + 30 ? BTN_QUIT : 0;
    }
    if (phase > 3) {
        return 0;
    }
    switch (*p) {
    case 'U': return BTN_UP;
    case 'D': return BTN_DOWN;
    case 'L': return BTN_LEFT;
    case 'R': return BTN_RIGHT;
    case 'A': return BTN_A;
    case 'B': return BTN_B;
    case 'X': return BTN_X;
    case 'Y': return BTN_Y;
    case 'S': return BTN_SELECT;
    case 'T': return BTN_START;
    case 'M': return BTN_MODE;
    }

    return 0;
}

void plat_input(input_t *e)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) quit_flag = BTN_QUIT;
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F2) p2_fake ^= 1;
        if (ev.type == SDL_KEYDOWN) keys |= key_to_button(ev.key.keysym.sym);
        if (ev.type == SDL_KEYUP) keys &= ~key_to_button(ev.key.keysym.sym);
        if (ev.type == SDL_CONTROLLERDEVICEADDED)
            for (int i = 0; i < 2; i++)
                if (!pads[i]) { pads[i] = SDL_GameControllerOpen(ev.cdevice.which); break; }
    }
    memset(e, 0, sizeof *e);
    e->bt[0] = keys | quit_flag | (script ? scripted_key() : 0);
    e->present[0] = 1;
    e->present[1] = p2_fake;
    for (int i = 0; i < 2; i++)
        if (pads[i]) { e->bt[i] |= pad_bt(pads[i]); e->present[i] = 1; }
}

uint32_t plat_ms(void) { return SDL_GetTicks(); }
void plat_sleep(int ms) { SDL_Delay(ms); }

const char *plat_roms_dir(void)
{
    const char *d = getenv("STICK_ROMS");
    return d ? d : "roms";
}

void plat_log(const char *fmt, int v)
{
    if (log_file) {
        fprintf(log_file, fmt, v);
        fputc('\n', log_file);
        fflush(log_file);
    }
}

void plat_present_565(const uint16_t *px, int w, int h, int pitch_px)
{
    if (!tex565 || tw565 != w || th565 != h) {
        if (tex565) SDL_DestroyTexture(tex565);
        tex565 = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w, h);
        tw565 = w; th565 = h;
    }
    if (getenv("STICK_SHOT")) {
        for (int y = 0; y < SCREEN_H; y += 1) {
            for (int x = 0; x < SCREEN_W; x += 1) {
                uint16_t c = px[(y * h / SCREEN_H) * pitch_px + x * w / SCREEN_W];
                shot[y * SCREEN_W + x] = ((c & 0xf800) << 8) | ((c & 0x07e0) << 5) | ((c & 0x1f) << 3);
            }
        }
        have_shot = 1;
    }
    SDL_RenderSetLogicalSize(ren, 640, 480);
    SDL_UpdateTexture(tex565, NULL, px, pitch_px * 2);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex565, NULL, NULL);
    SDL_RenderPresent(ren);
}

int plat_audio_init(int rate)
{
    SDL_AudioSpec q = {0}, r;
    q.freq = rate; q.format = AUDIO_S16SYS; q.channels = 2; q.samples = 1024;
    aud = SDL_OpenAudioDevice(NULL, 0, &q, &r, 0);
    if (!aud) return -1;
    aud_rate = r.freq;
    SDL_PauseAudioDevice(aud, 0);
    return 0;
}

int plat_audio(const int16_t *s, int n)
{
    SDL_QueueAudio(aud, s, n * 4);
    /* holds the pacing: at most ~60 ms in the queue */
    while (SDL_GetQueuedAudioSize(aud) > (Uint32)(aud_rate * 4 * 60 / 1000)) SDL_Delay(1);
    return n;
}

int plat_audio_rate(void) { return aud ? aud_rate : 0; }
void plat_audio_close(void) { if (aud) SDL_CloseAudioDevice(aud); aud = 0; }

int plat_audio_queue(void) { return aud ? (int)(SDL_GetQueuedAudioSize(aud) / 4) : -1; }
void plat_disable_retrace(void) {}
int plat_pacing(void) { return aud ? PACE_AUDIO : PACE_CLOCK; }
uint32_t plat_us(void) { return (uint32_t)(SDL_GetPerformanceCounter() / (SDL_GetPerformanceFrequency() / 1000000)); }
void plat_reactivate(void) { SDL_RaiseWindow(win); }

const char *plat_runner(const char *sys)
{
    static char path[256];
    const char *dir = getenv("STICK_RUNNERS");
    snprintf(path, sizeof path, "%s/%s-pc", dir ? dir : "out", sys);
    return access(path, X_OK) ? NULL : path;
}
void plat_diagnostics(void) {}
