/*
 * SPACE GL - 3D LOGIC ENGINE - libFuzzer harness for the server packet
 * dispatcher and the command parser.
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
 * along with the program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * THE TARGET
 * ----------
 * src/server/packets.c:dispatch_packet() — the single entry point for
 * EVERY byte an unauthenticated, untrusted client sends to the server —
 * plus src/server/commands.c:process_command() and its handler table,
 * the second attacker-controlled surface (the command string of a
 * PKT_COMMAND packet). This harness replaces the stub that used to sit
 * at this path: the old one returned 0 unconditionally, so the "fuzzing"
 * claimed in the 2026.10.10.02 changelog entry never executed a single
 * production line.
 *
 * INPUT CONTRACT
 * --------------
 * One fuzz input = one full client SESSION byte stream: a sequence of
 * packets, each an int32 type followed by its fixed-size body
 * (PacketHandshake / PacketLogin / PacketCommand / PacketMessage ...).
 * The harness reproduces the production LEGACY event loop of
 * spacegl_server.c byte for byte: it owns the server-side socket end,
 * reads the 4-byte type itself and hands (io, type) to dispatch_packet()
 * with a legacy PktIO (io->rx == NULL), which then reads the body from
 * the socket with read_all() exactly as the real server does.
 *
 * SAFETY MEASURES (why this can run unattended)
 * ---------------------------------------------
 * 1. SOCKPAIR, NOT A SERVER: the session runs on a socketpair; no port
 *    is ever bound, nothing listens.
 * 2. NON-BLOCKING READS: read_all() is poll(5 s)-bounded; a truncated
 *    body (the fuzzer's favorite mutation) would otherwise cost 5 s per
 *    run. With O_NONBLOCK the read returns EAGAIN/EOF immediately and
 *    the dispatcher takes its "read failed / connection lost" paths —
 *    the same paths a real network failure exercises.
 * 3. SHUT_WR AFTER SEEDING: once the whole stream is in the socket
 *    buffer, the write side is shut so any over-read hits a clean EOF.
 * 4. SANDBOXED PERSISTENCE: server_set_data_dir() points at a private
 *    mkdtemp() directory, so the login path's captains/ file I/O
 *    (identity.hash, salts, algo keys — real production code, linked
 *    from net.c / globals.c) never touches the repository tree.
 * 5. WORLD RESET PER INPUT: players[], the galaxy master and the
 *    torpedo pool are re-zeroed before every session, so one fuzzer
 *    input cannot leak state into the next (the bounded quadrant index
 *    is the single exception: it is a production-sized 41x41x41 volume
 *    that per-quadrant caps keep bounded even under dbgspn spam).
 * 6. STUBS ONLY FOR THE UNLINKED MAIN FILE: spacegl_server.c is not
 *    linked (it is a main, with threads and a real network). Its four
 *    cross-TU exports the linked sources call are stubbed with their
 *    production contract: sync_client_task (generation check + player
 *    activation — what makes a logged-in player accept PKT_COMMAND),
 *    derive_algo_keys / ensure_player_algo_key (HMAC derivation; the
 *    disk persistence is pointless in a temp sandbox) and save_galaxy
 *    (no-op: there is nothing to persist).
 *
 * BUILDS
 * ------
 *  - fuzz_packets     : clang, -fsanitize=fuzzer,address,undefined —
 *                       the real libFuzzer (CI "fuzz" job, and locally:
 *                       ./build/tests/fuzz_packets tests/fuzz/corpus)
 *  - fuzz_corpus_test : any compiler (gcc too), plain main() that runs
 *                       every seed of tests/fuzz/corpus through the
 *                       same LLVMFuzzerTestOneInput under
 *                       ASan/UBSan (ctest, every matrix job): the seed
 *                       corpus itself becomes a regression test.
 *
 * Exit (corpus mode): 0 = all seeds processed, 1 = a seed failed.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <ftw.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <limits.h>
#include <signal.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "server_internal.h"
#include "game_state.h"
#include "packets.h"
#include "network.h"
#include "conn.h"

/* ================================================================== */
/* Globals owned by this TU (production sizes; in the server they    */
/* live in spacegl_server.c / galaxy.c, which are not linked).       */
/* Same list and same adjacency contract as target_bounds_test.c:    */
/* players[] first, spacegl_master right after (the BSS layout the   */
/* 2026.10.06.01 regression corrupts across), canary included.       */
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
uint8_t deep_space_key[32] = {0}; /* net.c encrypt_payload key source */
/* supernova_event lives in globals.c (linked). */

/* Allocated exactly like galaxy.c: one 41x41x41 block. Real allocation
 * (NOT NULL like the pure-parser tests): the dispatcher-reachable
 * command handlers (srs/lrs/orb/jum/...) index spatial_index[q1][q2][q3]
 * directly, so the volume must be addressable end to end. */
QuadrantIndex (*spatial_index)[41][41] = NULL;

/* The thread pool is deliberately NULL: packets.c / net.c fall back to
 * the synchronous paths (sync_client_task inline, broadcast inline),
 * which is exactly what the harness exercises single-threaded. */
threadpool_t *g_pool = NULL;

/* Defined in spacegl_server.c (the main file, not linked); conn.c
 * observes it for its graceful-stop loop. */
volatile sig_atomic_t g_running = 1;

/* ================================================================== */
/* Stubs for the exports of spacegl_server.c (the main file, not     */
/* linked). Each keeps the production contract the caller relies     */
/* on; none of them hides behavior under test — the packet parsing,  */
/* the security checks and the command handlers are all REAL.        */
/* ================================================================== */

/* Production: snapshot the galaxy, sign it, write_all() the ~MB master
 * copy to the client, then finalize activation. Harness: the wire
 * transfer is meaningless over a socketpair nobody reads, so only the
 * activation contract is kept — with the same slot/fd/generation
 * re-checks, so a stale task still no-ops like in production. */
void sync_client_task(void *arg) {
    SyncTask *task = (SyncTask *)arg;
    if (!task) return;
    int slot = task->slot;
    int fd = task->fd;
    if (slot < 0 || slot >= MAX_CLIENTS) { free(task); return; }
    pthread_mutex_lock(&game_mutex);
    if (players[slot].socket != fd || players[slot].generation != task->generation) {
        pthread_mutex_unlock(&game_mutex);
        free(task);
        return;
    }
    /* Production sets active = 1 here (after the rescue check; the
     * rescue branch needs a live galaxy, which the harness does not
     * seed — a zeroed ship state is "safe" by the same rule). */
    players[slot].active = 1;
    pthread_mutex_unlock(&game_mutex);
    free(task);
}

/* Production: load captains/<name>/algo_N.key or derive it and persist
 * it. Harness: derive only (HMAC-SHA256, the exact production salt);
 * persistence is pointless into a per-run temp sandbox. */
void derive_algo_keys(uint8_t *master_key, const char *name,
                      uint8_t target_keys[MAX_CRYPTO_ALGOS + 1][32]) {
    (void)name;
    for (int k = 1; k <= MAX_CRYPTO_ALGOS; k++) {
        char salt[64];
        sprintf(salt, "SPACEGL-ALGO-FREQUENCY-GALAXY-WORMHOLE-SIG-%d", k);
        unsigned int len = 32;
        HMAC(EVP_sha256(), master_key, 32, (uint8_t *)salt, strlen(salt),
             target_keys[k], &len);
    }
}

void ensure_player_algo_key(int p_idx, int k, bool private_mode) {
    if (p_idx < 0 || p_idx >= MAX_CLIENTS) return;
    if (k < 1 || k > MAX_CRYPTO_ALGOS) return;
    char salt[96];
    if (private_mode)
        sprintf(salt, "SPACEGL-ALGO-PRIVATE-%s-SIG-%d",
                players[p_idx].name[0] ? players[p_idx].name : "?", k);
    else
        sprintf(salt, "SPACEGL-ALGO-FREQUENCY-GALAXY-WORMHOLE-SIG-%d", k);
    unsigned int len = 32;
    HMAC(EVP_sha256(), MASTER_SESSION_KEY, 32, (uint8_t *)salt,
         strlen(salt), players[p_idx].algo_keys[k], &len);
}

/* Production: persist the galaxy state (galaxy.c). Harness: no-op. */
void save_galaxy(void) {}
void save_galaxy_async(void) {}

/* Galaxy-population exports of galaxy.c (not linked). Same no-op
 * contract as target_bounds_test.c: every object pool here is empty
 * and zeroed, so there is nothing to spawn, name or re-index. */
void spawn_derelict(int q1, int q2, int q3, double x, double y, double z,
                    int faction, int ship_class, const char *name)
{
    (void)q1; (void)q2; (void)q3; (void)x; (void)y; (void)z;
    (void)faction; (void)ship_class; (void)name;
}
const char *get_species_name(int s) { (void)s; return "TEST"; }
void rebuild_spatial_index(void) {}   /* index stays empty: all pools empty */
void refresh_lrs_grid(void) {}
void update_moon_orbits(void) {}
void update_star_systems(void) {}

/* ================================================================== */
/* World reset: every fuzzer input starts from the same known state. */
/* ================================================================== */

/* The sandbox directory (set once in main / LLVMFuzzerInitialize). */
static char g_sandbox[64];

static int rm_entry(const char *path, const struct stat *sb, int flag, struct FTW *ftw) {
    (void)sb; (void)flag; (void)ftw;
    remove(path);
    return 0;
}

static void reset_world(void) {
    /* The captains/ records written by a previous input are part of the
     * world too (2026.10.31.09): since PKT_LOGIN proves the identity, a
     * previous input that enrolled SEEDCAPTAIN with other credentials
     * would lock every later seed out of the login, and with it out of
     * the command parser, for the rest of the run. */
    if (g_sandbox[0]) {
        char cpath[96];
        snprintf(cpath, sizeof(cpath), "%s/captains", g_sandbox);
        nftw(cpath, rm_entry, 8, FTW_DEPTH | FTW_PHYS);
    }
    memset(players, 0, sizeof(players));
    memset(&spacegl_master, 0, sizeof(spacegl_master));
    memset(players_torpedoes, 0, sizeof(players_torpedoes));
    memset(&supernova_event, 0, sizeof(supernova_event));
    global_tick = 0;
}

/* ================================================================== */
/* The harness.                                                       */
/* ================================================================== */

/* Drive one session stream through the REAL legacy dispatcher loop.
 * Returns the number of packets dispatched (diagnostic only; the
 * fuzzer cares about crashes / leaks, not return values). */
static size_t run_session(const uint8_t *data, size_t size) {
    signal(SIGPIPE, SIG_IGN);
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv) != 0) return 0;

    /* Generous buffers: the dispatcher's replies (handshake ACK, login
     * status, broadcast messages up to 65 KB) must not block a write;
     * nobody drains the client end during the run. */
    const int buf = 4 * 1024 * 1024;
    setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));
    setsockopt(sv[1], SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));
    setsockopt(sv[0], SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
    setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));

    /* Seed the stream. Non-blocking write with a bounded retry: a
     * pathologically huge input simply truncates (a valid mutation). */
    size_t off = 0;
    for (int spin = 0; spin < 64 && off < size; spin++) {
        ssize_t n = send(sv[0], data + off, size - off, 0);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        break;
    }
    /* EOF for the dispatcher's reads: every further over-read returns
     * immediately (0 or EAGAIN) instead of polling for 5 s. */
    shutdown(sv[0], SHUT_WR);
    close(sv[0]);

    /* The legacy PktIO: no staged rx buffer, no session — the body is
     * read from the socket, exactly like the pre-async event loop. */
    PktIO io;
    memset(&io, 0, sizeof(io));
    io.epoll_fd = -1;
    io.fd = sv[1];
    io.conn = NULL;

    size_t dispatched = 0;
    for (;;) {
        int32_t type = 0;
        ssize_t n = read(sv[1], &type, sizeof(type));
        if (n == (ssize_t)sizeof(type)) {
            int closed = dispatch_packet(&io, type);
            dispatched++;
            if (closed) break;
            continue;
        }
        break; /* EOF (0), EAGAIN/ECONNRESET (-1) or fd already closed
                  by the dispatcher's io_close: end of the session. */
    }

    close(sv[1]); /* EBADF if the dispatcher already closed it: fine */
    return dispatched;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    reset_world();
    run_session(data, size);
    return 0;
}

/* ================================================================== */
/* Entry points                                                       */
/* ================================================================== */

#ifndef FUZZ_LIBFUZZER
/* Corpus mode (fuzz_corpus_test): run every seed file of the corpus
 * directory through the harness. This is what ctest runs on every
 * matrix job (gcc included, where libFuzzer does not exist): the
 * committed corpus is itself a regression suite, under ASan/UBSan.
 *
 * Usage: fuzz_corpus_test [corpus_dir]   (default: tests/fuzz/corpus)
 */
int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : "tests/fuzz/corpus";

    if (!spatial_index) {
        spatial_index = calloc(41 * 41 * 41, sizeof(QuadrantIndex));
        if (!spatial_index) {
            fprintf(stderr, "fuzz_corpus_test: cannot allocate the "
                            "quadrant index (production-sized volume)\n");
            return 1;
        }
    }

    /* Sandbox the login path's file I/O before the first session. */
    char tmpl[] = "/tmp/sgfuzz-XXXXXX";
    char *sandbox = mkdtemp(tmpl);
    if (!sandbox) {
        fprintf(stderr, "fuzz_corpus_test: mkdtemp failed\n");
        return 1;
    }
    snprintf(g_sandbox, sizeof(g_sandbox), "%s", sandbox);
    server_set_data_dir(sandbox);
    log_init();

    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "fuzz_corpus_test: corpus dir not found: %s\n", dir);
        return 1;
    }

    size_t seeds = 0, bytes = 0;
    int failed = 0;
    uint8_t *buf = NULL;
    size_t cap = 0, len = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        len = 0;
        for (;;) {
            if (len + 4096 + 1 > cap) {
                cap = cap ? cap * 2 : 262144;
                uint8_t *nb = realloc(buf, cap);
                if (!nb) { fclose(f); closedir(d); free(buf); return 1; }
                buf = nb;
            }
            size_t r = fread(buf + len, 1, cap - len - 1, f);
            len += r;
            if (r == 0) break;
        }
        buf[len] = '\0';
        fclose(f);
        LLVMFuzzerTestOneInput(buf, len);
        seeds++;
        bytes += len;
        printf("corpus: %-28s %8zu bytes OK\n", ent->d_name, len);
    }
    closedir(d);
    free(buf);
    printf("corpus: %zu seed(s), %zu bytes, all processed "
           "(sandbox %s)\n", seeds, bytes, sandbox);
    if (seeds == 0) {
        fprintf(stderr, "fuzz_corpus_test: no seeds in %s\n", dir);
        return 1;
    }
    return failed;
}
#else
/* libFuzzer mode: initialize the sandbox + the quadrant index once.
 * (clang's -fsanitize=fuzzer supplies main and calls this.) */
int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    if (!spatial_index) {
        spatial_index = calloc(41 * 41 * 41, sizeof(QuadrantIndex));
        if (!spatial_index) return -1;
    }
    char tmpl[] = "/tmp/sgfuzz-XXXXXX";
    char *sandbox = mkdtemp(tmpl);
    if (!sandbox) return -1;
    snprintf(g_sandbox, sizeof(g_sandbox), "%s", sandbox);
    server_set_data_dir(sandbox);
    log_init();
    return 0;
}
#endif
