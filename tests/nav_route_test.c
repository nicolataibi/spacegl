/*
 * nav_route_test.c - contract of include/nav_route.h (the `nav` course).
 *
 * A course must (a) be the straight line when nothing is in the way,
 * (b) never let any leg enter a keep-out sphere (r_avoid + extra) - and so
 * never a contact sphere - on single obstacles, walls of overlapping ones
 * and thousands of random fields, (c) end at the goal, or at the edge of
 * the keep-out sphere that holds it, (d) leave a keep-out sphere the start
 * is in, (e) stay in the allowed box, (f) stop short, flagged PARTIAL, when
 * the goal is walled in, (g) be deterministic, (h) report the obstacles it
 * bends around, and nav_route_first_hit must see an object that moves onto
 * the course.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "nav_route.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) \
    do { \
        if (cond) g_pass++; \
        else { \
            g_fail++; \
            printf("  FAIL: "); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

static ObsSphere sph(double x, double y, double z, double rh, double ra, int id) {
    ObsSphere o = { x, y, z, rh, ra, 0, id };
    return o;
}

static NavRouteCfg cfg_box(double lo, double hi) {
    NavRouteCfg c;
    memset(&c, 0, sizeof(c));
    for (int a = 0; a < 3; a++) {
        c.lo[a] = lo;
        c.hi[a] = hi;
    }
    c.extra = 0.0;
    c.seed = 7;
    return c;
}

/* smallest distance of the course to the centre of o, minus its keep-out radius */
static double clearance(const NavRoute *r, const ObsSphere *o, int from) {
    double c[3] = { o->x, o->y, o->z };
    double dmin = HUGE_VAL;
    for (int k = from + 1; k < r->n; k++) {
        double d = nr_point_seg_dist(c, r->p[k - 1], r->p[k]);
        if (d < dmin) dmin = d;
    }
    return dmin - o->r_avoid;
}

/* every leg outside every keep-out sphere (a leg that starts inside one and
 * leaves it is fine: sa_segment_enter semantics). Returns the offending
 * obstacle or -1. */
static int course_enters(const NavRoute *r, const ObsSphere *obs, int n, const NavRouteCfg *c) {
    for (int k = 1; k < r->n; k++) {
        double t;
        int i = nr_first_block(c, obs, n, r->p[k - 1], r->p[k], &t);
        if (i >= 0) return i;
    }
    return -1;
}

static int course_hits(const NavRoute *r, const ObsSphere *obs, int n) {
    for (int k = 1; k < r->n; k++) {
        double t;
        int i = sa_first_hit(r->p[k - 1], r->p[k], obs, n, 0.0, 0, &t);
        if (i >= 0) return i;
    }
    return -1;
}

static void test_free_path(void) {
    NavRouteCfg c = cfg_box(0, 100);
    ObsSphere obs[2] = { sph(50, 30, 50, 0.8, 2.0, 1), sph(10, 90, 10, 0.8, 2.0, 2) };
    double s[3] = { 10, 50, 50 }, g[3] = { 90, 50, 50 };
    NavRoute r;
    int rc = nav_route_plan(s, g, obs, 2, &c, &r);
    CHECK(rc == 0 && r.n == 2, "free path: straight line (n=%d)", r.n);
    CHECK(r.flags == 0, "free path: no flags (0x%x)", r.flags);
    CHECK(fabs(r.length - 80.0) < 1e-9, "free path length %.6f", r.length);
    CHECK(r.n_hazards == 0, "free path: no hazards (%d)", r.n_hazards);
}

static void test_single_obstacle(void) {
    NavRouteCfg c = cfg_box(0, 100);
    ObsSphere o = sph(50, 50, 50, 0.8, 2.0, 1);
    double s[3] = { 10, 50, 50 }, g[3] = { 90, 50, 50 };    /* dead centre */
    NavRoute r;
    int rc = nav_route_plan(s, g, &o, 1, &c, &r);
    CHECK(rc == 0 && r.n >= 3, "head-on: detour (n=%d)", r.n);
    CHECK(r.flags & NAV_RF_DETOUR, "head-on: DETOUR flag");
    CHECK(clearance(&r, &o, 0) > 0.0, "head-on: clears keep-out (%.4f)", clearance(&r, &o, 0));
    CHECK(nr_dist(r.p[r.n - 1], g) < 1e-9, "head-on: ends at goal");
    CHECK(r.length < 80.0 + 1.0, "head-on: short detour (%.3f)", r.length);
    CHECK(r.n_hazards == 1 && r.hazard[0] == 0, "head-on: reports the obstacle as hazard");

    /* off-centre: must bend on the side the line already passes */
    double s2[3] = { 10, 51.0, 50 }, g2[3] = { 90, 51.0, 50 };
    rc = nav_route_plan(s2, g2, &o, 1, &c, &r);
    int side_ok = 1;
    for (int k = 1; k < r.n - 1; k++) side_ok &= (r.p[k][1] > 50.0);
    CHECK(rc == 0 && side_ok, "off-centre: detour on the near side");
    CHECK(clearance(&r, &o, 0) > 0.0, "off-centre: clears keep-out");
}

static void test_wall(void) {
    /* wall of overlapping spheres across the straight line (plane x = 50) */
    NavRouteCfg c = cfg_box(0, 100);
    ObsSphere obs[64];
    int n = 0;
    for (int y = -3; y <= 3; y++)
        for (int z = -2; z <= 2; z++) {
            obs[n] = sph(50, 50 + y * 2.5, 50 + z * 2.5, 0.8, 2.0, n + 1);
            n++;
        }
    double s[3] = { 20, 50, 50 }, g[3] = { 80, 50, 50 };
    NavRoute r;
    int rc = nav_route_plan(s, g, obs, n, &c, &r);
    CHECK(rc == 0 && !(r.flags & NAV_RF_PARTIAL), "wall: way around found (flags 0x%x)", r.flags);
    CHECK(course_enters(&r, obs, n, &c) < 0, "wall: no leg enters a keep-out sphere");
    CHECK(nr_dist(r.p[r.n - 1], g) < 1e-9, "wall: ends at goal");
    CHECK(r.n_hazards >= 1, "wall: hazards reported (%d)", r.n_hazards);
}

static void test_goal_inside(void) {
    NavRouteCfg c = cfg_box(0, 100);
    ObsSphere o = sph(70, 50, 50, 0.8, 2.0, 1);
    double s[3] = { 10, 50, 50 }, g[3] = { 70.5, 50, 50 };   /* aimed at the planet */
    NavRoute r;
    int rc = nav_route_plan(s, g, &o, 1, &c, &r);
    CHECK(rc == 0, "goal inside: planned");
    CHECK(r.flags & NAV_RF_GOAL_MOVED, "goal inside: GOAL_MOVED");
    CHECK(r.goal_obstacle == 0, "goal inside: goal_obstacle");
    double c3[3] = { 70, 50, 50 };
    double de = nr_dist(r.p[r.n - 1], c3);
    CHECK(de > 2.0 && de < 2.6, "goal inside: ends at the edge (%.3f)", de);
    CHECK(course_enters(&r, &o, 1, &c) < 0, "goal inside: never enters");

    /* goal exactly on the centre: approached from the start side */
    double g2[3] = { 70, 50, 50 };
    rc = nav_route_plan(s, g2, &o, 1, &c, &r);
    CHECK(rc == 0 && r.p[r.n - 1][0] < 70.0, "goal on centre: stops on the near side");
}

static void test_start_inside(void) {
    NavRouteCfg c = cfg_box(0, 100);
    ObsSphere o = sph(50, 50, 50, 0.8, 2.0, 1);
    double s[3] = { 48.8, 50, 50 }, g[3] = { 90, 50, 50 };   /* parked, goal behind the planet */
    NavRoute r;
    int rc = nav_route_plan(s, g, &o, 1, &c, &r);
    CHECK(rc == 0 && (r.flags & NAV_RF_START_INSIDE), "start inside: leaves first (0x%x)", r.flags);
    CHECK(course_enters(&r, &o, 1, &c) < 0, "start inside: legs never move inward");
    CHECK(course_hits(&r, &o, 1) < 0, "start inside: no contact");
    CHECK(nr_dist(r.p[r.n - 1], g) < 1e-9, "start inside: ends at goal");

    /* inside but the goal is away from the centre: no exit leg needed */
    double g2[3] = { 10, 50, 50 };
    rc = nav_route_plan(s, g2, &o, 1, &c, &r);
    CHECK(rc == 0 && r.n == 2, "start inside, flying away: straight (n=%d)", r.n);
}

static void test_bounds(void) {
    NavRouteCfg c = cfg_box(0, 100);
    /* obstacle hugging the floor (z ~ 1): detours must not go below 0 */
    ObsSphere o = sph(50, 50, 1.5, 0.8, 2.0, 1);
    double s[3] = { 10, 50, 1.5 }, g[3] = { 90, 50, 1.5 };
    NavRoute r;
    int rc = nav_route_plan(s, g, &o, 1, &c, &r);
    int in = 1;
    for (int k = 0; k < r.n; k++) in &= nr_in_box(&c, r.p[k]);
    CHECK(rc == 0 && in, "bounds: every waypoint inside the box");
    CHECK(clearance(&r, &o, 0) > 0.0, "bounds: clears keep-out");

    double g2[3] = { 150, 50, 50 }, s2[3] = { 10, 50, 50 };
    rc = nav_route_plan(s2, g2, NULL, 0, &c, &r);
    CHECK(rc == 0 && (r.flags & NAV_RF_BOUNDED) && fabs(r.p[r.n - 1][0] - 100.0) < 1e-9,
          "bounds: goal clamped into the box");
}

static void test_walled_in(void) {
    /* goal enclosed by a shell of spheres: no way in -> PARTIAL, stops short */
    NavRouteCfg c = cfg_box(0, 100);
    ObsSphere obs[512];
    int n = 0;
    double R = 6.0;
    for (int i = 0; i < 24 && n < 512; i++) {
        double th = acos(1.0 - 2.0 * (i + 0.5) / 24.0);
        for (int j = 0; j < 16 && n < 512; j++) {
            double ph = j * 2.0 * M_PI / 16.0;
            obs[n] = sph(70 + R * sin(th) * cos(ph), 50 + R * sin(th) * sin(ph), 50 + R * cos(th), 0.8, 2.0, n + 1);
            n++;
        }
    }
    double s[3] = { 10, 50, 50 }, g[3] = { 70, 50, 50 };
    NavRoute r;
    int rc = nav_route_plan(s, g, obs, n, &c, &r);
    CHECK(rc == 0 && (r.flags & NAV_RF_PARTIAL), "walled in: PARTIAL (rc %d flags 0x%x)", rc, r.flags);
    CHECK(course_enters(&r, obs, n, &c) < 0, "walled in: the part flown is safe");
    CHECK(r.block_obstacle >= 0, "walled in: names the blocking obstacle");
}

static unsigned g_rng = 12345u;
static double rnd(void) {
    g_rng = g_rng * 1103515245u + 12345u;
    return (double)((g_rng >> 8) & 0xFFFFFF) / 16777216.0;
}

static void test_random_fields(void) {
    NavRouteCfg c = cfg_box(0, 100);
    c.extra = 0.0;
    static ObsSphere obs[400];
    int runs = 0, ok = 0, partial = 0, unsafe = 0, contact = 0, wrong_end = 0;
    double worst_ratio = 0.0;
    for (int trial = 0; trial < 3000; trial++) {
        int n = 20 + (int)(rnd() * 300);
        for (int i = 0; i < n; i++) {
            double rh = 0.8 + rnd() * 2.0;
            double ra = rh + 1.2;
            if (rnd() < 0.05) {
                rh = 0.0;
                ra = 4.2;
            }        /* black hole: keep-out only */
            obs[i] = sph(5 + rnd() * 90, 5 + rnd() * 90, 5 + rnd() * 90, rh, ra, i + 1);
        }
        double s[3], g[3];
        int tries = 0;
        do {
            for (int a = 0; a < 3; a++) {
                s[a] = 2 + rnd() * 96;
                g[a] = 2 + rnd() * 96;
            }
            tries++;
        } while ((nr_inside(&c, obs, n, s, 1.0) >= 0 || nr_inside(&c, obs, n, g, 1.0) >= 0) && tries < 100);
        if (tries >= 100) continue;
        runs++;
        NavRoute r;
        int rc = nav_route_plan(s, g, obs, n, &c, &r);
        if (rc != 0) {
            partial++;
            continue;
        }
        if (course_enters(&r, obs, n, &c) >= 0) unsafe++;
        if (course_hits(&r, obs, n) >= 0) contact++;
        if (r.flags & NAV_RF_PARTIAL) {
            partial++;
            continue;
        }
        if (nr_dist(r.p[r.n - 1], g) > 1e-6) wrong_end++;
        ok++;
        double ratio = r.length / (r.direct > 1e-9 ? r.direct : 1.0);
        if (r.direct > 20.0 && ratio > worst_ratio) worst_ratio = ratio;
    }
    printf("  random fields: %d runs, %d complete, %d partial, worst length ratio %.2f\n",
           runs, ok, partial, worst_ratio);
    CHECK(unsafe == 0, "random fields: %d courses enter a keep-out sphere", unsafe);
    CHECK(contact == 0, "random fields: %d courses touch an obstacle", contact);
    CHECK(wrong_end == 0, "random fields: %d complete courses do not end at the goal", wrong_end);
    CHECK(ok >= runs * 97 / 100, "random fields: %d/%d complete (>= 97%%)", ok, runs);
    CHECK(worst_ratio < 2.0, "random fields: detours stay reasonable (worst %.2f)", worst_ratio);
}

/* dense fields (up to ~25% of the volume in keep-out spheres): may stop
 * short, but whatever is flown must still be safe */
static void test_dense_fields(void) {
    NavRouteCfg c = cfg_box(0, 100);
    static ObsSphere obs[2000];
    int runs = 0, ok = 0, unsafe = 0;
    for (int trial = 0; trial < 300; trial++) {
        int n = 500 + (int)(rnd() * 1500);
        for (int i = 0; i < n; i++) {
            double rh = 0.8 + rnd() * 1.0;
            obs[i] = sph(5 + rnd() * 90, 5 + rnd() * 90, 5 + rnd() * 90, rh, rh + 1.2, i + 1);
        }
        double s[3], g[3];
        int tries = 0;
        do {
            for (int a = 0; a < 3; a++) {
                s[a] = 2 + rnd() * 96;
                g[a] = 2 + rnd() * 96;
            }
            tries++;
        } while ((nr_inside(&c, obs, n, s, 1.0) >= 0 || nr_inside(&c, obs, n, g, 1.0) >= 0) && tries < 200);
        if (tries >= 200) continue;
        runs++;
        NavRoute r;
        if (nav_route_plan(s, g, obs, n, &c, &r) != 0) continue;
        if (course_enters(&r, obs, n, &c) >= 0) unsafe++;
        if (!(r.flags & NAV_RF_PARTIAL)) ok++;
    }
    printf("  dense fields: %d runs, %d complete\n", runs, ok);
    CHECK(unsafe == 0, "dense fields: %d courses enter a keep-out sphere", unsafe);
    CHECK(ok >= runs * 80 / 100, "dense fields: %d/%d complete (>= 80%%)", ok, runs);
}

static void test_determinism(void) {
    NavRouteCfg c = cfg_box(0, 100);
    ObsSphere obs[50];
    g_rng = 999u;
    for (int i = 0; i < 50; i++) obs[i] = sph(20 + rnd() * 60, 40 + rnd() * 20, 40 + rnd() * 20, 0.8, 2.0, i + 1);
    double s[3] = { 5, 50, 50 }, g[3] = { 95, 50, 50 };
    NavRoute a, b;
    nav_route_plan(s, g, obs, 50, &c, &a);
    nav_route_plan(s, g, obs, 50, &c, &b);
    CHECK(a.n == b.n && memcmp(a.p, b.p, sizeof(double) * 3 * (size_t)a.n) == 0, "deterministic course");
}

static void test_first_hit_moving(void) {
    NavRouteCfg c = cfg_box(0, 100);
    double s[3] = { 10, 50, 50 }, g[3] = { 90, 50, 50 };
    NavRoute r;
    nav_route_plan(s, g, NULL, 0, &c, &r);
    ObsSphere comet = sph(60, 70, 50, 0.8, 2.0, 5);
    double pos[3] = { 20, 50, 50 };
    CHECK(nav_route_first_hit(&r, 1, pos, 100.0, &comet, 1, 0.0) < 0, "first_hit: comet off course");
    comet.y = 50.3;                                  /* it moved onto the course */
    CHECK(nav_route_first_hit(&r, 1, pos, 100.0, &comet, 1, 0.0) == 0, "first_hit: comet on course");
    CHECK(nav_route_first_hit(&r, 1, pos, 20.0, &comet, 1, 0.0) < 0, "first_hit: beyond the lookahead");
}

static void test_heading(void) {
    double h, m;
    double d1[3] = { 0, -1, 0 };
    nav_route_dir_to_hm(d1, &h, &m);
    CHECK(fabs(h) < 1e-9 && fabs(m) < 1e-9, "heading 0 = -y (%.3f, %.3f)", h, m);
    double d2[3] = { 1, 0, 0 };
    nav_route_dir_to_hm(d2, &h, &m);
    CHECK(fabs(h - 90) < 1e-9, "heading 90 = +x (%.3f)", h);
    double d3[3] = { 0, 0, 1 };
    nav_route_dir_to_hm(d3, &h, &m);
    CHECK(fabs(m - 90) < 1e-9, "mark 90 = +z (%.3f)", m);
    /* round trip with the `nav` formulas */
    for (int k = 0; k < 36; k++) {
        double hh = k * 10.0 + 3.0, mm = -80.0 + k * 4.5;
        double rh = hh * M_PI / 180.0, rm = mm * M_PI / 180.0;
        double d[3] = { cos(rm) * sin(rh), cos(rm) * -cos(rh), sin(rm) };
        nav_route_dir_to_hm(d, &h, &m);
        CHECK(fabs(h - hh) < 1e-6 && fabs(m - mm) < 1e-6, "round trip h %.1f m %.1f -> %.4f %.4f", hh, mm, h, m);
    }
}

int main(void) {
    printf("nav_route_test\n");
    test_free_path();
    test_single_obstacle();
    test_wall();
    test_goal_inside();
    test_start_inside();
    test_bounds();
    test_walled_in();
    test_random_fields();
    test_dense_fields();
    test_determinism();
    test_first_hit_moving();
    test_heading();
    printf("nav_route_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
