/*
 * SPACE GL - 3D LOGIC ENGINE - server vector-thrust ("vec") contract test.
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of that License,
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
 * Behaviour regression (2026.10.07.02): the pilot hat-switch
 * retro-thrusters need a thrust command that TRANSLATES the ship
 * WITHOUT changing its attitude. The pre-fix hat drove the 3-arg
 * "imp (h0+180) -m0 S": handle_imp ALWAYS aligns the nose to the
 * target course (NAV_STATE_ALIGN_IMPULSE, up to a 1 s ramp), so the
 * ship rolled 180 deg (the cockpit view spun) before the burn and
 * no thruster ever read as "firing". The new "vec <H> <M> <S>"
 * command sets the flight vector from the requested course and
 * cruises NAV_STATE_IMPULSE along it while van_h / van_m / van_r
 * are left exactly as they are; "vec <H> <M> 0" releases the
 * thrust (all stop, dead in space, same stop semantics as the
 * 1-arg impulse).
 *
 * The code under test is the REAL production source: commands.c is
 * linked whole (plus nav_math.c, targets.c and logic.c, the same
 * way target_bounds_test pulls in production TUs). This TU owns the
 * global object arrays at production size (same scaffolding as
 * target_bounds_test) and a send_server_msg recorder, so the exact
 * HELMSMAN/COMPUTER feedback is asserted as well.
 *
 * Pinned contract:
 *   1. Thrust engage: "vec 90 0 100" on a ship at (270, 30, roll 45)
 *      sets dx/dy/dz to the unit vector of course (90, 0),
 *      hyper_speed = 100 / COEFF_IMPULSE_DIVISOR, target_gx = -1
 *      (unlimited cruise), nav_state = NAV_STATE_IMPULSE and the
 *      HELMSMAN "Vector thrust engaged at 100%." answer — while
 *      van_h / van_m / van_r are UNTOUCHED (270 / 30 / 45).
 *   2. Retrograde (the hat DOWN case): nose (90, 0) +
 *      "vec 270 0 100" → flight vector exactly opposite the nose
 *      (dx ≈ -1, dy ≈ 0, dz ≈ 0), nose still at 90.
 *   3. Thrust release: "vec 270 0 0" → nav_state = NAV_STATE_IDLE,
 *      dx = dy = dz = 0, hyper_speed = 0, "Vector thrust released.
 *      All stop." — attitude still untouched.
 *   4. Speed clamp: S above the 100 % grid clamps hyper_speed to
 *      SPEED_IMPULSE_TICK_MAX.
 *   5. normalize_upright: "vec 90 120 50" folds mark 120 → 60 and
 *      heading 90 → 270 before building the flight vector.
 *   6. Guards: wrong argument count → "Usage: vec ..." and no state
 *      change; impulse system CRITICAL → "Vector thrusters system
 *      is CRITICAL." and no state change; energy below
 *      COST_ACTION_HIGH → "Insufficient energy for vector thrust."
 *      and no state change. On every accepted command the energy
 *      cost is exactly COST_ACTION_HIGH.
 *
 * Exit: 0 = pass, 1 = fail.
 */

#include "server_internal.h"
#include "game_state.h"
#include "nav_math.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The vector-thrust handler is dispatched through commands.c's static
 * command_registry table, so no public header declares it. */
void handle_vec(int i, const char *params, bool *should_disconnect);
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

#define P 0  /* the pilot under test */

/* Reset the pilot to a controlled, healthy, fully-energized ship at
 * the given attitude (heading / mark / roll in degrees). */
static void pose_ship(int i, double h, double m, double r)
{
    memset(&players[i], 0, sizeof players[i]);
    players[i].active = 1;
    snprintf(players[i].name, sizeof players[i].name, "TESTCAP");
    players[i].state.van_h = h;
    players[i].state.van_m = m;
    players[i].state.van_r = r;
    players[i].state.q1 = 1; players[i].state.q2 = 1; players[i].state.q3 = 1;
    players[i].state.s1 = 20.0; players[i].state.s2 = 20.0; players[i].state.s3 = 20.0;
    players[i].gx = 20.0; players[i].gy = 20.0; players[i].gz = 20.0;
    for (int s = 0; s < 10; s++) players[i].state.system_health[s] = 100.0;
    players[i].state.energy = 100000;
    players[i].nav_state = NAV_STATE_IDLE;
    players[i].hyper_speed = 0.0;
    players[i].dx = 0.0; players[i].dy = 0.0; players[i].dz = 0.0;
    players[i].target_gx = -1.0;
    players[i].is_docked = 0;
}

/* Course -> flight vector, the same formula handle_vec must apply. */
static void course_vector(double h, double m, double *vx, double *vy, double *vz)
{
    double rh = h * M_PI / 180.0, rm = m * M_PI / 180.0;
    *vx = cos(rm) * sin(rh);
    *vy = cos(rm) * -cos(rh);
    *vz = sin(rm);
}

int main(void)
{
    bool dc = false;
    double e0;
    double vx, vy, vz;

    /* 1. Thrust engage: course (90, 0) @ 100 % on a ship at
     *    (270, 30) roll 45 — the flight vector follows the requested
     *    course, the ship cruises IMPULSE, the attitude is UNTOUCHED. */
    pose_ship(P, 270.0, 30.0, 45.0);
    e0 = (double)players[P].state.energy;
    handle_vec(P, "90 0 100", &dc);
    CHECK(players[P].nav_state == NAV_STATE_IMPULSE,
          "engage: nav_state IMPULSE (got %d)", players[P].nav_state);
    course_vector(90.0, 0.0, &vx, &vy, &vz);
    CHECK(fabs(players[P].dx - vx) < 1e-12 &&
          fabs(players[P].dy - vy) < 1e-12 &&
          fabs(players[P].dz - vz) < 1e-12,
          "engage: flight vector of course (90,0) = (1,0,0) "
          "(got %.6f, %.6f, %.6f)", players[P].dx, players[P].dy, players[P].dz);
    CHECK(fabs(players[P].hyper_speed - 100.0 / COEFF_IMPULSE_DIVISOR) < 1e-12,
          "engage: hyper_speed = S / COEFF_IMPULSE_DIVISOR (got %f)",
          players[P].hyper_speed);
    CHECK(players[P].target_gx == -1.0, "engage: unlimited cruise (target_gx == -1)");
    CHECK(players[P].state.van_h == 270.0 && players[P].state.van_m == 30.0 &&
          players[P].state.van_r == 45.0,
          "engage: attitude UNTOUCHED (got h=%.3f m=%.3f r=%.3f)",
          players[P].state.van_h, players[P].state.van_m, players[P].state.van_r);
    CHECK((double)(e0 - players[P].state.energy) == (double)COST_ACTION_HIGH,
          "engage: energy cost exactly COST_ACTION_HIGH (got %f)",
          e0 - (double)players[P].state.energy);
    CHECK(msg_find(P, "HELMSMAN", "Vector thrust engaged at 100%") >= 0,
          "engage: HELMSMAN feedback");
    CHECK(!dc, "engage: no disconnect");

    /* 2. Retrograde — the hat DOWN case: nose (90, 0), the command is
     *    the exact reverse course (270, 0): the ship must translate
     *    opposite the nose while the nose (and the cockpit view)
     *    stays put. */
    pose_ship(P, 90.0, 0.0, 0.0);
    handle_vec(P, "270 0 100", &dc);
    CHECK(players[P].nav_state == NAV_STATE_IMPULSE, "retro: nav_state IMPULSE");
    CHECK(fabs(players[P].dx - (-1.0)) < 1e-9 &&
          fabs(players[P].dy) < 1e-9 && fabs(players[P].dz) < 1e-9,
          "retro: flight vector (-1,0,0), opposite the nose "
          "(got %.6f, %.6f, %.6f)", players[P].dx, players[P].dy, players[P].dz);
    CHECK(players[P].state.van_h == 90.0 && players[P].state.van_m == 0.0,
          "retro: nose stays at 90/0 (got %.3f / %.3f)",
          players[P].state.van_h, players[P].state.van_m);

    /* 2b. Retrograde with mark — the hat diagonal case: nose
     *    (90, 20), reverse course (270, -20). */
    pose_ship(P, 90.0, 20.0, 0.0);
    handle_vec(P, "270 -20 100", &dc);
    course_vector(270.0, -20.0, &vx, &vy, &vz);
    CHECK(fabs(players[P].dx - vx) < 1e-12 &&
          fabs(players[P].dy - vy) < 1e-12 &&
          fabs(players[P].dz - vz) < 1e-12,
          "retro+m: flight vector of course (270,-20) "
          "(got %.6f, %.6f, %.6f; want %.6f, %.6f, %.6f)",
          players[P].dx, players[P].dy, players[P].dz, vx, vy, vz);
    CHECK(players[P].state.van_h == 90.0 && players[P].state.van_m == 20.0,
          "retro+m: nose stays at 90/20");

    /* 3. Thrust release: "vec H M 0" — all stop, dead in space,
     *    flight vector zeroed, attitude still untouched. */
    pose_ship(P, 123.0, -45.0, 77.0);
    players[P].nav_state = NAV_STATE_IMPULSE;
    players[P].hyper_speed = 0.2;
    players[P].dx = 1.0;
    handle_vec(P, "303 45 0", &dc);
    CHECK(players[P].nav_state == NAV_STATE_IDLE,
          "release: nav_state IDLE (got %d)", players[P].nav_state);
    CHECK(players[P].dx == 0.0 && players[P].dy == 0.0 && players[P].dz == 0.0,
          "release: flight vector zeroed");
    CHECK(players[P].hyper_speed == 0.0, "release: hyper_speed zeroed");
    CHECK(players[P].state.van_h == 123.0 && players[P].state.van_m == -45.0 &&
          players[P].state.van_r == 77.0,
          "release: attitude UNTOUCHED (got %.3f, %.3f, %.3f)",
          players[P].state.van_h, players[P].state.van_m, players[P].state.van_r);
    CHECK(msg_find(P, "HELMSMAN", "Vector thrust released. All stop.") >= 0,
          "release: HELMSMAN feedback");

    /* 4. Speed clamp: S above the 100 % grid clamps to the per-tick
     *    maximum (same clamp as the impulse drive). */
    pose_ship(P, 0.0, 0.0, 0.0);
    handle_vec(P, "0 0 99999", &dc);
    CHECK(players[P].hyper_speed == SPEED_IMPULSE_TICK_MAX,
          "clamp: hyper_speed clamped to SPEED_IMPULSE_TICK_MAX (got %f)",
          players[P].hyper_speed);

    /* 5. normalize_upright fold: "vec 90 120 50" must fold mark 120
     *    -> 60 and heading 90 -> 270 BEFORE building the vector. */
    pose_ship(P, 0.0, 0.0, 0.0);
    handle_vec(P, "90 120 50", &dc);
    course_vector(270.0, 60.0, &vx, &vy, &vz);
    CHECK(fabs(players[P].dx - vx) < 1e-12 &&
          fabs(players[P].dy - vy) < 1e-12 &&
          fabs(players[P].dz - vz) < 1e-12,
          "fold: (90,120) normalized to (270,60) before the flight "
          "vector (got %.6f, %.6f, %.6f; want %.6f, %.6f, %.6f)",
          players[P].dx, players[P].dy, players[P].dz, vx, vy, vz);

    /* 6. Guards. */
    pose_ship(P, 40.0, 10.0, 5.0);
    handle_vec(P, "90 0", &dc);            /* 2 args */
    CHECK(msg_find(P, "COMPUTER", "Usage: vec") >= 0, "usage: 2-arg rejected");
    CHECK(players[P].nav_state == NAV_STATE_IDLE && players[P].hyper_speed == 0.0 &&
          players[P].dx == 0.0, "usage: no state change (2 args)");

    pose_ship(P, 40.0, 10.0, 5.0);
    handle_vec(P, "", &dc);                /* 0 args */
    CHECK(msg_find(P, "COMPUTER", "Usage: vec") >= 0, "usage: 0-arg rejected");

    pose_ship(P, 40.0, 10.0, 5.0);
    handle_vec(P, "90 abc", &dc);          /* 1 parseable arg */
    CHECK(msg_find(P, "COMPUTER", "Usage: vec") >= 0, "usage: non-numeric 2nd arg rejected");
    CHECK(players[P].nav_state == NAV_STATE_IDLE && players[P].state.van_h == 40.0,
          "usage: no state change (non-numeric)");

    pose_ship(P, 40.0, 10.0, 5.0);
    players[P].state.system_health[1] = THRESHOLD_SYS_CRITICAL - 1.0;
    handle_vec(P, "90 0 50", &dc);
    CHECK(msg_find(P, "ENGINEERING", "Vector thrusters system is CRITICAL") >= 0,
          "critical: impulse system below CRITICAL rejects");
    CHECK(players[P].nav_state == NAV_STATE_IDLE && players[P].hyper_speed == 0.0,
          "critical: no state change");

    pose_ship(P, 40.0, 10.0, 5.0);
    players[P].state.energy = (uint64_t)COST_ACTION_HIGH - 1;
    handle_vec(P, "90 0 50", &dc);
    CHECK(msg_find(P, "COMPUTER", "Insufficient energy for vector thrust") >= 0,
          "energy: below COST_ACTION_HIGH rejects");
    CHECK(players[P].nav_state == NAV_STATE_IDLE &&
          players[P].state.energy == (uint64_t)COST_ACTION_HIGH - 1,
          "energy: nothing consumed on rejection");

    printf("vec_thrust: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
