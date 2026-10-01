/*
 * SPACE GL - 3D LOGIC ENGINE - protocol contract test:
 * radio cipher table (audit item B5: fleet messages lost on 15 of 19
 * frequencies).
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
 * Regression: the cipher table that maps a CRYPTO_* radio slot to an
 * OpenSSL EVP cipher lived in FOUR divergent copies — the client
 * encrypt (spacegl_client.c encrypt_payload), the client decrypt
 * (spacegl_client.c PKT_MESSAGE receive path), the server fleet
 * decrypt (server/net.c broadcast_message) and the server's own
 * encrypt_payload (server/net.c adaptive relay re-encryption). The
 * client copies knew all 19 tunable slots while the server copies
 * knew only 4 (AES-256-GCM, CHACHA20-POLY1305, ARIA-256-GCM,
 * CAMELLIA-256-CTR). A fleet (is_encrypted=0x01) message on any of
 * the other 15 slots (enc seed, enc cast, enc rc4, ...) was
 * decrypted on the server with AES-256-GCM no matter what crypto_algo
 * said: the tag check failed, the message died with "Frequency parity
 * failure" and was never delivered — not even back to the sender.
 *
 * The fix extracts the table into ONE shared client/server lookup
 * (include/radio_crypto.h, radio_cipher_for_algo) used by every
 * encrypt/decrypt site of both endpoints. The implemented ciphers are
 * the four modern ones only (AES-256-GCM, CHACHA20-POLY1305,
 * ARIA-256-GCM, CAMELLIA-256-CTR); every other slot is a fixed alias
 * of AES-256-GCM, the strongest of the four — no legacy/weak cipher
 * (SEED, CAST5, IDEA, 3DES, BLOWFISH, RC4, DES) is ever used on the
 * wire, and no name-based lookup (SM4, GOST-KUZNYECHIK) is involved,
 * so the slot->cipher mapping is deterministic and identical on any
 * client and any server (the B5 failure mode). This test pins the
 * contract:
 *
 *   1. Coverage: every slot 1..MAX_CRYPTO_ALGOS (and the fallback
 *      values 0 = CRYPTO_NONE, 22 and 255) resolves to a non-NULL
 *      cipher.
 *   2. Table: the four implemented slots pin their cipher (pointer
 *      identity against the same constructor the client always used)
 *      and AEAD flag; EVERY other slot must be the AES-256-GCM alias
 *      with is_gcm = 1 (project decision: the strongest of the four
 *      is the fallback for everything else).
 *   3. Round-trip: for EVERY slot, a payload encrypted with the
 *      client-side EVP sequence (16-byte IV, 16-byte GCM tag when
 *      AEAD) is decrypted with the server-side sequence using the
 *      shared table and recovers the plaintext — the exact
 *      client-encrypt/server-decrypt pairing B5 broke. A wrong key
 *      must never pass: the AEAD slots must fail the 16-byte tag
 *      check, the unauthenticated CTR slot must at least not
 *      decrypt to the same bytes.
 *   4. Legacy subset: the four slots the old server table handled
 *      (AES, CHACHA, ARIA, CAMELLIA) keep exactly the same
 *      resolution as before (no regression on the frequencies that
 *      already worked).
 *
 * The code under test is the real production header
 * (../include/radio_crypto.h, included by both endpoints).
 *
 * Exit: 0 = pass, 1 = fail.
 */

#include "radio_crypto.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
           fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } \
} while (0)

/* The 21 tunable slots (the "19 ciphers" of the client's radio menu
 * are slots 1..19; GOST/SALSA are the two name-based extras). */
static const int SLOTS[] = {
    CRYPTO_AES, CRYPTO_CHACHA, CRYPTO_ARIA, CRYPTO_CAMELLIA,
    CRYPTO_SEED, CRYPTO_CAST5, CRYPTO_IDEA, CRYPTO_3DES,
    CRYPTO_BLOWFISH, CRYPTO_RC4, CRYPTO_DES, CRYPTO_PQC,
    CRYPTO_MCELIECE, CRYPTO_DILITHIUM, CRYPTO_SERPENT,
    CRYPTO_TWOFISH, CRYPTO_SM4, CRYPTO_ASCON, CRYPTO_PRESENT,
    CRYPTO_GOST, CRYPTO_SALSA
};
#define N_SLOTS ((int)(sizeof(SLOTS) / sizeof(SLOTS[0])))

/* Expected resolution per slot: cipher (pointer identity) + AEAD
 * flag. The four modern slots pin their real cipher; every other
 * slot must be the AES-256-GCM alias. The EVP_* constructors are
 * not constant expressions, so the table is filled at startup. */
typedef struct {
    int slot;
    const EVP_CIPHER *cipher;
    int is_gcm;
} SlotExpect;

static SlotExpect EXPECT[N_SLOTS];

static void init_expect(void) {
    const EVP_CIPHER *aes = EVP_aes_256_gcm();
    EXPECT[0]  = (SlotExpect){ CRYPTO_AES,       aes,                         1 };
    EXPECT[1]  = (SlotExpect){ CRYPTO_CHACHA,    EVP_chacha20_poly1305(),     1 };
    EXPECT[2]  = (SlotExpect){ CRYPTO_ARIA,      EVP_aria_256_gcm(),          1 };
    EXPECT[3]  = (SlotExpect){ CRYPTO_CAMELLIA,  EVP_camellia_256_ctr(),      0 };
    for (int i = 4; i < N_SLOTS; i++) {
        EXPECT[i] = (SlotExpect){ SLOTS[i], aes, 1 }; /* AES-256-GCM alias */
    }
}

/* Deterministic 32-byte key for a slot (and a second one for the
 * wrong-key check), same shape as the derived algo keys. */
static void make_key(int slot, int variant, uint8_t *key) {
    for (int b = 0; b < 32; b++)
        key[b] = (uint8_t)(slot * 31 + variant * 101 + b * 7);
}

/* Client encrypt sequence, mirroring spacegl_client.c
 * encrypt_payload() (B5: cipher from the shared table). */
static int client_encrypt(int algo, const uint8_t *key, const uint8_t *iv,
                          const uint8_t *in, int in_len, uint8_t *out,
                          int *out_len, uint8_t *tag)
{
    int is_gcm = 0;
    const EVP_CIPHER *cipher = radio_cipher_for_algo(algo, &is_gcm);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, cipher, NULL, NULL, NULL);
    if (is_gcm) EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 16, NULL);
    if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) <= 0) {
        EVP_CIPHER_CTX_free(ctx);
        return 0;
    }
    int outl = 0, fin = 0;
    if (EVP_EncryptUpdate(ctx, out, &outl, in, in_len) <= 0 ||
        EVP_EncryptFinal_ex(ctx, out + outl, &fin) <= 0) {
        EVP_CIPHER_CTX_free(ctx);
        return 0;
    }
    if (is_gcm) EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag);
    else memset(tag, 0, 16);
    EVP_CIPHER_CTX_free(ctx);
    *out_len = outl + fin;
    return 1;
}

/* Server fleet-decrypt sequence, mirroring server/net.c
 * broadcast_message() (B5: cipher from the shared table). */
static int server_decrypt(int algo, const uint8_t *key, const uint8_t *iv,
                          const uint8_t *in, int in_len, uint8_t *out,
                          int *out_len, uint8_t *tag)
{
    int is_gcm = 0;
    const EVP_CIPHER *cipher = radio_cipher_for_algo(algo, &is_gcm);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, cipher, NULL, NULL, NULL);
    if (is_gcm) EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 16, NULL);
    if (EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) <= 0) {
        EVP_CIPHER_CTX_free(ctx);
        return 0;
    }
    int outl = 0, fin = 0;
    EVP_DecryptUpdate(ctx, out, &outl, in, in_len);
    if (is_gcm) EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag);
    int ok = EVP_DecryptFinal_ex(ctx, out + outl, &fin) > 0;
    EVP_CIPHER_CTX_free(ctx);
    *out_len = ok ? outl + fin : 0;
    return ok;
}

/* ================================================================== */
/* 1. Coverage: every slot (and the fallback values) resolves         */
/* ================================================================== */
static void test_coverage(void) {
    for (int i = 0; i < N_SLOTS; i++) {
        int is_gcm = -1;
        const EVP_CIPHER *c = radio_cipher_for_algo(SLOTS[i], &is_gcm);
        CHECK(c != NULL, "coverage: slot %d must resolve to a cipher", SLOTS[i]);
        CHECK(is_gcm == 0 || is_gcm == 1,
              "coverage: slot %d is_gcm must be 0/1 (got %d)", SLOTS[i], is_gcm);
    }
    /* Fallback values: 0 (CRYPTO_NONE) and out-of-range 22..255. */
    const int fallbacks[] = { CRYPTO_NONE, 22, 23, 100, 255 };
    for (size_t k = 0; k < sizeof(fallbacks) / sizeof(fallbacks[0]); k++) {
        int is_gcm = 0;
        const EVP_CIPHER *c = radio_cipher_for_algo(fallbacks[k], &is_gcm);
        CHECK(c == EVP_aes_256_gcm() && is_gcm == 1,
              "coverage: fallback value %d must be AES-256-GCM/AEAD", fallbacks[k]);
    }
}

/* ================================================================== */
/* 2. Table: the 4 modern slots pinned, everything else the alias     */
/* ================================================================== */
static void test_table(void) {
    for (int i = 0; i < N_SLOTS; i++) {
        const SlotExpect *e = &EXPECT[i];
        int is_gcm = 0;
        const EVP_CIPHER *c = radio_cipher_for_algo(e->slot, &is_gcm);

        CHECK(c == e->cipher,
              "table: slot %d must resolve to its pinned cipher", e->slot);
        CHECK(is_gcm == e->is_gcm,
              "table: slot %d is_gcm must be %d (got %d)",
              e->slot, e->is_gcm, is_gcm);

        /* The alias rule: every non-modern slot is AES-256-GCM/AEAD. */
        if (e->slot != CRYPTO_AES && e->slot != CRYPTO_CHACHA &&
            e->slot != CRYPTO_ARIA && e->slot != CRYPTO_CAMELLIA) {
            CHECK(c == EVP_aes_256_gcm() && is_gcm == 1,
                  "table: slot %d (legacy/alias) must be the AES-256-GCM "
                  "alias with is_gcm=1 (no legacy cipher on the wire)",
                  e->slot);
        }
    }
}

/* ================================================================== */
/* 3. Round-trip: client encrypt -> server decrypt, every slot        */
/* ================================================================== */
static void test_roundtrip(void) {
    const char *plaintext = "B5 FLEET ROUND-TRIP: all frequencies must be "
                            "delivered, not just the core four. ";
    const int plen = (int)strlen(plaintext);
    uint8_t key[32], bad_key[32], iv[16], tag[16];
    uint8_t ctext[512], dec[512];

    for (int i = 0; i < N_SLOTS; i++) {
        int algo = SLOTS[i];
        make_key(algo, 0, key);
        make_key(algo, 1, bad_key);
        RAND_bytes(iv, 16);

        /* Client side: encrypt the plaintext on the slot's frequency. */
        int clen = 0;
        CHECK(client_encrypt(algo, key, iv, (const uint8_t *)plaintext, plen,
                             ctext, &clen, tag),
              "roundtrip: slot %d: client encrypt must succeed", algo);
        if (clen == 0) continue;

        /* Server side: fleet decrypt with the shared table must
         * recover the exact plaintext (this is where the old 4-slot
         * server table failed on 15 slots). */
        int dlen = 0;
        CHECK(server_decrypt(algo, key, iv, ctext, clen, dec, &dlen, tag),
              "roundtrip: slot %d: server fleet decrypt must succeed "
              "(B5: 'Frequency parity failure')", algo);
        CHECK(dlen == plen && memcmp(dec, plaintext, plen) == 0,
              "roundtrip: slot %d: plaintext must be recovered "
              "(len %d vs %d)", algo, dlen, plen);

        /* Wrong key: the message must NOT come through. AEAD slots
         * fail the 16-byte tag check deterministically; the
         * unauthenticated CTR slot must at least not decrypt to the
         * same bytes. */
        int wlen = 0;
        int w_ok = server_decrypt(algo, bad_key, iv, ctext, clen, dec, &wlen, tag);
        int is_gcm = 0;
        (void)radio_cipher_for_algo(algo, &is_gcm);
        if (is_gcm) {
            CHECK(!w_ok, "roundtrip: slot %d: wrong key must FAIL the "
                         "tag check", algo);
        } else {
            int same = (w_ok && wlen == plen && memcmp(dec, plaintext, plen) == 0);
            CHECK(!same, "roundtrip: slot %d: wrong key must not recover "
                         "the plaintext", algo);
        }
    }
}

/* ================================================================== */
/* 4. Legacy subset: the 4 slots the old server table handled         */
/* ================================================================== */
static void test_legacy_subset(void) {
    struct { int slot; const EVP_CIPHER *cipher; int is_gcm; } legacy[] = {
        { CRYPTO_AES,      EVP_aes_256_gcm(),       1 },
        { CRYPTO_CHACHA,   EVP_chacha20_poly1305(), 1 },
        { CRYPTO_ARIA,     EVP_aria_256_gcm(),      1 },
        { CRYPTO_CAMELLIA, EVP_camellia_256_ctr(),  0 },
    };
    for (size_t k = 0; k < sizeof(legacy) / sizeof(legacy[0]); k++) {
        int is_gcm = 0;
        const EVP_CIPHER *c = radio_cipher_for_algo(legacy[k].slot, &is_gcm);
        CHECK(c == legacy[k].cipher && is_gcm == legacy[k].is_gcm,
              "legacy: slot %d must keep its pre-fix resolution "
              "(cipher + AEAD flag)", legacy[k].slot);
    }
}

int main(void) {
    printf("crypto_table test (one shared client/server cipher table, B5)\n");
    init_expect();
    test_coverage();
    test_table();
    test_roundtrip();
    test_legacy_subset();
    printf("  %d checks passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
