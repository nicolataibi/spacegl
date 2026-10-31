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
 * License: GPL-3.0-or-later
 */

#ifndef NETWORK_H
#define NETWORK_H

#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <openssl/rand.h>
#include "game_config.h"
#include "game_state.h"

#pragma pack(push, 1)

#define DEFAULT_PORT 5000
#define MAX_CLIENTS GAME_MAX_PLAYERS
#define PKT_LOGIN 1
#define PKT_COMMAND 2
#define PKT_UPDATE 3
#define PKT_MESSAGE 4
#define PKT_QUERY 5
#define PKT_HANDSHAKE 6
#define PKT_UPDATE_DELTA 7
#define PKT_QUERY_KEY 8
#define PKT_LRS_UPDATE 9

typedef struct {
    int32_t type;
    int32_t q1, q2, q3;
    int32_t z_offset;
    int32_t object_count;
    NetObject objects[MAX_NET_OBJECTS];
} PacketLRSUpdate;

/* Update Mask Bits for Delta Compression */
#define UPD_TRANSFORM (1ULL << 0)
#define UPD_VITALS    (1ULL << 1)
#define UPD_SHIELDS   (1ULL << 2)
#define UPD_SYSTEMS   (1ULL << 3)
#define UPD_INTERNAL  (1ULL << 4)
#define UPD_COMBAT    (1ULL << 5)
#define UPD_FLAGS     (1ULL << 6)
#define UPD_EFFECTS   (1ULL << 7)
#define UPD_PROBES    (1ULL << 8)
#define UPD_OBJECTS   (1ULL << 9)
#define UPD_MAP       (1ULL << 10)
#define UPD_CONTINUE  (1ULL << 11)
#define UPD_KINEMATICS (1ULL << 12)
#define UPD_FULL      (0xFFFFFFFFFFFFFFFFULL)

/* Delta Compression Blocks */
typedef struct {
    int32_t q1, q2, q3;
    double s1, s2, s3;
    double van_h, van_m, van_r;
    double eta;
} UpdateBlockTransform;

typedef struct {
    uint64_t energy;
    int32_t torpedoes;
    uint64_t cargo_energy;
    int32_t cargo_torpedoes;
    int32_t crew_count;
    int32_t prison_unit;
    int32_t composite_plating;
    double hull_integrity;
} UpdateBlockVitals;

typedef struct {
    int32_t shields[6];
} UpdateBlockShields;

typedef struct {
    double system_health[10];
} UpdateBlockSystems;

typedef struct {
    int32_t inventory[10];
    double power_dist[3];
    double life_support;
    int32_t anti_matter_count;
} UpdateBlockInternal;

typedef struct {
    int32_t lock_target;
    int32_t tube_state;
    int32_t tube_load_timers[4];
    int32_t tube_torpedo_etas[4];
    int32_t current_tube;
    double ion_beam_charge;
} UpdateBlockCombat;

typedef struct {
    uint8_t is_cloaked;
    uint8_t is_docked;
    uint8_t red_alert;
    uint8_t is_jammed;
    uint8_t nav_state;
    uint8_t show_axes;
    uint8_t show_grid;
    uint8_t show_bridge;
    uint8_t show_map;
    uint8_t map_filter;
    uint8_t force_shutdown;
    uint8_t encryption_algo;
    uint32_t encryption_flags;
    int32_t radio_lock_target;
} UpdateBlockFlags;

typedef struct {
    NetPoint supernova_pos; 
    int32_t supernova_q[3];
    NetPoint torps[4];
    NetPoint wormhole;
    int32_t event_count;
    NetEvent events[MAX_NET_EVENTS];
    int32_t torpedo_count;
    NetVisibleTorpedo visible_torpedoes[MAX_VISIBLE_TORPEDOES];
} UpdateBlockEffects;

typedef struct {
    NetProbe probes[3];
} UpdateBlockProbes;

typedef struct {
    int32_t beam_count;
    NetBeam beams[MAX_NET_BEAMS];
} UpdateBlockBeams;

typedef struct {
    int32_t object_count;
    NetObject objects[MAX_NET_OBJECTS];
} UpdateBlockObjects;

typedef struct {
    int32_t id;
    float net_x, net_y, net_z;
    float vx, vy, vz;
} NetKinematics;

typedef struct {
    int32_t object_count;
    NetKinematics items[MAX_NET_OBJECTS];
} UpdateBlockKinematics;

typedef struct {
    int64_t map_update_val;
    int32_t map_update_q[3];
    int64_t map_update_val2;
    int32_t map_update_q2[3];
} UpdateBlockMap;

typedef struct {
    int32_t type;
    int64_t frame_id;
    uint64_t update_mask;
    uint8_t data[]; /* Variable length payload */
} PacketUpdateDelta;

/* Magic Signature for Key Verification (32 bytes) */
#define HANDSHAKE_MAGIC_STRING "SPACEGL-KEY-VERIFICATION-SIG-32B"

/*
 * Handshake protocol (v2):
 *   C -> S: PacketHandshake (random session key || HANDSHAKE_MAGIC_STRING, XOR master key)
 *   S -> C: int32 type (PKT_HANDSHAKE)  [ACK]
 *           uint8[32]  galaxy verification key XOR session key
 *
 * The galaxy verification key is a STABLE key derived from the master key
 * (HMAC-SHA256(master, "SPACEGL-GALAXY-VERIFY-V1")). It allows the client to
 * verify the HMAC-SHA256 integrity signature of the galaxy state even after
 * the local deep_space_key has been rotated to the session key.
 */

/*
 * Identity (password) verification protocol (v2):
 *   - pass_hash        = HMAC-SHA256(master, "SPACEGL-ID-V2" || name || 0 || salt || 0 || password)
 *   - pass_hash_legacy = HMAC-SHA256(master, password)   [legacy accounts only]
 *   - Salt exchange: the client starts with an all-zero salt ("I hold no salt
 *     for this account"). The server replies with status
 *     PKT_ID_STATUS_SALT_REQUIRED plus uint8[16]: the stored salt for existing
 *     salted accounts, or zeros for new/legacy accounts (the client then
 *     generates its own random salt). The client retries with the resulting
 *     salt; for new accounts the retry creates the record (identity.hash +
 *     identity.salt), and for legacy accounts it transparently migrates the
 *     stored record to the salted scheme.
 *   - S -> C reply: int32 status, followed by uint8[16] salt when
 *     status == PKT_ID_STATUS_SALT_REQUIRED.
 *   - PKT_LOGIN carries the same name, salt and hashes and is verified
 *     again against the stored record (2026.10.31.04): no record, a wrong
 *     salt or a wrong hash closes the connection. The reconnect path sends
 *     PKT_LOGIN alone, so this check is the one that guards it. A verified
 *     PKT_LOGIN for a captain still bound to another connection takes the
 *     captain over and the older socket is shut down.
 */
#define PKT_ID_STATUS_SALT_REQUIRED 5

#define CRYPTO_NONE 0
#define CRYPTO_AES  1
#define CRYPTO_CHACHA 2
#define CRYPTO_ARIA 3
#define CRYPTO_CAMELLIA 4
#define CRYPTO_SEED     5
#define CRYPTO_CAST5    6
#define CRYPTO_IDEA     7
#define CRYPTO_3DES     8
#define CRYPTO_BLOWFISH 9
#define CRYPTO_RC4      10
#define CRYPTO_DES      11
#define CRYPTO_PQC      12
#define CRYPTO_MCELIECE 13
#define CRYPTO_DILITHIUM 14
#define CRYPTO_SERPENT   15
#define CRYPTO_TWOFISH   16
#define CRYPTO_SM4       17
#define CRYPTO_ASCON     18
#define CRYPTO_PRESENT   19
#define CRYPTO_GOST      20
#define CRYPTO_SALSA     21

#define MAX_CRYPTO_ALGOS 21

#define SCOPE_GLOBAL 0
#define SCOPE_FACTION 1
#define SCOPE_PRIVATE 2

typedef enum {
    FACTION_ALLIANCE = 0,
    FACTION_KORTHIAN = 10,
    FACTION_XYLARI = 11,
    FACTION_SWARM = 12,
    FACTION_VESPERIAN = 13,
    FACTION_JEM_HADAR = 14,
    FACTION_THOLIAN = 15,
    FACTION_GORN = 16,
    FACTION_GILDED = 17,
    FACTION_SPECIES_8472 = 18,
    FACTION_BREEN = 19,
    FACTION_HIROGEN = 20
} Faction;

typedef enum {
    SHIP_CLASS_LEGACY = 0,
    SHIP_CLASS_SCOUT,
    SHIP_CLASS_HEAVY_CRUISER,
    SHIP_CLASS_MULTI_ENGINE,
    SHIP_CLASS_ESCORT,
    SHIP_CLASS_EXPLORER,          /* Aegis-D style */
    SHIP_CLASS_FLAGSHIP,       /* Aegis-E style */
    SHIP_CLASS_SCIENCE,        /* Advanced Science style */
    SHIP_CLASS_CARRIER,           /* Heavy Escort / Carrier */
    SHIP_CLASS_TACTICAL,          /* Galaxy-variant with pod */
    SHIP_CLASS_DIPLOMATIC,      /* Aegis-C style */
    SHIP_CLASS_RESEARCH,          /* Small Science Ship */
    SHIP_CLASS_FRIGATE,     /* Specialized Escort */
    SHIP_CLASS_SENTINEL,    /* Heavy Defensive Escort */
    SHIP_CLASS_GENERIC_ALIEN
} ShipClass;

typedef struct {
    int32_t type;
    char target_name[64];
    uint8_t x25519_pubkey[32];
    int32_t found;
} PacketQueryKey;

typedef struct {
    int32_t type;
    char name[64];
    int32_t faction;
    int32_t ship_class;
    uint8_t pass_hash[32];      /* V2 salted identity hash (see protocol note above) */
    uint8_t salt[16];           /* Per-captain salt (random at enrollment) */
    uint8_t pass_hash_legacy[32]; /* Legacy unsalted hash; verified only for legacy accounts */
    uint8_t x25519_pubkey[32];
} PacketLogin;

typedef struct {
    int32_t type;
    char cmd[256];
} PacketCommand;

typedef struct {
    int32_t type;
    int32_t pubkey_len;
    uint8_t pubkey[256]; /* Standard EC Public Key */
} PacketHandshake;

typedef struct {
    int32_t type;
    char from[64];
    int32_t faction;
    int32_t scope; /* 0: Global, 1: Faction, 2: Private */
    int32_t target_id; /* Player ID (1-based) for Private Message */
    int32_t length;
    int64_t origin_frame; /* Server frame used for frequency scrambling */
    uint8_t is_encrypted;
    uint8_t crypto_algo; /* 1-11:Standard, 12-21:Advanced slots (12-19,21 are experimental PQC-named aliases of AES-256-GCM) */
    uint8_t iv[16];      /* Full 128-bit IV for CBC/CTR/GCM */
    uint8_t tag[16];     /* Auth Tag */
    uint8_t has_signature;
    uint8_t signature[64]; /* Ed25519 Signature */
    uint8_t sender_pubkey[32]; 
    char text[65536];
} PacketMessage;

/* Canonical bounds check for the attacker-controlled PacketMessage.length.
   text is 65536 bytes (65535 chars + NUL), so only [0, 65535] can be
   safely consumed. BOTH endpoints MUST apply it before using the length
   and treat a violation as fatal to the connection: the payload cannot
   be skipped without desynchronizing the TCP stream, so the client
   aborts the listener (spacegl_client.c) and the server drops the
   connection (spacegl_server.c). An out-of-range value must never reach
   the size_t arithmetic in server/net.c (broadcast c_len,
   EVP_DecryptUpdate, relay pkt_size): e.g. length = -5 becomes a
   (size_t) ~4 GB copy/read and crashes the process (remote DoS). */
static inline bool packet_message_length_valid(int32_t length) {
    return length >= 0 && length < 65536;
}

/* Update Packet: Optimized for variable length transmission */
typedef struct {
    int32_t type;
    int64_t frame_id;
    int32_t q1, q2, q3;
    double s1, s2, s3;
    double van_h, van_m, van_r;
    double eta;
    uint64_t energy;
    int32_t torpedoes;
    uint64_t cargo_energy;
    int32_t cargo_torpedoes;
    int32_t crew_count;
    int32_t prison_unit;
    int32_t composite_plating;
    double hull_integrity;
    int32_t shields[6];
    int32_t inventory[10];
    double system_health[10];
    double power_dist[3];
    double life_support;
    int32_t anti_matter_count;
    int32_t target_shields[6];
    char captain_name[64];
    int32_t lock_target;
    int32_t tube_state;
    int32_t tube_load_timers[4];
    int32_t tube_torpedo_etas[4];
    int32_t current_tube;
    double ion_beam_charge;
    uint8_t is_cloaked;
    uint8_t is_docked;
    uint8_t show_axes;
    uint8_t show_grid;
    uint8_t show_bridge;
    uint8_t show_map;
    uint8_t map_filter;
    uint8_t force_shutdown;
    uint8_t shm_crypto_algo;
    uint32_t encryption_flags;
    int32_t radio_lock_target;
    uint8_t red_alert;
    uint8_t is_jammed;
    uint8_t nav_state;
    NetPoint supernova_pos; 
    int32_t supernova_q[3];
    int64_t map_update_val;
    int32_t map_update_q[3];
    int64_t map_update_val2;
    int32_t map_update_q2[3];
    NetPoint torps[4];
    NetPoint wormhole;
    int32_t event_count;
    NetEvent events[MAX_NET_EVENTS];
    int32_t torpedo_count;
    NetVisibleTorpedo visible_torpedoes[MAX_VISIBLE_TORPEDOES];
    NetProbe probes[3];
    int32_t beam_count;
    NetBeam beams[MAX_NET_BEAMS];
    int32_t object_count;
    NetObject objects[MAX_NET_OBJECTS];
} PacketUpdate;

#pragma pack(pop)

/* Transport tuning: disable Nagle (TCP_NODELAY), enable keepalive,
 * and configure low-latency socket buffers. */
static inline void tune_socket(int fd) {
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));
#ifdef __linux__
    int idle = 60;    /* first probe after 60 s idle */
    int intvl = 10;   /* probe every 10 s */
    int cnt = 10;     /* drop after 10 failed probes */
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#endif

    int rbuf = 256 * 1024;
    int sbuf = 128 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rbuf, sizeof(rbuf));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sbuf, sizeof(sbuf));
}

/* Station names: the "from" of the server's own messages (send_server_msg
 * callers: COMPUTER, ENGINEERING, TACTICAL, ...). The client shows them as
 * ship systems, and the relay lets a few of them through a captain's
 * exclusive radio lock. No captain may take one of these names, in any
 * letter case (2026.10.31.05): with "from" now set by the server from the
 * authenticated captain, a captain named COMPUTER would still speak as the
 * ship's computer. Names with a space (DAMAGE CONTROL, LIFE SUPPORT,
 * Alliance Command) cannot be captain names anyway. */
static inline bool spacegl_is_station_name(const char *name) {
    static const char *const stations[] = {
        "ADMIN", "BOARDING", "COMMAND", "COMMANDER", "COMPUTER", "CRITICAL",
        "DAMAGE", "DEBUG", "ENGINE", "ENGINEERING", "GEOLOGY", "HELMSMAN",
        "LOGISTICS", "MEDICAL", "SCIENCE", "SECURITY", "SENSORS", "SERVER",
        "STARBASE", "SYSTEM", "TACTICAL", "WARNING",
    };
    if (!name) return false;
    for (size_t i = 0; i < sizeof(stations) / sizeof(stations[0]); i++)
        if (strcasecmp(name, stations[i]) == 0) return true;
    return false;
}

/* Guard B3-b: bounds-checked count clamps for network-supplied counts */
static inline int32_t validate_object_count(int32_t count) {
    if (count < 0) return 0;
    if (count > MAX_NET_OBJECTS) return MAX_NET_OBJECTS;
    return count;
}

static inline int32_t validate_beam_count(int32_t count) {
    if (count < 0) return 0;
    if (count > MAX_NET_BEAMS) return MAX_NET_BEAMS;
    return count;
}

/* Key management: load 32-byte master key.
 *
 * Priority:
 *   1. SPACEGL_KEY_FILE (preferred): path to a file with 64 hex characters.
 *      Permissions MUST be 0600 (owner rw only; fails if group/world readable).
 *   2. SPACEGL_KEY (legacy): 64 hex characters (or legacy string) in env var.
 *   3. Default (no env, no file): $HOME/.spacegl/key (0600, generated if absent).
 *
 * Security: Key bytes are NEVER logged or printed.
 */
static inline int load_master_key(uint8_t key[32]) {
    /* 1. SPACEGL_KEY_FILE (preferred) */
    const char *key_file = getenv("SPACEGL_KEY_FILE");
    if (key_file && key_file[0]) {
        struct stat st;
        if (stat(key_file, &st) != 0) {
            fprintf(stderr, "FATAL: SPACEGL_KEY_FILE='%s' not found\n", key_file);
            return -1;
        }
        /* Check permissions: must be 0600 (owner rw only).
         * If the file is group- or world-readable, refuse. */
        if ((st.st_mode & 077) != 0) {
            fprintf(stderr,
                "FATAL: SPACEGL_KEY_FILE='%s' permissions are %o. "
                "The key file must be 0600 (chmod 600 %s).\n",
                key_file, st.st_mode & 0777, key_file);
            return -1;
        }
        FILE *f = fopen(key_file, "r");
        if (!f) {
            fprintf(stderr, "FATAL: cannot open SPACEGL_KEY_FILE='%s'\n", key_file);
            return -1;
        }
        char hex[128] = {0};
        if (!fgets(hex, sizeof(hex), f)) {
            fclose(f);
            fprintf(stderr, "FATAL: SPACEGL_KEY_FILE='%s' is empty\n", key_file);
            return -1;
        }
        fclose(f);
        hex[strcspn(hex, "\r\n")] = '\0';
        if (strlen(hex) != 64) {
            fprintf(stderr, "FATAL: SPACEGL_KEY_FILE must contain exactly 64 hex chars (got %zu)\n", strlen(hex));
            return -1;
        }
        for (int i = 0; i < 32; i++) {
            if (sscanf(hex + 2*i, "%2hhx", &key[i]) != 1) {
                fprintf(stderr, "FATAL: invalid hex in key file at position %d\n", i);
                return -1;
            }
        }
        printf("[SECURITY] Master key loaded from file '%s'\n", key_file);
        return 0;
    }

    /* 2. SPACEGL_KEY (legacy) */
    const char *key_env = getenv("SPACEGL_KEY");
    if (key_env && key_env[0]) {
        size_t l = strlen(key_env);
        if (l == 64) {
            for (int i = 0; i < 32; i++) {
                if (sscanf(key_env + 2*i, "%2hhx", &key[i]) != 1) {
                    fprintf(stderr, "FATAL: invalid hex in SPACEGL_KEY at position %d\n", i);
                    return -1;
                }
            }
        } else if (l >= 32) {
            /* Raw string backward compatibility: its first 32 bytes */
            memcpy(key, key_env, 32);
        } else {
            /* Shorter than 32 bytes: the rest of the key used to stay
             * zero, a weak key with no warning (2026.10.31.07). */
            fprintf(stderr, "FATAL: SPACEGL_KEY is %zu characters: use 64 hex characters, "
                            "a string of at least 32 characters, or SPACEGL_KEY_FILE\n", l);
            return -1;
        }
        fprintf(stderr, "\033[1;33m[SECURITY WARN]\033[0m Master key loaded from SPACEGL_KEY env var (legacy; prefer SPACEGL_KEY_FILE)\n");
        return 0;
    }

    /* 3. Default: $HOME/.spacegl/key, generated if absent.
     * 2026.10.31.07: an existing file that does not hold 64 hex characters
     * is an error, no longer silently replaced by a new key (which voided
     * every captain identity HMAC'd with the old one); a group/world
     * readable file is tightened to 0600; the new key is written to a
     * private temporary file (O_EXCL, 0600, fsync) and published with
     * link(), which fails if another process (server and client started
     * together) published one first — then that key is used. */
    const char *home = getenv("HOME");
    if (!home || !home[0]) home = "/tmp";
    char dir_path[1024];
    char key_path[1024];
    snprintf(dir_path, sizeof(dir_path), "%s/.spacegl", home);
    snprintf(key_path, sizeof(key_path), "%s/.spacegl/key", home);

    for (int attempt = 0; attempt < 2; attempt++) {
        /* Read an existing key */
        int rfd = open(key_path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (rfd >= 0) {
            struct stat st;
            if (fstat(rfd, &st) != 0 || !S_ISREG(st.st_mode)) {
                close(rfd);
                fprintf(stderr, "FATAL: '%s' is not a regular file\n", key_path);
                return -1;
            }
            if ((st.st_mode & 077) != 0) {
                if (fchmod(rfd, 0600) != 0) {
                    close(rfd);
                    fprintf(stderr, "FATAL: '%s' is readable by others (mode %o) and cannot be changed to 0600\n",
                            key_path, (unsigned)(st.st_mode & 0777));
                    return -1;
                }
                fprintf(stderr, "\033[1;33m[SECURITY WARN]\033[0m '%s' was mode %o: tightened to 0600\n",
                        key_path, (unsigned)(st.st_mode & 0777));
            }
            char hex[130] = {0};
            ssize_t n = read(rfd, hex, sizeof(hex) - 1);
            close(rfd);
            if (n < 0) n = 0;
            hex[n] = '\0';
            hex[strcspn(hex, "\r\n")] = '\0';
            bool ok = (strlen(hex) == 64);
            for (int i = 0; ok && i < 32; i++) {
                if (sscanf(hex + 2*i, "%2hhx", &key[i]) != 1) ok = false;
            }
            if (!ok) {
                if (attempt == 0 && n < 65) {
                    /* maybe a writer is publishing it right now: retry once */
                    struct timespec wait_ts = { 0, 100L * 1000L * 1000L };
                    nanosleep(&wait_ts, NULL);
                    continue;
                }
                fprintf(stderr, "FATAL: '%s' does not hold a 64-hex-character key; it is left untouched. "
                                "Restore it, or remove it to generate a new key (this voids every existing captain identity)\n",
                        key_path);
                return -1;
            }
            printf("[SECURITY] Master key loaded from '%s'\n", key_path);
            return 0;
        }
        if (errno != ENOENT) {
            fprintf(stderr, "FATAL: cannot open '%s': %s\n", key_path, strerror(errno));
            return -1;
        }

        /* Generate a new random key: the CSPRNG or the kernel, never rand() */
        if (RAND_bytes(key, 32) != 1) {
            int ur = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
            ssize_t got = (ur >= 0) ? read(ur, key, 32) : -1;
            if (ur >= 0) close(ur);
            if (got != 32) {
                fprintf(stderr, "FATAL: no secure random source for the master key\n");
                return -1;
            }
        }

        if (mkdir(dir_path, 0700) != 0 && errno != EEXIST) {
            fprintf(stderr, "FATAL: cannot create '%s': %s\n", dir_path, strerror(errno));
            return -1;
        }
        char tmp_path[1100];
        snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.%ld", key_path, (long)getpid());
        unlink(tmp_path);
        int wfd = open(tmp_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (wfd < 0) {
            fprintf(stderr, "FATAL: cannot create key file '%s': %s\n", tmp_path, strerror(errno));
            return -1;
        }
        char out[65];
        for (int i = 0; i < 32; i++) snprintf(out + 2*i, 3, "%02x", key[i]);
        out[64] = '\n';
        bool wrote = (write(wfd, out, 65) == 65) && (fsync(wfd) == 0);
        memset(out, 0, sizeof(out));
        close(wfd);
        if (!wrote) {
            unlink(tmp_path);
            fprintf(stderr, "FATAL: cannot write key file '%s'\n", tmp_path);
            return -1;
        }
        int linked = link(tmp_path, key_path);
        int link_errno = errno;
        unlink(tmp_path);
        if (linked == 0) {
            printf("[SECURITY] Generated new master key, stored in '%s' (0600)\n", key_path);
            return 0;
        }
        if (link_errno != EEXIST) {
            fprintf(stderr, "FATAL: cannot publish key file '%s': %s\n", key_path, strerror(link_errno));
            return -1;
        }
        /* another process published its key first: load that one */
    }
    fprintf(stderr, "FATAL: could not load or create '%s'\n", key_path);
    return -1;
}

#endif
