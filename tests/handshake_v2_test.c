/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * handshake_v2_test — include/handshake_v2.h and the server's handshake in
 * src/server/packets.c (real dispatch_packet, LEGACY wire mode):
 *
 *  1. HKDF-SHA256 against RFC 5869 test case 1, X25519 against RFC 7748 6.1.
 *  2. client <-> server exchange: same session key, verify key recovered.
 *  3. refusals: wrong K on either side, every reply field tampered,
 *     small-order (all-zero) client key.
 *  4. K never appears on the wire, and the v1 recovery (bytes 32..63 of the
 *     hello XOR the public magic) no longer yields it.
 *  5. dispatcher: a v1 hello closes the connection; a v2 hello gets the
 *     132-byte reply and the slot holds the client's session key.
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

static void hex32(const char *hex, uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++) sscanf(hex + 2 * i, "%2hhx", &out[i]);
}

static bool contains32(const uint8_t *hay, size_t len, const uint8_t needle[32]) {
    for (size_t i = 0; i + 32 <= len; i++) if (memcmp(hay + i, needle, 32) == 0) return true;
    return false;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    /* 1. vectors */
    {
        uint8_t ikm[22], salt[13], okm[32], want[32];
        memset(ikm, 0x0b, sizeof(ikm));
        for (int i = 0; i < 13; i++) salt[i] = (uint8_t)i;
        char info[11];
        for (int i = 0; i < 10; i++) info[i] = (char)(0xf0 + i);
        info[10] = 0;
        TEST_CHECK(hs_v2_hkdf32(salt, sizeof(salt), ikm, sizeof(ikm), info, okm));
        hex32("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf", want, 32);
        TEST_CHECK(memcmp(okm, want, 32) == 0);

        uint8_t a_priv[32], b_pub[32], z[32];
        hex32("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a_priv, 32);
        hex32("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", b_pub, 32);
        TEST_CHECK(hs_v2_x25519(a_priv, b_pub, z));
        hex32("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", want, 32);
        TEST_CHECK(memcmp(z, want, 32) == 0);
        printf("[1] HKDF (RFC 5869 TC1) and X25519 (RFC 7748 6.1) vectors: OK\n");
    }

    uint8_t K[32], K2[32], GV[32];
    RAND_bytes(K, 32); RAND_bytes(K2, 32); RAND_bytes(GV, 32);

    /* 2. exchange */
    uint8_t hello[256], reply[HANDSHAKE_V2_REPLY_LEN], s_srv[32], s_cli[32], gv[32];
    {
        HsV2Client st;
        TEST_CHECK(hs_v2_client_hello(K, hello, &st));
        TEST_CHECK(hs_v2_server_reply(K, hello, GV, reply, s_srv));
        TEST_CHECK(hs_v2_client_finish(K, &st, reply, s_cli, gv));
        TEST_CHECK(memcmp(s_srv, s_cli, 32) == 0);
        TEST_CHECK(memcmp(gv, GV, 32) == 0);
        printf("[2] exchange: same session key, galaxy verify key recovered: OK\n");
    }

    /* 3. refusals */
    {
        HsV2Client st;
        uint8_t h[256], r[HANDSHAKE_V2_REPLY_LEN], s1[32], s2[32], g[32];
        TEST_CHECK(hs_v2_client_hello(K2, h, &st));
        TEST_CHECK(!hs_v2_server_reply(K, h, GV, r, s1));          /* client lacks K */

        TEST_CHECK(hs_v2_client_hello(K, h, &st));
        TEST_CHECK(hs_v2_server_reply(K2, h, GV, r, s1) == false); /* server lacks K: MAC of hello fails */

        for (int field = 0; field < 4; field++) {                  /* eS, Ns, MAC, G */
            TEST_CHECK(hs_v2_client_hello(K, h, &st));
            TEST_CHECK(hs_v2_server_reply(K, h, GV, r, s1));
            r[field * 32 + 7] ^= 0x01;
            TEST_CHECK(!hs_v2_client_finish(K, &st, r, s2, g));
        }

        /* a server that knows a different K cannot answer a valid client */
        TEST_CHECK(hs_v2_client_hello(K, h, &st));
        TEST_CHECK(hs_v2_server_reply(K, h, GV, r, s1));
        uint8_t forged[32];
        hs_v2_hmac(K2, 32, HS_V2_LABEL_SERVER, st.pub, st.nonce, r, r + 32, r + 96, forged);
        memcpy(r + 64, forged, 32);
        TEST_CHECK(!hs_v2_client_finish(K, &st, r, s2, g));

        /* small-order client key (all zero), with a valid MAC */
        uint8_t zero_key[32] = {0}, nonce[32];
        memset(h, 0, sizeof(h));
        RAND_bytes(nonce, 32);
        memcpy(h + 32, nonce, 32);
        hs_v2_hmac(K, 32, HS_V2_LABEL_CLIENT, zero_key, nonce, NULL, NULL, NULL, h + 64);
        TEST_CHECK(!hs_v2_server_reply(K, h, GV, r, s1));
        printf("[3] wrong K either side, 4 tampered fields, small-order key refused: OK\n");
    }

    /* 4. K off the wire */
    {
        uint8_t wire[256 + HANDSHAKE_V2_REPLY_LEN];
        memcpy(wire, hello, 256);
        memcpy(wire + 256, reply, HANDSHAKE_V2_REPLY_LEN);
        TEST_CHECK(!contains32(wire, sizeof(wire), K));
        uint8_t v1_guess[32];
        const char *magic = "SPACEGL-KEY-VERIFICATION-SIG-32B";
        for (int i = 0; i < 32; i++) v1_guess[i] = hello[32 + i] ^ (uint8_t)magic[i];
        TEST_CHECK(memcmp(v1_guess, K, 32) != 0);
        TEST_CHECK(!contains32(wire, sizeof(wire), GV));
        printf("[4] master key and galaxy verify key absent from the wire: OK\n");
    }

    /* 5. real dispatcher */
    for (int i = 0; i < MAX_CLIENTS; i++) pthread_mutex_init(&players[i].socket_mutex, NULL);
    memcpy(MASTER_SESSION_KEY, K, 32);
    memcpy(GALAXY_VERIFY_KEY, GV, 32);
    {
        int sv[2];
        TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
        PacketHandshake p;
        memset(&p, 0, sizeof(p));
        p.type = PKT_HANDSHAKE;
        p.pubkey_len = 64;                                   /* v1 */
        size_t body = sizeof(PacketHandshake) - sizeof(int32_t);
        TEST_CHECK(write(sv[0], ((char *)&p) + sizeof(int32_t), body) == (ssize_t)body);
        PktIO io;
        memset(&io, 0, sizeof(io));
        io.fd = sv[1];
        io.epoll_fd = -1;
        TEST_CHECK(dispatch_packet(&io, PKT_HANDSHAKE) == 1);
        TEST_CHECK(fcntl(sv[1], F_GETFD) == -1 && errno == EBADF);
        close(sv[0]);

        TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
        HsV2Client st;
        p.pubkey_len = HANDSHAKE_V2_MARKER;
        TEST_CHECK(hs_v2_client_hello(K, p.pubkey, &st));
        TEST_CHECK(write(sv[0], ((char *)&p) + sizeof(int32_t), body) == (ssize_t)body);
        io.fd = sv[1];
        TEST_CHECK(dispatch_packet(&io, PKT_HANDSHAKE) == 0);
        uint8_t ack[4 + HANDSHAKE_V2_REPLY_LEN];
        TEST_CHECK(read(sv[0], ack, sizeof(ack)) == (ssize_t)sizeof(ack));
        int32_t t;
        memcpy(&t, ack, 4);
        TEST_CHECK(t == PKT_HANDSHAKE);
        uint8_t sk[32], g[32];
        TEST_CHECK(hs_v2_client_finish(K, &st, ack + 4, sk, g));
        TEST_CHECK(memcmp(g, GV, 32) == 0);
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) if (players[i].socket == sv[1]) slot = i;
        TEST_CHECK(slot >= 0);
        TEST_CHECK(memcmp(players[slot].session_key, sk, 32) == 0);
        close(sv[0]); close(sv[1]);
        printf("[5] dispatcher: v1 hello dropped, v2 hello answered, session key shared: OK\n");
    }

    printf("handshake_v2_test: all checks passed\n");
    return 0;
}
