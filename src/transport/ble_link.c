/*
 * Vault (fork of ZeroFIDO)
 * Copyright (C) 2026 Alex Stoyanov and Vault contributors
 *
 * Parts of the GATT/event handling are adapted from zerofido-tpm
 * (gg1nids12/zerofido-tpm, src/transport/ble_link.c, GPL-3.0-or-later).
 * Profile structure follows the official firmware's HID profile
 * (flipperdevices/flipperzero-firmware lib/ble_profile, GPL-3.0).
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

#include "ble_link.h"

#include <ble/core/ble_defs.h>
#include <bt/bt_service/bt.h>
#include <furi_ble/event_dispatcher.h>
#include <furi_ble/gatt.h>
#include <furi_ble/profile_interface.h>
#include <furi_hal_bt.h>
#include <furi_hal_version.h>
#include <gap.h>
#include <services/dev_info_service.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <storage/storage.h>

#include "../zerofido_storage.h"
#include "../zerofido_types.h"

#define TAG "VaultBle"

/* Bonds made with this profile live in the app's own data folder. */
#define ZF_BLE_KEYS_PATH ZF_APP_DATA_DIR "/.ble_fido.keys"
/* Lets the radio core flush bonds before the key file path changes (as the HID app does). */
#define ZF_BLE_NVM_SETTLE_MS 200U
#define ZF_BLE_FIDO_ATTRIBUTE_RECORDS 12U
#define ZF_BLE_DEFAULT_ATT_MTU 23U

/*
 * HCI framing as handed to service handlers (hci_uart_pckt -> hci_event_pckt
 * -> evt_blecore_aci). The ST WPAN headers declaring these are not part of the
 * FAP SDK, so the layouts and event codes are restated here.
 */
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t evt;
    uint8_t plen;
    uint8_t data[];
} ZfBleHciEvent;

typedef struct __attribute__((packed)) {
    uint16_t ecode;
    uint8_t data[];
} ZfBleVendorEvent;

#define ZF_HCI_DISCONNECTION_COMPLETE 0x05U
#define ZF_HCI_ENCRYPTION_CHANGE 0x08U
#define ZF_HCI_LE_META 0x3EU
#define ZF_HCI_LE_CONNECTION_COMPLETE 0x01U
#define ZF_HCI_LE_ENHANCED_CONNECTION_COMPLETE 0x0AU
#define ZF_HCI_VENDOR 0xFFU
#define ZF_ACI_GATT_ATTRIBUTE_MODIFIED 0x0C01U
#define ZF_ACI_ATT_EXCHANGE_MTU_RESP 0x0C03U

typedef enum {
    ZfBleCharControlPoint = 0,
    ZfBleCharStatus,
    ZfBleCharControlPointLength,
    ZfBleCharRevisionBitfield,
    ZfBleCharCount,
} ZfBleCharId;

typedef struct {
    const uint8_t *data;
    uint16_t len;
} ZfBleNotifyFrame;

struct ZfBleLink {
    FuriMutex *mutex;
    Bt *bt;
    FuriHalBleProfileBase *profile;
    FuriThreadId worker;

    /* GATT, filled by the profile start callback on the Bluetooth service thread. */
    bool gatt_ready;
    uint16_t svc_handle;
    BleGattCharacteristicInstance chars[ZfBleCharCount];
    BleServiceDevInfo *dev_info;
    GapSvcEventHandler *event_handler;

    /* Link state (BLE event thread / Bluetooth service thread). */
    bool running;
    bool advertising;
    bool connected;
    bool encrypted;
    uint32_t connection_id;
    uint16_t fragment_size;
    bool control_point_length_dirty;
    uint8_t revision_selected;

    /* Request reassembly and hand-off. */
    ZfBleRx rx;
    bool busy;
    bool cancel;
    uint8_t pending_error;
    uint8_t rx_buffer[ZF_MAX_MSG_SIZE];

    uint8_t tx_frame[ZF_BLE_FRAGMENT_MAX];
};

typedef struct {
    FuriHalBleProfileBase base;
    ZfBleLink *link;
} ZfBleProfile;

_Static_assert(offsetof(ZfBleProfile, base) == 0, "profile base must come first");

static volatile ZfBleLinkState zf_ble_link_state = ZfBleLinkStateOff;

/* FIDO characteristic UUIDs F1D0FFFx-DEAA-ECEE-B42F-C9BA7ED623BB, little-endian. */
#define ZF_BLE_FIDO_UUID(x)                                                                        \
    {0xBB, 0x23, 0xD6, 0x7E, 0xBA, 0xC9, 0x2F, 0xB4, 0xEE, 0xEC, 0xAA, 0xDE, (x), 0xFF, 0xD0, 0xF1}

static bool zf_ble_status_data(const void *context, const uint8_t **data, uint16_t *data_len) {
    const ZfBleNotifyFrame *frame = context;

    if (!data) {
        /* Attribute creation asks for the maximum value size. */
        *data_len = ZF_BLE_FRAGMENT_MAX;
        return false;
    }
    *data = frame ? frame->data : NULL;
    *data_len = frame ? frame->len : 0U;
    return false;
}

static const BleGattCharacteristicParams zf_ble_chars[ZfBleCharCount] = {
    [ZfBleCharControlPoint] =
        {
            .name = "fidoControlPoint",
            .data_prop_type = FlipperGattCharacteristicDataFixed,
            .data.fixed.length = ZF_BLE_FRAGMENT_MAX,
            .uuid.Char_UUID_128 = ZF_BLE_FIDO_UUID(0xF1),
            .uuid_type = UUID_TYPE_128,
            .char_properties = CHAR_PROP_WRITE,
            .security_permissions = ATTR_PERMISSION_AUTHEN_WRITE | ATTR_PERMISSION_ENCRY_WRITE,
            .gatt_evt_mask = GATT_NOTIFY_ATTRIBUTE_WRITE,
            .is_variable = CHAR_VALUE_LEN_VARIABLE,
        },
    [ZfBleCharStatus] =
        {
            .name = "fidoStatus",
            .data_prop_type = FlipperGattCharacteristicDataCallback,
            .data.callback.fn = zf_ble_status_data,
            .data.callback.context = NULL,
            .uuid.Char_UUID_128 = ZF_BLE_FIDO_UUID(0xF2),
            .uuid_type = UUID_TYPE_128,
            .char_properties = CHAR_PROP_NOTIFY,
            .security_permissions = ATTR_PERMISSION_AUTHEN_READ | ATTR_PERMISSION_ENCRY_READ,
            .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
            .is_variable = CHAR_VALUE_LEN_VARIABLE,
        },
    [ZfBleCharControlPointLength] =
        {
            .name = "fidoControlPointLength",
            .data_prop_type = FlipperGattCharacteristicDataFixed,
            .data.fixed.length = 2,
            .uuid.Char_UUID_128 = ZF_BLE_FIDO_UUID(0xF3),
            .uuid_type = UUID_TYPE_128,
            .char_properties = CHAR_PROP_READ,
            .security_permissions = ATTR_PERMISSION_AUTHEN_READ | ATTR_PERMISSION_ENCRY_READ,
            .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
            .is_variable = CHAR_VALUE_LEN_CONSTANT,
        },
    [ZfBleCharRevisionBitfield] =
        {
            .name = "fidoServiceRevisionBitfield",
            .data_prop_type = FlipperGattCharacteristicDataFixed,
            .data.fixed.length = 1,
            .uuid.Char_UUID_128 = ZF_BLE_FIDO_UUID(0xF4),
            .uuid_type = UUID_TYPE_128,
            .char_properties = CHAR_PROP_READ | CHAR_PROP_WRITE,
            .security_permissions = ATTR_PERMISSION_AUTHEN_READ | ATTR_PERMISSION_ENCRY_READ |
                                    ATTR_PERMISSION_AUTHEN_WRITE | ATTR_PERMISSION_ENCRY_WRITE,
            .gatt_evt_mask = GATT_NOTIFY_ATTRIBUTE_WRITE,
            .is_variable = CHAR_VALUE_LEN_CONSTANT,
        },
};

/* ---- helpers (caller holds link->mutex) --------------------------------------------------- */

static void zf_ble_publish_state_locked(const ZfBleLink *link) {
    ZfBleLinkState state = ZfBleLinkStateOff;

    if (link->running) {
        if (link->connected) {
            state = link->encrypted ? ZfBleLinkStateSecured : ZfBleLinkStateConnected;
        } else if (link->advertising) {
            state = ZfBleLinkStateAdvertising;
        }
    }
    zf_ble_link_state = state;
}

static void zf_ble_wake_worker_locked(const ZfBleLink *link) {
    if (link->worker) {
        furi_thread_flags_set(link->worker, ZF_BLE_LINK_EVT_WAKE);
    }
}

static void zf_ble_set_error_locked(ZfBleLink *link, uint8_t error) {
    link->pending_error = error;
    zf_ble_wake_worker_locked(link);
}

/* One write to fidoControlPoint (BLE event thread). */
static void zf_ble_on_control_point(ZfBleLink *link, const uint8_t *data, uint16_t len,
                                    uint16_t offset) {
    uint8_t error = 0;

    furi_mutex_acquire(link->mutex, FuriWaitForever);
    if ((offset & 0x7FFFU) != 0U) {
        /* Fragments are bounded by fidoControlPointLength; long writes are not expected. */
        zf_ble_rx_reset(&link->rx);
        zf_ble_set_error_locked(link, ZF_BLE_ERR_INVALID_LEN);
    } else if (link->busy) {
        if (len > 0U && data[0] == ZF_BLE_CMD_CANCEL) {
            link->cancel = true;
            zf_ble_wake_worker_locked(link);
        } else if (len > 0U && (data[0] & 0x80U) != 0U) {
            zf_ble_set_error_locked(link, ZF_BLE_ERR_BUSY);
        }
    } else {
        switch (zf_ble_rx_feed(&link->rx, data, len, furi_get_tick(), &error)) {
        case ZfBleRxComplete:
            zf_ble_wake_worker_locked(link);
            break;
        case ZfBleRxError:
            zf_ble_set_error_locked(link, error);
            break;
        case ZfBleRxCancel:
            /* Nothing is running: drop any queued or partial request. */
            zf_ble_rx_reset(&link->rx);
            break;
        case ZfBleRxNone:
        default:
            break;
        }
    }
    furi_mutex_release(link->mutex);
}

/* BLE event thread. Only writes to our own attributes are acknowledged. */
static BleEventAckStatus zf_ble_event_handler(void *event, void *context) {
    ZfBleLink *link = context;
    const ZfBleHciEvent *hci = event;

    if (!link || !hci) {
        return BleEventNotAck;
    }

    if (hci->evt == ZF_HCI_LE_META &&
        (hci->data[0] == ZF_HCI_LE_CONNECTION_COMPLETE ||
         hci->data[0] == ZF_HCI_LE_ENHANCED_CONNECTION_COMPLETE) &&
        hci->data[1] == 0x00U) {
        furi_mutex_acquire(link->mutex, FuriWaitForever);
        link->connected = true;
        link->encrypted = false;
        link->connection_id++;
        if (link->connection_id == 0U) {
            link->connection_id = 1U;
        }
        link->fragment_size = zf_ble_fragment_size_for_mtu(ZF_BLE_DEFAULT_ATT_MTU);
        link->control_point_length_dirty = true;
        zf_ble_rx_reset(&link->rx);
        link->pending_error = 0;
        zf_ble_publish_state_locked(link);
        zf_ble_wake_worker_locked(link);
        furi_mutex_release(link->mutex);
        return BleEventNotAck;
    }
    if (hci->evt == ZF_HCI_DISCONNECTION_COMPLETE) {
        furi_mutex_acquire(link->mutex, FuriWaitForever);
        link->connected = false;
        link->encrypted = false;
        link->fragment_size = zf_ble_fragment_size_for_mtu(ZF_BLE_DEFAULT_ATT_MTU);
        zf_ble_rx_reset(&link->rx);
        link->pending_error = 0;
        if (link->busy) {
            link->cancel = true;
        }
        zf_ble_publish_state_locked(link);
        zf_ble_wake_worker_locked(link);
        furi_mutex_release(link->mutex);
        return BleEventNotAck;
    }
    if (hci->evt == ZF_HCI_ENCRYPTION_CHANGE) {
        furi_mutex_acquire(link->mutex, FuriWaitForever);
        if (hci->data[0] == 0x00U) {
            link->encrypted = hci->data[3] != 0U;
        }
        zf_ble_publish_state_locked(link);
        zf_ble_wake_worker_locked(link);
        furi_mutex_release(link->mutex);
        return BleEventNotAck;
    }
    if (hci->evt != ZF_HCI_VENDOR) {
        return BleEventNotAck;
    }

    const ZfBleVendorEvent *vendor = (const ZfBleVendorEvent *)hci->data;
    if (vendor->ecode == ZF_ACI_ATT_EXCHANGE_MTU_RESP) {
        const aci_att_exchange_mtu_resp_event_rp0 *mtu = (const void *)vendor->data;

        furi_mutex_acquire(link->mutex, FuriWaitForever);
        link->fragment_size = zf_ble_fragment_size_for_mtu(mtu->Server_RX_MTU);
        link->control_point_length_dirty = true;
        zf_ble_wake_worker_locked(link);
        furi_mutex_release(link->mutex);
        return BleEventNotAck; /* GAP tracks the MTU as well */
    }
    if (vendor->ecode != ZF_ACI_GATT_ATTRIBUTE_MODIFIED) {
        return BleEventNotAck;
    }

    const aci_gatt_attribute_modified_event_rp0 *mod = (const void *)vendor->data;
    if (mod->Attr_Handle == link->chars[ZfBleCharControlPoint].handle + 1U) {
        zf_ble_on_control_point(link, mod->Attr_Data, mod->Attr_Data_Length, mod->Offset);
        return BleEventAckFlowEnable;
    }
    if (mod->Attr_Handle == link->chars[ZfBleCharRevisionBitfield].handle + 1U) {
        furi_mutex_acquire(link->mutex, FuriWaitForever);
        if (mod->Attr_Data_Length > 0U) {
            link->revision_selected = mod->Attr_Data[0];
        }
        furi_mutex_release(link->mutex);
        return BleEventAckFlowEnable;
    }
    if (mod->Attr_Handle == link->chars[ZfBleCharStatus].handle + 2U) {
        /* fidoStatus CCCD: notifications switched on or off. */
        return BleEventAckFlowEnable;
    }
    return BleEventNotAck;
}

static void zf_ble_bt_status_callback(BtStatus status, void *context) {
    ZfBleLink *link = context;

    if (!link) {
        return;
    }
    furi_mutex_acquire(link->mutex, FuriWaitForever);
    link->advertising = status == BtStatusAdvertising;
    zf_ble_publish_state_locked(link);
    furi_mutex_release(link->mutex);
}

static void zf_ble_write_control_point_length(ZfBleLink *link, uint16_t fragment) {
    uint8_t value[2] = {(uint8_t)(fragment >> 8), (uint8_t)fragment};

    /* ble_gatt_characteristic_update() returns true on failure. */
    if (ble_gatt_characteristic_update(link->svc_handle, &link->chars[ZfBleCharControlPointLength],
                                       value)) {
        FURI_LOG_W(TAG, "cpLength update failed");
    }
}

/* ---- profile template (runs on the Bluetooth service thread) ------------------------------ */

static const FuriHalBleProfileTemplate zf_ble_profile_template;

static FuriHalBleProfileBase *zf_ble_profile_start(FuriHalBleProfileParams params) {
    ZfBleLink *link = params;
    ZfBleProfile *profile = malloc(sizeof(ZfBleProfile));
    const Service_UUID_t fido_uuid = {.Service_UUID_16 = ZF_BLE_FIDO_SERVICE_UUID};
    uint8_t revision = ZF_BLE_REVISION_FIDO2;

    profile->base.config = &zf_ble_profile_template;
    profile->link = link;
    link->gatt_ready = false;
    link->dev_info = ble_svc_dev_info_start();

    if (ble_gatt_service_add(UUID_TYPE_16, &fido_uuid, PRIMARY_SERVICE,
                             ZF_BLE_FIDO_ATTRIBUTE_RECORDS, &link->svc_handle)) {
        for (size_t i = 0; i < ZfBleCharCount; ++i) {
            ble_gatt_characteristic_init(link->svc_handle, &zf_ble_chars[i], &link->chars[i]);
        }
        zf_ble_write_control_point_length(link, ZF_BLE_FRAGMENT_MIN);
        if (ble_gatt_characteristic_update(link->svc_handle,
                                           &link->chars[ZfBleCharRevisionBitfield], &revision)) {
            FURI_LOG_W(TAG, "revision update failed");
        }
        link->gatt_ready = true;
    } else {
        FURI_LOG_E(TAG, "FIDO service add failed");
    }
    /* Register last: the handler reads the characteristic handles. */
    link->event_handler = ble_event_dispatcher_register_svc_handler(zf_ble_event_handler, link);
    return &profile->base;
}

static void zf_ble_profile_stop(FuriHalBleProfileBase *base) {
    ZfBleProfile *profile = (ZfBleProfile *)base;
    ZfBleLink *link = NULL;

    if (!profile || profile->base.config != &zf_ble_profile_template) {
        return;
    }
    link = profile->link;
    if (link->event_handler) {
        ble_event_dispatcher_unregister_svc_handler(link->event_handler);
        link->event_handler = NULL;
    }
    if (link->gatt_ready) {
        for (size_t i = 0; i < ZfBleCharCount; ++i) {
            ble_gatt_characteristic_delete(link->svc_handle, &link->chars[i]);
        }
        ble_gatt_service_delete(link->svc_handle);
        link->gatt_ready = false;
    }
    if (link->dev_info) {
        ble_svc_dev_info_stop(link->dev_info);
        link->dev_info = NULL;
    }
    free(profile);
}

#define ZF_BLE_CONN_INTERVAL_MIN 0x0006U /* 7.5 ms */
#define ZF_BLE_CONN_INTERVAL_MAX 0x0024U /* 45 ms */

static void zf_ble_profile_gap_config(GapConfig *config, FuriHalBleProfileParams params) {
    const char *local_name = furi_hal_version_get_ble_local_device_name_ptr();
    const char *flipper_name = furi_hal_version_get_name_ptr();

    UNUSED(params);
    memset(config, 0, sizeof(*config));
    config->adv_service.UUID_Type = UUID_TYPE_16;
    config->adv_service.Service_UUID_16 = ZF_BLE_FIDO_SERVICE_UUID;
    config->appearance_char = 0x0000U;
    config->bonding_mode = true;
    /* Numeric comparison: the code shows on the host and on the Flipper. */
    config->pairing_method = GapPairingPinCodeVerifyYesNo;
    config->conn_param.conn_int_min = ZF_BLE_CONN_INTERVAL_MIN;
    config->conn_param.conn_int_max = ZF_BLE_CONN_INTERVAL_MAX;
    config->conn_param.slave_latency = 0;
    config->conn_param.supervisor_timeout = 0;
    memcpy(config->mac_address, furi_hal_version_get_ble_mac(), sizeof(config->mac_address));
    /* A distinct address keeps host pairings apart from the default and HID profiles. */
    config->mac_address[2] ^= 0x5AU;
    /* adv_name[0] is the AD type byte the firmware keeps at the front of the local name. */
    snprintf(config->adv_name, sizeof(config->adv_name), "%cVault %s",
             local_name ? local_name[0] : 0x09, flipper_name ? flipper_name : "Flipper");
}

static const FuriHalBleProfileTemplate zf_ble_profile_template = {
    .start = zf_ble_profile_start,
    .stop = zf_ble_profile_stop,
    .get_gap_config = zf_ble_profile_gap_config,
};

/* ---- public ------------------------------------------------------------------------------- */

ZfBleLink *zf_ble_link_start(FuriThreadId worker) {
    ZfBleLink *link = malloc(sizeof(ZfBleLink));
    Storage *storage = NULL;

    if (!link) {
        return NULL;
    }
    memset(link, 0, sizeof(*link));
    link->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    link->worker = worker;
    link->fragment_size = zf_ble_fragment_size_for_mtu(ZF_BLE_DEFAULT_ATT_MTU);
    zf_ble_rx_init(&link->rx, link->rx_buffer, sizeof(link->rx_buffer));

    storage = furi_record_open(RECORD_STORAGE);
    (void)zf_storage_ensure_app_data_dir(storage);
    furi_record_close(RECORD_STORAGE);

    link->bt = furi_record_open(RECORD_BT);
    bt_disconnect(link->bt);
    furi_delay_ms(ZF_BLE_NVM_SETTLE_MS);
    bt_keys_storage_set_storage_path(link->bt, ZF_BLE_KEYS_PATH);
    link->profile = bt_profile_start(link->bt, &zf_ble_profile_template, link);
    if (!link->profile || !link->gatt_ready) {
        FURI_LOG_E(TAG, "profile start failed");
        bt_keys_storage_set_default_path(link->bt);
        if (!bt_profile_restore_default(link->bt)) {
            FURI_LOG_E(TAG, "default profile restore failed");
        }
        furi_record_close(RECORD_BT);
        furi_mutex_free(link->mutex);
        free(link);
        zf_ble_link_state = ZfBleLinkStateOff;
        return NULL;
    }

    furi_mutex_acquire(link->mutex, FuriWaitForever);
    link->running = true;
    link->advertising = true;
    zf_ble_publish_state_locked(link);
    furi_mutex_release(link->mutex);
    bt_set_status_changed_callback(link->bt, zf_ble_bt_status_callback, link);
    furi_hal_bt_start_advertising();
    FURI_LOG_I(TAG, "FIDO profile up");
    return link;
}

void zf_ble_link_stop(ZfBleLink *link) {
    if (!link) {
        return;
    }

    bt_set_status_changed_callback(link->bt, NULL, NULL);
    furi_mutex_acquire(link->mutex, FuriWaitForever);
    link->running = false;
    link->worker = NULL;
    zf_ble_publish_state_locked(link);
    furi_mutex_release(link->mutex);

    bt_disconnect(link->bt);
    furi_delay_ms(ZF_BLE_NVM_SETTLE_MS);
    bt_keys_storage_set_default_path(link->bt);
    /* Runs zf_ble_profile_stop(), which unregisters the event handler. */
    if (!bt_profile_restore_default(link->bt)) {
        FURI_LOG_E(TAG, "default profile restore failed");
    }
    furi_record_close(RECORD_BT);
    FURI_LOG_I(TAG, "default profile restored");

    furi_mutex_acquire(link->mutex, FuriWaitForever);
    zf_ble_rx_reset(&link->rx);
    furi_mutex_release(link->mutex);
    furi_mutex_free(link->mutex);
    memset(link, 0, sizeof(*link));
    free(link);
    zf_ble_link_state = ZfBleLinkStateOff;
}

void zf_ble_link_service(ZfBleLink *link) {
    bool update_length = false;
    uint16_t fragment = ZF_BLE_FRAGMENT_MIN;

    if (!link) {
        return;
    }
    furi_mutex_acquire(link->mutex, FuriWaitForever);
    if (zf_ble_rx_check_timeout(&link->rx, furi_get_tick())) {
        link->pending_error = ZF_BLE_ERR_REQ_TIMEOUT;
    }
    if (link->control_point_length_dirty && link->gatt_ready) {
        link->control_point_length_dirty = false;
        update_length = true;
        fragment = link->fragment_size;
    }
    furi_mutex_release(link->mutex);

    if (update_length) {
        zf_ble_write_control_point_length(link, fragment);
    }
}

bool zf_ble_link_take_request(ZfBleLink *link, uint8_t *cmd, uint8_t *out, size_t out_capacity,
                              size_t *out_len) {
    bool ok = false;

    if (!link || !cmd || !out || !out_len) {
        return false;
    }
    furi_mutex_acquire(link->mutex, FuriWaitForever);
    if (link->rx.complete && !link->busy) {
        if (link->rx.total_len <= out_capacity) {
            if (link->rx.total_len > 0U) {
                memcpy(out, link->rx.buffer, link->rx.total_len);
            }
            *out_len = link->rx.total_len;
            *cmd = link->rx.cmd;
            link->busy = true;
            link->cancel = false;
            ok = true;
        }
        zf_ble_rx_reset(&link->rx);
    }
    furi_mutex_release(link->mutex);
    return ok;
}

void zf_ble_link_end_request(ZfBleLink *link) {
    if (!link) {
        return;
    }
    furi_mutex_acquire(link->mutex, FuriWaitForever);
    link->busy = false;
    link->cancel = false;
    furi_mutex_release(link->mutex);
}

uint8_t zf_ble_link_take_error(ZfBleLink *link) {
    uint8_t error = 0;

    if (!link) {
        return 0;
    }
    furi_mutex_acquire(link->mutex, FuriWaitForever);
    error = link->pending_error;
    link->pending_error = 0;
    furi_mutex_release(link->mutex);
    return error;
}

bool zf_ble_link_cancel_requested(ZfBleLink *link) {
    bool cancel = false;

    if (!link) {
        return true;
    }
    furi_mutex_acquire(link->mutex, FuriWaitForever);
    cancel = link->cancel || !link->connected;
    furi_mutex_release(link->mutex);
    return cancel;
}

uint32_t zf_ble_link_connection_id(ZfBleLink *link) {
    uint32_t id = 0;

    if (!link) {
        return 0;
    }
    furi_mutex_acquire(link->mutex, FuriWaitForever);
    id = link->connection_id;
    furi_mutex_release(link->mutex);
    return id;
}

bool zf_ble_link_is_connected(ZfBleLink *link) {
    bool connected = false;

    if (!link) {
        return false;
    }
    furi_mutex_acquire(link->mutex, FuriWaitForever);
    connected = link->connected;
    furi_mutex_release(link->mutex);
    return connected;
}

/* Worker thread: one message, cut into notifications of the current fragment size. */
bool zf_ble_link_send(ZfBleLink *link, uint8_t cmd, const uint8_t *data, size_t len) {
    ZfBleNotifyFrame frame = {.data = NULL, .len = 0};
    ZfBleTx tx;
    uint16_t fragment = ZF_BLE_FRAGMENT_MIN;
    size_t frame_len = 0;
    bool ok = false;

    if (!link) {
        return false;
    }
    furi_mutex_acquire(link->mutex, FuriWaitForever);
    fragment = link->fragment_size;
    ok = link->connected && link->gatt_ready;
    furi_mutex_release(link->mutex);
    if (!ok || !zf_ble_tx_begin(&tx, cmd, data, len, fragment)) {
        return false;
    }

    frame.data = link->tx_frame;
    while (ok && zf_ble_tx_next(&tx, link->tx_frame, sizeof(link->tx_frame), &frame_len)) {
        frame.len = (uint16_t)frame_len;
        ok = !ble_gatt_characteristic_update(link->svc_handle, &link->chars[ZfBleCharStatus],
                                             &frame);
        if (ok && !zf_ble_link_is_connected(link)) {
            ok = false;
        }
    }
    memset(link->tx_frame, 0, sizeof(link->tx_frame));
    return ok;
}

ZfBleLinkState zf_ble_link_get_state(void) {
    return zf_ble_link_state;
}
