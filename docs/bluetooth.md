# Bluetooth LE transport

Vault speaks CTAP2 over Bluetooth Low Energy as defined in the CTAP 2.x specification
("Bluetooth Smart / Bluetooth Low Energy Technology"). In practice only Windows 10/11 uses
paired Bluetooth security keys for WebAuthn; browsers on Android, iOS and macOS do not.

Sources: `src/transport/ble_framing.[ch]` (framing state machine, no Flipper dependencies),
`src/transport/ble_link.[ch]` (BLE profile, GATT, bonds, reassembly hand-off),
`src/transport/ble_worker.[ch]` (transport adapter: requests, keepalive, cancel).
Parts are adapted from zerofido-tpm (`gg1nids12/zerofido-tpm`, GPL-3.0-or-later) and follow the
official firmware's HID profile.

## Build

- `ZEROFIDO_PROFILE=full` (the default) builds USB HID, NFC and Bluetooth; the transport is chosen
  at runtime (Settings, persisted as `transport_mode` 2 = "Bluetooth").
- `ZEROFIDO_BLE=0` drops Bluetooth from the full profile. The `usb` and `nfc` profiles never
  contain it (`ble_*.c` is excluded and `ZF_BLE_ENABLED=0`).
- `VAULT_DEV_DEFAULT_BLE=1` (development only) makes Bluetooth the first-launch transport. In USB
  mode the app replaces the Flipper's USB serial with a FIDO HID device, which stops `ufbt launch`
  and the CLI until someone exits the app; in Bluetooth (or NFC) mode the serial CLI stays up. The
  flag only changes the default, so a transport saved in `runtime_config.bin` still wins.

## Profile

While the Bluetooth transport runs it replaces the Flipper's default Bluetooth profile (the
mobile-app serial profile) and restores it when the transport stops or the app exits
(`bt_keys_storage_set_default_path`, then `bt_profile_restore_default`).

- Advertising: name `Vault <Flipper name>`, 16-bit service UUID `0xFFFD`, connectable. The
  address is the Flipper's own with byte 2 XOR `0x5A`, so hosts keep this pairing apart from the
  mobile-app and HID profiles.
- Pairing: bonding on, numeric comparison (`GapPairingPinCodeVerifyYesNo`): the firmware shows the
  code on the Flipper with Cancel/OK, the host shows the same code.
- Bonds are stored in the app's own file `/ext/apps_data/flipper_vault/.ble_fido.keys`, never in
  the system `bt.keys`.
- Services: FIDO `0xFFFD` and the firmware Device Information service (`ble_svc_dev_info_start`).

| Characteristic | UUID | Properties | Security |
|---|---|---|---|
| fidoControlPoint | `F1D0FFF1-DEAA-ECEE-B42F-C9BA7ED623BB` | write | encrypted + authenticated write |
| fidoStatus | `F1D0FFF2-DEAA-ECEE-B42F-C9BA7ED623BB` | notify | encrypted + authenticated (value) |
| fidoControlPointLength | `F1D0FFF3-DEAA-ECEE-B42F-C9BA7ED623BB` | read | encrypted + authenticated read |
| fidoServiceRevisionBitfield | `F1D0FFF4-DEAA-ECEE-B42F-C9BA7ED623BB` | read, write | encrypted + authenticated |

The revision bitfield offers `0x20` (FIDO2) only: MSG payloads are CTAP2 (command byte + CBOR) and
go to the same CTAP2 dispatcher USB uses. Legacy U2F (CTAP1) is not supported on any transport: a
CTAP1 APDU in a MSG frame gets `CTAP1_ERR_INVALID_COMMAND`.

## Framing and limits

- Initial fragment `CMD | HLEN | LLEN | DATA`, continuation `SEQ | DATA` (SEQ 0x00-0x7F, wraps).
  Commands: PING `0x81`, KEEPALIVE `0x82`, MSG `0x83`, CANCEL `0xBE`, ERROR `0xBF`.
- Fragment size = negotiated ATT_MTU - 3, clamped to 20..240 bytes (20 until the MTU exchange).
  `fidoControlPointLength` (2 bytes, big-endian) is reset to 20 on each connection and updated to
  the fragment size after the MTU exchange; responses never exceed it.
- Requests are bounded by the app's max message size (`ZF_MAX_MSG_SIZE`, 1024 bytes). Errors:
  oversize, short or overrunning fragments → `ERROR 0x03` (INVALID_LEN), unknown command or a
  host-sent KEEPALIVE → `0x01` (INVALID_CMD), out-of-order SEQ → `0x04` (INVALID_SEQ), a
  partial request stalled for 3 s → `0x05` (REQ_TIMEOUT), a new request while one runs →
  `0x06` (BUSY), empty MSG → `0x03`, empty CTAP response → `0x7F`.
- A new initial fragment replaces an abandoned partial request; stray continuations are ignored.
- While a request waits for the user the Flipper sends `KEEPALIVE 0x02` (UP needed) every 300 ms;
  during long work polled by the CTAP code it sends `KEEPALIVE 0x01` (processing).
- CANCEL (or a disconnect) aborts the pending approval, hides the prompt and answers the request
  with CTAP status `0x2D` (KEEPALIVE_CANCEL).
- The CTAP session id is the connection counter: stable for a connection (getNextAssertion works)
  and new after every reconnect.

## Threads

Control-point writes arrive on the BLE event thread as `ACI_GATT_ATTRIBUTE_MODIFIED` events via the
event dispatcher; the handler only reassembles into a buffer owned by the link and wakes the
worker. CTAP processing, approval waits, notifications and the `fidoControlPointLength` update all
run on the transport worker thread. The link is marked idle before a reply is sent, because
hosts send the next request as soon as they see the reply.

## UI state

`zf_transport_ble_link_state()` (`src/transport/ble_worker.h`, full builds with
`ZF_BLE_ENABLED`) returns `ZfBleLinkStateOff`, `Advertising`, `Connected` (not yet encrypted) or
`Secured` (encrypted, i.e. paired). It is lock-free and safe from any thread, including with
`ui_mutex` held. `zerofido_ui_set_transport_connected()` is also called on connect/disconnect, as
for USB. For the settings menu, `zf_runtime_config_next_transport_mode()` cycles
USB HID -> NFC -> Bluetooth (skipping modes the build lacks), `zf_transport_mode_name()` gives
"USB HID" / "NFC" / "Bluetooth" and `zf_transport_mode_short_name()` "USB" / "NFC" / "BLE".

## Firmware APIs used

Only symbols exported to FAPs by the official firmware 1.5.1 (API 88.2): `bt_profile_start`,
`bt_profile_restore_default`, `bt_disconnect`, `bt_set_status_changed_callback`,
`bt_keys_storage_set_storage_path`, `bt_keys_storage_set_default_path`, `ble_gatt_service_add`,
`ble_gatt_service_delete`, `ble_gatt_characteristic_init`, `ble_gatt_characteristic_update`,
`ble_gatt_characteristic_delete`, `ble_event_dispatcher_register_svc_handler`,
`ble_event_dispatcher_unregister_svc_handler`, `ble_svc_dev_info_start`,
`ble_svc_dev_info_stop`, `furi_hal_bt_start_advertising`, `furi_hal_version_get_ble_mac`,
`furi_hal_version_get_ble_local_device_name_ptr`, `furi_hal_version_get_name_ptr`. No `aci_*` or
`hci_*` calls; the HCI/ACI event layouts the handler parses are restated in `ble_link.c`.
