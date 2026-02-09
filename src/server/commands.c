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

/* Helper to get sensor error based on system health */
static double get_sensor_error(int p_idx) {
    if (is_near_buoy(p_idx)) return 0.0; /* Buoy provides perfect sensor calibration */
    float health = players[p_idx].state.system_health[2];
    if (health >= 100.0f) return 0.0;
    /* Noise increases exponentially as health drops */
    double noise_factor = pow(1.0 - (health / 100.0), 2.0);
    return ((rand() % 2000 - 1000) / 1000.0) * noise_factor * 2.5;
}

/* Type definition for command handlers */
typedef void (*CommandHandler)(int p_idx, const char *params);

void handle_help(int p_idx, const char *params);

typedef struct {
    const char *name;
    CommandHandler handler;
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
        send_server_msg(i, "COMPUTER", "Deep Space encryption: BLOWFISH-CBC (FERENGI) ACTIVE.");
    } else if (strstr(params, "rc4")) {
        players[i].crypto_algo = CRYPTO_RC4;
        send_server_msg(i, "COMPUTER", "Deep Space encryption: RC4-STREAM (TACTICAL) ACTIVE.");
    } else if (strstr(params, "des") && !strstr(params, "3des")) {
        players[i].crypto_algo = CRYPTO_DES;
        send_server_msg(i, "COMPUTER", "Deep Space encryption: DES-CBC (PRE-HYPERDRIVE) ACTIVE.");
    } else if (strstr(params, "pqc") || strstr(params, "kyber")) {
        players[i].crypto_algo = CRYPTO_PQC;
        send_server_msg(i, "COMPUTER", "Deep Space encryption: ML-KEM-1024 (POST-QUANTUM) ACTIVE.");
        send_server_msg(i, "SCIENCE", "Quantum Tunnel established. Communications are now immune to Shor's algorithm.");
    } else if (strstr(params, "off")) {
        players[i].crypto_algo = CRYPTO_NONE;
        send_server_msg(i, "COMPUTER", "WARNING: Encryption DISABLED. Signal is now RAW.");
    } else {
        send_server_msg(i, "COMPUTER", "Usage: enc aes | chacha | aria | camellia | seed | cast | idea | 3des | bf | rc4 | des | pqc | off");
    }
}

void handle_pow(int i, const char *params) {
    float e, s, w;
    if (sscanf(params, "%f %f %f", &e, &s, &w) == 3) {
        float total = e + s + w;
        if (total > 0) {
            players[i].state.power_dist[0] = e / total;
            players[i].state.power_dist[1] = s / total;
            players[i].state.power_dist[2] = w / total;
            send_server_msg(i, "ENGINEERING", "Power distribution updated.");
        } else {
            send_server_msg(i, "COMPUTER", "Invalid power distribution ratio.");
        }
    } else {
        send_server_msg(i, "COMPUTER", "Usage: pow <Engines> <Shields> <Weapons> (Percentages)");
    }
}

void handle_nav(int i, const char *params) {
    double h, m, w, factor = 6.0; /* Default Hyperdrive 6 */
    int args = sscanf(params, "%lf %lf %lf %lf", &h, &m, &w, &factor);
    if (args >= 3) {
        if (factor < 1.0) factor = 1.0;
        if (factor > 9.9) factor = 9.9;
        
        normalize_upright(&h, &m);
        players[i].target_h = h; players[i].target_m = m;
        players[i].start_h = players[i].state.van_h; players[i].start_m = players[i].state.van_m;
        double rad_h = h * M_PI / 180.0; double rad_m = m * M_PI / 180.0;
        players[i].dx = cos(rad_m) * sin(rad_h); players[i].dy = cos(rad_m) * -cos(rad_h); players[i].dz = sin(rad_m);
        players[i].target_gx = (players[i].state.q1-1)*10.0+players[i].state.s1+players[i].dx*w*10.0;
        players[i].target_gy = (players[i].state.q2-1)*10.0+players[i].state.s2+players[i].dy*w*10.0;
        players[i].target_gz = (players[i].state.q3-1)*10.0+players[i].state.s3+players[i].dz*w*10.0;
        
        /* Store warp factor in warp_speed temporarily to pass it to logic.c */
        players[i].warp_speed = factor; 
        players[i].nav_state = NAV_STATE_ALIGN; 
        
        double dh = players[i].target_h - players[i].state.van_h;
        while(dh>180) dh-=360; 
        while(dh<-180) dh+=360;
        if(fabs(dh)<1.0 && fabs(players[i].target_m - players[i].state.van_m)<1.0) players[i].nav_timer=10;
        else players[i].nav_timer = 60;
        
        char msg[128];
        sprintf(msg, "Course plotted. Aligning ship for Hyperdrive %.1f.", factor);
        send_server_msg(i, "HELMSMAN", msg);
    } else {
        send_server_msg(i, "COMPUTER", "Usage: nav <H> <M> <W> [Factor]");
    }
}

void handle_imp(int i, const char *params) {
    double h, m, s;
    int args = sscanf(params, "%lf %lf %lf", &h, &m, &s);
    if (args == 1) {
        /* Speed update only */
        players[i].warp_speed = h / 200.0; if (players[i].warp_speed > 0.5) players[i].warp_speed = 0.5;
        char msg[64]; sprintf(msg, "Impulse adjusted to %.0f%%.", players[i].warp_speed * 200.0);
        send_server_msg(i, "HELMSMAN", msg);
        players[i].nav_state = NAV_STATE_IMPULSE;
    } else if (args == 3) {
        normalize_upright(&h, &m);
        players[i].target_h = h; players[i].target_m = m;
        players[i].start_h = players[i].state.van_h; players[i].start_m = players[i].state.van_m;
        double rad_h = h * M_PI / 180.0; double rad_m = m * M_PI / 180.0;
        players[i].dx = cos(rad_m) * sin(rad_h); players[i].dy = cos(rad_m) * -cos(rad_h); players[i].dz = sin(rad_m);
        players[i].warp_speed = s / 200.0; if (players[i].warp_speed > 0.5) players[i].warp_speed = 0.5;
        players[i].nav_state = NAV_STATE_ALIGN_IMPULSE;
        
        double dh = players[i].target_h - players[i].state.van_h;
        while(dh>180) dh-=360; 
        while(dh<-180) dh+=360;
        if(fabs(dh)<1.0 && fabs(players[i].target_m - players[i].state.van_m)<1.0) players[i].nav_timer=10;
        else players[i].nav_timer = 60;
        
        send_server_msg(i, "HELMSMAN", "Course plotted. Aligning ship.");
    } else {
        send_server_msg(i, "COMPUTER", "Usage: imp <H> <M> <S> or imp <S>");
    }
}

void handle_apr(int i, const char *params) {
    int tid = 0; double tdist = 2.0; /* Default approach distance */
    int args = sscanf(params, " %d %lf", &tid, &tdist);
    
    if (args == 0) {
        tid = players[i].state.lock_target;
        tdist = 2.0;
    } else if (args == 1) {
        /* If only one number provided, check if it's likely a distance or an ID */
        if (tid < 100) { /* Likely a distance for the locked target */
            tdist = (double)tid;
            tid = players[i].state.lock_target;
        } else {
            /* Likely an ID, use default distance */
            tdist = 2.0;
        }
    }

    if (tid > 0) {
        double tx, ty, tz; bool found = false;
        int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
        
        /* 1. Players (Global) */
        if (tid >= 1 && tid <= 32) {
            int idx = tid - 1;
            if (players[idx].active) {
                tx = players[idx].gx; ty = players[idx].gy; tz = players[idx].gz;
                found = true;
            }
        }
        /* 2. NPCs (Global) */
        else if (tid >= 1000 && tid < 1000+MAX_NPC) {
            int idx = tid - 1000;
            if (npcs[idx].active) {
                tx = npcs[idx].gx; ty = npcs[idx].gy; tz = npcs[idx].gz;
                found = true;
            }
        }
        /* 3. Starbases (Local) */
        else if (tid >= 2000 && tid < 2000+MAX_BASES) {
            int idx = tid - 2000;
            if (bases[idx].active && bases[idx].q1==pq1 && bases[idx].q2==pq2 && bases[idx].q3==pq3) {
                tx = (bases[idx].q1-1)*10+bases[idx].x; ty = (bases[idx].q2-1)*10+bases[idx].y; tz = (bases[idx].q3-1)*10+bases[idx].z;
                found = true;
            }
        }
        /* 4. Planets (Local) */
        else if (tid >= 3000 && tid < 3000+MAX_PLANETS) {
            int idx = tid - 3000;
            if (planets[idx].active && planets[idx].q1==pq1 && planets[idx].q2==pq2 && planets[idx].q3==pq3) {
                tx = (planets[idx].q1-1)*10+planets[idx].x; ty = (planets[idx].q2-1)*10+planets[idx].y; tz = (planets[idx].q3-1)*10+planets[idx].z;
                found = true;
            }
        }
        /* 5. Stars (Local) */
        else if (tid >= 4000 && tid < 4000+MAX_STARS) {
            int idx = tid - 4000;
            if (stars_data[idx].active && stars_data[idx].q1==pq1 && stars_data[idx].q2==pq2 && stars_data[idx].q3==pq3) {
                tx = (stars_data[idx].q1-1)*10+stars_data[idx].x; ty = (stars_data[idx].q2-1)*10+stars_data[idx].y; tz = (stars_data[idx].q3-1)*10+stars_data[idx].z;
                found = true;
            }
        }
        /* 6. Black Holes (Local) */
        else if (tid >= 7000 && tid < 7000+MAX_BH) {
            int idx = tid - 7000;
            if (black_holes[idx].active && black_holes[idx].q1==pq1 && black_holes[idx].q2==pq2 && black_holes[idx].q3==pq3) {
                tx = (black_holes[idx].q1-1)*10+black_holes[idx].x; ty = (black_holes[idx].q2-1)*10+black_holes[idx].y; tz = (black_holes[idx].q3-1)*10+black_holes[idx].z;
                found = true;
            }
        }
        /* 7. Comets (Global) */
        else if (tid >= 10000 && tid < 10000+MAX_COMETS) {
            int idx = tid - 10000;
            if (comets[idx].active) {
                tx = (comets[idx].q1-1)*10+comets[idx].x; ty = (comets[idx].q2-1)*10+comets[idx].y; tz = (comets[idx].q3-1)*10+comets[idx].z;
                found = true;
            }
        }
        /* 8. Monsters (Global) */
        else if (tid >= 18000 && tid < 18000+MAX_MONSTERS) {
            int idx = tid - 18000;
            if (monsters[idx].active) {
                tx = (monsters[idx].q1-1)*10+monsters[idx].x; ty = (monsters[idx].q2-1)*10+monsters[idx].y; tz = (monsters[idx].q3-1)*10+monsters[idx].z;
                found = true;
            }
        }
        /* 9. Nebulas (Local) */
        else if (tid >= 8000 && tid < 8000+MAX_NEBULAS) {
            int idx = tid - 8000;
            if (nebulas[idx].active && nebulas[idx].q1==pq1 && nebulas[idx].q2==pq2 && nebulas[idx].q3==pq3) {
                tx = (nebulas[idx].q1-1)*10+nebulas[idx].x; ty = (nebulas[idx].q2-1)*10+nebulas[idx].y; tz = (nebulas[idx].q3-1)*10+nebulas[idx].z;
                found = true;
            }
        }
        /* 10. Pulsars (Local) */
        else if (tid >= 9000 && tid < 9000+MAX_PULSARS) {
            int idx = tid - 9000;
            if (pulsars[idx].active && pulsars[idx].q1==pq1 && pulsars[idx].q2==pq2 && pulsars[idx].q3==pq3) {
                tx = (pulsars[idx].q1-1)*10+pulsars[idx].x; ty = (pulsars[idx].q2-1)*10+pulsars[idx].y; tz = (pulsars[idx].q3-1)*10+pulsars[idx].z;
                found = true;
            }
        }
        /* 11. Derelicts (Local) */
        else if (tid >= 11000 && tid < 11000+MAX_DERELICTS) {
            int idx = tid - 11000;
            if (derelicts[idx].active && derelicts[idx].q1==pq1 && derelicts[idx].q2==pq2 && derelicts[idx].q3==pq3) {
                tx = (derelicts[idx].q1-1)*10+derelicts[idx].x; ty = (derelicts[idx].q2-1)*10+derelicts[idx].y; tz = (derelicts[idx].q3-1)*10+derelicts[idx].z;
                found = true;
            }
        }
        /* 12. Asteroids (Local) */
        else if (tid >= 12000 && tid < 12000+MAX_ASTEROIDS) {
            int idx = tid - 12000;
            if (asteroids[idx].active && asteroids[idx].q1==pq1 && asteroids[idx].q2==pq2 && asteroids[idx].q3==pq3) {
                tx = (asteroids[idx].q1-1)*10+asteroids[idx].x; ty = (asteroids[idx].q2-1)*10+asteroids[idx].y; tz = (asteroids[idx].q3-1)*10+asteroids[idx].z;
                found = true;
            }
        }
        /* 13. Mines (Local) */
        else if (tid >= 14000 && tid < 14000+MAX_MINES) {
            int idx = tid - 14000;
            if (mines[idx].active && mines[idx].q1==pq1 && mines[idx].q2==pq2 && mines[idx].q3==pq3) {
                tx = (mines[idx].q1-1)*10+mines[idx].x; ty = (mines[idx].q2-1)*10+mines[idx].y; tz = (mines[idx].q3-1)*10+mines[idx].z;
                found = true;
            }
        }
        /* 14. Buoys (Local) */
        else if (tid >= 15000 && tid < 15000+MAX_BUOYS) {
            int idx = tid - 15000;
            if (buoys[idx].active && buoys[idx].q1==pq1 && buoys[idx].q2==pq2 && buoys[idx].q3==pq3) {
                tx = (buoys[idx].q1-1)*10+buoys[idx].x; ty = (buoys[idx].q2-1)*10+buoys[idx].y; tz = (buoys[idx].q3-1)*10+buoys[idx].z;
                found = true;
            }
        }
        /* 15. Platforms (Local) */
        else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS) {
            int idx = tid - 16000;
            if (platforms[idx].active && platforms[idx].q1==pq1 && platforms[idx].q2==pq2 && platforms[idx].q3==pq3) {
                tx = (platforms[idx].q1-1)*10+platforms[idx].x; ty = (platforms[idx].q2-1)*10+platforms[idx].y; tz = (platforms[idx].q3-1)*10+platforms[idx].z;
                found = true;
            }
        }
        /* 16. Rifts (Local) */
        else if (tid >= 17000 && tid < 17000+MAX_RIFTS) {
            int idx = tid - 17000;
            if (rifts[idx].active && rifts[idx].q1==pq1 && rifts[idx].q2==pq2 && rifts[idx].q3==pq3) {
                tx = (rifts[idx].q1-1)*10+rifts[idx].x; ty = (rifts[idx].q2-1)*10+rifts[idx].y; tz = (rifts[idx].q3-1)*10+rifts[idx].z;
                found = true;
            }
        }

        if(found) {
            double cx = players[i].gx; double cy = players[i].gy; double cz = players[i].gz;
            double dx = tx - cx, dy = ty - cy, dz = tz - cz; double d = sqrt(dx*dx + dy*dy + dz*dz);
            if(d > tdist) {
                double h = atan2(dx, -dy) * 180.0 / M_PI; if(h < 0) h += 360; 
                double m = asin(dz/d) * 180.0 / M_PI;
                players[i].target_h = h; players[i].target_m = m; players[i].dx = dx/d; players[i].dy = dy/d; players[i].dz = dz/d;
                players[i].target_gx = cx + players[i].dx * (d - tdist); 
                players[i].target_gy = cy + players[i].dy * (d - tdist); 
                players[i].target_gz = cz + players[i].dz * (d - tdist);
                players[i].nav_state = NAV_STATE_ALIGN; players[i].nav_timer = 60; 
                players[i].start_h = players[i].state.van_h; players[i].start_m = players[i].state.van_m;
                send_server_msg(i, "HELMSMAN", "Autopilot engaged. Approaching target.");
            } else send_server_msg(i, "COMPUTER", "Target already in range.");
        } else send_server_msg(i, "COMPUTER", "Target not identified or out of sensor range.");
    } else {
        send_server_msg(i, "COMPUTER", "Usage: apr <ID> <DIST>");
    }
}

void handle_cha(int i, const char *params) {
    if (players[i].state.lock_target > 0) {
        players[i].nav_state = NAV_STATE_CHASE;
        send_server_msg(i, "HELMSMAN", "Chase mode engaged. Intercepting target vector.");
    } else {
        send_server_msg(i, "COMPUTER", "Unable to comply. No target locked.");
    }
}

void handle_srs(int i, const char *params) {
    char *b = calloc(LARGE_DATA_BUFFER, sizeof(char));
    if (!b) return;

    int q1=players[i].state.q1, q2=players[i].state.q2, q3=players[i].state.q3; 
    double s1=players[i].state.s1, s2=players[i].state.s2, s3=players[i].state.s3;

    snprintf(b, LARGE_DATA_BUFFER, CYAN "\n--- SHORT RANGE SENSOR ANALYSIS ---" RESET "\nQUADRANT: [%d,%d,%d] | SECTOR: [%.1f,%.1f,%.1f]\n", q1, q2, q3, s1, s2, s3);
    snprintf(b+strlen(b), LARGE_DATA_BUFFER-strlen(b), "ENERGY: %d | TORPEDOES: %d | STATUS: %s\n", players[i].state.energy, players[i].state.torpedoes, players[i].state.is_cloaked ? MAGENTA "CLOAKED" RESET : GREEN "NORMAL" RESET);
    strncat(b, "\nTYPE       ID    POSITION      DIST   H / M         DETAILS\n", LARGE_DATA_BUFFER-strlen(b)-1);

    QuadrantIndex *local_q = &spatial_index[q1][q2][q3];
    int locked_id = players[i].state.lock_target;
    bool chasing = (players[i].nav_state == NAV_STATE_CHASE);
    float sensor_h = players[i].state.system_health[2];

    /* 1. Players */
    for(int j=0; j<local_q->player_count; j++) {
        ConnectedPlayer *p = local_q->players[j]; if (p == &players[i] || p->state.is_cloaked) continue;
        
        /* Chance to miss object if sensors are very damaged */
        if (sensor_h < 30.0f && (rand()%100 > sensor_h + 50)) continue;

        double dx=p->state.s1-s1 + get_sensor_error(i), dy=p->state.s2-s2 + get_sensor_error(i), dz=p->state.s3-s3 + get_sensor_error(i); 
        double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=asin(dz/d)*180/M_PI;
        int pid = (int)(p-players)+1;
        char status[64] = "";
        if (pid == locked_id) { strcat(status, RED "[LOCKED]" RESET); if(chasing) strcat(status, B_RED "[CHASE]" RESET); }
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     %s (Player) [E:%d] %s\n", "Vessel", pid, p->state.s1 + get_sensor_error(i), p->state.s2 + get_sensor_error(i), p->state.s3 + get_sensor_error(i), d, h, m, p->name, p->state.energy, status);
    }

    /* 2. NPC Ships */
    for(int n=0; n<local_q->npc_count; n++) {
        NPCShip *npc = local_q->npcs[n];
        if (sensor_h < 30.0f && (rand()%100 > sensor_h + 50)) continue;

        double dx=npc->x-s1 + get_sensor_error(i), dy=npc->y-s2 + get_sensor_error(i), dz=npc->z-s3 + get_sensor_error(i); 
        double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=asin(dz/d)*180/M_PI;
        int nid = npc->id+1000;
        char status[64] = "";
        if (nid == locked_id) { strcat(status, RED "[LOCKED]" RESET); if(chasing) strcat(status, B_RED "[CHASE]" RESET); }
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     %s [E:%d] [Engines:%.0f%%] %s\n", "Vessel", nid, npc->x + get_sensor_error(i), npc->y + get_sensor_error(i), npc->z + get_sensor_error(i), d, h, m, get_species_name(npc->faction), npc->energy, npc->engine_health, status);
    }

    /* 3. Starbases */
    for(int b_idx=0; b_idx<local_q->base_count; b_idx++) {
        NPCBase *ba = local_q->bases[b_idx];
        double dx=ba->x-s1, dy=ba->y-s2, dz=ba->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int baid = ba->id+2000;
        char status[64] = "";
        if (baid == locked_id) { strcat(status, RED "[LOCKED]" RESET); if(chasing) strcat(status, B_RED "[CHASE]" RESET); }
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Alliance Starbase %s\n", "Starbase", baid, ba->x, ba->y, ba->z, d, h, m, status);
    }

    /* 4. Planets */
    for(int p_idx=0; p_idx<local_q->planet_count; p_idx++) {
        NPCPlanet *pl = local_q->planets[p_idx];
        double dx=pl->x-s1, dy=pl->y-s2, dz=pl->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int plid = pl->id+3000;
        char status[64] = ""; if (plid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Class-M Planet %s\n", "Planet", plid, pl->x, pl->y, pl->z, d, h, m, status);
    }

    /* 5. Stars */
    for(int s_idx=0; s_idx<local_q->star_count; s_idx++) {
        NPCStar *st = local_q->stars[s_idx];
        double dx=st->x-s1, dy=st->y-s2, dz=st->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int sid = st->id+4000;
        char status[64] = ""; if (sid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Star %s\n", "Star", sid, st->x, st->y, st->z, d, h, m, status);
    }

    /* 6. Black Holes */
    for(int h_idx=0; h_idx<local_q->bh_count; h_idx++) {
        NPCBlackHole *bh = local_q->black_holes[h_idx];
        double dx=bh->x-s1, dy=bh->y-s2, dz=bh->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double hh=atan2(dx,-dy)*180/M_PI; if(hh<0)hh+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int bid = bh->id+7000;
        char status[64] = ""; if (bid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Black Hole (Grav Pull) %s\n", "B-Hole", bid, bh->x, bh->y, bh->z, d, hh, m, status);
    }

    /* 7. Nebulas */
    for(int n_idx=0; n_idx<local_q->nebula_count; n_idx++) {
        NPCNebula *nb = local_q->nebulas[n_idx];
        double dx=nb->x-s1, dy=nb->y-s2, dz=nb->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int nid = nb->id+8000;
        char status[64] = ""; if (nid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        const char *neb_names[] = {"Mutara Class", "Metreon Class", "Dark Matter Cloud Class", "Paulson Class", "McAllister Class", "Arachnia Class"};
        const char *n_name = (nb->type >= 0 && nb->type < 6) ? neb_names[nb->type] : "Unknown";
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     %s %s\n", "Tactical Cruiser", nid, nb->x, nb->y, nb->z, d, h, m, n_name, status);
    }

    /* 8. Pulsars */
    for (int p_idx=0; p_idx<local_q->pulsar_count; p_idx++) {
        NPCPulsar *pu = local_q->pulsars[p_idx];
        double dx=pu->x-s1, dy=pu->y-s2, dz=pu->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int puid = pu->id+9000;
        char status[64] = ""; if (puid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Pulsar (Radiation) %s\n", "Pulsar", puid, pu->x, pu->y, pu->z, d, h, m, status);
    }

    /* 9. Comets */
    for (int c_idx=0; c_idx<local_q->comet_count; c_idx++) {
        NPCComet *co = local_q->comets[c_idx];
        double dx=co->x-s1, dy=co->y-s2, dz=co->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int cid = co->id+10000;
        char status[64] = ""; if (cid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Comet (Energy Source) %s\n", "Comet", cid, co->x, co->y, co->z, d, h, m, status);
    }

    /* 10. Asteroids */
    for (int a_idx = 0; a_idx < local_q->asteroid_count; a_idx++) {
        NPCAsteroid *as = local_q->asteroids[a_idx];
        double dx=as->x-s1, dy=as->y-s2, dz=as->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int aid = as->id+12000;
        char status[64] = ""; if (aid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Asteroid (Hazard) %s\n", "Asteroid", aid, as->x, as->y, as->z, d, h, m, status);
    }

    /* 11. Monsters */
    for (int m_idx = 0; m_idx < local_q->monster_count; m_idx++) {
        NPCMonster *mo = local_q->monsters[m_idx];
        double dx=mo->x-s1, dy=mo->y-s2, dz=mo->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int moid = mo->id+18000;
        char status[64] = ""; if (moid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     %s %s\n", "Monster", moid, mo->x, mo->y, mo->z, d, h, m, (mo->type==30)?"Crystalline Entity":"Space Amoeba", status);
    }

    /* 12. Deep Space Probes (Global) */
    for (int p_j = 0; p_j < MAX_CLIENTS; p_j++) {
        if (!players[p_j].socket) continue;
        for (int pr = 0; pr < 3; pr++) {
            if (players[p_j].state.probes[pr].active) {
                int pr_q1 = get_q_from_g(players[p_j].state.probes[pr].gx);
                int pr_q2 = get_q_from_g(players[p_j].state.probes[pr].gy);
                int pr_q3 = get_q_from_g(players[p_j].state.probes[pr].gz);
                
                if (pr_q1 == q1 && pr_q2 == q2 && pr_q3 == q3) {
                    float ps1 = players[p_j].state.probes[pr].s1;
                    float ps2 = players[p_j].state.probes[pr].s2;
                    float ps3 = players[p_j].state.probes[pr].s3;
                    double dx = ps1 - s1, dy = ps2 - s2, dz = ps3 - s3;
                    double d = sqrt(dx*dx + dy*dy + dz*dz);
                    double h = atan2(dx,-dy)*180/M_PI; if(h<0)h+=360;
                    double m = (d > 0.001) ? asin(dz/d)*180/M_PI : 0;
                    int prid = 19000 + (p_j * 3) + pr;
                    const char *st = (players[p_j].state.probes[pr].status == 2) ? RED "DERELICT" RESET : CYAN "ACTIVE" RESET;
                    char status[64] = ""; if (prid == locked_id) strcat(status, RED "[LOCKED]" RESET);
                    SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Deep Space Probe (%s) %s %s\n", "Probe", prid, ps1, ps2, ps3, d, h, m, players[p_j].name, st, status);
                }
            }
        }
    }

    /* 12. Derelicts */
    for (int d_idx = 0; d_idx < local_q->derelict_count; d_idx++) {
        NPCDerelict *dr = local_q->derelicts[d_idx];
        double dx=dr->x-s1, dy=dr->y-s2, dz=dr->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int drid = dr->id+11000;
        char status[64] = ""; if (drid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Derelict Ship %s\n", "Derelict", drid, dr->x, dr->y, dr->z, d, h, m, status);
    }

    /* 13. Defense Platforms */
    for (int pt_idx = 0; pt_idx < local_q->platform_count; pt_idx++) {
        NPCPlatform *pt = local_q->platforms[pt_idx];
        double dx=pt->x-s1, dy=pt->y-s2, dz=pt->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int ptid = pt->id+16000;
        char status[64] = ""; if (ptid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Defense Platform %s\n", "Platform", ptid, pt->x, pt->y, pt->z, d, h, m, status);
    }

    /* 14. Rifts, Buoys, Mines */
    for (int rf_idx = 0; rf_idx < local_q->rift_count; rf_idx++) {
        NPCRift *rf = local_q->rifts[rf_idx];
        double dx=rf->x-s1, dy=rf->y-s2, dz=rf->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int rfid = rf->id+17000;
        char status[64] = ""; if (rfid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Spatial Rift %s\n", "Rift", rfid, rf->x, rf->y, rf->z, d, h, m, status);
    }
    for (int bu_idx = 0; bu_idx < local_q->buoy_count; bu_idx++) {
        NPCBuoy *bu = local_q->buoys[bu_idx];
        double dx=bu->x-s1, dy=bu->y-s2, dz=bu->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m_val=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int buid = bu->id+15000;
        char status[64] = ""; if (buid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Comm Buoy %s\n", "Buoy", buid, bu->x, bu->y, bu->z, d, h, m_val, status);
    }
    for (int mi_idx = 0; mi_idx < local_q->mine_count; mi_idx++) {
        NPCMine *mi = local_q->mines[mi_idx];
        double dx=mi->x-s1, dy=mi->y-s2, dz=mi->z-s3; double d=sqrt(dx*dx+dy*dy+dz*dz); double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m_val=(d>0.001)?asin(dz/d)*180/M_PI:0;
        int miid = mi->id+14000;
        char status[64] = ""; if (miid == locked_id) strcat(status, RED "[LOCKED]" RESET);
        SAFE_APPEND(b, LARGE_DATA_BUFFER, "%-10s %-5d [%.1f,%.1f,%.1f] %-5.1f %03.0f / %+03.0f     Cloaked Mine %s\n", "Mine", miid, mi->x, mi->y, mi->z, d, h, m_val, status);
    }

    /* 15. Neighborhood Scan (Inter-Quadrant awareness) */
    bool found_neighbor = false;
    char neighbor_buf[4096] = "";
    if (s1 < 2.5 || s1 > 7.5 || s2 < 2.5 || s2 > 7.5 || s3 < 2.5 || s3 > 7.5) {
        for (int dq1 = -1; dq1 <= 1; dq1++) {
            for (int dq2 = -1; dq2 <= 1; dq2++) {
                for (int dq3 = -1; dq3 <= 1; dq3++) {
                    if (dq1 == 0 && dq2 == 0 && dq3 == 0) continue;
                    int nq1 = q1 + dq1, nq2 = q2 + dq2, nq3 = q3 + dq3;
                    if (!IS_Q_VALID(nq1, nq2, nq3)) continue;
                    QuadrantIndex *nq = &spatial_index[nq1][nq2][nq3];
                    double off_x = dq1 * 10.0, off_y = dq2 * 10.0, off_z = dq3 * 10.0;
                    
                    /* Scan for mobile entities and beacons only */
                    for(int n=0; n<nq->npc_count; n++) {
                        NPCShip *npc = nq->npcs[n]; if (!npc->active) continue;
                        double dx=(npc->x+off_x)-s1, dy=(npc->y+off_y)-s2, dz=(npc->z+off_z)-s3;
                        double d=sqrt(dx*dx+dy*dy+dz*dz); if (d > 8.0) continue;
                        double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=asin(dz/d)*180/M_PI;
                        if(!found_neighbor) { sprintf(neighbor_buf, YELLOW "\n--- NEIGHBORHOOD SENSOR SCAN (Adjacent Quadrants) ---" RESET "\nTYPE       ID    QUADRANT      DIST   H / M         DETAILS\n"); found_neighbor=true; }
                        char line[256]; sprintf(line, "%-10s %-5d [%d,%d,%d] %-5.1f %03.0f / %+03.0f     %s (NPC)\n", "Vessel", npc->id+1000, nq1, nq2, nq3, d, h, m, get_species_name(npc->faction));
                        strcat(neighbor_buf, line);
                    }
                    for(int c=0; c<nq->comet_count; c++) {
                        NPCComet *co = nq->comets[c]; if (!co->active) continue;
                        double dx=(co->x+off_x)-s1, dy=(co->y+off_y)-s2, dz=(co->z+off_z)-s3;
                        double d=sqrt(dx*dx+dy*dy+dz*dz); if (d > 8.0) continue;
                        double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=asin(dz/d)*180/M_PI;
                        if(!found_neighbor) { sprintf(neighbor_buf, YELLOW "\n--- NEIGHBORHOOD SENSOR SCAN (Adjacent Quadrants) ---" RESET "\nTYPE       ID    QUADRANT      DIST   H / M         DETAILS\n"); found_neighbor=true; }
                        char line[256]; sprintf(line, "%-10s %-5d [%d,%d,%d] %-5.1f %03.0f / %+03.0f     Comet\n", "Comet", co->id+10000, nq1, nq2, nq3, d, h, m);
                        strcat(neighbor_buf, line);
                    }
                    for(int bu=0; bu<nq->buoy_count; bu++) {
                        NPCBuoy *buoy = nq->buoys[bu]; if (!buoy->active) continue;
                        double dx=(buoy->x+off_x)-s1, dy=(buoy->y+off_y)-s2, dz=(buoy->z+off_z)-s3;
                        double d=sqrt(dx*dx+dy*dy+dz*dz); if (d > 8.0) continue;
                        double h=atan2(dx,-dy)*180/M_PI; if(h<0)h+=360; double m=asin(dz/d)*180/M_PI;
                        if(!found_neighbor) { sprintf(neighbor_buf, YELLOW "\n--- NEIGHBORHOOD SENSOR SCAN (Adjacent Quadrants) ---" RESET "\nTYPE       ID    QUADRANT      DIST   H / M         DETAILS\n"); found_neighbor=true; }
                        char line[256]; sprintf(line, "%-10s %-5d [%d,%d,%d] %-5.1f %03.0f / %+03.0f     Comm Buoy\n", "Buoy", buoy->id+15000, nq1, nq2, nq3, d, h, m);
                        strcat(neighbor_buf, line);
                    }
                }
            }
        }
    }
    if(found_neighbor) SAFE_APPEND(b, LARGE_DATA_BUFFER, "%s", neighbor_buf);

    #undef SAFE_APPEND
    send_server_msg(i, "TACTICAL", b);
    free(b);
}

void handle_lrs(int i, const char *params) {
    char *b = calloc(LARGE_DATA_BUFFER, sizeof(char));
    if (!b) return;

    int q1=players[i].state.q1, q2=players[i].state.q2, q3=players[i].state.q3;
    double s1=players[i].state.s1, s2=players[i].state.s2, s3=players[i].state.s3;
    
    snprintf(b, LARGE_DATA_BUFFER, B_CYAN "\n.--- LCARS LONG RANGE TACTICAL SENSORS --------------------------------------.\n" RESET);
    char status[256];
    sprintf(status, WHITE " POS: [%d,%d,%d] SECTOR: [%.1f,%.1f,%.1f] | HDG: %03.0f MRK: %+03.0f\n" RESET, 
            q1, q2, q3, s1, s2, s3, players[i].state.van_h, players[i].state.van_m);
    strcat(b, status);
    strcat(b, B_CYAN "'------------------------------------------------------------------------------'\n" RESET);
    strcat(b, " DATA: [ H:B-Hole P:Planet N:NPC B:Base S:Star ] (Counts only near Buoys)\n");
    strcat(b, " Symbols: ~:Nebula *:Pulsar +:Comet #:Asteroid M:Monster >:Rift\n\n");

    const char* section_names[] = {"[ ULTRA-LONG DEPTH +2 ]", "[ LONG RANGE DEPTH +1 ]", "[ LOCAL TACTICAL ZONE 0 ]", "[ LONG RANGE DEPTH -1 ]", "[ ULTRA-LONG DEPTH -2 ]"};
    const char* section_colors[] = {B_MAGENTA, B_GREEN, B_YELLOW, B_RED, B_BLUE};

    /* Check for buoy proximity for sensor boost */
    bool sensor_boost = is_near_buoy(i);

    for (int section = 0; section < 5; section++) {
        int dq3 = 2 - section; /* From +2 down to -2 */
        int nq3 = q3 + dq3;
        
        if (nq3 < 1 || nq3 > 10) continue;

        /* RESTRICTION: Ultra-long range (+2/-2) only visible with sensor boost */
        if (!sensor_boost && abs(dq3) > 1) continue;

        char header[128];
        sprintf(header, "%s%s (Level Z:%d)" RESET "\n", section_colors[section], section_names[section], nq3);
        strcat(b, header);
        strcat(b, "  QUADRANT      NAV (H/M/W)    OBJECTS [H P N B S]  ANOMALIES\n");

        for (int dq2 = -2; dq2 <= 2; dq2++) {
            /* RESTRICTION: Far rows only with boost */
            if (!sensor_boost && abs(dq2) > 1) continue;

            for (int dq1 = -2; dq1 <= 2; dq1++) {
                /* RESTRICTION: Far columns only with boost */
                if (!sensor_boost && abs(dq1) > 1) continue;

                int nq1 = q1 + dq1;
                int nq2 = q2 + dq2;
                char row[1024];
                float sensor_h = players[i].state.system_health[2];
                
                if (IS_Q_VALID(nq1, nq2, nq3)) {
                    long long v = spacegl_master.g[nq1][nq2][nq3];
                    
                    /* Scramble data if sensors are damaged */
                    if (sensor_h < 50.0f && (rand()%100 > sensor_h)) {
                        v = (v / 10) + (rand()%9);
                    }

                    int s=v%10, b_cnt=(v/10)%10, k=(v/100)%10, p=(v/1000)%10, bh=(v/10000)%10;
                    int neb=(v/100000)%10, pul=(v/1000000)%10, com=(v/100000000)%10, ast=(v/1000000000)%10;
                    int mon=(v/10000000000000000LL)%10;
                    int rift=(v/100000000000000LL)%10;
                    
                    /* Navigation Solution */
                    double dx = (nq1 - q1) * 10.0; double dy = (nq2 - q2) * 10.0; double dz = (nq3 - q3) * 10.0;
                    double dist = sqrt(dx*dx + dy*dy + dz*dz);
                    double h_v = 0, m_v = 0;
                    if (dist > 0.01) { h_v = atan2(dx, -dy) * 180.0 / M_PI; if(h_v < 0) h_v += 360; m_v = asin(dz / dist) * 180.0 / M_PI; }

                                        char nav_info[64], obj_info[128], an_info[32] = "";

                                        

                                        /* Quadrant and Navigation */

                                        if (nq1==q1 && nq2==q2 && nq3==q3)

                                            snprintf(nav_info, sizeof(nav_info), B_BLUE "[%2d,%2d,%d]" RESET "  *-CURRENT-* ", nq1, nq2, nq3);

                                        else {

                                            if (sensor_h < 40.0f && (rand()%100 > sensor_h + 30))

                                                snprintf(nav_info, sizeof(nav_info), WHITE "[ ?,?,?]" RESET "  ERR/ER/W?   ");

                                            else

                                                snprintf(nav_info, sizeof(nav_info), WHITE "[%2d,%2d,%d]" RESET "  %03.0f/%+03.0f/W%.1f ", nq1, nq2, nq3, h_v, m_v, dist/10.0);

                                        }

                    

                                        /* Objects [ H P N B S ] */

                                        char h_s[16], p_s[16], n_s[16], b_s[16], s_s[16];

                                        if (sensor_h < 25.0f && (rand()%100 > 50)) {

                                            strcpy(h_s, "?"); strcpy(p_s, "?"); strcpy(n_s, "?"); strcpy(b_s, "?"); strcpy(s_s, "?");

                                        } else if (sensor_boost || (nq1 == q1 && nq2 == q2 && nq3 == q3)) {

                                            if(bh>0) snprintf(h_s, sizeof(h_s), "%s%d" RESET, MAGENTA, bh); else strcpy(h_s, ".");

                                            if(p>0) snprintf(p_s, sizeof(p_s), "%s%d" RESET, CYAN, p); else strcpy(p_s, ".");

                                            if(k>0) snprintf(n_s, sizeof(n_s), "%s%d" RESET, RED, k); else strcpy(n_s, ".");

                                            if(b_cnt>0) snprintf(b_s, sizeof(b_s), "%s%d" RESET, GREEN, b_cnt); else strcpy(b_s, ".");

                                            if(s>0) snprintf(s_s, sizeof(s_s), "%s%d" RESET, YELLOW, s); else strcpy(s_s, ".");

                                        } else {

                                            if(bh>0) snprintf(h_s, sizeof(h_s), "%sH" RESET, MAGENTA); else strcpy(h_s, ".");

                                            if(p>0)  snprintf(p_s, sizeof(p_s), "%sP" RESET, CYAN);    else strcpy(p_s, ".");

                                            if(k>0)  snprintf(n_s, sizeof(n_s), "%sN" RESET, RED);     else strcpy(n_s, ".");

                                            if(b_cnt>0) snprintf(b_s, sizeof(b_s), "%sB" RESET, GREEN); else strcpy(b_s, ".");

                                            if(s>0)  snprintf(s_s, sizeof(s_s), "%sS" RESET, YELLOW);  else strcpy(s_s, ".");

                                        }

                                        snprintf(obj_info, sizeof(obj_info), "[%s %s %s %s %s" RESET "]", h_s, p_s, n_s, b_s, s_s);

                    

                                        /* Anomalies */

                                        if(neb>0)  strcat(an_info, "~"); 

                                        if(pul>0)  strcat(an_info, "*"); 

                                        if(com>0)  strcat(an_info, "+");

                                        if(ast>0)  strcat(an_info, "#"); 

                                        if(mon>0)  strcat(an_info, "M"); 

                                        if(rift>0) strcat(an_info, ">");

                    

                                        snprintf(row, sizeof(row), "  %s  %-18s  %-4s\n", nav_info, obj_info, an_info);

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
        /* User provided only energy, use current lock target */
        e = tid;
        tid = players[i].state.lock_target;
        if (tid == 0) {
            send_server_msg(i, "COMPUTER", "No target locked. Usage: pha <ID> <E> or lock a target.");
            return;
        }
    } else if (args != 2) {
        send_server_msg(i, "COMPUTER", "Usage: pha <ID> <E> or pha <E> (requires lock)");
        return;
    }

    if (players[i].state.energy < e) { send_server_msg(i, "COMPUTER", "Insufficient energy for Ion Beam burst."); return; }
    if (players[i].state.ion_beam_charge < 10.0f) { send_server_msg(i, "TACTICAL", "Ion Beams banks recharging. Wait for capacitor."); return; }
    if (players[i].state.is_cloaked) { send_server_msg(i, "TACTICAL", "Cannot fire Ion Beams while cloaked. Decloak first."); return; }
    
    players[i].state.energy -= e;
    float charge_consumed = (e / 5000.0f) * 100.0f;
    if (charge_consumed < 15.0f) charge_consumed = 15.0f;
    if (charge_consumed > players[i].state.ion_beam_charge) charge_consumed = players[i].state.ion_beam_charge;
    players[i].state.ion_beam_charge -= charge_consumed;
    
    double tx, ty, tz; bool found = false;
    int pq1=players[i].state.q1, pq2=players[i].state.q2, pq3=players[i].state.q3;
    
    if (tid >= 1 && tid <= 32 && players[tid-1].active && players[tid-1].state.q1 == pq1 && players[tid-1].state.q2 == pq2 && players[tid-1].state.q3 == pq3) { tx=players[tid-1].state.s1; ty=players[tid-1].state.s2; tz=players[tid-1].state.s3; found=true; }
    else if (tid >= 1000 && tid < 1000+MAX_NPC && npcs[tid-1000].active && npcs[tid-1000].q1 == pq1 && npcs[tid-1000].q2 == pq2 && npcs[tid-1000].q3 == pq3) { tx=npcs[tid-1000].x; ty=npcs[tid-1000].y; tz=npcs[tid-1000].z; found=true; }
    else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS && platforms[tid-16000].active && platforms[tid-16000].q1 == pq1 && platforms[tid-16000].q2 == pq2 && platforms[tid-16000].q3 == pq3) { tx=platforms[tid-16000].x; ty=platforms[tid-16000].y; tz=platforms[tid-16000].z; found=true; }
    else if (tid >= 18000 && tid < 18000+MAX_MONSTERS && monsters[tid-18000].active && monsters[tid-18000].q1 == pq1 && monsters[tid-18000].q2 == pq2 && monsters[tid-18000].q3 == pq3) { tx=monsters[tid-18000].x; ty=monsters[tid-18000].y; tz=monsters[tid-18000].z; found=true; }
    
    if (found) {
        double dx=tx-players[i].state.s1, dy=ty-players[i].state.s2, dz=tz-players[i].state.s3; double dist=sqrt(dx*dx+dy*dy+dz*dz);
        if (dist < 0.1) dist = 0.1;
        /* Damage Scales with Weapons Power (0.0 - 1.0). Baseline is 1x, max is 3x */
        float weapon_mult = 0.5f + (players[i].state.power_dist[2] * 2.5f);
        int hit = (int)((e / dist) * (players[i].state.system_health[4] / 100.0f) * weapon_mult);
        players[i].state.beam_count = 1; 
        players[i].state.beams[0] = (NetBeam){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, (float)tx, (float)ty, (float)tz, 1};
        if (tid <= 32) {
            ConnectedPlayer *target = &players[tid-1];
            /* Calculate relative angle to determine which shield quadrant is hit */
            double rel_dx = players[i].state.s1 - target->state.s1;
            double rel_dy = players[i].state.s2 - target->state.s2;
            double angle = atan2(rel_dx, -rel_dy) * 180.0 / M_PI; if (angle < 0) angle += 360;
            /* Normalize relative to target heading */
            double rel_angle = angle - target->state.van_h;
            while (rel_angle < 0) rel_angle += 360;
            while (rel_angle >= 360) rel_angle -= 360;

            /* Full 3D Shield Mapping: 0:F, 1:R, 2:T, 3:B, 4:L, 5:RI */
            double rel_dz = players[i].state.s3 - target->state.s3;
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

            int dmg_rem = hit;
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
                float hull_dmg = dmg_rem / 1000.0f;
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
                    sprintf(alert, "CRITICAL: Direct hull impact! %s system damaged!", sys_names[sys_idx]);
                    send_server_msg(tid-1, "DAMAGE", alert);
                }

                target->state.energy -= dmg_rem / 2;
            }

            target->shield_regen_delay = 90; /* 3 seconds at 30Hz */
            
            /* Renegade Status: Ion Beam friendly fire */
            if (target->faction == players[i].faction) {
                players[i].renegade_timer = 18000;
                send_server_msg(i, "CRITICAL", "UNAUTHORIZED Ion Beam FIRE ON ALLY! YOU ARE NOW A RENEGADE!");
            }

            if (target->state.hull_integrity <= 0 || target->state.energy <= 0) { 
                target->state.energy = 0; target->state.hull_integrity = 0; target->state.crew_count = 0; target->active = 0; 
                target->state.boom = (NetPoint){(float)target->state.s1,(float)target->state.s2,(float)target->state.s3,1}; 
            }
            send_server_msg(tid-1, "WARNING", "UNDER Ion Beam ATTACK!");
        } else if (tid >= 1000 && tid < 1000+MAX_NPC) {
            npcs[tid-1000].energy -= hit; float engine_dmg = (hit / 1000.0f) * 10.0f; npcs[tid-1000].engine_health -= engine_dmg; if (npcs[tid-1000].engine_health < 0) npcs[tid-1000].engine_health = 0;
            
            /* Renegade Status: Ion Beam friendly fire (NPC) */
            if (npcs[tid-1000].faction == players[i].faction) {
                players[i].renegade_timer = 18000;
                send_server_msg(i, "CRITICAL", "TRAITOROUS ATTACK! Friendly Ion Beam lock detected!");
            }

            if (npcs[tid-1000].energy <= 0) { npcs[tid-1000].active = 0; players[i].state.boom = (NetPoint){(float)npcs[tid-1000].x, (float)npcs[tid-1000].y, (float)npcs[tid-1000].z, 1}; }
        } else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS) {
            platforms[tid-16000].energy -= hit;
            
            /* Renegade Status: Platforms */
            if (platforms[tid-16000].faction == players[i].faction) {
                players[i].renegade_timer = 18000;
                send_server_msg(i, "CRITICAL", "ACT OF SABOTAGE! Alliance/Faction property attacked!");
            }

            if (platforms[tid-16000].energy <= 0) { platforms[tid-16000].active = 0; players[i].state.boom = (NetPoint){(float)platforms[tid-16000].x, (float)platforms[tid-16000].y, (float)platforms[tid-16000].z, 1}; }
        } else if (tid >= 18000 && tid < 18000+MAX_MONSTERS) {
            monsters[tid-18000].energy -= hit;
            if (monsters[tid-18000].energy <= 0) { monsters[tid-18000].active = 0; players[i].state.boom = (NetPoint){(float)monsters[tid-18000].x, (float)monsters[tid-18000].y, (float)monsters[tid-18000].z, 1}; }
        }
        char msg[64]; sprintf(msg, "Ion Beams locked. Target hit for %d damage.", hit); send_server_msg(i, "TACTICAL", msg);
    } else send_server_msg(i, "COMPUTER", "Target out of Ion Beam range or not in current quadrant.");
}

void handle_tor(int i, const char *params) {
    if (players[i].state.system_health[5] <= 50.0f) { send_server_msg(i, "TACTICAL", "Torpedo tubes OFFLINE."); return; }
    if (players[i].torp_active) { send_server_msg(i, "TACTICAL", "Tubes currently FIRING. Wait for impact."); return; }
    if (players[i].torp_load_timer > 0) { send_server_msg(i, "TACTICAL", "Tubes are LOADING..."); return; }
    if (players[i].state.is_cloaked) { send_server_msg(i, "TACTICAL", "Cannot fire torpedoes while cloaked."); return; }

    if(players[i].state.torpedoes > 0) {
        players[i].state.torpedoes--; players[i].torp_active=true;
        players[i].torp_load_timer = 150; /* 5 seconds at 30Hz logic tick */
        players[i].torp_timeout = 300;    /* 10 seconds timeout */
        players[i].torp_target = players[i].state.lock_target;
        double h=players[i].state.van_h, m=players[i].state.van_m;
        bool manual = true; if (players[i].torp_target > 0) manual = false;
        double rad_h = h * M_PI / 180.0; double rad_m = m * M_PI / 180.0;
        players[i].tx = players[i].state.s1; players[i].ty = players[i].state.s2; players[i].tz = players[i].state.s3;
        players[i].tdx = cos(rad_m) * sin(rad_h); players[i].tdy = cos(rad_m) * -cos(rad_h); players[i].tdz = sin(rad_m);
        send_server_msg(i, "TACTICAL", manual ? "Torpedo away (Manual)." : "Torpedo away (Locked).");
    } else send_server_msg(i, "TACTICAL", "Insufficient torpedoes.");
}

void handle_she(int i, const char *params) {
    int f,r,t,b,l,ri; if(sscanf(params, "%d %d %d %d %d %d", &f,&r,&t,&b,&l,&ri) == 6) {
        players[i].state.shields[0]=f; players[i].state.shields[1]=r; players[i].state.shields[2]=t;
        players[i].state.shields[3]=b; players[i].state.shields[4]=l; players[i].state.shields[5]=ri;
        send_server_msg(i, "ENGINEERING", "Shields configured.");
    } else send_server_msg(i, "COMPUTER", "Usage: she <F> <R> <T> <B> <L> <RI>");
}

void handle_lock(int i, const char *params) {
    int tid; if(sscanf(params, " %d", &tid) == 1) {
        players[i].state.lock_target = tid; send_server_msg(i, "TACTICAL", "Target locked.");
    } else { players[i].state.lock_target = 0; send_server_msg(i, "TACTICAL", "Lock released."); }
}

void handle_scan(int i, const char *params) {
    int tid; if(sscanf(params, " %d", &tid) == 1) {
        char *rep = calloc(1024, sizeof(char));
        if (!rep) return;
        bool found = false;
        int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
        if (tid >= 1 && tid <= 32) {
             ConnectedPlayer *t = &players[tid-1];
             if (t->active && t->state.q1 == pq1 && t->state.q2 == pq2 && t->state.q3 == pq3) {
                 found = true;
                 snprintf(rep, 1024, CYAN "\n--- SENSOR SCAN ANALYSIS: TARGET ID %d ---" RESET, tid);
                 snprintf(rep+strlen(rep), 1024-strlen(rep), "COMMANDER: %s\n", t->name);
                 snprintf(rep+strlen(rep), 1024-strlen(rep), "ENERGY: %d | CREW: %d | TORPS: %d\n", t->state.energy, t->state.crew_count, t->state.torpedoes);
                 strcat(rep, BLUE "SYSTEMS STATUS:\n" RESET);
                 const char* sys[] = {"Hyperdrive", "Impulse", "Sensors", "Transp", "Ion Beams", "Torps", "Computer", "Life", "Shields", "Aux"};
                 for(int s=0; s<10; s++) {
                     char bar[11]; int fills = (int)(t->state.system_health[s]/10.0f); for(int k=0; k<10; k++) bar[k]=(k<fills)?'|':'.'; bar[10]=0;
                     char line[64]; snprintf(line, sizeof(line), " %-8s [%s] %.1f%%\n", sys[s], bar, t->state.system_health[s]);
                     if (strlen(rep) + strlen(line) < 1023) strcat(rep, line);
                 }
             }
        } 
        if (!found && tid >= 1000 && tid < 1000+MAX_NPC) {
            int idx = tid - 1000;
            if (npcs[idx].active && npcs[idx].q1==pq1 && npcs[idx].q2==pq2 && npcs[idx].q3==pq3) {
                found = true;
                NPCShip *n = &npcs[idx];
                snprintf(rep, 1024, CYAN "\n--- TACTICAL SCAN ANALYSIS: TARGET ID %d ---" RESET, tid);
                snprintf(rep+strlen(rep), 1024-strlen(rep), "SPECIES: %s\n", get_species_name(n->faction));
                snprintf(rep+strlen(rep), 1024-strlen(rep), "ENERGY CORE: %d\n", n->energy);
                snprintf(rep+strlen(rep), 1024-strlen(rep), "PROPULSION: %.1f%%\n", n->engine_health);
                snprintf(rep+strlen(rep), 1024-strlen(rep), "BEHAVIOR: %s\n", (n->ai_state==AI_STATE_FLEE)?"RETREATING":(n->ai_state==AI_STATE_CHASE)?"AGGRESSIVE":"PATROLLING");
            }
        } 
        if (!found && tid >= 2000 && tid < 2000+MAX_BASES) {
            int idx = tid - 2000;
            if (bases[idx].active && bases[idx].q1==pq1 && bases[idx].q2==pq2 && bases[idx].q3==pq3) {
                found = true;
                snprintf(rep, 1024, WHITE "\n--- Alliance STARBASE ANALYSIS ---" RESET "\nTYPE: Supply and Repair Outpost\nSTATUS: Active\nSERVICES: Full Repair, Torpedo Reload, Energy Recharge.\n");
            }
        } 
        if (!found && tid >= 3000 && tid < 3000+MAX_PLANETS) {
            int idx = tid - 3000;
            if (planets[idx].active && planets[idx].q1==pq1 && planets[idx].q2==pq2 && planets[idx].q3==pq3) {
                found = true;
                const char* res[] = {"-","Isotope-X","Tritanium","Core-Matter","Monotanium","Isolinear","Gases","Composite"};
                snprintf(rep, 1024, GREEN "\n--- PLANETARY SURVEY ---" RESET "\nTYPE: Class-M Habitable\nRESOURCE: %s\nRESERVES: %d units\n", res[planets[idx].resource_type], planets[idx].amount);
            }
        } 
        if (!found && tid >= 4000 && tid < 4000+MAX_STARS) {
            int idx = tid - 4000;
            if (stars_data[idx].active && stars_data[idx].q1==pq1 && stars_data[idx].q2==pq2 && stars_data[idx].q3==pq3) {
                found = true;
                snprintf(rep, 1024, YELLOW "\n--- STELLAR ANALYSIS ---" RESET "\nTYPE: Main Sequence G-Class Star\nLUMINOSITY: Standard\nADVISORY: Proximity scooping active (sco).\n");
            }
        } 
        if (!found && tid >= 7000 && tid < 7000+MAX_BH) {
            int idx = tid - 7000;
            if (black_holes[idx].active && black_holes[idx].q1==pq1 && black_holes[idx].q2==pq2 && black_holes[idx].q3==pq3) {
                found = true;
                snprintf(rep, 1024, MAGENTA "\n--- SINGULARITY ANALYSIS ---" RESET "\nTYPE: Schwarzschild Black Hole\nEFFECT: Extreme Time-Dilation and Space Curvature.\nADVISORY: Significant gravitational pull detected within 3.0 units. Escape velocity required.\n");
            }
        } 
        if (!found && tid >= 8000 && tid < 8000+MAX_NEBULAS) {
            int idx = tid - 8000;
            if (nebulas[idx].active && nebulas[idx].q1==pq1 && nebulas[idx].q2==pq2 && nebulas[idx].q3==pq3) {
                found = true;
                if (nebulas[idx].type == 1) { /* Metreon */
                    snprintf(rep, 1024, BLUE "\n--- STELLAR PHENOMENON ANALYSIS ---" RESET "\nTYPE: Metreon-Class Nebula\nCOMPOSITION: Highly volatile Metreon gas.\nEFFECT: High risk of ignition. Sensor interference.\n");
                } else if (nebulas[idx].type == 2) { /* Dark Matter Cloud */
                    snprintf(rep, 1024, BLUE "\n--- STELLAR PHENOMENON ANALYSIS ---" RESET "\nTYPE: Dark Matter Cloud-Class Nebula\nCOMPOSITION: Non-baryonic matter clusters.\nEFFECT: Gravitational anomalies. Severe sensor blindness.\n");
                } else if (nebulas[idx].type == 3) { /* Paulson */
                    snprintf(rep, 1024, BLUE "\n--- STELLAR PHENOMENON ANALYSIS ---" RESET "\nTYPE: Paulson-Class Nebula\nCOMPOSITION: High density baryonic particulates.\nEFFECT: Natural cloaking cover. Severe communication dampening.\n");
                } else if (nebulas[idx].type == 4) { /* McAllister */
                    snprintf(rep, 1024, BLUE "\n--- STELLAR PHENOMENON ANALYSIS ---" RESET "\nTYPE: McAllister-Class Nebula\nCOMPOSITION: Verteron-enriched gas.\nEFFECT: Deep Space sensor distortion. Tactical advantage.\n");
                } else if (nebulas[idx].type == 5) { /* Arachnia */
                    snprintf(rep, 1024, BLUE "\n--- STELLAR PHENOMENON ANALYSIS ---" RESET "\nTYPE: Arachnia-Class Nebula\nCOMPOSITION: Photonic-active plasma.\nEFFECT: Unpredictable holographic ghosting on sensors.\n");
                } else { /* Mutara (Default) */
                    snprintf(rep, 1024, BLUE "\n--- STELLAR PHENOMENON ANALYSIS ---" RESET "\nTYPE: Mutara-Class Nebula\nCOMPOSITION: Ionized Gases, Sensor-dampening particulates.\nEFFECT: Reduced sensor range, Shield regeneration inhibition.\n");
                }
            }
        } 
        if (!found && tid >= 9000 && tid < 9000+MAX_PULSARS) {
            int idx = tid - 9000;
            if (pulsars[idx].active && pulsars[idx].q1==pq1 && pulsars[idx].q2==pq2 && pulsars[idx].q3==pq3) {
                found = true;
                snprintf(rep, 1024, RED "\n--- WARNING: PULSAR DETECTED ---" RESET "\nTYPE: Rotating Neutron Star\nRADIATION: Extreme (Gamma/X-Ray)\nADVISORY: Maintain minimum safe distance 2.0. Shield failure imminent in proximity.\n");
            }
        } 
        if (!found && tid >= 10000 && tid < 10000+MAX_COMETS) {
            int idx = tid - 10000;
            if (comets[idx].active && comets[idx].q1==pq1 && comets[idx].q2==pq2 && comets[idx].q3==pq3) {
                found = true;
                snprintf(rep, 1024, CYAN "\n--- COMET TRACKING DATA ---" RESET "\nTYPE: Icy Nucleus / Ion Tail\nSPEED: Orbital Intercept possible.\nCOMPOSITION: Rare gases, frozen Core-Matter.\n");
            }
        } 
        if (!found && tid >= 11000 && tid < 11000+MAX_DERELICTS) {
            int idx = tid - 11000;
            if (derelicts[idx].active && derelicts[idx].q1==pq1 && derelicts[idx].q2==pq2 && derelicts[idx].q3==pq3) {
                found = true;
                snprintf(rep, 1024, WHITE "\n--- DERELICT SENSOR LOG ---" RESET "\nTYPE: Abandoned Vessel\nINTEGRITY: Critical (Adrift)\nADVISORY: Boarding (bor) may recover valuable resources or tech.\n");
            }
        } 
        if (!found && tid >= 12000 && tid < 12000+MAX_ASTEROIDS) {
            int idx = tid - 12000;
            if (asteroids[idx].active && asteroids[idx].q1==pq1 && asteroids[idx].q2==pq2 && asteroids[idx].q3==pq3) {
                found = true;
                snprintf(rep, 1024, WHITE "\n--- ASTEROID ANALYSIS ---" RESET "\nTYPE: Carbonaceous / Metallic\nEFFECT: Navigation hazard at high impulse/warp.\n");
            }
        } 
        if (!found && tid >= 16000 && tid < 16000+MAX_PLATFORMS) {
            int idx = tid - 16000;
            if (platforms[idx].active && platforms[idx].q1==pq1 && platforms[idx].q2==pq2 && platforms[idx].q3==pq3) {
                found = true;
                snprintf(rep, 1024, RED "\n--- DEFENSE PLATFORM TACTICAL ---" RESET "\nTYPE: Automated Weapon Sentry\nSTATUS: ACTIVE / HOSTILE\nENERGY CORE: %d\nCOOLDOWN: %d ticks\n", platforms[idx].energy, platforms[idx].fire_cooldown);
            }
        } 
        if (!found && tid >= 18000 && tid < 18000+MAX_MONSTERS) {
            int idx = tid - 18000;
            if (monsters[idx].active && monsters[idx].q1==pq1 && monsters[idx].q2==pq2 && monsters[idx].q3==pq3) {
                found = true;
                snprintf(rep, 1024, MAGENTA "\n--- XENO-BIOLOGICAL ANALYSIS ---" RESET "\nTYPE: %s\nTHREAT LEVEL: EXTREME\nADVISORY: Conventional weapons effective but risky in close proximity.\n", (monsters[idx].type==30)?"Crystalline Entity":"Space Amoeba");
            }
        }

        if (found) send_server_msg(i, "SCIENCE", rep);
        else send_server_msg(i, "COMPUTER", "Unable to lock sensors on specified ID.");
        free(rep);
    } else {
        send_server_msg(i, "COMPUTER", "Usage: scan <ID>");
    }
}

void handle_clo(int i, const char *params) {
    players[i].state.is_cloaked = !players[i].state.is_cloaked;
    send_server_msg(i, "HELMSMAN", players[i].state.is_cloaked ? "Cloaking device engaged. Sensors limited." : "Cloaking device disengaged.");
}

void handle_bor(int i, const char *params) {
    int tid = 0;
    if(sscanf(params, " %d", &tid) != 1) {
        tid = players[i].state.lock_target;
    }

    if (tid > 0) {
        if (players[i].state.energy < 5000) { send_server_msg(i, "COMPUTER", "Insufficient energy for boarding operation."); return; }
        players[i].state.energy -= 5000;
        double tx, ty, tz; bool found = false;
        int pq1=players[i].state.q1, pq2=players[i].state.q2, pq3=players[i].state.q3;
        if (tid >= 1 && tid <= 32 && players[tid-1].active && players[tid-1].state.q1 == pq1 && players[tid-1].state.q2 == pq2 && players[tid-1].state.q3 == pq3) { tx=players[tid-1].state.s1; ty=players[tid-1].state.s2; tz=players[tid-1].state.s3; found=true; }
        else if (tid >= 1000 && tid < 1000+MAX_NPC && npcs[tid-1000].active && npcs[tid-1000].q1 == pq1 && npcs[tid-1000].q2 == pq2 && npcs[tid-1000].q3 == pq3) { tx=npcs[tid-1000].x; ty=npcs[tid-1000].y; tz=npcs[tid-1000].z; found=true; }
        else if (tid >= 11000 && tid < 11000+MAX_DERELICTS && derelicts[tid-11000].active && derelicts[tid-11000].q1 == pq1 && derelicts[tid-11000].q2 == pq2 && derelicts[tid-11000].q3 == pq3) { tx=derelicts[tid-11000].x; ty=derelicts[tid-11000].y; tz=derelicts[tid-11000].z; found=true; }
        else if (tid >= 16000 && tid < 16000+MAX_PLATFORMS && platforms[tid-16000].active && platforms[tid-16000].q1 == pq1 && platforms[tid-16000].q2 == pq2 && platforms[tid-16000].q3 == pq3) { tx=platforms[tid-16000].x; ty=platforms[tid-16000].y; tz=platforms[tid-16000].z; found=true; }
        
        if (found) {
            double dx=tx-players[i].state.s1, dy=ty-players[i].state.s2, dz=tz-players[i].state.s3; double dist=sqrt(dx*dx+dy*dy+dz*dz);
            if (dist < 1.0) {
                /* New logic: If target is a PLAYER, show interactive menu */
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

                if (tid >= 16000) {
                    /* Platform Boarding: Interactive Menu */
                    players[i].pending_bor_target = tid;
                    players[i].pending_bor_type = 3; /* PLATFORM */
                    char menu[512];
                    sprintf(menu, YELLOW "\n--- BOARDING MENU: DEFENSE PLATFORM [%d] ---\n" RESET 
                           "1: Reprogram IFF (Capture platform for your faction)\n"
                           "2: Overload Reactor (Trigger self-destruct)\n"
                           "3: Salvage Tech (Retrieve 250 Isolinear Chips)\n"
                           WHITE "Type the number to confirm choice." RESET, tid);
                    send_server_msg(i, "BOARDING", menu);
                    return;
                }

                if (rand()%100 < 45) { /* 45% success (slightly increased) */
                    int reward = rand()%4; /* Added 4th reward type */
                    if (reward == 0) { players[i].state.inventory[1] += 5; send_server_msg(i, "BOARDING", "Success! Captured Isotope-X crystals."); }
                    else if (reward == 1) { players[i].state.inventory[5] += 100; send_server_msg(i, "ENGINEERING", "Salvaged advanced Isolinear Chips from the ship's computer."); }
                    else if (reward == 2) { 
                        /* Recover Crew or Prisoners */
                        int found_people = 5 + rand()%25;
                        if (tid >= 11000) { /* Derelict */
                            players[i].state.crew_count += found_people;
                            char m[128]; sprintf(m, "Success! Recovered %d survivors from the wreck.", found_people);
                            send_server_msg(i, "BOARDING", m);
                        } else { /* Enemy NPC */
                            players[i].state.prison_unit += found_people;
                            char m[128]; sprintf(m, "Success! Captured %d enemy prisoners. Return them to Starbase for debrief.", found_people);
                            send_server_msg(i, "SECURITY", m);
                        }
                    }
                    else { for(int s=0; s<10; s++) players[i].state.system_health[s] = 100.0f; send_server_msg(i, "REPAIR", "Found automated repair drones. All systems restored!"); }
                } else if (rand()%100 < 80) {
                    int loss = 5 + rand()%15; players[i].state.crew_count -= loss;
                    send_server_msg(i, "SECURITY", "Boarding party repelled! Heavy casualties reported.");
                } else {
                    send_server_msg(i, "SECURITY", "Operation failed. Enemy systems too heavily defended.");
                }
            } else send_server_msg(i, "COMPUTER", "Target not in transporter range.");
        } else send_server_msg(i, "COMPUTER", "Invalid boarding target.");
    } else send_server_msg(i, "COMPUTER", "Usage: bor <ID>");
}

void handle_dis(int i, const char *params) {
    int tid = 0; 
    if(sscanf(params, " %d", &tid) != 1) {
        tid = players[i].state.lock_target;
    }

    if (tid > 0) {
        int pq1=players[i].state.q1, pq2=players[i].state.q2, pq3=players[i].state.q3;
        bool done = false;

        /* 1. Case: Enemy NPC Wreck (ID 1000-1999) */
        if (tid >= 1000 && tid < 1000+MAX_NPC && npcs[tid-1000].active && npcs[tid-1000].q1 == pq1 && npcs[tid-1000].q2 == pq2 && npcs[tid-1000].q3 == pq3) {
            double dx=npcs[tid-1000].x-players[i].state.s1, dy=npcs[tid-1000].y-players[i].state.s2, dz=npcs[tid-1000].z-players[i].state.s3;
            if (sqrt(dx*dx+dy*dy+dz*dz) < 1.5) {
                int yield = (npcs[tid-1000].energy / 100); 
                if (yield < 10) yield = 10;
                players[i].state.inventory[2] += yield; 
                players[i].state.inventory[5] += yield / 5; 
                npcs[tid-1000].active = 0;
                players[i].state.dismantle = (NetDismantle){(float)npcs[tid-1000].x, (float)npcs[tid-1000].y, (float)npcs[tid-1000].z, npcs[tid-1000].faction, 1};
                send_server_msg(i, "ENGINEERING", "Vessel dismantled. Resources transferred to cargo bay.");
                if (players[i].state.lock_target == tid) players[i].state.lock_target = 0;
                done = true;
            } else { send_server_msg(i, "COMPUTER", "Not in range for dismantling."); return; }
        } 
        /* 2. Case: Static Derelict Wreck (ID 11000+) */
        else if (tid >= 11000 && tid < 11000+MAX_DERELICTS && derelicts[tid-11000].active && derelicts[tid-11000].q1 == pq1 && derelicts[tid-11000].q2 == pq2 && derelicts[tid-11000].q3 == pq3) {
            int d_idx = tid-11000;
            double dx=derelicts[d_idx].x-players[i].state.s1, dy=derelicts[d_idx].y-players[i].state.s2, dz=derelicts[d_idx].z-players[i].state.s3;
            if (sqrt(dx*dx+dy*dy+dz*dz) < 1.5) {
                int yield = 50 + rand()%150; /* Fixed yield for ancient wrecks */
                players[i].state.inventory[2] += yield; 
                players[i].state.inventory[5] += yield / 4; 
                derelicts[d_idx].active = 0;
                players[i].state.dismantle = (NetDismantle){(float)derelicts[d_idx].x, (float)derelicts[d_idx].y, (float)derelicts[d_idx].z, 0, 1};
                send_server_msg(i, "ENGINEERING", "Ancient wreck dismantled. Raw materials salvaged.");
                if (players[i].state.lock_target == tid) players[i].state.lock_target = 0;
                done = true;
            } else { send_server_msg(i, "COMPUTER", "Not in range for dismantling."); return; }
        }

        if (!done) send_server_msg(i, "COMPUTER", "Invalid dismantle target (Must be a wreck or derelict).");
    } else {
        send_server_msg(i, "COMPUTER", "Usage: dis <ID> or lock a target first.");
    }
}

void handle_min(int i, const char *params) {
    int locked_id = players[i].state.lock_target;
    int target_idx = -1;
    int target_type = -1; /* 0: Asteroid, 1: Planet */
    double min_dist = DIST_MINING_MAX + 0.05;

    /* 1. If target is locked, check if it's mineable and in range */
    if (locked_id >= 3000 && locked_id < 3000 + MAX_PLANETS) {
        int p = locked_id - 3000;
        if (planets[p].active && planets[p].q1 == players[i].state.q1 && planets[p].q2 == players[i].state.q2 && planets[p].q3 == players[i].state.q3) {
            double d = sqrt(pow(planets[p].x - players[i].state.s1, 2) + pow(planets[p].y - players[i].state.s2, 2) + pow(planets[p].z - players[i].state.s3, 2));
            if (d <= min_dist) { target_idx = p; target_type = 1; min_dist = d; }
        }
    } else if (locked_id >= 12000 && locked_id < 12000 + MAX_ASTEROIDS) {
        int a = locked_id - 12000;
        if (asteroids[a].active && asteroids[a].q1 == players[i].state.q1 && asteroids[a].q2 == players[i].state.q2 && asteroids[a].q3 == players[i].state.q3) {
            double d = sqrt(pow(asteroids[a].x - players[i].state.s1, 2) + pow(asteroids[a].y - players[i].state.s2, 2) + pow(asteroids[a].z - players[i].state.s3, 2));
            if (d <= min_dist) { target_idx = a; target_type = 0; min_dist = d; }
        }
    }

    /* 2. If no valid locked target, find the closest asteroid or planet */
    if (target_idx == -1) {
        /* Check Asteroids first */
        for (int a = 0; a < MAX_ASTEROIDS; a++) {
            if (asteroids[a].active && asteroids[a].q1 == players[i].state.q1 && asteroids[a].q2 == players[i].state.q2 && asteroids[a].q3 == players[i].state.q3) {
                double d = sqrt(pow(asteroids[a].x - players[i].state.s1, 2) + pow(asteroids[a].y - players[i].state.s2, 2) + pow(asteroids[a].z - players[i].state.s3, 2));
                if (d < min_dist) { min_dist = d; target_idx = a; target_type = 0; }
            }
        }
        /* Check Planets */
        for (int p = 0; p < MAX_PLANETS; p++) {
            if (planets[p].active && planets[p].q1 == players[i].state.q1 && planets[p].q2 == players[i].state.q2 && planets[p].q3 == players[i].state.q3) {
                double d = sqrt(pow(planets[p].x - players[i].state.s1, 2) + pow(planets[p].y - players[i].state.s2, 2) + pow(planets[p].z - players[i].state.s3, 2));
                if (d < min_dist) { min_dist = d; target_idx = p; target_type = 1; }
            }
        }
    }

    /* 3. Execute Mining */
    if (target_idx != -1) {
        if (target_type == 0) { /* Asteroid */
            int ex = (asteroids[target_idx].amount > 50) ? 50 : asteroids[target_idx].amount;
            int r_type = asteroids[target_idx].resource_type;
            if (r_type < 0) r_type = 0; 
            if (r_type > 9) r_type = 9;
            asteroids[target_idx].amount -= ex;
            players[i].state.inventory[r_type] += ex;
            if (asteroids[target_idx].amount <= 0) asteroids[target_idx].active = 0;
            send_server_msg(i, "MINING", "[RADIO] MINING (Alliance Command): Asteroid extraction complete. Minerals transferred to cargo.");
        } else { /* Planet */
            int ex = (planets[target_idx].amount > 100) ? 100 : planets[target_idx].amount;
            int r_type = planets[target_idx].resource_type;
            if (r_type < 0) r_type = 0; 
            if (r_type > 9) r_type = 9;
            planets[target_idx].amount -= ex;
            players[i].state.inventory[r_type] += ex;
            send_server_msg(i, "GEOLOGY", "[RADIO] GEOLOGY (Alliance Command): Planetary mining successful.");
        }
    } else {
        send_server_msg(i, "COMPUTER", "No planet or asteroid in range for mining.");
    }
}

void handle_sco(int i, const char *params) {
    bool near=false; for(int s=0; s<MAX_STARS; s++) if(stars_data[s].active && stars_data[s].q1==players[i].state.q1 && stars_data[s].q2==players[i].state.q2 && stars_data[s].q3==players[i].state.q3) {
        double d=sqrt(pow(stars_data[s].x-players[i].state.s1,2)+pow(stars_data[s].y-players[i].state.s2,2)+pow(stars_data[s].z-players[i].state.s3,2)); if(d<DIST_SCOOPING_MAX) { near=true; break; }
    }
    if(near) { players[i].state.cargo_energy += 5000; if(players[i].state.cargo_energy > 1000000) players[i].state.cargo_energy = 1000000; int s_idx = rand()%6; players[i].state.shields[s_idx] -= 500; if(players[i].state.shields[s_idx]<0) players[i].state.shields[s_idx]=0; send_server_msg(i, "ENGINEERING", "Solar energy stored."); } 
    else send_server_msg(i, "COMPUTER", "No star in range.");
}

void handle_har(int i, const char *params) {
    bool near=false; for(int h=0; h<MAX_BH; h++) if(black_holes[h].active && black_holes[h].q1==players[i].state.q1 && black_holes[h].q2==players[i].state.q2 && black_holes[h].q3==players[i].state.q3) {
        double d=sqrt(pow(black_holes[h].x-players[i].state.s1,2)+pow(black_holes[h].y-players[i].state.s2,2)+pow(black_holes[h].z-players[i].state.s3,2)); 
        if(d <= (DIST_INTERACTION_MAX + 0.05f)) { near=true; break; }
    }
    if(near) { 
        players[i].state.cargo_energy += 10000; 
        if(players[i].state.cargo_energy > 1000000) players[i].state.cargo_energy = 1000000; 
        players[i].state.inventory[1] += 100; 
        int s_idx = rand()%6; players[i].state.shields[s_idx] -= 1000; 
        if(players[i].state.shields[s_idx]<0) players[i].state.shields[s_idx]=0; 
        send_server_msg(i, "ENGINEERING", "Antimatter harvested and stored. Isotope-X crystals stabilized (+100)."); 
    } 
    else send_server_msg(i, "COMPUTER", "No black hole in range for antimatter harvesting.");
}

void handle_doc(int i, const char *params) {
    bool near=false; for(int b=0; b<MAX_BASES; b++) if(bases[b].active && bases[b].q1==players[i].state.q1 && bases[b].q2==players[i].state.q2 && bases[b].q3==players[i].state.q3) {
        double d=sqrt(pow(bases[b].x-players[i].state.s1,2)+pow(bases[b].y-players[i].state.s2,2)+pow(bases[b].z-players[i].state.s3,2)); if(d <= (DIST_DOCKING_MAX + 0.05f)) { near=true; break; } 
    }
    if(near) { 
        players[i].state.energy=1000000; 
        players[i].state.torpedoes=1000; 
        players[i].state.cargo_energy=1000000;
        players[i].state.cargo_torpedoes=1000;
        for(int s=0; s<8; s++) players[i].state.system_health[s]=100.0f; 
        send_server_msg(i, "STARBASE", "Docking complete. Reactor and Cargo Bay replenished."); 
    } 
    else send_server_msg(i,"COMPUTER","No starbase in range.");
}

void handle_con(int i, const char *params) {
    int t,a; 
    if(sscanf(params," %d %d",&t,&a)==2 && t>=1 && t<=8 && players[i].state.inventory[t]>=a) {
        players[i].state.inventory[t]-=a; 
        if(t==1) { players[i].state.cargo_energy+=a*10; } 
        else if(t==2) { players[i].state.cargo_energy+=a*2; } 
        else if(t==3) { players[i].state.cargo_torpedoes+=a/20; } 
        else if(t==6) { players[i].state.cargo_energy+=a*5; }
        else if(t==7) { players[i].state.cargo_energy+=a*4; }
        else if(t==8) { players[i].state.cargo_energy+=a*25; } /* Keronium is high energy */
        
        if(players[i].state.cargo_energy>1000000) players[i].state.cargo_energy=1000000; 
        if(players[i].state.cargo_torpedoes>1000) players[i].state.cargo_torpedoes=1000;
        
        send_server_msg(i,"ENGINEERING","Assets stored in Cargo Bay.");
    }
}

void handle_load(int i, const char *params) {
    int type, amount; if (sscanf(params, "%d %d", &type, &amount) == 2) {
        if (type == 1) { 
            if(amount>players[i].state.cargo_energy) amount=players[i].state.cargo_energy; 
            players[i].state.cargo_energy-=amount; 
            players[i].state.energy+=amount; 
            if(players[i].state.energy>9999999) players[i].state.energy=9999999; 
            send_server_msg(i,"ENGINEERING","Energy loaded."); 
        }
        else if (type == 2) { if(amount>players[i].state.cargo_torpedoes) amount=players[i].state.cargo_torpedoes; players[i].state.cargo_torpedoes-=amount; players[i].state.torpedoes+=amount; if(players[i].state.torpedoes>1000) players[i].state.torpedoes=1000; send_server_msg(i,"TACTICAL","Torps loaded."); }
    }
}

void handle_psy(int i, const char *params) {
    if (players[i].state.anti_matter_count > 0) {
        send_server_msg(i, "COMMANDER", "Broadcasting Anti-Matter threat on all frequencies...");
        int pq1 = players[i].state.q1, pq2 = players[i].state.q2, pq3 = players[i].state.q3;
        QuadrantIndex *local_q = &spatial_index[pq1][pq2][pq3];
        
        if (rand() % 100 < 60) { /* 60% success rate */
            for (int n = 0; n < local_q->npc_count; n++) {
                local_q->npcs[n]->ai_state = AI_STATE_FLEE;
                local_q->npcs[n]->energy += 5000; /* Give them a 'panic' boost to run away */
            }
            send_server_msg(i, "SCIENCE", "Bluff successful. Hostile vessels are breaking formation!");
        } else {
            send_server_msg(i, "TACTICAL", "The enemy is ignoring our broadcast. Bluff failed.");
        }
        players[i].state.anti_matter_count--;
    } else {
        send_server_msg(i, "COMPUTER", "No Anti-Matter devices available in inventory.");
    }
}

void handle_supernova(int i, const char *params) {
    if (supernova_event.supernova_timer > 0) {
        send_server_msg(i, "COMPUTER", "A supernova event is already in progress.");
        return;
    }
    int q1 = players[i].state.q1, q2 = players[i].state.q2, q3 = players[i].state.q3;
    supernova_event.supernova_q1 = q1;
    supernova_event.supernova_q2 = q2;
    supernova_event.supernova_q3 = q3;
    supernova_event.supernova_timer = 1800; /* 60 seconds */
    
    /* Find a star to explode */
    supernova_event.x = 5.0; supernova_event.y = 5.0; supernova_event.z = 5.0;
    supernova_event.star_id = -1;
    QuadrantIndex *qi = &spatial_index[q1][q2][q3];
    if (qi->star_count > 0) {
        supernova_event.x = qi->stars[0]->x;
        supernova_event.y = qi->stars[0]->y;
        supernova_event.z = qi->stars[0]->z;
        supernova_event.star_id = qi->stars[0]->id;
    }

    send_server_msg(i, "ADMIN", "SUPERNOVA INITIATED IN CURRENT QUADRANT.");
}

void handle_rep(int i, const char *params) {
    int sid; 
    if(sscanf(params," %d",&sid) == 1) {
        if (sid >= 0 && sid < 10) {
            bool can=false;
            if(players[i].state.inventory[2]>=50 && players[i].state.inventory[5]>=10) {
                players[i].state.inventory[2]-=50; players[i].state.inventory[5]-=10;
                can=true;
            }
            if(can){ players[i].state.system_health[sid]=100.0f; send_server_msg(i,"ENGINEERING","Repairs complete."); }
            else send_server_msg(i,"ENGINEERING","Insufficient materials.");
        } else {
            send_server_msg(i, "COMPUTER", "Invalid system ID. Use 'rep' to list systems.");
        }
    } else {
        /* List all systems */
        char list[1024] = CYAN "\n--- ENGINEERING: SHIP SYSTEMS DIRECTORY ---" RESET "\n";
        const char* sys_names[] = {"Hyperdrive", "Impulse", "Sensors", "Transp", "Ion Beams", "Torps", "Computer", "Life", "Shields", "Aux"};
        for(int s=0; s<10; s++) {
            char line[128];
            snprintf(line, sizeof(line), WHITE "%d" RESET ": %-10s | STATUS: %.1f%%\n", s, sys_names[s], players[i].state.system_health[s]);
            strcat(list, line);
        }
        strcat(list, YELLOW "\nUsage: rep <ID> (Requires 50 Tritanium + 10 Isolinear Chips)\n" RESET);
        send_server_msg(i, "COMPUTER", list);
    }
}

void handle_sta(int i, const char *params) {
    char *b = calloc(4096, sizeof(char));
    if (!b) return;
    
    const char* f_name = get_species_name(players[i].faction);
    const char* c_names[] = {"Legacy Class", "Scout Class", "Heavy Cruiser", "Multi-Engine Cruiser", "Escort Class", "Explorer Class", "Flagship Class", "Science Vessel", "Carrier Class", "Tactical Cruiser", "Diplomatic Cruiser", "Research Vessel", "Frigate Class", "Vessel"};
    const char* class_name = (players[i].ship_class >= 0 && players[i].ship_class <= 13) ? c_names[players[i].ship_class] : "Unknown";
    snprintf(b, 4096, CYAN "\n.--- GDIS MAIN COMPUTER: SHIP DIAGNOSTICS -----------------------.\n" RESET WHITE " COMMANDER: %-18s CLASS: %-15s\n FACTION:   %-18s STATUS: %s\n CREW COMPLEMENT: %d\n" RESET, players[i].name, class_name, f_name, players[i].state.is_cloaked ? MAGENTA "[ CLOAKED ]" RESET : GREEN "[ ACTIVE ]" RESET, players[i].state.crew_count);
    strcat(b, BLUE "\n[ POSITION AND TELEMETRY ]\n" RESET);
    snprintf(b+strlen(b), 4096-strlen(b), " QUADRANT: [%d,%d,%d]  SECTOR: [%.2f, %.2f, %.2f]\n", players[i].state.q1, players[i].state.q2, players[i].state.q3, players[i].state.s1, players[i].state.s2, players[i].state.s3);
    snprintf(b+strlen(b), 4096-strlen(b), " HEADING:  %03.0f\302\260        MARK:   %+03.0f\302\260\n", players[i].state.van_h, players[i].state.van_m);
    snprintf(b+strlen(b), 4096-strlen(b), " NAV MODE: %s\n", (players[i].nav_state == NAV_STATE_CHASE) ? B_RED "[ CHASE ACTIVE ]" RESET : "[ NORMAL ]");
    strcat(b, BLUE "\n[ POWER AND REACTOR STATUS ]\n" RESET);
    float en_pct = (players[i].state.energy / 1000000.0f) * 100.0f; char en_bar[21]; int en_fills = (int)(en_pct / 5); for(int j=0; j<20; j++) en_bar[j] = (j < en_fills) ? '|' : '-'; en_bar[20] = '\0';
    snprintf(b+strlen(b), 4096-strlen(b), " MAIN REACTOR: [%s] %d / 1000000 (%.1f%%)\n ALLOCATION:   ENGINES: %.0f%%  SHIELDS: %.0f%%  WEAPONS: %.0f%%\n", en_bar, players[i].state.energy, en_pct, players[i].state.power_dist[0]*100, players[i].state.power_dist[1]*100, players[i].state.power_dist[2]*100);
    strcat(b, YELLOW "[ CARGO BAY - LOGISTICS ]\n" RESET);
    snprintf(b+strlen(b), 4096-strlen(b), " PLASMA RESERVES:  %-7d  CARGO TORPEDOES: %-3d\n", players[i].state.cargo_energy, players[i].state.cargo_torpedoes);
    strcat(b, YELLOW "[ STORED MINERALS & RESOURCES ]\n" RESET);
    snprintf(b+strlen(b), 4096-strlen(b), " Aetherium:  %-5d  NEO-TITANIUM:  %-5d  Void-Essence:%-5d [WARHEADS]\n", players[i].state.inventory[1], players[i].state.inventory[2], players[i].state.inventory[3]);
    snprintf(b+strlen(b), 4096-strlen(b), " GRAPHENE:   %-5d  SYNAPTICS:  %-5d  NEBULAR GAS:   %-5d\n", players[i].state.inventory[4], players[i].state.inventory[5], players[i].state.inventory[6]);
    snprintf(b+strlen(b), 4096-strlen(b), " Composite:   %-5d  PRISON UNIT: %-5d  PLATING:   %-5d\n", players[i].state.inventory[7], players[i].state.prison_unit, players[i].state.composite_plating);
    strcat(b, BLUE "\n[ DEFENSIVE GRID AND ARMAMENTS ]\n" RESET);
    snprintf(b+strlen(b), 4096-strlen(b), " SHIELDS: F:%-4d R:%-4d T:%-4d B:%-4d L:%-4d RI:%-4d\n Plasma Torpedoes: %-2d  LOCK: %s\n", players[i].state.shields[0], players[i].state.shields[1], players[i].state.shields[2], players[i].state.shields[3], players[i].state.shields[4], players[i].state.shields[5], players[i].state.torpedoes, (players[i].state.lock_target > 0) ? RED "[ LOCKED ]" RESET : "[ NONE ]");
    strcat(b, BLUE "\n[ SYSTEMS INTEGRITY ]\n" RESET);
    const char* sys[] = {"Hyperdrive","Imp","Sens","Tran","Phas","Torp","Comp","Life"};
    for(int s=0; s<8; s++) { float hp = players[i].state.system_health[s]; const char* col = (hp > 75) ? GREEN : (hp > 25) ? YELLOW : RED; snprintf(b+strlen(b), 4096-strlen(b), " %-8s: %s%5.1f%%" RESET " ", sys[s], col, hp); if (s == 3) strcat(b, "\n"); }
    strcat(b, CYAN "\n'-----------------------------------------------------------------'\n" RESET);
    send_server_msg(i, "COMPUTER", b);
    free(b);
}

void handle_inv(int i, const char *params) {
    char b[512]=YELLOW "\n--- CARGO MANIFEST ---\n" RESET; char it[64]; const char* r[]={"-","Aetherium","Neo-Titanium","Void-Essence (Torp)","Graphene","Synaptics","Nebular Gas","Composite","Dark-Matter"};
    for(int j=1; j<=8; j++){ sprintf(it," %-16s: %-4d\n",r[j],players[i].state.inventory[j]); strcat(b,it); }
    snprintf(it, sizeof(it), BLUE " Plasma Reserves:  %d\n CARGO Torpedoes:  %d\n" RESET, players[i].state.cargo_energy, players[i].state.cargo_torpedoes); strcat(b, it);
    send_server_msg(i, "LOGISTICS", b);
}

void handle_dam(int i, const char *params) {
    char b[512]=RED "\n--- DAMAGE REPORT ---" RESET; char sbuf[64]; const char* sys[]={"Hyperdrive","Impulse","Sensors","Transp","Ion Beams","Torps","Computer","Life"};
    for(int s=0; s<8; s++){ sprintf(sbuf," %-10s: %.1f%%\n",sys[s],players[i].state.system_health[s]); strcat(b,sbuf); }
    send_server_msg(i, "ENGINEERING", b);
}

void handle_cal(int i, const char *params) {
    int qx, qy, qz; 
    float sx = 5.0f, sy = 5.0f, sz = 5.0f;
    int args = sscanf(params, "%d %d %d %f %f %f", &qx, &qy, &qz, &sx, &sy, &sz);
    
    if (args >= 3) {
        if (!IS_Q_VALID(qx, qy, qz)) {
            send_server_msg(i, "COMPUTER", "Invalid quadrant coordinates.");
            return;
        }

        /* High-Precision: Calculate distance from current absolute position to specific target */
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
                     " Hyperdrive 9.0      %6.1fs      Maximum Hyperdrive\n"
                     "-------------------------------------------------\n"
                     " Use 'nav %.1f %.1f %.2f [Factor]'", 
                     qx, qy, qz, sx, sy, sz, h, m, q_dist,
                     q_dist * (10.0 / pow(1.0, 0.8)),
                     q_dist * (10.0 / pow(3.0, 0.8)),
                     q_dist * (10.0 / pow(6.0, 0.8)),
                     q_dist * (10.0 / pow(8.0, 0.8)),
                     q_dist * (10.0 / pow(9.0, 0.8)),
                     h, m, q_dist
        );
        send_server_msg(i, "COMPUTER", buf);
    } else {
        send_server_msg(i, "COMPUTER", "Usage: cal <QX> <QY> <QZ> [SX SY SZ]");
    }
}

void handle_who(int i, const char *params) {
    char b[1024]=WHITE "\n--- ACTIVE CAPTAINS LOG ---\n" RESET;
    for(int j=0; j<MAX_CLIENTS; j++) if(players[j].active) {
        char line[128]; sprintf(line, " [%2d] %-18s (Q:%d,%d,%d)\n", j+1, players[j].name, players[j].state.q1, players[j].state.q2, players[j].state.q3);
        strcat(b, line);
    }
    send_server_msg(i, "COMPUTER", b);
}

void handle_aux(int i, const char *params) {
    const char *p_ptr = params;
    while(*p_ptr && (*p_ptr == ' ' || *p_ptr == '\t')) p_ptr++;
    
    if(strncmp(p_ptr, "jettison", 8) == 0) {
        send_server_msg(i, "ENGINEERING", "HYPERDRIVE CORE EJECTED! MASSIVE ENERGY DISCHARGE DETECTED!");
        players[i].state.energy = 0; players[i].active = 0;
        players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1};
    } else if(strncmp(p_ptr, "probe", 5) == 0) {
        int qx, qy, qz;
        const char *args = p_ptr + 5;
        if (sscanf(args, "%d %d %d", &qx, &qy, &qz) == 3) {
            if (!IS_Q_VALID(qx, qy, qz)) {
                send_server_msg(i, "COMPUTER", "Invalid quadrant coordinates.");
                return;
            }
            
            /* Find free probe slot */
            int p_idx = -1;
            for(int p=0; p<3; p++) {
                if(!players[i].state.probes[p].active) {
                    p_idx = p;
                    break;
                }
            }
            
            if(p_idx == -1) {
                send_server_msg(i, "COMPUTER", "All 3 probe slots are currently active.");
                return;
            }
            
            /* Launch probe */
            players[i].state.probes[p_idx].active = 1;
            players[i].state.probes[p_idx].q1 = qx;
            players[i].state.probes[p_idx].q2 = qy;
            players[i].state.probes[p_idx].q3 = qz;
            
            /* Absolute Galactic Start Position (Ship position) */
            players[i].state.probes[p_idx].gx = (float)players[i].gx;
            players[i].state.probes[p_idx].gy = (float)players[i].gy;
            players[i].state.probes[p_idx].gz = (float)players[i].gz;
            
            /* Target Galactic Position (Center of target quadrant) */
            float target_gx = (float)(qx - 1) * 10.0f + 5.0f;
            float target_gy = (float)(qy - 1) * 10.0f + 5.0f;
            float target_gz = (float)(qz - 1) * 10.0f + 5.0f;
            
            /* Calculate ETA and Velocity Vector */
            float dx = target_gx - players[i].state.probes[p_idx].gx;
            float dy = target_gy - players[i].state.probes[p_idx].gy;
            float dz = target_gz - players[i].state.probes[p_idx].gz;
            float dist_gal = sqrtf(dx*dx + dy*dy + dz*dz);
            
            /* Speed: 1 quadrant per 3 seconds (0.33 units/tick galactically) */
            float time_total = dist_gal / 3.33f; 
            if (time_total < 1.0f) time_total = 1.0f;
            
            players[i].state.probes[p_idx].vx = dx / (time_total * 30.0f);
            players[i].state.probes[p_idx].vy = dy / (time_total * 30.0f);
            players[i].state.probes[p_idx].vz = dz / (time_total * 30.0f);
            
            players[i].state.probes[p_idx].eta = time_total;
            players[i].state.probes[p_idx].status = 0; /* LAUNCHED */
            
            /* Initial local sector position */
            players[i].state.probes[p_idx].s1 = (float)players[i].state.s1;
            players[i].state.probes[p_idx].s2 = (float)players[i].state.s2;
            players[i].state.probes[p_idx].s3 = (float)players[i].state.s3;
            
            char msg[128];
            sprintf(msg, "Deep Space probe launched to [%d,%d,%d]. ETA: %.1f sec.", qx, qy, qz, players[i].state.probes[p_idx].eta);
            send_server_msg(i, "SCIENCE", msg);
        } else {
            send_server_msg(i, "COMPUTER", "Usage: aux probe <QX> <QY> <QZ>");
        }
    } else if(strncmp(p_ptr, "report", 6) == 0) {
        int p_idx;
        if (sscanf(p_ptr + 6, "%d", &p_idx) == 1) {
            p_idx--; /* 1-based to 0-based */
            if (p_idx < 0 || p_idx >= 3 || !players[i].state.probes[p_idx].active) {
                send_server_msg(i, "COMPUTER", "Specified probe is not active.");
                return;
            }
            
            if (players[i].state.probes[p_idx].status == 0) {
                send_server_msg(i, "SCIENCE", "Probe is still en route. No data available yet.");
                return;
            }

            int pq1 = players[i].state.probes[p_idx].q1;
            int pq2 = players[i].state.probes[p_idx].q2;
            int pq3 = players[i].state.probes[p_idx].q3;
            
            /* Real-time Query via Live Spatial Index */
            QuadrantIndex *lq = &spatial_index[pq1][pq2][pq3];
            
            char msg[1024];
            sprintf(msg, "\n" CYAN ".--- PROBE P%d: REAL-TIME DEEP SPACE TELEMETRY ---." RESET "\n"
                         " QUADRANT: " WHITE "[%d, %d, %d]" RESET " | SECTOR: " YELLOW "[%.1f, %.1f, %.1f]" RESET "\n"
                         "-------------------------------------------------\n"
                         " 🚀 PLAYERS:   %d    ⚔️  HOSTILES:  %d\n"
                         " 🛰️  STARBASES: %d    🌟 STARS:      %d\n"
                         " 🪐 PLANETS:   %d    🕳️  BLACK HOLES:%d\n"
                         " 🌫️  NEBULAS:   %d    ⚛️  PULSARS:    %d\n"
                         " ☄️  COMETS:    %d    🪨  ASTEROIDS:  %d\n"
                         " 🏗️  PLATFORMS: %d    🏚️  DERELICTS:  %d\n"
                         " 📡 COMM BUOYS:%d    🌀 RIFTS:      %d\n"
                         " ⚓ MINES:      %d    👾 MONSTERS:   %d\n"
                         "-------------------------------------------------", 
                         p_idx+1, pq1, pq2, pq3, 
                         players[i].state.probes[p_idx].s1, 
                         players[i].state.probes[p_idx].s2, 
                         players[i].state.probes[p_idx].s3,
                         lq->player_count, lq->npc_count,
                         lq->base_count, lq->star_count,
                         lq->planet_count, lq->bh_count,
                         lq->nebula_count, lq->pulsar_count,
                         lq->comet_count, lq->asteroid_count,
                         lq->platform_count, lq->derelict_count,
                         lq->buoy_count, lq->rift_count,
                         lq->mine_count, lq->monster_count);
            send_server_msg(i, "SCIENCE", msg);
        } else {
            send_server_msg(i, "COMPUTER", "Usage: aux report <1-3>");
        }
    } else if(strncmp(p_ptr, "recover", 7) == 0) {
        int p_idx;
        if (sscanf(p_ptr + 7, "%d", &p_idx) == 1) {
            p_idx--; 
            if (p_idx < 0 || p_idx >= 3 || !players[i].state.probes[p_idx].active) {
                send_server_msg(i, "COMPUTER", "Specified probe is not active.");
                return;
            }

            int pq1 = players[i].state.probes[p_idx].q1;
            int pq2 = players[i].state.probes[p_idx].q2;
            int pq3 = players[i].state.probes[p_idx].q3;

            if (pq1 != players[i].state.q1 || pq2 != players[i].state.q2 || pq3 != players[i].state.q3) {
                send_server_msg(i, "COMPUTER", "Probe is in a different quadrant. You must be in the same quadrant to recover it.");
                return;
            }

            float dx = players[i].state.probes[p_idx].s1 - (float)players[i].state.s1;
            float dy = players[i].state.probes[p_idx].s2 - (float)players[i].state.s2;
            float dz = players[i].state.probes[p_idx].s3 - (float)players[i].state.s3;
            float dist = sqrtf(dx*dx + dy*dy + dz*dz);

            if (dist > 2.0f) {
                send_server_msg(i, "COMPUTER", "Probe is too far for recovery (Distance > 2.0).");
                return;
            }

            /* Trigger Visual FX (set timer for sync) */
            players[i].state.recovery_fx = (NetPoint){
                players[i].state.probes[p_idx].s1,
                players[i].state.probes[p_idx].s2,
                players[i].state.probes[p_idx].s3,
                10 /* Active for 10 ticks to ensure sync */
            };

            players[i].state.probes[p_idx].active = 0;
            players[i].state.energy += 500;
            if (players[i].state.energy > 9999999) players[i].state.energy = 9999999;
            
            send_server_msg(i, "ENGINEERING", "Probe recovered. 500 Energy units salvaged and slot freed.");
        } else {
            send_server_msg(i, "COMPUTER", "Usage: aux recover <1-3>");
        }
    } else {
        /* No valid sub-command or empty 'aux' */
        send_server_msg(i, "COMPUTER", "AUXILIARY SYSTEMS:\n"
                                       " aux probe <QX> <QY> <QZ> : Launch sensor probe\n"
                                       " aux report <1-3>         : Get data from probe\n"
                                       " aux recover <1-3>        : Recover probe in sector\n"
                                       " aux jettison             : Eject Hyperdrive Core (WARNING!)");
    }
}

void handle_xxx(int i, const char *params) {
    send_server_msg(i, "COMPUTER", "Self-destruct sequence initiated. Zero-zero-zero-destruct-zero.");
    players[i].state.energy = 0; players[i].state.crew_count = 0; players[i].active = 0;
    players[i].state.boom = (NetPoint){(float)players[i].state.s1, (float)players[i].state.s2, (float)players[i].state.s3, 1};
}

void handle_hull(int i, const char *params) {
    if (players[i].state.inventory[7] >= 100) {
        players[i].state.inventory[7] -= 100;
        players[i].state.composite_plating += 500;
        send_server_msg(i, "ENGINEERING", "Hull reinforced with Composite plating. Structural integrity increased.");
    } else {
        send_server_msg(i, "COMPUTER", "Insufficient Composite for hull reinforcement (Req: 100).");
    }
}

void handle_jum(int i, const char *params) {
    int qx, qy, qz;
    if (sscanf(params, "%d %d %d", &qx, &qy, &qz) == 3) {
        if (!IS_Q_VALID(qx, qy, qz)) {
            send_server_msg(i, "COMPUTER", "Invalid quadrant coordinates.");
            return;
        }
        
        /* Cost calculation: 5000 Energy + 1 Isotope-X crystal */
        if (players[i].state.energy < 5000 || players[i].state.inventory[1] < 1) {
             send_server_msg(i, "ENGINEERING", "Insufficient resources for Wormhole generation (Req: 5000 Energy, 1 Isotope-X).");
             return;
        }

        players[i].state.energy -= 5000;
        players[i].state.inventory[1] -= 1;

        /* Calculate Wormhole Entrance Position (5 units in front of ship) */
        double rad_h = players[i].state.van_h * M_PI / 180.0;
        double rad_m = players[i].state.van_m * M_PI / 180.0;
        
        /* Local sector coordinates for the effect */
        double wx = players[i].state.s1 + cos(rad_m) * sin(rad_h) * 5.0;
        double wy = players[i].state.s2 + cos(rad_m) * -cos(rad_h) * 5.0;
        double wz = players[i].state.s3 + sin(rad_m) * 5.0;
        
        players[i].wx = wx; players[i].wy = wy; players[i].wz = wz;

        /* Set Destination */
        players[i].target_gx = (qx - 1) * 10.0 + 5.5; /* Center of target quadrant */
        players[i].target_gy = (qy - 1) * 10.0 + 5.5;
        players[i].target_gz = (qz - 1) * 10.0 + 5.5;
        
                        /* Engage */
        
                        players[i].nav_state = NAV_STATE_WORMHOLE;
        
                        players[i].nav_timer = 450; /* 15 seconds at 30Hz logic tick */
        
                        
        
                        players[i].warp_speed = 0; /* Stop ship */
        
        send_server_msg(i, "HELMSMAN", "Initiating trans-quadrant jump. Calculating Schwarzschild coordinates...");
    } else {
        send_server_msg(i, "COMPUTER", "Usage: jum <Q1> <Q2> <Q3>");
    }
}

void handle_ical(int i, const char *params) {
    double tx, ty, tz;
    if (sscanf(params, "%lf %lf %lf", &tx, &ty, &tz) == 3) {
        double dx = tx - players[i].state.s1;
        double dy = ty - players[i].state.s2;
        double dz = tz - players[i].state.s3;
        double d = sqrt(dx*dx + dy*dy + dz*dz);
        if (d < 0.001) {
            send_server_msg(i, "COMPUTER", "Target sector matches current position.");
            return;
        }
        double h = atan2(dx, -dy) * 180.0 / M_PI; if (h < 0) h += 360;
        double m = (d > 0.001) ? asin(dz / d) * 180.0 / M_PI : 0;
        
        float engine_mult = 8.0f + (players[i].state.power_dist[0] * 17.0f);
        /* Base speed for 100% impulse is 0.5 units/tick. 
           Actual speed = 0.5 * engine_mult units/tick.
           At 30 FPS, speed_sec = 0.5 * engine_mult * 30.0 units/sec. */
        double speed_sec = 0.5 * engine_mult * 30.0;
        double time_sec = d / speed_sec;

        char buf[512];
        sprintf(buf, "\n" CYAN ".--- IMPULSE NAVIGATION COMPUTATION -------------." RESET "\n"
                     " DESTINATION:  " WHITE "Sector [%.1f, %.1f, %.1f]" RESET "\n"
                     " BEARING:      " GREEN "Heading %.1f, Mark %+.1f" RESET "\n"
                     " DISTANCE:     " YELLOW "%.2f Sector Units" RESET "\n"
                     " EST. TIME:    " MAGENTA "%.2f seconds (at 100%% Impulse)" RESET "\n"
                     "-------------------------------------------------", 
                     tx, ty, tz, h, m, d, time_sec);
        send_server_msg(i, "COMPUTER", buf);
    } else {
        send_server_msg(i, "COMPUTER", "Usage: ical <X> <Y> <Z> (Target Sector Coords)");
    }
}

/* --- Command Registry Table --- */

static const CommandDef command_registry[] = {
    {"nav ", handle_nav, "Hyperdrive Navigation <H> <M> <W> [Factor]"},
    {"imp ", handle_imp, "Impulse Drive <H> <M> <S>"},
    {"jum ", handle_jum, "Wormhole Jump <Q1> <Q2> <Q3>"},
    {"apr ", handle_apr, "Approach target <ID> <DIST>"},
    {"cha",  handle_cha, "Chase locked target"},
    {"srs",  handle_srs, "Short Range Sensors"},
    {"lrs",  handle_lrs, "Long Range Sensors"},
    {"pha ", handle_pha, "Fire Ion Beams <ID> <E> or <E> (Lock)"},
    {"tor",  handle_tor, "Fire Torpedo <H> <M> or auto (Lock)"},
    {"she ", handle_she, "Shield Configuration <F> <R> <T> <B> <L> <RI>"},
    {"lock ",handle_lock, "Target Lock-on <ID>"},
    {"enc ", handle_enc,  "Encryption Toggle <algo>"},
    {"pow ", handle_pow,  "Power Allocation <E> <S> <W>"},
    {"psy",  handle_psy,  "Psychological Warfare (Bluff)"},
    {"scan ",handle_scan, "Detailed Scan <ID>"},
    {"clo",  handle_clo, "Cloaking Device"},
    {"bor",  handle_bor, "Boarding Party"},
    {"dis",  handle_dis, "Dismantle Wreck"},
    {"min",  handle_min, "Planetary Mining"},
    {"sco",  handle_sco, "Solar Scooping"},
    {"har",  handle_har, "Antimatter Harvest"},
    {"doc",  handle_doc, "Dock at Starbase"},
    {"con ", handle_con, "Resource Converter"},
    {"load ",handle_load, "Load Cargo"},
    {"rep",  handle_rep, "Repair Systems"},
    {"sta",  handle_sta, "Status Report"},
    {"inv",  handle_inv, "Inventory Report"},
    {"dam",  handle_dam, "Damage Report"},
    {"cal ", handle_cal, "Hyperdrive Calculator <QX><QY><QZ> [SX][SY][SZ]"},
    {"ical ",handle_ical, "Impulse Calculator (ETA)"},
    {"who",  handle_who, "Active Captains List"},
    {"help", handle_help, "Display this directory"},
    {"aux ", handle_aux, "Auxiliary (probe/report/recover)"},
    {"xxx",  handle_xxx, "Self-Destruct"},
    {"hull", handle_hull, "Reinforce Hull (100 Composite)"},
    {"supernova", handle_supernova, "Admin: Trigger Supernova"},
    {NULL, NULL, NULL}
};

void handle_help(int i, const char *params) {
    char *b = calloc(4096, sizeof(char));
    if (!b) return;
    
    strcpy(b, CYAN "\n--- LCARS COMMAND DIRECTORY ---" RESET);
    for (int c = 0; command_registry[c].name != NULL; c++) {
        char line[128];
        snprintf(line, sizeof(line), WHITE "%-8s" RESET " : %s\n", command_registry[c].name, command_registry[c].description);
        strcat(b, line);
    }
    send_server_msg(i, "COMPUTER", b);
    free(b);
}

void process_command(int i, const char *cmd) {
    pthread_mutex_lock(&game_mutex);
    
    /* Intercept numeric input for pending boarding actions */
    if (players[i].pending_bor_target > 0) {
        /* Check if input is a pure number 1-3 */
        if (strlen(cmd) == 1 && cmd[0] >= '1' && cmd[0] <= '3') {
            int choice = cmd[0] - '0';
            int tid = players[i].pending_bor_target;
            
            /* Target resolution based on ID range */
            double tx=0, ty=0, tz=0;
            ConnectedPlayer *target_p = NULL;

            if (tid <= 32) {
                target_p = &players[tid-1];
                tx = target_p->state.s1; ty = target_p->state.s2; tz = target_p->state.s3;
            } else if (tid >= 16000) {
                int pt_idx = tid - 16000;
                tx = platforms[pt_idx].x; ty = platforms[pt_idx].y; tz = platforms[pt_idx].z;
            }

            /* Verify distance */
            double dx = tx - players[i].state.s1;
            double dy = ty - players[i].state.s2;
            double dz = tz - players[i].state.s3;
            if (sqrt(dx*dx+dy*dy+dz*dz) > 1.2) {
                send_server_msg(i, "COMPUTER", "Target out of transporter range. Operation cancelled.");
            } else {
                if (players[i].pending_bor_type == 1) { /* ALLY PLAYER */
                    if (choice == 1) {
                        int amount = 50000; if(players[i].state.energy < amount) amount = players[i].state.energy;
                        players[i].state.energy -= amount; target_p->state.energy += amount;
                        send_server_msg(i, "ENGINEERING", "Energy transfer complete.");
                        send_server_msg(tid-1, "ENGINEERING", "Received emergency energy from allied vessel.");
                    } else if (choice == 2) {
                        int sys = rand()%10; target_p->state.system_health[sys] = 100.0f;
                        send_server_msg(i, "ENGINEERING", "Repairs performed on allied ship.");
                        send_server_msg(tid-1, "ENGINEERING", "Allied engineers fixed one of our systems.");
                    } else {
                        int crew = 20; if(players[i].state.crew_count < 50) crew = 0;
                        players[i].state.crew_count -= crew; target_p->state.crew_count += crew;
                        send_server_msg(i, "SECURITY", "Personnel transferred to ally.");
                        send_server_msg(tid-1, "SECURITY", "Allied reinforcements joined our crew.");
                    }
                } else if (players[i].pending_bor_type == 2) { /* ENEMY PLAYER */
                    if (rand()%100 < 30) {
                        int loss = 5 + rand()%10; players[i].state.crew_count -= loss;
                        send_server_msg(i, "SECURITY", "Raid repelled! Team suffered casualties.");
                    } else {
                        if (choice == 1) {
                            int sys = rand()%10; target_p->state.system_health[sys] = 0.0f;
                            send_server_msg(i, "BOARDING", "Sabotage successful. Enemy system offline.");
                            send_server_msg(tid-1, "CRITICAL", "Intruders sabotaged our systems!");
                        } else if (choice == 2) {
                            int res = 1 + rand()%6; int amt = target_p->state.inventory[res] / 2;
                            target_p->state.inventory[res] -= amt; players[i].state.inventory[res] += amt;
                            send_server_msg(i, "BOARDING", "Raid successful. Resources seized.");
                            send_server_msg(tid-1, "SECURITY", "Enemy raid in progress! Cargo hold breached!");
                        } else {
                            int pris = 2 + rand()%10; target_p->state.crew_count -= pris; players[i].state.prison_unit += pris;
                            send_server_msg(i, "SECURITY", "Hostages captured. Prisoners in Prison Unit.");
                            send_server_msg(tid-1, "SECURITY", "Intruders captured our officers!");
                        }
                    }
                } else if (players[i].pending_bor_type == 3) { /* PLATFORM */
                    int pt_idx = tid - 16000;
                    if (rand()%100 < 40) {
                        int loss = 10 + rand()%20; players[i].state.crew_count -= loss;
                        send_server_msg(i, "SECURITY", "Platform automated defenses active! Team suffered casualties.");
                    } else {
                        if (choice == 1) {
                            platforms[pt_idx].faction = players[i].faction;
                            send_server_msg(i, "BOARDING", "IFF Reprogrammed. Platform captured.");
                        } else if (choice == 2) {
                            platforms[pt_idx].active = 0;
                            players[i].state.boom = (NetPoint){(float)platforms[pt_idx].x, (float)platforms[pt_idx].y, (float)platforms[pt_idx].z, 1};
                            send_server_msg(i, "BOARDING", "Self-destruct triggered. Platform neutralized.");
                        } else {
                            players[i].state.inventory[5] += 250;
                            send_server_msg(i, "BOARDING", "Salvage successful. Retrieved 250 Isolinear Chips.");
                        }
                    }
                }
            }
            players[i].pending_bor_target = 0;
            pthread_mutex_unlock(&game_mutex);
            return;
        } else {
            players[i].pending_bor_target = 0;
        }
    }

    bool found = false;
    for (int c = 0; command_registry[c].name != NULL; c++) {
        size_t len = strlen(command_registry[c].name);
        if (strncmp(cmd, command_registry[c].name, len) == 0) {
            command_registry[c].handler(i, cmd + len);
            found = true;
            break;
        }
    }
    
    if (!found) {
        char first_word[32]; sscanf(cmd, "%31s", first_word);
        int cmd_idx = -1;
        for (int c = 0; command_registry[c].name != NULL; c++) {
            char reg_name[32]; sscanf(command_registry[c].name, "%31s", reg_name);
            if (strcmp(first_word, reg_name) == 0) { cmd_idx = c; break; }
        }

        if (cmd_idx != -1) {
            char hb[2048]; const char* n = first_word; const char* d = command_registry[cmd_idx].description;
            sprintf(hb, "\n" CYAN ".--- COMMAND ASSISTANCE: %s -------------------." RESET "\n", n);
            if (strcmp(n, "nav") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "nav <Heading> <Mark> <Dist> [Hyperdrive]" RESET "\n"
                           " INFO:   Main FTL propulsion. H: 0-359, M: -90/+90.\n"
                           "         Dist: Quadrants. Hyperdrive: 1.0-9.9 speed factor.\n");
            } else if (strcmp(n, "imp") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "imp <Heading> <Mark> <Speed%>" RESET "\n"
                           " INFO:   Sub-light propulsion. Speed: 1-100%% of max.\n"
                           "         Max speed scales with Engine power (see 'pow').\n");
            } else if (strcmp(n, "jum") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "jum <Q1> <Q2> <Q3>" RESET "\n"
                           " INFO:   Instantaneous trans-quadrant jump.\n"
                           "         COST: 5000 Energy + 1 Isotope-X Crystal.\n");
            } else if (strcmp(n, "apr") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "apr <ID> [Distance]" RESET "\n"
                           " INFO:   Autopilot approach. Default distance is 2.0.\n"
                           "         Works galaxy-wide for stationary and moving objects.\n");
            } else if (strcmp(n, "cha") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "cha" RESET "\n"
                           " INFO:   Chase mode. Intercepts and follows locked target.\n"
                           "         Maintains optimal combat/interaction distance.\n");
            } else if (strcmp(n, "srs") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "srs" RESET "\n"
                           " INFO:   Short Range Scan. Lists all objects in sector.\n"
                           "         Provides IDs, coordinates, and health status.\n");
            } else if (strcmp(n, "lrs") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "lrs" RESET "\n"
                           " INFO:   Long Range Scan. Maps adjacent 26 quadrants.\n"
                           "         Shows BPNBS-encoded population counts.\n");
            } else if (strcmp(n, "har") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "har" RESET "\n"
                           " INFO:   Harvest Antimatter from Black Holes. \n"
                           "         RANGE: < 3.1 units. Safety limit is 3.0.\n");
            } else if (strcmp(n, "min") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "min" RESET "\n"
                           " INFO:   Extracts minerals from Planet or Asteroid.\n"
                           "         RANGE: < 3.1 units. Consumes time per extraction.\n");
            } else if (strcmp(n, "pha") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "pha <ID> <Energy>" RESET "\n"
                           " INFO:   Fires directed Ion Beam banks. Damage decreases with distance.\n"
                           "         Requires at least 10%% Ion Beam capacitor charge.\n");
            } else if (strcmp(n, "tor") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "tor <H> <M>" RESET " or " GREEN "tor" RESET " (with Lock)\n"
                           " INFO:   Launches high-yield Plasma Torpedo.\n"
                           "         Cooldown: 5 seconds. Range: Sector-wide.\n");
            } else if (strcmp(n, "she") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "she <F> <R> <T> <B> <L> <RI>" RESET "\n"
                           " INFO:   Distributes total energy across 6 shield quadrants.\n");
            } else if (strcmp(n, "pow") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "pow <Engines> <Shields> <Weapons>" RESET "\n"
                           " INFO:   Sets reactor power priority (relative values).\n");
            } else if (strcmp(n, "load") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "load <1|2> <Amount>" RESET "\n"
                           " INFO:   Transfers cargo to active systems. 1:Antimatter, 2:Torpedoes.\n");
            } else if (strcmp(n, "bor") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "bor <ID>" RESET "\n"
                           " INFO:   Boarding operation (Range < 1.0). Provides tactical menus.\n");
            } else if (strcmp(n, "scan") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "scan <ID>" RESET "\n"
                           " INFO:   Detailed tactical analysis of a target or celestial body.\n");
            } else if (strcmp(n, "rep") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "rep <ID>" RESET "\n"
                           " INFO:   Repairs a damaged system (Req: 50 Tritanium, 10 Isolinear).\n");
            } else if (strcmp(n, "clo") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "clo" RESET "\n"
                           " INFO:   Engage/Disengage Cloaking Device. Consumes 15 energy/tick.\n");
            } else if (strcmp(n, "sta") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "sta" RESET "\n"
                           " INFO:   Strategic Status Report. Comprehensive ship diagnostics.\n");
            } else if (strcmp(n, "inv") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "inv" RESET "\n"
                           " INFO:   Cargo manifest. Lists all resources and prison count.\n");
            } else if (strcmp(n, "cal") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "cal <Q1-3> [S1-3]" RESET "\n"
                           " INFO:   Astrometrics computation for bearing and distance.\n");
            } else if (strcmp(n, "aux") == 0) {
                strcat(hb, WHITE " USAGE:  " GREEN "aux <probe|report|recover>" RESET "\n"
                           " INFO:   Deep Space probe management and mission reconnaissance.\n");
            } else {
                strcat(hb, WHITE " USAGE:  " GREEN); strcat(hb, d); strcat(hb, RESET "\n");
            }
            strcat(hb, CYAN "---------------------------------------------------" RESET);
            send_server_msg(i, "COMPUTER", hb);
        } else {
            send_server_msg(i, "COMPUTER", "Invalid command. Type 'help' for assistance.");
        }
    }
    pthread_mutex_unlock(&game_mutex);
}
