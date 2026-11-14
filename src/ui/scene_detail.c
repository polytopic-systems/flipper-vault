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

void scene_detail_draw(Canvas *canvas, const VaultDetailModel *m) {
    canvas_set_color(canvas, ColorBlack);

    /* The passkey's seal, turning in slowly as the drawer opens. */
    int32_t t = vg_ease_out_back(vg_progress(m->now, m->entered_at, 380));
    vault_sigil_draw_seal(canvas, 22, 34, &m->seed, ((VG_ONE - t) * 256) / VG_ONE);

    const int32_t x = 47;
    const int32_t w = 127 - x;
    scene_icon(canvas, x, 15, VaultIconGlobe, VgSet);
    canvas_set_font(canvas, FontPrimary);
    vt_str_marquee(canvas, x + 9, 22, w - 9, m->website, m->now);
    scene_icon(canvas, x, 27, VaultIconUser, VgSet);
    canvas_set_font(canvas, FontSecondary);
    vt_str_marquee(canvas, x + 9, 34, w - 9, m->account, m->now);

    /* Type tag. */
    canvas_set_font(canvas, FontSecondary);
    int32_t tw = canvas_string_width(canvas, m->type) + 6;
    vg_rframe(canvas, x, 38, tw, 10, VgSet);
    if (m->resident_key) vg_dither(canvas, x + 1, 39, tw - 2, 8, 3, VgSet);
    vt_str(canvas, x + 3, 46, m->type);

    scene_hint(canvas, 46, "<", "Back", false);
    if (m->allow_delete) scene_hint(canvas, 127, "OK", "Delete", true);
    scene_title_bar(canvas, VaultIconKey, "Passkey", NULL);
}
