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

#include "vault_gfx.h"

#include <string.h>

/* Quarter sine wave, 0..64 -> 0..1024. */
static const int16_t vg_sin_q[65] = {
    0,   25,  50,  75,  100, 125,  150,  175,  200,  224,  249,  273,  297,  321,  345, 369, 392,
    415, 438, 460, 483, 505, 526,  548,  569,  590,  610,  630,  650,  669,  688,  706, 724, 742,
    759, 775, 792, 807, 822, 837,  851,  865,  878,  891,  903,  915,  926,  936,  946, 955, 964,
    972, 980, 987, 993, 999, 1004, 1009, 1013, 1016, 1019, 1021, 1023, 1024, 1024,
};

static const uint8_t vg_bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

int32_t vg_sin(int32_t angle) {
    angle &= 255;
    int32_t q = angle & 63;
    int32_t v = (angle & 64) ? vg_sin_q[64 - q] : vg_sin_q[q];
    return angle & 128 ? -v : v;
}

int32_t vg_cos(int32_t angle) {
    return vg_sin(angle + 64);
}

int32_t vg_lerp(int32_t a, int32_t b, int32_t t) {
    return a + ((b - a) * t) / VG_ONE;
}

int32_t vg_clamp(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

int32_t vg_progress(uint32_t now, uint32_t start, uint32_t duration) {
    int32_t elapsed = (int32_t)(now - start);
    if (elapsed <= 0)
        return 0;
    if ((uint32_t)elapsed >= duration)
        return VG_ONE;
    return (int32_t)(((uint32_t)elapsed * VG_ONE) / duration);
}

int32_t vg_ease_in(int32_t t) {
    t = vg_clamp(t, 0, VG_ONE);
    return (t * t / VG_ONE) * t / VG_ONE;
}

int32_t vg_ease_out(int32_t t) {
    return VG_ONE - vg_ease_in(VG_ONE - t);
}

int32_t vg_ease_back(int32_t t) {
    /* 1 + 2.70158 u^3 + 1.70158 u^2, u = t - 1 */
    int32_t u = vg_clamp(t, 0, VG_ONE) - VG_ONE;
    int32_t u2 = u * u / VG_ONE;
    return VG_ONE + (u2 * u / VG_ONE * 2766) / VG_ONE + (u2 * 1742) / VG_ONE;
}

bool vg_bayer(int32_t x, int32_t y, int32_t level) {
    return vg_bayer4[((y & 3) << 2) | (x & 3)] < level;
}

void vg_px(Canvas *canvas, int32_t x, int32_t y, bool ink) {
    if ((uint32_t)x >= VG_W || (uint32_t)y >= VG_H)
        return;
    uint8_t *byte = &canvas_get_buffer(canvas)[x + (y >> 3) * VG_W];
    uint8_t bit = (uint8_t)(1U << (y & 7));
    *byte = ink ? (uint8_t)(*byte | bit) : (uint8_t)(*byte & ~bit);
}

void vg_dither(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, int32_t level,
               bool ink) {
    for (int32_t yy = y; yy < y + h; ++yy) {
        for (int32_t xx = x; xx < x + w; ++xx) {
            if (vg_bayer(xx, yy, level))
                vg_px(canvas, xx, yy, ink);
        }
    }
}

void vg_polar(int32_t cx, int32_t cy, int32_t r16, int32_t angle, int32_t *x, int32_t *y) {
    /* Rounded to the nearest pixel (+8 in 1/16 units, values stay positive here). */
    *x = cx + ((vg_sin(angle) * r16 / VG_ONE) + 8 + 1024) / 16 - 64;
    *y = cy + ((-vg_cos(angle) * r16 / VG_ONE) + 8 + 1024) / 16 - 64;
}

void vg_spoke(Canvas *canvas, int32_t cx, int32_t cy, int32_t r0, int32_t r1, int32_t angle) {
    int32_t x0, y0, x1, y1;
    vg_polar(cx, cy, r0 * 16, angle, &x0, &y0);
    vg_polar(cx, cy, r1 * 16, angle, &x1, &y1);
    canvas_draw_line(canvas, x0, y0, x1, y1);
}

/* ---- text ---- */

void vt_line(Canvas *canvas, int32_t x, int32_t y, int32_t max_w, const char *text, uint32_t now,
             bool scroll) {
    char buf[64];
    size_t len = strlen(text);
    size_t start = 0;

    if ((int32_t)canvas_string_width(canvas, text) > max_w && scroll) {
        /* Steps needed until the tail fits; 300 ms per step, 4 steps of pause each end. */
        size_t steps = 0;
        while (steps + 1 < len && (int32_t)canvas_string_width(canvas, text + steps) > max_w) {
            ++steps;
        }
        size_t cycle = 2U * (steps + 4U);
        size_t phase = (now / 300U) % cycle;
        start = phase < 4U           ? 0
                : phase < steps + 4U ? phase - 4U
                : phase < steps + 8U ? steps
                                     : cycle - phase;
        if (start > steps)
            start = steps;
    }
    /* Copy the longest prefix (from `start`) that fits max_w. */
    size_t n = 0;
    int32_t w = 0;
    while (start + n < len && n < sizeof(buf) - 1) {
        int32_t gw = (int32_t)canvas_glyph_width(canvas, (uint8_t)text[start + n]);
        if (w + gw > max_w + 1)
            break;
        w += gw;
        buf[n] = text[start + n];
        ++n;
    }
    buf[n] = '\0';
    canvas_draw_str(canvas, x, y, buf);
}

static void vt_center(Canvas *canvas, int32_t cx, int32_t y, const char *text) {
    canvas_draw_str_aligned(canvas, cx, y, AlignCenter, AlignBottom, text);
}

uint8_t vt_fit_label(Canvas *canvas, int32_t cx, int32_t y, int32_t max_w, const char *text,
                     uint32_t now, bool allow_primary) {
    char line[72];
    size_t len = strlen(text);

    canvas_set_font(canvas, allow_primary ? FontPrimary : FontSecondary);
    if ((int32_t)canvas_string_width(canvas, text) <= max_w) {
        vt_center(canvas, cx, y, text);
        return 1;
    }
    canvas_set_font(canvas, FontSecondary);
    if ((int32_t)canvas_string_width(canvas, text) <= max_w) {
        vt_center(canvas, cx, y, text);
        return 1;
    }
    if (len < sizeof(line)) {
        /* Split at the separator nearest the middle. */
        size_t split = len / 2;
        size_t best = len;
        for (size_t i = 1; i + 1 < len; ++i) {
            char c = text[i];
            if (c != '.' && c != '@' && c != '-' && c != ' ')
                continue;
            size_t d = i > len / 2 ? i - len / 2 : len / 2 - i;
            if (d < best) {
                best = d;
                split = c == ' ' ? i + 1 : i;
            }
        }
        memcpy(line, text, split);
        line[split] = '\0';
        if ((int32_t)canvas_string_width(canvas, line) <= max_w &&
            (int32_t)canvas_string_width(canvas, text + split) <= max_w) {
            vt_center(canvas, cx, y - 4, line);
            vt_center(canvas, cx, y + 5, text + split);
            return 2;
        }
    }
    vt_line(canvas, cx - max_w / 2, y, max_w, text, now, true);
    return 1;
}
