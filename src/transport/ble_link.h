/*
 * Vault (fork of ZeroFIDO)
 * Copyright (C) 2026 Alex Stoyanov and Vault contributors
 *
 * Parts of the GATT/event handling are adapted from zerofido-tpm
 * (gg1nids12/zerofido-tpm, src/transport/ble_link.c, GPL-3.0-or-later).
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
#include <stddef.h>
#include <stdint.h>

#include <furi.h>

#include "ble_framing.h"

/*
 * Bluetooth LE FIDO link: owns the BLE profile (FIDO service 0xFFFD plus the
 * firmware Device Information service), the app's own bond file, and request
 * reassembly. Control-point writes arrive on the BLE event thread and are only
 * reassembled there; everything slow happens on the worker thread, which the
 * link wakes with ZF_BLE_LINK_EVT_WAKE.
 */

#define ZF_BLE_LINK_EVT_STOP (1U << 0)
#define ZF_BLE_LINK_EVT_WAKE (1U << 1)

#define ZF_BLE_FIDO_SERVICE_UUID 0xFFFDU
#define ZF_BLE_REVISION_FIDO2 0x20U

typedef enum {
    ZfBleLinkStateOff = 0,     /* profile not running */
    ZfBleLinkStateAdvertising, /* waiting for a host */
    ZfBleLinkStateConnected,   /* host connected, link not yet encrypted */
    ZfBleLinkStateSecured,     /* encrypted (paired/bonded) link */
} ZfBleLinkState;

typedef struct ZfBleLink ZfBleLink;

/* Switches the Flipper's Bluetooth to the FIDO profile. NULL on failure (default restored). */
ZfBleLink *zf_ble_link_start(FuriThreadId worker);
/* Restores the default Bluetooth profile and bond file, then frees the link. */
void zf_ble_link_stop(ZfBleLink *link);

/* Worker-side housekeeping: MTU-driven control point length, RX timeout. */
void zf_ble_link_service(ZfBleLink *link);
/* Copies a complete request out and marks the link busy. */
bool zf_ble_link_take_request(ZfBleLink *link, uint8_t *cmd, uint8_t *out, size_t out_capacity,
                              size_t *out_len);
/* Clears busy; call before sending the reply (hosts send the next request at once). */
void zf_ble_link_end_request(ZfBleLink *link);
/* Returns a pending framing error code to report, or 0. */
uint8_t zf_ble_link_take_error(ZfBleLink *link);
/* CANCEL received or the host disconnected while the current request runs. */
bool zf_ble_link_cancel_requested(ZfBleLink *link);
/* Connection counter: changes on every new connection (CTAP channel identity). */
uint32_t zf_ble_link_connection_id(ZfBleLink *link);
bool zf_ble_link_is_connected(ZfBleLink *link);
/* Sends one message as notifications on fidoStatus. */
bool zf_ble_link_send(ZfBleLink *link, uint8_t cmd, const uint8_t *data, size_t len);

/* Lock-free snapshot for the UI; valid from any thread. */
ZfBleLinkState zf_ble_link_get_state(void);
