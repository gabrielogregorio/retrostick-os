/* PC platform: SDL2 window (menu preview). Keyboard + any controller.
 * Keyboard: arrows, Z=A, X=B, A=X, S=Y, Q=L, W=R, right Shift=Select, Enter=Start, Esc=quit.
 * F2 toggles "controller 2 connected" just to see the indicator. */
#include <SDL2/SDL.h>
#include <stdlib.h>
#include <string.h>
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

int plat_init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO)) return 1;
    win = SDL_CreateWindow("Game Stick - preview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W * 2,
                           SCREEN_H * 2, SDL_WINDOW_RESIZABLE);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    return !(win && ren && tex);
}

void plat_shutdown(void) { SDL_Quit(); }

void plat_present(const uint32_t *t)
{
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
    e->bt[0] = keys | quit_flag;
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

void plat_log(const char *fmt, int v) { (void)fmt; (void)v; }

void plat_present_565(const uint16_t *px, int w, int h, int pitch_px)
{
    if (!tex565 || tw565 != w || th565 != h) {
        if (tex565) SDL_DestroyTexture(tex565);
        tex565 = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w, h);
        tw565 = w; th565 = h;
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
    aud_rate = rate;
    SDL_PauseAudioDevice(aud, 0);
    return 0;
}

void plat_audio(const int16_t *s, int n)
{
    SDL_QueueAudio(aud, s, n * 4);
    /* holds the pacing: at most ~60 ms in the queue */
    while (SDL_GetQueuedAudioSize(aud) > (Uint32)(aud_rate * 4 * 60 / 1000)) SDL_Delay(1);
}

int plat_audio_headroom_ms(void) { return aud ? 0 : -1; }
void plat_reactivate(void) { SDL_RaiseWindow(win); }

const char *plat_runner(const char *sys)
{
    static char path[256];
    snprintf(path, sizeof path, "out/%s-pc", sys);
    return SDL_strcmp(sys, "nes") ? NULL : path;
}
void plat_diagnostics(void) {}
