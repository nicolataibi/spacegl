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
#include "shared_state.h" /* For IPC_EV_ types */

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

void broadcast_server_event(int q1, int q2, int q3, int type, double x1, double y1, double z1, double x2, double y2, double z2, int extra) {
    if (!IS_Q_VALID(q1, q2, q3)) return;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].active && players[i].state.q1 == q1 && players[i].state.q2 == q2 && players[i].state.q3 == q3) {
            push_server_event(i, type, x1, y1, z1, x2, y2, z2, extra);
        }
    }
}

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

void update_npc_ai(int n) {
    if (!npcs[n].active) {
        return;
    }

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
    double min_d2 = QUADRANT_SIZE * QUADRANT_SIZE;
    for (int j = 0; j < local_q->player_count; j++) {
        ConnectedPlayer *p = local_q->players[j];
        if (p->state.is_cloaked) {
            continue;
        }
        if (p->faction == npcs[n].faction && p->renegade_timer <= 0) {
            continue;
        }
        double d2 = pow(npcs[n].gx - p->gx, 2) + pow(npcs[n].gy - p->gy, 2) + pow(npcs[n].gz - p->gz, 2);
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
    if (npcs[n].engine_health < THRESHOLD_SYS_CRITICAL || npcs[n].health < (YIELD_MINE_MAX)) {
        speed = 0;
    } else {
        speed *= (npcs[n].engine_health / (double)YIELD_HARVEST_MAX);
    }

    if (npcs[n].ai_state == AI_STATE_ATTACK_RUN && closest_p != -1) {
        if (npcs[n].nav_timer <= 0) {
            /* Pick a random direction and move 3 units in that direction */
            double rx = (rand() % 200 - 100) / (double)YIELD_HARVEST_MAX;
            double ry = (rand() % 200 - 100) / (double)YIELD_HARVEST_MAX;
            double rz = (rand() % 200 - 100) / (double)YIELD_HARVEST_MAX;
            double rl = sqrt(rx * rx + ry * ry + rz * rz);
            if (rl < 0.001) { rx = 1.0; ry = 0; rz = 0; rl = 1.0; }
            
            npcs[n].tx = npcs[n].gx + (rx / rl) * DIST_NPC_PATROL_STEP;
            npcs[n].ty = npcs[n].gy + (ry / rl) * DIST_NPC_PATROL_STEP;
            npcs[n].tz = npcs[n].gz + (rz / rl) * DIST_NPC_PATROL_STEP;
            
            /* Keep within quadrant boundaries if possible, or just let it fly */
            npcs[n].nav_timer = (int)(3.3 * GAME_TICK_RATE); /* Safety timeout for the run */
        }
        double dx = npcs[n].tx - npcs[n].gx;
        double dy = npcs[n].ty - npcs[n].gy;
        double dz = npcs[n].tz - npcs[n].gz;
        double dist = sqrt(dx * dx + dy * dy + dz * dz);
        if (dist > (double)DIST_BOUNDARY_MARGIN) {
            d_dx = dx / dist;
            d_dy = dy / dist;
            d_dz = dz / dist;
            npcs[n].h = atan2(d_dx, -d_dy) * 180.0 / M_PI;
            if(npcs[n].h < 0) {
                npcs[n].h += 360;
            }
            npcs[n].m = asin(d_dz) * 180.0 / M_PI;
        } else {
            npcs[n].ai_state = AI_STATE_ATTACK_POSITION;
            npcs[n].nav_timer = (2 * GAME_TICK_RATE); /* 2 seconds of firing */
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
            if (npcs[n].beam_count < 4) {
                npcs[n].beams[npcs[n].beam_count++] = (NetBeam){npcs[n].x, npcs[n].y, npcs[n].z, target->state.s1, target->state.s2, target->state.s3, 1};
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
                apply_hull_damage(closest_p, hull_dmg);
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
                push_server_event(closest_p, IPC_EV_BOOM, target->state.s1, target->state.s2, target->state.s3, 0, 0, 0, 1);
            }
            npcs[n].fire_cooldown = 2 * GAME_TICK_RATE;
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
            npcs[n].nav_timer = (int)(1.6 * GAME_TICK_RATE) + rand() % (int)(3.3 * GAME_TICK_RATE); 
            double rx = (rand() % 100 - 50) / (double)YIELD_HARVEST_MAX;
            double ry = (rand() % 100 - 50) / (double)YIELD_HARVEST_MAX;
            double rz = (rand() % 100 - 50) / (double)YIELD_HARVEST_MAX;
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
    npcs[n].gx += d_dx * speed;
    npcs[n].gy += d_dy * speed;
    npcs[n].gz += d_dz * speed;
    
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
}

void update_game_logic() {
    /* global_tick is already incremented in the calling thread loop */
    
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

    /* Autosave every 60 seconds */
    if (global_tick % (60 * GAME_TICK_RATE) == 0) {
        save_galaxy_async();
    }

    /* Refresh LRS Grid once per second */
    if (global_tick % GAME_TICK_RATE == 0) {
        extern void refresh_lrs_grid();
        refresh_lrs_grid();
    }

    #pragma omp parallel for schedule(dynamic, 10)
    for (int n = 0; n < MAX_NPC; n++) {
        if (npcs[n].active) {
            if (npcs[n].death_timer > 0) {
                npcs[n].death_timer--;
                if (npcs[n].death_timer <= 0) {
                    npcs[n].active = 0;
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
                }
            }
        }
    }

    for (int c = 0; c < MAX_COMETS; c++) {
        if (comets[c].active) {
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

            comets[c].q1 = get_q_from_g(gx);
            comets[c].q2 = get_q_from_g(gy);
            comets[c].q3 = get_q_from_g(gz);
            comets[c].x = gx - (comets[c].q1 - 1) * QUADRANT_SIZE;
            comets[c].y = gy - (comets[c].q2 - 1) * QUADRANT_SIZE;
            comets[c].z = gz - (comets[c].q3 - 1) * QUADRANT_SIZE;
        }
    }

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
    } else if (global_tick > (2 * GAME_TICK_RATE) && supernova_event.supernova_timer <= 0 && (rand() % 9000 < 1)) {
        int rq1 = rand() % GALAXY_SIZE + 1; 
        int rq2 = rand() % GALAXY_SIZE + 1; 
        int rq3 = rand() % GALAXY_SIZE + 1;
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

    #pragma omp parallel for schedule(dynamic)
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
            monsters[mo].x += (dx / dist) * DIST_EPSILON; 
            monsters[mo].y += (dy / dist) * DIST_EPSILON; 
            monsters[mo].z += (dz / dist) * DIST_EPSILON;
            if (min_d < DIST_MONSTER_ATTACK && global_tick % TIMER_MONSTER_PULSE == 0) {
                #pragma omp critical
                {
                    if (monsters[mo].beam_count < 4) {
                        monsters[mo].beams[monsters[mo].beam_count++] = (NetBeam){monsters[mo].x, monsters[mo].y, monsters[mo].z, target->state.s1, target->state.s2, target->state.s3, 1};
                    }
                    if (target->state.energy > COST_ACTION_EXTREME) target->state.energy -= COST_ACTION_EXTREME;
                    else target->state.energy = 0; 
                    send_server_msg((int)(target - players), "SCIENCE", "CRYSTALLINE RESONANCE DETECTED!");
                }
            }
        }
    }

    /* Comet Tail Resource Collection */
    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!players[i].active) continue;
        int pq1=players[i].state.q1, pq2=players[i].state.q2, pq3=players[i].state.q3;
        QuadrantIndex *lq = &spatial_index[pq1][pq2][pq3];
        for(int c=0; c<lq->comet_count; c++) {
            NPCComet *co = lq->comets[c];
            if (co->active) {
                double dx = co->x - players[i].state.s1;
                double dy = co->y - players[i].state.s2;
                double dz = co->z - players[i].state.s3;
                double dist = sqrt(dx*dx+dy*dy+dz*dz);
                if (dist < 0.6) {
                    if (global_tick % (GAME_TICK_RATE / 2) == 0) { /* Once per second */
                        #pragma omp atomic
                        players[i].state.inventory[6] += 5;
                        #pragma omp critical
                        send_server_msg(i, "SCIENCE", "Nebular Gas collected from comet tail (+5).");
                    }
                }
            }
        }
    }

    #pragma omp parallel for schedule(dynamic)
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
                    int rq1, rq2, rq3;
                    do {
                        rq1 = rand() % GALAXY_SIZE + 1;
                        rq2 = rand() % GALAXY_SIZE + 1;
                        rq3 = rand() % GALAXY_SIZE + 1;
                    } while (supernova_event.supernova_timer > 0 && 
                            rq1 == supernova_event.supernova_q1 && 
                            rq2 == supernova_event.supernova_q2 && 
                            rq3 == supernova_event.supernova_q3);

                    players[i].state.q1 = rq1;
                    players[i].state.q2 = rq2;
                    players[i].state.q3 = rq3;
                    players[i].state.s1 = (QUADRANT_SIZE / 8.0);
                    players[i].state.s2 = (QUADRANT_SIZE / 8.0);
                    players[i].state.s3 = (QUADRANT_SIZE / 8.0);
                    players[i].state.energy = MAX_ENERGY_CAPACITY; /* Standard capacity */
                    players[i].state.torpedoes = (MAX_TORPEDO_CAPACITY / 10);   /* Emergency reload */
                    if (players[i].state.crew_count <= 0) {
                        players[i].state.crew_count = (MAX_CREW_EXPLORER / 10);
                    }
                    players[i].state.hull_integrity = (float)THRESHOLD_SYS_STABLE + 5.0f;
                    for (int s = 0; s < MAX_SYSTEMS; s++) {
                        players[i].state.system_health[s] = (float)THRESHOLD_SYS_STABLE + 5.0f;
                    }
                    players[i].gx = (players[i].state.q1 - 1) * QUADRANT_SIZE + (QUADRANT_SIZE / 8.0);
                    players[i].gy = (players[i].state.q2 - 1) * QUADRANT_SIZE + (QUADRANT_SIZE / 8.0);
                    players[i].gz = (players[i].state.q3 - 1) * QUADRANT_SIZE + (QUADRANT_SIZE / 8.0);
                    players[i].nav_state = NAV_STATE_IDLE;
                    players[i].hyper_speed = 0;
                    players[i].dx = 0;
                    players[i].dy = 0;
                    players[i].dz = 0;
                    players[i].is_docked = 0;
                    players[i].torp_active = false;
                    for(int s=0; s<4; s++) {
                        players[i].torp_slots[s].active = false;
                        players[i].state.torps[s].active = 0;
                    }
                    
                    send_server_msg(i, "COMMAND", "EMERGENCY REENTRY: Your hull was critical. Transferred to escape vessel and relocated to safe sector.");
                }
            }
        }
    }

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!players[i].active) {
            continue;
        }
        
        /* DEAD SHIP LOGIC: Hull at or below zero */
        if (players[i].state.hull_integrity <= 0.0) {
            players[i].state.hull_integrity = 0.0;
            players[i].nav_state = NAV_STATE_IDLE;
            players[i].hyper_speed = 0;
            players[i].dx = 0;
            players[i].dy = 0;
            players[i].dz = 0;
            
            /* Shields collapse completely */
            for (int s = 0; s < 6; s++) {
                players[i].state.shields[s] = 0;
            }
            
            /* Trigger death sequence if not already counting down */
            if (players[i].death_timer <= 0) {
                #pragma omp critical
                {
                    /* Server-side logging of ship destruction */
                    time_t now_death = time(NULL);
                    struct tm *t_death = localtime(&now_death);
                    char time_death[64];
                    strftime(time_death, sizeof(time_death), "%Y-%m-%d %H:%M:%S", t_death);
                    printf("\033[1;31m[DESTRUCTION]\033[0m Captain \033[1;37m%-15s\033[0m ship has been destroyed. [\033[1;33m%s\033[0m]\n", 
                           players[i].name, time_death);

                    players[i].death_timer = SHIELD_REGEN_DELAY; /* 2.5 seconds until derelict spawn */
                    send_server_msg(i, "CRITICAL", "FALLIMENTO CRITICO: Integrità scafo esaurita. Segnale di emergenza attivato. Scialuppe espulse. NAVE ALLA DERIVA.");
                    send_server_msg(i, "CRITICAL", "EMERGENCY ESCAPE INITIATED. SHIP IS DEAD IN SPACE.");
                }
            }
            
            /* Periodic alert during countdown */
            if (global_tick % GAME_TICK_RATE == 0) {
                #pragma omp critical
                send_server_msg(i, "WARNING", "EVACUATE IMMEDIATELY: Hull collapse imminent.");
            }
            
            continue; /* Skip normal updates for dead ship */
        }

        if (players[i].state.crew_count <= 0) {
            #pragma omp critical
            {
                /* TRANSFER HULL TO DERELICTS */
                spawn_derelict(players[i].state.q1, players[i].state.q2, players[i].state.q3, players[i].state.s1, players[i].state.s2, players[i].state.s3, players[i].faction, players[i].ship_class, players[i].name);
                broadcast_server_event(players[i].state.q1, players[i].state.q2, players[i].state.q3, IPC_EV_BOOM, players[i].state.s1, players[i].state.s2, players[i].state.s3, 0, 0, 0, 1);

                /* EMERGENCY REENTRY PROTOCOL */
                int rq1, rq2, rq3;
                do {
                    rq1 = rand() % GALAXY_SIZE + 1;
                    rq2 = rand() % GALAXY_SIZE + 1;
                    rq3 = rand() % GALAXY_SIZE + 1;
                } while (supernova_event.supernova_timer > 0 && 
                        rq1 == supernova_event.supernova_q1 && 
                        rq2 == supernova_event.supernova_q2 && 
                        rq3 == supernova_event.supernova_q3);

                players[i].state.q1 = rq1;
                players[i].state.q2 = rq2;
                players[i].state.q3 = rq3;
                players[i].state.s1 = (QUADRANT_SIZE / 8.0);
                players[i].state.s2 = (QUADRANT_SIZE / 8.0);
                players[i].state.s3 = (QUADRANT_SIZE / 8.0);
                players[i].state.energy = MAX_ENERGY_CAPACITY;
                players[i].state.torpedoes = (MAX_TORPEDO_CAPACITY / 10);
                players[i].state.crew_count = (MAX_CREW_EXPLORER / 10); /* Minimum crew for escape vessel */
                players[i].state.hull_integrity = (float)THRESHOLD_SYS_STABLE + 5.0f;
                for (int s = 0; s < MAX_SYSTEMS; s++) {
                    players[i].state.system_health[s] = (float)THRESHOLD_SYS_STABLE + 5.0f;
                }
                players[i].gx = (players[i].state.q1 - 1) * QUADRANT_SIZE + (QUADRANT_SIZE / 8.0);
                players[i].gy = (players[i].state.q2 - 1) * QUADRANT_SIZE + (QUADRANT_SIZE / 8.0);
                players[i].gz = (players[i].state.q3 - 1) * QUADRANT_SIZE + (QUADRANT_SIZE / 8.0);
                players[i].nav_state = NAV_STATE_IDLE;
                players[i].hyper_speed = 0;
                players[i].dx = 0;
                players[i].dy = 0;
                players[i].dz = 0;
                players[i].is_docked = 0;
                players[i].torp_active = false;
                for(int s=0; s<4; s++) {
                    players[i].torp_slots[s].active = false;
                    players[i].state.torps[s].active = 0;
                }

                send_server_msg(i, "COMMAND", "EMERGENCY REENTRY: Crew casualties critical. Escape vessel engaged and relocated to safe sector.");
            }
            continue;
        }

        /* --- BASE ENERGY DRAIN (Life Support & Systems) --- */
        int base_drain = (global_tick % (GAME_TICK_RATE / 30) == 0) ? 1 : 0; /* Effectively 0.5 per tick, GAME_TICK_RATE/2 per sec */
        if (players[i].state.red_alert) base_drain += (GAME_TICK_RATE / 60); /* Effectively 1.5 per tick, 1.5*GAME_TICK_RATE per sec */
        if (players[i].is_docked) base_drain = 0; 
        
                    if (players[i].state.energy >= (uint64_t)base_drain) {
                        #pragma omp atomic
                        players[i].state.energy -= (uint64_t)base_drain;
                        /* Gradually recover life support if energy is present (RATE_LS_REGEN per tick) */
                        if (players[i].state.life_support < (double)YIELD_HARVEST_MAX) players[i].state.life_support += RATE_LS_REGEN;
                    } else {
        
            players[i].state.energy = 0;
            /* Life support failure if no energy (RATE_LS_DRAIN per tick) */
            if (players[i].state.life_support > 0.0) {
                players[i].state.life_support -= (double)DIST_EPSILON;
                if (global_tick % TIMER_ALERT_LS == 0) {
                    #pragma omp critical
                    send_server_msg(i, "LIFE SUPPORT", "CRITICAL: Life support failing. Energy reserves depleted.");
                }
            }
        }

        /* Crew casualties if life support is zero */
        if (players[i].state.life_support <= 0.0) {
            players[i].state.life_support = 0.0;
            if (global_tick % GAME_TICK_RATE == 0 && players[i].state.crew_count > 0) {
                #pragma omp atomic
                players[i].state.crew_count--;
                if (global_tick % (10 * GAME_TICK_RATE) == 0) {
                    #pragma omp critical
                    send_server_msg(i, "MEDICAL", "Emergency: Life support offline. Casualties reported.");
                }
            }
        }

        // --- PROGRESSIVE SHIELD CHARGING/DRAINING ---
        if (players[i].state.shield_change_timer > 0) {
            bool all_shields_at_target = true;
            for (int s = 0; s < 6; s++) {
                if (players[i].state.shields[s] < players[i].state.target_shields[s]) {
                    // Charge shields
                    players[i].state.shields[s] += players[i].state.shield_change_rate;
                    if (players[i].state.shields[s] > players[i].state.target_shields[s]) {
                        players[i].state.shields[s] = players[i].state.target_shields[s];
                    }
                    all_shields_at_target = false;
                } else if (players[i].state.shields[s] > players[i].state.target_shields[s]) {
                    // Drain shields
                    players[i].state.shields[s] -= players[i].state.shield_change_rate;
                    if (players[i].state.shields[s] < players[i].state.target_shields[s]) {
                        players[i].state.shields[s] = players[i].state.target_shields[s];
                    }
                    all_shields_at_target = false;
                }
            }

            if (global_tick % GAME_TICK_RATE == 0) { // Send update message once per second during change
                #pragma omp critical
                send_server_msg(i, "ENGINEERING", "Shield reconfiguration in progress...");
            }

            players[i].state.shield_change_timer--;
            if (players[i].state.shield_change_timer <= 0 || all_shields_at_target) {
                players[i].state.shield_change_timer = 0;
                #pragma omp critical
                send_server_msg(i, "ENGINEERING", "Shield grids reconfigured and stabilized.");
            }
        } else {
            // --- NORMAL SHIELD REGENERATION (if no progressive change is active) ---
            if (players[i].state.energy > COST_ACTION_HIGH) {
                double integrity_mult = players[i].state.system_health[8] / (double)YIELD_HARVEST_MAX;
                double regen_rate = (RATIO_BASE_POWER + (players[i].state.power_dist[1] * RATIO_SHIELD_POWER)) * integrity_mult;
                bool needs_regen = false;
                for(int s=0; s<6; s++) {
                    if (players[i].state.shields[s] < SHIELD_MAX_STRENGTH) {
                        // Apply regeneration only if below max strength and not actively changing
                        if (players[i].state.shields[s] < players[i].state.target_shields[s] || players[i].state.target_shields[s] == 0) {
                             players[i].state.shields[s] += (int)regen_rate;
                             if (players[i].state.shields[s] > SHIELD_MAX_STRENGTH) {
                                 players[i].state.shields[s] = SHIELD_MAX_STRENGTH;
                             }
                             needs_regen = true;
                        }
                    }
                }
                if (needs_regen) {
                    #pragma omp atomic
                    players[i].state.energy -= (uint64_t)(regen_rate * 2.0);
                }
            }
        }

        if (players[i].state.ion_beam_charge < (double)YIELD_HARVEST_MAX) {
            double recharge_rate = RATIO_BASE_POWER + (players[i].state.power_dist[2] * RATIO_WEAPON_POWER);
            players[i].state.ion_beam_charge += recharge_rate;
            if (players[i].state.ion_beam_charge > (double)YIELD_HARVEST_MAX) {
                players[i].state.ion_beam_charge = (double)YIELD_HARVEST_MAX;
            }
            uint64_t r_cost = (uint64_t)(recharge_rate * 2.0);
            if (players[i].state.energy >= r_cost) players[i].state.energy -= r_cost;
            else players[i].state.energy = 0;
        }

        for(int t=0; t<4; t++) {
            if (players[i].tube_load_timers[t] > 0) {
                players[i].tube_load_timers[t]--;
            }
        }
        if (players[i].torp_load_timer > 0) {
            players[i].torp_load_timer--;
        }

        if (players[i].state.system_health[5] <= THRESHOLD_SYS_DEGRADED) {
            players[i].state.tube_state = 3;
        } else if (players[i].torp_active) {
            players[i].state.tube_state = 1;
        } else if (players[i].tube_load_timers[players[i].current_tube] > 0) {
            players[i].state.tube_state = 2;
        } else {
            players[i].state.tube_state = 0;
        }

        if (players[i].state.lock_target > 0) {
            int tid = players[i].state.lock_target;
            bool valid = false; 
            int pq1 = players[i].state.q1; 
            int pq2 = players[i].state.q2; 
            int pq3 = players[i].state.q3;
            
            /* Check if player ship has moved quadrant since last check */
            if (pq1 != players[i].last_q1 || pq2 != players[i].last_q2 || pq3 != players[i].last_q3) {
                /* Quadrant change: release lock automatically */
                valid = false;
            } else if (tid >= 1 && tid <= 32) { 
                if (players[tid - 1].active && 
                    players[tid - 1].state.q1 == pq1 && 
                    players[tid - 1].state.q2 == pq2 && 
                    players[tid - 1].state.q3 == pq3) {
                    valid = true; 
                }
            } else if (tid >= GALAXY_OBJECT_MIN_NPC && tid <= GALAXY_OBJECT_MAX_NPC) { 
                if (npcs[tid - GALAXY_OBJECT_MIN_NPC].active && 
                    npcs[tid - GALAXY_OBJECT_MIN_NPC].q1 == pq1 && 
                    npcs[tid - GALAXY_OBJECT_MIN_NPC].q2 == pq2 && 
                    npcs[tid - GALAXY_OBJECT_MIN_NPC].q3 == pq3) {
                    valid = true; 
                }
            } else if (tid >= GALAXY_OBJECT_MIN_STARBASE && tid <= GALAXY_OBJECT_MAX_STARBASE) { 
                if (bases[tid - GALAXY_OBJECT_MIN_STARBASE].active && 
                    bases[tid - GALAXY_OBJECT_MIN_STARBASE].q1 == pq1 && 
                    bases[tid - GALAXY_OBJECT_MIN_STARBASE].q2 == pq2 && 
                    bases[tid - GALAXY_OBJECT_MIN_STARBASE].q3 == pq3) {
                    valid = true; 
                }
            } else if (tid >= GALAXY_OBJECT_MIN_PLANET && tid <= GALAXY_OBJECT_MAX_PLANET) { 
                if (planets[tid - GALAXY_OBJECT_MIN_PLANET].active && 
                    planets[tid - GALAXY_OBJECT_MIN_PLANET].q1 == pq1 && 
                    planets[tid - GALAXY_OBJECT_MIN_PLANET].q2 == pq2 && 
                    planets[tid - GALAXY_OBJECT_MIN_PLANET].q3 == pq3) {
                    valid = true; 
                }
            } else if (tid >= GALAXY_OBJECT_MIN_STAR && tid <= GALAXY_OBJECT_MAX_STAR) {
                if (stars_data[tid - GALAXY_OBJECT_MIN_STAR].active && 
                    stars_data[tid - GALAXY_OBJECT_MIN_STAR].q1 == pq1 && 
                    stars_data[tid - GALAXY_OBJECT_MIN_STAR].q2 == pq2 && 
                    stars_data[tid - GALAXY_OBJECT_MIN_STAR].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= GALAXY_OBJECT_MIN_BLACKHOLE && tid <= GALAXY_OBJECT_MAX_BLACKHOLE) {
                if (black_holes[tid - GALAXY_OBJECT_MIN_BLACKHOLE].active && 
                    black_holes[tid - GALAXY_OBJECT_MIN_BLACKHOLE].q1 == pq1 && 
                    black_holes[tid - GALAXY_OBJECT_MIN_BLACKHOLE].q2 == pq2 && 
                    black_holes[tid - GALAXY_OBJECT_MIN_BLACKHOLE].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= GALAXY_OBJECT_MIN_NEBULA && tid <= GALAXY_OBJECT_MAX_NEBULA) {
                if (nebulas[tid - GALAXY_OBJECT_MIN_NEBULA].active && 
                    nebulas[tid - GALAXY_OBJECT_MIN_NEBULA].q1 == pq1 && 
                    nebulas[tid - GALAXY_OBJECT_MIN_NEBULA].q2 == pq2 && 
                    nebulas[tid - GALAXY_OBJECT_MIN_NEBULA].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= GALAXY_OBJECT_MIN_PULSAR && tid <= GALAXY_OBJECT_MAX_PULSAR) {
                if (pulsars[tid - GALAXY_OBJECT_MIN_PULSAR].active && 
                    pulsars[tid - GALAXY_OBJECT_MIN_PULSAR].q1 == pq1 && 
                    pulsars[tid - GALAXY_OBJECT_MIN_PULSAR].q2 == pq2 && 
                    pulsars[tid - GALAXY_OBJECT_MIN_PULSAR].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= GALAXY_OBJECT_MIN_COMET && tid <= GALAXY_OBJECT_MAX_COMET) {
                if (comets[tid - GALAXY_OBJECT_MIN_COMET].active && 
                    comets[tid - GALAXY_OBJECT_MIN_COMET].q1 == pq1 && 
                    comets[tid - GALAXY_OBJECT_MIN_COMET].q2 == pq2 && 
                    comets[tid - GALAXY_OBJECT_MIN_COMET].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= GALAXY_OBJECT_MIN_DERELICT && tid <= GALAXY_OBJECT_MAX_DERELICT) {
                if (derelicts[tid - GALAXY_OBJECT_MIN_DERELICT].active && 
                    derelicts[tid - GALAXY_OBJECT_MIN_DERELICT].q1 == pq1 && 
                    derelicts[tid - GALAXY_OBJECT_MIN_DERELICT].q2 == pq2 && 
                    derelicts[tid - GALAXY_OBJECT_MIN_DERELICT].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= GALAXY_OBJECT_MIN_ASTEROID && tid <= GALAXY_OBJECT_MAX_ASTEROID) {
                if (asteroids[tid - GALAXY_OBJECT_MIN_ASTEROID].active && 
                    asteroids[tid - GALAXY_OBJECT_MIN_ASTEROID].q1 == pq1 && 
                    asteroids[tid - GALAXY_OBJECT_MIN_ASTEROID].q2 == pq2 && 
                    asteroids[tid - GALAXY_OBJECT_MIN_ASTEROID].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= GALAXY_OBJECT_MIN_MINE && tid <= GALAXY_OBJECT_MAX_MINE) {
                if (mines[tid - GALAXY_OBJECT_MIN_MINE].active && 
                    mines[tid - GALAXY_OBJECT_MIN_MINE].q1 == pq1 && 
                    mines[tid - GALAXY_OBJECT_MIN_MINE].q2 == pq2 && 
                    mines[tid - GALAXY_OBJECT_MIN_MINE].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= GALAXY_OBJECT_MIN_BUOY && tid <= GALAXY_OBJECT_MAX_BUOY) {
                if (buoys[tid - GALAXY_OBJECT_MIN_BUOY].active && 
                    buoys[tid - GALAXY_OBJECT_MIN_BUOY].q1 == pq1 && 
                    buoys[tid - GALAXY_OBJECT_MIN_BUOY].q2 == pq2 && 
                    buoys[tid - GALAXY_OBJECT_MIN_BUOY].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= GALAXY_OBJECT_MIN_PLATFORM && tid <= GALAXY_OBJECT_MAX_PLATFORM) { 
                if (platforms[tid - GALAXY_OBJECT_MIN_PLATFORM].active && 
                    platforms[tid - GALAXY_OBJECT_MIN_PLATFORM].q1 == pq1 && 
                    platforms[tid - GALAXY_OBJECT_MIN_PLATFORM].q2 == pq2 && 
                    platforms[tid - GALAXY_OBJECT_MIN_PLATFORM].q3 == pq3) {
                    valid = true; 
                }
            } else if (tid >= GALAXY_OBJECT_MIN_RIFT && tid <= GALAXY_OBJECT_MAX_RIFT) {
                if (rifts[tid - GALAXY_OBJECT_MIN_RIFT].active && 
                    rifts[tid - GALAXY_OBJECT_MIN_RIFT].q1 == pq1 && 
                    rifts[tid - GALAXY_OBJECT_MIN_RIFT].q2 == pq2 && 
                    rifts[tid - GALAXY_OBJECT_MIN_RIFT].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= GALAXY_OBJECT_MIN_MONSTER && tid <= GALAXY_OBJECT_MAX_MONSTER) {
                if (monsters[tid - GALAXY_OBJECT_MIN_MONSTER].active && 
                    monsters[tid - GALAXY_OBJECT_MIN_MONSTER].q1 == pq1 && 
                    monsters[tid - GALAXY_OBJECT_MIN_MONSTER].q2 == pq2 && 
                    monsters[tid - GALAXY_OBJECT_MIN_MONSTER].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= GALAXY_OBJECT_MIN_PROBE && tid <= GALAXY_OBJECT_MAX_PROBE) {
                int p_idx = (tid - GALAXY_OBJECT_MIN_PROBE) / 3;
                int pr_idx = (tid - GALAXY_OBJECT_MIN_PROBE) % 3;
                if (p_idx < MAX_CLIENTS && players[p_idx].state.probes[pr_idx].active) {
                    /* Check if probe is in the same quadrant */
                    int pr_q1 = get_q_from_g(players[p_idx].state.probes[pr_idx].gx);
                    int pr_q2 = get_q_from_g(players[p_idx].state.probes[pr_idx].gy);
                    int pr_q3 = get_q_from_g(players[p_idx].state.probes[pr_idx].gz);
                    if (pr_q1 == pq1 && pr_q2 == pq2 && pr_q3 == pq3) {
                        valid = true;
                    }
                }
            }
            
            if (!valid) { 
                players[i].state.lock_target = 0; 
                send_server_msg(i, "TACTICAL", "Target lost. Lock released."); 
            }
        }

        for(int p=0; p<3; p++) {
            if (players[i].state.probes[p].active) {
                if (players[i].state.probes[p].status == 0) {
                    /* Probe en route */
                    players[i].state.probes[p].gx += players[i].state.probes[p].vx;
                    players[i].state.probes[p].gy += players[i].state.probes[p].vy;
                    players[i].state.probes[p].gz += players[i].state.probes[p].vz;
                    
                    int pr_q1 = get_q_from_g(players[i].state.probes[p].gx);
                    int pr_q2 = get_q_from_g(players[i].state.probes[p].gy);
                    int pr_q3 = get_q_from_g(players[i].state.probes[p].gz);
                    
                    /* Update current probe quadrant for 3D view filtering */
                    players[i].state.probes[p].q1 = pr_q1;
                    players[i].state.probes[p].q2 = pr_q2;
                    players[i].state.probes[p].q3 = pr_q3;

                    players[i].state.probes[p].s1 = (players[i].state.probes[p].gx - (pr_q1-1)*QUADRANT_SIZE);
                    players[i].state.probes[p].s2 = (players[i].state.probes[p].gy - (pr_q2-1)*QUADRANT_SIZE);
                    players[i].state.probes[p].s3 = (players[i].state.probes[p].gz - (pr_q3-1)*QUADRANT_SIZE);
                    
                    players[i].state.probes[p].eta -= (1.0 / (double)GAME_TICK_RATE);
                    if (players[i].state.probes[p].eta <= 0) {
                        players[i].state.probes[p].eta = 0;
                        players[i].state.probes[p].status = 1; /* Arrived */
                        players[i].state.probes[p].s1 = 5.0;
                        players[i].state.probes[p].s2 = 5.0;
                        players[i].state.probes[p].s3 = 5.0;
                        send_server_msg(i, "SCIENCE", "Sensor probe has reached target quadrant.");
                    }
                }
            }
        }

        if (players[i].nav_state == NAV_STATE_ALIGN || players[i].nav_state == NAV_STATE_ALIGN_IMPULSE) {
            players[i].nav_timer--;
            
            /* Safety: Keep absolute coordinates synced while turning */
            players[i].gx = (players[i].state.q1 - 1) * QUADRANT_SIZE + players[i].state.s1;
            players[i].gy = (players[i].state.q2 - 1) * QUADRANT_SIZE + players[i].state.s2;
            players[i].gz = (players[i].state.q3 - 1) * QUADRANT_SIZE + players[i].state.s3;

            double diff_h = players[i].target_h - players[i].start_h;
            while (diff_h > 180.0) {
                diff_h -= 360.0;
            }
            while (diff_h < -180.0) {
                diff_h += 360.0;
            }
            double init_t = (players[i].pending_bor_type > 0) ? players[i].pending_bor_type : 60.0;
            double t = 1.0 - players[i].nav_timer / init_t;
            players[i].state.van_h = (players[i].start_h + diff_h * t);
            players[i].state.van_m = (players[i].start_m + (players[i].target_m - players[i].start_m) * t);
            /* Reset ETA when idle */
            players[i].eta = 0;

            if (players[i].nav_timer <= 0) {
                if (players[i].nav_state == NAV_STATE_ALIGN) {
                    players[i].nav_state = (players[i].apr_target > 0) ? NAV_STATE_APPROACH : NAV_STATE_HYPERDRIVE;
                } else {
                    players[i].nav_state = NAV_STATE_IMPULSE;
                }
            }
        } else if (players[i].nav_state == NAV_STATE_HYPERDRIVE) {
            double hyper_h = players[i].state.system_health[0];
            if (hyper_h < 1.0) hyper_h = 1.0; /* Safety floor */

            /* Constant speed as requested: Factor 9.9 = Galaxy Diagonal in 10s */
            double step = players[i].hyper_speed;
            
            double dx_t = players[i].target_gx - players[i].gx;
            double dy_t = players[i].target_gy - players[i].gy;
            double dz_t = players[i].target_gz - players[i].gz;
            double dist_to_target = sqrt(dx_t * dx_t + dy_t * dy_t + dz_t * dz_t);
            
            /* ETA Calculation */
            double speed_per_sec = step * GAME_TICK_RATE;
            if (speed_per_sec > 0.01) {
                players[i].eta = dist_to_target / speed_per_sec;
            } else {
                players[i].eta = 0;
            }

            if (dist_to_target <= step) {
                players[i].gx = players[i].target_gx;
                players[i].gy = players[i].target_gy;
                players[i].gz = players[i].target_gz;
                players[i].nav_state = NAV_STATE_IDLE;
                players[i].hyper_speed = 0;
                players[i].eta = 0;
                send_server_msg(i, "HELMSMAN", "Target reached. Dropping out of Hyperdrive.");
            } else {
                players[i].gx += players[i].dx * step;
                players[i].gy += players[i].dy * step;
                players[i].gz += players[i].dz * step;
            }

            players[i].state.q1 = get_q_from_g(players[i].gx); 
            players[i].state.q2 = get_q_from_g(players[i].gy); 
            players[i].state.q3 = get_q_from_g(players[i].gz);
            players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * QUADRANT_SIZE); 
            players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * QUADRANT_SIZE); 
            players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * QUADRANT_SIZE);

            /* Galactic Boundary Enforcement: Stop and Invert on edge contact */
            bool oob = false;
            double gal_limit = GALAXY_SIZE * QUADRANT_SIZE - DIST_EPSILON;
            if (players[i].gx < DIST_EPSILON) { players[i].gx = DIST_EPSILON; oob = true; }
            if (players[i].gx > gal_limit) { players[i].gx = gal_limit; oob = true; }
            if (players[i].gy < DIST_EPSILON) { players[i].gy = DIST_EPSILON; oob = true; }
            if (players[i].gy > gal_limit) { players[i].gy = gal_limit; oob = true; }
            if (players[i].gz < DIST_EPSILON) { players[i].gz = DIST_EPSILON; oob = true; }
            if (players[i].gz > gal_limit) { players[i].gz = gal_limit; oob = true; }

            if (oob) {
                players[i].nav_state = NAV_STATE_IDLE;
                players[i].hyper_speed = 0;
                players[i].eta = 0;
                players[i].dx = 0;
                players[i].dy = 0;
                players[i].dz = 0;
                players[i].state.van_h = fmod(players[i].state.van_h + 180.0, 360.0);
                players[i].state.van_m = -players[i].state.van_m;
                send_server_msg(i, "COMPUTER", "GALACTIC LIMIT REACHED: Engines disengaged. Position inverted.");
                players[i].state.q1 = get_q_from_g(players[i].gx);
                players[i].state.q2 = get_q_from_g(players[i].gy);
                players[i].state.q3 = get_q_from_g(players[i].gz);
                players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * QUADRANT_SIZE);
                players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * QUADRANT_SIZE);
                players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * QUADRANT_SIZE);
            }
            
            /* Realistic Hyperdrive consumption: Linear with factor, Inverse with integrity */
            double current_f = players[i].hyper_speed * (double)GAME_TICK_RATE;
            int drain = (int)(((double)COST_ACTION_MED + (current_f * (double)COST_ACTION_MED)) * (100.0 / hyper_h));
            
            if (players[i].state.energy > (uint64_t)drain) {
                players[i].state.energy -= (uint64_t)drain;
            } else {
                players[i].state.energy = 0;
                players[i].nav_state = NAV_STATE_DRIFT;
                send_server_msg(i, "COMPUTER", "Hyperdrive failure: Zero energy. Ship is drifting.");
            }
            if (players[i].state.system_health[0] < THRESHOLD_SYS_DEGRADED) {
                players[i].nav_state = NAV_STATE_DRIFT;
                send_server_msg(i, "ENGINEERING", "Hyperdrive integrity compromised. Emergency drop. Ship is drifting.");
            }
        } else if (players[i].nav_state == NAV_STATE_IMPULSE) {
            double impulse_h = players[i].state.system_health[1];
            if (impulse_h < 1.0) impulse_h = 1.0;

            double engine_mult = RATIO_BASE_POWER + (players[i].state.power_dist[0] * RATIO_ENGINE_POWER); 
            /* Impulse speed affected by integrity */
            double imp_step = players[i].hyper_speed * engine_mult * (impulse_h / (double)YIELD_HARVEST_MAX);

            bool arrived = false;
            if (players[i].target_gx != -1.0) {
                double dx_t = players[i].target_gx - players[i].gx;
                double dy_t = players[i].target_gy - players[i].gy;
                double dz_t = players[i].target_gz - players[i].gz;
                double dist_to_target = sqrt(dx_t * dx_t + dy_t * dy_t + dz_t * dz_t);
                
                /* ETA Calculation */
                double speed_per_sec = imp_step * GAME_TICK_RATE;
                if (speed_per_sec > 0.01) {
                    players[i].eta = dist_to_target / speed_per_sec;
                } else {
                    players[i].eta = 0;
                }

                if (dist_to_target <= imp_step) {
                    players[i].gx = players[i].target_gx;
                    players[i].gy = players[i].target_gy;
                    players[i].gz = players[i].target_gz;
                    players[i].nav_state = NAV_STATE_IDLE;
                    players[i].hyper_speed = 0;
                    players[i].eta = 0;
                    players[i].target_gx = -1.0;
                    send_server_msg(i, "HELMSMAN", "Impulse target reached. All stop.");
                    arrived = true;
                }
            } else {
                players[i].eta = 0; /* No target, no ETA */
            }

            if (!arrived) {
                players[i].gx += players[i].dx * imp_step;
                players[i].gy += players[i].dy * imp_step;
                players[i].gz += players[i].dz * imp_step;
            }

            /* Derive Quadrant and Sector from High-Precision Global Coordinates */
            players[i].state.q1 = (int)(players[i].gx / QUADRANT_SIZE) + 1;
            players[i].state.q2 = (int)(players[i].gy / QUADRANT_SIZE) + 1;
            players[i].state.q3 = (int)(players[i].gz / QUADRANT_SIZE) + 1;
            players[i].state.s1 = players[i].gx - (players[i].state.q1 - 1) * QUADRANT_SIZE;
            players[i].state.s2 = players[i].gy - (players[i].state.q2 - 1) * QUADRANT_SIZE;
            players[i].state.s3 = players[i].gz - (players[i].state.q3 - 1) * QUADRANT_SIZE;

            /* Galactic Boundary Enforcement: Stop and Invert on edge contact */
            bool oob = false;
            double gal_limit = GALAXY_SIZE * QUADRANT_SIZE - DIST_EPSILON;
            if (players[i].gx < DIST_EPSILON) { players[i].gx = DIST_EPSILON; oob = true; }
            if (players[i].gx > gal_limit) { players[i].gx = gal_limit; oob = true; }
            if (players[i].gy < DIST_EPSILON) { players[i].gy = DIST_EPSILON; oob = true; }
            if (players[i].gy > gal_limit) { players[i].gy = gal_limit; oob = true; }
            if (players[i].gz < DIST_EPSILON) { players[i].gz = DIST_EPSILON; oob = true; }
            if (players[i].gz > gal_limit) { players[i].gz = gal_limit; oob = true; }

            if (oob) {
                players[i].nav_state = NAV_STATE_IDLE;
                players[i].hyper_speed = 0;
                players[i].eta = 0;
                players[i].dx = 0;
                players[i].dy = 0;
                players[i].dz = 0;
                players[i].state.van_h = fmod(players[i].state.van_h + 180.0, 360.0);
                players[i].state.van_m = -players[i].state.van_m;
                send_server_msg(i, "COMPUTER", "GALACTIC LIMIT REACHED: Engines disengaged. Position inverted.");
                players[i].state.q1 = get_q_from_g(players[i].gx);
                players[i].state.q2 = get_q_from_g(players[i].gy);
                players[i].state.q3 = get_q_from_g(players[i].gz);
                players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * QUADRANT_SIZE);
                players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * QUADRANT_SIZE);
                players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * QUADRANT_SIZE);
            }
            
            /* Realistic Impulse consumption: Linear with speed, Inverse with integrity */
            int imp_drain = (int)((QUADRANT_SIZE + (players[i].hyper_speed * (double)MAX_TORPEDO_CAPACITY)) * (100.0 / impulse_h));
            
            if (players[i].state.energy > (uint64_t)imp_drain) {
                players[i].state.energy -= (uint64_t)imp_drain;
            } else {
                players[i].state.energy = 0;
                players[i].nav_state = NAV_STATE_DRIFT;
                send_server_msg(i, "COMPUTER", "Impulse drive failure: Zero energy. Ship is drifting.");
            }
        } else if (players[i].nav_state == NAV_STATE_ALIGN_ONLY) {
            players[i].nav_timer--;

            /* Safety: Keep absolute coordinates synced while turning */
            players[i].gx = (players[i].state.q1 - 1) * QUADRANT_SIZE + players[i].state.s1;
            players[i].gy = (players[i].state.q2 - 1) * QUADRANT_SIZE + players[i].state.s2;
            players[i].gz = (players[i].state.q3 - 1) * QUADRANT_SIZE + players[i].state.s3;

            double diff_h = players[i].target_h - players[i].start_h;
            while(diff_h > 180.0) diff_h -= 360.0;
            while(diff_h < -180.0) diff_h += 360.0;
            double diff_r = players[i].target_r - players[i].start_r;
            while(diff_r > 180.0) diff_r -= 360.0;
            while(diff_r < -180.0) diff_r += 360.0;

            double init_t = (players[i].pending_bor_type > 0) ? players[i].pending_bor_type : 60.0;
            double t = 1.0 - players[i].nav_timer / init_t;
            players[i].state.van_h = (players[i].start_h + diff_h * t);
            players[i].state.van_m = (players[i].start_m + (players[i].target_m - players[i].start_m) * t);
            players[i].state.van_r = (players[i].start_r + diff_r * t);
            if (players[i].nav_timer <= 0) {
                players[i].state.van_h = players[i].target_h;
                players[i].state.van_m = players[i].target_m;
                players[i].state.van_r = players[i].target_r;
                players[i].nav_state = NAV_STATE_IDLE;
            }

        } else if (players[i].nav_state == NAV_STATE_APPROACH) {
            /* Standardized target resolution for all ID ranges */
            double tx = 0;
            double ty = 0;
            double tz = 0;
            bool found = false;
            int tid = players[i].apr_target;
            int q1 = players[i].state.q1;
            int q2 = players[i].state.q2;
            int q3 = players[i].state.q3;
            
            if (tid >= GALAXY_OBJECT_MIN_PLAYER && tid <= GALAXY_OBJECT_MAX_PLAYER) { 
                if (players[tid - 1].active) {
                    tx = players[tid - 1].gx;
                    ty = players[tid - 1].gy;
                    tz = players[tid - 1].gz;
                    found = true;
                } 
            } else if (tid >= GALAXY_OBJECT_MIN_NPC && tid <= GALAXY_OBJECT_MAX_NPC) { 
                if (npcs[tid - GALAXY_OBJECT_MIN_NPC].active) {
                    tx = npcs[tid - GALAXY_OBJECT_MIN_NPC].gx;
                    ty = npcs[tid - GALAXY_OBJECT_MIN_NPC].gy;
                    tz = npcs[tid - GALAXY_OBJECT_MIN_NPC].gz;
                    found = true;
                } 
            } else {
                /* 1. Local objects check (Fast path) */
                QuadrantIndex *lq = &spatial_index[q1][q2][q3];
                if (tid >= GALAXY_OBJECT_MIN_STARBASE && tid <= GALAXY_OBJECT_MAX_STARBASE) {
                    for (int b = 0; b < lq->base_count; b++) {
                        if (lq->bases[b]->id + GALAXY_OBJECT_MIN_STARBASE == tid) {
                            tx = (lq->bases[b]->q1 - 1) * QUADRANT_SIZE + lq->bases[b]->x;
                            ty = (lq->bases[b]->q2 - 1) * QUADRANT_SIZE + lq->bases[b]->y;
                            tz = (lq->bases[b]->q3 - 1) * QUADRANT_SIZE + lq->bases[b]->z;
                            found = true;
                        }
                    }
                } else if (tid >= GALAXY_OBJECT_MIN_PLANET && tid <= GALAXY_OBJECT_MAX_PLANET) {
                    for (int p = 0; p < lq->planet_count; p++) {
                        if (lq->planets[p]->id + GALAXY_OBJECT_MIN_PLANET == tid) {
                            tx = (lq->planets[p]->q1 - 1) * QUADRANT_SIZE + lq->planets[p]->x;
                            ty = (lq->planets[p]->q2 - 1) * QUADRANT_SIZE + lq->planets[p]->y;
                            tz = (lq->planets[p]->q3 - 1) * QUADRANT_SIZE + lq->planets[p]->z;
                            found = true;
                        }
                    }
                } else if (tid >= GALAXY_OBJECT_MIN_STAR && tid <= GALAXY_OBJECT_MAX_STAR) {
                    for (int s = 0; s < lq->star_count; s++) {
                        if (lq->stars[s]->id + GALAXY_OBJECT_MIN_STAR == tid) {
                            tx = (lq->stars[s]->q1 - 1) * QUADRANT_SIZE + lq->stars[s]->x;
                            ty = (lq->stars[s]->q2 - 1) * QUADRANT_SIZE + lq->stars[s]->y;
                            tz = (lq->stars[s]->q3 - 1) * QUADRANT_SIZE + lq->stars[s]->z;
                            found = true;
                        }
                    }
                } else if (tid >= GALAXY_OBJECT_MIN_ASTEROID && tid <= GALAXY_OBJECT_MAX_ASTEROID) {
                    for (int a = 0; a < lq->asteroid_count; a++) {
                        if (lq->asteroids[a]->id + GALAXY_OBJECT_MIN_ASTEROID == tid) {
                            tx = (lq->asteroids[a]->q1 - 1) * QUADRANT_SIZE + lq->asteroids[a]->x;
                            ty = (lq->asteroids[a]->q2 - 1) * QUADRANT_SIZE + lq->asteroids[a]->y;
                            tz = (lq->asteroids[a]->q3 - 1) * QUADRANT_SIZE + lq->asteroids[a]->z;
                            found = true;
                        }
                    }
                } else if (tid >= GALAXY_OBJECT_MIN_DERELICT && tid <= GALAXY_OBJECT_MAX_DERELICT) {
                    for (int d = 0; d < lq->derelict_count; d++) {
                        if (lq->derelicts[d]->id + GALAXY_OBJECT_MIN_DERELICT == tid) {
                            tx = (lq->derelicts[d]->q1 - 1) * QUADRANT_SIZE + lq->derelicts[d]->x;
                            ty = (lq->derelicts[d]->q2 - 1) * QUADRANT_SIZE + lq->derelicts[d]->y;
                            tz = (lq->derelicts[d]->q3 - 1) * QUADRANT_SIZE + lq->derelicts[d]->z;
                            found = true;
                        }
                    }
                } else if (tid >= GALAXY_OBJECT_MIN_PLATFORM && tid <= GALAXY_OBJECT_MAX_PLATFORM) {
                    for (int p = 0; p < lq->platform_count; p++) {
                        if (lq->platforms[p]->id + GALAXY_OBJECT_MIN_PLATFORM == tid) {
                            tx = (lq->platforms[p]->q1 - 1) * QUADRANT_SIZE + lq->platforms[p]->x;
                            ty = (lq->platforms[p]->q2 - 1) * QUADRANT_SIZE + lq->platforms[p]->y;
                            tz = (lq->platforms[p]->q3 - 1) * QUADRANT_SIZE + lq->platforms[p]->z;
                            found = true;
                        }
                    }
                }
                
                /* 2. Global fallback (Cross-quadrant tracking) */
                if (!found) {
                    if (tid >= GALAXY_OBJECT_MIN_STARBASE && tid <= GALAXY_OBJECT_MAX_STARBASE && bases[tid - GALAXY_OBJECT_MIN_STARBASE].active) {
                        int idx = tid - GALAXY_OBJECT_MIN_STARBASE;
                        tx = (bases[idx].q1 - 1) * QUADRANT_SIZE + bases[idx].x;
                        ty = (bases[idx].q2 - 1) * QUADRANT_SIZE + bases[idx].y;
                        tz = (bases[idx].q3 - 1) * QUADRANT_SIZE + bases[idx].z;
                        found = true;
                    } else if (tid >= GALAXY_OBJECT_MIN_PLANET && tid <= GALAXY_OBJECT_MAX_PLANET && planets[tid - GALAXY_OBJECT_MIN_PLANET].active) {
                        int idx = tid - GALAXY_OBJECT_MIN_PLANET;
                        tx = (planets[idx].q1 - 1) * QUADRANT_SIZE + planets[idx].x;
                        ty = (planets[idx].q2 - 1) * QUADRANT_SIZE + planets[idx].y;
                        tz = (planets[idx].q3 - 1) * QUADRANT_SIZE + planets[idx].z;
                        found = true;
                    } else if (tid >= GALAXY_OBJECT_MIN_STAR && tid <= GALAXY_OBJECT_MAX_STAR && stars_data[tid - GALAXY_OBJECT_MIN_STAR].active) {
                        int idx = tid - GALAXY_OBJECT_MIN_STAR;
                        tx = (stars_data[idx].q1 - 1) * QUADRANT_SIZE + stars_data[idx].x;
                        ty = (stars_data[idx].q2 - 1) * QUADRANT_SIZE + stars_data[idx].y;
                        tz = (stars_data[idx].q3 - 1) * QUADRANT_SIZE + stars_data[idx].z;
                        found = true;
                    } else if (tid >= GALAXY_OBJECT_MIN_BLACKHOLE && tid <= GALAXY_OBJECT_MAX_BLACKHOLE && black_holes[tid - GALAXY_OBJECT_MIN_BLACKHOLE].active) {
                        int idx = tid - GALAXY_OBJECT_MIN_BLACKHOLE;
                        tx = (black_holes[idx].q1 - 1) * QUADRANT_SIZE + black_holes[idx].x;
                        ty = (black_holes[idx].q2 - 1) * QUADRANT_SIZE + black_holes[idx].y;
                        tz = (black_holes[idx].q3 - 1) * QUADRANT_SIZE + black_holes[idx].z;
                        found = true;
                    } else if (tid >= GALAXY_OBJECT_MIN_ASTEROID && tid <= GALAXY_OBJECT_MAX_ASTEROID && asteroids[tid - GALAXY_OBJECT_MIN_ASTEROID].active) {
                        int idx = tid - GALAXY_OBJECT_MIN_ASTEROID;
                        tx = (asteroids[idx].q1 - 1) * QUADRANT_SIZE + asteroids[idx].x;
                        ty = (asteroids[idx].q2 - 1) * QUADRANT_SIZE + asteroids[idx].y;
                        tz = (asteroids[idx].q3 - 1) * QUADRANT_SIZE + asteroids[idx].z;
                        found = true;
                    } else if (tid >= GALAXY_OBJECT_MIN_DERELICT && tid <= GALAXY_OBJECT_MAX_DERELICT && derelicts[tid - GALAXY_OBJECT_MIN_DERELICT].active) {
                        int idx = tid - GALAXY_OBJECT_MIN_DERELICT;
                        tx = (derelicts[idx].q1 - 1) * QUADRANT_SIZE + derelicts[idx].x;
                        ty = (derelicts[idx].q2 - 1) * QUADRANT_SIZE + derelicts[idx].y;
                        tz = (derelicts[idx].q3 - 1) * QUADRANT_SIZE + derelicts[idx].z;
                        found = true;
                    }
                }
            }
            
            if (found) {
                double engine_mult = RATIO_BASE_POWER + (players[i].state.power_dist[0] * RATIO_ENGINE_POWER);
                double dx = tx - players[i].gx;
                double dy = ty - players[i].gy;
                double dz = tz - players[i].gz;
                double dist = sqrt(dx * dx + dy * dy + dz * dz);
                if (dist > players[i].approach_dist + (float)DIST_APPROACH_MARGIN) {
                    players[i].dx = dx / dist;
                    players[i].dy = dy / dist;
                    players[i].dz = dz / dist;
                    
                    /* Deceleration phase: slow down when approaching target distance */
                    double speed = RATIO_BASE_POWER * engine_mult;
                    double delta_d = dist - players[i].approach_dist;
                    if (delta_d < (double)DIST_DECEL_START) {
                        speed *= (RATIO_DECEL_MIN + RATIO_DECEL_SLOPE * delta_d); /* Linear ramp down to 20% speed */
                    }
                    
                    players[i].gx += players[i].dx * speed; 
                    players[i].gy += players[i].dy * speed; 
                    players[i].gz += players[i].dz * speed;
                    
                    /* Smooth Orientation Tracking (Avoid wild spinning) */
                    double th = atan2(dx, -dy) * 180.0 / M_PI;
                    if (th < 0) {
                        th += 360;
                    }
                    double tm = asin(dz / dist) * 180.0 / M_PI;
                    double dh = th - players[i].state.van_h;
                    while (dh > 180) {
                        dh -= 360; 
                    }
                    while (dh < -180) {
                        dh += 360;
                    }
                    players[i].state.van_h = fmod(players[i].state.van_h + dh * 0.15 + 360.0, 360.0);
                    players[i].state.van_m = (players[i].state.van_m + (tm - players[i].state.van_m) * 0.15);
                } else {
                    /* Arrival: Snap to exact approach position to prevent oscillation */
                    if (dist > 0.001) {
                        players[i].gx = tx - (dx / dist) * players[i].approach_dist;
                        players[i].gy = ty - (dy / dist) * players[i].approach_dist;
                        players[i].gz = tz - (dz / dist) * players[i].approach_dist;
                    }
                    players[i].nav_state = NAV_STATE_IDLE;
                    players[i].apr_target = 0;
                    send_server_msg(i, "COMPUTER", "Approach distance reached. Autopilot disengaged.");
                }
            } else {
                players[i].nav_state = NAV_STATE_IDLE;
                players[i].apr_target = 0;
            }
            players[i].state.q1 = get_q_from_g(players[i].gx);
            players[i].state.q2 = get_q_from_g(players[i].gy);
            players[i].state.q3 = get_q_from_g(players[i].gz);
            players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * QUADRANT_SIZE);
            players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * QUADRANT_SIZE);
            players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * QUADRANT_SIZE);
        } else if (players[i].nav_state == NAV_STATE_WORMHOLE) {
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
                players[i].state.wormhole = (NetPoint){rwx, rwy, rwz, 1};
                
                /* Move ship INTO the wormhole during the entry phase */
                players[i].gx += (players[i].wx - players[i].gx) * 0.05;
                players[i].gy += (players[i].wy - players[i].gy) * 0.05;
                players[i].gz += (players[i].wz - players[i].gz) * 0.05;
                players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * QUADRANT_SIZE);
                players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * QUADRANT_SIZE);
                players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * QUADRANT_SIZE);
            } else {
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
                
                /* Register the arrival wormhole at this starting position */
                push_server_event(i, IPC_EV_JUMP, players[i].state.s1, players[i].state.s2, players[i].state.s3, 0, 0, 0, 1);
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

            if (players[i].nav_timer <= SHIELD_REGEN_DELAY) { 
                players[i].nav_state = NAV_STATE_IDLE;
                players[i].state.wormhole.active = 0;
                send_server_msg(i, "HELMSMAN", "Wormhole traversal successful. Welcome to destination.");
            }
        } else if (players[i].nav_state == NAV_STATE_CHASE) {
            int tid = players[i].state.lock_target;
            double tx = 0;
            double ty = 0;
            double tz = 0;
            bool found = false;
            if (tid >= GALAXY_OBJECT_MIN_PLAYER && tid <= GALAXY_OBJECT_MAX_PLAYER) {
                if (players[tid - 1].active) {
                    tx = players[tid - 1].gx;
                    ty = players[tid - 1].gy;
                    tz = players[tid - 1].gz;
                    found = true;
                }
            } else if (tid >= GALAXY_OBJECT_MIN_NPC && tid <= GALAXY_OBJECT_MAX_NPC) {
                if (npcs[tid - GALAXY_OBJECT_MIN_NPC].active) {
                    tx = npcs[tid - GALAXY_OBJECT_MIN_NPC].gx;
                    ty = npcs[tid - GALAXY_OBJECT_MIN_NPC].gy;
                    tz = npcs[tid - GALAXY_OBJECT_MIN_NPC].gz;
                    found = true;
                }
            } else if (tid >= GALAXY_OBJECT_MIN_COMET && tid <= GALAXY_OBJECT_MAX_COMET) {
                if (comets[tid - GALAXY_OBJECT_MIN_COMET].active) {
                    tx = (comets[tid - GALAXY_OBJECT_MIN_COMET].q1 - 1) * QUADRANT_SIZE + comets[tid - GALAXY_OBJECT_MIN_COMET].x;
                    ty = (comets[tid - GALAXY_OBJECT_MIN_COMET].q2 - 1) * QUADRANT_SIZE + comets[tid - GALAXY_OBJECT_MIN_COMET].y;
                    tz = (comets[tid - GALAXY_OBJECT_MIN_COMET].q3 - 1) * QUADRANT_SIZE + comets[tid - GALAXY_OBJECT_MIN_COMET].z;
                    found = true;
                }
            } else if (tid >= GALAXY_OBJECT_MIN_MONSTER && tid <= GALAXY_OBJECT_MAX_MONSTER) {
                if (monsters[tid - GALAXY_OBJECT_MIN_MONSTER].active) {
                    tx = (monsters[tid - GALAXY_OBJECT_MIN_MONSTER].q1 - 1) * QUADRANT_SIZE + monsters[tid - GALAXY_OBJECT_MIN_MONSTER].x;
                    ty = (monsters[tid - GALAXY_OBJECT_MIN_MONSTER].q2 - 1) * QUADRANT_SIZE + monsters[tid - GALAXY_OBJECT_MIN_MONSTER].y;
                    tz = (monsters[tid - GALAXY_OBJECT_MIN_MONSTER].q3 - 1) * QUADRANT_SIZE + monsters[tid - GALAXY_OBJECT_MIN_MONSTER].z;
                    found = true;
                }
            } else if (tid >= GALAXY_OBJECT_MIN_PROBE && tid <= GALAXY_OBJECT_MAX_PROBE) {
                int p_idx = (tid - GALAXY_OBJECT_MIN_PROBE) / 3;
                int pr_idx = (tid - GALAXY_OBJECT_MIN_PROBE) % 3;
                if (p_idx < MAX_CLIENTS && players[p_idx].state.probes[pr_idx].active) {
                    tx = players[p_idx].state.probes[pr_idx].gx;
                    ty = players[p_idx].state.probes[pr_idx].gy;
                    tz = players[p_idx].state.probes[pr_idx].gz;
                    found = true;
                }
            }
            
            if (found) {
                double engine_mult = RATIO_BASE_POWER + (players[i].state.power_dist[0] * RATIO_ENGINE_POWER);
                double dx = tx - players[i].gx;
                double dy = ty - players[i].gy;
                double dz = tz - players[i].gz;
                double dist = sqrt(dx * dx + dy * dy + dz * dz);
                double target_dist = 2.0; /* Default chase distance */
                if (tid >= GALAXY_OBJECT_MIN_NPC && tid <= GALAXY_OBJECT_MAX_NPC) {
                    target_dist = (npcs[tid - GALAXY_OBJECT_MIN_NPC].health > (YIELD_MINE_MAX)) ? 3.0 : 1.5;
                } else if (tid >= GALAXY_OBJECT_MIN_COMET && tid <= GALAXY_OBJECT_MAX_COMET) {
                    target_dist = 0.5; /* Harvesting range for comets */
                } else if (tid >= GALAXY_OBJECT_MIN_MONSTER && tid <= GALAXY_OBJECT_MAX_MONSTER) {
                    target_dist = 2.5; /* Safety distance for monsters */
                }

                if (dist > target_dist + (float)DIST_APPROACH_MARGIN) {
                    players[i].dx = dx / dist;
                    players[i].dy = dy / dist;
                    players[i].dz = dz / dist;
                    players[i].gx += players[i].dx * (double)DIST_EPSILON * engine_mult; 
                    players[i].gy += players[i].dy * (double)DIST_EPSILON * engine_mult; 
                    players[i].gz += players[i].dz * (double)DIST_EPSILON * engine_mult;
                } else if (dist < target_dist - (float)DIST_APPROACH_MARGIN) {
                    players[i].dx = -dx / dist;
                    players[i].dy = -dy / dist;
                    players[i].dz = -dz / dist;
                    players[i].gx += players[i].dx * 0.03 * engine_mult; 
                    players[i].gy += players[i].dy * 0.03 * engine_mult; 
                    players[i].gz += players[i].dz * 0.03 * engine_mult;
                }
                players[i].state.van_h = atan2(dx, -dy) * 180.0 / M_PI;
                if (players[i].state.van_h < 0) {
                    players[i].state.van_h += 360;
                }
                players[i].state.van_m = asin(dz / dist) * 180.0 / M_PI;
            } else {
                players[i].nav_state = NAV_STATE_IDLE;
                send_server_msg(i, "COMPUTER", "Chase target lost.");
            }
            players[i].state.q1 = get_q_from_g(players[i].gx);
            players[i].state.q2 = get_q_from_g(players[i].gy);
            players[i].state.q3 = get_q_from_g(players[i].gz);
            players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * QUADRANT_SIZE);
            players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * QUADRANT_SIZE);
            players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * QUADRANT_SIZE);
            players[i].state.energy -= (COST_ACTION_LOW / 5);
        } else if (players[i].nav_state == NAV_STATE_DOCKING) {
            players[i].nav_timer--;
            int b_idx = players[i].pending_bor_target - GALAXY_OBJECT_MIN_STARBASE;
            if (b_idx < 0 || b_idx >= MAX_BASES || !bases[b_idx].active) {
                players[i].nav_state = NAV_STATE_IDLE;
                send_server_msg(i, "STARBASE", "Docking aborted: Starbase link lost.");
            } else {
                double d = sqrt(pow(bases[b_idx].x - players[i].state.s1, 2) + 
                                pow(bases[b_idx].y - players[i].state.s2, 2) + 
                                pow(bases[b_idx].z - players[i].state.s3, 2));
                if (d > 3.5) {
                    players[i].nav_state = NAV_STATE_IDLE;
                    send_server_msg(i, "STARBASE", "Docking aborted: Out of range.");
                }
            }
            if (players[i].nav_state == NAV_STATE_DOCKING && players[i].nav_timer <= 0) {
                players[i].state.energy = MAX_ENERGY_CAPACITY;
                players[i].state.torpedoes = MAX_TORPEDO_CAPACITY;
                players[i].state.hull_integrity = (double)YIELD_HARVEST_MAX;
                players[i].state.life_support = (double)YIELD_HARVEST_MAX;
                for (int s = 0; s < MAX_SYSTEMS; s++) {
                    players[i].state.system_health[s] = (double)YIELD_HARVEST_MAX;
                }
                players[i].is_docked = 1;
                players[i].nav_state = NAV_STATE_IDLE;
                send_server_msg(i, "STARBASE", "Docking complete. Systems replenished and repaired.");
            }
        } else if (players[i].nav_state == NAV_STATE_DRIFT) {
            players[i].gx += players[i].dx * players[i].hyper_speed;
            players[i].gy += players[i].dy * players[i].hyper_speed;
            players[i].gz += players[i].dz * players[i].hyper_speed;
            players[i].hyper_speed *= 0.995; /* Slow down gradually */
            if (players[i].hyper_speed < 0.001) { players[i].hyper_speed = 0; players[i].nav_state = NAV_STATE_IDLE; }
            players[i].state.q1 = get_q_from_g(players[i].gx); players[i].state.q2 = get_q_from_g(players[i].gy); players[i].state.q3 = get_q_from_g(players[i].gz);
            players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * QUADRANT_SIZE); players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * QUADRANT_SIZE); players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * QUADRANT_SIZE);
        } else {
            /* Safety Anchor: ONLY when completely idle or in a state not handled above */
            players[i].gx = (players[i].state.q1 - 1) * QUADRANT_SIZE + players[i].state.s1;
            players[i].gy = (players[i].state.q2 - 1) * QUADRANT_SIZE + players[i].state.s2;
            players[i].gz = (players[i].state.q3 - 1) * QUADRANT_SIZE + players[i].state.s3;
        }

        /* Set torp_active based on global torpedo check for this player */
        bool has_active_torp = false;
        for(int t=0; t<MAX_GLOBAL_TORPEDOES; t++) if(players_torpedoes[t].active && players_torpedoes[t].owner_idx == i) { has_active_torp = true; break; }
        players[i].torp_active = has_active_torp;
    }

    rebuild_spatial_index();

    /* Global Torpedo System Update Loop - High Performance Spatial Filtering */
    #pragma omp parallel for schedule(dynamic, 10)
    for (int t = 0; t < MAX_GLOBAL_TORPEDOES; t++) {
        if (!players_torpedoes[t].active) continue;

        PlayerTorpedo *pt = &players_torpedoes[t];
        
        /* 1. Homing Logic */
        if (pt->target_id > 0) {
            double target_gx = -1.0, target_gy = -1.0, target_gz = -1.0;
            int tid = pt->target_id; 
            
            if (tid >= GALAXY_OBJECT_MIN_PLAYER && tid <= GALAXY_OBJECT_MAX_PLAYER && players[tid-1].active) { 
                target_gx = players[tid-1].gx; target_gy = players[tid-1].gy; target_gz = players[tid-1].gz; 
            } else if (tid >= GALAXY_OBJECT_MIN_NPC && tid <= GALAXY_OBJECT_MAX_NPC && npcs[tid-GALAXY_OBJECT_MIN_NPC].active) { 
                target_gx = npcs[tid-GALAXY_OBJECT_MIN_NPC].gx; target_gy = npcs[tid-GALAXY_OBJECT_MIN_NPC].gy; target_gz = npcs[tid-GALAXY_OBJECT_MIN_NPC].gz; 
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
                broadcast_server_event(pt->q1, pt->q2, pt->q3, IPC_EV_BOOM, bx, by, bz, 0, 0, 0, 1);
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
                if (hit_target >= GALAXY_OBJECT_MIN_PLAYER && hit_target <= GALAXY_OBJECT_MAX_PLAYER) {
                    ConnectedPlayer *p = &players[hit_target - 1];

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
                } else if (hit_target >= GALAXY_OBJECT_MIN_NPC) {
                    npcs[hit_target - GALAXY_OBJECT_MIN_NPC].health -= (DMG_TORPEDO / YIELD_HARVEST_MAX);
                    if (npcs[hit_target - GALAXY_OBJECT_MIN_NPC].health <= 0) npcs[hit_target - GALAXY_OBJECT_MIN_NPC].death_timer = GAME_TICK_RATE;
                }
                broadcast_server_event(pt->q1, pt->q2, pt->q3, IPC_EV_BOOM, pt->x, pt->y, pt->z, 0, 0, 0, 1); 
            }
            pt->active = false;
        }
        if (pt->timeout-- <= 0) pt->active = false;
        if (pt->gx < 0 || pt->gx > GALAXY_SIZE*QUADRANT_SIZE || pt->gy < 0 || pt->gy > GALAXY_SIZE*QUADRANT_SIZE || pt->gz < 0 || pt->gz > GALAXY_SIZE*QUADRANT_SIZE) pt->active = false;
    }

    /* Prepare Global Visible Torpedoes for Networking */
    NetVisibleTorpedo global_vt_list[MAX_GLOBAL_TORPEDOES];
    int global_vt_count = 0;
    for(int t=0; t<MAX_GLOBAL_TORPEDOES; t++) {
        if (players_torpedoes[t].active) {
            global_vt_list[global_vt_count++] = (NetVisibleTorpedo){players_torpedoes[t].gx, players_torpedoes[t].gy, players_torpedoes[t].gz, (float)players_torpedoes[t].faction, (uint32_t)players_torpedoes[t].id};
        }
    }

    pthread_mutex_unlock(&game_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].socket == 0 || !players[i].active) {
            continue;
        }
        PacketUpdate upd; 
        memset(&upd, 0, sizeof(PacketUpdate)); 
        upd.type = PKT_UPDATE;
        /* ... existing field assignments ... */
        upd.q1 = players[i].state.q1; 
        upd.q2 = players[i].state.q2; 
        upd.q3 = players[i].state.q3;
        /* ... (keeping all assignments for consistency) ... */
        upd.s1 = players[i].state.s1; upd.s2 = players[i].state.s2; upd.s3 = players[i].state.s3;
        upd.van_h = players[i].state.van_h; upd.van_m = players[i].state.van_m; upd.van_r = players[i].state.van_r;
        upd.eta = players[i].eta;
        upd.energy = players[i].state.energy; upd.hull_integrity = players[i].state.hull_integrity;
        upd.torpedoes = players[i].state.torpedoes; upd.cargo_energy = players[i].state.cargo_energy;
        upd.cargo_torpedoes = players[i].state.cargo_torpedoes; upd.crew_count = players[i].state.crew_count;
        upd.prison_unit = players[i].state.prison_unit; upd.composite_plating = players[i].state.composite_plating;
        for(int s=0; s<6; s++) upd.shields[s] = players[i].state.shields[s];
        for(int inv=0; inv<10; inv++) upd.inventory[inv] = players[i].state.inventory[inv];
        for(int sys=0; sys < MAX_SYSTEMS; sys++) upd.system_health[sys] = players[i].state.system_health[sys];
        for(int p=0; p<3; p++) upd.power_dist[p] = players[i].state.power_dist[p];
        upd.life_support = players[i].state.life_support; upd.anti_matter_count = players[i].state.anti_matter_count;
        upd.lock_target = players[i].state.lock_target; upd.tube_state = players[i].state.tube_state;
        for(int t=0; t<4; t++) upd.tube_load_timers[t] = players[i].tube_load_timers[t];
        upd.current_tube = players[i].current_tube; upd.ion_beam_charge = players[i].state.ion_beam_charge;
        upd.is_cloaked = players[i].state.is_cloaked; upd.is_docked = players[i].is_docked;
        upd.red_alert = players[i].state.red_alert; upd.nav_state = (uint8_t)players[i].nav_state;
        upd.show_axes = players[i].state.show_axes; upd.show_grid = players[i].state.show_grid;
        upd.show_bridge = players[i].state.show_bridge; upd.show_map = players[i].state.show_map;
        upd.map_filter = players[i].state.map_filter;
        upd.shm_crypto_algo = players[i].state.shm_crypto_algo;
        upd.encryption_flags = players[i].state.encryption_flags;
        
        int o_idx = 0;
        upd.objects[o_idx] = (NetObject){players[i].state.s1, players[i].state.s2, players[i].state.s3, players[i].state.van_h, players[i].state.van_m, players[i].state.van_r, 1, players[i].ship_class, 1, (int)players[i].state.hull_integrity, players[i].state.energy, 0, (int)players[i].state.hull_integrity, players[i].faction, i+1, players[i].state.is_cloaked, ""};
        strncpy(upd.objects[o_idx].name, players[i].name, 63); upd.objects[o_idx].name[63] = '\0';
        o_idx++;

        QuadrantIndex *lq = &spatial_index[upd.q1][upd.q2][upd.q3];
        for(int n=0; n<lq->npc_count && o_idx < MAX_NET_OBJECTS; n++) {
            NPCShip *npc = lq->npcs[n]; if (!npc->active) continue;
            upd.objects[o_idx] = (NetObject){npc->x, npc->y, npc->z, npc->h, npc->m, 0.0, npc->faction, npc->ship_class, 1, (int)(npc->health / (int)THRESHOLD_SYS_CRITICAL), npc->energy, npc->plating, (int)(npc->health / (int)THRESHOLD_SYS_CRITICAL), npc->faction, npc->id + GALAXY_OBJECT_MIN_NPC, npc->is_cloaked, ""};
            snprintf(upd.objects[o_idx].name, 64, "%s", npc->name); o_idx++;
        }
        for(int j=0; j<lq->player_count && o_idx < MAX_NET_OBJECTS; j++) {
            ConnectedPlayer *p = lq->players[j]; if (!p->active || p == &players[i]) continue;
            upd.objects[o_idx] = (NetObject){p->state.s1, p->state.s2, p->state.s3, p->state.van_h, p->state.van_m, p->state.van_r, 1, p->ship_class, 1, (int)p->state.hull_integrity, p->state.energy, 0, (int)p->state.hull_integrity, p->faction, (int)(p - players) + GALAXY_OBJECT_MIN_PLAYER, p->state.is_cloaked, ""};
            snprintf(upd.objects[o_idx].name, 64, "%s", p->name); o_idx++;
        }
        /* ... celestial objects ... */
        for(int s=0; s<lq->star_count && o_idx < MAX_NET_OBJECTS; s++) { NPCStar *st = lq->stars[s]; if(!st->active) continue; upd.objects[o_idx++] = (NetObject){st->x, st->y, st->z, 0, 0, 0, 4, 1, 1, 100, 0, 0, 100, 4, st->id + GALAXY_OBJECT_MIN_STAR, 0, "Star"}; }
        for(int p=0; p<lq->planet_count && o_idx < MAX_NET_OBJECTS; p++) { NPCPlanet *pl = lq->planets[p]; if(!pl->active) continue; upd.objects[o_idx++] = (NetObject){pl->x, pl->y, pl->z, 0, 0, 0, 5, pl->resource_type, 1, 100, pl->amount, 0, 100, 5, pl->id + GALAXY_OBJECT_MIN_PLANET, 0, "Planet"}; }
        for(int b=0; b<lq->base_count && o_idx < MAX_NET_OBJECTS; b++) { NPCBase *ba = lq->bases[b]; if(!ba->active) continue; upd.objects[o_idx++] = (NetObject){ba->x, ba->y, ba->z, 0, 0, 0, 3, 1, 1, (int)(ba->health/(COST_ACTION_MED * 2)), 0, 0, (int)(ba->health/(COST_ACTION_MED * 2)), 0, ba->id + GALAXY_OBJECT_MIN_STARBASE, 0, "Starbase"}; }
        for(int h=0; h<lq->bh_count && o_idx < MAX_NET_OBJECTS; h++) { NPCBlackHole *bh = lq->black_holes[h]; if(!bh->active) continue; upd.objects[o_idx++] = (NetObject){bh->x, bh->y, bh->z, 0, 0, 0, 6, 0, 1, 100, 0, 0, 100, 6, bh->id + GALAXY_OBJECT_MIN_BLACKHOLE, 0, "Black Hole"}; }
        for(int n=0; n<lq->nebula_count && o_idx < MAX_NET_OBJECTS; n++) { NPCNebula *nb = lq->nebulas[n]; if(!nb->active) continue; upd.objects[o_idx++] = (NetObject){nb->x, nb->y, nb->z, 0, 0, 0, 7, nb->type, 1, 100, 0, 0, 100, 7, nb->id + GALAXY_OBJECT_MIN_NEBULA, 0, "Nebula"}; }
        for(int p=0; p<lq->pulsar_count && o_idx < MAX_NET_OBJECTS; p++) { NPCPulsar *pu = lq->pulsars[p]; if(!pu->active) continue; upd.objects[o_idx++] = (NetObject){pu->x, pu->y, pu->z, 0, 0, 0, 8, 0, 1, 100, 0, 0, 100, 8, pu->id + GALAXY_OBJECT_MIN_PULSAR, 0, "Pulsar"}; }
        for(int qsr=0; qsr<lq->quasar_count && o_idx < MAX_NET_OBJECTS; qsr++) { NPCQuasar *qs = lq->quasars[qsr]; if(!qs->active) continue; upd.objects[o_idx++] = (NetObject){qs->x, qs->y, qs->z, 0, 0, 0, 29, qs->type, 1, 100, 0, 0, 100, 0, qs->id + GALAXY_OBJECT_MIN_QUASAR, 0, "Quasar"}; }
        for(int c=0; c<lq->comet_count && o_idx < MAX_NET_OBJECTS; c++) { NPCComet *co = lq->comets[c]; if(!co->active) continue; upd.objects[o_idx++] = (NetObject){co->x, co->y, co->z, 0, 0, 0, 9, 0, 1, 100, 0, 0, 100, 9, co->id + GALAXY_OBJECT_MIN_COMET, 0, "Comet"}; }
        for(int d=0; d<lq->derelict_count && o_idx < MAX_NET_OBJECTS; d++) { NPCDerelict *de = lq->derelicts[d]; if(!de->active) continue; upd.objects[o_idx] = (NetObject){de->x, de->y, de->z, 0, 0, 0, 22, de->ship_class, 1, 100, 0, 0, 100, de->faction, de->id + GALAXY_OBJECT_MIN_DERELICT, 0, ""}; snprintf(upd.objects[o_idx].name, 64, "%s", de->name); o_idx++; }
        for(int a=0; a<lq->asteroid_count && o_idx < MAX_NET_OBJECTS; a++) { NPCAsteroid *as = lq->asteroids[a]; if(!as->active) continue; upd.objects[o_idx++] = (NetObject){as->x, as->y, as->z, 0, 0, 0, 21, as->resource_type, 1, 100, as->amount, (int)(as->size * 100), 100, 21, as->id + GALAXY_OBJECT_MIN_ASTEROID, 0, "Asteroid"}; }
        for(int m=0; m<lq->mine_count && o_idx < MAX_NET_OBJECTS; m++) { NPCMine *mi = lq->mines[m]; if(!mi->active) continue; upd.objects[o_idx++] = (NetObject){mi->x, mi->y, mi->z, 0, 0, 0, 23, 0, 1, 100, 0, 0, 100, 23, mi->id + GALAXY_OBJECT_MIN_MINE, 0, "Mine"}; }
        for(int b=0; b<lq->buoy_count && o_idx < MAX_NET_OBJECTS; b++) { NPCBuoy *bu = lq->buoys[b]; if(!bu->active) continue; upd.objects[o_idx++] = (NetObject){bu->x, bu->y, bu->z, 0, 0, 0, 24, 0, 1, 100, 0, 0, 100, 24, bu->id + GALAXY_OBJECT_MIN_BUOY, 0, "Comm Buoy"}; }
        for(int p=0; p<lq->platform_count && o_idx < MAX_NET_OBJECTS; p++) { NPCPlatform *pl = lq->platforms[p]; if(!pl->active) continue; upd.objects[o_idx++] = (NetObject){pl->x, pl->y, pl->z, 0, 0, 0, 25, 0, 1, (int)(pl->health/(COST_ACTION_MED * 2)), 0, 0, (int)(pl->health/(COST_ACTION_MED * 2)), pl->faction, pl->id + GALAXY_OBJECT_MIN_PLATFORM, 0, "Defense Platform"}; }
        for(int r=0; r<lq->rift_count && o_idx < MAX_NET_OBJECTS; r++) { NPCRift *ri = lq->rifts[r]; if(!ri->active) continue; upd.objects[o_idx++] = (NetObject){ri->x, ri->y, ri->z, 0, 0, 0, 26, 0, 1, 100, 0, 0, 100, 26, ri->id + GALAXY_OBJECT_MIN_RIFT, 0, "Spatial Rift"}; }
        for(int m=0; m<lq->monster_count && o_idx < MAX_NET_OBJECTS; m++) { NPCMonster *mo = lq->monsters[m]; if(!mo->active) continue; upd.objects[o_idx] = (NetObject){mo->x, mo->y, mo->z, 0, 0, 0, mo->type, 0, 1, (int)(mo->health/MAX_TORPEDO_CAPACITY), 0, 0, (int)(mo->health/MAX_TORPEDO_CAPACITY), 30, mo->id + GALAXY_OBJECT_MIN_MONSTER, 0, ""}; snprintf(upd.objects[o_idx].name, 64, "%s", (mo->type==30)?"Crystalline Entity":"Space Amoeba"); o_idx++; }

        upd.object_count = o_idx;
        for(int s=0; s<4; s++) upd.torps[s] = players[i].state.torps[s];
        
        /* Torpedo Streaming - Aggregated Zero-Loss FX approach */
        double qbx = (players[i].state.q1 - 1) * QUADRANT_SIZE;
        double qby = (players[i].state.q2 - 1) * QUADRANT_SIZE;
        double qbz = (players[i].state.q3 - 1) * QUADRANT_SIZE;

        for (int gt = 0; gt < global_vt_count; gt++) {
            double rtx = global_vt_list[gt].x - qbx;
            double rty = global_vt_list[gt].y - qby;
            double rtz = global_vt_list[gt].z - qbz;
            /* Visibility check: Only if within range */
            if (rtx >= -10.0 && rtx <= (QUADRANT_SIZE + 10.0) && rty >= -10.0 && rty <= (QUADRANT_SIZE + 10.0) && rtz >= -10.0 && rtz <= (QUADRANT_SIZE + 10.0)) {
                push_server_event(i, IPC_EV_TORPEDO, rtx, rty, rtz, 0, 0, 0, (int)global_vt_list[gt].id);
            }
        }
        upd.torpedo_count = 0; /* Standard stream disabled, using Zero-Loss queue instead */

        upd.event_count = players[i].state.event_count;
        if (upd.event_count > MAX_NET_EVENTS) upd.event_count = MAX_NET_EVENTS;
        memcpy(upd.events, players[i].state.events, upd.event_count * sizeof(NetEvent));
        
        upd.wormhole = players[i].state.wormhole; 
        for(int p=0; p<3; p++) upd.probes[p] = players[i].state.probes[p];
        /* ... beam aggregation (already fast but could be pre-aggregated too if needed) ... */
        int b_idx = 0;
        for (int j = 0; j < lq->player_count; j++) { ConnectedPlayer *p_src = lq->players[j]; if (!p_src->active) continue; for (int b = 0; b < p_src->state.beam_count && b_idx < MAX_NET_BEAMS; b++) upd.beams[b_idx++] = p_src->state.beams[b]; }
        for (int n = 0; n < lq->npc_count; n++) { NPCShip *ns = lq->npcs[n]; if (!ns->active) continue; for (int b = 0; b < ns->beam_count && b_idx < MAX_NET_BEAMS; b++) upd.beams[b_idx++] = ns->beams[b]; }
        for (int m = 0; m < lq->monster_count; m++) { NPCMonster *mo = lq->monsters[m]; if (!mo->active) continue; for (int b = 0; b < mo->beam_count && b_idx < MAX_NET_BEAMS; b++) upd.beams[b_idx++] = mo->beams[b]; }
        for (int p = 0; p < lq->platform_count; p++) { NPCPlatform *pl = lq->platforms[p]; if (!pl->active) continue; for (int b = 0; b < pl->beam_count && b_idx < MAX_NET_BEAMS; b++) upd.beams[b_idx++] = pl->beams[b]; }
        upd.beam_count = b_idx;

        if (supernova_event.supernova_timer > 0) { upd.map_update_q[0] = supernova_event.supernova_q1; upd.map_update_q[1] = supernova_event.supernova_q2; upd.map_update_q[2] = supernova_event.supernova_q3; upd.map_update_val = -supernova_event.supernova_timer; }
        else { upd.map_update_q[0] = upd.q1; upd.map_update_q[1] = upd.q2; upd.map_update_q[2] = upd.q3; upd.map_update_val = spacegl_master.g[upd.q1][upd.q2][upd.q3]; }
        int rq1 = rand() % GALAXY_SIZE + 1; int rq2 = rand() % GALAXY_SIZE + 1; int rq3 = rand() % GALAXY_SIZE + 1;
        upd.map_update_q2[0] = rq1; upd.map_update_q2[1] = rq2; upd.map_update_q2[2] = rq3; upd.map_update_val2 = spacegl_master.g[rq1][rq2][rq3];

        send_optimized_update(i, &upd);
    }

    rebuild_spatial_index();

    /* Reset transient effects after all updates have been sent */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].active) {
            players[i].state.beam_count = 0;
            players[i].state.event_count = 0;
        }
    }
    for (int i = 0; i < MAX_NPC; i++) {
        if (npcs[i].active) {
            npcs[i].beam_count = 0;
        }
    }
    for (int i = 0; i < MAX_MONSTERS; i++) {
        if (monsters[i].active) {
            monsters[i].beam_count = 0;
        }
    }
    for (int i = 0; i < MAX_PLATFORMS; i++) {
        if (platforms[i].active) {
            platforms[i].beam_count = 0;
        }
    }
}

void apply_hull_damage(int i, double amount) {
    if (i < 0 || i >= MAX_CLIENTS || !players[i].active) return;
    
    players[i].state.hull_integrity -= amount;
    if (players[i].state.hull_integrity < 0) players[i].state.hull_integrity = 0;
    
    /* Random system damage (1-5% per hit) */
    int sys = rand() % MAX_SYSTEMS;
    double sys_dmg = 1.0 + (rand() % 400) / (double)YIELD_HARVEST_MAX;
    players[i].state.system_health[sys] -= sys_dmg;
    if (players[i].state.system_health[sys] < 0) players[i].state.system_health[sys] = 0;
    
    const char* sys_names[] = {"Hyperdrive", "Impulse", "Sensors", "Transporters", "Ion Beams", "Torpedoes", "Computer", "Life Support", "Shields", "Auxiliary"};
    char msg[128];
    sprintf(msg, "Hull impact! System %s damaged by %.1f%%.", sys_names[sys], sys_dmg);
    send_server_msg(i, "DAMAGE", msg);
}

int calculate_shield_index(double shooter_x, double shooter_y, double shooter_z,
                           double target_x, double target_y, double target_z,
                           double target_h, double target_m, double target_r) {
    double rel_dx = shooter_x - target_x;
    double rel_dy = shooter_y - target_y;
    double rel_dz = shooter_z - target_z;

    double rad_h = target_h * M_PI / 180.0;
    double rad_m = target_m * M_PI / 180.0;
    double rad_r = target_r * M_PI / 180.0;

    /* Local basis vectors of the target ship (Standard North alignment) */
    /* Forward (F) - Always longitudinal axis */
    double fx = cos(rad_m) * sin(rad_h);
    double fy = cos(rad_m) * -cos(rad_h);
    double fz = sin(rad_m);

    /* Original Right (R_orig) - No roll */
    double rx_orig = cos(rad_h);
    double ry_orig = sin(rad_h);
    double rz_orig = 0;

    /* Original Up (U_orig) = F x R_orig */
    double ux_orig = fy * rz_orig - fz * ry_orig;
    double uy_orig = fz * rx_orig - fx * rz_orig;
    double uz_orig = fx * ry_orig - fy * rx_orig;

    /* Apply Roll (Rodrigues Rotation Formula around F) */
    /* R_new = R_orig*cos(r) + (F x R_orig)*sin(r)  [F.R_orig is zero] */
    double rx = rx_orig * cos(rad_r) + ux_orig * sin(rad_r);
    double ry = ry_orig * cos(rad_r) + uy_orig * sin(rad_r);
    double rz = rz_orig * cos(rad_r) + uz_orig * sin(rad_r);

    /* U_new = U_orig*cos(r) + (F x U_orig)*sin(r) 
       F x U_orig = F x (F x R_orig) = F(F.R_orig) - R_orig(F.F) = -R_orig */
    double ux = ux_orig * cos(rad_r) - rx_orig * sin(rad_r);
    double uy = uy_orig * cos(rad_r) - ry_orig * sin(rad_r);
    double uz = uz_orig * cos(rad_r) - rz_orig * sin(rad_r);

    /* Project relative vector onto local basis */
    double v_f = rel_dx * fx + rel_dy * fy + rel_dz * fz;
    double v_r = rel_dx * rx + rel_dy * ry + rel_dz * rz;
    double v_u = rel_dx * ux + rel_dy * uy + rel_dz * uz;

    double dist = sqrt(rel_dx * rel_dx + rel_dy * rel_dy + rel_dz * rel_dz);
    if (dist < 1e-6) return 0;
    /* Vertical angle in local space */
    double local_vertical_angle = asin(v_u / dist) * 180.0 / M_PI;

    if (local_vertical_angle > 45.0) return 2; 
    if (local_vertical_angle < -45.0) return 3; 

    /* Horizontal angle in local space (F is forward, R is right) */
    double local_horizontal_angle = atan2(v_r, v_f) * 180.0 / M_PI;

    if (local_horizontal_angle > -45.0 && local_horizontal_angle <= 45.0) return 0;  
    if (local_horizontal_angle > 45.0 && local_horizontal_angle <= 135.0) return 5; /* Right */
    if (local_horizontal_angle > 135.0 || local_horizontal_angle <= -135.0) return 1; 
    return 4; /* Left */
    }
