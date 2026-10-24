/* Runner pause menu (MODE button). */
#pragma once
#include <stdint.h>

/* frame: last game frame in RGB565, w x h, contiguous lines (may be NULL).
 * notes: small text to the right of each item (may be NULL). notice: replaces the footer (may be NULL). */
void pause_draw(const uint16_t *frame, int w, int h, const char *const *items, const char *const *notes, int n,
                   int sel, const char *notice);
