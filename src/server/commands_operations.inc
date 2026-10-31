/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * commands_operations.inc — command handlers: ship operations (clo, bor, dis, min, sco, har, doc, con, load).
 * Part of the command handlers, split by domain from commands_handlers.inc
 * (2026.10.31.15) without any code change; included, in this order, by
 * commands_handlers.inc into commands.c (one translation unit).
 */
void handle_clo(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    if (!players[i].state.is_cloaked) {
        /* 1. Integrity check for engagement (Auxiliary ID 9) */
        if (players[i].state.system_health[9] < (THRESHOLD_SYS_CRITICAL + (THRESHOLD_SYS_CRITICAL / RATIO_ENERGY_REDUCTION))) {
            send_server_msg(i, "ENGINEERING", "CLOAKING FAILURE: Gravimetric stabilizers damaged.");
            return;
        }

        /* 2. Activation Cost */
        if (players[i].state.energy < COST_CLOAK_INIT) {
            send_server_msg(i, "COMPUTER", "Insufficient energy to initialize cloaking field.");
            return;
        }
        
        /* 3. Reliability logic (15-50% health) */
        if (players[i].state.system_health[9] < THRESHOLD_SYS_DEGRADED && (rand() % 100 > (int)players[i].state.system_health[9])) {
            players[i].state.energy -= (COST_CLOAK_INIT / 5); /* Partial loss */
            send_server_msg(i, "ENGINEERING", "Cloaking field collapsed during synchronization!");
            return;
        }

        players[i].state.energy -= COST_CLOAK_INIT;
        players[i].state.is_cloaked = 1;
        send_server_msg(i, "HELMSMAN", "Cloaking device engaged. Sensors limited.");
    } else {
        players[i].state.is_cloaked = 0;
        send_server_msg(i, "HELMSMAN", "Cloaking device disengaged.");
    }
}

void handle_bor(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    int tid = 0;
    if(sscanf(params, " %d", &tid) != 1) {
        tid = players[i].state.lock_target;
    }
    /* Fallback to autopilot target if no lock/param */
    if (tid == 0) {
        tid = players[i].apr_target;
    }

    if (tid > 0) {
        /* 1. Hardware Requirement: Transporters (ID 3) >= 20% */
        if (players[i].state.system_health[3] < (THRESHOLD_SYS_CRITICAL + 10.0)) {
            send_server_msg(i, "ENGINEERING", "BOARDING ABORTED: Transporter buffers unstable. Repair required.");
            return;
        }

        /* 2. Energy Cost Check: 5000 units */
        if (players[i].state.energy < COST_BOARDING_INIT) { 
            char err_msg[128];
            SG_SPRINTF(err_msg, "Insufficient energy for massive matter transport (Req: %d).", COST_BOARDING_INIT);
            send_server_msg(i, "COMPUTER", err_msg); 
            return; 
        }
        
        double tx = 0.0, ty = 0.0, tz = 0.0; bool found = false;
        ConnectedPlayer *target_p = player_by_id(tid);
        NPCShip *target_n = npc_by_id(tid);
        /* Resolve target position and validity (Use ABSOLUTE GALACTIC coordinates for distance) */
        if (target_p && target_p->active) {
            tx = target_p->gx;
            ty = target_p->gy;
            tz = target_p->gz;
            found = true;
        } else if (target_n && target_n->active) {
            tx = target_n->gx;
            ty = target_n->gy;
            tz = target_n->gz;
            found = true;
        } else if (tid >= GALAXY_OBJECT_MIN_DERELICT && tid <= GALAXY_OBJECT_MAX_DERELICT && derelicts[tid - GALAXY_OBJECT_MIN_DERELICT].active) {
            tx = (derelicts[tid - GALAXY_OBJECT_MIN_DERELICT].q1 - 1) * QUADRANT_SIZE + derelicts[tid - GALAXY_OBJECT_MIN_DERELICT].x;
            ty = (derelicts[tid - GALAXY_OBJECT_MIN_DERELICT].q2 - 1) * QUADRANT_SIZE + derelicts[tid - GALAXY_OBJECT_MIN_DERELICT].y;
            tz = (derelicts[tid - GALAXY_OBJECT_MIN_DERELICT].q3 - 1) * QUADRANT_SIZE + derelicts[tid - GALAXY_OBJECT_MIN_DERELICT].z;
            found = true;
        } else if (tid >= GALAXY_OBJECT_MIN_PLATFORM && tid <= GALAXY_OBJECT_MAX_PLATFORM && platforms[tid - GALAXY_OBJECT_MIN_PLATFORM].active) {
            tx = (platforms[tid - GALAXY_OBJECT_MIN_PLATFORM].q1 - 1) * QUADRANT_SIZE + platforms[tid - GALAXY_OBJECT_MIN_PLATFORM].x;
            ty = (platforms[tid - GALAXY_OBJECT_MIN_PLATFORM].q2 - 1) * QUADRANT_SIZE + platforms[tid - GALAXY_OBJECT_MIN_PLATFORM].y;
            tz = (platforms[tid - GALAXY_OBJECT_MIN_PLATFORM].q3 - 1) * QUADRANT_SIZE + platforms[tid - GALAXY_OBJECT_MIN_PLATFORM].z;
            found = true;
        }
        
        if (found) {
            double dx = tx - players[i].gx;
            double dy = ty - players[i].gy;
            double dz = tz - players[i].gz; 
            double dist = sqrt(dx * dx + dy * dy + dz * dz);
            if (dist <= (DIST_BOARDING_MAX + (float)DIST_APPROACH_MARGIN)) {
                players[i].state.energy -= COST_BOARDING_INIT;

                /* 3. Success probability base (halved failure chance: 60% to 80% success) */
                int success_chance = PROB_BOARD_SUCCESS_BASE + (int)(players[i].state.system_health[3] * RATIO_BOARD_H_BONUS);

                /* Handle Player Boarding via Interactive Menu */
                if (target_p) {
                    ConnectedPlayer *target = target_p;
                    players[i].pending_bor_target = tid;
                    char menu[512];
                    if (target->faction == players[i].faction) {
                        players[i].pending_bor_type = 1; /* Ally */
                        SG_SPRINTF(menu, CYAN "\n--- BOARDING MENU: ALLIED VESSEL (%s) ---\n" RESET 
                               "1: Transfer Energy (%d units)\n"
                               "2: Technical Support (Repair random system)\n"
                               "3: Reinforce Crew (Transfer 20 personnel)\n"
                               YELLOW "Type the number to confirm choice." RESET, target->name, DMG_TORPEDO_PLATFORM);
                    } else {
                        players[i].pending_bor_type = 2; /* Enemy */
                        SG_SPRINTF(menu, RED "\n--- BOARDING MENU: HOSTILE VESSEL (%s) ---\n" RESET 
                               "1: Sabotage (Damage random system)\n"
                               "2: Raid Cargo (Steal random resources)\n"
                               "3: Take Hostages (Capture officers as prisoners)\n"
                               YELLOW "Type the number to confirm choice." RESET, target->name);
                    }
                    send_server_msg(i, "BOARDING", menu);
                    return;
                }

                /* Handle other targets with probability logic */
                if (rand() % 100 < success_chance) {
                    if (tid >= GALAXY_OBJECT_MIN_DERELICT && tid <= GALAXY_OBJECT_MAX_DERELICT) {
                        int d_idx = tid - GALAXY_OBJECT_MIN_DERELICT;
                        players[i].pending_bor_target = tid;
                        players[i].pending_bor_type = 4;
                        char menu[512];
                        if (derelicts[d_idx].faction == players[i].faction) {
                            SG_SPRINTF(menu, CYAN "\n--- BOARDING MENU: ALLIED WRECK [%d] ---\n" RESET 
                                   "1: Salvage Resources\n"
                                   "2: Recover Data (Map reveal)\n"
                                   "3: Field Repairs\n"
                                   "4: Rescue Survivors (Crew)\n"
                                   YELLOW "Type the number to confirm choice." RESET, tid);
                        } else {
                            SG_SPRINTF(menu, RED "\n--- BOARDING MENU: ALIEN WRECK [%d] ---\n" RESET 
                                   "1: Salvage Resources\n"
                                   "2: Recover Data (Map reveal)\n"
                                   "3: Field Repairs\n"
                                   "4: Take Prisoners (Stasis units)\n"
                                   YELLOW "Type the number to confirm choice." RESET, tid);
                        }
                        send_server_msg(i, "BOARDING", menu);
                    } else if (tid >= GALAXY_OBJECT_MIN_PLATFORM && tid <= GALAXY_OBJECT_MAX_PLATFORM) {
                        players[i].pending_bor_target = tid;
                        players[i].pending_bor_type = 3;
                        char menu[512];
                        SG_SPRINTF(menu, YELLOW "\n--- BOARDING MENU: DEFENSE PLATFORM [%d] ---\n" RESET 
                               "1: Reprogram IFF\n"
                               "2: Overload Reactor\n"
                               "3: Salvage Tech\n"
                               WHITE "Type the number to confirm choice." RESET, tid);
                        send_server_msg(i, "BOARDING", menu);
                    } else if (target_n) {
                        NPCShip *target_npc = target_n;
                        /* Requirement: Target must be disabled (Engines < 50% OR Hull < 50%) */
                        if (target_npc->engine_health >= THRESHOLD_SYS_DEGRADED && target_npc->health >= (YIELD_MINE_MAX)) {
                            send_server_msg(i, "SECURITY", "BOARDING DENIED: Target vessel is still fully operational. Disable engines or damage hull first!");
                            return;
                        }
                        
                        players[i].pending_bor_target = tid;
                        players[i].pending_bor_type = 2; /* Enemy */
                        char menu[512];
                        SG_SPRINTF(menu, RED "\n--- BOARDING MENU: HOSTILE NPC [%d] ---\n" RESET 
                               "1: Sabotage Engines\n"
                               "2: Raid Cargo Bay\n"
                               "3: Take Hostages\n"
                               YELLOW "Type the number to confirm choice." RESET, tid);
                        send_server_msg(i, "BOARDING", menu);
                    } else {
                        /* NPC success reward */
                        int reward = rand() % 2;
                        if (reward == 0) {
                            players[i].state.inventory[1] += 5;
                            send_server_msg(i, "BOARDING", "Success! Captured Aetherium crystals.");
                        } else { 
                            int found_people = 5 + rand() % 25;
                            players[i].state.prison_unit += found_people;
                            char m[128];
                            SG_SPRINTF(m, "Success! Captured %d enemy prisoners.", found_people);
                            send_server_msg(i, "SECURITY", m);
                        }
                    }
                } else {
                    int loss = 5 + rand() % 15; 
                    players[i].state.crew_count -= loss;
                    if (players[i].state.crew_count < 0) {
                        players[i].state.crew_count = 0;
                    }
                    send_server_msg(i, "SECURITY", "Boarding party repelled! Heavy casualties reported.");
                }
            } else {
                send_server_msg(i, "COMPUTER", "Target not in transporter range (< 1.0).");
            }
        } else {
            send_server_msg(i, "COMPUTER", "Invalid boarding target.");
        }
    } else send_server_msg(i, "COMPUTER", "Usage: bor <ID>");
}

void handle_dis(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    int tid = 0; 
    if(sscanf(params, " %d", &tid) != 1) {
        tid = players[i].state.lock_target;
    }
    /* Fallback to autopilot target if no lock/param */
    if (tid == 0) {
        tid = players[i].apr_target;
    }

    if (tid > 0) {
        /* 1. Hardware Requirement: Transporters (ID 3) >= 15% */
        if (players[i].state.system_health[3] < (THRESHOLD_SYS_CRITICAL + (THRESHOLD_SYS_CRITICAL / RATIO_ENERGY_REDUCTION))) {
            send_server_msg(i, "ENGINEERING", "DISMANTLING FAILURE: Transporter arrays damaged. Cannot stabilize debris beams.");
            return;
        }

        /* 2. Energy Cost Check */
        if (players[i].state.energy < COST_ACTION_EXTREME) {
            char err_msg[128];
            SG_SPRINTF(err_msg, "Insufficient energy for structural dismantling operation (Req: %d).", COST_ACTION_EXTREME);
            send_server_msg(i, "COMPUTER", err_msg);
            return;
        }

        bool done = false;
        
        /* 4. Yield scaling based on Transporter integrity (0.7 to 1.0 multiplier) */
        double transporter_mult = RATIO_CONVERSION_EFF_MIN + (players[i].state.system_health[3] / (double)YIELD_HARVEST_MAX) * (1.0 - RATIO_CONVERSION_EFF_MIN);

        /* Case: NPC Ship Wreck (ID 1000-3999) */
        NPCShip *target_npc = npc_by_id(tid);
        if (target_npc) {
            /* Restriction: Only wrecks in the same quadrant can be dismantled */
            if (target_npc->active && target_npc->q1 == players[i].state.q1 && 
                target_npc->q2 == players[i].state.q2 && target_npc->q3 == players[i].state.q3) {
                double tx = target_npc->gx;
                double ty = target_npc->gy;
                double tz = target_npc->gz;
                double dx = tx - players[i].gx;
                double dy = ty - players[i].gy;
                double dz = tz - players[i].gz; 
                
                if (sqrt(dx * dx + dy * dy + dz * dz) < DIST_DISMANTLE_MAX) {
                    players[i].state.energy -= COST_ACTION_EXTREME;
                    uint64_t yield = (uint64_t)((double)(target_npc->energy / YIELD_BOARD_ENERGY_DIV) * transporter_mult);  // NOLINT(bugprone-integer-division): integer division first, the yield as the game has always computed it
                    if (yield < (uint64_t)THRESHOLD_SYS_CRITICAL) {
                        yield = (uint64_t)THRESHOLD_SYS_CRITICAL;
                    }
                    
                    players[i].state.inventory[2] += (int)yield; /* Neo-Titanium */
                    players[i].state.inventory[5] += (int)(yield / 5); /* Synaptics */
                    target_npc->active = 0;
                    
                    /* Visual FX */
                    double rs1 = (tx - (players[i].state.q1 - 1) * QUADRANT_SIZE);
                    double rs2 = (ty - (players[i].state.q2 - 1) * QUADRANT_SIZE);
                    double rs3 = (tz - (players[i].state.q3 - 1) * QUADRANT_SIZE);
                    broadcast_server_event(players[i].state.q1, players[i].state.q2, players[i].state.q3, IPC_EV_DISMANTLE, rs1, rs2, rs3, 0, 0, 0, target_npc->faction);
                    
                    char msg[128];
                    SG_SPRINTF(msg, "Vessel dismantled. Recovered %" PRIu64 " Neo-Titanium and %" PRIu64 " Synaptics.", yield, yield / 5);
                    send_server_msg(i, "ENGINEERING", msg);
                    slog("[DISMANTLE] Player %d dismantled NPC %d (Yield: %" PRIu64 ")\n", i, tid, yield);
                    if (players[i].state.lock_target == tid) {
                        players[i].state.lock_target = 0;
                    }
                    done = true;
                } else {
                    char err_msg[128];
                    double dist = sqrt(dx * dx + dy * dy + dz * dz);
                    SG_SPRINTF(err_msg, "Not in range for dismantling (Dist %.2f > %.1f).", dist, DIST_DISMANTLE_MAX);
                    send_server_msg(i, "COMPUTER", err_msg);
                    slog("[DISMANTLE] Player %d failed NPC %d: Distance %.2f\n", i, tid, dist);
                    return;
                }
            } else {
                send_server_msg(i, "COMPUTER", "Target inactive. Scan for derelict ID to dismantle.");
            }
        } 
        /* Case: Static Derelict Wreck (ID 15000+) */
        else if (tid >= GALAXY_OBJECT_MIN_DERELICT && tid <= GALAXY_OBJECT_MAX_DERELICT) {
            int d_idx = tid - GALAXY_OBJECT_MIN_DERELICT;
            /* Restriction: Only derelicts in the same quadrant can be dismantled */
            if (derelicts[d_idx].active && derelicts[d_idx].q1 == players[i].state.q1 && 
                derelicts[d_idx].q2 == players[i].state.q2 && derelicts[d_idx].q3 == players[i].state.q3) {
                double tx = (derelicts[d_idx].q1 - 1) * QUADRANT_SIZE + derelicts[d_idx].x;
                double ty = (derelicts[d_idx].q2 - 1) * QUADRANT_SIZE + derelicts[d_idx].y;
                double tz = (derelicts[d_idx].q3 - 1) * QUADRANT_SIZE + derelicts[d_idx].z;
                double dx = tx - players[i].gx;
                double dy = ty - players[i].gy;
                double dz = tz - players[i].gz; 
                
                if (sqrt(dx * dx + dy * dy + dz * dz) < DIST_DISMANTLE_MAX) {
                    players[i].state.energy -= COST_ACTION_EXTREME;
                    uint64_t yield = (uint64_t)((YIELD_BOARD_NPC_MIN + rand() % YIELD_BOARD_NPC_RANGE) * transporter_mult);
                     
                    players[i].state.inventory[2] += (int)yield; 
                    players[i].state.inventory[5] += (int)(yield / 4); 
                    derelicts[d_idx].active = 0;
                    
                    /* Visual FX - Use local quadrant coordinates (0.0 - 40.0) relative to player for consistent rendering */
                    double rs1 = derelicts[d_idx].x;
                    double rs2 = derelicts[d_idx].y;
                    double rs3 = derelicts[d_idx].z;
                    broadcast_server_event(players[i].state.q1, players[i].state.q2, players[i].state.q3, IPC_EV_DISMANTLE, rs1, rs2, rs3, 0, 0, 0, derelicts[d_idx].faction);
                    
                    char msg[128];
                    SG_SPRINTF(msg, "Ancient wreck dismantled. Recovered %" PRIu64 " Neo-Titanium and %" PRIu64 " Synaptics.", yield, yield / 4);
                    send_server_msg(i, "ENGINEERING", msg);
                    slog("[DISMANTLE] Player %d dismantled Derelict %d (Yield: %" PRIu64 ")\n", i, tid, yield);
                    if (players[i].state.lock_target == tid) {
                        players[i].state.lock_target = 0;
                    }
                    done = true;
                } else {
                    char err_msg[128];
                    double dist = sqrt(dx * dx + dy * dy + dz * dz);
                    SG_SPRINTF(err_msg, "Not in range for dismantling (Dist %.2f > %.1f).", dist, DIST_DISMANTLE_MAX);
                    send_server_msg(i, "COMPUTER", err_msg);
                    slog("[DISMANTLE] Player %d failed Derelict %d: Distance %.2f\n", i, tid, dist);
                    return;
                }
            }
        }

        if (!done) send_server_msg(i, "COMPUTER", "Invalid dismantle target (Must be a wreck or derelict).");
    } else {
        send_server_msg(i, "COMPUTER", "Usage: dis <ID> or lock a target first.");
    }
}

void handle_min(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    /* 1. Hardware Requirement: Transporters (ID 3) >= 15% */
    if (players[i].state.system_health[3] < (THRESHOLD_SYS_CRITICAL + (THRESHOLD_SYS_CRITICAL / RATIO_ENERGY_REDUCTION))) {
        send_server_msg(i, "ENGINEERING", "MINING FAILURE: Extraction beams offline (Transporter damage).");
        return;
    }

    /* 2. Energy Cost: 250 units */
    if (players[i].state.energy < COST_ACTION_VERY_HIGH) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for planetary extraction pulse.");
        return;
    }

    int locked_id = players[i].state.lock_target;
    int target_idx = -1;
    int target_type = -1; /* 0: Asteroid, 1: Planet, 2: star-system planet */
    double min_dist = DIST_INTERACTION_MAX; /* Interaction range */

    /* Search for mineable target (locked or closest) */
    int q1 = players[i].state.q1, q2 = players[i].state.q2, q3 = players[i].state.q3;
    
    if (locked_id >= GALAXY_OBJECT_MIN_PLANET && locked_id <= GALAXY_OBJECT_MAX_PLANET) {
        int p = locked_id - GALAXY_OBJECT_MIN_PLANET;
        if (p >= 0 && p < MAX_PLANETS && planets[p].active && planets[p].q1 == q1 && planets[p].q2 == q2 && planets[p].q3 == q3) {
            double d = sqrt(pow(planets[p].x - players[i].state.s1, 2) + pow(planets[p].y - players[i].state.s2, 2) + pow(planets[p].z - players[i].state.s3, 2));
            if (d <= min_dist) {
                target_idx = p;
                target_type = 1;
            }
        }
    } else if (sys_id_is_planet(locked_id)) {
        /* star-system planet: the range is measured from its centre, around its larger contact sphere */
        int sp = locked_id - GALAXY_OBJECT_MIN_SYS_PLANET;
        NPCSysPlanet *pl = &sys_planets[sp];
        if (pl->active && pl->q1 == q1 && pl->q2 == q2 && pl->q3 == q3) {
            sys_refresh_positions(sys_planet_star(sp), sys_clock_s());
            double d = sqrt(pow(pl->x - players[i].state.s1, 2) + pow(pl->y - players[i].state.s2, 2) + pow(pl->z - players[i].state.s3, 2));
            if (d <= sys_interaction_range(sys_planet_hit_radius(), DIST_INTERACTION_MAX)) {
                target_idx = sp;
                target_type = 2;
            }
        }
    } else if (locked_id >= GALAXY_OBJECT_MIN_ASTEROID && locked_id <= GALAXY_OBJECT_MAX_ASTEROID) {
        int a = locked_id - GALAXY_OBJECT_MIN_ASTEROID;
        if (a >= 0 && a < MAX_ASTEROIDS && asteroids[a].active && asteroids[a].q1 == q1 && asteroids[a].q2 == q2 && asteroids[a].q3 == q3) {
            double d = sqrt(pow(asteroids[a].x - players[i].state.s1, 2) + pow(asteroids[a].y - players[i].state.s2, 2) + pow(asteroids[a].z - players[i].state.s3, 2));
            if (d <= min_dist) {
                target_idx = a;
                target_type = 0;
            }
        }
    }

    /* If no valid target, find closest */
    if (target_idx == -1) {
        for (int a = 0; a < MAX_ASTEROIDS; a++) {
            if (asteroids[a].active && asteroids[a].q1 == q1 && asteroids[a].q2 == q2 && asteroids[a].q3 == q3) {
                double d = sqrt(pow(asteroids[a].x - players[i].state.s1, 2) + pow(asteroids[a].y - players[i].state.s2, 2) + pow(asteroids[a].z - players[i].state.s3, 2));
                if (d < min_dist) { min_dist = d; target_idx = a; target_type = 0; }
            }
        }
        for (int p = 0; p < MAX_PLANETS; p++) {
            if (planets[p].active && planets[p].q1 == q1 && planets[p].q2 == q2 && planets[p].q3 == q3) {
                double d = sqrt(pow(planets[p].x - players[i].state.s1, 2) + pow(planets[p].y - players[i].state.s2, 2) + pow(planets[p].z - players[i].state.s3, 2));
                if (d < min_dist) { min_dist = d; target_idx = p; target_type = 1; }
            }
        }
        /* star-system planets of this quadrant: their limit is larger (contact radius + 2.3), so they are
         * compared on the same footing, as the distance to the generic limit (min_dist is DIST_INTERACTION_MAX) */
        const QuadrantIndex *mq = &spatial_index[q1][q2][q3];
        if (mq->sys_star_count > 0 && mq->sys_stars[0]) {
            sys_refresh_positions((int)(mq->sys_stars[0] - sys_stars), sys_clock_s());
            double extra = sys_interaction_range(sys_planet_hit_radius(), DIST_INTERACTION_MAX) - DIST_INTERACTION_MAX;
            for (int k = 0; k < mq->sys_planet_count; k++) {
                NPCSysPlanet *pl = mq->sys_planets[k];
                if (!pl || !pl->active) continue;
                double d = sqrt(pow(pl->x - players[i].state.s1, 2) + pow(pl->y - players[i].state.s2, 2) + pow(pl->z - players[i].state.s3, 2)) - extra;
                if (d < min_dist) { min_dist = d; target_idx = (int)(pl - sys_planets); target_type = 2; }
            }
        }
    }

    if (target_idx != -1) {
        players[i].state.energy -= COST_ACTION_VERY_HIGH;
        /* 3. Yield scaling based on Transporter integrity (0.8 to 1.0 multiplier) */
        double yield_mult = RATIO_MINING_EFF_MIN + (players[i].state.system_health[3] / (double)YIELD_HARVEST_MAX) * (1.0 - RATIO_MINING_EFF_MIN);
        const char* res_names[] = {"None", "Aetherium", "Neo-Titanium", "Void-Essence", "Graphene", "Synaptics", "Nebular Gas", "Composite", "Dark-Matter"};

        int r_type, raw_amount;
        if (target_type == 0) {
            r_type = asteroids[target_idx].resource_type;
            raw_amount = (asteroids[target_idx].amount > (YIELD_MINE_MAX / 2)) ? (YIELD_MINE_MAX / 2) : asteroids[target_idx].amount;
            asteroids[target_idx].amount -= raw_amount;
            if (asteroids[target_idx].amount <= 0) asteroids[target_idx].active = 0;
        } else if (target_type == 1) {
            r_type = planets[target_idx].resource_type;
            raw_amount = (planets[target_idx].amount > YIELD_MINE_MAX) ? YIELD_MINE_MAX : planets[target_idx].amount;
            planets[target_idx].amount -= raw_amount;
        } else {
            /* star-system planet: same yield rule as the other planets */
            r_type = sys_planets[target_idx].resource_type;
            raw_amount = (sys_planets[target_idx].amount > YIELD_MINE_MAX) ? YIELD_MINE_MAX : sys_planets[target_idx].amount;
            sys_planets[target_idx].amount -= raw_amount;
        }

        int final_yield = (int)(raw_amount * yield_mult);
        players[i].state.inventory[r_type] += final_yield;
        
        /* 4. Improved Feedback */
        char msg[128];
        SG_SPRINTF(msg, "[RADIO] MINING: Collected %d units of %s. Extraction efficiency: %.0f%%.", final_yield, res_names[r_type], yield_mult * 100);
        send_server_msg(i, "GEOLOGY", msg);
    } else {
        char err_msg[128];
        SG_SPRINTF(err_msg, "No planet or asteroid in range for mining (< %.1f units; %.1f from the centre of a system planet).", DIST_INTERACTION_MAX, sys_interaction_range(sys_planet_hit_radius(), DIST_INTERACTION_MAX));
        send_server_msg(i, "COMPUTER", err_msg);
    }
}

void handle_sco(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    /* 1. Hardware Requirement: Auxiliary (ID 9) >= 15% */
    if (players[i].state.system_health[9] < 15.0) {
        send_server_msg(i, "ENGINEERING", "SCOOPING FAILURE: Solar collectors damaged. Repair Auxiliary systems.");
        return;
    }

    /* 2. Operational Cost */
    if (players[i].state.energy < COST_ACTION_HIGH) {
        send_server_msg(i, "COMPUTER", "Insufficient energy to initialize magnetic collection field.");
        return;
    }

    bool near = false; 
    int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
    for(int s=0; s<MAX_STARS; s++) {
        if(stars_data[s].active && stars_data[s].q1==pq1 && stars_data[s].q2==pq2 && stars_data[s].q3==pq3) {
            double d=sqrt(pow(stars_data[s].x-players[i].state.s1,2)+pow(stars_data[s].y-players[i].state.s2,2)+pow(stars_data[s].z-players[i].state.s3,2)); 
            if(d < DIST_SCOOPING_MAX) { near=true; break; }
        }
    }

    /* the star of this quadrant's star system: range measured around its larger contact sphere */
    if (!near) {
        const QuadrantIndex *sq = &spatial_index[pq1][pq2][pq3];
        if (sq->sys_star_count > 0 && sq->sys_stars[0] && sq->sys_stars[0]->active) {
            const NPCSysStar *st = sq->sys_stars[0];
            double d = sqrt(pow(st->x - players[i].state.s1, 2) + pow(st->y - players[i].state.s2, 2) + pow(st->z - players[i].state.s3, 2));
            if (d < sys_interaction_range(sys_star_hit_radius(), DIST_SCOOPING_MAX)) near = true;
        }
    }

    if(near) {
        players[i].state.energy -= COST_ACTION_HIGH;
        
        /* 3. Variable Yield based on Auxiliary health (0.8 to 1.0 multiplier) */
        double aux_h = players[i].state.system_health[9];
        int gain = (int)(COST_HYPERDRIVE_INIT * (RATIO_MINING_EFF_MIN + (aux_h / (double)YIELD_HARVEST_MAX) * (1.0 - RATIO_MINING_EFF_MIN)));
        
        players[i].state.cargo_energy += gain; 
        if(players[i].state.cargo_energy > MAX_ENERGY_CAPACITY) players[i].state.cargo_energy = MAX_ENERGY_CAPACITY; 
        
        /* 4. Shield and Structural Damage */
        int s_idx = rand()%6; 
        if (players[i].state.shields[s_idx] >= COST_ACTION_EXTREME) {
            players[i].state.shields[s_idx] -= COST_ACTION_EXTREME;
        } else {
            /* Heat bypasses shields */
            players[i].state.shields[s_idx] = 0;
            double hull_dmg = 2.0 + (rand() % 300) / (double)YIELD_HARVEST_MAX;
            apply_hull_damage(i, hull_dmg);
            players[i].state.system_health[7] -= (THRESHOLD_SYS_CRITICAL / 2.0); /* Life Support damage (ID 7) */
            send_server_msg(i, "CRITICAL", "WARNING: Solar heat bypassing shields! Hull and Life Support damaged!");
        }
        
        char msg[128];
        SG_SPRINTF(msg, "Solar energy harvested: %" PRIu64 " units stored in Cargo Bay. Collectors efficiency: %.0f%%.", (uint64_t)gain, (aux_h/(double)YIELD_HARVEST_MAX)*100);
        send_server_msg(i, "ENGINEERING", msg); 
    } 
    else {
        char err_msg[128];
        SG_SPRINTF(err_msg, "No star in proximity range (< %.1f units; %.1f from the centre of a system star).", DIST_SCOOPING_MAX, sys_interaction_range(sys_star_hit_radius(), DIST_SCOOPING_MAX));
        send_server_msg(i, "COMPUTER", err_msg);
    }
}

void handle_har(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    /* 1. Hardware Requirement: Auxiliary (ID 9) >= 25% (Higher than sco) */
    if (players[i].state.system_health[9] < THRESHOLD_SYS_DAMAGED) {
        send_server_msg(i, "ENGINEERING", "HARVESTING FAILURE: Antimatter containment field unstable. Repairs required.");
        return;
    }

    /* 2. Operational Cost */
    if (players[i].state.energy < COST_ACTION_EXTREME) {
        send_server_msg(i, "COMPUTER", "Insufficient energy to initialize Hawking radiation shielding.");
        return;
    }

    bool near = false; 
    int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
    for(int h=0; h<MAX_BH; h++) {
        if(black_holes[h].active && black_holes[h].q1==pq1 && black_holes[h].q2==pq2 && black_holes[h].q3==pq3) {
            double d=sqrt(pow(black_holes[h].x-players[i].state.s1,2)+pow(black_holes[h].y-players[i].state.s2,2)+pow(black_holes[h].z-players[i].state.s3,2)); 
            if(d <= DIST_INTERACTION_MAX) { near=true; break; }
        }
    }

    if(near) { 
        players[i].state.energy -= COST_ACTION_EXTREME;
        
        /* 3. Variable Yield based on Auxiliary health (0.6 to 1.0 multiplier) */
        double aux_h = players[i].state.system_health[9];
        double efficiency = RATIO_HARVEST_EFF_MIN + (aux_h / (double)YIELD_HARVEST_MAX) * (1.0 - RATIO_HARVEST_EFF_MIN);
        
        uint64_t energy_gain = (uint64_t)(SHIELD_MAX_STRENGTH * efficiency);
        int crystal_gain = (int)(YIELD_HARVEST_MAX * efficiency);
        
        players[i].state.cargo_energy += energy_gain; 
        if(players[i].state.cargo_energy > MAX_ENERGY_CAPACITY) players[i].state.cargo_energy = MAX_ENERGY_CAPACITY; 
        players[i].state.inventory[1] += crystal_gain; 
        
        /* 4. Shield, Hull and System Damage (Radiation and Gravity) */
        int s_idx = rand()%6; 
        if (players[i].state.shields[s_idx] >= (COST_ACTION_EXTREME * 2)) {
            players[i].state.shields[s_idx] -= (COST_ACTION_EXTREME * 2);
        } else {
            /* Event horizon shear bypasses weak shields */
            players[i].state.shields[s_idx] = 0;
            double hull_dmg = (THRESHOLD_SYS_CRITICAL / 2.0) + (rand() % (int)COST_ACTION_EXTREME) / (double)YIELD_HARVEST_MAX;
            apply_hull_damage(i, hull_dmg);
            
            /* Heavy damage to critical systems: Sensors(2), Computer(6), Life Support(7) */
            players[i].state.system_health[2] -= ((THRESHOLD_SYS_CRITICAL / 2.0) + (rand() % (int)THRESHOLD_SYS_CRITICAL));
            players[i].state.system_health[6] -= ((THRESHOLD_SYS_CRITICAL / 2.0) + (rand() % (int)THRESHOLD_SYS_CRITICAL));
            players[i].state.system_health[7] -= ((THRESHOLD_SYS_CRITICAL / 2.0) + (rand() % (int)THRESHOLD_SYS_CRITICAL));
            
            send_server_msg(i, "CRITICAL", "EVENT HORIZON SHEAR! Shields buckled! Hull and internal systems damaged!");
        }
        
        char msg[128];
        SG_SPRINTF(msg, "Antimatter harvested: %" PRIu64 " units. Aetherium crystals: +%d. Containment efficiency: %.0f%%.", energy_gain, crystal_gain, efficiency * 100);
        send_server_msg(i, "ENGINEERING", msg); 
    } 
    else {
        char err_msg[128];
        SG_SPRINTF(err_msg, "No black hole in proximity range (< %.1f units).", DIST_INTERACTION_MAX);
        send_server_msg(i, "COMPUTER", err_msg);
    }
}

void handle_doc(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    int base_idx = -1;
    for(int b=0; b<MAX_BASES; b++) {
        if(bases[b].active && bases[b].q1==players[i].state.q1 && bases[b].q2==players[i].state.q2 && bases[b].q3==players[i].state.q3) {
            double d=sqrt(pow(bases[b].x-players[i].state.s1,2)+pow(bases[b].y-players[i].state.s2,2)+pow(bases[b].z-players[i].state.s3,2)); 
            if(d <= (DIST_DOCKING_MAX + (double)DIST_EPSILON)) { base_idx = b; break; } 
        }
    }

    if(base_idx != -1) {
        if (bases[base_idx].faction != players[i].faction && bases[base_idx].faction != FACTION_ALLIANCE) {
            /* Allow sequence to start so defense AI can engage, but warn immediately */
            players[i].state.energy -= COST_ACTION_HIGH;
            players[i].nav_state = NAV_STATE_DOCKING;
            players[i].nav_timer = (int)TIMER_DOCKING_SEQ;
            players[i].hyper_speed = 0;
            players[i].pending_bor_target = base_idx + GALAXY_OBJECT_MIN_STARBASE;
            char alert[256];
            SG_SPRINTF(alert, "\033[31m*** RED ALERT: UNAUTHORIZED DOCKING ATTEMPT DETECTED! %s DEFENSIVE BATTERIES ARE POWERING UP! ***\033[0m", get_species_name(bases[base_idx].faction));
            send_server_msg(i, "STARBASE", alert);
            return;
        }

        /* Integrity Checks: Impulse (ID 1) and Auxiliary (ID 9) */
        if (players[i].state.system_health[1] < THRESHOLD_SYS_CRITICAL || players[i].state.system_health[9] < THRESHOLD_SYS_CRITICAL) {
            send_server_msg(i, "ENGINEERING", "DOCKING FAILURE: Maneuvering thrusters or docking clamps offline.");
            return;
        }

        if (players[i].state.energy < COST_ACTION_HIGH) {
            send_server_msg(i, "COMPUTER", "Insufficient energy for magnetic docking sequence.");
            return;
        }

        /* Start Docking Sequence (10 seconds) */
        players[i].state.energy -= COST_ACTION_HIGH;
        players[i].nav_state = NAV_STATE_DOCKING;
        players[i].nav_timer = (int)TIMER_DOCKING_SEQ;
        players[i].hyper_speed = 0; /* Stop ship movement */
        
        /* Repurpose pending_bor_target to store base index during docking */
        players[i].pending_bor_target = base_idx + GALAXY_OBJECT_MIN_STARBASE; 

        send_server_msg(i, get_species_name(bases[base_idx].faction), "Docking sequence initiated. Clamps engaging. Please hold position for 10 seconds.");
    } 
    else send_server_msg(i,"COMPUTER","No starbase in range.");
}

void handle_con(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    int type, amount;
    if (sscanf(params, "%d %d", &type, &amount) != 2) {
        send_server_msg(i, "COMPUTER", "Usage: con <Type> <Amount>");
        return;
    }

    /* 1. Hardware Requirement: Auxiliary (ID 9) */
    if (players[i].state.system_health[9] < (THRESHOLD_SYS_CRITICAL + (THRESHOLD_SYS_CRITICAL / RATIO_ENERGY_REDUCTION))) {
        char err_msg[128];
        SG_SPRINTF(err_msg, "CONVERTER FAILURE: Auxiliary systems integrity too low (< %.0f%%).", (THRESHOLD_SYS_CRITICAL + (THRESHOLD_SYS_CRITICAL / RATIO_ENERGY_REDUCTION)));
        send_server_msg(i, "ENGINEERING", err_msg);
        return;
    }

    /* 2. Energy Cost: 100 units */
    if (players[i].state.energy < COST_ACTION_HIGH) {
        char err_msg[128];
        SG_SPRINTF(err_msg, "Insufficient energy for conversion cycle (Req: %d).", COST_ACTION_HIGH);
        send_server_msg(i, "COMPUTER", err_msg);
        return;
    }

    if (amount <= 0) return;
    if (type < 1 || type > 8 || players[i].state.inventory[type] < amount) {
        send_server_msg(i, "LOGISTICS", "Insufficient resources in cargo bay.");
        return;
    }

    /* 3. Efficiency based on Auxiliary health (70% - 100%) */
    double efficiency = RATIO_CONVERSION_EFF_MIN + (players[i].state.system_health[9] / (double)YIELD_HARVEST_MAX) * (1.0 - RATIO_CONVERSION_EFF_MIN);
    players[i].state.energy -= COST_ACTION_HIGH;
    players[i].state.inventory[type] -= amount;

    char msg[128];
    if (type == 1) { /* Aetherium -> Energy x10 */
        uint64_t gain = (uint64_t)(amount * YIELD_CONVERSION_AE * efficiency);
        players[i].state.cargo_energy += gain;
        if (players[i].state.cargo_energy > MAX_ENERGY_CAPACITY) players[i].state.cargo_energy = MAX_ENERGY_CAPACITY;
        SG_SPRINTF(msg, "Converted %d Aetherium into %" PRIu64 " Energy units. Efficiency: %.1f%%.", amount, gain, efficiency * 100);
    } else if (type == 2) { /* Neo-Titanium -> Energy x2 */
        uint64_t gain = (uint64_t)(amount * 2 * efficiency);
        players[i].state.cargo_energy += gain;
        if (players[i].state.cargo_energy > MAX_ENERGY_CAPACITY) players[i].state.cargo_energy = MAX_ENERGY_CAPACITY;
        SG_SPRINTF(msg, "Converted %d Neo-Titanium into %" PRIu64 " Energy units. Efficiency: %.1f%%.", amount, gain, efficiency * 100);
    } else if (type == 3) { /* Void-Essence -> Torpedoes (1/20) */
        int gain = (int)((amount / YIELD_CONVERSION_TORP) * efficiency);
        players[i].state.cargo_torpedoes += gain;
        if (players[i].state.cargo_torpedoes > MAX_TORPEDO_CAPACITY) players[i].state.cargo_torpedoes = MAX_TORPEDO_CAPACITY;
        SG_SPRINTF(msg, "Converted %d Void-Essence into %d Torpedoes. Efficiency: %.1f%%.", amount, gain, efficiency * 100);
    } else if (type == 6) { /* Gas -> Energy x5 */
        uint64_t gain = (uint64_t)(amount * YIELD_CONVERSION_GAS * efficiency);
        players[i].state.cargo_energy += gain;
        if (players[i].state.cargo_energy > MAX_ENERGY_CAPACITY) players[i].state.cargo_energy = MAX_ENERGY_CAPACITY;
        SG_SPRINTF(msg, "Converted %d Nebular Gas into %" PRIu64 " Energy units. Efficiency: %.1f%%.", amount, gain, efficiency * 100);
    } else if (type == 7) { /* Composite -> Energy x4 */
        uint64_t gain = (uint64_t)(amount * YIELD_CONVERSION_COMP * efficiency);
        players[i].state.cargo_energy += gain;
        if (players[i].state.cargo_energy > MAX_ENERGY_CAPACITY) players[i].state.cargo_energy = MAX_ENERGY_CAPACITY;
        SG_SPRINTF(msg, "Converted %d Composite into %" PRIu64 " Energy units. Efficiency: %.1f%%.", amount, gain, efficiency * 100);
    } else if (type == 8) { /* Dark-Matter -> Energy x25 */
        uint64_t gain = (uint64_t)(amount * YIELD_CONVERSION_DM * efficiency);
        players[i].state.cargo_energy += gain;
        if (players[i].state.cargo_energy > MAX_ENERGY_CAPACITY) players[i].state.cargo_energy = MAX_ENERGY_CAPACITY;
        SG_SPRINTF(msg, "Converted %d Dark-Matter into %" PRIu64 " Energy units. Efficiency: %.1f%%.", amount, gain, efficiency * 100);
    } else {
        send_server_msg(i, "COMPUTER", "Resource type not suitable for atomic conversion.");
        players[i].state.energy += COST_ACTION_HIGH; /* Refund */
        players[i].state.inventory[type] += amount;
        return;
    }

    send_server_msg(i, "ENGINEERING", msg);
}

void handle_load(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    /* 1. Hardware Requirement: Auxiliary (ID 9) */
    if (players[i].state.system_health[9] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "ENGINEERING", "TRANSFER FAILURE: Internal power bus non-functional.");
        return;
    }

    int type, amount; 
    if (sscanf(params, "%d %d", &type, &amount) == 2) {
        /* 2. Energy Cost: 25 units */
        if (players[i].state.energy < (uint64_t)(COST_ACTION_LOW * RATIO_WEAPON_POWER)) {
            send_server_msg(i, "COMPUTER", "Insufficient energy for internal transfer systems.");
            return;
        }
        players[i].state.energy -= (uint64_t)(COST_ACTION_LOW * RATIO_WEAPON_POWER);

        if (type == 1) { /* Load Energy */
            uint64_t load_amount = (uint64_t)amount;
            if (load_amount > players[i].state.cargo_energy) load_amount = players[i].state.cargo_energy; 
            uint64_t space = MAX_ENERGY_CAPACITY - players[i].state.energy;
            if (load_amount > space) load_amount = space;
            
            players[i].state.cargo_energy -= load_amount; 
            players[i].state.energy += load_amount; 
            
            char msg[128];
            SG_SPRINTF(msg, "Energy transfer complete. %" PRIu64 " units loaded into main reactor.", load_amount);
            send_server_msg(i, "ENGINEERING", msg); 
        }
        else if (type == 2) { /* Load Torpedoes */
            if (amount > players[i].state.cargo_torpedoes) amount = players[i].state.cargo_torpedoes; 
            int space = MAX_TORPEDO_CAPACITY - players[i].state.torpedoes;
            if (amount > space) amount = space;
            
            players[i].state.cargo_torpedoes -= amount; 
            players[i].state.torpedoes += amount; 
            
            char msg[128];
            SG_SPRINTF(msg, "Torpedoes loaded. %d warheads moved to active tubes.", amount);
            send_server_msg(i, "TACTICAL", msg); 
        } else {
            send_server_msg(i, "COMPUTER", "Invalid transfer type. Use 1:Energy or 2:Torpedoes.");
        }
    } else {
        send_server_msg(i, "COMPUTER", "Usage: load <Type> <Amount> (1: Energy, 2: Torpedoes)");
    }
}

