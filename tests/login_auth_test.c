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
 *  3. PKT_LOGIN with the right salt and hash -> the slot is bound, and the
 *     login's ed25519_pubkey becomes the slot's radio signing key.
 *  4. A second verified PKT_LOGIN for "alice" on another connection takes
 *     the captain over: the new fd is bound, the old one is shut down.
 *  5. (2026.10.31.25) a record without identity.salt (the old unsalted
 *     scheme) is refused, whatever the packet carries: the hash stored in
 *     it, in pass_hash or in the reserved field.
 *  6. (2026.10.31.09) credentials sent in clear instead of masked under the
 *     session key, and a PKT_LOGIN with no handshake, are refused.
 *  7. (2026.10.31.14) a handshake that never logs in is closed once its
 *     deadline passes and its slot is freed; a logged-in one is kept.
 *  8. (2026.10.31.39) two new captains logging in at the same time are
 *     both bound, one slot each (the first took the other's pending
 *     handshake slot and detached him).
 *  9. (2026.10.31.39) a handshake parked on an offline captain's slot is
 *     released when its connection logs in.
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

static PacketLogin mk(int type, const char *name, uint8_t hash_byte, uint8_t salt_byte, uint8_t reserved_byte) {
    PacketLogin p;
    memset(&p, 0, sizeof(p));
    p.type = type;
    strncpy(p.name, name, sizeof(p.name) - 1);
    memset(p.pass_hash, hash_byte, 32);
    memset(p.salt, salt_byte, 16);
    memset(p.reserved, reserved_byte, 32);   /* ignored by the server */
    memset(p.x25519_pubkey, 0x5a, 32);
    memset(p.ed25519_pubkey, 0x5b, 32);
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
        uint8_t bound[32], want[32];
        memset(want, 0x5b, 32);                    /* mk() puts 0x5b in ed25519_pubkey */
        TEST_CHECK(packets_signing_key(s, bound) && memcmp(bound, want, 32) == 0);
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

    /* 5. old unsalted record: identity.hash only, no salt file -> refused */
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

        /* the stored hash in the reserved (ex pass_hash_legacy) field */
        int sv[2]; make_pair_hs(sv);
        PacketLogin old_field = mk(PKT_LOGIN, "oldtimer", 0x11, 0x22, 0x77);
        TEST_CHECK(send_pkt(sv, &old_field) == 1);
        TEST_CHECK(fd_closed(sv[1]));
        close(sv[0]);

        /* the stored hash in pass_hash, which the old scheme also accepted */
        int sw[2]; make_pair_hs(sw);
        PacketLogin in_hash = mk(PKT_LOGIN, "oldtimer", 0x77, 0x22, 0x00);
        TEST_CHECK(send_pkt(sw, &in_hash) == 1);
        TEST_CHECK(fd_closed(sw[1]));
        close(sw[0]);
        TEST_CHECK(slot_of("oldtimer") < 0);

        /* PKT_QUERY: no migration any more, status 3, record untouched */
        int sq[2]; make_pair_hs(sq);
        PacketLogin q = mk(PKT_QUERY, "oldtimer", 0x77, 0x22, 0x77);
        TEST_CHECK(send_pkt(sq, &q) == 0);
        int32_t status = 0;
        TEST_CHECK(read(sq[0], &status, sizeof(status)) == (ssize_t)sizeof(status));
        TEST_CHECK(status == 3);
        char salt_path[1300];
        snprintf(salt_path, sizeof(salt_path), "%s/identity.salt", dir);
        struct stat st_salt;
        TEST_CHECK(stat(salt_path, &st_salt) != 0);     /* not migrated */
        close(sq[0]); close(sq[1]);
        printf("[5] record without salt refused (LOGIN with the stored hash in pass_hash or reserved, QUERY status 3, no migration): OK\n");
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

    /* 7. handshake that never logs in: closed once its deadline passes;
     *    a logged-in connection is left alone */
    {
        int sv[2]; make_pair_hs(sv);                       /* handshake only */
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) if (players[i].socket == sv[1]) slot = i;
        TEST_CHECK(slot >= 0);
        time_t t0 = time(NULL);
        TEST_CHECK(packets_close_stale_handshakes(-1, t0 + 60) == 0);   /* not yet */
        TEST_CHECK(!fd_closed(sv[1]));

        int sl[2]; make_pair_hs(sl);                       /* this one logs in */
        PacketLogin ok = mk(PKT_LOGIN, "alice", 0x42, 0xaa, 0x24);
        TEST_CHECK(send_pkt(sl, &ok) == 0);
        int s_alice = slot_of("alice");
        TEST_CHECK(s_alice >= 0 && players[s_alice].socket == sl[1]);

        TEST_CHECK(packets_close_stale_handshakes(-1, t0 + HANDSHAKE_PENDING_TIMEOUT_S + 1) == 1);
        TEST_CHECK(fd_closed(sv[1]));
        TEST_CHECK(players[slot].socket == 0);             /* slot freed */
        TEST_CHECK(!fd_closed(sl[1]));                     /* logged in: kept */
        TEST_CHECK(players[s_alice].socket == sl[1]);
        TEST_CHECK(packets_close_stale_handshakes(-1, t0 + 10 * HANDSHAKE_PENDING_TIMEOUT_S) == 0);
        close(sv[0]); close(sl[0]); close(sl[1]);
        printf("[7] handshake without login closed after its deadline, logged-in kept: OK\n");
    }

    /* The connections above were closed by hand, not reaped: release their
     * slots as the server's departure path would (the fd numbers are
     * reused by the next socketpairs). */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        players[i].socket = 0;
        players[i].active = 0;
    }

    /* 8. (2026.10.31.39) two new captains logging in at the same time: the
     *    first to send PKT_LOGIN must not take the other's pending handshake
     *    slot (it did: an unnamed slot counted as free even with another
     *    connection on it, and the "takeover" detached that connection). */
    {
        int sb[2];
        make_pair_hs(sb);                       /* carol: lower slot */
        int sa[2];
        make_pair_hs(sa);                       /* bob */
        PacketLogin qb = mk(PKT_QUERY, "bob", 0x61, 0xb1, 0);
        PacketLogin qc = mk(PKT_QUERY, "carol", 0x62, 0xc1, 0);
        int32_t status = 0;
        TEST_CHECK(send_pkt(sa, &qb) == 0);
        TEST_CHECK(read(sa[0], &status, sizeof(status)) == (ssize_t)sizeof(status) && status == 2);
        TEST_CHECK(send_pkt(sb, &qc) == 0);
        TEST_CHECK(read(sb[0], &status, sizeof(status)) == (ssize_t)sizeof(status) && status == 2);
        PacketLogin lb = mk(PKT_LOGIN, "bob", 0x61, 0xb1, 0);
        TEST_CHECK(send_pkt(sa, &lb) == 0);                /* bob first */
        PacketLogin lc = mk(PKT_LOGIN, "carol", 0x62, 0xc1, 0);
        TEST_CHECK(send_pkt(sb, &lc) == 0);
        int s_bob = slot_of("bob"), s_carol = slot_of("carol");
        TEST_CHECK(s_bob >= 0 && s_carol >= 0 && s_bob != s_carol);
        TEST_CHECK(players[s_bob].socket == sa[1]);
        TEST_CHECK(players[s_carol].socket == sb[1]);
        TEST_CHECK(!fd_closed(sa[1]) && !fd_closed(sb[1]));
        int bound_a = 0, bound_b = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (players[i].socket == sa[1]) bound_a++;
            if (players[i].socket == sb[1]) bound_b++;
        }
        TEST_CHECK(bound_a == 1 && bound_b == 1);          /* one slot per connection */
        close(sa[0]);
        close(sa[1]);
        close(sb[0]);
        close(sb[1]);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            players[i].socket = 0;
            players[i].active = 0;
        }
        printf("[8] two new captains logging in at the same time: both bound, one slot each: OK\n");
    }

    /* 9. (2026.10.31.39) a handshake that had to sit on an offline
     *    captain's slot (no unnamed slot left) releases it at login: the
     *    returning captain is bound to his own slot only, the offline
     *    captain's slot keeps its name and no socket. */
    {
        char ghost_names[MAX_CLIENTS];
        for (int i = 0; i < MAX_CLIENTS; i++) {
            ghost_names[i] = (players[i].name[0] == '\0');
            if (ghost_names[i]) snprintf(players[i].name, sizeof(players[i].name), "ghost%d", i);
        }
        int sv[2];
        make_pair_hs(sv);
        int hs_slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (players[i].socket == sv[1]) hs_slot = i;
        TEST_CHECK(hs_slot >= 0);
        int s_bob = slot_of("bob");
        TEST_CHECK(s_bob >= 0 && s_bob != hs_slot);        /* the handshake went elsewhere */
        char hs_name[64];
        snprintf(hs_name, sizeof(hs_name), "%s", players[hs_slot].name);
        TEST_CHECK(hs_name[0] != '\0');                   /* an offline captain's slot */
        PacketLogin ok = mk(PKT_LOGIN, "bob", 0x61, 0xb1, 0);
        TEST_CHECK(send_pkt(sv, &ok) == 0);
        TEST_CHECK(players[s_bob].socket == sv[1]);
        TEST_CHECK(players[hs_slot].socket == 0);          /* released */
        TEST_CHECK(strcmp(players[hs_slot].name, hs_name) == 0);
        int bound = 0;
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (players[i].socket == sv[1]) bound++;
        TEST_CHECK(bound == 1);
        close(sv[0]);
        close(sv[1]);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            players[i].socket = 0;
            players[i].active = 0;
            if (ghost_names[i]) players[i].name[0] = '\0';
        }
        printf("[9] handshake on an offline captain's slot released at login: OK\n");
    }

    printf("login_auth_test: all checks passed\n");
    return 0;
}
