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

#include "vault_anim.h"

#include <furi.h>

typedef struct {
    FuriTimer *timer;
    FuriMutex *mutex;
    View *view;
    bool ambient;
    uint32_t until;
    uint32_t ambient_until;
} VaultAnim;

static VaultAnim vault_anim;

static bool vault_anim_wanted(uint32_t now) {
    if (!vault_anim.view)
        return false;
    if ((int32_t)(vault_anim.until - now) > 0)
        return true;
    return vault_anim.ambient && (int32_t)(vault_anim.ambient_until - now) > 0;
}

static void vault_anim_tick(void *context) {
    UNUSED(context);
    if (furi_mutex_acquire(vault_anim.mutex, 0) != FuriStatusOk)
        return;
    if (vault_anim_wanted(furi_get_tick()) && vault_anim.view) {
        /* Touch the model to schedule a redraw of the view. */
        view_get_model(vault_anim.view);
        view_commit_model(vault_anim.view, true);
    }
    furi_mutex_release(vault_anim.mutex);
}

bool vault_anim_init(void) {
    vault_anim.mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    vault_anim.timer = furi_timer_alloc(vault_anim_tick, FuriTimerTypePeriodic, NULL);
    vault_anim.ambient_until = furi_get_tick() + VAULT_AMBIENT_MS;
    return vault_anim.mutex && vault_anim.timer;
}

void vault_anim_deinit(void) {
    if (vault_anim.timer) {
        furi_timer_stop(vault_anim.timer);
        furi_timer_free(vault_anim.timer);
    }
    if (vault_anim.mutex)
        furi_mutex_free(vault_anim.mutex);
    vault_anim.timer = NULL;
    vault_anim.mutex = NULL;
    vault_anim.view = NULL;
}

void vault_anim_bind(View *view, bool ambient) {
    if (!vault_anim.mutex)
        return;
    furi_mutex_acquire(vault_anim.mutex, FuriWaitForever);
    vault_anim.view = view;
    vault_anim.ambient = ambient;
    furi_mutex_release(vault_anim.mutex);
    vault_anim_service();
}

void vault_anim_unbind(View *view) {
    if (!vault_anim.mutex)
        return;
    furi_mutex_acquire(vault_anim.mutex, FuriWaitForever);
    if (vault_anim.view == view)
        vault_anim.view = NULL;
    furi_mutex_release(vault_anim.mutex);
}

void vault_anim_run(uint32_t ms) {
    uint32_t until = furi_get_tick() + ms;
    if ((int32_t)(until - vault_anim.until) > 0)
        vault_anim.until = until;
    vault_anim_service();
}

void vault_anim_poke(void) {
    vault_anim.ambient_until = furi_get_tick() + VAULT_AMBIENT_MS;
}

uint32_t vault_anim_ambient_until(void) {
    return vault_anim.ambient_until;
}

void vault_anim_service(void) {
    if (!vault_anim.timer)
        return;
    bool want = vault_anim_wanted(furi_get_tick());
    bool running = furi_timer_is_running(vault_anim.timer);
    if (want && !running) {
        furi_timer_start(vault_anim.timer, furi_ms_to_ticks(VAULT_ANIM_FRAME_MS));
    } else if (!want && running) {
        furi_timer_stop(vault_anim.timer);
    }
}
