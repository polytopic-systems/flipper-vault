/*
 * ZeroFIDO
 * Copyright (C) 2026 Alex Stoyanov
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

#include "../zerofido_ui.h"

#include <stdio.h>
#include <string.h>

#include "../zerofido_app_i.h"
#include "../zerofido_store.h"
#include "../zerofido_ui_i.h"
#if ZF_BLE_ENABLED
#include "../transport/ble_worker.h"
#endif
#include "scene.h"
#include "status.h"
#include "vault_anim.h"

/* Home: the vault door with transport and count; OK opens the passkey list. */

#define ZF_HOME_MESSAGE_MS 4000U

typedef struct {
    ZfCredentialRecord record;
    uint8_t store_io[ZF_STORE_RECORD_IO_SIZE];
} ZfStatusCredentialScratch;

_Static_assert(sizeof(ZfStatusCredentialScratch) <= ZF_UI_SCRATCH_SIZE,
               "status credential scratch exceeds UI arena");

typedef struct {
    VaultHomeModel scene;
    uint32_t message_at;
} ZfStatusModel;

static bool zf_status_text_is_error(const char *text) {
    return text && (strstr(text, "failed") || strstr(text, "Failed") || strstr(text, "error") ||
                    strstr(text, "denied") || strstr(text, "timed out") ||
                    strstr(text, "Could not") || strstr(text, "not found"));
}

static VaultTransportArt zf_status_transport_art(ZfTransportMode mode) {
    switch (mode) {
    case ZfTransportModeNfc:
        return VaultTransportNfc;
    case ZfTransportModeBle:
        return VaultTransportBle;
    case ZfTransportModeUsbHid:
    default:
        return VaultTransportUsb;
    }
}

void zf_status_sigil_seed(const ZfCredentialIndexEntry *entry, VaultSigilSeed *seed) {
#ifdef ZF_HOST_TEST
    vault_sigil_seed(seed, entry->credential_id, entry->credential_id_len,
                     (const uint8_t *)entry->rp_id, strlen(entry->rp_id));
#else
    vault_sigil_seed(seed, entry->credential_id, entry->credential_id_len, entry->rp_id_hash,
                     sizeof(entry->rp_id_hash));
#endif
}

static void zf_status_format_record_item(const ZfCredentialRecord *record, VaultListItem *item) {
    const char *user = record->user_display_name[0] ? record->user_display_name
                       : record->user_name[0]       ? record->user_name
                                                    : "No account name";
    scene_copy(item->title, sizeof(item->title),
               record->rp_id[0] ? record->rp_id : "Unknown website");
    scene_copy(item->subtitle, sizeof(item->subtitle), user);
}

/* List scroll: keep the selection on the middle row when possible. */
static int16_t zf_status_list_scroll(uint16_t selected, uint16_t count) {
    int32_t max_scroll = ((int32_t)count - 3) * VAULT_ROW_H;
    return (int16_t)vg_clamp(((int32_t)selected - 1) * VAULT_ROW_H, 0,
                             max_scroll > 0 ? max_scroll : 0);
}

static bool zf_status_refresh_model(ZerofidoApp *app, bool redraw, bool reload_credentials) {
    ZfCredentialIndexEntry entries[VAULT_LIST_WINDOW];
    VaultHomeStatus status;
    ZfTransportMode mode;
    char message[sizeof(((VaultHomeModel *)0)->message)];
    bool starting;
    uint16_t count = 0;
    uint16_t selected = 0;
    uint16_t window_start = 0;
    uint8_t window_count = 0;

    if (!app || !app->status_view) {
        return false;
    }

    memset(entries, 0, sizeof(entries));
    furi_mutex_acquire(app->ui_mutex, FuriWaitForever);
    mode = app->runtime_config.transport_mode;
    starting = !app->startup_complete;
    if (zf_status_text_is_error(app->status_text)) {
        status = VaultHomeError;
    } else if (app->approval.state == ZfApprovalPending || app->status_text[0] != '\0' ||
               !app->startup_complete || !app->worker_thread) {
        status = VaultHomeWaiting;
    } else if (app->transport_connected) {
        status = VaultHomeConnected;
    } else {
        status = VaultHomeReady;
    }
    scene_copy(message, sizeof(message), starting ? "" : app->status_text);

    if (reload_credentials && app->startup_complete && app->store.records) {
        uint32_t first_record_index = UINT32_MAX;
        bool selected_found = false;

        for (size_t i = 0; i < app->store.count; ++i) {
            if (!app->store.records[i].in_use)
                continue;
            if (first_record_index == UINT32_MAX)
                first_record_index = (uint32_t)i;
            if ((uint32_t)i == app->credentials_selected_index) {
                selected = count;
                selected_found = true;
            }
            ++count;
        }
        if (count > 0 && !selected_found) {
            app->credentials_selected_index = first_record_index;
            selected = 0;
        }
        if (count > VAULT_LIST_WINDOW && selected > 2U) {
            window_start = selected - 2U;
            if (window_start + VAULT_LIST_WINDOW > count)
                window_start = count - VAULT_LIST_WINDOW;
        }
        size_t position = 0;
        for (size_t i = 0; i < app->store.count && window_count < VAULT_LIST_WINDOW; ++i) {
            if (!app->store.records[i].in_use)
                continue;
            if (position++ >= window_start)
                entries[window_count++] = app->store.records[i];
        }
    }
    furi_mutex_release(app->ui_mutex);

    if (!reload_credentials) {
        with_view_model(
            app->status_view, ZfStatusModel * model,
            {
                model->scene.transport = zf_status_transport_art(mode);
                snprintf(model->scene.transport_label, sizeof(model->scene.transport_label), "%s",
                         zf_transport_mode_short_name(mode));
                model->scene.status = status;
                model->scene.starting = starting;
                if (strcmp(model->scene.message, message) != 0) {
                    scene_copy(model->scene.message, sizeof(model->scene.message), message);
                    model->message_at = furi_get_tick();
                }
            },
            redraw);
        return true;
    }

    /* Load display labels for the visible window outside the UI lock. */
    VaultListItem *items = malloc(sizeof(VaultListItem) * VAULT_LIST_WINDOW);
    if (!items)
        return false;
    memset(items, 0, sizeof(VaultListItem) * VAULT_LIST_WINDOW);
    ZfStatusCredentialScratch *scratch = zf_app_ui_scratch_acquire(app, sizeof(*scratch));
    for (uint8_t i = 0; i < window_count; ++i) {
        VaultListItem *item = &items[i];
        item->valid = true;
        zf_status_sigil_seed(&entries[i], &item->seed);
        if (scratch && zf_store_load_record_for_display_with_buffer(
                           app->storage, &entries[i], &scratch->record, scratch->store_io,
                           sizeof(scratch->store_io))) {
            zf_status_format_record_item(&scratch->record, item);
            memset(&scratch->record, 0, sizeof(scratch->record));
        } else {
            zf_ui_format_passkey_index_title(&entries[i], item->title, sizeof(item->title));
            zf_ui_format_passkey_index_subtitle(&entries[i], item->subtitle,
                                                sizeof(item->subtitle));
        }
    }
    zf_app_ui_scratch_release(app);

    with_view_model(
        app->status_view, ZfStatusModel * model,
        {
            VaultHomeModel *m = &model->scene;
            m->transport = zf_status_transport_art(mode);
            snprintf(m->transport_label, sizeof(m->transport_label), "%s",
                     zf_transport_mode_short_name(mode));
            m->status = status;
            m->starting = starting;
            if (strcmp(m->message, message) != 0) {
                scene_copy(m->message, sizeof(m->message), message);
                model->message_at = furi_get_tick();
            }
            int16_t scroll = zf_status_list_scroll(selected, count);
            m->count = count;
            m->selected = selected;
            if (scroll != m->scroll_to) {
                m->scroll_from = m->scroll_to;
                m->scroll_to = scroll;
                m->scroll_at = furi_get_tick();
            }
            m->window_start = window_start;
            m->window_count = window_count;
            memcpy(m->items, items, sizeof(VaultListItem) * VAULT_LIST_WINDOW);
            if (count == 0)
                m->list_mode = false;
        },
        redraw);
    free(items);
    vault_anim_run(160);
    return true;
}

static void zf_status_draw_callback(Canvas *canvas, void *model) {
    ZfStatusModel *status = model;
    VaultHomeModel *m = &status->scene;

    m->now = furi_get_tick();
    m->ambient = (int32_t)(vault_anim_ambient_until() - m->now) > 0;
    VaultHomeStatus saved = m->status;
#if ZF_BLE_ENABLED
    /* Bluetooth link state drives the radio rings and the status light. */
    if (m->transport == VaultTransportBle) {
        ZfBleLinkState link = zf_transport_ble_link_state();
        if (link == ZfBleLinkStateConnected || link == ZfBleLinkStateSecured) {
            if (m->status == VaultHomeReady)
                m->status = VaultHomeConnected;
        } else if (link == ZfBleLinkStateOff && m->status != VaultHomeError) {
            m->ambient = false; /* no rings while the radio is off */
        }
    }
#endif
    /* Status messages show for a few seconds, then the count returns. */
    char message[sizeof(m->message)];
    memcpy(message, m->message, sizeof(message));
    if ((m->now - status->message_at) > ZF_HOME_MESSAGE_MS && m->status != VaultHomeError) {
        m->message[0] = '\0';
    }
    scene_home_draw(canvas, m);
    memcpy(m->message, message, sizeof(message));
    m->status = saved;
}

static void zf_status_enter_callback(void *context) {
    ZerofidoApp *app = context;
    vault_anim_bind(app->status_view, true);
}

static void zf_status_exit_callback(void *context) {
    ZerofidoApp *app = context;
    vault_anim_unbind(app->status_view);
}

void zerofido_ui_status_bind_view(ZerofidoApp *app) {
    view_allocate_model(app->status_view, ViewModelTypeLocking, sizeof(ZfStatusModel));
    view_set_context(app->status_view, app);
    view_set_draw_callback(app->status_view, zf_status_draw_callback);
    view_set_enter_callback(app->status_view, zf_status_enter_callback);
    view_set_exit_callback(app->status_view, zf_status_exit_callback);
    with_view_model(
        app->status_view, ZfStatusModel * model,
        {
            memset(model, 0, sizeof(*model));
            model->scene.boot_at = furi_get_tick();
        },
        false);
    zf_status_refresh_model(app, false, true);
    vault_anim_run(VAULT_BOOT_MS + 100U);
}

void zerofido_ui_status_redraw(ZerofidoApp *app) {
    if (!app || !app->status_view) {
        return;
    }

    with_view_model(app->status_view, ZfStatusModel * model, { UNUSED(model); }, true);
}

bool zerofido_ui_status_list_mode(ZerofidoApp *app) {
    bool list_mode = false;
    if (!app || !app->status_view)
        return false;
    with_view_model(
        app->status_view, ZfStatusModel * model,
        { list_mode = model->scene.list_mode && model->scene.count > 0; }, false);
    return list_mode;
}

bool zerofido_ui_status_set_list_mode(ZerofidoApp *app, bool list_mode) {
    bool changed = false;
    if (!app || !app->status_view)
        return false;
    with_view_model(
        app->status_view, ZfStatusModel * model,
        {
            if (list_mode && model->scene.count == 0)
                list_mode = false;
            changed = model->scene.list_mode != list_mode;
            model->scene.list_mode = list_mode;
            model->scene.boot_at = 0;
        },
        false);
    if (changed) {
        zerofido_ui_status_redraw(app);
    }
    return changed;
}

static void zerofido_ui_refresh_status_internal(ZerofidoApp *app, bool credentials_dirty) {
    bool dispatch_event = false;
    bool reload_credentials = false;

    if (!app) {
        return;
    }

    if (app && app->ui_thread_id != NULL && app->ui_thread_id != furi_thread_get_current_id()) {
        furi_mutex_acquire(app->ui_mutex, FuriWaitForever);
        if (credentials_dirty) {
            app->status_credentials_dirty = true;
        }
        dispatch_event = !app->status_refresh_pending;
        app->status_refresh_pending = true;
        furi_mutex_release(app->ui_mutex);

        if (dispatch_event) {
            zerofido_ui_dispatch_custom_event(app, ZfEventActivity);
        }
        return;
    }

    furi_mutex_acquire(app->ui_mutex, FuriWaitForever);
    if (credentials_dirty) {
        app->status_credentials_dirty = true;
    }
    reload_credentials = app->status_credentials_dirty;
    app->status_refresh_pending = false;
    if (reload_credentials) {
        app->status_credentials_dirty = false;
    }
    furi_mutex_release(app->ui_mutex);

    if (!zf_status_refresh_model(app, true, reload_credentials) && reload_credentials) {
        furi_mutex_acquire(app->ui_mutex, FuriWaitForever);
        app->status_credentials_dirty = true;
        furi_mutex_release(app->ui_mutex);
    }
}

void zerofido_ui_refresh_status(ZerofidoApp *app) {
    zerofido_ui_refresh_status_internal(app, true);
}

void zerofido_ui_refresh_status_line(ZerofidoApp *app) {
    zerofido_ui_refresh_status_internal(app, false);
}

void zerofido_ui_refresh_credentials_status(ZerofidoApp *app) {
    zerofido_ui_refresh_status_internal(app, true);
}

void zerofido_ui_set_status_locked(ZerofidoApp *app, const char *text) {
    if (!app) {
        return;
    }

    if (text) {
        if (strncmp(app->status_text, text, sizeof(app->status_text)) == 0) {
            return;
        }

        strncpy(app->status_text, text, sizeof(app->status_text) - 1U);
        app->status_text[sizeof(app->status_text) - 1U] = '\0';
    } else {
        if (app->status_text[0] == '\0') {
            return;
        }
        app->status_text[0] = '\0';
    }
}

void zerofido_ui_set_status(ZerofidoApp *app, const char *text) {
    bool changed = false;

    if (!app) {
        return;
    }

    furi_mutex_acquire(app->ui_mutex, FuriWaitForever);
    if (text) {
        changed = strncmp(app->status_text, text, sizeof(app->status_text)) != 0;
    } else {
        changed = app->status_text[0] != '\0';
    }
    zerofido_ui_set_status_locked(app, text);
    furi_mutex_release(app->ui_mutex);
    if (changed) {
        zerofido_ui_refresh_status_line(app);
    }
}

void zerofido_ui_apply_transport_connected(ZerofidoApp *app, bool connected) {
    bool changed = false;

    furi_mutex_acquire(app->ui_mutex, FuriWaitForever);
    changed = app->transport_connected != connected;
    app->transport_connected = connected;
    furi_mutex_release(app->ui_mutex);
    if (changed) {
        zerofido_ui_refresh_status_line(app);
    }
}

void zerofido_ui_set_transport_connected(ZerofidoApp *app, bool connected) {
    zerofido_ui_apply_transport_connected(app, connected);
}
