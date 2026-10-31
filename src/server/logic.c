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
 * SPACE GL - SERVER TICK & NAVIGATION LOGIC
 *
 * Translation Unit: logic.c
 * Role: Main game tick loop, NPC AI scheduling, player navigation
 * state machine, and client update packet distribution.
 * Peer modules: logic_events.c, commands.c, targets.c.
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



/* --- Ship collisions with solid objects -------------------------------- */

/* Ticks left before ship i can take damage from another impact (the ship is
 * stopped on the surface of the object, so a player pushing on at once would
 * otherwise be hurt on every tick). Only the player loop below touches it. */
static int g_collision_grace[MAX_CLIENTS];

/* Called once per tick after the movement of ship i, which went from
 * (ox, oy, oz) to its current galactic position while in state `nav_before`.
 * When that segment enters a solid object (obstacles.h) the ship is put on
 * the surface, the engines are dropped and the hull takes damage that grows
 * with the speed at impact. The target of an approach/chase is not an obstacle.
 * The test is on the segment, not on the end point: at warp 9.9 a tick is
 * ~4.6 units, far more than the 0.8 contact radius. */
static void ship_collision_check(int i, int nav_before, int skip_before, double ox, double oy, double oz) {
    if (g_collision_grace[i] > 0) g_collision_grace[i]--;
    switch (nav_before) {
    case NAV_STATE_HYPERDRIVE:
    case NAV_STATE_IMPULSE:
    case NAV_STATE_APPROACH:
    case NAV_STATE_CHASE:
    case NAV_STATE_DRIFT:
        break;
    default:
        return;
    }
    double p0[3] = { ox, oy, oz };
    double p1[3] = { players[i].gx, players[i].gy, players[i].gz };
    double d[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
    double len = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 1e-9) return;

    /* The target of an approach / chase, as it was BEFORE this tick's movement:
     * an approach that ends (arrival) clears apr_target in the same tick, and
     * the ship would then "crash" into the very object it was flying to. */
    int skip_id = skip_before;

    ObsHit hit;
    if (!obs_segment_hit(p0, p1, 0.0, OBS_MASK_ALL, skip_id, &hit)) return;

    /* a comet that is run into does not survive the crash (the ship still
     * stops and takes the damage below) */
    comet_destroyed_by_ship(&hit);

    /* stop on the surface, just outside the contact distance */
    double n[3] = { hit.point[0] - hit.centre[0], hit.point[1] - hit.centre[1], hit.point[2] - hit.centre[2] };
    double nl = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (nl < 1e-9) { n[0] = -d[0]; n[1] = -d[1]; n[2] = -d[2]; nl = len; }
    double surf = hit.r_hit + 0.001;
    players[i].gx = hit.centre[0] + n[0] / nl * surf;
    players[i].gy = hit.centre[1] + n[1] / nl * surf;
    players[i].gz = hit.centre[2] + n[2] / nl * surf;
    players[i].state.q1 = get_q_from_g(players[i].gx);
    players[i].state.q2 = get_q_from_g(players[i].gy);
    players[i].state.q3 = get_q_from_g(players[i].gz);
    players[i].state.s1 = players[i].gx - (players[i].state.q1 - 1) * QUADRANT_SIZE;
    players[i].state.s2 = players[i].gy - (players[i].state.q2 - 1) * QUADRANT_SIZE;
    players[i].state.s3 = players[i].gz - (players[i].state.q3 - 1) * QUADRANT_SIZE;

    players[i].nav_state = NAV_STATE_IDLE;
    players[i].hyper_speed = 0;
    players[i].eta = 0;
    players[i].dx = 0; players[i].dy = 0; players[i].dz = 0;
    players[i].target_gx = -1.0;
    players[i].apr_target = 0;

    if (g_collision_grace[i] > 0) return;
    g_collision_grace[i] = TIMER_COLLISION_GRACE;

    double speed_ups = len * GAME_TICK_RATE;
    double dmg = DMG_COLLISION_BASE + DMG_COLLISION_PER_UPS * speed_ups;
    if (dmg > DMG_COLLISION_MAX) dmg = DMG_COLLISION_MAX;

    char msg[128];
    snprintf(msg, sizeof(msg), "COLLISION: impact with a %s at %.0f units/s. Engines disengaged.",
             obs_kind_name(hit.kind), speed_ups);
    send_server_msg(i, "COMPUTER", msg);
    apply_hull_damage(i, dmg);
    #pragma omp critical
    broadcast_server_event(players[i].state.q1, players[i].state.q2, players[i].state.q3, IPC_EV_IMPACT,
                           players[i].state.s1, players[i].state.s2, players[i].state.s3, 0, 0, 0, 0);
}

void update_game_logic() {
    /* global_tick is already incremented in the calling thread loop */
    unsigned int ai_seed = (unsigned int)global_tick;
    /* Autosave every 60 seconds */
    if (global_tick % (60 * GAME_TICK_RATE) == 0) {
        save_galaxy_async();
    }

    /* Natural moons: slow individual orbits around their planet */
    update_moon_orbits();
    update_star_systems();

    /* Refresh LRS Grid once per second */
    if (global_tick % GAME_TICK_RATE == 0) {
        extern void refresh_lrs_grid();
        refresh_lrs_grid();
    }

    /* High-Energy Nebula Antimatter Refill */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!players[i].active) continue;
        int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
        QuadrantIndex *lq = &spatial_index[pq1][pq2][pq3];
        for (int n = 0; n < lq->nebula_count; n++) {
            NPCNebula *nb = lq->nebulas[n];
            if (nb->type == 1) { /* High-Energy Nebula */
                double dx = nb->x - players[i].state.s1;
                double dy = nb->y - players[i].state.s2;
                double dz = nb->z - players[i].state.s3;
                if (sqrt(dx*dx + dy*dy + dz*dz) < DIST_NEBULA_EFFECT) {
                    static double am_acc[MAX_CLIENTS] = {0};
                    am_acc[i] += (10.0 / (double)GAME_TICK_RATE);
                    if (am_acc[i] >= 1.0) {
                        int add = (int)am_acc[i];
                        players[i].state.anti_matter_count += add;
                        am_acc[i] -= add;
                    }
                }
            }
        }
    }

    logic_update_death_timers(&ai_seed);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!players[i].active) {
            g_collision_grace[i] = 0;   /* a new pilot in this slot starts without grace */
            continue;
        }
        
        double old_gx = players[i].gx;
        double old_gy = players[i].gy;
        double old_gz = players[i].gz;
        int nav_before = players[i].nav_state;
        int skip_before = 0;     /* target of an approach / chase: not an obstacle (see ship_collision_check) */
        if (nav_before == NAV_STATE_APPROACH) skip_before = players[i].apr_target;
        else if (nav_before == NAV_STATE_CHASE) skip_before = players[i].state.lock_target;
        
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
                    slog("\033[1;31m[DESTRUCTION]\033[0m Captain \033[1;37m%-15s\033[0m ship has been destroyed. [\033[1;33m%s\033[0m]\n", 
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
                rescue_player(i, &RESCUE_PARAMS_CREW, &ai_seed);

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
                             /* Fix: Ensure at least 1 unit recharges if regen_rate > 0 to avoid int truncation stalling */
                             int actual_regen = (int)regen_rate;
                             if (actual_regen == 0 && regen_rate > 0.001) actual_regen = 1;
                             
                             players[i].state.shields[s] += actual_regen;
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

        /* Sync Torpedo ETA with actual active torpedoes - MOVED BELOW PHYSICAL UPDATES */
        for(int t=0; t<4; t++) {
            if (players[i].tube_load_timers[t] > 0) {
                players[i].tube_load_timers[t]--;
            }
        }
        if (players[i].torp_load_timer > 0) {
            players[i].torp_load_timer--;
        }

        /* tube_state will be updated in the Late Sync loop below for perfect accuracy */

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
            } else if (player_by_id(tid) != NULL) { 
                ConnectedPlayer *target_p = player_by_id(tid);
                if (target_p->active && 
                    target_p->state.q1 == pq1 && 
                    target_p->state.q2 == pq2 && 
                    target_p->state.q3 == pq3) {
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
            } else {
                /* All static object types: single table-driven lookup (targets.c) */
                valid = target_is_active_in_quadrant(tid, pq1, pq2, pq3);
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

            double init_t = (players[i].align_timer > 0) ? players[i].align_timer : 60.0;
            double t = 1.0 - players[i].nav_timer / init_t;
            /* Shortest-arc interpolation, re-wrapped into [0, 360) so a
             * turn across the 0/360 boundary never leaves the range
             * (B2: the HUD must not display headings above 360). */
            players[i].state.van_h = nav_heading_at(players[i].start_h, players[i].target_h, t);
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

            double init_t = (players[i].align_timer > 0) ? players[i].align_timer : 60.0;
            double t = 1.0 - players[i].nav_timer / init_t;
            players[i].state.van_h = nav_heading_at(players[i].start_h, players[i].target_h, t);
            players[i].state.van_m = (players[i].start_m + (players[i].target_m - players[i].start_m) * t);
            /* Shortest-arc interpolation, re-wrapped into [0, 360) so a
             * roll turn across the 0/360 boundary never leaves the range
             * (same contract as the B2 heading fix: the HUD displays
             * van_r as-is and must never show a roll above 360). */
            players[i].state.van_r = nav_roll_at(players[i].start_r, players[i].target_r, t);
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
            
            ConnectedPlayer *target_p = player_by_id(tid);
            NPCShip *target_npc = npc_by_id(tid);
            if (target_p) { 
                if (target_p->active) {
                    tx = target_p->gx;
                    ty = target_p->gy;
                    tz = target_p->gz;
                    found = true;
                } 
            } else if (target_npc) { 
                if (target_npc->active) {
                    tx = target_npc->gx;
                    ty = target_npc->gy;
                    tz = target_npc->gz;
                    found = true;
                } 
            } else {
                QuadrantIndex *lq = &spatial_index[q1][q2][q3];
                /* Table-driven resolution (targets.c): local spatial-index
                   lookup first, then the cross-quadrant global fallback. */
                const TargetRangeDef *tr = target_range_for(tid);

                /* 1. Local objects check (Fast path) */
                if (tr && (tr->flags & TGT_F_APR_LOCAL)) {
                    const void *obj = target_find_local(tr, lq, tid);
                    if (obj) {
                        target_abs_pos(tr, obj, &tx, &ty, &tz);
                        found = true;
                    }
                }

                /* 2. Global fallback (Cross-quadrant tracking) */
                if (!found && tr && (tr->flags & TGT_F_APR_GLOBAL)) {
                    const void *obj = target_find_global_active(tid);
                    if (obj) {
                        target_abs_pos(tr, obj, &tx, &ty, &tz);
                        found = true;
                    }
                }

                /* 3. Player probes live in the player state, not the index */
                if (!found && tid >= GALAXY_OBJECT_MIN_PROBE && tid <= GALAXY_OBJECT_MAX_PROBE) {
                    int p_idx = (tid - GALAXY_OBJECT_MIN_PROBE) / 3;
                    int pr_idx = (tid - GALAXY_OBJECT_MIN_PROBE) % 3;
                    if (p_idx < MAX_CLIENTS && players[p_idx].state.probes[pr_idx].active) {
                        tx = players[p_idx].state.probes[pr_idx].gx;
                        ty = players[p_idx].state.probes[pr_idx].gy;
                        tz = players[p_idx].state.probes[pr_idx].gz;
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
            logic_handle_wormhole_nav(i);
        } else if (players[i].nav_state == NAV_STATE_CHASE) {
            int tid = players[i].state.lock_target;
            double tx = 0;
            double ty = 0;
            double tz = 0;
            bool found = false;
            if (tid >= GALAXY_OBJECT_MIN_PLAYER && tid <= GALAXY_OBJECT_MAX_PLAYER) {
                if (tid <= MAX_CLIENTS && players[tid - 1].active) {
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
            } else if (tid >= GALAXY_OBJECT_MIN_PROBE && tid <= GALAXY_OBJECT_MAX_PROBE) {
                int p_idx = (tid - GALAXY_OBJECT_MIN_PROBE) / 3;
                int pr_idx = (tid - GALAXY_OBJECT_MIN_PROBE) % 3;
                if (p_idx < MAX_CLIENTS && players[p_idx].state.probes[pr_idx].active) {
                    tx = players[p_idx].state.probes[pr_idx].gx;
                    ty = players[p_idx].state.probes[pr_idx].gy;
                    tz = players[p_idx].state.probes[pr_idx].gz;
                    found = true;
                }
            } else {
                /* Table-driven global lookup (targets.c): comets & monsters */
                const TargetRangeDef *tr = target_range_for(tid);
                if (tr && (tr->flags & TGT_F_CHASE)) {
                    const void *obj = target_find_global_active(tid);
                    if (obj) {
                        target_abs_pos(tr, obj, &tx, &ty, &tz);
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
                double target_dist = 2.0; /* Default chase distance */
                NPCShip *target_npc = npc_by_id(tid);
                if (target_npc) {
                    target_dist = (target_npc->health > (YIELD_MINE_MAX)) ? 3.0 : 1.5;
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
                const char *sender = (b_idx >= 0 && b_idx < MAX_BASES) ? get_species_name(bases[b_idx].faction) : "STARBASE";
                send_server_msg(i, sender, "Docking complete. Systems replenished and repaired.");
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

        ship_collision_check(i, nav_before, skip_before, old_gx, old_gy, old_gz);

        /* torp_active will be updated in the Late Sync loop below for better accuracy */

        players[i].vx = players[i].gx - old_gx;
        players[i].vy = players[i].gy - old_gy;
        players[i].vz = players[i].gz - old_gz;
    }

    /* Global Torpedo System Update Loop - High Performance Spatial Filtering */
    /* Run event passes: torpedoes, supernova, monsters, proximity, starbase defense */
    logic_generate_events(&ai_seed);

    /* 3. Send Updates to clients */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].socket == 0 || !players[i].active) {
            continue;
        }
        PacketUpdate *upd = &players[i].upd_packet;
        memset(upd, 0, sizeof(PacketUpdate));
        upd->type = PKT_UPDATE;
        
        upd->frame_id = global_tick;
        upd->q1 = players[i].state.q1; 
        upd->q2 = players[i].state.q2; 
        upd->q3 = players[i].state.q3;
        upd->s1 = players[i].state.s1; upd->s2 = players[i].state.s2; upd->s3 = players[i].state.s3;
        upd->van_h = players[i].state.van_h; upd->van_m = players[i].state.van_m; upd->van_r = players[i].state.van_r;
        upd->eta = players[i].eta;
        upd->energy = players[i].state.energy; upd->hull_integrity = players[i].state.hull_integrity;
        upd->torpedoes = players[i].state.torpedoes; upd->cargo_energy = players[i].state.cargo_energy;
        upd->cargo_torpedoes = players[i].state.cargo_torpedoes; upd->crew_count = players[i].state.crew_count;
        upd->prison_unit = players[i].state.prison_unit; upd->composite_plating = players[i].state.composite_plating;
        for(int s=0; s<6; s++) {
            upd->shields[s] = players[i].state.shields[s];
            upd->target_shields[s] = players[i].state.target_shields[s];
        }
        strncpy(upd->captain_name, players[i].state.captain_name, 63);
        upd->captain_name[63] = '\0';
        for(int inv=0; inv<10; inv++) upd->inventory[inv] = players[i].state.inventory[inv];
        for(int sys=0; sys < MAX_SYSTEMS; sys++) upd->system_health[sys] = players[i].state.system_health[sys];
        for(int p=0; p<3; p++) upd->power_dist[p] = players[i].state.power_dist[p];
        upd->life_support = players[i].state.life_support; upd->anti_matter_count = players[i].state.anti_matter_count;
        upd->lock_target = players[i].state.lock_target; upd->tube_state = players[i].state.tube_state;
        for(int t=0; t<4; t++) {
            upd->tube_load_timers[t] = players[i].tube_load_timers[t];
            upd->tube_torpedo_etas[t] = players[i].tube_torpedo_etas[t];
        }
        upd->current_tube = players[i].current_tube; upd->ion_beam_charge = players[i].state.ion_beam_charge;
        upd->is_cloaked = players[i].state.is_cloaked; upd->is_docked = players[i].is_docked;
        upd->red_alert = players[i].state.red_alert; upd->nav_state = (uint8_t)players[i].nav_state;
        upd->show_axes = players[i].state.show_axes; upd->show_grid = players[i].state.show_grid;
        upd->show_bridge = players[i].state.show_bridge; upd->show_map = players[i].state.show_map;
        upd->map_filter = players[i].state.map_filter;
        upd->shm_crypto_algo = players[i].state.shm_crypto_algo;
        upd->encryption_flags = players[i].state.encryption_flags;
        upd->radio_lock_target = players[i].radio_lock_target;
        
        int o_idx = 0;
        double pulse = 0.5 + 0.5 * sin(global_tick * 0.05);
        upd->objects[o_idx] = (NetObject){players[i].state.s1, players[i].state.s2, players[i].state.s3, players[i].state.van_h, players[i].state.van_m, players[i].state.van_r, OBJ_PLAYER, players[i].ship_class, 1, (int)players[i].state.hull_integrity, players[i].state.energy, 0, (int)players[i].state.hull_integrity, players[i].faction, i+1, players[i].state.is_cloaked, "", players[i].vx, players[i].vy, players[i].vz};
        strncpy(upd->objects[o_idx].name, players[i].name, 63); upd->objects[o_idx].name[63] = '\0';
        o_idx++;

        QuadrantIndex *lq = &spatial_index[upd->q1][upd->q2][upd->q3];
        for(int n=0; n<lq->npc_count && o_idx < MAX_NET_OBJECTS; n++) {
            NPCShip *npc = lq->npcs[n]; if (!npc->active) continue;
            upd->objects[o_idx] = (NetObject){npc->x, npc->y, npc->z, npc->h, npc->m, 0.0, npc->faction, npc->ship_class, 1, (int)(npc->health / (int)THRESHOLD_SYS_CRITICAL), npc->energy, npc->plating, (int)(npc->health / (int)THRESHOLD_SYS_CRITICAL), npc->faction, npc->id + GALAXY_OBJECT_MIN_NPC, npc->is_cloaked, "", npc->vx, npc->vy, npc->vz};
            snprintf(upd->objects[o_idx].name, 64, "%s", npc->name); o_idx++;
        }
        for(int j=0; j<lq->player_count && o_idx < MAX_NET_OBJECTS; j++) {
            ConnectedPlayer *p = lq->players[j]; if (!p->active || p == &players[i]) continue;
            upd->objects[o_idx] = (NetObject){p->state.s1, p->state.s2, p->state.s3, p->state.van_h, p->state.van_m, p->state.van_r, OBJ_PLAYER, p->ship_class, 1, (int)p->state.hull_integrity, p->state.energy, 0, (int)p->state.hull_integrity, p->faction, (int)(p - players) + GALAXY_OBJECT_MIN_PLAYER, p->state.is_cloaked, "", p->vx, p->vy, p->vz};
            snprintf(upd->objects[o_idx].name, 64, "%s", p->name); o_idx++;
        }
        /* ... celestial objects ... */
        for(int s=0; s<lq->star_count && o_idx < MAX_NET_OBJECTS; s++) { NPCStar *st = lq->stars[s]; if(!st->active) continue; upd->objects[o_idx++] = (NetObject){st->x, st->y, st->z, 0, 0, 0, OBJ_STAR, ((unsigned)st->faction < 7u ? st->faction : 4), 1, 100, 0, 0, 100, OBJ_STAR, st->id + GALAXY_OBJECT_MIN_STAR, 0, "Star", 0,0,0}; }
        for(int p=0; p<lq->planet_count && o_idx < MAX_NET_OBJECTS; p++) { NPCPlanet *pl = lq->planets[p]; if(!pl->active) continue; upd->objects[o_idx++] = (NetObject){pl->x, pl->y, pl->z, 0, 0, 0, OBJ_PLANET, ((unsigned)pl->planet_class < (unsigned)PLANET_CLASS_COUNT ? pl->planet_class : PLANET_CLASS_TERRESTRIAL), 1, 100, pl->amount, pl->resource_type, 100, OBJ_PLANET, pl->id + GALAXY_OBJECT_MIN_PLANET, 0, "Planet", 0,0,0}; }
        /* Star system of this quadrant: star, planets (orbiting it) and moons (orbiting them).
         * Their ids are the full ids of the system bands; positions are the cached ones
         * (update_star_systems() refreshes the systems players are in every tick). */
        if (lq->sys_star_count > 0 && lq->sys_stars[0] && lq->sys_stars[0]->active) {
            NPCSysStar *ss = lq->sys_stars[0];
            if (o_idx < MAX_NET_OBJECTS)
                upd->objects[o_idx++] = (NetObject){.net_x = ss->x, .net_y = ss->y, .net_z = ss->z, .type = OBJ_STAR,
                    .ship_class = ((unsigned)ss->star_class < 7u ? ss->star_class : 4), .active = 1, .health_pct = 100,
                    .hull_integrity = 100, .faction = OBJ_STAR, .id = ss->id, .name = "Star"};
            for (int sp = 0; sp < lq->sys_planet_count && o_idx < MAX_NET_OBJECTS; sp++) {
                NPCSysPlanet *spl = lq->sys_planets[sp]; if (!spl || !spl->active) continue;
                upd->objects[o_idx++] = (NetObject){.net_x = spl->x, .net_y = spl->y, .net_z = spl->z, .type = OBJ_PLANET,
                    .ship_class = ((unsigned)spl->planet_class < (unsigned)PLANET_CLASS_COUNT ? spl->planet_class : PLANET_CLASS_TERRESTRIAL),
                    .active = 1, .health_pct = 100, .energy = (uint64_t)spl->amount, .plating = spl->resource_type,
                    .hull_integrity = 100, .faction = OBJ_PLANET, .id = spl->id, .name = "Planet"};
            }
            for (int sm = 0; sm < lq->sys_moon_count && o_idx < MAX_NET_OBJECTS; sm++) {
                NPCSysMoon *smo = lq->sys_moons[sm]; if (!smo || !smo->active) continue;
                upd->objects[o_idx] = (NetObject){.net_x = smo->x, .net_y = smo->y, .net_z = smo->z, .type = smo->moon_type,
                    .active = 1, .id = smo->id};
                const char *mn = obj_type_name(smo->moon_type);
                snprintf(upd->objects[o_idx].name, 64, "%s", mn ? mn : "Moon"); o_idx++;
            }
        }
        for(int b=0; b<lq->base_count && o_idx < MAX_NET_OBJECTS; b++) { 
            NPCBase *ba = lq->bases[b]; if(!ba->active) continue; 
            upd->objects[o_idx] = (NetObject){ba->x, ba->y, ba->z, 0, 0, 0, OBJ_BASE, 1, 1, (int)(ba->health/(COST_ACTION_MED * 2)), 0, 0, (int)(ba->health/(COST_ACTION_MED * 2)), ba->faction, ba->id + GALAXY_OBJECT_MIN_STARBASE, 0, "", 0,0,0}; 
            snprintf(upd->objects[o_idx].name, 64, "%s Starbase", get_species_name(ba->faction)); o_idx++;
        }
        for(int h=0; h<lq->bh_count && o_idx < MAX_NET_OBJECTS; h++) { NPCBlackHole *bh = lq->black_holes[h]; if(!bh->active) continue; upd->objects[o_idx++] = (NetObject){bh->x, bh->y, bh->z, 0, 0, 0, OBJ_BLACK_HOLE, 0, 1, 100, 0, 0, 100, OBJ_BLACK_HOLE, bh->id + GALAXY_OBJECT_MIN_BLACKHOLE, 0, "Black Hole", 0,0,0}; }
        for(int n=0; n<lq->nebula_count && o_idx < MAX_NET_OBJECTS; n++) { NPCNebula *nb = lq->nebulas[n]; if(!nb->active) continue; upd->objects[o_idx++] = (NetObject){nb->x, nb->y, nb->z, 0, 0, 0, OBJ_NEBULA, nb->type, 1, 100, 0, 0, 100, OBJ_NEBULA, nb->id + GALAXY_OBJECT_MIN_NEBULA, 0, "Nebula", 0,0,0}; }
        for(int p=0; p<lq->pulsar_count && o_idx < MAX_NET_OBJECTS; p++) { NPCPulsar *pu = lq->pulsars[p]; if(!pu->active) continue; upd->objects[o_idx++] = (NetObject){pu->x, pu->y, pu->z, 0, 0, 0, OBJ_PULSAR, pu->type, 1, 100, 0, 0, 100, OBJ_PULSAR, pu->id + GALAXY_OBJECT_MIN_PULSAR, 0, "Pulsar", 0,0,0}; }
        for(int d=0; d<lq->dyson_count && o_idx < MAX_NET_OBJECTS; d++) { NPCDyson *dy = lq->dysons[d]; if(!dy->active) continue; upd->objects[o_idx++] = (NetObject){dy->x, dy->y, dy->z, pulse, pulse*0.5, 0, OBJ_DYSON_FRAGMENT, 0, 1, 100, 0, 0, 100, OBJ_DYSON_FRAGMENT, dy->id + GALAXY_OBJECT_MIN_DYSON, 0, "Dyson Fragment", 0,0,0}; }
        for(int h=0; h<lq->hub_count && o_idx < MAX_NET_OBJECTS; h++) { NPCHub *hb = lq->hubs[h]; if(!hb->active) continue; upd->objects[o_idx++] = (NetObject){hb->x, hb->y, hb->z, 0, pulse*0.1, 0, OBJ_TRADING_HUB, 0, 1, 100, 0, 0, 100, OBJ_TRADING_HUB, hb->id + GALAXY_OBJECT_MIN_HUB, 0, "Trading Hub", 0,0,0}; }
        for(int r=0; r<lq->relic_count && o_idx < MAX_NET_OBJECTS; r++) { NPCRelic *re = lq->relics[r]; if(!re->active) continue; upd->objects[o_idx++] = (NetObject){re->x, re->y, re->z, pulse*0.3, pulse*0.7, 0, OBJ_ANCIENT_RELIC, 0, 1, 100, 0, 0, 100, OBJ_ANCIENT_RELIC, re->id + GALAXY_OBJECT_MIN_RELIC, 0, "Ancient Relic", 0,0,0}; }
        for(int ru=0; ru<lq->rupture_count && o_idx < MAX_NET_OBJECTS; ru++) { NPCRupture *rp = lq->ruptures[ru]; if(!rp->active) continue; upd->objects[o_idx++] = (NetObject){rp->x, rp->y, rp->z, 0, 0, 0, OBJ_SUBSPACE_RUPTURE, 0, 1, 100, 0, 0, 100, OBJ_SUBSPACE_RUPTURE, rp->id + GALAXY_OBJECT_MIN_RUPTURE, 0, "Subspace Rupture", 0,0,0}; }
        for(int sa=0; sa<lq->satellite_count && o_idx < MAX_NET_OBJECTS; sa++) { NPCSatellite *st = lq->satellites[sa]; if(!st->active) continue; upd->objects[o_idx++] = (NetObject){st->x, st->y, st->z, pulse, 0, 0, OBJ_SATELLITE, 0, 1, 100, 0, 0, 100, OBJ_SATELLITE, st->id + GALAXY_OBJECT_MIN_SATELLITE, 0, "Satellite", 0,0,0}; }
        for(int so=0; so<lq->storm_count && o_idx < MAX_NET_OBJECTS; so++) { NPCStorm *sm = lq->storms[so]; if(!sm->active) continue; upd->objects[o_idx++] = (NetObject){sm->x, sm->y, sm->z, 0, 0, 0, OBJ_ION_STORM, 0, 1, 100, 0, 0, 100, OBJ_ION_STORM, sm->id + GALAXY_OBJECT_MIN_STORM, 0, "Ion Storm", 0,0,0}; }
        for(int a=0; a<lq->artifact_count && o_idx < MAX_NET_OBJECTS; a++) { NPCArtifact *obj = lq->artifacts[a]; if(!obj->active) continue; upd->objects[o_idx++] = (NetObject){obj->x, obj->y, obj->z, 0, 0, 0, OBJ_ALIEN_ARTIFACT, 0, 1, 100, 0, 0, 100, OBJ_ALIEN_ARTIFACT, obj->id + GALAXY_OBJECT_MIN_ARTIFACT, 0, "Alien Artifact", 0,0,0}; }
        for(int a=0; a<lq->warp_gate_count && o_idx < MAX_NET_OBJECTS; a++) { NPCWarpGate *obj = lq->warp_gates[a]; if(!obj->active) continue; upd->objects[o_idx++] = (NetObject){obj->x, obj->y, obj->z, 0, 0, 0, OBJ_WARP_GATE, 0, 1, 100, 0, 0, 100, OBJ_WARP_GATE, obj->id + GALAXY_OBJECT_MIN_WARP_GATE, 0, "Warp Gate", 0,0,0}; }
        for(int a=0; a<lq->neutron_star_count && o_idx < MAX_NET_OBJECTS; a++) { NPCNeutronStar *obj = lq->neutron_stars[a]; if(!obj->active) continue; upd->objects[o_idx++] = (NetObject){obj->x, obj->y, obj->z, 0, 0, 0, OBJ_NEUTRON_STAR, 0, 1, 100, 0, 0, 100, OBJ_NEUTRON_STAR, obj->id + GALAXY_OBJECT_MIN_NEUTRON_STAR, 0, "Neutron Star", 0,0,0}; }
        for(int a=0; a<lq->mega_struct_count && o_idx < MAX_NET_OBJECTS; a++) { NPCMegaStructure *obj = lq->mega_structs[a]; if(!obj->active) continue; upd->objects[o_idx++] = (NetObject){obj->x, obj->y, obj->z, 0, 0, 0, OBJ_MEGA_STRUCTURE, 0, 1, 100, 0, 0, 100, OBJ_MEGA_STRUCTURE, obj->id + GALAXY_OBJECT_MIN_MEGA_STRUCT, 0, "Mega Structure", 0,0,0}; }
        for(int a=0; a<lq->dark_cloud_count && o_idx < MAX_NET_OBJECTS; a++) { NPCDarkCloud *obj = lq->dark_clouds[a]; if(!obj->active) continue; upd->objects[o_idx++] = (NetObject){obj->x, obj->y, obj->z, 0, 0, 0, OBJ_DARK_CLOUD, 0, 1, 100, 0, 0, 100, OBJ_DARK_CLOUD, obj->id + GALAXY_OBJECT_MIN_DARK_CLOUD, 0, "Dark Matter Cloud", 0,0,0}; }
        for(int a=0; a<lq->singularity_count && o_idx < MAX_NET_OBJECTS; a++) { NPCSingularity *obj = lq->singularities[a]; if(!obj->active) continue; upd->objects[o_idx++] = (NetObject){obj->x, obj->y, obj->z, 0, 0, 0, OBJ_SINGULARITY, 0, 1, 100, 0, 0, 100, OBJ_SINGULARITY, obj->id + GALAXY_OBJECT_MIN_SINGULARITY, 0, "Quantum Singularity", 0,0,0}; }
        for(int a=0; a<lq->plasma_storm_count && o_idx < MAX_NET_OBJECTS; a++) { NPCPlasmaStorm *obj = lq->plasma_storms[a]; if(!obj->active) continue; upd->objects[o_idx++] = (NetObject){obj->x, obj->y, obj->z, 0, 0, 0, OBJ_PLASMA_STORM, 0, 1, 100, 0, 0, 100, OBJ_PLASMA_STORM, obj->id + GALAXY_OBJECT_MIN_PLASMA_STORM, 0, "Plasma Storm", 0,0,0}; }
        for(int a=0; a<lq->orbital_ring_count && o_idx < MAX_NET_OBJECTS; a++) { NPCOrbitalRing *obj = lq->orbital_rings[a]; if(!obj->active) continue; upd->objects[o_idx++] = (NetObject){obj->x, obj->y, obj->z, 0, 0, 0, OBJ_ORBITAL_RING, 0, 1, 100, 0, 0, 100, OBJ_ORBITAL_RING, obj->id + GALAXY_OBJECT_MIN_ORBITAL_RING, 0, "Orbital Ring", 0,0,0}; }
        for(int a=0; a<lq->time_anomaly_count && o_idx < MAX_NET_OBJECTS; a++) { NPCTimeAnomaly *obj = lq->time_anomalies[a]; if(!obj->active) continue; upd->objects[o_idx++] = (NetObject){obj->x, obj->y, obj->z, 0, 0, 0, OBJ_TIME_ANOMALY, 0, 1, 100, 0, 0, 100, OBJ_TIME_ANOMALY, obj->id + GALAXY_OBJECT_MIN_TIME_ANOMALY, 0, "Time Anomaly", 0,0,0}; }
        for(int a=0; a<lq->void_crystal_count && o_idx < MAX_NET_OBJECTS; a++) { NPCVoidCrystal *obj = lq->void_crystals[a]; if(!obj->active) continue; upd->objects[o_idx++] = (NetObject){obj->x, obj->y, obj->z, 0, 0, 0, OBJ_VOID_CRYSTAL, 0, 1, 100, 0, 0, 100, OBJ_VOID_CRYSTAL, obj->id + GALAXY_OBJECT_MIN_VOID_CRYSTAL, 0, "Void Crystal", 0,0,0}; }
        for(int a=0; a<lq->subspace_anomaly_count && o_idx < MAX_NET_OBJECTS; a++) { NPCSubspaceAnomaly *obj = lq->subspace_anomalies[a]; if(!obj->active) continue; upd->objects[o_idx++] = (NetObject){obj->x, obj->y, obj->z, 0, 0, 0, OBJ_SUBSPACE_ANOMALY, 0, 1, 100, 0, 0, 100, OBJ_SUBSPACE_ANOMALY, obj->id + GALAXY_OBJECT_MIN_SUBSPACE_ANOM, 0, "Subspace Anomaly", 0,0,0}; }
        for(int qsr=0; qsr<lq->quasar_count && o_idx < MAX_NET_OBJECTS; qsr++) { NPCQuasar *qs = lq->quasars[qsr]; if(!qs->active) continue; upd->objects[o_idx++] = (NetObject){qs->x, qs->y, qs->z, 0, 0, 0, OBJ_QUASAR, qs->type, 1, 100, 0, 0, 100, 0, qs->id + GALAXY_OBJECT_MIN_QUASAR, 0, "Quasar", 0,0,0}; }
        for(int c=0; c<lq->comet_count && o_idx < MAX_NET_OBJECTS; c++) { NPCComet *co = lq->comets[c]; if(!co->active) continue; upd->objects[o_idx++] = (NetObject){co->x, co->y, co->z, co->h, co->m, 0, OBJ_COMET, 0, 1, 100, 0, 0, 100, OBJ_COMET, co->id + GALAXY_OBJECT_MIN_COMET, 0, "Comet", 0,0,0}; }
        for(int d=0; d<lq->derelict_count && o_idx < MAX_NET_OBJECTS; d++) { NPCDerelict *de = lq->derelicts[d]; if(!de->active) continue; upd->objects[o_idx] = (NetObject){de->x, de->y, de->z, 0, 0, 0, OBJ_DERELICT, de->ship_class, 1, 100, 0, 0, 100, de->faction, de->id + GALAXY_OBJECT_MIN_DERELICT, 0, "", 0,0,0}; snprintf(upd->objects[o_idx].name, 64, "%s", de->name); o_idx++; }
        for(int a=0; a<lq->asteroid_count && o_idx < MAX_NET_OBJECTS; a++) { NPCAsteroid *as = lq->asteroids[a]; if(!as->active) continue; upd->objects[o_idx++] = (NetObject){as->x, as->y, as->z, 0, 0, 0, OBJ_ASTEROID, as->resource_type, 1, 100, as->amount, (int)(as->size * 100), 100, OBJ_ASTEROID, as->id + GALAXY_OBJECT_MIN_ASTEROID, 0, "Asteroid", 0,0,0}; }
        for(int m=0; m<lq->mine_count && o_idx < MAX_NET_OBJECTS; m++) { NPCMine *mi = lq->mines[m]; if(!mi->active) continue; upd->objects[o_idx++] = (NetObject){mi->x, mi->y, mi->z, 0, 0, 0, OBJ_MINE, 0, 1, 100, 0, 0, 100, OBJ_MINE, mi->id + GALAXY_OBJECT_MIN_MINE, 0, "Mine", 0,0,0}; }
        for(int b=0; b<lq->buoy_count && o_idx < MAX_NET_OBJECTS; b++) { NPCBuoy *bu = lq->buoys[b]; if(!bu->active) continue; upd->objects[o_idx++] = (NetObject){bu->x, bu->y, bu->z, 0, 0, 0, OBJ_BUOY, 0, 1, 100, 0, 0, 100, OBJ_BUOY, bu->id + GALAXY_OBJECT_MIN_BUOY, 0, "Comm Buoy", 0,0,0}; }
        for(int p=0; p<lq->platform_count && o_idx < MAX_NET_OBJECTS; p++) { NPCPlatform *pl = lq->platforms[p]; if(!pl->active) continue; upd->objects[o_idx++] = (NetObject){pl->x, pl->y, pl->z, 0, 0, 0, OBJ_PLATFORM, 0, 1, (int)(pl->health/(COST_ACTION_MED * 2)), 0, 0, (int)(pl->health/(COST_ACTION_MED * 2)), pl->faction, pl->id + GALAXY_OBJECT_MIN_PLATFORM, 0, "Defense Platform", 0,0,0}; }
        for(int r=0; r<lq->rift_count && o_idx < MAX_NET_OBJECTS; r++) { NPCRift *ri = lq->rifts[r]; if(!ri->active) continue; upd->objects[o_idx++] = (NetObject){ri->x, ri->y, ri->z, 0, 0, 0, OBJ_RIFT, 0, 1, 100, 0, 0, 100, OBJ_RIFT, ri->id + GALAXY_OBJECT_MIN_RIFT, 0, "Spatial Rift", 0,0,0}; }
        for(int m=0; m<lq->monster_count && o_idx < MAX_NET_OBJECTS; m++) { NPCMonster *mo = lq->monsters[m]; if(!mo->active) continue; upd->objects[o_idx] = (NetObject){mo->x, mo->y, mo->z, 0, 0, 0, mo->type, 0, 1, (int)(mo->health/MAX_TORPEDO_CAPACITY), 0, 0, (int)(mo->health/MAX_TORPEDO_CAPACITY), 30, mo->id + GALAXY_OBJECT_MIN_MONSTER, 0, "", 0,0,0}; snprintf(upd->objects[o_idx].name, 64, "%s", (mo->type==30)?"Crystalline Entity":"Space Amoeba"); o_idx++; }
        for(int i_n = 0; i_n < lq->diffuse_nebula_count && o_idx < MAX_NET_OBJECTS; i_n++) if (lq->diffuse_nebulae[i_n]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->diffuse_nebulae[i_n]->x, .net_y = lq->diffuse_nebulae[i_n]->y, .net_z = lq->diffuse_nebulae[i_n]->z, .type = OBJ_DIFFUSE_NEBULA, .id = lq->diffuse_nebulae[i_n]->id + GALAXY_OBJECT_MIN_DIFFUSE_NEBULA, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Diffuse Neb"); o_idx++; }
        for(int i_n = 0; i_n < lq->dark_nebula_count && o_idx < MAX_NET_OBJECTS; i_n++) if (lq->dark_nebulae[i_n]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->dark_nebulae[i_n]->x, .net_y = lq->dark_nebulae[i_n]->y, .net_z = lq->dark_nebulae[i_n]->z, .type = OBJ_DARK_NEBULA, .id = lq->dark_nebulae[i_n]->id + GALAXY_OBJECT_MIN_DARK_NEBULA, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Dark Nebula"); o_idx++; }
        for(int i_n = 0; i_n < lq->planetary_nebula_count && o_idx < MAX_NET_OBJECTS; i_n++) if (lq->planetary_nebulae[i_n]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->planetary_nebulae[i_n]->x, .net_y = lq->planetary_nebulae[i_n]->y, .net_z = lq->planetary_nebulae[i_n]->z, .type = OBJ_PLANETARY_NEBULA, .id = lq->planetary_nebulae[i_n]->id + GALAXY_OBJECT_MIN_PLANETARY_NEBULA, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Planetary Neb"); o_idx++; }
        for(int i_n = 0; i_n < lq->snr_count && o_idx < MAX_NET_OBJECTS; i_n++) if (lq->snrs[i_n]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->snrs[i_n]->x, .net_y = lq->snrs[i_n]->y, .net_z = lq->snrs[i_n]->z, .type = OBJ_SUPERNOVA_REMNANT, .id = lq->snrs[i_n]->id + GALAXY_OBJECT_MIN_SNR, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "SNR"); o_idx++; }
        for(int i_n = 0; i_n < lq->gmc_count && o_idx < MAX_NET_OBJECTS; i_n++) if (lq->gmcs[i_n]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->gmcs[i_n]->x, .net_y = lq->gmcs[i_n]->y, .net_z = lq->gmcs[i_n]->z, .type = OBJ_GIANT_MOLECULAR_CLOUD, .id = lq->gmcs[i_n]->id + GALAXY_OBJECT_MIN_GMC, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Giant Cloud"); o_idx++; }
        /* Adding remaining cosmic environments */
        for(int i_n = 0; i_n < lq->interstellar_filament_count && o_idx < MAX_NET_OBJECTS; i_n++) if (lq->interstellar_filaments[i_n]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->interstellar_filaments[i_n]->x, .net_y = lq->interstellar_filaments[i_n]->y, .net_z = lq->interstellar_filaments[i_n]->z, .type = OBJ_INTERSTELLAR_FILAMENT, .id = lq->interstellar_filaments[i_n]->id + GALAXY_OBJECT_MIN_INTERSTELLAR_FILAMENT, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Int. Filament"); o_idx++; }
        for(int i_n = 0; i_n < lq->interstellar_bubble_count && o_idx < MAX_NET_OBJECTS; i_n++) if (lq->interstellar_bubbles[i_n]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->interstellar_bubbles[i_n]->x, .net_y = lq->interstellar_bubbles[i_n]->y, .net_z = lq->interstellar_bubbles[i_n]->z, .type = OBJ_INTERSTELLAR_BUBBLE, .id = lq->interstellar_bubbles[i_n]->id + GALAXY_OBJECT_MIN_INTERSTELLAR_BUBBLE, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Int. Bubble"); o_idx++; }
        for(int i_n = 0; i_n < lq->bok_globule_count && o_idx < MAX_NET_OBJECTS; i_n++) if (lq->bok_globules[i_n]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->bok_globules[i_n]->x, .net_y = lq->bok_globules[i_n]->y, .net_z = lq->bok_globules[i_n]->z, .type = OBJ_BOK_GLOBULE, .id = lq->bok_globules[i_n]->id + GALAXY_OBJECT_MIN_BOK_GLOBULE, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Bok Globule"); o_idx++; }
        for(int i_n = 0; i_n < lq->clump_core_count && o_idx < MAX_NET_OBJECTS; i_n++) if (lq->clump_cores[i_n]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->clump_cores[i_n]->x, .net_y = lq->clump_cores[i_n]->y, .net_z = lq->clump_cores[i_n]->z, .type = OBJ_CLUMP_CORE, .id = lq->clump_cores[i_n]->id + GALAXY_OBJECT_MIN_CLUMP_CORE, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Clump Core"); o_idx++; }
        for(int i = 0; i < lq->accretion_disk_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->accretion_disks[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->accretion_disks[i]->x, .net_y = lq->accretion_disks[i]->y, .net_z = lq->accretion_disks[i]->z, .type = OBJ_ACCRETION_DISK, .id = lq->accretion_disks[i]->id + GALAXY_OBJECT_MIN_ACCRETION_DISK, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Accretion Disk"); o_idx++; }
        for(int i = 0; i < lq->relativistic_jet_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->relativistic_jets[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->relativistic_jets[i]->x, .net_y = lq->relativistic_jets[i]->y, .net_z = lq->relativistic_jets[i]->z, .type = OBJ_RELATIVISTIC_JET, .id = lq->relativistic_jets[i]->id + GALAXY_OBJECT_MIN_RELATIVISTIC_JET, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Relativist Jet"); o_idx++; }
        for(int i = 0; i < lq->shock_wave_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->shock_waves[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->shock_waves[i]->x, .net_y = lq->shock_waves[i]->y, .net_z = lq->shock_waves[i]->z, .type = OBJ_SHOCK_WAVE, .id = lq->shock_waves[i]->id + GALAXY_OBJECT_MIN_SHOCK_WAVE, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Shock Wave"); o_idx++; }
        for(int i = 0; i < lq->stellar_bow_shock_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->stellar_bow_shocks[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->stellar_bow_shocks[i]->x, .net_y = lq->stellar_bow_shocks[i]->y, .net_z = lq->stellar_bow_shocks[i]->z, .type = OBJ_STELLAR_BOW_SHOCK, .id = lq->stellar_bow_shocks[i]->id + GALAXY_OBJECT_MIN_STELLAR_BOW_SHOCK, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Stellar Bow"); o_idx++; }
        for(int i = 0; i < lq->cosmic_void_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->cosmic_voids[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->cosmic_voids[i]->x, .net_y = lq->cosmic_voids[i]->y, .net_z = lq->cosmic_voids[i]->z, .type = OBJ_COSMIC_VOID, .id = lq->cosmic_voids[i]->id + GALAXY_OBJECT_MIN_COSMIC_VOID, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Cosmic Void"); o_idx++; }
        for(int i = 0; i < lq->cosmic_filament_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->cosmic_filaments[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->cosmic_filaments[i]->x, .net_y = lq->cosmic_filaments[i]->y, .net_z = lq->cosmic_filaments[i]->z, .type = OBJ_COSMIC_FILAMENT, .id = lq->cosmic_filaments[i]->id + GALAXY_OBJECT_MIN_COSMIC_FILAMENT, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Cosm Filament"); o_idx++; }
        for(int i = 0; i < lq->event_horizon_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->event_horizons[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->event_horizons[i]->x, .net_y = lq->event_horizons[i]->y, .net_z = lq->event_horizons[i]->z, .type = OBJ_EVENT_HORIZON, .id = lq->event_horizons[i]->id + GALAXY_OBJECT_MIN_EVENT_HORIZON, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Event Horizon"); o_idx++; }
        for(int i = 0; i < lq->kilonova_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->kilonovae[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->kilonovae[i]->x, .net_y = lq->kilonovae[i]->y, .net_z = lq->kilonovae[i]->z, .type = OBJ_KILONOVA, .id = lq->kilonovae[i]->id + GALAXY_OBJECT_MIN_KILONOVA, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Kilonova"); o_idx++; }
        for(int i = 0; i < lq->grav_lens_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->grav_lenses[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->grav_lenses[i]->x, .net_y = lq->grav_lenses[i]->y, .net_z = lq->grav_lenses[i]->z, .type = OBJ_GRAV_LENS, .id = lq->grav_lenses[i]->id + GALAXY_OBJECT_MIN_GRAV_LENS, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Grav Lens"); o_idx++; }
        for(int i = 0; i < lq->grb_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->grbs[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->grbs[i]->x, .net_y = lq->grbs[i]->y, .net_z = lq->grbs[i]->z, .type = OBJ_GRB, .id = lq->grbs[i]->id + GALAXY_OBJECT_MIN_GRB, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Gamma Ray B."); o_idx++; }
        for(int i = 0; i < lq->grav_wave_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->grav_waves[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->grav_waves[i]->x, .net_y = lq->grav_waves[i]->y, .net_z = lq->grav_waves[i]->z, .type = OBJ_GRAV_WAVE, .id = lq->grav_waves[i]->id + GALAXY_OBJECT_MIN_GRAV_WAVE, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Grav Wave"); o_idx++; }
        for(int i = 0; i < lq->protoplanetary_disk_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->protoplanetary_disks[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->protoplanetary_disks[i]->x, .net_y = lq->protoplanetary_disks[i]->y, .net_z = lq->protoplanetary_disks[i]->z, .type = OBJ_PROTOPLANETARY_DISK, .id = lq->protoplanetary_disks[i]->id + GALAXY_OBJECT_MIN_PROTOPLANETARY_DISK, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Protoplan Disk"); o_idx++; }
        for(int i = 0; i < lq->debris_disk_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->debris_disks[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->debris_disks[i]->x, .net_y = lq->debris_disks[i]->y, .net_z = lq->debris_disks[i]->z, .type = OBJ_DEBRIS_DISK, .id = lq->debris_disks[i]->id + GALAXY_OBJECT_MIN_DEBRIS_DISK, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Debris Disk"); o_idx++; }
        for(int i = 0; i < lq->planetesimal_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->planetesimals[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->planetesimals[i]->x, .net_y = lq->planetesimals[i]->y, .net_z = lq->planetesimals[i]->z, .type = OBJ_PLANETESIMAL, .id = lq->planetesimals[i]->id + GALAXY_OBJECT_MIN_PLANETESIMAL, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Planetesimal"); o_idx++; }
        for(int i = 0; i < lq->rogue_planet_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->rogue_planets[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->rogue_planets[i]->x, .net_y = lq->rogue_planets[i]->y, .net_z = lq->rogue_planets[i]->z, .type = OBJ_ROGUE_PLANET, .id = lq->rogue_planets[i]->id + GALAXY_OBJECT_MIN_ROGUE_PLANET, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Rogue Planet"); o_idx++; }
        for(int i = 0; i < lq->brown_dwarf_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->brown_dwarfs[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->brown_dwarfs[i]->x, .net_y = lq->brown_dwarfs[i]->y, .net_z = lq->brown_dwarfs[i]->z, .type = OBJ_BROWN_DWARF, .id = lq->brown_dwarfs[i]->id + GALAXY_OBJECT_MIN_BROWN_DWARF, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Brown Dwarf"); o_idx++; }
        for(int i = 0; i < lq->iso_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->isos[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->isos[i]->x, .net_y = lq->isos[i]->y, .net_z = lq->isos[i]->z, .type = OBJ_INTERSTELLAR_OBJECT, .id = lq->isos[i]->id + GALAXY_OBJECT_MIN_ISO, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Int.St. Obj"); o_idx++; }
        for(int i = 0; i < lq->mag_reconn_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->mag_reconns[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->mag_reconns[i]->x, .net_y = lq->mag_reconns[i]->y, .net_z = lq->mag_reconns[i]->z, .type = OBJ_MAG_RECONNECTION, .id = lq->mag_reconns[i]->id + GALAXY_OBJECT_MIN_MAG_RECONN, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Mag Reconn"); o_idx++; }
        for(int i = 0; i < lq->current_sheet_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->current_sheets[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->current_sheets[i]->x, .net_y = lq->current_sheets[i]->y, .net_z = lq->current_sheets[i]->z, .type = OBJ_CURRENT_SHEET, .id = lq->current_sheets[i]->id + GALAXY_OBJECT_MIN_CURRENT_SHEET, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Current Sheet"); o_idx++; }
        for(int i = 0; i < lq->heliosphere_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->heliospheres[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->heliospheres[i]->x, .net_y = lq->heliospheres[i]->y, .net_z = lq->heliospheres[i]->z, .type = OBJ_HELIOSPHERE, .id = lq->heliospheres[i]->id + GALAXY_OBJECT_MIN_HELIOSPHERE, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Heliosphere"); o_idx++; }
        for(int i = 0; i < lq->term_shock_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->term_shocks[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->term_shocks[i]->x, .net_y = lq->term_shocks[i]->y, .net_z = lq->term_shocks[i]->z, .type = OBJ_TERMINATION_SHOCK, .id = lq->term_shocks[i]->id + GALAXY_OBJECT_MIN_TERM_SHOCK, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Term Shock"); o_idx++; }
        for(int i = 0; i < lq->magnetosphere_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->magnetospheres[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->magnetospheres[i]->x, .net_y = lq->magnetospheres[i]->y, .net_z = lq->magnetospheres[i]->z, .type = OBJ_MAGNETOSPHERE, .id = lq->magnetospheres[i]->id + GALAXY_OBJECT_MIN_MAGNETOSPHERE, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Magnetosphere"); o_idx++; }
        for(int i = 0; i < lq->cosmic_string_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->cosmic_strings[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->cosmic_strings[i]->x, .net_y = lq->cosmic_strings[i]->y, .net_z = lq->cosmic_strings[i]->z, .type = OBJ_COSMIC_STRING, .id = lq->cosmic_strings[i]->id + GALAXY_OBJECT_MIN_COSMIC_STRING, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Cosm String"); o_idx++; }
        for(int i = 0; i < lq->domain_wall_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->domain_walls[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->domain_walls[i]->x, .net_y = lq->domain_walls[i]->y, .net_z = lq->domain_walls[i]->z, .type = OBJ_DOMAIN_WALL, .id = lq->domain_walls[i]->id + GALAXY_OBJECT_MIN_DOMAIN_WALL, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Domain Wall"); o_idx++; }
        for(int i = 0; i < lq->dm_halo_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->dm_halos[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->dm_halos[i]->x, .net_y = lq->dm_halos[i]->y, .net_z = lq->dm_halos[i]->z, .type = OBJ_DM_HALO, .id = lq->dm_halos[i]->id + GALAXY_OBJECT_MIN_DM_HALO, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "DM Halo"); o_idx++; }
        for(int i = 0; i < lq->igm_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->igms[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->igms[i]->x, .net_y = lq->igms[i]->y, .net_z = lq->igms[i]->z, .type = OBJ_IGM, .id = lq->igms[i]->id + GALAXY_OBJECT_MIN_IGM, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "IGM"); o_idx++; }
        for(int i = 0; i < lq->cgm_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->cgms[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->cgms[i]->x, .net_y = lq->cgms[i]->y, .net_z = lq->cgms[i]->z, .type = OBJ_CGM, .id = lq->cgms[i]->id + GALAXY_OBJECT_MIN_CGM, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "CGM"); o_idx++; }
        for(int i = 0; i < lq->lyman_alpha_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->lyman_alphas[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->lyman_alphas[i]->x, .net_y = lq->lyman_alphas[i]->y, .net_z = lq->lyman_alphas[i]->z, .type = OBJ_LYMAN_ALPHA, .id = lq->lyman_alphas[i]->id + GALAXY_OBJECT_MIN_LYMAN_ALPHA, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "Lyman Alpha"); o_idx++; }
        for(int i = 0; i < lq->cmb_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->cmbs[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->cmbs[i]->x, .net_y = lq->cmbs[i]->y, .net_z = lq->cmbs[i]->z, .type = OBJ_CMB, .id = lq->cmbs[i]->id + GALAXY_OBJECT_MIN_CMB, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "CMB"); o_idx++; }
        for(int i = 0; i < lq->feature_count && o_idx < MAX_NET_OBJECTS; i++) if (lq->features[i]->active) { upd->objects[o_idx] = (NetObject){.net_x = lq->features[i]->x, .net_y = lq->features[i]->y, .net_z = lq->features[i]->z, .type = lq->features[i]->type, .id = lq->features[i]->id, .active = 1}; snprintf(upd->objects[o_idx].name, 64, "%s", lq->features[i]->name[0] ? lq->features[i]->name : "Cosmic Feature"); o_idx++; }


        upd->object_count = o_idx;

        /* === DIAG [LRS-3] (smoke test): full dump of the quadrant object
         * list built for this client, logged ONLY when the (id,type)
         * sequence changes (never per-tick). Pairs with the client-side
         * 'rx obj list' dump and the viewer-side 'gdd other-player ships'
         * dump to localize where a ship entry is dropped or overwritten:
         * server list -> wire -> client state -> shm -> GDD builder. === */
        {
            static uint64_t last_built_sig[MAX_CLIENTS];
            uint64_t sig = 1469598103934665603ull;
            for (int o = 0; o < o_idx; o++) {
                sig ^= (uint64_t)(int64_t)upd->objects[o].id;   sig *= 1099511628211ull;
                sig ^= (uint64_t)(int64_t)upd->objects[o].type;  sig *= 1099511628211ull;
            }
            if (sig != last_built_sig[i]) {
                last_built_sig[i] = sig;
                char listbuf[1536]; int off = 0;
                for (int o = 0; o < o_idx && off < (int)sizeof(listbuf) - 64; o++)
                    off += snprintf(listbuf + off, sizeof(listbuf) - (size_t)off,
                                    " %d:id=%d,t=%d,a=%d,'%s'",
                                    o, upd->objects[o].id, upd->objects[o].type,
                                    upd->objects[o].active, upd->objects[o].name);
                char qpl[192]; int qo = 0;
                for (int j = 0; j < lq->player_count && j < MAX_Q_PLAYERS && qo < (int)sizeof(qpl) - 16; j++)
                    qo += snprintf(qpl + qo, sizeof(qpl) - (size_t)qo, "%d,", (int)(lq->players[j] - players) + 1);
                SG_TRACE3(SG_CAT_LRS,
                    "Q-%d-%d-%d built list for %s(slot %d): objs=%d | lq players=%d[%s] npcs=%d stars=%d planets=%d |%s",
                    upd->q1, upd->q2, upd->q3, players[i].name, i, o_idx,
                    lq->player_count, qpl, lq->npc_count, lq->star_count + lq->sys_star_count, lq->planet_count + lq->sys_planet_count, listbuf);
            }
        }

        /* Torpedo Streaming - Aggregated Zero-Loss FX approach */
        /* IPC_EV_TORPEDO is now correctly sent once upon firing in commands.c */
        upd->torpedo_count = 0; /* Standard stream disabled, using Zero-Loss queue instead */

        upd->event_count = players[i].state.event_count;
        if (upd->event_count > MAX_NET_EVENTS) upd->event_count = MAX_NET_EVENTS;
        memcpy(upd->events, players[i].state.events, upd->event_count * sizeof(NetEvent));
        
        upd->wormhole = players[i].state.wormhole; 
        for(int p=0; p<3; p++) upd->probes[p] = players[i].state.probes[p];
        /* ... beam aggregation (already fast but could be pre-aggregated too if needed) ... */
        int b_idx = 0;
        for (int j = 0; j < lq->player_count; j++) { ConnectedPlayer *p_src = lq->players[j]; if (!p_src->active) continue; for (int b = 0; b < p_src->state.beam_count && b_idx < MAX_NET_BEAMS; b++) upd->beams[b_idx++] = p_src->state.beams[b]; }
        for (int n = 0; n < lq->npc_count; n++) { NPCShip *ns = lq->npcs[n]; if (!ns->active) continue; for (int b = 0; b < ns->beam_count && b_idx < MAX_NET_BEAMS; b++) upd->beams[b_idx++] = ns->beams[b]; }
        for (int m = 0; m < lq->monster_count; m++) { NPCMonster *mo = lq->monsters[m]; if (!mo->active) continue; for (int b = 0; b < mo->beam_count && b_idx < MAX_NET_BEAMS; b++) upd->beams[b_idx++] = mo->beams[b]; }
        for (int p = 0; p < lq->platform_count; p++) { NPCPlatform *pl = lq->platforms[p]; if (!pl->active) continue; for (int b = 0; b < pl->beam_count && b_idx < MAX_NET_BEAMS; b++) upd->beams[b_idx++] = pl->beams[b]; }
        for (int bb = 0; bb < lq->base_count; bb++) { NPCBase *ba = lq->bases[bb]; if (!ba->active) continue; for (int b = 0; b < ba->beam_count && b_idx < MAX_NET_BEAMS; b++) upd->beams[b_idx++] = ba->beams[b]; }
        upd->beam_count = b_idx;
        if (b_idx > 0) {
            SG_TRACE3(SG_CAT_PHA, "Q-%d-%d-%d: %d beams aggregated into upd for player %d (%s) [quadrant players=%d npcs=%d monsters=%d platforms=%d bases=%d]",
                      upd->q1, upd->q2, upd->q3, b_idx, i, players[i].name,
                      lq->player_count, lq->npc_count, lq->monster_count,
                      lq->platform_count, lq->base_count);
        }

        if (supernova_event.supernova_timer > 0) { upd->map_update_q[0] = supernova_event.supernova_q1; upd->map_update_q[1] = supernova_event.supernova_q2; upd->map_update_q[2] = supernova_event.supernova_q3; upd->map_update_val = -supernova_event.supernova_timer; }
        else { upd->map_update_q[0] = upd->q1; upd->map_update_q[1] = upd->q2; upd->map_update_q[2] = upd->q3; upd->map_update_val = spacegl_master.g[upd->q1][upd->q2][upd->q3]; }
        int rq1 = rand_r(&ai_seed) % GALAXY_SIZE + 1; int rq2 = rand_r(&ai_seed) % GALAXY_SIZE + 1; int rq3 = rand_r(&ai_seed) % GALAXY_SIZE + 1;
        upd->map_update_q2[0] = rq1; upd->map_update_q2[1] = rq2; upd->map_update_q2[2] = rq3; upd->map_update_val2 = spacegl_master.g[rq1][rq2][rq3];

        /* Reset transient effects IMMEDIATELY after copying to update packet while holding the lock.
         * NOTE: events are safe to reset here because each receiver i reads
         * ONLY players[i].state.events (its own queue). Player BEAMS are
         * different: they are aggregated from EVERY quadrant player into
         * every receiver's update, so their queues must survive the whole
         * build pass (consumed in phase 3b below). */
        if (players[i].state.event_count > 0) {
            for (int e = 0; e < players[i].state.event_count; e++) {
                if (players[i].state.events[e].type == IPC_EV_DISMANTLE) {
                    /* Log disabled to reduce noise */
                }
            }
            players[i].state.event_count = 0;
        }
        players[i].pending_send = true;
    }

    /* 3b. PHASE 2 - Consume the per-player beam queues.
     *
     * Player beams are aggregated into EVERY quadrant member's update in
     * the loop above (receiver i reads lq->players[j]->state.beams for all
     * j). Resetting a player's beam_count inside that loop destroyed its
     * beams before the updates of the higher-slot players were built, so
     * a client only ever received shots fired by players with a HIGHER
     * slot index (empirically confirmed: slot N saw only owners > N).
     * The queues are consumed here, after every update packet has been
     * built (still under game_mutex, same tick). */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!players[i].active) continue;
        if (players[i].state.beam_count > 0) SG_TRACE4(SG_CAT_PHA, "player %d (%s): state.beam_count %d -> 0 (consumed by update builder, phase 3b)", i, players[i].name, players[i].state.beam_count);
        players[i].state.beam_count = 0;
        players[i].fire_requested_this_tick = false;
    }

    /* === STARBASE FACTION DEFENSE: fire at players docking into hostile bases === */
    /* Reset transient effects for NPCs and other entities after all updates have been sent */
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
    for (int i = 0; i < MAX_BASES; i++) {
        if (bases[i].active) {
            bases[i].beam_count = 0;
        }
    }
}
