/*
 * SPACE GL - 3D LOGIC ENGINE - server navigation heading contract test.
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
 * B2 regression (HUD heading above 360 degrees): the server's
 * alignment interpolation (NAV_STATE_ALIGN / NAV_STATE_ALIGN_IMPULSE
 * and NAV_STATE_ALIGN_ONLY in logic.c) must keep the player heading
 * (van_h, what the clients display as-is as shm_h) inside [0, 360).
 * Before the fix, "van_h = start_h + diff_h * t" with diff_h wrapped
 * to ±180° drifted outside [0, 360) on every turn crossing the
 * 0/360 boundary (e.g. 350° -> 30° passed through 390°).
 *
 * The code under test is the real production source
 * (../src/server/nav_math.c, pulled in by the test project the same
 * way gdd_contract_test pulls in spacegl_vulkan_gdd.c).
 *
 *   1. nav_wrap_heading: wrap contract over the whole range.
 *   2. B2 scenario: the reported 350 -> 30 turn, sampled densely:
 *      always in [0, 360), exactly one 0/360 crossing, exact
 *      endpoints.
 *   3. Shortest-arc property: turns cross 0 (never 180) and a
 *      ±180° turn is exact.
 *   4. Motion invariance: for fixed scenarios the wrapped heading is
 *      the SAME angle as the pre-fix (buggy) formula — cos/sin must
 *      be identical, so ship motion vectors are unchanged.
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

/* The pre-fix (buggy) formula, kept as the reference for the
 * motion-invariance checks: the wrap must change only the display
 * range, never the ship's actual orientation. */
static double old_heading_at(double start_h, double target_h, double t) {
    double d = target_h - start_h;
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    return start_h + d * t;   /* no re-wrap: the B2 bug */
}

static int in_range(double h) { return h >= 0.0 && h < 360.0; }

/* ================================================================== */
/* 1. Wrap contract                                                    */
/* ================================================================== */
static void test_wrap(void) {
    CHECK_F(nav_wrap_heading(30.0), 30.0, 1e-12, "wrap(30)");
    CHECK_F(nav_wrap_heading(0.0), 0.0, 1e-12, "wrap(0)");
    CHECK_F(nav_wrap_heading(359.9), 359.9, 1e-12, "wrap(359.9)");
    CHECK_F(nav_wrap_heading(390.0), 30.0, 1e-12, "wrap(390)");
    CHECK_F(nav_wrap_heading(540.0), 180.0, 1e-12, "wrap(540)");
    CHECK_F(nav_wrap_heading(720.0), 0.0, 1e-12, "wrap(720)");
    CHECK_F(nav_wrap_heading(-10.0), 350.0, 1e-12, "wrap(-10)");
    CHECK_F(nav_wrap_heading(-360.0), 0.0, 1e-12, "wrap(-360)");
    CHECK_F(nav_wrap_heading(-390.0), 330.0, 1e-12, "wrap(-390)");

    /* The 390 of the B2 report must come back as 30 */
    CHECK_F(nav_wrap_heading(350.0 + 40.0), 30.0, 1e-12,
            "wrap(350 + 40*1) == 30 (B2 example)");

    int bad = 0;
    for (double h = -720.0; h <= 720.0; h += 0.5) {
        double w = nav_wrap_heading(h);
        /* in range AND congruent to h modulo 360 */
        if (!in_range(w) || fabs(fmod(w - h, 360.0)) > 1e-9) bad++;
    }
    CHECK(bad == 0, "wrap: %d wrong values over -720..720", bad);
}

/* ================================================================== */
/* 2. B2 scenario: 350 -> 30 (the reported HUD symptom)                */
/* ================================================================== */
static void test_b2_scenario(void) {
    const double S = 350.0, T = 30.0;
    const int N = 1000;               /* t = 0 .. 1 */
    int out_of_range = 0, wraps = 0;
    double prev = nav_heading_at(S, T, 0.0);

    CHECK_F(prev, S, 1e-12, "B2: h(0) == start (350)");
    for (int i = 1; i <= N; i++) {
        double t = (double)i / N;
        double h = nav_heading_at(S, T, t);
        if (!in_range(h)) {
            if (out_of_range < 5)
                fprintf(stderr, "  (B2 out of range: t=%.3f -> %.2f)\n", t, h);
            out_of_range++;
        }
        if (h < prev) wraps++;        /* 360 -> 0 crossing */
        prev = h;
    }
    CHECK(out_of_range == 0,
          "B2: 350->30 stays in [0,360) for all t (%d of %d out of range)",
          out_of_range, N + 1);
    CHECK(wraps == 1, "B2: 350->30 crosses the 0/360 boundary exactly once (%d)", wraps);
    CHECK_F(nav_heading_at(S, T, 1.0), T, 1e-12, "B2: h(1) == target (30), not 390");

    /* The crossing happens where the raw trajectory hits 360 (t=0.25):
     * 359.6 before, 0.4 after — both inside [0, 360). */
    CHECK_F(nav_heading_at(S, T, 0.24), 359.6, 1e-9, "B2: h(0.24) == 359.6");
    CHECK_F(nav_heading_at(S, T, 0.26), 0.4, 1e-9, "B2: h(0.26) == 0.4");
    CHECK_F(nav_heading_at(S, T, 0.5), 10.0, 1e-9, "B2: h(0.5) == 10 (was 370)");

    /* The raw (pre-fix) trajectory really left the range, proving the
     * test discriminates: at t=1 it is 390. */
    CHECK(old_heading_at(S, T, 1.0) > 360.0,
          "B2: pre-fix formula reaches %.1f at t=1 (discriminating case)",
          old_heading_at(S, T, 1.0));
}

/* ================================================================== */
/* 3. Shortest-arc property                                            */
/* ================================================================== */
static void test_shortest_arc(void) {
    /* 350 -> 10 turns +20 through 0 (NOT -340 through 180) */
    CHECK_F(nav_heading_at(350.0, 10.0, 0.5), 0.0, 1e-12, "arc: 350->10, h(0.5) via 0");
    CHECK_F(nav_heading_at(350.0, 10.0, 1.0), 10.0, 1e-12, "arc: 350->10, h(1)");
    /* 10 -> 350 turns -20 the other way, also through 0 */
    CHECK_F(nav_heading_at(10.0, 350.0, 0.5), 0.0, 1e-12, "arc: 10->350, h(0.5) via 0");
    CHECK_F(nav_heading_at(10.0, 350.0, 1.0), 350.0, 1e-12, "arc: 10->350, h(1)");

    /* Exactly ±180°: the delta loop keeps it, mid-point is 90 either way */
    CHECK_F(nav_heading_at(0.0, 180.0, 0.5), 90.0, 1e-12, "arc: 0->180, h(0.5)");
    CHECK_F(nav_heading_at(180.0, 0.0, 0.5), 90.0, 1e-12, "arc: 180->0, h(0.5)");
    CHECK_F(nav_heading_at(180.0, 0.0, 1.0), 0.0, 1e-12, "arc: 180->0, h(1)");

    /* A plain 100 -> 200 turn never crosses the boundary */
    int out = 0;
    for (int i = 0; i <= 100; i++) {
        double h = nav_heading_at(100.0, 200.0, (double)i / 100.0);
        if (!in_range(h) || h < 100.0 - 1e-12 || h > 200.0 + 1e-12) out++;
    }
    CHECK(out == 0, "arc: 100->200 monotonic in [100,200] (%d violations)", out);
}

/* ================================================================== */
/* 4. Motion invariance (wrap must not change the ship orientation)    */
/* ================================================================== */
static void test_motion_invariance(void) {
    static const double starts[][2] = {
        {350.0, 30.0}, {30.0, 350.0}, {0.0, 180.0}, {180.0, 0.0},
        {359.5, 0.5}, {120.0, 300.0}, {0.0, 0.0}, {90.0, 270.0}
    };
    int bad = 0;
    for (size_t k = 0; k < sizeof(starts) / sizeof(starts[0]); k++) {
        for (int i = 0; i <= 20; i++) {
            double t = (double)i / 20.0;
            double hf = nav_heading_at(starts[k][0], starts[k][1], t);
            double ho = old_heading_at(starts[k][0], starts[k][1], t);
            double rad = M_PI / 180.0;
            if (fabs(cos(hf * rad) - cos(ho * rad)) > 1e-12
                || fabs(sin(hf * rad) - sin(ho * rad)) > 1e-12) bad++;
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
    srand(20260924);
    const int N = 20000;
    int out_of_range = 0, motion_bad = 0, end_bad = 0;
    for (int i = 0; i < N; i++) {
        double s = (double)(rand() % 36000) / 100.0;      /* [0, 360) */
        double tg = (double)(rand() % 36000) / 100.0;     /* [0, 360) */
        double t = (double)(rand() % 1001) / 1000.0;      /* [0, 1] */

        double h = nav_heading_at(s, tg, t);
        if (!in_range(h)) out_of_range++;

        double ho = old_heading_at(s, tg, t);
        double rad = M_PI / 180.0;
        if (fabs(cos(h * rad) - cos(ho * rad)) > 1e-12
            || fabs(sin(h * rad) - sin(ho * rad)) > 1e-12) motion_bad++;

        if (fabs(nav_heading_at(s, tg, 0.0) - nav_wrap_heading(s)) > 1e-9
            || fabs(nav_heading_at(s, tg, 1.0) - nav_wrap_heading(tg)) > 1e-9)
            end_bad++;
    }
    CHECK(out_of_range == 0, "fuzz: %d/%d headings outside [0,360)", out_of_range, N);
    CHECK(motion_bad == 0, "fuzz: %d/%d samples changed the orientation", motion_bad, N);
    CHECK(end_bad == 0, "fuzz: %d/%d endpoints wrong (h(0)=start, h(1)=target)", end_bad, N);
}

int main(void) {
    printf("nav_heading test (B2: server heading must stay in [0, 360))\n");
    test_wrap();
    test_b2_scenario();
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
