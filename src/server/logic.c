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
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include <sys/socket.h>
#include "server_internal.h"

bool is_player_in_nebula(int i) {
    if (!players[i].active) return false;
    QuadrantIndex *q = &spatial_index[players[i].state.q1][players[i].state.q2][players[i].state.q3];
    for (int n = 0; n < q->nebula_count; n++) {
        double d = sqrt(pow(players[i].state.s1 - q->nebulas[n]->x, 2) + 
                        pow(players[i].state.s2 - q->nebulas[n]->y, 2) + 
                        pow(players[i].state.s3 - q->nebulas[n]->z, 2));
        if (d < DIST_NEBULA_EFFECT) return true;
    }
    return false;
}

/* --- Modular AI Controller --- */

void update_npc_ai(int n) {
    if (!npcs[n].active) return;

    /* Sync absolute if first time */
    if (npcs[n].gx <= 0.001 && npcs[n].gy <= 0.001) {
        npcs[n].gx = (npcs[n].q1-1)*10.0 + npcs[n].x;
        npcs[n].gy = (npcs[n].q2-1)*10.0 + npcs[n].y;
        npcs[n].gz = (npcs[n].q3-1)*10.0 + npcs[n].z;
    }

    int q1 = npcs[n].q1, q2 = npcs[n].q2, q3 = npcs[n].q3;
    if (!IS_Q_VALID(q1, q2, q3)) return;
    QuadrantIndex *local_q = &spatial_index[q1][q2][q3];
    
    int closest_p = -1; double min_d2 = 100.0;
    for (int j = 0; j < local_q->player_count; j++) {
        ConnectedPlayer *p = local_q->players[j]; if (p->state.is_cloaked) continue;
        
        /* Faction/Reputation Logic: NPCs only attack if:
         * 1. Player is from a different faction
         * 2. Player is from SAME faction but is currently marked as RENEGADE/TRAITOR
         */
        if (p->faction == npcs[n].faction && p->renegade_timer <= 0) continue;

        double d2 = pow(npcs[n].gx - p->gx, 2) + pow(npcs[n].gy - p->gy, 2) + pow(npcs[n].gz - p->gz, 2);
        if (d2 < min_d2) { min_d2 = d2; closest_p = (int)(p - players); }
    }
    
    /* State Machine Logic */
    if (npcs[n].energy < 200) npcs[n].ai_state = AI_STATE_FLEE;
    else if (closest_p != -1) {
        /* Transition from legacy/patrol to attack run */
        if (npcs[n].ai_state == AI_STATE_PATROL || npcs[n].ai_state == AI_STATE_CHASE) {
            npcs[n].ai_state = AI_STATE_ATTACK_RUN;
            npcs[n].nav_timer = 0; /* Force immediate target pick */
        }
    } else {
        npcs[n].ai_state = AI_STATE_PATROL;
    }

    /* Xylari Cloak Logic */
    if (npcs[n].faction == FACTION_XYLARI) {
        if (closest_p == -1) npcs[n].is_cloaked = 1; /* Stalking/Patrolling */
        else if (npcs[n].ai_state == AI_STATE_FLEE) npcs[n].is_cloaked = 1;
        else npcs[n].is_cloaked = 0; /* Reveal to attack */
    } else {
        npcs[n].is_cloaked = 0;
    }

    double d_dx = 0, d_dy = 0, d_dz = 0, speed = 0.03;
    if (npcs[n].engine_health < 10.0f) speed = 0; else speed *= (npcs[n].engine_health/100.0f);

    if (npcs[n].ai_state == AI_STATE_ATTACK_RUN && closest_p != -1) {
        /* 1. Pick a random destination in the quadrant if timer expired or first run */
        if (npcs[n].nav_timer <= 0) {
            npcs[n].tx = (double)(rand() % 100) / 10.0;
            npcs[n].ty = (double)(rand() % 100) / 10.0;
            npcs[n].tz = (double)(rand() % 100) / 10.0;
            /* Convert to absolute global coordinates */
            npcs[n].tx += (npcs[n].q1 - 1) * 10.0;
            npcs[n].ty += (npcs[n].q2 - 1) * 10.0;
            npcs[n].tz += (npcs[n].q3 - 1) * 10.0;
            npcs[n].nav_timer = 3000; /* Timeout failsafe */
        }

        /* 2. Move towards target */
        double dx = npcs[n].tx - npcs[n].gx;
        double dy = npcs[n].ty - npcs[n].gy;
        double dz = npcs[n].tz - npcs[n].gz;
        double dist = sqrt(dx*dx + dy*dy + dz*dz);

        if (dist > 0.5) {
            /* Still moving */
            d_dx = dx / dist; d_dy = dy / dist; d_dz = dz / dist;
            /* Face movement direction */
            npcs[n].h = atan2(d_dx, -d_dy) * 180.0 / M_PI; if(npcs[n].h<0) npcs[n].h+=360;
            npcs[n].m = asin(d_dz) * 180.0 / M_PI;
        } else {
            /* Arrived! Switch to engagement */
            npcs[n].ai_state = AI_STATE_ATTACK_POSITION;
            npcs[n].nav_timer = 120; /* Wait 4 seconds (30 ticks/s * 4) */
        }
        
    } else if (npcs[n].ai_state == AI_STATE_ATTACK_POSITION && closest_p != -1) {
        /* Hold Position and Fire */
        speed = 0.0; /* Stop engines */
        
        ConnectedPlayer *target = &players[closest_p];
        double dx = target->gx - npcs[n].gx;
        double dy = target->gy - npcs[n].gy;
        double dz = target->gz - npcs[n].gz;
        double dist_to_player = sqrt(dx*dx + dy*dy + dz*dz);

        /* Turn bow towards player */
        if (dist_to_player > 0.01) {
            npcs[n].h = atan2(dx, -dy) * 180.0 / M_PI; if(npcs[n].h<0) npcs[n].h+=360;
            npcs[n].m = asin(dz / dist_to_player) * 180.0 / M_PI;
        }

        /* Fire Logic */
        if (npcs[n].fire_cooldown > 0) npcs[n].fire_cooldown--;
        if (npcs[n].fire_cooldown <= 0 && dist_to_player < 8.0) {
             /* Beam FX logic mapped to player state for transmission */
            players[closest_p].state.beam_count = 1; 
            players[closest_p].state.beams[0] = (NetBeam){(float)npcs[n].x, (float)npcs[n].y, (float)npcs[n].z, (float)target->state.s1, (float)target->state.s2, (float)target->state.s3, 1};
            
            /* Damage Calculation */
            float base_dmg = DMG_ION_BEAM_BASE;
            if (npcs[n].faction == FACTION_SWARM) base_dmg = 8000.0f;
            else if (npcs[n].faction == FACTION_KORTHIAN) base_dmg = 2500.0f;
            else if (npcs[n].faction == FACTION_XYLARI) base_dmg = 3500.0f;
            
            float dist_val = (dist_to_player > 0.1) ? (float)dist_to_player : 0.1f;
            float dist_factor = 1.5f / dist_val; if (dist_factor > 1.0f) dist_factor = 1.0f;
            int dmg = (int)(base_dmg * dist_factor);

            /* Directional Shield Damage Logic (Standardized) */
            double rel_dx = npcs[n].x - target->state.s1;
            double rel_dy = npcs[n].y - target->state.s2;
            double angle = atan2(rel_dx, -rel_dy) * 180.0 / M_PI; if (angle < 0) angle += 360;
            double rel_angle = angle - target->state.van_h;
            while (rel_angle < 0) rel_angle += 360;
            while (rel_angle >= 360) rel_angle -= 360;
            
            /* 3D Shield Mapping: 0:F, 1:R, 2:T, 3:B, 4:L, 5:RI */
            double rel_dz = npcs[n].z - target->state.s3;
            double dist_2d = sqrt(pow(npcs[n].x - target->state.s1, 2) + pow(npcs[n].y - target->state.s2, 2));
            double vertical_angle = atan2(rel_dz, dist_2d) * 180.0 / M_PI;

            int s_idx = 0;
            if (vertical_angle > 45) s_idx = 2;      /* Top */
            else if (vertical_angle < -45) s_idx = 3; /* Bottom */
            else {
                if (rel_angle > 315 || rel_angle <= 45) s_idx = 0;
                else if (rel_angle > 45 && rel_angle <= 135) s_idx = 5;
                else if (rel_angle > 135 && rel_angle <= 225) s_idx = 1;
                else s_idx = 4;
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
            
            if (dmg_rem > 0 && target->state.composite_plating > 0) {
                if (target->state.composite_plating >= dmg_rem) {
                    target->state.composite_plating -= dmg_rem;
                    dmg_rem = 0;
                } else {
                    dmg_rem -= target->state.composite_plating;
                    target->state.composite_plating = 0;
                }
            }

            if (dmg_rem > 0) {
                float hull_dmg = dmg_rem / 1000.0f; /* 1000 dmg = 1% hull */
                target->state.hull_integrity -= hull_dmg;
                if (target->state.hull_integrity < 0) target->state.hull_integrity = 0;
                
                /* Internal System Damage Logic: Chance to hit a subsystem when shields are down */
                if (rand() % 100 < (15 + (int)(dmg_rem / 500))) {
                    int sys_idx = rand() % 10;
                    float sys_dmg = 5.0f + (rand() % 20);
                    target->state.system_health[sys_idx] -= sys_dmg;
                    if (target->state.system_health[sys_idx] < 0) target->state.system_health[sys_idx] = 0;
                    
                    const char* sys_names[] = {"HYPERDRIVE", "IMPULSE", "SENSORS", "TRANSPORTERS", "Ion BeamS", "TORPEDOES", "COMPUTER", "LIFE SUPPORT", "SHIELDS", "AUXILIARY"};
                    char alert[128];
                    sprintf(alert, "CRITICAL: Impact on bare hull! %s system damaged!", sys_names[sys_idx]);
                    send_server_msg(closest_p, "DAMAGE", alert);
                }

                /* Energy also takes some impact damage */
                target->state.energy -= dmg_rem / 2;
            }

            target->shield_regen_delay = 90;
            
            if (target->state.hull_integrity <= 0 || target->state.energy <= 0) {
                target->state.energy = 0; target->state.hull_integrity = 0; target->state.crew_count = 0; target->active = 0;
                target->state.boom = (NetPoint){(float)target->state.s1, (float)target->state.s2, (float)target->state.s3, 1};
            }
            
            npcs[n].fire_cooldown = (npcs[n].faction == FACTION_SWARM) ? 100 : 150;
        }

        /* Countdown to next move */
        npcs[n].nav_timer--;
        if (npcs[n].nav_timer <= 0) {
            npcs[n].ai_state = AI_STATE_ATTACK_RUN; /* Pick new position */
            npcs[n].nav_timer = 0;
        }

    } else if (npcs[n].ai_state == AI_STATE_FLEE && closest_p != -1) {
        double dx = npcs[n].gx - players[closest_p].gx, dy = npcs[n].gy - players[closest_p].gy, dz = npcs[n].gz - players[closest_p].gz;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        if (d > 0.1) { d_dx = dx/d; d_dy = dy/d; d_dz = dz/d; speed *= 1.8; }
        if (d > 8.5) npcs[n].ai_state = AI_STATE_PATROL; /* Safely away */
    } else {
        if (npcs[n].nav_timer-- <= 0) { 
            npcs[n].nav_timer = 100 + rand()%200; 
            double rx = (rand()%100-50)/100.0, ry = (rand()%100-50)/100.0, rz = (rand()%100-50)/100.0;
            double rl = sqrt(rx*rx + ry*ry + rz*rz);
            if (rl > 0.001) { npcs[n].dx = rx/rl; npcs[n].dy = ry/rl; npcs[n].dz = rz/rl; }
        }
        d_dx = npcs[n].dx; d_dy = npcs[n].dy; d_dz = npcs[n].dz;
    }
    
    /* Movement and Collision with Celestial Bodies */
    npcs[n].gx += d_dx * speed; npcs[n].gy += d_dy * speed; npcs[n].gz += d_dz * speed;
    
    /* Clamp to galaxy bounds */
    if (npcs[n].gx < 0.05) { npcs[n].gx = 0.05; }
    if (npcs[n].gx > 99.95) { npcs[n].gx = 99.95; }
    if (npcs[n].gy < 0.05) { npcs[n].gy = 0.05; }
    if (npcs[n].gy > 99.95) { npcs[n].gy = 99.95; }
    if (npcs[n].gz < 0.05) { npcs[n].gz = 0.05; }
    if (npcs[n].gz > 99.95) { npcs[n].gz = 99.95; }

    npcs[n].q1 = get_q_from_g(npcs[n].gx); npcs[n].q2 = get_q_from_g(npcs[n].gy); npcs[n].q3 = get_q_from_g(npcs[n].gz);
    npcs[n].x = npcs[n].gx - (npcs[n].q1 - 1) * 10.0; npcs[n].y = npcs[n].gy - (npcs[n].q2 - 1) * 10.0; npcs[n].z = npcs[n].gz - (npcs[n].q3 - 1) * 10.0;

    /* Enviromental Hazards */
    for(int h=0; h<local_q->bh_count; h++) {
        double d=sqrt(pow(local_q->black_holes[h]->x-npcs[n].x,2)+pow(local_q->black_holes[h]->y-npcs[n].y,2)+pow(local_q->black_holes[h]->z-npcs[n].z,2));
        if(d < 1.0) npcs[n].active = 0;
    }
}

void update_game_logic() {
    global_tick++;

    pthread_mutex_lock(&game_mutex);
    
    /* Phase 0: Map cleanup (Storms) */
    if (global_tick % 500 == 0) {
        for(int i=1; i<=10; i++) for(int j=1; j<=10; j++) for(int l=1; l<=10; l++) {
            long long val = spacegl_master.g[i][j][l];
            if (val > 0) {
                int has_storm = (val / 10000000LL) % 10;
                if (has_storm) spacegl_master.g[i][j][l] -= 10000000LL;
            }
        }
    }

    /* Phase 1: NPC Movement & AI */
    for (int n = 0; n < MAX_NPC; n++) update_npc_ai(n);

    /* Phase 1.2: Platform AI (Static Defense) */
    for (int pt = 0; pt < MAX_PLATFORMS; pt++) {
        if (!platforms[pt].active) continue;
        if (platforms[pt].fire_cooldown > 0) platforms[pt].fire_cooldown--;
        
        if (platforms[pt].fire_cooldown <= 0) {
            int q1 = platforms[pt].q1, q2 = platforms[pt].q2, q3 = platforms[pt].q3;
            QuadrantIndex *local_q = &spatial_index[q1][q2][q3];
            
            for (int j = 0; j < local_q->player_count; j++) {
                ConnectedPlayer *p = local_q->players[j];
                if (p->state.is_cloaked) continue;
                
                /* Faction Check for Platforms */
                if (p->faction == platforms[pt].faction && p->renegade_timer <= 0) continue;

                double dx = p->state.s1 - platforms[pt].x;
                double dy = p->state.s2 - platforms[pt].y;
                double dz = p->state.s3 - platforms[pt].z;
                double dist = sqrt(dx*dx + dy*dy + dz*dz);
                
                if (dist < 5.0) {
                    /* Fire! */
                    p->state.beam_count = 1;
                    p->state.beams[0] = (NetBeam){(float)platforms[pt].x, (float)platforms[pt].y, (float)platforms[pt].z, (float)p->state.s1, (float)p->state.s2, (float)p->state.s3, 1};
                    
                    /* Tactical Damage Logic */
                    int dmg = 2000; /* Platform Ion Beam base damage */
                    
                    /* Correct relative vector: Attacker position relative to Target */
                    double r_dx = platforms[pt].x - p->state.s1;
                    double r_dy = platforms[pt].y - p->state.s2;
                    double r_dz = platforms[pt].z - p->state.s3;
                    double dist_2d = sqrt(r_dx*r_dx + r_dy*r_dy);

                    double angle = atan2(r_dx, -r_dy) * 180.0 / M_PI; if (angle < 0) angle += 360;
                    double rel_angle = angle - p->state.van_h;
                    while (rel_angle < 0) rel_angle += 360;
                    while (rel_angle >= 360) rel_angle -= 360;

                    double vertical_angle = atan2(r_dz, dist_2d) * 180.0 / M_PI;
                    int s_idx = 0;
                    if (vertical_angle > 45) s_idx = 2;      /* Top */
                    else if (vertical_angle < -45) s_idx = 3; /* Bottom */
                    else {
                        if (rel_angle > 315 || rel_angle <= 45) s_idx = 0;
                        else if (rel_angle > 45 && rel_angle <= 135) s_idx = 5;
                        else if (rel_angle > 135 && rel_angle <= 225) s_idx = 1;
                        else s_idx = 4;
                    }

                    int dmg_rem = dmg;
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
                        float hull_dmg = dmg_rem / 1000.0f;
                        p->state.hull_integrity -= hull_dmg;
                        if (p->state.hull_integrity < 0) p->state.hull_integrity = 0;
                        
                        /* Internal System Damage */
                        if (rand() % 100 < 20) {
                            int sys_idx = rand() % 10;
                            p->state.system_health[sys_idx] -= (5.0f + (rand() % 15));
                            if (p->state.system_health[sys_idx] < 0) p->state.system_health[sys_idx] = 0;
                            send_server_msg(j, "DAMAGE", "Platform hit bypassed shields! System damage detected!");
                        }
                        p->state.energy -= dmg_rem / 2;
                    }

                    p->shield_regen_delay = 90;
                    if (p->state.hull_integrity <= 0 || p->state.energy <= 0) {
                        p->state.energy = 0; p->state.hull_integrity = 0; p->state.crew_count = 0; p->active = 0;
                        p->state.boom = (NetPoint){(float)p->state.s1, (float)p->state.s2, (float)p->state.s3, 1};
                    }

                    platforms[pt].fire_cooldown = 100; /* ~3.3 seconds */
                    send_server_msg(j, "WARNING", "UNDER ATTACK BY DEFENSE PLATFORM!");
                    break;
                }
            }
        }
    }

    /* Phase 1.5: Comet Orbital Movement */
    for (int c = 0; c < MAX_COMETS; c++) {
        if (!comets[c].active) continue;
        
        /* 1. Update orbital angle */
        comets[c].angle += comets[c].speed;
        if (comets[c].angle > 2*M_PI) comets[c].angle -= 2*M_PI;
        
        /* 2. Calculate position in orbital plane */
        double ox = comets[c].a * cos(comets[c].angle);
        double oy = comets[c].b * sin(comets[c].angle);
        
        /* 3. Rotate by inclination (simplified rotation around X-axis for variety) */
        double gx = comets[c].cx + ox;
        double gy = comets[c].cy + oy * cos(comets[c].inc);
        double gz = comets[c].cz + oy * sin(comets[c].inc);
        
        /* 4. Clamp to Galactic Bounds 0-100 */
        if (gx < 0) { gx = 0; } if (gx > 100) { gx = 100; }
        if (gy < 0) { gy = 0; } if (gy > 100) { gy = 100; }
        if (gz < 0) { gz = 0; } if (gz > 100) { gz = 100; }
        
        /* 5. Update local quadrant and sector */
        int nq1 = (int)(gx / 10.0) + 1; if(nq1>10) nq1=10; if(nq1<1) nq1=1;
        int nq2 = (int)(gy / 10.0) + 1; if(nq2>10) nq2=10; if(nq2<1) nq2=1;
        int nq3 = (int)(gz / 10.0) + 1; if(nq3>10) nq3=10; if(nq3<1) nq3=1;
        
        comets[c].q1 = nq1; comets[c].q2 = nq2; comets[c].q3 = nq3;
        comets[c].x = gx - (nq1-1)*10.0;
        comets[c].y = gy - (nq2-1)*10.0;
        comets[c].z = gz - (nq3-1)*10.0;
    }

    /* Phase 1.6: Supernova Event Logic */
    if (supernova_event.supernova_timer > 0) {
        supernova_event.supernova_timer--;
        
        int q1 = supernova_event.supernova_q1, q2 = supernova_event.supernova_q2, q3 = supernova_event.supernova_q3;
        /* Force negative value in galaxy grid to trigger red blinking on all client maps */
        spacegl_master.g[q1][q2][q3] = -supernova_event.supernova_timer;

        int sec = supernova_event.supernova_timer / 30;
        if (sec > 0 && (supernova_event.supernova_timer % 300 == 0 || (sec <= 10 && supernova_event.supernova_timer % 30 == 0))) {
            char msg[128];
            sprintf(msg, "!!! WARNING: SUPERNOVA IMMINENT IN Q-%d-%d-%d. T-MINUS %d SECONDS !!!", 
                    supernova_event.supernova_q1, supernova_event.supernova_q2, supernova_event.supernova_q3, sec);
            for(int i=0; i<MAX_CLIENTS; i++) if(players[i].active) send_server_msg(i, "SCIENCE", msg);
        }

        if (supernova_event.supernova_timer == 0) {
            /* KABOOM! Destroy everything in that quadrant */
            int q1 = supernova_event.supernova_q1, q2 = supernova_event.supernova_q2, q3 = supernova_event.supernova_q3;
            LOG_DEBUG("SUPERNOVA EXPLOSION in Q-%d-%d-%d\n", q1, q2, q3);

            /* Destroy the specific star */
            if (supernova_event.star_id >= 0 && supernova_event.star_id < MAX_STARS) {
                stars_data[supernova_event.star_id].active = 0;
            }
            
            /* Destroy Planets */
            for(int p=0; p<MAX_PLANETS; p++) if(planets[p].active && planets[p].q1 == q1 && planets[p].q2 == q2 && planets[p].q3 == q3) planets[p].active = 0;
            /* Destroy NPCs */
            for(int n=0; n<MAX_NPC; n++) if(npcs[n].active && npcs[n].q1 == q1 && npcs[n].q2 == q2 && npcs[n].q3 == q3) npcs[n].active = 0;
            /* Destroy Bases */
            for(int b=0; b<MAX_BASES; b++) if(bases[b].active && bases[b].q1 == q1 && bases[b].q2 == q2 && bases[b].q3 == q3) bases[b].active = 0;
            
            /* Destroy Players */
            for(int i=0; i<MAX_CLIENTS; i++) {
                if(players[i].active && players[i].state.q1 == q1 && players[i].state.q2 == q2 && players[i].state.q3 == q3) {
                    send_server_msg(i, "CRITICAL", "SUPERNOVA IMPACT. VESSEL VAPORIZED.");
                    players[i].state.energy = 0; players[i].state.crew_count = 0;
                    players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1};
                    players[i].active = 0;
                }
            }

            /* Convert to a Black Hole remnant in the galaxy map */
            spacegl_master.g[q1][q2][q3] = 10000; /* BPNBS: 1 Black Hole, 0 Planets, 0 Bases, 0 Stars */
            
            /* Create the physical Black Hole object in the quadrant */
            for(int bh=0; bh<MAX_BH; bh++) {
                if(!black_holes[bh].active) {
                    black_holes[bh].id = bh;
                    black_holes[bh].q1 = q1; black_holes[bh].q2 = q2; black_holes[bh].q3 = q3;
                    black_holes[bh].x = supernova_event.x;
                    black_holes[bh].y = supernova_event.y;
                    black_holes[bh].z = supernova_event.z;
                    black_holes[bh].active = 1;
                    break;
                }
            }

            supernova_event.supernova_timer = 0; /* EXPLICITLY CLEAR EVENT */
            rebuild_spatial_index();
            save_galaxy();
            
            /* Broadcaster: Force immediate map update for all players */
            for(int i=0; i<MAX_CLIENTS; i++) {
                if (players[i].active && players[i].socket != 0) {
                    send_server_msg(i, "SCIENCE", "SENSOR ALERT: Gravitational waves confirmed. Singularity detected at explosion epicenter.");
                }
            }
        }
    } else {
        /* Small chance to trigger a new supernova if none active */
        if (global_tick > 100 && supernova_event.supernova_timer <= 0 && (rand() % 9000 < 1)) {
            int rq1 = rand()%10+1, rq2 = rand()%10+1, rq3 = rand()%10+1;
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

    /* Phase 1.7: Monster AI Logic */

    /* Phase 1.7: Monster AI Logic */
    for (int mo = 0; mo < MAX_MONSTERS; mo++) {
        if (!monsters[mo].active) continue;
        int q1 = monsters[mo].q1, q2 = monsters[mo].q2, q3 = monsters[mo].q3;
        QuadrantIndex *local_q = &spatial_index[q1][q2][q3];
        
        ConnectedPlayer *target = NULL; double min_d = 10.0;
        for (int j = 0; j < local_q->player_count; j++) {
            ConnectedPlayer *p = local_q->players[j]; if (p->state.is_cloaked) continue;
            double dx = p->state.s1 - monsters[mo].x, dy = p->state.s2 - monsters[mo].y, dz = p->state.s3 - monsters[mo].z;
            double d = sqrt(dx*dx + dy*dy + dz*dz);
            if (d < min_d) { min_d = d; target = p; }
        }

        if (monsters[mo].type == 30) { /* Crystalline Entity */
            if (target) {
                double dx = target->state.s1 - monsters[mo].x, dy = target->state.s2 - monsters[mo].y, dz = target->state.s3 - monsters[mo].z;
                double dist = (min_d > 0.001) ? min_d : 0.001;
                monsters[mo].x += (dx/dist) * 0.05; monsters[mo].y += (dy/dist) * 0.05; monsters[mo].z += (dz/dist) * 0.05;
                if (min_d < 4.0 && global_tick % 60 == 0) {
                    target->state.beam_count = 1;
                    target->state.beams[0] = (NetBeam){(float)monsters[mo].x, (float)monsters[mo].y, (float)monsters[mo].z, 30};
                    target->state.energy -= 500;
                    send_server_msg((int)(target-players), "SCIENCE", "CRYSTALLINE RESONANCE DETECTED! SHIELDS BUCKLING!");
                }
            }
        } else if (monsters[mo].type == 31) { /* Space Amoeba */
            if (target && min_d < 1.5) {
                target->state.energy -= 200;
                if (global_tick % 30 == 0) send_server_msg((int)(target-players), "WARNING", "SPACE AMOEBA ADHERING TO HULL! ENERGY DRAIN CRITICAL!");
            }
        }
    }

    /* Phase 2: Player Interaction & Hazards */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!players[i].active) continue;
        
        /* Crew Management Logic */
        if (global_tick % 100 == 0) {
            float ls_health = players[i].state.system_health[7]; /* Life Support */
            if (ls_health < 75.0f) {
                int loss = (ls_health < 25.0f) ? 5 : 1;
                players[i].state.crew_count -= loss;
                if (players[i].state.crew_count < 0) players[i].state.crew_count = 0;
                if (players[i].state.crew_count == 0) {
                    send_server_msg(i, "CRITICAL", "Life support failure. Crew lost. Vessel adrift.");
                    players[i].active = 0;
                    players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1};
                } else if (global_tick % 300 == 0) {
                    send_server_msg(i, "MEDICAL", "Warning: Casualties reported due to life support instability.");
                }
            }
        }

        /* Random Environmental Events - Increased frequency */
        if (global_tick % 1000 == 0 && (rand() % 100 < 20)) {
            int event_type = rand() % 4; /* 0,1 = Ion Storm, 2 = Shear, 3 = Power Surge */
            if (event_type <= 1) {
                send_server_msg(i, "SCIENCE", "Ion Storm detected! Sensors effectively blinded.");
                players[i].state.system_health[2] *= 0.5f; /* Damage sensors */
                /* Mark storm in galaxy grid for the map (8th digit - 10^7) */
                int q1=players[i].state.q1, q2=players[i].state.q2, q3=players[i].state.q3;
                if (IS_Q_VALID(q1,q2,q3)) {
                    long long current_val = spacegl_master.g[q1][q2][q3];
                    int already_has_storm = (current_val / 10000000LL) % 10;
                    if (!already_has_storm) {
                        spacegl_master.g[q1][q2][q3] += 10000000LL;
                    }
                }
            } else if (event_type == 2) {
                send_server_msg(i, "HELMSMAN", "Spatial shear encountered! We are being pushed off course!");
                players[i].gx += (rand()%100 - 50) / 50.0;
                players[i].gy += (rand()%100 - 50) / 50.0;
                players[i].gz += (rand()%100 - 50) / 50.0;
            } else {
                send_server_msg(i, "ENGINEERING", "Deep Space surge detected. Power levels fluctuating.");
                players[i].state.energy += (rand() % 10000) - 5000;
                if (players[i].state.energy < 0) players[i].state.energy = 0;
            }
        }

        /* Anomaly Effects: Nebulas & Pulsars */
        QuadrantIndex *anomaly_q = &spatial_index[players[i].state.q1][players[i].state.q2][players[i].state.q3];
        
        for (int n = 0; n < anomaly_q->nebula_count; n++) {
            double d = sqrt(pow(players[i].state.s1 - anomaly_q->nebulas[n]->x, 2) + pow(players[i].state.s2 - anomaly_q->nebulas[n]->y, 2) + pow(players[i].state.s3 - anomaly_q->nebulas[n]->z, 2));
            if (d < 2.0) {
                 if (global_tick % 60 == 0) { /* Once per second */
                     players[i].state.energy -= 50;
                     if (players[i].state.energy < 0) players[i].state.energy = 0;
                 }
                 if (global_tick % 300 == 0) send_server_msg(i, "COMPUTER", "Alert: Nebular interference draining shields.");
            }
        }
        for (int p = 0; p < anomaly_q->pulsar_count; p++) {
            double d = sqrt(pow(players[i].state.s1 - anomaly_q->pulsars[p]->x, 2) + pow(players[i].state.s2 - anomaly_q->pulsars[p]->y, 2) + pow(players[i].state.s3 - anomaly_q->pulsars[p]->z, 2));
            if (d < 2.5) {
                if (global_tick % 60 == 0) {
                    int dmg = (int)((2.5 - d) * 400.0);
                    int shield_hit = 0;
                    for(int s=0; s<6; s++) { 
                        if(players[i].state.shields[s] > 0) {
                            int abs = (players[i].state.shields[s] >= dmg/6) ? dmg/6 : players[i].state.shields[s];
                            players[i].state.shields[s] -= abs;
                            shield_hit += abs;
                        }
                    }
                    if (shield_hit < dmg) {
                        /* Radiation penetrates shields */
                        players[i].state.crew_count -= (rand()%5 + 1);
                        if (players[i].state.crew_count < 0) players[i].state.crew_count = 0;
                    }
                    char msg[64]; sprintf(msg, "Radiation Critical! Shield Integrity Failing. (Dmg: %d)", dmg);
                    send_server_msg(i, "WARNING", msg);
                    
                    if (players[i].state.crew_count == 0) {
                         send_server_msg(i, "CRITICAL", "ALL HANDS LOST TO RADIATION.");
                         players[i].active = 0;
                         players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1};
                    }
                }
            }
        }

        /* Comet Interception Logic */
        for (int c = 0; c < anomaly_q->comet_count; c++) {
            double d = sqrt(pow(players[i].state.s1 - anomaly_q->comets[c]->x, 2) + pow(players[i].state.s2 - anomaly_q->comets[c]->y, 2) + pow(players[i].state.s3 - anomaly_q->comets[c]->z, 2));
            if (d < 0.6) {
                if (global_tick % 100 == 0) {
                    players[i].state.inventory[6] += 5; /* Gases */
                    send_server_msg(i, "ENGINEERING", "Collecting rare gases from comet tail.");
                }
            }
        }

        /* Asteroid Collision Logic */
        for (int a = 0; a < anomaly_q->asteroid_count; a++) {
            double d = sqrt(pow(players[i].state.s1 - anomaly_q->asteroids[a]->x, 2) + pow(players[i].state.s2 - anomaly_q->asteroids[a]->y, 2) + pow(players[i].state.s3 - anomaly_q->asteroids[a]->z, 2));
            if (d < 0.8) {
                if (players[i].hyper_speed > 0.1) {
                    if (global_tick % 30 == 0) {
                        int dmg = (int)(players[i].hyper_speed * 1000.0);
                        for(int s=0; s<6; s++) players[i].state.shields[s] -= (dmg/10);
                        players[i].state.system_health[1] -= 0.5f; /* Impulse engines damage */
                        send_server_msg(i, "WARNING", "Colliding with asteroids! Reduce speed!");
                    }
                }
            }
        }

        bool in_nebula = false;
        for (int n = 0; n < anomaly_q->nebula_count; n++) {
            double d = sqrt(pow(players[i].state.s1 - anomaly_q->nebulas[n]->x, 2) + pow(players[i].state.s2 - anomaly_q->nebulas[n]->y, 2) + pow(players[i].state.s3 - anomaly_q->nebulas[n]->z, 2));
            if (d < 2.0) { in_nebula = true; break; }
        }

        /* Nebula Shield Interference */
        if (in_nebula) {
            /* Shields recharge at 25% speed, and cloak is unstable */
            if (players[i].state.energy > 0) {
                for(int s=0; s<6; s++) if(players[i].state.shields[s] < 5000) players[i].state.shields[s] -= 2; /* Slow drain instead of recharge */
            }
        }

        /* Pulsar Radiation Logic */
        for (int p = 0; p < anomaly_q->pulsar_count; p++) {
            double d = sqrt(pow(players[i].state.s1 - anomaly_q->pulsars[p]->x, 2) + pow(players[i].state.s2 - anomaly_q->pulsars[p]->y, 2) + pow(players[i].state.s3 - anomaly_q->pulsars[p]->z, 2));
            if (d < 2.0) {
                /* Radiation penetrates shields */
                if (rand()%100 < 10) {
                    players[i].state.crew_count--;
                    send_server_msg(i, "MEDICAL", "RADIATION ALERT! EQUIPMENT FAILURE IN SICKBAY!");
                }
                players[i].state.energy -= 50; 
            }
        }

        /* Black Hole Gravity Pull */
        for (int h = 0; h < anomaly_q->bh_count; h++) {
            double dx = anomaly_q->black_holes[h]->x - players[i].state.s1;
            double dy = anomaly_q->black_holes[h]->y - players[i].state.s2;
            double dz = anomaly_q->black_holes[h]->z - players[i].state.s3;
            double d = sqrt(dx*dx + dy*dy + dz*dz);
            if (d < 3.0 && d > 0.1) {
                /* Pull player towards center */
                double force = 0.05 / (d * d);
                players[i].state.s1 += (dx/d) * force;
                players[i].state.s2 += (dy/d) * force;
                players[i].state.s3 += (dz/d) * force;
                /* Sync absolute coordinates */
                players[i].gx = (players[i].state.q1-1)*10.0 + players[i].state.s1;
                players[i].gy = (players[i].state.q2-1)*10.0 + players[i].state.s2;
                players[i].gz = (players[i].state.q3-1)*10.0 + players[i].state.s3;
            }
        }

        /* Mine Detonation Logic */
        for (int m = 0; m < anomaly_q->mine_count; m++) {
            if (!anomaly_q->mines[m]->active) continue;
            double d = sqrt(pow(players[i].state.s1 - anomaly_q->mines[m]->x, 2) + pow(players[i].state.s2 - anomaly_q->mines[m]->y, 2) + pow(players[i].state.s3 - anomaly_q->mines[m]->z, 2));
            if (d < 0.4) {
                /* BOOM! */
                anomaly_q->mines[m]->active = 0;
                players[i].state.boom = (NetPoint){(float)anomaly_q->mines[m]->x, (float)anomaly_q->mines[m]->y, (float)anomaly_q->mines[m]->z, 1};
                int dmg = 25000;
                for(int s=0; s<6; s++) { int abs=(players[i].state.shields[s]>=dmg/6)?dmg/6:players[i].state.shields[s]; players[i].state.shields[s]-=abs; dmg-=abs; }
                players[i].state.energy -= dmg;
                send_server_msg(i, "CRITICAL", "MINE DETONATION! PROXIMITY ALERT FAILURE!");
            }
        }

        /* Spatial Rift Teleportation Logic */
        for (int rf = 0; rf < anomaly_q->rift_count; rf++) {
            double d = sqrt(pow(players[i].state.s1 - anomaly_q->rifts[rf]->x, 2) + pow(players[i].state.s2 - anomaly_q->rifts[rf]->y, 2) + pow(players[i].state.s3 - anomaly_q->rifts[rf]->z, 2));
            if (d < 0.5) {
                /* Random Jump */
                int nq1 = 1 + rand()%10;
                int nq2 = 1 + rand()%10;
                int nq3 = 1 + rand()%10;
                double ns1 = (rand()%100)/10.0;
                double ns2 = (rand()%100)/10.0;
                double ns3 = (rand()%100)/10.0;
                
                players[i].gx = (nq1-1)*10.0 + ns1;
                players[i].gy = (nq2-1)*10.0 + ns2;
                players[i].gz = (nq3-1)*10.0 + ns3;
                
                players[i].state.q1 = nq1; players[i].state.q2 = nq2; players[i].state.q3 = nq3;
                players[i].state.s1 = ns1; players[i].state.s2 = ns2; players[i].state.s3 = ns3;
                
                players[i].nav_state = NAV_STATE_IDLE;
                players[i].hyper_speed = 0;
                
                send_server_msg(i, "CRITICAL", "SPATIAL RIFT ENCOUNTERED! UNCONTROLLED Deep Space FOLDING IN PROGRESS!");
                send_server_msg(i, "HELMSMAN", "Teleportation complete. Sensors recalibrating to new position.");
                break;
            }
        }

        if (players[i].gx <= 0.001 && players[i].gy <= 0.001) {
            players[i].gx = (players[i].state.q1-1)*10.0 + players[i].state.s1;
            players[i].gy = (players[i].state.q2-1)*10.0 + players[i].state.s2;
            players[i].gz = (players[i].state.q3-1)*10.0 + players[i].state.s3;
        }

        /* Reactor and Systems Logic */
        /* 1. Shield Regeneration: Scales with power_dist[1] AND system_health[8] (Shields) */
        if (players[i].state.energy > 100) {
            /* Baseline rate modified by Shield System Integrity (0.0 - 1.0) */
            float integrity_mult = players[i].state.system_health[8] / 100.0f;
            float regen_rate = (0.5f + (players[i].state.power_dist[1] * 10.0f)) * integrity_mult;
            
            bool needs_regen = false;
            for(int s=0; s<6; s++) {
                if (players[i].state.shields[s] < 10000) {
                    players[i].state.shields[s] += (int)regen_rate;
                    if (players[i].state.shields[s] > 10000) players[i].state.shields[s] = 10000;
                    needs_regen = true;
                }
            }
            if (needs_regen) players[i].state.energy -= (int)(regen_rate * 0.8f);
        }

        /* 1.1 Ion Beam Recharge: Scales with power_dist[2] (Weapons) */
        if (players[i].state.ion_beam_charge < 100.0f) {
            float recharge_rate = 0.5f + (players[i].state.power_dist[2] * 2.5f);
            players[i].state.ion_beam_charge += recharge_rate;
            if (players[i].state.ion_beam_charge > 100.0f) players[i].state.ion_beam_charge = 100.0f;
            /* Optimized energy drain for weapons capacitor */
            players[i].state.energy -= (int)(recharge_rate * 2.0f);
        }

        /* 1.2 Torpedo Loading Timer */
        if (players[i].torp_load_timer > 0) players[i].torp_load_timer--;
        
        /* 1.2.1 Renegade Timer Decrement */
        if (players[i].renegade_timer > 0) {
            players[i].renegade_timer--;
            if (players[i].renegade_timer == 0) {
                send_server_msg(i, "COMMAND", "Amnesty granted. Your status has been restored to active duty.");
            }
        }

        /* 1.3 Update Tube State for HUD */
        if (players[i].state.system_health[5] <= 50.0f) players[i].state.tube_state = 3; /* OFFLINE */
        else if (players[i].torp_active) players[i].state.tube_state = 1;                /* FIRING */
        else if (players[i].torp_load_timer > 0) players[i].state.tube_state = 2;        /* LOADING */
        else players[i].state.tube_state = 0;                                           /* READY */

        /* 1.4 Update Life Support Value */
        players[i].state.life_support = players[i].state.system_health[7];

        /* 2. Passive and Systems Energy Drain */
        int passive_drain = 1; /* Minimal base usage */
        if (players[i].state.is_cloaked) passive_drain += 15; /* Cloak cost */
        players[i].state.energy -= passive_drain;
        if (players[i].state.energy < 0) players[i].state.energy = 0;

        if (players[i].nav_state == NAV_STATE_ALIGN || players[i].nav_state == NAV_STATE_ALIGN_IMPULSE) {
            players[i].nav_timer--;
            
            double diff_h = players[i].target_h - players[i].start_h;
            while (diff_h > 180.0) diff_h -= 360.0;
            while (diff_h < -180.0) diff_h += 360.0;
            
            double diff_m = players[i].target_m - players[i].start_m;
            
            double t = 1.0 - (double)players[i].nav_timer / 60.0;
            players[i].state.van_h = players[i].start_h + diff_h * t;
            players[i].state.van_m = players[i].start_m + diff_m * t;
            
            /* Normalize heading */
            while (players[i].state.van_h >= 360.0) players[i].state.van_h -= 360.0;
            while (players[i].state.van_h < 0.0) players[i].state.van_h += 360.0;

            if (players[i].nav_timer <= 0) {
                if (players[i].nav_state == NAV_STATE_ALIGN) {
                    players[i].nav_state = NAV_STATE_HYPERDRIVE;
                    double factor = players[i].hyper_speed; /* Factor was stored here temporarily */
                    if (factor < 1.0) factor = 1.0;
                    
                    double dist = sqrt(pow(players[i].target_gx - players[i].gx, 2) + pow(players[i].target_gy - players[i].gy, 2) + pow(players[i].target_gz - players[i].gz, 2));
                    
                    /* 
                     * Hyperdrive Velocity Computation:
                     * Base speed (Hyperdrive 1) = 10 seconds per quadrant (300 ticks)
                     * Scaling: Time = BaseTime / Factor^1.2
                     * Hyperdrive 6 ~ 3.5 seconds
                     * Hyperdrive 9 ~ 2.1 seconds
                     */
                    double time_per_q = 10.0 / pow(factor, 0.8);
                    players[i].nav_timer = (int)((dist / 10.0) * time_per_q * 30.0);
                    if (players[i].nav_timer < 20) players[i].nav_timer = 20;
                    
                    players[i].hyper_speed = dist / players[i].nav_timer;
                    
                    char msg[128];
                    sprintf(msg, "Hyperdrive drive engaged. Velocity: Hyperdrive %.1f. ETA: %.1f seconds.", factor, (double)players[i].nav_timer / 30.0);
                    send_server_msg(i, "HELMSMAN", msg);
                } else {
                    players[i].nav_state = NAV_STATE_IMPULSE;
                    char msg[64]; sprintf(msg, "Impulse engaged at %.0f%%.", players[i].hyper_speed * 200.0);
                    send_server_msg(i, "HELMSMAN", msg);
                }
            }
        }
        else if (players[i].nav_state == NAV_STATE_HYPERDRIVE) {
            players[i].nav_timer--;
            players[i].gx += players[i].dx * players[i].hyper_speed;
            players[i].gy += players[i].dy * players[i].hyper_speed;
            players[i].gz += players[i].dz * players[i].hyper_speed;
            
            /* Recalculate local sector and quadrant for visualization */
            players[i].state.q1 = get_q_from_g(players[i].gx);
            players[i].state.q2 = get_q_from_g(players[i].gy);
            players[i].state.q3 = get_q_from_g(players[i].gz);
            players[i].state.s1 = players[i].gx - (players[i].state.q1 - 1) * 10.0;
            players[i].state.s2 = players[i].gy - (players[i].state.q2 - 1) * 10.0;
            players[i].state.s3 = players[i].gz - (players[i].state.q3 - 1) * 10.0;

            if (players[i].nav_timer <= 0) { players[i].nav_state = NAV_STATE_REALIGN; players[i].nav_timer = 60; players[i].start_h = players[i].state.van_h; players[i].start_m = players[i].state.van_m; }
        }
        else if (players[i].nav_state == NAV_STATE_REALIGN) {
            players[i].nav_timer--;
            double t = 1.0 - (double)players[i].nav_timer / 60.0;
            players[i].state.van_m = players[i].start_m * (1.0 - t);
            if (players[i].nav_timer <= 0) { 
                players[i].state.van_m = 0; 
                players[i].nav_state = NAV_STATE_IDLE; 
                send_server_msg(i, "HELMSMAN", "Stabilization complete. Ship aligned.");
            }
        }
        else if (players[i].nav_state == NAV_STATE_IMPULSE) {
            if (players[i].state.energy > 0) {
                players[i].state.energy -= 1;
                /* Speed Scales with Engine Power (0.0 - 1.0). Baseline is 10x, max is 25x */
                float engine_mult = 8.0f + (players[i].state.power_dist[0] * 17.0f);
                players[i].gx += players[i].dx * players[i].hyper_speed * engine_mult;
                players[i].gy += players[i].dy * players[i].hyper_speed * engine_mult;
                players[i].gz += players[i].dz * players[i].hyper_speed * engine_mult;
                
                players[i].state.q1 = get_q_from_g(players[i].gx);
                players[i].state.q2 = get_q_from_g(players[i].gy);
                players[i].state.q3 = get_q_from_g(players[i].gz);
                players[i].state.s1 = players[i].gx - (players[i].state.q1 - 1) * 10.0;
                players[i].state.s2 = players[i].gy - (players[i].state.q2 - 1) * 10.0;
                players[i].state.s3 = players[i].gz - (players[i].state.q3 - 1) * 10.0;
            } else {
                players[i].nav_state = NAV_STATE_IDLE;
                send_server_msg(i, "COMPUTER", "Impulse drive failure: Zero energy.");
            }
        }
        else if (players[i].nav_state == NAV_STATE_WORMHOLE) {
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
                players[i].state.wormhole = (NetPoint){(float)players[i].wx, (float)players[i].wy, (float)players[i].wz, 1};
            } else {
                players[i].state.wormhole.active = 0;
            }

            /* Move ship INTO the wormhole during the entry phase (ticks 450-300) */
            if (players[i].nav_timer > 300 && players[i].nav_timer < 380) {
                 /* Absolute coordinates of the entry mouth */
                 double target_gx = (players[i].state.q1 - 1) * 10.0 + players[i].wx;
                 double target_gy = (players[i].state.q2 - 1) * 10.0 + players[i].wy;
                 double target_gz = (players[i].state.q3 - 1) * 10.0 + players[i].wz;
                 
                 players[i].gx += (target_gx - players[i].gx) * 0.05;
                 players[i].gy += (target_gy - players[i].gy) * 0.05;
                 players[i].gz += (target_gz - players[i].gz) * 0.05;
            }

            /* EXECUTE JUMP at T=300 (leave 10 seconds for arrival contemplations) */
            if (players[i].nav_timer == 300) {
                players[i].gx = players[i].target_gx;
                players[i].gy = players[i].target_gy;
                players[i].gz = players[i].target_gz;
                players[i].dx = 0; players[i].dy = 0; players[i].dz = 0;
                players[i].hyper_speed = 0;
                
                int tq1 = get_q_from_g(players[i].gx);
                int tq2 = get_q_from_g(players[i].gy);
                int tq3 = get_q_from_g(players[i].gz);
                float ts1 = (float)(players[i].gx - (tq1 - 1) * 10.0);
                float ts2 = (float)(players[i].gy - (tq2 - 1) * 10.0);
                float ts3 = (float)(players[i].gz - (tq3 - 1) * 10.0);
                
                players[i].state.jump_arrival = (NetPoint){ts1, ts2, ts3, 1};
                players[i].state.wormhole.active = 0;
            }

            if (players[i].nav_timer == 240) { /* T+2.0s from arrival */
                send_server_msg(i, "HELMSMAN", "Wormhole stabilized in target sector. Maintaining hull integrity.");
            }

            if (players[i].nav_timer <= 150) { /* Exactly 3.0s after the previous message */
                players[i].nav_state = NAV_STATE_IDLE;
                players[i].state.wormhole.active = 0;
                players[i].state.jump_arrival.active = 0;
                send_server_msg(i, "HELMSMAN", "Wormhole traversal successful. Welcome to destination.");
            }
        }
        else if (players[i].nav_state == NAV_STATE_CHASE) {
            int tid = players[i].state.lock_target;
            double tx, ty, tz, tvx=0, tvy=0, tvz=0; bool found = false;
            int tq1=0, tq2=0, tq3=0;

            if (tid >= 1 && tid <= 32 && players[tid-1].active) {
                tx = players[tid-1].gx; ty = players[tid-1].gy; tz = players[tid-1].gz;
                tvx = players[tid-1].dx * players[tid-1].hyper_speed; tvy = players[tid-1].dy * players[tid-1].hyper_speed; tvz = players[tid-1].dz * players[tid-1].hyper_speed;
                tq1 = players[tid-1].state.q1; tq2 = players[tid-1].state.q2; tq3 = players[tid-1].state.q3;
                found = true;
            } else if (tid >= 100 && tid < 100+MAX_NPC && npcs[tid-100].active) {
                tx = npcs[tid-100].gx; ty = npcs[tid-100].gy; tz = npcs[tid-100].gz;
                tvx = npcs[tid-100].dx * 0.03; tvy = npcs[tid-100].dy * 0.03; tvz = npcs[tid-100].dz * 0.03;
                tq1 = npcs[tid-100].q1; tq2 = npcs[tid-100].q2; tq3 = npcs[tid-100].q3;
                found = true;
            } else if (tid >= 10000 && tid < 10000+MAX_COMETS && comets[tid-10000].active) {
                int c = tid - 10000;
                tx = (comets[c].q1-1)*10.0 + comets[c].x;
                ty = (comets[c].q2-1)*10.0 + comets[c].y;
                tz = (comets[c].q3-1)*10.0 + comets[c].z;
                /* Approximate velocity from heading/mark */
                double rad_h = comets[c].h * M_PI / 180.0;
                double rad_m = comets[c].m * M_PI / 180.0;
                tvx = cos(rad_m) * sin(rad_h) * 0.02; 
                tvy = cos(rad_m) * -cos(rad_h) * 0.02;
                tvz = sin(rad_m) * 0.02;
                tq1 = comets[c].q1; tq2 = comets[c].q2; tq3 = comets[c].q3;
                found = true;
            }

            if (found && players[i].state.energy > 5000) {
                double dx = tx - players[i].gx, dy = ty - players[i].gy, dz = tz - players[i].gz;
                double dist = sqrt(dx*dx + dy*dy + dz*dz);
                
                /* Deep Space Tracking: Calculate vectors using galactic coordinates */
                if (dist > 0.05) {
                    double des_h = atan2(dx, -dy) * 180.0 / M_PI; if(des_h<0) des_h+=360;
                    double des_m = asin(dz/dist) * 180.0 / M_PI;
                    double diff_h = des_h - players[i].state.van_h;
                    while (diff_h > 180) { diff_h -= 360; }
                    while (diff_h < -180) { diff_h += 360; }
                    players[i].state.van_h += diff_h * 0.15;
                    players[i].state.van_m += (des_m - players[i].state.van_m) * 0.15;
                    if (players[i].state.van_h >= 360) { players[i].state.van_h -= 360; }
                    if (players[i].state.van_h < 0) { players[i].state.van_h += 360; }
                }
                
                double rad_h = players[i].state.van_h * M_PI / 180.0;
                double rad_m = players[i].state.van_m * M_PI / 180.0;
                players[i].dx = cos(rad_m) * sin(rad_h); players[i].dy = cos(rad_m) * -cos(rad_h); players[i].dz = sin(rad_m);
                
                /* Auto-Hyperdrive: Increase speed if target is in another quadrant (dist > 10) */
                double target_dist = (players[i].approach_dist > 0.05) ? players[i].approach_dist : 2.0;
                double base_speed = (dist > 10.0) ? 0.8 : 0.4;
                double ideal_speed = (dist - target_dist) * base_speed + sqrt(tvx*tvx + tvy*tvy + tvz*tvz);
                
                if (ideal_speed > 0.8) { ideal_speed = 0.8; }
                if (ideal_speed < -0.1) { ideal_speed = -0.1; }
                
                players[i].hyper_speed = (players[i].hyper_speed * 0.7) + (ideal_speed * 0.3);
                players[i].gx += players[i].dx * players[i].hyper_speed;
                players[i].gy += players[i].dy * players[i].hyper_speed;
                players[i].gz += players[i].dz * players[i].hyper_speed;
                
                int drain = 10 + (int)(fabs(players[i].hyper_speed)*20.0);
                players[i].state.energy -= drain;
                
                /* Quadrant Transition Check */
                if (players[i].state.q1 != tq1 || players[i].state.q2 != tq2 || players[i].state.q3 != tq3) {
                    static int last_warn = 0;
                    if (global_tick - last_warn > 300) {
                        send_server_msg(i, "HELMSMAN", "Target has left the quadrant. Engaging inter-sector Deep Space tracking.");
                        last_warn = global_tick;
                    }
                }
            } else {
                players[i].nav_state = NAV_STATE_IDLE;
                if (!found) send_server_msg(i, "COMPUTER", "Chase target lost.");
            }
        }

        /* Galactic Barrier Enforcement: Standardized for all axes and corners */
        bool hit_barrier = false;
        if (players[i].gx < 0.05) { players[i].gx = 0.05; hit_barrier = true; }
        else if (players[i].gx > 99.95) { players[i].gx = 99.95; hit_barrier = true; }
        
        if (players[i].gy < 0.05) { players[i].gy = 0.05; hit_barrier = true; }
        else if (players[i].gy > 99.95) { players[i].gy = 99.95; hit_barrier = true; }
        
        if (players[i].gz < 0.05) { players[i].gz = 0.05; hit_barrier = true; }
        else if (players[i].gz > 99.95) { players[i].gz = 99.95; hit_barrier = true; }

        if (hit_barrier && players[i].nav_state != NAV_STATE_CHASE && players[i].nav_state != NAV_STATE_IDLE) {
            players[i].nav_state = NAV_STATE_IDLE;
            players[i].hyper_speed = 0;
            send_server_msg(i, "HELMSMAN", "Warning: We have hit the Galactic Barrier. Engines disengaged.");
        }

        /* Update Sector Coordinates and Quadrant */
        players[i].state.q1 = get_q_from_g(players[i].gx);
        players[i].state.q2 = get_q_from_g(players[i].gy);
        players[i].state.q3 = get_q_from_g(players[i].gz);
        players[i].state.s1 = players[i].gx - (players[i].state.q1 - 1) * 10.0;
        players[i].state.s2 = players[i].gy - (players[i].state.q2 - 1) * 10.0;
        players[i].state.s3 = players[i].gz - (players[i].state.q3 - 1) * 10.0;

        /* Collision Detection: Celestial Bodies */
        QuadrantIndex *current_q = &spatial_index[players[i].state.q1][players[i].state.q2][players[i].state.q3];
        for (int h = 0; h < current_q->bh_count; h++) {
            double dx = current_q->black_holes[h]->x - players[i].state.s1;
            double dy = current_q->black_holes[h]->y - players[i].state.s2;
            double dz = current_q->black_holes[h]->z - players[i].state.s3;
            double d = sqrt(dx*dx + dy*dy + dz*dz);
            
            if (d < DIST_GRAVITY_WELL) {
                /* Gravity Well Effect: Drain Shields and Energy */
                int drain = (int)((DIST_GRAVITY_WELL - d) * 1000.0);
                for(int s=0; s<6; s++) { if(players[i].state.shields[s] > 0) players[i].state.shields[s] -= (drain/10); if(players[i].state.shields[s] < 0) players[i].state.shields[s] = 0; }
                players[i].state.energy -= drain;
                
                /* Physical Pull: Displace ship towards singularity */
                double pull_strength = (DIST_GRAVITY_WELL - d) * 0.05;
                if (d > 0.001) {
                    players[i].gx += (dx / d) * pull_strength;
                    players[i].gy += (dy / d) * pull_strength;
                    players[i].gz += (dz / d) * pull_strength;
                }

                if (global_tick % 20 == 0) send_server_msg(i, "WARNING", "Extreme gravitational shear detected! Hull integrity at risk.");
            }
            if (d < DIST_EVENT_HORIZON) { 
                send_server_msg(i, "CRITICAL", "Event Horizon crossed! Spaghettification in progress..."); 
                players[i].state.energy = 0; players[i].state.crew_count = 0;
                players[i].nav_state = NAV_STATE_IDLE; players[i].hyper_speed = 0;
                players[i].dx = 0; players[i].dy = 0; players[i].dz = 0;
                players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1}; 
                players[i].active = 0; /* Ship destroyed */
                break; 
            }
        }
        if (players[i].active && players[i].state.energy > 0) for (int s = 0; s < current_q->star_count; s++) {
            double d = sqrt(pow(players[i].state.s1 - current_q->stars[s]->x, 2) + pow(players[i].state.s2 - current_q->stars[s]->y, 2) + pow(players[i].state.s3 - current_q->stars[s]->z, 2));
            if (d < 0.8) { 
                send_server_msg(i, "CRITICAL", "Impact with star corona! Hull melting..."); 
                players[i].state.energy = 0; players[i].state.crew_count = 0;
                players[i].nav_state = NAV_STATE_IDLE; players[i].hyper_speed = 0;
                players[i].dx = 0; players[i].dy = 0; players[i].dz = 0;
                players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1}; 
                break; 
            }
        }
        if (players[i].active && players[i].state.energy > 0) for (int p = 0; p < current_q->planet_count; p++) {
            double d = sqrt(pow(players[i].state.s1 - current_q->planets[p]->x, 2) + pow(players[i].state.s2 - current_q->planets[p]->y, 2) + pow(players[i].state.s3 - current_q->planets[p]->z, 2));
            if (d < 0.8) { 
                send_server_msg(i, "CRITICAL", "Planetary collision! Structural failure."); 
                players[i].state.energy = 0; players[i].state.crew_count = 0;
                players[i].nav_state = NAV_STATE_IDLE; players[i].hyper_speed = 0;
                players[i].dx = 0; players[i].dy = 0; players[i].dz = 0;
                players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1}; 
                break; 
            }
        }

        /* Target Lock Validation (Inter-Quadrant aware) */
        if (players[i].state.lock_target > 0) {
            int tid = players[i].state.lock_target;
            bool valid = false;
            int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
            
            if (tid >= 1 && tid <= 32) {
                /* Players can be locked as long as they are active anywhere */
                if (players[tid-1].active) valid = true;
            } else if (tid >= 1000 && tid < 1000+MAX_NPC) {
                if (npcs[tid-1000].active) valid = true;
            } else if (tid >= 2000 && tid < 2000+MAX_BASES) {
                /* Static objects remain local-only for locking sanity */
                if (bases[tid-2000].active && bases[tid-2000].q1 == pq1 && bases[tid-2000].q2 == pq2 && bases[tid-2000].q3 == pq3) valid = true;
            } else if (tid >= 3000 && tid < 3000+MAX_PLANETS) {
                if (planets[tid-3000].active && planets[tid-3000].q1 == pq1 && planets[tid-3000].q2 == pq2 && planets[tid-3000].q3 == pq3) valid = true;
            } else if (tid >= 4000 && tid < 4000+MAX_STARS) {
                if (stars_data[tid-4000].active && stars_data[tid-4000].q1 == pq1 && stars_data[tid-4000].q2 == pq2 && stars_data[tid-4000].q3 == pq3) valid = true;
            } else if (tid >= 7000 && tid < 7000+MAX_BH) {
                if (black_holes[tid-7000].active && black_holes[tid-7000].q1 == pq1 && black_holes[tid-7000].q2 == pq2 && black_holes[tid-7000].q3 == pq3) valid = true;
            } else if (tid >= 8000 && tid < 8000+MAX_NEBULAS) {
                if (nebulas[tid-8000].active && nebulas[tid-8000].q1 == pq1 && nebulas[tid-8000].q2 == pq2 && nebulas[tid-8000].q3 == pq3) valid = true;
            } else if (tid >= 9000 && tid < 9000+MAX_PULSARS) {
                if (pulsars[tid-9000].active && pulsars[tid-9000].q1 == pq1 && pulsars[tid-9000].q2 == pq2 && pulsars[tid-9000].q3 == pq3) valid = true;
            } else if (tid >= 10000 && tid < 10000+MAX_COMETS) {
                if (comets[tid-10000].active) valid = true; /* Persistent across quadrants */
            } else if (tid >= 11000 && tid < 11000+MAX_DERELICTS) {
                if (derelicts[tid-11000].active && derelicts[tid-11000].q1 == pq1 && derelicts[tid-11000].q2 == pq2 && derelicts[tid-11000].q3 == pq3) valid = true;
            } else if (tid >= 12000 && tid < 12000+MAX_ASTEROIDS) {
                if (asteroids[tid-12000].active && asteroids[tid-12000].q1 == pq1 && asteroids[tid-12000].q2 == pq2 && asteroids[tid-12000].q3 == pq3) valid = true;
            } else if (tid >= 14000 && tid < 14000+MAX_MINES) {
                if (mines[tid-14000].active && mines[tid-14000].q1 == pq1 && mines[tid-14000].q2 == pq2 && mines[tid-14000].q3 == pq3) valid = true;
            } else if (tid >= 15000 && tid < 15000+MAX_BUOYS) {
                if (buoys[tid-15000].active && buoys[tid-15000].q1 == pq1 && buoys[tid-15000].q2 == pq2 && buoys[tid-15000].q3 == pq3) valid = true;
            } else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS) {
                if (platforms[tid-16000].active) valid = true; /* Persistent */
            } else if (tid >= 17000 && tid < 17000+MAX_RIFTS) {
                if (rifts[tid-17000].active && rifts[tid-17000].q1 == pq1 && rifts[tid-17000].q2 == pq2 && rifts[tid-17000].q3 == pq3) valid = true;
            } else if (tid >= 18000 && tid < 18000+MAX_MONSTERS) {
                if (monsters[tid-18000].active) valid = true; /* Persistent */
            }

            if (!valid) {
                players[i].state.lock_target = 0;
                send_server_msg(i, "TACTICAL", "Target lost. Lock released.");
            }
        }

        /* Probe Physics & Logic */
        for (int p = 0; p < 3; p++) {
            if (players[i].state.probes[p].active) {
                if (players[i].state.probes[p].status == 0) { /* LAUNCHED & EN ROUTE */
                    players[i].state.probes[p].eta -= 0.033f; 
                    
                    /* Galactic Movement */
                    players[i].state.probes[p].gx += players[i].state.probes[p].vx;
                    players[i].state.probes[p].gy += players[i].state.probes[p].vy;
                    players[i].state.probes[p].gz += players[i].state.probes[p].vz;
                    
                    /* Update current quadrant/sector of the probe based on galactic position */
                    int cur_q1 = get_q_from_g(players[i].state.probes[p].gx);
                    int cur_q2 = get_q_from_g(players[i].state.probes[p].gy);
                    int cur_q3 = get_q_from_g(players[i].state.probes[p].gz);
                    
                    players[i].state.probes[p].s1 = players[i].state.probes[p].gx - (cur_q1 - 1) * 10.0f;
                    players[i].state.probes[p].s2 = players[i].state.probes[p].gy - (cur_q2 - 1) * 10.0f;
                    players[i].state.probes[p].s3 = players[i].state.probes[p].gz - (cur_q3 - 1) * 10.0f;

                    if (players[i].state.probes[p].eta <= 0) {
                        players[i].state.probes[p].status = 1; /* ARRIVED */
                        players[i].state.probes[p].eta = 5.0f; 
                        
                        /* Snap to final target quadrant accurately */
                        int pq1 = players[i].state.probes[p].q1;
                        int pq2 = players[i].state.probes[p].q2;
                        int pq3 = players[i].state.probes[p].q3;
                        
                        players[i].state.z[pq1][pq2][pq3] = 1;
                        
                        /* Real-time Query via Live Spatial Index */
                        QuadrantIndex *lq = &spatial_index[pq1][pq2][pq3];
                        char msg[128];
                        sprintf(msg, "Probe arrived at [%d,%d,%d]. Hostiles: %d, Bases: %d, Stars: %d", pq1, pq2, pq3, lq->npc_count, lq->base_count, lq->star_count);
                        send_server_msg(i, "SCIENCE", msg);
                    }
                } else if (players[i].state.probes[p].status == 1) { /* TRANSMITTING */
                    players[i].state.probes[p].eta -= 0.033f;
                    if (players[i].state.probes[p].eta <= 0) {
                        players[i].state.probes[p].status = 2; /* DERELICT */
                    }
                }
            }
        }

        /* Torpedo Physics */
        if (players[i].torp_active) {
            /* Guidance System */
            if (players[i].torp_target > 0) {
                double target_x = -1, target_y = -1, target_z = -1;
                int tid = players[i].torp_target;
                int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
                if (tid <= 32 && players[tid-1].active && players[tid-1].state.q1 == pq1 && players[tid-1].state.q2 == pq2 && players[tid-1].state.q3 == pq3) {
                    target_x = players[tid-1].state.s1; target_y = players[tid-1].state.s2; target_z = players[tid-1].state.s3;
                } else if (tid >= 1000 && tid < 1000+MAX_NPC && npcs[tid-1000].active && npcs[tid-1000].q1 == pq1 && npcs[tid-1000].q2 == pq2 && npcs[tid-1000].q3 == pq3) {
                    target_x = npcs[tid-1000].x; target_y = npcs[tid-1000].y; target_z = npcs[tid-1000].z;
                } else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS && platforms[tid-16000].active && platforms[tid-16000].q1 == pq1 && platforms[tid-16000].q2 == pq2 && platforms[tid-16000].q3 == pq3) {
                    target_x = platforms[tid-16000].x; target_y = platforms[tid-16000].y; target_z = platforms[tid-16000].z;
                } else if (tid >= 18000 && tid < 18000+MAX_MONSTERS && monsters[tid-18000].active && monsters[tid-18000].q1 == pq1 && monsters[tid-18000].q2 == pq2 && monsters[tid-18000].q3 == pq3) {
                    target_x = monsters[tid-18000].x; target_y = monsters[tid-18000].y; target_z = monsters[tid-18000].z;
                }
                if (target_x != -1) {
                    double dx = target_x - players[i].tx, dy = target_y - players[i].ty, dz = target_z - players[i].tz;
                    double d = sqrt(dx*dx + dy*dy + dz*dz);
                    if (d > 0.01) {
                        /* Aggressive guidance: 50% correction per tick */
                        players[i].tdx = (players[i].tdx * 0.5) + ((dx/d) * 0.5);
                        players[i].tdy = (players[i].tdy * 0.5) + ((dy/d) * 0.5);
                        players[i].tdz = (players[i].tdz * 0.5) + ((dz/d) * 0.5);
                        double s = sqrt(players[i].tdx*players[i].tdx + players[i].tdy*players[i].tdy + players[i].tdz*players[i].tdz);
                        players[i].tdx /= s; players[i].tdy /= s; players[i].tdz /= s;
                    }
                }
            }
            /* Increased velocity: 0.25 units per tick */
            players[i].tx += players[i].tdx * 0.25; players[i].ty += players[i].tdy * 0.25; players[i].tz += players[i].tdz * 0.25;
            players[i].state.torp = (NetPoint){(float)players[i].tx, (float)players[i].ty, (float)players[i].tz, 1};
            
            /* Collision Detection (Radius increased to 0.8 to prevent tunneling) */
            bool hit = false;
            QuadrantIndex *lq = &spatial_index[players[i].state.q1][players[i].state.q2][players[i].state.q3];
            
            /* 1. Players */
            for (int j=0; j<lq->player_count; j++) {
                ConnectedPlayer *p = lq->players[j]; if (p == &players[i] || !p->active) continue;
                double d = sqrt(pow(players[i].tx - p->state.s1, 2) + pow(players[i].ty - p->state.s2, 2) + pow(players[i].tz - p->state.s3, 2));
                if (d < DIST_COLLISION_TORP) {
                    int dmg = DMG_TORPEDO;
                    /* Calculate hit angle for torpedo */
                    double rel_dx = players[i].tx - p->state.s1;
                    double rel_dy = players[i].ty - p->state.s2;
                    double angle = atan2(rel_dx, -rel_dy) * 180.0 / M_PI; if (angle < 0) angle += 360;
                    double rel_angle = angle - p->state.van_h;
                    while (rel_angle < 0) rel_angle += 360;
                    while (rel_angle >= 360) rel_angle -= 360;
                    
                    double rel_dz = players[i].tz - p->state.s3;
                    double dist_2d = sqrt(rel_dx*rel_dx + rel_dy*rel_dy);
                    double vertical_angle = atan2(rel_dz, dist_2d) * 180.0 / M_PI;

                    int s_idx = 0;
                    if (vertical_angle > 45) s_idx = 2;      /* Top */
                    else if (vertical_angle < -45) s_idx = 3; /* Bottom */
                    else {
                        if (rel_angle > 315 || rel_angle <= 45) s_idx = 0;      /* Front */
                        else if (rel_angle > 45 && rel_angle <= 135) s_idx = 5; /* Right */
                        else if (rel_angle > 135 && rel_angle <= 225) s_idx = 1;/* Rear */
                        else s_idx = 4;                                        /* Left */
                    }

                    if (p->state.shields[s_idx] >= dmg) {
                        p->state.shields[s_idx] -= dmg;
                        dmg = 0;
                    } else {
                        dmg -= p->state.shields[s_idx];
                        p->state.shields[s_idx] = 0;
                    }

                    p->state.energy -= dmg; 
                    p->shield_regen_delay = 150; /* 5 seconds for torpedoes */
                    
                    /* Torpedo System Damage: High chance to damage internal systems if shields are bypassed */
                    if (dmg > 0) {
                        if (rand() % 100 < (50 + (int)(dmg / 1000))) {
                            int sys_idx = rand() % 10;
                            float sys_dmg = 15.0f + (rand() % 35);
                            p->state.system_health[sys_idx] -= sys_dmg;
                            if (p->state.system_health[sys_idx] < 0) p->state.system_health[sys_idx] = 0;
                            
                            const char* sys_names[] = {"HYPERDRIVE", "IMPULSE", "SENSORS", "TRANSPORTERS", "Ion BeamS", "TORPEDOES", "COMPUTER", "LIFE SUPPORT", "SHIELDS", "AUXILIARY"};
                            char alert[128];
                            sprintf(alert, "SYSTEM ALERT: Torpedo impact caused critical failure in %s!", sys_names[sys_idx]);
                            send_server_msg((int)(p-players), "DAMAGE", alert);
                        }
                    }

                    /* Renegade Status: If you hit a friendly player, you are a traitor */
                    if (p->faction == players[i].faction) {
                        players[i].renegade_timer = 18000; /* 10 minutes renegade status */
                        send_server_msg(i, "CRITICAL", "FRIENDLY FIRE DETECTED! You have been marked as a TRAITOR by the fleet!");
                    }

                    send_server_msg((int)(p-players), "WARNING", "HIT BY Plasma Torpedo!");
                    if(p->state.energy <= 0) { 
                        p->state.energy = 0; p->state.crew_count = 0;
                        p->nav_state = NAV_STATE_IDLE; p->hyper_speed = 0;
                        p->state.boom = (NetPoint){(float)players[i].tx, (float)players[i].ty, (float)players[i].tz, 1}; 
                    }
                    hit = true; break;
                }
            }
            /* 2. NPCs */
            if (!hit) for (int n=0; n<lq->npc_count; n++) {
                NPCShip *npc = lq->npcs[n];
                double d = sqrt(pow(players[i].tx - npc->x, 2) + pow(players[i].ty - npc->y, 2) + pow(players[i].tz - npc->z, 2));
                if (d < 0.8) { 
                    npc->energy -= 75000; 
                    
                    /* Renegade Status: If you hit a friendly NPC */
                    if (npc->faction == players[i].faction) {
                        players[i].renegade_timer = 18000;
                        send_server_msg(i, "CRITICAL", "ATTACKING FRIENDLY VESSEL! Sector command has revoked your status!");
                    }

                    if(npc->energy <= 0) { npc->active = 0; players[i].state.boom = (NetPoint){(float)players[i].tx, (float)players[i].ty, (float)players[i].tz, 1}; } hit = true; break; 
                }
            }
            /* 3. Planets/Stars/Bases (Solid obstacles) */
            if (!hit) for (int p=0; p<lq->planet_count; p++) {
                double d = sqrt(pow(players[i].tx - lq->planets[p]->x, 2) + pow(players[i].ty - lq->planets[p]->y, 2) + pow(players[i].tz - lq->planets[p]->z, 2));
                if (d < 1.2) { hit = true; break; } /* Planet hit (absorbed) */
            }
            if (!hit) for (int s=0; s<lq->star_count; s++) {
                double d = sqrt(pow(players[i].tx - lq->stars[s]->x, 2) + pow(players[i].ty - lq->stars[s]->y, 2) + pow(players[i].tz - lq->stars[s]->z, 2));
                if (d < 1.5) { hit = true; break; } /* Star hit (vaporized) */
            }
            if (!hit) for (int b=0; b<lq->base_count; b++) {
                double d = sqrt(pow(players[i].tx - lq->bases[b]->x, 2) + pow(players[i].ty - lq->bases[b]->y, 2) + pow(players[i].tz - lq->bases[b]->z, 2));
                if (d < 1.0) { hit = true; break; } /* Base hit (absorbed by planetary shields) */
            }
            /* 4. Platforms/Monsters */
            if (!hit) for (int pt=0; pt<lq->platform_count; pt++) {
                NPCPlatform *plat = lq->platforms[pt];
                double d = sqrt(pow(players[i].tx - plat->x, 2) + pow(players[i].ty - plat->y, 2) + pow(players[i].tz - plat->z, 2));
                if (d < DIST_COLLISION_TORP) { plat->energy -= DMG_TORPEDO_PLATFORM; if(plat->energy <= 0) { plat->active = 0; players[i].state.boom = (NetPoint){(float)players[i].tx, (float)players[i].ty, (float)players[i].tz, 1}; } hit = true; break; }
            }
            if (!hit) for (int mo=0; mo<lq->monster_count; mo++) {
                NPCMonster *mon = lq->monsters[mo];
                double d = sqrt(pow(players[i].tx - mon->x, 2) + pow(players[i].ty - mon->y, 2) + pow(players[i].tz - mon->z, 2));
                if (d < 1.0) { mon->energy -= DMG_TORPEDO_MONSTER; if(mon->energy <= 0) { mon->active = 0; players[i].state.boom = (NetPoint){(float)players[i].tx, (float)players[i].ty, (float)players[i].tz, 1}; } hit = true; break; }
            }
            if (players[i].torp_timeout > 0) players[i].torp_timeout--;

            if (hit || players[i].tx<0||players[i].tx>10||players[i].ty<0||players[i].ty>10||players[i].tz<0||players[i].tz>10 || players[i].torp_timeout <= 0) {
                if (hit) { players[i].state.boom = (NetPoint){(float)players[i].tx, (float)players[i].ty, (float)players[i].tz, 1}; send_server_msg(i, "TACTICAL", "Torpedo impact confirmed."); }
                else if (players[i].torp_timeout <= 0) { send_server_msg(i, "TACTICAL", "Torpedo lost - Self-destruct activated."); }
                players[i].torp_active = false; players[i].state.torp.active = 0;
            }
        }
    }

    rebuild_spatial_index();
    if (global_tick % 1800 == 0) save_galaxy();

    /* Phase 3: Network Updates - Atomic Broadcast */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].socket == 0 || !players[i].active) continue;
        PacketUpdate upd; memset(&upd, 0, sizeof(PacketUpdate)); upd.type = PKT_UPDATE;
        upd.q1 = players[i].state.q1; upd.q2 = players[i].state.q2; upd.q3 = players[i].state.q3;
        upd.s1 = players[i].state.s1; upd.s2 = players[i].state.s2; upd.s3 = players[i].state.s3;
        upd.van_h = players[i].state.van_h; upd.van_m = players[i].state.van_m;
        upd.energy = players[i].state.energy; upd.torpedoes = players[i].state.torpedoes;
        upd.cargo_energy = players[i].state.cargo_energy; upd.cargo_torpedoes = players[i].state.cargo_torpedoes;
        upd.crew_count = players[i].state.crew_count;
        upd.prison_unit = players[i].state.prison_unit;
        upd.composite_plating = players[i].state.composite_plating;
        upd.hull_integrity = players[i].state.hull_integrity;
        for(int s=0; s<6; s++) upd.shields[s] = players[i].state.shields[s];
        for(int inv=0; inv<10; inv++) upd.inventory[inv] = players[i].state.inventory[inv];
        for(int sys=0; sys<10; sys++) upd.system_health[sys] = players[i].state.system_health[sys];
        for(int p=0; p<3; p++) upd.power_dist[p] = players[i].state.power_dist[p];
        upd.life_support = players[i].state.life_support;
        upd.anti_matter_count = players[i].state.anti_matter_count;
        upd.lock_target = players[i].state.lock_target;
        upd.tube_state = players[i].state.tube_state;
        upd.ion_beam_charge = players[i].state.ion_beam_charge;
        upd.is_cloaked = players[i].state.is_cloaked;
        upd.encryption_enabled = players[i].crypto_algo;
        int o_idx = 0;
        upd.objects[o_idx] = (NetObject){(float)players[i].state.s1,(float)players[i].state.s2,(float)players[i].state.s3,(float)players[i].state.van_h,(float)players[i].state.van_m,1,players[i].ship_class,1,(int)players[i].state.hull_integrity,players[i].state.energy,players[i].state.composite_plating,(int)players[i].state.hull_integrity,players[i].faction,i+1,players[i].state.is_cloaked,""};
        strncpy(upd.objects[o_idx++].name, players[i].name, 63);
        
        /* 1. Prioritize Current Quadrant Objects (Critical for SRS/HUD) */
        if (IS_Q_VALID(upd.q1, upd.q2, upd.q3)) {
            QuadrantIndex *lq = &spatial_index[upd.q1][upd.q2][upd.q3];
            /* Players in current quadrant */
            for(int j=0; j<lq->player_count; j++) {
                ConnectedPlayer *p = lq->players[j]; 
                if (p == &players[i] || !p->active || o_idx >= MAX_NET_OBJECTS) continue;
                if (p->state.is_cloaked && p->faction != players[i].faction) continue;
                NetObject *no = &upd.objects[o_idx];
                *no = (NetObject){(float)p->state.s1, (float)p->state.s2, (float)p->state.s3, (float)p->state.van_h, (float)p->state.van_m, 1, p->ship_class, 1, (int)p->state.hull_integrity, p->state.energy, p->state.composite_plating, (int)p->state.hull_integrity, p->faction, (int)(p-players)+1, p->state.is_cloaked, ""};
                size_t nlen = strlen(p->name);
                if (nlen > 63) nlen = 63;
                memcpy(no->name, p->name, nlen);
                no->name[nlen] = '\0';
                o_idx++;
            }
            /* NPCs in current quadrant */
            for(int n=0; n<lq->npc_count && o_idx < MAX_NET_OBJECTS; n++) {
                NPCShip *npc = lq->npcs[n]; if (!npc->active) continue;
                NetObject *no = &upd.objects[o_idx];
                *no = (NetObject){(float)npc->x, (float)npc->y, (float)npc->z, (float)npc->h, (float)npc->m, npc->faction, 0, 1, (int)npc->engine_health, npc->energy, 0, (int)npc->engine_health, npc->faction, npc->id+1000, npc->is_cloaked, ""};
                strncpy(no->name, get_species_name(npc->faction), 63); o_idx++;
            }
            /* Static objects in current quadrant */
                for(int p=0; p<lq->planet_count && o_idx < MAX_NET_OBJECTS; p++) if(lq->planets[p]->active) upd.objects[o_idx++] = (NetObject){(float)lq->planets[p]->x, (float)lq->planets[p]->y, (float)lq->planets[p]->z, 0, 0, 5, lq->planets[p]->resource_type, 1, 100, 0, 0, 100, 0, lq->planets[p]->id+3000, 0, "Planet"};
                for(int s=0; s<lq->star_count && o_idx < MAX_NET_OBJECTS; s++) if(lq->stars[s]->active) upd.objects[o_idx++] = (NetObject){(float)lq->stars[s]->x, (float)lq->stars[s]->y, (float)lq->stars[s]->z, 0, 0, 4, lq->stars[s]->id % 7, 1, 100, 0, 0, 100, 0, lq->stars[s]->id+4000, 0, "Star"};
                for(int h=0; h<lq->bh_count && o_idx < MAX_NET_OBJECTS; h++) if(lq->black_holes[h]->active) upd.objects[o_idx++] = (NetObject){(float)lq->black_holes[h]->x, (float)lq->black_holes[h]->y, (float)lq->black_holes[h]->z, 0, 0, 6, 0, 1, 100, 0, 0, 100, 0, lq->black_holes[h]->id+7000, 0, "Black Hole"};
                for(int b=0; b<lq->base_count && o_idx < MAX_NET_OBJECTS; b++) if(lq->bases[b]->active) upd.objects[o_idx++] = (NetObject){(float)lq->bases[b]->x, (float)lq->bases[b]->y, (float)lq->bases[b]->z, 0, 0, 3, 0, 1, 100, 0, 0, 100, 0, lq->bases[b]->id+2000, 0, "Starbase"};
                for(int n=0; n<lq->nebula_count && o_idx < MAX_NET_OBJECTS; n++) upd.objects[o_idx++] = (NetObject){(float)lq->nebulas[n]->x, (float)lq->nebulas[n]->y, (float)lq->nebulas[n]->z, 0, 0, 7, lq->nebulas[n]->type, 1, 100, 0, 0, 100, 0, lq->nebulas[n]->id+8000, 0, "Tactical Cruiser"};
                for(int p=0; p<lq->pulsar_count && o_idx < MAX_NET_OBJECTS; p++) upd.objects[o_idx++] = (NetObject){(float)lq->pulsars[p]->x, (float)lq->pulsars[p]->y, (float)lq->pulsars[p]->z, 0, 0, 8, 0, 1, 100, 0, 0, 100, 0, lq->pulsars[p]->id+9000, 0, "Pulsar"};
                for(int c=0; c<lq->comet_count && o_idx < MAX_NET_OBJECTS; c++) upd.objects[o_idx++] = (NetObject){(float)lq->comets[c]->x, (float)lq->comets[c]->y, (float)lq->comets[c]->z, (float)lq->comets[c]->h, (float)lq->comets[c]->m, 9, 0, 1, 100, 0, 0, 100, 0, lq->comets[c]->id+10000, 0, "Comet"};
                for(int a=0; a<lq->asteroid_count && o_idx < MAX_NET_OBJECTS; a++) upd.objects[o_idx++] = (NetObject){(float)lq->asteroids[a]->x, (float)lq->asteroids[a]->y, (float)lq->asteroids[a]->z, 0, 0, 21, lq->asteroids[a]->resource_type, 1, 100, lq->asteroids[a]->amount, 0, 100, 0, lq->asteroids[a]->id+12000, 0, "Asteroid"};
                for(int d=0; d<lq->derelict_count && o_idx < MAX_NET_OBJECTS; d++) upd.objects[o_idx++] = (NetObject){(float)lq->derelicts[d]->x, (float)lq->derelicts[d]->y, (float)lq->derelicts[d]->z, 0, 0, 22, lq->derelict_count, 1, 30, 0, 0, 100, 0, lq->derelicts[d]->id+11000, 0, "Derelict"};
                for(int pt=0; pt<lq->platform_count && o_idx < MAX_NET_OBJECTS; pt++) upd.objects[o_idx++] = (NetObject){(float)lq->platforms[pt]->x, (float)lq->platforms[pt]->y, (float)lq->platforms[pt]->z, 0, 0, 25, 0, 1, (int)((lq->platforms[pt]->energy/10000.0)*100), (int)lq->platforms[pt]->energy, 0, 100, lq->platforms[pt]->faction, lq->platforms[pt]->id+16000, 0, "Defense Platform"};
                for(int bu=0; bu<lq->buoy_count && o_idx < MAX_NET_OBJECTS; bu++) if(lq->buoys[bu]->active) upd.objects[o_idx++] = (NetObject){(float)lq->buoys[bu]->x, (float)lq->buoys[bu]->y, (float)lq->buoys[bu]->z, 0, 0, 24, 0, 1, 100, 0, 0, 100, 0, lq->buoys[bu]->id+15000, 0, "Comm Buoy"};
                for(int mn=0; mn<lq->mine_count && o_idx < MAX_NET_OBJECTS; mn++) if(lq->mines[mn]->active) upd.objects[o_idx++] = (NetObject){(float)lq->mines[mn]->x, (float)lq->mines[mn]->y, (float)lq->mines[mn]->z, 0, 0, 23, 0, 1, 100, 0, 0, 100, lq->mines[mn]->faction, lq->mines[mn]->id+14000, 0, "Space Mine"};
                for(int rf=0; rf<lq->rift_count && o_idx < MAX_NET_OBJECTS; rf++) if(lq->rifts[rf]->active) upd.objects[o_idx++] = (NetObject){(float)lq->rifts[rf]->x, (float)lq->rifts[rf]->y, (float)lq->rifts[rf]->z, 0, 0, 26, 0, 1, 100, 0, 0, 100, 0, lq->rifts[rf]->id+17000, 0, "Spatial Rift"};
                for(int mo=0; mo<lq->monster_count && o_idx < MAX_NET_OBJECTS; mo++) { NetObject *no = &upd.objects[o_idx++]; *no = (NetObject){(float)lq->monsters[mo]->x, (float)lq->monsters[mo]->y, (float)lq->monsters[mo]->z, 0, 0, lq->monsters[mo]->type, 0, 1, 100, (int)lq->monsters[mo]->energy, 0, 100, 0, lq->monsters[mo]->id+18000, 0, ""}; strncpy(no->name, (lq->monsters[mo]->type==30)?"Crystalline Entity":"Space Amoeba", 63); }
            /* Global Probes: Check ALL probes from ALL players */
            for (int p_j = 0; p_j < MAX_CLIENTS; p_j++) {
                if (!players[p_j].socket) continue;
                for (int pr = 0; pr < 3; pr++) {
                    if (players[p_j].state.probes[pr].active && o_idx < MAX_NET_OBJECTS) {
                        /* Check if this probe is in player i's current quadrant */
                        int pr_q1 = get_q_from_g(players[p_j].state.probes[pr].gx);
                        int pr_q2 = get_q_from_g(players[p_j].state.probes[pr].gy);
                        int pr_q3 = get_q_from_g(players[p_j].state.probes[pr].gz);
                        
                        if (pr_q1 == upd.q1 && pr_q2 == upd.q2 && pr_q3 == upd.q3) {
                            NetObject *no = &upd.objects[o_idx++];
                            no->net_x = players[p_j].state.probes[pr].s1;
                            no->net_y = players[p_j].state.probes[pr].s2;
                            no->net_z = players[p_j].state.probes[pr].s3;
                            no->type = 27; /* TYPE_PROBE */
                            no->id = 19000 + (p_j * 3) + pr; /* Unique ID range for probes */
                            no->ship_class = players[p_j].state.probes[pr].status; /* Pass status here */
                            no->is_cloaked = 0;
                            snprintf(no->name, 64, "P:%.58s", players[p_j].name);
                            no->active = 1;
                        }
                    }
                }
            }
        }

        upd.object_count = o_idx;
        upd.beam_count = players[i].state.beam_count; for(int b=0; b<upd.beam_count && b<MAX_NET_BEAMS; b++) upd.beams[b] = players[i].state.beams[b];
        
        /* Map Synchronizer: Always send supernova quadrant if active, otherwise send current */
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

        upd.torp = players[i].state.torp; upd.boom = players[i].state.boom; upd.dismantle = players[i].state.dismantle; 
        upd.wormhole = players[i].state.wormhole;
        upd.jump_arrival = players[i].state.jump_arrival;
        upd.recovery_fx = players[i].state.recovery_fx;
        for(int p=0; p<3; p++) upd.probes[p] = players[i].state.probes[p];
        
        if (supernova_event.supernova_timer > 0) {
            upd.supernova_pos = (NetPoint){(float)supernova_event.x, (float)supernova_event.y, (float)supernova_event.z, supernova_event.supernova_timer};
            upd.supernova_q[0] = supernova_event.supernova_q1;
            upd.supernova_q[1] = supernova_event.supernova_q2;
            upd.supernova_q[2] = supernova_event.supernova_q3;
        } else {
            upd.supernova_pos.active = 0;
        }

        players[i].state.beam_count = 0; players[i].state.boom.active = 0; players[i].state.dismantle.active = 0;
        if (players[i].state.recovery_fx.active > 0) players[i].state.recovery_fx.active--;
        int current_sock = players[i].socket;
        if (current_sock != 0) { 
            size_t p_size = sizeof(PacketUpdate) - sizeof(NetObject) * (MAX_NET_OBJECTS - upd.object_count); 
            if (p_size < offsetof(PacketUpdate, objects)) p_size = offsetof(PacketUpdate, objects); 
            
            pthread_mutex_lock(&players[i].socket_mutex);
            write_all(current_sock, &upd, p_size); 
            pthread_mutex_unlock(&players[i].socket_mutex);
        }
    }
    pthread_mutex_unlock(&game_mutex);
}
        
