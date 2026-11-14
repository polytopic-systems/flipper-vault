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

#include <string.h>

/* 7x7 icons, one byte per row, bit 0 = leftmost pixel. */
static const uint8_t icon_bits[][7] = {
    [VaultIconNone] = {0},
    [VaultIconLock] = {0x1C, 0x22, 0x22, 0x7F, 0x77, 0x77, 0x7F},
    [VaultIconKey] = {0x00, 0x06, 0x79, 0x69, 0x06, 0x00, 0x00},
    [VaultIconGear] = {0x08, 0x3E, 0x36, 0x77, 0x36, 0x3E, 0x08},
    [VaultIconGlobe] = {0x1C, 0x2A, 0x7F, 0x49, 0x7F, 0x2A, 0x1C},
    [VaultIconUser] = {0x1C, 0x3E, 0x1C, 0x00, 0x3E, 0x7F, 0x7F},
    [VaultIconUsb] = {0x08, 0x1C, 0x28, 0x2B, 0x3B, 0x0E, 0x08},
    [VaultIconNfc] = {0x10, 0x24, 0x48, 0x49, 0x48, 0x24, 0x10},
    [VaultIconBle] = {0x08, 0x18, 0x2A, 0x1C, 0x2A, 0x18, 0x08},
    [VaultIconInfo] = {0x1C, 0x36, 0x3E, 0x36, 0x36, 0x36, 0x1C},
    [VaultIconTrash] = {0x1C, 0x7F, 0x00, 0x3E, 0x2A, 0x2A, 0x3E},
    [VaultIconWarn] = {0x08, 0x14, 0x14, 0x2A, 0x22, 0x49, 0x7F},
};

void scene_icon(Canvas *canvas, int32_t x, int32_t y, VaultIcon icon, VgMode mode) {
    if (icon == VaultIconNone || icon > VaultIconWarn) return;
    vg_xbm(canvas, x, y, 7, 7, icon_bits[icon], mode);
}

VaultIcon scene_transport_icon(VaultTransportArt art) {
    switch (art) {
    case VaultTransportNfc:
        return VaultIconNfc;
    case VaultTransportBle:
        return VaultIconBle;
    case VaultTransportUsb:
    default:
        return VaultIconUsb;
    }
}

void scene_title_bar(Canvas *canvas, VaultIcon icon, const char *title, const char *right) {
    vg_fill(canvas, 0, 0, VG_W, 11, VgSet);
    /* Rivets on the bar ends. */
    vg_px(canvas, 1, 1, VgClear);
    vg_px(canvas, 126, 1, VgClear);
    vg_px(canvas, 1, 9, VgClear);
    vg_px(canvas, 126, 9, VgClear);
    int32_t x = 4;
    if (icon != VaultIconNone) {
        scene_icon(canvas, 4, 2, icon, VgClear);
        x = 14;
    }
    canvas_set_color(canvas, ColorWhite);
    int32_t right_w = 0;
    if (right && right[0]) {
        canvas_set_font(canvas, FontSecondary);
        right_w = canvas_string_width(canvas, right) + 4;
        vt_str_right(canvas, 124, 9, right);
    }
    canvas_set_font(canvas, FontPrimary);
    vt_str_clip(canvas, x, 9, 122 - x - right_w, title);
    canvas_set_color(canvas, ColorBlack);
}

static void hint_arrow(Canvas *canvas, int32_t x, int32_t y, bool right, VgMode mode) {
    for (int32_t i = 0; i < 3; ++i) {
        int32_t col = right ? x + i : x + 2 - i;
        vg_vline(canvas, col, y + i, y + 4 - i, mode);
    }
}

int32_t scene_hint(Canvas *canvas, int32_t x, const char *key, const char *label, bool right) {
    canvas_set_font(canvas, FontSecondary);
    bool arrow = key && (strcmp(key, "<") == 0 || strcmp(key, ">") == 0);
    int32_t key_w = arrow ? 7 : (int32_t)canvas_string_width(canvas, key) + 5;
    int32_t label_w = label && label[0] ? (int32_t)canvas_string_width(canvas, label) + 2 : 0;
    int32_t total = key_w + label_w;
    int32_t left = right ? x - total : x;
    const int32_t y = 54;

    vg_rfill(canvas, left, y, key_w, 10, VgSet);
    if (arrow) {
        hint_arrow(canvas, left + 2, y + 3, key[0] == '>', VgClear);
    } else {
        canvas_set_color(canvas, ColorWhite);
        vt_str(canvas, left + 3, y + 8, key);
        canvas_set_color(canvas, ColorBlack);
    }
    if (label_w) vt_str(canvas, left + key_w + 2, y + 8, label);
    return total;
}

void scene_transport_ambient(Canvas *canvas, int32_t x, int32_t y, VaultTransportArt art,
                             uint32_t now, bool active) {
    /* Box is 26 x 16. */
    switch (art) {
    case VaultTransportUsb: {
        /* Cable into a plug; packets pulse along it towards the vault. */
        vg_hline(canvas, x, x + 15, y + 7, VgSet);
        vg_hline(canvas, x, x + 15, y + 8, VgSet);
        vg_rfill(canvas, x + 16, y + 4, 7, 8, VgSet);
        vg_fill(canvas, x + 23, y + 5, 3, 2, VgSet);
        vg_fill(canvas, x + 23, y + 9, 3, 2, VgSet);
        vg_px(canvas, x + 18, y + 6, VgClear);
        vg_px(canvas, x + 18, y + 9, VgClear);
        if (active) {
            for (int32_t p = 0; p < 3; ++p) {
                int32_t pos = (int32_t)((now / 40U + (uint32_t)p * 6U) % 18U);
                if (pos < 15) {
                    vg_fill(canvas, x + pos, y + 6, 2, 4, VgSet);
                    vg_hline(canvas, x + pos, x + pos + 1, y + 7, VgClear);
                }
            }
        }
        break;
    }
    case VaultTransportNfc: {
        /* A card on the left; field ripples roll out to the right. */
        vg_rframe(canvas, x, y + 3, 7, 10, VgSet);
        vg_fill(canvas, x + 2, y + 5, 3, 2, VgSet);
        int32_t cx = x + 6;
        int32_t cy = y + 8;
        for (int32_t k = 0; k < 3; ++k) {
            int32_t phase = active ? (int32_t)((now / 30U + (uint32_t)k * 20U) % 60U) : k * 20;
            int32_t r = 4 + phase / 4;
            int32_t level = active ? 16 - phase / 5 : 12;
            for (int32_t a = -26; a <= 26; a += 2) {
                int32_t px, py;
                vg_polar(cx, cy, r * 16, 64 + a, &px, &py);
                if (vg_bayer(px, py, level) || level >= 15) vg_px(canvas, px, py, VgSet);
            }
        }
        break;
    }
    case VaultTransportBle:
    default: {
        /* Antenna with the rune; radio rings radiate all around. */
        int32_t cx = x + 13;
        int32_t cy = y + 8;
        for (int32_t k = 0; k < 2; ++k) {
            int32_t phase = active ? (int32_t)((now / 35U + (uint32_t)k * 24U) % 48U) : 12 + k * 18;
            int32_t r = 5 + phase / 6;
            int32_t level = active ? 16 - phase / 3 : 10;
            for (int32_t a = 0; a < 256; a += 6) {
                int32_t px, py;
                vg_polar(cx, cy, r * 16, a, &px, &py);
                if (py < y || py > y + 15) continue;
                if (vg_bayer(px, py, level) || level >= 15) vg_px(canvas, px, py, VgSet);
            }
        }
        vg_disc(canvas, cx, cy, 4, VgClear);
        scene_icon(canvas, cx - 3, cy - 3, VaultIconBle, VgSet);
        break;
    }
    }
}

/* ---- transitions ---- */

static uint8_t scene_last_frame[VG_W * VG_H / 8];
static VaultTransition scene_transition_kind;
static uint32_t scene_transition_at;
static bool scene_last_valid;

void scene_transition_begin(VaultTransition kind, uint32_t now) {
    if (!scene_last_valid) return;
    scene_transition_kind = kind;
    scene_transition_at = now;
}

bool scene_transition_busy(uint32_t now) {
    return scene_transition_kind != VaultTransitionNone &&
           (now - scene_transition_at) < VAULT_TRANSITION_MS;
}

void scene_transition_apply(Canvas *canvas, uint32_t now) {
    uint8_t *fb = canvas_get_buffer(canvas);

    if (scene_transition_busy(now)) {
        int32_t t = vg_ease_out_cubic(vg_progress(now, scene_transition_at, VAULT_TRANSITION_MS));
        if (scene_transition_kind == VaultTransitionDissolve) {
            /* Old frame survives where the threshold has not reached yet. */
            int32_t level = (t * 17) / VG_ONE;
            for (int32_t y = 0; y < VG_H; ++y) {
                for (int32_t x = 0; x < VG_W; ++x) {
                    if (!vg_bayer(x, y, level)) {
                        uint8_t bit = (uint8_t)(1U << (y & 7));
                        uint8_t *dst = &fb[x + (y >> 3) * VG_W];
                        *dst = (uint8_t)((*dst & ~bit) |
                                         (scene_last_frame[x + (y >> 3) * VG_W] & bit));
                    }
                }
            }
        } else {
            /* Slide: new screen pushes the old one out. */
            int32_t shift = VG_W - (t * VG_W) / VG_ONE; /* new frame offset */
            bool left = scene_transition_kind == VaultTransitionSlideLeft;
            for (int32_t page = 0; page < VG_H / 8; ++page) {
                uint8_t row[VG_W];
                uint8_t *dst = &fb[page * VG_W];
                const uint8_t *old = &scene_last_frame[page * VG_W];
                for (int32_t x = 0; x < VG_W; ++x) {
                    int32_t nx = left ? x - shift : x + shift;
                    int32_t ox = left ? x + (VG_W - shift) : x - (VG_W - shift);
                    if (nx >= 0 && nx < VG_W) {
                        row[x] = dst[nx];
                    } else if (ox >= 0 && ox < VG_W) {
                        row[x] = old[ox];
                    } else {
                        row[x] = 0;
                    }
                }
                /* Seam line between the screens. */
                int32_t seam = left ? shift - 1 : VG_W - shift;
                if (seam >= 0 && seam < VG_W) row[seam] = 0xFF;
                memcpy(dst, row, VG_W);
            }
        }
        return;
    }
    scene_transition_kind = VaultTransitionNone;
    memcpy(scene_last_frame, fb, sizeof(scene_last_frame));
    scene_last_valid = true;
}
