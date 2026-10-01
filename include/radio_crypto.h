/*
 * SPACE GL - 3D LOGIC ENGINE
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
 */

#ifndef RADIO_CRYPTO_H
#define RADIO_CRYPTO_H

#include <openssl/evp.h>
#include "network.h" /* CRYPTO_* slot ids, MAX_CRYPTO_ALGOS */

/*
 * Single source of truth for the radio cipher table (audit item B5):
 * maps a CRYPTO_* frequency slot to the OpenSSL EVP cipher that
 * implements it, plus whether the slot is an AEAD (GCM family) mode.
 *
 * Before this existed the table lived in FOUR divergent copies —
 * the client encrypt (spacegl_client.c encrypt_payload), the client
 * decrypt (spacegl_client.c PKT_MESSAGE receive path), the server
 * fleet decrypt (server/net.c broadcast_message) and the server's
 * own encrypt_payload (server/net.c, adaptive relay re-encryption and
 * send_server_msg). The client copies knew every slot, while the two
 * server copies knew only 4 of them: a fleet (is_encrypted=0x01)
 * message on any of the other 15 tunable frequencies (enc seed,
 * enc cast, enc rc4, ...) was decrypted on the server with
 * AES-256-GCM no matter what the packet's crypto_algo said, the tag
 * check failed, and the message died with "Frequency parity failure"
 * without ever being delivered — not even back to the sender. The
 * same 4-slot table in the server encrypt side also re-encrypted a
 * successfully decrypted fleet message to a non-core recipient with
 * AES-256-GCM while labeling it with the recipient's slot:
 * undecryptable on the client.
 *
 * The implemented ciphers are the four modern ones only:
 *
 *   CRYPTO_AES       -> AES-256-GCM         (AEAD)
 *   CRYPTO_CHACHA    -> CHACHA20-POLY1305   (AEAD)
 *   CRYPTO_ARIA      -> ARIA-256-GCM        (AEAD)
 *   CRYPTO_CAMELLIA  -> CAMELLIA-256-CTR    (not AEAD: no tag)
 *
 * EVERY other slot (SEED, CAST5, IDEA, 3DES, BLOWFISH, RC4, DES, the
 * post-quantum-named slots 12-19, SM4, GOST, SALSA, CRYPTO_NONE and
 * any out-of-range wire value) is an ALIAS of AES-256-GCM, the
 * strongest of the four. Project decision: no legacy/weak cipher is
 * ever used on the wire. Besides being the strongest of the four,
 * AES-256-GCM was already the default of all the pre-B5 tables, so
 * the alias keeps the frequencies that already worked working. The
 * legacy ciphers (SEED, CAST5, IDEA, 3DES, BLOWFISH, RC4, DES) are
 * no longer available in this OpenSSL build anyway — OpenSSL 3.x
 * ships them only in the "legacy" provider, which the system
 * configuration deliberately leaves unloaded — and the name-based
 * lookups (SM4, GOST-KUZNYECHIK) are absent from it. The mapping is
 * therefore fixed and environment-independent: no
 * EVP_get_cipherbyname, no legacy provider, no weak cipher, and the
 * slot->cipher contract cannot differ between a client and a server
 * running different OpenSSL builds (the B5 failure mode).
 *
 * Every encrypt/decrypt site of BOTH endpoints must use this
 * function: the wire slot and the cipher have to agree on every
 * endpoint or the message is lost in transit.
 *
 * Returns the cipher for the slot (never NULL; if a constructor
 * ever returned NULL the slot falls back to AES-256-GCM, keeping
 * the contract total). When is_gcm is not NULL, stores 1 if the
 * slot is an AEAD (GCM family) mode — the caller must set the
 * 16-byte IV length and fetch/set the 16-byte authentication tag —
 * 0 otherwise (CTR: no tag, the tag field is zeroed on encrypt).
 */
static inline const EVP_CIPHER *radio_cipher_for_algo(int algo, int *is_gcm)
{
    const EVP_CIPHER *cipher;
    int gcm;

    switch (algo) {
    case CRYPTO_CHACHA:   cipher = EVP_chacha20_poly1305(); gcm = 1; break;
    case CRYPTO_ARIA:     cipher = EVP_aria_256_gcm();      gcm = 1; break;
    case CRYPTO_CAMELLIA: cipher = EVP_camellia_256_ctr();  gcm = 0; break;
    default:              cipher = EVP_aes_256_gcm();       gcm = 1; break;
    }

    if (!cipher) { cipher = EVP_aes_256_gcm(); gcm = 1; }

    if (is_gcm) *is_gcm = gcm;
    return cipher;
}

#endif /* RADIO_CRYPTO_H */
