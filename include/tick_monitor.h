/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * tick_monitor.h — the server's own measure of its 60 Hz tick
 * (2026.11.01.13). The game thread times the phases of every tick (the
 * logic under game_mutex, a galaxy save image, the network updates, the
 * telemetry) and how late it woke up; a tick whose total exceeds the
 * budget (GAME_TICK_NSEC) is an overrun. Overruns are reported as one
 * warning at most every TICK_MONITOR_REPORT_S seconds, with the worst
 * tick of the interval broken down by phase (and the time spent waiting
 * for game_mutex, which another thread may hold), so a slow phase on a real
 * machine is visible in the log without a profiler; the totals are
 * logged when the server stops.
 *
 * Header-only and free of server globals: tests/tick_monitor_test.c
 * drives it with synthetic ticks.
 */
#ifndef TICK_MONITOR_H
#define TICK_MONITOR_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TICK_MONITOR_REPORT_S 10.0

enum {
    TICK_PHASE_WAKE,       /* woke up after the tick's deadline by */
    TICK_PHASE_LOCK,       /* waited for game_mutex (held by another thread) */
    TICK_PHASE_LOGIC,      /* update_game_logic + telemetry sync (game_mutex) */
    TICK_PHASE_SAVE,       /* the galaxy save image (game_mutex), 0 without a save */
    TICK_PHASE_SEND,       /* send_pending_updates */
    TICK_PHASE_TELEMETRY,  /* telemetry_broadcast */
    TICK_PHASES
};

typedef struct {
    double budget_ms;
    uint64_t ticks;
    uint64_t overruns;
    double worst_ms;              /* worst total since the start */
    /* the current report interval */
    double window_start_s;
    uint64_t window_overruns;
    double window_worst_ms;
    double window_worst_phase[TICK_PHASES];
    double window_worst_cpu;      /* CPU time of the game thread in that tick */
    uint64_t window_worst_tick;
} TickMonitor;

static inline void tick_monitor_init(TickMonitor *m, double budget_ms, double now_s) {
    memset(m, 0, sizeof(*m));
    m->budget_ms = budget_ms;
    m->window_start_s = now_s;
}

static inline double tick_monitor_total(const double phase_ms[TICK_PHASES]) {
    double t = 0.0;
    for (int i = 0; i < TICK_PHASES; i++) t += phase_ms[i];
    return t;
}

/* A tick whose CPU time is less than half its wall time spent most of it
 * waiting for a CPU (other programs, the host of a virtual machine): the
 * cause is the machine's load, not the server's work. */
static inline bool tick_monitor_starved(double wall_ms, double cpu_ms) {
    return cpu_ms >= 0.0 && cpu_ms * 2.0 < wall_ms;
}

/* Record one tick (cpu_ms: CPU time of the game thread in it, < 0 when
 * unknown). Returns true, with the warning in msg, when the
 * interval that just ended had overruns; the interval restarts then. */
static inline bool tick_monitor_record(TickMonitor *m, uint64_t tick, const double phase_ms[TICK_PHASES],
                                       double cpu_ms, double now_s, char *msg, size_t msg_len) {
    double total = tick_monitor_total(phase_ms);
    m->ticks++;
    if (total > m->worst_ms) m->worst_ms = total;
    if (total > m->budget_ms) {
        m->overruns++;
        m->window_overruns++;
        if (total > m->window_worst_ms) {
            m->window_worst_ms = total;
            memcpy(m->window_worst_phase, phase_ms, sizeof(m->window_worst_phase));
            m->window_worst_cpu = cpu_ms;
            m->window_worst_tick = tick;
        }
    }
    if (now_s - m->window_start_s < TICK_MONITOR_REPORT_S) return false;
    bool report = m->window_overruns > 0;
    if (report && msg && msg_len) {
        const double *p = m->window_worst_phase;
        snprintf(msg, msg_len,
                 "%llu tick(s) over the %.1f ms budget in the last %.0f s; worst: tick %llu, %.1f ms "
                 "(logic %.1f, save image %.1f, network %.1f, telemetry %.1f, waiting for game_mutex %.1f, "
                 "late wake-up %.1f; CPU of the game thread %.1f ms%s)",
                 (unsigned long long)m->window_overruns, m->budget_ms, now_s - m->window_start_s,
                 (unsigned long long)m->window_worst_tick, m->window_worst_ms, p[TICK_PHASE_LOGIC],
                 p[TICK_PHASE_SAVE], p[TICK_PHASE_SEND], p[TICK_PHASE_TELEMETRY], p[TICK_PHASE_LOCK],
                 p[TICK_PHASE_WAKE], m->window_worst_cpu,
                 tick_monitor_starved(m->window_worst_ms, m->window_worst_cpu)
                     ? ": the thread mostly waited for a CPU, the machine is busy"
                     : "");
    }
    m->window_start_s = now_s;
    m->window_overruns = 0;
    m->window_worst_ms = 0.0;
    m->window_worst_cpu = 0.0;
    memset(m->window_worst_phase, 0, sizeof(m->window_worst_phase));
    return report;
}

/* OpenMP threads of the tick's parallel loops (2026.11.01.13):
 * SPACEGL_TICK_THREADS, 1..64, default 1. The loops are short (64
 * torpedoes, 4000 NPCs every third tick): a second thread saves about 6%
 * of the average tick, but a join waits for every thread of the team,
 * and a thread the system scheduler has just taken away from its CPU
 * (another program, the save being written, a deck on the same machine)
 * holds the tick for a whole time slice, 5-10 ms. Measured on the real
 * server with decks logging in and out: 33 such ticks with two threads,
 * 5 with one. A dedicated many-core machine may gain from more. */
static inline int tick_threads_from_env(const char *value) {
    if (!value || !*value) return 1;
    char *end = NULL;
    long n = strtol(value, &end, 10);
    if (*end != '\0' || n < 1) return 1;
    return n > 64 ? 64 : (int)n;
}

/* The totals, for the shutdown log. */
static inline void tick_monitor_summary(const TickMonitor *m, char *msg, size_t msg_len) {
    double pct = m->ticks ? 100.0 * (double)m->overruns / (double)m->ticks : 0.0;
    snprintf(msg, msg_len, "%llu ticks, %llu over the %.1f ms budget (%.3f%%), worst %.1f ms",
             (unsigned long long)m->ticks, (unsigned long long)m->overruns, m->budget_ms, pct, m->worst_ms);
}

#endif
