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

uint32_t vault_hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    return x ^ (x >> 16);
}

static uint32_t sigil_fnv(const uint8_t *data, size_t len, uint32_t h) {
    for (size_t i = 0; data && i < len; ++i) {
        h ^= data[i];
        h *= 16777619U;
    }
    return vault_hash32(h);
}

void vault_sigil_seed(VaultSigilSeed *seed, const uint8_t *credential_id, size_t credential_id_len,
                      const uint8_t *site, size_t site_len) {
    uint32_t glyph =
        sigil_fnv(site, site_len, sigil_fnv(credential_id, credential_id_len, 2166136261U));
    for (int i = 0; i < 4; ++i) {
        seed->bytes[i] = (uint8_t)(glyph >> (8 * i));
        seed->bytes[4 + i] = (uint8_t)(glyph >> (8 * (3 - i)));
    }
}

static uint32_t sigil_glyph_bits(const VaultSigilSeed *seed) {
    uint32_t bits = (uint32_t)seed->bytes[0] | ((uint32_t)seed->bytes[1] << 8) |
                    ((uint32_t)seed->bytes[2] << 16);
    /* 15 bits = 5 rows x (2 mirrored columns + centre). Keep it neither empty nor solid. */
    bits &= 0x7FFFU;
    bits &= ~((1U << 0) | (1U << 12)); /* rounded silhouette: no outer corners */
    int count = 0;
    for (int i = 0; i < 15; ++i)
        count += (bits >> i) & 1U;
    if (count < 5)
        bits |= 0x2492U; /* centre spine */
    if (count > 11)
        bits &= ~0x0124U;
    return bits;
}

static bool sigil_cell(uint32_t bits, int32_t row, int32_t col) {
    if (row < 0 || row > 4 || col < 0 || col > 4)
        return false;
    int32_t mc = col <= 2 ? col : 4 - col;
    return (bits >> (row * 3 + mc)) & 1U;
}

void vault_sigil_draw(Canvas *canvas, int32_t x, int32_t y, int32_t scale,
                      const VaultSigilSeed *seed) {
    uint32_t bits = sigil_glyph_bits(seed);
    const int32_t s = scale < 1 ? 1 : scale;

    for (int32_t row = 0; row < 5; ++row) {
        for (int32_t col = 0; col < 5; ++col) {
            if (!sigil_cell(bits, row, col))
                continue;
            int32_t px = x + col * 3 * s;
            int32_t py = y + row * 3 * s;
            canvas_draw_box(canvas, px, py, 3 * s, 3 * s);
            /* Round off convex corners so joined cells read as one stroke. */
            bool up = sigil_cell(bits, row - 1, col);
            bool down = sigil_cell(bits, row + 1, col);
            bool left = sigil_cell(bits, row, col - 1);
            bool right = sigil_cell(bits, row, col + 1);
            canvas_invert_color(canvas);
            if (!up && !left)
                canvas_draw_box(canvas, px, py, s, s);
            if (!up && !right)
                canvas_draw_box(canvas, px + 2 * s, py, s, s);
            if (!down && !left)
                canvas_draw_box(canvas, px, py + 2 * s, s, s);
            if (!down && !right)
                canvas_draw_box(canvas, px + 2 * s, py + 2 * s, s, s);
            canvas_invert_color(canvas);
        }
    }
}
