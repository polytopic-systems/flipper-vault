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
#include <stddef.h>
#include <stdint.h>

/*
 * CTAP 2.x Bluetooth LE framing ("Bluetooth Smart / Bluetooth Low Energy
 * Technology" transport). Pure C with no Flipper dependencies so the host test
 * suite can drive it directly.
 *
 * A message is an initial fragment  CMD(1) HLEN(1) LLEN(1) DATA...
 * followed by continuation fragments SEQ(1) DATA...  (SEQ 0x00..0x7F, wraps).
 * Fragments never exceed the negotiated fragment size (fidoControlPointLength
 * for requests, ATT_MTU - 3 for responses).
 */

#define ZF_BLE_CMD_PING 0x81U
#define ZF_BLE_CMD_KEEPALIVE 0x82U
#define ZF_BLE_CMD_MSG 0x83U
#define ZF_BLE_CMD_CANCEL 0xBEU
#define ZF_BLE_CMD_ERROR 0xBFU

#define ZF_BLE_ERR_INVALID_CMD 0x01U
#define ZF_BLE_ERR_INVALID_PAR 0x02U
#define ZF_BLE_ERR_INVALID_LEN 0x03U
#define ZF_BLE_ERR_INVALID_SEQ 0x04U
#define ZF_BLE_ERR_REQ_TIMEOUT 0x05U
#define ZF_BLE_ERR_BUSY 0x06U
#define ZF_BLE_ERR_OTHER 0x7FU

#define ZF_BLE_KEEPALIVE_PROCESSING 0x01U
#define ZF_BLE_KEEPALIVE_UP_NEEDED 0x02U

/* Default ATT_MTU 23 gives 20-byte fragments; 240 keeps one fragment per HCI packet. */
#define ZF_BLE_FRAGMENT_MIN 20U
#define ZF_BLE_FRAGMENT_MAX 240U
#define ZF_BLE_INIT_HEADER_LEN 3U
#define ZF_BLE_CONT_HEADER_LEN 1U
#define ZF_BLE_SEQ_MASK 0x7FU

/* Continuation frames must keep coming; a stalled request is abandoned. */
#define ZF_BLE_RX_TIMEOUT_MS 3000U
/* Keepalives while a request waits for the user or for long work. */
#define ZF_BLE_KEEPALIVE_INTERVAL_MS 300U

typedef enum {
    ZfBleRxNone = 0, /* fragment consumed (or ignored), nothing to do yet */
    ZfBleRxComplete, /* a full PING/MSG request is in the buffer */
    ZfBleRxError,    /* reply ERROR with *error; assembly state was reset */
    ZfBleRxCancel,   /* CANCEL command received */
} ZfBleRxResult;

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    uint16_t total_len;
    uint16_t received_len;
    uint8_t cmd;
    uint8_t next_seq;
    bool active;
    bool complete;
    uint32_t last_tick;
} ZfBleRx;

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t offset;
    uint16_t fragment_size;
    uint8_t cmd;
    uint8_t seq;
    bool started;
} ZfBleTx;

/* Maps a negotiated ATT_MTU to the fragment size we use in both directions. */
uint16_t zf_ble_fragment_size_for_mtu(uint16_t att_mtu);

void zf_ble_rx_init(ZfBleRx *rx, uint8_t *buffer, size_t capacity);
void zf_ble_rx_reset(ZfBleRx *rx);
/*
 * Feeds one control-point write. Requests are bounded by rx->capacity (the
 * app's max CTAP message size). On ZfBleRxComplete the request is
 * rx->buffer[0..rx->total_len) with command rx->cmd; it stays there until
 * zf_ble_rx_reset().
 */
ZfBleRxResult zf_ble_rx_feed(ZfBleRx *rx, const uint8_t *fragment, size_t fragment_len,
                             uint32_t now, uint8_t *error);
/* True (and assembly reset) if a partial request stalled for ZF_BLE_RX_TIMEOUT_MS. */
bool zf_ble_rx_check_timeout(ZfBleRx *rx, uint32_t now);

/* Response fragmentation. Returns false when every fragment was produced. */
bool zf_ble_tx_begin(ZfBleTx *tx, uint8_t cmd, const uint8_t *data, size_t len,
                     uint16_t fragment_size);
bool zf_ble_tx_next(ZfBleTx *tx, uint8_t *out, size_t out_capacity, size_t *out_len);
