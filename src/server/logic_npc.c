/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * logic_npc.c — NPC behaviour: nebula test, per-NPC runtime state, obstacle
 * avoidance and escape, collisions with solid objects and the AI step.
 * Split from logic.c (2026.10.31.17) without code change.
 */

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <sys/socket.h>
#include <omp.h>
#include "game_config.h"
#include "server_internal.h"
#include "object_types.h"
#include "shared_state.h"
#include "nav_math.h"
#include "obstacles.h"
#include "ui.h"
#include "logic_internal.h"

bool is_player_in_nebula(int i) {
    if (!players[i].active) {
        return false;
    }
    int q1 = players[i].state.q1;
    int q2 = players[i].state.q2;
    int q3 = players[i].state.q3;
    QuadrantIndex *q = &spatial_index[q1][q2][q3];
    for (int n = 0; n < q->nebula_count; n++) {
        double d = sqrt(pow(players[i].state.s1 - q->nebulas[n]->x, 2) + 
                        pow(players[i].state.s2 - q->nebulas[n]->y, 2) + 
                        pow(players[i].state.s3 - q->nebulas[n]->z, 2));
        if (d < DIST_NEBULA_EFFECT) {
            return true;
        }
    }
    return false;
}

/* --- NPC obstacle avoidance --------------------------------------------- */

/* Runtime state of NPC n for obstacle avoidance and collisions. Runtime only
 * (nothing is saved); update_npc_ai(n) and npc_collision_check(n) are the only
 * code that touches element n, so the parallel NPC loop needs no lock.
 * A slot taken by a freshly spawned NPC must call npc_runtime_reset(n): what
 * the previous occupant left here is about another ship and another place. */
typedef struct {
    /* AI steps of smooth turning left after the way ahead of the NPC became clear. */
    unsigned char  avoid_hold;
    /* AI steps left before the NPC scans for obstacles again; an NPC in open
     * space scans every (TIMER_NPC_AVOID_IDLE + 1) steps, one near objects at
     * every step. */
    unsigned char  avoid_skip;
    /* Ticks left before the NPC can take damage from another impact. */
    unsigned short collision_grace;
    /* Heading of the NPC when it last found open space (avoid_skip armed): a
     * new heading invalidates the "open space" verdict. */
    double         scan_dir[3];
} NpcRuntime;

static NpcRuntime g_npc_rt[MAX_NPC];

/* Explicit spawn hook: the NPC slot n now holds a new ship. */
void npc_runtime_reset(int n) {
    if (n < 0 || n >= MAX_NPC) return;
    memset(&g_npc_rt[n], 0, sizeof g_npc_rt[n]);
}

/* Direction that takes the point p out of every keep-out sphere of obs[] it is
 * inside: the radial pushes, each weighted by how deep p is in that sphere.
 * Returns 0 when p is inside none of them. A point exactly on a centre gets a
 * pseudo-random side from `seed`. */
static int npc_escape_dir(const double p[3], const ObsSphere *obs, int cnt,
                          unsigned seed, double out[3]) {
    double sum[3] = { 0.0, 0.0, 0.0 };
    int inside = 0;
    for (int i = 0; i < cnt; i++) {
        double R = obs[i].r_avoid;
        if (R <= 0.0) continue;
        double rel[3] = { p[0] - obs[i].x, p[1] - obs[i].y, p[2] - obs[i].z };
        double D = sqrt(sa_dot(rel, rel));
        if (D >= R) continue;
        double away[3];
        if (D < 1e-9) {
            const double up[3] = { 0.0, 0.0, 1.0 };
            sa_perp(up, seed + (unsigned)i, away);
        } else {
            away[0] = rel[0] / D; away[1] = rel[1] / D; away[2] = rel[2] / D;
        }
        double w = (R - D) / R + 0.05;
        sum[0] += away[0] * w; sum[1] += away[1] * w; sum[2] += away[2] * w;
        inside = 1;
    }
    if (!inside) return 0;
    if (sa_norm(sum, out) < 1e-9) {                 /* pushes cancel out: any side will do */
        const double up[3] = { 0.0, 0.0, 1.0 };
        sa_perp(up, seed, out);
    }
    return 1;
}

/* NPC n sits inside a SOLID object (r_hit): the direction out of it. */
static int npc_embedded_escape(int n, double esc[3]) {
    double p[3] = { npcs[n].gx, npcs[n].gy, npcs[n].gz };
    ObsSphere obs[OBS_MAX_PER_CALL];
    int cnt = obs_gather_segment_ex(p, p, OBS_MAX_AVOID_R, OBS_MASK_ALL, obs, OBS_MAX_PER_CALL, &npcs[n]);
    int m = 0;
    for (int i = 0; i < cnt; i++) {
        if (obs[i].r_hit <= 0.0) continue;
        obs[m] = obs[i];
        obs[m].r_avoid = obs[m].r_hit;              /* "inside" = touching the object itself */
        m++;
    }
    return m > 0 && npc_escape_dir(p, obs, m, (unsigned)n, esc);
}

/* Steers the direction `dir` of NPC n around the solid objects AND the other
 * ships (NPCs and players) within DIST_NPC_AVOID_LOOKAHEAD (obstacles.h /
 * steer_avoid.h). `prev` is the heading of the previous AI step: the new
 * heading is blended into it, so the turn is a visible curve and not a kink,
 * and it keeps blending for TIMER_NPC_AVOID_HOLD steps after the obstacle is
 * behind. The step itself is also tested as a segment: if it would still
 * enter a solid object the NPC does not move this step (*speed = 0), but its
 * heading is turned away from that object, never cleared: a frozen NPC with a
 * null heading would stay put until its AI picks a new direction (that was the
 * "stuck inside an object" bug). A null heading (an NPC that was stopped, or
 * spawned without one) is replaced at once by the way out of the keep-out
 * spheres it is in. Returns 1 when `dir` was changed. Read-only on the index:
 * safe inside the OpenMP loop of the NPCs. */
static int npc_avoid_obstacles(int n, const double prev[3], double dir[3], double *speed) {
    if (*speed <= 0.0) {
        g_npc_rt[n].avoid_hold = 0;
        return 0;
    }
    double d[3];
    int null_heading = (sa_norm(dir, d) < 1e-9);
    if (!null_heading && g_npc_rt[n].avoid_hold == 0 && g_npc_rt[n].avoid_skip > 0) {
        g_npc_rt[n].avoid_skip--;
        return 0;
    }

    double p[3] = { npcs[n].gx, npcs[n].gy, npcs[n].gz };
    const double look = (double)DIST_NPC_AVOID_LOOKAHEAD;
    double end[3] = { p[0], p[1], p[2] };
    if (!null_heading) { end[0] += d[0] * look; end[1] += d[1] * look; end[2] += d[2] * look; }
    ObsSphere obs[OBS_MAX_PER_CALL];
    int cnt = obs_gather_segment_ex(p, end, OBS_MAX_AVOID_R, OBS_MASK_AVOID, obs, OBS_MAX_PER_CALL, &npcs[n]);

    if (null_heading) {
        double esc[3];
        if (cnt > 0 && npc_escape_dir(p, obs, cnt, (unsigned)n, esc)) {
            dir[0] = esc[0]; dir[1] = esc[1]; dir[2] = esc[2];
            g_npc_rt[n].avoid_skip = 0;
            return 1;
        }
        return 0;
    }

    if (cnt == 0 && g_npc_rt[n].avoid_hold == 0) {             /* the usual case: open space */
        g_npc_rt[n].avoid_skip = TIMER_NPC_AVOID_IDLE;
        g_npc_rt[n].scan_dir[0] = d[0]; g_npc_rt[n].scan_dir[1] = d[1]; g_npc_rt[n].scan_dir[2] = d[2];
        return 0;
    }
    g_npc_rt[n].avoid_skip = 0;                                /* objects near: look at every step */

    double steered[3];
    int threat = (cnt > 0) && sa_steer(p, d, look, obs, cnt, 0.0, (unsigned)n, 0, steered);
    int changed = 0;
    if (threat) {
        g_npc_rt[n].avoid_hold = TIMER_NPC_AVOID_HOLD;
    }
    if (threat || g_npc_rt[n].avoid_hold > 0) {
        const double *want = threat ? steered : d;
        double pn[3];
        double base[3];
        if (sa_norm(prev, pn) > 1e-6) { base[0] = pn[0]; base[1] = pn[1]; base[2] = pn[2]; }
        else { base[0] = want[0]; base[1] = want[1]; base[2] = want[2]; }
        /* A smooth blend can never turn a heading around: when the way out is
         * more than 90 degrees from the present heading (the NPC is already
         * inside a keep-out zone, flying at the object) turn at once. */
        double k = (sa_dot(base, want) < 0.0) ? 1.0 : (double)RATIO_NPC_AVOID_TURN;
        sa_blend(base, want, k, dir);
        if (!threat) g_npc_rt[n].avoid_hold--;
        changed = 1;
    }

    if (cnt > 0) {
        double p1[3] = { p[0] + dir[0] * *speed, p[1] + dir[1] * *speed, p[2] + dir[2] * *speed };
        double t;
        int hi = sa_first_hit(p, p1, obs, cnt, 0.0, 0, &t);
        if (hi >= 0) {
            /* the steering could not keep it out: no step this time, and a
             * heading that points away from the object it would have entered */
            double away[3] = { p[0] - obs[hi].x, p[1] - obs[hi].y, p[2] - obs[hi].z };
            if (sa_norm(away, away) < 1e-9) {
                const double up[3] = { 0.0, 0.0, 1.0 };
                sa_perp(up, (unsigned)n, away);
            }
            dir[0] = away[0]; dir[1] = away[1]; dir[2] = away[2];
            *speed = 0.0;
            changed = 1;
        }
    }
    return changed;
}

/* Comet `hit` was run into by a ship (a player's or an NPC's): the comet is
 * destroyed and the quadrant is told. Does nothing for other kinds. Must not
 * be called from inside an `omp critical` region. */
void comet_destroyed_by_ship(const ObsHit *hit) {
    if (hit->kind != OBS_COMET) return;
    int found = 0;
    for (int c = 0; c < MAX_COMETS; c++) {
        if (comets[c].active && comets[c].id == hit->id) {
            comets[c].active = 0;
            found = 1;
            break;
        }
    }
    if (!found) return;
    int hq1 = get_q_from_g(hit->point[0]);
    int hq2 = get_q_from_g(hit->point[1]);
    int hq3 = get_q_from_g(hit->point[2]);
    #pragma omp critical
    {
        for (int pi_ = 0; pi_ < MAX_CLIENTS; pi_++) {
            if (players[pi_].active && players[pi_].state.q1 == hq1 &&
                players[pi_].state.q2 == hq2 && players[pi_].state.q3 == hq3)
                send_server_msg(pi_, "SENSORS", "A comet was destroyed by a ship impact.");
        }
    }
}

/* Called after every movement of NPC n (AI step and fast physics step), which
 * went from (ox, oy, oz) to its current galactic position. Same rule as for
 * the players (ship_collision_check): a step that enters a solid object puts
 * the NPC on the surface, stops it, and it takes hull damage that grows with
 * its speed, plus IPC_EV_IMPACT for the quadrant. The heading is turned away
 * from the object instead of being cleared, so the NPC leaves at once; a
 * comet it hits is destroyed. The steering normally keeps NPCs out of
 * objects, so this is the safety net for what it cannot prevent (a fast
 * physics step, a heading changed by the AI, a crowded spot). */
static void npc_collision_check_impl(int n, double ox, double oy, double oz) {
    if (g_npc_rt[n].collision_grace > 0) g_npc_rt[n].collision_grace--;
    if (npcs[n].death_timer > 0) return;

    /* A scan of the AI found open space ahead and nothing changed since: the
     * few steps taken since cannot have reached any object. */
    if (g_npc_rt[n].avoid_skip > 0 && g_npc_rt[n].avoid_hold == 0 &&
        npcs[n].dx == g_npc_rt[n].scan_dir[0] && npcs[n].dy == g_npc_rt[n].scan_dir[1] &&
        npcs[n].dz == g_npc_rt[n].scan_dir[2])
        return;

    double p0[3] = { ox, oy, oz };
    double p1[3] = { npcs[n].gx, npcs[n].gy, npcs[n].gz };
    double d[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
    double len = sqrt(sa_dot(d, d));
    if (len < 1e-12) return;

    ObsHit hit;
    if (!obs_segment_hit(p0, p1, 0.0, OBS_MASK_ALL, 0, &hit)) return;

    /* on the surface, just outside the contact distance */
    double nrm[3] = { hit.point[0] - hit.centre[0], hit.point[1] - hit.centre[1], hit.point[2] - hit.centre[2] };
    double nl = sa_norm(nrm, nrm);
    if (nl < 1e-9) { nrm[0] = -d[0]; nrm[1] = -d[1]; nrm[2] = -d[2]; sa_norm(nrm, nrm); }
    double surf = hit.r_hit + 0.001;
    npcs[n].gx = hit.centre[0] + nrm[0] * surf;
    npcs[n].gy = hit.centre[1] + nrm[1] * surf;
    npcs[n].gz = hit.centre[2] + nrm[2] * surf;
    npcs[n].q1 = get_q_from_g(npcs[n].gx);
    npcs[n].q2 = get_q_from_g(npcs[n].gy);
    npcs[n].q3 = get_q_from_g(npcs[n].gz);
    npcs[n].x = npcs[n].gx - (npcs[n].q1 - 1) * QUADRANT_SIZE;
    npcs[n].y = npcs[n].gy - (npcs[n].q2 - 1) * QUADRANT_SIZE;
    npcs[n].z = npcs[n].gz - (npcs[n].q3 - 1) * QUADRANT_SIZE;

    /* stopped now, then free to leave: heading away from the object */
    npcs[n].dx = nrm[0]; npcs[n].dy = nrm[1]; npcs[n].dz = nrm[2];
    npcs[n].vx = npcs[n].vy = npcs[n].vz = 0.0;
    npcs[n].h = atan2(nrm[0], -nrm[1]) * 180.0 / M_PI;
    if (npcs[n].h < 0) npcs[n].h += 360;
    npcs[n].m = asin(nrm[2] > 1.0 ? 1.0 : (nrm[2] < -1.0 ? -1.0 : nrm[2])) * 180.0 / M_PI;
    g_npc_rt[n].avoid_skip = 0;
    g_npc_rt[n].avoid_hold = 0;

    comet_destroyed_by_ship(&hit);

    if (g_npc_rt[n].collision_grace > 0) return;
    g_npc_rt[n].collision_grace = TIMER_COLLISION_GRACE;

    double speed_ups = len * GAME_TICK_RATE;
    double dmg = DMG_COLLISION_BASE + DMG_COLLISION_PER_UPS * speed_ups;
    if (dmg > DMG_COLLISION_MAX) dmg = DMG_COLLISION_MAX;
    npcs[n].health -= (int)(dmg * MAX_TORPEDO_CAPACITY / 100.0 + 0.5);
    if (npcs[n].health <= 0 && npcs[n].death_timer <= 0) npcs[n].death_timer = GAME_TICK_RATE;

    int hq1 = get_q_from_g(hit.point[0]);
    int hq2 = get_q_from_g(hit.point[1]);
    int hq3 = get_q_from_g(hit.point[2]);
    #pragma omp critical
    broadcast_server_event(hq1, hq2, hq3, IPC_EV_IMPACT,
                           hit.point[0] - (hq1 - 1) * QUADRANT_SIZE,
                           hit.point[1] - (hq2 - 1) * QUADRANT_SIZE,
                           hit.point[2] - (hq3 - 1) * QUADRANT_SIZE, 0, 0, 0, 0);
}

void npc_collision_check(int n, double ox, double oy, double oz) {
    npc_collision_check_impl(n, ox, oy, oz);
}

void update_npc_ai(int n) {
    if (!npcs[n].active) {
        return;
    }
    unsigned int ai_seed = (unsigned int)n ^ (unsigned int)global_tick;
    const double prev_dir[3] = { npcs[n].dx, npcs[n].dy, npcs[n].dz };   /* heading of the previous AI step */

    if (npcs[n].gx <= 0.001 && npcs[n].gy <= 0.001) {
        npcs[n].gx = (npcs[n].q1 - 1) * QUADRANT_SIZE + npcs[n].x;
        npcs[n].gy = (npcs[n].q2 - 1) * QUADRANT_SIZE + npcs[n].y;
        npcs[n].gz = (npcs[n].q3 - 1) * QUADRANT_SIZE + npcs[n].z;
    }

    int q1 = npcs[n].q1;
    int q2 = npcs[n].q2;
    int q3 = npcs[n].q3;
    if (!IS_Q_VALID(q1, q2, q3)) {
        return;
    }
    QuadrantIndex *local_q = &spatial_index[q1][q2][q3];
    
    int closest_p = -1;
    double min_d2 = DIST_QUADRANT_DIAGONAL_SQ;
    for (int j = 0; j < local_q->player_count; j++) {
        ConnectedPlayer *p = local_q->players[j];
        if (p->state.is_cloaked) {
            continue;
        }
        if (p->faction == npcs[n].faction && p->renegade_timer <= 0) {
            continue;
        }
        double d2 = pow(npcs[n].x - p->state.s1, 2) + pow(npcs[n].y - p->state.s2, 2) + pow(npcs[n].z - p->state.s3, 2);
        if (d2 < min_d2) {
            min_d2 = d2;
            closest_p = (int)(p - players);
        }
    }
    
    if (npcs[n].energy < (COST_ACTION_VERY_HIGH - COST_ACTION_MED)) {
        npcs[n].ai_state = AI_STATE_FLEE;
    } else if (closest_p != -1) {
        if (npcs[n].ai_state == AI_STATE_PATROL || npcs[n].ai_state == AI_STATE_CHASE) {
            npcs[n].ai_state = AI_STATE_ATTACK_RUN;
            npcs[n].nav_timer = 0;
        }
    } else {
        npcs[n].ai_state = AI_STATE_PATROL;
    }

    if (npcs[n].faction == FACTION_XYLARI) {
        if (closest_p == -1) {
            npcs[n].is_cloaked = 1;
        } else if (npcs[n].ai_state == AI_STATE_FLEE) {
            npcs[n].is_cloaked = 1;
        } else {
            npcs[n].is_cloaked = 0;
        }
    } else {
        npcs[n].is_cloaked = 0;
    }

    double d_dx = 0;
    double d_dy = 0;
    double d_dz = 0;
    double speed = 0.03;
    double engine_ratio = (npcs[n].engine_health / (double)YIELD_HARVEST_MAX);
    if (npcs[n].engine_health < THRESHOLD_SYS_CRITICAL || npcs[n].health < (YIELD_MINE_MAX)) {
        speed = 0;
    } else {
        speed *= engine_ratio;
    }
    
    if (npcs[n].ai_state == AI_STATE_FLEE) speed *= 1.8;
    const double move_speed = speed;     /* what the engines can do, before an attack pose stops the ship */

    if (npcs[n].ai_state == AI_STATE_ATTACK_RUN && closest_p != -1) {
        if (npcs[n].nav_timer <= 0) {
            /* Pick a random direction and move 3 units in that direction */
            double rx = (rand_r(&ai_seed) % 200 - 100) / (double)YIELD_HARVEST_MAX;
            double ry = (rand_r(&ai_seed) % 200 - 100) / (double)YIELD_HARVEST_MAX;
            double rz = (rand_r(&ai_seed) % 200 - 100) / (double)YIELD_HARVEST_MAX;
            double rl = sqrt(rx * rx + ry * ry + rz * rz);
            if (rl < 0.001) { rx = 1.0; ry = 0; rz = 0; rl = 1.0; }
            
            npcs[n].tx = npcs[n].gx + (rx / rl) * DIST_NPC_PATROL_STEP;
            npcs[n].ty = npcs[n].gy + (ry / rl) * DIST_NPC_PATROL_STEP;
            npcs[n].tz = npcs[n].gz + (rz / rl) * DIST_NPC_PATROL_STEP;
            
            /* Keep within quadrant boundaries if possible, or just let it fly */
            npcs[n].nav_timer = (int)(2.5 * GAME_TICK_RATE); /* Reduced safety timeout */
        }

        ConnectedPlayer *target = &players[closest_p];
        double dx = target->gx - npcs[n].gx;
        double dy = target->gy - npcs[n].gy;
        double dz = target->gz - npcs[n].gz;
        double dist_to_player = sqrt(dx * dx + dy * dy + dz * dz);

        /* MOBILE FIRE: Attack while running if within range */
        if (dist_to_player < (DIST_MONSTER_ATTACK * 0.75)) {
            if (npcs[n].fire_cooldown <= 0) {
                if (npcs[n].beam_count < 4) {
                    npcs[n].beams[npcs[n].beam_count++] = (NetBeam){
                        npcs[n].x, npcs[n].y, npcs[n].z, 
                        target->state.s1, target->state.s2, target->state.s3, 
                        n + GALAXY_OBJECT_MIN_NPC, closest_p + 1, 1
                    };
                    /* Reduced mobile damage to balance higher frequency */
                    #pragma omp critical
                    {
                        apply_hull_damage(closest_p, (DMG_ION_BEAM_NPC / 2.5) / (double)MAX_TORPEDO_CAPACITY);
                    }
                }
                npcs[n].fire_cooldown = (20 + (rand_r(&ai_seed) % 40)); /* Very fast mobile firing */
            }
        }

        double t_dx = npcs[n].tx - npcs[n].gx;
        double t_dy = npcs[n].ty - npcs[n].gy;
        double t_dz = npcs[n].tz - npcs[n].gz;
        double t_dist = sqrt(t_dx * t_dx + t_dy * t_dy + t_dz * t_dz);
        if (t_dist > (double)DIST_BOUNDARY_MARGIN) {
            d_dx = t_dx / t_dist;
            d_dy = t_dy / t_dist;
            d_dz = t_dz / t_dist;
            
            /* Save persistent direction for fast physics ticks */
            npcs[n].dx = d_dx;
            npcs[n].dy = d_dy;
            npcs[n].dz = d_dz;

            npcs[n].h = atan2(d_dx, -d_dy) * 180.0 / M_PI;
            if(npcs[n].h < 0) {
                npcs[n].h += 360;
            }
            npcs[n].m = asin(d_dz) * 180.0 / M_PI;
        } else {
            npcs[n].ai_state = AI_STATE_ATTACK_POSITION;
            npcs[n].nav_timer = (int)(1.5 * GAME_TICK_RATE); /* Shorter stationary phase */
        }
    } else if (npcs[n].ai_state == AI_STATE_ATTACK_POSITION && closest_p != -1) {
        speed = 0.0;
        ConnectedPlayer *target = &players[closest_p];
        double dx = target->gx - npcs[n].gx;
        double dy = target->gy - npcs[n].gy;
        double dz = target->gz - npcs[n].gz;
        double dist_to_player = sqrt(dx * dx + dy * dy + dz * dz);
        if (dist_to_player > 0.01) {
            npcs[n].h = atan2(dx, -dy) * 180.0 / M_PI;
            if(npcs[n].h < 0) {
                npcs[n].h += 360;
            }
            npcs[n].m = asin(dz / dist_to_player) * 180.0 / M_PI;
        }
        
        if (npcs[n].fire_cooldown > 0) {
            npcs[n].fire_cooldown--;
        }

        if (npcs[n].fire_cooldown <= 0) {
            /* Alliance ships fire real homing torpedoes instead of instant beams */
            if (npcs[n].faction == FACTION_ALLIANCE) {
                int t_idx = -1;
                #pragma omp critical
                {
                    for (int i = 0; i < MAX_GLOBAL_TORPEDOES; i++) {
                        if (!players_torpedoes[i].active) { t_idx = i; players_torpedoes[i].active = true; break; }
                    }
                }
                if (t_idx != -1) {
                    PlayerTorpedo *pt = &players_torpedoes[t_idx];
                    pt->id = t_idx + 30000;
                    pt->owner_idx = n + GALAXY_OBJECT_MIN_NPC;
                    pt->faction = FACTION_ALLIANCE;
                    pt->active = true;
                    pt->timeout = TIMER_TORP_TIMEOUT;
                    pt->target_id = closest_p + 1;
                    pt->gx = npcs[n].gx; pt->gy = npcs[n].gy; pt->gz = npcs[n].gz;
                    
                    double tdx = target->gx - npcs[n].gx;
                    double tdy = target->gy - npcs[n].gy;
                    double tdz = target->gz - npcs[n].gz;
                    double tdist = sqrt(tdx*tdx + tdy*tdy + tdz*tdz);
                    if (tdist > 0.01) {
                        pt->dx = tdx / tdist; pt->dy = tdy / tdist; pt->dz = tdz / tdist;
                    } else {
                        pt->dx = 0; pt->dy = -1; pt->dz = 0;
                    }
                    pt->q1 = npcs[n].q1; pt->q2 = npcs[n].q2; pt->q3 = npcs[n].q3;
                    pt->x = npcs[n].x; pt->y = npcs[n].y; pt->z = npcs[n].z;
                    
                    #pragma omp critical
                    {
                        broadcast_server_event(pt->q1, pt->q2, pt->q3, IPC_EV_TORPEDO, pt->x, pt->y, pt->z, pt->dx * SPEED_TORPEDO, pt->dy * SPEED_TORPEDO, pt->dz * SPEED_TORPEDO, IPC_TORPEDO_ID_OFFSET + t_idx);
                    }
                }
            } else if (npcs[n].beam_count < 4) {
                npcs[n].beams[npcs[n].beam_count++] = (NetBeam){
                    npcs[n].x, npcs[n].y, npcs[n].z, 
                    target->state.s1, target->state.s2, target->state.s3, 
                    n + GALAXY_OBJECT_MIN_NPC, closest_p + 1, 1
                };
            }
            double base_dmg = (double)MAX_TORPEDO_CAPACITY;
            if (npcs[n].faction == FACTION_SWARM) {
                base_dmg = (double)DMG_TORPEDO_MONSTER / 12.5;
            } else if (npcs[n].faction == FACTION_KORTHIAN) {
                base_dmg = (double)DMG_TORPEDO_PLATFORM / 20.0;
            } else if (npcs[n].faction == FACTION_XYLARI) {
                base_dmg = (double)DMG_TORPEDO_PLATFORM / 14.28;
            }
            double dist_val = dist_to_player;
            if (dist_val < DIST_EPSILON) {
                dist_val = DIST_EPSILON;
            }
            double dist_factor = (DIST_BOARDING_MAX + 0.5) / dist_val;
            if (dist_factor > 1.0) {
                dist_factor = 1.0;
            }
            int dmg = (int)(base_dmg * dist_factor);
            
            int s_idx = calculate_shield_index(npcs[n].gx, npcs[n].gy, npcs[n].gz,
                                               target->gx, target->gy, target->gz,
                                               target->state.van_h, target->state.van_m, target->state.van_r);
            
            int dmg_rem = dmg;
            if (target->state.shields[s_idx] > 0) {
                if (target->state.shields[s_idx] >= dmg_rem) {
                    target->state.shields[s_idx] -= dmg_rem;
                    dmg_rem = 0;
                } else {
                    dmg_rem -= target->state.shields[s_idx];
                    target->state.shields[s_idx] = 0;
                }
            }
            if (dmg_rem > 0) {
                double hull_dmg = dmg_rem / (double)MAX_TORPEDO_CAPACITY;
                #pragma omp critical
                {
                    apply_hull_damage(closest_p, hull_dmg);
                }
                uint64_t e_loss = (uint64_t)(dmg_rem / 2);
                if (target->state.energy > e_loss) target->state.energy -= e_loss;
                else target->state.energy = 0;
            }
            target->shield_regen_delay = SHIELD_REGEN_DELAY;
            if (target->state.hull_integrity <= 0 || target->state.energy == 0) {
                target->state.energy = 0;
                target->state.hull_integrity = 0;
                target->state.crew_count = 0;
                target->death_timer = (GAME_TICK_RATE / 2);
                #pragma omp critical
                {
                    push_server_event(closest_p, IPC_EV_BOOM, target->state.s1, target->state.s2, target->state.s3, 0, 0, 0, 1);
                }
            }
            npcs[n].fire_cooldown = (40 + (rand_r(&ai_seed) % 40)); /* Faster stationary firing */
        }
        npcs[n].nav_timer--;
        if (npcs[n].nav_timer <= 0) {
            npcs[n].ai_state = AI_STATE_ATTACK_RUN;
            npcs[n].nav_timer = 0;
        }
    } else if (npcs[n].ai_state == AI_STATE_FLEE && closest_p != -1) {
        double dx = npcs[n].gx - players[closest_p].gx;
        double dy = npcs[n].gy - players[closest_p].gy;
        double dz = npcs[n].gz - players[closest_p].gz;
        double d = sqrt(dx * dx + dy * dy + dz * dz);
        if (d > DIST_EPSILON) {
            d_dx = dx / d;
            d_dy = dy / d;
            d_dz = dz / d;
            speed *= 1.8;
        }
        if (d > (double)DIST_FLEE_LIMIT) {
            npcs[n].ai_state = AI_STATE_PATROL;
        }
    } else {
        if (npcs[n].nav_timer-- <= 0) { 
            npcs[n].nav_timer = (int)(1.6 * GAME_TICK_RATE) + rand_r(&ai_seed) % (int)(3.3 * GAME_TICK_RATE); 
            double rx = (rand_r(&ai_seed) % 100 - 50) / (double)YIELD_HARVEST_MAX;
            double ry = (rand_r(&ai_seed) % 100 - 50) / (double)YIELD_HARVEST_MAX;
            double rz = (rand_r(&ai_seed) % 100 - 50) / (double)YIELD_HARVEST_MAX;
            double rl = sqrt(rx * rx + ry * ry + rz * rz);
            if (rl > 0.001) {
                npcs[n].dx = rx / rl;
                npcs[n].dy = ry / rl;
                npcs[n].dz = rz / rl;
            }
        }
        d_dx = npcs[n].dx;
        d_dy = npcs[n].dy;
        d_dz = npcs[n].dz;
    }
    if (speed <= 0.0 && move_speed > 0.0 && npcs[n].ai_state == AI_STATE_ATTACK_POSITION) {
        /* A stationary attacker that sits inside a solid object (spawned
         * there, or pushed in) does not wait for its pose to end: it flies out
         * and goes back to the attack run. */
        double esc[3];
        if (npc_embedded_escape(n, esc)) {
            d_dx = esc[0]; d_dy = esc[1]; d_dz = esc[2];
            speed = move_speed;
            npcs[n].dx = d_dx; npcs[n].dy = d_dy; npcs[n].dz = d_dz;
            npcs[n].ai_state = AI_STATE_ATTACK_RUN;
            npcs[n].nav_timer = 0;
            g_npc_rt[n].avoid_skip = 0;
        }
    }
    const double step_from[3] = { npcs[n].gx, npcs[n].gy, npcs[n].gz };
    {
        /* Do not fly into solid objects or other ships: steer around them (obstacles.h) */
        double avoid_dir[3] = { d_dx, d_dy, d_dz };
        if (npc_avoid_obstacles(n, prev_dir, avoid_dir, &speed)) {
            d_dx = avoid_dir[0]; d_dy = avoid_dir[1]; d_dz = avoid_dir[2];
            /* keep it for the fast physics ticks and show the real heading */
            npcs[n].dx = d_dx; npcs[n].dy = d_dy; npcs[n].dz = d_dz;
            if (speed > 0.0) {
                double hz = d_dz > 1.0 ? 1.0 : (d_dz < -1.0 ? -1.0 : d_dz);
                npcs[n].h = atan2(d_dx, -d_dy) * 180.0 / M_PI;
                if (npcs[n].h < 0) {
                    npcs[n].h += 360;
                }
                npcs[n].m = asin(hz) * 180.0 / M_PI;
            }
        }
    }
    npcs[n].gx += d_dx * speed;
    npcs[n].gy += d_dy * speed;
    npcs[n].gz += d_dz * speed;
    
    npcs[n].vx = d_dx * speed;
    npcs[n].vy = d_dy * speed;
    npcs[n].vz = d_dz * speed;

    double gal_limit = GALAXY_SIZE * QUADRANT_SIZE - DIST_EPSILON;
    if (npcs[n].gx < DIST_EPSILON) {
        npcs[n].gx = DIST_EPSILON;
    }
    if (npcs[n].gx > gal_limit) {
        npcs[n].gx = gal_limit;
    }
    if (npcs[n].gy < DIST_EPSILON) {
        npcs[n].gy = DIST_EPSILON;
    }
    if (npcs[n].gy > gal_limit) {
        npcs[n].gy = gal_limit;
    }
    if (npcs[n].gz < DIST_EPSILON) {
        npcs[n].gz = DIST_EPSILON;
    }
    if (npcs[n].gz > gal_limit) {
        npcs[n].gz = gal_limit;
    }
    npcs[n].q1 = get_q_from_g(npcs[n].gx);
    npcs[n].q2 = get_q_from_g(npcs[n].gy);
    npcs[n].q3 = get_q_from_g(npcs[n].gz);
    npcs[n].x = npcs[n].gx - (npcs[n].q1 - 1) * QUADRANT_SIZE;
    npcs[n].y = npcs[n].gy - (npcs[n].q2 - 1) * QUADRANT_SIZE;
    npcs[n].z = npcs[n].gz - (npcs[n].q3 - 1) * QUADRANT_SIZE;

    /* the steering is not a guarantee: a step that still enters a solid object is a collision */
    npc_collision_check(n, step_from[0], step_from[1], step_from[2]);
}

/* --- Emergency Reentry (shared) ---------------------------------------- */

/* The four historical "EMERGENCY REENTRY" call sites, as parameter presets. */
const RescueParams RESCUE_PARAMS_STANDARD = {
    .sector_offset = QUADRANT_SIZE / 8.0,
    .full_torpedo_reload = false,
    .always_reset_crew = false,
    .force_shutdown = false,
    .reset_motion = true,
    .reactivate = false
};
const RescueParams RESCUE_PARAMS_CREW = {
    .sector_offset = QUADRANT_SIZE / 8.0,
    .full_torpedo_reload = false,
    .always_reset_crew = true,
    .force_shutdown = false,
    .reset_motion = true,
    .reactivate = false
};
const RescueParams RESCUE_PARAMS_TACTICAL = {
    .sector_offset = QUADRANT_SIZE / 8.0,
    .full_torpedo_reload = false,
    .always_reset_crew = true,
    .force_shutdown = true,
    .reset_motion = true,
    .reactivate = false
};
const RescueParams RESCUE_PARAMS_LOGIN = {
    .sector_offset = QUADRANT_SIZE / 2.0,
    .full_torpedo_reload = true,
    .always_reset_crew = false,
    .force_shutdown = false,
    .reset_motion = false,
    .reactivate = true
};

/*
 * EMERGENCY REENTRY PROTOCOL: relocate the player's hull to a random safe
 * quadrant and restore escape-vessel state.
 *
 * When seed is non-NULL, rand_r(seed) is used (deterministic tick seed, as in
 * the game loop); otherwise rand() is used (command / sync threads).
 */
