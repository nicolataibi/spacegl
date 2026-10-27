/*
 * steer_avoid_test.c - contract of include/steer_avoid.h.
 *
 * The segment test must see a solid even when a fast ship crosses it inside
 * a single tick, must not hit again a ship that was left inside an object
 * and flies away, and the steering must (a) leave a free path alone,
 * (b) turn to the side the path already leans to, (c) produce a direction
 * whose straight line clears the keep-out sphere, (d) bring a simulated ship
 * around an obstacle to its target without ever touching it.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "steer_avoid.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; \
    printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static ObsSphere sph(double x, double y, double z, double rh, double ra, int id) {
    ObsSphere o = { x, y, z, rh, ra, 0, id };
    return o;
}

/* distance from the centre of o to the half line p + s*dir, s >= 0 */
static double ray_dist(const double p[3], const double dir[3], const ObsSphere *o) {
    double rel[3] = { o->x - p[0], o->y - p[1], o->z - p[2] };
    double t = sa_dot(rel, dir);
    if (t < 0) t = 0;
    double q[3] = { p[0] + dir[0] * t - o->x, p[1] + dir[1] * t - o->y, p[2] + dir[2] * t - o->z };
    return sqrt(sa_dot(q, q));
}

static void test_segment_enter(void) {
    double c[3] = { 10, 0, 0 }, t;
    double a[3] = { 0, 0, 0 }, b[3] = { 20, 0, 0 };
    CHECK(sa_segment_enter(a, b, c, 0.8, &t), "head-on segment must hit");
    CHECK(fabs(t - (10 - 0.8) / 20.0) < 1e-9, "contact parameter %.6f", t);

    double b2[3] = { 5, 0, 0 };
    CHECK(!sa_segment_enter(a, b2, c, 0.8, &t), "segment that stops short must not hit");

    double a3[3] = { 0, 1.0, 0 }, b3[3] = { 20, 1.0, 0 };
    CHECK(!sa_segment_enter(a3, b3, c, 0.8, &t), "pass at 1.0 > r 0.8 must miss");
    double a4[3] = { 0, 0.7, 0 }, b4[3] = { 20, 0.7, 0 };
    CHECK(sa_segment_enter(a4, b4, c, 0.8, &t), "pass at 0.7 < r 0.8 must hit");

    double a5[3] = { 20, 0, 0 }, b5[3] = { 30, 0, 0 };
    CHECK(!sa_segment_enter(a5, b5, c, 0.8, &t), "moving away must not hit");

    /* already inside: hit only while moving toward the centre */
    double in0[3] = { 9.5, 0, 0 }, in_to[3] = { 10.2, 0, 0 }, in_away[3] = { 8.0, 0, 0 };
    CHECK(sa_segment_enter(in0, in_to, c, 0.8, &t) && t == 0.0, "inside and moving in: hit at t=0");
    CHECK(!sa_segment_enter(in0, in_away, c, 0.8, &t), "inside but leaving: no hit");

    CHECK(!sa_segment_enter(a, a, c, 0.8, &t), "zero length segment: no hit");
    CHECK(!sa_segment_enter(a, b, c, 0.0, &t), "r = 0 is not solid");
}

static void test_fast_ship_does_not_tunnel(void) {
    /* hyperdrive at warp 9.9: ~4.6 units in one tick, the contact radius is 0.8 */
    double a[3] = { 7.7, 0.2, 0 }, b[3] = { 12.3, 0.2, 0 }, t;
    double c[3] = { 10, 0, 0 };
    CHECK(sa_segment_enter(a, b, c, 0.8, &t), "a 4.6 unit step through a 0.8 sphere must hit");
    /* a point test at the end of the step would miss it */
    double d2 = (b[0] - c[0]) * (b[0] - c[0]) + (b[1] - c[1]) * (b[1] - c[1]);
    CHECK(sqrt(d2) > 0.8, "sanity: the end point is outside the sphere");
}

static void test_first_hit(void) {
    ObsSphere obs[4] = {
        sph(30, 0, 0, 0.8, 2.0, 11),
        sph(10, 0, 0, 0.8, 2.0, 22),
        sph(20, 0, 0, 0.0, 3.5, 33),     /* black hole: never solid */
        sph(15, 0, 0, 0.8, 2.0, 44),
    };
    double a[3] = { 0, 0, 0 }, b[3] = { 40, 0, 0 }, t;
    int i = sa_first_hit(a, b, obs, 4, 0.0, 0, &t);
    CHECK(i == 1 && obs[i].id == 22, "earliest solid must be hit first (got %d)", i);
    i = sa_first_hit(a, b, obs, 4, 0.0, 22, &t);
    CHECK(i == 3 && obs[i].id == 44, "skip_id must ignore the approach target (got %d)", i);
    double c[3] = { 0, 1.5, 0 }, d[3] = { 40, 1.5, 0 };
    CHECK(sa_first_hit(c, d, obs, 4, 0.0, 0, &t) == -1, "offset path must miss");
    CHECK(sa_first_hit(c, d, obs, 4, 1.0, 0, &t) == 1, "extra radius widens the contact distance");
    CHECK(sa_first_hit(a, b, obs, 0, 0.0, 0, &t) == -1, "no obstacles, no hit");
}

static void test_steer_free_and_ignored(void) {
    double p[3] = { 0, 0, 0 }, dir[3] = { 2, 0, 0 }, out[3];
    ObsSphere none[1] = { { 0, 0, 0, 0, 0, 0, 0 } };
    CHECK(sa_steer(p, dir, 10.0, none, 0, 0.0, 1, 0, out) == 0, "no obstacle: free");
    CHECK(fabs(out[0] - 1) < 1e-12 && fabs(out[1]) < 1e-12, "free path returns the unit direction");

    ObsSphere behind[1] = { sph(-5, 0, 0, 0.8, 2.0, 1) };
    CHECK(sa_steer(p, dir, 10.0, behind, 1, 0.0, 1, 0, out) == 0, "obstacle behind is ignored");
    ObsSphere far1[1] = { sph(30, 0, 0, 0.8, 2.0, 1) };
    CHECK(sa_steer(p, dir, 10.0, far1, 1, 0.0, 1, 0, out) == 0, "obstacle beyond the look-ahead is ignored");
    ObsSphere aside[1] = { sph(5, 3.0, 0, 0.8, 2.0, 1) };
    CHECK(sa_steer(p, dir, 10.0, aside, 1, 0.0, 1, 0, out) == 0, "obstacle beside the path is ignored");
    double zero[3] = { 0, 0, 0 };
    CHECK(sa_steer(p, zero, 10.0, aside, 1, 0.0, 1, 0, out) == 0, "zero direction is left alone");
    ObsSphere tgt[1] = { sph(5, 0, 0, 0.8, 2.0, 77) };
    CHECK(sa_steer(p, dir, 10.0, tgt, 1, 0.0, 1, 77, out) == 0, "skip_id: the approach target is not an obstacle");
}

static void test_steer_deflects(void) {
    double p[3] = { 0, 0, 0 }, dir[3] = { 1, 0, 0 }, out[3];
    /* centre slightly below the path: pass above it */
    ObsSphere o1[1] = { sph(10, -0.5, 0, 0.8, 2.0, 1) };
    CHECK(sa_steer(p, dir, 20.0, o1, 1, 0.0, 5, 0, out) == 1, "blocked path must steer");
    CHECK(fabs(sa_dot(out, out) - 1.0) < 1e-9, "output is a unit vector");
    CHECK(out[1] > 0.0, "centre below the path: the ship passes above (out_y=%.3f)", out[1]);
    CHECK(ray_dist(p, out, &o1[0]) >= 2.0 - 1e-9, "new path clears r_avoid (%.3f)", ray_dist(p, out, &o1[0]));

    ObsSphere o2[1] = { sph(10, 0.5, 0, 0.8, 2.0, 1) };
    CHECK(sa_steer(p, dir, 20.0, o2, 1, 0.0, 5, 0, out) == 1 && out[1] < 0.0,
          "centre above the path: the ship passes below (out_y=%.3f)", out[1]);
    CHECK(ray_dist(p, out, &o2[0]) >= 2.0 - 1e-9, "mirror case clears r_avoid");

    /* every axis, off-axis obstacles */
    for (int k = 0; k < 3; k++) {
        double d[3] = { 0, 0, 0 };
        d[k] = 1.0;
        ObsSphere o[1] = { sph(12 * d[0] + 0.2, 12 * d[1] + 0.2, 12 * d[2] + 0.2, 0.8, 2.0, 1) };
        CHECK(sa_steer(p, d, 30.0, o, 1, 0.0, 3, 0, out) == 1, "axis %d blocked", k);
        CHECK(ray_dist(p, out, &o[0]) >= 2.0 - 1e-9, "axis %d: clears r_avoid", k);
    }

    /* head on: stable per seed, perpendicular offset, may differ per seed */
    ObsSphere ho[1] = { sph(10, 0, 0, 0.8, 2.0, 1) };
    double o_a[3], o_b[3], o_c[3];
    sa_steer(p, dir, 20.0, ho, 1, 0.0, 7, 0, o_a);
    sa_steer(p, dir, 20.0, ho, 1, 0.0, 7, 0, o_b);
    sa_steer(p, dir, 20.0, ho, 1, 0.0, 8, 0, o_c);
    CHECK(memcmp(o_a, o_b, sizeof o_a) == 0, "same seed, same side");
    CHECK(ray_dist(p, o_a, &ho[0]) >= 2.0 - 1e-9, "head-on deflection clears r_avoid");
    CHECK(ray_dist(p, o_c, &ho[0]) >= 2.0 - 1e-9, "head-on deflection (other seed) clears r_avoid");

    /* extra radius (ship size / margin) */
    sa_steer(p, dir, 20.0, o1, 1, 1.0, 5, 0, out);
    CHECK(ray_dist(p, out, &o1[0]) >= 3.0 - 1e-9, "extra radius is honoured (%.3f)", ray_dist(p, out, &o1[0]));
}

static void test_steer_nearest_first(void) {
    double p[3] = { 0, 0, 0 }, dir[3] = { 1, 0, 0 }, out[3];
    ObsSphere o[2] = { sph(40, 0.3, 0, 0.8, 2.0, 2), sph(10, -0.3, 0, 0.8, 2.0, 1) };
    CHECK(sa_steer(p, dir, 60.0, o, 2, 0.0, 1, 0, out) == 1, "two blockers");
    CHECK(out[1] > 0.0, "the nearest blocker (centre below) decides the side (out_y=%.3f)", out[1]);
}

static void test_inside_keepout(void) {
    ObsSphere o[1] = { sph(0, 0, 0, 0.8, 2.0, 1) };
    double p[3] = { 1.0, 0, 0 }, out[3];
    double inward[3] = { -1, 0, 0 }, outward[3] = { 1, 0, 0 }, slide[3] = { -0.5, 1, 0 };
    CHECK(sa_steer(p, inward, 5.0, o, 1, 0.0, 1, 0, out) == 1 && out[0] > 0.0,
          "inside and moving in: pushed outward (out_x=%.3f)", out[0]);
    CHECK(sa_steer(p, outward, 5.0, o, 1, 0.0, 1, 0, out) == 0, "inside but leaving: left alone");
    CHECK(sa_steer(p, slide, 5.0, o, 1, 0.0, 1, 0, out) == 1 && out[0] > 0.0 && out[1] > 0.0,
          "slides along the sphere while leaving (%.3f, %.3f)", out[0], out[1]);
    double centre[3] = { 0, 0, 0 };
    CHECK(sa_steer(centre, inward, 5.0, o, 1, 0.0, 1, 0, out) == 1 &&
          fabs(sa_dot(out, out) - 1.0) < 1e-9, "exactly on the centre still yields a unit direction");
}

/* A simulated ship (NPC pace: 0.05 per tick) flies to a target behind a
 * sphere; the direction is re-evaluated every 3 ticks like update_npc_ai. */
static void simulate(double speed, double lookahead, const ObsSphere *obs, int n,
                     const double start[3], const double target[3],
                     double *min_hit_dist, int *reached, int max_ticks) {
    double p[3] = { start[0], start[1], start[2] };
    double heading[3] = { 0, 0, 0 };
    double rel[3] = { target[0] - p[0], target[1] - p[1], target[2] - p[2] };
    sa_norm(rel, heading);
    *min_hit_dist = 1e300;
    *reached = 0;
    for (int tick = 0; tick < max_ticks; tick++) {
        if (tick % 3 == 0) {
            double want[3] = { target[0] - p[0], target[1] - p[1], target[2] - p[2] };
            double dl = sa_norm(want, want);
            if (dl < speed * 3) { *reached = 1; return; }
            double steered[3];
            sa_steer(p, want, lookahead, obs, n, 0.0, 99, 0, steered);
            sa_blend(heading, steered, 0.25, heading);
        }
        p[0] += heading[0] * speed; p[1] += heading[1] * speed; p[2] += heading[2] * speed;
        for (int i = 0; i < n; i++) {
            double dx = p[0] - obs[i].x, dy = p[1] - obs[i].y, dz = p[2] - obs[i].z;
            double d = sqrt(dx * dx + dy * dy + dz * dz);
            if (d < *min_hit_dist) *min_hit_dist = d;
        }
    }
}

static void test_simulation(void) {
    double start[3] = { 0, 0, 0 }, target[3] = { 30, 0, 0 };
    double md; int ok;

    ObsSphere one[1] = { sph(15, 0, 0, 0.8, 2.0, 1) };
    simulate(0.05, 6.0, one, 1, start, target, &md, &ok, 4000);
    CHECK(ok, "NPC reaches a target behind a head-on obstacle");
    CHECK(md > one[0].r_hit, "NPC never touches the obstacle (closest %.3f)", md);

    ObsSphere two[2] = { sph(10, 0, 0, 0.8, 2.0, 1), sph(20, 0.4, 0.3, 0.8, 2.0, 2) };
    simulate(0.05, 6.0, two, 2, start, target, &md, &ok, 6000);
    CHECK(ok, "NPC reaches the target past two obstacles");
    CHECK(md > two[0].r_hit, "NPC never touches either obstacle (closest %.3f)", md);

    /* black hole: only a keep-out distance of 3.5, no solid core */
    ObsSphere bh[1] = { sph(15, 0.2, 0, 0.0, 3.5, 3) };
    simulate(0.05, 8.0, bh, 1, start, target, &md, &ok, 6000);
    CHECK(ok && md > 3.0, "NPC keeps clear of a black hole well (closest %.3f)", md);

    /* a ship spawned inside the keep-out zone gets out */
    ObsSphere around[1] = { sph(0.4, 0, 0, 0.8, 2.0, 4) };
    simulate(0.05, 6.0, around, 1, start, target, &md, &ok, 4000);
    CHECK(ok, "ship starting inside a keep-out sphere still reaches its target");
}

static void test_blend_and_perp(void) {
    double a[3] = { 1, 0, 0 }, b[3] = { 0, 1, 0 }, o[3];
    sa_blend(a, b, 1.0, o);
    CHECK(fabs(o[0]) < 1e-12 && fabs(o[1] - 1) < 1e-12, "k=1 gives the new direction");
    sa_blend(a, b, 0.5, o);
    CHECK(fabs(sa_dot(o, o) - 1.0) < 1e-9 && o[0] > 0.0 && o[1] > 0.0, "k=0.5 stays unit and between");
    double m[3] = { -1, 0, 0 };
    sa_blend(a, m, 0.5, o);
    CHECK(fabs(sa_dot(o, o) - 1.0) < 1e-9, "opposite vectors do not produce NaN");

    for (unsigned s = 0; s < 40; s++) {
        double u[3] = { 0.3 * (s % 5) - 0.6, 0.2 * (s % 7) - 0.5, 1.0 }, un[3], q[3];
        sa_norm(u, un);
        sa_perp(un, s, q);
        CHECK(fabs(sa_dot(q, q) - 1.0) < 1e-9 && fabs(sa_dot(q, un)) < 1e-9,
              "perpendicular must be unit and orthogonal (seed %u)", s);
    }
}

int main(void) {
    test_segment_enter();
    test_fast_ship_does_not_tunnel();
    test_first_hit();
    test_steer_free_and_ignored();
    test_steer_deflects();
    test_steer_nearest_first();
    test_inside_keepout();
    test_simulation();
    test_blend_and_perp();
    printf("steer_avoid_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
