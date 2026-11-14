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

typedef struct {
    int32_t cx;
    int32_t cy;
    int32_t plate; /* door plate radius */
    int32_t sx; /* horizontal scale from the swing, 0..1024 */
} DoorXf;

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

/* Local point in 1/16 px (relative to the door centre) to screen coordinates. */
static void door_map(const DoorXf *xf, int32_t lx16, int32_t ly16, int32_t *x, int32_t *y) {
    int32_t from_hinge16 = lx16 + xf->plate * 16;
    int32_t scaled16 = (from_hinge16 * xf->sx) / VG_ONE;
    *x = xf->cx - xf->plate + (scaled16 + 8) / 16;
    *y = xf->cy + (ly16 >= 0 ? (ly16 + 8) / 16 : -((-ly16 + 8) / 16));
}

static void door_polar(const DoorXf *xf, int32_t r16, int32_t turn1024, int32_t *x, int32_t *y) {
    int32_t a = turn1024 / 4;
    int32_t lx16 = (vg_sin(a) * r16) / VG_ONE;
    int32_t ly16 = -(vg_cos(a) * r16) / VG_ONE;
    door_map(xf, lx16, ly16, x, y);
}

static void door_spoke(Canvas *canvas, const DoorXf *xf, int32_t r0_16, int32_t r1_16,
                       int32_t turn, VgMode mode) {
    int32_t x0, y0, x1, y1;
    door_polar(xf, r0_16, turn, &x0, &y0);
    door_polar(xf, r1_16, turn, &x1, &y1);
    vg_line(canvas, x0, y0, x1, y1, mode);
}

/* Circle on the door face: exact Bresenham when flat, a polygon when swinging. */
static void door_ring(Canvas *canvas, const DoorXf *xf, int32_t r, VgMode mode) {
    if (xf->sx >= VG_ONE) {
        vg_circle(canvas, xf->cx, xf->cy, r, mode);
        return;
    }
    int32_t px = 0, py = 0;
    for (int32_t i = 0; i <= 32; ++i) {
        int32_t x, y;
        door_polar(xf, r * 16, i * 32, &x, &y);
        if (i > 0 && (x != px || y != py)) vg_line(canvas, px, py, x, y, mode);
        px = x;
        py = y;
    }
}

static void door_disc(Canvas *canvas, const DoorXf *xf, int32_t r, VgMode mode) {
    if (xf->sx >= VG_ONE) {
        vg_disc(canvas, xf->cx, xf->cy, r, mode);
        return;
    }
    for (int32_t dy = -r; dy <= r; ++dy) {
        int32_t half = 0;
        while ((half + 1) * (half + 1) + dy * dy <= r * r + r) ++half;
        int32_t xa, xb, yy;
        door_map(xf, -half * 16, dy * 16, &xa, &yy);
        door_map(xf, half * 16, dy * 16, &xb, &yy);
        vg_hline(canvas, xa, xb, yy, mode);
    }
}

/* The wall frame around the doorway and the dark (or lit) vault interior. */
static void door_draw_frame(Canvas *canvas, const VaultDoor *door, int32_t plate) {
    const int32_t r = door->r;
    const int32_t seam = plate + 1;
    const int32_t light_r = (door->light * (plate + 6)) / VG_ONE;

    for (int32_t dy = -r; dy <= r; ++dy) {
        for (int32_t dx = -r; dx <= r; ++dx) {
            int32_t d2 = dx * dx + dy * dy;
            int32_t x = door->cx + dx;
            int32_t y = door->cy + dy;
            if (d2 > r * r + r) continue;
            if (d2 >= (r - 1) * (r - 1)) {
                vg_px(canvas, x, y, VgSet); /* outer lip */
            } else if (d2 > seam * seam + seam) {
                /* Frame band: lit metal, a light grain darkening to the bottom right. */
                int32_t level = ((dx + dy) * 4) / r;
                if (vg_bayer(x, y, level)) vg_px(canvas, x, y, VgSet);
            } else if (d2 > plate * plate + plate) {
                vg_px(canvas, x, y, VgSet); /* seam */
            } else {
                /* Interior: dark, with light spilling out from the centre. */
                int32_t level = 14;
                if (light_r > 0) {
                    int32_t lr2 = light_r * light_r;
                    if (d2 < lr2) {
                        level = 0;
                    } else {
                        int32_t edge = (light_r + 5) * (light_r + 5);
                        level = d2 >= edge ? 14 : (14 * (d2 - lr2)) / (edge - lr2);
                    }
                }
                if (vg_bayer(x, y, level)) vg_px(canvas, x, y, VgSet);
            }
        }
    }
}

static void door_draw_plate(Canvas *canvas, const VaultDoor *door, const DoorXf *xf) {
    const int32_t plate = xf->plate;
    const int32_t left = xf->cx - plate;
    const int32_t width = (2 * plate * xf->sx) / VG_ONE + 1;
    const int32_t shade_k = plate > 0 ? plate : 1;

    if (xf->sx <= 0) return;
    /* Shaded face, lit from the top left. Inverse-map each pixel to the face. */
    for (int32_t y = door->cy - plate; y <= door->cy + plate; ++y) {
        int32_t v = y - door->cy;
        for (int32_t x = left; x <= left + width; ++x) {
            int32_t u = ((x - left) * VG_ONE) / xf->sx - plate;
            int32_t d2 = u * u + v * v;
            if (d2 > plate * plate + plate) continue;
            /* Lit face with a soft crescent of shade at the lower right. */
            int32_t level = ((u + v) * 6) / shade_k - 3;
            if (d2 >= (plate - 1) * (plate - 1) && (u + v) > plate / 3) {
                level = 16; /* lower rim bevel */
            } else if (d2 >= (plate - 2) * (plate - 2) && (u + v) < -plate / 3) {
                level = 0; /* upper rim catches the light */
            }
            /* Swinging away darkens the face. */
            level += (VG_ONE - xf->sx) * 6 / VG_ONE;
            if (level > 0 && vg_bayer(x, y, level)) vg_px(canvas, x, y, VgSet);
            else vg_px(canvas, x, y, VgClear);
        }
    }
    /* Door edge thickness while it swings. */
    if (xf->sx < VG_ONE) {
        int32_t edge = ((VG_ONE - xf->sx) * 4) / VG_ONE;
        for (int32_t dy = -plate; dy <= plate; ++dy) {
            int32_t half = 0;
            while ((half + 1) * (half + 1) + dy * dy <= plate * plate + plate) ++half;
            int32_t xb, yy;
            door_map(xf, half * 16, dy * 16, &xb, &yy);
            vg_hline(canvas, xb, xb + edge, yy, VgSet);
        }
    }
}

static void door_draw_bolts(Canvas *canvas, const VaultDoor *door, const DoorXf *xf) {
    const int32_t plate = xf->plate;
    const int32_t count = plate >= 14 ? VAULT_DOOR_BOLTS : 4;

    for (int32_t i = 0; i < count; ++i) {
        int32_t idx = count == VAULT_DOOR_BOLTS ? i : i * 2;
        int32_t turn = idx * (1024 / VAULT_DOOR_BOLTS) + 1024 / 16;
        int32_t out = vg_clamp(door->bolt[idx], -256, VG_ONE + 256);
        int32_t size = 1;
        int32_t sx, sy, bx, by;
        /* Empty socket in the frame, visible once the bolt is withdrawn. */
        vg_polar(door->cx, door->cy, (plate + 4) * 16, turn / 4, &sx, &sy);
        vg_disc(canvas, sx, sy, size - 1, VgSet);
        if (xf->sx < VG_ONE / 3) continue; /* bolts travel with the door */
        /* Bolt head: thrown across the seam when locked, pulled into the plate. */
        int32_t r16 = (plate - 3) * 16 + (out * 7 * 16) / VG_ONE;
        door_polar(xf, r16, turn, &bx, &by);
        vg_disc(canvas, bx, by, size, VgSet);
        vg_px(canvas, bx - 1, by - 1, VgClear);
    }
}

static void door_draw_hinges(Canvas *canvas, const VaultDoor *door, int32_t plate) {
    int32_t x = door->cx - door->r - 2;
    (void)plate;
    for (int32_t s = -1; s <= 1; s += 2) {
        int32_t y = door->cy + s * (door->r / 2) - 3;
        vg_rfill(canvas, x, y, 5, 7, VgSet);
        vg_vline(canvas, x + 1, y + 2, y + 4, VgClear);
    }
}

static void door_draw_face(Canvas *canvas, const VaultDoor *door, const DoorXf *xf) {
    const int32_t plate = xf->plate;
    const int32_t dial_r = plate * 9 / 20;
    const int32_t ring_r = plate * 7 / 10;

    if (xf->sx <= 64) return;
    if (plate >= 18) door_ring(canvas, xf, ring_r, VgSet);

    /* Spoke wheel: three spokes with round grips. */
    for (int32_t s = 0; s < 3; ++s) {
        int32_t turn = door->handle + s * 341 + 170;
        int32_t gx, gy, x0, y0, x1, y1;
        door_polar(xf, (dial_r + 1) * 16, turn, &x0, &y0);
        door_polar(xf, (ring_r + 2) * 16, turn, &x1, &y1);
        vg_thick_line(canvas, x0, y0, x1, y1, VgSet);
        door_polar(xf, (ring_r + 3) * 16, turn, &gx, &gy);
        if (xf->sx >= VG_ONE) {
            vg_disc(canvas, gx, gy, 2, VgSet);
            vg_px(canvas, gx - 1, gy - 1, VgClear);
        } else {
            vg_fill(canvas, gx - 1, gy - 1, 3, 3, VgSet);
        }
    }

    /* Dial: lit disc, rotating ticks, fixed index notch above. */
    door_disc(canvas, xf, dial_r, VgClear);
    door_ring(canvas, xf, dial_r, VgSet);
    for (int32_t t = 0; t < 32; ++t) {
        int32_t turn = door->dial + t * 32;
        int32_t len = (t % 8 == 0) ? 4 : (t % 2 == 0 ? 2 : 1);
        if (dial_r < 8 && t % 2) continue;
        door_spoke(canvas, xf, (dial_r - 1) * 16, (dial_r - len) * 16, turn, VgSet);
    }
    int32_t nx, ny;
    door_polar(xf, (dial_r + 2) * 16, 0, &nx, &ny);
    vg_px(canvas, nx, ny, VgSet);
    vg_px(canvas, nx - 1, ny - 1, VgSet);
    vg_px(canvas, nx + 1, ny - 1, VgSet);
    /* Hub. */
    door_disc(canvas, xf, dial_r / 3 + 1, VgSet);
    int32_t hx, hy;
    door_map(xf, -16, -16, &hx, &hy);
    vg_px(canvas, hx, hy, VgClear);
}

void vault_door_draw(Canvas *canvas, const VaultDoor *door) {
    const int32_t plate = door->r - 8;
    if (door->r < 16) {
        /* Too small for a door: just the dial. */
        vault_dial_badge(canvas, door->cx, door->cy, door->r > 9 ? 9 : door->r, door->dial);
        return;
    }
    DoorXf xf = {
        .cx = door->cx,
        .cy = door->cy,
        .plate = plate,
        .sx = vg_cos((vg_clamp(door->swing, 0, VG_ONE) * 64) / VG_ONE),
    };

    door_draw_frame(canvas, door, plate);
    door_draw_plate(canvas, door, &xf);
    door_draw_face(canvas, door, &xf);
    door_draw_bolts(canvas, door, &xf);
    door_draw_hinges(canvas, door, plate);
}

void vault_dial_badge(Canvas *canvas, int32_t cx, int32_t cy, int32_t r, int32_t dial) {
    int32_t x0, y0;
    vg_disc(canvas, cx, cy, r + 1, VgClear);
    vg_circle(canvas, cx, cy, r, VgSet);
    if (r >= 8) vg_circle(canvas, cx, cy, r - 1, VgSet);
    /* Tick dots that turn with the dial, and a knob with a pointer. */
    for (int32_t t = 0; t < 12; ++t) {
        vg_polar(cx, cy, (r - 2) * 16 - 8, (dial + t * 85) / 4, &x0, &y0);
        if (r >= 8 || t % 3 == 0) vg_px(canvas, x0, y0, VgSet);
    }
    int32_t knob = r >= 8 ? r / 3 : 1;
    vg_disc(canvas, cx, cy, knob, VgSet);
    vg_polar(cx, cy, (knob + 1) * 16 + 8, dial / 4, &x0, &y0);
    vg_line(canvas, cx, cy, x0, y0, VgSet);
    vg_px(canvas, cx, cy - r - 2, VgSet);
}
