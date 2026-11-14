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

#include <stdio.h>
#include <string.h>

#define HOME_DOOR_X 34
#define HOME_DOOR_Y 32
#define HOME_DOOR_R 30
#define HOME_PANEL_X 70
#define HOME_BOOT_MS 1100U
#define HOME_SCROLL_MS 140U

static uint32_t home_ambient_now(const VaultHomeModel *m) {
    if (m->ambient_until && (int32_t)(m->now - m->ambient_until) > 0) return m->ambient_until;
    return m->now;
}

static void home_draw_door(Canvas *canvas, const VaultHomeModel *m) {
    VaultDoor door;
    uint32_t t = home_ambient_now(m);
    vault_door_init(&door, HOME_DOOR_X, HOME_DOOR_Y, HOME_DOOR_R);
    /* Idle: the dial creeps round; while loading or waiting it hunts back and forth. */
    door.dial = (int32_t)(t / 32U);
    if (m->starting || m->status == VaultHomeWaiting) {
        door.dial = vg_sin((int32_t)(t / 6U)) / 6;
    }
    door.handle = 0;

    if (m->boot_at && (m->now - m->boot_at) < HOME_BOOT_MS) {
        uint32_t e = m->now - m->boot_at;
        /* Door swings shut (accelerating), bolts shoot home in sequence, dial settles. */
        door.swing = VG_ONE - vg_ease_in_cubic(vg_progress(m->now, m->boot_at, 420));
        for (int i = 0; i < VAULT_DOOR_BOLTS; ++i) {
            uint32_t start = m->boot_at + 430U + (uint32_t)i * 35U;
            door.bolt[i] = vg_ease_out_back(vg_progress(m->now, start, 160));
        }
        door.dial = -VG_ONE + vg_ease_out_cubic(vg_progress(m->now, m->boot_at + 700U, 400)) *
                                  1 + (int32_t)(t / 32U);
        if (e >= 420U && e < 600U) {
            int32_t k = (int32_t)(600U - e) / 40;
            vg_set_offset((e / 30U) % 2 ? k : -k, 0);
        }
    }
    vault_door_draw(canvas, &door);
    vg_set_offset(0, 0);
}

static void home_draw_led(Canvas *canvas, const VaultHomeModel *m, int32_t x, int32_t y) {
    switch (m->status) {
    case VaultHomeConnected:
        vg_disc(canvas, x, y, 2, VgSet);
        break;
    case VaultHomeWaiting:
        vg_circle(canvas, x, y, 2, VgSet);
        if ((m->now / 300U) % 2U) vg_px(canvas, x, y, VgSet);
        break;
    case VaultHomeError:
        vg_line(canvas, x - 2, y - 2, x + 2, y + 2, VgSet);
        vg_line(canvas, x - 2, y + 2, x + 2, y - 2, VgSet);
        break;
    case VaultHomeReady:
    default:
        vg_circle(canvas, x, y, 2, VgSet);
        break;
    }
}

/* Word-wraps `text` into up to `max_lines` lines of `max_w` px, top baseline y. */
static void home_wrap(Canvas *canvas, int32_t x, int32_t y, int32_t max_w, const char *text,
                      int32_t max_lines) {
    char line[48];
    const char *p = text;
    for (int32_t n = 0; n < max_lines && *p; ++n) {
        size_t len = 0;
        size_t fit = 0;
        while (p[len] && len < sizeof(line) - 1) {
            line[len] = p[len];
            line[len + 1] = '\0';
            if ((int32_t)canvas_string_width(canvas, line) > max_w) break;
            ++len;
            if (p[len] == ' ' || p[len] == '\0') fit = len;
        }
        if (fit == 0 || n == max_lines - 1) fit = len;
        memcpy(line, p, fit);
        line[fit] = '\0';
        vt_str(canvas, x, y + n * 9, line);
        p += fit;
        while (*p == ' ') ++p;
    }
}

static void home_draw_panel(Canvas *canvas, const VaultHomeModel *m) {
    const int32_t x = HOME_PANEL_X;
    char count[8];

    canvas_set_font(canvas, FontPrimary);
    vt_str(canvas, x + 1, 9, "VAULT");
    home_draw_led(canvas, m, 123, 5);
    for (int32_t i = x; i < 128; i += 2) vg_px(canvas, i, 12, VgSet);

    /* Transport badge with its ambient animation. */
    bool active = !m->starting && m->status != VaultHomeError &&
                  (!m->ambient_until || (int32_t)(m->now - m->ambient_until) <= 0);
    scene_transport_ambient(canvas, x + 1, 15, m->transport, m->now, active);
    canvas_set_font(canvas, FontSecondary);
    vt_str(canvas, x + 30, 26, m->transport_label);

    if (m->starting) {
        canvas_set_font(canvas, FontSecondary);
        static const char *const dots[] = {"Opening", "Opening.", "Opening..", "Opening..."};
        vt_str(canvas, x + 1, 44, dots[(m->now / 250U) % 4U]);
    } else if (m->message[0]) {
        canvas_set_font(canvas, FontSecondary);
        if (m->status == VaultHomeError) {
            scene_icon(canvas, x + 1, 34, VaultIconWarn, VgSet);
            home_wrap(canvas, x + 10, 40, 127 - x - 10, m->message, 2);
        } else {
            home_wrap(canvas, x + 1, 40, 127 - x - 1, m->message, 2);
        }
    } else {
        snprintf(count, sizeof(count), "%u", (unsigned)m->count);
        canvas_set_font(canvas, FontBigNumbers);
        vt_str(canvas, x, 51, count);
        int32_t w = canvas_string_width(canvas, count);
        canvas_set_font(canvas, FontSecondary);
        vt_str(canvas, x + w + 3, 43, m->count == 1 ? "passkey" : "passkeys");
        vt_str(canvas, x + w + 3, 51, m->count == 0 ? "stored" : "sealed");
    }

    if (m->count > 0 && !m->starting) {
        scene_hint(canvas, x, "OK", "open", false);
    }
    int32_t w = scene_hint(canvas, 127, ">", "", true);
    scene_icon(canvas, 127 - w - 9, 55, VaultIconGear, VgSet);
}

static int32_t home_anim(const VaultHomeModel *m, int32_t from, int32_t to) {
    int32_t t = vg_ease_out_cubic(vg_progress(m->now, m->scroll_at, HOME_SCROLL_MS));
    return vg_lerp(from, to, t);
}

static void home_draw_list(Canvas *canvas, const VaultHomeModel *m) {
    const int32_t top = 12;
    const int32_t scroll = home_anim(m, m->scroll_from, m->scroll_to);
    const int32_t cursor = home_anim(m, m->cursor_from, m->cursor_to);
    char pos[12];

    /* Highlight first so rows draw over it in inverse. */
    int32_t hy = top + cursor - scroll;
    vg_rfill(canvas, 0, hy, 124, VAULT_ROW_H - 1, VgSet);

    for (uint8_t i = 0; i < m->window_count; ++i) {
        const VaultListItem *item = &m->items[i];
        int32_t index = (int32_t)m->window_start + i;
        int32_t y = top + index * VAULT_ROW_H - scroll;
        if (y > VG_H || y + VAULT_ROW_H < top || !item->valid) continue;
        bool selected = index == (int32_t)m->selected;
        VgMode ink = selected ? VgClear : VgSet;
        vault_sigil_draw(canvas, 2, y + 1, 1, &item->seed, ink);
        canvas_set_color(canvas, selected ? ColorWhite : ColorBlack);
        canvas_set_font(canvas, FontPrimary);
        if (selected) {
            vt_str_marquee(canvas, 21, y + 8, 100, item->title, m->now);
        } else {
            vt_str_clip(canvas, 21, y + 8, 100, item->title);
        }
        canvas_set_font(canvas, FontSecondary);
        if (selected) {
            vt_str_marquee(canvas, 21, y + 15, 100, item->subtitle, m->now);
        } else {
            vt_str_clip(canvas, 21, y + 15, 100, item->subtitle);
        }
        canvas_set_color(canvas, ColorBlack);
        if (!selected && index + 1 < (int32_t)m->count) {
            for (int32_t dx = 21; dx < 122; dx += 3) vg_px(canvas, dx, y + VAULT_ROW_H - 1, VgSet);
        }
    }

    /* Scrollbar: rail of dots and a thumb. */
    if (m->count > 3) {
        for (int32_t y = top + 1; y < VG_H; y += 2) vg_px(canvas, 126, y, VgSet);
        int32_t track = VG_H - top - 2;
        int32_t thumb = vg_clamp(track * 3 / (int32_t)m->count, 4, track);
        int32_t ty = top + 1 + ((track - thumb) * scroll) /
                                   vg_clamp(((int32_t)m->count - 3) * VAULT_ROW_H, 1, 1 << 20);
        vg_fill(canvas, 125, ty, 3, thumb, VgSet);
    }

    snprintf(pos, sizeof(pos), "%u/%u", (unsigned)m->selected + 1U, (unsigned)m->count);
    vg_fill(canvas, 0, 0, VG_W, top, VgClear);
    scene_title_bar(canvas, VaultIconKey, "Passkeys", pos);
}

void scene_home_draw(Canvas *canvas, const VaultHomeModel *m) {
    canvas_set_color(canvas, ColorBlack);
    if (m->list_mode && m->count > 0) {
        home_draw_list(canvas, m);
        return;
    }
    home_draw_door(canvas, m);
    if (m->boot_at && (m->now - m->boot_at) < HOME_BOOT_MS) {
        /* Panel slides in once the bolts are home. */
        int32_t t = vg_ease_out_cubic(vg_progress(m->now, m->boot_at + 650U, 350));
        vg_set_offset((VG_ONE - t) * 60 / VG_ONE, 0);
        if (t > 0) home_draw_panel(canvas, m);
        vg_set_offset(0, 0);
        return;
    }
    home_draw_panel(canvas, m);
}

bool scene_home_busy(const VaultHomeModel *m) {
    if (m->boot_at && (m->now - m->boot_at) < HOME_BOOT_MS) return true;
    if ((m->now - m->scroll_at) < HOME_SCROLL_MS) return true;
    return false;
}
