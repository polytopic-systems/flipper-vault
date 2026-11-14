/*
 * Host regressions for the CTAP Bluetooth LE framing state machine
 * (src/transport/ble_framing.c). GPL-3.0-or-later, like the rest of the app.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "transport/ble_framing.c"

#define MAX_MSG 1024U

static int failures = 0;

static void expect(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static uint8_t rx_buffer[MAX_MSG];

static void rx_setup(ZfBleRx *rx) {
    memset(rx_buffer, 0xAA, sizeof(rx_buffer));
    zf_ble_rx_init(rx, rx_buffer, sizeof(rx_buffer));
}

/* Splits payload into BLE request fragments of fragment_size and feeds them. */
static ZfBleRxResult feed_message(ZfBleRx *rx, uint8_t cmd, const uint8_t *payload, size_t len,
                                  uint16_t fragment_size, uint32_t now, uint8_t *error) {
    ZfBleTx tx;
    uint8_t frame[ZF_BLE_FRAGMENT_MAX];
    size_t frame_len = 0;
    ZfBleRxResult result = ZfBleRxNone;

    if (!zf_ble_tx_begin(&tx, cmd, payload, len, fragment_size)) {
        return ZfBleRxError;
    }
    while (zf_ble_tx_next(&tx, frame, sizeof(frame), &frame_len)) {
        result = zf_ble_rx_feed(rx, frame, frame_len, now, error);
        if (result != ZfBleRxNone) {
            return result;
        }
    }
    return result;
}

static void test_fragment_size_for_mtu(void) {
    expect(zf_ble_fragment_size_for_mtu(0) == ZF_BLE_FRAGMENT_MIN, "MTU 0 clamps to 20");
    expect(zf_ble_fragment_size_for_mtu(23) == 20, "default MTU gives 20-byte fragments");
    expect(zf_ble_fragment_size_for_mtu(100) == 97, "MTU 100 gives 97-byte fragments");
    expect(zf_ble_fragment_size_for_mtu(247) == ZF_BLE_FRAGMENT_MAX, "MTU 247 clamps to 240");
    expect(zf_ble_fragment_size_for_mtu(517) == ZF_BLE_FRAGMENT_MAX, "MTU 517 clamps to 240");
}

static void test_single_fragment_ping(void) {
    ZfBleRx rx;
    uint8_t error = 0;
    const uint8_t frame[] = {ZF_BLE_CMD_PING, 0x00, 0x03, 'a', 'b', 'c'};

    rx_setup(&rx);
    expect(zf_ble_rx_feed(&rx, frame, sizeof(frame), 1, &error) == ZfBleRxComplete,
           "single-fragment PING completes");
    expect(rx.cmd == ZF_BLE_CMD_PING && rx.total_len == 3, "PING command and length kept");
    expect(memcmp(rx.buffer, "abc", 3) == 0, "PING payload copied");
}

static void test_empty_msg_completes(void) {
    ZfBleRx rx;
    uint8_t error = 0;
    const uint8_t frame[] = {ZF_BLE_CMD_MSG, 0x00, 0x00};

    rx_setup(&rx);
    expect(zf_ble_rx_feed(&rx, frame, sizeof(frame), 1, &error) == ZfBleRxComplete,
           "zero-length MSG completes (worker rejects it)");
    expect(rx.total_len == 0, "zero-length MSG length");
}

static void test_multi_fragment_round_trip(void) {
    static const uint16_t sizes[] = {20, 64, 97, 240};
    uint8_t payload[MAX_MSG];

    for (size_t i = 0; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)(i * 7U + 3U);
    }
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s) {
        ZfBleRx rx;
        uint8_t error = 0;

        rx_setup(&rx);
        expect(feed_message(&rx, ZF_BLE_CMD_MSG, payload, sizeof(payload), sizes[s], 5, &error) ==
                   ZfBleRxComplete,
               "max-size MSG reassembles at every fragment size");
        expect(rx.total_len == sizeof(payload) && memcmp(rx.buffer, payload, sizeof(payload)) == 0,
               "reassembled payload matches");
    }
}

static void test_tx_fragments_respect_size_and_seq(void) {
    uint8_t payload[300];
    uint8_t frame[ZF_BLE_FRAGMENT_MAX];
    size_t frame_len = 0;
    size_t total = 0;
    size_t count = 0;
    ZfBleTx tx;

    memset(payload, 0x5C, sizeof(payload));
    expect(zf_ble_tx_begin(&tx, ZF_BLE_CMD_MSG, payload, sizeof(payload), 20), "tx begin");
    while (zf_ble_tx_next(&tx, frame, sizeof(frame), &frame_len)) {
        expect(frame_len <= 20, "fragment never exceeds the fragment size");
        if (count == 0) {
            expect(frame[0] == ZF_BLE_CMD_MSG && frame[1] == 0x01 && frame[2] == 0x2C,
                   "initial fragment carries CMD and big-endian length");
            total += frame_len - 3;
        } else {
            expect(frame[0] == (uint8_t)((count - 1) & 0x7F), "continuation SEQ counts from 0");
            total += frame_len - 1;
        }
        count++;
    }
    expect(total == sizeof(payload), "all payload bytes emitted");
    expect(count == 1 + (300 - 17 + 18) / 19, "expected fragment count for 300 bytes at 20");

    expect(zf_ble_tx_begin(&tx, ZF_BLE_CMD_KEEPALIVE, (const uint8_t[]){2}, 1, 240),
           "keepalive begin");
    expect(zf_ble_tx_next(&tx, frame, sizeof(frame), &frame_len) && frame_len == 4 &&
               frame[0] == ZF_BLE_CMD_KEEPALIVE && frame[3] == ZF_BLE_KEEPALIVE_UP_NEEDED,
           "keepalive is one 4-byte fragment");
    expect(!zf_ble_tx_next(&tx, frame, sizeof(frame), &frame_len), "keepalive has no continuation");

    expect(zf_ble_tx_begin(&tx, ZF_BLE_CMD_PING, NULL, 0, 20), "empty message begin");
    expect(zf_ble_tx_next(&tx, frame, sizeof(frame), &frame_len) && frame_len == 3,
           "empty message is a bare header");
    expect(!zf_ble_tx_next(&tx, frame, sizeof(frame), &frame_len), "empty message ends");
}

static void test_seq_wraps_after_127(void) {
    /* 20-byte fragments: 17 + 19*n; enough continuations to wrap SEQ needs a large buffer. */
    static uint8_t big_buffer[4000];
    static uint8_t payload[3000];
    ZfBleRx rx;
    uint8_t error = 0;

    memset(payload, 0x11, sizeof(payload));
    zf_ble_rx_init(&rx, big_buffer, sizeof(big_buffer));
    expect(feed_message(&rx, ZF_BLE_CMD_MSG, payload, sizeof(payload), 20, 1, &error) ==
               ZfBleRxComplete,
           "SEQ wraps from 0x7F to 0x00 without error");
}

static void test_oversize_rejected(void) {
    ZfBleRx rx;
    uint8_t error = 0;
    const uint8_t frame[] = {ZF_BLE_CMD_MSG, 0x04, 0x01, 0x00};

    rx_setup(&rx);
    expect(zf_ble_rx_feed(&rx, frame, sizeof(frame), 1, &error) == ZfBleRxError &&
               error == ZF_BLE_ERR_INVALID_LEN,
           "request longer than the max message is INVALID_LEN");
    expect(!rx.active && !rx.complete, "oversize request leaves no state");
}

static void test_payload_longer_than_declared_rejected(void) {
    ZfBleRx rx;
    uint8_t error = 0;
    const uint8_t frame[] = {ZF_BLE_CMD_MSG, 0x00, 0x02, 1, 2, 3};
    const uint8_t init[] = {ZF_BLE_CMD_MSG, 0x00, 0x04, 1, 2};
    const uint8_t cont[] = {0x00, 3, 4, 5};

    rx_setup(&rx);
    expect(zf_ble_rx_feed(&rx, frame, sizeof(frame), 1, &error) == ZfBleRxError &&
               error == ZF_BLE_ERR_INVALID_LEN,
           "initial fragment with extra bytes is INVALID_LEN");
    rx_setup(&rx);
    expect(zf_ble_rx_feed(&rx, init, sizeof(init), 1, &error) == ZfBleRxNone, "partial init");
    expect(zf_ble_rx_feed(&rx, cont, sizeof(cont), 2, &error) == ZfBleRxError &&
               error == ZF_BLE_ERR_INVALID_LEN,
           "continuation overrunning the length is INVALID_LEN");
}

static void test_short_init_and_bad_cmd(void) {
    ZfBleRx rx;
    uint8_t error = 0;
    const uint8_t short_init[] = {ZF_BLE_CMD_MSG, 0x00};
    const uint8_t keepalive[] = {ZF_BLE_CMD_KEEPALIVE, 0x00, 0x01, 0x01};
    const uint8_t unknown[] = {0x90, 0x00, 0x00};

    rx_setup(&rx);
    expect(zf_ble_rx_feed(&rx, short_init, sizeof(short_init), 1, &error) == ZfBleRxError &&
               error == ZF_BLE_ERR_INVALID_LEN,
           "init shorter than the header is INVALID_LEN");
    expect(zf_ble_rx_feed(&rx, keepalive, sizeof(keepalive), 1, &error) == ZfBleRxError &&
               error == ZF_BLE_ERR_INVALID_CMD,
           "KEEPALIVE from the host is INVALID_CMD");
    expect(zf_ble_rx_feed(&rx, unknown, sizeof(unknown), 1, &error) == ZfBleRxError &&
               error == ZF_BLE_ERR_INVALID_CMD,
           "unknown command is INVALID_CMD");
}

static void test_bad_sequence(void) {
    ZfBleRx rx;
    uint8_t error = 0;
    const uint8_t init[] = {ZF_BLE_CMD_MSG, 0x00, 0x05, 1, 2};
    const uint8_t skip[] = {0x01, 3, 4, 5};

    rx_setup(&rx);
    expect(zf_ble_rx_feed(&rx, init, sizeof(init), 1, &error) == ZfBleRxNone, "partial init");
    expect(zf_ble_rx_feed(&rx, skip, sizeof(skip), 2, &error) == ZfBleRxError &&
               error == ZF_BLE_ERR_INVALID_SEQ,
           "out-of-order continuation is INVALID_SEQ");
    expect(!rx.active, "INVALID_SEQ drops the partial request");
}

static void test_stray_continuation_ignored(void) {
    ZfBleRx rx;
    uint8_t error = 0xEE;
    const uint8_t cont[] = {0x00, 1, 2};

    rx_setup(&rx);
    expect(zf_ble_rx_feed(&rx, cont, sizeof(cont), 1, &error) == ZfBleRxNone && error == 0xEE,
           "continuation without a request is ignored");
}

static void test_timeout(void) {
    ZfBleRx rx;
    uint8_t error = 0;
    const uint8_t init[] = {ZF_BLE_CMD_MSG, 0x00, 0x05, 1, 2};

    rx_setup(&rx);
    expect(zf_ble_rx_feed(&rx, init, sizeof(init), 1000, &error) == ZfBleRxNone, "partial init");
    expect(!zf_ble_rx_check_timeout(&rx, 1000 + ZF_BLE_RX_TIMEOUT_MS - 1), "not yet timed out");
    expect(zf_ble_rx_check_timeout(&rx, 1000 + ZF_BLE_RX_TIMEOUT_MS), "stalled request times out");
    expect(!rx.active, "timeout drops the partial request");
    expect(!zf_ble_rx_check_timeout(&rx, 999999), "idle assembler never times out");

    rx_setup(&rx);
    expect(zf_ble_rx_feed(&rx, init, sizeof(init), 0xFFFFFF00U, &error) == ZfBleRxNone,
           "partial init near tick wrap");
    expect(!zf_ble_rx_check_timeout(&rx, 0x00000010U), "tick wrap does not fake a timeout");
}

static void test_cancel_and_busy(void) {
    ZfBleRx rx;
    uint8_t error = 0;
    const uint8_t init[] = {ZF_BLE_CMD_MSG, 0x00, 0x05, 1, 2};
    const uint8_t cancel[] = {ZF_BLE_CMD_CANCEL, 0x00, 0x00};
    const uint8_t done[] = {ZF_BLE_CMD_MSG, 0x00, 0x01, 0x04};
    const uint8_t next[] = {ZF_BLE_CMD_PING, 0x00, 0x01, 0x00};

    rx_setup(&rx);
    expect(zf_ble_rx_feed(&rx, init, sizeof(init), 1, &error) == ZfBleRxNone, "partial init");
    expect(zf_ble_rx_feed(&rx, cancel, sizeof(cancel), 2, &error) == ZfBleRxCancel,
           "CANCEL is reported");
    expect(!rx.active, "CANCEL drops a partial request");

    expect(zf_ble_rx_feed(&rx, done, sizeof(done), 3, &error) == ZfBleRxComplete,
           "request completes");
    expect(zf_ble_rx_feed(&rx, next, sizeof(next), 4, &error) == ZfBleRxError &&
               error == ZF_BLE_ERR_BUSY,
           "new request while one is queued is BUSY");
    expect(rx.complete && rx.cmd == ZF_BLE_CMD_MSG && rx.buffer[0] == 0x04,
           "BUSY keeps the queued request intact");
    zf_ble_rx_reset(&rx);
    expect(zf_ble_rx_feed(&rx, next, sizeof(next), 5, &error) == ZfBleRxComplete,
           "after reset the next request is accepted");
}

static void test_new_init_restarts_partial(void) {
    ZfBleRx rx;
    uint8_t error = 0;
    const uint8_t init[] = {ZF_BLE_CMD_MSG, 0x00, 0x05, 1, 2};
    const uint8_t ping[] = {ZF_BLE_CMD_PING, 0x00, 0x02, 9, 8};

    rx_setup(&rx);
    expect(zf_ble_rx_feed(&rx, init, sizeof(init), 1, &error) == ZfBleRxNone, "partial init");
    expect(zf_ble_rx_feed(&rx, ping, sizeof(ping), 2, &error) == ZfBleRxComplete,
           "a new initial fragment replaces an abandoned partial request");
    expect(rx.cmd == ZF_BLE_CMD_PING && rx.total_len == 2 && rx.buffer[0] == 9,
           "replacement request is the one kept");
}

static void test_oversized_fragment_rejected(void) {
    ZfBleRx rx;
    uint8_t error = 0;
    uint8_t frame[ZF_BLE_FRAGMENT_MAX + 1];

    memset(frame, 0, sizeof(frame));
    frame[0] = ZF_BLE_CMD_MSG;
    frame[1] = 0x01;
    frame[2] = 0x00;
    rx_setup(&rx);
    expect(zf_ble_rx_feed(&rx, frame, sizeof(frame), 1, &error) == ZfBleRxError &&
               error == ZF_BLE_ERR_INVALID_LEN,
           "fragment larger than the control point is INVALID_LEN");
}

int main(void) {
    test_fragment_size_for_mtu();
    test_single_fragment_ping();
    test_empty_msg_completes();
    test_multi_fragment_round_trip();
    test_tx_fragments_respect_size_and_seq();
    test_seq_wraps_after_127();
    test_oversize_rejected();
    test_payload_longer_than_declared_rejected();
    test_short_init_and_bad_cmd();
    test_bad_sequence();
    test_stray_continuation_ignored();
    test_timeout();
    test_cancel_and_busy();
    test_new_init_restarts_partial();
    test_oversized_fragment_rejected();

    if (failures != 0) {
        fprintf(stderr, "native BLE framing regressions failed: %d\n", failures);
        return 1;
    }
    printf("native BLE framing regressions passed\n");
    return 0;
}
