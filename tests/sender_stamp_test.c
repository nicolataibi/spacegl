/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * sender_stamp_test — the sender of a radio message is the authenticated
 * captain of the connection (src/server/packets.c, net.c). Drives the REAL
 * dispatch_packet() (LEGACY wire mode) and the real broadcast_message():
 *
 *  1. "from" forged as another captain -> recipients see the real sender.
 *  2. "from" forged as COMPUTER through a captain's exclusive radio lock ->
 *     not delivered (the station exemption no longer applies).
 *  3. "faction" forged to reach another faction's channel -> not delivered
 *     there; the sender's own faction still receives it.
 *  4. station names are reserved: sanitize_captain_name() refuses them in
 *     any letter case, and a PKT_QUERY with one closes the connection.
 *  5. (2026.10.31.10) a relayed signature is kept only if made with the
 *     signing key bound to the sender's login; otherwise it is removed.
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
#include <poll.h>
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

typedef struct { int sv[2]; } Pair;

static void pair_open(Pair *p) {
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, p->sv) == 0);
}

static void seat(int i, const char *name, int faction, int fd) {
    memset(players[i].name, 0, sizeof(players[i].name));
    strncpy(players[i].name, name, 63);
    players[i].faction = faction;
    players[i].socket = fd;
    players[i].active = 1;
    players[i].radio_lock_target = 0;
    players[i].state.shm_crypto_algo = CRYPTO_NONE;
}

/* Send a cleartext PKT_MESSAGE body through the dispatcher on fd sv[1];
 * sig_key != NULL marks it signed with that public key (bytes only: the
 * relay checks the key, the receiving client checks the signature). */
static void send_msg_signed(Pair *p, const char *from, int faction, int scope, const char *text,
                            const uint8_t *sig_key) {
    PacketMessage *m = calloc(1, sizeof(PacketMessage));
    m->type = PKT_MESSAGE;
    if (sig_key) {
        m->has_signature = 1;
        memset(m->signature, 0x77, sizeof(m->signature));
        memcpy(m->sender_pubkey, sig_key, 32);
    }
    strncpy(m->from, from, 63);
    m->faction = faction;
    m->scope = scope;
    m->length = (int32_t)strlen(text);
    memcpy(m->text, text, strlen(text));
    size_t len = offsetof(PacketMessage, text) + (size_t)m->length - sizeof(int32_t);
    TEST_CHECK(write(p->sv[0], ((char *)m) + sizeof(int32_t), len) == (ssize_t)len);
    PktIO io;
    memset(&io, 0, sizeof(io));
    io.fd = p->sv[1];
    io.epoll_fd = -1;
    TEST_CHECK(dispatch_packet(&io, PKT_MESSAGE) == 0);
    free(m);
}

static void send_msg(Pair *p, const char *from, int faction, int scope, const char *text) {
    send_msg_signed(p, from, faction, scope, text, NULL);
}

/* Next relayed message on the client end within 300 ms; 0 if none. */
static int recv_msg(Pair *p, PacketMessage *out) {
    size_t hdr = offsetof(PacketMessage, text), got = 0, want = hdr;
    memset(out, 0, sizeof(*out));
    while (got < want) {
        struct pollfd pf = { .fd = p->sv[0], .events = POLLIN };
        if (poll(&pf, 1, 300) <= 0) return 0;
        ssize_t n = recv(p->sv[0], ((char *)out) + got, want - got, 0);
        if (n <= 0) return 0;
        got += (size_t)n;
        if (got == hdr && want == hdr) want = hdr + (size_t)out->length;
    }
    return 1;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    spatial_index = calloc(41 * 41 * 41, sizeof(QuadrantIndex));
    TEST_CHECK(spatial_index != NULL);
    for (int i = 0; i < MAX_CLIENTS; i++) pthread_mutex_init(&players[i].socket_mutex, NULL);
    char tmpl[] = "/tmp/sgsender-XXXXXX";
    TEST_CHECK(mkdtemp(tmpl) != NULL);
    server_set_data_dir(tmpl);
    log_init();

    Pair mal, bob, eve;
    pair_open(&mal); pair_open(&bob); pair_open(&eve);
    seat(0, "mallory", 1, mal.sv[1]);
    seat(1, "bob",     2, bob.sv[1]);
    seat(2, "eve",     1, eve.sv[1]);
    PacketMessage *rx = calloc(1, sizeof(PacketMessage));

    /* 1. forged as another captain */
    send_msg(&mal, "bob", 1, 0, "hello from bob?");
    TEST_CHECK(recv_msg(&bob, rx) == 1);
    TEST_CHECK(strcmp(rx->from, "mallory") == 0);
    TEST_CHECK(recv_msg(&mal, rx) == 1 && strcmp(rx->from, "mallory") == 0);   /* loopback */
    TEST_CHECK(recv_msg(&eve, rx) == 1 && strcmp(rx->from, "mallory") == 0);
    printf("[1] forged \"from\" replaced by the authenticated captain: OK\n");

    /* 2. forged as COMPUTER through bob's exclusive lock (locked on eve) */
    players[1].radio_lock_target = 3;          /* bob only hears eve (slot 2) */
    send_msg(&mal, "COMPUTER", 1, 0, "Hull breach, abandon ship");
    TEST_CHECK(recv_msg(&bob, rx) == 0);       /* not through the lock */
    TEST_CHECK(recv_msg(&mal, rx) == 1 && strcmp(rx->from, "mallory") == 0);
    TEST_CHECK(recv_msg(&eve, rx) == 1 && strcmp(rx->from, "mallory") == 0);
    players[1].radio_lock_target = 0;
    printf("[2] forged COMPUTER does not pass an exclusive radio lock: OK\n");

    /* 3. faction scope forged to bob's faction (2); mallory is faction 1 */
    send_msg(&mal, "mallory", 2, SCOPE_FACTION, "faction 2 only");
    TEST_CHECK(recv_msg(&bob, rx) == 0);       /* other faction: not delivered */
    TEST_CHECK(recv_msg(&eve, rx) == 1);       /* mallory's own faction */
    TEST_CHECK(rx->faction == 1);
    TEST_CHECK(recv_msg(&mal, rx) == 1);
    printf("[3] forged faction does not reach another faction: OK\n");

    /* 4. reserved station names */
    TEST_CHECK(sanitize_captain_name("COMPUTER") == 0);
    TEST_CHECK(sanitize_captain_name("computer") == 0);
    TEST_CHECK(sanitize_captain_name("Starbase") == 0);
    TEST_CHECK(sanitize_captain_name("Science") == 0);
    TEST_CHECK(sanitize_captain_name("Computer2") == 1);
    TEST_CHECK(sanitize_captain_name("Picard") == 1);
    {
        Pair q; pair_open(&q);
        /* v2 handshake first, so the reserved name is what closes it */
        PacketHandshake h;
        memset(&h, 0, sizeof(h));
        h.type = PKT_HANDSHAKE;
        h.pubkey_len = HANDSHAKE_V2_MARKER;
        HsV2Client st;
        TEST_CHECK(hs_v2_client_hello(MASTER_SESSION_KEY, h.pubkey, &st));
        size_t hb = sizeof(PacketHandshake) - sizeof(int32_t);
        TEST_CHECK(write(q.sv[0], ((char *)&h) + sizeof(int32_t), hb) == (ssize_t)hb);
        PktIO hio;
        memset(&hio, 0, sizeof(hio));
        hio.fd = q.sv[1];
        hio.epoll_fd = -1;
        TEST_CHECK(dispatch_packet(&hio, PKT_HANDSHAKE) == 0);
        uint8_t ack[4 + HANDSHAKE_V2_REPLY_LEN], sk[32], gv[32];
        TEST_CHECK(read(q.sv[0], ack, sizeof(ack)) == (ssize_t)sizeof(ack));
        TEST_CHECK(hs_v2_client_finish(MASTER_SESSION_KEY, &st, ack + 4, sk, gv));
        PacketLogin p;
        memset(&p, 0, sizeof(p));
        p.type = PKT_QUERY;
        strcpy(p.name, "Tactical");
        memset(p.salt, 0xaa, 16);
        TEST_CHECK(hs_v2_cred_xor(sk, 0, (uint8_t *)&p + offsetof(PacketLogin, pass_hash)));
        size_t body = sizeof(PacketLogin) - sizeof(int32_t);
        TEST_CHECK(write(q.sv[0], ((char *)&p) + sizeof(int32_t), body) == (ssize_t)body);
        PktIO io;
        memset(&io, 0, sizeof(io));
        io.fd = q.sv[1];
        io.epoll_fd = -1;
        TEST_CHECK(dispatch_packet(&io, PKT_QUERY) == 1);
        TEST_CHECK(fcntl(q.sv[1], F_GETFD) == -1 && errno == EBADF);
        close(q.sv[0]);
    }
    printf("[4] station names reserved for captains, any case: OK\n");

    /* 5. signatures: kept only with the key bound to the sender's login */
    {
        uint8_t k_mal[32], k_other[32], zero[32] = {0};
        memset(k_mal, 0x31, 32);
        memset(k_other, 0x32, 32);
        packets_bind_signing_key(0, k_mal);              /* as mallory's PKT_LOGIN would */
        uint8_t got[32];
        TEST_CHECK(packets_signing_key(0, got) && memcmp(got, k_mal, 32) == 0);

        send_msg_signed(&mal, "mallory", 1, 0, "signed by my login key", k_mal);
        TEST_CHECK(recv_msg(&bob, rx) == 1);
        TEST_CHECK(rx->has_signature == 1 && memcmp(rx->sender_pubkey, k_mal, 32) == 0);
        TEST_CHECK(recv_msg(&mal, rx) == 1 && recv_msg(&eve, rx) == 1);

        send_msg_signed(&mal, "mallory", 1, 0, "signed by another key", k_other);
        TEST_CHECK(recv_msg(&bob, rx) == 1);
        TEST_CHECK(rx->has_signature == 0);
        TEST_CHECK(memcmp(rx->sender_pubkey, zero, 32) == 0);
        TEST_CHECK(recv_msg(&mal, rx) == 1 && recv_msg(&eve, rx) == 1);

        /* eve never bound a key: her signatures are removed too */
        send_msg_signed(&eve, "eve", 1, 0, "unbound", k_other);
        TEST_CHECK(recv_msg(&bob, rx) == 1 && rx->has_signature == 0);
        TEST_CHECK(recv_msg(&mal, rx) == 1 && recv_msg(&eve, rx) == 1);

        packets_bind_signing_key(0, zero);               /* all-zero key unbinds */
        TEST_CHECK(!packets_signing_key(0, got));
        printf("[5] signature kept only with the key bound to the login: OK\n");
    }

    free(rx);
    printf("sender_stamp_test: all checks passed\n");
    return 0;
}
