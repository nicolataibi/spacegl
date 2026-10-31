/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * commands_tactical.inc — command handlers: weapons, shields, lock and scan (pha, tor, she, lock, scan).
 * Part of the command handlers, split by domain from commands_handlers.inc
 * (2026.10.31.15) without any code change; included, in this order, by
 * commands_handlers.inc into commands.c (one translation unit).
 */
void handle_pha(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    int e, tid; 
    int args = sscanf(params, " %d %d", &tid, &e);
    
    if (args == 1) {
        e = tid;
        tid = players[i].state.lock_target;
        if (tid == 0) {
            SG_INFO(SG_CAT_PHA, "player %d (%s) pha REJECTED: no target locked", i, players[i].name);
            send_server_msg(i, "COMPUTER", "No target locked. Usage: pha <ID> <E>.");
            return;
        }
    } else if (args != 2) {
        SG_INFO(SG_CAT_PHA, "player %d (%s) pha REJECTED: bad params '%s'", i, players[i].name, params);
        send_server_msg(i, "COMPUTER", "Usage: pha <ID> <E>.");
        return;
    }

    /* 1. Minimum Integrity Check (ID 4) */
    if (players[i].state.system_health[4] < THRESHOLD_SYS_CRITICAL) {
        SG_INFO(SG_CAT_PHA, "player %d (%s) pha REJECTED: ion beam banks OFFLINE (sys4=%.1f)", i, players[i].name, players[i].state.system_health[4]);
        send_server_msg(i, "TACTICAL", "Ion Beam banks OFFLINE. Repair required.");
        return;
    }

    if (players[i].state.energy < (uint64_t)e) { SG_INFO(SG_CAT_PHA, "player %d (%s) pha REJECTED: insufficient energy (have %llu, need %d)", i, players[i].name, (unsigned long long)players[i].state.energy, e); send_server_msg(i, "COMPUTER", "Insufficient energy."); return; }
    if (players[i].state.ion_beam_charge < THRESHOLD_SYS_CRITICAL) { SG_INFO(SG_CAT_PHA, "player %d (%s) pha REJECTED: banks recharging (charge %.1f)", i, players[i].name, players[i].state.ion_beam_charge); send_server_msg(i, "TACTICAL", "Banks recharging."); return; }
    if (players[i].state.is_cloaked) { SG_INFO(SG_CAT_PHA, "player %d (%s) pha REJECTED: cloaked", i, players[i].name); send_server_msg(i, "TACTICAL", "Cannot fire while cloaked."); return; }

    /* 2. Miss Probability based on Sensor health (ID 2) */
    double sensor_h = players[i].state.system_health[2];
    if (sensor_h < THRESHOLD_SYS_DEGRADED && (rand() % 100 > (int)(sensor_h + (float)THRESHOLD_SYS_DAMAGED))) {
        SG_INFO(SG_CAT_PHA, "player %d (%s) pha MISSED: sensor noise (sensor=%.1f), no beams generated", i, players[i].name, sensor_h);
        players[i].state.energy -= (e / (uint64_t)RATIO_ENERGY_REDUCTION);
        players[i].state.ion_beam_charge -= THRESHOLD_SYS_CRITICAL;
        send_server_msg(i, "TACTICAL", "Target lock failed due to sensor noise. Shot missed!");
        return;
    }

    /* 3. Overheating Risk */
    if (e > SHIELD_MAX_STRENGTH && (rand() % 100 < (int)(THRESHOLD_SYS_CRITICAL / RATIO_ENERGY_REDUCTION))) {
        players[i].state.system_health[4] -= 2.5;
        send_server_msg(i, "ENGINEERING", "Ion Beam banks overheating! Minor hardware damage.");
    }

    players[i].state.energy -= e;
    players[i].state.ion_beam_charge -= 15.0;
    
    double tx = 0.0, ty = 0.0, tz = 0.0; bool found = false;
    int pq1=players[i].state.q1, pq2=players[i].state.q2, pq3=players[i].state.q3;
    
    ConnectedPlayer *target_p = player_by_id(tid);
    NPCShip *target_npc = npc_by_id(tid);
    NPCBase *target_base = base_by_id(tid);
    NPCPlanet *target_planet = planet_by_id(tid);
    NPCStar *target_star = star_by_id(tid);
    if (target_p && target_p->active && target_p->state.q1 == pq1 && target_p->state.q2 == pq2 && target_p->state.q3 == pq3) { tx=target_p->state.s1; ty=target_p->state.s2; tz=target_p->state.s3; found=true; }
    else if (target_npc && target_npc->active && target_npc->q1 == pq1 && target_npc->q2 == pq2 && target_npc->q3 == pq3) { tx=target_npc->x; ty=target_npc->y; tz=target_npc->z; found=true; }
    else if (tid >= GALAXY_OBJECT_MIN_PLATFORM && tid <= GALAXY_OBJECT_MAX_PLATFORM && platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].active && platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].q1 == pq1 && platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].q2 == pq2 && platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].q3 == pq3) { tx=platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].x; ty=platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].y; tz=platforms[tid-GALAXY_OBJECT_MIN_PLATFORM].z; found=true; }
    else if (tid >= GALAXY_OBJECT_MIN_MONSTER && tid <= GALAXY_OBJECT_MAX_MONSTER && monsters[tid-GALAXY_OBJECT_MIN_MONSTER].active && monsters[tid-GALAXY_OBJECT_MIN_MONSTER].q1 == pq1 && monsters[tid-GALAXY_OBJECT_MIN_MONSTER].q2 == pq2 && monsters[tid-GALAXY_OBJECT_MIN_MONSTER].q3 == pq3) { tx=monsters[tid-GALAXY_OBJECT_MIN_MONSTER].x; ty=monsters[tid-GALAXY_OBJECT_MIN_MONSTER].y; tz=monsters[tid-GALAXY_OBJECT_MIN_MONSTER].z; found=true; }
    else if (target_base && target_base->active && target_base->q1 == pq1 && target_base->q2 == pq2 && target_base->q3 == pq3) { tx=target_base->x; ty=target_base->y; tz=target_base->z; found=true; }
    else if (target_planet && target_planet->active && target_planet->q1 == pq1 && target_planet->q2 == pq2 && target_planet->q3 == pq3) { tx=target_planet->x; ty=target_planet->y; tz=target_planet->z; found=true; }
    else if (target_star && target_star->active && target_star->q1 == pq1 && target_star->q2 == pq2 && target_star->q3 == pq3) { tx=target_star->x; ty=target_star->y; tz=target_star->z; found=true; }
    else if (tid >= GALAXY_OBJECT_MIN_ASTEROID && tid <= GALAXY_OBJECT_MAX_ASTEROID && asteroids[tid-GALAXY_OBJECT_MIN_ASTEROID].active && asteroids[tid-GALAXY_OBJECT_MIN_ASTEROID].q1 == pq1 && asteroids[tid-GALAXY_OBJECT_MIN_ASTEROID].q2 == pq2 && asteroids[tid-GALAXY_OBJECT_MIN_ASTEROID].q3 == pq3) { tx=asteroids[tid-GALAXY_OBJECT_MIN_ASTEROID].x; ty=asteroids[tid-GALAXY_OBJECT_MIN_ASTEROID].y; tz=asteroids[tid-GALAXY_OBJECT_MIN_ASTEROID].z; found=true; }
    else if (tid >= GALAXY_OBJECT_MIN_QUASAR && tid <= GALAXY_OBJECT_MAX_QUASAR && quasars[tid-GALAXY_OBJECT_MIN_QUASAR].active && quasars[tid-GALAXY_OBJECT_MIN_QUASAR].q1 == pq1 && quasars[tid-GALAXY_OBJECT_MIN_QUASAR].q2 == pq2 && quasars[tid-GALAXY_OBJECT_MIN_QUASAR].q3 == pq3) { tx=quasars[tid-GALAXY_OBJECT_MIN_QUASAR].x; ty=quasars[tid-GALAXY_OBJECT_MIN_QUASAR].y; tz=quasars[tid-GALAXY_OBJECT_MIN_QUASAR].z; found=true; }
    else if (tid >= GALAXY_OBJECT_MIN_PROBE && tid <= GALAXY_OBJECT_MAX_PROBE) {
        int p_idx = (tid - GALAXY_OBJECT_MIN_PROBE) / 3;
        int pr_idx = (tid - GALAXY_OBJECT_MIN_PROBE) % 3;
        if (p_idx < MAX_CLIENTS && players[p_idx].state.probes[pr_idx].active) {
            /* Check if probe is in the same quadrant */
            int pr_q1 = get_q_from_g(players[p_idx].state.probes[pr_idx].gx);
            int pr_q2 = get_q_from_g(players[p_idx].state.probes[pr_idx].gy);
            int pr_q3 = get_q_from_g(players[p_idx].state.probes[pr_idx].gz);
            if (pr_q1 == pq1 && pr_q2 == pq2 && pr_q3 == pq3) {
                tx = players[p_idx].state.probes[pr_idx].s1;
                ty = players[p_idx].state.probes[pr_idx].s2;
                tz = players[p_idx].state.probes[pr_idx].s3;
                found = true;
            }
        }
    }
    
    if (found) {
        double dx = tx - players[i].state.s1;
        double dy = ty - players[i].state.s2;
        double dz = tz - players[i].state.s3; 
        double dist = sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < (double)DIST_EPSILON) {
            dist = (double)DIST_EPSILON;
        }
        double weapon_mult = RATIO_BASE_POWER + (players[i].state.power_dist[2] * RATIO_WEAPON_POWER);
        int hit = (int)((e / dist) * (players[i].state.system_health[4] / (double)YIELD_HARVEST_MAX) * weapon_mult);
        
        /* VISUAL FX: Integrated via NetUpdate.beams for full tracking support */

        SG_TRACE3(SG_CAT_PHA, "player %d (%s) FIRING: target=%d e=%d dist=%.2f hit=%d q=[%d,%d,%d] pos=(%.2f,%.2f,%.2f)",
                  i, players[i].name, tid, e, dist, hit,
                  players[i].state.q1, players[i].state.q2, players[i].state.q3,
                  players[i].state.s1, players[i].state.s2, players[i].state.s3);

        if (players[i].state.beam_count < MAX_NET_BEAMS - 1) {
            /* Beam 1: Top Emitter */
            players[i].state.beams[players[i].state.beam_count++] = (NetBeam){
                players[i].state.s1, 
                players[i].state.s2 + (double)DIST_EPSILON, 
                players[i].state.s3, 
                tx, 
                ty, 
                tz, 
                i + 1,
                (int)tid,
                1
            };
            /* Beam 2: Bottom Emitter */
            players[i].state.beams[players[i].state.beam_count++] = (NetBeam){
                players[i].state.s1, 
                players[i].state.s2 - (double)DIST_EPSILON, 
                players[i].state.s3, 
                tx, 
                ty, 
                tz, 
                i + 1,
                (int)tid,
                2
            };
            SG_TRACE3(SG_CAT_PHA, "player %d (%s): 2 beams queued (emitters 1+2 -> target %d), state.beam_count=%d",
                      i, players[i].name, tid, players[i].state.beam_count);
        } else {
            SG_WARNING(SG_CAT_PHA, "player %d (%s): beam array FULL (beam_count=%d) - beams NOT queued!",
                       i, players[i].name, players[i].state.beam_count);
        }
        
        ConnectedPlayer *target = player_by_id(tid);
        NPCShip *target_npc = npc_by_id(tid);
        if (target) {
            int s_idx = calculate_shield_index(players[i].gx, players[i].gy, players[i].gz,
                                               target->gx, target->gy, target->gz,
                                               target->state.van_h, target->state.van_m, target->state.van_r);

            int dmg_rem = hit;
            if (target->state.shields[s_idx] >= dmg_rem) {
                target->state.shields[s_idx] -= dmg_rem;
                dmg_rem = 0;
            } else {
                dmg_rem -= target->state.shields[s_idx];
                target->state.shields[s_idx] = 0;
            }
            
            if (dmg_rem > 0) {
                apply_hull_damage(tid - 1, (dmg_rem / (double)YIELD_HARVEST_MAX));
                target->state.energy -= dmg_rem / (uint64_t)RATIO_ENERGY_REDUCTION;
            }
            if (target->faction == players[i].faction) {
                players[i].renegade_timer = (300 * GAME_TICK_RATE);
                send_server_msg(i, "CRITICAL", "FRIENDLY FIRE! YOU ARE NOW A RENEGADE!");
            }
            if (target->state.hull_integrity <= 0 || target->state.energy <= 0) {
                target->death_timer = (GAME_TICK_RATE / (int)RATIO_ENERGY_REDUCTION);
                push_server_event(tid - 1, IPC_EV_BOOM, target->state.s1, target->state.s2, target->state.s3, 0, 0, 0, 1);
            }
            send_server_msg(tid - 1, "WARNING", "UNDER Ion Beam ATTACK!");
        } else if (target_npc) {
            NPCShip *target = target_npc;
            int dmg_rem = hit;
            if (target->plating >= dmg_rem) {
                target->plating -= dmg_rem;
                dmg_rem = 0;
            } else {
                dmg_rem -= target->plating;
                target->plating = 0;
            }
            
            if (dmg_rem > 0) {
                target->health -= (dmg_rem / (int)THRESHOLD_SYS_CRITICAL);
                /* Chance to damage internal systems (Engines) */
                if (rand() % 100 < PROB_NPC_ESCAPE) {
                    double sys_dmg = 5.0 + (rand() % (int)PROB_NPC_ESCAPE);
                    target->engine_health -= sys_dmg;
                    if (target->engine_health < 0) {
                        target->engine_health = 0;
                    }
                    send_server_msg(i, "TACTICAL", "Hull impact! Target maneuvering capability degraded.");
                }
            }
            if (target->health <= 0 || target->energy <= 0) {
                target->death_timer = (GAME_TICK_RATE / (int)RATIO_ENERGY_REDUCTION);
                push_server_event(i, IPC_EV_BOOM, target->x, target->y, target->z, 0, 0, 0, 1);
                send_server_msg(i, "TACTICAL", "Target vessel neutralized.");
            }
        } else if (tid >= GALAXY_OBJECT_MIN_PLATFORM && tid <= GALAXY_OBJECT_MAX_PLATFORM) {
            NPCPlatform *target = &platforms[tid - GALAXY_OBJECT_MIN_PLATFORM];
            int dmg_rem = hit;
            if (target->energy >= (uint64_t)dmg_rem) {
                target->energy -= (uint64_t)dmg_rem;
                dmg_rem = 0;
            } else {
                dmg_rem -= target->energy;
                target->energy = 0;
            }
            
            if (dmg_rem > 0) {
                target->health -= (dmg_rem / (int)THRESHOLD_SYS_CRITICAL);
            }
            if (target->health <= 0) {
                target->active = 0;
                push_server_event(i, IPC_EV_BOOM, target->x, target->y, target->z, 0, 0, 0, 1);
                send_server_msg(i, "TACTICAL", "Defense platform neutralized.");
            }
        } else if (tid >= GALAXY_OBJECT_MIN_MONSTER && tid <= GALAXY_OBJECT_MAX_MONSTER) {
            NPCMonster *target = &monsters[tid - GALAXY_OBJECT_MIN_MONSTER];
            target->health -= (hit / (int)THRESHOLD_SYS_CRITICAL);
            if (target->health <= 0) {
                target->active = 0;
                push_server_event(i, IPC_EV_BOOM, target->x, target->y, target->z, 0, 0, 0, 1);
                send_server_msg(i, "TACTICAL", "Anomaly neutralized.");
            }
        } else if (tid >= GALAXY_OBJECT_MIN_PROBE && tid <= GALAXY_OBJECT_MAX_PROBE) {
            int p_idx = (tid - GALAXY_OBJECT_MIN_PROBE) / 3;
            int pr_idx = (tid - GALAXY_OBJECT_MIN_PROBE) % 3;
            if (p_idx < MAX_CLIENTS && players[p_idx].state.probes[pr_idx].active) {
                players[p_idx].state.probes[pr_idx].active = 0;
                push_server_event(i, IPC_EV_BOOM, players[p_idx].state.probes[pr_idx].s1, players[p_idx].state.probes[pr_idx].s2, players[p_idx].state.probes[pr_idx].s3, 0, 0, 0, 1);
                send_server_msg(i, "TACTICAL", "Probe neutralized.");
                if (p_idx != i) {
                    send_server_msg(p_idx, "WARNING", "Telemetry lost: Probe destroyed by external fire.");
                }
            }
        }
        char msg[64];
        SG_SPRINTF(msg, "Ion Beams hit ID %d for %d effective damage.", tid, hit);
        send_server_msg(i, "TACTICAL", msg);
    } else {
        send_server_msg(i, "COMPUTER", "Target out of range.");
    }
}

void handle_tor(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    if (players[i].state.system_health[5] < 50.0) {
        send_server_msg(i, "TACTICAL", "Torpedo tubes OFFLINE.");
        return;
    }

    /* Check specifically if a tube is ready and doesn't have an active torpedo in flight */
    /* This allows the next tube in rotation to fire immediately if Tube 0 is still in flight */
    int start_tube = players[i].current_tube;
    int tube = -1;
    for (int t = 0; t < 4; t++) {
        int check = (start_tube + t) % 4;
        if (players[i].tube_load_timers[check] <= 0 && !players[i].torp_slots[check].active) {
            tube = check;
            break;
        }
    }

    if (tube == -1) {
        send_server_msg(i, "TACTICAL", "All torpedo tubes are currently LOADING or FIRING.");
        return;
    }

    if (players[i].state.is_cloaked) {
        send_server_msg(i, "TACTICAL", "Cannot fire while cloaked.");
        return;
    }
    if (players[i].state.energy < COST_ACTION_VERY_HIGH) {
        send_server_msg(i, "COMPUTER", "Insufficient energy.");
        return;
    }

    if (players[i].state.torpedoes > 0) {
        /* Misfire risk (Integrity 50-75%) */
        if (players[i].state.system_health[5] < THRESHOLD_SYS_STABLE && (rand() % 100 > (int)players[i].state.system_health[5])) {
            players[i].state.torpedoes--;
            players[i].state.energy -= COST_ACTION_HIGH;
            players[i].tube_load_timers[tube] = TIMER_TORP_LOAD;
            players[i].current_tube = (tube + 1) % 4;
            send_server_msg(i, "TACTICAL", "CRITICAL: Torpedo misfire! Warhead ejected.");
            return;
        }

        double h;
        double m;
        bool manual = false;
        if (sscanf(params, "%lf %lf", &h, &m) == 2) {
            manual = true;
            normalize_upright(&h, &m);
        } else {
            h = players[i].state.van_h;
            m = players[i].state.van_m;
        }

        if (players[i].fire_requested_this_tick) return;

        /* Find a slot in the global torpedo array BEFORE consuming resources */
        int g_torp_idx = -1;
        for (int t_idx = 0; t_idx < MAX_GLOBAL_TORPEDOES; t_idx++) {
            if (!players_torpedoes[t_idx].active) {
                g_torp_idx = t_idx;
                break;
            }
        }

        if (g_torp_idx == -1) {
            send_server_msg(i, "TACTICAL", "ERROR: Tactical computer at capacity. Cannot track more projectiles.");
            return;
        }

        /* Resources verified and slot found: Commit to firing */
        players[i].state.energy -= COST_ACTION_VERY_HIGH;
        players[i].state.torpedoes--;
        players[i].fire_requested_this_tick = true;

        PlayerTorpedo *pt = &players_torpedoes[g_torp_idx];
        pt->id = g_torp_idx + 30000;
        pt->owner_idx = i;
        pt->faction = players[i].faction;
        pt->origin_tube = tube;
        pt->active = true;
        pt->timeout = TIMER_TORP_TIMEOUT;
        pt->target_id = manual ? 0 : players[i].state.lock_target;
        
        double rad_h = h * M_PI / 180.0;
        double rad_m = m * M_PI / 180.0;
        pt->gx = players[i].gx;
        pt->gy = players[i].gy;
        pt->gz = players[i].gz;
        pt->dx = cos(rad_m) * sin(rad_h);
        pt->dy = cos(rad_m) * -cos(rad_h);
        pt->dz = sin(rad_m);
        pt->q1 = get_q_from_g(pt->gx);
        pt->q2 = get_q_from_g(pt->gy);
        pt->q3 = get_q_from_g(pt->gz);
        pt->x = pt->gx - (pt->q1 - 1) * QUADRANT_SIZE;
        pt->y = pt->gy - (pt->q2 - 1) * QUADRANT_SIZE;
        pt->z = pt->gz - (pt->q3 - 1) * QUADRANT_SIZE;

        /* Synchronize the specific tube slot to block it until impact/timeout */
        players[i].torp_slots[tube].active = true;
        players[i].torp_slots[tube].target = pt->target_id;
        players[i].torp_slots[tube].timeout = pt->timeout;

        /* Set legacy global flag for HUD backward compatibility */
        players[i].torp_active = true;

        /* Broadcast visual event to all players in the quadrant */
        broadcast_server_event(pt->q1, pt->q2, pt->q3, IPC_EV_TORPEDO, 
                               pt->x, pt->y, pt->z, 
                               pt->dx * SPEED_TORPEDO, pt->dy * SPEED_TORPEDO, pt->dz * SPEED_TORPEDO, 
                               IPC_TORPEDO_ID_OFFSET + g_torp_idx);

        /* Reloading starts immediately with a 0.5s (30 ticks) FIRING phase delay */
        players[i].tube_load_timers[tube] = TIMER_TORP_LOAD + 30;
        players[i].tube_torpedo_etas[tube] = TIMER_TORP_TIMEOUT; 
        players[i].current_tube = (tube + 1) % 4;
        send_server_msg(i, "TACTICAL", manual ? "Torpedo away (Manual)." : (players[i].state.lock_target > 0 ? "Torpedo away (Locked)." : "Torpedo away (Boresight)."));
    } else {
        send_server_msg(i, "TACTICAL", "Insufficient torpedoes.");
    }
}

void handle_she(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    int f, r, t, b, l, ri;
    if (sscanf(params, "%d %d %d %d %d %d", &f, &r, &t, &b, &l, &ri) == 6) {
        /* 1. Integrity Check (Shield System ID 8) */
        if (players[i].state.system_health[8] < THRESHOLD_SYS_CRITICAL) {
            send_server_msg(i, "ENGINEERING", "Shield generator OFFLINE. Grid reconfiguration impossible.");
            return;
        }

        /* 2. Basic cost and input cleaning */
        if (players[i].state.energy < COST_ACTION_MED) {
            send_server_msg(i, "COMPUTER", "Insufficient energy for grid reconfiguration pulse.");
            return;
        }

        if (f < 0) f = 0; 
        if (r < 0) r = 0; 
        if (t < 0) t = 0;
        if (b < 0) b = 0; 
        if (l < 0) l = 0; 
        if (ri < 0) ri = 0;

        /* 3. Cap values at SHIELD_MAX_STRENGTH */
        if (f > SHIELD_MAX_STRENGTH) f = SHIELD_MAX_STRENGTH; 
        if (r > SHIELD_MAX_STRENGTH) r = SHIELD_MAX_STRENGTH; 
        if (t > SHIELD_MAX_STRENGTH) t = SHIELD_MAX_STRENGTH;
        if (b > SHIELD_MAX_STRENGTH) b = SHIELD_MAX_STRENGTH; 
        if (l > SHIELD_MAX_STRENGTH) l = SHIELD_MAX_STRENGTH; 
        if (ri > SHIELD_MAX_STRENGTH) ri = SHIELD_MAX_STRENGTH;

        /* 4. Reactor Consumption/Balance */
        int requested_total = f + r + t + b + l + ri;
        int current_total = 0;
        for (int s = 0; s < 6; s++) current_total += players[i].state.shields[s];

        int diff = requested_total - current_total;
        if (diff > 0) {
            /* Adding energy to shields */
            if (players[i].state.energy < (uint64_t)(diff + COST_ACTION_MED)) {
                send_server_msg(i, "COMPUTER", "Insufficient reactor energy to charge grids to requested levels.");
                return;
            }
            players[i].state.energy -= (diff + COST_ACTION_MED);
        } else {
            /* Removing energy from shields: return 80% to reactor, 20% lost in flux */
            players[i].state.energy -= COST_ACTION_MED;
            players[i].state.energy += (uint64_t)(abs(diff) * RATIO_MINING_EFF_MIN);
            if (players[i].state.energy > MAX_ENERGY_CAPACITY) players[i].state.energy = MAX_ENERGY_CAPACITY;
        }

        // Store target shield values (0:F, 1:R, 2:T, 3:B, 4:L, 5:RI)
        players[i].state.target_shields[0] = f;
        players[i].state.target_shields[1] = r;
        players[i].state.target_shields[2] = t;
        players[i].state.target_shields[3] = b;
        players[i].state.target_shields[4] = l;
        players[i].state.target_shields[5] = ri;

        // Set the timer for progressive shield change
        // For simplicity, a fixed duration for any change.
        players[i].state.shield_change_timer = TIMER_SHIELD_CHANGE_TICKS;
        players[i].state.shield_change_rate = (float)SHIELD_MAX_STRENGTH / TIMER_SHIELD_CHANGE_TICKS;
        
        send_server_msg(i, "ENGINEERING", "Shield grids reconfiguring progressively.");
    } else {
        send_server_msg(i, "COMPUTER", "Usage: she <F> <R> <T> <B> <L> <RI>");
    }
}

void handle_lock(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    int tid;
    if (sscanf(params, " %d", &tid) == 1 && tid > 0) {
        /* 1. Integrity Check (Sensors ID 2) */
        if (players[i].state.system_health[2] < THRESHOLD_SYS_CRITICAL) {
            send_server_msg(i, "TACTICAL", "Targeting arrays OFFLINE (Sensor damage).");
            return;
        }

        /* 2. Energy Cost */
        if (players[i].state.energy < (uint64_t)(COST_ACTION_LOW / (uint64_t)RATIO_ENERGY_REDUCTION)) {
            send_server_msg(i, "COMPUTER", "Insufficient energy for active tracking.");
            return;
        }

        /* 3. Validation of Target Visibility and Range */
        bool found = false;
        int pq1 = players[i].state.q1;
        int pq2 = players[i].state.q2;
        int pq3 = players[i].state.q3;
        
        ConnectedPlayer *target_p = player_by_id(tid);
        NPCShip *target_npc = npc_by_id(tid);
        if (target_p) {
            /* Player Target */
            if (target_p->active && 
                target_p->state.q1 == pq1 && 
                target_p->state.q2 == pq2 && 
                target_p->state.q3 == pq3) {
                if (!target_p->state.is_cloaked || target_p->faction == players[i].faction) {
                    found = true;
                }
            }
        } else if (target_npc) {
            /* NPC Target - Inter-sector aware */
            if (target_npc->active) {
                if (!target_npc->is_cloaked) {
                    found = true;
                }
            }
        } else {
            /* Static objects: table-driven lookup (targets.c) */
            const TargetRangeDef *tr = target_range_for(tid);
            if (tr && (tr->flags & TGT_F_CMD_LOCK)) {
                if (tr->flags & TGT_F_LOCK_GLOBAL) {
                    /* Historical quirk: derelicts are resolved via the global array */
                    found = (target_find_global_active(tid) != NULL);
                } else {
                    QuadrantIndex *lq = &spatial_index[pq1][pq2][pq3];
                    found = (target_find_local(tr, lq, tid) != NULL);
                }
            } else if (tid >= GALAXY_OBJECT_MIN_PROBE && tid <= GALAXY_OBJECT_MAX_PROBE) {
                int p_idx = (tid - GALAXY_OBJECT_MIN_PROBE) / 3;
                int pr_idx = (tid - GALAXY_OBJECT_MIN_PROBE) % 3;
                if (p_idx < MAX_CLIENTS && players[p_idx].state.probes[pr_idx].active) {
                    found = true;
                }
            }
        }

        if (found) {
            players[i].state.lock_target = tid;
            players[i].state.energy -= (COST_ACTION_LOW / (uint64_t)RATIO_ENERGY_REDUCTION);
            send_server_msg(i, "TACTICAL", "Target locked and tracking.");
        } else {
            send_server_msg(i, "TACTICAL", "Unable to acquire lock. Target not detected.");
        }
    } else {
        players[i].state.lock_target = 0;
        send_server_msg(i, "TACTICAL", "Lock released.");
    }
}

void handle_scan(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    int tid = 0;
    if (sscanf(params, " %d", &tid) != 1) {
        tid = players[i].state.lock_target;
    }
    /* Fallback to autopilot target if no lock/param */
    if (tid == 0) {
        tid = players[i].apr_target;
    }

    if (tid > 0) {
        if (players[i].state.system_health[2] < (float)COST_MANEUVER_ADJUST) {
            send_server_msg(i, "COMPUTER", "DEEP SCAN FAILURE: Sensor resolution insufficient (< 20%).");
            return;
        }
        if (players[i].state.energy < (uint64_t)COST_MANEUVER_ADJUST) {
            send_server_msg(i, "COMPUTER", "Insufficient energy for high-resolution scan.");
            return;
        }
        players[i].state.energy -= (uint64_t)COST_MANEUVER_ADJUST;

        char rep[4096]; memset(rep, 0, 4096);
        bool found = false;
        int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
        double sensor_h = players[i].state.system_health[2];
        bool scrambled = (sensor_h < 50.0 && (rand() % 100 > (int)sensor_h));

        ConnectedPlayer *t = player_by_id(tid);
        NPCShip *target_npc = npc_by_id(tid);
        if (t) {
             if (t->active && t->state.q1 == pq1 && t->state.q2 == pq2 && t->state.q3 == pq3) {
                 found = true;
                 SG_SPRINTF(rep, CYAN "\n--- SENSOR SCAN ANALYSIS: TARGET ID %d ---" RESET "\n", tid);
                 SG_APPENDF(rep, "COMMANDER: %s | CLASS: %d\n", t->name, t->ship_class);
                 if (scrambled) SG_STRCAT(rep, "HULL INTEGRITY: [SCRAMBLED]\nENERGY: ????\n");
                 else SG_APPENDF(rep, "HULL INTEGRITY: %.1f%%\nENERGY: %" PRIu64 " | CREW: %d | TORPS: %d\n", t->state.hull_integrity, t->state.energy, t->state.crew_count, t->state.torpedoes);
                 SG_STRCAT(rep, BLUE "SYSTEMS STATUS:\n" RESET);
                 const char* sys[] = {"Hyperdrive", "Impulse", "Sensors", "Transp", "Ion Beams", "Torps", "Computer", "Life", "Shields", "Aux"};
                 for(int s=0; s<10; s++) {
                     char line[64];
                     if (scrambled && (rand()%100 > 50)) SG_SPRINTF(line, " %-10s : [????]\n", sys[s]);
                     else SG_SPRINTF(line, " %-10s : %.1f%%\n", sys[s], t->state.system_health[s]);
                     SG_STRCAT(rep, line);
                 }
             }
        } else if (target_npc) {
             if (target_npc->active && target_npc->q1 == pq1 && target_npc->q2 == pq2 && target_npc->q3 == pq3) {
                 found = true;
                 SG_SPRINTF(rep, CYAN "\n--- TACTICAL SCAN: NPC ID %d ---" RESET "\n", tid);
                 SG_APPENDF(rep, "SPECIES: %s\nENERGY: %" PRIu64 "\nPROPULSION: %.1f%%\n", 
                     scrambled ? "UNKNOWN" : get_species_name(target_npc->faction), 
                     scrambled ? (uint64_t)0 : target_npc->energy, target_npc->engine_health);
            }
        } else {
            QuadrantIndex *lq = &spatial_index[pq1][pq2][pq3];
            if (tid >= GALAXY_OBJECT_MIN_PLANET && tid <= GALAXY_OBJECT_MAX_PLANET) {
                for (int p = 0; p < lq->planet_count; p++) {
                    if (lq->planets[p]->id + GALAXY_OBJECT_MIN_PLANET == tid) {
                        found = true;
                        SG_SPRINTF(rep, GREEN "\n--- PLANETARY SURVEY ---" RESET "\nCLASS: %s\nRESERVES: %d units\n", scrambled ? "Unknown" : planet_class_name(lq->planets[p]->planet_class), scrambled ? 0 : lq->planets[p]->amount);
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_STAR && tid <= GALAXY_OBJECT_MAX_STAR) {
                for (int s = 0; s < lq->star_count; s++) {
                    if (lq->stars[s]->id + GALAXY_OBJECT_MIN_STAR == tid) {
                        found = true;
                        SG_SPRINTF(rep, YELLOW "\n--- STELLAR ANALYSIS ---" RESET "\nTYPE: Main Sequence G-Class Star\nADVISORY: Proximity scooping active (sco).\n");
                    }
                }
            } else if (sys_id_is_star(tid) || sys_id_is_planet(tid) || sys_id_is_moon(tid)) {
                /* star system bodies (include/star_system.h): ids are slots of the system bands */
                if (lq->sys_star_count > 0 && lq->sys_stars[0] && lq->sys_stars[0]->active) {
                    const NPCSysStar *ss_ = lq->sys_stars[0];
                    if (sys_id_is_star(tid) && tid == ss_->id) {
                        found = true;
                        SG_SPRINTF(rep, YELLOW "\n--- STELLAR ANALYSIS ---" RESET "\nSPECTRAL CLASS: %d (0=O .. 6=M)\nPLANETS IN ORBIT: %d\nADVISORY: Proximity scooping active (sco).\n",
                                scrambled ? -1 : ss_->star_class, ss_->n_planets);
                    } else if (sys_id_is_planet(tid)) {
                        for (int p = 0; p < lq->sys_planet_count; p++) {
                            const NPCSysPlanet *pl_ = lq->sys_planets[p];
                            if (!pl_ || !pl_->active || pl_->id != tid) continue;
                            found = true;
                            SG_SPRINTF(rep, GREEN "\n--- PLANETARY SURVEY ---" RESET "\nCLASS: %s\nRESERVES: %d units\nORBIT: %.1f units around the star\nMOONS: %d\n",
                                    scrambled ? "Unknown" : planet_class_name(pl_->planet_class), scrambled ? 0 : pl_->amount,
                                    pl_->orbit_r, pl_->n_moons);
                        }
                    } else {
                        for (int m = 0; m < lq->sys_moon_count; m++) {
                            const NPCSysMoon *mo_ = lq->sys_moons[m];
                            if (!mo_ || !mo_->active || mo_->id != tid) continue;
                            found = true;
                            const char *mn_ = obj_type_name(mo_->moon_type);
                            int parent_ = GALAXY_OBJECT_MIN_SYS_PLANET + sys_moon_planet(tid - GALAXY_OBJECT_MIN_SYS_MOON);
                            SG_SPRINTF(rep, GREEN "\n--- SELENOGRAPHIC SURVEY ---" RESET "\nTYPE: %s\nORBITING PLANET: %d\n", mn_ ? mn_ : "Moon", parent_);
                        }
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_BLACKHOLE && tid <= GALAXY_OBJECT_MAX_BLACKHOLE) {
                for (int h = 0; h < lq->bh_count; h++) {
                    if (lq->black_holes[h]->id + GALAXY_OBJECT_MIN_BLACKHOLE == tid) {
                        found = true;
                        SG_SPRINTF(rep, MAGENTA "\n--- SINGULARITY ANALYSIS ---" RESET "\nTYPE: Schwarzschild Black Hole\nADVISORY: Gravity well detected (< %.1f units). Escape velocity required.\n", DIST_GRAVITY_WELL);
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_NEBULA && tid <= GALAXY_OBJECT_MAX_NEBULA) {
                for (int n = 0; n < lq->nebula_count; n++) {
                    if (lq->nebulas[n]->id + GALAXY_OBJECT_MIN_NEBULA == tid) {
                        found = true;
                        const char *neb_desc[] = {
                            "Standard Class: Reduced sensor range and inhibited shield regeneration.",
                            "High-Energy Class: Volatile ionized gas. High risk of ignition.",
                            "Dark Matter Class: Severe sensor blindness and gravitational anomalies.",
                            "Ionic Class: Natural cloaking cover. Severe communication dampening.",
                            "Gravimetric Class: Tactical advantage due to deep space distortion.",
                            "Temporal Class: Unpredictable holographic ghosting on sensors."
                        };
                        SG_SPRINTF(rep, BLUE "\n--- STELLAR PHENOMENON ANALYSIS ---" RESET "\n%s\n", (lq->nebulas[n]->type >= 0 && lq->nebulas[n]->type < 6) ? neb_desc[lq->nebulas[n]->type] : "Unknown phenomena.");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_COMET && tid <= GALAXY_OBJECT_MAX_COMET) {
                for (int c = 0; c < lq->comet_count; c++) {
                    if (lq->comets[c]->id + GALAXY_OBJECT_MIN_COMET == tid) {
                        found = true;
                        SG_SPRINTF(rep, WHITE "\n--- COMET ANALYSIS ---" RESET "\nCOMPOSITION: Ice and Silicates\nVELOCITY: High-relative\nADVISORY: Proximity impact risk.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_ASTEROID && tid <= GALAXY_OBJECT_MAX_ASTEROID) {
                for (int a = 0; a < lq->asteroid_count; a++) {
                    if (lq->asteroids[a]->id + GALAXY_OBJECT_MIN_ASTEROID == tid) {
                        found = true;
                        const char* res_names[] = {"None", "Aetherium", "Neo-Titanium", "Void-Essence", "Graphene", "Synaptics", "Nebular Gas", "Composite", "Dark-Matter"};
                        int r_type = lq->asteroids[a]->resource_type;
                        const char* r_name = (r_type >= 0 && r_type <= 8) ? res_names[r_type] : "Unknown";
                        SG_SPRINTF(rep, WHITE "\n--- ASTEROID ANALYSIS ---" RESET "\nCOMPOSITION: %s\nRESERVES: %d units\nSIZE: Class-%.1f\n", 
                            r_name, lq->asteroids[a]->amount, lq->asteroids[a]->size * QUADRANT_SIZE);
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_DERELICT && tid <= GALAXY_OBJECT_MAX_DERELICT) {
                for (int d = 0; d < lq->derelict_count; d++) {
                    if (lq->derelicts[d]->id + GALAXY_OBJECT_MIN_DERELICT == tid) {
                        found = true;
                        SG_SPRINTF(rep, WHITE "\n--- DERELICT ANALYSIS ---" RESET "\nSTATUS: DEAD\nPOWER: NONE\nADVISORY: Salvage operations authorized.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_MINE && tid <= GALAXY_OBJECT_MAX_MINE) {
                for (int m = 0; m < lq->mine_count; m++) {
                    if (lq->mines[m]->id + GALAXY_OBJECT_MIN_MINE == tid) {
                        found = true;
                        SG_SPRINTF(rep, RED "\n--- WARNING: ORDNANCE DETECTED ---" RESET "\nTYPE: Proximity Mine\nSTATUS: ARMED\nADVISORY: Maintain safe distance.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_BUOY && tid <= GALAXY_OBJECT_MAX_BUOY) {
                for (int b = 0; b < lq->buoy_count; b++) {
                    if (lq->buoys[b]->id + GALAXY_OBJECT_MIN_BUOY == tid) {
                        found = true;
                        SG_SPRINTF(rep, CYAN "\n--- COMMUNICATION BUOY ---" RESET "\nSTATUS: BROADCASTING\nFREQUENCY: Standard Alliance Band\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_RIFT && tid <= GALAXY_OBJECT_MAX_RIFT) {
                for (int r = 0; r < lq->rift_count; r++) {
                    if (lq->rifts[r]->id + GALAXY_OBJECT_MIN_RIFT == tid) {
                        found = true;
                        SG_SPRINTF(rep, MAGENTA "\n--- SPATIAL RIFT ANALYSIS ---" RESET "\nTYPE: Subspace Rupture\nADVISORY: Extreme gravitational shear detected.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_PULSAR && tid <= GALAXY_OBJECT_MAX_PULSAR) {
                for (int p = 0; p < lq->pulsar_count; p++) {
                    if (lq->pulsars[p]->id + GALAXY_OBJECT_MIN_PULSAR == tid) {
                        found = true;
                        const char *p_classes[] = {"Rotation-Powered", "Accretion-Powered", "Magnetar"};
                        int p_type = lq->pulsars[p]->type;
                        SG_SPRINTF(rep, YELLOW "\n--- NEUTRON STAR (PULSAR) ANALYSIS ---" RESET "\nCLASS: %s\nADVISORY: Lethal radiation beams. Maintain distance > %.1f units.\n", 
                            (p_type >= 0 && p_type < 3) ? p_classes[p_type] : "Unknown", DIST_PULSAR_BEAM);
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_QUASAR && tid <= GALAXY_OBJECT_MAX_QUASAR) {
                for (int q = 0; q < lq->quasar_count; q++) {
                    if (lq->quasars[q]->id + GALAXY_OBJECT_MIN_QUASAR == tid) {
                        found = true;
                        const char *q_classes[] = {"Radio-loud", "Radio-quiet", "Broad absorption-line", "Type 2", "Red", "Optically violent variable", "Weak emission line"};
                        int q_type = lq->quasars[q]->type;
                        SG_SPRINTF(rep, YELLOW "\n--- EXTRAGALACTIC SOURCE (QUASAR) ANALYSIS ---" RESET "\nCLASS: %s\nADVISORY: Extreme energetic output. High gravimetric flux detected.\n", 
                            (q_type >= 0 && q_type < 7) ? q_classes[q_type] : "Unknown");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_MONSTER && tid <= GALAXY_OBJECT_MAX_MONSTER) {
                for (int m = 0; m < lq->monster_count; m++) {
                    if (lq->monsters[m]->id + GALAXY_OBJECT_MIN_MONSTER == tid) {
                        found = true;
                        SG_SPRINTF(rep, RED "\n--- XENO-BIOLOGICAL THREAT ---" RESET "\nTYPE: %s\nADVISORY: Highly aggressive. Close proximity will result in rapid energy drain.\n", 
                            (lq->monsters[m]->type == 30) ? "Crystalline Entity" : "Space Amoeba");
                    }
                }
                            } else if (tid >= GALAXY_OBJECT_MIN_PLATFORM && tid <= GALAXY_OBJECT_MAX_PLATFORM) {
                for (int p = 0; p < lq->platform_count; p++) {
                    if (lq->platforms[p]->id + GALAXY_OBJECT_MIN_PLATFORM == tid) {
                        found = true;
                        SG_SPRINTF(rep, CYAN "\n--- DEFENSE PLATFORM SCAN ---" RESET "\nENERGY: %" PRIu64 " units\nFACTION: %s\nSTATUS: ACTIVE\n",
                            scrambled ? (uint64_t)0 : lq->platforms[p]->energy, get_species_name(lq->platforms[p]->faction));
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_STARBASE && tid <= GALAXY_OBJECT_MAX_STARBASE) {
                for (int b = 0; b < lq->base_count; b++) {
                    if (lq->bases[b]->id + GALAXY_OBJECT_MIN_STARBASE == tid) {
                        found = true;
                        SG_SPRINTF(rep, CYAN "\n--- STARBASE DEEP SCAN ---" RESET "\nID: %d\nFACTION: %s\nENERGY: %" PRIu64 " units\nSTATUS: ACTIVE\nADVISORY: Authorized docking available for allied vessels.\n", 
                            tid, get_species_name(lq->bases[b]->faction), scrambled ? (uint64_t)0 : lq->bases[b]->energy);
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_DYSON && tid <= GALAXY_OBJECT_MAX_DYSON) {
                for (int d = 0; d < lq->dyson_count; d++) {
                    if (lq->dysons[d]->id + GALAXY_OBJECT_MIN_DYSON == tid) {
                        found = true;
                        SG_SPRINTF(rep, YELLOW "\n--- MEGASTRUCTURE FRAGMENT ANALYSIS ---" RESET "\nTYPE: Dyson Shell Component\nSTATUS: INERT\nADVISORY: Massive energy readings within internal conduits.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_HUB && tid <= GALAXY_OBJECT_MAX_HUB) {
                for (int h = 0; h < lq->hub_count; h++) {
                    if (lq->hubs[h]->id + GALAXY_OBJECT_MIN_HUB == tid) {
                        found = true;
                        SG_SPRINTF(rep, GREEN "\n--- INTERSTELLAR TRADING HUB ---" RESET "\nSTATUS: OPERATIONAL\nSERVICES: Logistics and Trade synchronization.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_RELIC && tid <= GALAXY_OBJECT_MAX_RELIC) {
                for (int r = 0; r < lq->relic_count; r++) {
                    if (lq->relics[r]->id + GALAXY_OBJECT_MIN_RELIC == tid) {
                        found = true;
                        SG_SPRINTF(rep, WHITE "\n--- ANCIENT RELIC SCAN ---" RESET "\nORIGIN: PRE-EXPANSION\nSTATUS: DORMANT\nADVISORY: High-density data cores detected.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_RUPTURE && tid <= GALAXY_OBJECT_MAX_RUPTURE) {
                for (int r = 0; r < lq->rupture_count; r++) {
                    if (lq->ruptures[r]->id + GALAXY_OBJECT_MIN_RUPTURE == tid) {
                        found = true;
                        SG_SPRINTF(rep, RED "\n--- SUBSPACE RUPTURE DETECTED ---" RESET "\nSTATUS: VOLATILE\nADVISORY: High risk of ship displacement or structural failure.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_SATELLITE && tid <= GALAXY_OBJECT_MAX_SATELLITE) {
                for (int s = 0; s < lq->satellite_count; s++) {
                    if (lq->satellites[s]->id + GALAXY_OBJECT_MIN_SATELLITE == tid) {
                        found = true;
                        SG_SPRINTF(rep, CYAN "\n--- ORBITAL SATELLITE ANALYSIS ---" RESET "\nTYPE: Automated Relay\nSTATUS: ACTIVE\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_STORM && tid <= GALAXY_OBJECT_MAX_STORM) {
                for (int s = 0; s < lq->storm_count; s++) {
                    if (lq->storms[s]->id + GALAXY_OBJECT_MIN_STORM == tid) {
                        found = true;
                        SG_SPRINTF(rep, BLUE "\n--- IONIC STORM ANALYSIS ---" RESET "\nTYPE: High-Energy Plasma Swirl\nADVISORY: Severe shield degradation expected upon entry.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_ARTIFACT && tid <= GALAXY_OBJECT_MAX_ARTIFACT) {
                for (int a = 0; a < lq->artifact_count; a++) {
                    if (lq->artifacts[a]->id + GALAXY_OBJECT_MIN_ARTIFACT == tid) {
                        found = true;
                        SG_SPRINTF(rep, WHITE "\n--- ALIEN ARTIFACT SCAN ---" RESET "\nSTATUS: UNKNOWN\nADVISORY: Proximity research recommended.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_WARP_GATE && tid <= GALAXY_OBJECT_MAX_WARP_GATE) {
                for (int w = 0; w < lq->warp_gate_count; w++) {
                    if (lq->warp_gates[w]->id + GALAXY_OBJECT_MIN_WARP_GATE == tid) {
                        found = true;
                        SG_SPRINTF(rep, MAGENTA "\n--- WARP GATE ARRAY ---" RESET "\nSTATUS: STANDBY\nADVISORY: Gateway synchronization required for transit.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_NEUTRON_STAR && tid <= GALAXY_OBJECT_MAX_NEUTRON_STAR) {
                for (int n = 0; n < lq->neutron_star_count; n++) {
                    if (lq->neutron_stars[n]->id + GALAXY_OBJECT_MIN_NEUTRON_STAR == tid) {
                        found = true;
                        SG_SPRINTF(rep, YELLOW "\n--- NEUTRON STAR ANALYSIS ---" RESET "\nTYPE: Degenerate Stellar Core\nADVISORY: Intense gravity and radiation. Approach with caution.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_MEGA_STRUCT && tid <= GALAXY_OBJECT_MAX_MEGA_STRUCT) {
                for (int m = 0; m < lq->mega_struct_count; m++) {
                    if (lq->mega_structs[m]->id + GALAXY_OBJECT_MIN_MEGA_STRUCT == tid) {
                        found = true;
                        SG_SPRINTF(rep, WHITE "\n--- MEGASTRUCTURE ANALYSIS ---" RESET "\nTYPE: Large-scale Habitability Ring or Array\nSTATUS: ACTIVE\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_DARK_CLOUD && tid <= GALAXY_OBJECT_MAX_DARK_CLOUD) {
                for (int d = 0; d < lq->dark_cloud_count; d++) {
                    if (lq->dark_clouds[d]->id + GALAXY_OBJECT_MIN_DARK_CLOUD == tid) {
                        found = true;
                        SG_SPRINTF(rep, BLUE "\n--- DARK MATTER CLOUD ANALYSIS ---" RESET "\nTYPE: Non-baryonic accumulation\nADVISORY: Visual and sensor range reduced within cloud.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_SINGULARITY && tid <= GALAXY_OBJECT_MAX_SINGULARITY) {
                for (int s = 0; s < lq->singularity_count; s++) {
                    if (lq->singularities[s]->id + GALAXY_OBJECT_MIN_SINGULARITY == tid) {
                        found = true;
                        SG_SPRINTF(rep, MAGENTA "\n--- QUANTUM SINGULARITY SCAN ---" RESET "\nTYPE: Stable Micro-Black Hole\nADVISORY: Potential source of energy or spatial distortion.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_PLASMA_STORM && tid <= GALAXY_OBJECT_MAX_PLASMA_STORM) {
                for (int p = 0; p < lq->plasma_storm_count; p++) {
                    if (lq->plasma_storms[p]->id + GALAXY_OBJECT_MIN_PLASMA_STORM == tid) {
                        found = true;
                        SG_SPRINTF(rep, RED "\n--- PLASMA STORM ANALYSIS ---" RESET "\nTYPE: Superheated ionized gas\nADVISORY: Extreme thermal stress on hull.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_ORBITAL_RING && tid <= GALAXY_OBJECT_MAX_ORBITAL_RING) {
                for (int r = 0; r < lq->orbital_ring_count; r++) {
                    if (lq->orbital_rings[r]->id + GALAXY_OBJECT_MIN_ORBITAL_RING == tid) {
                        found = true;
                        SG_SPRINTF(rep, WHITE "\n--- ORBITAL RING ARRAY ---" RESET "\nTYPE: Planetary Transit System\nSTATUS: OPERATIONAL\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_TIME_ANOMALY && tid <= GALAXY_OBJECT_MAX_TIME_ANOMALY) {
                for (int t = 0; t < lq->time_anomaly_count; t++) {
                    if (lq->time_anomalies[t]->id + GALAXY_OBJECT_MIN_TIME_ANOMALY == tid) {
                        found = true;
                        SG_SPRINTF(rep, MAGENTA "\n--- TEMPORAL ANOMALY SCAN ---" RESET "\nTYPE: Localized Time Dialation Field\nADVISORY: Extreme risk of causal paradox or displacement.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_VOID_CRYSTAL && tid <= GALAXY_OBJECT_MAX_VOID_CRYSTAL) {
                for (int v = 0; v < lq->void_crystal_count; v++) {
                    if (lq->void_crystals[v]->id + GALAXY_OBJECT_MIN_VOID_CRYSTAL == tid) {
                        found = true;
                        SG_SPRINTF(rep, CYAN "\n--- VOID CRYSTAL CLUSTER ---" RESET "\nTYPE: Subspace growth\nADVISORY: Resonates with warp fields.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_SUBSPACE_ANOM && tid <= GALAXY_OBJECT_MAX_SUBSPACE_ANOM) {
                for (int s = 0; s < lq->subspace_anomaly_count; s++) {
                    if (lq->subspace_anomalies[s]->id + GALAXY_OBJECT_MIN_SUBSPACE_ANOM == tid) {
                        found = true;
                        SG_SPRINTF(rep, BLUE "\n--- SUBSPACE ANOMALY ANALYSIS ---" RESET "\nTYPE: Spatial discontinuity\nADVISORY: Sensors reporting impossible physics readings.\n");
                    }
                }
            } else if (tid >= GALAXY_OBJECT_MIN_PROBE && tid <= GALAXY_OBJECT_MAX_PROBE) {
                int p_idx = (tid - GALAXY_OBJECT_MIN_PROBE) / 3;
                int pr_idx = (tid - GALAXY_OBJECT_MIN_PROBE) % 3;
                if (p_idx < MAX_CLIENTS && players[p_idx].state.probes[pr_idx].active) {
                    found = true;
                    const char *status_str[] = {"LAUNCHED", "ARRIVED", "TRANSMITTING"};
                    int s = players[p_idx].state.probes[pr_idx].status;
                    SG_SPRINTF(rep, CYAN "\n--- SENSOR PROBE ANALYSIS ---" RESET "\nID: %d\nORIGIN: %s\nSTATUS: %s\n", 
                        tid, players[p_idx].name, (s >= 0 && s < 3) ? status_str[s] : "UNKNOWN");
                }
            }
        }

        if (found) send_server_msg(i, "SCIENCE", rep);
        else send_server_msg(i, "COMPUTER", "Target not in sensor range.");
    } else send_server_msg(i, "COMPUTER", "Usage: scan <ID>");
}

