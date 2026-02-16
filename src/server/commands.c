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
#include <sys/socket.h>
#include "server_internal.h"
#include "game_config.h"
#include "ui.h"

/* Helper macro to safely append to a buffer with length check (used in handle_srs) */
#define SAFE_APPEND(buffer, max_len, format, ...) do { \
    size_t _cur_len = strlen(buffer); \
    if (_cur_len < (max_len)) { \
        snprintf((buffer) + _cur_len, (max_len) - _cur_len, format, ##__VA_ARGS__); \
    } \
} while(0)

/* Prototypes to avoid 'undeclared' errors */
void handle_nav(int i, const char *params);
void handle_imp(int i, const char *params);
void handle_pos(int i, const char *params);
void handle_jum(int i, const char *params);
void handle_apr(int i, const char *params);
void handle_cha(int i, const char *params);
void handle_srs(int i, const char *params);
void handle_lrs(int i, const char *params);
void handle_pha(int i, const char *params);
void handle_tor(int i, const char *params);
void handle_she(int i, const char *params);
void handle_lock(int i, const char *params);
void handle_enc(int i, const char *params);
void handle_pow(int i, const char *params);
void handle_psy(int i, const char *params);
void handle_scan(int i, const char *params);
void handle_clo(int i, const char *params);
void handle_bor(int i, const char *params);
void handle_dis(int i, const char *params);
void handle_min(int i, const char *params);
void handle_sco(int i, const char *params);
void handle_har(int i, const char *params);
void handle_doc(int i, const char *params);
void handle_con(int i, const char *params);
void handle_load(int i, const char *params);
void handle_rep(int i, const char *params);
void handle_fix(int i, const char *params);
void handle_sta(int i, const char *params);
void handle_inv(int i, const char *params);
void handle_dam(int i, const char *params);
void handle_cal(int i, const char *params);
void handle_ical(int i, const char *params);
void handle_who(int i, const char *params);
void handle_help(int i, const char *params);
void handle_aux(int i, const char *params);
void handle_und(int i, const char *params);
void handle_xxx(int i, const char *params);
void handle_hull(int i, const char *params);
void handle_supernova(int i, const char *params);
void handle_axs(int i, const char *params);
void handle_grd(int i, const char *params);
void handle_bridge(int i, const char *params);
void handle_map(int i, const char *params);
void handle_red(int i, const char *params);
void handle_orb(int i, const char *params);

/* Helper to check if a player is near a communication buoy (< DIST_BUOY_BOOST) */
static bool is_near_buoy(int i) {
    int q1=players[i].state.q1, q2=players[i].state.q2, q3=players[i].state.q3;
    double s1=players[i].state.s1, s2=players[i].state.s2, s3=players[i].state.s3;
    if (!IS_Q_VALID(q1, q2, q3)) return false;
    QuadrantIndex *lq = &spatial_index[q1][q2][q3];
    for (int bu_idx = 0; bu_idx < lq->buoy_count; bu_idx++) {
        NPCBuoy *bu = lq->buoys[bu_idx];
        double d_bu = sqrt(pow(s1 - bu->x, 2) + pow(s2 - bu->y, 2) + pow(s3 - bu->z, 2));
        if (d_bu < DIST_BUOY_BOOST) return true;
    }
    return false;
}



/* Type definition for command handlers */
typedef struct {
    const char *name;
    void (*handler)(int p_idx, const char *params);
    const char *description;
} CommandDef;

void normalize_upright(double *h, double *m) {
    *h = fmod(*h, 360.0); if (*h < 0) *h += 360.0;
    while (*m > 180.0) *m -= 360.0; 
    while (*m < -180.0) *m += 360.0;
    if (*m > 90.0) { *m = 180.0 - *m; *h = fmod(*h + 180.0, 360.0); }
    else if (*m < -90.0) { *m = -180.0 - *m; *h = fmod(*h + 180.0, 360.0); }
}

/* --- Command Handlers --- */

void handle_enc(int i, const char *params) {
    if (players[i].state.system_health[6] < 10.0) {
        send_server_msg(i, "COMPUTER", "CRYPTOGRAPHIC FAILURE: Logic core damaged. Encryption systems offline.");
        return;
    }

    int cost = 50;
    bool is_pqc = strstr(params, "pqc") || strstr(params, "kyber");
    if (is_pqc) {
        cost = 250;
        if (players[i].state.system_health[6] < 50.0) {
            send_server_msg(i, "SCIENCE", "Quantum Tunnel initialization failed. Computer integrity must be above 50% for PQC.");
            return;
        }
    }

    if (players[i].state.energy < cost) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for cryptographic frequency shift.");
        return;
    }

    bool valid = true;
    if (strstr(params, "aes")) {
        players[i].crypto_algo = CRYPTO_AES;
        send_server_msg(i, "COMPUTER", "Deep Space encryption: AES-256-GCM ACTIVE.");
    } else if (strstr(params, "chacha")) {
        players[i].crypto_algo = CRYPTO_CHACHA;
        send_server_msg(i, "COMPUTER", "Deep Space encryption: CHACHA20-POLY1305 ACTIVE.");
    } else if (strstr(params, "aria")) {
        players[i].crypto_algo = CRYPTO_ARIA;
        send_server_msg(i, "COMPUTER", "Deep Space encryption: ARIA-256-GCM ACTIVE.");
    } else if (strstr(params, "camellia")) {
        players[i].crypto_algo = CRYPTO_CAMELLIA;
        send_server_msg(i, "COMPUTER", "Deep Space encryption: CAMELLIA-256-CTR (Xylari) ACTIVE.");
    } else if (strstr(params, "seed")) {
        players[i].crypto_algo = CRYPTO_SEED;
        send_server_msg(i, "COMPUTER", "Deep Space encryption: SEED-CBC (ORION) ACTIVE.");
    } else if (strstr(params, "cast")) {
        players[i].crypto_algo = CRYPTO_CAST5;
        send_server_msg(i, "COMPUTER", "Deep Space encryption: CAST5-CBC (OLD REPUBLIC) ACTIVE.");
    } else if (strstr(params, "idea")) {
        players[i].crypto_algo = CRYPTO_IDEA;
        send_server_msg(i, "COMPUTER", "Deep Space encryption: IDEA-CBC (MAQUIS) ACTIVE.");
    } else if (strstr(params, "3des")) {
        players[i].crypto_algo = CRYPTO_3DES;
        send_server_msg(i, "COMPUTER", "Deep Space encryption: DES-EDE3-CBC (ANCIENT) ACTIVE.");
    } else if (strstr(params, "bf") || strstr(params, "blowfish")) {
        players[i].crypto_algo = CRYPTO_BLOWFISH;
        send_server_msg(i, "COMPUTER", "Deep Space encryption: BLOWFISH-CBC (GILDED) ACTIVE.");
    } else if (strstr(params, "rc4")) {
        players[i].crypto_algo = CRYPTO_RC4;
        send_server_msg(i, "COMPUTER", "Deep Space encryption: RC4-STREAM (TACTICAL) ACTIVE.");
    } else if (strstr(params, "des") && !strstr(params, "3des")) {
        players[i].crypto_algo = CRYPTO_DES;
        send_server_msg(i, "COMPUTER", "Deep Space encryption: DES-CBC (PRE-HYPERDRIVE) ACTIVE.");
    } else if (is_pqc) {
        players[i].crypto_algo = CRYPTO_PQC;
        send_server_msg(i, "COMPUTER", "Deep Space encryption: ML-KEM-1024 (POST-QUANTUM) ACTIVE.");
        send_server_msg(i, "SCIENCE", "Quantum Tunnel established. Signal is now immune to Shor's algorithm.");
    } else if (strstr(params, "off")) {
        players[i].crypto_algo = CRYPTO_NONE;
        send_server_msg(i, "COMPUTER", "WARNING: Encryption DISABLED. Signal is now RAW.");
        cost = 0;
    } else {
        send_server_msg(i, "COMPUTER", "Usage: enc aes | chacha | aria | camellia | seed | cast | idea | 3des | bf | rc4 | des | pqc | off");
        valid = false;
    }
    
    if (valid) players[i].state.energy -= cost;
}

void handle_pow(int i, const char *params) {
    double e, s, w;
    if (sscanf(params, "%lf %lf %lf", &e, &s, &w) == 3) {
        if (players[i].state.system_health[6] < 10.0) {
            send_server_msg(i, "ENGINEERING", "Power routing core FAILURE. Reallocation impossible.");
            return;
        }
                if (players[i].state.energy < 50) {
                    send_server_msg(i, "COMPUTER", "Insufficient energy for reactor bus reconfiguration.");
                    return;
                }
        
                /* 3. Reliability logic (Computer 10-50% health) */
                if (players[i].state.system_health[6] < 50.0 && (rand() % 100 < 30)) {
                    players[i].state.energy -= 25; /* Partial energy loss even on failure */
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
            players[i].state.energy -= 50;
            char msg[128];
            sprintf(msg, "Power distribution stabilized: E:%.0f%% S:%.0f%% W:%.0f%%.", 
                    players[i].state.power_dist[0]*100, players[i].state.power_dist[1]*100, players[i].state.power_dist[2]*100);
            send_server_msg(i, "ENGINEERING", msg);
        } else send_server_msg(i, "COMPUTER", "Invalid power distribution ratio.");
    } else send_server_msg(i, "COMPUTER", "Usage: pow <Engines> <Shields> <Weapons>");
}

/* Helper to calculate hyperdrive speed: Factor 9.9 = 10 units/sec (1 sec per quadrant) */
static double calculate_hyperdrive_speed(double factor) {
    /* Precise scaling: Factor 9.9 / 29.7 = 0.3333 units/tick.
       0.3333 * 30 ticks = 10 units/sec.
       Calibrated for nominal engine_mult = 1.0. */
    return factor / 29.7;
}

void handle_nav(int i, const char *params) {
    double h, m, w, factor = 6.0;
    int args = sscanf(params, "%lf %lf %lf %lf", &h, &m, &w, &factor);
    if (args >= 3) {
        if (players[i].state.system_health[0] < 50.0 || players[i].state.system_health[2] < 50.0) {
            send_server_msg(i, "ENGINEERING", "CRITICAL: Hyperdrive and Sensors must be above 50% integrity.");
            return;
        }
        if (players[i].state.energy < 5000 || players[i].state.inventory[1] < 1) {
            send_server_msg(i, "COMPUTER", "Insufficient resources for Hyperdrive (Req: 5000 Energy, 1 Aetherium).");
            return;
        }
        if (factor < 0.1) factor = 0.1; 
        if (factor > 9.9) factor = 9.9;
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
        players[i].target_gx = (players[i].state.q1-1)*10.0+players[i].state.s1+players[i].dx*w*10.0;
        players[i].target_gy = (players[i].state.q2-1)*10.0+players[i].state.s2+players[i].dy*w*10.0;
        players[i].target_gz = (players[i].state.q3-1)*10.0+players[i].state.s3+players[i].dz*w*10.0;
        
        players[i].hyper_speed = calculate_hyperdrive_speed(factor); 
        players[i].nav_state = NAV_STATE_ALIGN; 
        players[i].is_docked = 0;
        players[i].state.energy -= 5000;
        players[i].state.inventory[1] -= 1;
        double dh = players[i].target_h - players[i].state.van_h;
        while(dh>180) dh-=360; 
        while(dh<-180) dh+=360;
        players[i].nav_timer = (fabs(dh)<1.0 && fabs(players[i].target_m - players[i].state.van_m)<1.0) ? 10 : 60;
        players[i].pending_bor_type = players[i].nav_timer;
        char msg[128]; sprintf(msg, "Course plotted. Aligning for Hyperdrive %.1f.", factor);
        send_server_msg(i, "HELMSMAN", msg);
    } else send_server_msg(i, "COMPUTER", "Usage: nav <H> <M> <W> [Factor]");
}

void handle_imp(int i, const char *params) {
    double h, m, s, dist = -1.0;
    int args = sscanf(params, "%lf %lf %lf %lf", &h, &m, &s, &dist);
    if (players[i].state.system_health[1] < 10.0) {
        send_server_msg(i, "ENGINEERING", "Impulse drive system is CRITICAL.");
        return;
    }
    if (players[i].state.system_health[1] < 40.0 && (rand() % 100 > (int)players[i].state.system_health[1])) {
        send_server_msg(i, "ENGINEERING", "Impulse manifold pressure unstable. Engines failed to engage!");
        return;
    }
    if (players[i].state.energy < 100) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for impulse.");
        return;
    }
    if (args == 1) {
        players[i].hyper_speed = h / 50.0; if (players[i].hyper_speed > 0.2) players[i].hyper_speed = 0.2;
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
            
            char msg[64]; sprintf(msg, "Impulse adjusted to %.0f%%.", players[i].hyper_speed * 50.0);
            send_server_msg(i, "HELMSMAN", msg); players[i].nav_state = NAV_STATE_IMPULSE;
        }
        players[i].state.energy -= 50;
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
        
        players[i].hyper_speed = s / 50.0; 
        if (players[i].hyper_speed > 0.2) players[i].hyper_speed = 0.2;
        
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
        players[i].state.energy -= 100;
        players[i].is_docked = 0;
        double dh = players[i].target_h - players[i].state.van_h;
        while(dh>180) dh-=360; 
        while(dh<-180) dh+=360;
        players[i].nav_timer = (fabs(dh)<1.0 && fabs(players[i].target_m - players[i].state.van_m)<1.0) ? 10 : 60;
        players[i].pending_bor_type = players[i].nav_timer;
    } else send_server_msg(i, "COMPUTER", "Usage: imp <H> <M> <S> [Dist] or imp <S>");
}

void handle_pos(int i, const char *params) {
    double h, m;
    if (sscanf(params, "%lf %lf", &h, &m) == 2) {
        if (players[i].state.system_health[1] < 10.0) {
            send_server_msg(i, "ENGINEERING", "Attitude thrusters OFFLINE.");
            return;
        }
        if (players[i].state.energy < 20) {
            send_server_msg(i, "COMPUTER", "Insufficient energy for adjustment.");
            return;
        }
        normalize_upright(&h, &m);
        players[i].target_h = h; players[i].target_m = m;
        players[i].start_h = players[i].state.van_h; players[i].start_m = players[i].state.van_m;
        players[i].nav_state = NAV_STATE_ALIGN_ONLY; players[i].state.energy -= 20;
        double dh = players[i].target_h - players[i].state.van_h;
        while(dh>180) dh-=360; 
        while(dh<-180) dh+=360;
        players[i].nav_timer = (fabs(dh)<1.0 && fabs(players[i].target_m - players[i].state.van_m)<1.0) ? 10 : 60;
        players[i].pending_bor_type = players[i].nav_timer;
        send_server_msg(i, "HELMSMAN", "Ship re-orienting.");
    } else send_server_msg(i, "COMPUTER", "Usage: pos <H> <M>");
}

void handle_apr(int i, const char *params) {
    int tid = 0; double tdist = 2.0;
    int args = sscanf(params, " %d %lf", &tid, &tdist);
    
    /* 1. Integrity Check: Impulse (ID 1) and Computer (ID 6) >= 10% */
    if (players[i].state.system_health[1] < 10.0 || players[i].state.system_health[6] < 10.0) {
        send_server_msg(i, "COMPUTER", "AUTOPILOT FAILURE: Impulse thrusters or navigation core offline.");
        return;
    }

    if (args == 0) tid = players[i].state.lock_target;
    else if (args == 1 && tid < 100) { tdist = tid; tid = players[i].state.lock_target; }

    if (tid > 0) {
        /* 2. Energy Cost check */
        if (players[i].state.energy < 100) {
            send_server_msg(i, "COMPUTER", "Insufficient energy for autopilot engagement (Req: 100).");
            return;
        }

        double tx = 0, ty = 0, tz = 0; 
        bool found = false;
        int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
        char target_name[64] = "target";

        /* Extensive Target Resolution Logic - QUADRANT RESTRICTED */
        if (tid >= 1 && tid <= 32) {
            if (players[tid-1].active && players[tid-1].state.q1 == pq1 && players[tid-1].state.q2 == pq2 && players[tid-1].state.q3 == pq3) {
                /* 3. Cloak check */
                if (!players[tid-1].state.is_cloaked || players[tid-1].faction == players[i].faction) {
                    tx = players[tid-1].gx; ty = players[tid-1].gy; tz = players[tid-1].gz; found = true;
                    strncpy(target_name, players[tid-1].name, 63);
                }
            }
        } else if (tid >= 1000 && tid < 1000+MAX_NPC) {
            int idx = tid - 1000;
            if (npcs[idx].active && npcs[idx].q1 == pq1 && npcs[idx].q2 == pq2 && npcs[idx].q3 == pq3) {
                if (!npcs[idx].is_cloaked || npcs[idx].faction == players[i].faction) {
                    tx = npcs[idx].gx; ty = npcs[idx].gy; tz = npcs[idx].gz; found = true;
                    strncpy(target_name, get_species_name(npcs[idx].faction), 63);
                }
            }
        } else {
            /* 1. Local objects check (current quadrant only) */
            QuadrantIndex *lq = &spatial_index[pq1][pq2][pq3];
            if (tid >= 2000 && tid < 2000+MAX_BASES) { for(int b=0; b<lq->base_count; b++) if(lq->bases[b]->id+2000 == tid) { tx = (lq->bases[b]->q1-1)*10.0+lq->bases[b]->x; ty = (lq->bases[b]->q2-1)*10.0+lq->bases[b]->y; tz = (lq->bases[b]->q3-1)*10.0+lq->bases[b]->z; found = true; strcpy(target_name, "Starbase"); } }
            else if (tid >= 3000 && tid < 3000+MAX_PLANETS) { for(int p=0; p<lq->planet_count; p++) if(lq->planets[p]->id+3000 == tid) { tx = (lq->planets[p]->q1-1)*10.0+lq->planets[p]->x; ty = (lq->planets[p]->q2-1)*10.0+lq->planets[p]->y; tz = (lq->planets[p]->q3-1)*10.0+lq->planets[p]->z; found = true; strcpy(target_name, "Planet"); } }
            else if (tid >= 4000 && tid < 4000+MAX_STARS) { for(int s=0; s<lq->star_count; s++) if(lq->stars[s]->id+4000 == tid) { tx = (lq->stars[s]->q1-1)*10.0+lq->stars[s]->x; ty = (lq->stars[s]->q2-1)*10.0+lq->stars[s]->y; tz = (lq->stars[s]->q3-1)*10.0+lq->stars[s]->z; found = true; strcpy(target_name, "Star"); } }
            else if (tid >= 7000 && tid < 7000+MAX_BH) { for(int h=0; h<lq->bh_count; h++) if(lq->black_holes[h]->id+7000 == tid) { tx = (lq->black_holes[h]->q1-1)*10.0+lq->black_holes[h]->x; ty = (lq->black_holes[h]->q2-1)*10.0+lq->black_holes[h]->y; tz = (lq->black_holes[h]->q3-1)*10.0+lq->black_holes[h]->z; found = true; strcpy(target_name, "Black Hole"); } }
            else if (tid >= 10000 && tid < 10000+MAX_COMETS) { for(int c=0; c<lq->comet_count; c++) if(lq->comets[c]->id+10000 == tid) { tx = (lq->comets[c]->q1-1)*10.0+lq->comets[c]->x; ty = (lq->comets[c]->q2-1)*10.0+lq->comets[c]->y; tz = (lq->comets[c]->q3-1)*10.0+lq->comets[c]->z; found = true; strcpy(target_name, "Comet"); } }
            else if (tid >= 11000 && tid < 11000+MAX_DERELICTS) { for(int d=0; d<lq->derelict_count; d++) if(lq->derelicts[d]->id+11000 == tid) { tx = (lq->derelicts[d]->q1-1)*10.0+lq->derelicts[d]->x; ty = (lq->derelicts[d]->q2-1)*10.0+lq->derelicts[d]->y; tz = (lq->derelicts[d]->q3-1)*10.0+lq->derelicts[d]->z; found = true; strcpy(target_name, "Derelict"); } }
            else if (tid >= 12000 && tid < 12000+MAX_ASTEROIDS) { for(int a=0; a<lq->asteroid_count; a++) if(lq->asteroids[a]->id+12000 == tid) { tx = (lq->asteroids[a]->q1-1)*10.0+lq->asteroids[a]->x; ty = (lq->asteroids[a]->q2-1)*10.0+lq->asteroids[a]->y; tz = (lq->asteroids[a]->q3-1)*10.0+lq->asteroids[a]->z; found = true; strcpy(target_name, "Asteroid"); } }
            else if (tid >= 14000 && tid < 14000+MAX_MINES) { for(int m=0; m<lq->mine_count; m++) if(lq->mines[m]->id+14000 == tid) { tx = (lq->mines[m]->q1-1)*10.0+lq->mines[m]->x; ty = (lq->mines[m]->q2-1)*10.0+lq->mines[m]->y; tz = (lq->mines[m]->q3-1)*10.0+lq->mines[m]->z; found = true; strcpy(target_name, "Mine"); } }
            else if (tid >= 15000 && tid < 15000+MAX_BUOYS) { for(int b=0; b<lq->buoy_count; b++) if(lq->buoys[b]->id+15000 == tid) { tx = (lq->buoys[b]->q1-1)*10.0+lq->buoys[b]->x; ty = (lq->buoys[b]->q2-1)*10.0+lq->buoys[b]->y; tz = (lq->buoys[b]->q3-1)*10.0+lq->buoys[b]->z; found = true; strcpy(target_name, "Comm Buoy"); } }
            else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS) { for(int p=0; p<lq->platform_count; p++) if(lq->platforms[p]->id+16000 == tid) { tx = (lq->platforms[p]->q1-1)*10.0+lq->platforms[p]->x; ty = (lq->platforms[p]->q2-1)*10.0+lq->platforms[p]->y; tz = (lq->platforms[p]->q3-1)*10.0+lq->platforms[p]->z; found = true; strcpy(target_name, "Defense Platform"); } }
            else if (tid >= 17000 && tid < 17000+MAX_RIFTS) { for(int r=0; r<lq->rift_count; r++) if(lq->rifts[r]->id+17000 == tid) { tx = (lq->rifts[r]->q1-1)*10.0+lq->rifts[r]->x; ty = (lq->rifts[r]->q2-1)*10.0+lq->rifts[r]->y; tz = (lq->rifts[r]->q3-1)*10.0+lq->rifts[r]->z; found = true; strcpy(target_name, "Spatial Rift"); } }
            else if (tid >= 18000 && tid < 18000+MAX_MONSTERS) { for(int m=0; m<lq->monster_count; m++) if(lq->monsters[m]->id+18000 == tid) { tx = (lq->monsters[m]->q1-1)*10.0+lq->monsters[m]->x; ty = (lq->monsters[m]->q2-1)*10.0+lq->monsters[m]->y; tz = (lq->monsters[m]->q3-1)*10.0+lq->monsters[m]->z; found = true; strcpy(target_name, "Monster"); } }
            else if (tid >= 19000 && tid < 19200) {
                int p_idx = (tid - 19000) / 3;
                int pr_idx = (tid - 19000) % 3;
                if (p_idx < MAX_CLIENTS && players[p_idx].state.probes[pr_idx].active) {
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

        if (found) {
            double cx = players[i].gx, cy = players[i].gy, cz = players[i].gz;
            double dx = tx - cx, dy = ty - cy, dz = tz - cz; double d = sqrt(dx*dx + dy*dy + dz*dz);
            if (d > (tdist + 0.01)) {
                players[i].state.energy -= 100;
                double h = atan2(dx, -dy) * 180.0 / M_PI; if(h < 0) h += 360; 
                double m = asin(dz/d) * 180.0 / M_PI;
                players[i].target_h = h; players[i].target_m = m;
                players[i].dx = dx/d; players[i].dy = dy/d; players[i].dz = dz/d;
                players[i].target_gx = cx + players[i].dx * (d - tdist); 
                players[i].target_gy = cy + players[i].dy * (d - tdist); 
                players[i].target_gz = cz + players[i].dz * (d - tdist);
                players[i].approach_dist = tdist;
                players[i].apr_target = tid;
                players[i].nav_state = NAV_STATE_ALIGN; players[i].nav_timer = 60; 
                players[i].pending_bor_type = 60; /* Store initial timer for smooth LERP */
                players[i].start_h = players[i].state.van_h; players[i].start_m = players[i].state.van_m;
                
                /* 4. Improved Feedback */
                char msg[128];
                sprintf(msg, "Autopilot engaged. Approaching %s at %.1f sector units.", target_name, tdist);
                send_server_msg(i, "HELMSMAN", msg);
            } else send_server_msg(i, "COMPUTER", "Target already in range.");
        } else send_server_msg(i, "COMPUTER", "Target not found or obscured (Targets for 'apr' must be in current quadrant).");
    } else send_server_msg(i, "COMPUTER", "Usage: apr <ID> [DIST]");
}

void handle_xxx(int i, const char *params) {
    players[i].active = 0; 
    spawn_derelict(players[i].state.q1, players[i].state.q2, players[i].state.q3, players[i].state.s1, players[i].state.s2, players[i].state.s3, players[i].faction, players[i].ship_class, players[i].name);
    send_server_msg(i, "COMPUTER", "Self-destruct sequence complete. BOOM.");
}

void handle_cha(int i, const char *params) {
    if (players[i].state.system_health[1] < 10.0 || players[i].state.system_health[6] < 10.0) { send_server_msg(i, "COMPUTER", "CHASE FAILURE."); return; }
    if (players[i].state.energy < 150) { send_server_msg(i, "COMPUTER", "Insufficient energy."); return; }
    int tid = players[i].state.lock_target;
    if (tid > 0) {
        players[i].state.energy -= 150; 
        players[i].nav_state = NAV_STATE_CHASE;
        players[i].hyper_speed = 0; /* Reset speed to prevent initial overshoot */
        char msg[64];
        snprintf(msg, sizeof(msg), "Chase mode engaged for target ID %d.", tid);
        send_server_msg(i, "HELMSMAN", msg);
    } else send_server_msg(i, "COMPUTER", "No target locked.");
}

/* Helper to get sensor error based on system health */
static double get_sensor_error(int p_idx) {
    if (is_near_buoy(p_idx)) return 0.0; /* Buoy provides perfect sensor calibration */
    double health = players[p_idx].state.system_health[2];
    if (health >= 100.0) return 0.0;
    /* Noise increases exponentially as health drops */
    double noise_factor = pow(1.0 - (health / 100.0), 2.0);
    return ((rand() % 2000 - 1000) / 1000.0) * noise_factor * 2.5;
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
        case SHIP_CLASS_GENERIC_ALIEN: return "Vessel";
        default: return "Unknown";
    }
}

void handle_srs(int i, const char *params) {
    if (players[i].state.system_health[2] < 5.0) {
        send_server_msg(i, "COMPUTER", "SENSOR FAILURE: Primary arrays are non-functional.");
        return;
    }
    if (players[i].state.energy < 10) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for short-range pulse.");
        return;
    }
    players[i].state.energy -= 10;

    char *b = calloc(LARGE_DATA_BUFFER, sizeof(char));
    if (!b) return;

    int q1=players[i].state.q1, q2=players[i].state.q2, q3=players[i].state.q3; 
    double s1=players[i].state.s1, s2=players[i].state.s2, s3=players[i].state.s3;

    snprintf(b, LARGE_DATA_BUFFER, CYAN "\n--- SHORT RANGE SENSOR ANALYSIS ---" RESET "\nQUADRANT: [%d,%d,%d] | SECTOR: [%.1f,%.1f,%.1f]\n", q1, q2, q3, s1, s2, s3);
    
    /* Improved Feedback: Nebula and Ion Storm detection */
    extern bool is_player_in_nebula(int i);
    if (is_player_in_nebula(i)) {
        strcat(b, YELLOW "WARNING: High-density nebular interference detected. Sensor range and accuracy degraded.\n" RESET);
    }
    long long g_val = spacegl_master.g[q1][q2][q3];
    if (g_val > 0 && (g_val / 10000000LL) % 10) {
        strcat(b, RED "CRITICAL: Ion Storm in progress! Sub-space telemetry heavily scrambled.\n" RESET);
    }

    snprintf(b+strlen(b), LARGE_DATA_BUFFER-strlen(b), "ENERGY: %d | TORPEDOES: %d | STATUS: %s\n", players[i].state.energy, players[i].state.torpedoes, players[i].state.is_cloaked ? MAGENTA "CLOAKED" RESET : GREEN "NORMAL" RESET);
    strcat(b, "\nTYPE       ID    POSITION      DIST   H / M         DETAILS\n");

    QuadrantIndex *local_q = &spatial_index[q1][q2][q3];
    int locked_id = players[i].state.lock_target;
    bool chasing = (players[i].nav_state == NAV_STATE_CHASE);
    double sensor_h = players[i].state.system_health[2];

    /* 1. Players */
    for(int j=0; j<local_q->player_count; j++) {
        ConnectedPlayer *p = local_q->players[j]; if (p == &players[i] || p->state.is_cloaked) continue;
        if (sensor_h < 30.0 && (rand()%100 > sensor_h + 50)) continue;

        double dx=p->state.s1-s1 + get_sensor_error(i), dy=p->state.s2-s2 + get_sensor_error(i), dz=p->state.s3-s3 + get_sensor_error(i); 
        double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=asin(dz/d)*180/M_PI;
        int pid = (int)(p-players)+1;
        char status[64] = "";
        if (pid == locked_id) { strcat(status, RED "[LOCKED]" RESET); if(chasing) strcat(status, B_RED "[CHASE]" RESET); }
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🚀 %-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0ff / %+03.0ff     %s (Player) [E:%d] %s\n", "Vessel", pid, p->state.s1 + get_sensor_error(i), p->state.s2 + get_sensor_error(i), p->state.s3 + get_sensor_error(i), d, h, m, p->name, p->state.energy, status);
    }

    /* 2. NPC Ships */
    for(int n=0; n<local_q->npc_count; n++) {
        NPCShip *npc = local_q->npcs[n];
        if (sensor_h < 30.0 && (rand()%100 > sensor_h + 50)) continue;

        double dx=npc->x-s1 + get_sensor_error(i), dy=npc->y-s2 + get_sensor_error(i), dz=npc->z-s3 + get_sensor_error(i); 
        double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=asin(dz/d)*180/M_PI;
        int nid = npc->id+1000;
        char status[64] = "";
        if (nid == locked_id) { strcat(status, RED "[LOCKED]" RESET); if(chasing) strcat(status, B_RED "[CHASE]" RESET); }
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "⚔️  %-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0ff / %+03.0ff     %s (%s) [E:%d] [Engines:%.0f%%] %s\n", "Vessel", nid, npc->x + get_sensor_error(i), npc->y + get_sensor_error(i), npc->z + get_sensor_error(i), d, h, m, get_species_name(npc->faction), npc->name, npc->energy, npc->engine_health, status);
    }

    /* 3. Starbases */
    for(int b_idx=0; b_idx<local_q->base_count; b_idx++) {
        NPCBase *ba = local_q->bases[b_idx];
        double dx=ba->x-s1, dy=ba->y-s2, dz=ba->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int baid = ba->id+2000;
        char status[64] = "";
        if (baid == locked_id) { strcat(status, RED "[LOCKED]" RESET); }
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🛰️  %-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0ff / %+03.0ff     Alliance Starbase %s\n", "Starbase", baid, ba->x, ba->y, ba->z, d, h, m, status);
    }

    /* 4. Planets */
    for(int p_idx=0; p_idx<local_q->planet_count; p_idx++) {
        NPCPlanet *pl = local_q->planets[p_idx];
        double dx=pl->x-s1, dy=pl->y-s2, dz=pl->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int plid = pl->id+3000;
        char status[64] = ""; if (plid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🪐 %-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0ff / %+03.0ff     Class-H Planet %s\n", "Planet", plid, pl->x, pl->y, pl->z, d, h, m, status);
    }

    /* 5. Stars */
    for(int s_idx=0; s_idx<local_q->star_count; s_idx++) {
        NPCStar *st = local_q->stars[s_idx];
        double dx=st->x-s1, dy=st->y-s2, dz=st->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int sid = st->id+4000;
        char status[64] = ""; if (sid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌟 %-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0ff / %+03.0ff     Star %s\n", "Star", sid, st->x, st->y, st->z, d, h, m, status);
    }

    /* 6. Anomalies (Black Holes, Nebulas, Pulsars, etc) */
    for(int h_idx=0; h_idx<local_q->bh_count; h_idx++) {
        NPCBlackHole *bh = local_q->black_holes[h_idx];
        double dx=bh->x-s1, dy=bh->y-s2, dz=bh->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double hh=atan2(dx,-dy)*180/M_PI; if(hh<0)hh+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🕳️  %-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0ff / %+03.0ff     Black Hole (Gravity Well)\n", "B-Hole", bh->id+7000, bh->x, bh->y, bh->z, d, hh, m);
    }
    for(int n_idx=0; n_idx<local_q->nebula_count; n_idx++) {
        NPCNebula *nb = local_q->nebulas[n_idx];
        double dx=nb->x-s1, dy=nb->y-s2, dz=nb->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        const char *neb_names[] = {"Standard", "High-Energy", "Dark Matter", "Ionic", "Gravimetric", "Temporal"};
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🌫️  %-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0ff / %+03.0ff     %s Nebula\n", "Nebula", nb->id+8000, nb->x, nb->y, nb->z, d, h, m, (nb->type>=0&&nb->type<6)?neb_names[nb->type]:"Unknown");
    }

    /* 7. Derelicts (Wrecks) */
    for(int d_idx=0; d_idx<local_q->derelict_count; d_idx++) {
        NPCDerelict *de = local_q->derelicts[d_idx];
        double dx=de->x-s1, dy=de->y-s2, dz=de->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int deid = de->id+11000;
        char status[64] = ""; if (deid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "🏚️  %-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0ff / %+03.0ff     %s (%s Wreck) %s\n", "Wreck", deid, de->x, de->y, de->z, d, h, m, de->name, get_ship_class_name(de->ship_class), status);
    }

    send_server_msg(i, "TACTICAL", b);
    free(b);
}

void handle_lrs(int i, const char *params) {
    if (players[i].state.system_health[2] < 15.0) {
        send_server_msg(i, "COMPUTER", "LONG RANGE SENSOR FAILURE: Sub-space arrays offline.");
        return;
    }
    if (players[i].state.energy < 25) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for long-range sweep.");
        return;
    }
    players[i].state.energy -= 25;

    char *b = calloc(LARGE_DATA_BUFFER, sizeof(char));
    if (!b) return;

    int q1=players[i].state.q1, q2=players[i].state.q2, q3=players[i].state.q3;
    snprintf(b, LARGE_DATA_BUFFER, B_CYAN "\n.--- LCARS LONG RANGE TACTICAL SENSORS --------------------------------------.\n" RESET);
    if (players[i].state.system_health[2] < 50.0) {
        strcat(b, YELLOW " WARNING: Sensor integrity degraded. Deep space telemetry unstable.\n" RESET);
    }
    char status[256];
    sprintf(status, WHITE " POS: [%d,%d,%d] SECTOR: [%.1f,%.1f,%.1f] | HDG: %03.0ff MRK: %+03.0f\n" RESET, 
            q1, q2, q3, players[i].state.s1, players[i].state.s2, players[i].state.s3, players[i].state.van_h, players[i].state.van_m);
    strcat(b, status);
    strcat(b, B_CYAN "'------------------------------------------------------------------------------'\n" RESET);
    strcat(b, " DATA: [ H:B-Hole P:Planet N:NPC B:Base S:Star ] Symbols: ~:*+#!M>\n\n");

    bool sensor_boost = is_near_buoy(i);
    int scan_range = sensor_boost ? 2 : 1;

    for (int dq3 = scan_range; dq3 >= -scan_range; dq3--) {
        int nq3 = q3 + dq3;
        if (nq3 < 1 || nq3 > 10) continue;

        char header[128];
        sprintf(header, B_YELLOW "[ DEPTH ZONE Z:%d ]" RESET "\n  QUADRANT      NAV (H/M/W)    OBJECTS [H P N B S]  ANOMALIES\n", nq3);
        strcat(b, header);

        for (int dq2 = -scan_range; dq2 <= scan_range; dq2++) {
            for (int dq1 = -scan_range; dq1 <= scan_range; dq1++) {
                int nq1 = q1 + dq1, nq2 = q2 + dq2;
                if (IS_Q_VALID(nq1, nq2, nq3)) {
                    long long v = spacegl_master.g[nq1][nq2][nq3];
                    double sensor_h = players[i].state.system_health[2];
                    if (sensor_h < 50.0 && (rand()%100 > sensor_h)) v = (v / 10) + (rand()%9);

                    int s=v%10, b_cnt=(v/10)%10, k=(v/100)%10, p=(v/1000)%10, bh=(v/10000)%10;
                    int neb=(v/100000)%10, pul=(v/1000000)%10, storm=(v/10000000LL)%10, com=(v/100000000)%10, ast=(v/1000000000)%10;
                    int mon=(v/10000000000000000LL)%10, u=(v/1000000000000000LL)%10, rift=(v/100000000000000LL)%10;
                    int vessels = k + u;
                    if (nq1 == q1 && nq2 == q2 && nq3 == q3 && vessels > 0) vessels--;

                    double dx = (nq1 - q1) * 10.0, dy = (nq2 - q2) * 10.0, dz = (nq3 - q3) * 10.0;
                    double dist = sqrt(dx*dx + dy*dy + dz*dz);
                    double h_v = 0, m_v = 0;
                    if (dist > 0.01) { h_v = atan2(dx, -dy) * 180.0 / M_PI; if(h_v < 0) h_v += 360; m_v = asin(dz / dist) * 180.0 / M_PI; }

                    char nav_info[64], obj_info[128], an_info[32] = "";
                    if (nq1==q1 && nq2==q2 && nq3==q3) sprintf(nav_info, B_BLUE "[%2d,%2d,%d]" RESET "  *-CURRENT-* ", nq1, nq2, nq3);
                    else sprintf(nav_info, WHITE "[%2d,%2d,%d]" RESET "  %03.0f/%+03.0f/W%.1f ", nq1, nq2, nq3, h_v, m_v, dist/10.0);

                    sprintf(obj_info, "[%s %s %s %s %s" RESET "]", 
                        bh>0?MAGENTA"H":".", p>0?CYAN"P":".", vessels>0?RED"N":".", b_cnt>0?GREEN"B":".", s>0?YELLOW"S":".");
                    
                    if(neb>0) strcat(an_info, "~"); 
                    if(pul>0) strcat(an_info, "*"); 
                    if(storm>0) strcat(an_info, "!");
                    if(com>0) strcat(an_info, "+"); 
                    if(ast>0) strcat(an_info, "#"); 
                    if(mon>0) strcat(an_info, "M"); 
                    if(rift>0) strcat(an_info, ">");

                    char row[256]; sprintf(row, "  %s  %-18s  %-4s\n", nav_info, obj_info, an_info);
                    strcat(b, row);
                }
            }
        }
        strcat(b, "\n");
    }
    strcat(b, B_CYAN "'------------------------------------------------------------------------------'\n" RESET);
    send_server_msg(i, "SCIENCE", b);
    free(b);
}

void handle_pha(int i, const char *params) {
    int e, tid; 
    int args = sscanf(params, " %d %d", &tid, &e);
    
    if (args == 1) {
        e = tid;
        tid = players[i].state.lock_target;
        if (tid == 0) {
            send_server_msg(i, "COMPUTER", "No target locked. Usage: pha <ID> <E>.");
            return;
        }
    } else if (args != 2) {
        send_server_msg(i, "COMPUTER", "Usage: pha <ID> <E>.");
        return;
    }

    /* 1. Minimum Integrity Check (ID 4) */
    if (players[i].state.system_health[4] < 10.0) {
        send_server_msg(i, "TACTICAL", "Ion Beam banks OFFLINE. Repair required.");
        return;
    }

    if (players[i].state.energy < e) { send_server_msg(i, "COMPUTER", "Insufficient energy."); return; }
    if (players[i].state.ion_beam_charge < 10.0) { send_server_msg(i, "TACTICAL", "Banks recharging."); return; }
    if (players[i].state.is_cloaked) { send_server_msg(i, "TACTICAL", "Cannot fire while cloaked."); return; }

    /* 2. Miss Probability based on Sensor health (ID 2) */
    double sensor_h = players[i].state.system_health[2];
    if (sensor_h < 50.0 && (rand() % 100 > (int)(sensor_h + 25))) {
        players[i].state.energy -= (e / 2);
        players[i].state.ion_beam_charge -= 10.0;
        send_server_msg(i, "TACTICAL", "Target lock failed due to sensor noise. Shot missed!");
        return;
    }

    /* 3. Overheating Risk */
    if (e > 10000 && (rand() % 100 < 5)) {
        players[i].state.system_health[4] -= 2.5;
        send_server_msg(i, "ENGINEERING", "Ion Beam banks overheating! Minor hardware damage.");
    }

    players[i].state.energy -= e;
    players[i].state.ion_beam_charge -= 15.0;
    
    double tx, ty, tz; bool found = false;
    int pq1=players[i].state.q1, pq2=players[i].state.q2, pq3=players[i].state.q3;
    
    if (tid >= 1 && tid <= 32 && players[tid-1].active && players[tid-1].state.q1 == pq1 && players[tid-1].state.q2 == pq2 && players[tid-1].state.q3 == pq3) { tx=players[tid-1].state.s1; ty=players[tid-1].state.s2; tz=players[tid-1].state.s3; found=true; }
    else if (tid >= 1000 && tid < 1000+MAX_NPC && npcs[tid-1000].active && npcs[tid-1000].q1 == pq1 && npcs[tid-1000].q2 == pq2 && npcs[tid-1000].q3 == pq3) { tx=npcs[tid-1000].x; ty=npcs[tid-1000].y; tz=npcs[tid-1000].z; found=true; }
    else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS && platforms[tid-16000].active && platforms[tid-16000].q1 == pq1 && platforms[tid-16000].q2 == pq2 && platforms[tid-16000].q3 == pq3) { tx=platforms[tid-16000].x; ty=platforms[tid-16000].y; tz=platforms[tid-16000].z; found=true; }
    else if (tid >= 18000 && tid < 18000+MAX_MONSTERS && monsters[tid-18000].active && monsters[tid-18000].q1 == pq1 && monsters[tid-18000].q2 == pq2 && monsters[tid-18000].q3 == pq3) { tx=monsters[tid-18000].x; ty=monsters[tid-18000].y; tz=monsters[tid-18000].z; found=true; }
    else if (tid >= 19000 && tid < 19200) {
        int p_idx = (tid - 19000) / 3;
        int pr_idx = (tid - 19000) % 3;
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
        double dx=tx-players[i].state.s1, dy=ty-players[i].state.s2, dz=tz-players[i].state.s3; 
        double dist=sqrt(dx*dx+dy*dy+dz*dz); if (dist < 0.1) dist = 0.1;
        double weapon_mult = 0.5 + (players[i].state.power_dist[2] * 2.5);
        int hit = (int)((e / dist) * (players[i].state.system_health[4] / 100.0) * weapon_mult);
        
        players[i].state.beam_count = 1; 
        players[i].state.beams[0] = (NetBeam){players[i].state.s1, players[i].state.s2, players[i].state.s3, tx, ty, tz, 1};
        
        if (tid <= 32) {
            ConnectedPlayer *target = &players[tid-1];
            /* Directional Shield Logic */
            double rel_dx = players[i].state.s1 - target->state.s1;
            double rel_dy = players[i].state.s2 - target->state.s2;
            double angle = atan2(rel_dx, -rel_dy) * 180.0 / M_PI; if (angle < 0) angle += 360;
            double rel_angle = angle - target->state.van_h;
            while (rel_angle < 0) rel_angle += 360; 
            while (rel_angle >= 360) rel_angle -= 360;

            int s_idx = 0;
            if (rel_angle > 315 || rel_angle <= 45) s_idx = 0;      /* Front */
            else if (rel_angle > 45 && rel_angle <= 135) s_idx = 5; /* Right */
            else if (rel_angle > 135 && rel_angle <= 225) s_idx = 1;/* Rear */
            else s_idx = 4;                                        /* Left */

            int dmg_rem = hit;
            if (target->state.shields[s_idx] >= dmg_rem) { target->state.shields[s_idx] -= dmg_rem; dmg_rem = 0; }
            else { dmg_rem -= target->state.shields[s_idx]; target->state.shields[s_idx] = 0; }
            
            if (dmg_rem > 0) {
                apply_hull_damage(tid - 1, (dmg_rem / 100.0));
                target->state.energy -= dmg_rem / 2;
            }
            if (target->faction == players[i].faction) {
                players[i].renegade_timer = 18000;
                send_server_msg(i, "CRITICAL", "FRIENDLY FIRE! YOU ARE NOW A RENEGADE!");
            }
            if (target->state.hull_integrity <= 0 || target->state.energy <= 0) {
                target->death_timer = 30;
                target->state.boom = (NetPoint){target->state.s1,target->state.s2,target->state.s3,1};
            }
            send_server_msg(tid-1, "WARNING", "UNDER Ion Beam ATTACK!");
        } else if (tid >= 1000 && tid < 1000+MAX_NPC) {
            NPCShip *target = &npcs[tid-1000];
            int dmg_rem = hit;
            if (target->plating >= dmg_rem) { target->plating -= dmg_rem; dmg_rem = 0; }
            else { dmg_rem -= target->plating; target->plating = 0; }
            
            if (dmg_rem > 0) {
                target->health -= (dmg_rem / 10);
                /* Chance to damage internal systems (Engines) */
                if (rand() % 100 < 15) {
                    double sys_dmg = 5.0 + (rand() % 15);
                    target->engine_health -= sys_dmg;
                    if (target->engine_health < 0) target->engine_health = 0;
                    send_server_msg(i, "TACTICAL", "Hull impact! Target maneuvering capability degraded.");
                }
            }
            if (target->health <= 0 || target->energy <= 0) {
                target->death_timer = 30;
                players[i].state.boom = (NetPoint){target->x, target->y, target->z, 1};
                send_server_msg(i, "TACTICAL", "Target vessel neutralized.");
            }
        } else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS) {
            NPCPlatform *target = &platforms[tid-16000];
            int dmg_rem = hit;
            if (target->energy >= dmg_rem) { target->energy -= dmg_rem; dmg_rem = 0; }
            else { dmg_rem -= target->energy; target->energy = 0; }
            
            if (dmg_rem > 0) target->health -= (dmg_rem / 10);
            if (target->health <= 0) {
                target->active = 0; players[i].state.boom = (NetPoint){target->x, target->y, target->z, 1};
                send_server_msg(i, "TACTICAL", "Defense platform neutralized.");
            }
        } else if (tid >= 18000 && tid < 18000+MAX_MONSTERS) {
            NPCMonster *target = &monsters[tid-18000];
            target->health -= (hit / 10);
            if (target->health <= 0) {
                target->active = 0; players[i].state.boom = (NetPoint){target->x, target->y, target->z, 1};
                send_server_msg(i, "TACTICAL", "Anomaly neutralized.");
            }
        } else if (tid >= 19000 && tid < 19200) {
            int p_idx = (tid - 19000) / 3;
            int pr_idx = (tid - 19000) % 3;
            if (p_idx < MAX_CLIENTS && players[p_idx].state.probes[pr_idx].active) {
                players[p_idx].state.probes[pr_idx].active = 0;
                players[i].state.boom = (NetPoint){players[p_idx].state.probes[pr_idx].s1, players[p_idx].state.probes[pr_idx].s2, players[p_idx].state.probes[pr_idx].s3, 1};
                send_server_msg(i, "TACTICAL", "Probe neutralized.");
                if (p_idx != i) send_server_msg(p_idx, "WARNING", "Telemetry lost: Probe destroyed by external fire.");
            }
        }
        char msg[64]; sprintf(msg, "Ion Beams hit ID %d for %d effective damage.", tid, hit); send_server_msg(i, "TACTICAL", msg);
    } else send_server_msg(i, "COMPUTER", "Target out of range.");
}

void handle_tor(int i, const char *params) {
    if (players[i].state.system_health[5] < 50.0) { send_server_msg(i, "TACTICAL", "Torpedo tubes OFFLINE."); return; }
    if (players[i].torp_active) { send_server_msg(i, "TACTICAL", "Main firing sequence BUSY."); return; }
    
    /* Find next available tube in rotation */
    int tube = players[i].current_tube;
    if (players[i].tube_load_timers[tube] > 0) {
        send_server_msg(i, "TACTICAL", "Current tube is LOADING. Cycle to next available.");
        return;
    }

    if (players[i].state.is_cloaked) { send_server_msg(i, "TACTICAL", "Cannot fire while cloaked."); return; }
    if (players[i].state.energy < 250) { send_server_msg(i, "COMPUTER", "Insufficient energy."); return; }

    if(players[i].state.torpedoes > 0) {
        /* Misfire risk (Integrity 50-75%) */
        if (players[i].state.system_health[5] < 75.0 && (rand() % 100 > (int)players[i].state.system_health[5])) {
            players[i].state.torpedoes--;
            players[i].state.energy -= 100;
            players[i].tube_load_timers[tube] = 90; /* 3 seconds at 30fps */
            players[i].current_tube = (tube + 1) % 4;
            send_server_msg(i, "TACTICAL", "CRITICAL: Torpedo misfire! Warhead ejected.");
            return;
        }

        double h, m;
        bool manual = false;
        if (sscanf(params, "%lf %lf", &h, &m) == 2) {
            manual = true;
            normalize_upright(&h, &m);
        } else {
            h = players[i].state.van_h;
            m = players[i].state.van_m;
        }

        players[i].state.energy -= 250;
        players[i].state.torpedoes--; 
        players[i].torp_active = true;
        players[i].state.torp.active = 1;
        players[i].state.torp.net_x = players[i].state.s1;
        players[i].state.torp.net_y = players[i].state.s2;
        players[i].state.torp.net_z = players[i].state.s3;
        
        players[i].tube_load_timers[tube] = 90; /* 3 seconds reload for this tube */
        players[i].current_tube = (tube + 1) % 4; /* Rotate to next tube */
        
        players[i].torp_timeout = 300;
        players[i].torp_target = manual ? 0 : players[i].state.lock_target;
        
        double rad_h = h * M_PI / 180.0; double rad_m = m * M_PI / 180.0;
        players[i].tx = players[i].state.s1; players[i].ty = players[i].state.s2; players[i].tz = players[i].state.s3;
        players[i].tdx = cos(rad_m) * sin(rad_h); players[i].tdy = cos(rad_m) * -cos(rad_h); players[i].tdz = sin(rad_m);
        
        send_server_msg(i, "TACTICAL", manual ? "Torpedo away (Manual)." : (players[i].torp_target > 0 ? "Torpedo away (Locked)." : "Torpedo away (Boresight)."));
    } else send_server_msg(i, "TACTICAL", "Insufficient torpedoes.");
}

void handle_she(int i, const char *params) {
    int f, r, t, b, l, ri;
    if (sscanf(params, "%d %d %d %d %d %d", &f, &r, &t, &b, &l, &ri) == 6) {
        /* 1. Integrity Check (Shield System ID 8) */
        if (players[i].state.system_health[8] < 10.0) {
            send_server_msg(i, "ENGINEERING", "Shield generator OFFLINE. Grid reconfiguration impossible.");
            return;
        }

        /* 2. Basic cost and input cleaning */
        if (players[i].state.energy < 50) {
            send_server_msg(i, "COMPUTER", "Insufficient energy for grid reconfiguration pulse.");
            return;
        }

        if (f < 0) f = 0; 
        if (r < 0) r = 0; 
        if (t < 0) t = 0;
        if (b < 0) b = 0; 
        if (l < 0) l = 0; 
        if (ri < 0) ri = 0;

        /* 3. Cap values at 10,000 */
        if (f > 10000) f = 10000; 
        if (r > 10000) r = 10000; 
        if (t > 10000) t = 10000;
        if (b > 10000) b = 10000; 
        if (l > 10000) l = 10000; 
        if (ri > 10000) ri = 10000;

        /* 4. Reactor Consumption/Balance */
        int requested_total = f + r + t + b + l + ri;
        int current_total = 0;
        for (int s = 0; s < 6; s++) current_total += players[i].state.shields[s];

        int diff = requested_total - current_total;
        if (diff > 0) {
            /* Adding energy to shields */
            if (players[i].state.energy < (diff + 50)) {
                send_server_msg(i, "COMPUTER", "Insufficient reactor energy to charge grids to requested levels.");
                return;
            }
            players[i].state.energy -= (diff + 50);
        } else {
            /* Removing energy from shields: return 80% to reactor, 20% lost in flux */
            players[i].state.energy -= 50;
            players[i].state.energy += (int)(abs(diff) * 0.8);
            if (players[i].state.energy > 9999999) players[i].state.energy = 9999999;
        }

        players[i].state.shields[0] = f; players[i].state.shields[1] = r; players[i].state.shields[2] = t;
        players[i].state.shields[3] = b; players[i].state.shields[4] = l; players[i].state.shields[5] = ri;
        
        send_server_msg(i, "ENGINEERING", "Shield grids reconfigured and stabilized.");
    } else {
        send_server_msg(i, "COMPUTER", "Usage: she <F> <R> <T> <B> <L> <RI>");
    }
}

void handle_lock(int i, const char *params) {
    int tid;
    if (sscanf(params, " %d", &tid) == 1) {
        /* 1. Integrity Check (Sensors ID 2) */
        if (players[i].state.system_health[2] < 10.0) {
            send_server_msg(i, "TACTICAL", "Targeting arrays OFFLINE (Sensor damage).");
            return;
        }

        /* 2. Energy Cost */
        if (players[i].state.energy < 5) {
            send_server_msg(i, "COMPUTER", "Insufficient energy for active tracking.");
            return;
        }

        /* 3. Validation of Target Visibility and Range */
        bool found = false;
        int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
        
        if (tid >= 1 && tid <= 32) {
            /* Player Target */
            if (players[tid-1].active && players[tid-1].state.q1 == pq1 && players[tid-1].state.q2 == pq2 && players[tid-1].state.q3 == pq3) {
                if (!players[tid-1].state.is_cloaked || players[tid-1].faction == players[i].faction) found = true;
            }
        } else if (tid >= 1000 && tid < 1000+MAX_NPC) {
            /* NPC Target - Inter-sector aware */
            int idx = tid - 1000;
            if (npcs[idx].active) {
                if (!npcs[idx].is_cloaked) found = true;
            }
        } else {
            /* Check static objects in current quadrant or globally for IDs */
            if (tid >= 11000 && tid < 11000+MAX_DERELICTS) { 
                if (derelicts[tid-11000].active) found = true; 
            } else {
                QuadrantIndex *lq = &spatial_index[pq1][pq2][pq3];
                if (tid >= 2000 && tid < 2000+MAX_BASES) { for(int b=0; b<lq->base_count; b++) if(lq->bases[b]->id+2000 == tid) found = true; }
                else if (tid >= 3000 && tid < 3000+MAX_PLANETS) { for(int p=0; p<lq->planet_count; p++) if(lq->planets[p]->id+3000 == tid) found = true; }
                else if (tid >= 4000 && tid < 4000+MAX_STARS) { for(int s=0; s<lq->star_count; s++) if(lq->stars[s]->id+4000 == tid) found = true; }
                else if (tid >= 7000 && tid < 7000+MAX_BH) { for(int h=0; h<lq->bh_count; h++) if(lq->black_holes[h]->id+7000 == tid) found = true; }
                else if (tid >= 10000 && tid < 10000+MAX_COMETS) { for(int c=0; c<lq->comet_count; c++) if(lq->comets[c]->id+10000 == tid) found = true; }
                else if (tid >= 12000 && tid < 12000+MAX_ASTEROIDS) { for(int a=0; a<lq->asteroid_count; a++) if(lq->asteroids[a]->id+12000 == tid) found = true; }
                else if (tid >= 14000 && tid < 14000+MAX_MINES) { for(int m=0; m<lq->mine_count; m++) if(lq->mines[m]->id+14000 == tid) found = true; }
                else if (tid >= 15000 && tid < 15000+MAX_BUOYS) { for(int b=0; b<lq->buoy_count; b++) if(lq->buoys[b]->id+15000 == tid) found = true; }
                else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS) { for(int p=0; p<lq->platform_count; p++) if(lq->platforms[p]->id+16000 == tid) found = true; }
                else if (tid >= 17000 && tid < 17000+MAX_RIFTS) { for(int r=0; r<lq->rift_count; r++) if(lq->rifts[r]->id+17000 == tid) found = true; }
                else if (tid >= 18000 && tid < 18000+MAX_MONSTERS) { for(int m=0; m<lq->monster_count; m++) if(lq->monsters[m]->id+18000 == tid) found = true; }
                else if (tid >= 19000 && tid < 19200) {
                    int p_idx = (tid - 19000) / 3;
                    int pr_idx = (tid - 19000) % 3;
                    if (p_idx < MAX_CLIENTS && players[p_idx].state.probes[pr_idx].active) {
                        found = true;
                    }
                }
            }
        }

        if (found) {
            players[i].state.lock_target = tid;
            players[i].state.energy -= 5;
            send_server_msg(i, "TACTICAL", "Target locked and tracking.");
        } else {
            send_server_msg(i, "TACTICAL", "Unable to acquire lock. Target not detected.");
        }
    } else {
        players[i].state.lock_target = 0;
        send_server_msg(i, "TACTICAL", "Lock released.");
    }
}

void handle_scan(int i, const char *params) {
    int tid = 0;
    if (sscanf(params, " %d", &tid) != 1) {
        tid = players[i].state.lock_target;
    }
    /* Fallback to autopilot target if no lock/param */
    if (tid == 0) {
        tid = players[i].apr_target;
    }

    if (tid > 0) {
        if (players[i].state.system_health[2] < 20.0) {
            send_server_msg(i, "COMPUTER", "DEEP SCAN FAILURE: Sensor resolution insufficient (< 20%).");
            return;
        }
        if (players[i].state.energy < 20) {
            send_server_msg(i, "COMPUTER", "Insufficient energy for high-resolution scan.");
            return;
        }
        players[i].state.energy -= 20;

        char rep[4096]; memset(rep, 0, 4096);
        bool found = false;
        int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
        double sensor_h = players[i].state.system_health[2];
        bool scrambled = (sensor_h < 50.0 && (rand() % 100 > (int)sensor_h));

        if (tid >= 1 && tid <= 32) {
             ConnectedPlayer *t = &players[tid-1];
             if (t->active && t->state.q1 == pq1 && t->state.q2 == pq2 && t->state.q3 == pq3) {
                 found = true;
                 sprintf(rep, CYAN "\n--- SENSOR SCAN ANALYSIS: TARGET ID %d ---" RESET "\n", tid);
                 sprintf(rep+strlen(rep), "COMMANDER: %s | CLASS: %d\n", t->name, t->ship_class);
                 if (scrambled) strcat(rep, "HULL INTEGRITY: [SCRAMBLED]\nENERGY: ????\n");
                 else sprintf(rep + strlen(rep), "HULL INTEGRITY: %.1f%%\nENERGY: %d | CREW: %d | TORPS: %d\n", t->state.hull_integrity, t->state.energy, t->state.crew_count, t->state.torpedoes);
                 strcat(rep, BLUE "SYSTEMS STATUS:\n" RESET);
                 const char* sys[] = {"Hyperdrive", "Impulse", "Sensors", "Transp", "Ion Beams", "Torps", "Computer", "Life", "Shields", "Aux"};
                 for(int s=0; s<10; s++) {
                     char line[64];
                     if (scrambled && (rand()%100 > 50)) sprintf(line, " %-10s : [????]\n", sys[s]);
                     else sprintf(line, " %-10s : %.1f%%\n", sys[s], t->state.system_health[s]);
                     strcat(rep, line);
                 }
             }
        } else if (tid >= 1000 && tid < 1000+MAX_NPC) {
            int idx = tid - 1000;
            if (npcs[idx].active && npcs[idx].q1 == pq1 && npcs[idx].q2 == pq2 && npcs[idx].q3 == pq3) {
                found = true;
                sprintf(rep, CYAN "\n--- TACTICAL SCAN: NPC ID %d ---" RESET "\n", tid);
                sprintf(rep + strlen(rep), "SPECIES: %s\nENERGY: %d\nPROPULSION: %.1f%%\n", 
                    scrambled ? "UNKNOWN" : get_species_name(npcs[idx].faction), 
                    scrambled ? 0 : npcs[idx].energy, npcs[idx].engine_health);
            }
        } else {
            QuadrantIndex *lq = &spatial_index[pq1][pq2][pq3];
            if (tid >= 3000 && tid < 3000+MAX_PLANETS) {
                for(int p=0; p<lq->planet_count; p++) if(lq->planets[p]->id+3000 == tid) {
                    found = true;
                    sprintf(rep, GREEN "\n--- PLANETARY SURVEY ---" RESET "\nTYPE: Class-H Habitable\nRESERVES: %d units\n", scrambled ? 0 : lq->planets[p]->amount);
                }
            } else if (tid >= 4000 && tid < 4000+MAX_STARS) {
                for(int s=0; s<lq->star_count; s++) if(lq->stars[s]->id+4000 == tid) {
                    found = true;
                    sprintf(rep, YELLOW "\n--- STELLAR ANALYSIS ---" RESET "\nTYPE: Main Sequence G-Class Star\nADVISORY: Proximity scooping active (sco).\n");
                }
            } else if (tid >= 7000 && tid < 7000+MAX_BH) {
                for(int h=0; h<lq->bh_count; h++) if(lq->black_holes[h]->id+7000 == tid) {
                    found = true;
                    sprintf(rep, MAGENTA "\n--- SINGULARITY ANALYSIS ---" RESET "\nTYPE: Schwarzschild Black Hole\nADVISORY: Gravity well detected (< 3.0 units). Escape velocity required.\n");
                }
            } else if (tid >= 8000 && tid < 8000+MAX_NEBULAS) {
                for(int n=0; n<lq->nebula_count; n++) if(lq->nebulas[n]->id+8000 == tid) {
                    found = true;
                    const char *neb_desc[] = {
                        "Standard Class: Reduced sensor range and inhibited shield regeneration.",
                        "High-Energy Class: Volatile ionized gas. High risk of ignition.",
                        "Dark Matter Class: Severe sensor blindness and gravitational anomalies.",
                        "Ionic Class: Natural cloaking cover. Severe communication dampening.",
                        "Gravimetric Class: Tactical advantage due to deep space distortion.",
                        "Temporal Class: Unpredictable holographic ghosting on sensors."
                    };
                    sprintf(rep, BLUE "\n--- STELLAR PHENOMENON ANALYSIS ---" RESET "\n%s\n", (lq->nebulas[n]->type>=0&&lq->nebulas[n]->type<6)?neb_desc[lq->nebulas[n]->type]:"Unknown phenomena.");
                }
            } else if (tid >= 10000 && tid < 10000+MAX_COMETS) {
                for(int c=0; c<lq->comet_count; c++) if(lq->comets[c]->id+10000 == tid) {
                    found = true;
                    sprintf(rep, WHITE "\n--- COMET ANALYSIS ---" RESET "\nCOMPOSITION: Ice and Silicates\nVELOCITY: High-relative\nADVISORY: Proximity impact risk.\n");
                }
            } else if (tid >= 11000 && tid < 11000+MAX_DERELICTS) {
                for(int d=0; d<lq->derelict_count; d++) if(lq->derelicts[d]->id+11000 == tid) {
                    found = true;
                    sprintf(rep, WHITE "\n--- DERELICT ANALYSIS ---" RESET "\nSTATUS: DEAD\nPOWER: NONE\nADVISORY: Salvage operations authorized.\n");
                }
            } else if (tid >= 12000 && tid < 12000+MAX_ASTEROIDS) {
                for(int a=0; a<lq->asteroid_count; a++) if(lq->asteroids[a]->id+12000 == tid) {
                    found = true;
                    const char* res_names[] = {"None", "Aetherium", "Neo-Titanium", "Void-Essence", "Graphene", "Synaptics", "Nebular Gas", "Composite", "Dark-Matter"};
                    int r_type = lq->asteroids[a]->resource_type;
                    const char* r_name = (r_type >= 0 && r_type <= 8) ? res_names[r_type] : "Unknown";
                    sprintf(rep, WHITE "\n--- ASTEROID ANALYSIS ---" RESET "\nCOMPOSITION: %s\nRESERVES: %d units\nSIZE: Class-%.1f\n", 
                        r_name, lq->asteroids[a]->amount, lq->asteroids[a]->size * 10.0);
                }
            } else if (tid >= 14000 && tid < 14000+MAX_MINES) {
                for(int m=0; m<lq->mine_count; m++) if(lq->mines[m]->id+14000 == tid) {
                    found = true;
                    sprintf(rep, RED "\n--- WARNING: ORDNANCE DETECTED ---" RESET "\nTYPE: Proximity Mine\nSTATUS: ARMED\nADVISORY: Maintain safe distance.\n");
                }
            } else if (tid >= 15000 && tid < 15000+MAX_BUOYS) {
                for(int b=0; b<lq->buoy_count; b++) if(lq->buoys[b]->id+15000 == tid) {
                    found = true;
                    sprintf(rep, CYAN "\n--- COMMUNICATION BUOY ---" RESET "\nSTATUS: BROADCASTING\nFREQUENCY: Standard Alliance Band\n");
                }
            } else if (tid >= 17000 && tid < 17000+MAX_RIFTS) {
                for(int r=0; r<lq->rift_count; r++) if(lq->rifts[r]->id+17000 == tid) {
                    found = true;
                    sprintf(rep, MAGENTA "\n--- SPATIAL RIFT ANALYSIS ---" RESET "\nTYPE: Subspace Rupture\nADVISORY: Extreme gravitational shear detected.\n");
                }
            } else if (tid >= 9000 && tid < 9000+MAX_PULSARS) {
                for(int p=0; p<lq->pulsar_count; p++) if(lq->pulsars[p]->id+9000 == tid) {
                    found = true;
                    const char *p_classes[] = {"Rotation-Powered", "Accretion-Powered", "Magnetar"};
                    int p_type = lq->pulsars[p]->type;
                    sprintf(rep, YELLOW "\n--- NEUTRON STAR (PULSAR) ANALYSIS ---" RESET "\nCLASS: %s\nADVISORY: Lethal radiation beams. Maintain distance > 2.5 units.\n", 
                        (p_type >= 0 && p_type < 3) ? p_classes[p_type] : "Unknown");
                }
            } else if (tid >= 18000 && tid < 18000+MAX_MONSTERS) {
                for(int m=0; m<lq->monster_count; m++) if(lq->monsters[m]->id+18000 == tid) {
                    found = true;
                    sprintf(rep, RED "\n--- XENO-BIOLOGICAL THREAT ---" RESET "\nTYPE: %s\nADVISORY: Highly aggressive. Close proximity will result in rapid energy drain.\n", 
                        (lq->monsters[m]->type==30)?"Crystalline Entity":"Space Amoeba");
                }
            } else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS) {
                for(int p=0; p<lq->platform_count; p++) if(lq->platforms[p]->id+16000 == tid) {
                    found = true;
                    sprintf(rep, CYAN "\n--- DEFENSE PLATFORM SCAN ---" RESET "\nENERGY: %d units\nFACTION: %s\nSTATUS: ACTIVE\n", 
                        scrambled ? 0 : lq->platforms[p]->energy, get_species_name(lq->platforms[p]->faction));
                }
            } else if (tid >= 19000 && tid < 19200) {
                int p_idx = (tid - 19000) / 3;
                int pr_idx = (tid - 19000) % 3;
                if (p_idx < MAX_CLIENTS && players[p_idx].state.probes[pr_idx].active) {
                    found = true;
                    const char *status_str[] = {"LAUNCHED", "ARRIVED", "TRANSMITTING"};
                    int s = players[p_idx].state.probes[pr_idx].status;
                    sprintf(rep, CYAN "\n--- SENSOR PROBE ANALYSIS ---" RESET "\nID: %d\nORIGIN: %s\nSTATUS: %s\n", 
                        tid, players[p_idx].name, (s>=0 && s<3) ? status_str[s] : "UNKNOWN");
                }
            }
        }

        if (found) send_server_msg(i, "SCIENCE", rep);
        else send_server_msg(i, "COMPUTER", "Target not in sensor range.");
    } else send_server_msg(i, "COMPUTER", "Usage: scan <ID>");
}

void handle_clo(int i, const char *params) {
    if (!players[i].state.is_cloaked) {
        /* 1. Integrity check for engagement (Auxiliary ID 9) */
        if (players[i].state.system_health[9] < 15.0) {
            send_server_msg(i, "ENGINEERING", "CLOAKING FAILURE: Gravimetric stabilizers damaged.");
            return;
        }

        /* 2. Activation Cost: 500 units */
        if (players[i].state.energy < 500) {
            send_server_msg(i, "COMPUTER", "Insufficient energy to initialize cloaking field.");
            return;
        }
        
        /* 3. Reliability logic (15-50% health) */
        if (players[i].state.system_health[9] < 50.0 && (rand() % 100 > (int)players[i].state.system_health[9])) {
            players[i].state.energy -= 100; /* Partial loss */
            send_server_msg(i, "ENGINEERING", "Cloaking field collapsed during synchronization!");
            return;
        }

        players[i].state.energy -= 500;
        players[i].state.is_cloaked = 1;
        send_server_msg(i, "HELMSMAN", "Cloaking device engaged. Sensors limited.");
    } else {
        players[i].state.is_cloaked = 0;
        send_server_msg(i, "HELMSMAN", "Cloaking device disengaged.");
    }
}

void handle_bor(int i, const char *params) {
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
        if (players[i].state.system_health[3] < 20.0) {
            send_server_msg(i, "ENGINEERING", "BOARDING ABORTED: Transporter buffers unstable. Repair required.");
            return;
        }

        /* 2. Energy Cost Check: 5000 units */
        if (players[i].state.energy < 5000) { 
            send_server_msg(i, "COMPUTER", "Insufficient energy for massive matter transport (Req: 5000)."); 
            return; 
        }
        
        double tx, ty, tz; bool found = false;
        /* Resolve target position and validity (Use ABSOLUTE GALACTIC coordinates for distance) */
        if (tid >= 1 && tid <= 32 && players[tid-1].active) { tx=players[tid-1].gx; ty=players[tid-1].gy; tz=players[tid-1].gz; found=true; }
        else if (tid >= 1000 && tid < 1000+MAX_NPC && npcs[tid-1000].active) { tx=npcs[tid-1000].gx; ty=npcs[tid-1000].gy; tz=npcs[tid-1000].gz; found=true; }
        else if (tid >= 11000 && tid < 11000+MAX_DERELICTS && derelicts[tid-11000].active) { tx=(derelicts[tid-11000].q1-1)*10.0 + derelicts[tid-11000].x; ty=(derelicts[tid-11000].q2-1)*10.0 + derelicts[tid-11000].y; tz=(derelicts[tid-11000].q3-1)*10.0 + derelicts[tid-11000].z; found=true; }
        else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS && platforms[tid-16000].active) { tx=(platforms[tid-16000].q1-1)*10.0 + platforms[tid-16000].x; ty=(platforms[tid-16000].q2-1)*10.0 + platforms[tid-16000].y; tz=(platforms[tid-16000].q3-1)*10.0 + platforms[tid-16000].z; found=true; }
        
        if (found) {
            double dx=tx-players[i].gx, dy=ty-players[i].gy, dz=tz-players[i].gz; 
            double dist=sqrt(dx*dx+dy*dy+dz*dz);
            if (dist < 1.1) {
                players[i].state.energy -= 5000;

                /* 3. Success probability base (halved failure chance: 60% to 80% success) */
                int success_chance = 60 + (int)(players[i].state.system_health[3] * 0.2);

                /* Handle Player Boarding via Interactive Menu */
                if (tid >= 1 && tid <= 32) {
                    ConnectedPlayer *target = &players[tid-1];
                    players[i].pending_bor_target = tid;
                    char menu[512];
                    if (target->faction == players[i].faction) {
                        players[i].pending_bor_type = 1; /* Ally */
                        sprintf(menu, CYAN "\n--- BOARDING MENU: ALLIED VESSEL (%s) ---\n" RESET 
                               "1: Transfer Energy (50,000 units)\n"
                               "2: Technical Support (Repair random system)\n"
                               "3: Reinforce Crew (Transfer 20 personnel)\n"
                               YELLOW "Type the number to confirm choice." RESET, target->name);
                    } else {
                        players[i].pending_bor_type = 2; /* Enemy */
                        sprintf(menu, RED "\n--- BOARDING MENU: HOSTILE VESSEL (%s) ---\n" RESET 
                               "1: Sabotage (Damage random system)\n"
                               "2: Raid Cargo (Steal random resources)\n"
                               "3: Take Hostages (Capture officers as prisoners)\n"
                               YELLOW "Type the number to confirm choice." RESET, target->name);
                    }
                    send_server_msg(i, "BOARDING", menu);
                    return;
                }

                /* Handle other targets with probability logic */
                if (rand()%100 < success_chance) {
                    if (tid >= 11000 && tid < 11000+MAX_DERELICTS) {
                        /* ... derelict menu ... */
                        players[i].pending_bor_target = tid;
                        players[i].pending_bor_type = 4;
                        char menu[512];
                        sprintf(menu, WHITE "\n--- BOARDING MENU: DERELICT WRECK [%d] ---\n" RESET 
                               "1: Salvage Resources\n"
                               "2: Recover Data (Map reveal)\n"
                               "3: Field Repairs\n"
                               "4: Rescue Survivors (Crew)\n"
                               YELLOW "Type the number to confirm choice." RESET, tid);
                        send_server_msg(i, "BOARDING", menu);
                    } else if (tid >= 16000) {
                        /* ... platform menu ... */
                        players[i].pending_bor_target = tid;
                        players[i].pending_bor_type = 3;
                        char menu[512];
                        sprintf(menu, YELLOW "\n--- BOARDING MENU: DEFENSE PLATFORM [%d] ---\n" RESET 
                               "1: Reprogram IFF\n"
                               "2: Overload Reactor\n"
                               "3: Salvage Tech\n"
                               WHITE "Type the number to confirm choice." RESET, tid);
                        send_server_msg(i, "BOARDING", menu);
                    } else if (tid >= 1000 && tid < 1000+MAX_NPC) {
                        NPCShip *target_npc = &npcs[tid-1000];
                        /* Requirement: Target must be disabled (Engines < 50% OR Hull < 50%) */
                        if (target_npc->engine_health >= 50.0 && target_npc->health >= 500) {
                            send_server_msg(i, "SECURITY", "BOARDING DENIED: Target vessel is still fully operational. Disable engines or damage hull first!");
                            return;
                        }
                        
                        players[i].pending_bor_target = tid;
                        players[i].pending_bor_type = 2; /* Enemy */
                        char menu[512];
                        sprintf(menu, RED "\n--- BOARDING MENU: HOSTILE NPC [%d] ---\n" RESET 
                               "1: Sabotage Engines\n"
                               "2: Raid Cargo Bay\n"
                               "3: Take Hostages\n"
                               YELLOW "Type the number to confirm choice." RESET, tid);
                        send_server_msg(i, "BOARDING", menu);
                    } else {
                        /* NPC success reward */
                        int reward = rand()%2;
                        if (reward == 0) { players[i].state.inventory[1] += 5; send_server_msg(i, "BOARDING", "Success! Captured Aetherium crystals."); }
                        else { 
                            int found_people = 5 + rand()%25;
                            players[i].state.prison_unit += found_people;
                            char m[128]; sprintf(m, "Success! Captured %d enemy prisoners.", found_people);
                            send_server_msg(i, "SECURITY", m);
                        }
                    }
                } else {
                    int loss = 5 + rand()%15; 
                    players[i].state.crew_count -= loss;
                    if (players[i].state.crew_count < 0) players[i].state.crew_count = 0;
                    send_server_msg(i, "SECURITY", "Boarding party repelled! Heavy casualties reported.");
                }
            } else send_server_msg(i, "COMPUTER", "Target not in transporter range (< 1.0).");
        } else send_server_msg(i, "COMPUTER", "Invalid boarding target.");
    } else send_server_msg(i, "COMPUTER", "Usage: bor <ID>");
}

void handle_dis(int i, const char *params) {
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
        if (players[i].state.system_health[3] < 15.0) {
            send_server_msg(i, "ENGINEERING", "DISMANTLING FAILURE: Transporter arrays damaged. Cannot stabilize debris beams.");
            return;
        }

        /* 2. Energy Cost Check */
        if (players[i].state.energy < 500) {
            send_server_msg(i, "COMPUTER", "Insufficient energy for structural dismantling operation (Req: 500).");
            return;
        }

        bool done = false;
        
        /* 4. Yield scaling based on Transporter integrity (0.7 to 1.0 multiplier) */
        double transporter_mult = 0.7 + (players[i].state.system_health[3] / 100.0) * 0.3;

        /* Case: NPC Ship Wreck (ID 1000+) */
        if (tid >= 1000 && tid < 1000+MAX_NPC) {
            int n_idx = tid-1000;
            if (npcs[n_idx].active) {
                double tx = npcs[n_idx].gx;
                double ty = npcs[n_idx].gy;
                double tz = npcs[n_idx].gz;
                double dx = tx - players[i].gx, dy = ty - players[i].gy, dz = tz - players[i].gz;
                
                if (sqrt(dx*dx+dy*dy+dz*dz) < DIST_DISMANTLE_MAX) {
                    players[i].state.energy -= 500;
                    int yield = (int)((npcs[n_idx].energy / 100) * transporter_mult); 
                    if (yield < 10) yield = 10;
                    players[i].state.inventory[2] += yield; /* Neo-Titanium */
                    players[i].state.inventory[5] += yield / 5; /* Synaptics */
                    npcs[n_idx].active = 0;
                    
                    /* Visual FX */
                    double rs1 = (tx - (players[i].state.q1-1)*10.0);
                    double rs2 = (ty - (players[i].state.q2-1)*10.0);
                    double rs3 = (tz - (players[i].state.q3-1)*10.0);
                    players[i].state.dismantle = (NetDismantle){rs1, rs2, rs3, npcs[n_idx].faction, 1};
                    
                    char msg[128];
                    sprintf(msg, "Vessel dismantled. Recovered %d Neo-Titanium and %d Synaptics.", yield, yield/5);
                    send_server_msg(i, "ENGINEERING", msg);
                    if (players[i].state.lock_target == tid) players[i].state.lock_target = 0;
                    done = true;
                } else { send_server_msg(i, "COMPUTER", "Not in range for dismantling (Dist > 1.5)."); return; }
            }
        } 
        /* Case: Static Derelict Wreck (ID 11000+) */
        else if (tid >= 11000 && tid < 11000+MAX_DERELICTS) {
            int d_idx = tid-11000;
            if (derelicts[d_idx].active) {
                double tx = (derelicts[d_idx].q1-1)*10.0 + derelicts[d_idx].x;
                double ty = (derelicts[d_idx].q2-1)*10.0 + derelicts[d_idx].y;
                double tz = (derelicts[d_idx].q3-1)*10.0 + derelicts[d_idx].z;
                double dx = tx - players[i].gx, dy = ty - players[i].gy, dz = tz - players[i].gz;
                
                if (sqrt(dx*dx+dy*dy+dz*dz) < DIST_DISMANTLE_MAX) {
                    players[i].state.energy -= 500;
                    int yield = (int)((50 + rand()%150) * transporter_mult); 
                    players[i].state.inventory[2] += yield; 
                    players[i].state.inventory[5] += yield / 4; 
                    derelicts[d_idx].active = 0;
                    
                    /* Visual FX - Correct mapping to NetDismantle struct */
                    double rs1 = (tx - (players[i].state.q1-1)*10.0);
                    double rs2 = (ty - (players[i].state.q2-1)*10.0);
                    double rs3 = (tz - (players[i].state.q3-1)*10.0);
                    players[i].state.dismantle = (NetDismantle){rs1, rs2, rs3, derelicts[d_idx].faction, 1};
                    
                    char msg[128];
                    sprintf(msg, "Ancient wreck dismantled. Recovered %d Neo-Titanium and %d Synaptics.", yield, yield/4);
                    send_server_msg(i, "ENGINEERING", msg);
                    if (players[i].state.lock_target == tid) players[i].state.lock_target = 0;
                    done = true;
                } else { send_server_msg(i, "COMPUTER", "Not in range for dismantling (Dist > 1.5)."); return; }
            }
        }

        if (!done) send_server_msg(i, "COMPUTER", "Invalid dismantle target (Must be a wreck or derelict).");
    } else {
        send_server_msg(i, "COMPUTER", "Usage: dis <ID> or lock a target first.");
    }
}

void handle_min(int i, const char *params) {
    /* 1. Hardware Requirement: Transporters (ID 3) >= 15% */
    if (players[i].state.system_health[3] < 15.0) {
        send_server_msg(i, "ENGINEERING", "MINING FAILURE: Extraction beams offline (Transporter damage).");
        return;
    }

    /* 2. Energy Cost: 250 units */
    if (players[i].state.energy < 250) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for planetary extraction pulse.");
        return;
    }

    int locked_id = players[i].state.lock_target;
    int target_idx = -1;
    int target_type = -1; /* 0: Asteroid, 1: Planet */
    double min_dist = 3.1; /* Interaction range */

    /* Search for mineable target (locked or closest) */
    int q1 = players[i].state.q1, q2 = players[i].state.q2, q3 = players[i].state.q3;
    
    if (locked_id >= 3000 && locked_id < 3000 + MAX_PLANETS) {
        int p = locked_id - 3000;
        if (planets[p].active && planets[p].q1 == q1 && planets[p].q2 == q2 && planets[p].q3 == q3) {
            double d = sqrt(pow(planets[p].x - players[i].state.s1, 2) + pow(planets[p].y - players[i].state.s2, 2) + pow(planets[p].z - players[i].state.s3, 2));
            if (d <= min_dist) { target_idx = p; target_type = 1; }
        }
    } else if (locked_id >= 12000 && locked_id < 12000 + MAX_ASTEROIDS) {
        int a = locked_id - 12000;
        if (asteroids[a].active && asteroids[a].q1 == q1 && asteroids[a].q2 == q2 && asteroids[a].q3 == q3) {
            double d = sqrt(pow(asteroids[a].x - players[i].state.s1, 2) + pow(asteroids[a].y - players[i].state.s2, 2) + pow(asteroids[a].z - players[i].state.s3, 2));
            if (d <= min_dist) { target_idx = a; target_type = 0; }
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
    }

    if (target_idx != -1) {
        players[i].state.energy -= 250;
        /* 3. Yield scaling based on Transporter integrity (0.8 to 1.0 multiplier) */
        double yield_mult = 0.8 + (players[i].state.system_health[3] / 100.0) * 0.2;
        const char* res_names[] = {"None", "Aetherium", "Neo-Titanium", "Void-Essence", "Graphene", "Synaptics", "Nebular Gas", "Composite", "Dark-Matter"};

        int r_type, raw_amount;
        if (target_type == 0) {
            r_type = asteroids[target_idx].resource_type;
            raw_amount = (asteroids[target_idx].amount > 50) ? 50 : asteroids[target_idx].amount;
            asteroids[target_idx].amount -= raw_amount;
            if (asteroids[target_idx].amount <= 0) asteroids[target_idx].active = 0;
        } else {
            r_type = planets[target_idx].resource_type;
            raw_amount = (planets[target_idx].amount > 100) ? 100 : planets[target_idx].amount;
            planets[target_idx].amount -= raw_amount;
        }

        int final_yield = (int)(raw_amount * yield_mult);
        players[i].state.inventory[r_type] += final_yield;
        
        /* 4. Improved Feedback */
        char msg[128];
        sprintf(msg, "[RADIO] MINING: Collected %d units of %s. Extraction efficiency: %.0f%%.", final_yield, res_names[r_type], yield_mult * 100);
        send_server_msg(i, "GEOLOGY", msg);
    } else {
        send_server_msg(i, "COMPUTER", "No planet or asteroid in range for mining (< 3.1 units).");
    }
}

void handle_sco(int i, const char *params) {
    /* 1. Hardware Requirement: Auxiliary (ID 9) >= 15% */
    if (players[i].state.system_health[9] < 15.0) {
        send_server_msg(i, "ENGINEERING", "SCOOPING FAILURE: Solar collectors damaged. Repair Auxiliary systems.");
        return;
    }

    /* 2. Operational Cost: 100 units */
    if (players[i].state.energy < 100) {
        send_server_msg(i, "COMPUTER", "Insufficient energy to initialize magnetic collection field.");
        return;
    }

    bool near = false; 
    int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
    for(int s=0; s<MAX_STARS; s++) {
        if(stars_data[s].active && stars_data[s].q1==pq1 && stars_data[s].q2==pq2 && stars_data[s].q3==pq3) {
            double d=sqrt(pow(stars_data[s].x-players[i].state.s1,2)+pow(stars_data[s].y-players[i].state.s2,2)+pow(stars_data[s].z-players[i].state.s3,2)); 
            if(d < 1.5) { near=true; break; }
        }
    }

    if(near) { 
        players[i].state.energy -= 100;
        
        /* 3. Variable Yield based on Auxiliary health (0.8 to 1.0 multiplier) */
        double aux_h = players[i].state.system_health[9];
        int gain = (int)(5000 * (0.8 + (aux_h / 100.0) * 0.2));
        
        players[i].state.cargo_energy += gain; 
        if(players[i].state.cargo_energy > 1000000) players[i].state.cargo_energy = 1000000; 
        
        /* 4. Shield and Structural Damage */
        int s_idx = rand()%6; 
        if (players[i].state.shields[s_idx] >= 500) {
            players[i].state.shields[s_idx] -= 500;
        } else {
            /* Heat bypasses shields */
            players[i].state.shields[s_idx] = 0;
            double hull_dmg = 2.0 + (rand() % 300) / 100.0;
            apply_hull_damage(i, hull_dmg);
            players[i].state.system_health[7] -= 5.0; /* Life Support damage (ID 7) */
            send_server_msg(i, "CRITICAL", "WARNING: Solar heat bypassing shields! Hull and Life Support damaged!");
        }
        
        char msg[128];
        sprintf(msg, "Solar energy harvested: %d units stored in Cargo Bay. Collectors efficiency: %.0f%%.", gain, (aux_h/100.0)*100);
        send_server_msg(i, "ENGINEERING", msg); 
    } 
    else send_server_msg(i, "COMPUTER", "No star in proximity range (< 1.5 units).");
}

void handle_har(int i, const char *params) {
    /* 1. Hardware Requirement: Auxiliary (ID 9) >= 25% (Higher than sco) */
    if (players[i].state.system_health[9] < 25.0) {
        send_server_msg(i, "ENGINEERING", "HARVESTING FAILURE: Antimatter containment field unstable. Repairs required.");
        return;
    }

    /* 2. Operational Cost: 500 units */
    if (players[i].state.energy < 500) {
        send_server_msg(i, "COMPUTER", "Insufficient energy to initialize Hawking radiation shielding.");
        return;
    }

    bool near = false; 
    int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
    for(int h=0; h<MAX_BH; h++) {
        if(black_holes[h].active && black_holes[h].q1==pq1 && black_holes[h].q2==pq2 && black_holes[h].q3==pq3) {
            double d=sqrt(pow(black_holes[h].x-players[i].state.s1,2)+pow(black_holes[h].y-players[i].state.s2,2)+pow(black_holes[h].z-players[i].state.s3,2)); 
            if(d <= 3.1) { near=true; break; }
        }
    }

    if(near) { 
        players[i].state.energy -= 500;
        
        /* 3. Variable Yield based on Auxiliary health (0.6 to 1.0 multiplier) */
        double aux_h = players[i].state.system_health[9];
        double efficiency = 0.6 + (aux_h / 100.0) * 0.4;
        
        int energy_gain = (int)(10000 * efficiency);
        int crystal_gain = (int)(100 * efficiency);
        
        players[i].state.cargo_energy += energy_gain; 
        if(players[i].state.cargo_energy > 1000000) players[i].state.cargo_energy = 1000000; 
        players[i].state.inventory[1] += crystal_gain; 
        
        /* 4. Shield, Hull and System Damage (Radiation and Gravity) */
        int s_idx = rand()%6; 
        if (players[i].state.shields[s_idx] >= 1000) {
            players[i].state.shields[s_idx] -= 1000;
        } else {
            /* Event horizon shear bypasses weak shields */
            players[i].state.shields[s_idx] = 0;
            double hull_dmg = 5.0 + (rand() % 500) / 100.0;
            apply_hull_damage(i, hull_dmg);
            
            /* Heavy damage to critical systems: Sensors(2), Computer(6), Life Support(7) */
            players[i].state.system_health[2] -= (5.0 + (rand() % 10));
            players[i].state.system_health[6] -= (5.0 + (rand() % 10));
            players[i].state.system_health[7] -= (5.0 + (rand() % 10));
            
            send_server_msg(i, "CRITICAL", "EVENT HORIZON SHEAR! Shields buckled! Hull and internal systems damaged!");
        }
        
        char msg[128];
        sprintf(msg, "Antimatter harvested: %d units. Aetherium crystals: +%d. Containment efficiency: %.0f%%.", energy_gain, crystal_gain, efficiency * 100);
        send_server_msg(i, "ENGINEERING", msg); 
    } 
    else send_server_msg(i, "COMPUTER", "No black hole in proximity range (< 3.1 units).");
}

void handle_doc(int i, const char *params) {
    int base_idx = -1;
    for(int b=0; b<MAX_BASES; b++) {
        if(bases[b].active && bases[b].q1==players[i].state.q1 && bases[b].q2==players[i].state.q2 && bases[b].q3==players[i].state.q3) {
            double d=sqrt(pow(bases[b].x-players[i].state.s1,2)+pow(bases[b].y-players[i].state.s2,2)+pow(bases[b].z-players[i].state.s3,2)); 
            if(d <= (DIST_DOCKING_MAX + 0.05)) { base_idx = b; break; } 
        }
    }

    if(base_idx != -1) {
        if (bases[base_idx].faction != players[i].faction) {
            send_server_msg(i, "COMPUTER", "DOCKING DENIED: Starbase identified as HOSTILE or UNAUTHORIZED.");
            return;
        }

        /* Integrity Checks: Impulse (ID 1) and Auxiliary (ID 9) */
        if (players[i].state.system_health[1] < 10.0 || players[i].state.system_health[9] < 10.0) {
            send_server_msg(i, "ENGINEERING", "DOCKING FAILURE: Maneuvering thrusters or docking clamps offline.");
            return;
        }

        if (players[i].state.energy < 100) {
            send_server_msg(i, "COMPUTER", "Insufficient energy for magnetic docking sequence.");
            return;
        }

        /* Start Docking Sequence (10 seconds at 30Hz = 300 ticks) */
        players[i].state.energy -= 100;
        players[i].nav_state = NAV_STATE_DOCKING;
        players[i].nav_timer = 300;
        players[i].hyper_speed = 0; /* Stop ship movement */
        
        /* Repurpose pending_bor_target to store base index during docking */
        players[i].pending_bor_target = base_idx + 2000; 

        send_server_msg(i, "STARBASE", "Docking sequence initiated. Clamps engaging. Please hold position for 10 seconds.");
    } 
    else send_server_msg(i,"COMPUTER","No starbase in range.");
}

void handle_con(int i, const char *params) {
    int type, amount;
    if (sscanf(params, "%d %d", &type, &amount) != 2) {
        send_server_msg(i, "COMPUTER", "Usage: con <Type> <Amount>");
        return;
    }

    /* 1. Hardware Requirement: Auxiliary (ID 9) >= 15% */
    if (players[i].state.system_health[9] < 15.0) {
        send_server_msg(i, "ENGINEERING", "CONVERTER FAILURE: Auxiliary systems integrity too low (< 15%).");
        return;
    }

    /* 2. Energy Cost: 100 units */
    if (players[i].state.energy < 100) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for conversion cycle (Req: 100).");
        return;
    }

    if (amount <= 0) return;
    if (type < 1 || type > 8 || players[i].state.inventory[type] < amount) {
        send_server_msg(i, "LOGISTICS", "Insufficient resources in cargo bay.");
        return;
    }

    /* 3. Efficiency based on Auxiliary health (70% - 100%) */
    double efficiency = 0.7 + (players[i].state.system_health[9] / 100.0) * 0.3;
    players[i].state.energy -= 100;
    players[i].state.inventory[type] -= amount;

    char msg[128];
    if (type == 1) { /* Aetherium -> Energy x10 */
        int gain = (int)(amount * 10 * efficiency);
        players[i].state.cargo_energy += gain;
        if (players[i].state.cargo_energy > 1000000) players[i].state.cargo_energy = 1000000;
        sprintf(msg, "Converted %d Aetherium into %d Energy units. Efficiency: %.1f%%.", amount, gain, efficiency * 100);
    } else if (type == 2) { /* Neo-Titanium -> Energy x2 */
        int gain = (int)(amount * 2 * efficiency);
        players[i].state.cargo_energy += gain;
        if (players[i].state.cargo_energy > 1000000) players[i].state.cargo_energy = 1000000;
        sprintf(msg, "Converted %d Neo-Titanium into %d Energy units. Efficiency: %.1f%%.", amount, gain, efficiency * 100);
    } else if (type == 3) { /* Void-Essence -> Torpedoes (1/20) */
        int gain = (int)((amount / 20.0) * efficiency);
        players[i].state.cargo_torpedoes += gain;
        if (players[i].state.cargo_torpedoes > 1000) players[i].state.cargo_torpedoes = 1000;
        sprintf(msg, "Converted %d Void-Essence into %d Torpedoes. Efficiency: %.1f%%.", amount, gain, efficiency * 100);
    } else if (type == 6) { /* Gas -> Energy x5 */
        int gain = (int)(amount * 5 * efficiency);
        players[i].state.cargo_energy += gain;
        if (players[i].state.cargo_energy > 1000000) players[i].state.cargo_energy = 1000000;
        sprintf(msg, "Converted %d Nebular Gas into %d Energy units. Efficiency: %.1f%%.", amount, gain, efficiency * 100);
    } else if (type == 7) { /* Composite -> Energy x4 */
        int gain = (int)(amount * 4 * efficiency);
        players[i].state.cargo_energy += gain;
        if (players[i].state.cargo_energy > 1000000) players[i].state.cargo_energy = 1000000;
        sprintf(msg, "Converted %d Composite into %d Energy units. Efficiency: %.1f%%.", amount, gain, efficiency * 100);
    } else if (type == 8) { /* Dark-Matter -> Energy x25 */
        int gain = (int)(amount * 25 * efficiency);
        players[i].state.cargo_energy += gain;
        if (players[i].state.cargo_energy > 1000000) players[i].state.cargo_energy = 1000000;
        sprintf(msg, "Converted %d Dark-Matter into %d Energy units. Efficiency: %.1f%%.", amount, gain, efficiency * 100);
    } else {
        send_server_msg(i, "COMPUTER", "Resource type not suitable for atomic conversion.");
        players[i].state.energy += 100; /* Refund */
        players[i].state.inventory[type] += amount;
        return;
    }

    send_server_msg(i, "ENGINEERING", msg);
}

void handle_load(int i, const char *params) {
    /* 1. Hardware Requirement: Auxiliary (ID 9) >= 10% */
    if (players[i].state.system_health[9] < 10.0) {
        send_server_msg(i, "ENGINEERING", "TRANSFER FAILURE: Internal power bus non-functional.");
        return;
    }

    int type, amount; 
    if (sscanf(params, "%d %d", &type, &amount) == 2) {
        /* 2. Energy Cost: 25 units */
        if (players[i].state.energy < 25) {
            send_server_msg(i, "COMPUTER", "Insufficient energy for internal transfer systems.");
            return;
        }
        players[i].state.energy -= 25;

        if (type == 1) { /* Load Energy */
            if (amount > players[i].state.cargo_energy) amount = players[i].state.cargo_energy; 
            int space = 9999999 - players[i].state.energy;
            if (amount > space) amount = space;
            
            players[i].state.cargo_energy -= amount; 
            players[i].state.energy += amount; 
            
            char msg[128];
            sprintf(msg, "Energy transfer complete. %d units loaded into main reactor.", amount);
            send_server_msg(i, "ENGINEERING", msg); 
        }
        else if (type == 2) { /* Load Torpedoes */
            if (amount > players[i].state.cargo_torpedoes) amount = players[i].state.cargo_torpedoes; 
            int space = 1000 - players[i].state.torpedoes;
            if (amount > space) amount = space;
            
            players[i].state.cargo_torpedoes -= amount; 
            players[i].state.torpedoes += amount; 
            
            char msg[128];
            sprintf(msg, "Torpedoes loaded. %d warheads moved to active tubes.", amount);
            send_server_msg(i, "TACTICAL", msg); 
        } else {
            send_server_msg(i, "COMPUTER", "Invalid transfer type. Use 1:Energy or 2:Torpedoes.");
        }
    } else {
        send_server_msg(i, "COMPUTER", "Usage: load <Type> <Amount> (1: Energy, 2: Torpedoes)");
    }
}

void handle_rep(int i, const char *params) {
    const char* sys_names[] = {"Hyperdrive", "Impulse", "Sensors", "Transp", "Ion Beams", "Torps", "Computer", "Life Support", "Shields", "Auxiliary"};
    int sid; 
    if(sscanf(params, " %d", &sid) == 1) {
        if (sid >= 0 && sid < 10) {
            /* 1. Support System Check: Req Computer (ID 6) OR Auxiliary (ID 9) >= 10% */
            if (players[i].state.system_health[6] < 10.0 && players[i].state.system_health[9] < 10.0 && sid != 6 && sid != 9) {
                send_server_msg(i, "ENGINEERING", "REPAIR FAILURE: Both Computer and Auxiliary systems are offline. Manual coordination impossible.");
                return;
            }

            /* 2. Energy Cost Check: 500 units */
            if (players[i].state.energy < 500) {
                send_server_msg(i, "COMPUTER", "Insufficient energy to power automated repair drones.");
                return;
            }

            /* 3. Resource Check: 50 Neo-Ti, 10 Synaptics */
            if(players[i].state.inventory[2] >= 50 && players[i].state.inventory[5] >= 10) {
                players[i].state.inventory[2] -= 50; 
                players[i].state.inventory[5] -= 10;
                players[i].state.energy -= 500;
                players[i].state.system_health[sid] = 100.0; 
                
                /* 4. Improved Feedback */
                char msg[128];
                sprintf(msg, "System %s restored to 100%% integrity. [Neo-Ti -50, Synaptics -10]", sys_names[sid]);
                send_server_msg(i, "ENGINEERING", msg);
            } else {
                send_server_msg(i, "ENGINEERING", "Insufficient materials (Req: 50 Neo-Titanium, 10 Synaptics Chips).");
            }
        } else {
            send_server_msg(i, "COMPUTER", "Invalid system ID. Use 'rep' to list systems.");
        }
    } else {
        /* List all systems with health status */
        char list[1024]; 
        sprintf(list, CYAN "\n--- ENGINEERING: SHIP SYSTEMS DIRECTORY ---" RESET "\n");
        for(int s=0; s<10; s++) {
            char line[128];
            double hp = players[i].state.system_health[s];
            const char* col = (hp > 75) ? GREEN : (hp > 25) ? YELLOW : RED;
            sprintf(line, WHITE "%d" RESET ": %-15s | STATUS: %s%.1f%%" RESET "\n", s, sys_names[s], col, hp);
            strcat(list, line);
        }
        strcat(list, YELLOW "\nUsage: rep <ID> (Costs: 500 Energy, 50 Neo-Titanium, 10 Synaptics)\n" RESET);
        send_server_msg(i, "COMPUTER", list);
    }
}

void handle_fix(int i, const char *params) {
    /* 1. Hardware Requirement: Auxiliary (ID 9) >= 10% */
    if (players[i].state.system_health[9] < 10.0) {
        send_server_msg(i, "ENGINEERING", "FIELD REPAIR FAILURE: Welding arrays offline (Auxiliary damage).");
        return;
    }

    /* Check Hull Limit: Max 80% for field repairs */
    if (players[i].state.hull_integrity >= 80.0) {
        send_server_msg(i, "ENGINEERING", "Hull integrity at maximum field-repair capacity (80%). Starbase required for full overhaul.");
        return;
    }

    /* Check Resources: 50 Graphene (inv[4]), 20 Neo-Titanium (inv[2]) */
    if (players[i].state.inventory[4] < 50 || players[i].state.inventory[2] < 20) {
        send_server_msg(i, "COMPUTER", "Insufficient materials for hull repair (Req: 50 Graphene, 20 Neo-Titanium).");
        return;
    }

    /* 2. Energy Cost: 500 units */
    if (players[i].state.energy < 500) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for structural welding pulse (Req: 500).");
        return;
    }

    players[i].state.energy -= 500;
    players[i].state.inventory[4] -= 50;
    players[i].state.inventory[2] -= 20;

    /* 3. Efficiency scaling based on Auxiliary health: base 10% + up to 10% bonus */
    double efficiency = 10.0 + (players[i].state.system_health[9] / 100.0) * 10.0;
    players[i].state.hull_integrity += efficiency;
    if (players[i].state.hull_integrity > 80.0) players[i].state.hull_integrity = 80.0;
    
    /* 4. Improved Feedback */
    char msg[128];
    sprintf(msg, "Field repairs complete. Hull integrity restored by %.1f%% to %.1f%%. [Graphene -50, Neo-Ti -20]", efficiency, players[i].state.hull_integrity);
    send_server_msg(i, "ENGINEERING", msg);
}

void handle_sta(int i, const char *params) {
    /* 1. Hardware Requirement: Computer (ID 6) >= 5% */
    if (players[i].state.system_health[6] < 5.0) {
        send_server_msg(i, "COMPUTER", "DIAGNOSTICS FAILURE: Mainframe logic core non-responsive.");
        return;
    }

    /* 2. Energy Cost: 10 units */
    if (players[i].state.energy < 10) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for full systems diagnostic.");
        return;
    }
    players[i].state.energy -= 10;

    char *b = calloc(4096, sizeof(char));
    if (!b) return;
    
    double comp_h = players[i].state.system_health[6];
    /* 3. Data Scrambling Logic (< 30%) */
    bool scrambled = (comp_h < 30.0 && (rand() % 100 > (int)comp_h));

    const char* c_names[] = {"Legacy Class", "Scout Class", "Heavy Cruiser", "Multi-Engine Cruiser", "Escort Class", "Explorer Class", "Flagship Class", "Science Vessel", "Carrier Class", "Tactical Cruiser", "Diplomatic Cruiser", "Research Vessel", "Frigate Class", "Vessel"};
    const char* class_name = (players[i].ship_class >= 0 && players[i].ship_class <= 13) ? c_names[players[i].ship_class] : "Unknown";
    
    snprintf(b, 4096, CYAN "\n.--- GDIS MAIN COMPUTER: STRATEGIC DIAGNOSTICS -----------------.\n" RESET);
    if (scrambled) strcat(b, RED " WARNING: COMPUTER CORE CORRUPTED. DIAGNOSTICS UNRELIABLE.\n" RESET);
    
    sprintf(b+strlen(b), WHITE " COMMANDER: %-18s CLASS: %-15s\n STATUS:    %s\n CREW COMPLEMENT: %d\n" RESET, 
             players[i].name, class_name, 
             players[i].state.is_cloaked ? MAGENTA "[ CLOAKED ]" RESET : GREEN "[ ACTIVE ]" RESET, 
             scrambled ? (rand()%1000) : players[i].state.crew_count);

    strcat(b, BLUE "\n[ POSITION AND TELEMETRY ]\n" RESET);
    if (scrambled) {
        strcat(b, " QUADRANT: [?, ?, ?]  SECTOR: [??.??, ??.??, ??.??]\n");
    } else {
        sprintf(b+strlen(b), " QUADRANT: [%d,%d,%d]  SECTOR: [%.2f, %.2f, %.2f]\n", players[i].state.q1, players[i].state.q2, players[i].state.q3, players[i].state.s1, players[i].state.s2, players[i].state.s3);
    }
    sprintf(b+strlen(b), " HEADING:  %03.0ff        MARK:   %+03.0f\n", players[i].state.van_h, players[i].state.van_m);

    strcat(b, BLUE "\n[ POWER AND REACTOR STATUS ]\n" RESET);
    double en_pct = (players[i].state.energy / 1000000.0) * 100.0;
    sprintf(b+strlen(b), " MAIN REACTOR: %d / 1000000 (%.1f%%)\n ALLOCATION:   ENGINES: %.0f%%  SHIELDS: %.0f%%  WEAPONS: %.0f%%\n", players[i].state.energy, en_pct, players[i].state.power_dist[0]*100, players[i].state.power_dist[1]*100, players[i].state.power_dist[2]*100);

    strcat(b, BLUE "\n[ DEFENSIVE GRID AND ARMAMENTS ]\n" RESET);
    sprintf(b+strlen(b), " SHIELDS: F:%-4d R:%-4d T:%-4d B:%-4d L:%-4d RI:%-4d\n Torpedoes: %-2d  ION BEAM CHARGE: %.1f%%  LOCK: %d\n", players[i].state.shields[0], players[i].state.shields[1], players[i].state.shields[2], players[i].state.shields[3], players[i].state.shields[4], players[i].state.shields[5], players[i].state.torpedoes, players[i].state.ion_beam_charge, players[i].state.lock_target);

    strcat(b, BLUE "\n[ SYSTEMS INTEGRITY ]\n" RESET);
    double h_int = players[i].state.hull_integrity;
    const char* h_col = (h_int > 75) ? GREEN : (h_int > 25) ? YELLOW : RED;
    sprintf(b+strlen(b), " PHYSICAL HULL: %s%.1f%%" RESET "\n", h_col, h_int);
    
    /* 4. Inclusion of all 10 systems (0-9) */
    const char* sys[] = {"Hyperdrive", "Impulse", "Sensors", "Transp", "Ion Beams", "Torps", "Computer", "Life Sup", "Shields", "Auxiliary"};
    for(int s=0; s<10; s++) { 
        double hp = players[i].state.system_health[s]; 
        const char* col = (hp > 75) ? GREEN : (hp > 25) ? YELLOW : RED; 
        sprintf(b+strlen(b), " %-10s: %s%.1f%%" RESET " ", sys[s], col, hp); 
        if (s == 2 || s == 5 || s == 8) strcat(b, "\n"); 
    }
    strcat(b, CYAN "\n'-----------------------------------------------------------------'\n" RESET);
    
    send_server_msg(i, "COMPUTER", b);
    free(b);
}

void handle_inv(int i, const char *params) {
    /* 1. Hardware Requirement: Computer (ID 6) >= 5% */
    if (players[i].state.system_health[6] < 5.0) {
        send_server_msg(i, "COMPUTER", "INVENTORY FAILURE: Cargo database inaccessible. Repair computer core.");
        return;
    }

    /* 2. Energy Cost: 5 units */
    if (players[i].state.energy < 5) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for manifest scan.");
        return;
    }
    players[i].state.energy -= 5;

    char b[1024]; 
    sprintf(b, YELLOW "\n--- CARGO BAY MANIFEST (GDIS LOGISTICS) ---" RESET "\n");
    
    double comp_h = players[i].state.system_health[6];
    bool scrambled = (comp_h < 30.0 && (rand() % 100 > (int)comp_h));

    if (scrambled) {
        strcat(b, RED " WARNING: LOGISTICS CORE CORRUPTED. DATA UNRELIABLE.\n" RESET);
    }

    const char* r[] = {"-", "Aetherium", "Neo-Titanium", "Void-Essence", "Graphene", "Synaptics", "Nebular Gas", "Composite", "Dark-Matter"};
    for (int j = 1; j <= 8; j++) {
        char it[128];
        if (scrambled && (rand() % 100 > 50)) {
            sprintf(it, " %-18s: ??? units\n", r[j]);
        } else {
            sprintf(it, " %-18s: %-5d units\n", r[j], players[i].state.inventory[j]);
        }
        strcat(b, it);
    }

    char extra[256];
    if (scrambled) {
        sprintf(extra, BLUE " Plasma Reserves:   ??????\n CARGO Torpedoes:   ???\n PRISON UNIT:       ??\n" RESET);
    } else {
        sprintf(extra, BLUE " Plasma Reserves:   %-7d\n CARGO Torpedoes:   %-3d\n PRISON UNIT:       %-3d\n" RESET, 
                players[i].state.cargo_energy, players[i].state.cargo_torpedoes, players[i].state.prison_unit);
    }
    strcat(b, extra);
    strcat(b, YELLOW "--------------------------------------------" RESET);
    
    send_server_msg(i, "LOGISTICS", b);
}

void handle_dam(int i, const char *params) {
    /* 1. Hardware Requirement: Computer (ID 6) >= 5% */
    if (players[i].state.system_health[6] < 5.0) {
        send_server_msg(i, "COMPUTER", "DIAGNOSTICS FAILURE: Damage analysis core non-functional.");
        return;
    }

    /* 2. Energy Cost: 5 units */
    if (players[i].state.energy < 5) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for damage report scan.");
        return;
    }
    players[i].state.energy -= 5;

    char b[1024]; 
    sprintf(b, RED "\n--- GDIS DAMAGE CONTROL: SHIP INTEGRITY REPORT ---" RESET "\n");
    
    double comp_h = players[i].state.system_health[6];
    bool scrambled = (comp_h < 30.0 && (rand() % 100 > (int)comp_h));

    if (scrambled) {
        strcat(b, YELLOW " WARNING: SENSOR LINK UNSTABLE. DATA PARITY ERROR.\n" RESET);
    }

    double h_int = players[i].state.hull_integrity;
    const char* h_col = (h_int > 75) ? GREEN : (h_int > 25) ? YELLOW : RED;
    if (scrambled) {
        strcat(b, " PHYSICAL HULL: [ERROR]%\n");
    } else {
        sprintf(b+strlen(b), " PHYSICAL HULL INTEGRITY: %s%.1f%%" RESET "\n", h_col, h_int);
    }

    strcat(b, BLUE " SYSTEM INTEGRITY STATUS:\n" RESET);
    /* 3. Inclusion of all 10 systems (0-9) */
    const char* sys[] = {"Hyperdrive", "Impulse", "Sensors", "Transp", "Ion Beams", "Torps", "Computer", "Life Sup", "Shields", "Auxiliary"};
    for (int s = 0; s < 10; s++) {
        char sbuf[128];
        double hp = players[i].state.system_health[s];
        const char* col = (hp > 75) ? GREEN : (hp > 25) ? YELLOW : RED;
        
        if (scrambled && (rand() % 100 > 60)) {
            sprintf(sbuf, "  %-15s: [??.?%%]\n", sys[s]);
        } else {
            sprintf(sbuf, "  %-15s: %s%.1f%%" RESET "\n", sys[s], col, hp);
        }
        strcat(b, sbuf);
    }
    strcat(b, RED "--------------------------------------------------" RESET);
    
    send_server_msg(i, "ENGINEERING", b);
}

void handle_cal(int i, const char *params) {
    if (players[i].state.system_health[6] < 10.0) {
        send_server_msg(i, "COMPUTER", "CALCULATION FAILURE: Navigation core non-functional.");
        return;
    }
    if (players[i].state.energy < 25) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for navigational computation.");
        return;
    }

    int qx, qy, qz; 
    double sx = 5.0, sy = 5.0, sz = 5.0;
    int args = sscanf(params, "%d %d %d %lf %lf %lf", &qx, &qy, &qz, &sx, &sy, &sz);
    
    if (args >= 3) {
        if (!IS_Q_VALID(qx, qy, qz)) {
            send_server_msg(i, "COMPUTER", "Invalid quadrant coordinates.");
            return;
        }

        players[i].state.energy -= 25;
        double comp_h = players[i].state.system_health[6];
        bool scrambled = (comp_h < 50.0 && (rand() % 100 > (int)comp_h));

        double target_gx = (qx - 1) * 10.0 + sx;
        double target_gy = (qy - 1) * 10.0 + sy;
        double target_gz = (qz - 1) * 10.0 + sz;
        
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
        double q_dist = d / 10.0;
        
        char buf[1024]; 
        if (scrambled) {
            sprintf(buf, "\n" RED ".--- NAVIGATIONAL COMPUTATION: DATA CORRUPTED ---." RESET "\n"
                         " DESTINATION:  " WHITE "Q[%d,%d,%d] Sector [?, ?, ?]" RESET "\n"
                         " BEARING:      " RED "ERROR - Logic Parity Failure" RESET "\n"
                         " DISTANCE:     " YELLOW "??.?? Quadrants" RESET "\n\n"
                         " WARNING: Computer integrity low. Results unreliable.\n",
                         qx, qy, qz);
        } else {
            sprintf(buf, "\n" CYAN ".--- NAVIGATIONAL COMPUTATION: PINPOINT PRECISION --." RESET "\n"
                         " DESTINATION:  " WHITE "Q[%d,%d,%d] Sector [%.1f,%.1f,%.1f]" RESET "\n"
                         " BEARING:      " GREEN "Heading %.1f, Mark %+.1f" RESET "\n"
                         " DISTANCE:     " YELLOW "%.2f Quadrants" RESET "\n\n"
                         WHITE " HYPERDRIVE FACTOR   EST. TIME      NOTES" RESET "\n"
                         " -----------   ---------      -----------------\n"
                         " Hyperdrive 1.0      %6.1fs      Minimum Hyperdrive\n"
                         " Hyperdrive 3.0      %6.1fs      Economic\n"
                         " Hyperdrive 6.0      %6.1fs      Standard Cruise\n"
                         " Hyperdrive 8.0      %6.1fs      High Pursuit\n"
                         " Hyperdrive 9.9      %6.1fs      Maximum Hyperdrive\n"
                         "-------------------------------------------------\n"
                         " Use 'nav %.1f %.1f %.2f [Factor]'", 
                         qx, qy, qz, sx, sy, sz, h, m, q_dist,
                         (d / 1.0),
                         (d / 3.0),
                         (d / 6.0),
                         (d / 8.0),
                         (d / 9.9),
                         h, m, q_dist
            );
        }
        send_server_msg(i, "COMPUTER", buf);
    } else {
        send_server_msg(i, "COMPUTER", "Usage: cal <QX> <QY> <QZ> [SX SY SZ]");
    }
}

void handle_ical(int i, const char *params) {
    /* 1. Hardware Requirement: Computer (ID 6) >= 10% */
    if (players[i].state.system_health[6] < 10.0) {
        send_server_msg(i, "COMPUTER", "CALCULATION FAILURE: Navigation core offline.");
        return;
    }

    /* 2. Energy Cost: 10 units */
    if (players[i].state.energy < 10) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for impulse computation.");
        return;
    }

    double tx, ty, tz;
    if (sscanf(params, "%lf %lf %lf", &tx, &ty, &tz) == 3) {
        players[i].state.energy -= 10;
        double comp_h = players[i].state.system_health[6];
        /* 3. Data Reliability logic: Scrambling if integrity < 50% */
        bool scrambled = (comp_h < 50.0 && (rand() % 100 > (int)comp_h));

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
        /* NAV_STATE_IMPULSE use hyper_speed (max 0.2) * engine_mult (0.5 to 1.5) * integrity */
        double imp_h = players[i].state.system_health[1];
        if (imp_h < 1.0) imp_h = 1.0;
        
        double engine_mult = 0.5 + (players[i].state.power_dist[0] * 1.0);
        double max_impulse_speed = 0.2; /* Capped in handle_imp */
        double speed_per_tick = max_impulse_speed * engine_mult * (imp_h / 100.0);
        
        /* eta = distance / (speed_per_second) -> speed_per_second = speed_per_tick * 30 */
        double eta_sec = d / (speed_per_tick * 30.0);

        char buf[512];
        if (scrambled) {
            sprintf(buf, "\n" RED "--- IMPULSE NAVIGATION: DATA CORRUPTED ---" RESET "\n"
                         " DESTINATION: [%.1f, %.1f, %.1f]\n"
                         " BEARING:     ???.? / %+.1f\n"
                         " ETA:         ??.? sec\n"
                         " WARNING: Logic core parity error. Results unreliable.\n", 
                         tx, ty, tz, (rand()%180-90)*1.0);
        } else {
            sprintf(buf, "\n" CYAN "--- IMPULSE NAVIGATION COMPUTATION ---" RESET "\n"
                         " DESTINATION: [%.1f, %.1f, %.1f]\n"
                         " BEARING:     %05.1f / %+05.1f\n"
                         " DISTANCE:    %.2f units\n"
                         " ETA:         %.1f sec (at Full Impulse)\n"
                         " COMMAND:     " WHITE "imp %.1f %.1f 10.0 %.2f" RESET "\n",
                         tx, ty, tz, h, m, d, eta_sec, h, m, d);
        }
        send_server_msg(i, "COMPUTER", buf);
    } else {
        send_server_msg(i, "COMPUTER", "Usage: ical <X> <Y> <Z>");
    }
}

void handle_who(int i, const char *params) {
    /* 1. Hardware Requirement: Computer (ID 6) >= 5% */
    if (players[i].state.system_health[6] < 5.0) {
        send_server_msg(i, "COMPUTER", "REGISTRY FAILURE: Deep space link offline. Repair computer core.");
        return;
    }

    /* 2. Energy Cost: 10 units */
    if (players[i].state.energy < 10) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for registry synchronization.");
        return;
    }
    players[i].state.energy -= 10;

    char b[2048]; 
    sprintf(b, WHITE "\n--- ACTIVE CAPTAINS REGISTRY (GDIS CENTRAL) ---" RESET "\n"
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
                sprintf(line, " [%2d] %-18s %-18s [?, ?, ?]\n", j + 1, players[j].name, "UNKNOWN");
            } else {
                sprintf(line, " [%2d] %-18s %-18s [%d,%d,%d]\n", 
                        j + 1, players[j].name, get_species_name(players[j].faction), tq1, tq2, tq3);
            }
            strcat(b, line);
        }
    }
    strcat(b, "-------------------------------------------------------\n");
    send_server_msg(i, "COMPUTER", b);
}

void handle_jum(int i, const char *params) {
    int qx, qy, qz;
    if (sscanf(params, "%d %d %d", &qx, &qy, &qz) == 3) {
        if (!IS_Q_VALID(qx, qy, qz)) {
            send_server_msg(i, "COMPUTER", "Invalid quadrant coordinates.");
            return;
        }

        /* 1. Integrity requirements (Hyperdrive ID 0, Sensors ID 2) */
        if (players[i].state.system_health[0] < 50.0 || players[i].state.system_health[2] < 50.0) {
            send_server_msg(i, "SCIENCE", "UNABLE TO STABILIZE WORMHOLE: Hyperdrive and Sensors must be above 50% integrity.");
            return;
        }

        /* 2. Malfunction risk (Hyperdrive 50-75% health) */
        if (players[i].state.system_health[0] < 75.0 && (rand() % 100 > (int)players[i].state.system_health[0])) {
            send_server_msg(i, "ENGINEERING", "Wormhole collapse during initialization! Resources consumed.");
            players[i].state.energy -= 2500;
            return;
        }

        if (players[i].state.energy < 5000 || players[i].state.inventory[1] < 1) {
             send_server_msg(i, "ENGINEERING", "Insufficient resources for Jump (Req: 5000 Energy, 1 Aetherium).");
             return;
        }

        players[i].state.energy -= 5000;
        players[i].state.inventory[1] -= 1;

        /* 3. Stress damage (1-3% hull) */
        double stress = 1.0 + (rand() % 200) / 100.0;
        apply_hull_damage(i, stress);

        /* 4. Positional Uncertainty based on sensor damage */
        double off_x = 0, off_y = 0, off_z = 0;
        if (players[i].state.system_health[2] < 100.0) {
            double noise = (100.0 - players[i].state.system_health[2]) / 20.0; /* Up to 2.5 units off */
            off_x = ((rand() % 2000 - 1000) / 1000.0) * noise;
            off_y = ((rand() % 2000 - 1000) / 1000.0) * noise;
            off_z = ((rand() % 2000 - 1000) / 1000.0) * noise;
        }

        players[i].target_gx = (qx - 1) * 10.0 + 5.5 + off_x;
        players[i].target_gy = (qy - 1) * 10.0 + 5.5 + off_y;
        players[i].target_gz = (qz - 1) * 10.0 + 5.5 + off_z;
        
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
        players[i].nav_timer = 450; 
        send_server_msg(i, "HELMSMAN", "Initiating trans-quadrant jump. Structural stress detected.");
    } else send_server_msg(i, "COMPUTER", "Usage: jum <Q1> <Q2> <Q3>");
}

void handle_psy(int i, const char *params) {
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

    if (rand() % 100 < 60) {
        /* 3. Success: NPCs flee */
        for (int n = 0; n < lq->npc_count; n++) {
            lq->npcs[n]->ai_state = AI_STATE_FLEE;
            lq->npcs[n]->energy += 5000; /* Panic boost */
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

void handle_hull(int i, const char *params) {
    /* 1. Hardware Requirement: Auxiliary (ID 9) >= 10% */
    if (players[i].state.system_health[9] < 10.0) {
        send_server_msg(i, "ENGINEERING", "PLATING FAILURE: Hull integration systems offline.");
        return;
    }

    /* 3. Plating Limit: 5000 units */
    if (players[i].state.composite_plating >= 5000) {
        send_server_msg(i, "ENGINEERING", "Hull plating is already at maximum structural capacity (5000).");
        return;
    }

    /* Check Resources: 100 Composite (inventory[7]) */
    if (players[i].state.inventory[7] < 100) {
        send_server_msg(i, "COMPUTER", "Insufficient Composite materials for reinforcement (Req: 100).");
        return;
    }

    /* 2. Energy Cost: 1000 units */
    if (players[i].state.energy < 1000) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for hull reinforcement process (Req: 1000).");
        return;
    }

    players[i].state.energy -= 1000;
    players[i].state.inventory[7] -= 100;
    players[i].state.composite_plating += 500;
    if (players[i].state.composite_plating > 5000) players[i].state.composite_plating = 5000;
    
    /* 4. Improved Feedback */
    char msg[128];
    sprintf(msg, "Hull reinforced with Composite plating. Current plating: %d units.", players[i].state.composite_plating);
    send_server_msg(i, "ENGINEERING", msg);
}

void handle_supernova(int i, const char *params) {
    send_server_msg(i, "ADMIN", "SUPERNOVA INITIATED.");
}

void handle_axs(int i, const char *params) {
    if (players[i].state.system_health[6] < 10.0) {
        send_server_msg(i, "COMPUTER", "HUD FAILURE: Augmented Reality core offline.");
        return;
    }
    if (players[i].state.energy < 10) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for AR HUD toggle.");
        return;
    }
    players[i].state.energy -= 10;
    players[i].state.show_axes = !players[i].state.show_axes;
    send_server_msg(i, "COMPUTER", players[i].state.show_axes ? "AR Tactical Compass ENABLED." : "AR Tactical Compass DISABLED.");
}

void handle_grd(int i, const char *params) {
    if (players[i].state.system_health[6] < 10.0) {
        send_server_msg(i, "COMPUTER", "HUD FAILURE: Grid projection systems non-functional.");
        return;
    }
    if (players[i].state.energy < 10) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for Tactical Grid toggle.");
        return;
    }
    players[i].state.energy -= 10;
    players[i].state.show_grid = !players[i].state.show_grid;
    send_server_msg(i, "COMPUTER", players[i].state.show_grid ? "Tactical Grid ENABLED." : "Tactical Grid DISABLED.");
}

void handle_bridge(int i, const char *params) {
    if (players[i].state.system_health[6] < 10.0) {
        send_server_msg(i, "COMPUTER", "VISUAL FAILURE: Bridge camera link corrupted.");
        return;
    }
    if (players[i].state.energy < 10) {
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
        players[i].state.energy -= 10;
        players[i].state.show_bridge = new_val;
    }
}

void handle_map(int i, const char *params) {
    if (players[i].state.system_health[6] < 15.0) {
        send_server_msg(i, "COMPUTER", "CARTOGRAPHY FAILURE: Spatial projection mainframe damaged.");
        return;
    }
    if (players[i].state.energy < 50) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for real-time map rendering.");
        return;
    }

    const char *p = params;
    while (*p && (*p == ' ' || *p == '\t')) p++;

    if (*p == '\0') {
        /* Generic Toggle */
        players[i].state.show_map = !players[i].state.show_map;
        players[i].state.map_filter = 0;
        players[i].state.energy -= 50;
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

        if (filter > 0) {
            players[i].state.show_map = 1;
            players[i].state.map_filter = filter;
            players[i].state.energy -= 50;
            char msg[128];
            char filter_name[3]; strncpy(filter_name, p, 2); filter_name[2] = '\0';
            sprintf(msg, "Galaxy Map Filter: %s ACTIVE.", filter_name);
            send_server_msg(i, "COMPUTER", msg);
        } else {
            send_server_msg(i, "COMPUTER", "Invalid map filter. Use: st,pl,bs,en,bh,ne,pu,is,co,as,de,mi,bu,pf,ri,mo");
        }
    }
}

void handle_und(int i, const char *params) {
    if (!players[i].is_docked) {
        send_server_msg(i, "COMPUTER", "Vessel is already in open space.");
        return;
    }

    /* 1. Hardware Requirement: Auxiliary (ID 9) >= 10% */
    if (players[i].state.system_health[9] < 10.0) {
        send_server_msg(i, "ENGINEERING", "UNDOCKING FAILURE: Docking clamp release mechanism non-functional.");
        return;
    }

    /* 2. Energy Cost: 50 units */
    if (players[i].state.energy < 50) {
        send_server_msg(i, "COMPUTER", "Insufficient energy for clamp retraction pulse.");
        return;
    }

    players[i].state.energy -= 50;
    players[i].is_docked = 0;
    send_server_msg(i, "STARBASE", "Docking clamps retracted. Vessel is clear. Engines online.");
}

void handle_aux(int i, const char *params) {
    const char *p_ptr = params;
    while(*p_ptr && (*p_ptr == ' ' || *p_ptr == '\t')) p_ptr++;
    
    /* 1. Hardware Requirement: Auxiliary (ID 9) >= 10% */
    if (players[i].state.system_health[9] < 10.0) {
        send_server_msg(i, "COMPUTER", "AUXILIARY FAILURE: Backup systems non-functional.");
        return;
    }

    if(*p_ptr == '\0' || strncmp(p_ptr, "status", 6) == 0) {
        if (players[i].state.energy < 10) { send_server_msg(i, "COMPUTER", "Insufficient energy for status diagnostic."); return; }
        players[i].state.energy -= 10;
        char buf[1024];
        sprintf(buf, "\n" CYAN ".--- AUXILIARY SYSTEMS: SENSOR PROBE NETWORK ---." RESET "\n"
                     " SLOT  STATUS       LOCATION        ETA/UPTIME\n"
                     "-------------------------------------------------\n");
        for(int p=0; p<3; p++) {
            char line[256];
            if(players[i].state.probes[p].active) {
                const char* st = (players[i].state.probes[p].status == 0) ? YELLOW "EN ROUTE" RESET : GREEN "ACTIVE  " RESET;
                sprintf(line, "  %d    %s    Q[%d,%d,%d]    %.1fs\n", p+1, st, 
                        players[i].state.probes[p].q1, players[i].state.probes[p].q2, players[i].state.probes[p].q3,
                        players[i].state.probes[p].eta);
            } else {
                sprintf(line, "  %d    " WHITE "AVAILABLE" RESET "  ---             ---\n", p+1);
            }
            strcat(buf, line);
        }
        strcat(buf, "-------------------------------------------------\n"
                    " Usage: aux probe <Q1> <Q2> <Q3> | aux report <1-3>\n"
                    "        aux recover <1-3> | aux jettison");
        send_server_msg(i, "SCIENCE", buf);
    } else if(strncmp(p_ptr, "jettison", 8) == 0) {
        /* 2. Jettison Energy Cost */
        if (players[i].state.energy < 1000) {
            send_server_msg(i, "ENGINEERING", "Insufficient energy for controlled core ejection (Req: 1000).");
            return;
        }
        send_server_msg(i, "CRITICAL", "EMERGENCY JETTISON INITIATED! HYPERDRIVE CORE EJECTED!");
        send_server_msg(i, "ENGINEERING", "Hyperdrive system is OFFLINE. Massive feedback damage detected.");
        
        players[i].state.energy -= 1000;
        players[i].state.system_health[0] = 0.0; /* Hyperdrive destroyed */
        players[i].nav_state = NAV_STATE_DRIFT;
        
        /* Massive damage to the ship */
        apply_hull_damage(i, 40.0); /* 40% hull damage + random system damage */
        
        /* Visual FX */
        players[i].state.boom = (NetPoint){players[i].state.s1, players[i].state.s2, players[i].state.s3, 1};
    } else if(strncmp(p_ptr, "probe", 5) == 0) {
        /* Skip 'probe' word and spaces */
        const char *coords = p_ptr + 5;
        while(*coords && (*coords == ' ' || *coords == '\t')) coords++;

        /* 3. Probe Hardware Requirements (Sensors ID 2, Computer ID 6 >= 25%) */
        if (players[i].state.system_health[2] < 25.0 || players[i].state.system_health[6] < 25.0) {
            send_server_msg(i, "SCIENCE", "PROBE LAUNCH FAILURE: Sensors and Computer must be above 25% integrity.");
            return;
        }
        if (players[i].state.energy < 1000) {
            send_server_msg(i, "COMPUTER", "Insufficient energy for probe launch (Req: 1000).");
            return;
        }

        int qx, qy, qz;
        if (sscanf(coords, "%d %d %d", &qx, &qy, &qz) == 3) {
            if (!IS_Q_VALID(qx, qy, qz)) { send_server_msg(i, "COMPUTER", "Invalid coordinates."); return; }
            int p_idx = -1;
            for(int p=0; p<3; p++) if(!players[i].state.probes[p].active) { p_idx = p; break; }
            if(p_idx == -1) { send_server_msg(i, "COMPUTER", "All probe slots active."); return; }
            
            players[i].state.energy -= 1000;
            players[i].state.probes[p_idx].active = 1;
            players[i].state.probes[p_idx].q1 = qx;
            players[i].state.probes[p_idx].q2 = qy;
            players[i].state.probes[p_idx].q3 = qz;
            players[i].state.probes[p_idx].gx = players[i].gx;
            players[i].state.probes[p_idx].gy = players[i].gy;
            players[i].state.probes[p_idx].gz = players[i].gz;
            
            double target_gx = (qx - 1) * 10.0 + 5.0;
            double target_gy = (qy - 1) * 10.0 + 5.0;
            double target_gz = (qz - 1) * 10.0 + 5.0;
            double dx = target_gx - players[i].state.probes[p_idx].gx;
            double dy = target_gy - players[i].state.probes[p_idx].gy;
            double dz = target_gz - players[i].state.probes[p_idx].gz;
            double dist = sqrtf(dx*dx + dy*dy + dz*dz);
            double time = dist / 3.33; if (time < 1.0) time = 1.0;
            
            players[i].state.probes[p_idx].vx = dx / (time * 30.0);
            players[i].state.probes[p_idx].vy = dy / (time * 30.0);
            players[i].state.probes[p_idx].vz = dz / (time * 30.0);
            players[i].state.probes[p_idx].eta = time;
            players[i].state.probes[p_idx].status = 0;
            
            char msg[128]; sprintf(msg, "Deep Space probe launched to [%d,%d,%d]. ETA: %.1f sec.", qx, qy, qz, time);
            send_server_msg(i, "SCIENCE", msg);
        } else send_server_msg(i, "COMPUTER", "Usage: aux probe <QX> <QY> <QZ>");
    } else if(strncmp(p_ptr, "report", 6) == 0) {
        if (players[i].state.energy < 50) { send_server_msg(i, "COMPUTER", "Insufficient energy for telemetry (Req: 50)."); return; }
        int p_idx;
        if (sscanf(p_ptr + 6, "%d", &p_idx) == 1) {
            p_idx--;
            if (p_idx < 0 || p_idx >= 3 || !players[i].state.probes[p_idx].active) { send_server_msg(i, "COMPUTER", "Probe not active."); return; }
            if (players[i].state.probes[p_idx].status == 0) { send_server_msg(i, "SCIENCE", "Probe still en route."); return; }
            
            players[i].state.energy -= 50;
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
                }
            }

            char msg[2048];
            sprintf(msg, "\n" CYAN ".--- PROBE P%d: REAL-TIME DEEP SPACE TELEMETRY ---." RESET "\n"
                         " QUADRANT: " WHITE "[%d, %d, %d]" RESET " | SECTOR: " YELLOW "[%.1f, %.1f, %.1f]" RESET "\n"
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
                         lq->player_count, lq->npc_count, lq->base_count, lq->star_count,
                         lq->planet_count, lq->bh_count,
                         n_st, n_he, n_dm, n_io, n_gr, n_te,
                         p_rp, p_ac, p_ma,
                         lq->comet_count, lq->asteroid_count, lq->platform_count, lq->derelict_count,
                         lq->buoy_count, lq->rift_count, lq->mine_count, lq->monster_count);
            send_server_msg(i, "SCIENCE", msg);
        } else send_server_msg(i, "COMPUTER", "Usage: aux report <1-3>");
    } else if(strncmp(p_ptr, "recover", 7) == 0) {
        int p_idx;
        if (sscanf(p_ptr + 7, "%d", &p_idx) == 1) {
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
            if (sqrtf(dx*dx + dy*dy + dz*dz) > 2.0) {
                send_server_msg(i, "COMPUTER", "Probe out of recovery range (> 2.0).");
                return;
            }

            players[i].state.recovery_fx = (NetPoint){players[i].state.probes[p_idx].s1, players[i].state.probes[p_idx].s2, players[i].state.probes[p_idx].s3, 10};
            players[i].state.probes[p_idx].active = 0; players[i].state.energy += 500;
            if (players[i].state.energy > 9999999) players[i].state.energy = 9999999;
            send_server_msg(i, "ENGINEERING", "Probe recovered. 500 Energy salvaged.");
        } else send_server_msg(i, "COMPUTER", "Usage: aux recover <1-3>");
    } else send_server_msg(i, "COMPUTER", "AUXILIARY: probe | report | recover | jettison");
}

/* --- Command Registry Table --- */

static const CommandDef command_registry[] = {
    {"nav", handle_nav, "Hyperdrive Navigation (H 0-359, M -90/90, W Dist, F Factor 1-9.9)"},
    {"imp", handle_imp, "Impulse Drive (H, M, Speed 0.0-1.0). imp 0 0 0 to stop."},
    {"pos", handle_pos, "Position Ship (Align orientation without movement)"},
    {"jum", handle_jum, "Wormhole Jump (Instant travel, costs 5000 En + 1 Aetherium)"},
    {"apr", handle_apr, "Approach target autopilot (ID DIST). Works on Lock."},
    {"cha",  handle_cha, "Chase locked target (Inter-sector aware)"},
    {"srs",  handle_srs, "Short Range Sensors (Current Quadrant View)"},
    {"lrs",  handle_lrs, "Long Range Sensors (LCARS Tactical Grid)"},
    {"pha", handle_pha, "Fire Ion Beams at locked target (uses Energy E)"},
    {"tor",  handle_tor, "Launch Plasma Torpedo at locked target or Heading/Mark"},
    {"she", handle_she, "Shield Configuration (F R T B L RI)"},
    {"lock", handle_lock, "Target Lock-on (0:Self, 1+:Nearby vessels)"},
    {"enc", handle_enc,  "Encryption Toggle (aes, chacha, aria, camellia, ..., pqc)"},
    {"pow", handle_pow,  "Power Allocation (Engines, Shields, Weapons %)"},
    {"psy",  handle_psy,  "Psychological Warfare (Anti-Matter Bluff)"},
    {"scan", handle_scan, "Detailed analysis of vessel or anomaly"},
    {"clo",  handle_clo, "Toggle Cloaking Device (Consumes constant Energy)"},
    {"bor",  handle_bor, "Boarding party operation (Dist < 1.0). Works on Lock."},
    {"dis",  handle_dis, "Dismantle enemy wreck/derelict (Dist < 1.5)"},
    {"min",  handle_min, "Planetary Mining (Must be in orbit dist < 2.0)"},
    {"sco",  handle_sco, "Solar scooping for energy"},
    {"har",  handle_har, "Antimatter harvest from Black Hole"},
    {"doc",  handle_doc, "Dock with Starbase (Replenish/Repair, same faction)"},
    {"con", handle_con, "Convert resources (1:Aeth->E, 2:Neo-Ti->E, 3:Void-E->Torps, 6:Gas->E, 7:Comp->E)"},
    {"load", handle_load, "Load from Cargo Bay (1:Energy, 2:Torps)"},
    {"rep",  handle_rep, "Repair System (Uses 50 Neo-Titanium + 10 Synaptics)"},
    {"fix",  handle_fix, "Field Hull Repair (50 Graphene + 20 Neo-Ti)"},
    {"sta",  handle_sta, "Mission Status Report"},
    {"inv",  handle_inv, "Cargo Inventory Report"},
    {"dam",  handle_dam, "Detailed Damage Report"},
    {"cal", handle_cal, "Hyperdrive Calc (Pinpoint Precision Route & ETA)"},
    {"ical", handle_ical, "Impulse Calculator (Sector ETA at current power)"},
    {"who",  handle_who, "List active captains in galaxy"},
    {"help", handle_help, "Display LCARS Command Directory"},
    {"aux", handle_aux, "Auxiliary (probe/report/recover/jettison)"},
    {"xxx",  handle_xxx, "Self-Destruct (WARNING!)"},
    {"hull", handle_hull, "Reinforce Hull (Uses 100 Composite for +500 Plating)"},
    {"supernova", handle_supernova, "Admin: Trigger Supernova"},
    {"axs",  handle_axs,  "Toggle AR Compass"},
    {"grd",  handle_grd,  "Toggle Tactical Grid"},
    {"bridge", handle_bridge, "Change Bridge View (top, bottom, up, down, left, right, rear, off)"},
    {"map",    handle_map,    "Toggle Galaxy Map with optional Filter"},
    {"red",    handle_red,    "Toggle Red Alert / Condition Green"},
    {"orb",    handle_orb,    "Enter planetary orbit (Must be near target planet < 1.0)"},
    {"und",    handle_und,    "Undock from Starbase"},
    {"undock", handle_und,    "Undock from Starbase (alias)"},
    {NULL, NULL, NULL}
};

void handle_red(int i, const char *params) {
    if (players[i].state.system_health[6] < 10.0) {
        send_server_msg(i, "COMPUTER", "RED ALERT FAILURE: Tactical coordination core damaged.");
        return;
    }
    players[i].state.red_alert = !players[i].state.red_alert;
    if (players[i].state.red_alert) {
        send_server_msg(i, "COMMAND", "RED ALERT! Shields energized. Weapons to standby.");
    } else {
        send_server_msg(i, "COMMAND", "Stand down to Condition Green.");
    }
}

void handle_orb(int i, const char *params) {
    if (players[i].state.system_health[1] < 10.0) {
        send_server_msg(i, "ENGINEERING", "ORBITAL ENTRY FAILURE: Maneuvering thrusters offline.");
        return;
    }
    int tid = players[i].state.lock_target;
    if (tid >= 3000 && tid < 3000 + MAX_PLANETS) {
        int p = tid - 3000;
        double dx = planets[p].x - players[i].state.s1;
        double dy = planets[p].y - players[i].state.s2;
        double dz = planets[p].z - players[i].state.s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        if (d < 1.0) {
            players[i].nav_state = NAV_STATE_ORBIT;
            send_server_msg(i, "HELMSMAN", "Establishing stable orbit around target planet.");
        } else send_server_msg(i, "COMPUTER", "Target planet too distant for orbital capture (< 1.0 required).");
    } else send_server_msg(i, "COMPUTER", "No planet locked for orbital entry.");
}

void handle_help(int i, const char *params) {
    char b[8192] = CYAN "\n--- LCARS COMMAND DIRECTORY ---" RESET "\n";
    for (int c = 0; command_registry[c].name != NULL; c++) {
        char line[256]; sprintf(line, WHITE "%-10s" RESET " : %s\n", command_registry[c].name, command_registry[c].description);
        if (strlen(b) + strlen(line) < sizeof(b) - 1) {
            strcat(b, line);
        }
    }
    send_server_msg(i, "COMPUTER", b);
}

void process_command(int i, const char *cmd) {
    pthread_mutex_lock(&game_mutex);
    
    /* 1. Intercept numeric input for pending boarding actions */
    if (players[i].pending_bor_target > 0) {
        if (strlen(cmd) == 1 && cmd[0] >= '1' && cmd[0] <= '4') {
            int choice = cmd[0] - '0';
            int tid = players[i].pending_bor_target;
            double tx=0, ty=0, tz=0;
            ConnectedPlayer *target_p = NULL;

            /* Resolve target position */
            if (tid <= 32) { target_p = &players[tid-1]; tx = target_p->state.s1; ty = target_p->state.s2; tz = target_p->state.s3; }
            else if (tid >= 1000 && tid < 1000 + MAX_NPC) { int n_idx = tid - 1000; tx = npcs[n_idx].x; ty = npcs[n_idx].y; tz = npcs[n_idx].z; }
            else if (tid >= 11000 && tid < 11000 + MAX_DERELICTS) { int d_idx = tid - 11000; tx = derelicts[d_idx].x; ty = derelicts[d_idx].y; tz = derelicts[d_idx].z; }
            else if (tid >= 16000) { int pt_idx = tid - 16000; tx = platforms[pt_idx].x; ty = platforms[pt_idx].y; tz = platforms[pt_idx].z; }

            /* Verify distance again during choice */
            double dx = tx - players[i].state.s1, dy = ty - players[i].state.s2, dz = tz - players[i].state.s3;
            if (sqrt(dx*dx+dy*dy+dz*dz) > 1.2) {
                send_server_msg(i, "COMPUTER", "Target out of range. Operation cancelled.");
            } else {
                if (players[i].pending_bor_type == 1) { /* ALLY PLAYER */
                    if (choice == 1) { players[i].state.energy -= 50000; target_p->state.energy += 50000; send_server_msg(i, "ENGINEERING", "Energy transferred."); }
                    else if (choice == 2) { int s = rand()%10; target_p->state.system_health[s] = 100.0; send_server_msg(i, "ENGINEERING", "Repairs complete."); }
                    else { 
                        int t_crew = (players[i].state.crew_count >= 20) ? 20 : players[i].state.crew_count;
                        players[i].state.crew_count -= t_crew; target_p->state.crew_count += t_crew; 
                        send_server_msg(i, "SECURITY", "Crew transferred."); 
                    }
                } else if (players[i].pending_bor_type == 2) { /* ENEMY PLAYER / NPC */
                    if (tid <= 32) {
                        if (choice == 1) { int s = rand()%10; target_p->state.system_health[s] = 0.0; send_server_msg(i, "BOARDING", "Sabotage successful."); }
                        else if (choice == 2) { int r = 1 + rand()%8; int a = target_p->state.inventory[r]/2; target_p->state.inventory[r] -= a; players[i].state.inventory[r] += a; send_server_msg(i, "BOARDING", "Resources seized."); }
                        else { 
                            int p = 5 + rand()%15; 
                            if (target_p->state.crew_count < p) p = target_p->state.crew_count;
                            target_p->state.crew_count -= p; players[i].state.prison_unit += p; 
                            send_server_msg(i, "SECURITY", "Hostages taken."); 
                        }
                    } else {
                        /* NPC Sabotage Effects */
                        NPCShip *target_npc = &npcs[tid-1000];
                        if (choice == 1) { target_npc->engine_health = 0.0; target_npc->energy -= 10000; send_server_msg(i, "BOARDING", "NPC propulsion core sabotaged. Vessell is drifting."); }
                        else if (choice == 2) { players[i].state.inventory[1 + rand()%7] += 50; send_server_msg(i, "BOARDING", "Raid successful. Secured NPC cargo."); }
                        else { 
                            int p = 10 + rand()%30; 
                            if (target_npc->health < p) p = target_npc->health; /* Per NPC usiamo health come approssimazione crew */
                            target_npc->health -= p;
                            players[i].state.prison_unit += p; 
                            send_server_msg(i, "SECURITY", "Captured enemy personnel."); 
                        }
                    }
                } else if (players[i].pending_bor_type == 3) { /* PLATFORM */
                    int pt_idx = tid - 16000;
                    if (choice == 1) { platforms[pt_idx].faction = players[i].faction; send_server_msg(i, "BOARDING", "Platform captured."); }
                    else if (choice == 2) { platforms[pt_idx].active = 0; players[i].state.boom = (NetPoint){platforms[pt_idx].x, platforms[pt_idx].y, platforms[pt_idx].z, 1}; send_server_msg(i, "BOARDING", "Platform destroyed."); }
                    else { players[i].state.inventory[5] += 250; send_server_msg(i, "BOARDING", "Tech salvaged."); }
                } else if (players[i].pending_bor_type == 4) { /* DERELICT WRECK */
                    
                    /* 1. Choice Specific Reward */
                    if (choice == 1) { /* Salvage extra resources */
                        int r = 1 + rand()%8; players[i].state.inventory[r] += 150; 
                        send_server_msg(i, "BOARDING", "Cargo hold breached. Significant resources recovered."); 
                    }
                    else if (choice == 2) { /* Recover Map Data */
                        int rev = 0; for(int r=0; r<10; r++) { int rq1=rand()%10+1, rq2=rand()%10+1, rq3=rand()%10+1; if (players[i].state.z[rq1][rq2][rq3] == 0) { players[i].state.z[rq1][rq2][rq3] = 1; rev++; } if (rev >= 4) break; }
                        send_server_msg(i, "BOARDING", "Navigational logs decrypted. Star-charts updated with new quadrants."); 
                    }
                    else if (choice == 3) { /* Emergency Field Repairs */
                        int s = rand()%10; players[i].state.system_health[s] = 100.0;
                        send_server_msg(i, "BOARDING", "Engineers recovered compatible spare parts. System fully restored."); 
                    }
                    else if (choice == 4) { /* Crew Rescue (Survivors) */
                        int found_crew = 10 + rand()%21;
                        players[i].state.crew_count += found_crew;
                        char msg[128]; sprintf(msg, "Search teams found %d survivors in stasis. They have been integrated into the crew.", found_crew);
                        send_server_msg(i, "SECURITY", msg);
                    }

                    /* 2. Removed automatic dismantling / collapse logic as requested.
                       The derelict remains active for further boarding or dismantling via 'dis' command. */
                }
            }
            players[i].pending_bor_target = 0;
            pthread_mutex_unlock(&game_mutex);
            return;
        } else players[i].pending_bor_target = 0;
    }

    /* 2. Docking Restrictions Logic */
    bool is_action = true;
    const char* allowed[] = {"rad", "sta", "inv", "dam", "who", "help", "cal", "ical", "map", "axs", "grd", "bridge", "enc", "und", NULL};
    for(int a=0; allowed[a]; a++) {
        if(strncmp(cmd, allowed[a], strlen(allowed[a])) == 0) { is_action = false; break; }
    }

    if (players[i].is_docked) {
        /* Any movement command will undock the ship */
        if (strncmp(cmd, "nav", 3) == 0 || strncmp(cmd, "imp", 3) == 0 || strncmp(cmd, "pos", 3) == 0 || strncmp(cmd, "jum", 3) == 0 || strncmp(cmd, "und", 3) == 0) {
            players[i].is_docked = 0;
            if (strncmp(cmd, "und", 3) != 0) {
                send_server_msg(i, "STARBASE", "Auto-Undock triggered. Docking clamps released.");
            }
        } 
        else if (is_action) {
            send_server_msg(i, "COMPUTER", "COMMAND DENIED: All tactical systems locked while docked. Release clamps by engaging engines (nav/imp/pos).");
            pthread_mutex_unlock(&game_mutex);
            return;
        }
    }

    bool found = false;
    for (int c = 0; command_registry[c].name != NULL; c++) {
        size_t len = strlen(command_registry[c].name);
        if (strncmp(cmd, command_registry[c].name, len) == 0) {
            command_registry[c].handler(i, cmd + len);
            found = true; break;
        }
    }
    if (!found) send_server_msg(i, "COMPUTER", "Invalid command.");
    pthread_mutex_unlock(&game_mutex);
}
