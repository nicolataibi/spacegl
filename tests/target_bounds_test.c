/*
 * SPACE GL - 3D LOGIC ENGINE - server target ID boundary regression test.
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
 * Security regression (2026.10.06.01): the player-target lookups in
 * src/server/commands.c (pha/bor/lock/scan and the pending-boarding
 * confirm in process_command) and in src/server/logic.c (torpedo
 * homing and torpedo ETA late sync) guarded the players[] index only
 * by the universal player ID range (1..999, GALAXY_OBJECT_MAX_PLAYER)
 * while the array holds MAX_CLIENTS = 16 slots. An attacker-controlled
 * tid (pha/bor command parameters) or PlayerTorpedo.target_id of 17
 * .. 999 indexed players[16] .. players[998] out of bounds (OOB reads
 * of active/faction/name/position, OOB writes of shield/hull/energy),
 * corrupting the spacegl_master global that follows players[] in the
 * server BSS. Every player branch now clamps to tid <= MAX_CLIENTS;
 * this test pins those clamps against future refactors.
 *
 * The code under test is the REAL production source: commands.c and
 * logic.c are linked whole (plus nav_math.c, targets.c and sglog.c,
 * the same way the other tests pull in production TUs). This TU owns
 * the global object arrays at PRODUCTION SIZE: players[MAX_CLIENTS]
 * with exactly 16 elements, and spacegl_master declared IMMEDIATELY
 * AFTER it, mirroring the server BSS adjacency so an out-of-bounds
 * players[] access lands on the canary below (the test is also built
 * with ASan/UBSan, so any residual OOB aborts the run).
 *
 *   1. Command tid boundaries — the attacker (players[0]) issues
 *      lock/scan/pha/bor/apr against tid = 16 (last live slot:
 *      players[15], must resolve and act), tid = 17 (MAX_CLIENTS+1,
 *      in the universal range but past the last slot: must be
 *      rejected without touching players[16]) and tid = 999
 *      (GALAXY_OBJECT_MAX_PLAYER: must be rejected without touching
 *      players[998]). tid = 1 is the low-boundary control. The
 *      contract pinned: success/failure messages, lock_target /
 *      apr_target / pending_bor_target state, beams queued and their
 *      NetBeam.target_id, and shield damage only on the live slot.
 *   2. Pending-boarding confirm (process_command): the legal state
 *      (pending_bor_target = 16, type 2) sabotages players[15] and
 *      nothing else; a corrupted pending_bor_target = 17 falls out
 *      of every branch and must cancel without touching any slot.
 *   3. Torpedo target_id boundaries — one live torpedo in a valid
 *      quadrant, target_id = 16 (homes onto the live players[15]),
 *      17 / 999 (players[15] is LIVE in the same quadrant: the
 *      pre-fix code read players[16]/players[998] .active here; the
 *      clamped code must resolve nothing and leave the flight vector
 *      untouched), 0 / -1 (no target), and 1000 / 3999 (NPC range
 *      edges with the pools empty; an active npcs[0] is the positive
 *      control that the NPC branch still homes).
 *   4. Global invariants after EVERY case: the spacegl_master canary
 *      (memset 0x5A, the production-adjacent victim of the pre-fix
 *      OOB writes) is intact, and every send_server_msg recorded by
 *      the stub carried a p_idx in [0, MAX_CLIENTS) — the pre-fix
 *      "UNDER Ion Beam ATTACK!" / torpedo-impact sends used tid-1
 *      directly.
 *
 * Exit: 0 = pass, 1 = fail (ASan/UBSan abort on any OOB as well).
 */

#include "server_internal.h"
#include "game_state.h"
#include "nav_math.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Command handlers defined in commands.c: the server dispatches them
 * through its static command_registry table, so no public header
 * declares them (packets.c declares only process_command). */
void handle_lock(int i, const char *params, bool *should_disconnect);
void handle_scan(int i, const char *params, bool *should_disconnect);
void handle_pha(int i, const char *params, bool *should_disconnect);
void handle_bor(int i, const char *params, bool *should_disconnect);
void handle_apr(int i, const char *params, bool *should_disconnect);

/* ================================================================== */
/* Globals owned by this TU (production sizes; in the server they   */
/* live in galaxy.c / spacegl_server.c, which are not linked)        */
/* ================================================================== */

/* The two arrays the security regression corrupts across, in the
 * same adjacency as the server BSS: players[] then spacegl_master.
 * spacegl_master doubles as the canary (see CANARY_PATTERN). */
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
SupernovaState supernova_event = {0};

/* Allocated exactly like galaxy.c: one 41x41x41 block. */
QuadrantIndex (*spatial_index)[41][41] = NULL;

/* ================================================================== */
/* Stubs for the server I/O / persistence helpers the linked TUs     */
/* call (conn.c / net.c / galaxy.c are not linked).                  */
/* ================================================================== */

/* Message recorder: every send_server_msg is captured so the tests
 * can assert on the exact (p_idx, category, text) the handlers send,
 * and so the global invariant "p_idx always in [0, MAX_CLIENTS)" can
 * be checked (the pre-fix impact sends used tid-1 unclamped). */
#define MSG_REC_SLOT_TEXT 4096
#define MSG_REC_SLOTS 512
typedef struct {
    int p_idx;
    char cat[32];
    char text[MSG_REC_SLOT_TEXT];
} MsgRec;
static MsgRec g_msg[MSG_REC_SLOTS];
static int g_msg_count = 0;
static int g_msg_bad_pidx = 0;   /* calls with p_idx outside [0,16) */

void send_server_msg(int p_idx, const char *from, const char *text) {
    if (p_idx < 0 || p_idx >= MAX_CLIENTS) g_msg_bad_pidx++;
    if (g_msg_count >= MSG_REC_SLOTS) return;
    MsgRec *r = &g_msg[g_msg_count++];
    r->p_idx = p_idx;
    snprintf(r->cat, sizeof(r->cat), "%s", from ? from : "");
    snprintf(r->text, sizeof(r->text), "%s", text ? text : "");
}

/* Find the last recorded message to p_idx whose category and text
 * prefix match; returns -1 if none. */
static int msg_find(int p_idx, const char *cat, const char *text_prefix) {
    for (int i = g_msg_count - 1; i >= 0; i--) {
        if (g_msg[i].p_idx != p_idx) continue;
        if (cat && strcmp(g_msg[i].cat, cat) != 0) continue;
        if (text_prefix && strstr(g_msg[i].text, text_prefix) == NULL) continue;
        return i;
    }
    return -1;
}

void slog(const char *fmt, ...) { (void)fmt; }

void save_galaxy(void) {}
void save_galaxy_async(void) {}
void spawn_derelict(int q1, int q2, int q3, double x, double y, double z,
                    int faction, int ship_class, const char *name)
{
    (void)q1; (void)q2; (void)q3; (void)x; (void)y; (void)z;
    (void)faction; (void)ship_class; (void)name;
}
const char *get_species_name(int s) { (void)s; return "TEST"; }
void rebuild_spatial_index(void) {}   /* index stays empty: all pools empty */
void refresh_lrs_grid(void) {}
int write_all(int fd, const void *buf, size_t len) { (void)buf; return (int)len; (void)fd; }
void server_data_path(char *out, size_t out_len, const char *rel)
{
    snprintf(out, out_len, "%s", rel);
}

/* ================================================================== */
/* Test harness                                                       */
/* ================================================================== */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
           fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } \
} while (0)

#define CANARY_PATTERN 0x5A

/* The production-adjacent canary: spacegl_master sits right after
 * players[] in this TU exactly as in the server BSS, so a stray
 * players[tid-1] write lands here. */
static void canary_set(void) { memset(&spacegl_master, CANARY_PATTERN, sizeof(spacegl_master)); }

static int canary_intact(void)
{
    unsigned char *p = (unsigned char *)&spacegl_master;
    for (size_t i = 0; i < sizeof(spacegl_master); i++)
        if (p[i] != CANARY_PATTERN) return 0;
    return 1;
}

/* Reset the whole world to a controlled, quiet state.
 * global_tick = 1: every "every N ticks" branch in update_game_logic
 * (autosave, LRS refresh, BPNBS decay, supernova lottery which would
 * dereference an empty quadrant index) stays off for one tick. */
static void reset_world(void)
{
    memset(players, 0, sizeof(players));
    memset(&spacegl_master, 0, sizeof(spacegl_master));
    memset(npcs, 0, sizeof(npcs));
    memset(players_torpedoes, 0, sizeof(players_torpedoes));
    memset(stars_data, 0, sizeof(stars_data));
    memset(black_holes, 0, sizeof(black_holes));
    memset(nebulas, 0, sizeof(nebulas));
    memset(pulsars, 0, sizeof(pulsars));
    memset(quasars, 0, sizeof(quasars));
    memset(comets, 0, sizeof(comets));
    memset(asteroids, 0, sizeof(asteroids));
    memset(derelicts, 0, sizeof(derelicts));
    memset(mines, 0, sizeof(mines));
    memset(buoys, 0, sizeof(buoys));
    memset(platforms, 0, sizeof(platforms));
    memset(rifts, 0, sizeof(rifts));
    memset(monsters, 0, sizeof(monsters));
    memset(planets, 0, sizeof(planets));
    memset(bases, 0, sizeof(bases));
    memset(dysons, 0, sizeof(dysons));
    memset(hubs, 0, sizeof(hubs));
    memset(relics, 0, sizeof(relics));
    memset(ruptures, 0, sizeof(ruptures));
    memset(satellites, 0, sizeof(satellites));
    memset(storms, 0, sizeof(storms));
    memset(artifacts, 0, sizeof(artifacts));
    memset(warp_gates, 0, sizeof(warp_gates));
    memset(neutron_stars, 0, sizeof(neutron_stars));
    memset(mega_structs, 0, sizeof(mega_structs));
    memset(dark_clouds, 0, sizeof(dark_clouds));
    memset(singularities, 0, sizeof(singularities));
    memset(plasma_storms, 0, sizeof(plasma_storms));
    memset(orbital_rings, 0, sizeof(orbital_rings));
    memset(time_anomalies, 0, sizeof(time_anomalies));
    memset(void_crystals, 0, sizeof(void_crystals));
    memset(subspace_anomalies, 0, sizeof(subspace_anomalies));
    memset(diffuse_nebulae, 0, sizeof(diffuse_nebulae));
    memset(dark_nebulae, 0, sizeof(dark_nebulae));
    memset(planetary_nebulae, 0, sizeof(planetary_nebulae));
    memset(snrs, 0, sizeof(snrs));
    memset(gmcs, 0, sizeof(gmcs));
    memset(interstellar_filaments, 0, sizeof(interstellar_filaments));
    memset(interstellar_bubbles, 0, sizeof(interstellar_bubbles));
    memset(bok_globules, 0, sizeof(bok_globules));
    memset(clump_cores, 0, sizeof(clump_cores));
    memset(accretion_disks, 0, sizeof(accretion_disks));
    memset(relativistic_jets, 0, sizeof(relativistic_jets));
    memset(shock_waves, 0, sizeof(shock_waves));
    memset(stellar_bow_shocks, 0, sizeof(stellar_bow_shocks));
    memset(cosmic_voids, 0, sizeof(cosmic_voids));
    memset(cosmic_filaments, 0, sizeof(cosmic_filaments));
    memset(event_horizons, 0, sizeof(event_horizons));
    memset(kilonovae, 0, sizeof(kilonovae));
    memset(grav_lenses, 0, sizeof(grav_lenses));
    memset(grbs, 0, sizeof(grbs));
    memset(grav_waves, 0, sizeof(grav_waves));
    memset(protoplanetary_disks, 0, sizeof(protoplanetary_disks));
    memset(debris_disks, 0, sizeof(debris_disks));
    memset(planetesimals, 0, sizeof(planetesimals));
    memset(rogue_planets, 0, sizeof(rogue_planets));
    memset(brown_dwarfs, 0, sizeof(brown_dwarfs));
    memset(isos, 0, sizeof(isos));
    memset(mag_reconns, 0, sizeof(mag_reconns));
    memset(current_sheets, 0, sizeof(current_sheets));
    memset(heliospheres, 0, sizeof(heliospheres));
    memset(term_shocks, 0, sizeof(term_shocks));
    memset(magnetospheres, 0, sizeof(magnetospheres));
    memset(cosmic_strings, 0, sizeof(cosmic_strings));
    memset(domain_walls, 0, sizeof(domain_walls));
    memset(dm_halos, 0, sizeof(dm_halos));
    memset(igms, 0, sizeof(igms));
    memset(cgms, 0, sizeof(cgms));
    memset(lyman_alphas, 0, sizeof(lyman_alphas));
    memset(cmbs, 0, sizeof(cmbs));
    memset(cosmic_features, 0, sizeof(cosmic_features));
    cosmic_feature_count = 0;
    memset(spatial_index, 0, 41 * 41 * 41 * sizeof(QuadrantIndex));
    memset(&supernova_event, 0, sizeof(supernova_event));
    g_msg_count = 0;
    g_msg_bad_pidx = 0;
    global_tick = 1;
}

/* Absolute g-coordinates of a local position in quadrant (1,1,1). */
static double g1(double local) { return (1 - 1) * QUADRANT_SIZE + local; }

/* Nominal, fully-armed attacker at the center of quadrant (1,1,1). */
static void setup_attacker(void)
{
    ConnectedPlayer *p = &players[0];
    p->active = 1;
    strcpy(p->name, "ALPHA");
    p->faction = 1;
    p->ship_class = 1;
    p->last_q1 = p->last_q2 = p->last_q3 = 1;
    p->state.q1 = p->state.q2 = p->state.q3 = 1;
    p->state.s1 = p->state.s2 = p->state.s3 = 20.0;
    p->gx = p->gy = p->gz = g1(20.0);
    p->state.energy = 1000000000ULL;
    for (int s = 0; s < 10; s++) p->state.system_health[s] = 100.0;
    for (int s = 0; s < 6; s++) p->state.shields[s] = 500;
    p->state.hull_integrity = 100.0;
    p->state.life_support = 100.0;
    p->state.crew_count = 50;
    p->state.ion_beam_charge = 100.0;
    p->state.power_dist[0] = p->state.power_dist[1] = p->state.power_dist[2] = 0.0;
}

/* The live last-slot victim: ID 16 -> players[15]. */
static void setup_victim16(double sx, double sy, double sz)
{
    ConnectedPlayer *p = &players[MAX_CLIENTS - 1];
    p->active = 1;
    strcpy(p->name, "VICTIM");
    p->faction = 2;                       /* hostile: no friendly-fire path */
    p->ship_class = 1;
    p->last_q1 = p->last_q2 = p->last_q3 = 1;
    p->state.q1 = p->state.q2 = p->state.q3 = 1;
    p->state.s1 = sx; p->state.s2 = sy; p->state.s3 = sz;
    p->gx = g1(sx); p->gy = g1(sy); p->gz = g1(sz);
    p->state.energy = 1000000000ULL;
    for (int s = 0; s < 10; s++) p->state.system_health[s] = 100.0;
    for (int s = 0; s < 6; s++) p->state.shields[s] = 500;
    p->state.hull_integrity = 100.0;
    p->state.life_support = 100.0;
    p->state.crew_count = 50;
    p->state.ion_beam_charge = 100.0;
}

/* Per-case global invariants (section 4). */
static void check_invariants(const char *what)
{
    CHECK(canary_intact(), "%s: spacegl_master canary corrupted", what);
    CHECK(g_msg_bad_pidx == 0, "%s: %d send_server_msg calls with p_idx outside [0,%d)",
          what, g_msg_bad_pidx, MAX_CLIENTS);
}

/* ================================================================== */
/* 1. Command tid boundaries (16 / 17 / 999)                          */
/* ================================================================== */

static void test_lock(void)
{
    struct { int tid; int expect_lock; } cases[] = {
        { 1,  1 },   /* low boundary: the attacker itself */
        { 16, 1 },   /* last live slot: players[15] */
        { 17, 0 },   /* MAX_CLIENTS+1: past the array, in the universal range */
        { 999, 0 },  /* GALAXY_OBJECT_MAX_PLAYER */
    };
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        int tid = cases[c].tid;
        char params[16];
        bool sd = false;
        snprintf(params, sizeof(params), "%d", tid);
        reset_world();
        setup_attacker();
        setup_victim16(20.5, 20.0, 20.0);
        canary_set();

        handle_lock(0, params, &sd);

        if (cases[c].expect_lock) {
            CHECK(msg_find(0, "TACTICAL", "Target locked and tracking.") >= 0,
                  "lock tid=%d: expected success message", tid);
            CHECK(players[0].state.lock_target == tid,
                  "lock tid=%d: lock_target=%d", tid, players[0].state.lock_target);
        } else {
            CHECK(msg_find(0, "TACTICAL", "Unable to acquire lock. Target not detected.") >= 0,
                  "lock tid=%d: expected rejection message", tid);
            CHECK(players[0].state.lock_target == 0,
                  "lock tid=%d: lock_target=%d (must stay 0)", tid, players[0].state.lock_target);
        }
        check_invariants("lock");
    }
}

static void test_scan(void)
{
    int tids[] = { 16, 17, 999 };
    for (size_t c = 0; c < sizeof(tids) / sizeof(tids[0]); c++) {
        int tid = tids[c];
        char params[16];
        bool sd = false;
        snprintf(params, sizeof(params), "%d", tid);
        reset_world();
        setup_attacker();
        setup_victim16(20.5, 20.0, 20.0);
        canary_set();

        handle_scan(0, params, &sd);

        if (tid == 16) {
            int m = msg_find(0, "SCIENCE", "SENSOR SCAN ANALYSIS: TARGET ID 16");
            CHECK(m >= 0, "scan tid=16: expected the sensor analysis report");
            if (m >= 0)
                CHECK(strstr(g_msg[m].text, "COMMANDER: VICTIM") != NULL,
                      "scan tid=16: report must describe players[15]");
        } else {
            CHECK(msg_find(0, "COMPUTER", "Target not in sensor range.") >= 0,
                  "scan tid=%d: expected the not-in-range rejection", tid);
        }
        check_invariants("scan");
    }
}

static void test_pha(void)
{
    int tids[] = { 16, 17, 999 };
    for (size_t c = 0; c < sizeof(tids) / sizeof(tids[0]); c++) {
        int tid = tids[c];
        char params[16];
        bool sd = false;
        snprintf(params, sizeof(params), "%d 500", tid);
        reset_world();
        setup_attacker();
        setup_victim16(22.0, 20.0, 20.0);   /* dist = 2 from the attacker */
        canary_set();

        handle_pha(0, params, &sd);

        if (tid == 16) {
            /* Both emitters queue, tagged with the legal target ID. */
            CHECK(players[0].state.beam_count == 2,
                  "pha tid=16: beam_count=%d (expected 2)", players[0].state.beam_count);
            if (players[0].state.beam_count == 2) {
                CHECK(players[0].state.beams[0].target_id == 16 &&
                      players[0].state.beams[1].target_id == 16,
                      "pha tid=16: NetBeam.target_id=%d/%d",
                      players[0].state.beams[0].target_id,
                      players[0].state.beams[1].target_id);
            }
            /* Damage lands on the live slot: hit = (500/2)*(100/100)*0.5 = 125. */
            int shield_sum = 0;
            for (int s = 0; s < 6; s++) shield_sum += players[MAX_CLIENTS - 1].state.shields[s];
            CHECK(shield_sum == 6 * 500 - 125,
                  "pha tid=16: victim shields sum=%d (expected %d)",
                  shield_sum, 6 * 500 - 125);
            CHECK(msg_find(15, "WARNING", "UNDER Ion Beam ATTACK!") >= 0,
                  "pha tid=16: victim (p_idx 15) must be warned");
            CHECK(msg_find(0, "TACTICAL", "Ion Beams hit ID 16") >= 0,
                  "pha tid=16: expected the hit report");
        } else {
            CHECK(players[0].state.beam_count == 0,
                  "pha tid=%d: beam_count=%d (expected 0)", tid, players[0].state.beam_count);
            CHECK(msg_find(0, "COMPUTER", "Target out of range.") >= 0,
                  "pha tid=%d: expected the out-of-range rejection", tid);
            CHECK(msg_find(15, "WARNING", "UNDER Ion Beam ATTACK!") < 0,
                  "pha tid=%d: players[15] must not be hit", tid);
            int shield_sum = 0;
            for (int s = 0; s < 6; s++) shield_sum += players[MAX_CLIENTS - 1].state.shields[s];
            CHECK(shield_sum == 6 * 500,
                  "pha tid=%d: victim shields sum=%d (must be untouched)", tid, shield_sum);
        }
        check_invariants("pha");
    }
}

static void test_bor(void)
{
    int tids[] = { 16, 17, 999 };
    for (size_t c = 0; c < sizeof(tids) / sizeof(tids[0]); c++) {
        int tid = tids[c];
        char params[16];
        bool sd = false;
        snprintf(params, sizeof(params), "%d", tid);
        reset_world();
        setup_attacker();
        setup_victim16(20.5, 20.0, 20.0);   /* dist 0.5 <= DIST_BOARDING_MAX+margin */
        canary_set();

        handle_bor(0, params, &sd);

        if (tid == 16) {
            CHECK(players[0].pending_bor_target == 16,
                  "bor tid=16: pending_bor_target=%d", players[0].pending_bor_target);
            CHECK(players[0].pending_bor_type == 2,
                  "bor tid=16: pending_bor_type=%d (expected 2 = enemy)",
                  players[0].pending_bor_type);
            CHECK(msg_find(0, "BOARDING", "BOARDING MENU: HOSTILE VESSEL") >= 0,
                  "bor tid=16: expected the hostile-vessel menu");
        } else {
            CHECK(players[0].pending_bor_target == 0,
                  "bor tid=%d: pending_bor_target=%d (must stay 0)",
                  tid, players[0].pending_bor_target);
            CHECK(msg_find(0, "COMPUTER", "Invalid boarding target.") >= 0,
                  "bor tid=%d: expected the invalid-target rejection", tid);
        }
        check_invariants("bor");
    }
}

/* The pending-boarding confirm in process_command(): the numeric
 * keypress after the menu. tid is taken from pending_bor_target. */
static void test_bor_confirm(void)
{
    bool sd = false;

    /* Legal state: bor 16 then confirm choice 1 (sabotage). */
    reset_world();
    setup_attacker();
    setup_victim16(20.5, 20.0, 20.0);
    char params[16];
    snprintf(params, sizeof(params), "%d", MAX_CLIENTS);
    handle_bor(0, params, &sd);
    CHECK(players[0].pending_bor_target == MAX_CLIENTS,
          "bor confirm: setup left pending_bor_target=%d", players[0].pending_bor_target);
    canary_set();
    for (int i = 0; i < MAX_CLIENTS; i++)
        for (int s = 0; s < 10; s++) players[i].state.system_health[s] = 100.0;

    process_command(0, "1");

    int sabotaged = 0;
    for (int s = 0; s < 10; s++)
        if (players[MAX_CLIENTS - 1].state.system_health[s] == 0.0) sabotaged++;
    CHECK(sabotaged == 1, "bor confirm tid=16: exactly 1 system sabotaged (got %d)", sabotaged);
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
        for (int s = 0; s < 10; s++)
            CHECK(players[i].state.system_health[s] == 100.0,
                  "bor confirm tid=16: players[%d].system_health[%d] touched", i, s);
    check_invariants("bor confirm");

    /* Corrupted state: pending_bor_target = 17 (past the array) with
     * type 2. Every branch must be skipped; the confirm cancels on
     * the distance re-check and nothing may be touched. */
    reset_world();
    setup_attacker();
    players[0].pending_bor_target = 17;
    players[0].pending_bor_type = 2;
    canary_set();
    for (int i = 0; i < MAX_CLIENTS; i++)
        for (int s = 0; s < 10; s++) players[i].state.system_health[s] = 100.0;

    process_command(0, "1");

    CHECK(msg_find(0, "COMPUTER", "Target out of range. Operation cancelled.") >= 0,
          "bor confirm tid=17: expected the out-of-range cancel");
    for (int i = 0; i < MAX_CLIENTS; i++)
        for (int s = 0; s < 10; s++)
            CHECK(players[i].state.system_health[s] == 100.0,
                  "bor confirm tid=17: players[%d].system_health[%d] touched", i, s);
    check_invariants("bor confirm tid=17");
}

static void test_apr(void)
{
    int tids[] = { 16, 17, 999 };
    for (size_t c = 0; c < sizeof(tids) / sizeof(tids[0]); c++) {
        int tid = tids[c];
        char params[16];
        bool sd = false;
        snprintf(params, sizeof(params), "%d 2.0", tid);
        reset_world();
        setup_attacker();
        setup_victim16(25.0, 20.0, 20.0);   /* dist 5 > approach 2.0: engage */
        canary_set();

        handle_apr(0, params, &sd);

        if (tid == 16) {
            CHECK(players[0].apr_target == 16,
                  "apr tid=16: apr_target=%d", players[0].apr_target);
            CHECK(players[0].nav_state == NAV_STATE_ALIGN,
                  "apr tid=16: nav_state=%d (expected NAV_STATE_ALIGN)",
                  players[0].nav_state);
            CHECK(msg_find(0, "HELMSMAN", "Autopilot engaged.") >= 0,
                  "apr tid=16: expected the engage message");
        } else {
            CHECK(players[0].apr_target == 0,
                  "apr tid=%d: apr_target=%d (must stay 0)", tid, players[0].apr_target);
            CHECK(msg_find(0, "COMPUTER", "Target not found or obscured") >= 0,
                  "apr tid=%d: expected the not-found rejection", tid);
        }
        check_invariants("apr");
    }
}

/* ================================================================== */
/* 3. Torpedo target_id boundaries (update_game_logic tick)           */
/* ================================================================== */

/* One live torpedo in quadrant (1,1,1) at local (10,20,20) flying
 * +x, homing on the given target_id. Returns the flight vector after
 * the tick by writing into out_dx/out_dy/out_dz. */
static void tick_torpedo(int target_id, double *out_dx, double *out_dy,
                         double *out_dz, int *still_active)
{
    PlayerTorpedo *pt = &players_torpedoes[0];
    memset(pt, 0, sizeof(*pt));
    pt->id = 30000;
    pt->owner_idx = 16;          /* no player slot equals 16 (0..15) */
    pt->faction = 2;
    pt->q1 = pt->q2 = pt->q3 = 1;
    pt->x = 10.0; pt->y = 20.0; pt->z = 20.0;
    pt->gx = g1(10.0); pt->gy = g1(20.0); pt->gz = g1(20.0);
    pt->dx = 1.0; pt->dy = 0.0; pt->dz = 0.0;
    pt->target_id = target_id;
    pt->timeout = 100;
    pt->origin_tube = 0;
    pt->active = true;

    canary_set();
    update_game_logic();

    *out_dx = pt->dx; *out_dy = pt->dy; *out_dz = pt->dz;
    *still_active = (int)pt->active;
}

/* Homing onto a target at g=(20,25,20) from g=(10,20,20):
 * d = (10,5,0), |d| = sqrt(125); one tick bends the unit vector by
 * factor 0.35 and re-normalizes:
 *   v = 0.65*(1,0,0) + 0.35*(10,5,0)/sqrt(125)  ->  (0.98708, 0.16035, 0)
 * The values below are the production formula, pinned to 1e-4. */
#define HOME_DX 0.98708
#define HOME_DY 0.16035

static void test_torpedo_target_id(void)
{
    /* --- cases that must HOME: a live legal target exists -------- */

    /* tid = 16: the last live slot. */
    {
        double dx, dy, dz; int act;
        reset_world();
        setup_victim16(20.0, 25.0, 20.0);   /* g = (20,25,20) */
        tick_torpedo(16, &dx, &dy, &dz, &act);
        CHECK(fabs(dx - HOME_DX) < 1e-4 && fabs(dy - HOME_DY) < 1e-4 && fabs(dz) < 1e-9,
              "torp target_id=16: homing wrong (dx=%.5f dy=%.5f dz=%.5f)", dx, dy, dz);
        CHECK(act == 1, "torp target_id=16: torpedo deactivated");
        check_invariants("torp tid=16");
    }

    /* tid = 1000 with a live npcs[0]: the NPC branch still homes. */
    {
        double dx, dy, dz; int act;
        reset_world();
        NPCShip *n = &npcs[0];
        n->id = 0;
        n->faction = 2;
        n->active = 1;
        n->q1 = n->q2 = n->q3 = 1;
        n->x = 20.0; n->y = 25.0; n->z = 20.0;
        n->gx = g1(20.0); n->gy = g1(25.0); n->gz = g1(20.0);
        n->engine_health = 100.0;
        n->health = 100;
        tick_torpedo(1000, &dx, &dy, &dz, &act);
        CHECK(fabs(dx - HOME_DX) < 1e-4 && fabs(dy - HOME_DY) < 1e-4 && fabs(dz) < 1e-9,
              "torp target_id=1000: NPC homing wrong (dx=%.5f dy=%.5f dz=%.5f)", dx, dy, dz);
        CHECK(act == 1, "torp target_id=1000: torpedo deactivated");
        check_invariants("torp tid=1000 (live NPC)");
    }

    /* --- cases that must NOT home: the target is unresolvable --- */

    struct { int tid; const char *why; } nohome[] = {
        /* players[15] is LIVE at the homing spot in BOTH rows: the
         * pre-fix code read players[16] / players[998] here. */
        { 17,    "MAX_CLIENTS+1 (pre-fix: OOB players[16])" },
        { 999,   "GALAXY_OBJECT_MAX_PLAYER (pre-fix: OOB players[998])" },
        { 0,     "no target" },
        { -1,    "negative" },
        { 1000,  "NPC range edge, npcs[0] inactive" },
        { 3999,  "GALAXY_OBJECT_MAX_NPC, npcs[2999] inactive" },
    };
    for (size_t c = 0; c < sizeof(nohome) / sizeof(nohome[0]); c++) {
        double dx, dy, dz; int act;
        reset_world();
        if (nohome[c].tid == 17 || nohome[c].tid == 999)
            setup_victim16(20.0, 25.0, 20.0);   /* live, must still be ignored */
        tick_torpedo(nohome[c].tid, &dx, &dy, &dz, &act);
        CHECK(dx == 1.0 && dy == 0.0 && dz == 0.0,
              "torp target_id=%d (%s): flight vector changed (dx=%.5f dy=%.5f dz=%.5f)",
              nohome[c].tid, nohome[c].why, dx, dy, dz);
        CHECK(act == 1, "torp target_id=%d: torpedo deactivated", nohome[c].tid);
        if (nohome[c].tid == 17 || nohome[c].tid == 999) {
            /* The live last-slot player (at the homing spot) must never
             * receive a torpedo impact: the impact path sends a WARNING
             * to p_idx = target-1, which pre-fix was tid-1 unclamped. */
            CHECK(msg_find(15, "WARNING", "TORPEDO IMPACT DETECTED!") < 0,
                  "torp target_id=%d: impact delivered to live players[15]",
                  nohome[c].tid);
        }
        check_invariants("torp nohome");
    }
}

/* ================================================================== */

int main(void)
{
    printf("target_bounds test (players[] clamp boundaries: tid 16/17/999, "
           "torpedo target_id edges)\n");

    spatial_index = calloc(41 * 41 * 41, sizeof(QuadrantIndex));
    if (spatial_index == NULL) {
        fprintf(stderr, "cannot allocate spatial_index\n");
        return 1;
    }

    test_lock();
    test_scan();
    test_pha();
    test_bor();
    test_bor_confirm();
    test_apr();
    test_torpedo_target_id();

    printf("  %d checks passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
