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

/* Splits body at '\n' into up to two centred lines. */
static void confirm_body(Canvas *canvas, const char *body) {
    char line[64];
    const char *nl = strchr(body, '\n');
    canvas_set_font(canvas, FontSecondary);
    if (!nl) {
        vt_str_center(canvas, 64, 32, body);
        return;
    }
    size_t len = (size_t)(nl - body);
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, body, len);
    line[len] = '\0';
    vt_str_center(canvas, 64, 28, line);
    vt_str_center(canvas, 64, 38, nl + 1);
}

void scene_confirm_draw(Canvas *canvas, const VaultConfirmModel *m) {
    canvas_set_color(canvas, ColorBlack);
    confirm_body(canvas, m->body);

    scene_hint(canvas, 2, "<", "Cancel", false);
    if (!m->destructive) {
        scene_hint(canvas, 127, "OK", m->action, true);
    } else {
        /* Hold OK: the dial winds a ring closed; release early and it unwinds. */
        int32_t p = m->holding ? vg_progress(m->now, m->hold_at, VAULT_HOLD_MS) : 0;
        const int32_t cx = 112;
        const int32_t cy = 52;
        vault_dial_badge(canvas, cx, cy, 8, (p * 3) / 2);
        int32_t steps = (p * 32) / VG_ONE;
        for (int32_t i = 0; i < steps; ++i) {
            int32_t x, y;
            vg_polar(cx, cy, 11 * 16, i * 8, &x, &y);
            vg_disc(canvas, x, y, 1, VgSet);
        }
        canvas_set_font(canvas, FontSecondary);
        vt_str_right(canvas, cx - 13, 52, m->holding ? "keep holding" : "hold OK to");
        canvas_set_font(canvas, FontPrimary);
        vt_str_right(canvas, cx - 13, 62, m->action);
    }
    scene_title_bar(canvas, m->icon, m->title, NULL);
}
