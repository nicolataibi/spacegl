/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * commands_special.inc — command handlers: jumps, view toggles, auxiliary and debug commands (jum ... dbgnpc).
 * Part of the command handlers, split by domain from commands_handlers.inc
 * (2026.10.31.15) without any code change; included, in this order, by
 * commands_handlers.inc into commands.c (one translation unit).
 */
void handle_jum(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    int qx, qy, qz;
    int type = 2;
    bool valid = false;

    if (sg_scan_nums(params, "iiii", &type, &qx, &qy, &qz) == 4) {
        if (type < 1 || type > 2) {
            send_server_msg(i, "COMPUTER", "Invalid jump sequence type (1 or 2).");
            return;
        }
        valid = true;
    } else if (sg_scan_nums(params, "iii", &qx, &qy, &qz) == 3) {
        type = 2;
        valid = true;
    }

    if (valid) {
        if (!IS_Q_VALID(qx, qy, qz)) {
            send_server_msg(i, "COMPUTER", "Invalid quadrant coordinates.");
            return;
        }

        /* 1. Integrity requirements (Hyperdrive ID 0, Sensors ID 2) */
        if (players[i].state.system_health[0] < THRESHOLD_SYS_DEGRADED || players[i].state.system_health[2] < THRESHOLD_SYS_DEGRADED) {
            send_server_msg(i, "SCIENCE", "UNABLE TO STABILIZE WORMHOLE: Hyperdrive and Sensors must be above 50% integrity.");
            return;
        }

        /* 2. Malfunction risk (Hyperdrive 50-75% health) */
        if (players[i].state.system_health[0] < THRESHOLD_SYS_STABLE && (rand() % 100 > (int)players[i].state.system_health[0])) {
            send_server_msg(i, "ENGINEERING", "Wormhole collapse during initialization! Resources consumed.");
            players[i].state.energy -= (COST_WORMHOLE_INIT / 2);
            return;
        }

        if (players[i].state.energy < COST_WORMHOLE_INIT || players[i].state.inventory[1] < 1) {
             char err_msg[128];
             SG_SPRINTF(err_msg, "Insufficient resources for Jump (Req: %d Energy, 1 Aetherium).", COST_WORMHOLE_INIT);
             send_server_msg(i, "ENGINEERING", err_msg);
             return;
        }

        players[i].jump_type = type;
        players[i].state.energy -= COST_WORMHOLE_INIT;
        players[i].state.inventory[1] -= 1;

        /* 3. Stress damage (1-3% hull) */
        double stress = 1.0 + (rand() % 200) / (double)YIELD_HARVEST_MAX;
        apply_jump_stress_damage(i, stress);

        /* 4. Positional Uncertainty based on sensor damage */
        double off_x = 0, off_y = 0, off_z = 0;
        if (players[i].state.system_health[2] < (double)YIELD_HARVEST_MAX) {
            double noise = ((double)YIELD_HARVEST_MAX - players[i].state.system_health[2]) / 20.0; /* Up to 2.5 units off */
            off_x = ((rand() % 2000 - 1000) / 1000.0) * noise;
            off_y = ((rand() % 2000 - 1000) / 1000.0) * noise;
            off_z = ((rand() % 2000 - 1000) / 1000.0) * noise;
        }

        /* Random arrival point within the destination quadrant (40x40x40) */
        double rx = (rand() % (int)(QUADRANT_SIZE * (double)YIELD_HARVEST_MAX)) / (double)YIELD_HARVEST_MAX;
        double ry = (rand() % (int)(QUADRANT_SIZE * (double)YIELD_HARVEST_MAX)) / (double)YIELD_HARVEST_MAX;
        double rz = (rand() % (int)(QUADRANT_SIZE * (double)YIELD_HARVEST_MAX)) / (double)YIELD_HARVEST_MAX;

        players[i].target_gx = (qx - 1) * QUADRANT_SIZE + rx + off_x;
        players[i].target_gy = (qy - 1) * QUADRANT_SIZE + ry + off_y;
        players[i].target_gz = (qz - 1) * QUADRANT_SIZE + rz + off_z;
        
        /* Calculate entry wormhole position: 4 units in front of ship (Galactic Absolute) */
        double r_h = players[i].state.van_h * M_PI / 180.0;
        double r_m = players[i].state.van_m * M_PI / 180.0;
        double f_dx = sin(r_h) * cos(r_m);
        double f_dy = -cos(r_h) * cos(r_m);
        double f_dz = sin(r_m);
        players[i].wx = players[i].gx + 4.0 * f_dx;
        players[i].wy = players[i].gy + 4.0 * f_dy;
        players[i].wz = players[i].gz + 4.0 * f_dz;

        players[i].nav_state = NAV_STATE_WORMHOLE;
        players[i].nav_timer = TIMER_WORMHOLE_SEQ; 
        
        /* Server-side logging of the jump initiation */
        time_t now_jum = time(NULL);
        struct tm *t_jum = SG_LOCALTIME(&now_jum);
        char time_jum[64];
        strftime(time_jum, sizeof(time_jum), "%Y-%m-%d %H:%M:%S", t_jum);
        slog("\033[1;36m[WORMHOLE]\033[0m   Captain \033[1;37m%-15s\033[0m initiating jump to Q%d-%d-%d.   [\033[1;33m%s\033[0m]\n", 
               players[i].name, qx, qy, qz, time_jum);

        send_server_msg(i, "HELMSMAN", "Initiating trans-quadrant jump. Structural stress detected.");
    } else send_server_msg(i, "COMPUTER", "Usage: jum [type] <Q1> <Q2> <Q3> (type 1:legacy, 2:current)");
}

void handle_psy(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    /* 1. Hardware Requirement: Computer (ID 6) >= 20% */
    if (players[i].state.system_health[6] < 20.0) {
        send_server_msg(i, "COMPUTER", "PSY-OPS FAILURE: Logic core damaged. Cannot synthesize credible threat.");
        return;
    }

    /* 4. Resource Check */
    if (players[i].state.anti_matter_count <= 0) {
        send_server_msg(i, "COMPUTER", "No Anti-Matter devices available. Bluff impossible.");
        return;
    }

    /* 2. Energy Cost: 500 units */
    if (players[i].state.energy < 500) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for high-power broadcast burst.");
        return;
    }

    players[i].state.energy -= 500;
    players[i].state.anti_matter_count--;
    send_server_msg(i, "COMMANDER", "Broadcasting Anti-Matter threat on all frequencies...");

    int q1 = players[i].state.q1, q2 = players[i].state.q2, q3 = players[i].state.q3;
    if (!IS_Q_VALID(q1, q2, q3)) return;
    QuadrantIndex *lq = &spatial_index[q1][q2][q3];

    if (rand() % 100 < PROB_BOARD_SUCCESS_BASE) {
        /* 3. Success: NPCs flee */
        for (int n = 0; n < lq->npc_count; n++) {
            lq->npcs[n]->ai_state = AI_STATE_FLEE;
            lq->npcs[n]->energy += (uint64_t)COST_HYPERDRIVE_INIT; /* Panic boost */
        }
        send_server_msg(i, "SCIENCE", "Bluff successful! Hostile vessels are breaking formation and retreating.");
    } else {
        /* 3. Failure: NPCs become aggressive */
        for (int n = 0; n < lq->npc_count; n++) {
            lq->npcs[n]->ai_state = AI_STATE_CHASE;
        }
        send_server_msg(i, "TACTICAL", "Bluff failed! The enemy has detected the deception and is closing in!");
    }
}

void handle_hull(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    /* 1. Hardware Requirement: Auxiliary (ID 9) */
    if (players[i].state.system_health[9] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "ENGINEERING", "PLATING FAILURE: Hull integration systems offline.");
        return;
    }

    /* 3. Plating Limit: 5000 units */
    if (players[i].state.composite_plating >= COST_BOARDING_INIT) {
        char err_msg[128];
        SG_SPRINTF(err_msg, "Hull plating is already at maximum structural capacity (%d).", COST_BOARDING_INIT);
        send_server_msg(i, "ENGINEERING", err_msg);
        return;
    }

    /* Check Resources: 100 Composite (inventory[7]) */
    if ((uint32_t)players[i].state.inventory[7] < (uint32_t)COST_ACTION_HIGH) {
        char err_msg[128];
        SG_SPRINTF(err_msg, "Insufficient Composite materials for reinforcement (Req: %d).", COST_ACTION_HIGH);
        send_server_msg(i, "COMPUTER", err_msg);
        return;
    }

    /* 2. Energy Cost Check: 1000 units */
    if (players[i].state.energy < COST_PROBE_LAUNCH) {
        char err_msg[128];
        SG_SPRINTF(err_msg, "Insufficient energy for hull reinforcement process (Req: %d).", COST_PROBE_LAUNCH);
        send_server_msg(i, "COMPUTER", err_msg);
        return;
    }

    players[i].state.energy -= COST_PROBE_LAUNCH;
    players[i].state.inventory[7] -= COST_ACTION_HIGH;
    players[i].state.composite_plating += COST_ACTION_EXTREME;
    if (players[i].state.composite_plating > COST_BOARDING_INIT) players[i].state.composite_plating = COST_BOARDING_INIT;
    
    /* 4. Improved Feedback */
    char msg[128];
    SG_SPRINTF(msg, "Hull reinforced with Composite plating. Current plating: %d units.", players[i].state.composite_plating);
    send_server_msg(i, "ENGINEERING", msg);
}

void handle_supernova(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    send_server_msg(i, "ADMIN", "SUPERNOVA INITIATED.");
}

void handle_axs(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    if (players[i].state.system_health[6] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "COMPUTER", "HUD FAILURE: Augmented Reality core offline.");
        return;
    }
    if (players[i].state.energy < COST_ACTION_LOW) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for AR HUD toggle.");
        return;
    }
    players[i].state.energy -= COST_ACTION_LOW;
    players[i].state.show_axes = !players[i].state.show_axes;
    send_server_msg(i, "COMPUTER", players[i].state.show_axes ? "AR Tactical Compass ENABLED." : "AR Tactical Compass DISABLED.");
}

void handle_grd(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    if (players[i].state.system_health[6] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "COMPUTER", "HUD FAILURE: Grid projection systems non-functional.");
        return;
    }
    if (players[i].state.energy < COST_ACTION_LOW) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for Tactical Grid toggle.");
        return;
    }
    players[i].state.energy -= COST_ACTION_LOW;
    players[i].state.show_grid = !players[i].state.show_grid;
    send_server_msg(i, "COMPUTER", players[i].state.show_grid ? "Tactical Grid ENABLED." : "Tactical Grid DISABLED.");
}

void handle_bridge(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    if (players[i].state.system_health[6] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "COMPUTER", "VISUAL FAILURE: Bridge camera link corrupted.");
        return;
    }
    if (players[i].state.energy < COST_ACTION_LOW) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for bridge camera realignment.");
        return;
    }
    
    int current = players[i].state.show_bridge;
    int is_bottom = (current >= 11);
    int new_val = current;

    const char *p = params;
    while (*p && (*p == ' ' || *p == '\t')) p++;

    if (*p == '\0') {
        /* Default toggle */
        new_val = (current == 0) ? 1 : 0;
        send_server_msg(i, "COMPUTER", new_val ? "Bridge View: ON." : "Bridge View: OFF.");
    } else if (strstr(p, "off")) {
        new_val = 0;
        send_server_msg(i, "COMPUTER", "Bridge View: OFF.");
    } else if (strstr(p, "top") || strstr(p, "on")) {
        new_val = 1;
        send_server_msg(i, "COMPUTER", "Bridge View: TOP FORWARD.");
    } else if (strstr(p, "bottom")) {
        new_val = 11;
        send_server_msg(i, "COMPUTER", "Bridge View: BOTTOM FORWARD.");
    } else if (strstr(p, "left")) {
        new_val = (is_bottom ? 12 : 2);
        send_server_msg(i, "COMPUTER", is_bottom ? "Bridge View: BOTTOM LEFT." : "Bridge View: TOP LEFT.");
    } else if (strstr(p, "right")) {
        new_val = (is_bottom ? 13 : 3);
        send_server_msg(i, "COMPUTER", is_bottom ? "Bridge View: BOTTOM RIGHT." : "Bridge View: TOP RIGHT.");
    } else if (strstr(p, "up")) {
        new_val = (is_bottom ? 14 : 4);
        send_server_msg(i, "COMPUTER", is_bottom ? "Bridge View: BOTTOM UP." : "Bridge View: TOP UP.");
    } else if (strstr(p, "down")) {
        new_val = (is_bottom ? 15 : 5);
        send_server_msg(i, "COMPUTER", is_bottom ? "Bridge View: BOTTOM DOWN." : "Bridge View: TOP DOWN.");
    } else if (strstr(p, "rear")) {
        new_val = (is_bottom ? 16 : 6);
        send_server_msg(i, "COMPUTER", is_bottom ? "Bridge View: BOTTOM REAR." : "Bridge View: TOP REAR.");
    }

    if (new_val != current) {
        players[i].state.energy -= COST_ACTION_LOW;
        players[i].state.show_bridge = (uint8_t)new_val;   /* 0..16 */
    }
}

/* ------------------------------------------------------------------ */
/* handle_pilot: engage/disengage cockpit pilot mode (show_bridge=20)  */
/*                                                                     */
/* "pilot"      — toggle on/off                                        */
/* "pilot on"   — engage                                               */
/* "pilot off"  — disengage                                            */
/*                                                                     */
/* In pilot mode the Vulkan viewer drives the ship via the flight      */
/* stick (T.16000M FCS): throttle=speed, stick=heading/mark,           */
/* twist=roll, hat=retro-thruster, buttons=fire/lock/power.            */
/* The server only sees ordinary imp/pha/tor/etc. commands.            */
/* show_bridge value 20 is reserved for pilot mode; values 1-16 cover  */
/* the existing bridge sub-views.                                      */
/* ------------------------------------------------------------------ */
void handle_pilot(int i, const char *params, bool *should_disconnect) {
    (void)should_disconnect;
    if (players[i].state.system_health[6] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "COMPUTER",
                        "HELM FAILURE: Flight control interface corrupted.");
        return;
    }
    if (players[i].state.energy < COST_ACTION_LOW) {
        send_server_msg(i, "COMPUTER",
                        "Insufficient energy to engage pilot interface.");
        return;
    }

    int current = players[i].state.show_bridge;
    int new_val;

    const char *p = params;
    while (*p && (*p == ' ' || *p == '\t')) p++;

    if (*p == '\0') {
        /* bare "pilot": toggle */
        new_val = (current == 20) ? 0 : 20;
    } else if (strstr(p, "off")) {
        new_val = 0;
    } else {
        /* "pilot on" or any other arg: engage */
        new_val = 20;
    }

    if (new_val != current) {
        players[i].state.energy -= COST_ACTION_LOW;
        players[i].state.show_bridge = (uint8_t)new_val;   /* 0..16 */
        if (new_val == 20)
            send_server_msg(i, "HELMSMAN",
                            "PILOT MODE: ENGAGED. Flight stick active.");
        else
            send_server_msg(i, "HELMSMAN",
                            "PILOT MODE: DISENGAGED. Returning to tactical.");
    }
}

void handle_map(int i, const char *params, bool *should_disconnect) {

    (void)params; (void)should_disconnect;
    if (players[i].state.system_health[6] < (THRESHOLD_SYS_CRITICAL + 5.0)) {
        send_server_msg(i, "COMPUTER", "CARTOGRAPHY FAILURE: Spatial projection mainframe damaged.");
        return;
    }
    if (players[i].state.energy < COST_ACTION_MED) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for real-time map rendering.");
        return;
    }

    const char *p = params;
    while (*p && (*p == ' ' || *p == '\t')) p++;

    if (*p == '\0') {
        /* Generic Toggle */
        players[i].state.show_map = !players[i].state.show_map;
        players[i].state.map_filter = 0;
        players[i].state.energy -= COST_ACTION_MED;
        send_server_msg(i, "COMPUTER", players[i].state.show_map ? "Galaxy Map: ENABLED (Full Scan)." : "Galaxy Map: DISABLED.");
    } else {
        /* Filtered Display - use strncmp for robustness against trailing spaces */
        int filter = 0;
        if (strncmp(p, "st", 2) == 0) filter = 1;
        else if (strncmp(p, "pl", 2) == 0) filter = 2;
        else if (strncmp(p, "bs", 2) == 0) filter = 3;
        else if (strncmp(p, "en", 2) == 0) filter = 4;
        else if (strncmp(p, "bh", 2) == 0) filter = 5;
        else if (strncmp(p, "ne", 2) == 0) filter = 6;
        else if (strncmp(p, "pu", 2) == 0) filter = 7;
        else if (strncmp(p, "is", 2) == 0) filter = 8;
        else if (strncmp(p, "co", 2) == 0) filter = 9;
        else if (strncmp(p, "as", 2) == 0) filter = 10;
        else if (strncmp(p, "de", 2) == 0) filter = 11;
        else if (strncmp(p, "mi", 2) == 0) filter = 12;
        else if (strncmp(p, "bu", 2) == 0) filter = 13;
        else if (strncmp(p, "pf", 2) == 0) filter = 14;
        else if (strncmp(p, "ri", 2) == 0) filter = 15;
        else if (strncmp(p, "mo", 2) == 0) filter = 16;
        else if (strncmp(p, "qu", 2) == 0) filter = 17;

        if (filter > 0) {
            players[i].state.show_map = 1;
            players[i].state.map_filter = (uint8_t)filter;   /* 1..17 */
            players[i].state.energy -= COST_ACTION_MED;
            char msg[128];
            char filter_name[3]; strncpy(filter_name, p, 2); filter_name[2] = '\0';
            SG_SPRINTF(msg, "Galaxy Map Filter: %s ACTIVE.", filter_name);
            send_server_msg(i, "COMPUTER", msg);
        } else {
            send_server_msg(i, "COMPUTER", "Invalid map filter. Use: st,pl,bs,en,bh,ne,pu,is,co,as,de,mi,bu,pf,ri,mo,qu");
        }
    }
}

void handle_und(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    if (!players[i].is_docked) {
        send_server_msg(i, "COMPUTER", "Vessel is already in open space.");
        return;
    }

    /* 1. Hardware Requirement: Auxiliary (ID 9) */
    if (players[i].state.system_health[9] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "ENGINEERING", "UNDOCKING FAILURE: Docking clamp release mechanism non-functional.");
        return;
    }

    /* 2. Energy Cost Check */
    if (players[i].state.energy < COST_ACTION_MED) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for clamp retraction pulse.");
        return;
    }

    players[i].state.energy -= COST_ACTION_MED;
    players[i].is_docked = 0;

    /* Find current base for message sender name */
    const char *sender = "STARBASE";
    for(int b=0; b<MAX_BASES; b++) {
        if(bases[b].active && bases[b].q1==players[i].state.q1 && bases[b].q2==players[i].state.q2 && bases[b].q3==players[i].state.q3) {
            double d=sqrt(pow(bases[b].x-players[i].state.s1,2)+pow(bases[b].y-players[i].state.s2,2)+pow(bases[b].z-players[i].state.s3,2)); 
            if(d <= (DIST_DOCKING_MAX + (double)DIST_EPSILON)) { sender = get_species_name(bases[b].faction); break; } 
        }
    }
    send_server_msg(i, sender, "Docking clamps retracted. Vessel is clear. Engines online.");
}

void handle_aux(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    const char *p_ptr = params;
    while(*p_ptr && (*p_ptr == ' ' || *p_ptr == '\t')) p_ptr++;
    
    /* 1. Hardware Requirement: Auxiliary (ID 9) */
    if (players[i].state.system_health[9] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "COMPUTER", "AUXILIARY FAILURE: Backup systems non-functional.");
        return;
    }

    if(*p_ptr == '\0' || strncmp(p_ptr, "status", 6) == 0) {
        if (players[i].state.energy < COST_ACTION_LOW) { send_server_msg(i, "COMPUTER", "Insufficient energy for status diagnostic."); return; }
        players[i].state.energy -= COST_ACTION_LOW;
        char buf[1024];
        SG_SPRINTF(buf, "\n" CYAN ".--- AUXILIARY SYSTEMS: SENSOR PROBE NETWORK ---." RESET "\n"
                     " SLOT  STATUS       LOCATION        ETA/UPTIME\n"
                     "-------------------------------------------------\n");
        for(int p=0; p<3; p++) {
            char line[256];
            if(players[i].state.probes[p].active) {
                const char* st = (players[i].state.probes[p].status == 0) ? YELLOW "EN ROUTE" RESET : GREEN "ACTIVE  " RESET;
                SG_SPRINTF(line, "  %d    %s    Q[%d,%d,%d]    %.1fs\n", p+1, st, 
                        players[i].state.probes[p].q1, players[i].state.probes[p].q2, players[i].state.probes[p].q3,
                        players[i].state.probes[p].eta);
            } else {
                SG_SPRINTF(line, "  %d    " WHITE "AVAILABLE" RESET "  ---             ---\n", p+1);
            }
            SG_STRCAT(buf, line);
        }
        SG_STRCAT(buf, "-------------------------------------------------\n"
                    " Usage: aux probe <Q1> <Q2> <Q3> | aux report <1-3>\n"
                    "        aux recover <1-3> | aux jettison");
        send_server_msg(i, "SCIENCE", buf);
    } else if(strncmp(p_ptr, "jettison", 8) == 0) {
        /* 2. Jettison Energy Cost */
        if (players[i].state.energy < COST_PROBE_LAUNCH) {
            char err_msg[128];
            SG_SPRINTF(err_msg, "Insufficient energy for controlled core ejection (Req: %d).", COST_PROBE_LAUNCH);
            send_server_msg(i, "ENGINEERING", err_msg);
            return;
        }
        send_server_msg(i, "CRITICAL", "EMERGENCY JETTISON INITIATED! HYPERDRIVE CORE EJECTED!");
        send_server_msg(i, "ENGINEERING", "Hyperdrive system is OFFLINE. Massive feedback damage detected.");
        
        players[i].state.energy -= COST_PROBE_LAUNCH;
        players[i].state.system_health[0] = 0.0; /* Hyperdrive destroyed */
        players[i].nav_state = NAV_STATE_DRIFT;
        
        /* Massive damage to the ship */
        apply_hull_damage(i, 40.0); /* 40% hull damage + random system damage */
        
        /* Visual FX */
        push_server_event(i, IPC_EV_BOOM, players[i].state.s1, players[i].state.s2, players[i].state.s3, 0, 0, 0, 1);
    } else if(strncmp(p_ptr, "probe", 5) == 0) {
        /* Skip 'probe' word and spaces */
        const char *coords = p_ptr + 5;
        while(*coords && (*coords == ' ' || *coords == '\t')) coords++;

        /* 3. Probe Hardware Requirements (Sensors ID 2, Computer ID 6) */
        if (players[i].state.system_health[2] < THRESHOLD_SYS_DAMAGED || players[i].state.system_health[6] < THRESHOLD_SYS_DAMAGED) {
            char err_msg[128];
            SG_SPRINTF(err_msg, "PROBE LAUNCH FAILURE: Sensors and Computer must be above %.0f%% integrity.", THRESHOLD_SYS_DAMAGED);
            send_server_msg(i, "SCIENCE", err_msg);
            return;
        }
        if (players[i].state.energy < COST_PROBE_LAUNCH) {
            char err_msg[128];
            SG_SPRINTF(err_msg, "Insufficient energy for probe launch (Req: %d).", COST_PROBE_LAUNCH);
            send_server_msg(i, "COMPUTER", err_msg);
            return;
        }

        int qx, qy, qz;
        if (sg_scan_nums(coords, "iii", &qx, &qy, &qz) == 3) {
            if (!IS_Q_VALID(qx, qy, qz)) { send_server_msg(i, "COMPUTER", "Invalid coordinates."); return; }
            int p_idx = -1;
            for(int p=0; p<3; p++) if(!players[i].state.probes[p].active) { p_idx = p; break; }
            if(p_idx == -1) { send_server_msg(i, "COMPUTER", "All probe slots active."); return; }
            
            players[i].state.energy -= COST_PROBE_LAUNCH;
            players[i].state.probes[p_idx].active = 1;
            players[i].state.probes[p_idx].q1 = qx;
            players[i].state.probes[p_idx].q2 = qy;
            players[i].state.probes[p_idx].q3 = qz;
            players[i].state.probes[p_idx].gx = players[i].gx;
            players[i].state.probes[p_idx].gy = players[i].gy;
            players[i].state.probes[p_idx].gz = players[i].gz;
            
            double target_gx = (qx - 1) * QUADRANT_SIZE + 5.0;
            double target_gy = (qy - 1) * QUADRANT_SIZE + (QUADRANT_SIZE / 2.0);
            double target_gz = (qz - 1) * QUADRANT_SIZE + (QUADRANT_SIZE / 2.0);
            double dx = target_gx - players[i].state.probes[p_idx].gx;
            double dy = target_gy - players[i].state.probes[p_idx].gy;
            double dz = target_gz - players[i].state.probes[p_idx].gz;
            double dist = sqrtf((float)(dx*dx + dy*dy + dz*dz));
            double time = dist / (QUADRANT_SIZE / 12.0); if (time < 1.0) time = 1.0;
            
            players[i].state.probes[p_idx].vx = dx / (time * (double)GAME_TICK_RATE);
            players[i].state.probes[p_idx].vy = dy / (time * (double)GAME_TICK_RATE);
            players[i].state.probes[p_idx].vz = dz / (time * (double)GAME_TICK_RATE);
            players[i].state.probes[p_idx].eta = time;
            players[i].state.probes[p_idx].status = 0;
            
            char msg[128]; SG_SPRINTF(msg, "Deep Space probe launched to [%d,%d,%d]. ETA: %.1f sec.", qx, qy, qz, time);
            send_server_msg(i, "SCIENCE", msg);
        } else send_server_msg(i, "COMPUTER", "Usage: aux probe <QX> <QY> <QZ>");
    } else if(strncmp(p_ptr, "report", 6) == 0) {
        if (players[i].state.energy < COST_ACTION_MED) { 
            char err_msg[128];
            SG_SPRINTF(err_msg, "Insufficient energy for telemetry (Req: %d).", COST_ACTION_MED);
            send_server_msg(i, "COMPUTER", err_msg); 
            return; 
        }
        int p_idx;
        if (sg_scan_nums(p_ptr + 6, "i", &p_idx) == 1) {
            p_idx--;
            if (p_idx < 0 || p_idx >= 3 || !players[i].state.probes[p_idx].active) { send_server_msg(i, "COMPUTER", "Probe not active."); return; }
            if (players[i].state.probes[p_idx].status == 0) { send_server_msg(i, "SCIENCE", "Probe still en route."); return; }
            
            players[i].state.energy -= COST_ACTION_MED;
            int pq1 = players[i].state.probes[p_idx].q1, pq2 = players[i].state.probes[p_idx].q2, pq3 = players[i].state.probes[p_idx].q3;
            QuadrantIndex *lq = &spatial_index[pq1][pq2][pq3];

            /* Detailed Anomaly Breakdown */
            int p_rp=0, p_ac=0, p_ma=0;
            for(int p=0; p<lq->pulsar_count; p++) {
                if (lq->pulsars[p]->type == 0) p_rp++;
                else if (lq->pulsars[p]->type == 1) p_ac++;
                else p_ma++;
            }
            int n_st=0, n_he=0, n_dm=0, n_io=0, n_gr=0, n_te=0;
            for(int n=0; n<lq->nebula_count; n++) {
                switch(lq->nebulas[n]->type) {
                    case 0: n_st++; break; case 1: n_he++; break; case 2: n_dm++; break;
                    case 3: n_io++; break; case 4: n_gr++; break; case 5: n_te++; break;
                    default: break;
                }
            }

            char msg[2048];
            SG_SPRINTF(msg, "\n" CYAN ".--- PROBE P%d: REAL-TIME DEEP SPACE TELEMETRY ---." RESET "\n"
                         " QUADRANT: " WHITE "[%d, %d, %d]" RESET " | SECTOR: [%.2f, %.2f, %.2f]]]" RESET "\n"
                         "-------------------------------------------------\n"
                         " 🚀 PLAYERS:   %d    ⚔️  HOSTILES:  %d\n"
                         " 🛰️  STARBASES: %d    🌟 STARS:      %d\n"
                         " 🪐 PLANETS:   %d    🕳️  BLACK HOLES:%d\n"
                         "-------------------------------------------------\n"
                         " " YELLOW "[ NEBULA CLASSIFICATION ]" RESET "\n"
                         " Standard: %-2d High-En: %-2d Dark-Mat: %-2d\n"
                         " Ionic:    %-2d Grav:    %-2d Temporal: %-2d\n"
                         "-------------------------------------------------\n"
                         " " MAGENTA "[ PULSAR CLASSIFICATION ]" RESET "\n"
                         " Rotation-Powered: %-2d  Accretion-Powered: %-2d\n"
                         " Magnetar:         %-2d\n"
                         "-------------------------------------------------\n"
                         " ☄️  COMETS:    %d    🪨  ASTEROIDS:  %d\n"
                         " 🏗️  PLATFORMS: %d    🏚️  DERELICTS:  %d\n"
                         " 📡 COMM BUOYS:%d    🌀 RIFTS:      %d\n"
                         " ⚓ MINES:      %d    👾 MONSTERS:   %d\n"
                         "-------------------------------------------------", 
                         p_idx+1, pq1, pq2, pq3, 
                         players[i].state.probes[p_idx].s1, players[i].state.probes[p_idx].s2, players[i].state.probes[p_idx].s3,
                         lq->player_count, lq->npc_count, lq->base_count, lq->star_count + lq->sys_star_count,
                         lq->planet_count + lq->sys_planet_count, lq->bh_count,
                         n_st, n_he, n_dm, n_io, n_gr, n_te,
                         p_rp, p_ac, p_ma,
                         lq->comet_count, lq->asteroid_count, lq->platform_count, lq->derelict_count,
                         lq->buoy_count, lq->rift_count, lq->mine_count, lq->monster_count);
            send_server_msg(i, "SCIENCE", msg);
        } else send_server_msg(i, "COMPUTER", "Usage: aux report <1-3>");
    } else if(strncmp(p_ptr, "recover", 7) == 0) {
        int p_idx;
        if (sg_scan_nums(p_ptr + 7, "i", &p_idx) == 1) {
            p_idx--;
            if (p_idx < 0 || p_idx >= 3 || !players[i].state.probes[p_idx].active) { send_server_msg(i, "COMPUTER", "Probe not active."); return; }
            
            /* Recovery Range Check */
            int pq1 = players[i].state.probes[p_idx].q1, pq2 = players[i].state.probes[p_idx].q2, pq3 = players[i].state.probes[p_idx].q3;
            if (pq1 != players[i].state.q1 || pq2 != players[i].state.q2 || pq3 != players[i].state.q3) {
                send_server_msg(i, "COMPUTER", "Probe is in a different quadrant. Navigation required.");
                return;
            }
            double dx = players[i].state.probes[p_idx].s1 - players[i].state.s1;
            double dy = players[i].state.probes[p_idx].s2 - players[i].state.s2;
            double dz = players[i].state.probes[p_idx].s3 - players[i].state.s3;
            if (sqrtf((float)(dx*dx + dy*dy + dz*dz)) > 2.0) {
                send_server_msg(i, "COMPUTER", "Probe out of recovery range (> 2.0).");
                return;
            }

            push_server_event(i, IPC_EV_RECOVERY, players[i].state.probes[p_idx].s1, players[i].state.probes[p_idx].s2, players[i].state.probes[p_idx].s3, 0, 0, 0, 10);
            players[i].state.probes[p_idx].active = 0; players[i].state.energy += 500;
            if (players[i].state.energy > MAX_ENERGY_CAPACITY) players[i].state.energy = MAX_ENERGY_CAPACITY;
            send_server_msg(i, "ENGINEERING", "Probe recovered. 500 Energy salvaged.");
        } else send_server_msg(i, "COMPUTER", "Usage: aux recover <1-3>");
    } else send_server_msg(i, "COMPUTER", "AUXILIARY: probe | report | recover | jettison");
}

/* TEMP-DEBUG (REMOVE BEFORE RELEASE): spawn hostile NPCs + monsters in the
 * local player's quadrant so combat FX (beams, enemy movement) can be
 * reproduced on demand in an empty galaxy. */
static void handle_dbgspn(int i, const char *args, bool *profile_deleted) {
    (void)args; (void)profile_deleted;
    int q1 = players[i].state.q1, q2 = players[i].state.q2, q3 = players[i].state.q3;
    int nspawn = 0;
    for (int n = 0; n < MAX_NPC && nspawn < 8; n++) {
        if (npcs[n].active) continue;
        npcs[n] = (NPCShip){0};
        npcs[n].id = n;
        npcs[n].active = 1;
        npcs[n].faction = FACTION_KORTHIAN;
        npcs[n].ship_class = SHIP_CLASS_GENERIC_ALIEN;
        npcs[n].q1 = q1; npcs[n].q2 = q2; npcs[n].q3 = q3;
        npcs[n].x = 8.0 + nspawn * 3.5;
        npcs[n].y = 8.0;
        npcs[n].z = 20.0;
        npcs[n].gx = (q1 - 1) * QUADRANT_SIZE + npcs[n].x;
        npcs[n].gy = (q2 - 1) * QUADRANT_SIZE + npcs[n].y;
        npcs[n].gz = (q3 - 1) * QUADRANT_SIZE + npcs[n].z;
        npcs[n].health = MAX_TORPEDO_CAPACITY;
        npcs[n].engine_health = YIELD_HARVEST_MAX;
        npcs[n].energy = 100000;
        npcs[n].plating = 100;
        npcs[n].ai_state = AI_STATE_PATROL;
        snprintf(npcs[n].name, sizeof(npcs[n].name), "DbgNpc%d", nspawn);
        nspawn++;
    }
    int mspawn = 0;
    for (int m = 0; m < MAX_MONSTERS && mspawn < 2; m++) {
        if (monsters[m].active) continue;
        monsters[m] = (NPCMonster){0};
        monsters[m].id = m;
        monsters[m].active = 1;
        monsters[m].type = 30;
        monsters[m].q1 = q1; monsters[m].q2 = q2; monsters[m].q3 = q3;
        monsters[m].x = 28.0; monsters[m].y = 28.0; monsters[m].z = 28.0;
        monsters[m].health = MAX_TORPEDO_CAPACITY;
        monsters[m].energy = 100000;
        mspawn++;
    }
    char b[160];
    SG_SPRINTF(b, "TEMP-DEBUG: spawned %d NPCs, %d monsters in [%d,%d,%d]", nspawn, mspawn, q1, q2, q3);
    send_server_msg(i, "COMPUTER", b);
}

/* TEMP-DEBUG (REMOVE BEFORE RELEASE): report the state of the first 12 NPC
 * slots and first 4 monsters so spawned test entities can be inspected. */
static void handle_dbgnpc(int i, const char *args, bool *profile_deleted) {
    (void)args; (void)profile_deleted;
    int q1 = players[i].state.q1, q2 = players[i].state.q2, q3 = players[i].state.q3;
    int count = 0;
    for (int n = 0; n < MAX_NPC && count < 40; n++) {
        if (!npcs[n].active) continue;
        if (strncmp(npcs[n].name, "Dbg", 3) != 0 &&
            !(npcs[n].q1 == q1 && npcs[n].q2 == q2 && npcs[n].q3 == q3)) continue;
        char b[192];
        SG_SPRINTF(b, "NPC n=%d id=%d q=[%d,%d,%d] xyz=(%.2f,%.2f,%.2f) gx=%.1f h=%d eng=%.1f ai=%d death=%d beam_ct=%d name=%s", n, npcs[n].id, npcs[n].q1, npcs[n].q2, npcs[n].q3, npcs[n].x, npcs[n].y, npcs[n].z, npcs[n].gx, npcs[n].health, npcs[n].engine_health, npcs[n].ai_state, npcs[n].death_timer, npcs[n].beam_count, npcs[n].name);
        send_server_msg(i, "DEBUG", b);
        count++;
    }
    int mcount = 0;
    for (int m = 0; m < MAX_MONSTERS && mcount < 10; m++) {
        if (!monsters[m].active) continue;
        if (!(monsters[m].q1 == q1 && monsters[m].q2 == q2 && monsters[m].q3 == q3)) continue;
        char b[192];
        SG_SPRINTF(b, "MON m=%d type=%d q=[%d,%d,%d] xyz=(%.2f,%.2f,%.2f) h=%d beam_ct=%d", m, monsters[m].type, monsters[m].q1, monsters[m].q2, monsters[m].q3, monsters[m].x, monsters[m].y, monsters[m].z, monsters[m].health, monsters[m].beam_count);
        send_server_msg(i, "DEBUG", b);
        mcount++;
    }
    char b[128];
    SG_SPRINTF(b, "TEMP-DEBUG: NPC report complete (%d shown).", count);
    send_server_msg(i, "COMPUTER", b);
}

/* --- Command Registry Table --- */

