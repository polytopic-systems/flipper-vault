/*
 * Vault (fork of ZeroFIDO)
 * Copyright (C) 2026 Alex Stoyanov and Vault contributors
 *
 * Request flow adapted from zerofido-tpm (gg1nids12/zerofido-tpm,
 * src/transport/ble_worker.c, GPL-3.0-or-later).
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

#include "ble_worker.h"

#if ZF_BLE_ENABLED

#include <furi.h>
#include <string.h>

#include "../zerofido_app_i.h"
#include "../zerofido_crypto.h"
#include "../zerofido_ctap.h"
#include "../zerofido_notify.h"
#include "../zerofido_telemetry.h"
#include "../zerofido_ui.h"
#include "../zerofido_ui_i.h"

#define ZF_BLE_WORKER_POLL_MS 100U
#define ZF_BLE_WAIT_SLICE_MS 50U

typedef struct {
    ZfBleLink *link;
    volatile bool stopping;
    volatile bool processing;
    ZfTransportSessionId session_id;
    uint32_t last_keepalive_tick;
    bool connected_reported;
} ZfBleTransportState;

static ZfBleTransportState *zf_ble_state(ZerofidoApp *app) {
    return app ? (ZfBleTransportState *)app->transport_state : NULL;
}

static void zf_ble_send_error(ZfBleLink *link, uint8_t error) {
    (void)zf_ble_link_send(link, ZF_BLE_CMD_ERROR, &error, 1U);
}

static void zf_ble_send_keepalive(ZfBleTransportState *state, uint8_t status) {
    state->last_keepalive_tick = furi_get_tick();
    (void)zf_ble_link_send(state->link, ZF_BLE_CMD_KEEPALIVE, &status, 1U);
}

static bool zf_ble_keepalive_due(const ZfBleTransportState *state) {
    return (uint32_t)(furi_get_tick() - state->last_keepalive_tick) >= ZF_BLE_KEEPALIVE_INTERVAL_MS;
}

static void zf_ble_hide_interaction_if_needed(ZerofidoApp *app, bool canceled) {
    const ViewDispatcher *dispatcher = NULL;

    if (!canceled) {
        return;
    }
    furi_mutex_acquire(app->ui_mutex, FuriWaitForever);
    if (app->ui_events_enabled) {
        dispatcher = app->view_dispatcher;
    }
    furi_mutex_release(app->ui_mutex);
    if (dispatcher) {
        zerofido_ui_dispatch_custom_event(app, ZfEventHideApproval);
    }
}

static void zf_ble_update_connection(ZerofidoApp *app, ZfBleTransportState *state) {
    bool connected = zf_ble_link_is_connected(state->link);

    if (connected == state->connected_reported) {
        return;
    }
    state->connected_reported = connected;
    zerofido_ui_set_transport_connected(app, connected);
    if (!connected) {
        zerofido_notify_reset(app);
    }
}

/* Runs one complete request taken from the link. Slow work happens here, never in BLE callbacks. */
static void zf_ble_serve(ZerofidoApp *app, ZfBleTransportState *state) {
    ZfBleLink *link = state->link;
    uint8_t error = zf_ble_link_take_error(link);
    uint8_t cmd = 0;
    size_t len = 0;
    size_t out = 0;
    bool canceled = false;

    if (error != 0U) {
        zf_ble_send_error(link, error);
    }
    if (!zf_ble_link_take_request(link, &cmd, app->transport_arena, ZF_MAX_MSG_SIZE, &len)) {
        return;
    }

    if (cmd == ZF_BLE_CMD_PING) {
        /* Mark idle before replying: hosts send the next request as soon as they see the reply. */
        zf_ble_link_end_request(link);
        (void)zf_ble_link_send(link, ZF_BLE_CMD_PING, app->transport_arena, len);
    } else if (len == 0U) {
        zf_ble_link_end_request(link);
        zf_ble_send_error(link, ZF_BLE_ERR_INVALID_LEN);
    } else {
        /*
         * The session id names the CTAP channel: stable for a connection so
         * getNextAssertion matches the assertion that produced it, and new on
         * every reconnect so queued state from an old connection is rejected.
         */
        state->session_id = (ZfTransportSessionId)zf_ble_link_connection_id(link);
        state->last_keepalive_tick = furi_get_tick();
        state->processing = true;
        /* Request and response share the transport arena, as on USB. */
        out = zerofido_handle_ctap2(app, state->session_id, app->transport_arena, len,
                                    app->transport_arena, ZF_MAX_MSG_SIZE);
        state->processing = false;
        canceled = zf_ble_link_cancel_requested(link) || state->stopping;
        zf_ble_link_end_request(link);
        if (canceled) {
            app->transport_arena[0] = ZF_CTAP_ERR_KEEPALIVE_CANCEL;
            out = 1U;
        }
        if (!state->stopping && zf_ble_link_is_connected(link)) {
            if (out == 0U) {
                zf_ble_send_error(link, ZF_BLE_ERR_OTHER);
            } else {
                (void)zf_ble_link_send(link, ZF_BLE_CMD_MSG, app->transport_arena, out);
            }
        }
        zerofido_ui_refresh_status(app);
    }
    zf_app_transport_arena_wipe(app);
}

int32_t zf_transport_ble_worker(void *context) {
    ZerofidoApp *app = context;
    ZfBleTransportState *state = NULL;

    if (!app) {
        return 0;
    }
    zf_telemetry_log("ble worker start");
    state = malloc(sizeof(*state));
    if (!state) {
        zf_telemetry_log_oom("ble state", sizeof(*state));
        zerofido_ui_set_status(app, "Bluetooth init failed");
        return 0;
    }
    memset(state, 0, sizeof(*state));
    if (!zf_app_transport_arena_acquire(app)) {
        zf_telemetry_log_oom("ble transport arena", ZF_TRANSPORT_ARENA_SIZE);
        zerofido_ui_set_status(app, "Bluetooth init failed");
        free(state);
        return 0;
    }

    state->link = zf_ble_link_start(furi_thread_get_current_id());
    if (!state->link) {
        zerofido_ui_set_status(app, "Bluetooth init failed");
        zf_app_transport_arena_release(app);
        free(state);
        return 0;
    }

    furi_mutex_acquire(app->ui_mutex, FuriWaitForever);
    app->transport_state = state;
    furi_mutex_release(app->ui_mutex);
    zerofido_ui_refresh_status_line(app);

    while (!state->stopping) {
        uint32_t flags = furi_thread_flags_wait(ZF_BLE_LINK_EVT_STOP | ZF_BLE_LINK_EVT_WAKE,
                                                FuriFlagWaitAny, ZF_BLE_WORKER_POLL_MS);

        if ((flags & FuriFlagError) == 0U && (flags & ZF_BLE_LINK_EVT_STOP) != 0U) {
            break;
        }
        zf_ble_link_service(state->link);
        zf_ble_update_connection(app, state);
        zf_ble_serve(app, state);
    }

    furi_mutex_acquire(app->ui_mutex, FuriWaitForever);
    if (app->transport_state == state) {
        app->transport_state = NULL;
    }
    furi_mutex_release(app->ui_mutex);
    zf_ble_link_stop(state->link);
    state->link = NULL;
    if (state->connected_reported) {
        zerofido_ui_set_transport_connected(app, false);
    }
    zf_app_transport_arena_release(app);
    zerofido_notify_reset(app);
    zf_crypto_secure_zero(state, sizeof(*state));
    free(state);
    zf_telemetry_log("ble worker stop");
    return 0;
}

void zf_transport_ble_stop(ZerofidoApp *app) {
    ZfBleTransportState *state = NULL;
    FuriThreadId id = 0;

    if (!app) {
        return;
    }
    furi_mutex_acquire(app->ui_mutex, FuriWaitForever);
    state = zf_ble_state(app);
    if (state) {
        state->stopping = true;
    }
    if (app->worker_thread) {
        id = furi_thread_get_id(app->worker_thread);
    }
    furi_mutex_release(app->ui_mutex);
    if (app->approval.done) {
        furi_semaphore_release(app->approval.done);
    }
    if (id) {
        furi_thread_flags_set(id, ZF_BLE_LINK_EVT_STOP);
    }
}

void zf_transport_ble_send_dispatch_result(ZerofidoApp *app,
                                           const ZfProtocolDispatchRequest *request,
                                           const ZfProtocolDispatchResult *result) {
    ZfBleTransportState *state = zf_ble_state(app);

    if (!state || !state->link || !request || !result) {
        return;
    }
    if (result->send_transport_error || result->response_len == 0U) {
        zf_ble_send_error(state->link,
                          result->transport_error ? result->transport_error : ZF_BLE_ERR_OTHER);
        return;
    }
    (void)zf_ble_link_send(state->link,
                           request->protocol == ZfTransportProtocolKindPing ? ZF_BLE_CMD_PING
                                                                            : ZF_BLE_CMD_MSG,
                           result->response, result->response_len);
}

/* Waits for the user's answer on the Flipper, telling the host "user presence needed". */
bool zf_transport_ble_wait_for_interaction(ZerofidoApp *app,
                                           ZfTransportSessionId current_session_id,
                                           bool *approved) {
    ZfBleTransportState *state = zf_ble_state(app);

    if (!state || !state->link || !approved || current_session_id != state->session_id) {
        return false;
    }

    zf_ble_send_keepalive(state, ZF_BLE_KEEPALIVE_UP_NEEDED);
    while (true) {
        if (furi_semaphore_acquire(app->approval.done, ZF_BLE_WAIT_SLICE_MS) == FuriStatusOk) {
            break;
        }
        if (state->stopping) {
            return false;
        }
        if (zf_ble_link_cancel_requested(state->link)) {
            /* CANCEL from the host or a disconnect aborts the pending approval. */
            bool canceled = zerofido_ui_cancel_pending_interaction(app);
            zf_ble_hide_interaction_if_needed(app, canceled);
            return false;
        }
        zf_ble_link_service(state->link);
        if (zf_ble_keepalive_due(state)) {
            zf_ble_send_keepalive(state, ZF_BLE_KEEPALIVE_UP_NEEDED);
        }
    }
    if (state->stopping) {
        return false;
    }

    furi_mutex_acquire(app->ui_mutex, FuriWaitForever);
    *approved = (app->approval.state == ZfApprovalApproved);
    furi_mutex_release(app->ui_mutex);
    return true;
}

void zf_transport_ble_notify_interaction_changed(ZerofidoApp *app) {
    /* The wait loop polls the approval semaphore; nothing to wake. */
    UNUSED(app);
}

/* Called by long CTAP handlers: reports cancellation and keeps the host's timer alive. */
uint8_t zf_transport_ble_poll_cbor_control(ZerofidoApp *app,
                                           ZfTransportSessionId current_session_id) {
    ZfBleTransportState *state = zf_ble_state(app);

    if (!state || !state->link || !state->processing || current_session_id != state->session_id) {
        return ZF_CTAP_SUCCESS;
    }
    if (state->stopping || zf_ble_link_cancel_requested(state->link)) {
        return ZF_CTAP_ERR_KEEPALIVE_CANCEL;
    }
    if (zf_ble_keepalive_due(state)) {
        zf_ble_send_keepalive(state, ZF_BLE_KEEPALIVE_PROCESSING);
    }
    return ZF_CTAP_SUCCESS;
}

#endif
