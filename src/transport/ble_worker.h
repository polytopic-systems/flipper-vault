/*
 * Vault (fork of ZeroFIDO)
 * Copyright (C) 2026 Alex Stoyanov and Vault contributors
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

#include <stdbool.h>
#include <stdint.h>

#include "../zerofido_types.h"
#include "dispatch.h"

#if ZF_BLE_ENABLED

#include "ble_link.h"

typedef struct ZerofidoApp ZerofidoApp;

/*
 * Bluetooth LE transport adapter (CTAP 2.x BLE). The worker owns the FIDO BLE
 * profile while running: it switches the Flipper's Bluetooth to the FIDO
 * profile on start and restores the default profile on stop. CTAP2 requests
 * (MSG) run on the worker thread through zerofido_handle_ctap2(), the same
 * dispatcher USB HID uses; U2F over BLE is not routed.
 */
int32_t zf_transport_ble_worker(void *context);
void zf_transport_ble_stop(ZerofidoApp *app);
void zf_transport_ble_send_dispatch_result(ZerofidoApp *app,
                                           const ZfProtocolDispatchRequest *request,
                                           const ZfProtocolDispatchResult *result);
bool zf_transport_ble_wait_for_interaction(ZerofidoApp *app,
                                           ZfTransportSessionId current_session_id, bool *approved);
void zf_transport_ble_notify_interaction_changed(ZerofidoApp *app);
uint8_t zf_transport_ble_poll_cbor_control(ZerofidoApp *app,
                                           ZfTransportSessionId current_session_id);

/*
 * UI getter: Bluetooth link state (Off / Advertising / Connected / Secured).
 * Lock-free, callable from any thread, including with ui_mutex held. Off
 * whenever the BLE transport is not the running transport.
 */
static inline ZfBleLinkState zf_transport_ble_link_state(void) {
    return zf_ble_link_get_state();
}

#endif
