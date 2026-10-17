/*
 * crypto_table_oob_test.c — B2-b regression: radio cipher table
 * index bounds.
 *
 * The guard in radio_cipher_for_algo() (include/radio_crypto.h)
 * prevents an attacker-controlled algo byte (0-255) from indexing
 * out of bounds on the cipher table. This test exercises every
 * possible input byte and verifies:
 *
 *   1. Valid inputs (0..MAX_CRYPTO_ALGOS-1) return a non-NULL
 *      cipher with a correct key length.
 *   2. Invalid inputs (MAX_CRYPTO_ALGOS..255) return NULL (the
 *      "unknown algo" sentinel). No crash, no OOB access.
 *   3. A canary byte immediately after the table (placed in the
 *      same BSS adjacency as production) is unmodified after
 *      all 256 calls.
 *
 * The canary technique mirrors target_bounds_test: the test TU
 * declares a static array immediately after the cipher table's
 * external reference, simulating the production BSS layout where
 * the next global after the table is the canary.
 *
 * ASan/UBSan: YES (the test's entire purpose is to prove that
 * OOB access does not occur — ASan would catch it if it did).
 *
 * Audit item: B2-b (SECURITY.md §2).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "radio_crypto.h"

/* Canary: in production, the cipher table is a static array in
 * radio_crypto.h (a header with a static table, so each TU that
 * includes it gets its own copy). The test includes the header,
 * so it has its own copy. The canary is declared here to simulate
 * the BSS adjacency — if an OOB write goes past the table, it
 * hits the canary (and ASan catches it regardless). */
static uint8_t s_canary[64] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE,
                               0xBA, 0xBE, 0x12, 0x34, 0x56, 0x78,
                               0x9A, 0xBC, 0xDE, 0xF0};

int main(void) {
    int failures = 0;

    /* 1. All valid algos return a non-NULL cipher. */
    for (int algo = 0; algo < MAX_CRYPTO_ALGOS; algo++) {
        const RadioCipher *rc = radio_cipher_for_algo((uint8_t)algo);
        if (rc == NULL) {
            fprintf(stderr, "FAIL: algo %u returned NULL (expected valid)\n",
                    (unsigned)algo);
            failures++;
        } else if (rc->key_len == 0 || rc->key_len > 32) {
            fprintf(stderr, "FAIL: algo %u returned key_len %zu "
                    "(expected 1..32)\n",
                    (unsigned)algo, rc->key_len);
            failures++;
        }
    }

    /* 2. All invalid algos return NULL (no OOB access). */
    for (int algo = MAX_CRYPTO_ALGOS; algo < 256; algo++) {
        const RadioCipher *rc = radio_cipher_for_algo((uint8_t)algo);
        if (rc != NULL) {
            fprintf(stderr, "FAIL: algo %u (invalid) returned non-NULL "
                    "cipher (expected NULL)\n",
                    (unsigned)algo);
            failures++;
        }
    }

    /* 3. Canary unmodified. */
    static const uint8_t expected_canary[64] = {
        0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
        0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0
    };
    if (memcmp(s_canary, expected_canary, sizeof(expected_canary)) != 0) {
        fprintf(stderr, "FAIL: canary modified after 256 algo calls "
                "(OOB write detected)\n");
        for (int i = 0; i < 64; i++) {
            if (s_canary[i] != expected_canary[i]) {
                fprintf(stderr, "  canary[%d] = 0x%02X (expected 0x%02X)\n",
                        i, s_canary[i], expected_canary[i]);
            }
        }
        failures++;
    }

    if (failures == 0) {
        printf("PASS: crypto_table_oob (B2-b) — 256 algo values, "
               "canary intact\n");
        return 0;
    }
    fprintf(stderr, "FAIL: %d failure(s)\n", failures);
    return 1;
}
