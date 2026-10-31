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

/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * Shared client-packet dispatcher (legacy + async I/O modes).
 * See include/packets.h for the contract.
 */

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <openssl/crypto.h>
#include "server_internal.h"
#include "conn.h"
#include "packets.h"
#include "handshake_v2.h"

extern threadpool_t *g_pool;

/* QUERY/LOGIN packets received on the connection bound to each slot since
 * its handshake: the seq of hs_v2_cred_xor() (handshake_v2.h). Kept out of
 * ConnectedPlayer, whose layout is persisted in galaxy.dat. Guarded by
 * game_mutex. */
static uint32_t g_cred_seq[MAX_CLIENTS];

/* Signing key presented by each slot's authenticated PKT_LOGIN (also kept
 * out of the persisted ConnectedPlayer). Guarded by game_mutex. */
static uint8_t g_sign_pub[MAX_CLIENTS][32];
static bool g_sign_set[MAX_CLIENTS];

void packets_bind_signing_key(int slot, const uint8_t pub[32]) {
    if (slot < 0 || slot >= MAX_CLIENTS) return;
    uint8_t acc = 0;
    for (int i = 0; i < 32; i++) acc |= pub[i];
    memcpy(g_sign_pub[slot], pub, 32);
    g_sign_set[slot] = (acc != 0);
}

/* Pending handshakes: the fd the handshake bound to each slot (0 = none)
 * and its deadline. Guarded by game_mutex. */
static int g_hs_fd[MAX_CLIENTS];
static time_t g_hs_deadline[MAX_CLIENTS];

int packets_close_stale_handshakes(int epoll_fd, time_t now) {
    int stale[MAX_CLIENTS];
    int n = 0;
    pthread_mutex_lock(&game_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_hs_fd[i] == 0) continue;
        if (players[i].socket != g_hs_fd[i] || players[i].active) {
            g_hs_fd[i] = 0;              /* closed, moved or logged in meanwhile */
            continue;
        }
        if (now > g_hs_deadline[i]) {
            stale[n++] = g_hs_fd[i];
            g_hs_fd[i] = 0;
        }
    }
    pthread_mutex_unlock(&game_mutex);

    for (int k = 0; k < n; k++) {
        int fd = stale[k];
        SG_WARNING(SG_CAT_SECURITY, "fd %d: no login %d s after the handshake: connection closed",
                   fd, HANDSHAKE_PENDING_TIMEOUT_S);
        Conn *c = (g_io_mode == SPACEGL_IO_ASYNC) ? conn_find(fd) : NULL;
        if (c) {
            conn_close(epoll_fd, c, CONN_CLOSE_REAP);
        } else {
            reap_player_fd(fd, CONN_CLOSE_REAP);
            if (epoll_fd >= 0) epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
        }
    }
    return n;
}

bool packets_signing_key(int slot, uint8_t out[32]) {
    if (slot < 0 || slot >= MAX_CLIENTS || !g_sign_set[slot]) return false;
    memcpy(out, g_sign_pub[slot], 32);
    return true;
}

int sanitize_captain_name(const char *name) {
    if (!name || name[0] == '\0') {
        return 0;
    }
    if (spacegl_is_station_name(name)) {
        return 0;   /* reserved: the "from" of the server's own messages */
    }
    size_t len = 0;
    for (const char *p = name; *p != '\0'; p++) {
        len++;
        if (len > 32) {
            return 0;
        }
        if (!((*p >= 'A' && *p <= 'Z') ||
              (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') ||
              *p == '_' || *p == '-')) {
            return 0;
        }
    }
    return 1;
}

/* Captain identity record on disk: captains/<name>/identity.hash (32 B)
 * and, for salted (v2) accounts, captains/<name>/identity.salt (16 B). */
typedef struct {
    bool exists;          /* identity.hash could be opened             */
    bool have_hash;       /* ... and holds 32 bytes                    */
    bool have_salt;       /* identity.salt holds 16 bytes (v2 account) */
    uint8_t hash[32];
    uint8_t salt[16];
    char auth_path[1024];
    char salt_path[1024];
} IdentityRecord;

/* name must already have passed sanitize_captain_name(). */
static void identity_load(const char *name, IdentityRecord *r) {
    memset(r, 0, sizeof(*r));
    char rel_auth[192];
    char rel_salt[192];
    snprintf(rel_auth, sizeof(rel_auth), "captains/%s/identity.hash", name);
    snprintf(rel_salt, sizeof(rel_salt), "captains/%s/identity.salt", name);
    server_data_path(r->auth_path, sizeof(r->auth_path), rel_auth);
    server_data_path(r->salt_path, sizeof(r->salt_path), rel_salt);

    FILE *fa = fopen(r->auth_path, "rb");
    if (!fa) return;
    r->exists = true;
    r->have_hash = (fread(r->hash, 1, 32, fa) == 32);
    fclose(fa);

    FILE *fs = fopen(r->salt_path, "rb");
    if (fs) {
        r->have_salt = (fread(r->salt, 1, 16, fs) == 16);
        fclose(fs);
    }
}

/* Does the packet prove the password of this record? The client's salt
 * must be the stored one and pass_hash must match (constant-time
 * comparisons: the hashes are secrets). A record without identity.salt
 * (the unsalted scheme used before 2026) proves nothing: since
 * 2026.10.31.25 it is refused, the captain enrolls again. */
static bool identity_hash_ok(const IdentityRecord *r, const PacketLogin *pkt) {
    if (!r->exists || !r->have_hash || !r->have_salt) return false;
    return CRYPTO_memcmp(pkt->salt, r->salt, 16) == 0 &&
           CRYPTO_memcmp(r->hash, pkt->pass_hash, 32) == 0;
}

/* Fetch the next chunk of the packet body. LEGACY mode (io->rx == NULL)
 * reads it from the socket with read_all() — the blocking call bounded by
 * the 5 s poll, byte-for-byte as the pre-refactor inline loop did. ASYNC
 * mode consumes the bytes already staged by the conn state machine, so
 * the handler never touches the socket at all. */
static bool pkt_read_body(PktIO *io, void *dst, size_t len) {
    if (io->rx) {
        if (io->rx_len < len) return false;
        memcpy(dst, io->rx, len);
        io->rx += len;
        io->rx_len -= len;
        return true;
    }
    return read_all(io->fd, dst, len) > 0;
}

/* Send a small reply. LEGACY: write_all() (the legacy fd stays blocking,
 * so this is byte-for-byte the pre-refactor behavior). ASYNC: staged in
 * the session TX buffer and drained now or on EPOLLOUT — the loop is
 * never blocked on a slow receiver. Returns 0 on success, -1 if the
 * (async) connection was dropped. */
static int io_write(PktIO *io, const void *data, size_t len) {
    if (io->conn) return conn_send(io->epoll_fd, io->conn, data, len);
    write_all(io->fd, data, len);
    return 0;
}

/* Tear the connection down from inside the dispatcher. Both modes do the
 * same bookkeeping — reap_player_fd() (slot reclamation, see conn.h),
 * then epoll_ctl(DEL) + close(fd); async mode additionally releases the
 * session slot via conn_close(). Returns 1 so the caller stops processing
 * the event. */
static int io_close(PktIO *io, ConnCloseReason reason) {
    if (io->conn) {
        conn_close(io->epoll_fd, io->conn, reason);
    } else {
        reap_player_fd(io->fd, reason);
        epoll_ctl(io->epoll_fd, EPOLL_CTL_DEL, io->fd, NULL);
        close(io->fd);
    }
    return 1;
}

/* Rate limiting (DoS protection).
 *
 * Called on every incoming packet (except handshake). Refills the token bucket once
 * per second, then decrements. If the bucket is empty, the packet
 * is dropped (returns 1). If the drop run exceeds PKT_DROP_THRESHOLD,
 * returns 2 to indicate the connection should be closed. */
static int pkt_rate_limit_check(int epoll_fd, int fd) {
    (void)epoll_fd;
    time_t now = time(NULL);

    int slot = -1;
    pthread_mutex_lock(&game_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].socket == fd) { slot = i; break; }
    }

    if (slot < 0) {
        pthread_mutex_unlock(&game_mutex);
        return 0;  /* no slot yet (handshake): no rate limit */
    }

    ConnectedPlayer *p = &players[slot];
    if (p->pkt_last_refill == 0 || (uint32_t)now != p->pkt_last_refill) {
        p->pkt_last_refill = (uint32_t)now;
        p->pkt_tokens = PKT_RATE_LIMIT;
    }

    if (p->pkt_tokens > 0) {
        p->pkt_tokens--;
        p->pkt_drop_run = 0;
        pthread_mutex_unlock(&game_mutex);
        return 0;  /* allowed */
    }

    /* Over limit: drop this packet */
    p->pkt_drop_run++;
    uint32_t run = p->pkt_drop_run;
    pthread_mutex_unlock(&game_mutex);

    LOG_DEBUG("RATE LIMIT: fd %d over limit (%u drops)\n", fd, run);
    if (run > PKT_DROP_THRESHOLD) {
        LOG_DEBUG("RATE LIMIT: fd %d exceeded drop threshold, closing\n", fd);
        return 2; /* exceed threshold: close connection */
    }
    return 1;  /* drop this packet (don't process it) */
}

int dispatch_packet(PktIO *io, int32_t type) {
    int fd = io->fd;
    int p_idx = -1;

    /* Rate limiting check (DoS protection). */
    if (type != PKT_HANDSHAKE) {
        int rl = pkt_rate_limit_check(io->epoll_fd, fd);
        if (rl != 0) {
            if (rl == 2) {
                return io_close(io, CONN_CLOSE_SILENT);
            }
            /* Drain body if in legacy mode so next packet type aligns */
            if (!io->rx) {
                size_t bsz = 0;
                switch (type) {
                case PKT_QUERY_KEY: bsz = sizeof(PacketQueryKey) - sizeof(int32_t); break;
                case PKT_QUERY:
                case PKT_LOGIN:     bsz = sizeof(PacketLogin) - sizeof(int32_t); break;
                case PKT_COMMAND:   bsz = sizeof(PacketCommand) - sizeof(int32_t); break;
                case PKT_MESSAGE:   bsz = sizeof(PacketMessage) - sizeof(int32_t); break;
                default:            return io_close(io, CONN_CLOSE_SILENT);
                }
                char dummy[512];
                while (bsz > 0) {
                    size_t chunk = bsz < sizeof(dummy) ? bsz : sizeof(dummy);
                    if (!pkt_read_body(io, dummy, chunk)) {
                        return io_close(io, CONN_CLOSE_SILENT);
                    }
                    bsz -= chunk;
                }
            }
            return 1;
        }
    }

    if (type == PKT_HANDSHAKE) {
        LOG_DEBUG("Handshake request received from FD %d\n", fd);
        PacketHandshake h_pkt;
        h_pkt.type = type;
        size_t h_body = sizeof(PacketHandshake) - sizeof(int);
        if (pkt_read_body(io, ((char*)&h_pkt) + sizeof(int), h_body)) {
            LOG_DEBUG("Handshake data read successfully (%zu bytes)\n", h_body);
            /* Protocol v2 only (include/handshake_v2.h): the v1 hello put
             * the master key on the wire (MAGIC XOR K). A v1 client is
             * told nothing it could use and dropped. */
            if (h_pkt.pubkey_len != HANDSHAKE_V2_MARKER) {
                if (h_pkt.pubkey_len == HANDSHAKE_MARKER_REV2) {
                    SG_WARNING(SG_CAT_SECURITY, "fd %d: deck of wire revision 2 refused (revision %d required)",
                               fd, SPACEGL_WIRE_REVISION);
                    fprintf(stderr, "\033[1;33m[SECURITY]\033[0m Deck of wire revision 2 refused on FD %d: the client must be updated (revision %d).\n",
                            fd, SPACEGL_WIRE_REVISION);
                } else {
                    SG_WARNING(SG_CAT_SECURITY, "fd %d: handshake v1 refused (client too old: protocol v2 required)", fd);
                    fprintf(stderr, "\033[1;33m[SECURITY]\033[0m Handshake v1 refused on FD %d: the client must be updated (protocol v2).\n", fd);
                }
                return io_close(io, CONN_CLOSE_SILENT);
            }
            /* Verify the client's proof of K and build the reply OUTSIDE
             * game_mutex: two X25519 operations and a few HMACs. */
            uint8_t hs_reply[HANDSHAKE_V2_REPLY_LEN];
            uint8_t hs_session[32];
            if (!hs_v2_server_reply(MASTER_SESSION_KEY, h_pkt.pubkey, GALAXY_VERIFY_KEY, hs_reply, hs_session)) {
                OPENSSL_cleanse(&h_pkt, sizeof(h_pkt));
                fprintf(stderr, "\033[1;31m[SECURITY ALERT]\033[0m Handshake integrity failure on FD %d. Invalid Master Key.\n", fd);
                return io_close(io, CONN_CLOSE_SILENT);
            }
            OPENSSL_cleanse(&h_pkt, sizeof(h_pkt));

            LOG_DEBUG("Handshake proof verified. Attempting to lock game_mutex...\n");
            /* Second: Now lock only to assign slot and key */
            pthread_mutex_lock(&game_mutex);
            LOG_DEBUG("game_mutex ACQUIRED for FD %d\n", fd);
            int slot = -1;
            for(int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == fd) { slot = i; break; }
            /* A free slot: an unnamed one first, so the handshake does not
             * sit on the slot an offline captain keeps for his return
             * (2026.10.31.39); a named offline slot only when no unnamed
             * one is left. */
            if (slot == -1) {
                for (int pass = 0; pass < 2 && slot == -1; pass++) {
                    for (int i = 0; i < MAX_CLIENTS; i++) {
                        if (players[i].socket != 0) continue;
                        if (pass == 0 && players[i].name[0] != '\0') continue;
                        slot = i;
                        players[i].socket = fd;
                        players[i].active = 0;
                        break;
                    }
                }
            }

            if (slot != -1) {
                memcpy(players[slot].session_key, hs_session, 32);
                g_cred_seq[slot] = 0;
                g_hs_fd[slot] = fd;
                g_hs_deadline[slot] = time(NULL) + HANDSHAKE_PENDING_TIMEOUT_S;
                LOG_DEBUG("Secure Session Key negotiated for Client FD %d (Slot %d)\n", fd, slot);
                /* ACK = int32 type + the v2 reply (eS, Ns, server proof,
                 * masked galaxy verification key), one write. */
                int ack_type = PKT_HANDSHAKE;
                uint8_t ack[sizeof(int32_t) + HANDSHAKE_V2_REPLY_LEN];
                memcpy(ack, &ack_type, sizeof(int32_t));
                memcpy(ack + sizeof(int32_t), hs_reply, HANDSHAKE_V2_REPLY_LEN);
                OPENSSL_cleanse(hs_session, sizeof(hs_session));
                if (io_write(io, ack, sizeof(ack)) != 0) {
                    pthread_mutex_unlock(&game_mutex);
                    return 1; /* async: session dropped (staging overflow / send error) */
                }
                LOG_DEBUG("Handshake ACK sent to FD %d (with galaxy verify key)\n", fd);
            } else {
                OPENSSL_cleanse(hs_session, sizeof(hs_session));
                fprintf(stderr, "\033[1;33m[WARNING]\033[0m Connection rejected: Server full (FD %d).\n", fd);
                /* SILENT: no slot was bound to this fd, so reap_player_fd()
                 * is a no-op and conn_close() needs no game_mutex re-entry
                 * it could not survive (we hold it here). */
                int closed = io_close(io, CONN_CLOSE_SILENT);
                pthread_mutex_unlock(&game_mutex);
                return closed;
            }
            pthread_mutex_unlock(&game_mutex);
            LOG_DEBUG("game_mutex RELEASED for FD %d\n", fd);
        } else {
            LOG_DEBUG("Handshake read failed or partial on FD %d\n", fd);
        }
        return 0;
    }

    /* For all other packets, we need the player index and we need the mutex */
    pthread_mutex_lock(&game_mutex);
    for (int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == fd && players[i].active) { p_idx = i; break; }
    pthread_mutex_unlock(&game_mutex);

    /* MAIN PACKET DISPATCHER */
    if (type == PKT_QUERY_KEY) {
        PacketQueryKey qk;
        if (pkt_read_body(io, ((char*)&qk) + sizeof(int), sizeof(PacketQueryKey) - sizeof(int))) {
            pthread_mutex_lock(&game_mutex);
            qk.found = 0;
            qk.type = PKT_QUERY_KEY;
            for(int j=0; j<MAX_CLIENTS; j++) {
                if (players[j].active && players[j].name[0] != '\0' && strcmp(players[j].name, qk.target_name) == 0) {
                    memcpy(qk.x25519_pubkey, players[j].x25519_pubkey, 32);
                    qk.found = 1;
                    LOG_DEBUG("Tactical Link: Found %s. Key starts with: %02X%02X%02X%02X\n",
                              qk.target_name, qk.x25519_pubkey[0], qk.x25519_pubkey[1],
                              qk.x25519_pubkey[2], qk.x25519_pubkey[3]);
                    break;
                }
            }
            pthread_mutex_unlock(&game_mutex);
            if (io_write(io, &qk, sizeof(PacketQueryKey)) != 0) return 1;
        }
        return 0;
    } else if (type == PKT_QUERY || type == PKT_LOGIN) {
        /* ... login logic ... */
        PacketLogin pkt;
        if (pkt_read_body(io, ((char*)&pkt) + sizeof(int), sizeof(PacketLogin) - sizeof(int))) {
            /* The credential fields are masked under this connection's
             * session key (hs_v2_cred_xor): find the slot the handshake
             * bound to this fd, take its key and the next sequence number.
             * No handshake, no session key: the packet cannot be read. */
            {
                uint8_t sk[32];
                uint32_t seq = 0;
                int hs_slot = -1;
                pthread_mutex_lock(&game_mutex);
                for (int j = 0; j < MAX_CLIENTS; j++) if (players[j].socket == fd) { hs_slot = j; break; }
                if (hs_slot != -1) {
                    memcpy(sk, players[hs_slot].session_key, 32);
                    seq = g_cred_seq[hs_slot]++;
                    if (g_hs_fd[hs_slot] == fd)
                        g_hs_deadline[hs_slot] = time(NULL) + HANDSHAKE_PENDING_TIMEOUT_S;
                }
                pthread_mutex_unlock(&game_mutex);
                bool unmasked = (hs_slot != -1) &&
                                hs_v2_cred_xor(sk, seq, (uint8_t *)&pkt + offsetof(PacketLogin, pass_hash));
                OPENSSL_cleanse(sk, sizeof(sk));
                if (!unmasked) {
                    OPENSSL_cleanse(&pkt, sizeof(pkt));
                    SG_WARNING(SG_CAT_SECURITY, "fd %d: %s before the handshake: dropped",
                               fd, type == PKT_QUERY ? "PKT_QUERY" : "PKT_LOGIN");
                    return io_close(io, CONN_CLOSE_SILENT);
                }
            }
            /* 3.4 — Reject names that could cause path traversal.
             * Must be checked before any file I/O uses pkt.name. */
            pkt.name[sizeof(pkt.name) - 1] = '\0';
            if (!sanitize_captain_name(pkt.name)) {
                /* Unsafe captain name (path injection attempt):
                   refuse the connection before any file I/O.
                   Note: game_mutex is NOT held here. */
                LOG_DEBUG("SECURITY: rejected login with unsafe name\n");
                fprintf(stderr, "\033[1;33m[SECURITY]\033[0m Rejected login: unsafe captain name on FD %d (connection closed).\n", fd);
                /* REAP: reclaim the slot reserved by the handshake so
                   that repeated rejects cannot exhaust the server (DoS). */
                return io_close(io, CONN_CLOSE_REAP);
            }
            if (type == PKT_QUERY) {
                pthread_mutex_lock(&game_mutex);
                int status = 1; /* 1:Success (Known), 2:New, 3:WrongPass, 4:Duplicate */
                uint8_t stored_salt[16] = {0}; /* Salted-scheme reply payload (status 5) */


                /* 1. Check for Duplicate Name (already active session) */
                for(int j=0; j<MAX_CLIENTS; j++) {
                    if (players[j].active && players[j].name[0] != '\0' && strcmp(players[j].name, pkt.name) == 0) {
                        status = 4;
                        break;
                    }
                }

                if (status != 4) {
                    /* 2. Check for existence and password.
                     * Identity scheme v2 (per-user salt):
                     *   identity.hash = HMAC-SHA256(master, "SPACEGL-ID-V2" || name || 0 || salt || 0 || password)
                     *   identity.salt = random 16-byte per-captain salt (issued at enrollment)
                     * A record without identity.salt (the unsalted scheme used
                     * before 2026) is refused (2026.10.31.25). */
                    IdentityRecord rec;
                    identity_load(pkt.name, &rec);
                    const char *auth_path = rec.auth_path;
                    const char *salt_path = rec.salt_path;
                    bool auth_ok = false;

                    /* A zero salt is the client's "I do not hold a salt for
                       this account" probe. The server answers with status 5
                       plus the salt to use (stored salt, or zeros, which tell
                       a fresh client to generate its own random salt). */
                    bool client_salt_zero = true;
                    for(int b=0; b<16; b++) if (pkt.salt[b] != 0) { client_salt_zero = false; break; }

                    if (rec.exists) {
                        if (rec.have_salt) {
                            /* Salted (v2) account */
                            memcpy(stored_salt, rec.salt, 16);
                            if (client_salt_zero) {
                                status = PKT_ID_STATUS_SALT_REQUIRED; /* reply carries the stored salt */
                            } else if (CRYPTO_memcmp(pkt.salt, rec.salt, 16) != 0) {
                                status = 3; /* Wrong salt: verification fails */
                            } else {
                                auth_ok = identity_hash_ok(&rec, &pkt);
                            }
                        } else {
                            /* Record without a salt (pre-2026 unsalted scheme):
                             * no longer accepted. Refused like a wrong password;
                             * the operator removes captains/<name> so that the
                             * captain can enroll again. */
                            SG_WARNING(SG_CAT_SECURITY,
                                       "captain '%s': identity record without salt (old scheme) refused; "
                                       "remove captains/%s to enroll again", pkt.name, pkt.name);
                            status = 3;
                        }

                        if (status != 3 && status != PKT_ID_STATUS_SALT_REQUIRED) {
                            if (!auth_ok) {
                                status = 3; /* Wrong Password */
                            } else {
                                /* Password correct: check if the commander is in the current galaxy persistent state */
                                bool found_in_galaxy = false;
                                for(int j=0; j<MAX_CLIENTS; j++) {
                                    if (players[j].name[0] != '\0' && strcmp(players[j].name, pkt.name) == 0) {
                                        found_in_galaxy = true;
                                        break;
                                    }
                                }
                                if (found_in_galaxy) status = 1; /* Success (Known) */
                                else status = 2; /* New Recruit (Identity exists, but Galaxy was reset) */
                            }
                        }
                    } else if (client_salt_zero) {
                        /* New Captain, salt probe: do NOT create the account yet.
                           Reply with status 5 + zeros; the client picks a random
                           salt and re-queries, which creates the record. */
                        status = PKT_ID_STATUS_SALT_REQUIRED;
                    } else {
                        /* New Captain (salt supplied): create directory and
                           save salted hash + salt */
                        char rel_dir[160];
                        snprintf(rel_dir, sizeof(rel_dir), "captains/%s", pkt.name);
                        char dir_path[1024];
                        server_data_path(dir_path, sizeof(dir_path), rel_dir);
                        char captains_path[1024];
                        server_data_path(captains_path, sizeof(captains_path), "captains");
                        mkdir(captains_path, 0700);
                        mkdir(dir_path, 0700);
                        FILE *fa = fopen(auth_path, "wb");
                        if (fa) {
                            fwrite(pkt.pass_hash, 1, 32, fa);
                            if (fchmod(fileno(fa), 0600) != 0) { /* best effort */ }
                            fclose(fa);
                        }
                        FILE *fsm = fopen(salt_path, "wb");
                        if (fsm) {
                            fwrite(pkt.salt, 1, 16, fsm);
                            if (fchmod(fileno(fsm), 0600) != 0) { /* best effort */ }
                            fclose(fsm);
                        }
                        status = 2; /* New Captain */
                    }
                }

                pthread_mutex_unlock(&game_mutex);
                LOG_DEBUG("Security Check for '%s': Status %d\n", pkt.name, status);
                /* Reply = int32 status (+ uint8[16] salt when
                 * status == PKT_ID_STATUS_SALT_REQUIRED): one io_write,
                 * the identical byte sequence of the legacy two-write
                 * reply. */
                uint8_t reply[sizeof(int32_t) + 16];
                memcpy(reply, &status, sizeof(int32_t));
                size_t reply_len = sizeof(int32_t);
                if (status == PKT_ID_STATUS_SALT_REQUIRED) {
                    /* Disclose the stored salt so the client can recompute the
                     * salted identity hash (one extra round-trip; the salt is
                     * public-parameters material, like a database salt). */
                    memcpy(reply + sizeof(int32_t), stored_salt, 16);
                    reply_len = sizeof(int32_t) + 16;
                }
                if (io_write(io, reply, reply_len) != 0) return 1;
            } else {
                /* PKT_LOGIN: the identity must be proved here too. PKT_QUERY
                 * verifies the access code, but nothing ties a later
                 * PKT_LOGIN to it, and the reconnect path sends PKT_LOGIN
                 * alone: without this check a client holding the server key
                 * entered as any captain with any hash. The record must
                 * exist (created by PKT_QUERY for a new captain) and the
                 * packet must carry the matching hash. Checked before
                 * game_mutex: a refusal reaps the handshake slot, and
                 * reap_player_fd() takes game_mutex itself. */
                {
                    IdentityRecord rec;
                    identity_load(pkt.name, &rec);
                    bool proved = identity_hash_ok(&rec, &pkt);
                    OPENSSL_cleanse(rec.hash, sizeof(rec.hash));
                    if (!proved) {
                        SG_WARNING(SG_CAT_SECURITY,
                                   "login refused for '%s' on fd %d: %s",
                                   pkt.name, fd,
                                   rec.exists ? "identity not proved" : "no identity record (PKT_QUERY first)");
                        fprintf(stderr, "\033[1;33m[SECURITY]\033[0m Rejected login for '%s' on FD %d: identity not proved (connection closed).\n",
                                pkt.name, fd);
                        OPENSSL_cleanse(&pkt, sizeof(pkt));
                        return io_close(io, CONN_CLOSE_REAP);
                    }
                }
                pthread_mutex_lock(&game_mutex);
                int slot = -1;
                /* 1. Try to find a player with the same name (persistence) */
                for(int j=0; j<MAX_CLIENTS; j++) {
                    if (players[j].name[0] != '\0' && strcmp(players[j].name, pkt.name) == 0) {
                        slot = j;
                        break;
                    }
                }

                /* 2. If not found, a TRULY empty slot: no name and no other
                 * connection on it. An unnamed slot whose socket is another
                 * fd is that connection's pending handshake: taking it
                 * detached the other captain in the middle of his login
                 * (two new captains logging in at the same time,
                 * 2026.10.31.39). This connection's own handshake slot is
                 * the first choice. */
                if (slot == -1) {
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (players[j].socket == fd && players[j].name[0] == '\0') {
                            slot = j;
                            break;
                        }
                    }
                }
                if (slot == -1) {
                    for(int j=0; j<MAX_CLIENTS; j++) {
                        if (players[j].name[0] == '\0' && players[j].socket == 0) {
                            slot = j;
                            break;
                        }
                    }
                }

                /* 3. Fallback: if server is full of named players, reuse an inactive slot (socket == 0,
                   or this connection's own handshake slot) but we MUST clear it first so it's treated
                   as a new player. */
                if (slot == -1) {
                    for(int j=0; j<MAX_CLIENTS; j++) {
                        if (players[j].socket == 0 || players[j].socket == fd) {
                            slot = j;
                            memset(players[slot].name, 0, 64); /* Force is_new = true */
                            break;
                        }
                    }
                }

                if (slot != -1) {
                    /* Handle Session Key transfer from the temporary handshake slot if needed */
                    int handshake_slot = -1;
                    for(int j=0; j<MAX_CLIENTS; j++) if (players[j].socket == fd) { handshake_slot = j; break; }

                    if (handshake_slot != -1 && handshake_slot != slot) {
                        memcpy(players[slot].session_key, players[handshake_slot].session_key, 32);
                        g_cred_seq[slot] = g_cred_seq[handshake_slot];
                        /* The temporary handshake slot is released, also when
                         * it is the slot of an offline captain (the name
                         * stays, for his return): left bound to this fd, two
                         * slots shared one socket and the departure reaped
                         * only the first (2026.10.31.39). */
                        players[handshake_slot].socket = 0;
                    }

                    /* The captain is still bound to another connection (a
                     * reconnect after a network drop the server has not
                     * noticed yet, or a second terminal). The identity is
                     * proved above, so the newest connection takes the
                     * captain over: the old socket is shut down and
                     * detached (its epoll/pump path then closes it and
                     * finds no slot to reap), and the generation bump below
                     * cancels a galaxy sync still in flight for it. */
                    if (players[slot].socket != 0 && players[slot].socket != fd) {
                        SG_NOTICE(SG_CAT_CLIENT, "captain %s: session taken over by fd %d (old fd %d detached)",
                                  pkt.name, fd, players[slot].socket);
                        player_socket_detach(slot);
                        players[slot].active = 0;
                    }

                    players[slot].socket = fd;
                    g_hs_fd[slot] = 0;               /* logged in: not a pending handshake */
                    if (handshake_slot != -1) g_hs_fd[handshake_slot] = 0;
                    int is_new = (players[slot].name[0] == '\0');
                    players[slot].active = 0; /* Block updates during sync */

                    /* Always update keys on login, but only update faction/class for new players */
                    memcpy(players[slot].x25519_pubkey, pkt.x25519_pubkey, 32);
                    packets_bind_signing_key(slot, pkt.ed25519_pubkey);
                    if (is_new) {
                        players[slot].faction = pkt.faction;
                        players[slot].ship_class = pkt.ship_class;
                        strcpy(players[slot].name, pkt.name);

                        /* Notify the fleet of the new X25519 public key */
                        char key_info[256];
                        SG_SPRINTF(key_info, "[IDENTITY] Public Frequency for Captain %s: ", pkt.name);
                        for(int k=0; k<8; k++) SG_APPENDF(key_info, "%02X", pkt.x25519_pubkey[k]);
                        SG_STRCAT(key_info, "... [UPLINK ACTIVE]");
                        send_server_msg(-1, "COMPUTER", key_info);
                        players[slot].state.energy = MAX_ENERGY_CAPACITY;
                        players[slot].state.torpedoes = MAX_TORPEDO_CAPACITY;
                        int crew = (MAX_CREW_EXPLORER / 5);
                        switch(pkt.ship_class) {
                            case SHIP_CLASS_EXPLORER:    crew = MAX_CREW_EXPLORER; break;
                            case SHIP_CLASS_FLAGSHIP:    crew = 850; break;
                            case SHIP_CLASS_LEGACY:      crew = 430; break;
                            case SHIP_CLASS_HEAVY_CRUISER: crew = 750; break;
                            case SHIP_CLASS_ESCORT:      crew = (MAX_CREW_EXPLORER / 20); break;
                            case SHIP_CLASS_SCIENCE:     crew = (MAX_CREW_EXPLORER / 7); break;
                            case SHIP_CLASS_RESEARCH:    crew = 80; break;
                            case SHIP_CLASS_SCOUT:       crew = (MAX_CREW_EXPLORER / 33); break;
                            case SHIP_CLASS_MULTI_ENGINE: crew = (MAX_CREW_EXPLORER / 2); break;
                            case SHIP_CLASS_CARRIER:     crew = 1200; break;
                            case SHIP_CLASS_TACTICAL:    crew = 800; break;
                            case SHIP_CLASS_DIPLOMATIC:  crew = (MAX_CREW_EXPLORER / 3); break;
                            case SHIP_CLASS_FRIGATE:     crew = 250; break;
                            case SHIP_CLASS_SENTINEL:    crew = 950; break;
                            default: crew = (MAX_CREW_EXPLORER / 5); break;
                        }
                        players[slot].state.crew_count = crew;
                        /* a quadrant whose centre is clear (targets.c): a new
                         * ship is never spawned inside a star */
                        spawn_quadrant_pick(&players[slot].state.q1, &players[slot].state.q2,
                                            &players[slot].state.q3);
                        players[slot].state.s1 = (QUADRANT_SIZE / 2.0);
                        players[slot].state.s2 = (QUADRANT_SIZE / 2.0);
                        players[slot].state.s3 = (QUADRANT_SIZE / 2.0);

                        /* Initialize Absolute Galactic Coordinates */
                        players[slot].gx = (players[slot].state.q1 - 1) * QUADRANT_SIZE + players[slot].state.s1;
                        players[slot].gy = (players[slot].state.q2 - 1) * QUADRANT_SIZE + players[slot].state.s2;
                        players[slot].gz = (players[slot].state.q3 - 1) * QUADRANT_SIZE + players[slot].state.s3;

                        players[slot].state.inventory[1] = 1000000ULL; /* Initial Aetherium for jumps */

                        /* Default Balanced Power Distribution */
                        players[slot].state.power_dist[0] = 0.333; /* Engines */
                        players[slot].state.power_dist[1] = 0.334; /* Shields */
                        players[slot].state.power_dist[2] = 0.333; /* Weapons */

                        for (int s = 0; s < 6; s++) {
                            players[slot].state.shields[s] = SHIELD_MAX_STRENGTH;
                            players[slot].state.target_shields[s] = SHIELD_MAX_STRENGTH;
                        }
                        players[slot].state.shield_change_timer = 0;
                        players[slot].state.shield_change_rate = 0.0f;

                        players[slot].state.hull_integrity = (float)YIELD_HARVEST_MAX;
                        for (int s = 0; s < MAX_SYSTEMS; s++) {
                            players[slot].state.system_health[s] = (float)YIELD_HARVEST_MAX;
                        }
                        players[slot].state.life_support = (float)YIELD_HARVEST_MAX;
                        players[slot].state.ion_beam_charge = (float)YIELD_HARVEST_MAX;
                        memset(players[slot].state.probes, 0, sizeof(players[slot].state.probes));
                    } else {
                        /* RETURNING CAPTAIN: sync name to the game state for visual consistency */
                        strcpy(players[slot].state.captain_name, players[slot].name);

                        /* Default Power if zero (old accounts or corruption) */
                        double p_total = players[slot].state.power_dist[0] + players[slot].state.power_dist[1] + players[slot].state.power_dist[2];
                        if (p_total < 0.01) {
                            players[slot].state.power_dist[0] = 0.333;
                            players[slot].state.power_dist[1] = 0.334;
                            players[slot].state.power_dist[2] = 0.333;
                        }
                    }

                    /* WELCOME PACKAGE: Ensure all captains (new or returning) have at least 10 Aetherium for Jumps */
                    if (players[slot].state.inventory[1] < COST_ACTION_LOW) {
                        players[slot].state.inventory[1] = COST_ACTION_LOW;
                    }

                    /* SESSION INITIALIZATION: Reset transient event flags and force full sync */
                    players[slot].renegade_timer = 0;
                    players[slot].radio_lock_target = 0;
                    players[slot].jump_type = 2;

                    /* Derive Personal Algorithm keys for this Captain */
                    derive_algo_keys(MASTER_SESSION_KEY, players[slot].name, players[slot].algo_keys);

                    SG_SUCCESS(SG_CAT_CLIENT, "captain %s authenticated (slot %d, %s, quadrant [%d,%d,%d])",
                               players[slot].name, slot,
                               is_new ? "new identity" : "returning",
                               players[slot].state.q1, players[slot].state.q2,
                               players[slot].state.q3);
                    players[slot].state.beam_count = 0;
                    players[slot].state.event_count = 0;
                    players[slot].torp_active = false;
                    players[slot].full_update_timer = (5 * GAME_TICK_RATE + 1); /* Force UPD_FULL on next network pulse */
                    memset(&players[slot].last_sent_state, 0, sizeof(PacketUpdate));

                    /* FORCE COORDINATE SYNC: Ensure HUD and Viewer align immediately */
                    players[slot].state.q1 = get_q_from_g(players[slot].gx);
                    players[slot].state.q2 = get_q_from_g(players[slot].gy);
                    players[slot].state.q3 = get_q_from_g(players[slot].gz);
                    players[slot].state.s1 = players[slot].gx - (players[slot].state.q1 - 1) * QUADRANT_SIZE;
                    players[slot].state.s2 = players[slot].gy - (players[slot].state.q2 - 1) * QUADRANT_SIZE;
                    players[slot].state.s3 = players[slot].gz - (players[slot].state.q3 - 1) * QUADRANT_SIZE;

                    players[slot].crypto_algo = CRYPTO_NONE;
                    /* Delegate the giant Galaxy Master transmission to the Thread Pool */
                    SyncTask *stask = malloc(sizeof(SyncTask));
                    if (stask) {
                        stask->slot = slot;
                        stask->fd = fd;
                        players[slot].generation++;
                        stask->generation = players[slot].generation;
                        stask->is_new = is_new;
                        /* Enqueue under game_mutex so the generation bump is
                           atomic with the slot state. */
                        bool delegated = (g_pool &&
                                          threadpool_add_task(g_pool, sync_client_task, stask) == 0);
                        pthread_mutex_unlock(&game_mutex);
                        if (!delegated) {
                            /* Fallback if the pool is unavailable or the
                               enqueue fails: sync synchronously, outside
                               game_mutex (sync_client_task re-locks it). */
                            sync_client_task(stask);
                        }
                    } else {
                        pthread_mutex_unlock(&game_mutex);
                    }
                } else {
                    pthread_mutex_unlock(&game_mutex);
                }
            }
        }
        return 0;
    } else if (type == PKT_COMMAND || type == PKT_MESSAGE) {
        /* NOTE: the payload MUST always be consumed, even when the
           player is not active yet (sync in flight), otherwise the
           socket stream desynchronizes. Commands/messages arriving
           in that narrow window are dropped without desync. */
        if (type == PKT_COMMAND) {
            PacketCommand pkt;
            if (pkt_read_body(io, ((char*)&pkt) + sizeof(int), sizeof(PacketCommand) - sizeof(int))) {
                /* cmd[] is network-fed and the wire packet fills it to
                 * the brim: force-terminate, like the login path does
                 * with pkt.name, before process_command() strlen()s it. */
                pkt.cmd[sizeof(pkt.cmd) - 1] = '\0';
                if (p_idx != -1 && process_command(p_idx, pkt.cmd)) {
                    /* Profile was deleted (zztop), drop connection and
                       reap the slot so repeated zztops cannot exhaust
                       the server (DoS). game_mutex is NOT held here. */
                    return io_close(io, CONN_CLOSE_REAP);
                }
            }
            return 0;
        } else { /* PKT_MESSAGE */
            PacketMessage *pkt = malloc(sizeof(PacketMessage));
            if (pkt && pkt_read_body(io, ((char*)pkt) + sizeof(int), offsetof(PacketMessage, text) - sizeof(int))) {
                /* SECURITY: validate the network-supplied length BEFORE
                   touching the payload, with the same canonical check
                   as the client (packet_message_length_valid, network.h).
                   The value is attacker-controlled: outside [0, 65535]
                   it must never reach broadcast_message(), where it is
                   converted to size_t (c_len, EVP_DecryptUpdate, relay
                   pkt_size) and becomes a multi-GB copy/read (remote
                   DoS). Drop the connection, as the client does. In
                   async mode the state machine enforces the same bound
                   before staging the payload (conn_pump); this re-check
                   keeps the shared path safe on both. game_mutex is NOT
                   held here. */
                if (!packet_message_length_valid(pkt->length)) {
                    int bad_len = pkt->length;
                    free(pkt);
                    fprintf(stderr, "\033[1;31m[SECURITY ALERT]\033[0m Dropped FD %d: PKT_MESSAGE length out of range (%d).\n", fd, bad_len);
                    LOG_DEBUG("Connection dropped: PKT_MESSAGE length out of range (%d), FD %d\n", bad_len, fd);
                    return io_close(io, CONN_CLOSE_REAP);
                }
                if (pkt->length > 0) pkt_read_body(io, pkt->text, (size_t)pkt->length);
                else pkt->text[0] = '\0';
                pkt->type = type;

                /* The sender is the authenticated captain of this
                 * connection, not what the packet says: "from" and
                 * "faction" are client-written fields, and broadcast_message()
                 * picks the sender (its keys, its radio lock, the system
                 * sender exemptions) and the faction scope from them. A
                 * client could speak as another captain or as COMPUTER /
                 * STARBASE, or reach another faction's channel. Both fields
                 * are overwritten from the slot, under game_mutex, and the
                 * slot must still belong to this fd. */
                bool sender_ok = false;
                if (p_idx != -1) {
                    pthread_mutex_lock(&game_mutex);
                    if (players[p_idx].active && players[p_idx].socket == fd) {
                        memset(pkt->from, 0, sizeof(pkt->from));
                        strncpy(pkt->from, players[p_idx].name, sizeof(pkt->from) - 1);
                        pkt->faction = players[p_idx].faction;
                        sender_ok = true;
                        /* A signature counts only if it was made with the
                         * signing key of this captain's authenticated login:
                         * receivers verify against sender_pubkey, which the
                         * client writes. Otherwise the signature is removed
                         * and the message is relayed as unsigned. */
                        if (pkt->has_signature &&
                            !(g_sign_set[p_idx] &&
                              CRYPTO_memcmp(pkt->sender_pubkey, g_sign_pub[p_idx], 32) == 0)) {
                            pkt->has_signature = 0;
                            memset(pkt->signature, 0, sizeof(pkt->signature));
                            memset(pkt->sender_pubkey, 0, sizeof(pkt->sender_pubkey));
                            SG_WARNING(SG_CAT_SECURITY,
                                       "captain %s: radio signature with a key not bound to the login: removed",
                                       players[p_idx].name);
                        }
                    }
                    pthread_mutex_unlock(&game_mutex);
                }
                if (sender_ok) {
                    if (g_pool) threadpool_add_task(g_pool, broadcast_task, pkt);
                    else { broadcast_message(pkt); free(pkt); }
                } else {
                    free(pkt); /* player not active (yet): drop cleanly */
                }
            } else if (pkt) free(pkt);
            return 0;
        }
    } else {
        /* Unknown/unsupported packet type: close the connection
           (we cannot resynchronize the stream). */
        return io_close(io, CONN_CLOSE_SILENT);
    }
}

int packets_conn_dispatch(int epoll_fd, int fd, Conn *c, int32_t type, const uint8_t *rx, size_t len) {
    PktIO io;
    io.epoll_fd = epoll_fd;
    io.fd = fd;
    io.conn = c;
    io.rx = rx;
    io.rx_len = len;
    return dispatch_packet(&io, type);
}
