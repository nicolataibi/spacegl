/*
 * star_system_test.c - contract of the star systems (include/star_system.h).
 *
 * One star, up to 4 planets, up to 4 moons per planet, all inside ONE
 * quadrant: slot arithmetic and id bands, per-type contact radii, and the
 * containment of every body at every time for every layout, at the current
 * quadrant size and at 100.
 */
#include <stdio.h>
#include <math.h>
#include "star_system.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; \
    printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_bands_and_slots(void) {
    CHECK(SYS_MAX_PLANETS == 40000 && SYS_MAX_MOONS == 160000, "capacities 40000 / 160000");
    CHECK(GALAXY_OBJECT_MAX_SYS_STAR   - GALAXY_OBJECT_MIN_SYS_STAR   + 1 == SYS_MAX_STARS,   "star band spans SYS_MAX_STARS");
    CHECK(GALAXY_OBJECT_MAX_SYS_PLANET - GALAXY_OBJECT_MIN_SYS_PLANET + 1 == SYS_MAX_PLANETS, "planet band spans SYS_MAX_PLANETS");
    CHECK(GALAXY_OBJECT_MAX_SYS_MOON   - GALAXY_OBJECT_MIN_SYS_MOON   + 1 == SYS_MAX_MOONS,   "moon band spans SYS_MAX_MOONS");
    CHECK(GALAXY_OBJECT_MIN_SYS_STAR > 83000 + 2048 - 1, "bands start above the feature band end (85047)");
    CHECK(GALAXY_OBJECT_MAX_SYS_STAR + 1 == GALAXY_OBJECT_MIN_SYS_PLANET - 0 - 0 || GALAXY_OBJECT_MAX_SYS_STAR < GALAXY_OBJECT_MIN_SYS_PLANET, "star band below planet band");
    CHECK(GALAXY_OBJECT_MAX_SYS_PLANET < GALAXY_OBJECT_MIN_SYS_MOON, "planet band below moon band");
    CHECK((long)GALAXY_OBJECT_MAX_SYS_MOON < 2147483647L, "ids fit int32");
    int bad = 0;
    for (int s = 0; s < SYS_MAX_STARS; s += 37)
        for (int k = 0; k < SYS_PLANETS_PER_STAR; k++) {
            int ps = sys_planet_slot(s, k);
            if (sys_planet_star(ps) != s || sys_planet_ordinal(ps) != k) bad++;
            for (int j = 0; j < SYS_MOONS_PER_PLANET; j++) {
                int ms = sys_moon_slot(s, k, j);
                if (sys_moon_star(ms) != s || sys_moon_planet(ms) != ps || sys_moon_ordinal(ms) != j) bad++;
                if (ms < 0 || ms >= SYS_MAX_MOONS) bad++;
            }
        }
    CHECK(bad == 0, "slot arithmetic round-trips (%d errors)", bad);
    CHECK(sys_moon_slot(SYS_MAX_STARS - 1, 3, 3) == SYS_MAX_MOONS - 1, "last moon slot is the last slot");
    CHECK(sys_id_is_star(90000) && sys_id_is_star(99999) && !sys_id_is_star(89999) && !sys_id_is_star(100000), "star id bounds");
    CHECK(sys_id_is_planet(100000) && sys_id_is_planet(139999) && !sys_id_is_planet(99999) && !sys_id_is_planet(140000), "planet id bounds");
    CHECK(sys_id_is_moon(140000) && sys_id_is_moon(299999) && !sys_id_is_moon(139999) && !sys_id_is_moon(300000), "moon id bounds");
}

static void test_radii(void) {
    CHECK(fabs(sys_star_hit_radius()   - (2.5 + 0.8)) < 1e-6, "star contact radius = visual radius + 0.8");
    CHECK(fabs(sys_planet_hit_radius() - (1.8 + 0.8)) < 1e-6, "planet contact radius = visual radius + 0.8");
    CHECK(sys_star_hit_radius() > sys_planet_hit_radius() && sys_planet_hit_radius() > sys_moon_hit_radius(OBJ_MOON_FIRST + 5),
          "star > planet > moon");
    CHECK(sys_moon_hit_radius(OBJ_MOON_FIRST) >= (double)DIST_COLLISION_SHIP, "moon never below the generic contact radius");
}

static int check_system_inside(unsigned seed, double Q, int *planets_hist, int *moons_total) {
    SysLayout L;
    if (!sys_layout_make(seed, Q, &L)) return -1;
    double star[3];
    sys_star_position(seed, Q, L.r_sys, star);
    planets_hist[L.n_planets]++;
    double lo = 0.0, hi = Q;
    /* the star clears every face by the whole system radius */
    for (int i = 0; i < 3; i++) if (star[i] - L.r_sys < lo || star[i] + L.r_sys > hi) return 1;
    for (int k = 0; k < L.n_planets; k++) {
        if (L.n_moons[k] < 0 || L.n_moons[k] > SYS_MOONS_PER_PLANET) return 2;
        /* moons never reach the star */
        double inner = L.planet_r[k] - sys_moon_envelope(L.n_moons[k]);
        if (L.n_moons[k] > 0 && inner < SYS_STAR_RADIUS + SYS_MOON_MAX_RADIUS) return 3;
        if (L.planet_r[k] - SYS_PLANET_RADIUS < SYS_STAR_RADIUS) return 4;
        if (k > 0 && L.planet_r[k] <= L.planet_r[k - 1]) return 5;
        *moons_total += L.n_moons[k];
        for (int ti = 0; ti < 6; ti++) {
            double t = 137.0 * ti + (double)(seed % 1000);
            double p[3]; sys_planet_pos(seed, k, L.planet_r[k], star, t, p);
            for (int i = 0; i < 3; i++) if (p[i] < lo || p[i] > hi) return 6;
            for (int j = 0; j < L.n_moons[k]; j++) {
                double m[3]; unsigned id = (unsigned)sys_moon_slot((int)(seed % SYS_MAX_STARS), k, j);
                sys_moon_pos(id, OBJ_MOON_FIRST + (int)(id % 10), j, p, t, m);
                for (int i = 0; i < 3; i++) if (m[i] < lo || m[i] > hi) return 7;
            }
        }
    }
    return 0;
}

static void test_containment(double Q, int n_seeds) {
    int hist[SYS_PLANETS_PER_STAR + 1] = {0}, moons = 0, fail_code = 0, failed = 0, unfit = 0;
    for (int n = 0; n < n_seeds; n++) {
        int r = check_system_inside(2654435761u * (unsigned)(n + 1), Q, hist, &moons);
        if (r < 0) unfit++;
        else if (r > 0) { failed++; fail_code = r; }
    }
    CHECK(unfit == 0, "Q=%.0f: every drawn system finds a layout (%d did not)", Q, unfit);
    CHECK(failed == 0, "Q=%.0f: every body stays inside the quadrant at every sampled time (%d systems, code %d)", Q, failed, fail_code);
    printf("  Q=%.0f over %d systems: planets 1/2/3/4 = %d/%d/%d/%d, mean moons per system %.2f\n",
           Q, n_seeds, hist[1], hist[2], hist[3], hist[4], (double)moons / n_seeds);
}

static void test_determinism(void) {
    SysLayout a, b;
    sys_layout_make(12345u, 100.0, &a); sys_layout_make(12345u, 100.0, &b);
    CHECK(a.n_planets == b.n_planets && a.r_sys == b.r_sys && a.planet_r[0] == b.planet_r[0], "layout is deterministic");
    double s[3] = {50, 50, 50}, p1[3], p2[3];
    sys_planet_pos(777u, 1, 20.0, s, 1000.0, p1); sys_planet_pos(777u, 1, 20.0, s, 1000.0, p2);
    CHECK(p1[0] == p2[0] && p1[1] == p2[1] && p1[2] == p2[2], "planet position is a pure function of time");
    double q[3]; sys_planet_pos(777u, 1, 20.0, s, 1060.0, q);
    CHECK(fabs(q[0]-p1[0]) + fabs(q[1]-p1[1]) + fabs(q[2]-p1[2]) > 1e-6, "planet moves with time");
}

int main(void) {
    printf("star_system_test\n");
    test_bands_and_slots();
    test_radii();
    test_determinism();
    test_containment(100.0, 20000);
    test_containment((double)QUADRANT_SIZE, 20000);
    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
