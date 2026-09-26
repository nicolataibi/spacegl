/*
 * SPACE GL - 3D LOGIC ENGINE - server navigation roll contract test.
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Regression (HUD roll above 360 degrees): the server's roll state
 * (van_r, what the clients display as-is as shm_r in the HUD roll
 * field) must stay inside [0, 360). Two paths could violate it:
 *
 *   a) the "pos <H> <M> [R]" command stored the requested roll in
 *      target_r WITHOUT normalizing it (370, -10, ...), so the
 *      aligned ship kept an out-of-range roll forever;
 *   b) the NAV_STATE_ALIGN_ONLY interpolation in logic.c computed
 *      "van_r = start_r + diff_r * t" with diff_r wrapped to ±180°
 *      but no re-wrap of the result, so every roll turn crossing the
 *      0/360 boundary (e.g. 350° -> 20°) passed through 370..380°
 *      before snapping back to the target.
 *
 * The code under test is the real production source
 * (../src/server/nav_math.c, pulled in by the test project the same
 * way nav_heading_test does for the B2 heading fix):
 *
 *   1. B-roll scenario: the reported 350 -> 20 turn, sampled densely:
 *      always in [0, 360), exactly one 0/360 crossing, exact
 *      endpoints.
 *   2. "pos" normalization contract: out-of-range requested rolls
 *      (370, -10, 730, -370) are handled modulo 360 and the
 *      endpoints come back in [0, 360).
 *   3. Shortest-arc property: turns cross 0 (never 180) and a ±180°
 *      turn is exact.
 *   4. Motion invariance: for fixed scenarios the wrapped roll is the
 *      SAME angle as the pre-fix (buggy) formula — cos/sin must be
 *      identical, so the shield basis (calculate_shield_index) and the
 *      3D views see an unchanged ship orientation.
 *   5. Fuzz: 20000 random (start, target, t) triples stay in
 *      [0, 360) and match the pre-fix orientation.
 *
 * Exit: 0 = pass, 1 = fail.
 */

#include "nav_math.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
           fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } \
} while (0)

#define CHECK_F(a, b, tol, what) do { \
    if (fabs((a) - (b)) <= (tol)) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL %s:%d: %s: got %.9g want %.9g\n", \
            __FILE__, __LINE__, what, (double)(a), (double)(b)); } \
} while (0)

/* The pre-fix (buggy) roll formula, kept as the reference for the
 * motion-invariance checks: the wrap must change only the display
 * range, never the ship's actual orientation. */
static double old_roll_at(double start_r, double target_r, double t) {
    double d = target_r - start_r;
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    return start_r + d * t;   /* no re-wrap: the roll bug */
}

static int in_range(double r) { return r >= 0.0 && r < 360.0; }

/* ================================================================== */
/* 1. B-roll scenario: 350 -> 20 (the reported HUD symptom)           */
/* ================================================================== */
static void test_broll_scenario(void) {
    const double S = 350.0, T = 20.0;
    const int N = 1000;               /* t = 0 .. 1 */
    int out_of_range = 0, wraps = 0;
    double prev = nav_roll_at(S, T, 0.0);

    CHECK_F(prev, S, 1e-12, "roll: r(0) == start (350)");
    for (int i = 1; i <= N; i++) {
        double t = (double)i / N;
        double r = nav_roll_at(S, T, t);
        if (!in_range(r)) {
            if (out_of_range < 5)
                fprintf(stderr, "  (out of range: t=%.3f -> %.2f)\n", t, r);
            out_of_range++;
        }
        if (r < prev) wraps++;        /* 360 -> 0 crossing */
        prev = r;
    }
    CHECK(out_of_range == 0,
          "roll: 350->20 stays in [0,360) for all t (%d of %d out of range)",
          out_of_range, N + 1);
    CHECK(wraps == 1, "roll: 350->20 crosses the 0/360 boundary exactly once (%d)", wraps);
    CHECK_F(nav_roll_at(S, T, 1.0), T, 1e-12, "roll: r(1) == target (20), not 380");

    /* The crossing happens where the raw trajectory hits 360 (t=1/3):
     * 359.6 before, 0.2 after — both inside [0, 360). */
    CHECK_F(nav_roll_at(S, T, 0.32), 359.6, 1e-9, "roll: r(0.32) == 359.6");
    CHECK_F(nav_roll_at(S, T, 0.34), 0.2, 1e-9, "roll: r(0.34) == 0.2");
    CHECK_F(nav_roll_at(S, T, 0.5), 5.0, 1e-9, "roll: r(0.5) == 5 (was 365)");

    /* The raw (pre-fix) trajectory really left the range, proving the
     * test discriminates: at t=1 it is 380. */
    CHECK(old_roll_at(S, T, 1.0) > 360.0,
          "roll: pre-fix formula reaches %.1f at t=1 (discriminating case)",
          old_roll_at(S, T, 1.0));
}

/* ================================================================== */
/* 2. "pos" normalization contract (out-of-range requested rolls)     */
/* ================================================================== */
static void test_pos_normalization(void) {
    /* "pos H M R" with R outside [0, 360) is the same physical roll
     * modulo 360: the interpolation must treat it as such. */
    CHECK_F(nav_roll_at(10.0, 370.0, 0.5), 10.0, 1e-12, "pos: target 370 == 10 (no motion)");
    CHECK_F(nav_roll_at(10.0, 370.0, 1.0), 10.0, 1e-12, "pos: r(1) of 10->370 == 10, not 370");
    CHECK_F(nav_roll_at(350.0, -10.0, 1.0), 350.0, 1e-12, "pos: target -10 == 350 (no motion)");
    CHECK_F(nav_roll_at(10.0, 730.0, 1.0), 10.0, 1e-12, "pos: target 730 == 10 (two full turns)");
    CHECK_F(nav_roll_at(350.0, -370.0, 1.0), 350.0, 1e-12, "pos: target -370 == 350");

    /* Out-of-range targets that are NOT congruent to the start: the
     * turn still takes the shortest arc and stays in range. */
    CHECK_F(nav_roll_at(10.0, 380.0, 1.0), 20.0, 1e-12, "pos: 10 -> 380 lands on 20");
    CHECK_F(nav_roll_at(10.0, -20.0, 1.0), 340.0, 1e-12, "pos: 10 -> -20 lands on 340");
    CHECK_F(nav_roll_at(350.0, -20.0, 0.5), 345.0, 1e-12, "pos: 350 -> -20, mid == 345");

    /* Endpoints always come back in [0, 360) even for extreme inputs. */
    int bad = 0;
    const double targets[] = { 360.0, 370.0, 720.0, 1000.0, -10.0, -360.0, -1000.0 };
    for (size_t k = 0; k < sizeof(targets) / sizeof(targets[0]); k++) {
        double r0 = nav_roll_at(10.0, targets[k], 0.0);
        double r1 = nav_roll_at(10.0, targets[k], 1.0);
        if (!in_range(r0) || !in_range(r1)) bad++;
        /* r(1) must equal the requested roll wrapped into [0, 360) */
        if (fabs(r1 - nav_wrap_heading(targets[k])) > 1e-9) bad++;
    }
    CHECK(bad == 0, "pos: %d endpoint checks failed for out-of-range targets", bad);
}

/* ================================================================== */
/* 3. Shortest-arc property                                            */
/* ================================================================== */
static void test_shortest_arc(void) {
    /* 350 -> 10 turns +20 through 0 (NOT -340 through 180) */
    CHECK_F(nav_roll_at(350.0, 10.0, 0.5), 0.0, 1e-12, "arc: 350->10, r(0.5) via 0");
    CHECK_F(nav_roll_at(350.0, 10.0, 1.0), 10.0, 1e-12, "arc: 350->10, r(1)");
    /* 10 -> 350 turns -20 the other way, also through 0 */
    CHECK_F(nav_roll_at(10.0, 350.0, 0.5), 0.0, 1e-12, "arc: 10->350, r(0.5) via 0");
    CHECK_F(nav_roll_at(10.0, 350.0, 1.0), 350.0, 1e-12, "arc: 10->350, r(1)");

    /* Exactly ±180°: the delta loop keeps it, mid-point is 90 either way */
    CHECK_F(nav_roll_at(0.0, 180.0, 0.5), 90.0, 1e-12, "arc: 0->180, r(0.5)");
    CHECK_F(nav_roll_at(180.0, 0.0, 0.5), 90.0, 1e-12, "arc: 180->0, r(0.5)");
    CHECK_F(nav_roll_at(180.0, 0.0, 1.0), 0.0, 1e-12, "arc: 180->0, r(1)");

    /* A plain 100 -> 200 turn never crosses the boundary */
    int out = 0;
    for (int i = 0; i <= 100; i++) {
        double r = nav_roll_at(100.0, 200.0, (double)i / 100.0);
        if (!in_range(r) || r < 100.0 - 1e-12 || r > 200.0 + 1e-12) out++;
    }
    CHECK(out == 0, "arc: 100->200 monotonic in [100,200] (%d violations)", out);
}

/* ================================================================== */
/* 4. Motion invariance (wrap must not change the ship orientation)   */
/* ================================================================== */
static void test_motion_invariance(void) {
    static const double starts[][2] = {
        {350.0, 20.0}, {20.0, 350.0}, {0.0, 180.0}, {180.0, 0.0},
        {359.5, 0.5}, {120.0, 300.0}, {0.0, 0.0}, {90.0, 270.0}
    };
    int bad = 0;
    for (size_t k = 0; k < sizeof(starts) / sizeof(starts[0]); k++) {
        for (int i = 0; i <= 20; i++) {
            double t = (double)i / 20.0;
            double rf = nav_roll_at(starts[k][0], starts[k][1], t);
            double ro = old_roll_at(starts[k][0], starts[k][1], t);
            double rad = M_PI / 180.0;
            if (fabs(cos(rf * rad) - cos(ro * rad)) > 1e-12
                || fabs(sin(rf * rad) - sin(ro * rad)) > 1e-12) bad++;
        }
    }
    CHECK(bad == 0,
          "motion: %d of %zu samples where cos/sin differ from pre-fix (must be 0)",
          bad, (sizeof(starts) / sizeof(starts[0])) * 21);
}

/* ================================================================== */
/* 5. Fuzz                                                             */
/* ================================================================== */
static void test_fuzz(void) {
    srand(20260925);
    const int N = 20000;
    int out_of_range = 0, motion_bad = 0, end_bad = 0;
    for (int i = 0; i < N; i++) {
        double s = (double)(rand() % 36000) / 100.0;      /* [0, 360) */
        double tg = (double)(rand() % 36000) / 100.0;     /* [0, 360) */
        double t = (double)(rand() % 1001) / 1000.0;      /* [0, 1] */

        double r = nav_roll_at(s, tg, t);
        if (!in_range(r)) out_of_range++;

        double ro = old_roll_at(s, tg, t);
        double rad = M_PI / 180.0;
        if (fabs(cos(r * rad) - cos(ro * rad)) > 1e-12
            || fabs(sin(r * rad) - sin(ro * rad)) > 1e-12) motion_bad++;

        if (fabs(nav_roll_at(s, tg, 0.0) - nav_wrap_heading(s)) > 1e-9
            || fabs(nav_roll_at(s, tg, 1.0) - nav_wrap_heading(tg)) > 1e-9)
            end_bad++;
    }
    CHECK(out_of_range == 0, "fuzz: %d/%d rolls outside [0,360)", out_of_range, N);
    CHECK(motion_bad == 0, "fuzz: %d/%d samples changed the orientation", motion_bad, N);
    CHECK(end_bad == 0, "fuzz: %d/%d endpoints wrong (r(0)=start, r(1)=target)", end_bad, N);
}

int main(void) {
    printf("nav_roll test (server roll must stay in [0, 360))\n");
    test_broll_scenario();
    test_pos_normalization();
    test_shortest_arc();
    test_motion_invariance();
    test_fuzz();
    printf("  %d checks passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
