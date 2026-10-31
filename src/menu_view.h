/* Menu drawing: carousel, game panel, footer, notices and the controller diagnostics screen. */
#pragma once
#include "gfx.h"
#include "plat.h"

#define MENU_TEXT_LINE_SIZE 256
#define MENU_ELLIPSIS_ROOM 4
#define MENU_TEXT_LINE_MAX (MENU_TEXT_LINE_SIZE - MENU_ELLIPSIS_ROOM - 1)

int menu_view_measure_line(const font_t *font, const char *text, int width, char *line);
void menu_view_init(void); /* builds the icons and the background: expensive, once at startup */
void menu_view_draw_menu(float carousel, const input_t *input, const char *notice, int notice_alpha, float seconds);
void menu_view_draw_diagnostics(const input_t *input, float seconds);
void menu_view_present(void);
