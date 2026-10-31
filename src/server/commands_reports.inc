/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * commands_reports.inc — command handlers: status reports and computations (rep, fix, sta, inv, dam, cal, ical, who).
 * Part of the command handlers, split by domain from commands_handlers.inc
 * (2026.10.31.15) without any code change; included, in this order, by
 * commands_handlers.inc into commands.c (one translation unit).
 */
void handle_rep(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    const char* sys_names[] = {"Hyperdrive", "Impulse", "Sensors", "Transp", "Ion Beams", "Torps", "Computer", "Life Support", "Shields", "Auxiliary"};
    int sid; 
    if(sscanf(params, " %d", &sid) == 1) {
        if (sid >= 0 && sid < 10) {
            /* 1. Support System Check: Req Computer (ID 6) OR Auxiliary (ID 9) */
            if (players[i].state.system_health[6] < THRESHOLD_SYS_CRITICAL && players[i].state.system_health[9] < THRESHOLD_SYS_CRITICAL && sid != 6 && sid != 9) {
                send_server_msg(i, "ENGINEERING", "REPAIR FAILURE: Both Computer and Auxiliary systems are offline. Manual coordination impossible.");
                return;
            }

            /* 2. Energy Cost Check */
            if (players[i].state.energy < COST_ACTION_EXTREME) {
                send_server_msg(i, "COMPUTER", "Insufficient energy to power automated repair drones.");
                return;
            }

            /* 3. Resource Check: 50 Neo-Ti, 10 Synaptics */
            if(players[i].state.inventory[2] >= COST_ACTION_MED && players[i].state.inventory[5] >= COST_ACTION_LOW) {
                players[i].state.inventory[2] -= COST_ACTION_MED; 
                players[i].state.inventory[5] -= COST_ACTION_LOW;
                players[i].state.energy -= COST_ACTION_EXTREME;
                players[i].state.system_health[sid] = (double)YIELD_HARVEST_MAX; 
                
                /* 4. Improved Feedback */
                char msg[128];
                SG_SPRINTF(msg, "System %s restored to 100%% integrity. [Neo-Ti -%d, Synaptics -%d]", sys_names[sid], COST_ACTION_MED, COST_ACTION_LOW);
                send_server_msg(i, "ENGINEERING", msg);
            } else {
                char err_msg[128];
                SG_SPRINTF(err_msg, "Insufficient materials (Req: %d Neo-Titanium, %d Synaptics Chips).", COST_ACTION_MED, COST_ACTION_LOW);
                send_server_msg(i, "ENGINEERING", err_msg);
            }
        } else {
            send_server_msg(i, "COMPUTER", "Invalid system ID. Use 'rep' to list systems.");
        }
    } else {
        /* List all systems with health status */
        char list[1024]; 
        SG_SPRINTF(list, CYAN "\n--- ENGINEERING: SHIP SYSTEMS DIRECTORY ---" RESET "\n");
        for(int s=0; s<10; s++) {
            char line[128];
            double hp = players[i].state.system_health[s];
            const char* col = (hp > THRESHOLD_SYS_STABLE) ? GREEN : (hp > THRESHOLD_SYS_DAMAGED) ? YELLOW : RED;
            SG_SPRINTF(line, WHITE "%d" RESET ": %-15s | STATUS: %s%.1f%%" RESET "\n", s, sys_names[s], col, hp);
            SG_STRCAT(list, line);
        }
        char usage[128];
        SG_SPRINTF(usage, YELLOW "\nUsage: rep <ID> (Costs: %d Energy, %d Neo-Titanium, %d Synaptics)\n" RESET, COST_ACTION_EXTREME, COST_ACTION_MED, COST_ACTION_LOW);
        SG_STRCAT(list, usage);
        send_server_msg(i, "COMPUTER", list);
    }
}

void handle_fix(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    /* 1. Hardware Requirement: Auxiliary (ID 9) */
    if (players[i].state.system_health[9] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "ENGINEERING", "FIELD REPAIR FAILURE: Welding arrays offline (Auxiliary damage).");
        return;
    }

    /* Check Hull Limit: Max 80% for field repairs */
    if (players[i].state.hull_integrity >= 80.0) {
        send_server_msg(i, "ENGINEERING", "Hull integrity at maximum field-repair capacity (80%). Starbase required for full overhaul.");
        return;
    }

    /* Check Resources: 50 Graphene (inv[4]), 20 Neo-Titanium (inv[2]) */
    if ((uint32_t)players[i].state.inventory[4] < (uint32_t)COST_ACTION_MED || (uint32_t)players[i].state.inventory[2] < (uint32_t)COST_MANEUVER_ADJUST) {
        char err_msg[128];
        SG_SPRINTF(err_msg, "Insufficient materials for hull repair (Req: %d Graphene, %d Neo-Titanium).", COST_ACTION_MED, COST_MANEUVER_ADJUST);
        send_server_msg(i, "COMPUTER", err_msg);
        return;
    }

    /* 2. Energy Cost Check */
    if (players[i].state.energy < COST_ACTION_EXTREME) {
        char err_msg[128];
        SG_SPRINTF(err_msg, "Insufficient energy for structural welding pulse (Req: %d).", COST_ACTION_EXTREME);
        send_server_msg(i, "COMPUTER", err_msg);
        return;
    }

    players[i].state.energy -= COST_ACTION_EXTREME;
    players[i].state.inventory[4] -= COST_ACTION_MED;
    players[i].state.inventory[2] -= COST_MANEUVER_ADJUST;

    /* 3. Efficiency scaling based on Auxiliary health: base 10% + up to 10% bonus */
    double efficiency = THRESHOLD_SYS_CRITICAL + (players[i].state.system_health[9] / (double)YIELD_HARVEST_MAX) * (double)QUADRANT_SIZE / 4.0;
    players[i].state.hull_integrity += efficiency;
    if (players[i].state.hull_integrity > (float)THRESHOLD_SYS_STABLE + 5.0f) players[i].state.hull_integrity = (float)THRESHOLD_SYS_STABLE + 5.0f;
    
    /* 4. Improved Feedback */
    char msg[128];
    SG_SPRINTF(msg, "Field repairs complete. Hull integrity restored by %.1f%% to %.1f%%. [Graphene -%d, Neo-Ti -%d]", efficiency, players[i].state.hull_integrity, COST_ACTION_MED, COST_MANEUVER_ADJUST);
    send_server_msg(i, "ENGINEERING", msg);
}

void handle_sta(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    /* 1. Hardware Requirement: Computer (ID 6) */
    if (players[i].state.system_health[6] < (THRESHOLD_SYS_CRITICAL / 2.0)) {
        send_server_msg(i, "COMPUTER", "DIAGNOSTICS FAILURE: Mainframe logic core non-responsive.");
        return;
    }

    /* 2. Energy Cost Check */
    if (players[i].state.energy < COST_ACTION_LOW) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for full systems diagnostic.");
        return;
    }
    players[i].state.energy -= COST_ACTION_LOW;

    const size_t b_cap = 4096;
    char *b = calloc(b_cap, sizeof(char));
    if (!b) return;
    
    double comp_h = players[i].state.system_health[6];
    /* 3. Data Scrambling Logic (< 30%) */
    bool scrambled = (comp_h < (double)PROB_MISFIRE_DEGRADED && (rand() % 100 > (int)comp_h));

    const char* c_names[] = {"Legacy Class", "Scout Class", "Heavy Cruiser", "Multi-Engine Cruiser", "Escort Class", "Explorer Class", "Flagship Class", "Science Vessel", "Carrier Class", "Tactical Cruiser", "Diplomatic Cruiser", "Research Vessel", "Frigate Class", "Vessel"};
    const char* class_name = (players[i].ship_class >= 0 && players[i].ship_class <= 13) ? c_names[players[i].ship_class] : "Unknown";
    
    snprintf(b, 4096, CYAN "\n.--- GDIS MAIN COMPUTER: STRATEGIC DIAGNOSTICS -----------------.\n" RESET);
    if (scrambled) sg_strcat_n(b, b_cap, RED " WARNING: COMPUTER CORE CORRUPTED. DIAGNOSTICS UNRELIABLE.\n" RESET);
    
    sg_appendf_n(b, b_cap, WHITE " COMMANDER: %-18s CLASS: %-15s\n STATUS:    %s\n CREW COMPLEMENT: %d\n" RESET, 
             players[i].name, class_name, 
             players[i].state.is_cloaked ? MAGENTA "[ CLOAKED ]" RESET : GREEN "[ ACTIVE ]" RESET, 
             scrambled ? (rand()%1000) : players[i].state.crew_count);

    sg_strcat_n(b, b_cap, BLUE "\n[ POSITION AND TELEMETRY ]\n" RESET);
    if (scrambled) {
        sg_strcat_n(b, b_cap, " QUADRANT: [?, ?, ?]  SECTOR: [??.??, ??.??, ??.??]\n");
    } else {
        sg_appendf_n(b, b_cap, " QUADRANT: [%d,%d,%d]  SECTOR: [%.2f, %.2f, %.2f]]\n", players[i].state.q1, players[i].state.q2, players[i].state.q3, players[i].state.s1, players[i].state.s2, players[i].state.s3);
    }
    sg_appendf_n(b, b_cap, " HEADING:  %03.0ff        MARK:   %+03.0f\n", players[i].state.van_h, players[i].state.van_m);

    sg_strcat_n(b, b_cap, BLUE "\n[ POWER AND REACTOR STATUS ]\n" RESET);
    double en_pct = ((double)players[i].state.energy / (double)MAX_ENERGY_CAPACITY) * (double)YIELD_HARVEST_MAX;
    sg_appendf_n(b, b_cap, " MAIN REACTOR: %" PRIu64 " / %" PRIu64 " (%.1f%%)\n ALLOCATION:   ENGINES: %.0f%%  SHIELDS: %.0f%%  WEAPONS: %.0f%%\n", players[i].state.energy, (uint64_t)MAX_ENERGY_CAPACITY, en_pct, players[i].state.power_dist[0]*100, players[i].state.power_dist[1]*100, players[i].state.power_dist[2]*100);

    sg_strcat_n(b, b_cap, BLUE "\n[ DEFENSIVE GRID AND ARMAMENTS ]\n" RESET);
    sg_appendf_n(b, b_cap, " SHIELDS: F:%-4d R:%-4d T:%-4d B:%-4d L:%-4d R:%-4d\n Torpedoes: %-2d  ION BEAM CHARGE: %.1f%%  LOCK: %d\n", players[i].state.shields[0], players[i].state.shields[1], players[i].state.shields[2], players[i].state.shields[3], players[i].state.shields[4], players[i].state.shields[5], players[i].state.torpedoes, players[i].state.ion_beam_charge, players[i].state.lock_target);

    sg_strcat_n(b, b_cap, BLUE "\n[ SYSTEMS INTEGRITY ]\n" RESET);
    double h_int = players[i].state.hull_integrity;
    const char* h_col = (h_int > 75) ? GREEN : (h_int > 25) ? YELLOW : RED;
    sg_appendf_n(b, b_cap, " PHYSICAL HULL: %s%.1f%%" RESET "\n", h_col, h_int);
    
    /* 4. Inclusion of all 10 systems (0-9) */
    const char* sys[] = {"Hyperdrive", "Impulse", "Sensors", "Transp", "Ion Beams", "Torps", "Computer", "Life Sup", "Shields", "Auxiliary"};
    for(int s=0; s<10; s++) { 
        double hp = players[i].state.system_health[s]; 
        const char* col = (hp > 75) ? GREEN : (hp > 25) ? YELLOW : RED; 
        sg_appendf_n(b, b_cap, " %-10s: %s%.1f%%" RESET " ", sys[s], col, hp); 
        if (s == 2 || s == 5 || s == 8) sg_strcat_n(b, b_cap, "\n"); 
    }
    sg_strcat_n(b, b_cap, CYAN "\n'-----------------------------------------------------------------'\n" RESET);
    
    send_server_msg(i, "COMPUTER", b);
    free(b);
}

void handle_inv(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    /* 1. Hardware Requirement: Computer (ID 6) */
    if (players[i].state.system_health[6] < (THRESHOLD_SYS_CRITICAL / 2.0)) {
        send_server_msg(i, "COMPUTER", "INVENTORY FAILURE: Cargo database inaccessible. Repair computer core.");
        return;
    }

    /* 2. Energy Cost Check */
    if (players[i].state.energy < (uint64_t)(COST_ACTION_LOW / 2)) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for manifest scan.");
        return;
    }
    players[i].state.energy -= (uint64_t)(COST_ACTION_LOW / 2);

    char b[1024]; 
    SG_SPRINTF(b, YELLOW "\n--- CARGO BAY MANIFEST (GDIS LOGISTICS) ---" RESET "\n");
    
    double comp_h = players[i].state.system_health[6];
    bool scrambled = (comp_h < (double)PROB_MISFIRE_DEGRADED && (rand() % 100 > (int)comp_h));

    if (scrambled) {
        SG_STRCAT(b, RED " WARNING: LOGISTICS CORE CORRUPTED. DATA UNRELIABLE.\n" RESET);
    }

    const char* r[] = {"-", "Aetherium", "Neo-Titanium", "Void-Essence", "Graphene", "Synaptics", "Nebular Gas", "Composite", "Dark-Matter"};
    for (int j = 1; j <= 8; j++) {
        char it[128];
        if (scrambled && (rand() % 100 > 50)) {
            SG_SPRINTF(it, " %-18s: ??? units\n", r[j]);
        } else {
            SG_SPRINTF(it, " %-18s: %-5d units\n", r[j], players[i].state.inventory[j]);
        }
        SG_STRCAT(b, it);
    }

    char extra[256];
    if (scrambled) {
        SG_SPRINTF(extra, BLUE " Plasma Reserves:   ??????\n CARGO Torpedoes:   ???\n PRISON UNIT:       ??\n" RESET);
    } else {
        SG_SPRINTF(extra, BLUE " Plasma Reserves:   %-12" PRIu64 "\n CARGO Torpedoes:   %-3d\n PRISON UNIT:       %-3d\n" RESET, 
                players[i].state.cargo_energy, players[i].state.cargo_torpedoes, players[i].state.prison_unit);
    }
    SG_STRCAT(b, extra);
    SG_STRCAT(b, YELLOW "--------------------------------------------" RESET);
    
    send_server_msg(i, "LOGISTICS", b);
}

void handle_dam(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    /* 1. Hardware Requirement: Computer (ID 6) */
    if (players[i].state.system_health[6] < (THRESHOLD_SYS_CRITICAL / 2.0)) {
        send_server_msg(i, "COMPUTER", "DIAGNOSTICS FAILURE: Damage analysis core non-functional.");
        return;
    }

    /* 2. Energy Cost Check */
    if (players[i].state.energy < (uint64_t)(COST_ACTION_LOW / 2)) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for damage report scan.");
        return;
    }
    players[i].state.energy -= (uint64_t)(COST_ACTION_LOW / 2);

    char b[1024]; 
    SG_SPRINTF(b, RED "\n--- GDIS DAMAGE CONTROL: SHIP INTEGRITY REPORT ---" RESET "\n");
    
    double comp_h = players[i].state.system_health[6];
    bool scrambled = (comp_h < THRESHOLD_SYS_DAMAGED && (rand() % 100 > (int)comp_h));

    if (scrambled) {
        SG_STRCAT(b, YELLOW " WARNING: SENSOR LINK UNSTABLE. DATA PARITY ERROR.\n" RESET);
    }

    double h_int = players[i].state.hull_integrity;
    const char* h_col = (h_int > THRESHOLD_SYS_STABLE) ? GREEN : (h_int > THRESHOLD_SYS_DAMAGED) ? YELLOW : RED;
    if (scrambled) {
        SG_STRCAT(b, " PHYSICAL HULL: [ERROR]%\n");
    } else {
        SG_APPENDF(b, " PHYSICAL HULL INTEGRITY: %s%.1f%%" RESET "\n", h_col, h_int);
    }

    SG_STRCAT(b, BLUE " SYSTEM INTEGRITY STATUS:\n" RESET);
    /* 3. Inclusion of all 10 systems (0-9) */
    const char* sys[] = {"Hyperdrive", "Impulse", "Sensors", "Transp", "Ion Beams", "Torps", "Computer", "Life Sup", "Shields", "Auxiliary"};
    for (int s = 0; s < 10; s++) {
        char sbuf[128];
        double hp = players[i].state.system_health[s];
        const char* col = (hp > THRESHOLD_SYS_STABLE) ? GREEN : (hp > THRESHOLD_SYS_DAMAGED) ? YELLOW : RED;
        
        if (scrambled && (rand() % 100 > 60)) {
            SG_SPRINTF(sbuf, "  %-15s: [??.?%%]\n", sys[s]);
        } else {
            SG_SPRINTF(sbuf, "  %-15s: %s%.1f%%" RESET "\n", sys[s], col, hp);
        }
        SG_STRCAT(b, sbuf);
    }
    SG_STRCAT(b, RED "--------------------------------------------------" RESET);
    
    send_server_msg(i, "ENGINEERING", b);
}

void handle_cal(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    if (players[i].state.system_health[6] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "COMPUTER", "CALCULATION FAILURE: Navigation core non-functional.");
        return;
    }
    if (players[i].state.energy < (uint64_t)COST_ACTION_MED / 2) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for navigational computation.");
        return;
    }

    int qx, qy, qz; 
    double sx = (double)QUADRANT_SIZE / 8.0, sy = (double)QUADRANT_SIZE / 8.0, sz = (double)QUADRANT_SIZE / 8.0;
    int args = sscanf(params, "%d %d %d %lf %lf %lf", &qx, &qy, &qz, &sx, &sy, &sz);
    
    if (args >= 3) {
        if (!IS_Q_VALID(qx, qy, qz)) {
            send_server_msg(i, "COMPUTER", "Invalid quadrant coordinates.");
            return;
        }

        players[i].state.energy -= (uint64_t)COST_ACTION_MED / 2;
        double comp_h = players[i].state.system_health[6];
        bool scrambled = (comp_h < THRESHOLD_SYS_DEGRADED && (rand() % 100 > (int)comp_h));

        double target_gx = (qx - 1) * QUADRANT_SIZE + sx;
        double target_gy = (qy - 1) * QUADRANT_SIZE + sy;
        double target_gz = (qz - 1) * QUADRANT_SIZE + sz;
        
        double dx = target_gx - players[i].gx;
        double dy = target_gy - players[i].gy;
        double dz = target_gz - players[i].gz;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        
        if (d < 0.001) {
            send_server_msg(i, "COMPUTER", "Target matches current position.");
            return;
        }
        
        double h = atan2(dx, -dy) * 180.0 / M_PI; if (h < 0) h += 360;
        double m = asin(dz / d) * 180.0 / M_PI;
        double q_dist = d / QUADRANT_SIZE;
        
        char buf[2048]; 
        if (scrambled) {
            SG_SPRINTF(buf, "\n" RED ".--- NAVIGATIONAL COMPUTATION: DATA CORRUPTED ---." RESET "\n"
                         " DESTINATION:  " WHITE "Q[%d,%d,%d] Sector [?, ?, ?]" RESET "\n"
                         " BEARING:      " RED "ERROR - Logic Parity Failure" RESET "\n"
                         " DISTANCE:     " YELLOW "??.?? Quadrants" RESET "\n\n"
                         " WARNING: Computer integrity low. Results unreliable.\n",
                         qx, qy, qz);
        } else {
            char table[2048];
            SG_SPRINTF(table, "\n" CYAN ".--- NAVIGATIONAL COMPUTATION: PINPOINT PRECISION --." RESET "\n"
                         " DESTINATION:  " WHITE "Q[%d,%d,%d] Sector [%.2f,%.2f,%.2f]" RESET "\n"
                         " BEARING:      " GREEN "Heading %.2f, Mark %+.2f" RESET "\n"
                         " DISTANCE:     " YELLOW "%.2f Quadrants" RESET "\n"
                         " STATUS:       " B_WHITE "Constant Speed Mode Active" RESET "\n\n"
                         WHITE " FACTOR   TIME (1Q)   TIME (TOTAL)   NOTES" RESET "\n"
                         " ------   ---------   ------------   -----------------\n",
                         qx, qy, qz, sx, sy, sz, h, m, q_dist);

            for (double f = 0.1; f <= 10.0; ) {
                double cf = (f > 9.999) ? 9.999 : f;
                
                /* Real Time Calculation (60Hz Synchronized): 
                   Distance / Speed (cf / COEFF_HYPER_DIVISOR) = Ticks per Quad
                   Ticks / GAME_TICK_RATE = Seconds per Quad */
                double t_1q = (QUADRANT_SIZE * COEFF_HYPER_DIVISOR / cf) / (double)GAME_TICK_RATE;
                double t_total = (d * COEFF_HYPER_DIVISOR / cf) / (double)GAME_TICK_RATE;
                
                const char* note = "";
                if (cf < 0.2) note = "Minimum Jump";
                else if (cf > 2.9 && cf < 3.2) note = "Economic";
                else if (cf > 5.9 && cf < 6.2) note = "Standard Cruise";
                else if (cf > 7.9 && cf < 8.2) note = "High Pursuit";
                else if (cf > 9.8) note = "MAX VELOCITY";

                char row[128];
                /* Factor rounded to 3 decimals; the last row is 9.999 */
                SG_SPRINTF(row, " %5.3f    %6.2fs      %6.2fs      %s\n", cf, t_1q, t_total, note);
                SG_STRCAT(table, row);
                
                if (f >= 9.999) break;
                f += 0.5;
                if (f > 9.999) f = 9.999;
            }

            SG_APPENDF(table, "-------------------------------------------------\n"
                         " Use 'nav %.2f %.2f %.2f [Factor]'", h, m, q_dist);
            snprintf(buf, sizeof(buf), "%s", table);
        }
        send_server_msg(i, "COMPUTER", buf);
    } else {
        send_server_msg(i, "COMPUTER", "Usage: cal <QX> <QY> <QZ> [SX SY SZ]");
    }
}

void handle_ical(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    /* 1. Hardware Requirement: Computer (ID 6) */
    if (players[i].state.system_health[6] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "COMPUTER", "CALCULATION FAILURE: Navigation core offline.");
        return;
    }

    /* 2. Energy Cost */
    if (players[i].state.energy < COST_ACTION_LOW) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for impulse computation.");
        return;
    }

    double tx, ty, tz;
    if (sscanf(params, "%lf %lf %lf", &tx, &ty, &tz) == 3) {
        if (tx < 0 || tx > QUADRANT_SIZE || ty < 0 || ty > QUADRANT_SIZE || tz < 0 || tz > QUADRANT_SIZE) {
            char err_msg[128];
            SG_SPRINTF(err_msg, "Coordinates out of sector range (0.0-%.1f).", QUADRANT_SIZE);
            send_server_msg(i, "COMPUTER", err_msg);
            return;
        }
        players[i].state.energy -= COST_ACTION_LOW;
        double comp_h = players[i].state.system_health[6];
        /* 3. Data Reliability logic: Scrambling if integrity < 50% */
        bool scrambled = (comp_h < THRESHOLD_SYS_DEGRADED && (rand() % 100 > (int)comp_h));

        double s1 = players[i].state.s1;
        double s2 = players[i].state.s2;
        double s3 = players[i].state.s3;

        double dx = tx - s1;
        double dy = ty - s2;
        double dz = tz - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);

        if (d < 0.001) {
            send_server_msg(i, "COMPUTER", "Target matches current sector position.");
            return;
        }

        double h = atan2(dx, -dy) * 180.0 / M_PI; if (h < 0) h += 360;
        double m = asin(dz / d) * 180.0 / M_PI;

        /* 4. ETA Calculation based on ACTUAL logic in src/server/logic.c */
        double imp_h = players[i].state.system_health[1];
        if (imp_h < 1.0) imp_h = 1.0;
        
        double engine_mult = RATIO_BASE_POWER + (players[i].state.power_dist[0] * RATIO_ENGINE_POWER);
        double speed_per_tick = SPEED_IMPULSE_TICK_MAX * engine_mult * (imp_h / (double)YIELD_HARVEST_MAX);
        /* eta = distance / (speed_per_second) -> speed_per_second = speed_per_tick * GAME_TICK_RATE */
        double eta_sec = d / (speed_per_tick * (double)GAME_TICK_RATE);

        char buf[512];
        if (scrambled) {
            SG_SPRINTF(buf, "\n" RED "--- IMPULSE NAVIGATION: DATA CORRUPTED ---" RESET "\n"
                         " DESTINATION: [%.2f, %.2f, %.2f]\n"
                         " BEARING:     ???.? / %+.1f\n"
                         " ETA:         ??.? sec\n"
                         " WARNING: Logic core parity error. Results unreliable.\n", 
                         tx, ty, tz, (rand()%180-90)*1.0);
        } else {
            SG_SPRINTF(buf, "\n" CYAN "--- IMPULSE NAVIGATION COMPUTATION ---" RESET "\n"
                         " DESTINATION: [%.2f, %.2f, %.2f]\n"
                         " BEARING:     %06.2f / %+06.2f\n"
                         " DISTANCE:    %.2f units\n"
                         " ETA:         %.1f sec (at Full Impulse)\n"
                         " COMMAND:     " WHITE "imp %.2f %.2f %.1f %.2f" RESET "\n",
                         tx, ty, tz, h, m, d, eta_sec, h, m, 100.0, d);
        }
        send_server_msg(i, "COMPUTER", buf);
    } else {
        send_server_msg(i, "COMPUTER", "Usage: ical <X> <Y> <Z>");
    }
}

void handle_who(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    /* 1. Hardware Requirement: Computer (ID 6) */
    if (players[i].state.system_health[6] < (THRESHOLD_SYS_CRITICAL / 2.0)) {
        send_server_msg(i, "COMPUTER", "REGISTRY FAILURE: Deep space link offline. Repair computer core.");
        return;
    }

    /* 2. Energy Cost Check */
    if (players[i].state.energy < COST_ACTION_LOW) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for registry synchronization.");
        return;
    }
    players[i].state.energy -= COST_ACTION_LOW;

    char b[2048]; 
    SG_SPRINTF(b, WHITE "\n--- ACTIVE CAPTAINS REGISTRY (GDIS CENTRAL) ---" RESET "\n"
               " ID   NAME               FACTION            POSITION\n"
               "-------------------------------------------------------\n");

    double comp_h = players[i].state.system_health[6];
    int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;

    for (int j = 0; j < MAX_CLIENTS; j++) {
        if (players[j].active) {
            char line[256];
            int tq1 = players[j].state.q1, tq2 = players[j].state.q2, tq3 = players[j].state.q3;
            double q_dist = sqrt(pow(tq1-pq1, 2) + pow(tq2-pq2, 2) + pow(tq3-pq3, 2));
            
            /* 3. Scrambling for distant targets if computer is damaged (< 40%) */
            bool scrambled = (comp_h < 40.0 && q_dist > (comp_h / 4.0));

            if (scrambled) {
                SG_SPRINTF(line, " [%2d] %-18s %-18s [?, ?, ?]\n", j + 1, players[j].name, "UNKNOWN");
            } else {
                SG_SPRINTF(line, " [%2d] %-18s %-18s [%d,%d,%d]\n", 
                        j + 1, players[j].name, get_species_name(players[j].faction), tq1, tq2, tq3);
            }
            SG_STRCAT(b, line);
        }
    }
    SG_STRCAT(b, "-------------------------------------------------------\n");
    send_server_msg(i, "COMPUTER", b);
}

