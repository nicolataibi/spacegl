/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * lrs_grid.c — the long-range-scan digits of spacegl_master.g (one 64-bit
 * value per quadrant: how many objects of each kind it holds, from the
 * spatial index). Moved out of galaxy_index.c (2026.11.01.13) so that
 * tests/server_tick_bench.c runs the real code.
 *
 * The whole grid (64 000 quadrants, each read across its QuadrantIndex)
 * cost 10-15 ms, 30 ms on a busy machine, and ran once per second inside
 * one tick, over most of its 16.7 ms budget (measured with the tick
 * monitor, include/tick_monitor.h). refresh_lrs_grid_slice() refreshes one
 * plane of quadrants per tick instead: every quadrant still once per
 * second (GALAXY_SIZE = 40 planes, GAME_TICK_RATE = 60 ticks), about
 * 0.3 ms per tick.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include "server_internal.h"

static void refresh_lrs_plane(int i) {
    for (int j = 1; j <= GALAXY_SIZE; j++) {
        for (int l = 1; l <= GALAXY_SIZE; l++) {
            QuadrantIndex *q = &spatial_index[i][j][l];
            int c_mon = (q->monster_count > 9) ? 9 : q->monster_count;
            int c_u = (q->player_count > 9) ? 9 : q->player_count;
            /* Torpedoes contribute to tactical signal noise in LRS */
            int c_tor = (q->torpedo_count > 9) ? 9 : q->torpedo_count;

            int c_rift = (q->rift_count > 9) ? 9 : q->rift_count;
            int c_plat = (q->platform_count > 9) ? 9 : q->platform_count;
            int c_buoy = (q->buoy_count > 9) ? 9 : q->buoy_count;
            int c_mine = (q->mine_count > 9) ? 9 : q->mine_count;
            int c_der = (q->derelict_count > 9) ? 9 : q->derelict_count;
            int c_ast = (q->asteroid_count > 9) ? 9 : q->asteroid_count;
            int c_com = (q->comet_count > 9) ? 9 : q->comet_count;
            int c_pul = (q->pulsar_count > 9) ? 9 : q->pulsar_count;
            int c_neb = (q->nebula_count > 9) ? 9 : q->nebula_count;
            int c_bh = (q->bh_count > 9) ? 9 : q->bh_count;
            int c_p = ((q->planet_count + q->sys_planet_count) > 9) ? 9 : (q->planet_count + q->sys_planet_count);
            int c_npc = (q->npc_count > 9) ? 9 : q->npc_count;
            int c_b = (q->base_count > 9) ? 9 : q->base_count;
            int c_s = ((q->star_count + q->sys_star_count) > 9) ? 9 : (q->star_count + q->sys_star_count);

            int c_qsr = (q->quasar_count > 9) ? 9 : q->quasar_count;
            int c_sub = (q->subspace_anomaly_count > 9) ? 9 : q->subspace_anomaly_count;

            long long storm_flag = (spacegl_master.g[i][j][l] / 10000000LL) % 10;
            spacegl_master.g[i][j][l] = (long long)c_sub * 1000000000000000000LL + (long long)c_qsr * 100000000000000000LL + (long long)c_mon * 10000000000000000LL + (long long)c_u * 1000000000000000LL + (long long)c_tor * 1000000000000000LL /* Combine torps with user activity for LRS */
                                        + (long long)c_rift * 100000000000000LL + (long long)c_plat * 10000000000000LL + (long long)c_buoy * 1000000000000LL + (long long)c_mine * 100000000000LL + (long long)c_der * 10000000000LL + (long long)c_ast * 1000000000LL + (long long)c_com * 100000000LL + (long long)storm_flag * 10000000LL + (long long)c_pul * 1000000 + (long long)c_neb * 100000 + (long long)c_bh * 10000 + (long long)c_p * 1000 + (long long)c_npc * 100 + (long long)c_b * 10 + (long long)c_s;
        }
    }
}

/* The whole grid at once: boot (galaxy generation) only. */
void refresh_lrs_grid(void) {
    for (int i = 1; i <= GALAXY_SIZE; i++) refresh_lrs_plane(i);
}

/* The planes of this tick: plane k of the grid is refreshed at tick
 * (k - 1) of every second, so the whole grid once per GAME_TICK_RATE
 * ticks. */
void refresh_lrs_grid_slice(int tick) {
    enum { PER_TICK = (GALAXY_SIZE + GAME_TICK_RATE - 1) / GAME_TICK_RATE };
    int slot = tick % GAME_TICK_RATE;
    if (slot < 0) slot += GAME_TICK_RATE;
    for (int k = 0; k < PER_TICK; k++) {
        int i = slot * PER_TICK + k + 1;
        if (i <= GALAXY_SIZE) refresh_lrs_plane(i);
    }
}
