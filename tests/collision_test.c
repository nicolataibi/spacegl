/*
 * collision_test.c - ships and comets that hit a solid object.
 *
 * Drives the REAL update_game_logic() (logic.c + logic_events.c) on a hand-made
 * spatial index with one planet:
 *   - a ship in hyperdrive that crosses the planet inside a single tick (4.6
 *     units per tick, contact radius 0.8) is stopped on the surface, drops out
 *     of hyperdrive, takes damage that grows with the speed at impact and the
 *     quadrant sees IPC_EV_IMPACT; pushing on right away is not punished again;
 *   - a slow impulse ship is hurt less than the warp ship;
 *   - a ship that passes at a distance is left alone;
 *   - a comet whose tick ends inside a planet is destroyed with IPC_EV_IMPACT
 *     and the players of the quadrant are told; a comet over a black hole (not
 *     solid) or over empty space survives.
 *   - NPC ships steer around solid objects (obstacle avoidance): a patrolling
 *     NPC aimed at a planet or a black hole curves around it without ever
 *     touching it, an NPC in open space flies exactly as before, one inside the
 *     keep-out zone is pushed out, one that already leaves is left alone and a
 *     stationary one stays where it is.
 *   - a ship that runs into a comet stops and is hurt, AND the comet is destroyed;
 *   - NPCs also keep away from other ships, collide for real with solid objects
 *     (stop on the surface, damage, IPC_EV_IMPACT; also on the fast physics
 *     ticks, which skip the AI) and never stay frozen inside an object: a null
 *     heading, a respawned slot or a heading into the object all get the NPC out
 *     at once.
 * Same scaffolding as vec_thrust_test (production-size globals, message
 * recorder, ASan/UBSan build).
 */

#include "server_internal.h"
#include "game_state.h"
#include "nav_math.h"
#include "shared_state.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
void update_moon_orbits(void) {}
void update_star_systems(void) {}
void sys_refresh_positions(int s, double t) { (void)s; (void)t; }
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

#define P 0                       /* the pilot under test */
#define QX 5
#define QY 6
#define QZ 7
#define GXQ(q, x) (((q) - 1) * QUADRANT_SIZE + (x))

static QuadrantIndex *qidx(void) { return &spatial_index[QX][QY][QZ]; }

/* One planet at local (10,10,10) of quadrant (5,6,7), registered in the index. */
static void plant_planet(void)
{
    memset(&planets[0], 0, sizeof planets[0]);
    planets[0].id = GALAXY_OBJECT_MIN_PLANET;
    planets[0].q1 = QX; planets[0].q2 = QY; planets[0].q3 = QZ;
    planets[0].x = 10; planets[0].y = 10; planets[0].z = 10;
    planets[0].active = 1;
    memset(qidx(), 0, sizeof *qidx());
    qidx()->planets[0] = &planets[0];
    qidx()->planet_count = 1;
}

static void pose_ship(int i, double lx, double ly, double lz)
{
    players[i].active = 0;
    update_game_logic();              /* an empty slot clears the grace period */
    memset(&players[i], 0, sizeof players[i]);
    players[i].active = 1;
    snprintf(players[i].name, sizeof players[i].name, "TESTCAP");
    players[i].state.q1 = QX; players[i].state.q2 = QY; players[i].state.q3 = QZ;
    players[i].state.s1 = lx; players[i].state.s2 = ly; players[i].state.s3 = lz;
    players[i].gx = GXQ(QX, lx); players[i].gy = GXQ(QY, ly); players[i].gz = GXQ(QZ, lz);
    for (int s = 0; s < 10; s++) players[i].state.system_health[s] = 100.0;
    players[i].state.hull_integrity = 100;
    players[i].state.crew_count = 100;
    players[i].state.energy = 1000000;
    players[i].nav_state = NAV_STATE_IDLE;
    players[i].target_gx = -1.0;
}

static void tick(void)
{
    players[P].state.event_count = 0;
    update_game_logic();
}

static int has_impact_event(void)
{
    for (int e = 0; e < players[P].state.event_count; e++)
        if (players[P].state.events[e].type == IPC_EV_IMPACT) return 1;
    return 0;
}

static double dist_to_planet(void)
{
    double dx = players[P].gx - GXQ(QX, 10), dy = players[P].gy - GXQ(QY, 10), dz = players[P].gz - GXQ(QZ, 10);
    return sqrt(dx * dx + dy * dy + dz * dz);
}

static void test_warp_ship(void)
{
    plant_planet();
    pose_ship(P, 7.7, 10.2, 10.0);
    players[P].nav_state = NAV_STATE_HYPERDRIVE;
    players[P].hyper_speed = 4.6;                 /* warp 9.9: ~4.6 units per tick */
    players[P].dx = 1.0;
    players[P].target_gx = GXQ(QX + 10, 10); players[P].target_gy = GXQ(QY, 10.2); players[P].target_gz = GXQ(QZ, 10);
    g_msg_count = 0;
    tick();
    CHECK(players[P].nav_state == NAV_STATE_IDLE, "warp ship drops out of hyperdrive (state %d)", players[P].nav_state);
    CHECK(fabs(dist_to_planet() - DIST_COLLISION_SHIP) < 0.01,
          "warp ship stops on the surface, not on the far side (dist %.3f)", dist_to_planet());
    CHECK(players[P].gx < GXQ(QX, 10), "warp ship is still on the side it came from (gx %.3f)", players[P].gx);
    int loss = 100 - (int)players[P].state.hull_integrity;
    CHECK(loss >= 28 && loss <= 31, "warp impact: hull loss ~30%% (got %d)", loss);
    CHECK(has_impact_event(), "the quadrant gets IPC_EV_IMPACT");
    CHECK(msg_find(P, "COMPUTER", "COLLISION") >= 0 && msg_find(P, "COMPUTER", "planet") >= 0,
          "the pilot is told what was hit");
    CHECK(players[P].state.s1 < 10.0 && fabs(players[P].state.s1 - (players[P].gx - GXQ(QX, 0))) < 1e-9,
          "sector coordinates follow the corrected position");
    CHECK(players[P].dx == 0.0 && players[P].hyper_speed == 0.0 && players[P].target_gx == -1.0,
          "engines and target are cleared");
    CHECK(g_msg_bad_pidx == 0, "no message to an invalid player index");

    /* pushing on right away: stopped again, but not hurt again (grace period) */
    int hull = (int)players[P].state.hull_integrity;
    players[P].nav_state = NAV_STATE_IMPULSE;
    players[P].hyper_speed = 0.5;
    players[P].dx = 1.0; players[P].dy = 0; players[P].dz = 0;
    players[P].target_gx = -1.0;
    tick();
    CHECK(players[P].nav_state == NAV_STATE_IDLE, "impulse into the planet is stopped (state %d)", players[P].nav_state);
    CHECK(dist_to_planet() >= DIST_COLLISION_SHIP - 1e-6, "ship never ends up inside the planet (%.3f)", dist_to_planet());
    CHECK((int)players[P].state.hull_integrity == hull, "no second damage during the grace period (%d -> %d)",
          hull, (int)players[P].state.hull_integrity);
}

static void test_impulse_ship_is_hurt_less(void)
{
    plant_planet();
    pose_ship(P, 6.0, 10.2, 10.0);
    players[P].nav_state = NAV_STATE_IMPULSE;
    players[P].hyper_speed = 0.5;
    players[P].dx = 1.0;
    int ticks = 0;
    while (players[P].nav_state == NAV_STATE_IMPULSE && ticks < 600) { tick(); ticks++; }
    CHECK(players[P].nav_state == NAV_STATE_IDLE, "impulse ship reaches the planet and stops (%d ticks)", ticks);
    int loss = 100 - (int)players[P].state.hull_integrity;
    CHECK(loss >= 2 && loss < 10, "impulse impact hurts, but far less than warp (loss %d)", loss);
    CHECK(fabs(dist_to_planet() - DIST_COLLISION_SHIP) < 0.05, "impulse ship stops on the surface (%.3f)", dist_to_planet());
}

static void test_miss_and_grace_expiry(void)
{
    plant_planet();
    pose_ship(P, 7.7, 13.0, 10.0);                /* passes 3 units above the planet */
    players[P].nav_state = NAV_STATE_HYPERDRIVE;
    players[P].hyper_speed = 4.6;
    players[P].dx = 1.0;
    players[P].target_gx = GXQ(QX + 10, 10); players[P].target_gy = GXQ(QY, 13.0); players[P].target_gz = GXQ(QZ, 10);
    tick();
    CHECK(players[P].nav_state == NAV_STATE_HYPERDRIVE, "a ship passing at a distance keeps flying (state %d)", players[P].nav_state);
    CHECK((int)players[P].state.hull_integrity == 100 && !has_impact_event(), "no damage, no event");

    /* the grace period ends: a new impact hurts again */
    plant_planet();
    pose_ship(P, 7.7, 10.2, 10.0);
    players[P].nav_state = NAV_STATE_HYPERDRIVE;
    players[P].hyper_speed = 4.6; players[P].dx = 1.0;
    players[P].target_gx = GXQ(QX + 10, 10); players[P].target_gy = GXQ(QY, 10.2); players[P].target_gz = GXQ(QZ, 10);
    tick();
    int hull1 = (int)players[P].state.hull_integrity;
    for (int k = 0; k < TIMER_COLLISION_GRACE + 5; k++) tick();
    players[P].nav_state = NAV_STATE_IMPULSE; players[P].hyper_speed = 0.5; players[P].dx = 1.0;
    tick();
    CHECK((int)players[P].state.hull_integrity < hull1, "after the grace period an impact hurts again (%d -> %d)",
          hull1, (int)players[P].state.hull_integrity);
}

/* Put a comet so that its next tick ends at galactic point T, coming from T - (3,0,0). */
static void aim_comet(int c, double tx, double ty, double tz)
{
    const double speed = 0.02, a = 100.0;
    memset(&comets[c], 0, sizeof comets[c]);
    comets[c].id = GALAXY_OBJECT_MIN_COMET + c;
    comets[c].a = a; comets[c].b = a;
    comets[c].angle = 0.0; comets[c].speed = speed; comets[c].inc = 0.0;
    double th = speed;
    double r = a * (1.0 - 0.5 * 0.5) / (1.0 + 0.5 * cos(th));
    comets[c].cx = tx - r * cos(th);
    comets[c].cy = ty - r * sin(th);
    comets[c].cz = tz;
    double px = tx - 3.0;
    comets[c].q1 = get_q_from_g(px); comets[c].q2 = get_q_from_g(ty); comets[c].q3 = get_q_from_g(tz);
    comets[c].x = px - (comets[c].q1 - 1) * QUADRANT_SIZE;
    comets[c].y = ty - (comets[c].q2 - 1) * QUADRANT_SIZE;
    comets[c].z = tz - (comets[c].q3 - 1) * QUADRANT_SIZE;
    comets[c].active = 1;
}

static void test_comets(void)
{
    /* comet into the planet */
    plant_planet();
    pose_ship(P, 30.0, 30.0, 30.0);               /* observer in the same quadrant */
    memset(comets, 0, sizeof comets);
    aim_comet(0, GXQ(QX, 10), GXQ(QY, 10), GXQ(QZ, 10));
    g_msg_count = 0;
    tick();
    CHECK(!comets[0].active, "a comet that flies into a planet is destroyed");
    CHECK(has_impact_event(), "the observers of the quadrant get IPC_EV_IMPACT");
    int ev = -1;
    for (int e = 0; e < players[P].state.event_count; e++) if (players[P].state.events[e].type == IPC_EV_IMPACT) ev = e;
    CHECK(ev >= 0 && fabs(players[P].state.events[ev].x1 - (10.0 - DIST_COLLISION_SHIP)) < 0.05 &&
          fabs(players[P].state.events[ev].y1 - 10.0) < 0.05,
          "the explosion is at the contact point, local (%.2f, %.2f)",
          ev >= 0 ? players[P].state.events[ev].x1 : -1.0, ev >= 0 ? players[P].state.events[ev].y1 : -1.0);
    CHECK(msg_find(P, "SENSORS", "comet") >= 0, "the quadrant is told");

    /* comet over a black hole: not solid, it survives */
    plant_planet();
    planets[0].active = 0;
    memset(&black_holes[0], 0, sizeof black_holes[0]);
    black_holes[0].id = GALAXY_OBJECT_MIN_BLACKHOLE;
    black_holes[0].q1 = QX; black_holes[0].q2 = QY; black_holes[0].q3 = QZ;
    black_holes[0].x = 10; black_holes[0].y = 10; black_holes[0].z = 10; black_holes[0].active = 1;
    qidx()->black_holes[0] = &black_holes[0]; qidx()->bh_count = 1;
    aim_comet(0, GXQ(QX, 10), GXQ(QY, 10), GXQ(QZ, 10));
    tick();
    CHECK(comets[0].active, "a comet over a black hole is not destroyed by the collision test");

    /* comet through empty space */
    plant_planet();
    aim_comet(0, GXQ(QX, 25), GXQ(QY, 25), GXQ(QZ, 25));
    tick();
    CHECK(comets[0].active && fabs(comets[0].x - 25.0) < 0.05, "an unobstructed comet keeps flying (x %.3f)", comets[0].x);
    CHECK(!has_impact_event(), "no event for a free comet");

    /* an inactive planet is not an obstacle */
    plant_planet();
    planets[0].active = 0;
    aim_comet(0, GXQ(QX, 10), GXQ(QY, 10), GXQ(QZ, 10));
    tick();
    CHECK(comets[0].active, "an inactive planet is not an obstacle");
}


/* ---- NPC obstacle avoidance ---------------------------------------- */

/* The server loop raises global_tick; the NPC AI runs on every third tick. */
static void npc_tick(void)
{
    global_tick++;
    update_game_logic();
}

static void place_npc(double lx, double ly, double lz, double dx, double dy, double dz)
{
    memset(&npcs[0], 0, sizeof npcs[0]);
    npcs[0].id = GALAXY_OBJECT_MIN_NPC;
    npcs[0].active = 1;
    npcs[0].health = 1000;
    npcs[0].engine_health = 100.0f;
    npcs[0].energy = 100000;
    npcs[0].ai_state = AI_STATE_PATROL;
    npcs[0].nav_timer = 1000000;                  /* no random re-pick of the heading */
    npcs[0].q1 = QX; npcs[0].q2 = QY; npcs[0].q3 = QZ;
    npcs[0].x = lx; npcs[0].y = ly; npcs[0].z = lz;
    npcs[0].gx = GXQ(QX, lx); npcs[0].gy = GXQ(QY, ly); npcs[0].gz = GXQ(QZ, lz);
    npcs[0].dx = dx; npcs[0].dy = dy; npcs[0].dz = dz;
}

static double npc_dist(double cx, double cy, double cz)
{
    double dx = npcs[0].gx - GXQ(QX, cx), dy = npcs[0].gy - GXQ(QY, cy), dz = npcs[0].gz - GXQ(QZ, cz);
    return sqrt(dx * dx + dy * dy + dz * dz);
}

typedef struct { double min_dist, max_turn_deg, travelled, end_dist; } NpcRun;

/* Fly NPC 0 for `ticks` ticks; track the closest approach to (cx,cy,cz), the
 * sharpest turn between two consecutive steps and the distance flown. */
static NpcRun fly_npc(int ticks, double cx, double cy, double cz)
{
    NpcRun r = { 1e300, 0.0, 0.0, 0.0 };
    double sx = npcs[0].gx, sy = npcs[0].gy, sz = npcs[0].gz;
    double px = sx, py = sy, pz = sz, pdx = 0, pdy = 0, pdz = 0;
    for (int t = 0; t < ticks; t++) {
        npc_tick();
        double d = npc_dist(cx, cy, cz);
        if (d < r.min_dist) r.min_dist = d;
        double mx = npcs[0].gx - px, my = npcs[0].gy - py, mz = npcs[0].gz - pz;
        double ml = sqrt(mx * mx + my * my + mz * mz);
        double pl = sqrt(pdx * pdx + pdy * pdy + pdz * pdz);
        if (ml > 1e-9 && pl > 1e-9) {
            double c = (mx * pdx + my * pdy + mz * pdz) / (ml * pl);
            if (c > 1.0) c = 1.0;
            if (c < -1.0) c = -1.0;
            double a = acos(c) * 180.0 / M_PI;
            if (a > r.max_turn_deg) r.max_turn_deg = a;
        }
        if (ml > 1e-9) { pdx = mx; pdy = my; pdz = mz; }
        px = npcs[0].gx; py = npcs[0].gy; pz = npcs[0].gz;
    }
    double tx = npcs[0].gx - sx, ty = npcs[0].gy - sy, tz = npcs[0].gz - sz;
    r.travelled = sqrt(tx * tx + ty * ty + tz * tz);
    r.end_dist = npc_dist(cx, cy, cz);
    return r;
}

static void test_npc_avoidance(void)
{
    const double step = 0.03;
    memset(npcs, 0, sizeof npcs);
    pose_ship(P, 35.0, 35.0, 35.0);               /* an observer, so the quadrant is not empty */

    /* open space: exactly the old straight flight */
    plant_planet();
    planets[0].active = 0;
    place_npc(10, 30, 30, 1, 0, 0);
    NpcRun r = fly_npc(300, 10, 10, 10);
    CHECK(fabs(npcs[0].gx - (GXQ(QX, 10) + 300 * step)) < 1e-6 && fabs(npcs[0].gy - GXQ(QY, 30)) < 1e-9,
          "an NPC in open space flies straight, as before (gx %.6f)", npcs[0].gx);

    /* head on into a planet */
    plant_planet();
    place_npc(4, 10, 10, 1, 0, 0);
    r = fly_npc(1500, 10, 10, 10);
    CHECK(r.min_dist >= DIST_COLLISION_SHIP, "the NPC never touches the planet (closest %.3f)", r.min_dist);
    CHECK(r.min_dist > DIST_COLLISION_SHIP + 0.4, "it steers around it, it does not just stall on the surface (closest %.3f)", r.min_dist);
    CHECK(r.travelled > 30.0, "and keeps flying (%.1f units)", r.travelled);
    CHECK(r.max_turn_deg < 40.0, "the turn is a curve, not a kink (sharpest step %.1f deg)", r.max_turn_deg);

    /* glancing pass on the other side, aimed at a moon-sized object */
    plant_planet();
    place_npc(4, 10.4, 10, 1, 0, 0);
    r = fly_npc(1500, 10, 10, 10);
    CHECK(r.min_dist >= DIST_COLLISION_SHIP && r.travelled > 30.0 && r.max_turn_deg < 40.0,
          "glancing pass: closest %.3f, flown %.1f, turn %.1f", r.min_dist, r.travelled, r.max_turn_deg);

    /* a black hole is not solid but keeps the NPC out of its well */
    plant_planet();
    planets[0].active = 0;
    memset(&black_holes[0], 0, sizeof black_holes[0]);
    black_holes[0].id = GALAXY_OBJECT_MIN_BLACKHOLE;
    black_holes[0].q1 = QX; black_holes[0].q2 = QY; black_holes[0].q3 = QZ;
    black_holes[0].x = 10; black_holes[0].y = 10; black_holes[0].z = 10; black_holes[0].active = 1;
    qidx()->black_holes[0] = &black_holes[0]; qidx()->bh_count = 1;
    place_npc(2, 10, 10, 1, 0, 0);
    r = fly_npc(1500, 10, 10, 10);
    CHECK(r.min_dist >= DIST_GRAVITY_WELL, "the NPC stays out of the black hole well (closest %.3f)", r.min_dist);
    CHECK(r.travelled > 30.0, "and keeps flying (%.1f units)", r.travelled);
    black_holes[0].active = 0;

    /* inside the keep-out zone, moving in: pushed out, never into the planet */
    plant_planet();
    place_npc(11.2, 10, 10, -1, 0, 0);
    r = fly_npc(900, 10, 10, 10);
    CHECK(r.min_dist >= DIST_COLLISION_SHIP, "an NPC aimed at the planet from 1.2 units never enters it (closest %.3f)", r.min_dist);
    CHECK(r.end_dist > 2.0, "and ends outside the keep-out zone (%.3f)", r.end_dist);

    /* inside the solid but already leaving: left alone */
    plant_planet();
    place_npc(10.5, 10, 10, 1, 0, 0);
    r = fly_npc(60, 10, 10, 10);
    CHECK(r.end_dist > 0.5 + 60 * step * 0.9, "an NPC that is leaving keeps leaving (%.3f)", r.end_dist);

    /* no engines: stays put */
    plant_planet();
    place_npc(4, 10, 10, 1, 0, 0);
    npcs[0].engine_health = 5.0f;
    double gx0 = npcs[0].gx;
    for (int t = 0; t < 30; t++) npc_tick();
    CHECK(npcs[0].gx == gx0, "a stationary NPC does not move");
}


/* ---- ships vs comets, NPC vs ships, NPC collisions, stuck NPCs ----------- */

/* A comet that stays where it is (speed 0): local (lx,ly,lz) of the test quadrant. */
static void plant_comet(int c, double lx, double ly, double lz)
{
    memset(&comets[c], 0, sizeof comets[c]);
    comets[c].id = GALAXY_OBJECT_MIN_COMET + c;
    comets[c].a = 100.0; comets[c].b = 100.0;
    comets[c].angle = 0.0; comets[c].speed = 0.0; comets[c].inc = 0.0;
    comets[c].cx = GXQ(QX, lx) - 50.0;            /* r(0) = a * 0.75 / 1.5 = 50 */
    comets[c].cy = GXQ(QY, ly);
    comets[c].cz = GXQ(QZ, lz);
    comets[c].q1 = QX; comets[c].q2 = QY; comets[c].q3 = QZ;
    comets[c].x = lx; comets[c].y = ly; comets[c].z = lz;
    comets[c].active = 1;
    qidx()->comets[c] = &comets[c];
    if (qidx()->comet_count < c + 1) qidx()->comet_count = c + 1;
}

static void test_ship_destroys_comet(void)
{
    plant_planet();
    planets[0].active = 0;
    memset(comets, 0, sizeof comets);
    plant_comet(0, 15.0, 10.0, 10.0);
    pose_ship(P, 12.0, 10.0, 10.0);
    players[P].nav_state = NAV_STATE_IMPULSE;
    players[P].hyper_speed = 0.5;
    players[P].dx = 1.0;
    g_msg_count = 0;
    int ticks = 0;
    while (players[P].nav_state == NAV_STATE_IMPULSE && ticks < 600) { tick(); ticks++; }
    CHECK(players[P].nav_state == NAV_STATE_IDLE, "the ship stops at the comet (%d ticks)", ticks);
    CHECK(!comets[0].active, "the comet it hit is destroyed too");
    CHECK(msg_find(P, "SENSORS", "comet") >= 0, "the quadrant is told the comet is gone");
    CHECK((int)players[P].state.hull_integrity < 100, "the ship is hurt by the crash");

    /* the approach target is no obstacle: a ship that docks / approaches a comet does not destroy it */
    plant_comet(0, 15.0, 10.0, 10.0);
    pose_ship(P, 12.0, 10.0, 10.0);
    players[P].nav_state = NAV_STATE_APPROACH;
    players[P].apr_target = comets[0].id;
    players[P].hyper_speed = 0.5;
    players[P].dx = 1.0;
    for (int t = 0; t < 40; t++) tick();
    CHECK(comets[0].active, "approaching a comet on purpose does not destroy it");
}

/* NPC slot i, same defaults as place_npc (slot 0). Registered in the quadrant index. */
static void place_npc_i(int i, double lx, double ly, double lz, double dx, double dy, double dz)
{
    memset(&npcs[i], 0, sizeof npcs[i]);
    npc_runtime_reset(i);                                          /* the spawn hook */
    npcs[i].id = GALAXY_OBJECT_MIN_NPC + i;
    npcs[i].active = 1;
    npcs[i].health = 1000;
    npcs[i].engine_health = 100.0f;
    npcs[i].energy = 100000;
    npcs[i].ai_state = AI_STATE_PATROL;
    npcs[i].nav_timer = 1000000;
    npcs[i].q1 = QX; npcs[i].q2 = QY; npcs[i].q3 = QZ;
    npcs[i].x = lx; npcs[i].y = ly; npcs[i].z = lz;
    npcs[i].gx = GXQ(QX, lx); npcs[i].gy = GXQ(QY, ly); npcs[i].gz = GXQ(QZ, lz);
    npcs[i].dx = dx; npcs[i].dy = dy; npcs[i].dz = dz;
    qidx()->npcs[i] = &npcs[i];
    if (qidx()->npc_count < i + 1) qidx()->npc_count = i + 1;
}

static double npc_pair_dist(void)
{
    double dx = npcs[0].gx - npcs[1].gx, dy = npcs[0].gy - npcs[1].gy, dz = npcs[0].gz - npcs[1].gz;
    return sqrt(dx * dx + dy * dy + dz * dz);
}

static void test_npc_avoids_ships(void)
{
    memset(npcs, 0, sizeof npcs);
    pose_ship(P, 35.0, 35.0, 35.0);
    plant_planet();
    planets[0].active = 0;
    qidx()->npc_count = 0;

    /* NPC 0 flies straight at NPC 1, which sits still (engines dead) */
    place_npc_i(0, 4.0, 20.0, 20.0, 1, 0, 0);
    place_npc_i(1, 10.0, 20.0, 20.0, 0, 0, 0);
    npcs[1].engine_health = 5.0f;
    double closest = 1e300, travelled0 = npcs[0].gx;
    double min_pair = 1e300;
    for (int t = 0; t < 1500; t++) {
        npc_tick();
        double d = npc_pair_dist();
        if (d < min_pair) min_pair = d;
        (void)closest;
    }
    travelled0 = npcs[0].gx - travelled0;
    CHECK(min_pair >= DIST_COLLISION_SHIP + 0.4, "an NPC steers around a ship in its way (closest %.3f)", min_pair);
    CHECK(fabs(travelled0) > 20.0 || fabs(npcs[0].gy - GXQ(QY, 20)) > 0.5 || fabs(npcs[0].gz - GXQ(QZ, 20)) > 0.5,
          "and keeps flying (moved %.1f)", travelled0);
    CHECK(npcs[1].gx == GXQ(QX, 10.0) && npcs[1].gy == GXQ(QY, 20.0), "the still ship was not moved");

    /* two NPCs flying head on: neither goes through the other */
    place_npc_i(0, 4.0, 20.0, 20.0, 1, 0, 0);
    place_npc_i(1, 12.0, 20.0, 20.0, -1, 0, 0);
    min_pair = 1e300;
    for (int t = 0; t < 1500; t++) {
        npc_tick();
        double d = npc_pair_dist();
        if (d < min_pair) min_pair = d;
    }
    CHECK(min_pair >= DIST_COLLISION_SHIP + 0.4, "two NPCs head on pass each other without touching (closest %.3f)", min_pair);

    qidx()->npc_count = 0;
}

static void test_npc_collision(void)
{
    memset(npcs, 0, sizeof npcs);
    pose_ship(P, 35.0, 35.0, 35.0);
    plant_planet();

    /* a step that ends inside the planet: put on the surface, hurt, quadrant told */
    place_npc_i(0, 7.0, 10.0, 10.0, 1, 0, 0);
    double ox = npcs[0].gx, oy = npcs[0].gy, oz = npcs[0].gz;
    npcs[0].gx = GXQ(QX, 10.3);  npcs[0].x = 10.3;                /* a long step, through the contact radius */
    players[P].state.event_count = 0;
    npc_collision_check(0, ox, oy, oz);
    double dd = sqrt(pow(npcs[0].gx - GXQ(QX, 10), 2) + pow(npcs[0].gy - GXQ(QY, 10), 2) + pow(npcs[0].gz - GXQ(QZ, 10), 2));
    CHECK(fabs(dd - DIST_COLLISION_SHIP) < 0.01 && npcs[0].gx < GXQ(QX, 10), "an NPC that hits the planet stops on its surface, on the side it came from (%.3f)", dd);
    CHECK(npcs[0].health < 1000 && npcs[0].health > 600, "and takes hull damage (health %d)", npcs[0].health);
    CHECK(has_impact_event(), "the quadrant gets IPC_EV_IMPACT");
    CHECK(npcs[0].dx < -0.99 && npcs[0].vx == 0.0, "it is stopped and heads away from the planet, not frozen (dx %.2f)", npcs[0].dx);
    CHECK(fabs(npcs[0].x - (npcs[0].gx - GXQ(QX, 0))) < 1e-9, "local coordinates follow");

    /* pushing on again right away: stopped again, no second damage during the grace period */
    int h = npcs[0].health;
    double sx = npcs[0].gx, sy = npcs[0].gy, sz = npcs[0].gz;       /* on the surface, where the crash left it */
    npcs[0].gx = GXQ(QX, 10.3); npcs[0].x = 10.3;
    npc_collision_check(0, sx, sy, sz);
    CHECK(npcs[0].health == h, "no second damage inside the grace period (%d -> %d)", h, npcs[0].health);
    CHECK(fabs(sqrt(pow(npcs[0].gx - GXQ(QX, 10), 2) + pow(npcs[0].gy - GXQ(QY, 10), 2) + pow(npcs[0].gz - GXQ(QZ, 10), 2)) - DIST_COLLISION_SHIP) < 0.01,
          "but it is put back on the surface again");

    /* a wreck-in-waiting is not collided again; a miss is left alone */
    place_npc_i(0, 7.0, 13.0, 10.0, 1, 0, 0);
    ox = npcs[0].gx; oy = npcs[0].gy; oz = npcs[0].gz;
    npcs[0].gx = GXQ(QX, 13.0); npcs[0].x = 13.0;
    players[P].state.event_count = 0;
    npc_collision_check(0, ox, oy, oz);
    CHECK(npcs[0].health == 1000 && !has_impact_event() && npcs[0].gx == GXQ(QX, 13.0), "an NPC passing 3 units away is not touched");

    /* enough damage destroys it: the usual death timer starts */
    place_npc_i(2, 7.0, 10.0, 10.0, 1, 0, 0);
    npcs[2].health = 5;
    ox = npcs[2].gx; oy = npcs[2].gy; oz = npcs[2].gz;
    npcs[2].gx = GXQ(QX, 10.3); npcs[2].x = 10.3;
    npc_collision_check(2, ox, oy, oz);
    CHECK(npcs[2].health <= 0 && npcs[2].death_timer > 0, "a crippled NPC is destroyed by the crash (health %d, timer %d)", npcs[2].health, npcs[2].death_timer);
    memset(&npcs[2], 0, sizeof npcs[2]);

    /* the fast physics ticks (2 of every 3) skip the AI: they must collide too.
     * Heading straight at the planet from 2.2 units with the AI step forced
     * away (nav_timer / state irrelevant): every tick is moved by the fast path
     * or the AI path, and in neither may the NPC end up inside. */
    place_npc_i(0, 7.0, 10.0, 10.0, 1, 0, 0);
    double min_d = 1e300;
    for (int t = 0; t < 900; t++) {
        npc_tick();
        double dx = npcs[0].gx - GXQ(QX, 10), dy = npcs[0].gy - GXQ(QY, 10), dz = npcs[0].gz - GXQ(QZ, 10);
        double d = sqrt(dx * dx + dy * dy + dz * dz);
        if (d < min_d) min_d = d;
    }
    CHECK(min_d >= DIST_COLLISION_SHIP - 1e-6, "NPC never ends inside the planet over 900 ticks (closest %.3f)", min_d);
    qidx()->npc_count = 0;
}

static void test_npc_not_stuck(void)
{
    memset(npcs, 0, sizeof npcs);
    pose_ship(P, 35.0, 35.0, 35.0);

    /* born inside the planet with no heading: leaves at once, does not wait for the AI to pick a direction (nav_timer is huge) */
    plant_planet();
    place_npc_i(0, 10.3, 10.0, 10.0, 0, 0, 0);
    double x0 = npcs[0].gx, y0 = npcs[0].gy, z0 = npcs[0].gz;
    for (int t = 0; t < 6; t++) npc_tick();                        /* two AI steps at most */
    double moved = sqrt(pow(npcs[0].gx - x0, 2) + pow(npcs[0].gy - y0, 2) + pow(npcs[0].gz - z0, 2));
    CHECK(moved > 0.05, "an NPC with a null heading inside an object moves within 6 ticks (%.3f)", moved);
    CHECK(npcs[0].dx != 0.0 || npcs[0].dy != 0.0 || npcs[0].dz != 0.0, "and now has a heading");
    NpcRun r = fly_npc(900, 10, 10, 10);
    CHECK(r.end_dist > 2.0, "it ends outside the keep-out zone (%.3f)", r.end_dist);

    /* born inside, heading straight at the centre */
    plant_planet();
    place_npc_i(0, 10.3, 10.0, 10.0, -1, 0, 0);
    r = fly_npc(900, 10, 10, 10);
    CHECK(r.end_dist > 2.0 && r.travelled > 1.0, "inside the planet and heading at its centre: out anyway (end %.3f, flown %.2f)", r.end_dist, r.travelled);

    /* exactly on the centre of the object */
    plant_planet();
    place_npc_i(0, 10.0, 10.0, 10.0, 0, 0, 0);
    r = fly_npc(900, 10, 10, 10);
    CHECK(r.end_dist > 2.0, "an NPC born on the very centre gets out too (%.3f)", r.end_dist);

    /* a slot reused by a new NPC: the verdict "open space" of the old one must not stick */
    plant_planet();
    planets[0].active = 0;
    place_npc_i(0, 10.0, 30.0, 30.0, 1, 0, 0);
    for (int t = 0; t < 60; t++) npc_tick();                       /* the scan goes idle */
    plant_planet();
    place_npc_i(0, 10.3, 10.0, 10.0, -1, 0, 0);                    /* new NPC, same slot, inside the planet */
    r = fly_npc(900, 10, 10, 10);
    CHECK(r.end_dist > 2.0, "a reused slot is scanned again at once (end %.3f)", r.end_dist);

    /* a ship that overlaps another one is pushed apart */
    plant_planet();
    planets[0].active = 0;
    place_npc_i(0, 20.0, 20.0, 20.0, 0, 0, 0);
    place_npc_i(1, 20.3, 20.0, 20.0, 0, 0, 0);
    double d0 = npc_pair_dist();
    for (int t = 0; t < 600; t++) npc_tick();
    CHECK(npc_pair_dist() > d0 + 0.5, "two NPCs on top of each other drift apart (%.2f -> %.2f)", d0, npc_pair_dist());

    /* no engines: an NPC inside an object stays (it cannot move) */
    plant_planet();
    place_npc_i(0, 10.3, 10.0, 10.0, 0, 0, 0);
    npcs[0].engine_health = 5.0f;
    double gx0 = npcs[0].gx;
    for (int t = 0; t < 30; t++) npc_tick();
    CHECK(npcs[0].gx == gx0, "an NPC without engines does not move");
    qidx()->npc_count = 0;
}

int main(void)
{
    spatial_index = calloc((size_t)41 * 41 * 41, sizeof(QuadrantIndex));
    if (!spatial_index) { fprintf(stderr, "cannot allocate the spatial index\n"); return 1; }
    global_tick = 1;                  /* skip the "every N ticks" heavy branches */

    test_warp_ship();
    test_impulse_ship_is_hurt_less();
    test_miss_and_grace_expiry();
    test_comets();
    test_npc_avoidance();
    test_ship_destroys_comet();
    test_npc_avoids_ships();
    test_npc_collision();
    test_npc_not_stuck();

    free(spatial_index);
    printf("collision_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
