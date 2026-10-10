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
#include "../../include/telemetry.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include "server_internal.h"
#include "game_config.h"
#include "shared_state.h"
#include "nav_math.h"
#include "ui.h"
#include <stddef.h>

/* Helper macro to safely append to a buffer with length check (used in handle_srs) */
#define SAFE_APPEND(buffer, max_len, format, ...) do { \
    size_t _cur_len = strlen(buffer); \
    if (_cur_len < (max_len)) { \
        snprintf((buffer) + _cur_len, (max_len) - _cur_len, format, ##__VA_ARGS__); \
    } \
} while(0)

/* Prototypes to avoid 'undeclared' errors */
void handle_nav(int i, const char *params, bool *should_disconnect);
void handle_imp(int i, const char *params, bool *should_disconnect);
void handle_pos(int i, const char *params, bool *should_disconnect);
void handle_vec(int i, const char *params, bool *should_disconnect);
void handle_jum(int i, const char *params, bool *should_disconnect);
void handle_apr(int i, const char *params, bool *should_disconnect);
void handle_cha(int i, const char *params, bool *should_disconnect);
void handle_srs(int i, const char *params, bool *should_disconnect);
void handle_lrs(int i, const char *params, bool *should_disconnect);
void handle_pha(int i, const char *params, bool *should_disconnect);
void handle_tor(int i, const char *params, bool *should_disconnect);
void handle_she(int i, const char *params, bool *should_disconnect);
void handle_lock(int i, const char *params, bool *should_disconnect);
void handle_enc(int i, const char *params, bool *should_disconnect);
void handle_link(int i, const char *params, bool *should_disconnect);
void handle_pow(int i, const char *params, bool *should_disconnect);
void handle_psy(int i, const char *params, bool *should_disconnect);
void handle_scan(int i, const char *params, bool *should_disconnect);
void handle_clo(int i, const char *params, bool *should_disconnect);
void handle_bor(int i, const char *params, bool *should_disconnect);
void handle_dis(int i, const char *params, bool *should_disconnect);
void handle_min(int i, const char *params, bool *should_disconnect);
void handle_sco(int i, const char *params, bool *should_disconnect);
void handle_har(int i, const char *params, bool *should_disconnect);
void handle_doc(int i, const char *params, bool *should_disconnect);
void handle_con(int i, const char *params, bool *should_disconnect);
void handle_load(int i, const char *params, bool *should_disconnect);
void handle_rep(int i, const char *params, bool *should_disconnect);
void handle_fix(int i, const char *params, bool *should_disconnect);
void handle_sta(int i, const char *params, bool *should_disconnect);
void handle_inv(int i, const char *params, bool *should_disconnect);
void handle_dam(int i, const char *params, bool *should_disconnect);
void handle_cal(int i, const char *params, bool *should_disconnect);
void handle_ical(int i, const char *params, bool *should_disconnect);
void handle_who(int i, const char *params, bool *should_disconnect);
void handle_help(int i, const char *params, bool *should_disconnect);
void handle_aux(int i, const char *params, bool *should_disconnect);
void handle_und(int i, const char *params, bool *should_disconnect);
void handle_xxx(int i, const char *params, bool *should_disconnect);
void handle_zztop(int i, const char *params, bool *should_disconnect);
void handle_hull(int i, const char *params, bool *should_disconnect);
void handle_supernova(int i, const char *params, bool *should_disconnect);
void handle_axs(int i, const char *params, bool *should_disconnect);
void handle_grd(int i, const char *params, bool *should_disconnect);
void handle_bridge(int i, const char *params, bool *should_disconnect);
void handle_pilot(int i, const char *params, bool *should_disconnect);

void handle_map(int i, const char *params, bool *should_disconnect);
void handle_red(int i, const char *params, bool *should_disconnect);
void handle_orb(int i, const char *params, bool *should_disconnect);

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
    void (*handler)(int p_idx, const char *params, bool *should_disconnect);
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

#include "commands_handlers.inc"
static const CommandDef command_registry[] = {
    {"dbgspn", handle_dbgspn, "TEMP-DEBUG: spawn local NPCs/monsters"},
    {"dbgnpc", handle_dbgnpc, "TEMP-DEBUG: report NPC state"},
    {"nav", handle_nav, "Hyperdrive Navigation (H 0-359, M -90/90, W Dist, F Factor 1-9.9)"},
    {"imp", handle_imp, "Impulse Drive (H, M, Speed 0.0-1.0). imp 0 0 0 to stop."},
    {"pos", handle_pos, "Position Ship (Align orientation without movement)"},
    {"vec", handle_vec, "Vector Thrust (H, M, Speed 0.0-1.0): move along course without changing attitude. vec H M 0 to stop."},
    {"jum", handle_jum, "Wormhole Jump (Usage: jum [type] q1 q2 q3)"},
    {"apr", handle_apr, "Approach target autopilot (ID DIST). Works on Lock."},
    {"cha",  handle_cha, "Chase locked target (Inter-sector aware)"},
    {"srs",  handle_srs, "Short Range Sensors (Current Quadrant View)"},
    {"lrs",  handle_lrs, "Long Range Sensors (LCARS Tactical Grid)"},
    {"pha", handle_pha, "Fire Ion Beams at locked target (uses Energy E)"},
    {"tor",  handle_tor, "Launch Plasma Torpedo at locked target or Heading/Mark"},
    {"she", handle_she, "Shield Configuration (F R T B L RI)"},
    {"lock", handle_lock, "Target Lock-on (ID or 'off' to release)"},
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
    {"rad",  NULL,       "Radio Subspace (rad <msg>, rad @Faction <msg>, rad #ID <msg>)"},
    {"tune", NULL,       "Identity Tuner (tune <CaptainName>): Listen to a Captain's Level B frequency"},
    {"enc",  handle_enc,  "Fleet Encryption (enc <algo>): Standard Fleet-wide frequency (Level A)"},
    {"enc2", handle_enc2, "Identity Mode (enc2 <algo>): Broadcast on your personal frequency (Level B)"},
    {"enc3", handle_enc3, "Peer Mode (enc3 <CaptainName> <algo>): Secure P2P communication (Level C)"},
    {"enc4", handle_enc4, "Exclusive Mode (enc4 <CaptainName> <algo>): Filtered Private Link (Level D)"},
    {"link", handle_link, "Tactical Link (link <CaptainName>): Reciprocal X25519 E2E Handshake"},
    {"help", handle_help, "Display LCARS Command Directory"},
    {"exit", NULL,        "Exit game and disconnect from server"},
    {"quit", NULL,        "Exit game and disconnect from server (alias)"},
    {"aux", handle_aux, "Auxiliary (probe/report/recover/jettison)"},
    {"xxx",  handle_xxx, "Emergency Repositioning (Tactical Warp)"},
    {"zztop", handle_zztop, "PERMANENT PROFILE DELETION (NO UNDO!)"},
    {"hull", handle_hull, "Reinforce Hull (Uses 100 Composite for +500 Plating)"},
    {"supernova", handle_supernova, "Admin: Trigger Supernova"},
    {"axs",  handle_axs,  "Toggle AR Compass"},
    {"grd",  handle_grd,  "Toggle Tactical Grid"},
    {"bridge", handle_bridge, "Change Bridge View (top, bottom, up, down, left, right, rear, off)"},
    {"pilot", handle_pilot, "Cockpit Pilot Mode: flight stick drives the ship (pilot on/off)"},

    {"map",    handle_map,    "Toggle Galaxy Map. Filters: st,pl,bs,en,bh,ne,pu,is,co,as,de,mi,bu,pf,ri,mo,qu"},
    {"red",    handle_red,    "Toggle Red Alert / Condition Green"},
    {"orb",    handle_orb,    "Enter orbit around target celestial body (Planet, Star, BH, Pulsar, Quasar) < 1.0"},
    {"und",    handle_und,    "Undock from Starbase"},
    {"undock", handle_und,    "Undock from Starbase (alias)"},
    {NULL, NULL, NULL}
};

void handle_red(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    if (players[i].state.system_health[6] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "COMPUTER", "RED ALERT FAILURE: Tactical coordination core damaged.");
        return;
    }
    players[i].state.red_alert = !players[i].state.red_alert;
    
    /* Server-side logging of tactical status */
    time_t now_red = time(NULL);
    struct tm *t_red = localtime(&now_red);
    char time_red[64];
    strftime(time_red, sizeof(time_red), "%Y-%m-%d %H:%M:%S", t_red);
    slog("\033[1;33m[TACTICAL]\033[0m   Captain \033[1;37m%-15s\033[0m set alert to \033[1;%sm%-15s\033[0m [\033[1;33m%s\033[0m]\n", 
           players[i].name, players[i].state.red_alert ? "31" : "32", players[i].state.red_alert ? "RED ALERT" : "CONDITION GREEN", time_red);

    if (players[i].state.red_alert) {
        send_server_msg(i, "COMMAND", "RED ALERT! Shields energized. Weapons to standby.");
    } else {
        send_server_msg(i, "COMMAND", "Stand down to Condition Green.");
    }
}

void handle_orb(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    if (players[i].state.system_health[1] < THRESHOLD_SYS_CRITICAL) {
        send_server_msg(i, "ENGINEERING", "ORBITAL ENTRY FAILURE: Maneuvering thrusters offline.");
        return;
    }
    int tid = players[i].state.lock_target;
    double target_x = -1, target_y = -1, target_z = -1;
    const char *target_type = "object";
    bool valid_target = false;

    if (tid >= GALAXY_OBJECT_MIN_PLANET && tid <= GALAXY_OBJECT_MAX_PLANET) {
        int p = tid - GALAXY_OBJECT_MIN_PLANET;
        if (p >= 0 && p < MAX_PLANETS && planets[p].active) {
            target_x = planets[p].x;
            target_y = planets[p].y;
            target_z = planets[p].z;
            target_type = "planet";
            valid_target = true;
        }
    } else if (tid >= GALAXY_OBJECT_MIN_STAR && tid <= GALAXY_OBJECT_MAX_STAR) {
        int s = tid - GALAXY_OBJECT_MIN_STAR;
        if (s >= 0 && s < MAX_STARS && stars_data[s].active) {
            target_x = stars_data[s].x;
            target_y = stars_data[s].y;
            target_z = stars_data[s].z;
            target_type = "star";
            valid_target = true;
        }
    } else if (tid >= GALAXY_OBJECT_MIN_BLACKHOLE && tid <= GALAXY_OBJECT_MAX_BLACKHOLE) {
        int b = tid - GALAXY_OBJECT_MIN_BLACKHOLE;
        if (b >= 0 && b < MAX_BH && black_holes[b].active) {
            target_x = black_holes[b].x;
            target_y = black_holes[b].y;
            target_z = black_holes[b].z;
            target_type = "black hole";
            valid_target = true;
        }
    } else if (tid >= GALAXY_OBJECT_MIN_PULSAR && tid <= GALAXY_OBJECT_MAX_PULSAR) {
        int p = tid - GALAXY_OBJECT_MIN_PULSAR;
        if (p >= 0 && p < MAX_PULSARS && pulsars[p].active) {
            target_x = pulsars[p].x;
            target_y = pulsars[p].y;
            target_z = pulsars[p].z;
            target_type = "pulsar";
            valid_target = true;
        }
    } else if (tid >= GALAXY_OBJECT_MIN_QUASAR && tid <= GALAXY_OBJECT_MAX_QUASAR) {
        int q = tid - GALAXY_OBJECT_MIN_QUASAR;
        if (q >= 0 && q < MAX_QUASARS && quasars[q].active) {
            target_x = quasars[q].x;
            target_y = quasars[q].y;
            target_z = quasars[q].z;
            target_type = "quasar";
            valid_target = true;
        }
    }

    if (valid_target) {
        double dx = target_x - players[i].state.s1;
        double dy_val = target_y - players[i].state.s2;
        double dz = target_z - players[i].state.s3;
        double d = sqrt(dx*dx + dy_val*dy_val + dz*dz);
        if (d < DIST_BOARDING_MAX) {
            players[i].nav_state = NAV_STATE_ORBIT;
            char msg[128];
            sprintf(msg, "Establishing stable orbit around target %s.", target_type);
            send_server_msg(i, "HELMSMAN", msg);
        } else {
            char err_msg[128];
            sprintf(err_msg, "Target %s too distant for orbital capture (< %.1f required).", target_type, DIST_BOARDING_MAX);
            send_server_msg(i, "COMPUTER", err_msg);
        }
    } else {
        send_server_msg(i, "COMPUTER", "No valid celestial object locked for orbital entry.");
    }
}

void handle_link(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    send_server_msg(i, "COMPUTER", "Link initialization handled by client-side frequency exchange.");
}

void handle_help(int i, const char *params, bool *should_disconnect) {
    (void)params; (void)should_disconnect;
    send_server_msg(i, "COMPUTER", CYAN "\n--- LCARS COMMAND DIRECTORY ---" RESET);
    
    for (int c = 0; command_registry[c].name != NULL; c++) {
        char line[512];
        snprintf(line, sizeof(line), WHITE "%-10s" RESET " : %s", 
                 command_registry[c].name, command_registry[c].description);
        send_server_msg(i, "COMPUTER", line);
    }
    
    send_server_msg(i, "COMPUTER", CYAN "--- END OF DIRECTORY ---" RESET);
}

bool process_command(int i, const char *cmd) {
    pthread_mutex_lock(&game_mutex);
    
    bool profile_deleted = false;

    /* 1. Intercept numeric input for pending boarding actions */
    if (players[i].pending_bor_target > 0) {
        if (strlen(cmd) == 1 && cmd[0] >= '1' && cmd[0] <= '4') {
            int choice = cmd[0] - '0';
            int tid = players[i].pending_bor_target;
            double tx=0, ty=0, tz=0;
            ConnectedPlayer *target_p = NULL;

            /* Resolve target position */
            if (tid >= GALAXY_OBJECT_MIN_PLAYER && tid <= MAX_CLIENTS) { target_p = &players[tid-1]; tx = target_p->state.s1; ty = target_p->state.s2; tz = target_p->state.s3; }
            else if (tid >= GALAXY_OBJECT_MIN_NPC && tid <= GALAXY_OBJECT_MAX_NPC) { int n_idx = tid - GALAXY_OBJECT_MIN_NPC; tx = npcs[n_idx].x; ty = npcs[n_idx].y; tz = npcs[n_idx].z; }
            else if (tid >= GALAXY_OBJECT_MIN_DERELICT && tid <= GALAXY_OBJECT_MAX_DERELICT) { int d_idx = tid - GALAXY_OBJECT_MIN_DERELICT; tx = derelicts[d_idx].x; ty = derelicts[d_idx].y; tz = derelicts[d_idx].z; }
            else if (tid >= GALAXY_OBJECT_MIN_PLATFORM && tid <= GALAXY_OBJECT_MAX_PLATFORM) { int pt_idx = tid - GALAXY_OBJECT_MIN_PLATFORM; tx = platforms[pt_idx].x; ty = platforms[pt_idx].y; tz = platforms[pt_idx].z; }

            /* Verify distance again during choice */
            double dx = tx - players[i].state.s1, dy = ty - players[i].state.s2, dz = tz - players[i].state.s3;
            if (sqrt(dx*dx+dy*dy+dz*dz) > 1.2) {
                send_server_msg(i, "COMPUTER", "Target out of range. Operation cancelled.");
            } else {
                if (players[i].pending_bor_type == 1) { /* ALLY PLAYER */
                    if (choice == 1) { 
                        if (players[i].state.energy >= (uint64_t)DMG_TORPEDO_PLATFORM) {
                            players[i].state.energy -= (uint64_t)DMG_TORPEDO_PLATFORM; 
                            target_p->state.energy += (uint64_t)DMG_TORPEDO_PLATFORM;
                            if (target_p->state.energy > MAX_ENERGY_CAPACITY) target_p->state.energy = MAX_ENERGY_CAPACITY;
                            send_server_msg(i, "ENGINEERING", "Energy transferred.");
                        } else {
                            send_server_msg(i, "COMPUTER", "Insufficient energy reserves for transfer.");
                        }
                    }
                    else if (choice == 2) { int s = rand()%MAX_SYSTEMS; target_p->state.system_health[s] = (double)YIELD_HARVEST_MAX; send_server_msg(i, "ENGINEERING", "Repairs complete."); }
                    else { 
                        int t_crew = (players[i].state.crew_count >= (int)COST_MANEUVER_ADJUST) ? (int)COST_MANEUVER_ADJUST : players[i].state.crew_count;
                        players[i].state.crew_count -= t_crew; target_p->state.crew_count += t_crew; 
                        send_server_msg(i, "SECURITY", "Crew transferred."); 
                    }
                } else if (players[i].pending_bor_type == 2) { /* ENEMY PLAYER / NPC */
                    if (tid >= GALAXY_OBJECT_MIN_PLAYER && tid <= MAX_CLIENTS) {
                        if (choice == 1) { int s = rand()%MAX_SYSTEMS; target_p->state.system_health[s] = 0.0; send_server_msg(i, "BOARDING", "Sabotage successful."); }
                        else if (choice == 2) { int r = 1 + rand()%MAX_RESOURCE_TYPES; int a = target_p->state.inventory[r]/(int)RATIO_ENERGY_REDUCTION; target_p->state.inventory[r] -= a; players[i].state.inventory[r] += a; send_server_msg(i, "BOARDING", "Resources seized."); }
                        else { 
                            int p = YIELD_BOARD_CREW_MIN + rand()%YIELD_BOARD_CREW_RANGE; 
                            if (target_p->state.crew_count < p) p = target_p->state.crew_count;
                            target_p->state.crew_count -= p; players[i].state.prison_unit += p; 
                            send_server_msg(i, "SECURITY", "Hostages taken."); 
                        }
                    } else if (tid >= GALAXY_OBJECT_MIN_NPC && tid <= GALAXY_OBJECT_MAX_NPC) {
                        /* NPC Sabotage Effects */
                        NPCShip *target_npc = &npcs[tid-GALAXY_OBJECT_MIN_NPC];
                        if (choice == 1) { 
                            target_npc->engine_health = 0.0; 
                            if (target_npc->energy > SHIELD_MAX_STRENGTH) target_npc->energy -= SHIELD_MAX_STRENGTH; else target_npc->energy = 0;
                            send_server_msg(i, "BOARDING", "NPC propulsion core sabotaged. Vessell is drifting."); 
                        }
                        else if (choice == 2) { players[i].state.inventory[1 + rand()%(MAX_RESOURCE_TYPES - 1)] += COST_ACTION_MED; send_server_msg(i, "BOARDING", "Raid successful. Secured NPC cargo."); }
                        else { 
                            int p = YIELD_BOARD_NPC_MIN + rand()%YIELD_BOARD_NPC_RANGE; 
                            if (target_npc->health < p) p = target_npc->health; /* For NPCs we use health as a crew approximation */
                            target_npc->health -= p;
                            players[i].state.prison_unit += p; 
                            send_server_msg(i, "SECURITY", "Captured enemy personnel."); 
                        }
                    }
                } else if (players[i].pending_bor_type == 3) { /* PLATFORM */
                    int pt_idx = tid - GALAXY_OBJECT_MIN_PLATFORM;
                    if (choice == 1) { platforms[pt_idx].faction = players[i].faction; send_server_msg(i, "BOARDING", "Platform captured."); }
                    else if (choice == 2) { platforms[pt_idx].active = 0; push_server_event(i, IPC_EV_BOOM, platforms[pt_idx].x, platforms[pt_idx].y, platforms[pt_idx].z, 0, 0, 0, 1); send_server_msg(i, "BOARDING", "Platform destroyed."); }
                    else { players[i].state.inventory[5] += COST_PQC_INIT; send_server_msg(i, "BOARDING", "Tech salvaged."); }
                } else if (players[i].pending_bor_type == 4) { /* DERELICT WRECK */
                    
                    /* 1. Choice Specific Reward */
                    if (choice == 1) { /* Salvage extra resources */
                        int r = 1 + rand()%MAX_RESOURCE_TYPES; players[i].state.inventory[r] += (COST_ACTION_HIGH + COST_ACTION_MED); 
                        send_server_msg(i, "BOARDING", "Cargo hold breached. Significant resources recovered."); 
                    }
                    else if (choice == 2) { /* Recover Map Data */
                        int rev = 0; for(int r=0; r<10; r++) { int rq1=rand()%10+1, rq2=rand()%10+1, rq3=rand()%10+1; if (players[i].state.z[rq1][rq2][rq3] == 0) { players[i].state.z[rq1][rq2][rq3] = 1; rev++; } if (rev >= 4) break; }
                        send_server_msg(i, "BOARDING", "Navigational logs decrypted. Star-charts updated with new quadrants."); 
                    }
                    else if (choice == 3) { /* Emergency Field Repairs */
                        int s = rand()%MAX_SYSTEMS; players[i].state.system_health[s] = (double)YIELD_HARVEST_MAX;
                        send_server_msg(i, "BOARDING", "Engineers recovered compatible spare parts. System fully restored."); 
                    }
                    else if (choice == 4) { /* Crew Rescue (Survivors) / Take Prisoners */
                        int d_idx = tid - GALAXY_OBJECT_MIN_DERELICT;
                        int found_people = YIELD_BOARD_NPC_MIN + rand()%YIELD_BOARD_CREW_RANGE;
                        if (derelicts[d_idx].faction == players[i].faction) {
                            players[i].state.crew_count += found_people;
                            char msg[128]; sprintf(msg, "Search teams found %d survivors in stasis. They have been integrated into the crew.", found_people);
                            send_server_msg(i, "SECURITY", msg);
                        } else {
                            players[i].state.prison_unit += found_people;
                            char msg[128]; sprintf(msg, "Boarding party secured %d enemy personnel from stasis pods. They have been moved to the prison unit.", found_people);
                            send_server_msg(i, "SECURITY", msg);
                        }
                    }

                    /* 2. Removed automatic dismantling / collapse logic as requested.
                       The derelict remains active for further boarding or dismantling via 'dis' command. */
                }
            }
            players[i].pending_bor_target = 0;
            pthread_mutex_unlock(&game_mutex);
            return false;
        } else players[i].pending_bor_target = 0;
    }

    /* 1.5 Hull/Crew Integrity Check: Block most commands if hull or crew is 0 or less */
    if (players[i].state.hull_integrity <= 0.0 || players[i].state.crew_count <= 0) {
        bool is_emergency_allowed = false;
        if (strcmp(cmd, "xxx") == 0) {
            is_emergency_allowed = true;
        } else if (strncmp(cmd, "rad", 3) == 0) { /* rad, rad ... */
            is_emergency_allowed = true;
        } else if (strncmp(cmd, "sta", 3) == 0) {
            is_emergency_allowed = true;
        } else if (strncmp(cmd, "who", 3) == 0) {
            is_emergency_allowed = true;
        } else if (strncmp(cmd, "help", 4) == 0) {
            is_emergency_allowed = true;
        }
        
        if (!is_emergency_allowed) {
            send_server_msg(i, "COMPUTER", "CRITICAL ERROR: Vessel integrity compromised. All tactical and navigation systems OFFLINE. Only Emergency Egress (xxx) or Radio (rad) available.");
            pthread_mutex_unlock(&game_mutex);
            return false;
        }
    }

    /* 2. Docking Restrictions Logic */
    bool is_action = true;
    const char* allowed[] = {"rad", "sta", "inv", "dam", "who", "help", "cal", "ical", "map", "axs", "grd", "bridge", "pilot", "enc", "und", "exit", "quit", NULL};

    for(int a=0; allowed[a]; a++) {
        if(strncmp(cmd, allowed[a], strlen(allowed[a])) == 0) { is_action = false; break; }
    }

    if (players[i].is_docked) {
        /* Any movement command will undock the ship */
        /* "vec" is a movement command too (vector thrust, 2026.10.07.02):
         * handle_vec undocks, so the clamps release the same way. */
        if (strncmp(cmd, "nav", 3) == 0 || strncmp(cmd, "imp", 3) == 0 || strncmp(cmd, "pos", 3) == 0 || strncmp(cmd, "vec", 3) == 0 || strncmp(cmd, "jum", 3) == 0 || strncmp(cmd, "und", 3) == 0) {
            players[i].is_docked = 0;
            if (strncmp(cmd, "und", 3) != 0) {
                send_server_msg(i, "STARBASE", "Auto-Undock triggered. Docking clamps released.");
            }
        } 
        else if (is_action) {
            send_server_msg(i, "COMPUTER", "COMMAND DENIED: All tactical systems locked while docked. Release clamps by engaging engines (nav/imp/pos).");
            pthread_mutex_unlock(&game_mutex);
            return false;
        }
    }

    bool found = false;
    for (int c = 0; command_registry[c].name != NULL; c++) {
        size_t len = strlen(command_registry[c].name);
        if (strncmp(cmd, command_registry[c].name, len) == 0) {
            /* Check for exact match or followed by space */
            if (cmd[len] == '\0' || cmd[len] == ' ') {
                /* Per-command trace: the category IS the command name
                 * ("pha 2 100" -> [PHA]), so --log-category=PHA isolates
                 * the whole phaser chain across server+clients+viewers. */
                char cmdcat[8];
                sglog_cmd_category(cmd, cmdcat);
                SG_TRACE3(cmdcat, "player %d (%s) dispatch cmd '%s' params '%s'",
                          i, players[i].name, command_registry[c].name, cmd + len);
                if (command_registry[c].handler) {
                    command_registry[c].handler(i, cmd + len, &profile_deleted);
                } else {
                    send_server_msg(i, "COMPUTER", "INFO: Command handled locally by tactical terminal.");
                }
                found = true; 
                break;
            }
        }
    }
    if (!found) send_server_msg(i, "COMPUTER", "Invalid command.");
    pthread_mutex_unlock(&game_mutex);
    return profile_deleted;
}
