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

#include "vault_sigil.h"

static uint32_t sigil_fnv(const uint8_t *data, size_t len, uint32_t h) {
    for (size_t i = 0; data && i < len; ++i) {
        h ^= data[i];
        h *= 16777619U;
    }
    return vg_hash32(h);
}

void vault_sigil_seed(VaultSigilSeed *seed, const uint8_t *credential_id, size_t credential_id_len,
                      const uint8_t *site, size_t site_len) {
    uint32_t glyph = sigil_fnv(credential_id, credential_id_len, 2166136261U);
    uint32_t ring = sigil_fnv(site, site_len, 0x9e3779b9U);
    for (int i = 0; i < 4; ++i) {
        seed->bytes[i] = (uint8_t)(glyph >> (8 * i));
        seed->bytes[4 + i] = (uint8_t)(ring >> (8 * i));
    }
}

static uint32_t sigil_glyph_bits(const VaultSigilSeed *seed) {
    uint32_t bits = (uint32_t)seed->bytes[0] | ((uint32_t)seed->bytes[1] << 8) |
                    ((uint32_t)seed->bytes[2] << 16);
    /* 15 bits = 5 rows x (2 mirrored columns + centre). Keep it neither empty nor solid. */
    bits &= 0x7FFFU;
    bits &= ~((1U << 0) | (1U << 12)); /* rounded silhouette: no outer corners */
    int count = 0;
    for (int i = 0; i < 15; ++i) count += (bits >> i) & 1U;
    if (count < 5) bits |= 0x2492U; /* centre spine */
    if (count > 11) bits &= ~0x0124U;
    return bits;
}

static bool sigil_cell(uint32_t bits, int32_t row, int32_t col) {
    if (row < 0 || row > 4 || col < 0 || col > 4) return false;
    int32_t mc = col <= 2 ? col : 4 - col;
    return (bits >> (row * 3 + mc)) & 1U;
}

void vault_sigil_draw(Canvas *canvas, int32_t x, int32_t y, int32_t scale,
                      const VaultSigilSeed *seed, VgMode mode) {
    uint32_t bits = sigil_glyph_bits(seed);
    const int32_t s = scale < 1 ? 1 : scale;

    for (int32_t row = 0; row < 5; ++row) {
        for (int32_t col = 0; col < 5; ++col) {
            if (!sigil_cell(bits, row, col)) continue;
            int32_t px = x + col * 3 * s;
            int32_t py = y + row * 3 * s;
            bool up = sigil_cell(bits, row - 1, col);
            bool down = sigil_cell(bits, row + 1, col);
            bool left = sigil_cell(bits, row, col - 1);
            bool right = sigil_cell(bits, row, col + 1);
            for (int32_t j = 0; j < 3; ++j) {
                for (int32_t i = 0; i < 3; ++i) {
                    /* Round off convex corners so joined cells read as one stroke. */
                    if (i == 0 && j == 0 && !up && !left) continue;
                    if (i == 2 && j == 0 && !up && !right) continue;
                    if (i == 0 && j == 2 && !down && !left) continue;
                    if (i == 2 && j == 2 && !down && !right) continue;
                    vg_fill(canvas, px + i * s, py + j * s, s, s, mode);
                }
            }
        }
    }
}

void vault_sigil_draw_seal(Canvas *canvas, int32_t cx, int32_t cy, const VaultSigilSeed *seed,
                           int32_t turn) {
    uint32_t ring = (uint32_t)seed->bytes[4] | ((uint32_t)seed->bytes[5] << 8);

    vg_disc(canvas, cx, cy, 21, VgClear);
    vg_circle(canvas, cx, cy, 20, VgSet);
    vg_circle(canvas, cx, cy, 18, VgSet);
    /* Site signature: 16 notches on the seal, shared by passkeys of one site. */
    for (int32_t i = 0; i < 16; ++i) {
        int32_t a = turn / 4 + i * 16;
        int32_t x0, y0, x1, y1;
        if (ring & (1U << i)) {
            vg_polar(cx, cy, 18 * 16, a, &x0, &y0);
            vg_polar(cx, cy, 21 * 16, a, &x1, &y1);
            vg_line(canvas, x0, y0, x1, y1, VgSet);
        } else {
            vg_polar(cx, cy, 19 * 16, a, &x0, &y0);
            vg_px(canvas, x0, y0, VgSet);
        }
    }
    vault_sigil_draw(canvas, cx - 15, cy - 15, 2, seed, VgSet);
}
