# Changelog

## Unreleased (Vault)

- Removed legacy U2F (CTAP1) to fit the app in memory. GetInfo lists `FIDO_2_0` only; USB HID
  INIT sets `CAPABILITY_NMSG` and `CTAPHID_MSG` gets `ERR_INVALID_CMD`; NFC SELECT returns
  `FIDO_2_0` and CTAP1 APDUs get `6D00`. Passkeys and the credential store are unchanged; old
  files under `/ext/apps_data/flipper_vault/u2f` are ignored.
- Release builds compile out the developer-only CTAP 2.1 paths (`ZF_DEV_FIDO2_1=0`).

## 0.7.0

- Fixed intermittent U2F HID response drops under fast conformance-tool turn-around by adding a small pre-response settle window for CTAPHID MSG replies.

## 0.6.0

- Set the default release profile to USB HID with the stable FIDO2.0 feature set.
- Updated app metadata for catalog submission: version 0.6 and description FIDO2 for Flipper Zero.
- Added a packed-attestation build flag while keeping packed attestation enabled in the default build.
- Replaced the local AES-CBC implementation with Flipper HAL crypto calls to reduce app size.
- Fixed USB shutdown handling when a stop request arrives during user approval.
- Fixed an NFC response-buffer race by keeping async responses out of the shared transport arena.
- Added gated USB diagnostics for development builds; diagnostics remain disabled in production builds.
- Added native regression coverage for packed-off attestation and transport lifecycle fixes.

## 0.5.0 and earlier

- Added CTAP2/FIDO2.0 passkey registration and authentication over USB HID.
- Added U2F compatibility support.
- Added ClientPIN support, credential storage, credential management screens, and on-device approval prompts.
- Added NFC transport support for development and iPhone NFC testing.
- Added local software attestation modes for none and packed.
