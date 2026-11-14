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
#include <stdio.h>

#define PANEL_X 70

static void home_draw_door(Canvas *canvas, const VaultHomeModel *m) {
    VaultDoor door;
    uint32_t e = m->now - m->boot_at;
    int32_t shake = 0;

    vault_door_init(&door, 34, 32, 30);
    /* Idle: the dial creeps round; while loading or waiting it hunts. */
    door.dial = (m->starting || m->status == VaultHomeWaiting) ? vg_sin((int32_t)(m->now / 6U)) / 6
                                                               : (int32_t)(m->now / 32U);
    if (m->boot_at && e < VAULT_BOOT_MS) {
        /* Boot: the door swings shut, bolts shoot home in turn, a jolt. */
        door.swing = VG_ONE - vg_ease_in(vg_progress(m->now, m->boot_at, 400));
        for (int i = 0; i < VAULT_DOOR_BOLTS; ++i) {
            door.bolt[i] = vg_ease_back(vg_progress(m->now, m->boot_at + 410U + i * 35U, 150));
        }
        if (e >= 400U && e < 560U)
            shake = (e / 32U) % 2U ? 2 : -2;
    }
    door.cx += shake;
    vault_door_draw(canvas, &door);
}

static void home_draw_panel(Canvas *canvas, const VaultHomeModel *m) {
    char count[8];
    const int32_t x = PANEL_X;

    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, x + 1, 9, "VAULT");
    /* Status light. */
    if (m->status == VaultHomeConnected)
        canvas_draw_disc(canvas, 123, 5, 2);
    else if (m->status == VaultHomeError)
        canvas_draw_line(canvas, 121, 3, 125, 7);
    else
        canvas_draw_circle(canvas, 123, 5, 2);
    if (m->status == VaultHomeWaiting && (m->now / 300U) % 2U)
        canvas_draw_dot(canvas, 123, 5);
    for (int32_t i = x; i < VG_W; i += 2)
        canvas_draw_dot(canvas, i, 12);

    scene_transport_ambient(canvas, x + 1, 15, m->transport, m->now,
                            m->ambient && !m->starting && m->status != VaultHomeError);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, x + 30, 26, m->transport_label);

    if (m->starting) {
        canvas_draw_str(canvas, x + 1, 44, "Opening...");
    } else if (m->message[0]) {
        scene_icon(canvas, x + 1, 37, m->status == VaultHomeError ? VaultIconWarn : VaultIconInfo);
        vt_line(canvas, x + 10, 44, VG_W - x - 11, m->message, m->now, true);
    } else {
        snprintf(count, sizeof(count), "%u", (unsigned)m->count);
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str(canvas, x, 51, count);
        int32_t w = canvas_string_width(canvas, count) + 3;
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, x + w, 43, m->count > 9 ? "keys" : "passkeys");
        canvas_draw_str(canvas, x + w, 51, m->count ? "sealed" : "stored");
    }
    elements_button_right(canvas, "Settings");
}

static void home_draw_list(Canvas *canvas, const VaultHomeModel *m) {
    const int32_t top = 12;
    int32_t t = vg_ease_out(vg_progress(m->now, m->scroll_at, VAULT_SCROLL_MS));
    int32_t scroll = vg_lerp(m->scroll_from, m->scroll_to, t);
    char pos[12];

    for (uint8_t i = 0; i < m->window_count; ++i) {
        const VaultListItem *item = &m->items[i];
        int32_t index = (int32_t)m->window_start + i;
        int32_t y = top + index * VAULT_ROW_H - scroll;
        if (y >= VG_H || y + VAULT_ROW_H <= top || !item->valid)
            continue;
        bool sel = index == (int32_t)m->selected;
        canvas_set_color(canvas, ColorBlack);
        if (sel)
            canvas_draw_rbox(canvas, 0, y, 124, VAULT_ROW_H - 1, 2);
        canvas_set_color(canvas, sel ? ColorWhite : ColorBlack);
        vault_sigil_draw(canvas, 2, y + 1, 1, &item->seed);
        canvas_set_font(canvas, FontPrimary);
        vt_line(canvas, 21, y + 8, 100, item->title, m->now, sel);
        canvas_set_font(canvas, FontSecondary);
        vt_line(canvas, 21, y + 15, 100, item->subtitle, m->now, sel);
    }
    canvas_set_color(canvas, ColorBlack);
    if (m->count > 3)
        elements_scrollbar_pos(canvas, VG_W, top + 1, VG_H - top - 1, m->selected, m->count);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 0, 0, VG_W, top);
    snprintf(pos, sizeof(pos), "%u/%u", (unsigned)m->selected + 1U, (unsigned)m->count);
    scene_title_bar(canvas, VaultIconKey, "Passkeys", pos);
}

void scene_home_draw(Canvas *canvas, const VaultHomeModel *m) {
    if (m->list_mode && m->count > 0) {
        home_draw_list(canvas, m);
        return;
    }
    home_draw_door(canvas, m);
    if (!m->boot_at || (m->now - m->boot_at) >= 600U)
        home_draw_panel(canvas, m);
}
