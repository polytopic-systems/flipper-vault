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

#include "ble_framing.h"

#include <string.h>

uint16_t zf_ble_fragment_size_for_mtu(uint16_t att_mtu) {
    uint16_t fragment = att_mtu > 3U ? (uint16_t)(att_mtu - 3U) : 0U;

    if (fragment < ZF_BLE_FRAGMENT_MIN) {
        fragment = ZF_BLE_FRAGMENT_MIN;
    }
    if (fragment > ZF_BLE_FRAGMENT_MAX) {
        fragment = ZF_BLE_FRAGMENT_MAX;
    }
    return fragment;
}

void zf_ble_rx_init(ZfBleRx *rx, uint8_t *buffer, size_t capacity) {
    if (!rx) {
        return;
    }
    memset(rx, 0, sizeof(*rx));
    rx->buffer = buffer;
    rx->capacity = capacity > 0xFFFFU ? 0xFFFFU : capacity;
}

void zf_ble_rx_reset(ZfBleRx *rx) {
    if (!rx) {
        return;
    }
    if (rx->buffer && rx->total_len > 0U) {
        volatile uint8_t *bytes = rx->buffer;
        for (size_t i = 0; i < rx->total_len && i < rx->capacity; ++i) {
            bytes[i] = 0;
        }
    }
    rx->total_len = 0;
    rx->received_len = 0;
    rx->cmd = 0;
    rx->next_seq = 0;
    rx->active = false;
    rx->complete = false;
}

static ZfBleRxResult zf_ble_rx_fail(ZfBleRx *rx, uint8_t code, uint8_t *error) {
    zf_ble_rx_reset(rx);
    if (error) {
        *error = code;
    }
    return ZfBleRxError;
}

static ZfBleRxResult zf_ble_rx_append(ZfBleRx *rx, const uint8_t *data, size_t len,
                                      uint8_t *error) {
    size_t remaining = (size_t)rx->total_len - rx->received_len;

    if (len > remaining) {
        return zf_ble_rx_fail(rx, ZF_BLE_ERR_INVALID_LEN, error);
    }
    if (len > 0U) {
        memcpy(rx->buffer + rx->received_len, data, len);
        rx->received_len = (uint16_t)(rx->received_len + len);
    }
    if (rx->received_len == rx->total_len) {
        rx->active = false;
        rx->complete = true;
        return ZfBleRxComplete;
    }
    return ZfBleRxNone;
}

ZfBleRxResult zf_ble_rx_feed(ZfBleRx *rx, const uint8_t *fragment, size_t fragment_len,
                             uint32_t now, uint8_t *error) {
    uint8_t cmd = 0;
    uint16_t total = 0;

    if (!rx || !rx->buffer || !fragment || fragment_len == 0U) {
        return ZfBleRxNone;
    }
    if (fragment_len > ZF_BLE_FRAGMENT_MAX) {
        if (rx->complete) {
            if (error) {
                *error = ZF_BLE_ERR_INVALID_LEN;
            }
            return ZfBleRxError;
        }
        return zf_ble_rx_fail(rx, ZF_BLE_ERR_INVALID_LEN, error);
    }

    if ((fragment[0] & 0x80U) == 0U) {
        /* Continuation: only meaningful while a request is being assembled. */
        if (!rx->active) {
            return ZfBleRxNone;
        }
        if (fragment[0] != rx->next_seq) {
            return zf_ble_rx_fail(rx, ZF_BLE_ERR_INVALID_SEQ, error);
        }
        rx->next_seq = (uint8_t)((rx->next_seq + 1U) & ZF_BLE_SEQ_MASK);
        rx->last_tick = now;
        return zf_ble_rx_append(rx, fragment + ZF_BLE_CONT_HEADER_LEN,
                                fragment_len - ZF_BLE_CONT_HEADER_LEN, error);
    }

    cmd = fragment[0];
    if (cmd == ZF_BLE_CMD_CANCEL) {
        /* A finished request belongs to the worker; it decides what CANCEL means. */
        if (!rx->complete) {
            zf_ble_rx_reset(rx);
        }
        return ZfBleRxCancel;
    }
    if (rx->complete) {
        /* One request at a time: the previous one is still being processed. */
        if (error) {
            *error = ZF_BLE_ERR_BUSY;
        }
        return ZfBleRxError;
    }
    if (cmd != ZF_BLE_CMD_PING && cmd != ZF_BLE_CMD_MSG) {
        return zf_ble_rx_fail(rx, ZF_BLE_ERR_INVALID_CMD, error);
    }
    if (fragment_len < ZF_BLE_INIT_HEADER_LEN) {
        return zf_ble_rx_fail(rx, ZF_BLE_ERR_INVALID_LEN, error);
    }
    total = (uint16_t)(((uint16_t)fragment[1] << 8) | fragment[2]);
    if (total > rx->capacity) {
        return zf_ble_rx_fail(rx, ZF_BLE_ERR_INVALID_LEN, error);
    }

    /* A new initial fragment restarts any abandoned partial request. */
    zf_ble_rx_reset(rx);
    rx->cmd = cmd;
    rx->total_len = total;
    rx->received_len = 0;
    rx->next_seq = 0;
    rx->active = true;
    rx->last_tick = now;
    return zf_ble_rx_append(rx, fragment + ZF_BLE_INIT_HEADER_LEN,
                            fragment_len - ZF_BLE_INIT_HEADER_LEN, error);
}

bool zf_ble_rx_check_timeout(ZfBleRx *rx, uint32_t now) {
    if (!rx || !rx->active) {
        return false;
    }
    if ((uint32_t)(now - rx->last_tick) < ZF_BLE_RX_TIMEOUT_MS) {
        return false;
    }
    zf_ble_rx_reset(rx);
    return true;
}

bool zf_ble_tx_begin(ZfBleTx *tx, uint8_t cmd, const uint8_t *data, size_t len,
                     uint16_t fragment_size) {
    if (!tx || len > 0xFFFFU || (len > 0U && !data)) {
        return false;
    }
    if (fragment_size < ZF_BLE_FRAGMENT_MIN) {
        fragment_size = ZF_BLE_FRAGMENT_MIN;
    }
    if (fragment_size > ZF_BLE_FRAGMENT_MAX) {
        fragment_size = ZF_BLE_FRAGMENT_MAX;
    }
    memset(tx, 0, sizeof(*tx));
    tx->cmd = cmd;
    tx->data = data;
    tx->len = len;
    tx->fragment_size = fragment_size;
    return true;
}

bool zf_ble_tx_next(ZfBleTx *tx, uint8_t *out, size_t out_capacity, size_t *out_len) {
    size_t chunk = 0;
    size_t header = 0;

    if (!tx || !out || !out_len || out_capacity < tx->fragment_size) {
        return false;
    }
    if (tx->started && tx->offset >= tx->len) {
        return false;
    }

    if (!tx->started) {
        out[0] = tx->cmd;
        out[1] = (uint8_t)(tx->len >> 8);
        out[2] = (uint8_t)tx->len;
        header = ZF_BLE_INIT_HEADER_LEN;
        tx->started = true;
    } else {
        out[0] = tx->seq;
        tx->seq = (uint8_t)((tx->seq + 1U) & ZF_BLE_SEQ_MASK);
        header = ZF_BLE_CONT_HEADER_LEN;
    }
    chunk = tx->len - tx->offset;
    if (chunk > (size_t)tx->fragment_size - header) {
        chunk = (size_t)tx->fragment_size - header;
    }
    if (chunk > 0U) {
        memcpy(out + header, tx->data + tx->offset, chunk);
    }
    tx->offset += chunk;
    *out_len = header + chunk;
    return true;
}
