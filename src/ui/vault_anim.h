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

#include <gui/view.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * One ~30 fps frame clock for whichever vault view is on screen. It only
 * redraws; models carry timestamps and draw code derives motion from the
 * tick. The timer runs only while something moves: a timed burst
 * (vault_anim_run) or ambient motion, which falls asleep after a minute
 * without input or events. Start/stop happen on the UI thread
 * (vault_anim_service from the view dispatcher tick).
 */
#define VAULT_ANIM_FRAME_MS 33U
#define VAULT_AMBIENT_MS 60000U

bool vault_anim_init(void);
void vault_anim_deinit(void);
/* The view now on screen (NULL: none of ours). `ambient`: it has idle motion. */
void vault_anim_bind(View *view, bool ambient);
void vault_anim_unbind(View *view);
/* Keep frames coming for at least `ms`. */
void vault_anim_run(uint32_t ms);
/* User or app activity: wakes ambient motion. */
void vault_anim_poke(void);
uint32_t vault_anim_ambient_until(void);
/* UI thread, every dispatcher tick: starts or stops the timer. */
void vault_anim_service(void);
