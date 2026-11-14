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

#include <gui/elements.h>

void scene_detail_draw(Canvas *canvas, const VaultDetailModel *m) {
    if (m->about) {
        scene_about_draw(canvas, m->now);
        return;
    }
    canvas_set_color(canvas, ColorBlack);
    /* The passkey's sigil, doubled, in a riveted plate. */
    canvas_draw_rframe(canvas, 2, 14, 37, 37, 3);
    canvas_draw_dot(canvas, 4, 16);
    canvas_draw_dot(canvas, 36, 16);
    canvas_draw_dot(canvas, 4, 48);
    canvas_draw_dot(canvas, 36, 48);
    vault_sigil_draw(canvas, 6, 18, 2, &m->seed);

    const int32_t x = 44;
    scene_icon(canvas, x, 15, VaultIconGlobe);
    canvas_set_font(canvas, FontPrimary);
    vt_line(canvas, x + 9, 22, VG_W - x - 9, m->website, m->now, true);
    scene_icon(canvas, x, 27, VaultIconUser);
    canvas_set_font(canvas, FontSecondary);
    vt_line(canvas, x + 9, 34, VG_W - x - 9, m->account, m->now, true);
    canvas_draw_rframe(canvas, x, 38, canvas_string_width(canvas, m->type) + 6, 11, 2);
    canvas_draw_str(canvas, x + 3, 46, m->type);

    elements_button_left(canvas, "Back");
    if (m->allow_delete)
        elements_button_center(canvas, "Delete");
    scene_title_bar(canvas, VaultIconKey, "Passkey", NULL);
}

void scene_about_draw(Canvas *canvas, uint32_t now) {
    VaultDoor door;
    vault_door_init(&door, 23, 37, 21);
    door.dial = (int32_t)(now / 24U);
    vault_door_draw(canvas, &door);

    const int32_t x = 49;
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, x, 22, "Vault 0.8");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, x, 31, "FIDO2 passkeys");
    canvas_draw_str(canvas, x, 40, "based on ZeroFIDO");
    canvas_draw_str(canvas, x, 49, "by Alex Stoyanov");
    canvas_draw_rbox(canvas, x - 4, 53, VG_W - x + 4, 11, 2);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_str_aligned(canvas, (VG_W + x - 4) / 2, 61, AlignCenter, AlignBottom,
                            "GPL-3.0-or-later");
    scene_title_bar(canvas, VaultIconInfo, "About", NULL);
}
