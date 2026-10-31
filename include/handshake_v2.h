/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * handshake_v2.h — authenticated ephemeral key exchange between the command
 * deck and the server (2026.10.31.08, roadmap phase 1). One implementation,
 * used by both endpoints and by tests/handshake_v2_test.c.
 *
 * Why: the v1 handshake sent  session_key XOR K || MAGIC XOR K  with K the
 * deployment master key and MAGIC a public string, so anyone who saw one
 * handshake recovered K (MAGIC XOR (MAGIC XOR K)) and then every session
 * key, the fleet frequency keys and the galaxy verification key derived
 * from it. In v2 K never leaves the endpoints: it only keys the HMACs that
 * prove each side knows it, and the session secrets come from an ephemeral
 * X25519 exchange (forward secrecy: a later leak of K does not open past
 * sessions).
 *
 * Wire (PacketHandshake, unchanged size):
 *   C -> S  type = PKT_HANDSHAKE, pubkey_len = HANDSHAKE_V2_MARKER,
 *           pubkey[ 0..31] = eC  client ephemeral X25519 public key
 *           pubkey[32..63] = Nc  client nonce (32 random bytes)
 *           pubkey[64..95] = HMAC-SHA256(K, "SPACEGL-HS-V2/client" || eC || Nc)
 *           pubkey[96..255] = 0
 *   S -> C  int32 PKT_HANDSHAKE, then HANDSHAKE_V2_REPLY_LEN bytes:
 *           eS (32) server ephemeral public key
 *           Ns (32) server nonce
 *           HMAC-SHA256(K, "SPACEGL-HS-V2/server" || eC || Nc || eS || Ns || G)
 *           G = galaxy verification key XOR mask (32)
 *   Z    = X25519(e, peer)            (all-zero Z refused: small-order key)
 *   PRK  = HMAC-SHA256(Nc || Ns, Z)                   (HKDF-Extract)
 *   session key = HMAC-SHA256(PRK, "spacegl v2 session key" || 0x01)
 *   mask        = HMAC-SHA256(PRK, "spacegl v2 galaxy verify" || 0x01)
 *                                     (HKDF-Expand, L = 32, one block)
 * A v1 request, or one of another wire revision (pubkey_len !=
 * HANDSHAKE_V2_MARKER), is refused.
 *
 * K is a secret shared by every client of a deployment: v2 keeps it off the
 * wire, but a holder of K (any legitimate client) can still run the
 * exchange as either side. Per-server certificates remain future work
 * (SECURITY.md).
 */
#ifndef HANDSHAKE_V2_H
#define HANDSHAKE_V2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

/* pubkey_len of the hello: "HSV" + the wire revision. The handshake is v2
 * in every revision; the revision numbers the layout of the packets that
 * follow it, so a deck and a server of different revisions refuse each
 * other at the hello instead of misreading the stream:
 *   "HSV2"  revision 2, 2026.10.31.08 .. 2026.10.31.29
 *   "HSV3"  revision 3, 2026.10.31.30: per-object update delta
 *           (UPD_OBJDELTA, include/obj_delta.h) */
#define SPACEGL_WIRE_REVISION  3
#define HANDSHAKE_V2_MARKER    0x33565348   /* "HSV3" (little endian) in pubkey_len */
#define HANDSHAKE_MARKER_REV2  0x32565348   /* "HSV2": a revision-2 deck, refused */
#define HANDSHAKE_V2_REQ_LEN   96           /* eC + Nc + MAC used in pubkey[] */
#define HANDSHAKE_V2_REPLY_LEN 128          /* eS + Ns + MAC + masked verify key */

#define HS_V2_LABEL_CLIENT  "SPACEGL-HS-V2/client"
#define HS_V2_LABEL_SERVER  "SPACEGL-HS-V2/server"
#define HS_V2_INFO_SESSION  "spacegl v2 session key"
#define HS_V2_INFO_VERIFY   "spacegl v2 galaxy verify"

/* Client state between the hello and the server reply. */
typedef struct {
    uint8_t priv[32];
    uint8_t pub[32];
    uint8_t nonce[32];
} HsV2Client;

/* HMAC-SHA256(key, label || a || b || c || d || e), 32-byte parts; NULL
 * parts are skipped. */
static inline bool hs_v2_hmac(const uint8_t *key, size_t key_len, const char *label,
                              const uint8_t *a, const uint8_t *b, const uint8_t *c,
                              const uint8_t *d, const uint8_t *e, uint8_t out[32]) {
    uint8_t buf[64 + 5 * 32];
    size_t n = 0, ll = label ? strlen(label) : 0;
    if (ll > 64) return false;
    if (ll) { memcpy(buf, label, ll); n = ll; }  // NOLINT(bugprone-not-null-terminated-result): a byte label in a hash input, not a string)
    const uint8_t *parts[5] = { a, b, c, d, e };
    for (int i = 0; i < 5; i++) {
        if (!parts[i]) continue;
        memcpy(buf + n, parts[i], 32);
        n += 32;
    }
    unsigned int out_len = 32;
    bool ok = HMAC(EVP_sha256(), key, (int)key_len, buf, n, out, &out_len) != NULL && out_len == 32;
    OPENSSL_cleanse(buf, sizeof(buf));
    return ok;
}

/* HKDF-SHA256 (RFC 5869) with L = 32: Extract(salt, ikm) then one Expand
 * block T(1) = HMAC(PRK, info || 0x01). */
static inline bool hs_v2_hkdf32(const uint8_t *salt, size_t salt_len,
                                const uint8_t *ikm, size_t ikm_len,
                                const char *info, uint8_t out[32]) {
    uint8_t prk[32];
    unsigned int len = 32;
    if (!HMAC(EVP_sha256(), salt, (int)salt_len, ikm, ikm_len, prk, &len) || len != 32) return false;
    uint8_t blk[96];
    size_t il = strlen(info);
    if (il + 1 > sizeof(blk)) { OPENSSL_cleanse(prk, sizeof(prk)); return false; }
    memcpy(blk, info, il);  // NOLINT(bugprone-not-null-terminated-result): a byte label in a hash input, not a string)
    blk[il] = 0x01;
    len = 32;
    bool ok = HMAC(EVP_sha256(), prk, 32, blk, il + 1, out, &len) != NULL && len == 32;
    OPENSSL_cleanse(prk, sizeof(prk));
    OPENSSL_cleanse(blk, sizeof(blk));
    return ok;
}

static inline bool hs_v2_keygen(uint8_t priv[32], uint8_t pub[32]) {
    bool ok = false;
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, NULL);
    if (ctx && EVP_PKEY_keygen_init(ctx) > 0 && EVP_PKEY_keygen(ctx, &pkey) > 0) {
        size_t pl = 32, ql = 32;
        ok = EVP_PKEY_get_raw_private_key(pkey, priv, &pl) > 0 && pl == 32 &&
             EVP_PKEY_get_raw_public_key(pkey, pub, &ql) > 0 && ql == 32;
    }
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(ctx);
    return ok;
}

/* Z = X25519(priv, peer_pub); refuses an all-zero Z (small-order key). */
static inline bool hs_v2_x25519(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t z[32]) {
    bool ok = false;
    EVP_PKEY *mine = EVP_PKEY_new_raw_private_key(EVP_PKEY_X25519, NULL, priv, 32);
    EVP_PKEY *peer = EVP_PKEY_new_raw_public_key(EVP_PKEY_X25519, NULL, peer_pub, 32);
    EVP_PKEY_CTX *ctx = mine ? EVP_PKEY_CTX_new(mine, NULL) : NULL;
    size_t zl = 32;
    if (ctx && peer && EVP_PKEY_derive_init(ctx) > 0 && EVP_PKEY_derive_set_peer(ctx, peer) > 0 &&
        EVP_PKEY_derive(ctx, z, &zl) > 0 && zl == 32) {
        uint8_t acc = 0;
        for (int i = 0; i < 32; i++) acc |= z[i];
        ok = (acc != 0);
    }
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(peer);
    EVP_PKEY_free(mine);
    if (!ok) OPENSSL_cleanse(z, 32);
    return ok;
}

/* Session key and verify-key mask from Z and both nonces. */
static inline bool hs_v2_derive(const uint8_t z[32], const uint8_t nc[32], const uint8_t ns[32],
                                uint8_t session_key[32], uint8_t verify_mask[32]) {
    uint8_t salt[64];
    memcpy(salt, nc, 32);
    memcpy(salt + 32, ns, 32);
    bool ok = hs_v2_hkdf32(salt, 64, z, 32, HS_V2_INFO_SESSION, session_key) &&
              hs_v2_hkdf32(salt, 64, z, 32, HS_V2_INFO_VERIFY, verify_mask);
    OPENSSL_cleanse(salt, sizeof(salt));
    return ok;
}

/* Client: fill pubkey[] (256 bytes) and remember the ephemeral state. */
static inline bool hs_v2_client_hello(const uint8_t k[32], uint8_t pubkey[256], HsV2Client *st) {
    memset(pubkey, 0, 256);
    if (!hs_v2_keygen(st->priv, st->pub)) return false;
    if (RAND_bytes(st->nonce, 32) != 1) return false;
    memcpy(pubkey, st->pub, 32);
    memcpy(pubkey + 32, st->nonce, 32);
    return hs_v2_hmac(k, 32, HS_V2_LABEL_CLIENT, st->pub, st->nonce, NULL, NULL, NULL, pubkey + 64);
}

/* Server: check the hello, answer it, derive the session key. Returns false
 * (nothing to send) when the client MAC does not prove K or the exchange
 * fails; the caller then drops the connection. */
static inline bool hs_v2_server_reply(const uint8_t k[32], const uint8_t pubkey[256],
                                      const uint8_t galaxy_verify_key[32],
                                      uint8_t reply[HANDSHAKE_V2_REPLY_LEN],
                                      uint8_t session_key[32]) {
    const uint8_t *ec = pubkey, *nc = pubkey + 32, *mac_c = pubkey + 64;
    uint8_t expect[32];
    if (!hs_v2_hmac(k, 32, HS_V2_LABEL_CLIENT, ec, nc, NULL, NULL, NULL, expect)) return false;
    bool mac_ok = CRYPTO_memcmp(expect, mac_c, 32) == 0;
    OPENSSL_cleanse(expect, sizeof(expect));
    if (!mac_ok) return false;

    uint8_t es_priv[32], es_pub[32], ns[32], z[32], mask[32];
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    /* Fuzz harness ONLY (never in a product binary: spacegl_server.c and
     * spacegl_client.c refuse to compile with this macro): a fixed server
     * ephemeral key (RFC 7748 section 6.1, Bob) and nonce, so the seed
     * corpus can precompute the session key and reach the code behind the
     * login. */
    static const uint8_t fuzz_priv[32] = {
        0x5d, 0xab, 0x08, 0x7e, 0x62, 0x4a, 0x8a, 0x4b, 0x79, 0xe1, 0x7f, 0x8b, 0x83, 0x80, 0x0e, 0xe6,
        0x6f, 0x3b, 0xb1, 0x29, 0x26, 0x18, 0xb6, 0xfd, 0x1c, 0x2f, 0x8b, 0x27, 0xff, 0x88, 0xe0, 0xeb };
    static const uint8_t fuzz_pub[32] = {
        0xde, 0x9e, 0xdb, 0x7d, 0x7b, 0x7d, 0xc1, 0xb4, 0xd3, 0x5b, 0x61, 0xc2, 0xec, 0xe4, 0x35, 0x37,
        0x3f, 0x83, 0x43, 0xc8, 0x5b, 0x78, 0x67, 0x4d, 0xad, 0xfc, 0x7e, 0x14, 0x6f, 0x88, 0x2b, 0x4f };
    memcpy(es_priv, fuzz_priv, 32);
    memcpy(es_pub, fuzz_pub, 32);
    memset(ns, 0x44, 32);
    bool ok = hs_v2_x25519(es_priv, ec, z) &&
              hs_v2_derive(z, nc, ns, session_key, mask);
#else
    bool ok = hs_v2_keygen(es_priv, es_pub) && RAND_bytes(ns, 32) == 1 &&
              hs_v2_x25519(es_priv, ec, z) &&
              hs_v2_derive(z, nc, ns, session_key, mask);
#endif
    if (ok) {
        memcpy(reply, es_pub, 32);
        memcpy(reply + 32, ns, 32);
        for (int i = 0; i < 32; i++) reply[96 + i] = galaxy_verify_key[i] ^ mask[i];
        ok = hs_v2_hmac(k, 32, HS_V2_LABEL_SERVER, ec, nc, es_pub, ns, reply + 96, reply + 64);
    }
    OPENSSL_cleanse(es_priv, sizeof(es_priv));
    OPENSSL_cleanse(z, sizeof(z));
    OPENSSL_cleanse(mask, sizeof(mask));
    if (!ok) OPENSSL_cleanse(session_key, 32);
    return ok;
}

/* Client: check the server reply (it must prove K too) and derive the
 * session key and the galaxy verification key. */
static inline bool hs_v2_client_finish(const uint8_t k[32], HsV2Client *st,
                                       const uint8_t reply[HANDSHAKE_V2_REPLY_LEN],
                                       uint8_t session_key[32], uint8_t galaxy_verify_key[32]) {
    const uint8_t *es = reply, *ns = reply + 32, *mac_s = reply + 64, *gv = reply + 96;
    uint8_t expect[32], z[32], mask[32];
    bool ok = hs_v2_hmac(k, 32, HS_V2_LABEL_SERVER, st->pub, st->nonce, es, ns, gv, expect) &&
              CRYPTO_memcmp(expect, mac_s, 32) == 0 &&
              hs_v2_x25519(st->priv, es, z) &&
              hs_v2_derive(z, st->nonce, ns, session_key, mask);
    if (ok) {
        for (int i = 0; i < 32; i++) galaxy_verify_key[i] = gv[i] ^ mask[i];
    } else {
        OPENSSL_cleanse(session_key, 32);
    }
    OPENSSL_cleanse(expect, sizeof(expect));
    OPENSSL_cleanse(z, sizeof(z));
    OPENSSL_cleanse(mask, sizeof(mask));
    OPENSSL_cleanse(st->priv, sizeof(st->priv));
    return ok;
}

/* Login credentials under the session key (2026.10.31.09). PKT_QUERY and
 * PKT_LOGIN carry pass_hash, salt and reserved, 80 contiguous bytes
 * (network.h asserts the layout). They travelled in clear: a client of the
 * same deployment watching the network could capture another captain's
 * hash and replay it. Each such packet now XORs them with
 *   HMAC-SHA256(session_key, "spacegl v2 credentials" || seq || i), i = 1..3
 * truncated to 80 bytes, where seq counts the QUERY/LOGIN packets sent on
 * this connection since the handshake (0, 1, ...): a fresh mask for every
 * packet, from a key only the two ends of this session hold. Applying it
 * twice restores the fields (XOR). */
#define HS_V2_CRED_LEN 80

static inline bool hs_v2_cred_xor(const uint8_t session_key[32], uint32_t seq,
                                  uint8_t fields[HS_V2_CRED_LEN]) {
    static const char label[] = "spacegl v2 credentials";
    uint8_t in[sizeof(label) - 1 + 5];
    uint8_t blk[32];
    memcpy(in, label, sizeof(label) - 1);
    in[sizeof(label) - 1 + 0] = (uint8_t)(seq & 0xff);
    in[sizeof(label) - 1 + 1] = (uint8_t)((seq >> 8) & 0xff);
    in[sizeof(label) - 1 + 2] = (uint8_t)((seq >> 16) & 0xff);
    in[sizeof(label) - 1 + 3] = (uint8_t)((seq >> 24) & 0xff);
    size_t done = 0;
    for (uint8_t i = 1; done < HS_V2_CRED_LEN; i++) {
        in[sizeof(label) - 1 + 4] = i;
        unsigned int len = 32;
        if (!HMAC(EVP_sha256(), session_key, 32, in, sizeof(in), blk, &len) || len != 32) {
            OPENSSL_cleanse(blk, sizeof(blk));
            return false;
        }
        for (size_t j = 0; j < 32 && done < HS_V2_CRED_LEN; j++, done++) fields[done] ^= blk[j];
    }
    OPENSSL_cleanse(blk, sizeof(blk));
    return true;
}

#endif
