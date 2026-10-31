/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * commands_helm.inc — command handlers: power distribution and navigation (pow, nav, imp, pos, vec, apr).
 * Part of the command handlers, split by domain from commands_handlers.inc
 * (2026.10.31.15) without any code change; included, in this order, by
 * commands_handlers.inc into commands.c (one translation unit).
 */
void handle_pow(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    double e, s, w;
    if (sscanf(params, "%lf %lf %lf", &e, &s, &w) == 3) {
        if (players[i].state.system_health[6] < THRESHOLD_SYS_CRITICAL) {
            send_server_msg(i, "ENGINEERING", "Power routing core FAILURE. Reallocation impossible.");
            return;
        }
                if (players[i].state.energy < COST_ACTION_MED) {
                    send_server_msg(i, "COMPUTER", "Insufficient energy for reactor bus reconfiguration.");
                    return;
                }
        
                /* 3. Reliability logic (Computer 10-50% health) */
                if (players[i].state.system_health[6] < THRESHOLD_SYS_DEGRADED && (rand() % 100 < PROB_MISFIRE_DEGRADED)) {
                    players[i].state.energy -= (COST_ACTION_MED / (uint64_t)RATIO_ENERGY_REDUCTION); /* Partial energy loss even on failure */
                    send_server_msg(i, "ENGINEERING", "RECONFIGURATION FAILED: Logic core bus error. Data parity mismatch.");
                    return;
                }
        
                if (e < 0) e = 0; 
                if (s < 0) s = 0; 
                if (w < 0) w = 0;
        double total = e + s + w;
        if (total > 0) {
            players[i].state.power_dist[0] = e / total;
            players[i].state.power_dist[1] = s / total;
            players[i].state.power_dist[2] = w / total;
            players[i].state.energy -= COST_ACTION_MED;
            char msg[128];
            sprintf(msg, "Power distribution stabilized: E:%.0f%% S:%.0f%% W:%.0f%%.", 
                    players[i].state.power_dist[0]*(double)YIELD_HARVEST_MAX, players[i].state.power_dist[1]*(double)YIELD_HARVEST_MAX, players[i].state.power_dist[2]*(double)YIELD_HARVEST_MAX);
            send_server_msg(i, "ENGINEERING", msg);
        } else send_server_msg(i, "COMPUTER", "Invalid power distribution ratio.");
    } else send_server_msg(i, "COMPUTER", "Usage: pow <Engines> <Shields> <Weapons>");
}

/* Helper to calculate hyperdrive speed: Factor 9.9 calibrated for 
   traversing the galaxy diagonal (1600*sqrt(3) units) in 40 seconds. 
   Constant speed independent of power/health. */
static double calculate_hyperdrive_speed(double factor) {
    /* Calculated using COEFF_HYPER_DIVISOR defined in game_config.h */
    return factor / COEFF_HYPER_DIVISOR;
}

void handle_nav(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    double h, m, w, factor = 6.0;
    int args = sscanf(params, "%lf %lf %lf %lf", &h, &m, &w, &factor);
    if (args >= 3) {
        if (players[i].state.system_health[0] < THRESHOLD_SYS_DEGRADED || players[i].state.system_health[2] < THRESHOLD_SYS_DEGRADED) {
            send_server_msg(i, "ENGINEERING", "CRITICAL: Hyperdrive and Sensors must be above 50% integrity.");
            return;
        }
        if (players[i].state.energy < COST_HYPERDRIVE_INIT || players[i].state.inventory[1] < 1) {
            char err_msg[128];
            sprintf(err_msg, "Insufficient resources for Hyperdrive (Req: %d Energy, 1 Aetherium).", COST_HYPERDRIVE_INIT);
            send_server_msg(i, "COMPUTER", err_msg);
            return;
        }
        if (factor < (float)DIST_BOUNDARY_MARGIN) factor = (float)DIST_BOUNDARY_MARGIN; 
        if (factor > (float)SPEED_HYPER_MAX) factor = (float)SPEED_HYPER_MAX;
        normalize_upright(&h, &m);
        players[i].target_h = h; 
        players[i].target_m = m;
        players[i].start_h = players[i].state.van_h; 
        players[i].start_m = players[i].state.van_m;
        double rad_h = h * M_PI / 180.0; 
        double rad_m = m * M_PI / 180.0;
        players[i].dx = cos(rad_m) * sin(rad_h); 
        players[i].dy = cos(rad_m) * -cos(rad_h); 
        players[i].dz = sin(rad_m);
        players[i].target_gx = players[i].gx + players[i].dx * w * QUADRANT_SIZE;
        players[i].target_gy = players[i].gy + players[i].dy * w * QUADRANT_SIZE;
        players[i].target_gz = players[i].gz + players[i].dz * w * QUADRANT_SIZE;
        
        players[i].hyper_speed = calculate_hyperdrive_speed(factor); 
        players[i].nav_state = NAV_STATE_ALIGN; 
        players[i].is_docked = 0;
        players[i].state.energy -= COST_HYPERDRIVE_INIT;
        players[i].state.inventory[1] -= 1;
        double dh = players[i].target_h - players[i].state.van_h;
        while(dh>180) dh-=360; 
        while(dh<-180) dh+=360;
        players[i].nav_timer = (fabs(dh)<1.0 && fabs(players[i].target_m - players[i].state.van_m)<1.0) ? (GAME_TICK_RATE / 6) : (int)GAME_TICK_RATE;
        players[i].align_timer = players[i].nav_timer;
        char msg[128]; sprintf(msg, "Course plotted. Aligning for Hyperdrive %.1f.", factor);
        send_server_msg(i, "HELMSMAN", msg);
    } else send_server_msg(i, "COMPUTER", "Usage: nav <H> <M> <W> [Factor]");
}

void handle_imp(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    double h, m, s, dist = -1.0;
    int args = sscanf(params, "%lf %lf %lf %lf", &h, &m, &s, &dist);
    if (players[i].state.system_health[1] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "ENGINEERING", "Impulse drive system is CRITICAL.");
        return;
    }
    if (players[i].state.system_health[1] < (THRESHOLD_SYS_DAMAGED + 15.0) && (rand() % 100 > (int)players[i].state.system_health[1])) {
        send_server_msg(i, "ENGINEERING", "Impulse manifold pressure unstable. Engines failed to engage!");
        return;
    }
    if (players[i].state.energy < COST_ACTION_HIGH) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for impulse.");
        return;
    }
    if (args == 1) {
        players[i].hyper_speed = h / COEFF_IMPULSE_DIVISOR; 
        if (players[i].hyper_speed > SPEED_IMPULSE_TICK_MAX) players[i].hyper_speed = SPEED_IMPULSE_TICK_MAX;
        players[i].target_gx = -1.0; /* Reset target on manual speed change */
        if (players[i].hyper_speed <= 0) {
            players[i].nav_state = NAV_STATE_IDLE;
            players[i].dx = 0; players[i].dy = 0; players[i].dz = 0;
            send_server_msg(i, "HELMSMAN", "Impulse engines stopped. All stop.");
        } else {
            /* Update vectors based on CURRENT heading/mark */
            double rad_h = players[i].state.van_h * M_PI / 180.0;
            double rad_m = players[i].state.van_m * M_PI / 180.0;
            players[i].dx = cos(rad_m) * sin(rad_h); 
            players[i].dy = cos(rad_m) * -cos(rad_h); 
            players[i].dz = sin(rad_m);
            
            char msg[64]; sprintf(msg, "Impulse adjusted to %.0f%%.", players[i].hyper_speed * COEFF_IMPULSE_DIVISOR);
            send_server_msg(i, "HELMSMAN", msg); players[i].nav_state = NAV_STATE_IMPULSE;
        }
        players[i].state.energy -= (COST_ACTION_HIGH / (uint64_t)RATIO_ENERGY_REDUCTION);
    } else if (args >= 3) {
        normalize_upright(&h, &m);
        players[i].target_h = h; 
        players[i].target_m = m;
        players[i].start_h = players[i].state.van_h; 
        players[i].start_m = players[i].state.van_m;
        
        double rad_h = h * M_PI / 180.0; 
        double rad_m = m * M_PI / 180.0;
        players[i].dx = cos(rad_m) * sin(rad_h); 
        players[i].dy = cos(rad_m) * -cos(rad_h); 
        players[i].dz = sin(rad_m);
        
        players[i].hyper_speed = s / COEFF_IMPULSE_DIVISOR; 
        if (players[i].hyper_speed > SPEED_IMPULSE_TICK_MAX) players[i].hyper_speed = SPEED_IMPULSE_TICK_MAX;
        
        if (args == 4) {
            players[i].target_gx = players[i].gx + players[i].dx * dist;
            players[i].target_gy = players[i].gy + players[i].dy * dist;
            players[i].target_gz = players[i].gz + players[i].dz * dist;
        } else {
            /* No distance provided: unlimited cruise */
            players[i].target_gx = -1.0; 
        }
        
        if (players[i].hyper_speed <= 0) {
            players[i].nav_state = NAV_STATE_ALIGN_ONLY;
            send_server_msg(i, "HELMSMAN", "Course plotted. Aligning only (Speed is zero).");
        } else {
            players[i].nav_state = NAV_STATE_ALIGN_IMPULSE;
            char msg[128];
            if (args == 4) sprintf(msg, "Course plotted. Aligning for Impulse drive (Dist: %.2f).", dist);
            else sprintf(msg, "Course plotted. Aligning for Impulse drive.");
            send_server_msg(i, "HELMSMAN", msg);
        }
        players[i].state.energy -= COST_ACTION_HIGH;
        players[i].is_docked = 0;
        double dh = players[i].target_h - players[i].state.van_h;
        while(dh>180) dh-=360; 
        while(dh<-180) dh+=360;
        players[i].nav_timer = (fabs(dh)<1.0 && fabs(players[i].target_m - players[i].state.van_m)<1.0) ? (GAME_TICK_RATE / 6) : (int)GAME_TICK_RATE;
        players[i].align_timer = players[i].nav_timer;
    } else send_server_msg(i, "COMPUTER", "Usage: imp <H> <M> <S> [Dist] or imp <S>");
}

void handle_pos(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    double h, m, r = 0.0;
    int parsed = sscanf(params, "%lf %lf %lf", &h, &m, &r);
    if (parsed >= 2) {
        if (players[i].state.system_health[1] < THRESHOLD_SYS_CRITICAL) {
            send_server_msg(i, "ENGINEERING", "Attitude thrusters OFFLINE.");
            return;
        }
        if (players[i].state.energy < COST_MANEUVER_ADJUST) {
            send_server_msg(i, "COMPUTER", "Insufficient energy for adjustment.");
            return;
        }
        normalize_upright(&h, &m);
        /* Roll must stay in [0, 360): the HUD displays it as-is, so the
         * requested value is normalized (370 -> 10, -10 -> 350). */
        r = nav_wrap_heading(r);
        players[i].target_h = h; players[i].target_m = m;
        players[i].target_r = r;
        players[i].start_h = players[i].state.van_h; players[i].start_m = players[i].state.van_m;
        players[i].start_r = players[i].state.van_r;
        players[i].nav_state = NAV_STATE_ALIGN_ONLY; players[i].state.energy -= COST_MANEUVER_ADJUST;
        double dh = players[i].target_h - players[i].state.van_h;
        while(dh>180) dh-=360; 
        while(dh<-180) dh+=360;
        double dr = players[i].target_r - players[i].state.van_r;
        while(dr>180) dr-=360;
        while(dr<-180) dr+=360;
        players[i].nav_timer = (fabs(dh)<1.0 && fabs(players[i].target_m - players[i].state.van_m)<1.0 && fabs(dr)<1.0) ? (GAME_TICK_RATE / 6) : (int)GAME_TICK_RATE;
        players[i].align_timer = players[i].nav_timer;
        send_server_msg(i, "HELMSMAN", "Ship re-orienting.");
    } else send_server_msg(i, "COMPUTER", "Usage: pos <H> <M> [R]");
}

/* handle_vec: Vector Thrust — move along course (H, M) at speed S
 * WITHOUT touching the ship's attitude. Unlike "imp" (3 args), there
 * is no target alignment: van_h / van_m / van_r are left exactly as
 * they are, the flight vector (dx, dy, dz) is set from the requested
 * course and the ship cruises in NAV_STATE_IMPULSE along it. This is
 * what the pilot hat-switch retro-thrusters need: translate the ship
 * (brake / lateral / vertical) while the cockpit view stays on the
 * current nose. "vec <H> <M> 0" releases the thrust (all stop, ship
 * dead in space — same stop semantics as the 1-arg impulse). */
void handle_vec(int i, const char *params, bool *should_disconnect) {
    (void)should_disconnect;
    double h, m, s;
    int args = sscanf(params, "%lf %lf %lf", &h, &m, &s);
    if (args != 3) {
        send_server_msg(i, "COMPUTER", "Usage: vec <H> <M> <S> [S 0 to stop]");
        return;
    }
    if (players[i].state.system_health[1] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "ENGINEERING", "Vector thrusters system is CRITICAL.");
        return;
    }
    if (players[i].state.energy < COST_ACTION_HIGH) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for vector thrust.");
        return;
    }
    normalize_upright(&h, &m);
    double rad_h = h * M_PI / 180.0;
    double rad_m = m * M_PI / 180.0;
    players[i].dx = cos(rad_m) * sin(rad_h);
    players[i].dy = cos(rad_m) * -cos(rad_h);
    players[i].dz = sin(rad_m);
    players[i].hyper_speed = s / COEFF_IMPULSE_DIVISOR;
    if (players[i].hyper_speed > SPEED_IMPULSE_TICK_MAX) players[i].hyper_speed = SPEED_IMPULSE_TICK_MAX;
    players[i].target_gx = -1.0; /* unlimited cruise along the vector */
    players[i].is_docked = 0;
    if (players[i].hyper_speed <= 0) {
        players[i].nav_state = NAV_STATE_IDLE;
        players[i].dx = 0; players[i].dy = 0; players[i].dz = 0;
        send_server_msg(i, "HELMSMAN", "Vector thrust released. All stop.");
    } else {
        players[i].nav_state = NAV_STATE_IMPULSE;
        char msg[64];
        sprintf(msg, "Vector thrust engaged at %.0f%%.", players[i].hyper_speed * COEFF_IMPULSE_DIVISOR);
        send_server_msg(i, "HELMSMAN", msg);
    }
    players[i].state.energy -= COST_ACTION_HIGH;
}

void handle_apr(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    int tid = 0; double tdist = 2.0;
    int args = sscanf(params, " %d %lf", &tid, &tdist);
    bool tdist_given = (args == 2);
    
    /* 1. Integrity Check: Impulse (ID 1) and Computer (ID 6) */
    if (players[i].state.system_health[1] < THRESHOLD_SYS_CRITICAL || players[i].state.system_health[6] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "COMPUTER", "AUTOPILOT FAILURE: Impulse thrusters or navigation core offline.");
        return;
    }

    if (args == 0) {
        tid = players[i].state.lock_target;
        tdist = 2.0;
    } else if (args == 1) {
        if (tid < 100) { /* Treat as distance for locked target */
            tdist = tid;
            tdist_given = true;
            tid = players[i].state.lock_target;
        } else { /* Treat as ID with default distance */
            tdist = 2.0;
        }
    }

    if (tid > 0) {
        double tx = 0, ty = 0, tz = 0; 
        bool found = false;
        int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
        char target_name[64] = "target";

        ConnectedPlayer *target_p = player_by_id(tid);
        if (target_p) {
            if (target_p->active && 
                target_p->state.q1 == pq1 && 
                target_p->state.q2 == pq2 && 
                target_p->state.q3 == pq3) {
                /* 3. Cloak check */
                if (!target_p->state.is_cloaked || 
                    target_p->faction == players[i].faction) {
                    tx = target_p->gx;
                    ty = target_p->gy;
                    tz = target_p->gz;
                    found = true;
                    strncpy(target_name, target_p->name, 63);
                }
            }
        } else {
            /* 1. Local objects check (current quadrant only) */
            QuadrantIndex *lq = &spatial_index[pq1][pq2][pq3];

            /* Table-driven resolution (targets.c): NPC ships and every
               static object type indexed in the current quadrant. */
            const TargetRangeDef *tr = target_range_for(tid);
            if (tr && (tr->flags & TGT_F_CMD_APR)) {
                const void *obj = target_find_local(tr, lq, tid);
                if (obj && target_visible(tr, obj, players[i].faction)) {
                    found = true;
                    target_abs_pos(tr, obj, &tx, &ty, &tz);
                    target_make_name(tr, obj, target_name, sizeof(target_name));
                }
            } else if (tid >= GALAXY_OBJECT_MIN_PROBE && tid <= GALAXY_OBJECT_MAX_PROBE) {
                int p_idx = (tid - GALAXY_OBJECT_MIN_PROBE) / 3;
                int pr_idx = (tid - GALAXY_OBJECT_MIN_PROBE) % 3;
                if (p_idx < MAX_CLIENTS &&
                    players[p_idx].state.probes[pr_idx].active) {
                    if (get_q_from_g(players[p_idx].state.probes[pr_idx].gx) == pq1 &&
                        get_q_from_g(players[p_idx].state.probes[pr_idx].gy) == pq2 &&
                        get_q_from_g(players[p_idx].state.probes[pr_idx].gz) == pq3) {
                        tx = players[p_idx].state.probes[pr_idx].gx;
                        ty = players[p_idx].state.probes[pr_idx].gy;
                        tz = players[p_idx].state.probes[pr_idx].gz;
                        found = true;
                        snprintf(target_name, 64, "Probe %d", tid);
                    }
                }
            }
        }

        /* Star-system bodies have a contact sphere larger than the default stand-off (2.0): stop 1.0 outside
         * it by default and never inside it (+0.3), otherwise the ship would end up inside the body. */
        if (found && (sys_id_is_star(tid) || sys_id_is_planet(tid) || sys_id_is_moon(tid))) {
            double hr = sys_body_hit_radius(tid);
            if (!tdist_given) tdist = sys_standoff(hr);
            else if (tdist < sys_min_standoff(hr)) tdist = sys_min_standoff(hr);
        }

        if (found) {
            double cx = players[i].gx, cy = players[i].gy, cz = players[i].gz;
            double dx = tx - cx, dy = ty - cy, dz = tz - cz; double d = sqrt(dx*dx + dy*dy + dz*dz);
            if (d > (tdist + 0.01)) {
                players[i].state.energy -= COST_ACTION_HIGH;
                double h = atan2(dx, -dy) * 180.0 / M_PI; if(h < 0) h += 360; 
                double m = asin(dz/d) * 180.0 / M_PI;
                players[i].target_h = h; players[i].target_m = m;
                players[i].dx = dx/d; players[i].dy = dy/d; players[i].dz = dz/d;
                players[i].target_gx = cx + players[i].dx * (d - tdist); 
                players[i].target_gy = cy + players[i].dy * (d - tdist); 
                players[i].target_gz = cz + players[i].dz * (d - tdist);
                players[i].approach_dist = tdist;
                players[i].apr_target = tid;
                players[i].nav_state = NAV_STATE_ALIGN; players[i].nav_timer = (int)GAME_TICK_RATE; 
                players[i].align_timer = (int)GAME_TICK_RATE; /* Store initial timer for smooth LERP */
                players[i].start_h = players[i].state.van_h; players[i].start_m = players[i].state.van_m;
                
                /* 4. Improved Feedback */
                char msg[128];
                sprintf(msg, "Autopilot engaged. Approaching %s at %.1f sector units.", target_name, tdist);
                send_server_msg(i, "HELMSMAN", msg);
            } else send_server_msg(i, "COMPUTER", "Target already in range.");
        } else send_server_msg(i, "COMPUTER", "Target not found or obscured (Targets for 'apr' must be in current quadrant).");
    } else send_server_msg(i, "COMPUTER", "Usage: apr <ID> [DIST]");
}

