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
#include "server_internal.h"
#include "conn.h"
#include "packets.h"

extern threadpool_t *g_pool;

int sanitize_captain_name(const char *name) {
    if (!name || name[0] == '\0') {
        return 0;
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

int dispatch_packet(PktIO *io, int32_t type) {
    int fd = io->fd;
    int p_idx = -1;

    if (type == PKT_HANDSHAKE) {
        LOG_DEBUG("Handshake request received from FD %d\n", fd);
        PacketHandshake h_pkt;
        h_pkt.type = type;
        size_t h_body = sizeof(PacketHandshake) - sizeof(int);
        if (pkt_read_body(io, ((char*)&h_pkt) + sizeof(int), h_body)) {
            LOG_DEBUG("Handshake data read successfully (%zu bytes)\n", h_body);
            /* First: Security verification WITHOUT locking game_mutex */
            uint8_t sig[32];
            for(int k=0; k<32; k++) sig[k] = h_pkt.pubkey[32+k] ^ MASTER_SESSION_KEY[k];

            if (memcmp(sig, HANDSHAKE_MAGIC_STRING, 32) != 0) {
                fprintf(stderr, "\033[1;31m[SECURITY ALERT]\033[0m Handshake integrity failure on FD %d. Invalid Master Key.\n", fd);
                return io_close(io, CONN_CLOSE_SILENT);
            }

            LOG_DEBUG("Handshake signature verified. Attempting to lock game_mutex...\n");
            /* Second: Now lock only to assign slot and key */
            pthread_mutex_lock(&game_mutex);
            LOG_DEBUG("game_mutex ACQUIRED for FD %d\n", fd);
            int slot = -1;
            for(int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == fd) { slot = i; break; }
            if (slot == -1) {
                for(int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == 0) {
                    slot = i;
                    players[i].socket = fd;
                    players[i].active = 0;
                    break;
                }
            }

            if (slot != -1) {
                for(int k=0; k<32; k++) {
                    players[slot].session_key[k] = h_pkt.pubkey[k] ^ MASTER_SESSION_KEY[k];
                }
                LOG_DEBUG("Secure Session Key negotiated for Client FD %d (Slot %d)\n", fd, slot);
                /* ACK = int32 type + 32-byte galaxy-verify key, one reply:
                 * on the wire it is the identical byte sequence of the
                 * legacy two-write form (int32, then uint8[32]). */
                int ack_type = PKT_HANDSHAKE;
                uint8_t ack[sizeof(int32_t) + 32];
                memcpy(ack, &ack_type, sizeof(int32_t));
                /* Deliver the stable galaxy verification key, bound to this
                 * session: the client XORs it back with the session key it
                 * generated. This lets the client verify the galaxy state
                 * HMAC-SHA256 signature even after its local key rotated. */
                for(int k=0; k<32; k++) {
                    ack[sizeof(int32_t) + k] = GALAXY_VERIFY_KEY[k] ^ players[slot].session_key[k];
                }
                if (io_write(io, ack, sizeof(ack)) != 0) {
                    pthread_mutex_unlock(&game_mutex);
                    return 1; /* async: session dropped (staging overflow / send error) */
                }
                LOG_DEBUG("Handshake ACK sent to FD %d (with galaxy verify key)\n", fd);
            } else {
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
                     * Legacy unsalted identity.hash files remain verifiable and are
                     * transparently migrated to the salted scheme on first successful
                     * login (the client proved the password and supplied a fresh salt). */
                    char rel_auth[192];
                    char rel_salt[192];
                    char auth_path[1024];
                    char salt_path[1024];
                    snprintf(rel_auth, sizeof(rel_auth), "captains/%s/identity.hash", pkt.name);
                    snprintf(rel_salt, sizeof(rel_salt), "captains/%s/identity.salt", pkt.name);
                    server_data_path(auth_path, sizeof(auth_path), rel_auth);
                    server_data_path(salt_path, sizeof(salt_path), rel_salt);

                    bool have_stored_salt = false;
                    bool auth_ok = false;

                    /* A zero salt is the client's "I do not hold a salt for
                       this account" probe. The server answers with status 5
                       plus the salt to use (stored salt, or zeros, which tell
                       a fresh client to generate its own random salt). */
                    bool client_salt_zero = true;
                    for(int b=0; b<16; b++) if (pkt.salt[b] != 0) { client_salt_zero = false; break; }

                    FILE *fa = fopen(auth_path, "rb");
                    if (fa) {
                        uint8_t stored_hash[32] = {0};
                        bool have_stored_hash = (fread(stored_hash, 1, 32, fa) == 32);
                        fclose(fa);

                        FILE *fs = fopen(salt_path, "rb");
                        if (fs) {
                            if (fread(stored_salt, 1, 16, fs) == 16) have_stored_salt = true;
                            fclose(fs);
                        }

                        if (have_stored_salt) {
                            /* Salted (v2) account */
                            if (client_salt_zero) {
                                status = PKT_ID_STATUS_SALT_REQUIRED; /* reply carries the stored salt */
                            } else if (memcmp(pkt.salt, stored_salt, 16) != 0) {
                                status = 3; /* Wrong salt: verification fails */
                            } else {
                                auth_ok = have_stored_hash && (memcmp(stored_hash, pkt.pass_hash, 32) == 0);
                            }
                        } else {
                            /* Legacy account (no salt on disk) */
                            auth_ok = have_stored_hash &&
                                (memcmp(stored_hash, pkt.pass_hash_legacy, 32) == 0 ||
                                 memcmp(stored_hash, pkt.pass_hash, 32) == 0);
                            if (auth_ok && client_salt_zero) {
                                /* Password proved, but no salt supplied yet: ask for one
                                   so the record can be migrated to the salted scheme. */
                                status = PKT_ID_STATUS_SALT_REQUIRED; /* reply carries zeros */
                                auth_ok = false;
                            } else if (auth_ok) {
                                /* Transparent migration of the stored record */
                                FILE *fm = fopen(auth_path, "wb");
                                if (fm) {
                                    fwrite(pkt.pass_hash, 1, 32, fm);
                                    if (fchmod(fileno(fm), 0600) != 0) { /* best effort */ }
                                    fclose(fm);
                                }
                                FILE *fsm = fopen(salt_path, "wb");
                                if (fsm) {
                                    fwrite(pkt.salt, 1, 16, fsm);
                                    if (fchmod(fileno(fsm), 0600) != 0) { /* best effort */ }
                                    fclose(fsm);
                                }
                            }
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
                        fa = fopen(auth_path, "wb");
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
                /* PKT_LOGIN: Re-using the same packet read for login */
                pthread_mutex_lock(&game_mutex);
                int slot = -1;
                /* 1. Try to find a player with the same name (persistence) */
                for(int j=0; j<MAX_CLIENTS; j++) {
                    if (players[j].name[0] != '\0' && strcmp(players[j].name, pkt.name) == 0) {
                        slot = j;
                        break;
                    }
                }

                /* 2. If not found, find a TRULY empty slot (no name) */
                if (slot == -1) {
                    for(int j=0; j<MAX_CLIENTS; j++) {
                        if (players[j].name[0] == '\0') {
                            slot = j;
                            break;
                        }
                    }
                }

                /* 3. Fallback: if server is full of named players, reuse an inactive slot (socket == 0)
                   but we MUST clear it first so it's treated as a new player. */
                if (slot == -1) {
                    for(int j=0; j<MAX_CLIENTS; j++) {
                        if (players[j].socket == 0) {
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
                        /* If the temporary slot was just for handshake, clear it */
                        if (players[handshake_slot].name[0] == '\0') {
                            players[handshake_slot].socket = 0;
                        }
                    }

                    players[slot].socket = fd;
                    int is_new = (players[slot].name[0] == '\0');
                    players[slot].active = 0; /* Block updates during sync */

                    /* Always update keys on login, but only update faction/class for new players */
                    memcpy(players[slot].x25519_pubkey, pkt.x25519_pubkey, 32);
                    if (is_new) {
                        players[slot].faction = pkt.faction;
                        players[slot].ship_class = pkt.ship_class;
                        strcpy(players[slot].name, pkt.name);

                        /* Notify the fleet of the new X25519 public key */
                        char key_info[256];
                        sprintf(key_info, "[IDENTITY] Public Frequency for Captain %s: ", pkt.name);
                        for(int k=0; k<8; k++) sprintf(key_info + strlen(key_info), "%02X", pkt.x25519_pubkey[k]);
                        strcat(key_info, "... [UPLINK ACTIVE]");
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
                        players[slot].state.q1 = rand()%GALAXY_SIZE + 1;
                        players[slot].state.q2 = rand()%GALAXY_SIZE + 1;
                        players[slot].state.q3 = rand()%GALAXY_SIZE + 1;
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
                        if (threadpool_add_task(g_pool, sync_client_task, stask) != 0) {
                            /* Fallback if pool fails: sync synchronously */
                            sync_client_task(stask);
                        }
                    }
                    pthread_mutex_unlock(&game_mutex);
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
                if (pkt->length > 0) pkt_read_body(io, pkt->text, pkt->length);
                else pkt->text[0] = '\0';
                pkt->type = type;

                if (p_idx != -1) {
                    if (g_pool) threadpool_add_task(g_pool, broadcast_task, pkt);
                    else { broadcast_message(pkt); free(pkt); }
                } else {
                    free(pkt); /* player not active yet: drop cleanly */
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
