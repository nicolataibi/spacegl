/*
 * server_tick_bench.c — server tick rate benchmark (2026.10.18.04).
 *
 * Measures the average time per tick of the server's game loop: the
 * EXACT sequence the production loop runs under game_mutex
 * (spacegl_server.c game_loop_thread):
 *
 *     pthread_mutex_lock(&game_mutex);
 *     update_game_logic();         // src/server/logic.c
 *     global_tick++;               // caller increments BEFORE the call
 *     spacegl_master.frame_id++;   // caller increments BEFORE the call
 *     pthread_mutex_unlock(&game_mutex);
 *
 * (order as in the loop: the tick counter is bumped, then the logic
 * runs against the NEW tick value; this benchmark replicates that order
 * — increment first, call second, per tick).
 *
 * What it exercises (per tick):
 *   - update_game_logic(): the 16-player state machine (shields,
 *     energy, probes, lock validation, nav states), the per-quadrant
 *     update-packet build (every object type in the player's quadrant,
 *     beam aggregation), logic_generate_events() (torpedoes, supernova
 *     lottery, monster attacks, environmental hazards, starbase
 *     defense), and the two rebuild_spatial_index() calls inside
 *     logic_events.c (the dirty-quadrant dynamic re-insert of ~15.6k
 *     objects per call).
 *   - OpenMP parallelism: logic_events.c parallelizes the torpedo,
 *     monster, NPC-AI and BPNBS-decay loops (the same pragmas as in
 *     the server binary; link OpenMP so they behave as in production).
 *
 * What it does NOT measure (documented, on purpose):
 *   - sign_galaxy_data() + send_pending_updates(): network/crypto I/O.
 *     The 8.0 ms threshold is HALF the 16.7 ms frame budget precisely
 *     to leave headroom for these; measuring them here would need real
 *     sockets and would conflate I/O latency with simulation cost.
 *   - refresh_lrs_grid() (galaxy.c, once per second inside the tick):
 *     it lives in galaxy.c which cannot be linked (it owns the
 *     production globals + the persistence stack), so this TU provides
 *     a no-op stub. Its cost (a 40^3 integer pack, ~1-3 ms once per
 *     second) is well inside the headroom the threshold leaves.
 *   - The thread pool: in this codebase the pool serves async save and
 *     network message tasks (galaxy.c/net.c), NOT the tick itself, so
 *     the tick is single-threaded except for the OpenMP loops above.
 *
 * Scene ("worst case, stable"):
 *   - 16/16 players active (all MAX_CLIENTS slots), full vitals,
 *     shields below target (regen path runs), 3 active probes each,
 *     an NPC lock each, 4 active beams each.
 *   - ALL dynamic arrays at production capacity: 4000 NPCs (8 in each
 *     of 500 "hot" quadrants = the MAX_Q_NPC cap), 1000 monsters
 *     (passive types 1-29: the type-30 resonance beam would home onto
 *     the players and destabilize a 1000-tick window), 1000 comets,
 *     3000 asteroids, 3000 derelicts, 1500 mines, 1000 buoys,
 *     1000 platforms, 1000 rifts.
 *   - 64/64 player torpedoes active (4 per player quadrant; they
 *     explode at the quadrant boundary within the first ~3 s of the
 *     run, so the torpedo pass is exercised at peak load and then
 *     drops to its steady-state cost).
 *   - ALL 63 static arrays at capacity (4000 stars, 2000 planets,
 *     1000 of every other type): the per-quadrant update build copies
 *     every one of them for each player.
 *   - Hostile-faction NPCs in the players' 16 quadrants: they fight
 *     (attack-run beams, Alliance homing torpedoes), so the combat
 *     damage path runs; the players' shield economy (10000/facet +
 *     regen) absorbs the run without deaths — the end-of-run scene
 *     check below verifies every player is still alive, so a logic
 *     change that breaks the balance fails the benchmark instead of
 *     silently mutating the scene.
 *
 * Threshold: the AVERAGE tick time must stay under threshold_ms and
 * the 99th percentile under p99_ms from
 * tests/server_tick_thresholds.txt (re-baselined 2026.10.20 to
 * 15.3 / 31.6 = measured x 1.5 on the slowest GATED CI hardware —
 * GitHub ubuntu-latest 2-vCPU shared runner for the average, COPR
 * Fedora 45 builder for the p99 tail; see the file header for the
 * full rationale. The original 8.0 / 12.0 values were derived from
 * the 16.7 ms frame budget and assumed the tick runs at ~half a frame
 * on every machine, which a 2-vCPU shared CI runner does not meet).
 * The gate applies ONLY in optimized, non-instrumented builds:
 * the CI matrix's Debug jobs carry ASan/UBSan (2-3x slowdown, distorted
 * tick times), so CMake runs them with --report-only (numbers still
 * printed, no gate). The file is rewritten by `--update-thresholds`
 * (measured avg/p99 x 1.5, rounded UP to 0.1 ms) when a developer
 * explicitly accepts a new baseline after a refactoring.
 *
 * This is a performance REGRESSION test: on the slowest gated hardware
 * a refactoring that doubles the tick time fails the average gate,
 * a 2x tail regression fails p99, and every regression is at least
 * visible in the CI log numbers.
 *
 * Determinism: global_tick starts at 1 and the tick seed is
 * ai_seed = global_tick (as in the loop), so rand_r()-driven branches
 * (supernova lottery, NPC patrol legs) are reproducible run to run.
 *
 * Usage:
 *   server_tick_bench [--ticks N] [--warmup N]
 *                     [--threshold-file PATH] [--update-thresholds]
 *                     [--report-only]
 *
 * Exit: 0 = pass, 1 = threshold exceeded (or scene destabilized).
 *       --report-only prints the same stats but never applies the
 *       threshold gate (for sanitizer/unoptimized builds, where the
 *       numbers are distorted by the instrumentation). The scene
 *       stability check still applies: a destabilized scene makes the
 *       numbers meaningless and fails in every mode.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <stddef.h>
#include <pthread.h>

#include "game_config.h"
#include "server_internal.h"
#include "quad_index.h"

/* ================================================================== */
/* Globals owned by this TU (production sizes; in the server they    */
/* live in galaxy.c / spacegl_server.c, which are not linked).       */
/* globals.c IS linked: it provides supernova_event, g_data_dir and  */
/* server_data_path, so those are NOT defined here.                  */
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

/* Allocated exactly like galaxy.c: one 41x41x41 block. */
QuadrantIndex (*spatial_index)[41][41] = NULL;

/* ================================================================== */
/* rebuild_spatial_index() — MIRROR of src/server/galaxy.c ---------- */
/*                                                                    */
/* logic_events.c calls it TWICE per tick (after the torpedo late    */
/* sync and after the starbase-defense pass), so it is a first-class  */
/* part of the tick cost and must not be stubbed away. galaxy.c       */
/* cannot be linked (it owns the production globals plus the         */
/* persistence/crypto stack), so this TU carries a line-for-line      */
/* mirror of the production function, INCLUDING the dirty-quadrant    */
/* mechanism and the [LRS-3] diagnostic block. KEEP IN SYNC with      */
/* galaxy.c:rebuild_spatial_index() — a divergence here silently      */
/* un-measures part of the tick.                                      */
/* ================================================================== */

/* (mirror of galaxy.c) Optimization: track only quadrants that contain
 * dynamic objects to avoid triple-loop resets. */
typedef struct { uint16_t q1, q2, q3; } DirtyQuad;
static DirtyQuad dirty_quads[10000];
static int dirty_count = 0;

static void mark_quad_dirty(int q1, int q2, int q3) {
    QuadrantIndex *q = &spatial_index[q1][q2][q3];
    if (q->npc_count == 0 && q->player_count == 0 && q->comet_count == 0 && q->asteroid_count == 0 &&
        q->derelict_count == 0 && q->mine_count == 0 && q->buoy_count == 0 && q->platform_count == 0 &&
        q->rift_count == 0 && q->monster_count == 0 && q->torpedo_count == 0) {
        if (dirty_count < 10000) {
            dirty_quads[dirty_count++] = (DirtyQuad){(uint16_t)q1, (uint16_t)q2, (uint16_t)q3};
        }
    }
}

void rebuild_spatial_index() {
    if (!spatial_index) {
        spatial_index = calloc(41 * 41 * 41, sizeof(QuadrantIndex));
        if (!spatial_index) { fprintf(stderr, "FAIL: spatial_index alloc\n"); exit(1); }
    }

    /* Reset ONLY the dynamic-object counts in dirty quadrants.
     * Static objects (stars, planets, bases, nebulae, cosmic structures,
     * etc.) are placed once by the static insertion and never move, so
     * their counts are preserved. Only truly mobile objects are
     * re-inserted below. */
    for (int i = 0; i < dirty_count; i++) {
        QuadrantIndex *q = &spatial_index[dirty_quads[i].q1][dirty_quads[i].q2][dirty_quads[i].q3];
        q->npc_count      = 0;
        q->player_count   = 0;
        q->torpedo_count  = 0;
        q->monster_count  = 0;
        q->comet_count    = 0;
        q->asteroid_count = 0;
        q->derelict_count = 0;
        q->mine_count     = 0;
        q->buoy_count     = 0;
        q->platform_count = 0;
        q->rift_count     = 0;
    }
    dirty_count = 0;

    /* Re-insert only truly dynamic objects (those that can change
     * quadrant each tick). */
    for (int n = 0; n < MAX_NPC; n++) {
        if (npcs[n].active) {
            if (IS_Q_VALID(npcs[n].q1, npcs[n].q2, npcs[n].q3)) {
                mark_quad_dirty(npcs[n].q1, npcs[n].q2, npcs[n].q3);
                QuadrantIndex *q = &spatial_index[npcs[n].q1][npcs[n].q2][npcs[n].q3];
                if (q->npc_count < MAX_Q_NPC) {
                    q->npcs[q->npc_count++] = &npcs[n];
                }
            }
        }
    }
    for (int m = 0; m < MAX_MONSTERS; m++) {
        if (monsters[m].active) {
            if (IS_Q_VALID(monsters[m].q1, monsters[m].q2, monsters[m].q3)) {
                mark_quad_dirty(monsters[m].q1, monsters[m].q2, monsters[m].q3);
                QuadrantIndex *q = &spatial_index[monsters[m].q1][monsters[m].q2][monsters[m].q3];
                if (q->monster_count < MAX_Q_MONSTERS) {
                    q->monsters[q->monster_count++] = &monsters[m];
                }
            }
        }
    }
    for (int u = 0; u < MAX_CLIENTS; u++) {
        if (players[u].active && players[u].name[0] != '\0') {
            if (IS_Q_VALID(players[u].state.q1, players[u].state.q2, players[u].state.q3)) {
                mark_quad_dirty(players[u].state.q1, players[u].state.q2, players[u].state.q3);
                QuadrantIndex *q = &spatial_index[players[u].state.q1][players[u].state.q2][players[u].state.q3];
                if (q->player_count < MAX_Q_PLAYERS) {
                    q->players[q->player_count++] = &players[u];
                }
            }
        }
    }

    /* === DIAG [LRS-3] (mirror of galaxy.c): log each player whose
     * quadrant changed this tick. SG_TRACE is a no-op at the default
     * log threshold (INFO), so this costs one branch per player. === */
    {
        static int last_q1[MAX_CLIENTS], last_q2[MAX_CLIENTS], last_q3[MAX_CLIENTS];
        static int last_diag_init = 0;
        if (!last_diag_init) {
            for (int u = 0; u < MAX_CLIENTS; u++) { last_q1[u] = -1; last_q2[u] = -1; last_q3[u] = -1; }
            last_diag_init = 1;
        }
        for (int u = 0; u < MAX_CLIENTS; u++) {
            if (!players[u].active || players[u].name[0] == '\0') continue;
            int q1 = players[u].state.q1, q2 = players[u].state.q2, q3 = players[u].state.q3;
            if (q1 == last_q1[u] && q2 == last_q2[u] && q3 == last_q3[u]) continue;
            last_q1[u] = q1; last_q2[u] = q2; last_q3[u] = q3;
            if (!IS_Q_VALID(q1, q2, q3)) continue;
            QuadrantIndex *q = &spatial_index[q1][q2][q3];
            char ids[192]; int off = 0;
            for (int j = 0; j < q->player_count && j < MAX_Q_PLAYERS && off < (int)sizeof(ids) - 16; j++)
                off += snprintf(ids + off, sizeof(ids) - (size_t)off, "%d,", (int)(q->players[j] - players) + 1);
            SG_TRACE3(SG_CAT_LRS,
                "spatial: player %s(slot %d) now in Q-%d-%d-%d: quad players=%d[%s] npcs=%d",
                players[u].name, u, q1, q2, q3, q->player_count, ids, q->npc_count);
        }
    }

    for (int t = 0; t < MAX_GLOBAL_TORPEDOES; t++) {
        if (players_torpedoes[t].active) {
            if (IS_Q_VALID(players_torpedoes[t].q1, players_torpedoes[t].q2, players_torpedoes[t].q3)) {
                mark_quad_dirty(players_torpedoes[t].q1, players_torpedoes[t].q2, players_torpedoes[t].q3);
                QuadrantIndex *q = &spatial_index[players_torpedoes[t].q1][players_torpedoes[t].q2][players_torpedoes[t].q3];
                if (q->torpedo_count < MAX_Q_TORPEDOES) {
                    q->torpedoes[q->torpedo_count++] = &players_torpedoes[t];
                }
            }
        }
    }
    for (int c = 0; c < MAX_COMETS; c++) {
        if (comets[c].active) {
            if (IS_Q_VALID(comets[c].q1, comets[c].q2, comets[c].q3)) {
                mark_quad_dirty(comets[c].q1, comets[c].q2, comets[c].q3);
                QuadrantIndex *q = &spatial_index[comets[c].q1][comets[c].q2][comets[c].q3];
                if (q->comet_count < MAX_Q_COMETS) {
                    q->comets[q->comet_count++] = &comets[c];
                }
            }
        }
    }
    for (int a = 0; a < MAX_ASTEROIDS; a++) {
        if (asteroids[a].active) {
            if (IS_Q_VALID(asteroids[a].q1, asteroids[a].q2, asteroids[a].q3)) {
                mark_quad_dirty(asteroids[a].q1, asteroids[a].q2, asteroids[a].q3);
                QuadrantIndex *q = &spatial_index[asteroids[a].q1][asteroids[a].q2][asteroids[a].q3];
                if (q->asteroid_count < MAX_Q_ASTEROIDS) {
                    q->asteroids[q->asteroid_count++] = &asteroids[a];
                }
            }
        }
    }
    for (int d = 0; d < MAX_DERELICTS; d++) {
        if (derelicts[d].active) {
            if (IS_Q_VALID(derelicts[d].q1, derelicts[d].q2, derelicts[d].q3)) {
                mark_quad_dirty(derelicts[d].q1, derelicts[d].q2, derelicts[d].q3);
                QuadrantIndex *q = &spatial_index[derelicts[d].q1][derelicts[d].q2][derelicts[d].q3];
                if (q->derelict_count < MAX_Q_DERELICTS) {
                    q->derelicts[q->derelict_count++] = &derelicts[d];
                }
            }
        }
    }
    for (int m = 0; m < MAX_MINES; m++) {
        if (mines[m].active) {
            if (IS_Q_VALID(mines[m].q1, mines[m].q2, mines[m].q3)) {
                mark_quad_dirty(mines[m].q1, mines[m].q2, mines[m].q3);
                QuadrantIndex *q = &spatial_index[mines[m].q1][mines[m].q2][mines[m].q3];
                if (q->mine_count < MAX_Q_MINES) {
                    q->mines[q->mine_count++] = &mines[m];
                }
            }
        }
    }
    for (int b = 0; b < MAX_BUOYS; b++) {
        if (buoys[b].active) {
            if (IS_Q_VALID(buoys[b].q1, buoys[b].q2, buoys[b].q3)) {
                mark_quad_dirty(buoys[b].q1, buoys[b].q2, buoys[b].q3);
                QuadrantIndex *q = &spatial_index[buoys[b].q1][buoys[b].q2][buoys[b].q3];
                if (q->buoy_count < MAX_Q_BUOYS) {
                    q->buoys[q->buoy_count++] = &buoys[b];
                }
            }
        }
    }
    for (int pl = 0; pl < MAX_PLATFORMS; pl++) {
        if (platforms[pl].active) {
            if (IS_Q_VALID(platforms[pl].q1, platforms[pl].q2, platforms[pl].q3)) {
                mark_quad_dirty(platforms[pl].q1, platforms[pl].q2, platforms[pl].q3);
                QuadrantIndex *q = &spatial_index[platforms[pl].q1][platforms[pl].q2][platforms[pl].q3];
                if (q->platform_count < MAX_Q_PLATFORMS) {
                    q->platforms[q->platform_count++] = &platforms[pl];
                }
            }
        }
    }
    for (int r = 0; r < MAX_RIFTS; r++) {
        if (rifts[r].active) {
            if (IS_Q_VALID(rifts[r].q1, rifts[r].q2, rifts[r].q3)) {
                mark_quad_dirty(rifts[r].q1, rifts[r].q2, rifts[r].q3);
                QuadrantIndex *q = &spatial_index[rifts[r].q1][rifts[r].q2][rifts[r].q3];
                if (q->rift_count < MAX_Q_RIFTS) {
                    q->rifts[q->rift_count++] = &rifts[r];
                }
            }
        }
    }
}

/* ================================================================== */
/* Stubs for the server I/O / persistence helpers the linked TUs     */
/* call (galaxy.c / net.c are not linked). The refresh_lrs_grid      */
/* stub is the one intentional deviation, documented in the header:  */
/* its 40^3 pack runs once per SECOND (tick % 60) and its cost sits  */
/* in the headroom the threshold leaves for I/O overhead.            */
/* ================================================================== */

static int bench_msgs_sent = 0;

void send_server_msg(int p_idx, const char *from, const char *text) {
    (void)from; (void)text;
    if (p_idx < 0 || p_idx >= MAX_CLIENTS) return;
    bench_msgs_sent++;
}

void save_galaxy(void) {}
void save_galaxy_async(void) {}
void spawn_derelict(int q1, int q2, int q3, double x, double y, double z,
                    int faction, int ship_class, const char *name)
{
    (void)q1; (void)q2; (void)q3; (void)x; (void)y; (void)z;
    (void)faction; (void)ship_class; (void)name;
}
const char *get_species_name(int s) { (void)s; return "BENCH"; }
void refresh_lrs_grid(void) {}
void update_moon_orbits(void) {}
void update_star_systems(void) {}
void sys_refresh_positions(int s, double t) { (void)s; (void)t; }
int write_all(int fd, const void *buf, size_t len) { (void)fd; (void)buf; return (int)len; }

/* ================================================================== */
/* Scene construction ("worst case, stable")                         */
/* ================================================================== */

/* One static type as filled by the bench: the array (owned above) +
 * the field offsets the fill needs. The production insertion table
 * (quad_index.c) only carries the q/active offsets, so id/x/y/z are
 * filled through this local table and the REAL production insertion
 * (quad_index_insert_static_all) is then called on the result. */
typedef struct {
    const char *name;
    void *arr;
    size_t obj_size;
    size_t total;
    ptrdiff_t off_id;
    ptrdiff_t off_q1;
    ptrdiff_t off_q2;
    ptrdiff_t off_q3;
    ptrdiff_t off_x;
    ptrdiff_t off_y;
    ptrdiff_t off_z;
    ptrdiff_t off_active;
} StaticFill;

#define SF(TY, ARR) { #TY, (void *)(ARR), sizeof((ARR)[0]), sizeof(ARR) / sizeof((ARR)[0]), \
    offsetof(TY, id), offsetof(TY, q1), offsetof(TY, q2), offsetof(TY, q3), \
    offsetof(TY, x), offsetof(TY, y), offsetof(TY, z), offsetof(TY, active) }

static const StaticFill s_static_fills[] = {
    SF(NPCPlanet, planets),
    SF(NPCBase, bases),
    SF(NPCStar, stars_data),
    SF(NPCBlackHole, black_holes),
    SF(NPCNebula, nebulas),
    SF(NPCPulsar, pulsars),
    SF(NPCStorm, storms),
    SF(NPCDyson, dysons),
    SF(NPCHub, hubs),
    SF(NPCRelic, relics),
    SF(NPCRupture, ruptures),
    SF(NPCSatellite, satellites),
    SF(NPCQuasar, quasars),
    SF(NPCArtifact, artifacts),
    SF(NPCWarpGate, warp_gates),
    SF(NPCNeutronStar, neutron_stars),
    SF(NPCMegaStructure, mega_structs),
    SF(NPCDarkCloud, dark_clouds),
    SF(NPCSingularity, singularities),
    SF(NPCPlasmaStorm, plasma_storms),
    SF(NPCOrbitalRing, orbital_rings),
    SF(NPCTimeAnomaly, time_anomalies),
    SF(NPCVoidCrystal, void_crystals),
    SF(NPCSubspaceAnomaly, subspace_anomalies),
    SF(NPCDiffuseNebula, diffuse_nebulae),
    SF(NPCDarkNebula, dark_nebulae),
    SF(NPCPlanetaryNebula, planetary_nebulae),
    SF(NPCSNR, snrs),
    SF(NPCGMC, gmcs),
    SF(NPCInterstellarFilament, interstellar_filaments),
    SF(NPCInterstellarBubble, interstellar_bubbles),
    SF(NPCBokGlobule, bok_globules),
    SF(NPCClumpCore, clump_cores),
    SF(NPCAccretionDisk, accretion_disks),
    SF(NPCRelativisticJet, relativistic_jets),
    SF(NPCShockWave, shock_waves),
    SF(NPCStellarBowShock, stellar_bow_shocks),
    SF(NPCCosmicVoid, cosmic_voids),
    SF(NPCCosmicFilament, cosmic_filaments),
    SF(NPCEventHorizon, event_horizons),
    SF(NPCKilonova, kilonovae),
    SF(NPCGravLens, grav_lenses),
    SF(NPCGRB, grbs),
    SF(NPCGravWave, grav_waves),
    SF(NPCProtoplanetaryDisk, protoplanetary_disks),
    SF(NPCDebrisDisk, debris_disks),
    SF(NPCPlanetesimal, planetesimals),
    SF(NPCRoguePlanet, rogue_planets),
    SF(NPCBrownDwarf, brown_dwarfs),
    SF(NPCISO, isos),
    SF(NPCMagReconn, mag_reconns),
    SF(NPCCurrentSheet, current_sheets),
    SF(NPCHeliosphere, heliospheres),
    SF(NPCTermShock, term_shocks),
    SF(NPCMagnetosphere, magnetospheres),
    SF(NPCCosmicString, cosmic_strings),
    SF(NPCDomainWall, domain_walls),
    SF(NPCDMHalo, dm_halos),
    SF(NPCIGM, igms),
    SF(NPCCGM, cgms),
    SF(NPCLymanAlpha, lyman_alphas),
    SF(NPCCMB, cmbs),
    SF(NPCCosmicFeature, cosmic_features),
};
#define NUM_STATIC_FILLS (sizeof(s_static_fills) / sizeof(s_static_fills[0]))

/* Hot-quadrant geometry: h in [0, 512) -> (q1, q2, q3). 512 quadrants
 * keep the dirty-quadrant list far under its 10000 cap, so the
 * reset/re-insert cycle stays bounded for the whole run. */
static inline void hot_quad(int h, int *q1, int *q2, int *q3) {
    *q1 = (h % 40) + 1;
    *q2 = ((h / 40) % 40) + 1;
    *q3 = ((h / 1600) % 40) + 1;
}

static int bench_static_objects = 0;

static void build_scene(void) {
    int h, q1, q2, q3;

    /* 1. The 63 static arrays: one object per type per quadrant index,
     * at a FAR corner (5..14) of the quadrant. The players sit at the
     * opposite corner (30.0), so rupture/storm/nnebula proximity
     * effects (range <= 2.5) never trigger in the run. */
    for (size_t t = 0; t < NUM_STATIC_FILLS; t++) {
        const StaticFill *sf = &s_static_fills[t];
        for (size_t i = 0; i < sf->total; i++) {
            char *o = (char *)sf->arr + i * sf->obj_size;
            h = (int)i;
            hot_quad(h, &q1, &q2, &q3);
            *(int *)(o + sf->off_id) = (int)i;
            *(int *)(o + sf->off_q1) = q1;
            *(int *)(o + sf->off_q2) = q2;
            *(int *)(o + sf->off_q3) = q3;
            double p = 5.0 + (double)(i % 10);
            /* memcpy (not a *(double *) cast): the offset-based write
             * must not rely on char* <-> double* aliasing (cppcheck
             * portability: invalidPointerCast). */
            memcpy(o + sf->off_x, &p, sizeof p);
            memcpy(o + sf->off_y, &p, sizeof p);
            memcpy(o + sf->off_z, &p, sizeof p);
            *(int *)(o + sf->off_active) = 1;
            bench_static_objects++;
        }
    }
    cosmic_feature_count = MAX_COSMIC_FEATURES;

    /* Real production insertion of every static object into the
     * (zeroed) 41^3 index — the same call init_static_spatial_index()
     * makes at server startup. */
    if (!spatial_index) {
        spatial_index = calloc(41 * 41 * 41, sizeof(QuadrantIndex));
        if (!spatial_index) { fprintf(stderr, "FAIL: spatial_index alloc\n"); exit(1); }
    }
    quad_index_insert_static_all(spatial_index);

    /* 2. Players: all 16 slots active, one per hot quadrant h = 0..15,
     * at the far corner (30, 30, 30), full vitals, shields below
     * target so the regen path runs every tick. socket = 3: the
     * update-packet builder requires a non-zero socket; nothing is
     * ever written to it (send_pending_updates is not linked). */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        ConnectedPlayer *p = &players[i];
        memset(p, 0, sizeof(*p));
        pthread_mutex_init(&p->socket_mutex, NULL);
        hot_quad(i, &p->state.q1, &p->state.q2, &p->state.q3);
        p->socket = 3;
        snprintf(p->name, sizeof(p->name), "BENCH_%d", i);
        p->faction = i % 2;              /* hostile to the odd/even neighbor */
        p->ship_class = 0;
        p->active = 1;
        p->state.s1 = p->state.s2 = p->state.s3 = 30.0;
        p->gx = (p->state.q1 - 1) * QUADRANT_SIZE + p->state.s1;
        p->gy = (p->state.q2 - 1) * QUADRANT_SIZE + p->state.s2;
        p->gz = (p->state.q3 - 1) * QUADRANT_SIZE + p->state.s3;
        p->nav_state = NAV_STATE_IDLE;
        p->last_q1 = p->state.q1; p->last_q2 = p->state.q2; p->last_q3 = p->state.q3;

        SpaceGLGame *st = &p->state;
        st->energy = MAX_ENERGY_CAPACITY;
        st->torpedoes = MAX_TORPEDO_CAPACITY;
        st->cargo_energy = 1000;
        st->cargo_torpedoes = 100;
        st->crew_count = MAX_CREW_EXPLORER;
        st->hull_integrity = 100.0;
        st->life_support = 100.0;
        st->ion_beam_charge = 500.0;      /* below max: recharge path runs */
        st->composite_plating = 50;
        for (int s = 0; s < 6; s++) {
            st->shields[s] = SHIELD_MAX_STRENGTH;
            st->target_shields[s] = SHIELD_MAX_STRENGTH;
        }
        for (int s = 0; s < MAX_SYSTEMS; s++) st->system_health[s] = 100.0;
        st->power_dist[0] = 0.5; st->power_dist[1] = 0.3; st->power_dist[2] = 0.2;
        /* p->name and st->captain_name live in the SAME struct:
         * copy through a temporary (snprintf would overlap). */
        {
            char cap[64];
            snprintf(cap, sizeof(cap), "%s", p->name);
            memcpy(st->captain_name, cap, sizeof(st->captain_name));
        }

        /* 3 active probes en route (eta far beyond the run: they move
         * every tick, exercising the probe physics branch). */
        for (int pr = 0; pr < 3; pr++) {
            NetProbe *probe = &st->probes[pr];
            probe->active = 1;
            probe->status = 0;
            probe->eta = 3600.0;
            probe->q1 = p->state.q1; probe->q2 = p->state.q2; probe->q3 = p->state.q3;
            probe->s1 = probe->s2 = probe->s3 = 15.0;
            probe->gx = (p->state.q1 - 1) * QUADRANT_SIZE + 15.0 + pr;
            probe->gy = (p->state.q2 - 1) * QUADRANT_SIZE + 15.0 + pr;
            probe->gz = (p->state.q3 - 1) * QUADRANT_SIZE + 15.0 + pr;
            probe->vx = probe->vy = probe->vz = 0.01 + 0.005 * pr;
        }

        /* Lock on the first NPC of the player's own quadrant (filled
         * below): keeps the lock-validity table lookup live. */
        p->state.lock_target = GALAXY_OBJECT_MIN_NPC + 8 * i;

        /* 4 active beams (consumed by the phase-3b builder on tick 1,
         * which is exactly when the real server sees them too). */
        st->beam_count = 4;
        for (int b = 0; b < 4; b++) {
            st->beams[b] = (NetBeam){
                p->state.s1, p->state.s2, p->state.s3,
                10.0, 10.0, 10.0,
                i + GALAXY_OBJECT_MIN_PLAYER, ((i + 1) % MAX_CLIENTS) + GALAXY_OBJECT_MIN_PLAYER, 1
            };
        }
    }

    /* 3. NPCs: all 4000 active, 8 per hot quadrant (the MAX_Q_NPC cap)
     * across h = 0..499. In the 16 player quadrants the faction is
     * chosen HOSTILE to the resident player, so the full combat path
     * (attack run, mobile fire, Alliance homing torpedoes, stationary
     * fire) runs against the player's shield economy. */
    for (int n = 0; n < MAX_NPC; n++) {
        NPCShip *np = &npcs[n];
        memset(np, 0, sizeof(*np));
        h = (n / 8) % 500;
        hot_quad(h, &q1, &q2, &q3);
        np->id = n;
        np->faction = (h < 16) ? 1 - (h % 2) : (n % 12);
        np->q1 = q1; np->q2 = q2; np->q3 = q3;
        np->x = 1.0 + (n % 30);
        np->y = 1.0 + ((n / 30) % 30);
        np->z = 1.0 + ((n / 900) % 30);
        np->gx = (q1 - 1) * QUADRANT_SIZE + np->x;
        np->gy = (q2 - 1) * QUADRANT_SIZE + np->y;
        np->gz = (q3 - 1) * QUADRANT_SIZE + np->z;
        np->active = 1;
        np->health = 10000;
        np->plating = 50;
        np->energy = MAX_ENERGY_CAPACITY;
        np->engine_health = 100.0;
        np->ai_state = AI_STATE_PATROL;
        np->fire_cooldown = n % 120;
        np->nav_timer = 60 + n % 120;
        np->dx = 1.0; np->dy = 0.0; np->dz = 0.0;
        np->ship_class = n % 14;
        np->beams[0] = (NetBeam){np->x, np->y, np->z, 5.0, 5.0, 5.0,
                                 n + GALAXY_OBJECT_MIN_NPC, 1, 1};
        np->beam_count = 4;
        snprintf(np->name, sizeof(np->name), "NPC-%d", n);
    }

    /* 4. Monsters: all 1000 active, 2 per hot quadrant (the cap).
     * Passive types 1..29 only: the type-30 resonance beam homes onto
     * whatever player shares the quadrant and would destabilize the
     * 1000-tick window; the per-monster scan loop (the measurable
     * cost) runs identically for every type. */
    for (int m = 0; m < MAX_MONSTERS; m++) {
        NPCMonster *mo = &monsters[m];
        memset(mo, 0, sizeof(*mo));
        h = (m / 2) % 500;
        hot_quad(h, &q1, &q2, &q3);
        mo->id = m;
        mo->type = 1 + (m % 29);
        mo->q1 = q1; mo->q2 = q2; mo->q3 = q3;
        mo->x = 5.0 + (m % 30);
        mo->y = 5.0 + ((m / 30) % 30);
        mo->z = 5.0;
        mo->health = 100000;
        mo->energy = MAX_ENERGY_CAPACITY;
        mo->active = 1;
        mo->behavior_timer = m % TIMER_MONSTER_PULSE;
    }

    /* 5. The remaining dynamic pools, at capacity, packed into the hot
     * quadrants at their per-quadrant caps. */
    for (int c = 0; c < MAX_COMETS; c++) {
        NPCComet *co = &comets[c];
        memset(co, 0, sizeof(*co));
        h = (c / 4) % 250;
        hot_quad(h, &q1, &q2, &q3);
        co->id = c;
        co->q1 = q1; co->q2 = q2; co->q3 = q3;
        co->x = 2.0 + (c % 36);
        co->y = 2.0 + ((c / 36) % 36);
        co->z = 2.0;
        co->h = 0.2; co->m = 0.1; co->speed = 0.02;
        co->cx = 0.5; co->cy = 0.5; co->cz = 0.5;
        co->active = 1;
    }
    for (int a = 0; a < MAX_ASTEROIDS; a++) {
        NPCAsteroid *as = &asteroids[a];
        memset(as, 0, sizeof(*as));
        h = (a / 8) % 375;
        hot_quad(h, &q1, &q2, &q3);
        as->id = a;
        as->q1 = q1; as->q2 = q2; as->q3 = q3;
        as->x = 2.0 + (a % 36);
        as->y = 2.0 + ((a / 36) % 36);
        as->z = 3.0;
        as->size = 1.0f;
        as->resource_type = a % MAX_RESOURCE_TYPES;
        as->amount = 1000;
        as->active = 1;
    }
    for (int d = 0; d < MAX_DERELICTS; d++) {
        NPCDerelict *de = &derelicts[d];
        memset(de, 0, sizeof(*de));
        h = (d / 4) % 750;
        hot_quad(h, &q1, &q2, &q3);
        de->id = d;
        de->q1 = q1; de->q2 = q2; de->q3 = q3;
        de->x = 3.0; de->y = 3.0; de->z = 3.0;
        de->ship_class = d % 14;
        de->faction = d % 12;
        de->active = 1;
        snprintf(de->name, sizeof(de->name), "WRECK-%d", d);
    }
    for (int m = 0; m < MAX_MINES; m++) {
        NPCMine *mi = &mines[m];
        memset(mi, 0, sizeof(*mi));
        h = (m / 8) % 188;
        hot_quad(h, &q1, &q2, &q3);
        mi->id = m;
        mi->faction = m % 12;
        mi->q1 = q1; mi->q2 = q2; mi->q3 = q3;
        mi->x = 4.0; mi->y = 4.0; mi->z = 4.0;
        mi->active = 1;
    }
    for (int b = 0; b < MAX_BUOYS; b++) {
        NPCBuoy *bu = &buoys[b];
        memset(bu, 0, sizeof(*bu));
        h = (b / 4) % 250;
        hot_quad(h, &q1, &q2, &q3);
        bu->id = b;
        bu->q1 = q1; bu->q2 = q2; bu->q3 = q3;
        bu->x = 6.0; bu->y = 6.0; bu->z = 6.0;
        bu->active = 1;
    }
    for (int pl = 0; pl < MAX_PLATFORMS; pl++) {
        NPCPlatform *pf = &platforms[pl];
        memset(pf, 0, sizeof(*pf));
        h = (pl / 4) % 250;
        hot_quad(h, &q1, &q2, &q3);
        pf->id = pl;
        pf->faction = pl % 12;
        pf->q1 = q1; pf->q2 = q2; pf->q3 = q3;
        pf->x = 7.0; pf->y = 7.0; pf->z = 7.0;
        pf->health = 50000;
        pf->energy = MAX_ENERGY_CAPACITY;
        pf->active = 1;
    }
    for (int r = 0; r < MAX_RIFTS; r++) {
        NPCRift *rf = &rifts[r];
        memset(rf, 0, sizeof(*rf));
        h = (r / 2) % 500;
        hot_quad(h, &q1, &q2, &q3);
        rf->id = r;
        rf->q1 = q1; rf->q2 = q2; rf->q3 = q3;
        rf->x = 8.0; rf->y = 8.0; rf->z = 8.0;
        rf->active = 1;
    }

    /* 6. Torpedoes: 64/64 active, 4 per player quadrant, straight
     * flight (target_id = 0) — they detonate at the quadrant boundary
     * within the first ~3 s (peak-load exercise of the torpedo pass),
     * then the pool sits empty at steady state. */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        ConnectedPlayer *p = &players[i];
        for (int t = 0; t < 4; t++) {
            PlayerTorpedo *pt = &players_torpedoes[i * 4 + t];
            memset(pt, 0, sizeof(*pt));
            pt->id = i * 4 + t;
            pt->owner_idx = i + GALAXY_OBJECT_MIN_PLAYER;
            pt->faction = p->faction;
            pt->q1 = p->state.q1; pt->q2 = p->state.q2; pt->q3 = p->state.q3;
            pt->x = 2.0 + t; pt->y = 2.0 + t; pt->z = 2.0 + t;
            pt->gx = (pt->q1 - 1) * QUADRANT_SIZE + pt->x;
            pt->gy = (pt->q2 - 1) * QUADRANT_SIZE + pt->y;
            pt->gz = (pt->q3 - 1) * QUADRANT_SIZE + pt->z;
            pt->dx = 1.0; pt->dy = 0.0; pt->dz = 0.0;
            pt->target_id = 0;
            pt->timeout = TIMER_TORP_TIMEOUT;
            pt->origin_tube = t;
            pt->active = true;
        }
    }

    /* 7. Master state: empty BPNBS map, frame counter running. */
    memset(&spacegl_master, 0, sizeof(spacegl_master));
    snprintf(spacegl_master.captain_name, sizeof(spacegl_master.captain_name), "BENCH_MASTER");

    /* 8. First dynamic index build (server startup does the same via
     * load_galaxy -> rebuild_spatial_index). */
    rebuild_spatial_index();
}

/* ================================================================== */
/* Benchmark                                                         */
/* ================================================================== */

/* The exact production tick (spacegl_server.c game loop): the tick
 * counter is bumped BEFORE the logic call (update_game_logic reads
 * global_tick as its seed and for its periodic branches), under
 * game_mutex. */
static inline void bench_tick(void) {
    global_tick++;
    spacegl_master.frame_id++;
    pthread_mutex_lock(&game_mutex);
    update_game_logic();
    pthread_mutex_unlock(&game_mutex);
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

/* Scene-stability check: if a player died / got rescued mid-run the
 * scene is not the one the threshold was calibrated for (and the
 * tick cost shifted under the measurement). */
static int scene_stable(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        ConnectedPlayer *p = &players[i];
        if (!p->active || p->state.hull_integrity <= 0.0 || p->state.crew_count <= 0) {
            fprintf(stderr,
                "FAIL: scene destabilized: player %d (%s) active=%d hull=%.1f crew=%d "
                "after the run (the threshold no longer applies to a stable scene)\n",
                i, p->name, p->active, p->state.hull_integrity, p->state.crew_count);
            return 0;
        }
    }
    return 1;
}

/* Load the thresholds (both are optional in the file; the defaults
 * below apply when the file or a key is missing). */
static void load_thresholds(const char *path, double *thr_avg, double *thr_p99) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        double v;
        if (sscanf(line, " threshold_ms %lf", &v) == 1) *thr_avg = v;
        else if (sscanf(line, " p99_ms %lf", &v) == 1) *thr_p99 = v;
    }
    fclose(f);
}

/* Round a measured value UP to 0.1 ms (a re-baselined threshold must
 * never be tighter than what the machine just demonstrated). */
static double ceil01(double v) { return ceil(v * 10.0) / 10.0; }

static int write_thresholds(const char *path, double avg_ms, double p99_ms) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "FAIL: cannot write %s\n", path);
        return 0;
    }
    fprintf(f,
        "# Server tick rate benchmark thresholds\n"
        "# Format: key <value_ms>\n"
        "# Updated by: tests/server_tick_bench --update-thresholds\n"
        "# Do NOT edit manually: run the benchmark with --update-thresholds\n"
        "# to regenerate after a refactoring that changes the tick time.\n"
        "# (values = measured x 1.5, rounded up to 0.1 ms)\n\n"
        "threshold_ms %.1f\n"
        "p99_ms %.1f\n",
        ceil01(avg_ms * 1.5), ceil01(p99_ms * 1.5));
    fclose(f);
    printf("thresholds updated: %s (threshold_ms %.1f, p99_ms %.1f)\n",
           path, ceil01(avg_ms * 1.5), ceil01(p99_ms * 1.5));
    return 1;
}

static int qsort_cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static int run_bench(int n_ticks, int n_warmup, const char *thr_file,
                     int update_thresholds, double dump_slow_ms,
                     int report_only) {
    /* Fallbacks (only used when the threshold file is missing) mirror
     * the re-baselined tests/server_tick_thresholds.txt values. */
    double thr_avg = 15.3, thr_p99 = 31.6;
    load_thresholds(thr_file, &thr_avg, &thr_p99);

    printf("Building worst-case scene (16 players, all dynamic pools at "
           "capacity, %d static types)...\n", (int)NUM_STATIC_FILLS);

    build_scene();

    /* Warm-up: page-in, allocator warm, branch predictors, first-tick
     * one-offs (the phase-3b beam consumption, the first rebuild). */
    for (int i = 0; i < n_warmup; i++) bench_tick();

    double *tick_times = malloc(sizeof(double) * (size_t)n_ticks);
    if (!tick_times) { fprintf(stderr, "FAIL: malloc\n"); return 1; }

    for (int i = 0; i < n_ticks; i++) {
        double t0 = now_ms();
        bench_tick();
        tick_times[i] = now_ms() - t0;
        if (dump_slow_ms > 0.0 && tick_times[i] > dump_slow_ms) {
            printf("  slow tick %d (global_tick=%d): %.3f ms\n",
                   i, global_tick, tick_times[i]);
        }
    }

    double sum = 0.0, mn = tick_times[0], mx = tick_times[0];
    for (int i = 0; i < n_ticks; i++) {
        sum += tick_times[i];
        if (tick_times[i] < mn) mn = tick_times[i];
        if (tick_times[i] > mx) mx = tick_times[i];
    }
    double avg = sum / (double)n_ticks;

    double *sorted_copy = malloc(sizeof(double) * (size_t)n_ticks);
    if (!sorted_copy) { free(tick_times); fprintf(stderr, "FAIL: malloc\n"); return 1; }
    memcpy(sorted_copy, tick_times, sizeof(double) * (size_t)n_ticks);
    qsort(sorted_copy, (size_t)n_ticks, sizeof(double), qsort_cmp_double);
    int p99_idx = (int)((double)(n_ticks - 1) * 0.99);
    double p99 = sorted_copy[p99_idx];
    free(sorted_copy);

    printf("Server tick benchmark (%d ticks, %d warmup, %d players, "
           "%d static objects):\n", n_ticks, n_warmup, MAX_CLIENTS,
           bench_static_objects);
    printf("  average: %.3f ms\n", avg);
    printf("  p99:     %.3f ms\n", p99);
    printf("  min:     %.3f ms\n", mn);
    printf("  max:     %.3f ms\n", mx);
    printf("  threshold: %.1f ms (avg), %.1f ms (p99)\n", thr_avg, thr_p99);
    printf("  messages sent during run: %d (send_server_msg stub counter)\n",
           bench_msgs_sent);
    free(tick_times);

    if (update_thresholds) {
        if (!scene_stable()) return 1;
        return write_thresholds(thr_file, avg, p99) ? 0 : 1;
    }

    if (!scene_stable()) return 1;

    if (report_only) {
        printf("REPORT-ONLY: threshold gate disabled (sanitizer or "
               "unoptimized build distorts tick times);\n"
               "  the numbers above are informational for the CI log.\n");
        return 0;
    }

    int fail = 0;
    if (avg > thr_avg) {
        fprintf(stderr, "FAIL: average tick time %.3f ms > %.1f ms threshold\n",
                avg, thr_avg);
        fail = 1;
    }
    if (p99 > thr_p99) {
        fprintf(stderr, "FAIL: p99 tick time %.3f ms > %.1f ms threshold\n",
                p99, thr_p99);
        fail = 1;
    }
    if (!fail) {
        printf("PASS: server_tick_bench (avg %.3f ms < %.1f ms, p99 %.3f ms < %.1f ms)\n",
               avg, thr_avg, p99, thr_p99);
    }
    return fail;
}

int main(int argc, char **argv) {
    int n_ticks = 1000;
    int n_warmup = 100;
    int update_thresholds = 0;
    int report_only = 0;
    double dump_slow_ms = 0.0;
    const char *thr_file = "tests/server_tick_thresholds.txt";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            n_ticks = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            n_warmup = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--threshold-file") == 0 && i + 1 < argc) {
            thr_file = argv[++i];
        } else if (strcmp(argv[i], "--update-thresholds") == 0) {
            update_thresholds = 1;
        } else if (strcmp(argv[i], "--report-only") == 0) {
            report_only = 1;
        } else if (strcmp(argv[i], "--dump-slow") == 0 && i + 1 < argc) {
            dump_slow_ms = atof(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("usage: %s [--ticks N] [--warmup N] [--threshold-file PATH] "
                   "[--update-thresholds] [--report-only] [--dump-slow MS]\n", argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown argument: %s\n", argv[i]);
            return 1;
        }
    }
    if (n_ticks < 10 || n_warmup < 0) {
        fprintf(stderr, "FAIL: need --ticks >= 10 and --warmup >= 0\n");
        return 1;
    }

    return run_bench(n_ticks, n_warmup, thr_file, update_thresholds,
                     dump_slow_ms, report_only);
}
