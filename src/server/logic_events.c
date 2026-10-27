/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * SPACE GL - SERVER LOGIC EVENT & DAMAGE ENGINE
 *
 * Translation Unit: logic_events.c
 * Role: Event generation, damage calculation, torpedo simulation,
 * proximity hazard resolution, starbase defense fire, and player rescue.
 * Peer modules: logic.c, commands.c, targets.c.
 */

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <omp.h>
#include "game_config.h"
#include "server_internal.h"
#include "obstacles.h"
#include "shared_state.h"
#include "nav_math.h"
#include "ui.h"

void push_server_event(int p_idx, int type, double x1, double y1, double z1, double x2, double y2, double z2, int extra) {
    if (p_idx < 0 || p_idx >= MAX_CLIENTS) return;
    ConnectedPlayer *p = &players[p_idx];
    if (!p->active) return;
    
    if (p->state.event_count < MAX_NET_EVENTS) {
        NetEvent *ev = &p->state.events[p->state.event_count++];
        ev->type = type;
        ev->x1 = x1; ev->y1 = y1; ev->z1 = z1;
        ev->x2 = x2; ev->y2 = y2; ev->z2 = z2;
        ev->extra = extra;
    }
}

void update_npc_local_coords(int n) {
    npcs[n].q1 = get_q_from_g(npcs[n].gx);
    npcs[n].q2 = get_q_from_g(npcs[n].gy);
    npcs[n].q3 = get_q_from_g(npcs[n].gz);
    npcs[n].x = npcs[n].gx - (npcs[n].q1 - 1) * QUADRANT_SIZE;
    npcs[n].y = npcs[n].gy - (npcs[n].q2 - 1) * QUADRANT_SIZE;
    npcs[n].z = npcs[n].gz - (npcs[n].q3 - 1) * QUADRANT_SIZE;
}

void broadcast_server_event(int q1, int q2, int q3, int type, double x1, double y1, double z1, double x2, double y2, double z2, int extra) {
    if (!IS_Q_VALID(q1, q2, q3)) return;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].active && players[i].state.q1 == q1 && players[i].state.q2 == q2 && players[i].state.q3 == q3) {
            push_server_event(i, type, x1, y1, z1, x2, y2, z2, extra);
        }
    }
}


void rescue_player(int i, const RescueParams *p, unsigned int *seed) {
    int rq1, rq2, rq3;
    do {
        rq1 = seed ? rand_r(seed) % GALAXY_SIZE + 1 : rand() % GALAXY_SIZE + 1;
        rq2 = seed ? rand_r(seed) % GALAXY_SIZE + 1 : rand() % GALAXY_SIZE + 1;
        rq3 = seed ? rand_r(seed) % GALAXY_SIZE + 1 : rand() % GALAXY_SIZE + 1;
    } while (supernova_event.supernova_timer > 0 &&
             rq1 == supernova_event.supernova_q1 &&
             rq2 == supernova_event.supernova_q2 &&
             rq3 == supernova_event.supernova_q3);

    players[i].state.q1 = rq1;
    players[i].state.q2 = rq2;
    players[i].state.q3 = rq3;
    players[i].state.s1 = p->sector_offset;
    players[i].state.s2 = p->sector_offset;
    players[i].state.s3 = p->sector_offset;
    players[i].state.energy = MAX_ENERGY_CAPACITY; /* Standard capacity */
    players[i].state.torpedoes = p->full_torpedo_reload ? MAX_TORPEDO_CAPACITY : (MAX_TORPEDO_CAPACITY / 10);
    if (p->always_reset_crew || players[i].state.crew_count <= 0) {
        players[i].state.crew_count = (MAX_CREW_EXPLORER / 10);
    }
    players[i].state.hull_integrity = (float)THRESHOLD_SYS_STABLE + 5.0f;
    for (int s = 0; s < MAX_SYSTEMS; s++) {
        players[i].state.system_health[s] = (float)THRESHOLD_SYS_STABLE + 5.0f;
    }
    if (p->force_shutdown) {
        players[i].state.force_shutdown = 1;
    }
    players[i].gx = (rq1 - 1) * QUADRANT_SIZE + p->sector_offset;
    players[i].gy = (rq2 - 1) * QUADRANT_SIZE + p->sector_offset;
    players[i].gz = (rq3 - 1) * QUADRANT_SIZE + p->sector_offset;
    players[i].nav_state = NAV_STATE_IDLE;
    if (p->reset_motion) {
        players[i].hyper_speed = 0;
        players[i].dx = 0;
        players[i].dy = 0;
        players[i].dz = 0;
        players[i].is_docked = 0;
        players[i].torp_active = false;
        for (int s = 0; s < 4; s++) {
            players[i].torp_slots[s].active = false;
        }
    }
    if (p->reactivate) {
        players[i].active = 1;
    }
}



void logic_update_death_timers(unsigned int *ai_seed) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].socket && players[i].death_timer > 0) {
            players[i].death_timer--;
            if (players[i].death_timer <= 0) {
                #pragma omp critical
                {
                    /* TRANSFER HULL TO DERELICTS */
                    spawn_derelict(players[i].state.q1, players[i].state.q2, players[i].state.q3, players[i].state.s1, players[i].state.s2, players[i].state.s3, players[i].faction, players[i].ship_class, players[i].name);
                    broadcast_server_event(players[i].state.q1, players[i].state.q2, players[i].state.q3, IPC_EV_BOOM, players[i].state.s1, players[i].state.s2, players[i].state.s3, 0, 0, 0, 1);
                    
                    /* EMERGENCY REENTRY PROTOCOL: Instead of setting active=0, reset the ship to a safe state */
                    rescue_player(i, &RESCUE_PARAMS_STANDARD, ai_seed);

                    send_server_msg(i, "COMMAND", "EMERGENCY REENTRY: Your hull was critical. Transferred to escape vessel and relocated to safe sector.");
                }
            }
        }
    }
}

void logic_handle_wormhole_nav(int i) {
            players[i].nav_timer--;
            
            /* Sci-Fi Message Sequence (Adjusted for 900 tick / 15s sequence) */
            if (players[i].nav_timer == 850) 
                send_server_msg(i, "ENGINEERING", "Injecting exotic matter into local Schwarzschild metric...");
            else if (players[i].nav_timer == 700)
                send_server_msg(i, "SCIENCE", "Einstein-Rosen Bridge detected. Stabilizing singularity...");
            else if (players[i].nav_timer == 550)
                send_server_msg(i, "HELMSMAN", "Wormhole mouth stable. Entering event horizon.");

            /* Update Wormhole visual position in packet (Only before jump at half sequence) */
            if (players[i].nav_timer > (TIMER_WORMHOLE_SEQ / 2)) {
                /* Relativize to CURRENT quadrant so it renders correctly even if in adjacent Q */
                double rwx = (players[i].wx - (players[i].state.q1 - 1) * QUADRANT_SIZE);
                double rwy = (players[i].wy - (players[i].state.q2 - 1) * QUADRANT_SIZE);
                double rwz = (players[i].wz - (players[i].state.q3 - 1) * QUADRANT_SIZE);
                players[i].state.wormhole = (NetPoint){rwx, rwy, rwz, 1, players[i].jump_type};
                
                /* Move ship INTO the wormhole during the entry phase */
                players[i].gx += (players[i].wx - players[i].gx) * 0.05;
                players[i].gy += (players[i].wy - players[i].gy) * 0.05;
                players[i].gz += (players[i].wz - players[i].gz) * 0.05;
                players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * QUADRANT_SIZE);
                players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * QUADRANT_SIZE);
                players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * QUADRANT_SIZE);
            } else if (players[i].nav_timer < (TIMER_WORMHOLE_SEQ / 2) - 10) {
                /* Keep active for ~10 ticks after jump to avoid race with IPC_EV_JUMP delivery */
                players[i].state.wormhole.active = 0;
            }

            /* EXECUTE JUMP at mid-sequence (T=450 for 900 total) */
            if (players[i].nav_timer == (TIMER_WORMHOLE_SEQ / 2)) {
                double r_h = players[i].state.van_h * M_PI / 180.0;
                double r_m = players[i].state.van_m * M_PI / 180.0;
                double f_dx = sin(r_h) * cos(r_m);
                double f_dy = -cos(r_h) * cos(r_m);
                double f_dz = sin(r_m);

                /* Place ship at the wormhole mouth (4 units behind final target) */
                players[i].gx = players[i].target_gx - 4.0 * f_dx;
                players[i].gy = players[i].target_gy - 4.0 * f_dy;
                players[i].gz = players[i].target_gz - 4.0 * f_dz;
                
                players[i].dx = 0; players[i].dy = 0; players[i].dz = 0;
                players[i].hyper_speed = 0;
                
                players[i].state.q1 = get_q_from_g(players[i].gx);
                players[i].state.q2 = get_q_from_g(players[i].gy);
                players[i].state.q3 = get_q_from_g(players[i].gz);
                players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * QUADRANT_SIZE);
                players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * QUADRANT_SIZE);
                players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * QUADRANT_SIZE);
                
                /* Register the arrival wormhole at this starting position (Pass H/M as x2/y2 for orientation) */
                push_server_event(i, IPC_EV_JUMP, players[i].state.s1, players[i].state.s2, players[i].state.s3, players[i].state.van_h, players[i].state.van_m, 0, players[i].jump_type);
                players[i].state.wormhole.active = 0;
            }

            /* Move ship during emerging phase (450 -> 300) */
            if (players[i].nav_timer < (TIMER_WORMHOLE_SEQ / 2) && players[i].nav_timer > (TIMER_WORMHOLE_SEQ / 2 - 150)) {
                double r_h = players[i].state.van_h * M_PI / 180.0;
                double r_m = players[i].state.van_m * M_PI / 180.0;
                double f_dx = sin(r_h) * cos(r_m);
                double f_dy = -cos(r_h) * cos(r_m);
                double f_dz = sin(r_m);
                
                /* Travel 4 units in 150 ticks = 0.0266 per tick */
                players[i].gx += f_dx * (4.0 / 150.0);
                players[i].gy += f_dy * (4.0 / 150.0);
                players[i].gz += f_dz * (4.0 / 150.0);
                
                players[i].state.q1 = get_q_from_g(players[i].gx);
                players[i].state.q2 = get_q_from_g(players[i].gy);
                players[i].state.q3 = get_q_from_g(players[i].gz);
                players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * QUADRANT_SIZE);
                players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * QUADRANT_SIZE);
                players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * QUADRANT_SIZE);
            }

            if (players[i].nav_timer == (TIMER_WORMHOLE_SEQ / 2 - 150)) {
                /* Snap to final target destination precisely */
                players[i].gx = players[i].target_gx;
                players[i].gy = players[i].target_gy;
                players[i].gz = players[i].target_gz;
                players[i].state.q1 = get_q_from_g(players[i].gx);
                players[i].state.q2 = get_q_from_g(players[i].gy);
                players[i].state.q3 = get_q_from_g(players[i].gz);
                players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * QUADRANT_SIZE);
                players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * QUADRANT_SIZE);
                players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * QUADRANT_SIZE);
                send_server_msg(i, "HELMSMAN", "Wormhole stabilized in target sector. Maintaining hull integrity.");
            }

            if (players[i].nav_timer <= SHIELD_REGEN_DELAY) {
                players[i].nav_state = NAV_STATE_IDLE;
                players[i].state.wormhole.active = 0;
                send_server_msg(i, "HELMSMAN", "Wormhole traversal successful. Welcome to destination.");
            }
}

/* OpenMP scheduling note (applies to every schedule(dynamic) loop in this
 * file): since OpenMP 5.0 a plain schedule(dynamic[, n]) is NONMONOTONIC,
 * and LLVM's libomp maps it to its work-stealing algorithm
 * (kmp_sch_static_steal). Its stealing code asserts on consecutive loops
 * with very different trip counts (observed as "OMP: Error #13: Assertion
 * failure at kmp_dispatch.cpp(1617)" in the clang+ASan CI job, aborting the
 * server). The iterations here are independent, so the monotonic modifier
 * costs nothing and keeps libomp on the plain shared-counter dynamic
 * schedule (GCC's libgomp is unaffected). */
void logic_update_torpedoes(void) {
    #pragma omp parallel for schedule(monotonic: dynamic, 10)
    for (int t = 0; t < MAX_GLOBAL_TORPEDOES; t++) {
        if (!players_torpedoes[t].active) continue;

        PlayerTorpedo *pt = &players_torpedoes[t];
        
        /* 1. Homing Logic */
        if (pt->target_id > 0) {
            double target_gx = -1.0, target_gy = -1.0, target_gz = -1.0;
            int tid = pt->target_id; 
            
            ConnectedPlayer *target_p = player_by_id(tid);
            NPCShip *target_n = npc_by_id(tid);
            NPCBase *target_b = base_by_id(tid);
            NPCPlanet *target_pl = planet_by_id(tid);
            NPCStar *target_st = star_by_id(tid);
            if (target_p && target_p->active) { 
                target_gx = target_p->gx; target_gy = target_p->gy; target_gz = target_p->gz; 
            } else if (target_n && target_n->active) { 
                target_gx = target_n->gx; target_gy = target_n->gy; target_gz = target_n->gz; 
            } else if (target_b && target_b->active) { 
                target_gx = (target_b->q1-1)*QUADRANT_SIZE + target_b->x; 
                target_gy = (target_b->q2-1)*QUADRANT_SIZE + target_b->y; 
                target_gz = (target_b->q3-1)*QUADRANT_SIZE + target_b->z; 
            } else if (target_pl && target_pl->active) { 
                target_gx = (target_pl->q1-1)*QUADRANT_SIZE + target_pl->x; 
                target_gy = (target_pl->q2-1)*QUADRANT_SIZE + target_pl->y; 
                target_gz = (target_pl->q3-1)*QUADRANT_SIZE + target_pl->z; 
            } else if (target_st && target_st->active) { 
                target_gx = (target_st->q1-1)*QUADRANT_SIZE + target_st->x; 
                target_gy = (target_st->q2-1)*QUADRANT_SIZE + target_st->y; 
                target_gz = (target_st->q3-1)*QUADRANT_SIZE + target_st->z; 
            } else if (tid >= GALAXY_OBJECT_MIN_COMET && tid <= GALAXY_OBJECT_MAX_COMET && comets[tid-GALAXY_OBJECT_MIN_COMET].active) { 
                target_gx = (comets[tid-GALAXY_OBJECT_MIN_COMET].q1-1)*QUADRANT_SIZE + comets[tid-GALAXY_OBJECT_MIN_COMET].x; 
                target_gy = (comets[tid-GALAXY_OBJECT_MIN_COMET].q2-1)*QUADRANT_SIZE + comets[tid-GALAXY_OBJECT_MIN_COMET].y; 
                target_gz = (comets[tid-GALAXY_OBJECT_MIN_COMET].q3-1)*QUADRANT_SIZE + comets[tid-GALAXY_OBJECT_MIN_COMET].z; 
            } else if (tid >= GALAXY_OBJECT_MIN_PLATFORM && tid <= GALAXY_OBJECT_MAX_PLATFORM && platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].active) { 
                target_gx = (platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].q1-1)*QUADRANT_SIZE + platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].x; 
                target_gy = (platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].q2-1)*QUADRANT_SIZE + platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].y; 
                target_gz = (platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].q3-1)*QUADRANT_SIZE + platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].z; 
            } else if (tid >= GALAXY_OBJECT_MIN_MONSTER && tid <= GALAXY_OBJECT_MAX_MONSTER && monsters[tid-GALAXY_OBJECT_MIN_MONSTER].active) { 
                target_gx = (monsters[tid-GALAXY_OBJECT_MIN_MONSTER].q1-1)*QUADRANT_SIZE + monsters[tid-GALAXY_OBJECT_MIN_MONSTER].x; 
                target_gy = (monsters[tid-GALAXY_OBJECT_MIN_MONSTER].q2-1)*QUADRANT_SIZE + monsters[tid-GALAXY_OBJECT_MIN_MONSTER].y; 
                target_gz = (monsters[tid-GALAXY_OBJECT_MIN_MONSTER].q3-1)*QUADRANT_SIZE + monsters[tid-GALAXY_OBJECT_MIN_MONSTER].z; 
            }

            if (target_gx != -1.0) {
                double dx = target_gx - pt->gx; double dy = target_gy - pt->gy; double dz = target_gz - pt->gz;
                double d = sqrt(dx * dx + dy * dy + dz * dz);
                if (d > 0.01) {
                    double factor = 0.35; 
                    pt->dx = (pt->dx * (1.0 - factor)) + ((dx / d) * factor); 
                    pt->dy = (pt->dy * (1.0 - factor)) + ((dy / d) * factor); 
                    pt->dz = (pt->dz * (1.0 - factor)) + ((dz / d) * factor);
                    double speed = sqrt(pt->dx * pt->dx + pt->dy * pt->dy + pt->dz * pt->dz);
                    pt->dx /= speed; pt->dy /= speed; pt->dz /= speed;
                }
            }
        }
        
        pt->gx += pt->dx * SPEED_TORPEDO; pt->gy += pt->dy * SPEED_TORPEDO; pt->gz += pt->dz * SPEED_TORPEDO;
        
        /* Tactical Boundary Enforcement: Torpedoes MUST stay within their origin quadrant */
        int new_q1 = get_q_from_g(pt->gx);
        int new_q2 = get_q_from_g(pt->gy);
        int new_q3 = get_q_from_g(pt->gz);
        
        bool out_of_bounds = false;
        if (new_q1 != pt->q1 || new_q2 != pt->q2 || new_q3 != pt->q3) out_of_bounds = true;
        
        /* Also check local coordinate limits for safety */
        double lx = pt->gx - (pt->q1-1)*QUADRANT_SIZE;
        double ly = pt->gy - (pt->q2-1)*QUADRANT_SIZE;
        double lz = pt->gz - (pt->q3-1)*QUADRANT_SIZE;
        if (lx < 0.1 || lx > QUADRANT_SIZE - 0.1 || ly < 0.1 || ly > QUADRANT_SIZE - 0.1 || lz < 0.1 || lz > QUADRANT_SIZE - 0.1) out_of_bounds = true;

        if (out_of_bounds) {
            #pragma omp critical
            {
                /* Clamp for visual explosion on the wall */
                double bx = lx; if(bx<0) bx=0; if(bx>QUADRANT_SIZE) bx=QUADRANT_SIZE;
                double by = ly; if(by<0) by=0; if(by>QUADRANT_SIZE) by=QUADRANT_SIZE;
                double bz = lz; if(bz<0) bz=0; if(bz>QUADRANT_SIZE) bz=QUADRANT_SIZE;
                broadcast_server_event(pt->q1, pt->q2, pt->q3, IPC_EV_BOOM, bx, by, bz, 0, 0, 0, IPC_TORPEDO_ID_OFFSET + t);
            }
            pt->active = false;
            continue;
        }

        pt->x = lx; pt->y = ly; pt->z = lz;

        bool hit = false; 
        int hit_target = 0;

        if (IS_Q_VALID(pt->q1, pt->q2, pt->q3)) {
            QuadrantIndex *tq = &spatial_index[pt->q1][pt->q2][pt->q3];
            for (int n = 0; n < tq->npc_count; n++) {
                NPCShip *npc = tq->npcs[n];
                if (npc->active) {
                    double d = sqrt(pow(pt->gx - npc->gx, 2) + pow(pt->gy - npc->gy, 2) + pow(pt->gz - npc->gz, 2));
                    if (d < DIST_COLLISION_SHIP) { hit = true; hit_target = npc->id + GALAXY_OBJECT_MIN_NPC; break; }
                }
            }
            if (!hit) {
                for (int j = 0; j < tq->player_count; j++) {
                    ConnectedPlayer *other_p = tq->players[j];
                    if (other_p->active && (other_p - players) != pt->owner_idx) {
                        double d = sqrt(pow(pt->gx - other_p->gx, 2) + pow(pt->gy - other_p->gy, 2) + pow(pt->gz - other_p->gz, 2));
                        if (d < DIST_COLLISION_SHIP) { hit = true; hit_target = (int)(other_p - players) + GALAXY_OBJECT_MIN_PLAYER; break; }
                    }
                }
            }
        }

        if (hit) {
            #pragma omp critical
            {
                ConnectedPlayer *p = player_by_id(hit_target);
                NPCShip *target_npc = npc_by_id(hit_target);
                if (p) {
                    int s_idx = calculate_shield_index(pt->gx, pt->gy, pt->gz,
                                                       p->gx, p->gy, p->gz,
                                                       p->state.van_h, p->state.van_m, p->state.van_r);

                    int total_torpedo_damage = DMG_TORPEDO; // Use the full DMG_TORPEDO
                    int dmg_rem = total_torpedo_damage;

                    if (p->state.shields[s_idx] > 0) {
                        if (p->state.shields[s_idx] >= dmg_rem) {
                            p->state.shields[s_idx] -= dmg_rem;
                            dmg_rem = 0;
                        } else {
                            dmg_rem -= p->state.shields[s_idx];
                            p->state.shields[s_idx] = 0;
                        }
                    }
                    
                    if (dmg_rem > 0) {
                        // Apply remaining damage to hull and energy
                        double hull_dmg = (double)dmg_rem / (double)MAX_TORPEDO_CAPACITY; // Use DMG_TORPEDO for this scaling
                        p->state.hull_integrity -= hull_dmg;

                        uint64_t e_loss = (uint64_t)(dmg_rem / 2);
                        if (p->state.energy > e_loss) {
                            p->state.energy -= e_loss;
                        } else {
                            p->state.energy = 0;
                        }
                    }
                    
                    p->shield_regen_delay = (3 * GAME_TICK_RATE);
                    send_server_msg(hit_target - 1, "WARNING", "TORPEDO IMPACT DETECTED!");
                } else if (target_npc) {
                    target_npc->health -= (DMG_TORPEDO / YIELD_HARVEST_MAX);
                    if (target_npc->health <= 0) target_npc->death_timer = GAME_TICK_RATE;
                }
                broadcast_server_event(pt->q1, pt->q2, pt->q3, IPC_EV_BOOM, pt->x, pt->y, pt->z, 0, 0, 0, IPC_TORPEDO_ID_OFFSET + t); 
            }
            pt->active = false;
        }
        if (pt->timeout-- <= 0) pt->active = false;
        if (pt->gx < 0 || pt->gx > GALAXY_SIZE*QUADRANT_SIZE || pt->gy < 0 || pt->gy > GALAXY_SIZE*QUADRANT_SIZE || pt->gz < 0 || pt->gz > GALAXY_SIZE*QUADRANT_SIZE) pt->active = false;
    }

    /* 4. Homing Torpedo Trajectory Sync: Continuous broadcast for active homing projectiles */
    for (int t = 0; t < MAX_GLOBAL_TORPEDOES; t++) {
        if (players_torpedoes[t].active && players_torpedoes[t].target_id > 0) {
            broadcast_server_event(players_torpedoes[t].q1, players_torpedoes[t].q2, players_torpedoes[t].q3, 
                                   IPC_EV_TORPEDO, players_torpedoes[t].x, players_torpedoes[t].y, players_torpedoes[t].z, 
                                   players_torpedoes[t].dx * SPEED_TORPEDO, players_torpedoes[t].dy * SPEED_TORPEDO, players_torpedoes[t].dz * SPEED_TORPEDO, IPC_TORPEDO_ID_OFFSET + t);
        }
    }

    /* NetVisibleTorpedo list generation removed as IPC_EV_TORPEDO handles client sync */
    /* Late Sync: Torpedo ETA calculation AFTER physical updates */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!players[i].active) continue;
        bool has_active_torp = false;
        for (int t = 0; t < 4; t++) {
            int found_eta = 0;
            players[i].torp_slots[t].active = false;
            for (int gt = 0; gt < MAX_GLOBAL_TORPEDOES; gt++) {
                if (players_torpedoes[gt].active && players_torpedoes[gt].owner_idx == i && players_torpedoes[gt].origin_tube == t) {
                    players[i].torp_slots[t].active = true;
                    has_active_torp = true;
                    found_eta = players_torpedoes[gt].timeout;
                    int tid = players_torpedoes[gt].target_id;
                    if (tid > 0) {
                        double tgx = -1.0, tgy = -1.0, tgz = -1.0;
                        if (tid >= GALAXY_OBJECT_MIN_PLAYER && tid <= MAX_CLIENTS && players[tid-1].active) { 
                            tgx = players[tid-1].gx; tgy = players[tid-1].gy; tgz = players[tid-1].gz; 
                        } else if (tid >= GALAXY_OBJECT_MIN_NPC && tid <= GALAXY_OBJECT_MAX_NPC && npcs[tid-GALAXY_OBJECT_MIN_NPC].active) { 
                            tgx = npcs[tid-GALAXY_OBJECT_MIN_NPC].gx; tgy = npcs[tid-GALAXY_OBJECT_MIN_NPC].gy; tgz = npcs[tid-GALAXY_OBJECT_MIN_NPC].gz; 
                        } else if (tid >= GALAXY_OBJECT_MIN_COMET && tid <= GALAXY_OBJECT_MAX_COMET && comets[tid-GALAXY_OBJECT_MIN_COMET].active) { 
                            tgx = (comets[tid-GALAXY_OBJECT_MIN_COMET].q1-1)*QUADRANT_SIZE + comets[tid-GALAXY_OBJECT_MIN_COMET].x; 
                            tgy = (comets[tid-GALAXY_OBJECT_MIN_COMET].q2-1)*QUADRANT_SIZE + comets[tid-GALAXY_OBJECT_MIN_COMET].y; 
                            tgz = (comets[tid-GALAXY_OBJECT_MIN_COMET].q3-1)*QUADRANT_SIZE + comets[tid-GALAXY_OBJECT_MIN_COMET].z; 
                        } else if (tid >= GALAXY_OBJECT_MIN_PLATFORM && tid <= GALAXY_OBJECT_MAX_PLATFORM && platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].active) { 
                            tgx = (platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].q1-1)*QUADRANT_SIZE + platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].x; 
                            tgy = (platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].q2-1)*QUADRANT_SIZE + platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].y; 
                            tgz = (platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].q3-1)*QUADRANT_SIZE + platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].z; 
                        } else if (tid >= GALAXY_OBJECT_MIN_MONSTER && tid <= GALAXY_OBJECT_MAX_MONSTER && monsters[tid-GALAXY_OBJECT_MIN_MONSTER].active) { 
                            tgx = (monsters[tid-GALAXY_OBJECT_MIN_MONSTER].q1-1)*QUADRANT_SIZE + monsters[tid-GALAXY_OBJECT_MIN_MONSTER].x; 
                            tgy = (monsters[tid-GALAXY_OBJECT_MIN_MONSTER].q2-1)*QUADRANT_SIZE + monsters[tid-GALAXY_OBJECT_MIN_MONSTER].y; 
                            tgz = (monsters[tid-GALAXY_OBJECT_MIN_MONSTER].q3-1)*QUADRANT_SIZE + monsters[tid-GALAXY_OBJECT_MIN_MONSTER].z; 
                        } else if (tid >= GALAXY_OBJECT_MIN_STARBASE && tid <= GALAXY_OBJECT_MAX_STARBASE && bases[tid-GALAXY_OBJECT_MIN_STARBASE].active) {
                            tgx = (bases[tid-GALAXY_OBJECT_MIN_STARBASE].q1-1)*QUADRANT_SIZE + bases[tid-GALAXY_OBJECT_MIN_STARBASE].x; 
                            tgy = (bases[tid-GALAXY_OBJECT_MIN_STARBASE].q2-1)*QUADRANT_SIZE + bases[tid-GALAXY_OBJECT_MIN_STARBASE].y; 
                            tgz = (bases[tid-GALAXY_OBJECT_MIN_STARBASE].q3-1)*QUADRANT_SIZE + bases[tid-GALAXY_OBJECT_MIN_STARBASE].z;
                        }

                        if (tgx != -1.0) {
                            double dx = tgx - players_torpedoes[gt].gx;
                            double dy = tgy - players_torpedoes[gt].gy;
                            double dz = tgz - players_torpedoes[gt].gz;
                            double dist = sqrt(dx*dx + dy*dy + dz*dz);
                            if (SPEED_TORPEDO > 0.001) found_eta = (int)(dist / SPEED_TORPEDO);
                        }
                    }
                    break;
                }
            }
            players[i].tube_torpedo_etas[t] = found_eta;
        }
        players[i].torp_active = has_active_torp;

        /* Perfect HUD Sync: Update tube_state using the verified slot status */
        int ct = players[i].current_tube;
        if (players[i].state.system_health[5] <= THRESHOLD_SYS_DEGRADED) {
            players[i].state.tube_state = 3; /* OFFLINE */
        } else if (players[i].tube_load_timers[ct] > 180) {
            players[i].state.tube_state = 1; /* FIRING (Initial 0.5s) */
        } else if (players[i].tube_load_timers[ct] > 0) {
            players[i].state.tube_state = 2; /* LOADING (Next 3.0s) */
        } else if (players[i].torp_slots[ct].active) {
            players[i].state.tube_state = 1; /* BUSY (In Flight) */
        } else {
            players[i].state.tube_state = 0; /* READY */
        }
    }

    rebuild_spatial_index();

    if (global_tick % (10 * GAME_TICK_RATE) == 0) {
        #pragma omp parallel for collapse(3)
        for(int i=1; i<=GALAXY_SIZE; i++) {
            for(int j=1; j<=GALAXY_SIZE; j++) {
                for(int l=1; l<=GALAXY_SIZE; l++) {
                    long long val = spacegl_master.g[i][j][l];
                    if (val > 0) {
                        if ((val / 10000000LL) % 10) {
                            spacegl_master.g[i][j][l] -= 10000000LL;
                        }
                    }
                }
            }
        }
    }

#pragma omp parallel for schedule(monotonic: dynamic, 10)
    for (int n = 0; n < MAX_NPC; n++) {
        if (npcs[n].active) {
            if (npcs[n].death_timer > 0) {
                npcs[n].death_timer--;
                if (npcs[n].death_timer <= 0) {
                    npcs[n].active = 0;
                    /* Update local coordinates from global position before spawning wreck to ensure precise positioning */
                    update_npc_local_coords(n);
                    spawn_derelict(npcs[n].q1, npcs[n].q2, npcs[n].q3, npcs[n].x, npcs[n].y, npcs[n].z, npcs[n].faction, npcs[n].ship_class, npcs[n].name);
                    #pragma omp critical
                    broadcast_server_event(npcs[n].q1, npcs[n].q2, npcs[n].q3, IPC_EV_BOOM, npcs[n].x, npcs[n].y, npcs[n].z, 0, 0, 0, 1);
                }
            }
            /* Optimization: Only update NPC AI every 3 ticks to save CPU at 60Hz, 
               but still update physics (movement) every tick inside update_npc_ai. */
            if (n % 3 == global_tick % 3) {
                update_npc_ai(n);
            } else {
                /* Fast physics update: bypass complex AI scans, just move along DX/DY/DZ */
                double speed = 0.03 * (npcs[n].engine_health / (double)YIELD_HARVEST_MAX);
                if (npcs[n].ai_state == AI_STATE_FLEE) speed *= 1.8;
                if (npcs[n].engine_health < THRESHOLD_SYS_CRITICAL || npcs[n].health < YIELD_MINE_MAX) speed = 0;
                
                if (npcs[n].ai_state != AI_STATE_ATTACK_POSITION) {
                    npcs[n].gx += npcs[n].dx * speed;
                    npcs[n].gy += npcs[n].dy * speed;
                    npcs[n].gz += npcs[n].dz * speed;
                    npcs[n].q1 = get_q_from_g(npcs[n].gx);
                    npcs[n].q2 = get_q_from_g(npcs[n].gy);
                    npcs[n].q3 = get_q_from_g(npcs[n].gz);
                    npcs[n].x = npcs[n].gx - (npcs[n].q1 - 1) * QUADRANT_SIZE;
                    npcs[n].y = npcs[n].gy - (npcs[n].q2 - 1) * QUADRANT_SIZE;
                    npcs[n].z = npcs[n].gz - (npcs[n].q3 - 1) * QUADRANT_SIZE;
                    npcs[n].vx = npcs[n].dx * speed;
                    npcs[n].vy = npcs[n].dy * speed;
                    npcs[n].vz = npcs[n].dz * speed;
                } else {
                    npcs[n].vx = 0; npcs[n].vy = 0; npcs[n].vz = 0;
                }
            }
        }
    }

    for (int c = 0; c < MAX_COMETS; c++) {
        if (comets[c].active) {
            /* position before this tick, for the collision test below */
            double cp0[3] = { (comets[c].q1 - 1) * QUADRANT_SIZE + comets[c].x,
                              (comets[c].q2 - 1) * QUADRANT_SIZE + comets[c].y,
                              (comets[c].q3 - 1) * QUADRANT_SIZE + comets[c].z };
            comets[c].angle += comets[c].speed;
            double r = comets[c].a * (1.0 - 0.5 * 0.5) / (1.0 + 0.5 * cos(comets[c].angle)); /* Use eccentricity 0.5 for ellipses */
            double gx = comets[c].cx + r * cos(comets[c].angle) * cos(comets[c].inc);
            double gy = comets[c].cy + r * sin(comets[c].angle) * cos(comets[c].inc);
            double gz = comets[c].cz + r * sin(comets[c].inc);
            
            double gal_limit = GALAXY_SIZE * QUADRANT_SIZE - DIST_EPSILON;
            if (gx < DIST_EPSILON) gx = DIST_EPSILON; 
            if (gx > gal_limit) gx = gal_limit;
            if (gy < DIST_EPSILON) gy = DIST_EPSILON; 
            if (gy > gal_limit) gy = gal_limit;
            if (gz < DIST_EPSILON) gz = DIST_EPSILON; 
            if (gz > gal_limit) gz = gal_limit;

            /* Impact: a comet that flies into a solid object is destroyed
             * with a dedicated explosion (IPC_EV_IMPACT) seen by the players
             * of that quadrant. Other comets and black holes are not solid. */
            double cp1[3] = { gx, gy, gz };
            ObsHit chit;
            if (obs_segment_hit(cp0, cp1, 0.0, OBS_MASK_NO_COMET, 0, &chit)) {
                int hq1 = get_q_from_g(chit.point[0]);
                int hq2 = get_q_from_g(chit.point[1]);
                int hq3 = get_q_from_g(chit.point[2]);
                comets[c].active = 0;
                #pragma omp critical
                {
                    broadcast_server_event(hq1, hq2, hq3, IPC_EV_IMPACT,
                                           chit.point[0] - (hq1 - 1) * QUADRANT_SIZE,
                                           chit.point[1] - (hq2 - 1) * QUADRANT_SIZE,
                                           chit.point[2] - (hq3 - 1) * QUADRANT_SIZE, 0, 0, 0, 0);
                    for (int pi_ = 0; pi_ < MAX_CLIENTS; pi_++) {
                        if (players[pi_].active && players[pi_].state.q1 == hq1 &&
                            players[pi_].state.q2 == hq2 && players[pi_].state.q3 == hq3) {
                            char cmsg[96];
                            snprintf(cmsg, sizeof(cmsg), "A comet was destroyed hitting a %s.",
                                     obs_kind_name(chit.kind));
                            send_server_msg(pi_, "SENSORS", cmsg);
                        }
                    }
                }
                continue;
            }

            comets[c].q1 = get_q_from_g(gx);
            comets[c].q2 = get_q_from_g(gy);
            comets[c].q3 = get_q_from_g(gz);
            comets[c].x = gx - (comets[c].q1 - 1) * QUADRANT_SIZE;
            comets[c].y = gy - (comets[c].q2 - 1) * QUADRANT_SIZE;
            comets[c].z = gz - (comets[c].q3 - 1) * QUADRANT_SIZE;
        }
    }

}

void logic_update_supernova(unsigned int *ai_seed) {
    if (supernova_event.supernova_timer > 0) {
        supernova_event.supernova_timer--;
        int q1 = supernova_event.supernova_q1;
        int q2 = supernova_event.supernova_q2;
        int q3 = supernova_event.supernova_q3;
        spacegl_master.g[q1][q2][q3] = -supernova_event.supernova_timer;
        int sec = supernova_event.supernova_timer / GAME_TICK_RATE;
        if (sec > 0 && (supernova_event.supernova_timer % (5 * GAME_TICK_RATE) == 0 || (sec <= 10 && supernova_event.supernova_timer % (GAME_TICK_RATE / 2) == 0))) {
            char msg[128];
            sprintf(msg, "!!! WARNING: SUPERNOVA IMMINENT IN Q-%d-%d-%d. T-MINUS %d SECONDS !!!", 
                    supernova_event.supernova_q1, supernova_event.supernova_q2, supernova_event.supernova_q3, sec);
            for(int i=0; i<MAX_CLIENTS; i++) {
                if(players[i].active) {
                    send_server_msg(i, "SCIENCE", msg);
                }
            }
        }
        if (supernova_event.supernova_timer == 0) {
            if (supernova_event.star_id >= 0 && supernova_event.star_id < MAX_STARS) {
                stars_data[supernova_event.star_id].active = 0;
            }
            for(int p=0; p<MAX_PLANETS; p++) {
                if(planets[p].active && planets[p].q1 == q1 && planets[p].q2 == q2 && planets[p].q3 == q3) {
                    planets[p].active = 0;
                }
            }
            for(int n=0; n<MAX_NPC; n++) {
                if(npcs[n].active && npcs[n].q1 == q1 && npcs[n].q2 == q2 && npcs[n].q3 == q3) {
                    npcs[n].active = 0;
                }
            }
            for(int b=0; b<MAX_BASES; b++) {
                if(bases[b].active && bases[b].q1 == q1 && bases[b].q2 == q2 && bases[b].q3 == q3) {
                    bases[b].active = 0;
                }
            }
            for(int i=0; i<MAX_CLIENTS; i++) {
                if(players[i].active && players[i].state.q1 == q1 && players[i].state.q2 == q2 && players[i].state.q3 == q3) {
                    send_server_msg(i, "CRITICAL", "SUPERNOVA IMPACT. VESSEL VAPORIZED.");
                    players[i].state.energy = 0;
                    players[i].state.crew_count = 0;
                    #pragma omp critical
                    broadcast_server_event(q1, q2, q3, IPC_EV_BOOM, players[i].state.s1, players[i].state.s2, players[i].state.s3, 0, 0, 0, 1);
                    players[i].active = 0;
                }
            }
            spacegl_master.g[q1][q2][q3] = 10000;
            for(int bh=0; bh<MAX_BH; bh++) {
                if(!black_holes[bh].active) {
                    black_holes[bh].id = bh;
                    black_holes[bh].q1 = q1; 
                    black_holes[bh].q2 = q2; 
                    black_holes[bh].q3 = q3;
                    black_holes[bh].x = supernova_event.x;
                    black_holes[bh].y = supernova_event.y;
                    black_holes[bh].z = supernova_event.z;
                    black_holes[bh].active = 1;
                    break;
                }
            }
            supernova_event.supernova_timer = 0; 
            rebuild_spatial_index();
            save_galaxy();
        }
    } else if (global_tick > (2 * GAME_TICK_RATE) && supernova_event.supernova_timer <= 0 && (rand_r(ai_seed) % 9000 < 1)) {
        int rq1 = rand_r(ai_seed) % GALAXY_SIZE + 1; 
        int rq2 = rand_r(ai_seed) % GALAXY_SIZE + 1; 
        int rq3 = rand_r(ai_seed) % GALAXY_SIZE + 1;
        QuadrantIndex *qi = &spatial_index[rq1][rq2][rq3];
        if (qi->star_count > 0) {
            supernova_event.supernova_q1 = rq1; 
            supernova_event.supernova_q2 = rq2; 
            supernova_event.supernova_q3 = rq3;
            supernova_event.supernova_timer = TIMER_SUPERNOVA;
            supernova_event.x = qi->stars[0]->x; 
            supernova_event.y = qi->stars[0]->y; 
            supernova_event.z = qi->stars[0]->z;
            supernova_event.star_id = qi->stars[0]->id;
        }
    }
}

void logic_update_monster_attacks(void) {
    #pragma omp parallel for schedule(monotonic: dynamic)
    for (int mo = 0; mo < MAX_MONSTERS; mo++) {
        if (!monsters[mo].active) {
            continue;
        }
        int q1 = monsters[mo].q1; 
        int q2 = monsters[mo].q2; 
        int q3 = monsters[mo].q3;
        QuadrantIndex *local_q = &spatial_index[q1][q2][q3];
        ConnectedPlayer *target = NULL; 
        double min_d = THRESHOLD_SYS_CRITICAL;
        for (int j = 0; j < local_q->player_count; j++) {
            ConnectedPlayer *p = local_q->players[j]; 
            if (p->state.is_cloaked) {
                continue;
            }
            double dx = p->state.s1 - monsters[mo].x; 
            double dy = p->state.s2 - monsters[mo].y; 
            double dz = p->state.s3 - monsters[mo].z;
            double d = sqrt(dx * dx + dy * dy + dz * dz);
            if (d < min_d) { 
                min_d = d; 
                target = p; 
            }
        }
        if (monsters[mo].type == 30 && target) {
            double dx = target->state.s1 - monsters[mo].x; 
            double dy = target->state.s2 - monsters[mo].y; 
            double dz = target->state.s3 - monsters[mo].z;
            double dist = (min_d > 0.001) ? min_d : 0.001;
            /* Doubled monster speed for more pressure */
            monsters[mo].x += (dx / dist) * DIST_EPSILON * 2.0; 
            monsters[mo].y += (dy / dist) * DIST_EPSILON * 2.0; 
            monsters[mo].z += (dz / dist) * DIST_EPSILON * 2.0;
            
            /* Increased attack frequency: TIMER_MONSTER_PULSE / 3 (every 40 ticks ~ 0.6s) */
            if (min_d < DIST_MONSTER_ATTACK && global_tick % (TIMER_MONSTER_PULSE / 3) == 0) {
                #pragma omp critical
                {
                    if (monsters[mo].beam_count < 4) {
                        monsters[mo].beams[monsters[mo].beam_count++] = (NetBeam){
                            monsters[mo].x, monsters[mo].y, monsters[mo].z, 
                            target->state.s1, target->state.s2, target->state.s3, 
                            mo + GALAXY_OBJECT_MIN_MONSTER, (int)(target - players) + 1, 1
                        };
                    }
                    if (target->state.energy > COST_ACTION_EXTREME) target->state.energy -= COST_ACTION_EXTREME;
                    else target->state.energy = 0; 
                    send_server_msg((int)(target - players), "SCIENCE", "CRYSTALLINE RESONANCE DETECTED!");
                }
            }
        }
    }

}

void logic_update_environmental_hazards(void) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!players[i].active) continue;
        int q1 = players[i].state.q1, q2 = players[i].state.q2, q3 = players[i].state.q3;
        QuadrantIndex *lq = &spatial_index[q1][q2][q3];

        /* 1. Dyson Fragments: Massive energy recharge if very close */
        for (int d = 0; d < lq->dyson_count; d++) {
            double dx = players[i].state.s1 - lq->dysons[d]->x;
            double dy = players[i].state.s2 - lq->dysons[d]->y;
            double dz = players[i].state.s3 - lq->dysons[d]->z;
            double dist = sqrt(dx*dx + dy*dy + dz*dz);
            if (dist < DIST_INTERACTION_MAX) {
                uint64_t recharge = 5000 * (1.0 + (DIST_INTERACTION_MAX - dist));
                if (players[i].state.energy < MAX_ENERGY_CAPACITY - recharge) players[i].state.energy += recharge;
                if (global_tick % 120 == 0) send_server_msg(i, "ENGINE", "DYSON RESONANCE DETECTED: ENERGY OVERFLOW!");
            }
        }

        /* 2. Subspace Ruptures: Severe hull damage and energy drain */
        for (int r = 0; r < lq->rupture_count; r++) {
            double dx = players[i].state.s1 - lq->ruptures[r]->x;
            double dy = players[i].state.s2 - lq->ruptures[r]->y;
            double dz = players[i].state.s3 - lq->ruptures[r]->z;
            double dist = sqrt(dx*dx + dy*dy + dz*dz);
            if (dist < DIST_NEBULA_EFFECT) {
                apply_hull_damage(i, 0.05);
                if (players[i].state.energy > 2000) players[i].state.energy -= 2000;
                if (global_tick % 60 == 0) send_server_msg(i, "CRITICAL", "SUBSPACE RUPTURE: STRUCTURAL INTEGRITY COMPROMISED!");
            }
        }

        /* 3. Ancient Relics: Shield boost and system repairs */
        for (int re = 0; re < lq->relic_count; re++) {
            double dx = players[i].state.s1 - lq->relics[re]->x;
            double dy = players[i].state.s2 - lq->relics[re]->y;
            double dz = players[i].state.s3 - lq->relics[re]->z;
            double dist = sqrt(dx*dx + dy*dy + dz*dz);
            if (dist < DIST_INTERACTION_MAX) {
                for (int s = 0; s < 6; s++) {
                    if (players[i].state.shields[s] < SHIELD_MAX_STRENGTH) players[i].state.shields[s] += 100;
                }
                for (int sys = 0; sys < MAX_SYSTEMS; sys++) {
                    if (players[i].state.system_health[sys] < YIELD_HARVEST_MAX) players[i].state.system_health[sys] += 0.1;
                }
                if (global_tick % 180 == 0) send_server_msg(i, "SCIENCE", "ANCIENT RELIC DETECTED: HARMONIC SYSTEM REPAIR IN PROGRESS.");
            }
        }

        /* 4. Ion Storms: Heavy shield drain and sensor interference */
        for (int st = 0; st < lq->storm_count; st++) {
            double dx = players[i].state.s1 - lq->storms[st]->x;
            double dy = players[i].state.s2 - lq->storms[st]->y;
            double dz = players[i].state.s3 - lq->storms[st]->z;
            double dist = sqrt(dx*dx + dy*dy + dz*dz);
            if (dist < DIST_PULSAR_BEAM) {
                for (int s = 0; s < 6; s++) {
                    if (players[i].state.shields[s] > 200) players[i].state.shields[s] -= 200;
                    else players[i].state.shields[s] = 0;
                }
                if (global_tick % 90 == 0) send_server_msg(i, "TACTICAL", "ION STORM IMPACT: SHIELD COLLAPSE IMMINENT!");
            }
        }
    }

}

void logic_update_starbase_defense(void) {
    /* === STARBASE FACTION DEFENSE: fire at players docking into hostile bases === */
    /* Rate: 3 shots/s at 60 Hz  =>  cooldown = 20 ticks */
#define BASE_FIRE_COOLDOWN_TICKS  20
#define BASE_FIRE_RANGE           3.5   /* same as DIST_DOCKING_MAX */
#define BASE_FIRE_DAMAGE          8.0   /* hull % per shot */
    unsigned int ai_seed = (unsigned int)global_tick;
    for (int b = 0; b < MAX_BASES; b++) {
        if (!bases[b].active) {
            continue;
        }
        if (bases[b].fire_cooldown > 0) {
            bases[b].fire_cooldown--;
        }
        for (int pi = 0; pi < MAX_CLIENTS; pi++) {
            if (!players[pi].active) {
                continue;
            }
            /* Only target players who are in docking sequence with THIS base */
            int docking_base_id = players[pi].pending_bor_target - GALAXY_OBJECT_MIN_STARBASE;
            if (players[pi].nav_state != NAV_STATE_DOCKING) {
                continue;
            }
            if (docking_base_id != b) {
                continue;
            }
            /* Skip same faction */
            if (bases[b].faction == players[pi].faction || bases[b].faction == FACTION_ALLIANCE) {
                continue;
            }
            /* Distance check: player must be within firing range */
            double dx = bases[b].x - players[pi].state.s1;
            double dy = bases[b].y - players[pi].state.s2;
            double dz = bases[b].z - players[pi].state.s3;
            double dist = sqrt(dx * dx + dy * dy + dz * dz);
            if (dist > BASE_FIRE_RANGE * 2.0) {
                continue;
            }
            if (bases[b].fire_cooldown <= 0) {
                /* Abort the docking sequence */
                players[pi].nav_state = NAV_STATE_IDLE;
                players[pi].pending_bor_target = 0;
                /* Visual beam from base to player */
                if (bases[b].beam_count < 4) {
                    bases[b].beams[bases[b].beam_count++] = (NetBeam){
                        bases[b].x, bases[b].y, bases[b].z,
                        players[pi].state.s1, players[pi].state.s2, players[pi].state.s3,
                        b + GALAXY_OBJECT_MIN_STARBASE, pi + 1, 1
                    };
                }
                /* Damage player shields and hull */
                int s_idx = rand_r(&ai_seed) % 6;
                if (players[pi].state.shields[s_idx] > 0) {
                    int shield_dmg = 200 + rand_r(&ai_seed) % 300;
                    if ((int)players[pi].state.shields[s_idx] > shield_dmg) {
                        players[pi].state.shields[s_idx] -= shield_dmg;
                    } else {
                        players[pi].state.shields[s_idx] = 0;
                        apply_hull_damage(pi, BASE_FIRE_DAMAGE * 0.5);
                    }
                } else {
                    apply_hull_damage(pi, BASE_FIRE_DAMAGE);
                }
                send_server_msg(pi, "STARBASE", "\033[31m*** UNAUTHORIZED DOCKING ATTEMPT! DEFENSIVE BATTERIES OPENED FIRE! ***\033[0m");
                bases[b].fire_cooldown = BASE_FIRE_COOLDOWN_TICKS;
            }
        }
    }

    rebuild_spatial_index();

    /* Reset transient effects for NPCs and other entities after all updates have been sent */
}

/* Shared implementation: reduces hull integrity and damages one random
 * subsystem (1-5%). 'stress' selects the report wording: false for impacts
 * (combat, hazards), true for structural stress (wormhole jump). */
static void apply_hull_damage_impl(int i, double amount, bool stress) {
    if (i < 0 || i >= MAX_CLIENTS || !players[i].active) return;
    
    players[i].state.hull_integrity -= amount;
    if (players[i].state.hull_integrity < 0) players[i].state.hull_integrity = 0;
    
    /* Random system damage (1-5% per hit) */
    /* The seed must change on every call: deriving it only from the player
     * index and a stack address made rand_r() return the same system and the
     * same damage each time. A shared atomic counter keeps it thread-safe
     * (this code can run inside OpenMP regions). */
    static unsigned int dmg_counter = 0;
    unsigned int n = __atomic_add_fetch(&dmg_counter, 1u, __ATOMIC_RELAXED);
    unsigned int ai_seed = (unsigned int)time(NULL) ^ ((unsigned int)i * 2654435761u) ^ (n * 40503u);
    ai_seed ^= ai_seed >> 16; ai_seed *= 0x45d9f3bu; ai_seed ^= ai_seed >> 16;
    int sys = rand_r(&ai_seed) % MAX_SYSTEMS;
    double sys_dmg = 1.0 + (rand_r(&ai_seed) % 400) / (double)YIELD_HARVEST_MAX;
    players[i].state.system_health[sys] -= sys_dmg;
    if (players[i].state.system_health[sys] < 0) players[i].state.system_health[sys] = 0;
    
    const char* sys_names[] = {"Hyperdrive", "Impulse", "Sensors", "Transporters", "Ion Beams", "Torpedoes", "Computer", "Life Support", "Shields", "Auxiliary"};
    char msg[128];
    if (stress)
        snprintf(msg, sizeof(msg), "Wormhole stress: hull strained %.1f%%, system %s damaged by %.1f%%.", amount, sys_names[sys], sys_dmg);
    else
        snprintf(msg, sizeof(msg), "Hull impact! System %s damaged by %.1f%%.", sys_names[sys], sys_dmg);
    send_server_msg(i, "DAMAGE", msg);
}

void apply_hull_damage(int i, double amount) {
    apply_hull_damage_impl(i, amount, false);
}

/* Structural stress from a wormhole jump: same effect as apply_hull_damage(),
 * but reported as jump stress instead of a hull impact. */
void apply_jump_stress_damage(int i, double amount) {
    apply_hull_damage_impl(i, amount, true);
}

int calculate_shield_index(double shooter_x, double shooter_y, double shooter_z,
                           double target_x, double target_y, double target_z,
                           double target_h, double target_m, double target_r) {
    double rel_dx = shooter_x - target_x;
    double rel_dy = shooter_y - target_y;
    double rel_dz = shooter_z - target_z;

    double h = target_h * M_PI / 180.0;
    double m = target_m * M_PI / 180.0;
    double r = target_r * M_PI / 180.0;

    /* Build Local Basis Matrix (Ship Orientation) */
    /* Basis F (Forward): Standard Spherical to Cartesian */
    double fx = cos(m) * sin(h);
    double fy = cos(m) * -cos(h);
    double fz = sin(m);

    /* Initial Right (R0) and Up (U0) without roll */
    double r0x = cos(h); double r0y = sin(h); double r0z = 0;
    double u0x = fy * r0z - fz * r0y;
    double u0y = fz * r0x - fx * r0z;
    double u0z = fx * r0y - fy * r0x;

    /* Apply Roll (r) around Forward (f) axis */
    double rx = r0x * cos(r) + u0x * sin(r);
    double ry = r0y * cos(r) + u0y * sin(r);
    double rz = r0z * cos(r) + u0z * sin(r);

    double ux = u0x * cos(r) - r0x * sin(r);
    double uy = u0y * cos(r) - r0y * sin(r);
    double uz = u0z * cos(r) - r0z * sin(r);

    /* Project hit vector into this local orthonormal basis */
    double v_f = rel_dx * fx + rel_dy * fy + rel_dz * fz;
    double v_r = rel_dx * rx + rel_dy * ry + rel_dz * rz;
    double v_u = rel_dx * ux + rel_dy * uy + rel_dz * uz;

    double dist = sqrt(rel_dx * rel_dx + rel_dy * rel_dy + rel_dz * rel_dz);
    if (dist < 1e-6) return 0;

    /* Sector Mapping (Standard: 0:F, 1:B, 2:T, 3:B, 4:L, 5:R) */
    double v_ang = asin(v_u / dist) * 180.0 / M_PI;
    if (v_ang > 45.0) return 2;  /* TOP */
    if (v_ang < -45.0) return 3; /* BOTTOM */

    double h_ang = atan2(v_r, v_f) * 180.0 / M_PI;
    if (h_ang > -45.0 && h_ang <= 45.0) return 0;   /* FRONT */
    if (h_ang > 45.0 && h_ang <= 135.0) return 5;   /* RIGHT (Index 5) */
    if (h_ang > 135.0 || h_ang <= -135.0) return 1;  /* REAR */
    return 4; /* LEFT (Index 4) */
}


void logic_generate_events(unsigned int *ai_seed) {
    logic_update_torpedoes();
    logic_update_supernova(ai_seed);
    logic_update_monster_attacks();
    logic_update_environmental_hazards();
    logic_update_starbase_defense();
}
