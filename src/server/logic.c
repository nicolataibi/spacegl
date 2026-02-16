/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <sys/socket.h>
#include "server_internal.h"

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
        npcs[n].gx = (npcs[n].q1 - 1) * 10.0 + npcs[n].x;
        npcs[n].gy = (npcs[n].q2 - 1) * 10.0 + npcs[n].y;
        npcs[n].gz = (npcs[n].q3 - 1) * 10.0 + npcs[n].z;
    }

    int q1 = npcs[n].q1;
    int q2 = npcs[n].q2;
    int q3 = npcs[n].q3;
    if (!IS_Q_VALID(q1, q2, q3)) {
        return;
    }
    QuadrantIndex *local_q = &spatial_index[q1][q2][q3];
    
    int closest_p = -1;
    double min_d2 = 100.0;
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
    
    if (npcs[n].energy < 200) {
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
    if (npcs[n].engine_health < 10.0 || npcs[n].health < 500) {
        speed = 0;
    } else {
        speed *= (npcs[n].engine_health / 100.0);
    }

    if (npcs[n].ai_state == AI_STATE_ATTACK_RUN && closest_p != -1) {
        if (npcs[n].nav_timer <= 0) {
            npcs[n].tx = (rand() % 100) / 10.0;
            npcs[n].ty = (rand() % 100) / 10.0;
            npcs[n].tz = (rand() % 100) / 10.0;
            npcs[n].tx += (npcs[n].q1 - 1) * 10.0;
            npcs[n].ty += (npcs[n].q2 - 1) * 10.0;
            npcs[n].tz += (npcs[n].q3 - 1) * 10.0;
            npcs[n].nav_timer = 3000;
        }
        double dx = npcs[n].tx - npcs[n].gx;
        double dy = npcs[n].ty - npcs[n].gy;
        double dz = npcs[n].tz - npcs[n].gz;
        double dist = sqrt(dx * dx + dy * dy + dz * dz);
        if (dist > 0.5) {
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
            npcs[n].nav_timer = 120;
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
        if (npcs[n].fire_cooldown <= 0 && dist_to_player < 8.0) {
            players[closest_p].state.beam_count = 1; 
            players[closest_p].state.beams[0] = (NetBeam){npcs[n].x, npcs[n].y, npcs[n].z, target->state.s1, target->state.s2, target->state.s3, 1};
            double base_dmg = 1000.0;
            if (npcs[n].faction == FACTION_SWARM) {
                base_dmg = 8000.0;
            } else if (npcs[n].faction == FACTION_KORTHIAN) {
                base_dmg = 2500.0;
            } else if (npcs[n].faction == FACTION_XYLARI) {
                base_dmg = 3500.0;
            }
            double dist_val = dist_to_player;
            if (dist_val < 0.1) {
                dist_val = 0.1;
            }
            double dist_factor = 1.5 / dist_val;
            if (dist_factor > 1.0) {
                dist_factor = 1.0;
            }
            int dmg = (int)(base_dmg * dist_factor);
            
            double rel_dx = npcs[n].x - target->state.s1;
            double rel_dy = npcs[n].y - target->state.s2;
            double rel_dz = npcs[n].z - target->state.s3;
            double angle = atan2(rel_dx, -rel_dy) * 180.0 / M_PI;
            if (angle < 0) {
                angle += 360;
            }
            double rel_angle = angle - target->state.van_h;
            while (rel_angle < 0) {
                rel_angle += 360;
            }
            while (rel_angle >= 360) {
                rel_angle -= 360;
            }
            double dist_2d = sqrt(rel_dx * rel_dx + rel_dy * rel_dy);
            double vertical_angle = atan2(rel_dz, dist_2d) * 180.0 / M_PI;
            int s_idx = 0;
            if (vertical_angle > 45) {
                s_idx = 2;
            } else if (vertical_angle < -45) {
                s_idx = 3;
            } else {
                if (rel_angle > 315 || rel_angle <= 45) {
                    s_idx = 0;
                } else if (rel_angle > 45 && rel_angle <= 135) {
                    s_idx = 5;
                } else if (rel_angle > 135 && rel_angle <= 225) {
                    s_idx = 1;
                } else {
                    s_idx = 4;
                }
            }
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
                double hull_dmg = dmg_rem / 1000.0;
                apply_hull_damage(closest_p, hull_dmg);
                target->state.energy -= (dmg_rem / 2);
            }
            target->shield_regen_delay = 90;
            if (target->state.hull_integrity <= 0 || target->state.energy <= 0) {
                target->state.energy = 0;
                target->state.hull_integrity = 0;
                target->state.crew_count = 0;
                target->death_timer = 30;
                target->state.boom = (NetPoint){target->state.s1, target->state.s2, target->state.s3, 1};
            }
            npcs[n].fire_cooldown = (npcs[n].faction == FACTION_SWARM) ? 100 : 150;
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
        if (d > 0.1) {
            d_dx = dx / d;
            d_dy = dy / d;
            d_dz = dz / d;
            speed *= 1.8;
        }
        if (d > 8.5) {
            npcs[n].ai_state = AI_STATE_PATROL;
        }
    } else {
        if (npcs[n].nav_timer-- <= 0) { 
            npcs[n].nav_timer = 100 + rand() % 200; 
            double rx = (rand() % 100 - 50) / 100.0;
            double ry = (rand() % 100 - 50) / 100.0;
            double rz = (rand() % 100 - 50) / 100.0;
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
    if (npcs[n].gx < 0.05) {
        npcs[n].gx = 0.05;
    }
    if (npcs[n].gx > 99.95) {
        npcs[n].gx = 99.95;
    }
    if (npcs[n].gy < 0.05) {
        npcs[n].gy = 0.05;
    }
    if (npcs[n].gy > 99.95) {
        npcs[n].gy = 99.95;
    }
    if (npcs[n].gz < 0.05) {
        npcs[n].gz = 0.05;
    }
    if (npcs[n].gz > 99.95) {
        npcs[n].gz = 99.95;
    }
    npcs[n].q1 = get_q_from_g(npcs[n].gx);
    npcs[n].q2 = get_q_from_g(npcs[n].gy);
    npcs[n].q3 = get_q_from_g(npcs[n].gz);
    npcs[n].x = npcs[n].gx - (npcs[n].q1 - 1) * 10.0;
    npcs[n].y = npcs[n].gy - (npcs[n].q2 - 1) * 10.0;
    npcs[n].z = npcs[n].gz - (npcs[n].q3 - 1) * 10.0;
}

void update_game_logic() {
    global_tick++;
    pthread_mutex_lock(&game_mutex);
    
    if (global_tick % 500 == 0) {
        for(int i=1; i<=10; i++) {
            for(int j=1; j<=10; j++) {
                for(int l=1; l<=10; l++) {
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

    /* Autosave every 10 seconds (300 ticks @ 30 TPS) */
    if (global_tick % 300 == 0) {
        save_galaxy();
    }

    for (int n = 0; n < MAX_NPC; n++) {
        if (npcs[n].active) {
            if (npcs[n].death_timer > 0) {
                npcs[n].death_timer--;
                if (npcs[n].death_timer <= 0) {
                    npcs[n].active = 0;
                    spawn_derelict(npcs[n].q1, npcs[n].q2, npcs[n].q3, npcs[n].x, npcs[n].y, npcs[n].z, npcs[n].faction, npcs[n].ship_class, npcs[n].name);
                    for(int i=0; i<MAX_CLIENTS; i++) {
                        if (players[i].socket && players[i].state.q1 == npcs[n].q1 && players[i].state.q2 == npcs[n].q2 && players[i].state.q3 == npcs[n].q3) {
                            players[i].state.boom = (NetPoint){npcs[n].x, npcs[n].y, npcs[n].z, 1};
                        }
                    }
                }
            }
            update_npc_ai(n);
        }
    }

    for (int c = 0; c < MAX_COMETS; c++) {
        if (comets[c].active) {
            comets[c].angle += comets[c].speed;
            double r = comets[c].a * (1.0 - 0.5 * 0.5) / (1.0 + 0.5 * cos(comets[c].angle)); /* Use eccentricity 0.5 for ellipses */
            double gx = comets[c].cx + r * cos(comets[c].angle) * cos(comets[c].inc);
            double gy = comets[c].cy + r * sin(comets[c].angle) * cos(comets[c].inc);
            double gz = comets[c].cz + r * sin(comets[c].inc);
            
            if (gx < 0.05) gx = 0.05; 
            if (gx > 99.95) gx = 99.95;
            if (gy < 0.05) gy = 0.05; 
            if (gy > 99.95) gy = 99.95;
            if (gz < 0.05) gz = 0.05; 
            if (gz > 99.95) gz = 99.95;

            comets[c].q1 = get_q_from_g(gx);
            comets[c].q2 = get_q_from_g(gy);
            comets[c].q3 = get_q_from_g(gz);
            comets[c].x = gx - (comets[c].q1 - 1) * 10.0;
            comets[c].y = gy - (comets[c].q2 - 1) * 10.0;
            comets[c].z = gz - (comets[c].q3 - 1) * 10.0;
        }
    }

    if (supernova_event.supernova_timer > 0) {
        supernova_event.supernova_timer--;
        int q1 = supernova_event.supernova_q1;
        int q2 = supernova_event.supernova_q2;
        int q3 = supernova_event.supernova_q3;
        spacegl_master.g[q1][q2][q3] = -supernova_event.supernova_timer;
        int sec = supernova_event.supernova_timer / 30;
        if (sec > 0 && (supernova_event.supernova_timer % 300 == 0 || (sec <= 10 && supernova_event.supernova_timer % 30 == 0))) {
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
                    players[i].state.boom = (NetPoint){players[i].state.s1, players[i].state.s2, players[i].state.s3, 1};
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
    } else if (global_tick > 100 && supernova_event.supernova_timer <= 0 && (rand() % 9000 < 1)) {
        int rq1 = rand() % 10 + 1; 
        int rq2 = rand() % 10 + 1; 
        int rq3 = rand() % 10 + 1;
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

    for (int mo = 0; mo < MAX_MONSTERS; mo++) {
        if (!monsters[mo].active) {
            continue;
        }
        int q1 = monsters[mo].q1; 
        int q2 = monsters[mo].q2; 
        int q3 = monsters[mo].q3;
        QuadrantIndex *local_q = &spatial_index[q1][q2][q3];
        ConnectedPlayer *target = NULL; 
        double min_d = 10.0;
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
            monsters[mo].x += (dx / dist) * 0.05; 
            monsters[mo].y += (dy / dist) * 0.05; 
            monsters[mo].z += (dz / dist) * 0.05;
            if (min_d < 4.0 && global_tick % 60 == 0) {
                target->state.beam_count = 1; 
                target->state.beams[0] = (NetBeam){monsters[mo].x, monsters[mo].y, monsters[mo].z, target->state.s1, target->state.s2, target->state.s3, 1};
                target->state.energy -= 500; 
                send_server_msg((int)(target - players), "SCIENCE", "CRYSTALLINE RESONANCE DETECTED!");
            }
        }
    }

    /* Comet Tail Resource Collection */
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
                    if (global_tick % 30 == 0) { /* Once per second */
                        players[i].state.inventory[6] += 5;
                        send_server_msg(i, "SCIENCE", "Nebular Gas collected from comet tail (+5).");
                    }
                }
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].socket && players[i].death_timer > 0) {
            players[i].death_timer--;
            if (players[i].death_timer <= 0) {
                players[i].active = 0;
                spawn_derelict(players[i].state.q1, players[i].state.q2, players[i].state.q3, players[i].state.s1, players[i].state.s2, players[i].state.s3, players[i].faction, players[i].ship_class, players[i].name);
                players[i].state.boom = (NetPoint){players[i].state.s1, players[i].state.s2, players[i].state.s3, 1};
                send_server_msg(i, "CRITICAL", "SHIP DESTROYED.");
            }
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!players[i].active) {
            continue;
        }
        if (players[i].state.crew_count <= 0) {
            players[i].active = 0;
            spawn_derelict(players[i].state.q1, players[i].state.q2, players[i].state.q3, players[i].state.s1, players[i].state.s2, players[i].state.s3, players[i].faction, players[i].ship_class, players[i].name);
            players[i].state.boom = (NetPoint){players[i].state.s1, players[i].state.s2, players[i].state.s3, 1};
            continue;
        }

        if (players[i].state.energy > 100) {
            double integrity_mult = players[i].state.system_health[8] / 100.0;
            double regen_rate = (0.5 + (players[i].state.power_dist[1] * 10.0)) * integrity_mult;
            bool needs_regen = false;
            for(int s=0; s<6; s++) {
                if (players[i].state.shields[s] < 10000) {
                    players[i].state.shields[s] += (int)regen_rate;
                    if (players[i].state.shields[s] > 10000) {
                        players[i].state.shields[s] = 10000;
                    }
                    needs_regen = true;
                }
            }
            if (needs_regen) {
                players[i].state.energy -= (int)(regen_rate * 0.8);
            }
        }

        if (players[i].state.ion_beam_charge < 100.0) {
            double recharge_rate = 0.5 + (players[i].state.power_dist[2] * 2.5);
            players[i].state.ion_beam_charge += recharge_rate;
            if (players[i].state.ion_beam_charge > 100.0) {
                players[i].state.ion_beam_charge = 100.0;
            }
            players[i].state.energy -= (int)(recharge_rate * 2.0);
        }

        for(int t=0; t<4; t++) {
            if (players[i].tube_load_timers[t] > 0) {
                players[i].tube_load_timers[t]--;
            }
        }
        if (players[i].torp_load_timer > 0) {
            players[i].torp_load_timer--;
        }

        if (players[i].state.system_health[5] <= 50.0) {
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
            if (tid >= 1 && tid <= 32) { 
                if (players[tid-1].active) {
                    valid = true; 
                }
            } else if (tid >= 1000 && tid < 1000+MAX_NPC) { 
                if (npcs[tid-1000].active) {
                    valid = true; 
                }
            } else if (tid >= 2000 && tid < 2000+MAX_BASES) { 
                if (bases[tid-2000].active && bases[tid-2000].q1 == pq1 && bases[tid-2000].q2 == pq2 && bases[tid-2000].q3 == pq3) {
                    valid = true; 
                }
            } else if (tid >= 3000 && tid < 3000+MAX_PLANETS) { 
                if (planets[tid-3000].active && planets[tid-3000].q1 == pq1 && planets[tid-3000].q2 == pq2 && planets[tid-3000].q3 == pq3) {
                    valid = true; 
                }
            } else if (tid >= 4000 && tid < 4000+MAX_STARS) {
                if (stars_data[tid-4000].active && stars_data[tid-4000].q1 == pq1 && stars_data[tid-4000].q2 == pq2 && stars_data[tid-4000].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= 7000 && tid < 7000+MAX_BH) {
                if (black_holes[tid-7000].active && black_holes[tid-7000].q1 == pq1 && black_holes[tid-7000].q2 == pq2 && black_holes[tid-7000].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= 8000 && tid < 8000+MAX_NEBULAS) {
                if (nebulas[tid-8000].active && nebulas[tid-8000].q1 == pq1 && nebulas[tid-8000].q2 == pq2 && nebulas[tid-8000].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= 9000 && tid < 9000+MAX_PULSARS) {
                if (pulsars[tid-9000].active && pulsars[tid-9000].q1 == pq1 && pulsars[tid-9000].q2 == pq2 && pulsars[tid-9000].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= 10000 && tid < 10000+MAX_COMETS) {
                if (comets[tid-10000].active) {
                    valid = true;
                }
            } else if (tid >= 11000 && tid < 11000+MAX_DERELICTS) {
                if (derelicts[tid-11000].active) {
                    valid = true;
                }
            } else if (tid >= 12000 && tid < 12000+MAX_ASTEROIDS) {
                if (asteroids[tid-12000].active && asteroids[tid-12000].q1 == pq1 && asteroids[tid-12000].q2 == pq2 && asteroids[tid-12000].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= 14000 && tid < 14000+MAX_MINES) {
                if (mines[tid-14000].active && mines[tid-14000].q1 == pq1 && mines[tid-14000].q2 == pq2 && mines[tid-14000].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= 15000 && tid < 15000+MAX_BUOYS) {
                if (buoys[tid-15000].active && buoys[tid-15000].q1 == pq1 && buoys[tid-15000].q2 == pq2 && buoys[tid-15000].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS) { 
                if (platforms[tid-16000].active && platforms[tid-16000].q1 == pq1 && platforms[tid-16000].q2 == pq2 && platforms[tid-16000].q3 == pq3) {
                    valid = true; 
                }
            } else if (tid >= 17000 && tid < 17000+MAX_RIFTS) {
                if (rifts[tid-17000].active && rifts[tid-17000].q1 == pq1 && rifts[tid-17000].q2 == pq2 && rifts[tid-17000].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= 18000 && tid < 18000+MAX_MONSTERS) {
                if (monsters[tid-18000].active && monsters[tid-18000].q1 == pq1 && monsters[tid-18000].q2 == pq2 && monsters[tid-18000].q3 == pq3) {
                    valid = true;
                }
            } else if (tid >= 19000 && tid < 19200) {
                int p_idx = (tid - 19000) / 3;
                int pr_idx = (tid - 19000) % 3;
                if (p_idx < MAX_CLIENTS && players[p_idx].state.probes[pr_idx].active) {
                    valid = true;
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

                    players[i].state.probes[p].s1 = (players[i].state.probes[p].gx - (pr_q1-1)*10.0);
                    players[i].state.probes[p].s2 = (players[i].state.probes[p].gy - (pr_q2-1)*10.0);
                    players[i].state.probes[p].s3 = (players[i].state.probes[p].gz - (pr_q3-1)*10.0);
                    
                    players[i].state.probes[p].eta -= (1.0 / 30.0);
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

            double engine_mult = 0.1 + (players[i].state.power_dist[0] * 1.5); 
            /* Realistic physics: Effective speed scales with system integrity */
            double step = players[i].hyper_speed * engine_mult * (hyper_h / 100.0);
            
            double dx_t = players[i].target_gx - players[i].gx;
            double dy_t = players[i].target_gy - players[i].gy;
            double dz_t = players[i].target_gz - players[i].gz;
            double dist_to_target = sqrt(dx_t * dx_t + dy_t * dy_t + dz_t * dz_t);
            
            if (dist_to_target <= step) {
                players[i].gx = players[i].target_gx;
                players[i].gy = players[i].target_gy;
                players[i].gz = players[i].target_gz;
                players[i].nav_state = NAV_STATE_IDLE;
                players[i].hyper_speed = 0;
                send_server_msg(i, "HELMSMAN", "Target reached. Dropping out of Hyperdrive.");
            } else {
                players[i].gx += players[i].dx * step;
                players[i].gy += players[i].dy * step;
                players[i].gz += players[i].dz * step;
            }

            players[i].state.q1 = get_q_from_g(players[i].gx); 
            players[i].state.q2 = get_q_from_g(players[i].gy); 
            players[i].state.q3 = get_q_from_g(players[i].gz);
            players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * 10.0); 
            players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * 10.0); 
            players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * 10.0);

            /* Galactic Boundary Enforcement: Stop and Invert on edge contact */
            bool oob = false;
            if (players[i].gx < 0.05) { players[i].gx = 0.05; oob = true; }
            if (players[i].gx > 99.95) { players[i].gx = 99.95; oob = true; }
            if (players[i].gy < 0.05) { players[i].gy = 0.05; oob = true; }
            if (players[i].gy > 99.95) { players[i].gy = 99.95; oob = true; }
            if (players[i].gz < 0.05) { players[i].gz = 0.05; oob = true; }
            if (players[i].gz > 99.95) { players[i].gz = 99.95; oob = true; }

            if (oob) {
                players[i].nav_state = NAV_STATE_IDLE;
                players[i].hyper_speed = 0;
                players[i].dx = 0; players[i].dy = 0; players[i].dz = 0;
                players[i].state.van_h = fmod(players[i].state.van_h + 180.0, 360.0);
                players[i].state.van_m = -players[i].state.van_m;
                send_server_msg(i, "COMPUTER", "GALACTIC LIMIT REACHED: Engines disengaged. Position inverted.");
                players[i].state.q1 = get_q_from_g(players[i].gx); players[i].state.q2 = get_q_from_g(players[i].gy); players[i].state.q3 = get_q_from_g(players[i].gz);
                players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * 10.0); players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * 10.0); players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * 10.0);
            }
            
            /* Realistic Hyperdrive consumption: Quadratic with factor, Inverse with integrity */
            double current_f = players[i].hyper_speed * 30.0;
            int drain = (int)((50.0 + (current_f * current_f * 4.0)) * (100.0 / hyper_h));
            players[i].state.energy -= drain;

            if (players[i].state.energy <= 0) {
                players[i].state.energy = 0;
                players[i].nav_state = NAV_STATE_DRIFT;
                send_server_msg(i, "COMPUTER", "Hyperdrive failure: Zero energy. Ship is drifting.");
            }
            if (players[i].state.system_health[0] < 50.0) {
                players[i].nav_state = NAV_STATE_DRIFT;
                send_server_msg(i, "ENGINEERING", "Hyperdrive integrity compromised. Emergency drop. Ship is drifting.");
            }
        } else if (players[i].nav_state == NAV_STATE_IMPULSE) {
            double impulse_h = players[i].state.system_health[1];
            if (impulse_h < 1.0) impulse_h = 1.0;

            double engine_mult = 0.5 + (players[i].state.power_dist[0] * 1.0); 
            /* Impulse speed affected by integrity */
            double imp_step = players[i].hyper_speed * engine_mult * (impulse_h / 100.0);

            bool arrived = false;
            if (players[i].target_gx != -1.0) {
                double dx_t = players[i].target_gx - players[i].gx;
                double dy_t = players[i].target_gy - players[i].gy;
                double dz_t = players[i].target_gz - players[i].gz;
                double dist_to_target = sqrt(dx_t * dx_t + dy_t * dy_t + dz_t * dz_t);
                
                if (dist_to_target <= imp_step) {
                    players[i].gx = players[i].target_gx;
                    players[i].gy = players[i].target_gy;
                    players[i].gz = players[i].target_gz;
                    players[i].nav_state = NAV_STATE_IDLE;
                    players[i].hyper_speed = 0;
                    players[i].target_gx = -1.0;
                    send_server_msg(i, "HELMSMAN", "Impulse target reached. All stop.");
                    arrived = true;
                }
            }

            if (!arrived) {
                players[i].gx += players[i].dx * imp_step;
                players[i].gy += players[i].dy * imp_step;
                players[i].gz += players[i].dz * imp_step;
            }

            players[i].state.q1 = get_q_from_g(players[i].gx); 
            players[i].state.q2 = get_q_from_g(players[i].gy); 
            players[i].state.q3 = get_q_from_g(players[i].gz);
            players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * 10.0); 
            players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * 10.0); 
            players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * 10.0);

            /* Galactic Boundary Enforcement: Stop and Invert on edge contact */
            bool oob = false;
            if (players[i].gx < 0.05) { players[i].gx = 0.05; oob = true; }
            if (players[i].gx > 99.95) { players[i].gx = 99.95; oob = true; }
            if (players[i].gy < 0.05) { players[i].gy = 0.05; oob = true; }
            if (players[i].gy > 99.95) { players[i].gy = 99.95; oob = true; }
            if (players[i].gz < 0.05) { players[i].gz = 0.05; oob = true; }
            if (players[i].gz > 99.95) { players[i].gz = 99.95; oob = true; }

            if (oob) {
                players[i].nav_state = NAV_STATE_IDLE;
                players[i].hyper_speed = 0;
                players[i].dx = 0; players[i].dy = 0; players[i].dz = 0;
                players[i].state.van_h = fmod(players[i].state.van_h + 180.0, 360.0);
                players[i].state.van_m = -players[i].state.van_m;
                send_server_msg(i, "COMPUTER", "GALACTIC LIMIT REACHED: Engines disengaged. Position inverted.");
                players[i].state.q1 = get_q_from_g(players[i].gx); players[i].state.q2 = get_q_from_g(players[i].gy); players[i].state.q3 = get_q_from_g(players[i].gz);
                players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * 10.0); players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * 10.0); players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * 10.0);
            }
            
            /* Realistic Impulse consumption: Linear with speed, Inverse with integrity */
            int imp_drain = (int)((10.0 + (players[i].hyper_speed * 1000.0)) * (100.0 / impulse_h));
            players[i].state.energy -= imp_drain;

            if (players[i].state.energy <= 0) {
                players[i].state.energy = 0;
                players[i].nav_state = NAV_STATE_DRIFT;
                send_server_msg(i, "COMPUTER", "Impulse drive failure: Zero energy. Ship is drifting.");
            }
        } else if (players[i].nav_state == NAV_STATE_ALIGN_ONLY) {
            players[i].nav_timer--;
            double diff_h = players[i].target_h - players[i].start_h;
            while(diff_h > 180.0) diff_h -= 360.0;
            while(diff_h < -180.0) diff_h += 360.0;
            double init_t = (players[i].pending_bor_type > 0) ? players[i].pending_bor_type : 60.0;
            double t = 1.0 - players[i].nav_timer / init_t;
            players[i].state.van_h = (players[i].start_h + diff_h * t);
            players[i].state.van_m = (players[i].start_m + (players[i].target_m - players[i].start_m) * t);
            if (players[i].nav_timer <= 0) players[i].nav_state = NAV_STATE_IDLE;
        } else if (players[i].nav_state == NAV_STATE_APPROACH) {
            /* Standardized target resolution for all ID ranges */
            double tx=0, ty=0, tz=0; bool found=false;
            int tid = players[i].apr_target;
            int q1=players[i].state.q1, q2=players[i].state.q2, q3=players[i].state.q3;
            
            if (tid >= 1 && tid <= 32) { 
                if (players[tid-1].active) { tx=players[tid-1].gx; ty=players[tid-1].gy; tz=players[tid-1].gz; found=true; } 
            }
            else if (tid >= 1000 && tid < 1000+MAX_NPC) { 
                if (npcs[tid-1000].active) { tx=npcs[tid-1000].gx; ty=npcs[tid-1000].gy; tz=npcs[tid-1000].gz; found=true; } 
            }
            else {
                /* 1. Local objects check (Fast path) */
                QuadrantIndex *lq = &spatial_index[q1][q2][q3];
                if (tid >= 2000 && tid < 2000+MAX_BASES) { for(int b=0; b<lq->base_count; b++) if(lq->bases[b]->id+2000 == tid) { tx=(lq->bases[b]->q1-1)*10.0+lq->bases[b]->x; ty=(lq->bases[b]->q2-1)*10.0+lq->bases[b]->y; tz=(lq->bases[b]->q3-1)*10.0+lq->bases[b]->z; found=true; } }
                else if (tid >= 3000 && tid < 3000+MAX_PLANETS) { for(int p=0; p<lq->planet_count; p++) if(lq->planets[p]->id+3000 == tid) { tx=(lq->planets[p]->q1-1)*10.0+lq->planets[p]->x; ty=(lq->planets[p]->q2-1)*10.0+lq->planets[p]->y; tz=(lq->planets[p]->q3-1)*10.0+lq->planets[p]->z; found=true; } }
                else if (tid >= 4000 && tid < 4000+MAX_STARS) { for(int s=0; s<lq->star_count; s++) if(lq->stars[s]->id+4000 == tid) { tx=(lq->stars[s]->q1-1)*10.0+lq->stars[s]->x; ty=(lq->stars[s]->q2-1)*10.0+lq->stars[s]->y; tz=(lq->stars[s]->q3-1)*10.0+lq->stars[s]->z; found=true; } }
                else if (tid >= 11000 && tid < 11000+MAX_DERELICTS) { for(int d=0; d<lq->derelict_count; d++) if(lq->derelicts[d]->id+11000 == tid) { tx=(lq->derelicts[d]->q1-1)*10.0+lq->derelicts[d]->x; ty=(lq->derelicts[d]->q2-1)*10.0+lq->derelicts[d]->y; tz=(lq->derelicts[d]->q3-1)*10.0+lq->derelicts[d]->z; found=true; } }
                else if (tid >= 12000 && tid < 12000+MAX_ASTEROIDS) { for(int a=0; a<lq->asteroid_count; a++) if(lq->asteroids[a]->id+12000 == tid) { tx=(lq->asteroids[a]->q1-1)*10.0+lq->asteroids[a]->x; ty=(lq->asteroids[a]->q2-1)*10.0+lq->asteroids[a]->y; tz=(lq->asteroids[a]->q3-1)*10.0+lq->asteroids[a]->z; found=true; } }
                else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS) { for(int p=0; p<lq->platform_count; p++) if(lq->platforms[p]->id+16000 == tid) { tx=(lq->platforms[p]->q1-1)*10.0+lq->platforms[p]->x; ty=(lq->platforms[p]->q2-1)*10.0+lq->platforms[p]->y; tz=(lq->platforms[p]->q3-1)*10.0+lq->platforms[p]->z; found=true; } }
                
                /* 2. Global fallback (Cross-quadrant tracking) */
                if (!found) {
                    if (tid >= 2000 && tid < 2000+MAX_BASES && bases[tid-2000].active) { int idx=tid-2000; tx=(bases[idx].q1-1)*10.0+bases[idx].x; ty=(bases[idx].q2-1)*10.0+bases[idx].y; tz=(bases[idx].q3-1)*10.0+bases[idx].z; found=true; }
                    else if (tid >= 3000 && tid < 3000+MAX_PLANETS && planets[tid-3000].active) { int idx=tid-3000; tx=(planets[idx].q1-1)*10.0+planets[idx].x; ty=(planets[idx].q2-1)*10.0+planets[idx].y; tz=(planets[idx].q3-1)*10.0+planets[idx].z; found=true; }
                    else if (tid >= 4000 && tid < 4000+MAX_STARS && stars_data[tid-4000].active) { int idx=tid-4000; tx=(stars_data[idx].q1-1)*10.0+stars_data[idx].x; ty=(stars_data[idx].q2-1)*10.0+stars_data[idx].y; tz=(stars_data[idx].q3-1)*10.0+stars_data[idx].z; found=true; }
                    else if (tid >= 7000 && tid < 7000+MAX_BH && black_holes[tid-7000].active) { int idx=tid-7000; tx=(black_holes[idx].q1-1)*10.0+black_holes[idx].x; ty=(black_holes[idx].q2-1)*10.0+black_holes[idx].y; tz=(black_holes[idx].q3-1)*10.0+black_holes[idx].z; found=true; }
                    else if (tid >= 11000 && tid < 11000+MAX_DERELICTS && derelicts[tid-11000].active) { int idx=tid-11000; tx=(derelicts[idx].q1-1)*10.0+derelicts[idx].x; ty=(derelicts[idx].q2-1)*10.0+derelicts[idx].y; tz=(derelicts[idx].q3-1)*10.0+derelicts[idx].z; found=true; }
                    else if (tid >= 12000 && tid < 12000+MAX_ASTEROIDS && asteroids[tid-12000].active) { int idx=tid-12000; tx=(asteroids[idx].q1-1)*10.0+asteroids[idx].x; ty=(asteroids[idx].q2-1)*10.0+asteroids[idx].y; tz=(asteroids[idx].q3-1)*10.0+asteroids[idx].z; found=true; }
                }
            }
            
            if (found) {
                double engine_mult = 0.5 + (players[i].state.power_dist[0] * 1.0);
                double dx = tx - players[i].gx, dy = ty - players[i].gy, dz = tz - players[i].gz;
                double dist = sqrt(dx*dx + dy*dy + dz*dz);
                if (dist > players[i].approach_dist + 0.05) {
                    players[i].dx = dx/dist; players[i].dy = dy/dist; players[i].dz = dz/dist;
                    players[i].gx += players[i].dx * 0.05 * engine_mult; 
                    players[i].gy += players[i].dy * 0.05 * engine_mult; 
                    players[i].gz += players[i].dz * 0.05 * engine_mult;
                    
                    /* Smooth Orientation Tracking (Avoid wild spinning) */
                    double th = atan2(dx, -dy) * 180.0 / M_PI; if(th < 0) th += 360;
                    double tm = asin(dz/dist) * 180.0 / M_PI;
                    double dh = th - players[i].state.van_h;
                    while(dh > 180) 
                        dh -= 360; 
                    while(dh < -180) 
                        dh += 360;
                    players[i].state.van_h = fmod(players[i].state.van_h + dh * 0.15 + 360.0, 360.0);
                    players[i].state.van_m = (players[i].state.van_m + (tm - players[i].state.van_m) * 0.15);
                } else {
                    players[i].nav_state = NAV_STATE_IDLE; players[i].apr_target = 0;
                    send_server_msg(i, "COMPUTER", "Approach distance reached. Autopilot disengaged.");
                }
            } else { players[i].nav_state = NAV_STATE_IDLE; players[i].apr_target = 0; }
            players[i].state.q1 = get_q_from_g(players[i].gx); players[i].state.q2 = get_q_from_g(players[i].gy); players[i].state.q3 = get_q_from_g(players[i].gz);
            players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * 10.0); players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * 10.0); players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * 10.0);
        } else if (players[i].nav_state == NAV_STATE_WORMHOLE) {
            players[i].nav_timer--;
            
            /* Sci-Fi Message Sequence */
            if (players[i].nav_timer == 420) 
                send_server_msg(i, "ENGINEERING", "Injecting exotic matter into local Schwarzschild metric...");
            else if (players[i].nav_timer == 380)
                send_server_msg(i, "SCIENCE", "Einstein-Rosen Bridge detected. Stabilizing singularity...");
            else if (players[i].nav_timer == 320)
                send_server_msg(i, "HELMSMAN", "Wormhole mouth stable. Entering event horizon.");

            /* Update Wormhole visual position in packet (Only before jump) */
            if (players[i].nav_timer > 300) {
                /* Relativize to CURRENT quadrant so it renders correctly even if in adjacent Q */
                double rwx = (players[i].wx - (players[i].state.q1 - 1) * 10.0);
                double rwy = (players[i].wy - (players[i].state.q2 - 1) * 10.0);
                double rwz = (players[i].wz - (players[i].state.q3 - 1) * 10.0);
                players[i].state.wormhole = (NetPoint){rwx, rwy, rwz, 1};
                
                /* Move ship INTO the wormhole during the entry phase */
                players[i].gx += (players[i].wx - players[i].gx) * 0.05;
                players[i].gy += (players[i].wy - players[i].gy) * 0.05;
                players[i].gz += (players[i].wz - players[i].gz) * 0.05;
                players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * 10.0);
                players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * 10.0);
                players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * 10.0);
            } else {
                players[i].state.wormhole.active = 0;
            }

            /* EXECUTE JUMP at T=300 */
            if (players[i].nav_timer == 300) {
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
                players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * 10.0);
                players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * 10.0);
                players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * 10.0);
                
                /* Register the arrival wormhole at this starting position */
                players[i].state.jump_arrival = (NetPoint){players[i].state.s1, players[i].state.s2, players[i].state.s3, 1};
                players[i].state.wormhole.active = 0;
            }

            /* Move ship during emerging phase (300 -> 180) */
            if (players[i].nav_timer < 300 && players[i].nav_timer > 180) {
                double r_h = players[i].state.van_h * M_PI / 180.0;
                double r_m = players[i].state.van_m * M_PI / 180.0;
                double f_dx = sin(r_h) * cos(r_m);
                double f_dy = -cos(r_h) * cos(r_m);
                double f_dz = sin(r_m);
                
                /* Travel 4 units in 120 ticks = 0.0333 per tick */
                players[i].gx += f_dx * (4.0 / 120.0);
                players[i].gy += f_dy * (4.0 / 120.0);
                players[i].gz += f_dz * (4.0 / 120.0);
                
                players[i].state.q1 = get_q_from_g(players[i].gx);
                players[i].state.q2 = get_q_from_g(players[i].gy);
                players[i].state.q3 = get_q_from_g(players[i].gz);
                players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * 10.0);
                players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * 10.0);
                players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * 10.0);
            }

            if (players[i].nav_timer == 180) {
                /* Snap to final target destination precisely */
                players[i].gx = players[i].target_gx;
                players[i].gy = players[i].target_gy;
                players[i].gz = players[i].target_gz;
                players[i].state.q1 = get_q_from_g(players[i].gx);
                players[i].state.q2 = get_q_from_g(players[i].gy);
                players[i].state.q3 = get_q_from_g(players[i].gz);
                players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * 10.0);
                players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * 10.0);
                players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * 10.0);
                send_server_msg(i, "HELMSMAN", "Wormhole stabilized in target sector. Maintaining hull integrity.");
            }

            if (players[i].nav_timer <= 150) { 
                players[i].nav_state = NAV_STATE_IDLE;
                players[i].state.wormhole.active = 0;
                players[i].state.jump_arrival.active = 0;
                send_server_msg(i, "HELMSMAN", "Wormhole traversal successful. Welcome to destination.");
            }
        } else if (players[i].nav_state == NAV_STATE_CHASE) {
            int tid = players[i].state.lock_target;
            double tx=0, ty=0, tz=0; bool found=false;
            if (tid >= 1 && tid <= 32) { if (players[tid-1].active) { tx=players[tid-1].gx; ty=players[tid-1].gy; tz=players[tid-1].gz; found=true; } }
            else if (tid >= 1000 && tid < 1000+MAX_NPC) { if (npcs[tid-1000].active) { tx=npcs[tid-1000].gx; ty=npcs[tid-1000].gy; tz=npcs[tid-1000].gz; found=true; } }
            else if (tid >= 10000 && tid < 10000+MAX_COMETS) { if (comets[tid-10000].active) { tx = (comets[tid-10000].q1-1)*10.0 + comets[tid-10000].x; ty = (comets[tid-10000].q2-1)*10.0 + comets[tid-10000].y; tz = (comets[tid-10000].q3-1)*10.0 + comets[tid-10000].z; found=true; } }
            else if (tid >= 18000 && tid < 18000+MAX_MONSTERS) { if (monsters[tid-18000].active) { tx = (monsters[tid-18000].q1-1)*10.0 + monsters[tid-18000].x; ty = (monsters[tid-18000].q2-1)*10.0 + monsters[tid-18000].y; tz = (monsters[tid-18000].q3-1)*10.0 + monsters[tid-18000].z; found=true; } }
            else if (tid >= 19000 && tid < 19200) { int p_idx=(tid-19000)/3; int pr_idx=(tid-19000)%3; if (p_idx < MAX_CLIENTS && players[p_idx].state.probes[pr_idx].active) { tx=players[p_idx].state.probes[pr_idx].gx; ty=players[p_idx].state.probes[pr_idx].gy; tz=players[p_idx].state.probes[pr_idx].gz; found=true; } }
            
            if (found) {
                double engine_mult = 0.5 + (players[i].state.power_dist[0] * 1.0);
                double dx = tx - players[i].gx, dy = ty - players[i].gy, dz = tz - players[i].gz;
                double dist = sqrt(dx*dx + dy*dy + dz*dz);
                double target_dist = 2.0; /* Default chase distance */
                if (tid >= 1000) target_dist = (npcs[tid-1000].health > 500) ? 3.0 : 1.5;

                if (dist > target_dist + 0.1) {
                    players[i].dx = dx/dist; players[i].dy = dy/dist; players[i].dz = dz/dist;
                    players[i].gx += players[i].dx * 0.05 * engine_mult; 
                    players[i].gy += players[i].dy * 0.05 * engine_mult; 
                    players[i].gz += players[i].dz * 0.05 * engine_mult;
                } else if (dist < target_dist - 0.1) {
                    players[i].dx = -dx/dist; players[i].dy = -dy/dist; players[i].dz = -dz/dist;
                    players[i].gx += players[i].dx * 0.03 * engine_mult; 
                    players[i].gy += players[i].dy * 0.03 * engine_mult; 
                    players[i].gz += players[i].dz * 0.03 * engine_mult;
                }
                players[i].state.van_h = atan2(dx, -dy) * 180.0 / M_PI; if(players[i].state.van_h < 0) players[i].state.van_h += 360;
                players[i].state.van_m = asin(dz/dist) * 180.0 / M_PI;
            } else { players[i].nav_state = NAV_STATE_IDLE; send_server_msg(i, "COMPUTER", "Chase target lost."); }
            players[i].state.q1 = get_q_from_g(players[i].gx); players[i].state.q2 = get_q_from_g(players[i].gy); players[i].state.q3 = get_q_from_g(players[i].gz);
            players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * 10.0); players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * 10.0); players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * 10.0);
            players[i].state.energy -= 2;
        } else if (players[i].nav_state == NAV_STATE_DOCKING) {
            players[i].nav_timer--;
            int b_idx = players[i].pending_bor_target - 2000;
            if (b_idx < 0 || b_idx >= MAX_BASES || !bases[b_idx].active) {
                players[i].nav_state = NAV_STATE_IDLE; send_server_msg(i, "STARBASE", "Docking aborted: Starbase link lost.");
            } else {
                double d=sqrt(pow(bases[b_idx].x-players[i].state.s1,2)+pow(bases[b_idx].y-players[i].state.s2,2)+pow(bases[b_idx].z-players[i].state.s3,2));
                if (d > 3.5) { players[i].nav_state = NAV_STATE_IDLE; send_server_msg(i, "STARBASE", "Docking aborted: Out of range."); }
            }
            if (players[i].nav_state == NAV_STATE_DOCKING && players[i].nav_timer <= 0) {
                players[i].state.energy = 9999999; players[i].state.torpedoes = 1000;
                players[i].state.hull_integrity = 100.0; players[i].state.life_support = 100.0;
                for(int s=0; s<10; s++) players[i].state.system_health[s] = 100.0;
                players[i].is_docked = 1; players[i].nav_state = NAV_STATE_IDLE;
                send_server_msg(i, "STARBASE", "Docking complete. Systems replenished and repaired.");
            }
        } else if (players[i].nav_state == NAV_STATE_DRIFT) {
            players[i].gx += players[i].dx * players[i].hyper_speed;
            players[i].gy += players[i].dy * players[i].hyper_speed;
            players[i].gz += players[i].dz * players[i].hyper_speed;
            players[i].hyper_speed *= 0.995; /* Slow down gradually */
            if (players[i].hyper_speed < 0.001) { players[i].hyper_speed = 0; players[i].nav_state = NAV_STATE_IDLE; }
            players[i].state.q1 = get_q_from_g(players[i].gx); players[i].state.q2 = get_q_from_g(players[i].gy); players[i].state.q3 = get_q_from_g(players[i].gz);
            players[i].state.s1 = (players[i].gx - (players[i].state.q1 - 1) * 10.0); players[i].state.s2 = (players[i].gy - (players[i].state.q2 - 1) * 10.0); players[i].state.s3 = (players[i].gz - (players[i].state.q3 - 1) * 10.0);
        }

        if (players[i].torp_active) {
            if (players[i].torp_target > 0) {
                double target_x = -1.0; 
                double target_y = -1.0; 
                double target_z = -1.0;
                int tid = players[i].torp_target; 
                int pq1 = players[i].state.q1;
                if (tid <= 32 && players[tid-1].active && players[tid-1].state.q1 == pq1) { 
                    target_x = players[tid-1].state.s1; 
                    target_y = players[tid-1].state.s2; 
                    target_z = players[tid-1].state.s3; 
                } else if (tid >= 1000 && tid < 1000+MAX_NPC && npcs[tid-1000].active && npcs[tid-1000].q1 == pq1) { 
                    target_x = npcs[tid-1000].x; 
                    target_y = npcs[tid-1000].y; 
                    target_z = npcs[tid-1000].z; 
                } else if (tid >= 10000 && tid < 10000+MAX_COMETS && comets[tid-10000].active && comets[tid-10000].q1 == pq1) { 
                    target_x = comets[tid-10000].x; 
                    target_y = comets[tid-10000].y; 
                    target_z = comets[tid-10000].z; 
                } else if (tid >= 18000 && tid < 18000+MAX_MONSTERS && monsters[tid-18000].active && monsters[tid-18000].q1 == pq1) { 
                    target_x = monsters[tid-18000].x; 
                    target_y = monsters[tid-18000].y; 
                    target_z = monsters[tid-18000].z; 
                }
                if (target_x != -1.0) {
                    double dx = target_x - players[i].tx; 
                    double dy = target_y - players[i].ty; 
                    double dz = target_z - players[i].tz;
                    double d = sqrt(dx * dx + dy * dy + dz * dz);
                    if (d > 0.01) {
                        double sensor_h = players[i].state.system_health[2] / 100.0; 
                        double factor = 0.1 + (sensor_h * 0.3);
                        players[i].tdx = (players[i].tdx * (1.0 - factor)) + ((dx / d) * factor); 
                        players[i].tdy = (players[i].tdy * (1.0 - factor)) + ((dy / d) * factor); 
                        players[i].tdz = (players[i].tdz * (1.0 - factor)) + ((dz / d) * factor);
                        double s = sqrt(players[i].tdx * players[i].tdx + players[i].tdy * players[i].tdy + players[i].tdz * players[i].tdz);
                        players[i].tdx /= s; 
                        players[i].tdy /= s; 
                        players[i].tdz /= s;
                    }
                }
            }
            players[i].tx += players[i].tdx * 0.45; 
            players[i].ty += players[i].tdy * 0.45; 
            players[i].tz += players[i].tdz * 0.45;
            players[i].state.torp = (NetPoint){players[i].tx, players[i].ty, players[i].tz, 1};
            bool hit = false; 
            int tid = players[i].torp_target;
            int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;

            if (tid == 0) {
                /* Boresight / Manual Mode: Check for collisions with all objects in quadrant */
                QuadrantIndex *lq = &spatial_index[pq1][pq2][pq3];
                /* 1. Check NPC Ships */
                for (int n = 0; n < lq->npc_count; n++) {
                    NPCShip *npc = lq->npcs[n];
                    if (npc->active) {
                        double d = sqrt(pow(players[i].tx - npc->x, 2) + pow(players[i].ty - npc->y, 2) + pow(players[i].tz - npc->z, 2));
                        if (d < 0.8) { tid = npc->id + 1000; break; }
                    }
                }
                /* 2. Check Other Players */
                if (tid == 0) {
                    for (int j = 0; j < lq->player_count; j++) {
                        ConnectedPlayer *p = lq->players[j];
                        if (p != &players[i] && p->active) {
                            double d = sqrt(pow(players[i].tx - p->state.s1, 2) + pow(players[i].ty - p->state.s2, 2) + pow(players[i].tz - p->state.s3, 2));
                            if (d < 0.8) { tid = (int)(p - players) + 1; break; }
                        }
                    }
                }
            }

            if (tid > 0) {
                if (tid <= 32) {
                    ConnectedPlayer *p = &players[tid-1];
                    if (p->active && p->state.q1 == pq1) {
                        double d = sqrt(pow(players[i].tx - p->state.s1, 2) + pow(players[i].ty - p->state.s2, 2) + pow(players[i].tz - p->state.s3, 2));
                        if (d < 0.8) { 
                            hit = true; 
                            p->state.energy -= 20000; 
                            apply_hull_damage(tid - 1, 15.0);
                            p->state.boom = (NetPoint){players[i].tx, players[i].ty, players[i].tz, 1}; 
                            players[i].state.boom = (NetPoint){players[i].tx, players[i].ty, players[i].tz, 1}; 
                            if(p->state.energy <= 0) {
                                p->death_timer = 30;
                            }
                        }
                    }
                } else if (tid >= 1000 && tid < 1000+MAX_NPC) {
                    NPCShip *npc = &npcs[tid-1000];
                    if (npc->active && npc->q1 == pq1) {
                        double d = sqrt(pow(players[i].tx - npc->x, 2) + pow(players[i].ty - npc->y, 2) + pow(players[i].tz - npc->z, 2));
                        if (d < 0.8) { 
                            hit = true; 
                            
                            /* 1. Precision Multiplier */
                            double precision_mult = (d < 0.2) ? 1.2 : (d < 0.5) ? 1.0 : 0.7;
                            
                            /* 2. Faction Resistance Multiplier */
                            double faction_mult = 1.0;
                            if (npc->faction == FACTION_SWARM || npc->faction == FACTION_SPECIES_8472) faction_mult = 0.6; /* Bio-armor */
                            else if (npc->faction == FACTION_GILDED || npc->faction == FACTION_GORN) faction_mult = 1.4; /* Fragile sculls */
                            
                            /* 3. Base Damage calculation (Using config standard) */
                            int total_dmg = (int)(DMG_TORPEDO * precision_mult * faction_mult);
                            
                            /* 4. Two-tier damage model (Plating -> Health) */
                            if (npc->plating > 0) {
                                if (npc->plating >= total_dmg) {
                                    npc->plating -= total_dmg;
                                    total_dmg = 0;
                                } else {
                                    total_dmg -= npc->plating;
                                    npc->plating = 0;
                                }
                            }
                            
                            /* Remaining damage goes to hull health (Scaled: 100 plating points ~ 1 health point) */
                            if (total_dmg > 0) {
                                npc->health -= (total_dmg / 100);
                            }
                            
                            /* 5. Systemic Engine Damage */
                            npc->engine_health -= (10.0 + (rand()%11)); /* 10-20% engine damage */
                            if (npc->engine_health < 0) npc->engine_health = 0;

                            players[i].state.boom = (NetPoint){players[i].tx, players[i].ty, players[i].tz, 1}; 
                            if(npc->health <= 0) {
                                npc->death_timer = 30;
                            }
                        }
                    }
                }
            }
            if (players[i].torp_timeout > 0) {
                players[i].torp_timeout--;
            }
            if (hit || players[i].tx < 0 || players[i].tx > 10 || players[i].ty < 0 || players[i].ty > 10 || players[i].tz < 0 || players[i].tz > 10 || players[i].torp_timeout <= 0) { 
                players[i].torp_active = false; 
                players[i].state.torp.active = 0; 
            }
        }
    }

    rebuild_spatial_index();
    pthread_mutex_unlock(&game_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].socket == 0 || !players[i].active) {
            continue;
        }
        PacketUpdate upd; 
        memset(&upd, 0, sizeof(PacketUpdate)); 
        upd.type = PKT_UPDATE;
        upd.q1 = players[i].state.q1; 
        upd.q2 = players[i].state.q2; 
        upd.q3 = players[i].state.q3;
        upd.s1 = players[i].state.s1; 
        upd.s2 = players[i].state.s2; 
        upd.s3 = players[i].state.s3;
        upd.van_h = players[i].state.van_h; 
        upd.van_m = players[i].state.van_m;
        upd.energy = players[i].state.energy; 
        upd.hull_integrity = players[i].state.hull_integrity;
        upd.torpedoes = players[i].state.torpedoes; 
        upd.cargo_energy = players[i].state.cargo_energy; 
        upd.cargo_torpedoes = players[i].state.cargo_torpedoes;
        upd.crew_count = players[i].state.crew_count; 
        upd.prison_unit = players[i].state.prison_unit;
        for(int s=0; s<6; s++) {
            upd.shields[s] = players[i].state.shields[s];
        }
        for(int inv=0; inv<10; inv++) {
            upd.inventory[inv] = players[i].state.inventory[inv];
        }
        for(int sys=0; sys<10; sys++) {
            upd.system_health[sys] = players[i].state.system_health[sys];
        }
        for(int p=0; p<3; p++) {
            upd.power_dist[p] = players[i].state.power_dist[p];
        }
        upd.life_support = players[i].state.life_support; 
        upd.anti_matter_count = players[i].state.anti_matter_count;
        upd.lock_target = players[i].state.lock_target; 
        upd.tube_state = players[i].state.tube_state;
        for(int t=0; t<4; t++) {
            upd.tube_load_timers[t] = players[i].tube_load_timers[t];
        }
        upd.current_tube = players[i].current_tube; 
        upd.ion_beam_charge = players[i].state.ion_beam_charge;
        upd.is_cloaked = players[i].state.is_cloaked; 
        upd.is_docked = players[i].is_docked;
        upd.red_alert = players[i].state.red_alert;
        upd.nav_state = (uint8_t)players[i].nav_state;
        upd.show_axes = players[i].state.show_axes; 
        upd.show_grid = players[i].state.show_grid;
        upd.show_bridge = players[i].state.show_bridge; 
        upd.show_map = players[i].state.show_map; 
        upd.map_filter = players[i].state.map_filter;
        
        int o_idx = 0;
        upd.objects[o_idx] = (NetObject){players[i].state.s1, players[i].state.s2, players[i].state.s3, players[i].state.van_h, players[i].state.van_m, 1, players[i].ship_class, 1, (int)players[i].state.hull_integrity, players[i].state.energy, 0, (int)players[i].state.hull_integrity, players[i].faction, i+1, players[i].state.is_cloaked, ""};
        strncpy(upd.objects[o_idx].name, players[i].name, 63);
        upd.objects[o_idx].name[63] = '\0';
        o_idx++;

        QuadrantIndex *lq = &spatial_index[upd.q1][upd.q2][upd.q3];
        
        /* 1. NPC Ships */
        for(int n=0; n<lq->npc_count && o_idx < MAX_NET_OBJECTS; n++) {
            NPCShip *npc = lq->npcs[n]; if (!npc->active) continue;
            upd.objects[o_idx] = (NetObject){npc->x, npc->y, npc->z, npc->h, npc->m, npc->faction, npc->ship_class, 1, (int)(npc->health / 10), npc->energy, npc->plating, (int)(npc->health / 10), npc->faction, npc->id + 1000, npc->is_cloaked, ""};
            snprintf(upd.objects[o_idx].name, 64, "%s", npc->name);
            o_idx++;
        }
        
        /* 2. Other Players */
        for(int j=0; j<lq->player_count && o_idx < MAX_NET_OBJECTS; j++) {
            ConnectedPlayer *p = lq->players[j]; if (p == &players[i] || !p->active) continue;
            upd.objects[o_idx] = (NetObject){p->state.s1, p->state.s2, p->state.s3, p->state.van_h, p->state.van_m, 1, p->ship_class, 1, (int)p->state.hull_integrity, p->state.energy, 0, (int)p->state.hull_integrity, p->faction, (int)(p - players) + 1, p->state.is_cloaked, ""};
            snprintf(upd.objects[o_idx].name, 64, "%s", p->name);
            o_idx++;
        }

        /* 3. Static Celestial Entities */
        for(int s=0; s<lq->star_count && o_idx < MAX_NET_OBJECTS; s++) {
            NPCStar *st = lq->stars[s]; if(!st->active) continue;
            upd.objects[o_idx++] = (NetObject){st->x, st->y, st->z, 0, 0, 4, 0, 1, 100, 0, 0, 100, 4, st->id + 4000, 0, "Star"};
        }
        for(int p=0; p<lq->planet_count && o_idx < MAX_NET_OBJECTS; p++) {
            NPCPlanet *pl = lq->planets[p]; if(!pl->active) continue;
            upd.objects[o_idx++] = (NetObject){pl->x, pl->y, pl->z, 0, 0, 5, pl->resource_type, 1, 100, pl->amount, 0, 100, 5, pl->id + 3000, 0, "Planet"};
        }
        for(int b=0; b<lq->base_count && o_idx < MAX_NET_OBJECTS; b++) {
            NPCBase *ba = lq->bases[b]; if(!ba->active) continue;
            upd.objects[o_idx++] = (NetObject){ba->x, ba->y, ba->z, 0, 0, 3, 0, 1, (int)(ba->health/50), 0, 0, (int)(ba->health/50), 0, ba->id + 2000, 0, "Starbase"};
        }
        for(int h=0; h<lq->bh_count && o_idx < MAX_NET_OBJECTS; h++) {
            NPCBlackHole *bh = lq->black_holes[h]; if(!bh->active) continue;
            upd.objects[o_idx++] = (NetObject){bh->x, bh->y, bh->z, 0, 0, 6, 0, 1, 100, 0, 0, 100, 6, bh->id + 7000, 0, "Black Hole"};
        }
        for(int n=0; n<lq->nebula_count && o_idx < MAX_NET_OBJECTS; n++) {
            NPCNebula *nb = lq->nebulas[n]; if(!nb->active) continue;
            upd.objects[o_idx++] = (NetObject){nb->x, nb->y, nb->z, 0, 0, 7, nb->type, 1, 100, 0, 0, 100, 7, nb->id + 8000, 0, "Nebula"};
        }
        for(int p=0; p<lq->pulsar_count && o_idx < MAX_NET_OBJECTS; p++) {
            NPCPulsar *pu = lq->pulsars[p]; if(!pu->active) continue;
            upd.objects[o_idx++] = (NetObject){pu->x, pu->y, pu->z, 0, 0, 8, 0, 1, 100, 0, 0, 100, 8, pu->id + 9000, 0, "Pulsar"};
        }
        for(int c=0; c<lq->comet_count && o_idx < MAX_NET_OBJECTS; c++) {
            NPCComet *co = lq->comets[c]; if(!co->active) continue;
            upd.objects[o_idx++] = (NetObject){co->x, co->y, co->z, 0, 0, 9, 0, 1, 100, 0, 0, 100, 9, co->id + 10000, 0, "Comet"};
        }
        for(int d=0; d<lq->derelict_count && o_idx < MAX_NET_OBJECTS; d++) {
            NPCDerelict *de = lq->derelicts[d]; if(!de->active) continue;
            upd.objects[o_idx] = (NetObject){de->x, de->y, de->z, 0, 0, 22, de->ship_class, 1, 100, 0, 0, 100, de->faction, de->id + 11000, 0, ""};
            snprintf(upd.objects[o_idx].name, 64, "%s", de->name);
            o_idx++;
        }
        for(int a=0; a<lq->asteroid_count && o_idx < MAX_NET_OBJECTS; a++) {
            NPCAsteroid *as = lq->asteroids[a]; if(!as->active) continue;
            upd.objects[o_idx++] = (NetObject){as->x, as->y, as->z, 0, 0, 21, as->resource_type, 1, 100, as->amount, (int)(as->size * 100), 100, 21, as->id + 12000, 0, "Asteroid"};
        }
        for(int m=0; m<lq->mine_count && o_idx < MAX_NET_OBJECTS; m++) {
            NPCMine *mi = lq->mines[m]; if(!mi->active) continue;
            upd.objects[o_idx++] = (NetObject){mi->x, mi->y, mi->z, 0, 0, 23, 0, 1, 100, 0, 0, 100, 23, mi->id + 14000, 0, "Mine"};
        }
        for(int b=0; b<lq->buoy_count && o_idx < MAX_NET_OBJECTS; b++) {
            NPCBuoy *bu = lq->buoys[b]; if(!bu->active) continue;
            upd.objects[o_idx++] = (NetObject){bu->x, bu->y, bu->z, 0, 0, 24, 0, 1, 100, 0, 0, 100, 24, bu->id + 15000, 0, "Comm Buoy"};
        }
        for(int p=0; p<lq->platform_count && o_idx < MAX_NET_OBJECTS; p++) {
            NPCPlatform *pl = lq->platforms[p]; if(!pl->active) continue;
            upd.objects[o_idx++] = (NetObject){pl->x, pl->y, pl->z, 0, 0, 25, 0, 1, (int)(pl->health/50), 0, 0, (int)(pl->health/50), pl->faction, pl->id + 16000, 0, "Defense Platform"};
        }
        for(int r=0; r<lq->rift_count && o_idx < MAX_NET_OBJECTS; r++) {
            NPCRift *ri = lq->rifts[r]; if(!ri->active) continue;
            upd.objects[o_idx++] = (NetObject){ri->x, ri->y, ri->z, 0, 0, 26, 0, 1, 100, 0, 0, 100, 26, ri->id + 17000, 0, "Spatial Rift"};
        }
        for(int m=0; m<lq->monster_count && o_idx < MAX_NET_OBJECTS; m++) {
            NPCMonster *mo = lq->monsters[m]; if(!mo->active) continue;
            upd.objects[o_idx] = (NetObject){mo->x, mo->y, mo->z, 0, 0, mo->type, 0, 1, (int)(mo->health/1000), 0, 0, (int)(mo->health/1000), 30, mo->id + 18000, 0, ""};
            snprintf(upd.objects[o_idx].name, 64, "%s", (mo->type==30)?"Crystalline Entity":"Space Amoeba");
            o_idx++;
        }

        upd.object_count = o_idx;
        upd.torp = players[i].state.torp; 
        upd.boom = players[i].state.boom;
        upd.wormhole = players[i].state.wormhole; 
        upd.jump_arrival = players[i].state.jump_arrival;
        upd.dismantle = players[i].state.dismantle;
        upd.recovery_fx = players[i].state.recovery_fx;
        
        for(int p=0; p<3; p++) {
            upd.probes[p] = players[i].state.probes[p];
        }
        upd.beam_count = players[i].state.beam_count; 
        for(int b=0; b<upd.beam_count && b<MAX_NET_BEAMS; b++) {
            upd.beams[b] = players[i].state.beams[b];
        }

        if (supernova_event.supernova_timer > 0) {
            upd.map_update_q[0] = supernova_event.supernova_q1; 
            upd.map_update_q[1] = supernova_event.supernova_q2; 
            upd.map_update_q[2] = supernova_event.supernova_q3;
            upd.map_update_val = -supernova_event.supernova_timer;
        } else {
            upd.map_update_q[0] = upd.q1; 
            upd.map_update_q[1] = upd.q2; 
            upd.map_update_q[2] = upd.q3;
            upd.map_update_val = spacegl_master.g[upd.q1][upd.q2][upd.q3];
        }
        int rq1 = rand() % 10 + 1; 
        int rq2 = rand() % 10 + 1; 
        int rq3 = rand() % 10 + 1;
        upd.map_update_q2[0] = rq1; 
        upd.map_update_q2[1] = rq2; 
        upd.map_update_q2[2] = rq3;
        upd.map_update_val2 = spacegl_master.g[rq1][rq2][rq3];

        send_optimized_update(i, &upd);

        players[i].state.beam_count = 0; 
        players[i].state.boom.active = 0;
        players[i].state.dismantle.active = 0;
        players[i].state.recovery_fx.active = 0;
    }
}

void apply_hull_damage(int i, double amount) {
    if (i < 0 || i >= MAX_CLIENTS || !players[i].active) return;
    
    players[i].state.hull_integrity -= amount;
    if (players[i].state.hull_integrity < 0) players[i].state.hull_integrity = 0;
    
    /* Random system damage (1-5% per hit) */
    int sys = rand() % 10;
    double sys_dmg = 1.0 + (rand() % 400) / 100.0;
    players[i].state.system_health[sys] -= sys_dmg;
    if (players[i].state.system_health[sys] < 0) players[i].state.system_health[sys] = 0;
    
    const char* sys_names[] = {"Hyperdrive", "Impulse", "Sensors", "Transporters", "Ion Beams", "Torpedoes", "Computer", "Life Support", "Shields", "Auxiliary"};
    char msg[128];
    sprintf(msg, "Hull impact! System %s damaged by %.1f%%.", sys_names[sys], sys_dmg);
    send_server_msg(i, "DAMAGE", msg);
}
