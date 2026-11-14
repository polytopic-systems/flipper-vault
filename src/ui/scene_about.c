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

void scene_about_draw(Canvas *canvas, const VaultAboutModel *m) {
    VaultDoor door;
    canvas_set_color(canvas, ColorBlack);

    vault_door_init(&door, 22, 37, 20);
    door.dial = (int32_t)(m->now / 24U);
    vault_door_draw(canvas, &door);

    const int32_t x = 47;
    canvas_set_font(canvas, FontPrimary);
    vt_str(canvas, x, 22, "Vault");
    canvas_set_font(canvas, FontSecondary);
    int32_t w = canvas_string_width(canvas, m->version);
    vg_rframe(canvas, 125 - w - 4, 15, w + 5, 9, VgSet);
    vt_str(canvas, 125 - w - 1, 22, m->version);
    vt_str(canvas, x, 31, "FIDO2 passkeys");
    vt_str(canvas, x, 40, "based on ZeroFIDO");
    vt_str(canvas, x, 49, "by Alex Stoyanov");
    vg_rfill(canvas, x, 53, 80, 10, VgSet);
    canvas_set_color(canvas, ColorWhite);
    vt_str(canvas, x + 3, 61, "GPL-3.0-or-later");
    canvas_set_color(canvas, ColorBlack);
    scene_title_bar(canvas, VaultIconInfo, "About", NULL);
}
