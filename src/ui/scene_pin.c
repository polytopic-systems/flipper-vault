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

/*
 * PIN tumbler: the same alphabet as the stock keyboard it replaces (0-9,
 * a-z, A-Z), entered on a combination drum. Locked-in characters are only
 * ever shown as bullets.
 */

const char *const vault_pin_sets[VAULT_PIN_SETS] = {
    "0123456789",
    "abcdefghijklmnopqrstuvwxyz",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
};

static const char *const pin_set_names[VAULT_PIN_SETS] = {"0-9", "a-z", "A-Z"};

#define PIN_SPIN_MS 110U
#define PIN_SET_MS 160U
#define PIN_COMMIT_MS 180U
#define PIN_ERROR_MS 2200U
#define DRUM_X 46
#define DRUM_W 36
#define DRUM_CX (DRUM_X + DRUM_W / 2)

uint8_t vault_pin_set_len(uint8_t set) {
    return set < VAULT_PIN_SETS ? (uint8_t)strlen(vault_pin_sets[set]) : 0U;
}

static char pin_char(uint8_t set, int32_t index) {
    int32_t len = vault_pin_set_len(set);
    if (len <= 0) return '?';
    index %= len;
    if (index < 0) index += len;
    return vault_pin_sets[set][index];
}

/*
 * Big glyph: draws the character with the primary font at its spot, lifts the
 * pixels, and re-draws them doubled. Clipped to rows [clip_top, clip_bottom].
 */
static void pin_big_char(Canvas *canvas, int32_t cx, int32_t baseline, char ch, int32_t clip_top,
                         int32_t clip_bottom) {
    char s[2] = {ch, '\0'};
    uint8_t cells[12][2] = {{0}};
    const int32_t gx = 104; /* scratch spot, redrawn later anyway */
    const int32_t gy = 2;

    canvas_set_font(canvas, FontPrimary);
    canvas_set_color(canvas, ColorBlack);
    int32_t w = canvas_string_width(canvas, s);
    vg_set_offset(0, 0);
    /* Lift: clear the scratch box, draw, read, clear again. */
    vg_fill(canvas, gx, gy, 12, 12, VgClear);
    canvas_draw_str(canvas, gx, gy + 9, s);
    for (int32_t y = 0; y < 12; ++y) {
        for (int32_t x = 0; x < 12; ++x) {
            if (vg_get(canvas, gx + x, gy + y)) cells[y][x / 8] |= (uint8_t)(1U << (x % 8));
        }
    }
    vg_fill(canvas, gx, gy, 12, 12, VgClear);
    int32_t left = cx - w;
    int32_t top = baseline - 18;
    for (int32_t y = 0; y < 12; ++y) {
        for (int32_t x = 0; x < 12; ++x) {
            if (!(cells[y][x / 8] & (1U << (x % 8)))) continue;
            for (int32_t dy = 0; dy < 2; ++dy) {
                int32_t py = top + y * 2 + dy;
                if (py < clip_top || py > clip_bottom) continue;
                vg_hline(canvas, left + x * 2, left + x * 2 + 1, py, VgSet);
            }
        }
    }
}

static void pin_draw_slot(Canvas *canvas, const VaultPinModel *m) {
    vg_rframe(canvas, 2, 13, 124, 11, VgSet);
    vg_hline(canvas, 3, 124, 14, VgSet); /* recess shadow */

    if (m->error[0] && (m->now - m->error_at) < PIN_ERROR_MS) {
        uint32_t e = m->now - m->error_at;
        int32_t shake = e < 300U ? ((e / 40U) % 2U ? 2 : -2) : 0;
        vg_set_offset(shake, 0);
        vg_rfill(canvas, 3, 15, 122, 8, VgSet);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontSecondary);
        vt_str_center(canvas, 64, 22, m->error);
        canvas_set_color(canvas, ColorBlack);
        vg_set_offset(0, 0);
        return;
    }

    /* Bullets, newest on the right; the last one drops in. */
    const int32_t pitch = 5;
    const int32_t max_visible = 22;
    int32_t first = m->length > max_visible ? m->length - max_visible : 0;
    for (int32_t i = first; i < m->length; ++i) {
        int32_t x = 7 + (i - first) * pitch;
        int32_t y = 19;
        if (i == m->length - 1 && (m->now - m->commit_at) < PIN_COMMIT_MS) {
            int32_t t = vg_ease_out_bounce(vg_progress(m->now, m->commit_at, PIN_COMMIT_MS));
            y = vg_lerp(13, 19, t);
        }
        vg_disc(canvas, x, y, 1, VgSet);
    }
    if (first > 0) {
        vg_px(canvas, 4, 18, VgSet);
        vg_px(canvas, 4, 20, VgSet);
    }
    /* Minimum length marks under the slot. */
    for (int32_t i = 0; i < m->min_length && i < max_visible; ++i) {
        int32_t x = 7 + i * pitch;
        if (i >= m->length) vg_px(canvas, x, 19, VgSet);
    }
}

static void pin_draw_drum(Canvas *canvas, const VaultPinModel *m) {
    const int32_t top = 26;
    const int32_t bottom = 63;
    const int32_t pitch = 21; /* drum row pitch in px */
    int32_t len = vault_pin_set_len(m->set);

    /* Drum housing: frame with rivets and a centre window. */
    vg_rframe(canvas, DRUM_X, top, DRUM_W, bottom - top + 1, VgSet);
    vg_dither(canvas, DRUM_X + 1, top + 1, DRUM_W - 2, 5, 5, VgSet);
    vg_dither(canvas, DRUM_X + 1, bottom - 5, DRUM_W - 2, 5, 5, VgSet);
    vg_px(canvas, DRUM_X + 2, 44, VgSet);
    vg_px(canvas, DRUM_X + DRUM_W - 3, 44, VgSet);
    /* Index arrows at the window. */
    for (int32_t i = 0; i < 3; ++i) {
        vg_vline(canvas, DRUM_X - 2 - i, 44 - i, 44 + i, VgSet);
        vg_vline(canvas, DRUM_X + DRUM_W + 1 + i, 44 - i, 44 + i, VgSet);
    }

    /* Spin offset: drum rotates from the previous index to the current one. */
    int32_t target = (int32_t)m->index * 256;
    int32_t from = m->spin_from;
    /* Take the short way round the drum. */
    int32_t span = len * 256;
    if (span > 0) {
        while (from - target > span / 2) from -= span;
        while (target - from > span / 2) from += span;
    }
    int32_t pos = vg_lerp(from, target, vg_ease_out_cubic(vg_progress(m->now, m->spin_at, PIN_SPIN_MS)));
    int32_t base = pos >= 0 ? pos / 256 : -((-pos + 255) / 256);
    int32_t frac = pos - base * 256; /* 0..255 towards base + 1 */

    /* Set switch: the whole drum face slides sideways. */
    int32_t slide = 0;
    if ((m->now - m->set_at) < PIN_SET_MS) {
        int32_t t = vg_ease_out_cubic(vg_progress(m->now, m->set_at, PIN_SET_MS));
        slide = ((VG_ONE - t) * 18 * m->set_dir) / VG_ONE;
    }
    vg_set_offset(slide, 0);
    for (int32_t k = -2; k <= 2; ++k) {
        int32_t index = base + k;
        int32_t baseline = 52 + k * pitch - (frac * pitch) / 256;
        int32_t dist = baseline - 52;
        if (dist < 0) dist = -dist;
        char ch = pin_char(m->set, index);
        if (dist < 12) {
            pin_big_char(canvas, DRUM_CX, baseline, ch, top + 6, bottom - 6);
        } else {
            char s[2] = {ch, '\0'};
            canvas_set_font(canvas, FontPrimary);
            int32_t y = baseline - (baseline > 52 ? 9 : -2);
            if (y - 8 >= top + 1 && y <= bottom - 1) vt_str_center(canvas, DRUM_CX, y, s);
        }
    }
    vg_set_offset(0, 0);
    /* Curvature: fade the drum towards its top and bottom. */
    vg_fade(canvas, DRUM_X + 1, top + 1, DRUM_W - 2, 7, 6);
    vg_fade(canvas, DRUM_X + 1, bottom - 7, DRUM_W - 2, 7, 6);
}

static void pin_draw_sets(Canvas *canvas, const VaultPinModel *m) {
    canvas_set_font(canvas, FontSecondary);
    for (uint8_t s = 0; s < VAULT_PIN_SETS; ++s) {
        int32_t y = 28 + s * 12;
        if (s == m->set) {
            vg_rfill(canvas, 3, y, 30, 11, VgSet);
            canvas_set_color(canvas, ColorWhite);
            vt_str_center(canvas, 18, y + 8, pin_set_names[s]);
            canvas_set_color(canvas, ColorBlack);
        } else {
            vg_rframe(canvas, 3, y, 30, 11, VgSet);
            vt_str_center(canvas, 18, y + 8, pin_set_names[s]);
        }
    }
    /* Left/right arrows to switch sets. */
    vg_px(canvas, 6, 62, VgSet);
    vg_vline(canvas, 7, 61, 63, VgSet);
    vg_px(canvas, 30, 62, VgSet);
    vg_vline(canvas, 29, 61, 63, VgSet);
    vt_str_center(canvas, 18, 64, "set");
}

static void pin_draw_hints(Canvas *canvas, const VaultPinModel *m) {
    const int32_t x = 88;
    canvas_set_font(canvas, FontSecondary);
    vg_rfill(canvas, x, 28, 14, 9, VgSet);
    canvas_set_color(canvas, ColorWhite);
    vt_str(canvas, x + 2, 35, "OK");
    canvas_set_color(canvas, ColorBlack);
    vt_str(canvas, x + 16, 35, "add");

    bool ready = m->length >= m->min_length;
    vg_rframe(canvas, x, 39, 14, 9, VgSet);
    vt_str(canvas, x + 2, 46, "OK");
    if (ready) {
        vt_str(canvas, x + 16, 46, "hold");
    } else {
        for (int32_t i = x + 16; i < 126; i += 2) vg_px(canvas, i, 43, VgSet);
    }

    vg_rframe(canvas, x, 50, 14, 9, VgSet);
    for (int32_t i = 0; i < 3; ++i) vg_vline(canvas, x + 4 + i, 54 - i, 54 + i, VgSet);
    vg_hline(canvas, x + 7, x + 10, 54, VgSet);
    vt_str(canvas, x + 16, 57, "del");
}

void scene_pin_draw(Canvas *canvas, const VaultPinModel *m) {
    char right[12];
    canvas_set_color(canvas, ColorBlack);
    pin_draw_drum(canvas, m);
    pin_draw_sets(canvas, m);
    pin_draw_hints(canvas, m);
    snprintf(right, sizeof(right), "%u/%u+", (unsigned)m->length, (unsigned)m->min_length);
    if (m->length >= m->min_length) snprintf(right, sizeof(right), "%u", (unsigned)m->length);
    vg_fill(canvas, 0, 0, VG_W, 13, VgClear);
    scene_title_bar(canvas, VaultIconLock, m->header, right);
    pin_draw_slot(canvas, m);
}
