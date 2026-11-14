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

#define MENU_TOP 12
#define MENU_MOVE_MS 130U
#define MENU_FLASH_MS 260U

int32_t scene_menu_row_h(const VaultMenuModel *m) {
    return m->tall ? VAULT_ROW_H : 13;
}

int32_t scene_menu_visible(const VaultMenuModel *m) {
    return (VG_H - MENU_TOP) / scene_menu_row_h(m);
}

static int32_t menu_anim(const VaultMenuModel *m, int32_t from, int32_t to) {
    return vg_lerp(from, to, vg_ease_out_cubic(vg_progress(m->now, m->moved_at, MENU_MOVE_MS)));
}

void scene_menu_draw(Canvas *canvas, const VaultMenuModel *m) {
    const int32_t row_h = scene_menu_row_h(m);
    const int32_t scroll = menu_anim(m, m->scroll_from, m->scroll_to);
    const int32_t cursor = menu_anim(m, m->cursor_from, m->cursor_to);

    canvas_set_color(canvas, ColorBlack);
    if (m->count == 0) {
        canvas_set_font(canvas, FontSecondary);
        vt_str_center(canvas, 64, 38, m->footer[0] ? m->footer : "Nothing here");
    } else {
        /* Highlight plate slides between rows; a notch marks it as the key. */
        int32_t hy = MENU_TOP + 1 + cursor - scroll;
        vg_rfill(canvas, 0, hy, 124, row_h - 1, VgSet);
        vg_px(canvas, 1, hy + row_h / 2 - 1, VgClear);
        vg_vline(canvas, 2, hy + row_h / 2 - 2, hy + row_h / 2, VgClear);
    }

    for (uint8_t i = 0; i < m->count; ++i) {
        const VaultMenuItem *it = &m->items[i];
        int32_t y = MENU_TOP + 1 + i * row_h - scroll;
        if (y >= VG_H || y + row_h <= MENU_TOP) continue;
        bool sel = i == m->selected;
        canvas_set_color(canvas, sel ? ColorWhite : ColorBlack);
        if (m->tall) {
            int32_t tx = 5;
            if (it->has_seed) {
                vault_sigil_draw(canvas, 5, y + 1, 1, &it->seed, sel ? VgClear : VgSet);
                tx = 24;
            }
            canvas_set_font(canvas, FontPrimary);
            if (sel) vt_str_marquee(canvas, tx, y + 8, 120 - tx, it->label, m->now);
            else vt_str_clip(canvas, tx, y + 8, 120 - tx, it->label);
            canvas_set_font(canvas, FontSecondary);
            if (sel) vt_str_marquee(canvas, tx, y + 15, 120 - tx, it->subtitle, m->now);
            else vt_str_clip(canvas, tx, y + 15, 120 - tx, it->subtitle);
        } else {
            canvas_set_font(canvas, FontPrimary);
            int32_t value_w = 0;
            if (it->value[0]) {
                canvas_set_font(canvas, FontSecondary);
                value_w = canvas_string_width(canvas, it->value) + 6;
                int32_t vx = 120 - value_w;
                bool flash = sel && (m->now - m->flash_at) < MENU_FLASH_MS &&
                             ((m->now - m->flash_at) / 65U) % 2U == 0U;
                if (sel && !flash) {
                    vg_rfill(canvas, vx, y + 2, value_w, 9, VgClear);
                    canvas_set_color(canvas, ColorBlack);
                } else {
                    vg_rframe(canvas, vx, y + 2, value_w, 9, sel ? VgClear : VgSet);
                }
                vt_str(canvas, vx + 3, y + 9, it->value);
                canvas_set_color(canvas, sel ? ColorWhite : ColorBlack);
                canvas_set_font(canvas, FontPrimary);
            }
            vt_str_clip(canvas, 6, y + 10, 114 - value_w, it->label);
        }
        canvas_set_color(canvas, ColorBlack);
    }

    /* Scroll rail. */
    int32_t visible = scene_menu_visible(m);
    if (m->count > visible) {
        int32_t track = VG_H - MENU_TOP - 2;
        for (int32_t y = MENU_TOP + 1; y < VG_H; y += 2) vg_px(canvas, 126, y, VgSet);
        int32_t thumb = vg_clamp(track * visible / m->count, 4, track);
        int32_t max_scroll = (m->count - visible) * row_h;
        int32_t ty = MENU_TOP + 1 + ((track - thumb) * scroll) / (max_scroll > 0 ? max_scroll : 1);
        vg_fill(canvas, 125, ty, 3, thumb, VgSet);
    }

    vg_fill(canvas, 0, 0, VG_W, MENU_TOP, VgClear);
    scene_title_bar(canvas, m->icon, m->title, NULL);
}
