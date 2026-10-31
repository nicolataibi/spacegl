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

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stddef.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include "network.h"
#include "radio_crypto.h"
#include "server_internal.h"
#include "conn.h"
#include "obj_delta.h"

extern uint8_t deep_space_key[32];
extern void ensure_player_algo_key(int p_idx, int k, bool private_mode);


#include <poll.h>
int read_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    char *p = (char *)buf;
    while (total < len) {
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        if (poll(&pfd, 1, 5000) <= 0) return -1; /* 5 second timeout */
        ssize_t n = recv(fd, p + total, len - total, 0);
        if (n == 0) return 0;
        if (n < 0) return -1;
        total += (size_t)n;
    }
    return (int)total;
}

int write_all(int fd, const void *buf, size_t len) {
    size_t total = 0;
    const char *p = (char *)buf;
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (total < len) {
        ssize_t n = send(fd, p + total, len - total, 0);
        if (n > 0) { total += (size_t)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /* ASYNC mode only: the session sockets are non-blocking, so a
             * full send buffer returns EAGAIN instead of blocking. Wait
             * for writability with a bound (1 s per probe, 30 s in total)
             * so a stalled receiver can neither spin the thread nor stall
             * it forever. On a legacy blocking socket send() never returns
             * EAGAIN, so this path is inert there. */
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec - t0.tv_sec >= 30) break;
            struct pollfd pfd = { .fd = fd, .events = POLLOUT };
            if (poll(&pfd, 1, 1000) <= 0) break;
            continue;
        }
        if (n < 0) break; /* hard error (EPIPE/ECONNRESET/EBADF) */
        break;            /* n == 0 cannot happen on send() */
    }
    return (int)total;
}

/* plaintext_len is explicit (not strlen): the adaptive relay re-encrypts a
 * decrypted fleet message, whose bytes may contain a NUL; strlen() cut the
 * message at the first one. Clamped to 65535, the validated wire bound. */
void encrypt_payload(PacketMessage *msg, const char *plaintext, size_t plaintext_len_in,
                     const uint8_t *key, int64_t frame_id) {
    int plaintext_len = (plaintext_len_in > 65535) ? 65535 : (int)plaintext_len_in;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    RAND_bytes(msg->iv, 16); 
    
    /* B5: single shared client/server cipher table (radio_crypto.h):
       the slot must resolve to the same cipher on every endpoint or
       the message is lost in transit. Before this table was shared,
       this copy knew only 4 slots (AES-256-GCM, CHACHA20-POLY1305,
       ARIA-256-GCM, CAMELLIA-256-CTR), so the adaptive relay
       re-encrypted a decrypted fleet message to a non-core recipient
       with AES-256-GCM while labeling it with the recipient's slot:
       undecryptable on the client. See the header for the full
       history and the experimental aliases. */
    int is_gcm = 0;
    const EVP_CIPHER *cipher = radio_cipher_for_algo(msg->crypto_algo, &is_gcm);

    /* B4: the ciphertext is written into msg->text (65536 bytes) and the
       wire length is re-validated as [0, 65535] at both endpoints (see
       packet_message_length_valid), so the output must stay <= 65535.
       The PKCS padding of the CBC slots (SEED, CAST5, IDEA, 3DES,
       BLOWFISH, DES, SM4) rounds the output up to a whole block — and
       adds a full extra block when the plaintext is block-aligned — so
       a 65520-byte plaintext already yields 65536 bytes of ciphertext:
       it fills msg->text to the last byte (zero margin — without the
       65535 clamp above, 65536 bytes in would give 65552 out and 16
       bytes past the buffer) and sets msg->length = 65536, outside the
       validated wire range: the receiving endpoint aborts the
       connection. The current table resolves everything to AEAD/CTR
       (output == input), so this guard keeps the API bound exact for
       the day a CBC slot is added here (adaptive relay re-encrypts per
       recipient). Reject like the failure paths below: cleartext
       fallback. padded_len is the exact padded ciphertext length for
       this cipher (computed with its own block size, so it is exact
       for the 8-byte blocks too). */
    if (EVP_CIPHER_get_block_size(cipher) > 1) {
        size_t block = (size_t)EVP_CIPHER_get_block_size(cipher);
        size_t padded_len = ((size_t)plaintext_len + block) / block * block;
        if (padded_len > 65535) {
            msg->is_encrypted = 0;
            size_t plen = (size_t)plaintext_len;
            memcpy(msg->text, plaintext, plen);
            msg->text[plen] = '\0';
            msg->length = (int32_t)plen;   /* <= 65535 */
            EVP_CIPHER_CTX_free(ctx);
            return;
        }
    }
    
    EVP_EncryptInit_ex(ctx, cipher, NULL, NULL, NULL);
    if (is_gcm) EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 16, NULL);
    
    if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, msg->iv) <= 0) {
        msg->is_encrypted = 0;
        size_t plen = (size_t)plaintext_len;
        memcpy(msg->text, plaintext, plen);
        msg->text[plen] = '\0';
        msg->length = (int32_t)plen;   /* <= 65535 */
        EVP_CIPHER_CTX_free(ctx);
        return;
    }
    
    int outlen;
    EVP_EncryptUpdate(ctx, (uint8_t*)msg->text, &outlen, (const uint8_t*)plaintext, plaintext_len);
    int final_len;
    if (EVP_EncryptFinal_ex(ctx, (uint8_t*)msg->text + outlen, &final_len) <= 0) {
        msg->is_encrypted = 0;
        size_t plen = (size_t)plaintext_len;
        memcpy(msg->text, plaintext, plen);
        msg->text[plen] = '\0';
        msg->length = (int32_t)plen;   /* <= 65535 */
        EVP_CIPHER_CTX_free(ctx);
        return;
    }
    
    if (is_gcm) EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, msg->tag);
    else memset(msg->tag, 0, 16);
    
    msg->origin_frame = frame_id;
    msg->length = outlen + final_len;
    EVP_CIPHER_CTX_free(ctx);
}

void broadcast_task(void *arg) {
    PacketMessage *pkt = (PacketMessage *)arg;
    if (pkt) {
        broadcast_message(pkt);
        free(pkt);
    }
}

/* Per-recipient routing snapshot of broadcast_message(): everything the
 * send loop needs, copied under game_mutex, so that the loop (which runs
 * outside game_mutex to keep slow sockets off the simulation lock) never
 * reads players[] fields that the game thread may be rewriting (crypto
 * frequency, keys, radio lock, active/socket). */
typedef struct {
    bool deliver;
    int fd;                /* players[i].socket when the snapshot was taken */
    int algo;              /* recipient frequency (state.shm_crypto_algo)   */
    uint8_t key[32];       /* recipient key for that frequency              */
} RelayTarget;

/* The station names that pass a captain's exclusive radio lock (a subset
 * of spacegl_is_station_name(), network.h, which no captain may take). */
static bool relay_is_system_sender(const char *from) {
    return strcmp(from, "SERVER") == 0 || strcmp(from, "COMPUTER") == 0 ||
           strcmp(from, "SCIENCE") == 0 || strcmp(from, "TACTICAL") == 0 ||
           strcmp(from, "ENGINEERING") == 0 || strcmp(from, "HELMSMAN") == 0 ||
           strcmp(from, "WARNING") == 0 || strcmp(from, "DAMAGE CONTROL") == 0 ||
           strcmp(from, "STARBASE") == 0 || strcmp(from, "Alliance Command") == 0;
}

/* Write under the recipient's socket_mutex, only if the slot still holds the
 * fd seen in the snapshot: a slot whose socket was detached (0) or rebound to
 * a new connection in the meantime is skipped instead of being written to. */
static void relay_write(int i, int fd, const void *buf, size_t len) {
    pthread_mutex_lock(&players[i].socket_mutex);
    if (players[i].socket != 0 && players[i].socket == fd) write_all(fd, buf, len);
    pthread_mutex_unlock(&players[i].socket_mutex);
}

/* One encoding of a fleet message (2026.10.31.40): the packet every
 * recipient on the same frequency with the same key receives. The fleet
 * keys are the same for every captain (derived from the server key and the
 * frequency), so a fleet message to sixteen captains on AES is encrypted
 * once and written sixteen times, instead of encrypted and copied sixteen
 * times into one reused buffer. */
typedef struct {
    int algo;
    uint8_t key[32];
    PacketMessage *pkt;    /* the packet to write (== msg for the clear one) */
    bool owned;            /* pkt was allocated here */
    size_t len;            /* bytes on the wire: header + text */
} RelayEncoding;

/* The encoding for (algo, key): an existing one, or a new one built from
 * the header of msg and the validated plaintext. NULL on allocation
 * failure (the recipient is skipped, as before). */
static RelayEncoding *relay_encoding_for(RelayEncoding *enc, int *n_enc, PacketMessage *msg,
                                         bool msg_is_plaintext, const char *plain, size_t plain_len,
                                         int algo, const uint8_t *key, int64_t frame_id) {
    for (int e = 0; e < *n_enc; e++) {
        if (enc[e].algo != algo) continue;
        if (algo == CRYPTO_NONE || CRYPTO_memcmp(enc[e].key, key, 32) == 0) return &enc[e];
    }
    RelayEncoding *r = &enc[*n_enc];
    r->algo = algo;
    if (algo != CRYPTO_NONE) memcpy(r->key, key, 32);
    else memset(r->key, 0, 32);
    if (algo == CRYPTO_NONE && msg_is_plaintext) {
        /* A cleartext message to a cleartext recipient: msg itself, its
         * text already being the validated plaintext (no copy). */
        msg->is_encrypted = 0;
        msg->crypto_algo = CRYPTO_NONE;
        msg->text[plain_len] = '\0';
        msg->length = (int32_t)plain_len;
        r->pkt = msg;
        r->owned = false;
    } else {
        PacketMessage *out = malloc(sizeof(PacketMessage));
        if (!out) return NULL;
        memcpy(out, msg, offsetof(PacketMessage, text));
        if (algo != CRYPTO_NONE) {
            /* Recipient wants encryption: encrypt the validated plaintext */
            out->crypto_algo = (uint8_t)algo;
            out->is_encrypted = 0x01;
            encrypt_payload(out, plain, plain_len, key, frame_id);
        } else {
            /* Recipient wants cleartext (of a decrypted fleet message) */
            out->is_encrypted = 0;
            out->crypto_algo = CRYPTO_NONE;
            memcpy(out->text, plain, plain_len);
            out->text[plain_len] = '\0';
            out->length = (int32_t)plain_len;
        }
        r->pkt = out;
        r->owned = true;
    }
    r->len = offsetof(PacketMessage, text) + (size_t)r->pkt->length;
    (*n_enc)++;
    return r;
}

void broadcast_message(PacketMessage *msg) {
    /* The plaintext: msg->text itself for a cleartext message; a buffer
     * of its own only when a fleet message is decrypted here
     * (2026.10.31.40: no 64 KiB buffer zeroed and copied into for every
     * message). */
    char *decrypted = NULL;
    const char *plaintext = msg->text;
    size_t plain_len = 0;   /* exact plaintext length (may contain NUL bytes) */

    RelayTarget targets[MAX_CLIENTS];
    memset(targets, 0, sizeof(targets));

    /* 0x01 encrypted, 0x02 private, 0x04 peer, 0x08 exclusive */
    msg->from[sizeof(msg->from) - 1] = '\0';   /* network field: force a terminator */
    bool is_encrypted = (msg->is_encrypted != 0);
    bool is_private = (msg->is_encrypted & 0x02);
    bool is_peer = (msg->is_encrypted & 0x04);
    bool is_exclusive_msg = (msg->is_encrypted & 0x08);
    bool transparent = (is_peer || is_private || is_exclusive_msg);
    bool from_system = relay_is_system_sender(msg->from);

    pthread_mutex_lock(&game_mutex);

    int sender_idx = -1;
    for(int j=0; j<MAX_CLIENTS; j++) {
        if (players[j].active && strcmp(players[j].name, msg->from) == 0) {
            sender_idx = j;
            break;
        }
    }

    /* 1. CLASSIFY AND VALIDATE */
    if (is_encrypted && sender_idx != -1) {
        if (transparent) {
            /* PRIVACY ENFORCEMENT: We don't touch these. We relay the encrypted blob. */
            LOG_DEBUG("Encrypted Private/Peer/Exclusive message from %s. Relay mode: TRANSPARENT.\n", msg->from);
        } else {
            /* Level A (Fleet): Server must decrypt to allow cross-algorithm talk and cleartext fallback */
            decrypted = malloc(65536);
            if (!decrypted) {
                pthread_mutex_unlock(&game_mutex);
                return;
            }
            EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
            int algo = msg->crypto_algo;

            ensure_player_algo_key(sender_idx, algo, false);

            /* B5: the shared client/server cipher table (radio_crypto.h).
               Before this was shared, this copy knew only 4 slots and
               resolved every other one (enc seed, enc cast, enc rc4, ...)
               to AES-256-GCM: the tag check failed and the fleet message
               died here with "Frequency parity failure", never delivered
               even to the sender. */
            int is_gcm = 0;
            const EVP_CIPHER *cipher = radio_cipher_for_algo(algo, &is_gcm);

            /* RANGE CHECK: msg->crypto_algo is a network uint8_t (0..255) but
               algo_keys has only MAX_CRYPTO_ALGOS + 1 rows — an out-of-range
               value (22..255, or 0 = CRYPTO_NONE) would read past the array.
               Same 1..MAX guard as the adaptive relay / send_server_msg paths
               (ensure_player_algo_key above already no-ops these values, its
               result was ignored). Fallback key: tag check then fails and the
               message is dropped with a parity error. */
            uint8_t *k = (algo >= 1 && algo <= MAX_CRYPTO_ALGOS) ?
                             players[sender_idx].algo_keys[algo] :
                             players[sender_idx].session_key;
            EVP_DecryptInit_ex(ctx, cipher, NULL, NULL, NULL);
            if (is_gcm) EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 16, NULL);
            EVP_DecryptInit_ex(ctx, NULL, NULL, k, msg->iv);
            int outlen;
            EVP_DecryptUpdate(ctx, (uint8_t *)decrypted, &outlen, (const uint8_t *)msg->text, msg->length);
            if (is_gcm) EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, msg->tag);
            int final_len;
            if (EVP_DecryptFinal_ex(ctx, (uint8_t *)decrypted + outlen, &final_len) <= 0) {
                /* Decryption failed */
                send_server_msg(sender_idx, "COMPUTER", "TRANSMISSION ERROR: Frequency parity failure.");
                EVP_CIPHER_CTX_free(ctx); pthread_mutex_unlock(&game_mutex);
                OPENSSL_cleanse(decrypted, 65536);
                free(decrypted);
                return;
            }
            plain_len = (size_t)outlen + (size_t)final_len;   /* both >= 0 after a successful decrypt */
            if (plain_len > 65535) plain_len = 65535;
            decrypted[plain_len] = '\0';
            plaintext = decrypted;
            EVP_CIPHER_CTX_free(ctx);
        }
    } else {
        /* Cleartext message: the text is the plaintext (the wire length is
         * validated to [0, 65535] by the dispatcher; clamped again here). */
        plain_len = (msg->length <= 0) ? 0 : (msg->length < 65535) ? (size_t)msg->length
                                                                   : 65535;
    }

    /* 2. ROUTING SNAPSHOT (under game_mutex): who receives the message, on
     *    which fd, with which frequency and key. */
    int exclusive_target = -1;
    if (is_exclusive_msg) {
        /* Resolve destination: Priority 1: Packet target_id, Priority 2: Sender Lock, Priority 3: Reciprocity */
        if (msg->target_id > 0) {
            exclusive_target = msg->target_id - 1;
        } else if (sender_idx != -1) {
            exclusive_target = players[sender_idx].radio_lock_target - 1;
            if (exclusive_target < 0) {
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (players[j].active && players[j].radio_lock_target == (sender_idx + 1)) {
                        exclusive_target = j;
                        break;
                    }
                }
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!players[i].active || players[i].socket == 0) continue;

        if (is_exclusive_msg) {
            /* LEVEL D: EXCLUSIVE MODE HARD-LOCK — deliver ONLY to sender
               (loopback) and resolved target */
            if (i != sender_idx && i != exclusive_target) continue;
        } else {
            /* STANDARD FILTERS (only for non-exclusive messages) */
            if (msg->scope == SCOPE_FACTION && players[i].faction != msg->faction) continue;
            if (msg->scope == SCOPE_PRIVATE && (i + 1) != msg->target_id && i != sender_idx) continue;

            /* Recipient-side Exclusive Lock: if recipient is in enc4, they only hear their partner */
            if (players[i].radio_lock_target > 0) {
                int locked_partner_idx = players[i].radio_lock_target - 1;
                if (!from_system && sender_idx != locked_partner_idx && i != sender_idx) continue;
            }
        }

        RelayTarget *t = &targets[i];
        t->deliver = true;
        t->fd = players[i].socket;
        t->algo = players[i].state.shm_crypto_algo;
        if (!transparent && t->algo != CRYPTO_NONE) {
            /* Fallback to the session key for an out-of-table frequency */
            const uint8_t *rk = (t->algo >= 1 && t->algo <= MAX_CRYPTO_ALGOS) ?
                                players[i].algo_keys[t->algo] : players[i].session_key;
            memcpy(t->key, rk, 32);
        }
    }
    int64_t frame_id = spacegl_master.frame_id;

    /* Release game_mutex before the send loop: from here on only the
     * snapshot (targets[], plaintext, msg) is read; each write takes the
     * recipient's socket_mutex and re-checks the fd (relay_write). */
    pthread_mutex_unlock(&game_mutex);

    /* 3. BROADCAST LOOP (executed outside game_mutex) */
    RelayEncoding enc[MAX_CLIENTS];
    int n_enc = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        RelayTarget *t = &targets[i];
        if (!t->deliver) continue;

        if (transparent) {
            /* STRICT RELAY: No one gets cleartext if they are not the intended recipient with the key.
               This includes Exclusive Mode (0x08) to prevent noise on other frequencies. */
            relay_write(i, t->fd, msg, offsetof(PacketMessage, text) + (size_t)msg->length);
        } else {
            /* ADAPTIVE RELAY: Translate for recipient or send cleartext,
             * one encoding per distinct frequency and key */
            RelayEncoding *r = relay_encoding_for(enc, &n_enc, msg, decrypted == NULL, plaintext,
                                                  plain_len, t->algo, t->key, frame_id);
            if (r) relay_write(i, t->fd, r->pkt, r->len);
        }
    }

    for (int e = 0; e < n_enc; e++) {
        if (enc[e].owned) {
            /* a cleartext copy of a decrypted fleet message is secret */
            size_t used = enc[e].len + 1;
            OPENSSL_cleanse(enc[e].pkt, used < sizeof(PacketMessage) ? used : sizeof(PacketMessage));
            free(enc[e].pkt);
        }
    }
    OPENSSL_cleanse(enc, sizeof(enc));
    OPENSSL_cleanse(targets, sizeof(targets));
    if (decrypted) {
        OPENSSL_cleanse(decrypted, 65536);
        free(decrypted);
    }
}

/* Recipient route of a server message: fd, frequency and key captured when
 * send_server_msg() is called — in the caller's context, normally under
 * game_mutex — instead of being read later from players[] on a thread-pool
 * worker that holds no game lock (ASYNC mode). */
typedef struct {
    int p_idx;
    int fd;
    int algo;
    uint8_t key[32];
} MsgRoute;

static bool msg_route_capture(int p_idx, MsgRoute *r) {
    if (p_idx < 0 || p_idx >= MAX_CLIENTS) return false;
    memset(r, 0, sizeof(*r));
    r->p_idx = p_idx;
    r->fd = players[p_idx].socket;
    if (r->fd == 0) return false;
    r->algo = players[p_idx].state.shm_crypto_algo;
    if (r->algo != CRYPTO_NONE) {
        const uint8_t *k = (r->algo >= 1 && r->algo <= MAX_CRYPTO_ALGOS) ?
                           players[p_idx].algo_keys[r->algo] : players[p_idx].session_key;
        memcpy(r->key, k, 32);
    }
    return true;
}

/* Synchronous core: builds the PacketMessage, encrypts it for the captured
 * route and write_all()s it under the per-player socket_mutex, only if the
 * slot still holds the captured fd. LEGACY mode calls it directly; ASYNC
 * mode runs it on a thread-pool worker so the single-threaded epoll loop
 * (and the 60 Hz game thread) never blocks on a send. */
static void send_server_msg_sync(const MsgRoute *r, const char *from, const char *text) {
    PacketMessage *msg = malloc(sizeof(PacketMessage));
    if (!msg) return;
    memset(msg, 0, sizeof(PacketMessage));
    msg->type = PKT_MESSAGE;
    strncpy(msg->from, from, 63);

    size_t tlen = strlen(text);
    if (tlen > 65535) tlen = 65535;
    if (r->algo != CRYPTO_NONE) {
        msg->is_encrypted = 1;
        msg->crypto_algo = (uint8_t)r->algo;
        encrypt_payload(msg, text, tlen, r->key, spacegl_master.frame_id);
    } else {
        msg->is_encrypted = 0;
        memcpy(msg->text, text, tlen);
        msg->text[tlen] = '\0';
        msg->length = (int32_t)tlen;
    }

    size_t pkt_size = offsetof(PacketMessage, text) + (size_t)msg->length;
    pthread_mutex_lock(&players[r->p_idx].socket_mutex);
    if (players[r->p_idx].socket != 0 && players[r->p_idx].socket == r->fd)
        write_all(r->fd, msg, pkt_size);
    pthread_mutex_unlock(&players[r->p_idx].socket_mutex);
    OPENSSL_cleanse(msg, sizeof(PacketMessage));
    free(msg);
}

extern threadpool_t *g_pool;

typedef struct {
    MsgRoute route;
    char from[64];
    char *text;   /* heap copy, freed by the task */
} ServerMsgTask;

static void server_msg_task(void *arg) {
    ServerMsgTask *t = (ServerMsgTask *)arg;
    if (t) {
        if (t->text) send_server_msg_sync(&t->route, t->from, t->text);
        free(t->text);
        OPENSSL_cleanse(t, sizeof(*t));
        free(t);
    }
}

void send_server_msg(int p_idx, const char *from, const char *text) {
    if (p_idx == -1) {
        /* Broadcast system message to all connected captains */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (players[i].active && players[i].socket != 0) send_server_msg(i, from, text);
        }
        return;
    }

    MsgRoute route;
    if (!msg_route_capture(p_idx, &route)) return;   /* no such slot / not connected */

    /* ASYNC mode: the caller is (or sits on) the single-threaded epoll
     * loop, which must never block on a send — delegate the (blocking,
     * bounded) write to the thread pool, exactly like the other outbound
     * transfers (login sync, fleet broadcast). LEGACY mode: the original
     * synchronous behavior, unchanged. */
    if (g_io_mode == SPACEGL_IO_ASYNC && g_pool) {
        ServerMsgTask *t = malloc(sizeof(ServerMsgTask));
        if (t) {
            t->route = route;
            strncpy(t->from, from, 63);
            t->from[63] = '\0';
            size_t tlen = strlen(text);
            if (tlen > 65535) tlen = 65535;
            t->text = malloc(tlen + 1);
            if (t->text) {
                memcpy(t->text, text, tlen);
                t->text[tlen] = '\0';
                if (threadpool_add_task(g_pool, server_msg_task, t) == 0) {
                    OPENSSL_cleanse(&route, sizeof(route));
                    return; /* delegated */
                }
                free(t->text);
            }
            OPENSSL_cleanse(t, sizeof(*t));
            free(t);
        }
        /* Allocation or enqueue failure: degrade to the sync path. */
    }
    send_server_msg_sync(&route, from, text);
    OPENSSL_cleanse(&route, sizeof(route));
}

void send_optimized_update(int p_idx, PacketUpdate *upd) {
    ConnectedPlayer *p = &players[p_idx];
    uint64_t mask = 0;
    
    /* 1. Determine what changed */
    if (p->last_sent_state.q1 != upd->q1 || p->last_sent_state.q2 != upd->q2 || p->last_sent_state.q3 != upd->q3 ||
        p->last_sent_state.s1 != upd->s1 || p->last_sent_state.s2 != upd->s2 || p->last_sent_state.s3 != upd->s3 ||
        p->last_sent_state.van_h != upd->van_h || p->last_sent_state.van_m != upd->van_m || p->last_sent_state.van_r != upd->van_r ||
        p->last_sent_state.eta != upd->eta) mask |= UPD_TRANSFORM;

    if (p->last_sent_state.energy != upd->energy || p->last_sent_state.torpedoes != upd->torpedoes || 
        p->last_sent_state.hull_integrity != upd->hull_integrity || p->last_sent_state.cargo_energy != upd->cargo_energy ||
        p->last_sent_state.cargo_torpedoes != upd->cargo_torpedoes || p->last_sent_state.crew_count != upd->crew_count ||
        p->last_sent_state.prison_unit != upd->prison_unit || p->last_sent_state.composite_plating != upd->composite_plating) mask |= UPD_VITALS;

    if (memcmp(p->last_sent_state.shields, upd->shields, sizeof(upd->shields)) != 0) mask |= UPD_SHIELDS;
    if (memcmp(p->last_sent_state.system_health, upd->system_health, sizeof(upd->system_health)) != 0) mask |= UPD_SYSTEMS;  // NOLINT(bugprone-suspicious-memory-comparison,cert-exp42-c,cert-flp37-c): bitwise change detection on purpose; at worst one extra update)
    if (memcmp(p->last_sent_state.inventory, upd->inventory, sizeof(upd->inventory)) != 0 || 
        p->last_sent_state.life_support != upd->life_support || p->last_sent_state.anti_matter_count != upd->anti_matter_count ||
        memcmp(p->last_sent_state.power_dist, upd->power_dist, sizeof(upd->power_dist)) != 0) mask |= UPD_INTERNAL;  // NOLINT(bugprone-suspicious-memory-comparison,cert-exp42-c,cert-flp37-c): bitwise change detection on purpose; at worst one extra update)
    
    if (p->last_sent_state.lock_target != upd->lock_target || p->last_sent_state.tube_state != upd->tube_state ||
        p->last_sent_state.current_tube != upd->current_tube || p->last_sent_state.ion_beam_charge != upd->ion_beam_charge ||
        memcmp(p->last_sent_state.tube_load_timers, upd->tube_load_timers, sizeof(upd->tube_load_timers)) != 0 ||
        memcmp(p->last_sent_state.tube_torpedo_etas, upd->tube_torpedo_etas, sizeof(upd->tube_torpedo_etas)) != 0) mask |= UPD_COMBAT;
    
    if (p->last_sent_state.is_cloaked != upd->is_cloaked || p->last_sent_state.is_docked != upd->is_docked ||
        p->last_sent_state.red_alert != upd->red_alert || p->last_sent_state.nav_state != upd->nav_state ||
        p->last_sent_state.show_axes != upd->show_axes || p->last_sent_state.show_grid != upd->show_grid ||
        p->last_sent_state.show_bridge != upd->show_bridge || p->last_sent_state.show_map != upd->show_map ||
        p->last_sent_state.map_filter != upd->map_filter ||
        p->last_sent_state.force_shutdown != upd->force_shutdown ||
        p->last_sent_state.shm_crypto_algo != upd->shm_crypto_algo ||
        p->last_sent_state.radio_lock_target != upd->radio_lock_target ||
        p->last_sent_state.encryption_flags != upd->encryption_flags) mask |= UPD_FLAGS;

    bool any_torp = upd->torps[0].active || upd->torps[1].active || upd->torps[2].active || upd->torps[3].active;
    bool last_any_torp = p->last_sent_state.torps[0].active || p->last_sent_state.torps[1].active || p->last_sent_state.torps[2].active || p->last_sent_state.torps[3].active;
    
    if (upd->event_count > 0 || upd->torpedo_count > 0 || any_torp || (any_torp != last_any_torp) || upd->wormhole.active || upd->supernova_pos.active) mask |= UPD_EFFECTS;
    if (upd->probes[0].active || upd->probes[1].active || upd->probes[2].active) mask |= UPD_PROBES;
    
    if (upd->beam_count > 0 || p->last_sent_state.beam_count > 0) mask |= UPD_COMBAT; 
    
    ObjDeltaKind od_kind = OBJDELTA_NONE;
    ObjDeltaPlan od_plan = {0};
    bool quadrant_changed = (p->last_q1 != upd->q1 || p->last_q2 != upd->q2 || p->last_q3 != upd->q3);
    p->full_update_timer++;
    if (quadrant_changed || p->full_update_timer > (5ULL * GAME_TICK_RATE)) {
        mask |= UPD_OBJECTS | UPD_MAP | UPD_FULL;
        p->full_update_timer = 0;
        p->last_q1 = upd->q1; p->last_q2 = upd->q2; p->last_q3 = upd->q3;
        /* === DIAG [PROTOCOL-3] (smoke test): proves the full list is
         * re-sent even without a membership change (every 5 s or on
         * quadrant change). If the client stays wrong for >5 s despite
         * these lines, the fault is client-side (rx/parse/shm). === */
        SG_TRACE3(SG_CAT_PROTOCOL,
            "player %d (%s): UPD_OBJECTS forced into delta (quadrant_changed=%d, full_refresh) objs=%d frame=%llu",
            p_idx, p->name, (int)quadrant_changed, upd->object_count, (unsigned long long)upd->frame_id);
    } else {
        /* Per-object delta (wire revision 3, include/obj_delta.h): only the
         * objects that changed, the motion alone when only it changed; the
         * whole list when that would not be smaller. */
        od_kind = objdelta_plan(p->last_sent_state.objects, p->last_sent_state.object_count,
                                upd->objects, upd->object_count, OBJDELTA_MAX_SIZE, &od_plan);
        if (od_kind == OBJDELTA_FULL) {
            mask |= UPD_OBJECTS;
            SG_TRACE3(SG_CAT_PROTOCOL,
                "player %d (%s): UPD_OBJECTS (delta not smaller) %d -> %d objs frame=%llu",
                p_idx, p->name, p->last_sent_state.object_count, upd->object_count, (unsigned long long)upd->frame_id);
        } else if (od_kind == OBJDELTA_DELTA) {
            mask |= UPD_OBJDELTA;
        }
    }

    if (mask == 0) {
        /* Nothing changed for this client this tick: no packet at all.
         * TRACE1 only (per-tick, would flood anything coarser). */
        SG_TRACE1(SG_CAT_PROTOCOL, "no delta for player %d (%s): mask=0, frame=%llu",
                  p_idx, p->name, (unsigned long long)upd->frame_id);
        return; 
    }

    SG_TRACE2(SG_CAT_PROTOCOL, "delta to player %d (%s): mask=0x%llx frame=%llu (combat=%d beams_in_upd=%d)",
              p_idx, p->name, (unsigned long long)mask, (unsigned long long)upd->frame_id,
              (int)(mask & UPD_COMBAT), upd->beam_count);

    uint8_t *buffer = malloc(262144);
    if (!buffer) return;
    PacketUpdateDelta *delta = (PacketUpdateDelta *)buffer;
    delta->type = PKT_UPDATE_DELTA;
    delta->frame_id = upd->frame_id;
    delta->update_mask = mask;
    
    uint8_t *ptr = delta->data;
    
    if (mask & UPD_TRANSFORM) {
        UpdateBlockTransform b = {upd->q1, upd->q2, upd->q3, upd->s1, upd->s2, upd->s3, upd->van_h, upd->van_m, upd->van_r, upd->eta};
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }
    if (mask & UPD_VITALS) {
        UpdateBlockVitals b = {upd->energy, upd->torpedoes, upd->cargo_energy, upd->cargo_torpedoes, upd->crew_count, upd->prison_unit, upd->composite_plating, upd->hull_integrity};
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }
    if (mask & UPD_SHIELDS) {
        UpdateBlockShields b = {0}; memcpy(b.shields, upd->shields, sizeof(upd->shields));
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }
    if (mask & UPD_SYSTEMS) {
        UpdateBlockSystems b = {0}; memcpy(b.system_health, upd->system_health, sizeof(upd->system_health));
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }
    if (mask & UPD_INTERNAL) {
        UpdateBlockInternal b; memcpy(b.inventory, upd->inventory, sizeof(upd->inventory)); memcpy(b.power_dist, upd->power_dist, sizeof(upd->power_dist));
        b.life_support = upd->life_support; b.anti_matter_count = upd->anti_matter_count;
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }
    if (mask & UPD_COMBAT) {
        UpdateBlockCombat b = {upd->lock_target, upd->tube_state, {0}, {0}, upd->current_tube, upd->ion_beam_charge};
        memcpy(b.tube_load_timers, upd->tube_load_timers, sizeof(b.tube_load_timers));
        memcpy(b.tube_torpedo_etas, upd->tube_torpedo_etas, sizeof(b.tube_torpedo_etas));
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
        /* Append beams if any */
        int32_t bc = upd->beam_count;
        if (bc > MAX_NET_BEAMS) bc = MAX_NET_BEAMS;
        memcpy(ptr, &bc, sizeof(int32_t)); ptr += sizeof(int32_t);
        if (bc > 0) { memcpy(ptr, upd->beams, (size_t)bc * sizeof(NetBeam)); ptr += (size_t)bc * sizeof(NetBeam); }
        if (bc > 0)
            SG_TRACE3(SG_CAT_PHA, "DELIVER to player %d (%s): %d beams in DELTA (mask=0x%llx, frame=%llu)",
                      p_idx, p->name, bc, (unsigned long long)mask, (unsigned long long)upd->frame_id);
        else if (p->last_sent_state.beam_count > 0)
            SG_TRACE4(SG_CAT_PHA, "DELIVER to player %d (%s): beam CLEAR in DELTA (%d -> 0, mask=0x%llx)",
                      p_idx, p->name, p->last_sent_state.beam_count, (unsigned long long)mask);
    }
    if (mask & UPD_FLAGS) {
        UpdateBlockFlags b = {upd->is_cloaked, upd->is_docked, upd->red_alert, upd->is_jammed, upd->nav_state, upd->show_axes, upd->show_grid, upd->show_bridge, upd->show_map, upd->map_filter, upd->force_shutdown, upd->shm_crypto_algo, upd->encryption_flags, upd->radio_lock_target};
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }
    if (mask & UPD_EFFECTS) {
        UpdateBlockEffects b; memset(&b, 0, sizeof(b));
        b.supernova_pos = upd->supernova_pos; memcpy(b.supernova_q, upd->supernova_q, sizeof(upd->supernova_q));
        memcpy(b.torps, upd->torps, sizeof(upd->torps)); b.wormhole = upd->wormhole;
        b.event_count = upd->event_count; if (b.event_count > MAX_NET_EVENTS) b.event_count = MAX_NET_EVENTS;
        memcpy(b.events, upd->events, (size_t)b.event_count * sizeof(NetEvent));
        b.torpedo_count = upd->torpedo_count; if (b.torpedo_count > MAX_VISIBLE_TORPEDOES) b.torpedo_count = MAX_VISIBLE_TORPEDOES;
        memcpy(b.visible_torpedoes, upd->visible_torpedoes, (size_t)b.torpedo_count * sizeof(NetVisibleTorpedo));
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }
    if (mask & UPD_PROBES) {
        UpdateBlockProbes b = {0}; memcpy(b.probes, upd->probes, sizeof(upd->probes));
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }
    if (mask & UPD_OBJECTS) {
        int32_t oc = upd->object_count; memcpy(ptr, &oc, sizeof(int32_t)); ptr += sizeof(int32_t);
        if (oc > 0) { memcpy(ptr, upd->objects, (size_t)oc * sizeof(NetObject)); ptr += (size_t)oc * sizeof(NetObject); }
    } else if (mask & UPD_OBJDELTA) {
        ptr += objdelta_write(p->last_sent_state.objects, upd->objects, &od_plan, ptr);
    }
    if (mask & UPD_MAP) {
        UpdateBlockMap b = {upd->map_update_val, {upd->map_update_q[0], upd->map_update_q[1], upd->map_update_q[2]}, upd->map_update_val2, {upd->map_update_q2[0], upd->map_update_q2[1], upd->map_update_q2[2]}};
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }


    size_t total_size = (size_t)(ptr - buffer);
    pthread_mutex_lock(&p->socket_mutex);
    if (p->socket != 0) write_all(p->socket, buffer, total_size);
    pthread_mutex_unlock(&p->socket_mutex);
    memcpy(&p->last_sent_state, upd, sizeof(PacketUpdate));
    free(buffer);
}

/*
 * send_pending_updates — called from the game loop AFTER releasing game_mutex.
 * Sends all packets that update_game_logic() built and flagged with pending_send.
 * This decouples blocking TCP writes from the 60Hz simulation lock.
 */
void send_pending_updates(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!players[i].pending_send) {
            continue;
        }
        players[i].pending_send = false;
        send_optimized_update(i, &players[i].upd_packet);
    }
}
