/*
 * Flipper Vault UI (based on ZeroFIDO by Alex Stoyanov)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 or later.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "scene.h"

#include <string.h>

/* 7x7 icons, one byte per row, bit 0 = leftmost pixel. */
static const uint8_t icon_bits[][7] = {
    [VaultIconLock] = {0x1C, 0x22, 0x22, 0x7F, 0x77, 0x77, 0x7F},
    [VaultIconKey] = {0x00, 0x06, 0x79, 0x69, 0x06, 0x00, 0x00},
    [VaultIconGlobe] = {0x1C, 0x2A, 0x7F, 0x49, 0x7F, 0x2A, 0x1C},
    [VaultIconUser] = {0x1C, 0x3E, 0x1C, 0x00, 0x3E, 0x7F, 0x7F},
    [VaultIconWarn] = {0x08, 0x14, 0x14, 0x2A, 0x22, 0x49, 0x7F},
    [VaultIconInfo] = {0x1C, 0x36, 0x3E, 0x36, 0x36, 0x36, 0x1C},
};

void scene_copy(char *dst, size_t size, const char *src) {
    size_t len = src ? strlen(src) : 0U;
    if (size == 0U)
        return;
    if (len >= size)
        len = size - 1U;
    if (len)
        memcpy(dst, src, len);
    dst[len] = '\0';
}

void scene_icon(Canvas *canvas, int32_t x, int32_t y, VaultIcon icon) {
    canvas_draw_xbm(canvas, x, y, 7, 7, icon_bits[icon]);
}

void scene_title_bar(Canvas *canvas, VaultIcon icon, const char *title, const char *right) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, VG_W, 11);
    canvas_set_color(canvas, ColorWhite);
    /* Rivets at the bar ends. */
    canvas_draw_dot(canvas, 1, 1);
    canvas_draw_dot(canvas, 126, 1);
    canvas_draw_dot(canvas, 1, 9);
    canvas_draw_dot(canvas, 126, 9);
    scene_icon(canvas, 4, 2, icon);
    int32_t right_w = 0;
    if (right) {
        canvas_set_font(canvas, FontSecondary);
        right_w = canvas_string_width(canvas, right) + 4;
        canvas_draw_str_aligned(canvas, 124, 9, AlignRight, AlignBottom, right);
    }
    canvas_set_font(canvas, FontPrimary);
    vt_line(canvas, 14, 9, 108 - right_w, title, 0, false);
    canvas_set_color(canvas, ColorBlack);
}

void scene_transport_ambient(Canvas *canvas, int32_t x, int32_t y, VaultTransportArt art,
                             uint32_t now, bool active) {
    canvas_set_color(canvas, ColorBlack);
    if (art == VaultTransportUsb) {
        /* Cable into a plug; packets pulse along it towards the vault. */
        canvas_draw_box(canvas, x, y + 7, 16, 2);
        canvas_draw_rbox(canvas, x + 16, y + 4, 7, 8, 1);
        canvas_draw_box(canvas, x + 23, y + 5, 3, 2);
        canvas_draw_box(canvas, x + 23, y + 9, 3, 2);
        for (int32_t p = 0; active && p < 3; ++p) {
            int32_t pos = (int32_t)((now / 40U + (uint32_t)p * 6U) % 18U);
            if (pos < 15)
                canvas_draw_frame(canvas, x + pos, y + 6, 2, 4);
        }
        return;
    }
    /*
     * NFC: field arcs step out from a card. BLE: rings step out from the rune.
     * While active the arcs light one by one (160 ms steps); idle shows them all.
     */
    bool nfc = art == VaultTransportNfc;
    int32_t cx = nfc ? x + 6 : x + 13;
    int32_t cy = y + 8;
    int32_t lit = active ? 1 + (int32_t)((now / 160U) % 3U) : 3;
    for (int32_t k = 0; k < lit; ++k) {
        if (nfc) {
            int32_t ax = x + 9 + k * 3;
            int32_t h = 3 + k * 2;
            canvas_draw_line(canvas, ax, cy - h, ax + 1, cy - h + 1);
            canvas_draw_line(canvas, ax + 1, cy - h + 1, ax + 1, cy + h - 1);
            canvas_draw_line(canvas, ax + 1, cy + h - 1, ax, cy + h);
        } else {
            canvas_draw_circle(canvas, cx, cy, 5 + (k * 3) / 2);
        }
    }
    if (nfc) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, x, y + 3, 7, 10);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rframe(canvas, x, y + 3, 7, 10, 1);
        canvas_draw_box(canvas, x + 2, y + 5, 3, 2);
    } else {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_disc(canvas, cx, cy, 4);
        canvas_set_color(canvas, ColorBlack);
        /* Bluetooth rune. */
        canvas_draw_line(canvas, cx, cy - 3, cx, cy + 3);
        canvas_draw_line(canvas, cx, cy - 3, cx + 2, cy - 1);
        canvas_draw_line(canvas, cx + 2, cy - 1, cx - 2, cy + 2);
        canvas_draw_line(canvas, cx, cy + 3, cx + 2, cy + 1);
        canvas_draw_line(canvas, cx + 2, cy + 1, cx - 2, cy - 2);
    }
}
