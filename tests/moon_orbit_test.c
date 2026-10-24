/*
 * moon_orbit_test.c - contract of the natural-moon orbits (include/moon_orbits.h).
 *
 * Every moon orbits one planet on an orbit of its own, a planet has at most
 * MOON_MAX_PER_PLANET moons, the orbits are slow, the moon never leaves the
 * planet's quadrant and the assignment is deterministic (nothing is saved in
 * galaxy.dat, so a restart must give the same parents).
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "object_types.h"
#include "moon_orbits.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; \
    printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define QS 40.0

static void test_constants(void) {
    CHECK(MOON_MAX_PER_PLANET == 4, "at most 4 moons per planet");
    CHECK(MOON_ORBIT_JITTER < MOON_ORBIT_STEP, "jitter must stay below the step between orbits");
    CHECK(2.0 * MOON_ORBIT_MAX_RADIUS < QS, "largest orbit fits in a quadrant");
    CHECK(moon_planet_eligible(QS / 2, QS / 2, QS / 2, QS), "centre planet is eligible");
    CHECK(!moon_planet_eligible(0.5, QS / 2, QS / 2, QS), "planet near a face is not eligible");
    CHECK(!moon_planet_eligible(QS / 2, QS / 2, QS - 0.5, QS), "planet near the far face is not eligible");
}

static void test_individual_orbits(void) {
    /* the 4 moons of one planet: distinct, increasing radii (single orbits) */
    for (unsigned base = 0; base < 50; base++) {
        MoonOrbit o[MOON_MAX_PER_PLANET];
        for (int k = 0; k < MOON_MAX_PER_PLANET; k++)
            moon_orbit_make(83000u + base * 7u + (unsigned)k, k, k, &o[k]);
        for (int k = 0; k < MOON_MAX_PER_PLANET; k++) {
            CHECK(o[k].r >= MOON_ORBIT_MIN_RADIUS && o[k].r <= MOON_ORBIT_MAX_RADIUS,
                  "radius %.3f out of range", o[k].r);
            if (k > 0)
                CHECK(o[k].r - o[k - 1].r >= MOON_ORBIT_STEP - MOON_ORBIT_JITTER - 1e-9,
                      "orbits %d and %d too close (%.3f)", k - 1, k, o[k].r - o[k - 1].r);
        }
    }
}

static void test_geometry_and_speed(void) {
    double maxspeed = 0.0;
    for (unsigned id = 83000; id < 83100; id++) {
        int mi = (int)(id % OBJ_MOON_COUNT), ord = (int)(id % MOON_MAX_PER_PLANET);
        MoonOrbit o;
        moon_orbit_make(id, mi, ord, &o);
        double uu = o.u[0]*o.u[0] + o.u[1]*o.u[1] + o.u[2]*o.u[2];
        double vv = o.v[0]*o.v[0] + o.v[1]*o.v[1] + o.v[2]*o.v[2];
        double uv = o.u[0]*o.v[0] + o.u[1]*o.v[1] + o.u[2]*o.v[2];
        CHECK(fabs(uu - 1) < 1e-9 && fabs(vv - 1) < 1e-9 && fabs(uv) < 1e-9,
              "moon %u: orbital plane basis is not orthonormal", id);
        /* retrograde only for the irregular moon (index 8) */
        CHECK((o.omega < 0) == (mi == 8), "moon %u (index %d): wrong direction", id, mi);
        double speed = o.r * fabs(o.omega);                  /* units / second */
        if (speed > maxspeed) maxspeed = speed;
        for (double t = 0; t < 1e9; t = t * 3 + 12345.6) {
            double d[3];
            moon_orbit_offset(&o, t, d);
            double dist = sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
            CHECK(fabs(dist - o.r) < 1e-9, "moon %u at t=%.0f: distance %.9f != r %.9f", id, t, dist, o.r);
            CHECK(fabs(d[0]) <= MOON_ORBIT_MAX_RADIUS && fabs(d[1]) <= MOON_ORBIT_MAX_RADIUS &&
                  fabs(d[2]) <= MOON_ORBIT_MAX_RADIUS, "moon %u leaves the safety margin", id);
        }
        /* periodic */
        double T = fabs(6.283185307179586 / o.omega), a[3], b[3];
        moon_orbit_offset(&o, 5000.0, a);
        moon_orbit_offset(&o, 5000.0 + T, b);
        CHECK(fabs(a[0]-b[0]) < 1e-6 && fabs(a[1]-b[1]) < 1e-6 && fabs(a[2]-b[2]) < 1e-6,
              "moon %u is not periodic", id);
        CHECK(T >= MOON_ORBIT_BASE_PERIOD_S - 1e-6, "moon %u period %.0fs shorter than the base period", id, T);
    }
    CHECK(maxspeed < 0.5, "moons orbit speed: top speed %.4f units/s", maxspeed);
}

static void test_stays_in_quadrant(void) {
    /* planet exactly at the eligibility edge: the moon must stay inside */
    double m = MOON_ORBIT_MAX_RADIUS;
    double corners[2] = { m, QS - m };
    int bad = 0;
    for (int cx = 0; cx < 2; cx++) for (int cy = 0; cy < 2; cy++) for (int cz = 0; cz < 2; cz++) {
        CHECK(moon_planet_eligible(corners[cx], corners[cy], corners[cz], QS), "corner eligible");
        for (unsigned id = 83000; id < 83100; id++) {
            MoonOrbit o;
            moon_orbit_make(id, (int)(id % OBJ_MOON_COUNT), (int)(id % MOON_MAX_PER_PLANET), &o);
            for (double t = 0; t < 7200; t += 37.0) {
                double d[3];
                moon_orbit_offset(&o, t, d);
                double x = corners[cx] + d[0], y = corners[cy] + d[1], z = corners[cz] + d[2];
                if (x < 0 || x >= QS || y < 0 || y >= QS || z < 0 || z >= QS) bad++;
            }
        }
    }
    CHECK(bad == 0, "%d positions outside the quadrant", bad);
}

static void assign_all(int n_planets, int *parent, int *placed, int *maxcount) {
    int pcount[2000] = {0};
    *placed = 0; *maxcount = 0;
    for (int s = 0; s < OBJ_MOON_COUNT * 10; s++) {
        unsigned id = 83000u + 320u + 170u + (unsigned)s;   /* moon slots follow exotic + evolved */
        int j = moon_pick_planet(id, n_planets, pcount);
        parent[s] = j;
        if (j >= 0) { pcount[j]++; (*placed)++; if (pcount[j] > *maxcount) *maxcount = pcount[j]; }
    }
}

static void test_assignment(void) {
    int pa[100], pb[100], placed, maxc;
    assign_all(400, pa, &placed, &maxc);
    CHECK(placed == 100, "all 100 moons placed with 400 eligible planets, got %d", placed);
    CHECK(maxc <= MOON_MAX_PER_PLANET, "a planet got %d moons (max %d)", maxc, MOON_MAX_PER_PLANET);
    assign_all(400, pb, &placed, &maxc);
    CHECK(memcmp(pa, pb, sizeof(pa)) == 0, "assignment is not deterministic");

    /* few planets: capacity is exactly 4 per planet, the surplus stays unplaced */
    assign_all(30, pa, &placed, &maxc);
    CHECK(placed == 100 && maxc <= 4, "30 planets hold 120 >= 100 moons, got %d (max %d)", placed, maxc);
    assign_all(20, pa, &placed, &maxc);
    CHECK(placed == 80 && maxc == 4, "20 planets hold exactly 80 moons, got %d (max %d)", placed, maxc);
    assign_all(0, pa, &placed, &maxc);
    CHECK(placed == 0, "no planet, no moon placed");

    /* the moons are spread, not all piled on the same planets */
    assign_all(400, pa, &placed, &maxc);
    int used = 0, seen[400] = {0};
    for (int s = 0; s < 100; s++) if (pa[s] >= 0 && !seen[pa[s]]++) used++;
    CHECK(used >= 40, "moons concentrated on only %d planets", used);

    /* every planet gets at least one moon when moon count >= planet count */
    int p80[100];
    assign_all(80, p80, &placed, &maxc);
    int pcount_check[80] = {0};
    for (int s = 0; s < 100; s++) if (p80[s] >= 0) pcount_check[p80[s]]++;
    int zero_moons = 0;
    for (int j = 0; j < 80; j++) if (pcount_check[j] == 0) zero_moons++;
    CHECK(zero_moons == 0, "every planet must have at least one moon, got %d with none", zero_moons);
}

int main(void) {
    printf("Moon orbit test\n");
    test_constants();
    test_individual_orbits();
    test_geometry_and_speed();
    test_stays_in_quadrant();
    test_assignment();
    printf("  %d checks passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) { printf("PASS\n"); return 0; }
    printf("FAIL\n");
    return 1;
}
