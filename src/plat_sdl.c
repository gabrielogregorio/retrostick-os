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

#define PLAYERS 2
#define WINDOW_SCALE 2
#define AXIS_THRESHOLD 16000
#define READS_PER_KEY 15
#define LAST_PRESSED_PHASE 3 /* released for the rest of the key's reads */
#define READS_BEFORE_QUIT 30
#define BYTES_PER_AUDIO_FRAME 4 /* 16-bit stereo */
#define AUDIO_CHANNELS 2
#define AUDIO_DEVICE_SAMPLES 1024
#define AUDIO_MAX_QUEUE_MS 60
#define GAME_VIEW_W 640
#define GAME_VIEW_H 480
#define RED_565_MASK 0xf800
#define GREEN_565_MASK 0x07e0
#define BLUE_565_MASK 0x001f

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_GameController *pads[PLAYERS];
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
    FILE *file = fopen(path, "wb");
    if (!file) {
        return;
    }
    fprintf(file, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int pixel_index = 0; pixel_index < SCREEN_W * SCREEN_H; pixel_index += 1) {
        uint8_t rgb[3] = {px[pixel_index] >> 16, px[pixel_index] >> 8, px[pixel_index]};
        fwrite(rgb, 1, sizeof rgb, file);
    }
    fclose(file);
}

int plat_init(void)
{
    script = getenv("STICK_KEYS");
    if (getenv("STICK_LOG")) {
        log_file = fopen(getenv("STICK_LOG"), "a");
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO)) {
        return 1;
    }
    win = SDL_CreateWindow("RetroStick OS - preview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           SCREEN_W * WINDOW_SCALE, SCREEN_H * WINDOW_SCALE, SDL_WINDOW_RESIZABLE);
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

void plat_present(const uint32_t *screen)
{
    memcpy(shot, screen, sizeof shot);
    have_shot = 1;
    const char *frames = getenv("STICK_FRAMES");
    if (frames && frame_count % 2 == 0) {
        char path[512];
        snprintf(path, sizeof path, "%s/%05d.ppm", frames, frame_count / 2);
        save_ppm(path, screen);
    }
    frame_count += 1;
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
    SDL_UpdateTexture(tex, NULL, screen, SCREEN_W * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

static uint32_t key_to_button(SDL_Keycode key)
{
    switch (key) {
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

static uint32_t pad_axes_to_buttons(SDL_GameController *controller)
{
    uint32_t buttons = 0;
    int axis_x = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
    int axis_y = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
    if (axis_x < -AXIS_THRESHOLD) {
        buttons |= BTN_LEFT;
    }
    if (axis_x > AXIS_THRESHOLD) {
        buttons |= BTN_RIGHT;
    }
    if (axis_y < -AXIS_THRESHOLD) {
        buttons |= BTN_UP;
    }
    if (axis_y > AXIS_THRESHOLD) {
        buttons |= BTN_DOWN;
    }

    return buttons;
}

static uint32_t pad_bt(SDL_GameController *controller)
{
    static const struct {
        int sdl_button;
        uint32_t button;
    } PAD_MAP[] = {
        {SDL_CONTROLLER_BUTTON_DPAD_UP, BTN_UP},
        {SDL_CONTROLLER_BUTTON_DPAD_DOWN, BTN_DOWN},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, BTN_LEFT},
        {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, BTN_RIGHT},
        {SDL_CONTROLLER_BUTTON_A, BTN_A},
        {SDL_CONTROLLER_BUTTON_B, BTN_B},
        {SDL_CONTROLLER_BUTTON_X, BTN_X},
        {SDL_CONTROLLER_BUTTON_Y, BTN_Y},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, BTN_L},
        {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, BTN_R},
        {SDL_CONTROLLER_BUTTON_BACK, BTN_SELECT},
        {SDL_CONTROLLER_BUTTON_START, BTN_START},
        {SDL_CONTROLLER_BUTTON_GUIDE, BTN_MODE},
    };

    uint32_t buttons = 0;
    for (unsigned map_index = 0; map_index < sizeof PAD_MAP / sizeof PAD_MAP[0]; map_index += 1) {
        if (SDL_GameControllerGetButton(controller, PAD_MAP[map_index].sdl_button)) {
            buttons |= PAD_MAP[map_index].button;
        }
    }

    return buttons | pad_axes_to_buttons(controller);
}

static uint32_t script_button(char key)
{
    switch (key) {
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

/* Same semantics as simulated_key() in stick_input.c: change both together. */
static uint32_t scripted_key(void)
{
    static int reads;
    int read_index = reads;
    reads += 1;
    int key_index = read_index / READS_PER_KEY;
    int phase = read_index % READS_PER_KEY;
    int key_count = 1;
    for (const char *key = script; *key; key += 1) {
        key_count += *key == ',';
    }
    int commas = 0;
    const char *key = script;
    while (*key && commas < key_index) {
        commas += *key == ',';
        key += 1;
    }
    if (!*key) {
        return read_index > key_count * READS_PER_KEY + READS_BEFORE_QUIT ? BTN_QUIT : 0;
    }
    if (phase > LAST_PRESSED_PHASE) {
        return 0;
    }

    return script_button(*key);
}

static void open_new_pad(int device_index)
{
    for (int pad_index = 0; pad_index < PLAYERS; pad_index += 1) {
        if (!pads[pad_index]) {
            pads[pad_index] = SDL_GameControllerOpen(device_index);

            return;
        }
    }
}

static void handle_event(const SDL_Event *event)
{
    if (event->type == SDL_QUIT) {
        quit_flag = BTN_QUIT;
    }
    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F2) {
        p2_fake ^= 1;
    }
    if (event->type == SDL_KEYDOWN) {
        keys |= key_to_button(event->key.keysym.sym);
    }
    if (event->type == SDL_KEYUP) {
        keys &= ~key_to_button(event->key.keysym.sym);
    }
    if (event->type == SDL_CONTROLLERDEVICEADDED) {
        open_new_pad(event->cdevice.which);
    }
}

void plat_input(input_t *input)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        handle_event(&event);
    }
    memset(input, 0, sizeof *input);
    input->bt[0] = keys | quit_flag | (script ? scripted_key() : 0);
    input->present[0] = 1;
    input->present[1] = p2_fake;
    for (int pad_index = 0; pad_index < PLAYERS; pad_index += 1) {
        if (pads[pad_index]) {
            input->bt[pad_index] |= pad_bt(pads[pad_index]);
            input->present[pad_index] = 1;
        }
    }
}

uint32_t plat_ms(void)
{
    return SDL_GetTicks();
}

void plat_sleep(int ms)
{
    SDL_Delay(ms);
}

const char *plat_roms_dir(void)
{
    const char *dir = getenv("STICK_ROMS");

    return dir ? dir : "roms";
}

void plat_log(const char *fmt, int value)
{
    if (log_file) {
        fprintf(log_file, fmt, value);
        fputc('\n', log_file);
        fflush(log_file);
    }
}

static void capture_game_shot(const uint16_t *px, int w, int h, int pitch_px)
{
    for (int y = 0; y < SCREEN_H; y += 1) {
        for (int x = 0; x < SCREEN_W; x += 1) {
            uint16_t color = px[(y * h / SCREEN_H) * pitch_px + x * w / SCREEN_W];
            shot[y * SCREEN_W + x] =
                ((color & RED_565_MASK) << 8) | ((color & GREEN_565_MASK) << 5) | ((color & BLUE_565_MASK) << 3);
        }
    }
    have_shot = 1;
}

void plat_present_565(const uint16_t *px, int w, int h, int pitch_px)
{
    if (!tex565 || tw565 != w || th565 != h) {
        if (tex565) {
            SDL_DestroyTexture(tex565);
        }
        tex565 = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w, h);
        tw565 = w;
        th565 = h;
    }
    if (getenv("STICK_SHOT")) {
        capture_game_shot(px, w, h, pitch_px);
    }
    SDL_RenderSetLogicalSize(ren, GAME_VIEW_W, GAME_VIEW_H);
    SDL_UpdateTexture(tex565, NULL, px, pitch_px * 2);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex565, NULL, NULL);
    SDL_RenderPresent(ren);
}

int plat_audio_init(int rate)
{
    SDL_AudioSpec requested = {0}, obtained;
    requested.freq = rate;
    requested.format = AUDIO_S16SYS;
    requested.channels = AUDIO_CHANNELS;
    requested.samples = AUDIO_DEVICE_SAMPLES;
    aud = SDL_OpenAudioDevice(NULL, 0, &requested, &obtained, 0);
    if (!aud) {
        return -1;
    }
    aud_rate = obtained.freq;
    SDL_PauseAudioDevice(aud, 0);

    return 0;
}

int plat_audio(const int16_t *samples, int frames)
{
    SDL_QueueAudio(aud, samples, frames * BYTES_PER_AUDIO_FRAME);
    /* holds the pacing: at most ~60 ms in the queue */
    while (SDL_GetQueuedAudioSize(aud) > (Uint32)(aud_rate * BYTES_PER_AUDIO_FRAME * AUDIO_MAX_QUEUE_MS / 1000)) {
        SDL_Delay(1);
    }

    return frames;
}

int plat_audio_rate(void)
{
    return aud ? aud_rate : 0;
}

void plat_audio_close(void)
{
    if (aud) {
        SDL_CloseAudioDevice(aud);
    }
    aud = 0;
}

int plat_audio_queue(void)
{
    return aud ? (int)(SDL_GetQueuedAudioSize(aud) / BYTES_PER_AUDIO_FRAME) : -1;
}

void plat_disable_retrace(void)
{
}

int plat_pacing(void)
{
    return aud ? PACE_AUDIO : PACE_CLOCK;
}

uint32_t plat_us(void)
{
    return (uint32_t)(SDL_GetPerformanceCounter() / (SDL_GetPerformanceFrequency() / 1000000));
}

void plat_reactivate(void)
{
    SDL_RaiseWindow(win);
}

const char *plat_runner(const char *system_name)
{
    static char path[256];
    const char *dir = getenv("STICK_RUNNERS");
    snprintf(path, sizeof path, "%s/%s-pc", dir ? dir : "out", system_name);

    return access(path, X_OK) ? NULL : path;
}

void plat_diagnostics(void)
{
}
