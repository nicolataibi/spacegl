/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * galaxy_index.c — spatial index of the galaxy: static index, dirty-quadrant tracking, LRS grid and per-tick rebuild.
 * Split from galaxy.c (2026.10.31.16) without code change.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <stdatomic.h>
#include <pthread.h>
#include "server_internal.h"
#include "object_types.h"
#include "quad_index.h"
#include "moon_orbits.h"
#include "ui.h"

QuadrantIndex (*spatial_index)[41][41] = NULL;

/* Optimization: Tracking only quadrants that contain dynamic objects to avoid triple-loop resets */
typedef struct { uint16_t q1, q2, q3; } DirtyQuad;
static DirtyQuad dirty_quads[10000]; 
static int dirty_count = 0;

void init_static_spatial_index() {
    if (!spatial_index) {
        spatial_index = calloc((size_t)41 * 41 * 41, sizeof(QuadrantIndex));
        if (!spatial_index) { perror("Failed to allocate spatial_index"); exit(1); }  // NOLINT(concurrency-mt-unsafe): only one thread ever calls exit())
    }
    memset(spatial_index, 0, (size_t)41 * 41 * 41 * sizeof(QuadrantIndex));

    /* B3: the static insertion used to be a hand-written set of 12
     * per-type loops here that skipped the quasar and the other 49
     * static types, so their quadrant counts stayed 0 forever and the
     * per-frame update / LRS grid / LRS-SRS lists never sent them to
     * the clients. The complete table (all 62 static types) and the
     * insertion algorithm now live in src/server/quad_index.c.
     * Dynamic types are still handled per tick by
     * rebuild_spatial_index() below. */
    quad_index_insert_static_all(spatial_index);
}

static void mark_quad_dirty(int q1, int q2, int q3) {
    QuadrantIndex *q = &spatial_index[q1][q2][q3];
    if (q->npc_count == 0 && q->player_count == 0 && q->comet_count == 0 && q->asteroid_count == 0 && 
        q->derelict_count == 0 && q->mine_count == 0 && q->buoy_count == 0 && q->platform_count == 0 && 
        q->rift_count == 0 && q->monster_count == 0 && q->torpedo_count == 0) {
        if (dirty_count < 10000) {
            dirty_quads[dirty_count++] = (DirtyQuad){(uint16_t)q1, (uint16_t)q2, (uint16_t)q3};
        }
    }
}

void refresh_lrs_grid() {
    for (int i = 1; i <= GALAXY_SIZE; i++) {
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
                spacegl_master.g[i][j][l] = (long long)c_sub * 1000000000000000000LL
                                          + (long long)c_qsr * 100000000000000000LL
                                          + (long long)c_mon * 10000000000000000LL 
                                          + (long long)c_u   * 1000000000000000LL
                                          + (long long)c_tor * 1000000000000000LL /* Combine torps with user activity for LRS */
                                          + (long long)c_rift * 100000000000000LL 
                                          + (long long)c_plat * 10000000000000LL 
                                          + (long long)c_buoy * 1000000000000LL 
                                          + (long long)c_mine * 100000000000LL 
                                          + (long long)c_der * 10000000000LL 
                                          + (long long)c_ast * 1000000000LL 
                                          + (long long)c_com * 100000000LL 
                                          + (long long)storm_flag * 10000000LL
                                          + (long long)c_pul * 1000000 
                                          + (long long)c_neb * 100000 
                                          + (long long)c_bh * 10000
                                          + (long long)c_p * 1000 
                                          + (long long)c_npc * 100 
                                          + (long long)c_b * 10 
                                          + (long long)c_s;
            }
        }
    }
}

void rebuild_spatial_index() {
    if (!spatial_index) {
        spatial_index = calloc((size_t)41 * 41 * 41, sizeof(QuadrantIndex));
        if (!spatial_index) { perror("Failed to allocate spatial_index"); exit(1); }  // NOLINT(concurrency-mt-unsafe): only one thread ever calls exit())
    }

    /* Reset ONLY the dynamic-object counts in dirty quadrants.
     * Static objects (stars, planets, bases, nebulae, cosmic structures, etc.)
     * are placed once by init_static_spatial_index() and never move, so their
     * counts are preserved. Only truly mobile objects are re-inserted below. */
    for (int i = 0; i < dirty_count; i++) {
        QuadrantIndex *q = &spatial_index[dirty_quads[i].q1][dirty_quads[i].q2][dirty_quads[i].q3];
        q->npc_count      = 0;                         /* NPCs are always dynamic */
        q->player_count   = 0;
        q->torpedo_count  = 0;
        q->monster_count  = 0;
        q->comet_count    = 0;
        q->asteroid_count = 0;
        q->derelict_count = 0;
        q->mine_count     = 0;
        q->buoy_count     = 0;
        q->platform_count = 0;
        q->rift_count     = 0;
    }
    dirty_count = 0;

    /* Re-insert only truly dynamic objects (those that can change quadrant each tick) */
    for (int n = 0; n < MAX_NPC; n++) {
        if (npcs[n].active) {
            if (IS_Q_VALID(npcs[n].q1, npcs[n].q2, npcs[n].q3)) {
                mark_quad_dirty(npcs[n].q1, npcs[n].q2, npcs[n].q3);
                QuadrantIndex *q = &spatial_index[npcs[n].q1][npcs[n].q2][npcs[n].q3];
                if (q->npc_count < MAX_Q_NPC) {
                    q->npcs[q->npc_count++] = &npcs[n];
                }
            }
        }
    }
    for (int m = 0; m < MAX_MONSTERS; m++) {
        if (monsters[m].active) {
            if (IS_Q_VALID(monsters[m].q1, monsters[m].q2, monsters[m].q3)) {
                mark_quad_dirty(monsters[m].q1, monsters[m].q2, monsters[m].q3);
                QuadrantIndex *q = &spatial_index[monsters[m].q1][monsters[m].q2][monsters[m].q3];
                if (q->monster_count < MAX_Q_MONSTERS) {
                    q->monsters[q->monster_count++] = &monsters[m];
                }
            }
        }
    }
    for (int u = 0; u < MAX_CLIENTS; u++) {
        if (players[u].active && players[u].name[0] != '\0') {
            if (IS_Q_VALID(players[u].state.q1, players[u].state.q2, players[u].state.q3)) {
                mark_quad_dirty(players[u].state.q1, players[u].state.q2, players[u].state.q3);
                QuadrantIndex *q = &spatial_index[players[u].state.q1][players[u].state.q2][players[u].state.q3];
                if (q->player_count < MAX_Q_PLAYERS) {
                    q->players[q->player_count++] = &players[u];
                }
            }
        }
    }

    /* === DIAG [LRS-3] (smoke test): log each player whose quadrant changed
     * this tick, with the FINAL per-quadrant player index (ids). Pinpoints
     * exactly when a ship enters/leaves a quadrant and whether the index
     * holds each player exactly once (duplicates or drops would show up as
     * a repeated/missing id or a count that keeps growing tick over tick). === */
    {
        static int last_q1[MAX_CLIENTS], last_q2[MAX_CLIENTS], last_q3[MAX_CLIENTS];
        static int last_diag_init = 0;
        if (!last_diag_init) {
            for (int u = 0; u < MAX_CLIENTS; u++) { last_q1[u] = -1; last_q2[u] = -1; last_q3[u] = -1; }
            last_diag_init = 1;
        }
        for (int u = 0; u < MAX_CLIENTS; u++) {
            if (!players[u].active || players[u].name[0] == '\0') continue;
            int q1 = players[u].state.q1, q2 = players[u].state.q2, q3 = players[u].state.q3;
            if (q1 == last_q1[u] && q2 == last_q2[u] && q3 == last_q3[u]) continue;
            last_q1[u] = q1; last_q2[u] = q2; last_q3[u] = q3;
            if (!IS_Q_VALID(q1, q2, q3)) continue;
            QuadrantIndex *q = &spatial_index[q1][q2][q3];
            char ids[192]; int off = 0;
            for (int j = 0; j < q->player_count && j < MAX_Q_PLAYERS && off < (int)sizeof(ids) - 16; j++)
                off += snprintf(ids + off, sizeof(ids) - (size_t)off, "%d,", (int)(q->players[j] - players) + 1);
            SG_TRACE3(SG_CAT_LRS,
                "spatial: player %s(slot %d) now in Q-%d-%d-%d: quad players=%d[%s] npcs=%d",
                players[u].name, u, q1, q2, q3, q->player_count, ids, q->npc_count);
        }
    }

    for (int t = 0; t < MAX_GLOBAL_TORPEDOES; t++) {
        if (players_torpedoes[t].active) {
            if (IS_Q_VALID(players_torpedoes[t].q1, players_torpedoes[t].q2, players_torpedoes[t].q3)) {
                mark_quad_dirty(players_torpedoes[t].q1, players_torpedoes[t].q2, players_torpedoes[t].q3);
                QuadrantIndex *q = &spatial_index[players_torpedoes[t].q1][players_torpedoes[t].q2][players_torpedoes[t].q3];
                if (q->torpedo_count < MAX_Q_TORPEDOES) {
                    q->torpedoes[q->torpedo_count++] = &players_torpedoes[t];
                }
            }
        }
    }
    for (int c = 0; c < MAX_COMETS; c++) {
        if (comets[c].active) {
            if (IS_Q_VALID(comets[c].q1, comets[c].q2, comets[c].q3)) {
                mark_quad_dirty(comets[c].q1, comets[c].q2, comets[c].q3);
                QuadrantIndex *q = &spatial_index[comets[c].q1][comets[c].q2][comets[c].q3];
                if (q->comet_count < MAX_Q_COMETS) {
                    q->comets[q->comet_count++] = &comets[c];
                }
            }
        }
    }
    for (int a = 0; a < MAX_ASTEROIDS; a++) {
        if (asteroids[a].active) {
            if (IS_Q_VALID(asteroids[a].q1, asteroids[a].q2, asteroids[a].q3)) {
                mark_quad_dirty(asteroids[a].q1, asteroids[a].q2, asteroids[a].q3);
                QuadrantIndex *q = &spatial_index[asteroids[a].q1][asteroids[a].q2][asteroids[a].q3];
                if (q->asteroid_count < MAX_Q_ASTEROIDS) {
                    q->asteroids[q->asteroid_count++] = &asteroids[a];
                }
            }
        }
    }
    for (int d = 0; d < MAX_DERELICTS; d++) {
        if (derelicts[d].active) {
            if (IS_Q_VALID(derelicts[d].q1, derelicts[d].q2, derelicts[d].q3)) {
                mark_quad_dirty(derelicts[d].q1, derelicts[d].q2, derelicts[d].q3);
                QuadrantIndex *q = &spatial_index[derelicts[d].q1][derelicts[d].q2][derelicts[d].q3];
                if (q->derelict_count < MAX_Q_DERELICTS) {
                    q->derelicts[q->derelict_count++] = &derelicts[d];
                }
            }
        }
    }
    for (int m = 0; m < MAX_MINES; m++) {
        if (mines[m].active) {
            if (IS_Q_VALID(mines[m].q1, mines[m].q2, mines[m].q3)) {
                mark_quad_dirty(mines[m].q1, mines[m].q2, mines[m].q3);
                QuadrantIndex *q = &spatial_index[mines[m].q1][mines[m].q2][mines[m].q3];
                if (q->mine_count < MAX_Q_MINES) {
                    q->mines[q->mine_count++] = &mines[m];
                }
            }
        }
    }
    for (int b = 0; b < MAX_BUOYS; b++) {
        if (buoys[b].active) {
            if (IS_Q_VALID(buoys[b].q1, buoys[b].q2, buoys[b].q3)) {
                mark_quad_dirty(buoys[b].q1, buoys[b].q2, buoys[b].q3);
                QuadrantIndex *q = &spatial_index[buoys[b].q1][buoys[b].q2][buoys[b].q3];
                if (q->buoy_count < MAX_Q_BUOYS) {
                    q->buoys[q->buoy_count++] = &buoys[b];
                }
            }
        }
    }
    for (int pl = 0; pl < MAX_PLATFORMS; pl++) {
        if (platforms[pl].active) {
            if (IS_Q_VALID(platforms[pl].q1, platforms[pl].q2, platforms[pl].q3)) {
                mark_quad_dirty(platforms[pl].q1, platforms[pl].q2, platforms[pl].q3);
                QuadrantIndex *q = &spatial_index[platforms[pl].q1][platforms[pl].q2][platforms[pl].q3];
                if (q->platform_count < MAX_Q_PLATFORMS) {
                    q->platforms[q->platform_count++] = &platforms[pl];
                }
            }
        }
    }
    for (int r = 0; r < MAX_RIFTS; r++) {
        if (rifts[r].active) {
            if (IS_Q_VALID(rifts[r].q1, rifts[r].q2, rifts[r].q3)) {
                mark_quad_dirty(rifts[r].q1, rifts[r].q2, rifts[r].q3);
                QuadrantIndex *q = &spatial_index[rifts[r].q1][rifts[r].q2][rifts[r].q3];
                if (q->rift_count < MAX_Q_RIFTS) {
                    q->rifts[q->rift_count++] = &rifts[r];
                }
            }
        }
    }
}

