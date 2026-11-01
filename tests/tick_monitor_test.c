/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * tick_monitor_test — the server's measure of its own tick
 * (include/tick_monitor.h, 2026.11.01.13), with synthetic ticks:
 *  1. ticks inside the budget: no warning, ever; the totals count them;
 *  2. overruns: one warning per 10 s interval, naming how many, the worst
 *     tick and its phases; the interval restarts after it;
 *  3. a tick that mostly waited for a CPU is called so (CPU < wall / 2);
 *     one that worked is not;
 *  4. the shutdown summary (ticks, overruns, share, worst);
 *  5. SPACEGL_TICK_THREADS: default 1, 1..64, anything else 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tick_monitor.h"

#define TEST_CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "TEST FAILED: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

static void phases(double out[TICK_PHASES], double logic, double save, double send) {
    memset(out, 0, sizeof(double) * TICK_PHASES);
    out[TICK_PHASE_LOGIC] = logic;
    out[TICK_PHASE_SAVE] = save;
    out[TICK_PHASE_SEND] = send;
}

int main(void) {
    const double budget = 16.666666;
    const double dt = 1.0 / 60.0;
    char msg[512];
    double p[TICK_PHASES];

    /* 1. 20 s of ticks inside the budget */
    TickMonitor m;
    tick_monitor_init(&m, budget, 0.0);
    double t = 0.0;
    for (int i = 0; i < 1200; i++) {
        t += dt;
        phases(p, 2.5, 0.0, 0.3);
        TEST_CHECK(!tick_monitor_record(&m, (uint64_t)i, p, 2.8, t, msg, sizeof(msg)));
    }
    TEST_CHECK(m.ticks == 1200 && m.overruns == 0);
    TEST_CHECK(m.worst_ms > 2.7 && m.worst_ms < 2.9);
    printf("  [1] 1200 ticks inside the budget: no warning: OK\n");

    /* 2. two overruns in one interval: one warning at its end */
    tick_monitor_init(&m, budget, 0.0);
    t = 0.0;
    int reports = 0;
    for (int i = 0; i < 700; i++) {   /* 11.7 s */
        t += dt;
        if (i == 100) phases(p, 2.0, 30.0, 0.5);        /* a save image of 30 ms */
        else if (i == 200) phases(p, 18.0, 0.0, 0.2);   /* a slow logic tick */
        else phases(p, 2.5, 0.0, 0.3);
        double cpu = (i == 100) ? 32.0 : (i == 200 ? 18.1 : 2.8);
        if (tick_monitor_record(&m, (uint64_t)i, p, cpu, t, msg, sizeof(msg))) {
            reports++;
            TEST_CHECK(strstr(msg, "2 tick(s) over the 16.7 ms budget") != NULL);
            TEST_CHECK(strstr(msg, "worst: tick 100, 32.5 ms") != NULL);
            TEST_CHECK(strstr(msg, "save image 30.0") != NULL);
            TEST_CHECK(strstr(msg, "CPU of the game thread 32.0 ms)") != NULL);
            TEST_CHECK(strstr(msg, "waited for a CPU") == NULL);
            printf("  warning: %s\n", msg);
        }
    }
    TEST_CHECK(reports == 1);
    TEST_CHECK(m.overruns == 2 && m.window_overruns == 0);
    printf("  [2] two overruns: one warning with the worst tick: OK\n");

    /* 3. a tick that waited for a CPU */
    tick_monitor_init(&m, budget, 0.0);
    phases(p, 40.0, 0.0, 0.0);
    TEST_CHECK(!tick_monitor_record(&m, 7, p, 3.0, 1.0, msg, sizeof(msg)));
    phases(p, 2.0, 0.0, 0.0);
    TEST_CHECK(tick_monitor_record(&m, 8, p, 2.0, 11.0, msg, sizeof(msg)));
    TEST_CHECK(strstr(msg, "the thread mostly waited for a CPU, the machine is busy") != NULL);
    TEST_CHECK(tick_monitor_starved(40.0, 3.0) && !tick_monitor_starved(40.0, 39.0));
    TEST_CHECK(!tick_monitor_starved(40.0, -1.0));   /* CPU time unknown */
    printf("  [3] a starved tick is named as such: OK\n");

    /* 4. summary */
    char sum[200];
    tick_monitor_summary(&m, sum, sizeof(sum));
    TEST_CHECK(strstr(sum, "2 ticks, 1 over the 16.7 ms budget (50.000%), worst 40.0 ms") != NULL);
    printf("  [4] summary: %s: OK\n", sum);

    /* 5. SPACEGL_TICK_THREADS */
    TEST_CHECK(tick_threads_from_env(NULL) == 1);
    TEST_CHECK(tick_threads_from_env("") == 1);
    TEST_CHECK(tick_threads_from_env("4") == 4);
    TEST_CHECK(tick_threads_from_env("0") == 1);
    TEST_CHECK(tick_threads_from_env("-3") == 1);
    TEST_CHECK(tick_threads_from_env("2x") == 1);
    TEST_CHECK(tick_threads_from_env("1000") == 64);
    printf("  [5] SPACEGL_TICK_THREADS parsing: OK\n");

    printf("PASS: tick_monitor\n");
    return 0;
}
