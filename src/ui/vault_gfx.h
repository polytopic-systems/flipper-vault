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

#pragma once

/*
 * Vault drawing kit. Everything here is pure drawing: it touches only the
 * canvas (and its 1 KB frame buffer), never app state, the clock or the heap,
 * so the same files compile into the host preview tool (tools/ui_preview).
 *
 * Geometry is drawn straight into the u8g2 frame buffer with our own clipping
 * (u8g2 lines wrap negative coordinates). Maths is integer/fixed point:
 * angles are 0..255 per turn, sin/cos return -1024..1024, easing takes and
 * returns 0..1024.
 */

#include <gui/canvas.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VG_W 128
#define VG_H 64
#define VG_ONE 1024

typedef enum {
    VgClear = 0, /* background (lit) */
    VgSet = 1, /* ink */
    VgXor = 2,
} VgMode;

/* Camera offset applied to every vg_* and vt_* call (screen shake, slides). */
void vg_set_offset(int32_t dx, int32_t dy);
void vg_get_offset(int32_t *dx, int32_t *dy);

/* Fixed point helpers. */
int32_t vg_sin(int32_t angle);
int32_t vg_cos(int32_t angle);
int32_t vg_lerp(int32_t a, int32_t b, int32_t t);
int32_t vg_clamp(int32_t v, int32_t lo, int32_t hi);
/* Progress of `now` through [start, start + duration] as 0..1024. */
int32_t vg_progress(uint32_t now, uint32_t start, uint32_t duration);
int32_t vg_ease_out_cubic(int32_t t);
int32_t vg_ease_in_cubic(int32_t t);
int32_t vg_ease_in_out(int32_t t);
int32_t vg_ease_out_back(int32_t t);
int32_t vg_ease_out_bounce(int32_t t);
uint32_t vg_hash32(uint32_t x);

/* Pixel primitives (clipped, offset by the camera). */
void vg_px(Canvas *canvas, int32_t x, int32_t y, VgMode mode);
bool vg_get(Canvas *canvas, int32_t x, int32_t y);
void vg_hline(Canvas *canvas, int32_t x0, int32_t x1, int32_t y, VgMode mode);
void vg_vline(Canvas *canvas, int32_t x, int32_t y0, int32_t y1, VgMode mode);
void vg_line(Canvas *canvas, int32_t x0, int32_t y0, int32_t x1, int32_t y1, VgMode mode);
void vg_thick_line(Canvas *canvas, int32_t x0, int32_t y0, int32_t x1, int32_t y1, VgMode mode);
void vg_fill(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, VgMode mode);
void vg_frame(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, VgMode mode);
void vg_rfill(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, VgMode mode);
void vg_rframe(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, VgMode mode);
/* Ordered (Bayer 4x4) dither: level 0 = no ink, 16 = solid ink. */
bool vg_bayer(int32_t x, int32_t y, int32_t level);
void vg_dither(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, int32_t level,
               VgMode mode);
void vg_circle(Canvas *canvas, int32_t cx, int32_t cy, int32_t r, VgMode mode);
void vg_disc(Canvas *canvas, int32_t cx, int32_t cy, int32_t r, VgMode mode);
void vg_disc_dither(Canvas *canvas, int32_t cx, int32_t cy, int32_t r, int32_t level,
                    VgMode mode);
/* Point at radius r (1/16 px units) and angle around (cx, cy). */
void vg_polar(int32_t cx, int32_t cy, int32_t r16, int32_t angle, int32_t *x, int32_t *y);
/* 1-bit XBM (LSB = leftmost), ink bits only. */
void vg_xbm(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *bits,
            VgMode mode);
/* Whole-screen effects. */
void vg_invert(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h);
/* Dissolve the screen towards background: level 0 keeps everything, 16 clears. */
void vg_fade(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, int32_t level);

/* Text helpers (offset by the camera; colors via canvas_set_color). */
void vt_str(Canvas *canvas, int32_t x, int32_t y, const char *text);
void vt_str_center(Canvas *canvas, int32_t cx, int32_t y, const char *text);
void vt_str_right(Canvas *canvas, int32_t right, int32_t y, const char *text);
/* Width-limited single line: draws a prefix that fits and an ellipsis. */
void vt_str_clip(Canvas *canvas, int32_t x, int32_t y, int32_t max_w, const char *text);
/* Marquee: scrolls text that does not fit, pausing at both ends. */
void vt_str_marquee(Canvas *canvas, int32_t x, int32_t y, int32_t max_w, const char *text,
                    uint32_t now);
/*
 * Fits a security-relevant label (RP ID, account) in at most two centered
 * lines without ever hiding characters: tries the primary font, then the
 * secondary font, then two secondary lines split at a '.', '@', '-' or space,
 * then a marquee. Returns the number of lines used.
 */
uint8_t vt_fit_label(Canvas *canvas, int32_t cx, int32_t y, int32_t max_w, const char *text,
                     uint32_t now, bool allow_primary);
