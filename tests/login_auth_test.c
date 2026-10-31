/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * login_auth_test — PKT_LOGIN must prove the captain's identity
 * (src/server/packets.c). Drives the REAL dispatch_packet() in LEGACY wire
 * mode over socketpairs, with the captains/ records in a mkdtemp sandbox:
 *
 *  1. PKT_LOGIN for a name with no identity record -> refused, fd closed.
 *  2. PKT_QUERY creates "alice" (status 2); PKT_LOGIN with a wrong hash,
 *     and with the right hash but a wrong salt -> refused, fd closed.
 *  3. PKT_LOGIN with the right salt and hash -> the slot is bound.
 *  4. A second verified PKT_LOGIN for "alice" on another connection takes
 *     the captain over: the new fd is bound, the old one is shut down.
 *  5. A legacy (unsalted) record accepts the matching legacy hash and
 *     refuses another one.
 *  6. (2026.10.31.09) credentials sent in clear instead of masked under the
 *     session key, and a PKT_LOGIN with no handshake, are refused.
 * Every connection runs the real v2 handshake first and masks its
 * credentials with hs_v2_cred_xor(), as the command deck does.
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
#include <sys/stat.h>
#include <signal.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "server_internal.h"
#include "game_state.h"
#include "packets.h"
#include "network.h"
#include "conn.h"
#include "handshake_v2.h"

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
volatile sig_atomic_t g_running = 1;

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

static int g_fail_count = 0;

static void make_pair(int sv[2]) {
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    int b = 1 << 20;
    setsockopt(sv[0], SOL_SOCKET, SO_RCVBUF, &b, sizeof(b));
    setsockopt(sv[1], SOL_SOCKET, SO_SNDBUF, &b, sizeof(b));
}

static void drain(int fd) {
    char buf[16384];
    while (recv(fd, buf, sizeof(buf), MSG_DONTWAIT) > 0) {}
}

static bool fd_closed(int fd) {
    return fcntl(fd, F_GETFD) == -1 && errno == EBADF;
}

static PacketLogin mk(int type, const char *name, uint8_t hash_byte, uint8_t salt_byte, uint8_t legacy_byte) {
    PacketLogin p;
    memset(&p, 0, sizeof(p));
    p.type = type;
    strncpy(p.name, name, sizeof(p.name) - 1);
    memset(p.pass_hash, hash_byte, 32);
    memset(p.salt, salt_byte, 16);
    memset(p.pass_hash_legacy, legacy_byte, 32);
    memset(p.x25519_pubkey, 0x5a, 32);
    return p;
}

/* Per connection (indexed by the server fd): the session key from the v2
 * handshake and the credential sequence, as the client keeps them. */
static uint8_t g_sk[64][32];
static uint32_t g_seq[64];

/* Real v2 handshake through the dispatcher on server fd sv[1]. */
static void hs_connect(int sv[2]) {
    PacketHandshake h;
    memset(&h, 0, sizeof(h));
    h.type = PKT_HANDSHAKE;
    h.pubkey_len = HANDSHAKE_V2_MARKER;
    HsV2Client st;
    TEST_CHECK(hs_v2_client_hello(MASTER_SESSION_KEY, h.pubkey, &st));
    size_t body = sizeof(PacketHandshake) - sizeof(int32_t);
    TEST_CHECK(write(sv[0], ((char *)&h) + sizeof(int32_t), body) == (ssize_t)body);
    PktIO io;
    memset(&io, 0, sizeof(io));
    io.fd = sv[1];
    io.epoll_fd = -1;
    TEST_CHECK(dispatch_packet(&io, PKT_HANDSHAKE) == 0);
    uint8_t ack[4 + HANDSHAKE_V2_REPLY_LEN], gv[32];
    TEST_CHECK(read(sv[0], ack, sizeof(ack)) == (ssize_t)sizeof(ack));
    TEST_CHECK(sv[1] < 64);
    TEST_CHECK(hs_v2_client_finish(MASTER_SESSION_KEY, &st, ack + 4, g_sk[sv[1]], gv));
    g_seq[sv[1]] = 0;
}

static void make_pair_hs(int sv[2]) {
    make_pair(sv);
    hs_connect(sv);
}

/* Send one packet body through the real dispatcher on server fd sv[1],
 * credentials masked under the connection's session key (as the client). */
static int send_pkt_raw(int sv[2], const PacketLogin *p, bool mask) {
    PacketLogin w = *p;
    if (mask) TEST_CHECK(hs_v2_cred_xor(g_sk[sv[1]], g_seq[sv[1]]++, (uint8_t *)&w + offsetof(PacketLogin, pass_hash)));
    size_t body = sizeof(PacketLogin) - sizeof(int32_t);
    TEST_CHECK(write(sv[0], ((const char *)&w) + sizeof(int32_t), body) == (ssize_t)body);
    PktIO io;
    memset(&io, 0, sizeof(io));
    io.fd = sv[1];
    io.epoll_fd = -1;
    int r = dispatch_packet(&io, p->type);
    return r;
}

static int send_pkt(int sv[2], const PacketLogin *p) { return send_pkt_raw(sv, p, true); }

static int slot_of(const char *name) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (players[i].name[0] && strcmp(players[i].name, name) == 0) return i;
    return -1;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    (void)g_fail_count;
    spatial_index = calloc(41 * 41 * 41, sizeof(QuadrantIndex));
    TEST_CHECK(spatial_index != NULL);
    for (int i = 0; i < MAX_CLIENTS; i++) pthread_mutex_init(&players[i].socket_mutex, NULL);

    RAND_bytes(MASTER_SESSION_KEY, 32);
    char tmpl[] = "/tmp/sglogin-XXXXXX";
    char *sandbox = mkdtemp(tmpl);
    TEST_CHECK(sandbox != NULL);
    server_set_data_dir(sandbox);
    log_init();

    /* 1. no identity record */
    {
        int sv[2]; make_pair_hs(sv);
        PacketLogin p = mk(PKT_LOGIN, "ghost", 0x42, 0xaa, 0x24);
        TEST_CHECK(send_pkt(sv, &p) == 1);
        TEST_CHECK(fd_closed(sv[1]));
        TEST_CHECK(slot_of("ghost") == -1);
        close(sv[0]);
        printf("[1] PKT_LOGIN without an identity record refused: OK\n");
    }

    /* 2. create alice through PKT_QUERY, then wrong hash / wrong salt */
    {
        int sv[2]; make_pair_hs(sv);
        PacketLogin q = mk(PKT_QUERY, "alice", 0x42, 0xaa, 0x24);
        TEST_CHECK(send_pkt(sv, &q) == 0);
        int32_t status = 0;
        TEST_CHECK(read(sv[0], &status, sizeof(status)) == (ssize_t)sizeof(status));
        TEST_CHECK(status == 2);                       /* new captain created */
        char path[1200];
        snprintf(path, sizeof(path), "%s/captains/alice/identity.hash", sandbox);
        struct stat st;
        TEST_CHECK(stat(path, &st) == 0);
        close(sv[0]); close(sv[1]);

        int sb[2]; make_pair_hs(sb);
        PacketLogin bad = mk(PKT_LOGIN, "alice", 0x43, 0xaa, 0x24);   /* wrong hash */
        TEST_CHECK(send_pkt(sb, &bad) == 1);
        TEST_CHECK(fd_closed(sb[1]));
        TEST_CHECK(slot_of("alice") == -1);
        close(sb[0]);

        int sc[2]; make_pair_hs(sc);
        PacketLogin badsalt = mk(PKT_LOGIN, "alice", 0x42, 0xab, 0x24); /* wrong salt */
        TEST_CHECK(send_pkt(sc, &badsalt) == 1);
        TEST_CHECK(fd_closed(sc[1]));
        TEST_CHECK(slot_of("alice") == -1);
        close(sc[0]);
        printf("[2] PKT_LOGIN with a wrong hash or a wrong salt refused: OK\n");
    }

    /* 3. right salt + hash: the slot is bound */
    int first[2]; make_pair_hs(first);
    {
        PacketLogin ok = mk(PKT_LOGIN, "alice", 0x42, 0xaa, 0x24);
        TEST_CHECK(send_pkt(first, &ok) == 0);
        TEST_CHECK(!fd_closed(first[1]));
        int s = slot_of("alice");
        TEST_CHECK(s >= 0);
        TEST_CHECK(players[s].socket == first[1]);
        players[s].active = 1;                     /* as sync_client_task would */
        drain(first[0]);
        printf("[3] verified PKT_LOGIN binds the slot: OK\n");
    }

    /* 4. takeover by a second verified connection */
    {
        int second[2]; make_pair_hs(second);
        PacketLogin ok = mk(PKT_LOGIN, "alice", 0x42, 0xaa, 0x24);
        TEST_CHECK(send_pkt(second, &ok) == 0);
        int s = slot_of("alice");
        TEST_CHECK(s >= 0);
        TEST_CHECK(players[s].socket == second[1]);
        /* the old connection was shut down: its peer reads EOF */
        drain(first[0]);
        char c;
        TEST_CHECK(recv(first[0], &c, 1, 0) == 0);
        int named = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) if (strcmp(players[i].name, "alice") == 0) named++;
        TEST_CHECK(named == 1);
        close(first[0]); close(first[1]);
        close(second[0]); close(second[1]);
        printf("[4] second verified login takes the captain over, old fd shut down: OK\n");
    }

    /* 5. legacy record: identity.hash only, no salt file */
    {
        char dir[1200], path[1300];
        snprintf(dir, sizeof(dir), "%s/captains/oldtimer", sandbox);
        TEST_CHECK(mkdir(dir, 0700) == 0);
        snprintf(path, sizeof(path), "%s/identity.hash", dir);
        FILE *f = fopen(path, "wb");
        TEST_CHECK(f != NULL);
        uint8_t legacy[32]; memset(legacy, 0x77, 32);
        TEST_CHECK(fwrite(legacy, 1, 32, f) == 32);
        fclose(f);

        int sv[2]; make_pair_hs(sv);
        PacketLogin bad = mk(PKT_LOGIN, "oldtimer", 0x11, 0x22, 0x78);
        TEST_CHECK(send_pkt(sv, &bad) == 1);
        TEST_CHECK(fd_closed(sv[1]));
        close(sv[0]);

        int sw[2]; make_pair_hs(sw);
        PacketLogin ok = mk(PKT_LOGIN, "oldtimer", 0x11, 0x22, 0x77);
        TEST_CHECK(send_pkt(sw, &ok) == 0);
        int s = slot_of("oldtimer");
        TEST_CHECK(s >= 0 && players[s].socket == sw[1]);
        close(sw[0]); close(sw[1]);
        printf("[5] legacy record: matching legacy hash accepted, other refused: OK\n");
    }

    /* 6. credentials not masked under the session key, and no handshake */
    {
        int sv[2]; make_pair_hs(sv);
        PacketLogin ok = mk(PKT_LOGIN, "alice", 0x42, 0xaa, 0x24);
        TEST_CHECK(send_pkt_raw(sv, &ok, false) == 1);    /* right hash, but in clear */
        TEST_CHECK(fd_closed(sv[1]));
        close(sv[0]);

        int sn[2]; make_pair(sn);                          /* no handshake at all */
        TEST_CHECK(send_pkt_raw(sn, &ok, false) == 1);
        TEST_CHECK(fd_closed(sn[1]));
        close(sn[0]);
        printf("[6] credentials in clear, or without a handshake, refused: OK\n");
    }

    printf("login_auth_test: all checks passed\n");
    return 0;
}
