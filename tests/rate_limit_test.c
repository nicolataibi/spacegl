/*
 * SPACE GL - 3D LOGIC ENGINE - Packet rate limiting unit test.
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * TEST SUITE: RATE LIMITING & DOS PROTECTION (Prompt 3 / 2026.10.17.04)
 * ---------------------------------------------------------------------
 * Verifies per-connection token bucket rate limiting (PKT_RATE_LIMIT = 200):
 *   1. Under limit (<200 pkt/sec): all packets accepted and processed.
 *   2. Burst of 200 + 10: 10 extra packets dropped, connection remains open.
 *   3. 200 + 11: exceeds PKT_DROP_THRESHOLD (10), connection closed (io_close).
 *   4. Token refill after 1 second: fresh capacity for up to 200 pkt/sec.
 *   5. Async staged mode (io.rx != NULL) verification of the same rules.
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
void sys_refresh_positions(int s, double t) { (void)s; (void)t; }

/* Helper to wait for the system second to tick */
static void wait_next_second(void) {
    time_t start = time(NULL);
    while (time(NULL) == start) {
        usleep(10000); /* 10 ms sleep */
    }
}

static void drain_socket(int fd) {
    char buf[16384];
    while (recv(fd, buf, sizeof(buf), MSG_DONTWAIT) > 0) {}
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    printf("=== Running rate_limit_test (Prompt 3 Network Security) ===\n");

    /* Allocate spatial index */
    spatial_index = calloc(41 * 41 * 41, sizeof(QuadrantIndex));
    TEST_CHECK(spatial_index != NULL);

    char tmpl[] = "/tmp/sgrate-XXXXXX";
    char *sandbox = mkdtemp(tmpl);
    TEST_CHECK(sandbox != NULL);
    server_set_data_dir(sandbox);
    log_init();

    /* -------------------------------------------------------------
     * TEST 1: Under 200 pkt/sec -> all packets accepted
     * ------------------------------------------------------------- */
    printf("[TEST 1] Testing normal rate (< 200 pkt/sec)...\n");
    wait_next_second();

    int sv[2];
    int r = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    TEST_CHECK(r == 0);
    int buf = 4 * 1024 * 1024;
    setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));
    setsockopt(sv[1], SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));
    setsockopt(sv[0], SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
    setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));

    memset(players, 0, sizeof(players));
    players[0].socket = sv[1];
    players[0].active = 1;
    strcpy(players[0].name, "Tester");

    PktIO io;
    memset(&io, 0, sizeof(io));
    io.fd = sv[1];
    io.epoll_fd = -1;
    io.conn = NULL;
    io.rx = NULL; /* Legacy wire mode */

    PacketCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = PKT_COMMAND;
    strcpy(cmd.cmd, "grd");

    for (int i = 0; i < PKT_RATE_LIMIT; i++) {
        /* Write packet body to sv[0] */
        ssize_t w = write(sv[0], ((char*)&cmd) + sizeof(int), sizeof(PacketCommand) - sizeof(int));
        TEST_CHECK(w == (ssize_t)(sizeof(PacketCommand) - sizeof(int)));

        int res = dispatch_packet(&io, PKT_COMMAND);
        TEST_CHECK(res == 0); /* Accepted */
        TEST_CHECK(players[0].pkt_drop_run == 0);
        drain_socket(sv[0]);
    }
    TEST_CHECK(fcntl(sv[1], F_GETFD) != -1);
    printf("  -> OK: 200 packets accepted under rate limit.\n");

    /* -------------------------------------------------------------
     * TEST 2: Burst of 10 packets over limit -> dropped, socket open
     * ------------------------------------------------------------- */
    printf("[TEST 2] Testing burst of 10 packets over limit (drops)...\n");
    for (int i = 0; i < PKT_DROP_THRESHOLD; i++) {
        ssize_t w = write(sv[0], ((char*)&cmd) + sizeof(int), sizeof(PacketCommand) - sizeof(int));
        TEST_CHECK(w == (ssize_t)(sizeof(PacketCommand) - sizeof(int)));

        int res = dispatch_packet(&io, PKT_COMMAND);
        TEST_CHECK(res == 1); /* Dropped */
        TEST_CHECK(players[0].pkt_drop_run == (uint32_t)(i + 1));
        TEST_CHECK(fcntl(sv[1], F_GETFD) != -1); /* Socket still open */
        drain_socket(sv[0]);
    }
    printf("  -> OK: 10 extra packets dropped, drop run = 10, socket still open.\n");

    /* -------------------------------------------------------------
     * TEST 3: 11th packet over limit -> exceeds threshold, closed
     * ------------------------------------------------------------- */
    printf("[TEST 3] Testing 11th packet (exceeds drop threshold -> close)...\n");
    {
        ssize_t w = write(sv[0], ((char*)&cmd) + sizeof(int), sizeof(PacketCommand) - sizeof(int));
        TEST_CHECK(w == (ssize_t)(sizeof(PacketCommand) - sizeof(int)));

        int res = dispatch_packet(&io, PKT_COMMAND);
        TEST_CHECK(res == 1); /* Closed by io_close */

        /* Verify socket sv[1] was closed */
        int fd_check = fcntl(sv[1], F_GETFD);
        TEST_CHECK(fd_check == -1 && errno == EBADF);
    }
    close(sv[0]);
    printf("  -> OK: connection closed and socket invalidated after exceeding drop threshold.\n");

    /* -------------------------------------------------------------
     * TEST 4: Token refill after 1 second -> accepts up to 200 again
     * ------------------------------------------------------------- */
    printf("[TEST 4] Testing token refill after 1 second...\n");
    wait_next_second();

    int sv2[2];
    r = socketpair(AF_UNIX, SOCK_STREAM, 0, sv2);
    TEST_CHECK(r == 0);
    setsockopt(sv2[0], SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));
    setsockopt(sv2[1], SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));
    setsockopt(sv2[0], SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
    setsockopt(sv2[1], SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));

    memset(&players[0], 0, sizeof(players[0]));
    players[0].socket = sv2[1];
    players[0].active = 1;
    strcpy(players[0].name, "Tester2");

    io.fd = sv2[1];
    io.epoll_fd = -1;
    io.conn = NULL;
    io.rx = NULL;

    for (int i = 0; i < PKT_RATE_LIMIT; i++) {
        ssize_t w = write(sv2[0], ((char*)&cmd) + sizeof(int), sizeof(PacketCommand) - sizeof(int));
        TEST_CHECK(w == (ssize_t)(sizeof(PacketCommand) - sizeof(int)));

        int res = dispatch_packet(&io, PKT_COMMAND);
        TEST_CHECK(res == 0); /* Accepted */
        TEST_CHECK(players[0].pkt_drop_run == 0);
        drain_socket(sv2[0]);
    }
    TEST_CHECK(fcntl(sv2[1], F_GETFD) != -1);
    close(sv2[0]);
    close(sv2[1]);
    printf("  -> OK: full 200 packet capacity restored after 1 second refill.\n");

    /* -------------------------------------------------------------
     * TEST 5: Async mode (io.rx != NULL) verification
     * ------------------------------------------------------------- */
    printf("[TEST 5] Testing rate limiting in async mode (io.rx != NULL)...\n");
    wait_next_second();

    int sv3[2];
    r = socketpair(AF_UNIX, SOCK_STREAM, 0, sv3);
    TEST_CHECK(r == 0);

    memset(&players[0], 0, sizeof(players[0]));
    players[0].socket = sv3[1];
    players[0].active = 1;
    strcpy(players[0].name, "Tester3");

    io.fd = sv3[1];
    io.epoll_fd = -1;
    io.conn = NULL;

    for (int i = 0; i < PKT_RATE_LIMIT; i++) {
        PacketCommand c = cmd;
        io.rx = ((uint8_t*)&c) + sizeof(int);
        io.rx_len = sizeof(PacketCommand) - sizeof(int);

        int res = dispatch_packet(&io, PKT_COMMAND);
        TEST_CHECK(res == 0);
        drain_socket(sv3[0]);
    }
    printf("  -> OK: async 200 packets accepted.\n");

    /* 10 drops */
    for (int i = 0; i < PKT_DROP_THRESHOLD; i++) {
        PacketCommand c = cmd;
        io.rx = ((uint8_t*)&c) + sizeof(int);
        io.rx_len = sizeof(PacketCommand) - sizeof(int);

        int res = dispatch_packet(&io, PKT_COMMAND);
        TEST_CHECK(res == 1);
        TEST_CHECK(fcntl(sv3[1], F_GETFD) != -1);
        drain_socket(sv3[0]);
    }
    printf("  -> OK: async 10 drops processed.\n");

    /* 11th drops and closes */
    {
        PacketCommand c = cmd;
        io.rx = ((uint8_t*)&c) + sizeof(int);
        io.rx_len = sizeof(PacketCommand) - sizeof(int);

        int res = dispatch_packet(&io, PKT_COMMAND);
        TEST_CHECK(res == 1);
        TEST_CHECK(fcntl(sv3[1], F_GETFD) == -1 && errno == EBADF);
    }
    close(sv3[0]);
    printf("  -> OK: async 11th packet closed connection.\n");

    free(spatial_index);
    printf("=== ALL rate_limit_test CHECKS PASSED ===\n");
    return 0;
}
