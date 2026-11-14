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

static int32_t vg_ox;
static int32_t vg_oy;
/* Snapshot used to clip text to a window (marquee). */
static uint8_t vg_snapshot[VG_W * VG_H / 8];

/* Quarter sine wave, 0..64 -> 0..1024. */
static const int16_t vg_sin_q[65] = {
    0,    25,   50,   75,   100,  125,  150,  175,  200,  224,  249,  273,  297,
    321,  345,  369,  392,  415,  438,  460,  483,  505,  526,  548,  569,  590,
    610,  630,  650,  669,  688,  706,  724,  742,  759,  775,  792,  807,  822,
    837,  851,  865,  878,  891,  903,  915,  926,  936,  946,  955,  964,  972,
    980,  987,  993,  999,  1004, 1009, 1013, 1016, 1019, 1021, 1023, 1024, 1024,
};

static const uint8_t vg_bayer4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

void vg_set_offset(int32_t dx, int32_t dy) {
    vg_ox = dx;
    vg_oy = dy;
}

void vg_get_offset(int32_t *dx, int32_t *dy) {
    if (dx) *dx = vg_ox;
    if (dy) *dy = vg_oy;
}

int32_t vg_sin(int32_t angle) {
    angle &= 255;
    if (angle < 64) return vg_sin_q[angle];
    if (angle < 128) return vg_sin_q[128 - angle];
    if (angle < 192) return -vg_sin_q[angle - 128];
    return -vg_sin_q[256 - angle];
}

int32_t vg_cos(int32_t angle) {
    return vg_sin(angle + 64);
}

int32_t vg_lerp(int32_t a, int32_t b, int32_t t) {
    return a + (((b - a) * t) / VG_ONE);
}

int32_t vg_clamp(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

int32_t vg_progress(uint32_t now, uint32_t start, uint32_t duration) {
    int32_t elapsed = (int32_t)(now - start);
    if (duration == 0U) return VG_ONE;
    if (elapsed <= 0) return 0;
    if ((uint32_t)elapsed >= duration) return VG_ONE;
    return (int32_t)(((uint32_t)elapsed * VG_ONE) / duration);
}

int32_t vg_ease_out_cubic(int32_t t) {
    int32_t inv = VG_ONE - vg_clamp(t, 0, VG_ONE);
    return VG_ONE - (inv * inv / VG_ONE) * inv / VG_ONE;
}

int32_t vg_ease_in_cubic(int32_t t) {
    t = vg_clamp(t, 0, VG_ONE);
    return (t * t / VG_ONE) * t / VG_ONE;
}

int32_t vg_ease_in_out(int32_t t) {
    t = vg_clamp(t, 0, VG_ONE);
    if (t < VG_ONE / 2) return vg_ease_in_cubic(t * 2) / 2;
    return VG_ONE / 2 + vg_ease_out_cubic(t * 2 - VG_ONE) / 2;
}

int32_t vg_ease_out_back(int32_t t) {
    /* 1 + c3 (t-1)^3 + c1 (t-1)^2 with c1 = 1.70158 */
    int32_t u = vg_clamp(t, 0, VG_ONE) - VG_ONE;
    int32_t u2 = u * u / VG_ONE;
    int32_t u3 = u2 * u / VG_ONE;
    return VG_ONE + (u3 * 2702) / VG_ONE + (u2 * 1742) / VG_ONE;
}

int32_t vg_ease_out_bounce(int32_t t) {
    /* Piecewise parabola bounce, 7.5625 * x^2 segments in fixed point. */
    t = vg_clamp(t, 0, VG_ONE);
    const int32_t k = 7744; /* 7.5625 * 1024 */
    if (t < 372) return (k * t / VG_ONE) * t / VG_ONE;
    if (t < 745) {
        t -= 559;
        return (k * t / VG_ONE) * t / VG_ONE + 768;
    }
    if (t < 931) {
        t -= 838;
        return (k * t / VG_ONE) * t / VG_ONE + 960;
    }
    t -= 977;
    return (k * t / VG_ONE) * t / VG_ONE + 1008;
}

uint32_t vg_hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline uint8_t *vg_buf(Canvas *canvas) {
    return canvas_get_buffer(canvas);
}

static inline void vg_raw_px(uint8_t *fb, int32_t x, int32_t y, VgMode mode) {
    if ((uint32_t)x >= VG_W || (uint32_t)y >= VG_H) return;
    uint8_t *byte = &fb[x + (y >> 3) * VG_W];
    uint8_t bit = (uint8_t)(1U << (y & 7));
    if (mode == VgSet) {
        *byte |= bit;
    } else if (mode == VgClear) {
        *byte &= (uint8_t)~bit;
    } else {
        *byte ^= bit;
    }
}

void vg_px(Canvas *canvas, int32_t x, int32_t y, VgMode mode) {
    vg_raw_px(vg_buf(canvas), x + vg_ox, y + vg_oy, mode);
}

bool vg_get(Canvas *canvas, int32_t x, int32_t y) {
    x += vg_ox;
    y += vg_oy;
    if ((uint32_t)x >= VG_W || (uint32_t)y >= VG_H) return false;
    return (vg_buf(canvas)[x + (y >> 3) * VG_W] >> (y & 7)) & 1U;
}

void vg_hline(Canvas *canvas, int32_t x0, int32_t x1, int32_t y, VgMode mode) {
    uint8_t *fb = vg_buf(canvas);
    if (x0 > x1) {
        int32_t t = x0;
        x0 = x1;
        x1 = t;
    }
    x0 += vg_ox;
    x1 += vg_ox;
    y += vg_oy;
    if ((uint32_t)y >= VG_H) return;
    x0 = vg_clamp(x0, 0, VG_W - 1);
    x1 = vg_clamp(x1, -1, VG_W - 1);
    for (int32_t x = x0; x <= x1; ++x) {
        vg_raw_px(fb, x, y, mode);
    }
}

void vg_vline(Canvas *canvas, int32_t x, int32_t y0, int32_t y1, VgMode mode) {
    uint8_t *fb = vg_buf(canvas);
    if (y0 > y1) {
        int32_t t = y0;
        y0 = y1;
        y1 = t;
    }
    x += vg_ox;
    y0 += vg_oy;
    y1 += vg_oy;
    if ((uint32_t)x >= VG_W) return;
    y0 = vg_clamp(y0, 0, VG_H - 1);
    y1 = vg_clamp(y1, -1, VG_H - 1);
    for (int32_t y = y0; y <= y1; ++y) {
        vg_raw_px(fb, x, y, mode);
    }
}

void vg_line(Canvas *canvas, int32_t x0, int32_t y0, int32_t x1, int32_t y1, VgMode mode) {
    uint8_t *fb = vg_buf(canvas);
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;

    for (int32_t guard = 0; guard < 512; ++guard) {
        vg_raw_px(fb, x0 + vg_ox, y0 + vg_oy, mode);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void vg_thick_line(Canvas *canvas, int32_t x0, int32_t y0, int32_t x1, int32_t y1, VgMode mode) {
    int32_t dx = x1 - x0;
    int32_t dy = y1 - y0;
    if ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)) {
        vg_line(canvas, x0, y0, x1, y1, mode);
        vg_line(canvas, x0, y0 + 1, x1, y1 + 1, mode);
    } else {
        vg_line(canvas, x0, y0, x1, y1, mode);
        vg_line(canvas, x0 + 1, y0, x1 + 1, y1, mode);
    }
}

void vg_fill(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, VgMode mode) {
    for (int32_t row = 0; row < h; ++row) {
        vg_hline(canvas, x, x + w - 1, y + row, mode);
    }
}

void vg_frame(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, VgMode mode) {
    if (w <= 0 || h <= 0) return;
    vg_hline(canvas, x, x + w - 1, y, mode);
    if (h > 1) vg_hline(canvas, x, x + w - 1, y + h - 1, mode);
    if (h > 2) {
        vg_vline(canvas, x, y + 1, y + h - 2, mode);
        if (w > 1) vg_vline(canvas, x + w - 1, y + 1, y + h - 2, mode);
    }
}

void vg_rfill(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, VgMode mode) {
    if (w < 3 || h < 3) {
        vg_fill(canvas, x, y, w, h, mode);
        return;
    }
    vg_hline(canvas, x + 1, x + w - 2, y, mode);
    vg_fill(canvas, x, y + 1, w, h - 2, mode);
    vg_hline(canvas, x + 1, x + w - 2, y + h - 1, mode);
}

void vg_rframe(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, VgMode mode) {
    if (w < 3 || h < 3) {
        vg_frame(canvas, x, y, w, h, mode);
        return;
    }
    vg_hline(canvas, x + 1, x + w - 2, y, mode);
    vg_hline(canvas, x + 1, x + w - 2, y + h - 1, mode);
    vg_vline(canvas, x, y + 1, y + h - 2, mode);
    vg_vline(canvas, x + w - 1, y + 1, y + h - 2, mode);
}

bool vg_bayer(int32_t x, int32_t y, int32_t level) {
    return vg_bayer4[y & 3][x & 3] < level;
}

void vg_dither(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, int32_t level,
               VgMode mode) {
    uint8_t *fb = vg_buf(canvas);
    for (int32_t yy = y; yy < y + h; ++yy) {
        for (int32_t xx = x; xx < x + w; ++xx) {
            if (vg_bayer(xx, yy, level)) vg_raw_px(fb, xx + vg_ox, yy + vg_oy, mode);
        }
    }
}

void vg_circle(Canvas *canvas, int32_t cx, int32_t cy, int32_t r, VgMode mode) {
    uint8_t *fb = vg_buf(canvas);
    int32_t x = r;
    int32_t y = 0;
    int32_t err = 1 - r;
    cx += vg_ox;
    cy += vg_oy;
    if (r <= 0) {
        vg_raw_px(fb, cx, cy, mode);
        return;
    }
    while (x >= y) {
        /* Plot each distinct point once so XOR stays clean. */
        int32_t pts[8][2] = {{cx + x, cy + y}, {cx - x, cy + y}, {cx + x, cy - y},
                             {cx - x, cy - y}, {cx + y, cy + x}, {cx - y, cy + x},
                             {cx + y, cy - x}, {cx - y, cy - x}};
        for (int i = 0; i < 8; ++i) {
            bool dup = false;
            for (int j = 0; j < i; ++j) {
                if (pts[j][0] == pts[i][0] && pts[j][1] == pts[i][1]) {
                    dup = true;
                    break;
                }
            }
            if (!dup) vg_raw_px(fb, pts[i][0], pts[i][1], mode);
        }
        ++y;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            --x;
            err += 2 * (y - x) + 1;
        }
    }
}

/* Half-width of a disc of radius r at row offset dy (r*r compared with +r for roundness). */
static int32_t vg_disc_span(int32_t r, int32_t dy) {
    int32_t limit = r * r + r;
    int32_t w = r;
    while (w > 0 && w * w + dy * dy > limit) --w;
    return w;
}

void vg_disc(Canvas *canvas, int32_t cx, int32_t cy, int32_t r, VgMode mode) {
    if (r <= 0) {
        vg_px(canvas, cx, cy, mode);
        return;
    }
    for (int32_t dy = -r; dy <= r; ++dy) {
        int32_t w = vg_disc_span(r, dy);
        vg_hline(canvas, cx - w, cx + w, cy + dy, mode);
    }
}

void vg_disc_dither(Canvas *canvas, int32_t cx, int32_t cy, int32_t r, int32_t level,
                    VgMode mode) {
    uint8_t *fb = vg_buf(canvas);
    for (int32_t dy = -r; dy <= r; ++dy) {
        int32_t w = vg_disc_span(r, dy);
        for (int32_t dx = -w; dx <= w; ++dx) {
            int32_t x = cx + dx;
            int32_t y = cy + dy;
            if (vg_bayer(x, y, level)) vg_raw_px(fb, x + vg_ox, y + vg_oy, mode);
        }
    }
}

void vg_polar(int32_t cx, int32_t cy, int32_t r16, int32_t angle, int32_t *x, int32_t *y) {
    /* angle 0 points up, clockwise. Round to nearest pixel. */
    int32_t px = (vg_sin(angle) * r16) / 16;
    int32_t py = -(vg_cos(angle) * r16) / 16;
    *x = cx + (px >= 0 ? (px + 512) / VG_ONE : -((-px + 512) / VG_ONE));
    *y = cy + (py >= 0 ? (py + 512) / VG_ONE : -((-py + 512) / VG_ONE));
}

void vg_xbm(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *bits,
            VgMode mode) {
    uint8_t *fb = vg_buf(canvas);
    int32_t stride = (w + 7) / 8;
    for (int32_t row = 0; row < h; ++row) {
        for (int32_t col = 0; col < w; ++col) {
            if (bits[row * stride + col / 8] & (1U << (col & 7))) {
                vg_raw_px(fb, x + col + vg_ox, y + row + vg_oy, mode);
            }
        }
    }
}

void vg_invert(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h) {
    vg_fill(canvas, x, y, w, h, VgXor);
}

void vg_fade(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, int32_t level) {
    if (level <= 0) return;
    vg_dither(canvas, x, y, w, h, level, VgClear);
}

/* ---- text ---- */

void vt_str(Canvas *canvas, int32_t x, int32_t y, const char *text) {
    canvas_draw_str(canvas, x + vg_ox, y + vg_oy, text);
}

void vt_str_center(Canvas *canvas, int32_t cx, int32_t y, const char *text) {
    int32_t w = canvas_string_width(canvas, text);
    canvas_draw_str(canvas, cx - w / 2 + vg_ox, y + vg_oy, text);
}

void vt_str_right(Canvas *canvas, int32_t right, int32_t y, const char *text) {
    int32_t w = canvas_string_width(canvas, text);
    canvas_draw_str(canvas, right - w + vg_ox, y + vg_oy, text);
}

static int32_t vt_prefix_width(Canvas *canvas, const char *text, size_t len) {
    int32_t w = 0;
    for (size_t i = 0; i < len && text[i]; ++i) {
        w += (int32_t)canvas_glyph_width(canvas, (uint8_t)text[i]);
    }
    return w;
}

void vt_str_clip(Canvas *canvas, int32_t x, int32_t y, int32_t max_w, const char *text) {
    char buf[72];
    size_t len = strlen(text);

    if ((int32_t)canvas_string_width(canvas, text) <= max_w) {
        vt_str(canvas, x, y, text);
        return;
    }
    int32_t dots = vt_prefix_width(canvas, "...", 3);
    size_t keep = len < sizeof(buf) - 4 ? len : sizeof(buf) - 4;
    while (keep > 0 && vt_prefix_width(canvas, text, keep) + dots > max_w) {
        --keep;
    }
    memcpy(buf, text, keep);
    memcpy(buf + keep, "...", 4);
    vt_str(canvas, x, y, buf);
}

/*
 * Draws text clipped to the column window [x, x + max_w): the screen is
 * snapshotted, the string drawn, and pixels outside the window restored.
 */
static void vt_str_window(Canvas *canvas, int32_t x, int32_t y, int32_t win_x, int32_t win_w,
                          const char *text) {
    uint8_t *fb = vg_buf(canvas);
    int32_t left = win_x + vg_ox;
    int32_t right = win_x + win_w + vg_ox;

    memcpy(vg_snapshot, fb, sizeof(vg_snapshot));
    canvas_draw_str(canvas, x + vg_ox, y + vg_oy, text);
    for (int32_t page = 0; page < VG_H / 8; ++page) {
        for (int32_t col = 0; col < VG_W; ++col) {
            if (col < left || col >= right) {
                fb[page * VG_W + col] = vg_snapshot[page * VG_W + col];
            }
        }
    }
}

void vt_str_marquee(Canvas *canvas, int32_t x, int32_t y, int32_t max_w, const char *text,
                    uint32_t now) {
    int32_t w = canvas_string_width(canvas, text);
    if (w <= max_w) {
        vt_str(canvas, x, y, text);
        return;
    }
    /* 25 px/s, 1.2 s pause at both ends. */
    int32_t travel = w - max_w;
    uint32_t travel_ms = (uint32_t)travel * 40U;
    uint32_t period = 2U * (1200U + travel_ms);
    uint32_t phase = now % period;
    int32_t shift;
    if (phase < 1200U) {
        shift = 0;
    } else if (phase < 1200U + travel_ms) {
        shift = (int32_t)((phase - 1200U) / 40U);
    } else if (phase < 2400U + travel_ms) {
        shift = travel;
    } else {
        shift = travel - (int32_t)((phase - 2400U - travel_ms) / 40U);
    }
    vt_str_window(canvas, x - shift, y, x, max_w, text);
}

static int32_t vt_split_point(const char *text, size_t len) {
    /* Prefer a separator near the middle so both halves stay balanced. */
    int32_t best = -1;
    int32_t best_score = 1 << 30;
    for (size_t i = 1; i + 1 < len; ++i) {
        char c = text[i];
        if (c == '.' || c == '@' || c == '-' || c == ' ' || c == '_' || c == '/') {
            int32_t score = (int32_t)i - (int32_t)len / 2;
            if (score < 0) score = -score;
            if (score < best_score) {
                best_score = score;
                best = (int32_t)(c == ' ' ? i + 1 : i);
            }
        }
    }
    return best > 0 ? best : (int32_t)(len / 2);
}

uint8_t vt_fit_label(Canvas *canvas, int32_t cx, int32_t y, int32_t max_w, const char *text,
                     uint32_t now, bool allow_primary) {
    char line[132];
    size_t len = strlen(text);

    if (allow_primary) {
        canvas_set_font(canvas, FontPrimary);
        if ((int32_t)canvas_string_width(canvas, text) <= max_w) {
            vt_str_center(canvas, cx, y, text);
            return 1;
        }
    }
    canvas_set_font(canvas, FontSecondary);
    if ((int32_t)canvas_string_width(canvas, text) <= max_w) {
        vt_str_center(canvas, cx, y, text);
        return 1;
    }
    if (len < sizeof(line)) {
        int32_t split = vt_split_point(text, len);
        memcpy(line, text, (size_t)split);
        line[split] = '\0';
        const char *rest = text + split;
        if ((int32_t)canvas_string_width(canvas, line) <= max_w &&
            (int32_t)canvas_string_width(canvas, rest) <= max_w) {
            vt_str_center(canvas, cx, y - 4, line);
            vt_str_center(canvas, cx, y + 5, rest);
            return 2;
        }
    }
    vt_str_marquee(canvas, cx - max_w / 2, y, max_w, text, now);
    return 1;
}
