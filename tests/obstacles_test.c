/*
 * obstacles_test.c - contract of include/obstacles.h on the per-quadrant index.
 *
 * The test owns a spatial index and plants a few objects by hand (the
 * production arrays of galaxy.c are not linked): the provider must return the
 * active solid objects of a quadrant in galactic coordinates with the radii of
 * the kind table, honour the kind mask, reach the neighbouring quadrant when
 * a segment passes near the face, give no hit for a black hole (not solid),
 * stop a fast ship at the surface and survive out-of-range quadrants, a
 * missing index and very long segments.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "obstacles.h"

QuadrantIndex (*spatial_index)[41][41];
/* the production arrays of galaxy.c are not linked: the test owns them */
NPCSysStar   sys_stars[SYS_MAX_STARS];
NPCSysPlanet sys_planets[SYS_MAX_PLANETS];
NPCSysMoon   sys_moons[SYS_MAX_MOONS];
int sys_star_count = 0;

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; \
    printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define QS QUADRANT_SIZE
#define GX(q, x) (((q) - 1) * QS + (x))

static NPCPlanet   g_planet, g_planet_off;
static NPCStar     g_star;
static NPCBlackHole g_bh;
static NPCComet    g_comet;
static NPCAsteroid g_ast;
static NPCCosmicFeature g_moon, g_feat;

static void plant(void) {
    memset(&g_planet, 0, sizeof g_planet);
    g_planet.id = 5001; g_planet.q1 = 5; g_planet.q2 = 6; g_planet.q3 = 7;
    g_planet.x = 10; g_planet.y = 10; g_planet.z = 10; g_planet.active = 1;
    g_planet_off = g_planet; g_planet_off.id = 5002; g_planet_off.active = 0;
    g_star = (NPCStar){0}; g_star.id = 7001; g_star.x = 30; g_star.y = 30; g_star.z = 30; g_star.active = 1;
    g_bh = (NPCBlackHole){0}; g_bh.id = 11001; g_bh.x = 20; g_bh.y = 20; g_bh.z = 20; g_bh.active = 1;
    g_comet = (NPCComet){0}; g_comet.id = 9001; g_comet.x = 5; g_comet.y = 5; g_comet.z = 5; g_comet.active = 1;
    g_ast = (NPCAsteroid){0}; g_ast.id = 3001; g_ast.x = QS - 0.5; g_ast.y = 10; g_ast.z = 10; g_ast.active = 1;
    memset(&g_moon, 0, sizeof g_moon);
    g_moon.id = 83001; g_moon.type = OBJ_MOON_FIRST + 2; g_moon.x = 15; g_moon.y = 15; g_moon.z = 15; g_moon.active = 1;
    g_feat = g_moon; g_feat.id = 83002; g_feat.type = 40;     /* not a moon */
    g_feat.x = 25; g_feat.y = 5; g_feat.z = 5;

    QuadrantIndex *qi = &spatial_index[5][6][7];
    memset(qi, 0, sizeof *qi);
    qi->planets[0] = &g_planet; qi->planets[1] = &g_planet_off; qi->planet_count = 2;
    qi->stars[0] = &g_star; qi->star_count = 1;
    qi->black_holes[0] = &g_bh; qi->bh_count = 1;
    qi->comets[0] = &g_comet; qi->comet_count = 1;
    qi->features[0] = &g_moon; qi->features[1] = &g_feat; qi->feature_count = 2;
    QuadrantIndex *qn = &spatial_index[4][6][7];            /* neighbour on -x */
    memset(qn, 0, sizeof *qn);
    qn->asteroids[0] = &g_ast; qn->asteroid_count = 1;
}

static const ObsSphere *find_kind(const ObsSphere *o, int n, int kind) {
    for (int i = 0; i < n; i++) if (o[i].kind == kind) return &o[i];
    return NULL;
}

static void test_gather_quadrant(void) {
    ObsSphere o[OBS_MAX_PER_CALL];
    int n = obs_gather_quadrant(5, 6, 7, OBS_MASK_ALL, o, 0, OBS_MAX_PER_CALL);
    CHECK(n == 5, "planet, star, black hole, comet and moon (got %d)", n);
    const ObsSphere *p = find_kind(o, n, OBS_PLANET);
    CHECK(p && p->id == 5001, "the active planet is returned, the inactive one is not");
    CHECK(p && fabs(p->x - GX(5, 10)) < 1e-9 && fabs(p->y - GX(6, 10)) < 1e-9 && fabs(p->z - GX(7, 10)) < 1e-9,
          "planet centre in galactic coordinates");
    CHECK(p && fabs(p->r_hit - DIST_COLLISION_SHIP) < 1e-9 && fabs(p->r_avoid - (DIST_COLLISION_SHIP + OBS_AVOID_MARGIN)) < 1e-9,
          "solid radii come from the kind table");
    const ObsSphere *b = find_kind(o, n, OBS_BLACK_HOLE);
    CHECK(b && b->r_hit == 0.0 && b->r_avoid > DIST_GRAVITY_WELL, "black hole: not solid, keep-out beyond the well");
    CHECK(find_kind(o, n, OBS_MOON) != NULL, "moon type feature is an obstacle");
    int feats = 0;
    for (int i = 0; i < n; i++) if (o[i].id == 83002) feats++;
    CHECK(feats == 0, "a non-moon cosmic feature is not an obstacle");

    n = obs_gather_quadrant(5, 6, 7, OBS_MASK_NO_COMET, o, 0, OBS_MAX_PER_CALL);
    CHECK(n == 4 && find_kind(o, n, OBS_COMET) == NULL, "mask can leave the comets out");
    n = obs_gather_quadrant(5, 6, 7, OBS_BIT(OBS_STAR), o, 0, OBS_MAX_PER_CALL);
    CHECK(n == 1 && o[0].kind == OBS_STAR, "mask can select one kind");
    n = obs_gather_quadrant(5, 6, 7, OBS_MASK_ALL, o, 0, 2);
    CHECK(n == 2, "the output array limit is honoured (got %d)", n);

    CHECK(obs_gather_quadrant(0, 6, 7, OBS_MASK_ALL, o, 0, OBS_MAX_PER_CALL) == 0, "quadrant 0 is ignored");
    CHECK(obs_gather_quadrant(41, 6, 7, OBS_MASK_ALL, o, 0, OBS_MAX_PER_CALL) == 0, "quadrant 41 is ignored");
    CHECK(obs_gather_quadrant(-3, 6, 7, OBS_MASK_ALL, o, 0, OBS_MAX_PER_CALL) == 0, "negative quadrant is ignored");
}

static void test_gather_segment(void) {
    ObsSphere o[OBS_MAX_PER_CALL];
    /* a path that stays inside quadrant [5,6,7] near its -x face reaches the asteroid at [4,6,7] */
    double a[3] = { GX(5, 1.0), GX(6, 10), GX(7, 10) }, b[3] = { GX(5, 3.0), GX(6, 10), GX(7, 10) };
    int n = obs_gather_segment(a, b, 2.0, OBS_MASK_ALL, o, OBS_MAX_PER_CALL);
    CHECK(find_kind(o, n, OBS_ASTEROID) != NULL, "the neighbouring quadrant is scanned near the face");
    double c[3] = { GX(5, 20), GX(6, 20), GX(7, 20) }, d[3] = { GX(5, 22), GX(6, 20), GX(7, 20) };
    n = obs_gather_segment(c, d, 0.5, OBS_MASK_ALL, o, OBS_MAX_PER_CALL);
    CHECK(find_kind(o, n, OBS_ASTEROID) == NULL, "far from the face the neighbour is not scanned");

    double far0[3] = { 0.1, 0.1, 0.1 }, far1[3] = { 1599, 1599, 1599 };
    n = obs_gather_segment(far0, far1, 3.0, OBS_MASK_ALL, o, OBS_MAX_PER_CALL);
    CHECK(n >= 0 && n <= OBS_MAX_PER_CALL, "a galaxy-long segment is bounded (%d)", n);

    QuadrantIndex (*saved)[41][41] = spatial_index;
    spatial_index = NULL;
    CHECK(obs_gather_segment(a, b, 2.0, OBS_MASK_ALL, o, OBS_MAX_PER_CALL) == 0, "no index: nothing, no crash");
    spatial_index = saved;
}

static void test_segment_hit(void) {
    ObsHit h;
    /* warp 9.9: 4.6 units in one tick, straight through the planet at (10,10,10) */
    double a[3] = { GX(5, 7.7), GX(6, 10.2), GX(7, 10) }, b[3] = { GX(5, 12.3), GX(6, 10.2), GX(7, 10) };
    CHECK(obs_segment_hit(a, b, 0.0, OBS_MASK_ALL, 0, &h), "a fast ship is stopped by the planet");
    CHECK(h.kind == OBS_PLANET && h.id == 5001, "hit kind/id");
    double dx = h.point[0] - h.centre[0], dy = h.point[1] - h.centre[1], dz = h.point[2] - h.centre[2];
    CHECK(fabs(sqrt(dx * dx + dy * dy + dz * dz) - DIST_COLLISION_SHIP) < 1e-6, "contact point is on the surface");
    CHECK(h.t > 0.0 && h.t < 0.5, "contact before the middle of the step (t=%.3f)", h.t);
    CHECK(fabs(h.r_hit - DIST_COLLISION_SHIP) < 1e-9, "contact radius is reported");

    CHECK(!obs_segment_hit(a, b, 0.0, OBS_MASK_ALL, 5001, &h), "the approach target is skipped");

    /* black hole at (20,20,20): never a collision */
    double e[3] = { GX(5, 17), GX(6, 20), GX(7, 20) }, f[3] = { GX(5, 23), GX(6, 20), GX(7, 20) };
    CHECK(!obs_segment_hit(e, f, 0.0, OBS_MASK_ALL, 0, &h), "a black hole is not solid here");

    /* comet: solid for a ship, but a comet never collides with itself */
    double g0[3] = { GX(5, 3), GX(6, 5), GX(7, 5) }, g1[3] = { GX(5, 7), GX(6, 5), GX(7, 5) };
    CHECK(obs_segment_hit(g0, g1, 0.0, OBS_MASK_ALL, 0, &h) && h.kind == OBS_COMET, "ships can hit a comet");
    CHECK(!obs_segment_hit(g0, g1, 0.0, OBS_MASK_NO_COMET, 0, &h), "comets are left out of a comet's own test");

    /* moon */
    double m0[3] = { GX(5, 12), GX(6, 15), GX(7, 15) }, m1[3] = { GX(5, 18), GX(6, 15), GX(7, 15) };
    CHECK(obs_segment_hit(m0, m1, 0.0, OBS_MASK_ALL, 0, &h) && h.kind == OBS_MOON, "ships can hit a moon");

    /* path that misses everything */
    double n0[3] = { GX(5, 2), GX(6, 35), GX(7, 2) }, n1[3] = { GX(5, 6), GX(6, 35), GX(7, 2) };
    CHECK(!obs_segment_hit(n0, n1, 0.0, OBS_MASK_ALL, 0, &h), "an empty path has no hit");

    /* the asteroid in the neighbour quadrant, crossed across the face */
    /* asteroid: quadrant 4, local x QS - 0.5, just before the face where quadrant 5 starts (GX(5, 0)) */
    double t0[3] = { GX(5, 0) - 2.0, GX(6, 10), GX(7, 10) }, t1[3] = { GX(5, 0) + 1.0, GX(6, 10), GX(7, 10) };
    CHECK(obs_segment_hit(t0, t1, 0.0, OBS_MASK_ALL, 0, &h) && h.kind == OBS_ASTEROID,
          "a ship crossing the face hits the asteroid in the next quadrant");
}

/* NPC and player ships: avoidance-only obstacles (never a hit), the asking
 * NPC is left out, and OBS_MASK_ALL (what ships collide with) never has them. */
static void test_ships(void) {
    static NPCShip a, b, dead;
    static ConnectedPlayer pl;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); memset(&dead, 0, sizeof dead); memset(&pl, 0, sizeof pl);
    a.id = 100001; a.active = 1; a.gx = GX(5, 20); a.gy = GX(6, 30); a.gz = GX(7, 30);
    b.id = 100002; b.active = 1; b.gx = GX(5, 24); b.gy = GX(6, 30); b.gz = GX(7, 30);
    dead = b; dead.id = 100003; dead.death_timer = 5;                 /* being destroyed: not an obstacle */
    pl.active = 1; pl.gx = GX(5, 28); pl.gy = GX(6, 30); pl.gz = GX(7, 30);
    QuadrantIndex *qi = &spatial_index[5][6][7];
    qi->npcs[0] = &a; qi->npcs[1] = &b; qi->npcs[2] = &dead; qi->npc_count = 3;
    qi->players[0] = &pl; qi->player_count = 1;

    ObsSphere o[OBS_MAX_PER_CALL];
    int n = obs_gather_quadrant(5, 6, 7, OBS_MASK_ALL, o, 0, OBS_MAX_PER_CALL);
    CHECK(find_kind(o, n, OBS_SHIP) == NULL, "ships are not in OBS_MASK_ALL (collisions never see them)");
    n = obs_gather_quadrant(5, 6, 7, OBS_BIT(OBS_SHIP), o, 0, OBS_MAX_PER_CALL);
    CHECK(n == 3, "no self: two NPCs and one player are ships (%d)", n);
    n = obs_gather_quadrant_ex(5, 6, 7, OBS_BIT(OBS_SHIP), o, 0, OBS_MAX_PER_CALL, &a);
    CHECK(n == 2, "the asking NPC is not its own obstacle (%d)", n);
    int self_seen = 0;
    for (int i = 0; i < n; i++) if (o[i].id == 100001) self_seen = 1;
    CHECK(!self_seen, "NPC a is absent from its own list");
    n = obs_gather_quadrant_ex(5, 6, 7, OBS_BIT(OBS_SHIP), o, 0, OBS_MAX_PER_CALL, &pl);
    CHECK(n == 2, "a player that asks is left out too (%d)", n);
    n = obs_gather_quadrant_ex(5, 6, 7, OBS_BIT(OBS_SHIP), o, 0, OBS_MAX_PER_CALL, &a);
    CHECK(o[0].r_hit == 0.0 && fabs(o[0].r_avoid - (DIST_COLLISION_SHIP + OBS_SHIP_AVOID_MARGIN)) < 1e-9,
          "ships are never a hit, only keep-out (r_hit %.2f, r_avoid %.2f)", o[0].r_hit, o[0].r_avoid);
    CHECK(fabs(o[0].x - GX(5, 24)) < 1e-9 && fabs(o[1].x - GX(5, 28)) < 1e-9, "ships are in galactic coordinates");

    double p0[3] = { GX(5, 16), GX(6, 30), GX(7, 30) }, p1[3] = { GX(5, 28.5), GX(6, 30), GX(7, 30) };   /* stops short of the star at x=30 */
    ObsHit h;
    CHECK(!obs_segment_hit(p0, p1, 0.0, OBS_MASK_ALL, 0, &h), "a ship crossing other ships is not 'hit' (approach, boarding keep working)");
    n = obs_gather_segment_ex(p0, p1, 1.0, OBS_MASK_AVOID, o, OBS_MAX_PER_CALL, &a);
    CHECK(find_kind(o, n, OBS_SHIP) != NULL, "OBS_MASK_AVOID gathers ships along a segment (%d)", n);
    CHECK(strcmp(obs_kind_name(OBS_SHIP), "ship") == 0, "ship name");

    qi->npc_count = 0; qi->player_count = 0;
}

static void test_kind_names(void) {
    CHECK(strcmp(obs_kind_name(OBS_PLANET), "planet") == 0, "planet name");
    CHECK(strcmp(obs_kind_name(OBS_COMET), "comet") == 0, "comet name");
    CHECK(strcmp(obs_kind_name(-1), "object") == 0 && strcmp(obs_kind_name(999), "object") == 0, "unknown kind");
    CHECK(obs_is_moon_type(OBJ_MOON_FIRST) && obs_is_moon_type(OBJ_MOON_LAST) &&
          obs_is_moon_type(OBJ_MOON_EXT_FIRST) && obs_is_moon_type(OBJ_MOON_EXT_LAST) &&
          !obs_is_moon_type(OBJ_MOON_FIRST - 1) && !obs_is_moon_type(OBJ_MOON_LAST + 1) &&
          !obs_is_moon_type(OBJ_MOON_EXT_LAST + 1), "moon type ranges");
}


/* Star system: 1 star, planet 0 with 2 moons, planet 1 without moons, in quadrant
 * (8,8,8). Planets and moons move, so the provider computes their positions. */
static void test_star_system(void) {
    const int Q1 = 8, Q2 = 8, Q3 = 8;
    memset(&sys_stars[0], 0, sizeof sys_stars[0]);
    sys_stars[0] = (NPCSysStar){ .id = GALAXY_OBJECT_MIN_SYS_STAR, .q1 = Q1, .q2 = Q2, .q3 = Q3,
                                 .x = 20, .y = 20, .z = 20, .seed = 4242u, .n_planets = 2, .active = 1 };
    sys_planets[0] = (NPCSysPlanet){ .id = GALAXY_OBJECT_MIN_SYS_PLANET, .q1 = Q1, .q2 = Q2, .q3 = Q3,
                                     .n_moons = 2, .orbit_r = 14.5, .active = 1 };
    sys_planets[1] = (NPCSysPlanet){ .id = GALAXY_OBJECT_MIN_SYS_PLANET + 1, .q1 = Q1, .q2 = Q2, .q3 = Q3,
                                     .n_moons = 0, .orbit_r = 24.0, .active = 1 };
    sys_planets[2] = (NPCSysPlanet){ .id = GALAXY_OBJECT_MIN_SYS_PLANET + 2, .q1 = Q1, .q2 = Q2, .q3 = Q3,
                                     .n_moons = 0, .orbit_r = 30.0, .active = 0 };   /* inactive */
    sys_moons[0] = (NPCSysMoon){ .id = GALAXY_OBJECT_MIN_SYS_MOON, .q1 = Q1, .q2 = Q2, .q3 = Q3,
                                 .moon_type = OBJ_MOON_FIRST + 5, .active = 1 };
    sys_moons[1] = (NPCSysMoon){ .id = GALAXY_OBJECT_MIN_SYS_MOON + 1, .q1 = Q1, .q2 = Q2, .q3 = Q3,
                                 .moon_type = OBJ_MOON_FIRST, .active = 1 };
    QuadrantIndex *qi = &spatial_index[Q1][Q2][Q3];
    memset(qi, 0, sizeof *qi);
    qi->sys_stars[0] = &sys_stars[0]; qi->sys_star_count = 1;
    for (int k = 0; k < 3; k++) qi->sys_planets[k] = &sys_planets[k];
    qi->sys_planet_count = 3;
    qi->sys_moons[0] = &sys_moons[0]; qi->sys_moons[1] = &sys_moons[1]; qi->sys_moon_count = 2;

    ObsSphere o[OBS_MAX_PER_CALL];
    int n = obs_gather_quadrant(Q1, Q2, Q3, OBS_MASK_ALL, o, 0, OBS_MAX_PER_CALL);
    CHECK(n == 5, "system: star + 2 planets + 2 moons, the inactive planet is left out (got %d)", n);
    CHECK(find_kind(o, n, OBS_SYS_STAR) && find_kind(o, n, OBS_SYS_PLANET) && find_kind(o, n, OBS_SYS_MOON),
          "system: all three kinds are returned");

    const ObsSphere *st = find_kind(o, n, OBS_SYS_STAR);
    CHECK(st && fabs(st->x - GX(Q1, 20)) < 1e-9 && fabs(st->y - GX(Q2, 20)) < 1e-9 && fabs(st->z - GX(Q3, 20)) < 1e-9,
          "system: star position in galactic coordinates");
    CHECK(st && fabs(st->r_hit - (SYS_STAR_RADIUS + DIST_COLLISION_SHIP)) < 1e-9, "system: star contact radius is per type (visual radius + 0.8)");
    CHECK(st && st->r_hit > DIST_COLLISION_SHIP && st->r_avoid > st->r_hit, "system: star r_hit larger than the generic one, keep-out beyond it");

    int pid[2] = { GALAXY_OBJECT_MIN_SYS_PLANET, GALAXY_OBJECT_MIN_SYS_PLANET + 1 };
    double pos[2][3] = {{0}};
    int found = 0;
    for (int i = 0; i < n; i++) {
        if (o[i].kind != OBS_SYS_PLANET) continue;
        for (int k = 0; k < 2; k++) if (o[i].id == pid[k]) {
            pos[k][0] = o[i].x; pos[k][1] = o[i].y; pos[k][2] = o[i].z; found++;
            double d = sqrt(pow(o[i].x - st->x, 2) + pow(o[i].y - st->y, 2) + pow(o[i].z - st->z, 2));
            CHECK(fabs(d - sys_planets[k].orbit_r) < 1e-6, "system: planet %d is at its orbit radius from the star (%.6f)", k, d);
            CHECK(fabs(o[i].r_hit - (SYS_PLANET_RADIUS + DIST_COLLISION_SHIP)) < 1e-9, "system: planet contact radius");
        }
    }
    CHECK(found == 2, "system: both active planets found (%d)", found);

    for (int i = 0; i < n; i++) {
        if (o[i].kind != OBS_SYS_MOON) continue;
        int j = o[i].id - GALAXY_OBJECT_MIN_SYS_MOON;
        double d = sqrt(pow(o[i].x - pos[0][0], 2) + pow(o[i].y - pos[0][1], 2) + pow(o[i].z - pos[0][2], 2));
        double lo = MOON_ORBIT_MIN_RADIUS + MOON_ORBIT_STEP * j;
        CHECK(d >= lo - 1e-6 && d <= lo + MOON_ORBIT_JITTER + 1e-6, "system: moon %d orbits its planet at ordinal radius (%.4f)", j, d);
        CHECK(fabs(o[i].r_hit - sys_moon_hit_radius(sys_moons[j].moon_type)) < 1e-9, "system: moon contact radius depends on the moon type");
    }
    CHECK(sys_moon_hit_radius(OBJ_MOON_FIRST + 5) != sys_moon_hit_radius(OBJ_MOON_FIRST), "system: two moon types, two contact radii");

    n = obs_gather_quadrant(Q1, Q2, Q3, OBS_BIT(OBS_SYS_MOON), o, 0, OBS_MAX_PER_CALL);
    CHECK(n == 2, "system: mask selects the moons only (%d)", n);
    n = obs_gather_quadrant(Q1, Q2, Q3, OBS_MASK_ALL, o, 0, 3);
    CHECK(n == 3, "system: the output limit is honoured (%d)", n);

    /* the contact radius of the star is the per-type one: a ship passing 3.0
     * from its centre hits it (3.3), one passing at 4.0 does not (the generic
     * 0.8 would have let the first one fly through the visible sphere) */
    double c = GX(Q1, 20), a1[3] = { c - 10, c + 3.0, c }, b1[3] = { c + 10, c + 3.0, c };
    double a2[3] = { c - 10, c + 4.0, c }, b2[3] = { c + 10, c + 4.0, c };
    ObsHit h;
    CHECK(obs_segment_hit(a1, b1, 0.0, OBS_BIT(OBS_SYS_STAR), 0, &h) && h.kind == OBS_SYS_STAR && h.id == GALAXY_OBJECT_MIN_SYS_STAR,
          "system: a ship passing 3.0 from the star centre hits the star");
    CHECK(!obs_segment_hit(a2, b2, 0.0, OBS_BIT(OBS_SYS_STAR), 0, &h), "system: a ship passing 4.0 away does not");
    CHECK(OBS_MASK_ALL & OBS_BIT(OBS_SYS_STAR) && OBS_MASK_ALL & OBS_BIT(OBS_SYS_PLANET) && OBS_MASK_ALL & OBS_BIT(OBS_SYS_MOON) &&
          !(OBS_MASK_ALL & OBS_BIT(OBS_SHIP)), "system kinds are solid (in OBS_MASK_ALL), ships still are not");

    memset(qi, 0, sizeof *qi);
    n = obs_gather_quadrant(Q1, Q2, Q3, OBS_MASK_ALL, o, 0, OBS_MAX_PER_CALL);
    CHECK(n == 0, "system: an empty quadrant gathers nothing");
}

int main(void) {
    spatial_index = calloc((size_t)41 * 41 * 41, sizeof(QuadrantIndex));
    if (!spatial_index) { printf("cannot allocate the test index\n"); return 1; }
    plant();
    test_gather_quadrant();
    test_gather_segment();
    test_segment_hit();
    test_ships();
    test_kind_names();
    test_star_system();
    free(spatial_index);
    printf("obstacles_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
