/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * relay_snapshot_test — server message relay (src/server/net.c, conn.c):
 *
 *  1. encrypt_payload() takes an explicit length: a plaintext with embedded
 *     NUL bytes is encrypted whole (strlen() used to cut it at the first NUL).
 *  2. broadcast_message() relays a fleet message byte-exact, NULs included,
 *     to a cleartext recipient and to an encrypted recipient (re-encrypted
 *     with the key captured in the routing snapshot).
 *  3. send_server_msg() (LEGACY path) encrypts with the key captured for the
 *     recipient and delivers on the captured fd.
 *  4. player_socket_detach() wakes a writer blocked in send() on a full
 *     socket (shutdown first), so it never waits on socket_mutex for long,
 *     and leaves the slot with socket == 0: later sends are skipped.
 *  5. (2026.10.31.40) one encoding per frequency and key: eleven recipients
 *     (six on AES with the fleet key, two on ChaCha20, one on AES with a
 *     key of its own, two in clear) of a cleartext and of an encrypted
 *     fleet message each get the plaintext back; the six that share a key
 *     receive the same bytes (encrypted once), the one with its own key a
 *     packet of its own. The time of 200 relays of 16 KiB is printed.
 */
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <poll.h>
#include <stdatomic.h>
#include "radio_crypto.h"

#include "server_internal.h"
#include "game_state.h"
#include "packets.h"
#include "network.h"
#include "conn.h"

#define TEST_CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "TEST FAILED: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        exit(1); \
    } \
} while (0)

/* ================================================================== */
/* Globals required by linked server modules                          */
/* ================================================================== */

ConnectedPlayer players[MAX_CLIENTS];
SpaceGLGame spacegl_master;

NPCStar stars_data[MAX_STARS];
NPCBlackHole black_holes[MAX_BH];
NPCNebula nebulas[MAX_NEBULAS];
NPCPulsar pulsars[MAX_PULSARS];
NPCQuasar quasars[MAX_QUASARS];
NPCComet comets[MAX_COMETS];
NPCAsteroid asteroids[MAX_ASTEROIDS];
NPCDerelict derelicts[MAX_DERELICTS];
NPCMine mines[MAX_MINES];
NPCBuoy buoys[MAX_BUOYS];
NPCPlatform platforms[MAX_PLATFORMS];
NPCRift rifts[MAX_RIFTS];
NPCMonster monsters[MAX_MONSTERS];
NPCPlanet planets[MAX_PLANETS];
NPCSysStar   sys_stars[SYS_MAX_STARS];
NPCSysPlanet sys_planets[SYS_MAX_PLANETS];
NPCSysMoon   sys_moons[SYS_MAX_MOONS];
int sys_star_count = 0;
NPCBase bases[MAX_BASES];
NPCShip npcs[MAX_NPC];
NPCDyson dysons[MAX_DYSON];
NPCHub hubs[MAX_HUBS];
NPCRelic relics[MAX_RELICS];
NPCRupture ruptures[MAX_RUPTURES];
NPCSatellite satellites[MAX_SATELLITES];
NPCStorm storms[MAX_STORMS];
NPCArtifact artifacts[MAX_ARTIFACTS];
NPCWarpGate warp_gates[MAX_WARP_GATES];
NPCNeutronStar neutron_stars[MAX_NEUTRON_STARS];
NPCMegaStructure mega_structs[MAX_MEGA_STRUCTS];
NPCDarkCloud dark_clouds[MAX_DARK_CLOUDS];
NPCSingularity singularities[MAX_SINGULARITIES];
NPCPlasmaStorm plasma_storms[MAX_PLASMA_STORMS];
NPCOrbitalRing orbital_rings[MAX_ORBITAL_RINGS];
NPCTimeAnomaly time_anomalies[MAX_TIME_ANOMALIES];
NPCVoidCrystal void_crystals[MAX_VOID_CRYSTALS];
NPCSubspaceAnomaly subspace_anomalies[MAX_SUBSPACE_ANOMALIES];
NPCDiffuseNebula diffuse_nebulae[MAX_DIFFUSE_NEBULAE];
NPCDarkNebula dark_nebulae[MAX_DARK_NEBULAE];
NPCPlanetaryNebula planetary_nebulae[MAX_PLANETARY_NEBULAE];
NPCSNR snrs[MAX_SNR];
NPCGMC gmcs[MAX_GMC];
NPCInterstellarFilament interstellar_filaments[MAX_INTERSTELLAR_FILAMENTS];
NPCInterstellarBubble interstellar_bubbles[MAX_INTERSTELLAR_BUBBLES];
NPCBokGlobule bok_globules[MAX_BOK_GLOBULES];
NPCClumpCore clump_cores[MAX_CLUMP_CORES];
NPCAccretionDisk accretion_disks[MAX_ACCRETION_DISKS];
NPCRelativisticJet relativistic_jets[MAX_RELATIVISTIC_JETS];
NPCShockWave shock_waves[MAX_SHOCK_WAVES];
NPCStellarBowShock stellar_bow_shocks[MAX_STELLAR_BOW_SHOCKS];
NPCCosmicVoid cosmic_voids[MAX_COSMIC_VOIDS];
NPCCosmicFilament cosmic_filaments[MAX_COSMIC_FILAMENTS];
NPCEventHorizon event_horizons[MAX_EVENT_HORIZONS];
NPCKilonova kilonovae[MAX_KILONOVAE];
NPCGravLens grav_lenses[MAX_GRAV_LENSES];
NPCGRB grbs[MAX_GRB];
NPCGravWave grav_waves[MAX_GRAV_WAVES];
NPCProtoplanetaryDisk protoplanetary_disks[MAX_PROTOPLANETARY_DISKS];
NPCDebrisDisk debris_disks[MAX_DEBRIS_DISKS];
NPCPlanetesimal planetesimals[MAX_PLANETESIMALS];
NPCRoguePlanet rogue_planets[MAX_ROGUE_PLANETS];
NPCBrownDwarf brown_dwarfs[MAX_BROWN_DWARFS];
NPCISO isos[MAX_ISO];
NPCMagReconn mag_reconns[MAX_MAG_RECONN];
NPCCurrentSheet current_sheets[MAX_CURRENT_SHEETS];
NPCHeliosphere heliospheres[MAX_HELIOSPHERES];
NPCTermShock term_shocks[MAX_TERM_SHOCKS];
NPCMagnetosphere magnetospheres[MAX_MAGNETOSPHERES];
NPCCosmicString cosmic_strings[MAX_COSMIC_STRINGS];
NPCDomainWall domain_walls[MAX_DOMAIN_WALLS];
NPCDMHalo dm_halos[MAX_DM_HALO];
NPCIGM igms[MAX_IGM];
NPCCGM cgms[MAX_CGM];
NPCLymanAlpha lyman_alphas[MAX_LYMAN_ALPHA];
NPCCMB cmbs[MAX_CMB];

NPCCosmicFeature cosmic_features[MAX_COSMIC_FEATURES];
int cosmic_feature_count = 0;

PlayerTorpedo players_torpedoes[MAX_GLOBAL_TORPEDOES];
pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
int g_debug = 0;
int global_tick = 0;
uint8_t MASTER_SESSION_KEY[32] = {0};
uint8_t GALAXY_VERIFY_KEY[32] = {0};
uint8_t ALGO_KEYS[MAX_CRYPTO_ALGOS + 1][32] = {0};
uint8_t SERVER_PUBKEY[32] = {0};
uint8_t SERVER_PRIVKEY[64] = {0};
uint8_t deep_space_key[32] = {0};

QuadrantIndex (*spatial_index)[41][41] = NULL;
threadpool_t *g_pool = NULL;
atomic_int g_running = 1;

/* Stubs */
void sync_client_task(void *arg) { free(arg); }
void derive_algo_keys(uint8_t *master_key, const char *name,
                      uint8_t target_keys[MAX_CRYPTO_ALGOS + 1][32]) {
    (void)master_key; (void)name; (void)target_keys;
}
void ensure_player_algo_key(int p_idx, int k, bool private_mode) {
    (void)p_idx; (void)k; (void)private_mode;
}
void save_galaxy(void) {}
void save_galaxy_async(void) {}
void spawn_derelict(int q1, int q2, int q3, double x, double y, double z,
                    int faction, int ship_class, const char *name) {
    (void)q1; (void)q2; (void)q3; (void)x; (void)y; (void)z;
    (void)faction; (void)ship_class; (void)name;
}
const char *get_species_name(int s) { (void)s; return "TEST"; }
void rebuild_spatial_index(void) {}
void refresh_lrs_grid(void) {}
void update_moon_orbits(void) {}
void update_star_systems(void) {}

/* net.c (no public prototype) */
void encrypt_payload(PacketMessage *msg, const char *plaintext, size_t plaintext_len,
                     const uint8_t *key, int64_t frame_id);

static const char k_payload[] = { 'A', 'B', '\0', 'C', 'D', '\0', 'E' };
#define K_PAYLOAD_LEN ((int)sizeof(k_payload))

static double now_s(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* Decrypt a PacketMessage produced by encrypt_payload(); returns the length. */
static int decrypt_msg(const PacketMessage *m, const uint8_t *key, uint8_t *out) {
    int is_gcm = 0;
    const EVP_CIPHER *c = radio_cipher_for_algo(m->crypto_algo, &is_gcm);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, c, NULL, NULL, NULL);
    if (is_gcm) EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 16, NULL);
    EVP_DecryptInit_ex(ctx, NULL, NULL, key, m->iv);
    int ol = 0, fl = 0;
    EVP_DecryptUpdate(ctx, out, &ol, (const uint8_t *)m->text, m->length);
    if (is_gcm) EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void *)m->tag);
    int ok = EVP_DecryptFinal_ex(ctx, out + ol, &fl);
    EVP_CIPHER_CTX_free(ctx);
    return ok > 0 ? ol + fl : -1;
}

/* Read one PacketMessage (header + length bytes) from fd within 2 s. */
static int read_msg(int fd, PacketMessage *m) {
    size_t hdr = offsetof(PacketMessage, text), got = 0;
    uint8_t *p = (uint8_t *)m;
    double t0 = now_s();
    size_t want = hdr;
    while (got < want) {
        struct pollfd pf = { .fd = fd, .events = POLLIN };
        if (poll(&pf, 1, 200) > 0) {
            ssize_t n = recv(fd, p + got, want - got, 0);
            if (n <= 0) return -1;
            got += (size_t)n;
            if (got == hdr && want == hdr) {
                if (m->length < 0 || m->length > 65535) return -1;
                want = hdr + (size_t)m->length;
            }
        }
        if (now_s() - t0 > 2.0) return -1;
    }
    return 0;
}

static void set_player(int i, const char *name, int fd, int algo, uint8_t fill) {
    /* players[] is a zeroed global and socket_mutex is initialised in
     * main(): set only the fields the relay reads. */
    memset(players[i].name, 0, sizeof(players[i].name));
    strncpy(players[i].name, name, 63);
    players[i].radio_lock_target = 0;
    players[i].active = 1;
    players[i].socket = fd;
    players[i].state.shm_crypto_algo = algo;
    for (int k = 0; k <= MAX_CRYPTO_ALGOS; k++) memset(players[i].algo_keys[k], fill + k, 32);
    memset(players[i].session_key, fill, 32);
}

typedef struct { int slot; int fd; atomic_int done; int result; } BlockedWriter;

static void *blocked_writer(void *arg) {
    BlockedWriter *w = arg;
    size_t len = 8u << 20;   /* far more than the socket buffers hold */
    char *buf = calloc(1, len);
    pthread_mutex_lock(&players[w->slot].socket_mutex);
    w->result = write_all(w->fd, buf, len);   /* blocking socket: blocks in send() */
    pthread_mutex_unlock(&players[w->slot].socket_mutex);
    free(buf);
    atomic_store(&w->done, 1);
    return NULL;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    for (int i = 0; i < MAX_CLIENTS; i++) pthread_mutex_init(&players[i].socket_mutex, NULL);

    /* ---- 1. encrypt_payload with embedded NULs ---- */
    {
        PacketMessage *m = calloc(1, sizeof(PacketMessage));
        uint8_t key[32]; RAND_bytes(key, 32);
        m->crypto_algo = CRYPTO_AES;
        m->is_encrypted = 1;
        encrypt_payload(m, k_payload, sizeof(k_payload), key, 42);
        TEST_CHECK(m->is_encrypted == 1);
        TEST_CHECK(m->length == K_PAYLOAD_LEN);
        uint8_t out[64];
        TEST_CHECK(decrypt_msg(m, key, out) == K_PAYLOAD_LEN);
        TEST_CHECK(memcmp(out, k_payload, sizeof(k_payload)) == 0);
        free(m);
        printf("[1] encrypt_payload keeps %d bytes with embedded NULs: OK\n", K_PAYLOAD_LEN);
    }

    /* ---- 2. broadcast_message: cleartext + encrypted recipients ---- */
    int sa[2], sb[2], sc[2];
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sa) == 0);
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sb) == 0);
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sc) == 0);
    set_player(0, "alice", sa[0], CRYPTO_NONE, 0x10);   /* sender, cleartext */
    set_player(1, "bob",   sb[0], CRYPTO_NONE, 0x20);   /* cleartext recipient */
    set_player(2, "carol", sc[0], CRYPTO_AES,  0x30);   /* encrypted recipient */
    {
        PacketMessage *in = calloc(1, sizeof(PacketMessage));
        PacketMessage *rx = calloc(1, sizeof(PacketMessage));
        in->type = PKT_MESSAGE;
        strcpy(in->from, "alice");
        in->scope = 0;
        in->is_encrypted = 0;
        in->length = K_PAYLOAD_LEN;
        memcpy(in->text, k_payload, sizeof(k_payload));
        broadcast_message(in);

        TEST_CHECK(read_msg(sb[1], rx) == 0);
        TEST_CHECK(rx->is_encrypted == 0);
        TEST_CHECK(rx->length == K_PAYLOAD_LEN);
        TEST_CHECK(memcmp(rx->text, k_payload, sizeof(k_payload)) == 0);

        memset(rx, 0, sizeof(PacketMessage));
        TEST_CHECK(read_msg(sc[1], rx) == 0);
        TEST_CHECK(rx->is_encrypted == 0x01);
        TEST_CHECK(rx->crypto_algo == CRYPTO_AES);
        TEST_CHECK(rx->length == K_PAYLOAD_LEN);
        uint8_t out[64];
        TEST_CHECK(decrypt_msg(rx, players[2].algo_keys[CRYPTO_AES], out) == K_PAYLOAD_LEN);
        TEST_CHECK(memcmp(out, k_payload, sizeof(k_payload)) == 0);

        memset(rx, 0, sizeof(PacketMessage));
        TEST_CHECK(read_msg(sa[1], rx) == 0);          /* loopback to the sender */
        TEST_CHECK(rx->length == K_PAYLOAD_LEN);
        free(in); free(rx);
        printf("[2] fleet relay byte-exact to cleartext and encrypted recipients: OK\n");
    }

    /* ---- 3. send_server_msg (LEGACY): captured key and fd ---- */
    {
        PacketMessage *rx = calloc(1, sizeof(PacketMessage));
        send_server_msg(2, "COMPUTER", "Route check.");
        TEST_CHECK(read_msg(sc[1], rx) == 0);
        TEST_CHECK(rx->is_encrypted == 1 && rx->crypto_algo == CRYPTO_AES);
        uint8_t out[64];
        int n = decrypt_msg(rx, players[2].algo_keys[CRYPTO_AES], out);
        TEST_CHECK(n == (int)strlen("Route check."));
        TEST_CHECK(memcmp(out, "Route check.", (size_t)n) == 0);
        free(rx);
        printf("[3] send_server_msg encrypts with the captured route: OK\n");
    }

    /* ---- 4. player_socket_detach wakes a blocked writer ---- */
    {
        int sd[2];
        TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sd) == 0);   /* blocking */
        set_player(3, "dave", sd[0], CRYPTO_NONE, 0x40);
        BlockedWriter w = { .slot = 3, .fd = sd[0], .result = 0 };
        atomic_init(&w.done, 0);
        pthread_t th;
        TEST_CHECK(pthread_create(&th, NULL, blocked_writer, &w) == 0);
        usleep(300 * 1000);              /* the writer is now blocked in send() */
        TEST_CHECK(!atomic_load(&w.done));

        pthread_mutex_lock(&game_mutex);
        double t0 = now_s();
        player_socket_detach(3);
        double dt = now_s() - t0;
        pthread_mutex_unlock(&game_mutex);

        pthread_join(th, NULL);
        TEST_CHECK(atomic_load(&w.done));
        TEST_CHECK(dt < 2.0);
        TEST_CHECK(players[3].socket == 0);
        TEST_CHECK(w.result < (int)(8u << 20));   /* the write was cut short */

        /* a later send to the detached slot is skipped (no write, no crash) */
        send_server_msg(3, "COMPUTER", "nobody home");
        close(sd[0]); close(sd[1]);
        printf("[4] detach woke the blocked writer in %.3f s, slot cleared: OK\n", dt);
    }

    close(sa[0]); close(sa[1]); close(sb[0]); close(sb[1]); close(sc[0]); close(sc[1]);

    /* ---- 5. one encoding per frequency and key ---- */
    {
        for (int i = 0; i < MAX_CLIENTS; i++) {
            players[i].active = 0;
            players[i].socket = 0;
        }
        enum { N = 12 };
        int sp[N][2];
        static const int algo_of[N] = { CRYPTO_NONE,                     /* 0: sender */
                                        CRYPTO_AES, CRYPTO_AES, CRYPTO_AES,
                                        CRYPTO_AES, CRYPTO_AES, CRYPTO_AES,
                                        CRYPTO_CHACHA, CRYPTO_CHACHA,
                                        CRYPTO_AES,                      /* 9: own key */
                                        CRYPTO_NONE, CRYPTO_NONE };
        uint8_t fleet[MAX_CRYPTO_ALGOS + 1][32];
        for (int k = 0; k <= MAX_CRYPTO_ALGOS; k++) memset(fleet[k], 0x70 + k, 32);
        for (int i = 0; i < N; i++) {
            TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sp[i]) == 0);
            int b = 1 << 20;
            setsockopt(sp[i][0], SOL_SOCKET, SO_SNDBUF, &b, sizeof(b));
            setsockopt(sp[i][1], SOL_SOCKET, SO_RCVBUF, &b, sizeof(b));
            char name[16];
            snprintf(name, sizeof(name), "cpt%d", i);
            set_player(i, name, sp[i][0], algo_of[i], (uint8_t)(0x80 + i));
            if (i != 9) memcpy(players[i].algo_keys, fleet, sizeof(fleet));   /* the fleet keys */
        }
        enum { LEN = 16384 };
        char *text = malloc(LEN);
        for (int j = 0; j < LEN; j++) text[j] = (char)((j * 7 + 3) & 0xff);   /* NULs included */
        PacketMessage *in = calloc(1, sizeof(PacketMessage));
        PacketMessage *rx = calloc(N, sizeof(PacketMessage));
        uint8_t *out = malloc(65536);

        for (int round = 0; round < 2; round++) {
            memset(in, 0, sizeof(PacketMessage));
            in->type = PKT_MESSAGE;
            strcpy(in->from, "cpt0");
            if (round == 0) {                      /* cleartext from the sender */
                memcpy(in->text, text, LEN);
                in->length = LEN;
            } else {                               /* fleet AES: decrypted by the relay */
                in->crypto_algo = CRYPTO_AES;
                in->is_encrypted = 0x01;
                encrypt_payload(in, text, LEN, players[0].algo_keys[CRYPTO_AES], 7);
                TEST_CHECK(in->is_encrypted == 0x01 && in->length == LEN);
            }
            broadcast_message(in);
            for (int i = 0; i < N; i++) {
                memset(&rx[i], 0, sizeof(PacketMessage));
                TEST_CHECK(read_msg(sp[i][1], &rx[i]) == 0);
                TEST_CHECK(rx[i].crypto_algo == (uint8_t)algo_of[i]);
                if (algo_of[i] == CRYPTO_NONE) {
                    TEST_CHECK(rx[i].is_encrypted == 0 && rx[i].length == LEN);
                    TEST_CHECK(memcmp(rx[i].text, text, LEN) == 0);
                } else {
                    TEST_CHECK(rx[i].is_encrypted == 0x01);
                    TEST_CHECK(decrypt_msg(&rx[i], players[i].algo_keys[algo_of[i]], out) == LEN);
                    TEST_CHECK(memcmp(out, text, LEN) == 0);
                }
            }
            for (int i = 2; i <= 6; i++)            /* same frequency, same key: one encoding */
                TEST_CHECK(memcmp(&rx[i], &rx[1], offsetof(PacketMessage, text) + LEN) == 0);
            TEST_CHECK(memcmp(&rx[8], &rx[7], offsetof(PacketMessage, text) + LEN) == 0);
            TEST_CHECK(memcmp(rx[9].iv, rx[1].iv, 16) != 0);   /* its own key: its own packet */
        }

        /* 200 relays of a 16 KiB cleartext message to the eleven recipients */
        double t0 = now_s();
        for (int it = 0; it < 200; it++) {
            memset(in, 0, offsetof(PacketMessage, text));
            in->type = PKT_MESSAGE;
            strcpy(in->from, "cpt0");
            memcpy(in->text, text, LEN);
            in->length = LEN;
            broadcast_message(in);
            for (int i = 0; i < N; i++) TEST_CHECK(read_msg(sp[i][1], &rx[i]) == 0);
        }
        double dt = now_s() - t0;
        for (int i = 0; i < N; i++) {
            close(sp[i][0]);
            close(sp[i][1]);
        }
        free(text);
        free(in);
        free(rx);
        free(out);
        printf("[5] one encoding per frequency and key (6 AES + 2 ChaCha20 + 1 own key + 2 clear): OK;"
               " 200 relays of 16 KiB in %.1f ms\n",
               dt * 1000.0);
    }
    printf("relay_snapshot_test: all checks passed\n");
    return 0;
}
