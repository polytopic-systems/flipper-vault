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

#include "vault_door.h"

void vault_door_init(VaultDoor *door, int32_t cx, int32_t cy, int32_t r) {
    door->cx = cx;
    door->cy = cy;
    door->r = r;
    door->dial = 0;
    door->handle = 0;
    for (int i = 0; i < VAULT_DOOR_BOLTS; ++i) {
        door->bolt[i] = VG_ONE;
    }
    door->swing = 0;
    door->light = 0;
}

/*
 * One pass over the door's bounding box classifies every pixel: wall frame
 * (lit metal, grain darkening to the lower right), seam, interior (dark, or
 * lit from the centre), and the door plate. The plate is squashed towards
 * the left hinge as it swings (sx), with a dithered crescent of shade.
 */
static void door_shade(Canvas *canvas, const VaultDoor *d, int32_t plate, int32_t sx) {
    const int32_t r = d->r;
    const int32_t light_r = (d->light * (plate + 6)) / VG_ONE;
    const int32_t left = d->cx - plate;

    for (int32_t dy = -r; dy <= r; ++dy) {
        for (int32_t dx = -r; dx <= r; ++dx) {
            int32_t d2 = dx * dx + dy * dy;
            if (d2 > r * r + r)
                continue;
            int32_t x = d->cx + dx;
            int32_t y = d->cy + dy;
            int32_t level;
            if (d2 >= (r - 1) * (r - 1)) {
                level = 16; /* outer lip */
            } else if (d2 > (plate + 1) * (plate + 2)) {
                level = ((dx + dy) * 4) / r; /* frame band */
            } else if (d2 > plate * plate + plate) {
                level = 16; /* seam */
            } else {
                level = 14; /* interior */
                if (light_r > 0) {
                    int32_t edge = (light_r + 5) * (light_r + 5);
                    level = d2 < light_r * light_r ? 0
                            : d2 >= edge
                                ? 14
                                : (14 * (d2 - light_r * light_r)) / (edge - light_r * light_r);
                }
            }
            /* Door plate, inverse-mapped through the swing. */
            if (sx > 0 && x >= left && x <= left + (2 * plate * sx) / VG_ONE) {
                int32_t u = ((x - left) * VG_ONE) / sx - plate;
                int32_t p2 = u * u + dy * dy;
                if (p2 <= plate * plate + plate) {
                    level = ((u + dy) * 6) / plate - 3;
                    if (p2 >= (plate - 1) * (plate - 1) && (u + dy) > plate / 3)
                        level = 16;
                    level += ((VG_ONE - sx) * 8) / VG_ONE;
                }
            }
            vg_px(canvas, x, y, vg_bayer(x, y, level));
        }
    }
}

static void door_face(Canvas *canvas, const VaultDoor *d, int32_t plate) {
    const int32_t cx = d->cx;
    const int32_t cy = d->cy;
    const int32_t dial_r = plate * 9 / 20;
    const int32_t ring_r = plate * 7 / 10;

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_circle(canvas, cx, cy, ring_r);
    /* Spoke wheel with round grips. */
    for (int32_t s = 0; s < 3; ++s) {
        int32_t a = (d->handle + s * 341 + 170) / 4;
        int32_t gx, gy;
        vg_spoke(canvas, cx, cy, dial_r + 1, ring_r + 2, a);
        vg_spoke(canvas, cx + 1, cy, dial_r + 1, ring_r + 2, a);
        vg_polar(cx, cy, (ring_r + 3) * 16, a, &gx, &gy);
        canvas_draw_disc(canvas, gx, gy, 2);
        vg_px(canvas, gx - 1, gy - 1, false);
    }
    /* Dial: lit disc, rotating ticks, fixed index notch above. */
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_disc(canvas, cx, cy, dial_r);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_circle(canvas, cx, cy, dial_r);
    for (int32_t t = 0; t < 32; t += 2) {
        int32_t len = (t % 8 == 0) ? 4 : 2;
        vg_spoke(canvas, cx, cy, dial_r - 1, dial_r - len, (d->dial + t * 32) / 4);
    }
    canvas_draw_line(canvas, cx - 1, cy - dial_r - 3, cx + 1, cy - dial_r - 3);
    canvas_draw_dot(canvas, cx, cy - dial_r - 2);
    canvas_draw_disc(canvas, cx, cy, dial_r / 3 + 1);
    vg_px(canvas, cx - 1, cy - 1, false);
}

void vault_door_draw(Canvas *canvas, const VaultDoor *d) {
    const int32_t plate = d->r - 8;
    const int32_t sx = vg_cos((vg_clamp(d->swing, 0, VG_ONE) * 64) / VG_ONE);

    door_shade(canvas, d, plate, sx);
    if (sx >= VG_ONE)
        door_face(canvas, d, plate);
    /* Bolts: thrown into the frame band when locked, withdrawn into the plate. */
    canvas_set_color(canvas, ColorBlack);
    for (int32_t i = 0; i < VAULT_DOOR_BOLTS; ++i) {
        int32_t a = i * 32 + 16;
        int32_t x, y;
        vg_polar(d->cx, d->cy, (plate + 4) * 16, a, &x, &y);
        canvas_draw_dot(canvas, x, y); /* socket */
        if (sx < VG_ONE / 3)
            continue;
        int32_t out = vg_clamp(d->bolt[i], -256, VG_ONE + 256);
        vg_polar(d->cx, d->cy, (plate - 3) * 16 + (out * 7 * 16) / VG_ONE, a, &x, &y);
        canvas_draw_disc(canvas, x, y, 1);
        vg_px(canvas, x - 1, y - 1, false);
    }
    /* Hinges on the left. */
    for (int32_t s = -1; s <= 1; s += 2) {
        int32_t y = d->cy + s * (d->r / 2) - 3;
        canvas_draw_rbox(canvas, d->cx - d->r - 2, y, 5, 7, 1);
    }
}

void vault_dial_badge(Canvas *canvas, int32_t cx, int32_t cy, int32_t r, int32_t dial) {
    int32_t x, y;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_disc(canvas, cx, cy, r + 1);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_circle(canvas, cx, cy, r);
    for (int32_t t = 0; t < 4; ++t) {
        vg_polar(cx, cy, (r - 2) * 16, (dial + t * 256) / 4, &x, &y);
        canvas_draw_dot(canvas, x, y);
    }
    vg_spoke(canvas, cx, cy, 0, r - 2, dial / 4);
    canvas_draw_dot(canvas, cx, cy - r - 2);
}
