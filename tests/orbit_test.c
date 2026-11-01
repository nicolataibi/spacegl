/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * orbit_test.c - the geometry of the `orb` command (include/orbit_math.h):
 * plane and direction of travel at capture, constant radius and rate, the
 * Kepler-like period, the settling of a ship that was too close (or too far),
 * a moving centre, and the degenerate captures (on the centre, along z).
 * The command on a real galaxy is tested in server_commands_test.c.
 * Exit: 0 pass, 1 fail.
 */
#include "orbit_math.h"

#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, ...) \
    do { \
        g_checks++; \
        if (!(cond)) { \
            g_fail++; \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__); \
            fprintf(stderr, "\n"); \
        } \
    } while (0)

static int near(double a, double b, double eps) { return fabs(a - b) <= eps; }

/* orthonormal basis, whatever the capture */
static void check_basis(const OrbitState *o, const char *what) {
    CHECK(near(orb_len(o->e1), 1.0, 1e-12), "%s: |e1| = %.15f", what, orb_len(o->e1));
    CHECK(near(orb_len(o->e2), 1.0, 1e-12), "%s: |e2| = %.15f", what, orb_len(o->e2));
    CHECK(near(orb_dot(o->e1, o->e2), 0.0, 1e-12), "%s: e1.e2 = %.3g", what, orb_dot(o->e1, o->e2));
}

static void test_prograde(void) {
    OrbitState o;
    double rel[3] = { 3.0, 0.0, 0.0 }, vel[3] = { 0.0, 0.05, 0.0 };
    orbit_start(&o, 7, rel, vel, 2.0);
    check_basis(&o, "prograde");
    CHECK(o.active && o.target == 7, "state not filled");
    CHECK(near(o.e1[0], 1.0, 1e-12), "e1 should point from the object to the ship");
    CHECK(near(o.e2[1], 1.0, 1e-12), "e2 should follow the ship's motion (+y), got (%.3f,%.3f,%.3f)", o.e2[0], o.e2[1], o.e2[2]);
    double off[3], dir[3];
    orbit_advance(&o, off, dir);
    CHECK(off[1] > 0.0, "the ship should move towards +y first, off.y = %.5f", off[1]);
    CHECK(near(orb_len(off), 3.0, 1e-12), "radius after one tick %.12f", orb_len(off));
    CHECK(near(orb_len(dir), 1.0, 1e-12), "direction not unit");

    /* a velocity with a radial part: only the tangential one counts */
    double vel2[3] = { 0.08, 0.05, 0.0 };
    orbit_start(&o, 7, rel, vel2, 2.0);
    check_basis(&o, "radial+tangential");
    CHECK(near(o.e2[0], 0.0, 1e-12) && near(o.e2[1], 1.0, 1e-12), "radial part of the velocity leaked into e2");

    /* moving the other way round: the orbit runs the other way round */
    double vel3[3] = { 0.0, -0.05, 0.0 };
    orbit_start(&o, 7, rel, vel3, 2.0);
    orbit_advance(&o, off, dir);
    CHECK(off[1] < 0.0, "retrograde capture should go to -y, off.y = %.5f", off[1]);

    /* a ship moving straight at the object has no tangent: falls back to the horizontal one */
    double vel4[3] = { -0.1, 0.0, 0.0 };
    orbit_start(&o, 7, rel, vel4, 2.0);
    check_basis(&o, "head-on");
    CHECK(near(o.e2[2], 0.0, 1e-12), "head-on fallback should be horizontal");
}

static void test_rest_and_degenerate(void) {
    OrbitState o;
    double zero[3] = { 0.0, 0.0, 0.0 };
    double rel[3] = { 2.5, 1.0, -0.5 };
    orbit_start(&o, 1, rel, zero, 2.0);
    check_basis(&o, "at rest");
    CHECK(near(o.e2[2], 0.0, 1e-12), "a ship at rest orbits along the horizontal tangent (e2.z = %.3g)", o.e2[2]);

    double up[3] = { 0.0, 0.0, 3.0 };
    orbit_start(&o, 1, up, zero, 2.0);
    check_basis(&o, "along z");
    CHECK(!isnan(o.e2[0]) && !isnan(o.e2[1]) && !isnan(o.e2[2]), "NaN in e2 for a capture on the z axis");

    orbit_start(&o, 1, zero, zero, 2.0);        /* exactly on the centre */
    check_basis(&o, "on the centre");
    CHECK(near(o.r, 2.0, 1e-12) && near(o.r_goal, 2.0, 1e-12), "on the centre: r=%.3f goal=%.3f, expected the safe radius", o.r, o.r_goal);
    double off[3], dir[3];
    for (int t = 0; t < 2000; t++) orbit_advance(&o, off, dir);
    CHECK(near(orb_len(off), 2.0, 1e-9), "on the centre: radius %.9f after settling", orb_len(off));
}

static void test_circle(void) {
    OrbitState o;
    double rel[3] = { 0.0, -3.4, 0.0 }, vel[3] = { 0.03, 0.0, 0.02 };
    orbit_start(&o, 1, rel, vel, 2.0);
    check_basis(&o, "circle");
    CHECK(near(o.r_goal, 3.4, 1e-12), "radius at capture kept: %.6f", o.r_goal);
    double n[3];
    orb_cross(o.e1, o.e2, n);
    double off[3], dir[3], swept = 0.0, prev = 0.0;
    int period = (int)(orbit_period_s(3.4) * ORBIT_TICK_RATE), turn_at = -1;
    double worst_r = 0, worst_plane = 0, worst_tan = 0;
    for (int t = 1; t <= period * 3; t++) {
        orbit_advance(&o, off, dir);
        worst_r = fmax(worst_r, fabs(orb_len(off) - 3.4));
        worst_plane = fmax(worst_plane, fabs(orb_dot(off, n)));
        worst_tan = fmax(worst_tan, fabs(orb_dot(off, dir)));    /* direction of travel is tangent: dir . off = 0 */
        double a = atan2(orb_dot(off, o.e2), orb_dot(off, o.e1));
        double da = a - prev;
        while (da > M_PI) da -= 2 * M_PI;
        while (da <= -M_PI) da += 2 * M_PI;
        swept += da;
        prev = a;
        if (turn_at < 0 && swept >= 2 * M_PI) turn_at = t;
        CHECK(o.theta >= 0.0 && o.theta <= 2.0 * M_PI + 1e-9, "phase out of [0, 2pi]: %.6f", o.theta);
    }
    CHECK(worst_r < 1e-12, "radius error %.3g", worst_r);
    CHECK(worst_plane < 1e-12, "left the plane by %.3g", worst_plane);
    CHECK(worst_tan < 1e-12, "direction of travel not tangent: %.3g", worst_tan);
    CHECK(abs(turn_at - period) <= 1, "one turn in %d ticks, period says %d", turn_at, period);
    CHECK(near(swept, 3 * 2 * M_PI, 0.02), "three periods swept %.4f rad", swept);
}

static void test_scaling(void) {
    CHECK(near(orbit_period_s(ORBIT_RADIUS_REF), ORBIT_PERIOD_REF_S, 1e-9), "reference period");
    CHECK(near(orbit_period_s(6.0) / orbit_period_s(3.0), pow(2.0, 1.5), 1e-9), "Kepler: T ~ r^1.5");
    CHECK(orbit_omega(2.0) > orbit_omega(4.0), "a lower orbit is faster");
    CHECK(near(orbit_omega(3.0) * orbit_period_s(3.0) * ORBIT_TICK_RATE, 2 * M_PI, 1e-9), "omega x period = 2 pi");
    CHECK(isfinite(orbit_omega(0.0)) && orbit_omega(0.0) > 0, "omega(0) must stay finite");
}

static void test_settling(void) {
    OrbitState o;
    double rel[3] = { 0.5, 0.0, 0.0 }, vel[3] = { 0, 0, 0 };
    orbit_start(&o, 1, rel, vel, 2.0);
    CHECK(near(o.r, 0.5, 1e-12) && near(o.r_goal, 2.0, 1e-12), "too close: r=%.2f goal=%.2f", o.r, o.r_goal);
    double off[3], dir[3], last = o.r;
    int settled = -1;
    for (int t = 1; t < 500; t++) {
        orbit_advance(&o, off, dir);
        CHECK(o.r >= last - 1e-12, "radius shrank while settling outwards");
        CHECK(o.r - last <= ORBIT_RADIAL_SLEW + 1e-12, "radius jumped by %.4f", o.r - last);
        last = o.r;
        if (settled < 0 && near(o.r, 2.0, 1e-12)) settled = t;
    }
    int expect = (int)ceil((2.0 - 0.5) / ORBIT_RADIAL_SLEW);
    CHECK(settled > 0 && abs(settled - expect) <= 1, "settled after %d ticks, expected %d", settled, expect);

    /* captured farther out than a (lowered) goal: spirals in, never overshoots */
    o.r = 3.0;
    o.r_goal = 2.5;
    for (int t = 0; t < 100; t++) {
        orbit_advance(&o, off, dir);
        CHECK(o.r >= 2.5 - 1e-12, "overshoot below the goal");
    }
    CHECK(near(o.r, 2.5, 1e-12), "did not reach the goal: %.6f", o.r);
}

/* A body that moves (star-system planets and moons): the caller adds the offset to the centre read
 * again every tick, so the ship's distance to the body stays r and its path relative to the body
 * does not depend on how the body moves. */
static void test_moving_centre(void) {
    OrbitState a, b;
    double rel[3] = { 2.2, 0.0, 0.0 }, vel[3] = { 0, 0.04, 0 };
    orbit_start(&a, 1, rel, vel, 2.0);
    orbit_start(&b, 1, rel, vel, 2.0);
    double c[3] = { 100, 200, 300 }, offa[3], offb[3], dir[3];
    double worst = 0, worst_rel = 0;
    double ship_prev[3] = { c[0] + rel[0], c[1] + rel[1], c[2] + rel[2] }, worst_jump = 0;
    for (int t = 0; t < 3000; t++) {
        c[0] += 0.011;
        c[1] -= 0.007;
        c[2] += 0.003;            /* the body drifts along its own orbit */
        orbit_advance(&a, offa, dir);
        orbit_advance(&b, offb, dir);                           /* the same orbit round a fixed body */
        double ship[3] = { c[0] + offa[0], c[1] + offa[1], c[2] + offa[2] };
        double d[3] = { ship[0] - c[0], ship[1] - c[1], ship[2] - c[2] };
        worst = fmax(worst, fabs(orb_len(d) - 2.2));
        for (int k = 0; k < 3; k++) worst_rel = fmax(worst_rel, fabs(offa[k] - offb[k]));
        double j[3] = { ship[0] - ship_prev[0], ship[1] - ship_prev[1], ship[2] - ship_prev[2] };
        worst_jump = fmax(worst_jump, orb_len(j));
        for (int k = 0; k < 3; k++) ship_prev[k] = ship[k];
    }
    CHECK(worst < 1e-9, "distance to the moving body drifted by %.3g", worst);
    CHECK(worst_rel == 0.0, "the offset depends on the body's motion (%.3g)", worst_rel);
    /* ship speed = orbital speed + the body's own speed: no spikes */
    double v_orb = 2.2 * orbit_omega(2.2), v_body = sqrt(0.011 * 0.011 + 0.007 * 0.007 + 0.003 * 0.003);
    CHECK(worst_jump <= v_orb + v_body + 1e-9, "a step of %.5f > %.5f: the ship jumped", worst_jump, v_orb + v_body);
}

int main(void) {
    test_prograde();
    test_rest_and_degenerate();
    test_circle();
    test_scaling();
    test_settling();
    test_moving_centre();
    printf("orbit_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
