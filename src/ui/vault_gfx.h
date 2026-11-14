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
 * Vault drawing kit, kept deliberately small (RAM is the hard limit: FAP code
 * lives in heap). Shapes use the firmware's canvas primitives, which cost no
 * FAP code; only dithering and per-pixel shading touch the frame buffer.
 * Everything here is pure drawing (no app state, clock or heap), so the same
 * files build into the host preview (tools/ui_preview).
 *
 * Fixed point: angles 0..255 per turn, sin/cos -1024..1024, easing 0..1024.
 * Callers keep coordinates on screen (u8g2 lines wrap negative values).
 */

#include <gui/canvas.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VG_W 128
#define VG_H 64
#define VG_ONE 1024

int32_t vg_sin(int32_t angle);
int32_t vg_cos(int32_t angle);
int32_t vg_lerp(int32_t a, int32_t b, int32_t t);
int32_t vg_clamp(int32_t v, int32_t lo, int32_t hi);
/* Progress of `now` through [start, start + duration] as 0..1024. */
int32_t vg_progress(uint32_t now, uint32_t start, uint32_t duration);
int32_t vg_ease_out(int32_t t);  /* cubic */
int32_t vg_ease_in(int32_t t);   /* cubic */
int32_t vg_ease_back(int32_t t); /* overshoot */

/* Ordered (Bayer 4x4) dither: level 0 = no ink, 16 = solid ink. */
bool vg_bayer(int32_t x, int32_t y, int32_t level);
/* Sets (ink) or clears one pixel straight in the frame buffer, clipped. */
void vg_px(Canvas *canvas, int32_t x, int32_t y, bool ink);
/* Dither a rectangle towards ink (ink=true) or towards background. */
void vg_dither(Canvas *canvas, int32_t x, int32_t y, int32_t w, int32_t h, int32_t level, bool ink);
/* Point at radius r (1/16 px) and angle (0 = up, clockwise) around (cx, cy). */
void vg_polar(int32_t cx, int32_t cy, int32_t r16, int32_t angle, int32_t *x, int32_t *y);
void vg_spoke(Canvas *canvas, int32_t cx, int32_t cy, int32_t r0, int32_t r1, int32_t angle);

/*
 * One text line limited to max_w px. With `scroll`, text that does not fit
 * steps through character by character (pausing at both ends); otherwise it
 * is cut. Nothing is drawn past max_w.
 */
void vt_line(Canvas *canvas, int32_t x, int32_t y, int32_t max_w, const char *text, uint32_t now,
             bool scroll);
/*
 * Fits a security-relevant label (RP ID, account) centred, never hiding
 * characters: primary font, else secondary, else two secondary lines split at
 * '.', '@', '-' or ' ', else a scrolling line. Returns lines used.
 */
uint8_t vt_fit_label(Canvas *canvas, int32_t cx, int32_t y, int32_t max_w, const char *text,
                     uint32_t now, bool allow_primary);
