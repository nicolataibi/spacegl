/*
 * SPACE GL - 3D LOGIC ENGINE - protocol contract test:
 * PacketMessage.length bounds (server-side remote DoS guard).
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Regression (unvalidated PacketMessage.length -> remote server DoS):
 * the server read the fixed-size PacketMessage header and then branched
 * on "if (pkt->length > 0 && pkt->length < 65536) read_all(...)" WITHOUT
 * validating the length otherwise: a malicious value (e.g. -5) survived
 * the skipped read and flowed into broadcast_message() (server/net.c),
 * where it is converted to size_t in three sinks:
 *
 *   - memcpy(plaintext, msg->text, c_len) with
 *     c_len = msg->length < 65535 ? msg->length : 65535
 *     -> (size_t)-5 is a ~4 GB copy;
 *   - EVP_DecryptUpdate(..., msg->text, msg->length) (fleet path)
 *     -> same;
 *   - pkt_size = offsetof(PacketMessage, text) + msg->length (relay)
 *     -> heap garbage sent to every client.
 *
 * The client always validated ("Protocol error: message length out of
 * range") while the server did not. The fix makes both endpoints use
 * ONE canonical check (packet_message_length_valid, include/network.h)
 * and the server drop the connection on violation. This test pins
 * that shared contract:
 *
 *   1. Bounds: the canonical check accepts exactly [0, 65535] — the
 *      client's historical literal bounds — and rejects the attack
 *      values (-5, -1, INT_MIN, 65536, INT_MAX).
 *   2. Equivalence: it matches the client's legacy expression
 *      (length < 0 || length > 65535) on dense sweeps and a fuzz of
 *      full-range int32 values.
 *   3. Buffer tie-in: 65535 == sizeof(text) - 1, so the bound can
 *      never drift from the 65536-byte payload buffer.
 *   4. Sink arithmetic: for EVERY accepted length the three size_t
 *      conversions in net.c (c_len, decrypt input length, relay
 *      pkt_size) stay within the struct — the invariant the parse-site
 *      validation now guarantees.
 *   5. Reject: every attack value is refused by the validator, and for
 *      every one of them at least one pre-fix sink (c_len / decrypt /
 *      pkt_size) computes an out-of-bounds size (the test
 *      discriminates the bug).
 *
 * The code under test is the real production protocol header
 * (../include/network.h, included by both endpoints).
 *
 * Exit: 0 = pass, 1 = fail.
 */

#include "network.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
           fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } \
} while (0)

/* The three size_t conversions broadcast_message() applies to
 * msg->length (server/net.c), replicated here to pin the invariant. */
static size_t sink_c_len(int32_t length) {
    /* cleartext path: c_len = (msg->length < 65535) ? msg->length : 65535 */
    return (length < 65535) ? (size_t)length : 65535;
}
static size_t sink_decrypt_len(int32_t length) {
    /* fleet path: EVP_DecryptUpdate(ctx, ..., msg->text, msg->length) */
    return (size_t)length;
}
static size_t sink_pkt_size(int32_t length) {
    /* relay path: offsetof(PacketMessage, text) + msg->length */
    return offsetof(PacketMessage, text) + (size_t)length;
}

/* The client's historical literal condition (pre-fix source). */
static int legacy_client_rejects(int32_t length) {
    return length < 0 || length > 65535;
}

/* Size of the payload buffer a PacketMessage carries on the wire. */
static size_t text_buffer_bytes(void) {
    return sizeof(((PacketMessage *)0)->text);
}

/* The pre-fix server branch: did it read the payload? */
static int legacy_server_reads_payload(int32_t length) {
    return length > 0 && length < 65536;
}

/* ================================================================== */
/* 1. Canonical bounds: exactly [0, 65535]                            */
/* ================================================================== */
static void test_bounds(void) {
    const int32_t valid[]   = { 0, 1, 2, 100, 65534, 65535 };
    const int32_t invalid[] = { -1, -5, -4242, INT_MIN, 65536, 65537, 70000, INT_MAX };

    for (size_t k = 0; k < sizeof(valid) / sizeof(valid[0]); k++)
        CHECK(packet_message_length_valid(valid[k]),
              "bounds: %d must be ACCEPTED", valid[k]);
    for (size_t k = 0; k < sizeof(invalid) / sizeof(invalid[0]); k++)
        CHECK(!packet_message_length_valid(invalid[k]),
              "bounds: %d must be REJECTED", invalid[k]);
}

/* ================================================================== */
/* 2. Equivalence with the client's historical expression             */
/* ================================================================== */
static void test_equivalence_with_client(void) {
    /* Dense sweeps around both boundaries. */
    int mismatches = 0;
    for (int32_t v = -64; v <= 64; v++)
        if (packet_message_length_valid(v) == legacy_client_rejects(v)) mismatches++;
    for (int32_t v = 65520; v <= 65552; v++)
        if (packet_message_length_valid(v) == legacy_client_rejects(v)) mismatches++;
    CHECK(mismatches == 0,
          "equiv: %d boundary samples disagree with the client's legacy check", mismatches);

    /* Fuzz: full-range random int32 values. */
    srand(20260925);
    int fuzz_mismatch = 0;
    const int N = 200000;
    for (int i = 0; i < N; i++) {
        int32_t v = (int32_t)(((uint32_t)rand() << 15) ^ (uint32_t)rand());
        if (packet_message_length_valid(v) == legacy_client_rejects(v)) fuzz_mismatch++;
    }
    CHECK(fuzz_mismatch == 0, "equiv: %d/%d fuzz values disagree with the client's legacy check",
          fuzz_mismatch, N);
}

/* ================================================================== */
/* 3. The bound is tied to the wire-format buffer                     */
/* ================================================================== */
static void test_buffer_tie_in(void) {
    size_t text_sz = text_buffer_bytes();
    CHECK(text_sz == 65536, "buffer: text must be 65536 bytes (got %zu)", text_sz);
    CHECK((int32_t)(text_sz - 1) == 65535,
          "buffer: max accepted length 65535 == sizeof(text)-1");
    /* 65535 payload chars + the NUL broadcast_message() appends fit exactly. */
    CHECK(65535 + 1 == text_sz, "buffer: 65535 chars + NUL fit the text field");
}

/* ================================================================== */
/* 4. Sink arithmetic stays in bounds for every ACCEPTED length       */
/* ================================================================== */
static void test_sink_arithmetic(void) {
    int bad = 0;
    for (int32_t v = 0; v < 65536; v++) {
        if (!packet_message_length_valid(v)) { bad++; continue; }
        if (sink_c_len(v) > text_buffer_bytes()) bad++;
        if (sink_decrypt_len(v) > text_buffer_bytes()) bad++;
        if (sink_pkt_size(v) > sizeof(PacketMessage)) bad++;
    }
    CHECK(bad == 0,
          "sinks: %d violations across [0, 65535] (c_len/decrypt/pkt_size must stay in bounds)",
          bad);
}

/* ================================================================== */
/* 5. Attack values are rejected (and the pre-fix path was unsafe)    */
/* ================================================================== */
static void test_attack_values_rejected(void) {
    const int32_t attacks[] = { -5, -1, INT_MIN, 65536, 65537, INT_MAX };

    for (size_t k = 0; k < sizeof(attacks) / sizeof(attacks[0]); k++) {
        int32_t v = attacks[k];

        /* The fix: the validator refuses the value before any payload
         * read and before any sink can see it. */
        CHECK(!packet_message_length_valid(v),
              "attack: %d must be REJECTED by the canonical check", v);

        /* The pre-fix server skipped the payload read for these
         * values (stream desync) yet kept the malicious length —
         * that is exactly what let it reach the sinks. */
        CHECK(!legacy_server_reads_payload(v),
              "attack: pre-fix server skipped the read for %d (desync + stale length)", v);

        /* For every attack value, at least one of the pre-fix sinks
         * computes an out-of-bounds size (the c_len ternary clamps
         * only positive overflows to 65535; the decrypt input and
         * the relay pkt_size do not): the guard is what makes them
         * unreachable. The test discriminates the bug. */
        CHECK(sink_c_len(v) >= text_buffer_bytes()
              || sink_decrypt_len(v) >= text_buffer_bytes()
              || sink_pkt_size(v) >= sizeof(PacketMessage),
              "attack: pre-fix sinks would reach %zu bytes for %d (discriminating)",
              sink_decrypt_len(v), v);
    }
}

int main(void) {
    printf("pkt_length test (PacketMessage.length must stay in [0, 65535])\n");
    test_bounds();
    test_equivalence_with_client();
    test_buffer_tie_in();
    test_sink_arithmetic();
    test_attack_values_rejected();
    printf("  %d checks passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
