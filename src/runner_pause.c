/* Runner pause menu loop: what each item does. The drawing is in pause.c. */
#include <stdio.h>
#include "libretro.h"
#include "pause.h"
#include "plat.h"
#include "runner_internal.h"

#define NOTICE_MAX 64
#define NOTICE_AFTER_SAVE_MS 700
#define POLL_INTERVAL_MS 16

enum {
    ITEM_RESUME,
    ITEM_SAVE_1,
    ITEM_SAVE_2,
    ITEM_LOAD_1,
    ITEM_LOAD_2,
    ITEM_LAST_SAVE,
    ITEM_RESTART,
    ITEM_QUIT,
    ITEM_COUNT
};

typedef struct {
    int sel;
    bool quit;
    bool redraw;
    bool confirming_restart;
    char notice[NOTICE_MAX];
    uint32_t close_at_ms; /* 0 = stays open */
} pause_menu_t;

static void clear_prompt(pause_menu_t *menu)
{
    menu->confirming_restart = false;
    menu->notice[0] = '\0';
    menu->redraw = true;
}

static void save_to_slot(pause_menu_t *menu, char slot, int slot_number, uint32_t now)
{
    if (runner_save_state(slot)) {
        snprintf(menu->notice, sizeof menu->notice, "Could not save");

        return;
    }
    snprintf(menu->notice, sizeof menu->notice, "Saved to slot %d", slot_number);
    menu->close_at_ms = now + NOTICE_AFTER_SAVE_MS;
}

static char slot_to_load(int item)
{
    if (item == ITEM_LOAD_1) {
        return SLOT_1;
    }
    if (item == ITEM_LOAD_2) {
        return SLOT_2;
    }

    return runner_last_slot();
}

static void load_item(pause_menu_t *menu, uint32_t now)
{
    char slot = slot_to_load(menu->sel);
    if (slot == SLOT_NONE) {
        snprintf(menu->notice, sizeof menu->notice, "No save yet");

        return;
    }
    if (runner_load_state(slot)) {
        snprintf(menu->notice, sizeof menu->notice, "Could not load");

        return;
    }
    menu->close_at_ms = now;
}

/* Restarting loses progress since the last save, so it asks for confirmation. */
static void restart_item(pause_menu_t *menu, uint32_t now)
{
    if (!menu->confirming_restart) {
        menu->confirming_restart = true;
        snprintf(menu->notice, sizeof menu->notice, "A again restarts    B cancels");

        return;
    }
    retro_reset();
    plat_log("pause: game restarted", 0);
    menu->close_at_ms = now;
}

static void activate_item(pause_menu_t *menu, uint32_t now)
{
    switch (menu->sel) {
    case ITEM_RESUME: menu->close_at_ms = now; break;
    case ITEM_SAVE_1: save_to_slot(menu, SLOT_1, 1, now); break;
    case ITEM_SAVE_2: save_to_slot(menu, SLOT_2, 2, now); break;
    case ITEM_LOAD_1:
    case ITEM_LOAD_2:
    case ITEM_LAST_SAVE: load_item(menu, now); break;
    case ITEM_RESTART: restart_item(menu, now); break;
    case ITEM_QUIT: menu->quit = true; break;
    }
}

/* Returns true when the menu must close at once. */
static bool handle_buttons(pause_menu_t *menu, uint32_t held, uint32_t pressed, uint32_t now)
{
    if (pressed & (BTN_B | BTN_MODE)) {
        if (!menu->confirming_restart || pressed & BTN_MODE) {
            return true;
        }
        clear_prompt(menu);
    }
    if (pressed & BTN_DOWN) {
        menu->sel = (menu->sel + 1) % ITEM_COUNT;
        clear_prompt(menu);
    }
    if (pressed & BTN_UP) {
        menu->sel = (menu->sel + ITEM_COUNT - 1) % ITEM_COUNT;
        clear_prompt(menu);
    }
    if (pressed & BTN_A && !(held & (BTN_B | BTN_X | BTN_Y))) {
        activate_item(menu, now);
        if (menu->quit) {
            return true;
        }
        menu->redraw = true;
    }

    return false;
}

static const char *last_save_note(char slot)
{
    switch (slot) {
    case SLOT_1: return "slot 1";
    case SLOT_2: return "slot 2";
    case SLOT_AUTO: return "from last quit";
    }

    return "none";
}

static void draw_menu(const pause_menu_t *menu)
{
    bool slot_1_used = runner_slot_exists(SLOT_1);
    bool slot_2_used = runner_slot_exists(SLOT_2);
    const char *items[ITEM_COUNT] = {
        "Resume",
        "Save to slot 1",
        "Save to slot 2",
        "Load slot 1",
        "Load slot 2",
        "Back to last save",
        menu->confirming_restart ? "Restart? Confirm with A" : "Restart game",
        "Quit game",
    };
    const char *notes[ITEM_COUNT] = {
        "",
        slot_1_used ? "in use" : "empty",
        slot_2_used ? "in use" : "empty",
        slot_1_used ? "" : "empty",
        slot_2_used ? "" : "empty",
        last_save_note(runner_last_slot()),
        "",
        "",
    };
    int frame_w = 0;
    int frame_h = 0;
    const uint16_t *frame = runner_last_frame(&frame_w, &frame_h);
    pause_draw(frame, frame_w, frame_h, items, notes, ITEM_COUNT, menu->sel, menu->notice);
}

bool runner_pause_menu(void)
{
    pause_menu_t menu = {.redraw = true};
    uint32_t previous_buttons = BTN_MODE; /* the MODE that opened the menu is still held */
    plat_log("pause: menu opened", 0);
    for (;;) {
        runner_audio_idle();
        uint32_t buttons = runner_poll_buttons();
        uint32_t pressed = buttons & ~previous_buttons;
        uint32_t now = plat_ms();
        previous_buttons = buttons;
        if (menu.close_at_ms && (int32_t)(now - menu.close_at_ms) >= 0) {
            break;
        }
        if (buttons & BTN_QUIT) {
            menu.quit = true;
            break;
        }
        if (!menu.close_at_ms && handle_buttons(&menu, buttons, pressed, now)) {
            break;
        }
        if (menu.redraw) {
            draw_menu(&menu);
            menu.redraw = false;
        }
        plat_sleep(POLL_INTERVAL_MS);
    }
    plat_log("pause: menu closed, quit %d", menu.quit);

    return menu.quit;
}
