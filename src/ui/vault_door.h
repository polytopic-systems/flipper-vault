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

#define VAULT_DOOR_BOLTS 8

/*
 * Procedural vault door. All motion is expressed as parameters so screens can
 * drive it from their own timelines:
 *   dial      dial rotation, 1/1024 turn units (1024 = one full turn)
 *   handle    spoke-wheel rotation, same units
 *   bolt[i]   0 = retracted into the door, 1024 = thrown into the frame
 *   swing     0 = shut, 1024 = swung fully open on the left hinge
 *   light     0 = dark vault interior, 1024 = light pouring out
 */
typedef struct {
    int32_t cx;
    int32_t cy;
    int32_t r;
    int32_t dial;
    int32_t handle;
    int32_t bolt[VAULT_DOOR_BOLTS];
    int32_t swing;
    int32_t light;
} VaultDoor;

void vault_door_init(VaultDoor *door, int32_t cx, int32_t cy, int32_t r);
void vault_door_draw(Canvas *canvas, const VaultDoor *door);
/* A tiny dial badge (r <= 9): ring, rotating ticks and pointer. */
void vault_dial_badge(Canvas *canvas, int32_t cx, int32_t cy, int32_t r, int32_t dial);
