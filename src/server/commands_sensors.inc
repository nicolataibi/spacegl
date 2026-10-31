/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * commands_sensors.inc — command handlers: short and long range sensors (srs, lrs) and their helpers.
 * Part of the command handlers, split by domain from commands_handlers.inc
 * (2026.10.31.15) without any code change; included, in this order, by
 * commands_handlers.inc into commands.c (one translation unit).
 */
/* Helper to get sensor error based on system health */
static double get_sensor_error(int p_idx) {
    if (is_near_buoy(p_idx)) return 0.0; /* Buoy provides perfect sensor calibration */
    double health = players[p_idx].state.system_health[2];
    if (health >= (double)YIELD_HARVEST_MAX) return 0.0;
    /* Noise increases exponentially as health drops */
    double noise_factor = pow(1.0 - (health / (double)YIELD_HARVEST_MAX), RATIO_ENERGY_REDUCTION);
    return ((rand() % 2000 - 1000) / 1000.0) * noise_factor * RATIO_SENSOR_ERROR;
}

const char* get_ship_class_name(int ship_class) {
    switch(ship_class) {
        case SHIP_CLASS_LEGACY: return "Legacy Class";
        case SHIP_CLASS_SCOUT:      return "Scout Class";
        case SHIP_CLASS_HEAVY_CRUISER:    return "Heavy Cruiser";
        case SHIP_CLASS_MULTI_ENGINE: return "Multi-Engine Cruiser";
        case SHIP_CLASS_ESCORT:      return "Escort Class";
        case SHIP_CLASS_EXPLORER:       return "Explorer Class";
        case SHIP_CLASS_FLAGSHIP:    return "Flagship Class";
        case SHIP_CLASS_SCIENCE:     return "Science Vessel";
        case SHIP_CLASS_CARRIER:        return "Carrier Class";
        case SHIP_CLASS_TACTICAL:       return "Tactical Cruiser";
        case SHIP_CLASS_DIPLOMATIC:   return "Diplomatic Cruiser";
        case SHIP_CLASS_RESEARCH:       return "Research Vessel";
        case SHIP_CLASS_FRIGATE:  return "Frigate Class";
        case SHIP_CLASS_SENTINEL: return "Sentinel Class";
        case SHIP_CLASS_GENERIC_ALIEN: return "Vessel";
        default: return "Unknown";
    }
}

void handle_srs(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    if (players[i].state.system_health[2] < (THRESHOLD_SYS_CRITICAL / RATIO_ENERGY_REDUCTION)) {
        send_server_msg(i, "COMPUTER", "SENSOR FAILURE: Primary arrays are non-functional.");
        return;
    }
    if (players[i].state.energy < COST_ACTION_LOW) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for short-range pulse.");
        return;
    }
    players[i].state.energy -= COST_ACTION_LOW;

    const size_t b_cap = LARGE_DATA_BUFFER;
    char *b = calloc(b_cap, sizeof(char));
    if (!b) return;

    int q1=players[i].state.q1, q2=players[i].state.q2, q3=players[i].state.q3; 
    double s1=players[i].state.s1, s2=players[i].state.s2, s3=players[i].state.s3;

    snprintf(b, LARGE_DATA_BUFFER, CYAN "\n--- SHORT RANGE SENSOR ANALYSIS ---" RESET "\nQUADRANT: [%d,%d,%d] | SECTOR: [%.2f,%.2f,%.2f]\n", q1, q2, q3, s1, s2, s3);
    
    /* Improved Feedback: Nebula and Ion Storm detection */
    extern bool is_player_in_nebula(int i);
    if (is_player_in_nebula(i)) {
        sg_strcat_n(b, b_cap, YELLOW "WARNING: High-density nebular interference detected. Sensor range and accuracy degraded.\n" RESET);
    }
    long long g_val = spacegl_master.g[q1][q2][q3];
    if (g_val > 0 && (g_val / 10000000LL) % 10) {
        sg_strcat_n(b, b_cap, RED "CRITICAL: Ion Storm in progress! Sub-space telemetry heavily scrambled.\n" RESET);
    }

    snprintf(b+strlen(b), LARGE_DATA_BUFFER-strlen(b), "ENERGY: %" PRIu64 " | TORPEDOES: %d | STATUS: %s\n", players[i].state.energy, players[i].state.torpedoes, players[i].state.is_cloaked ? MAGENTA "CLOAKED" RESET : GREEN "NORMAL" RESET);
    sg_strcat_n(b, b_cap, "\nTYPE       ID    POSITION      DIST   H / M         DETAILS\n");

    QuadrantIndex *local_q = &spatial_index[q1][q2][q3];
    int locked_id = players[i].state.lock_target;
    bool chasing = (players[i].nav_state == NAV_STATE_CHASE);
    double sensor_h = players[i].state.system_health[2];

    /* 1. Players */
    for (int j = 0; j < local_q->player_count; j++) {
        ConnectedPlayer *p = local_q->players[j];
        if (p == &players[i] || p->state.is_cloaked) {
            continue;
        }
        if (sensor_h < (THRESHOLD_SYS_DAMAGED + 5.0) && (rand() % 100 > sensor_h + 50)) {
            continue;
        }

        double dx = p->state.s1 - s1 + get_sensor_error(i);
        double dy = p->state.s2 - s2 + get_sensor_error(i);
        double dz = p->state.s3 - s3 + get_sensor_error(i); 
        double d = sqrt(dx * dx + dy * dy + dz * dz);
        double h = atan2(dx, -dy) * 180 / M_PI;
        if (h < 0) {
            h += 360;
        }
        double m = (d > 0.001) ? asin(dz / d) * 180 / M_PI : 0;
        int pid = (int)(p - players) + 1;
        char status[64] = "";
        if (pid == locked_id) {
            strncpy(status, RED "[LOCKED]" RESET, 63);
            if (chasing) {
                strncat(status, B_RED "[CHASE]" RESET, 63 - strlen(status));
            }
        }
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🚀 %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     %s (Player) [E:%" PRIu64 "] %s\n", "Vessel", pid, p->state.s1 + get_sensor_error(i), p->state.s2 + get_sensor_error(i), p->state.s3 + get_sensor_error(i), d, h, m, p->name, p->state.energy, status);
    }

    /* 2. NPC Ships */
    for (int n = 0; n < local_q->npc_count; n++) {
        NPCShip *npc = local_q->npcs[n];
        if (sensor_h < (THRESHOLD_SYS_DAMAGED + 5.0) && (rand() % 100 > sensor_h + 50)) {
            continue;
        }

        double dx = npc->x - s1 + get_sensor_error(i);
        double dy = npc->y - s2 + get_sensor_error(i);
        double dz = npc->z - s3 + get_sensor_error(i); 
        double d = sqrt(dx * dx + dy * dy + dz * dz);
        double h = atan2(dx, -dy) * 180 / M_PI;
        if (h < 0) {
            h += 360;
        }
        double m = (d > 0.001) ? asin(dz / d) * 180 / M_PI : 0;
        int nid = npc->id + GALAXY_OBJECT_MIN_NPC;
        char status[64] = "";
        if (nid == locked_id) {
            strncpy(status, RED "[LOCKED]" RESET, 63);
            if (chasing) {
                strncat(status, B_RED "[CHASE]" RESET, 63 - strlen(status));
            }
        }
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "⚔️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     %s (%s) [E:%" PRIu64 "] [Engines:%.0f%%] %s\n", "Vessel", nid, npc->x + get_sensor_error(i), npc->y + get_sensor_error(i), npc->z + get_sensor_error(i), d, h, m, get_species_name(npc->faction), npc->name, npc->energy, npc->engine_health, status);
    }

    /* 3. Starbases */
    for (int b_idx = 0; b_idx < local_q->base_count; b_idx++) {
        NPCBase *ba = local_q->bases[b_idx];
        double dx = ba->x - s1;
        double dy = ba->y - s2;
        double dz = ba->z - s3;
        double d = sqrt(dx * dx + dy * dy + dz * dz);
        double h = atan2(dx, -dy) * 180 / M_PI;
        if (h < 0) {
            h += 360;
        }
        double m = (d > 0.001) ? asin(dz / d) * 180 / M_PI : 0;
        int baid = ba->id + GALAXY_OBJECT_MIN_STARBASE;
        char status[64] = "";
        if (baid == locked_id) {
            SG_STRCAT(status, RED "[LOCKED]" RESET);
        }
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🛰️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     %s Starbase %s\n", "Starbase", baid, ba->x, ba->y, ba->z, d, h, m, get_species_name(ba->faction), status);
    }

    /* 4. Planets */
    for (int p_idx = 0; p_idx < local_q->planet_count; p_idx++) {
        NPCPlanet *pl = local_q->planets[p_idx];
        double dx = pl->x - s1;
        double dy = pl->y - s2;
        double dz = pl->z - s3;
        double d = sqrt(dx * dx + dy * dy + dz * dz);
        double h = atan2(dx, -dy) * 180 / M_PI;
        if (h < 0) {
            h += 360;
        }
        double m = (d > 0.001) ? asin(dz / d) * 180 / M_PI : 0;
        int plid = pl->id + GALAXY_OBJECT_MIN_PLANET;
        char status[64] = "";
        if (plid == locked_id) {
            SG_STRCAT(status, RED "[LOCKED]" RESET);
        }
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🪐 %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     %s Planet %s\n", "Planet", plid, pl->x, pl->y, pl->z, d, h, m, planet_class_name(pl->planet_class), status);
    }

    /* 5. Stars */
    for (int s_idx = 0; s_idx < local_q->star_count; s_idx++) {
        NPCStar *st = local_q->stars[s_idx];
        double dx = st->x - s1;
        double dy = st->y - s2;
        double dz = st->z - s3;
        double d = sqrt(dx * dx + dy * dy + dz * dz);
        double h = atan2(dx, -dy) * 180 / M_PI;
        if (h < 0) {
            h += 360;
        }
        double m = (d > 0.001) ? asin(dz / d) * 180 / M_PI : 0;
        int sid = st->id + GALAXY_OBJECT_MIN_STAR;
        char status[64] = "";
        if (sid == locked_id) {
            SG_STRCAT(status, RED "[LOCKED]" RESET);
        }
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌟 %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Star %s\n", "Star", sid, st->x, st->y, st->z, d, h, m, status);
    }

    /* 5b. Star system of this quadrant: star, planets and moons (include/star_system.h) */
    if (local_q->sys_star_count > 0 && local_q->sys_stars[0]) {
        sys_refresh_positions((int)(local_q->sys_stars[0] - sys_stars), sys_clock_s());
        #define SRS_SYS_ROW(EMO, SOBJ, LABEL, INFO) do { \
            double dx_ = (SOBJ)->x - s1, dy_ = (SOBJ)->y - s2, dz_ = (SOBJ)->z - s3; \
            double d_ = sqrt(dx_ * dx_ + dy_ * dy_ + dz_ * dz_); \
            double h_ = atan2(dx_, -dy_) * 180 / M_PI; if (h_ < 0) h_ += 360; \
            double m_ = (d_ > 0.001) ? asin(dz_ / d_) * 180 / M_PI : 0; \
            char st_[64] = ""; if ((SOBJ)->id == locked_id) SG_STRCAT(st_, RED "[LOCKED]" RESET); \
            SAFE_APPEND(b, LARGE_DATA_BUFFER, EMO " %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     %s %s\n", \
                        (LABEL), (SOBJ)->id, (SOBJ)->x, (SOBJ)->y, (SOBJ)->z, d_, h_, m_, (INFO), st_); \
        } while (0)
        if (local_q->sys_stars[0]->active) {
            char info_[64]; snprintf(info_, sizeof(info_), "Star (spectral %d)", local_q->sys_stars[0]->star_class);
            SRS_SYS_ROW("🌟", local_q->sys_stars[0], "Star", info_);
        }
        for (int k_ = 0; k_ < local_q->sys_planet_count; k_++) {
            NPCSysPlanet *pl_ = local_q->sys_planets[k_]; if (!pl_ || !pl_->active) continue;
            char info_[96]; snprintf(info_, sizeof(info_), "Planet %s", planet_class_name(pl_->planet_class));
            SRS_SYS_ROW("🪐", pl_, "Planet", info_);
        }
        for (int k_ = 0; k_ < local_q->sys_moon_count; k_++) {
            NPCSysMoon *mo_ = local_q->sys_moons[k_]; if (!mo_ || !mo_->active) continue;
            const char *mn_ = obj_type_name(mo_->moon_type);
            SRS_SYS_ROW("🌙", mo_, "Moon", mn_ ? mn_ : "Moon");
        }
        #undef SRS_SYS_ROW
    }

    /* Buoys */
    for (int b_idx = 0; b_idx < local_q->buoy_count; b_idx++) {
        NPCBuoy *bu = local_q->buoys[b_idx];
        double dx = bu->x - s1; double dy = bu->y - s2; double dz = bu->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        int buid = bu->id + GALAXY_OBJECT_MIN_BUOY;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "📍 %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Buoy\n", "Buoy", buid, bu->x, bu->y, bu->z, d, h, m);
    }
    /* 6. Anomalies (Black Holes, Nebulas, Pulsars, etc) */
    for (int h_idx = 0; h_idx < local_q->bh_count; h_idx++) {
        NPCBlackHole *bh = local_q->black_holes[h_idx];
        double dx = bh->x - s1;
        double dy = bh->y - s2;
        double dz = bh->z - s3;
        double d = sqrt(dx * dx + dy * dy + dz * dz);
        double hh = atan2(dx, -dy) * 180 / M_PI;
        if (hh < 0) {
            hh += 360;
        }
        double m = (d > 0.001) ? asin(dz / d) * 180 / M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🕳️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Black Hole (Gravity Well)\n", "B-Hole", bh->id + GALAXY_OBJECT_MIN_BLACKHOLE, bh->x, bh->y, bh->z, d, hh, m);
    }
    for (int n_idx = 0; n_idx < local_q->nebula_count; n_idx++) {
        NPCNebula *nb = local_q->nebulas[n_idx];
        double dx = nb->x - s1;
        double dy = nb->y - s2;
        double dz = nb->z - s3;
        double d = sqrt(dx * dx + dy * dy + dz * dz);
        double h = atan2(dx, -dy) * 180 / M_PI;
        if (h < 0) {
            h += 360;
        }
        double m = (d > 0.001) ? asin(dz / d) * 180 / M_PI : 0;
        const char *neb_names[] = {"Standard", "High-Energy", "Dark Matter", "Ionic", "Gravimetric", "Temporal"};
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌫️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     %s Nebula\n", "Nebula", nb->id + GALAXY_OBJECT_MIN_NEBULA, nb->x, nb->y, nb->z, d, h, m, (nb->type >= 0 && nb->type < 6) ? neb_names[nb->type] : "Unknown");
    }
    for (int p_idx = 0; p_idx < local_q->pulsar_count; p_idx++) {
        NPCPulsar *pu = local_q->pulsars[p_idx];
        double dx = pu->x - s1; double dy = pu->y - s2; double dz = pu->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "☄️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Pulsar (Neutron Star)\n", "Pulsar", pu->id + GALAXY_OBJECT_MIN_PULSAR, pu->x, pu->y, pu->z, d, h, m);
    }
    for (int q_idx = 0; q_idx < local_q->quasar_count; q_idx++) {
        NPCQuasar *qu = local_q->quasars[q_idx];
        double dx = qu->x - s1; double dy = qu->y - s2; double dz = qu->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🎇  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Quasar (High-Energy Core)\n", "Quasar", qu->id + GALAXY_OBJECT_MIN_QUASAR, qu->x, qu->y, qu->z, d, h, m);
    }
    for (int r_idx = 0; r_idx < local_q->rift_count; r_idx++) {
        NPCRift *ri = local_q->rifts[r_idx];
        double dx = ri->x - s1; double dy = ri->y - s2; double dz = ri->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌀  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Sub-space Rift\n", "Rift", ri->id + GALAXY_OBJECT_MIN_RIFT, ri->x, ri->y, ri->z, d, h, m);
    }
    for (int m_idx = 0; m_idx < local_q->monster_count; m_idx++) {
        NPCMonster *mo = local_q->monsters[m_idx];
        double dx = mo->x - s1; double dy = mo->y - s2; double dz = mo->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "👾  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Space Monster\n", "Monster", mo->id + GALAXY_OBJECT_MIN_MONSTER, mo->x, mo->y, mo->z, d, h, m);
    }
    for (int s_idx = 0; s_idx < local_q->subspace_anomaly_count; s_idx++) {
        NPCSubspaceAnomaly *sa = local_q->subspace_anomalies[s_idx];
        double dx = sa->x - s1; double dy = sa->y - s2; double dz = sa->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "💥  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Subspace Anomaly\n", "Anomaly", sa->id + GALAXY_OBJECT_MIN_SUBSPACE_ANOM, sa->x, sa->y, sa->z, d, h, m);
    }
    for (int oi=0; oi<local_q->void_crystal_count; oi++) {
        NPCVoidCrystal *vc = local_q->void_crystals[oi];
        double dx = vc->x - s1; double dy = vc->y - s2; double dz = vc->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "💎  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Void Crystal\n", "Crystal", vc->id + GALAXY_OBJECT_MIN_VOID_CRYSTAL, vc->x, vc->y, vc->z, d, h, m);
    }
    for (int oi=0; oi<local_q->dyson_count; oi++) {
        NPCDyson *dy = local_q->dysons[oi];
        double dx = dy->x - s1; double dy_ = dy->y - s2; double dz = dy->z - s3;
        double d = sqrt(dx*dx + dy_*dy_ + dz*dz);
        double h = atan2(dx, -dy_) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🏗️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Dyson Fragment\n", "Dyson", dy->id + GALAXY_OBJECT_MIN_DYSON, dy->x, dy->y, dy->z, d, h, m);
    }
    for (int oi=0; oi<local_q->hub_count; oi++) {
        NPCHub *hu = local_q->hubs[oi];
        double dx = hu->x - s1; double dy = hu->y - s2; double dz = hu->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌐  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Galactic Hub\n", "Hub", hu->id + GALAXY_OBJECT_MIN_HUB, hu->x, hu->y, hu->z, d, h, m);
    }
    for (int oi=0; oi<local_q->relic_count; oi++) {
        NPCRelic *re = local_q->relics[oi];
        double dx = re->x - s1; double dy = re->y - s2; double dz = re->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "📜  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Relic\n", "Relic", re->id + GALAXY_OBJECT_MIN_RELIC, re->x, re->y, re->z, d, h, m);
    }
    for (int oi=0; oi<local_q->rupture_count; oi++) {
        NPCRupture *ru = local_q->ruptures[oi];
        double dx = ru->x - s1; double dy = ru->y - s2; double dz = ru->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🔪  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Spacetime Rupture\n", "Rupture", ru->id + GALAXY_OBJECT_MIN_RUPTURE, ru->x, ru->y, ru->z, d, h, m);
    }
    for (int oi=0; oi<local_q->satellite_count; oi++) {
        NPCSatellite *sa = local_q->satellites[oi];
        double dx = sa->x - s1; double dy = sa->y - s2; double dz = sa->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "📡  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Satellite\n", "Sat", sa->id + GALAXY_OBJECT_MIN_SATELLITE, sa->x, sa->y, sa->z, d, h, m);
    }
    for (int oi=0; oi<local_q->storm_count; oi++) {
        NPCStorm *st = local_q->storms[oi];
        double dx = st->x - s1; double dy = st->y - s2; double dz = st->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "⛈️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Ion Storm\n", "Storm", st->id + GALAXY_OBJECT_MIN_STORM, st->x, st->y, st->z, d, h, m);
    }
    for (int oi=0; oi<local_q->artifact_count; oi++) {
        NPCArtifact *ar = local_q->artifacts[oi];
        double dx = ar->x - s1; double dy = ar->y - s2; double dz = ar->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🏺  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Artifact\n", "Artifact", ar->id + GALAXY_OBJECT_MIN_ARTIFACT, ar->x, ar->y, ar->z, d, h, m);
    }
    for (int oi=0; oi<local_q->warp_gate_count; oi++) {
        NPCWarpGate *wg = local_q->warp_gates[oi];
        double dx = wg->x - s1; double dy = wg->y - s2; double dz = wg->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌌  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Warp Gate\n", "WarpGate", wg->id + GALAXY_OBJECT_MIN_WARP_GATE, wg->x, wg->y, wg->z, d, h, m);
    }
    for (int oi=0; oi<local_q->neutron_star_count; oi++) {
        NPCNeutronStar *ns = local_q->neutron_stars[oi];
        double dx = ns->x - s1; double dy = ns->y - s2; double dz = ns->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌟  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Neutron Star\n", "Neutron", ns->id + GALAXY_OBJECT_MIN_NEUTRON_STAR, ns->x, ns->y, ns->z, d, h, m);
    }
    for (int oi=0; oi<local_q->mega_struct_count; oi++) {
        NPCMegaStructure *ms = local_q->mega_structs[oi];
        double dx = ms->x - s1; double dy = ms->y - s2; double dz = ms->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🏛️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Mega Structure\n", "Mega", ms->id + GALAXY_OBJECT_MIN_MEGA_STRUCT, ms->x, ms->y, ms->z, d, h, m);
    }
    for (int oi=0; oi<local_q->dark_cloud_count; oi++) {
        NPCDarkCloud *dc = local_q->dark_clouds[oi];
        double dx = dc->x - s1; double dy = dc->y - s2; double dz = dc->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌑  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Dark Cloud\n", "Cloud", dc->id + GALAXY_OBJECT_MIN_DARK_CLOUD, dc->x, dc->y, dc->z, d, h, m);
    }
    for (int oi=0; oi<local_q->singularity_count; oi++) {
        NPCSingularity *si = local_q->singularities[oi];
        double dx = si->x - s1; double dy = si->y - s2; double dz = si->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🔮  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Singularity\n", "Sing", si->id + GALAXY_OBJECT_MIN_SINGULARITY, si->x, si->y, si->z, d, h, m);
    }
    for (int oi=0; oi<local_q->plasma_storm_count; oi++) {
        NPCPlasmaStorm *ps = local_q->plasma_storms[oi];
        double dx = ps->x - s1; double dy = ps->y - s2; double dz = ps->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "⚡  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Plasma Storm\n", "Plasma", ps->id + GALAXY_OBJECT_MIN_PLASMA_STORM, ps->x, ps->y, ps->z, d, h, m);
    }
    for (int oi=0; oi<local_q->orbital_ring_count; oi++) {
        NPCOrbitalRing *or = local_q->orbital_rings[oi];
        double dx = or->x - s1; double dy = or->y - s2; double dz = or->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "💍  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Orbital Ring\n", "Ring", or->id + GALAXY_OBJECT_MIN_ORBITAL_RING, or->x, or->y, or->z, d, h, m);
    }
    for (int oi=0; oi<local_q->time_anomaly_count; oi++) {
        NPCTimeAnomaly *ta = local_q->time_anomalies[oi];
        double dx = ta->x - s1; double dy = ta->y - s2; double dz = ta->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "⏳  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Time Anomaly\n", "T-Anom", ta->id + GALAXY_OBJECT_MIN_TIME_ANOMALY, ta->x, ta->y, ta->z, d, h, m);
    }
    for (int oi=0; oi<local_q->diffuse_nebula_count; oi++) {
        double dx = local_q->diffuse_nebulae[oi]->x - s1; double dy = local_q->diffuse_nebulae[oi]->y - s2; double dz = local_q->diffuse_nebulae[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "☁️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Diffuse Nebula\n", "Diffuse Ne", local_q->diffuse_nebulae[oi]->id + GALAXY_OBJECT_MIN_DIFFUSE_NEBULA, local_q->diffuse_nebulae[oi]->x, local_q->diffuse_nebulae[oi]->y, local_q->diffuse_nebulae[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->dark_nebula_count; oi++) {
        double dx = local_q->dark_nebulae[oi]->x - s1; double dy = local_q->dark_nebulae[oi]->y - s2; double dz = local_q->dark_nebulae[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌌  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Dark Nebula\n", "Dark Nebul", local_q->dark_nebulae[oi]->id + GALAXY_OBJECT_MIN_DARK_NEBULA, local_q->dark_nebulae[oi]->x, local_q->dark_nebulae[oi]->y, local_q->dark_nebulae[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->planetary_nebula_count; oi++) {
        double dx = local_q->planetary_nebulae[oi]->x - s1; double dy = local_q->planetary_nebulae[oi]->y - s2; double dz = local_q->planetary_nebulae[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🎆  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Planetary Neb\n", "Planetary ", local_q->planetary_nebulae[oi]->id + GALAXY_OBJECT_MIN_PLANETARY_NEBULA, local_q->planetary_nebulae[oi]->x, local_q->planetary_nebulae[oi]->y, local_q->planetary_nebulae[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->snr_count; oi++) {
        double dx = local_q->snrs[oi]->x - s1; double dy = local_q->snrs[oi]->y - s2; double dz = local_q->snrs[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "💥  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     SNR\n", "SNR", local_q->snrs[oi]->id + GALAXY_OBJECT_MIN_SNR, local_q->snrs[oi]->x, local_q->snrs[oi]->y, local_q->snrs[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->gmc_count; oi++) {
        double dx = local_q->gmcs[oi]->x - s1; double dy = local_q->gmcs[oi]->y - s2; double dz = local_q->gmcs[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌁  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     GMC\n", "GMC", local_q->gmcs[oi]->id + GALAXY_OBJECT_MIN_GMC, local_q->gmcs[oi]->x, local_q->gmcs[oi]->y, local_q->gmcs[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->interstellar_filament_count; oi++) {
        double dx = local_q->interstellar_filaments[oi]->x - s1; double dy = local_q->interstellar_filaments[oi]->y - s2; double dz = local_q->interstellar_filaments[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "〰️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Int Filament\n", "Int Filame", local_q->interstellar_filaments[oi]->id + GALAXY_OBJECT_MIN_INTERSTELLAR_FILAMENT, local_q->interstellar_filaments[oi]->x, local_q->interstellar_filaments[oi]->y, local_q->interstellar_filaments[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->interstellar_bubble_count; oi++) {
        double dx = local_q->interstellar_bubbles[oi]->x - s1; double dy = local_q->interstellar_bubbles[oi]->y - s2; double dz = local_q->interstellar_bubbles[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🫧  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Int Bubble\n", "Int Bubble", local_q->interstellar_bubbles[oi]->id + GALAXY_OBJECT_MIN_INTERSTELLAR_BUBBLE, local_q->interstellar_bubbles[oi]->x, local_q->interstellar_bubbles[oi]->y, local_q->interstellar_bubbles[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->bok_globule_count; oi++) {
        double dx = local_q->bok_globules[oi]->x - s1; double dy = local_q->bok_globules[oi]->y - s2; double dz = local_q->bok_globules[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "⚫  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Bok Globule\n", "Bok Globul", local_q->bok_globules[oi]->id + GALAXY_OBJECT_MIN_BOK_GLOBULE, local_q->bok_globules[oi]->x, local_q->bok_globules[oi]->y, local_q->bok_globules[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->clump_core_count; oi++) {
        double dx = local_q->clump_cores[oi]->x - s1; double dy = local_q->clump_cores[oi]->y - s2; double dz = local_q->clump_cores[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🪨  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Clump/Core\n", "Clump/Core", local_q->clump_cores[oi]->id + GALAXY_OBJECT_MIN_CLUMP_CORE, local_q->clump_cores[oi]->x, local_q->clump_cores[oi]->y, local_q->clump_cores[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->accretion_disk_count; oi++) {
        double dx = local_q->accretion_disks[oi]->x - s1; double dy = local_q->accretion_disks[oi]->y - s2; double dz = local_q->accretion_disks[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "💿  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Accretion Disk\n", "Accretion ", local_q->accretion_disks[oi]->id + GALAXY_OBJECT_MIN_ACCRETION_DISK, local_q->accretion_disks[oi]->x, local_q->accretion_disks[oi]->y, local_q->accretion_disks[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->relativistic_jet_count; oi++) {
        double dx = local_q->relativistic_jets[oi]->x - s1; double dy = local_q->relativistic_jets[oi]->y - s2; double dz = local_q->relativistic_jets[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "☄️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Relativ Jet\n", "Relativ Je", local_q->relativistic_jets[oi]->id + GALAXY_OBJECT_MIN_RELATIVISTIC_JET, local_q->relativistic_jets[oi]->x, local_q->relativistic_jets[oi]->y, local_q->relativistic_jets[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->shock_wave_count; oi++) {
        double dx = local_q->shock_waves[oi]->x - s1; double dy = local_q->shock_waves[oi]->y - s2; double dz = local_q->shock_waves[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "〰️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Shock Wave\n", "Shock Wave", local_q->shock_waves[oi]->id + GALAXY_OBJECT_MIN_SHOCK_WAVE, local_q->shock_waves[oi]->x, local_q->shock_waves[oi]->y, local_q->shock_waves[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->stellar_bow_shock_count; oi++) {
        double dx = local_q->stellar_bow_shocks[oi]->x - s1; double dy = local_q->stellar_bow_shocks[oi]->y - s2; double dz = local_q->stellar_bow_shocks[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌊  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Bow Shock\n", "Bow Shock", local_q->stellar_bow_shocks[oi]->id + GALAXY_OBJECT_MIN_STELLAR_BOW_SHOCK, local_q->stellar_bow_shocks[oi]->x, local_q->stellar_bow_shocks[oi]->y, local_q->stellar_bow_shocks[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->cosmic_void_count; oi++) {
        double dx = local_q->cosmic_voids[oi]->x - s1; double dy = local_q->cosmic_voids[oi]->y - s2; double dz = local_q->cosmic_voids[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "⬛  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Cosmic Void\n", "Cosmic Voi", local_q->cosmic_voids[oi]->id + GALAXY_OBJECT_MIN_COSMIC_VOID, local_q->cosmic_voids[oi]->x, local_q->cosmic_voids[oi]->y, local_q->cosmic_voids[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->cosmic_filament_count; oi++) {
        double dx = local_q->cosmic_filaments[oi]->x - s1; double dy = local_q->cosmic_filaments[oi]->y - s2; double dz = local_q->cosmic_filaments[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🕸️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Cosmic Fil\n", "Cosmic Fil", local_q->cosmic_filaments[oi]->id + GALAXY_OBJECT_MIN_COSMIC_FILAMENT, local_q->cosmic_filaments[oi]->x, local_q->cosmic_filaments[oi]->y, local_q->cosmic_filaments[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->event_horizon_count; oi++) {
        double dx = local_q->event_horizons[oi]->x - s1; double dy = local_q->event_horizons[oi]->y - s2; double dz = local_q->event_horizons[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🕳️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Event Horizon\n", "Event Hori", local_q->event_horizons[oi]->id + GALAXY_OBJECT_MIN_EVENT_HORIZON, local_q->event_horizons[oi]->x, local_q->event_horizons[oi]->y, local_q->event_horizons[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->kilonova_count; oi++) {
        double dx = local_q->kilonovae[oi]->x - s1; double dy = local_q->kilonovae[oi]->y - s2; double dz = local_q->kilonovae[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "✨  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Kilonova\n", "Kilonova", local_q->kilonovae[oi]->id + GALAXY_OBJECT_MIN_KILONOVA, local_q->kilonovae[oi]->x, local_q->kilonovae[oi]->y, local_q->kilonovae[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->grav_lens_count; oi++) {
        double dx = local_q->grav_lenses[oi]->x - s1; double dy = local_q->grav_lenses[oi]->y - s2; double dz = local_q->grav_lenses[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🔎  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Grav Lens\n", "Grav Lens", local_q->grav_lenses[oi]->id + GALAXY_OBJECT_MIN_GRAV_LENS, local_q->grav_lenses[oi]->x, local_q->grav_lenses[oi]->y, local_q->grav_lenses[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->grb_count; oi++) {
        double dx = local_q->grbs[oi]->x - s1; double dy = local_q->grbs[oi]->y - s2; double dz = local_q->grbs[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🎇  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     GRB\n", "GRB", local_q->grbs[oi]->id + GALAXY_OBJECT_MIN_GRB, local_q->grbs[oi]->x, local_q->grbs[oi]->y, local_q->grbs[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->grav_wave_count; oi++) {
        double dx = local_q->grav_waves[oi]->x - s1; double dy = local_q->grav_waves[oi]->y - s2; double dz = local_q->grav_waves[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "〰️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Grav Wave\n", "Grav Wave", local_q->grav_waves[oi]->id + GALAXY_OBJECT_MIN_GRAV_WAVE, local_q->grav_waves[oi]->x, local_q->grav_waves[oi]->y, local_q->grav_waves[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->protoplanetary_disk_count; oi++) {
        double dx = local_q->protoplanetary_disks[oi]->x - s1; double dy = local_q->protoplanetary_disks[oi]->y - s2; double dz = local_q->protoplanetary_disks[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🪐  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Protoplanetary\n", "Protoplane", local_q->protoplanetary_disks[oi]->id + GALAXY_OBJECT_MIN_PROTOPLANETARY_DISK, local_q->protoplanetary_disks[oi]->x, local_q->protoplanetary_disks[oi]->y, local_q->protoplanetary_disks[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->debris_disk_count; oi++) {
        double dx = local_q->debris_disks[oi]->x - s1; double dy = local_q->debris_disks[oi]->y - s2; double dz = local_q->debris_disks[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "💫  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Debris Disk\n", "Debris Dis", local_q->debris_disks[oi]->id + GALAXY_OBJECT_MIN_DEBRIS_DISK, local_q->debris_disks[oi]->x, local_q->debris_disks[oi]->y, local_q->debris_disks[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->planetesimal_count; oi++) {
        double dx = local_q->planetesimals[oi]->x - s1; double dy = local_q->planetesimals[oi]->y - s2; double dz = local_q->planetesimals[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🪨  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Planetesimal\n", "Planetesim", local_q->planetesimals[oi]->id + GALAXY_OBJECT_MIN_PLANETESIMAL, local_q->planetesimals[oi]->x, local_q->planetesimals[oi]->y, local_q->planetesimals[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->rogue_planet_count; oi++) {
        double dx = local_q->rogue_planets[oi]->x - s1; double dy = local_q->rogue_planets[oi]->y - s2; double dz = local_q->rogue_planets[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌑  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Rogue Planet\n", "Rogue Plan", local_q->rogue_planets[oi]->id + GALAXY_OBJECT_MIN_ROGUE_PLANET, local_q->rogue_planets[oi]->x, local_q->rogue_planets[oi]->y, local_q->rogue_planets[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->brown_dwarf_count; oi++) {
        double dx = local_q->brown_dwarfs[oi]->x - s1; double dy = local_q->brown_dwarfs[oi]->y - s2; double dz = local_q->brown_dwarfs[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🟤  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Brown Dwarf\n", "Brown Dwar", local_q->brown_dwarfs[oi]->id + GALAXY_OBJECT_MIN_BROWN_DWARF, local_q->brown_dwarfs[oi]->x, local_q->brown_dwarfs[oi]->y, local_q->brown_dwarfs[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->iso_count; oi++) {
        double dx = local_q->isos[oi]->x - s1; double dy = local_q->isos[oi]->y - s2; double dz = local_q->isos[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "☄️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Interst Obj\n", "Interst Ob", local_q->isos[oi]->id + GALAXY_OBJECT_MIN_ISO, local_q->isos[oi]->x, local_q->isos[oi]->y, local_q->isos[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->mag_reconn_count; oi++) {
        double dx = local_q->mag_reconns[oi]->x - s1; double dy = local_q->mag_reconns[oi]->y - s2; double dz = local_q->mag_reconns[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "⚡  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Mag Reconn\n", "Mag Reconn", local_q->mag_reconns[oi]->id + GALAXY_OBJECT_MIN_MAG_RECONN, local_q->mag_reconns[oi]->x, local_q->mag_reconns[oi]->y, local_q->mag_reconns[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->current_sheet_count; oi++) {
        double dx = local_q->current_sheets[oi]->x - s1; double dy = local_q->current_sheets[oi]->y - s2; double dz = local_q->current_sheets[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "〰️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Cur Sheet\n", "Cur Sheet", local_q->current_sheets[oi]->id + GALAXY_OBJECT_MIN_CURRENT_SHEET, local_q->current_sheets[oi]->x, local_q->current_sheets[oi]->y, local_q->current_sheets[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->heliosphere_count; oi++) {
        double dx = local_q->heliospheres[oi]->x - s1; double dy = local_q->heliospheres[oi]->y - s2; double dz = local_q->heliospheres[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌞  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Heliosphere\n", "Heliospher", local_q->heliospheres[oi]->id + GALAXY_OBJECT_MIN_HELIOSPHERE, local_q->heliospheres[oi]->x, local_q->heliospheres[oi]->y, local_q->heliospheres[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->term_shock_count; oi++) {
        double dx = local_q->term_shocks[oi]->x - s1; double dy = local_q->term_shocks[oi]->y - s2; double dz = local_q->term_shocks[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌊  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Term Shock\n", "Term Shock", local_q->term_shocks[oi]->id + GALAXY_OBJECT_MIN_TERM_SHOCK, local_q->term_shocks[oi]->x, local_q->term_shocks[oi]->y, local_q->term_shocks[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->magnetosphere_count; oi++) {
        double dx = local_q->magnetospheres[oi]->x - s1; double dy = local_q->magnetospheres[oi]->y - s2; double dz = local_q->magnetospheres[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🧲  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Magnetosphere\n", "Magnetosph", local_q->magnetospheres[oi]->id + GALAXY_OBJECT_MIN_MAGNETOSPHERE, local_q->magnetospheres[oi]->x, local_q->magnetospheres[oi]->y, local_q->magnetospheres[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->cosmic_string_count; oi++) {
        double dx = local_q->cosmic_strings[oi]->x - s1; double dy = local_q->cosmic_strings[oi]->y - s2; double dz = local_q->cosmic_strings[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🧵  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Cosmic String\n", "Cosmic Str", local_q->cosmic_strings[oi]->id + GALAXY_OBJECT_MIN_COSMIC_STRING, local_q->cosmic_strings[oi]->x, local_q->cosmic_strings[oi]->y, local_q->cosmic_strings[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->domain_wall_count; oi++) {
        double dx = local_q->domain_walls[oi]->x - s1; double dy = local_q->domain_walls[oi]->y - s2; double dz = local_q->domain_walls[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🧱  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Domain Wall\n", "Domain Wal", local_q->domain_walls[oi]->id + GALAXY_OBJECT_MIN_DOMAIN_WALL, local_q->domain_walls[oi]->x, local_q->domain_walls[oi]->y, local_q->domain_walls[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->dm_halo_count; oi++) {
        double dx = local_q->dm_halos[oi]->x - s1; double dy = local_q->dm_halos[oi]->y - s2; double dz = local_q->dm_halos[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "👻  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     DM Halo\n", "DM Halo", local_q->dm_halos[oi]->id + GALAXY_OBJECT_MIN_DM_HALO, local_q->dm_halos[oi]->x, local_q->dm_halos[oi]->y, local_q->dm_halos[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->igm_count; oi++) {
        double dx = local_q->igms[oi]->x - s1; double dy = local_q->igms[oi]->y - s2; double dz = local_q->igms[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌫️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     IGM\n", "IGM", local_q->igms[oi]->id + GALAXY_OBJECT_MIN_IGM, local_q->igms[oi]->x, local_q->igms[oi]->y, local_q->igms[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->cgm_count; oi++) {
        double dx = local_q->cgms[oi]->x - s1; double dy = local_q->cgms[oi]->y - s2; double dz = local_q->cgms[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌫️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     CGM\n", "CGM", local_q->cgms[oi]->id + GALAXY_OBJECT_MIN_CGM, local_q->cgms[oi]->x, local_q->cgms[oi]->y, local_q->cgms[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->lyman_alpha_count; oi++) {
        double dx = local_q->lyman_alphas[oi]->x - s1; double dy = local_q->lyman_alphas[oi]->y - s2; double dz = local_q->lyman_alphas[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌲  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Lyman Alpha\n", "Lyman Alph", local_q->lyman_alphas[oi]->id + GALAXY_OBJECT_MIN_LYMAN_ALPHA, local_q->lyman_alphas[oi]->x, local_q->lyman_alphas[oi]->y, local_q->lyman_alphas[oi]->z, d, h, m);
    }
    for (int oi=0; oi<local_q->cmb_count; oi++) {
        double dx = local_q->cmbs[oi]->x - s1; double dy = local_q->cmbs[oi]->y - s2; double dz = local_q->cmbs[oi]->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "📺  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     CMB\n", "CMB", local_q->cmbs[oi]->id + GALAXY_OBJECT_MIN_CMB, local_q->cmbs[oi]->x, local_q->cmbs[oi]->y, local_q->cmbs[oi]->z, d, h, m);
    }


    /* 7. Derelicts (Wrecks) */
    for (int d_idx = 0; d_idx < local_q->derelict_count; d_idx++) {
        NPCDerelict *de = local_q->derelicts[d_idx];
        double dx = de->x - s1;
        double dy = de->y - s2;
        double dz = de->z - s3;
        double d = sqrt(dx * dx + dy * dy + dz * dz);
        double h = atan2(dx, -dy) * 180 / M_PI;
        if (h < 0) h += 360;
        double m = (d > 0.001) ? asin(dz / d) * 180 / M_PI : 0;
        int deid = de->id + GALAXY_OBJECT_MIN_DERELICT;
        char status[64] = "";
        if (deid == locked_id) strncpy(status, RED "[LOCKED]" RESET, 63);
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🏚️  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     %s (%s Wreck) %s\n", "Wreck", deid, de->x, de->y, de->z, d, h, m, de->name, get_ship_class_name(de->ship_class), status);
    }
    /* 8. Mines */
    for (int m_idx = 0; m_idx < local_q->mine_count; m_idx++) {
        NPCMine *mi = local_q->mines[m_idx];
        double dx = mi->x - s1; double dy = mi->y - s2; double dz = mi->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "💣 %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Mine\n", "Mine", mi->id + GALAXY_OBJECT_MIN_MINE, mi->x, mi->y, mi->z, d, h, m);
    }
    /* 9. Platforms */
    for (int p_idx = 0; p_idx < local_q->platform_count; p_idx++) {
        NPCPlatform *pl = local_q->platforms[p_idx];
        double dx = pl->x - s1; double dy = pl->y - s2; double dz = pl->z - s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        double h = atan2(dx, -dy) * 180/M_PI; if(h<0) h+=360;
        double m = (d > 0.001) ? asin(dz/d) * 180/M_PI : 0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🏗️ %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Platform\n", "Platform", pl->id + GALAXY_OBJECT_MIN_PLATFORM, pl->x, pl->y, pl->z, d, h, m);
    }

    /* 8. Asteroids */
    for (int a_idx = 0; a_idx < local_q->asteroid_count; a_idx++) {
        NPCAsteroid *as = local_q->asteroids[a_idx];
        double dx = as->x - s1;
        double dy = as->y - s2;
        double dz = as->z - s3;
        double d = sqrt(dx * dx + dy * dy + dz * dz);
        double h = atan2(dx, -dy) * 180 / M_PI;
        if (h < 0) {
            h += 360;
        }
        double m = (d > 0.001) ? asin(dz / d) * 180 / M_PI : 0;
        int asid = as->id + GALAXY_OBJECT_MIN_ASTEROID;
        const char* res_names[] = {"None", "Aetherium", "Neo-Titanium", "Void-Essence", "Graphene", "Synaptics", "Nebular Gas", "Composite", "Dark-Matter"};
        const char* r_name = (as->resource_type >= 0 && as->resource_type <= 8) ? res_names[as->resource_type] : "Unknown";
        char status[64] = "";
        if (asid == locked_id) {
            strncpy(status, RED "[LOCKED]" RESET, 63);
        }
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🪨  %-10s %-5d [%.2f,%.2f,%.2f] %-5.2f %03.0ff / %+03.0ff     Asteroid (%s) %s\n", "Asteroid", asid, as->x, as->y, as->z, d, h, m, r_name, status);
    }

    send_server_msg(i, "TACTICAL", b);
    free(b);
}

void handle_lrs(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    if (players[i].state.system_health[2] < (THRESHOLD_SYS_DAMAGED - 10.0)) {
        send_server_msg(i, "COMPUTER", "LONG RANGE SENSOR FAILURE: Sub-space arrays offline.");
        return;
    }
    if (players[i].state.energy < (uint64_t)(COST_ACTION_MED / (uint64_t)RATIO_ENERGY_REDUCTION)) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for long-range sweep.");
        return;
    }
    players[i].state.energy -= (uint64_t)(COST_ACTION_MED / (uint64_t)RATIO_ENERGY_REDUCTION);

    int q1 = players[i].state.q1, q2 = players[i].state.q2, q3 = players[i].state.q3;
    
    for (int dq3 = 1; dq3 >= -1; dq3--) {
        int nq3 = q3 + dq3;
        if (nq3 < 1 || nq3 > GALAXY_SIZE) continue;
        
        for (int dq2 = -1; dq2 <= 1; dq2++) {
            for (int dq1 = -1; dq1 <= 1; dq1++) {
                int nq1 = q1 + dq1, nq2 = q2 + dq2;
                if (nq1 < 1 || nq1 > GALAXY_SIZE || nq2 < 1 || nq2 > GALAXY_SIZE) continue;
                
                QuadrantIndex *lq = &spatial_index[nq1][nq2][nq3];
                
                /* Temporary large buffer for ALL objects in quadrant */
                NetObject *all_objs = malloc(sizeof(NetObject) * 16384);
                if (!all_objs) continue; /* OOM: skip this quadrant */
                int total_count = 0;

                #define ADD_LRS_OBJ(arr, count, min_id, t_val)                     for(int n=0; n<count && total_count < 16384; n++) {                         if(arr[n]->active) {                             all_objs[total_count++] = (NetObject){.net_x = arr[n]->x, .net_y = arr[n]->y, .net_z = arr[n]->z, .type = t_val, .id = arr[n]->id + min_id, .active = 1};                         }                     }

                ADD_LRS_OBJ(lq->npcs, lq->npc_count, GALAXY_OBJECT_MIN_NPC, 10);
                ADD_LRS_OBJ(lq->stars, lq->star_count, GALAXY_OBJECT_MIN_STAR, 4);
                ADD_LRS_OBJ(lq->planets, lq->planet_count, GALAXY_OBJECT_MIN_PLANET, 5);
                /* star system: ids are already the full ids of the system bands; the neighbours' bodies move
                 * too, so their positions are refreshed here (cheap: at most 21 bodies per quadrant) */
                if (lq->sys_star_count > 0 && lq->sys_stars[0]) sys_refresh_positions((int)(lq->sys_stars[0] - sys_stars), sys_clock_s());
                ADD_LRS_OBJ(lq->sys_stars, lq->sys_star_count, 0, 4);
                ADD_LRS_OBJ(lq->sys_planets, lq->sys_planet_count, 0, 5);
                for (int sm_ = 0; sm_ < lq->sys_moon_count && total_count < 16384; sm_++) {
                    NPCSysMoon *mo_ = lq->sys_moons[sm_];
                    if (mo_ && mo_->active)
                        all_objs[total_count++] = (NetObject){.net_x = mo_->x, .net_y = mo_->y, .net_z = mo_->z, .type = mo_->moon_type, .id = mo_->id, .active = 1};
                }
                ADD_LRS_OBJ(lq->bases, lq->base_count, GALAXY_OBJECT_MIN_STARBASE, 3);
                ADD_LRS_OBJ(lq->black_holes, lq->bh_count, GALAXY_OBJECT_MIN_BLACKHOLE, 6);
                ADD_LRS_OBJ(lq->nebulas, lq->nebula_count, GALAXY_OBJECT_MIN_NEBULA, 50);
                ADD_LRS_OBJ(lq->pulsars, lq->pulsar_count, GALAXY_OBJECT_MIN_PULSAR, 28);
                ADD_LRS_OBJ(lq->quasars, lq->quasar_count, GALAXY_OBJECT_MIN_QUASAR, 29);
                ADD_LRS_OBJ(lq->comets, lq->comet_count, GALAXY_OBJECT_MIN_COMET, 20);
                ADD_LRS_OBJ(lq->asteroids, lq->asteroid_count, GALAXY_OBJECT_MIN_ASTEROID, 21);
                ADD_LRS_OBJ(lq->derelicts, lq->derelict_count, GALAXY_OBJECT_MIN_DERELICT, 10);
                ADD_LRS_OBJ(lq->void_crystals, lq->void_crystal_count, GALAXY_OBJECT_MIN_VOID_CRYSTAL, 49);
                ADD_LRS_OBJ(lq->diffuse_nebulae, lq->diffuse_nebula_count, GALAXY_OBJECT_MIN_DIFFUSE_NEBULA, 51);
                ADD_LRS_OBJ(lq->dark_nebulae, lq->dark_nebula_count, GALAXY_OBJECT_MIN_DARK_NEBULA, 52);
                ADD_LRS_OBJ(lq->planetary_nebulae, lq->planetary_nebula_count, GALAXY_OBJECT_MIN_PLANETARY_NEBULA, 53);
                ADD_LRS_OBJ(lq->snrs, lq->snr_count, GALAXY_OBJECT_MIN_SNR, 54);
                ADD_LRS_OBJ(lq->gmcs, lq->gmc_count, GALAXY_OBJECT_MIN_GMC, 55);
                ADD_LRS_OBJ(lq->interstellar_filaments, lq->interstellar_filament_count, GALAXY_OBJECT_MIN_INTERSTELLAR_FILAMENT, 56);
                ADD_LRS_OBJ(lq->interstellar_bubbles, lq->interstellar_bubble_count, GALAXY_OBJECT_MIN_INTERSTELLAR_BUBBLE, 57);
                ADD_LRS_OBJ(lq->bok_globules, lq->bok_globule_count, GALAXY_OBJECT_MIN_BOK_GLOBULE, 58);
                ADD_LRS_OBJ(lq->clump_cores, lq->clump_core_count, GALAXY_OBJECT_MIN_CLUMP_CORE, 59);
                ADD_LRS_OBJ(lq->accretion_disks, lq->accretion_disk_count, GALAXY_OBJECT_MIN_ACCRETION_DISK, 60);
                ADD_LRS_OBJ(lq->relativistic_jets, lq->relativistic_jet_count, GALAXY_OBJECT_MIN_RELATIVISTIC_JET, 61);
                ADD_LRS_OBJ(lq->shock_waves, lq->shock_wave_count, GALAXY_OBJECT_MIN_SHOCK_WAVE, 62);
                ADD_LRS_OBJ(lq->stellar_bow_shocks, lq->stellar_bow_shock_count, GALAXY_OBJECT_MIN_STELLAR_BOW_SHOCK, 63);
                ADD_LRS_OBJ(lq->cosmic_voids, lq->cosmic_void_count, GALAXY_OBJECT_MIN_COSMIC_VOID, 64);
                ADD_LRS_OBJ(lq->cosmic_filaments, lq->cosmic_filament_count, GALAXY_OBJECT_MIN_COSMIC_FILAMENT, 65);
                ADD_LRS_OBJ(lq->event_horizons, lq->event_horizon_count, GALAXY_OBJECT_MIN_EVENT_HORIZON, 66);
                ADD_LRS_OBJ(lq->kilonovae, lq->kilonova_count, GALAXY_OBJECT_MIN_KILONOVA, 67);
                ADD_LRS_OBJ(lq->grav_lenses, lq->grav_lens_count, GALAXY_OBJECT_MIN_GRAV_LENS, 68);
                ADD_LRS_OBJ(lq->grbs, lq->grb_count, GALAXY_OBJECT_MIN_GRB, 69);
                ADD_LRS_OBJ(lq->grav_waves, lq->grav_wave_count, GALAXY_OBJECT_MIN_GRAV_WAVE, 70);
                ADD_LRS_OBJ(lq->protoplanetary_disks, lq->protoplanetary_disk_count, GALAXY_OBJECT_MIN_PROTOPLANETARY_DISK, 71);
                ADD_LRS_OBJ(lq->debris_disks, lq->debris_disk_count, GALAXY_OBJECT_MIN_DEBRIS_DISK, 72);
                ADD_LRS_OBJ(lq->planetesimals, lq->planetesimal_count, GALAXY_OBJECT_MIN_PLANETESIMAL, 73);
                ADD_LRS_OBJ(lq->rogue_planets, lq->rogue_planet_count, GALAXY_OBJECT_MIN_ROGUE_PLANET, 74);
                ADD_LRS_OBJ(lq->brown_dwarfs, lq->brown_dwarf_count, GALAXY_OBJECT_MIN_BROWN_DWARF, 75);
                ADD_LRS_OBJ(lq->isos, lq->iso_count, GALAXY_OBJECT_MIN_ISO, 76);
                ADD_LRS_OBJ(lq->mag_reconns, lq->mag_reconn_count, GALAXY_OBJECT_MIN_MAG_RECONN, 77);
                ADD_LRS_OBJ(lq->current_sheets, lq->current_sheet_count, GALAXY_OBJECT_MIN_CURRENT_SHEET, 78);
                ADD_LRS_OBJ(lq->heliospheres, lq->heliosphere_count, GALAXY_OBJECT_MIN_HELIOSPHERE, 79);
                ADD_LRS_OBJ(lq->term_shocks, lq->term_shock_count, GALAXY_OBJECT_MIN_TERM_SHOCK, 80);
                ADD_LRS_OBJ(lq->magnetospheres, lq->magnetosphere_count, GALAXY_OBJECT_MIN_MAGNETOSPHERE, 81);
                ADD_LRS_OBJ(lq->cosmic_strings, lq->cosmic_string_count, GALAXY_OBJECT_MIN_COSMIC_STRING, 82);
                ADD_LRS_OBJ(lq->domain_walls, lq->domain_wall_count, GALAXY_OBJECT_MIN_DOMAIN_WALL, 83);
                ADD_LRS_OBJ(lq->dm_halos, lq->dm_halo_count, GALAXY_OBJECT_MIN_DM_HALO, 84);
                ADD_LRS_OBJ(lq->igms, lq->igm_count, GALAXY_OBJECT_MIN_IGM, 85);
                ADD_LRS_OBJ(lq->cgms, lq->cgm_count, GALAXY_OBJECT_MIN_CGM, 86);
                ADD_LRS_OBJ(lq->lyman_alphas, lq->lyman_alpha_count, GALAXY_OBJECT_MIN_LYMAN_ALPHA, 87);
                ADD_LRS_OBJ(lq->cmbs, lq->cmb_count, GALAXY_OBJECT_MIN_CMB, 88);

                /* Send in chunks of MAX_NET_OBJECTS */
                int sent = 0;
                do {
                    PacketLRSUpdate pkt;
                    pkt.type = PKT_LRS_UPDATE;
                    pkt.q1 = nq1; pkt.q2 = nq2; pkt.q3 = nq3;
                    pkt.z_offset = dq3;
                    
                    int to_send = total_count - sent;
                    if(to_send > MAX_NET_OBJECTS) to_send = MAX_NET_OBJECTS;
                    
                    pkt.object_count = to_send;
                    if(to_send > 0) {
                        memcpy(pkt.objects, &all_objs[sent], to_send * sizeof(NetObject));
                    }
                    
                    sent += to_send;
                    
                    size_t packet_size = offsetof(PacketLRSUpdate, objects) + (pkt.object_count * sizeof(NetObject));
                    pthread_mutex_lock(&players[i].socket_mutex);
                    if (players[i].socket != 0) write_all(players[i].socket, &pkt, packet_size);
                    pthread_mutex_unlock(&players[i].socket_mutex);
                    
                } while(sent < total_count && total_count > 0);
                
                free(all_objs);
            }
        }
    }
    send_server_msg(i, "SCIENCE", "LRS Scan complete.");
}
