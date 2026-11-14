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

#include "vault_gfx.h"

/*
 * Deterministic 1-bit sigil per passkey: a mirrored 5x5 glyph (from the
 * credential ID) inside a notched seal ring (from the site), so passkeys of
 * the same site share a ring and every passkey has its own mark. It is a
 * visual aid only, not a security check.
 */
#define VAULT_SIGIL_SEED_LEN 8U

typedef struct {
    uint8_t bytes[VAULT_SIGIL_SEED_LEN];
} VaultSigilSeed;

void vault_sigil_seed(VaultSigilSeed *seed, const uint8_t *credential_id, size_t credential_id_len,
                      const uint8_t *site, size_t site_len);
/* 15x15 glyph at (x, y); `scale` 1 or 2. */
void vault_sigil_draw(Canvas *canvas, int32_t x, int32_t y, int32_t scale,
                      const VaultSigilSeed *seed, VgMode mode);
/* Glyph (scale 2) inside a notched seal: a disc of radius 19 centred on (cx, cy). */
void vault_sigil_draw_seal(Canvas *canvas, int32_t cx, int32_t cy, const VaultSigilSeed *seed,
                           int32_t turn);
