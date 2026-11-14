/*
 * Release builds (ZF_DEV_FIDO2_1=0) compile the CTAP 2.1 paths out. Even with the
 * 2.1 capability flags forced on, the authenticator must behave as CTAP 2.0 only.
 */
#if !ZF_DEV_FIDO2_1
static void test_release_build_compiles_out_ctap21(void) {
    static const uint8_t get_retries_protocol2[] = {
        ZfCtapeCmdClientPin, 0xA2, 0x01, 0x02, 0x02, 0x01};
    ZerofidoApp app = {0};
    ZfCredentialIndexEntry app_store_records[ZF_MAX_CREDENTIALS] = {0};
    uint8_t response[512];
    size_t response_len = 0;

    app.store.records = app_store_records;
    test_storage_reset();
    zf_runtime_config_load_defaults(&app.runtime_config);
    zf_runtime_config_resolve_capabilities(&app.runtime_config, &app.capabilities);
    app.capabilities.advertise_fido_2_1 = true;
    app.capabilities.pin_uv_auth_protocol_2_enabled = true;
    app.capabilities.pin_uv_auth_token_enabled = true;
    app.capabilities.make_cred_uv_not_required = true;
    app.capabilities.selection_enabled = true;
    app.capabilities_resolved = true;

    response_len = zerofido_handle_ctap2(&app, 0x01020304, (const uint8_t[]){ZfCtapeCmdGetInfo}, 1,
                                         response, sizeof(response));
    expect(response_len > 1 && response[0] == ZF_CTAP_SUCCESS, "release GetInfo should succeed");
    expect(get_info_has_version(response + 1, response_len - 1, "FIDO_2_0"),
           "release GetInfo should advertise FIDO_2_0");
    expect(!get_info_has_version(response + 1, response_len - 1, "FIDO_2_1"),
           "release GetInfo must not advertise FIDO_2_1");
    expect(!get_info_has_version(response + 1, response_len - 1, "U2F_V2"),
           "release GetInfo must not advertise U2F_V2");

    response_len = zerofido_handle_ctap2(&app, 0x01020304, get_retries_protocol2,
                                         sizeof(get_retries_protocol2), response, sizeof(response));
    expect(response_len == 1 && response[0] == ZF_CTAP_ERR_INVALID_PARAMETER,
           "release clientPin must reject pinUvAuthProtocol 2");

    response_len = zerofido_handle_ctap2(&app, 0x01020304, (const uint8_t[]){ZfCtapeCmdSelection},
                                         1, response, sizeof(response));
    expect(response_len == 1 && response[0] == ZF_CTAP_ERR_INVALID_COMMAND,
           "release build must not implement authenticatorSelection");
}
#endif

/*
 * A U2F (CTAP1) APDU that reaches the CTAP2 dispatcher, e.g. in a Bluetooth
 * CTAPBLE_MSG frame, starts with CLA 0x00: it must be refused as an unknown
 * CTAP2 command, never parsed as U2F.
 */
static void test_ctap1_apdu_in_ctap2_dispatch_is_invalid_command(void) {
    static const uint8_t version_apdu[] = {0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t register_header[] = {0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x40};
    ZerofidoApp app = {0};
    ZfCredentialIndexEntry app_store_records[ZF_MAX_CREDENTIALS] = {0};
    uint8_t response[64];
    size_t response_len = 0;

    app.store.records = app_store_records;
    test_storage_reset();
    response_len = zerofido_handle_ctap2(&app, 0x01020304, version_apdu, sizeof(version_apdu),
                                         response, sizeof(response));
    expect(response_len == 1 && response[0] == ZF_CTAP_ERR_INVALID_COMMAND,
           "U2F VERSION APDU in a CTAP2 message should be CTAP1_ERR_INVALID_COMMAND");
    response_len = zerofido_handle_ctap2(&app, 0x01020304, register_header, sizeof(register_header),
                                         response, sizeof(response));
    expect(response_len == 1 && response[0] == ZF_CTAP_ERR_INVALID_COMMAND,
           "U2F REGISTER APDU in a CTAP2 message should be CTAP1_ERR_INVALID_COMMAND");
}
