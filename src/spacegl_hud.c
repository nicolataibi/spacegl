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
 * SPACE GL - ADVANCED TACTICAL HUD (NCURSES)
 */

#define _DEFAULT_SOURCE
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <stdatomic.h>
#include <inttypes.h>
#include "game_config.h"
#include "shared_state.h"
#include "../include/network.h"

#define HUD_REFRESH_USEC 50000

#define REFRESH_RATE_USEC HUD_REFRESH_USEC 

void draw_bar(int y, int x, int width, double val, double max, int color_pair) {
    attron(COLOR_PAIR(color_pair));
    int fill = (int)((val / max) * (double)width);
    if (fill > width) fill = width;
    if (fill < 0) fill = 0;
    for (int i = 0; i < width; i++) {
        mvaddch(y, x + i, (i < fill) ? ACS_CKBOARD : ACS_BULLET);
    }
    attroff(COLOR_PAIR(color_pair));
}

const char* get_faction_name(int f) {
    switch(f) {
        case FACTION_ALLIANCE: return "ALLIANCE";
        case FACTION_KORTHIAN: return "KORTHIAN";
        case FACTION_XYLARI:   return "XYLARI";
        case FACTION_SWARM:    return "SWARM";
        case FACTION_VESPERIAN: return "VESPERIAN";
        case FACTION_JEM_HADAR: return "ASCENDANT";
        case FACTION_THOLIAN:   return "QUARZITE";
        case FACTION_GORN:      return "SAURIAN";
        case FACTION_GILDED:    return "GILDED";
        case FACTION_SPECIES_8472: return "FLUIDIC";
        case FACTION_BREEN:     return "CRYOS";
        case FACTION_HIROGEN:   return "APEX";
        default: return "RENEGADE";
    }
}

const char* get_ship_class_full(int c) {
    const char* names[] = {"LEGACY", "SCOUT", "HEAVY CRUISER", "MULTI-ENGINE", "ESCORT", "EXPLORER", "FLAGSHIP", "SCIENCE", "CARRIER", "TACTICAL", "DIPLOMATIC", "RESEARCH", "FRIGATE"};
    if (c >= 0 && c <= 12) return names[c];
    return "UNKNOWN VESSEL";
}

const char* get_nav_state_name(int s) {
    switch(s) {
        case 0: return "IDLE"; case 1: return "ALIGN"; case 2: return "HYPERDRIVE";
        case 3: return "REALIGN"; case 4: return "IMPULSE"; case 5: return "CHASE";
        case 7: return "WORMHOLE"; case 9: return "DOCKING"; case 10: return "DRIFT";
        case 13: return "APPROACH"; default: return "MANEUVER";
    }
}

const char* get_crypto_name(int a) {
    const char* algos[] = {"NONE", "AES-256", "CHACHA20", "ARIA", "CAMELLIA", "SEED", "CAST5", "IDEA", "3DES", "BLOWFISH", "RC4", "DES", "PQC-KEM", "MCELIECE", "DILITHIUM", "SERPENT", "TWOFISH", "SM4", "ASCON", "PRESENT", "GOST", "SALSA"};
    if (a >= 0 && a <= MAX_CRYPTO_ALGOS) return algos[a];
    return "UNKNOWN";
}

int current_color_idx = 0;
short COLOR_AMBER = 8; /* Custom color index */
short hud_colors[] = {COLOR_GREEN, COLOR_BLUE, 8, COLOR_WHITE, COLOR_RED}; 

void cycle_hud_colors() {
    short color = hud_colors[current_color_idx];
    /* Update pairs to the new theme color */
    for (int i = 1; i <= 6; i++) {
        init_pair(i, color, COLOR_BLACK);
    }
    /* Pair 7 is ALWAYS Red for alerts */
    init_pair(7, COLOR_RED, COLOR_BLACK);
}

void draw_shm_inspector(SharedIPC *shm_ptr, GameState *st, int page) {
    attron(COLOR_PAIR(6) | A_BOLD);
    mvprintw(2, 2, "[ SHARED MEMORY INSPECTOR - PAGE %d/6 ]", page + 1);
    attroff(A_BOLD);

    int y = 4;

    switch(page) {
        case 0: // Core Systems
            mvprintw(y++, 2, "shm_energy: %" PRIu64, st->shm_energy);
            mvprintw(y++, 2, "shm_hull_integrity: %.2f", st->shm_hull_integrity);
            mvprintw(y++, 2, "shm_life_support: %.2f", st->shm_life_support);
            mvprintw(y++, 2, "shm_crew: %d", st->shm_crew);
            mvprintw(y++, 2, "shm_prison_unit: %d", st->shm_prison_unit);
            y++;
            mvprintw(y++, 2, "Shields:");
            for(int i=0; i<6; i++) mvprintw(y++, 4, "[%d]: %d", i, st->shm_shields[i]);
            y++;
            mvprintw(y++, 2, "System Health:");
            for(int i=0; i<10; i++) mvprintw(y++, 4, "[%d]: %.2f%%", i, st->shm_system_health[i]);
            y++;
            mvprintw(y++, 2, "Power Dist: Eng:%.2f Shl:%.2f Wpn:%.2f", st->shm_power_dist[0], st->shm_power_dist[1], st->shm_power_dist[2]);
            break;

        case 1: // Navigation
            mvprintw(y++, 2, "Quadrant: %d-%d-%d", st->shm_q[0], st->shm_q[1], st->shm_q[2]);
            mvprintw(y++, 2, "Sector (shm_s): %.4f, %.4f, %.4f", st->shm_s[0], st->shm_s[1], st->shm_s[2]);
            mvprintw(y++, 2, "Heading (shm_h): %.2f", st->shm_h);
            mvprintw(y++, 2, "Mark (shm_m): %.2f", st->shm_m);
            mvprintw(y++, 2, "Roll (shm_r): %.2f", st->shm_r);
            mvprintw(y++, 2, "Nav State: %d (%s)", st->shm_nav_state, get_nav_state_name(st->shm_nav_state));
            mvprintw(y++, 2, "ETA: %.2f s (Time to Destination)", st->shm_eta);
            mvprintw(y++, 2, "Flags: Docked=%d, Jammed=%d, Cloaked=%d", st->shm_is_docked, st->shm_is_jammed, st->is_cloaked);
            mvprintw(y++, 2, "Map Filter: %d", st->shm_map_filter);
            break;

        case 2: // Network & Crypto
            mvprintw(y++, 2, "Net Uptime: %ld", st->net_uptime);
            mvprintw(y++, 2, "Net KBPS: %.2f", st->net_kbps);
            mvprintw(y++, 2, "Net Efficiency: %.2f%%", st->net_efficiency);
            mvprintw(y++, 2, "Net Jitter: %.4f ms", st->net_jitter);
            mvprintw(y++, 2, "Net Integrity: %.2f%%", st->net_integrity);
            mvprintw(y++, 2, "Packet Count: %d", st->net_packet_count);
            mvprintw(y++, 2, "Avg Packet Size: %d", st->net_avg_packet_size);
            y++;
            mvprintw(y++, 2, "Crypto Algo: %d (%s)", st->shm_crypto_algo, get_crypto_name(st->shm_crypto_algo));
            mvprintw(y++, 2, "Encryption Flags: 0x%08X", st->shm_encryption_flags);
            mvprintw(y++, 2, "Frame ID: %lld", st->frame_id);
            break;

        case 3: // Combat & Cargo
            mvprintw(y++, 2, "Torpedoes: %d (Cargo: %d)", st->shm_torpedoes, st->shm_cargo_torpedoes);
            mvprintw(y++, 2, "Tube State: %d", st->shm_tube_state);
            mvprintw(y++, 2, "Current Tube: %d", st->current_tube);
            for(int i=0; i<4; i++) mvprintw(y++, 4, "Tube %d Timer: %d", i, st->tube_load_timers[i]);
            y++;
            mvprintw(y++, 2, "Cargo Energy: %" PRIu64, st->shm_cargo_energy);
            mvprintw(y++, 2, "Ion Beam Charge: %.2f", st->shm_ion_beam_charge);
            mvprintw(y++, 2, "Lock Target ID: %d", st->shm_lock_target);
            mvprintw(y++, 2, "Red Alert: %d", st->shm_red_alert);
            y++;
            mvprintw(y++, 2, "Inventory:");
            for(int i=0; i<10; i++) mvprintw(y++, 4, "[%d]: %d", i, st->inventory[i]);
            break;

        case 4: // Objects (Raw)
            mvprintw(y++, 2, "Object Count: %d", st->object_count);
            mvprintw(y++, 2, "idx | ID   | Name           | Type | Faction | X       | Y       | Z       | Act | Clk");
            mvprintw(y++, 2, "----|------|----------------|------|---------|---------|---------|---------|-----|----");
            for(int i=0; i < st->object_count && i < 15; i++) {
                 SharedObject *o = &st->objects[i];
                 mvprintw(y++, 2, "%3d | %4d | %-14.14s | %4d | %7d | %7.1f | %7.1f | %7.1f | %3d | %3d", 
                    i, o->id, o->shm_name, o->type, o->faction, o->shm_x, o->shm_y, o->shm_z, o->active, o->is_cloaked);
            }
            break;

        case 5: // Transient Events (IPC Queue)
            {
                int head = atomic_load(&shm_ptr->event_head);
                int tail = atomic_load(&shm_ptr->event_tail);
                mvprintw(y++, 2, "IPC Event Queue: Head=%d Tail=%d", head, tail);
                mvprintw(y++, 2, "idx | Type | X1      | Y1      | Z1      | X2      | Y2      | Z2      | Extra");
                mvprintw(y++, 2, "----|------|---------|---------|---------|---------|---------|---------|-------");
                
                /* Show the latest 15 events starting from tail going backwards */
                for (int i = 0; i < 15; i++) {
                    int idx = (tail - 1 - i + IPC_EVENT_QUEUE_SIZE) % IPC_EVENT_QUEUE_SIZE;
                    IPCEvent *ev = &shm_ptr->event_queue[idx];
                    const char* type_n = (ev->type==1?"BEAM":(ev->type==2?"BOOM":(ev->type==5?"JUMP":(ev->type==6?"TORP":"UNK"))));
                    mvprintw(y++, 2, "%3d | %-4s | %7.1f | %7.1f | %7.1f | %7.1f | %7.1f | %7.1f | %d", 
                        idx, type_n, ev->x1, ev->y1, ev->z1, ev->x2, ev->y2, ev->z2, ev->extra);
                    if (idx == head) break;
                }
            }
            break;
    }
    
    attron(COLOR_PAIR(2));
    mvprintw(30, 2, "INSPECTOR CONTROLS: [N] Next Page | [P] Prev Page | [M] Exit to HUD");
    attroff(COLOR_PAIR(2));
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    int fd = shm_open(argv[1], O_RDONLY, 0666);
    if (fd == -1) return 1;
    SharedIPC *shm = mmap(NULL, sizeof(SharedIPC), PROT_READ, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) return 1;

    initscr(); start_color(); curs_set(0); noecho(); nodelay(stdscr, TRUE); keypad(stdscr, TRUE);
    
    /* Define faithful Amber (Red: 1000, Green: 750, Blue: 0) if terminal allows */
    if (can_change_color()) {
        init_color(COLOR_AMBER, 1000, 750, 0);
    } else {
        COLOR_AMBER = COLOR_YELLOW; /* Fallback */
        hud_colors[2] = COLOR_YELLOW;
    }

    /* Fixed Status Colors (Not affected by Theme) */
    init_pair(8, COLOR_YELLOW, COLOR_BLACK); /* LOADING */
    init_pair(9, COLOR_GREEN, COLOR_BLACK);  /* READY */
    init_pair(10, COLOR_WHITE, COLOR_BLACK); /* OFFLINE */

    cycle_hud_colors();
    bkgd(COLOR_PAIR(2));

    int ch;
    int show_inspector = 0;
    int inspector_page = 0;

    while ((ch = getch()) != 'q') {
        if (atomic_load(&shm->force_shutdown)) {
            endwin();
            printf("[HUD] GLOBAL EMERGENCY SHUTDOWN SIGNAL RECEIVED. CLEAN EXIT.\n");
            return 0;
        }
        if (ch == 'c') {
            current_color_idx = (current_color_idx + 1) % 5;
            cycle_hud_colors();
        }
        if (ch == 'm') {
            show_inspector = !show_inspector;
            erase();
        }
        if (show_inspector) {
            if (ch == 'n') inspector_page = (inspector_page + 1) % 6;
            if (ch == 'p') inspector_page = (inspector_page - 1 + 6) % 6;
        }

        int r_idx = atomic_load(&shm->read_index);
        GameState *st = &shm->buffers[r_idx];

        if (st->shm_force_shutdown) {
            endwin();
            printf("[HUD] EMERGENCY SHUTDOWN SIGNAL RECEIVED (xxx). CLEAN EXIT.\n");
            return 0;
        }

        erase();

        if (show_inspector) {
             draw_shm_inspector(shm, st, inspector_page);
             refresh(); usleep(REFRESH_RATE_USEC);
             continue;
        }

        // --- TOP HEADER ---
        attron(COLOR_PAIR(2) | A_BOLD);
        mvprintw(0, 2, "CMDR: %-15s | FACTION: %-10s | SHIP: %-15s", st->objects[0].shm_name, get_faction_name(st->objects[0].faction), get_ship_class_full(st->objects[0].ship_class));
        mvprintw(0, 100, "UPTIME: %ld s", st->net_uptime);
        mvhline(1, 0, ACS_HLINE, 135);
        attroff(A_BOLD);

        // --- COL 1: NAVIGATION & VITALS ---
        attron(COLOR_PAIR(6) | A_BOLD); mvprintw(2, 2, "[ NAVIGATION ]"); attroff(A_BOLD);
        mvprintw(3, 2, "QUADRANT: [%d, %d, %d]", st->shm_q[0], st->shm_q[1], st->shm_q[2]);
        mvprintw(4, 2, "SECTOR  : [%.2f, %.2f, %.2f]", st->shm_s[0], st->shm_s[1], st->shm_s[2]);
        mvprintw(5, 2, "HEADING : %6.1f deg", st->shm_h);
        mvprintw(6, 2, "MARK    : %6.1f deg", st->shm_m);
        mvprintw(7, 2, "ROLL    : %6.1f deg", st->shm_r);
        
        if (st->shm_eta > 0.1) {
            int eta_col = (st->shm_eta < 5.0 && (st->frame_id % 10 < 5)) ? 7 : 6;
            attron(COLOR_PAIR(eta_col) | A_BOLD);
            mvprintw(8, 2, "ETA     : %6.1f s (TO DEST)", st->shm_eta);
            attroff(A_BOLD);
        } else {
            mvprintw(8, 2, "ETA     : ---");
        }
        mvprintw(9, 2, "STATE   : %s", get_nav_state_name(st->shm_nav_state));
        
        attron(COLOR_PAIR(6) | A_BOLD); mvprintw(10, 2, "[ SHIP VITALS ]"); attroff(A_BOLD);
        mvprintw(11, 2, "NRG: %-15" PRIu64, st->shm_energy);
        mvprintw(12, 2, "ENERGY :"); draw_bar(12, 11, 15, (double)st->shm_energy, (double)999999999999ULL, 2);
        mvprintw(13, 2, "HULL   :"); draw_bar(13, 11, 15, st->shm_hull_integrity, 100.0, (st->shm_hull_integrity > 50)?2:3);
        mvprintw(14, 2, "LIFE S :"); draw_bar(14, 11, 15, st->shm_life_support, 100.0, 1);
        mvprintw(15, 2, "CREW   : %-5d | PRISON: %-5d", st->shm_crew, st->shm_prison_unit);

        attron(COLOR_PAIR(6) | A_BOLD); mvprintw(17, 2, "[ SUBSYSTEMS ]"); attroff(A_BOLD);
        const char* sys_n[] = {"HDV", "IMP", "SRS", "LRS", "SEN", "TRP", "ION", "COM", "SHL", "LS "};
        for (int i=0; i<10; i++) {
            int col = (st->shm_system_health[i] > 75)?2:(st->shm_system_health[i] > 40?3:4);
            attron(COLOR_PAIR(col)); mvprintw(18+i, 2, "%s: [%5.1f%%]", sys_n[i], st->shm_system_health[i]); attroff(COLOR_PAIR(col));
        }

        // --- COL 2: DEFENSE, COMBAT & CARGO ---
        attron(COLOR_PAIR(2));
        mvvline(2, 35, ACS_VLINE, 26);
        attroff(COLOR_PAIR(2));
        attron(COLOR_PAIR(6) | A_BOLD); mvprintw(2, 37, "[ SHIELDS & POWER ]"); attroff(A_BOLD);
        mvprintw(3, 37, "F: %-5d", st->shm_shields[0]); draw_bar(3, 45, 10, st->shm_shields[0], 10000.0, 2);
        mvprintw(3, 56, "B: %-5d", st->shm_shields[1]); draw_bar(3, 64, 10, st->shm_shields[1], 10000.0, 2);
        
        mvprintw(4, 37, "L: %-5d", st->shm_shields[5]); draw_bar(4, 45, 10, st->shm_shields[5], 10000.0, 2);
        mvprintw(4, 56, "R: %-5d", st->shm_shields[4]); draw_bar(4, 56 + 8, 10, st->shm_shields[4], 10000.0, 2);

        mvprintw(5, 37, "UP:%-5d", st->shm_shields[2]); draw_bar(5, 45, 10, st->shm_shields[2], 10000.0, 2);
        mvprintw(5, 56, "DW:%-5d", st->shm_shields[3]); draw_bar(5, 64, 10, st->shm_shields[3], 10000.0, 2);
        
        mvprintw(8, 37, "PWR - ENG:"); draw_bar(8, 48, 15, st->shm_power_dist[0]*100, 100.0, 1);
        mvprintw(9, 37, "PWR - SHL:"); draw_bar(9, 48, 15, st->shm_power_dist[1]*100, 100.0, 2);
        mvprintw(10, 37, "PWR - WPN:"); draw_bar(10, 48, 15, st->shm_power_dist[2]*100, 100.0, 4);

        attron(COLOR_PAIR(6) | A_BOLD); mvprintw(12, 37, "[ TORPEDO TUBES ]"); attroff(A_BOLD);
        for(int t=0; t<4; t++) {
            const char* tube_str = "READY";
            int col = 9; /* Fixed Green */
            int timer = st->tube_load_timers[t];
            int eta = st->tube_torpedo_etas[t];
            bool is_current = (st->current_tube == t);

            if (st->shm_tube_state == 3) { tube_str = "OFFLINE"; col = 10; }
            else if (timer > 180) { tube_str = "FIRING "; col = 7; }
            else if (timer > 0) { tube_str = "LOADING"; col = 8; }
            else if (is_current && st->shm_tube_state == 2) { tube_str = "FIRING "; col = 7; }
            else { tube_str = "READY  "; col = 9; }

            char timing_buf[32] = " T:--- ";
            if (timer > 0) sprintf(timing_buf, " T:%4.1fs", (double)timer / 60.0);

            char eta_buf[32] = " ETA:---";
            if (eta > 0) sprintf(eta_buf, " ETA:%4.1fs", (double)eta / 60.0);

            attron(COLOR_PAIR(col));
            mvprintw(13+t, 37, "%sTUBE %d:[%-7s]%s%s", is_current ? ">" : " ", t+1, tube_str, timing_buf, eta_buf);
            attroff(COLOR_PAIR(col));
        }
        mvprintw(17, 37, "READY: %-3d | STIVA: %-3d", st->shm_torpedoes, st->shm_cargo_torpedoes);

        attron(COLOR_PAIR(6) | A_BOLD); mvprintw(19, 37, "[ CARGO INVENTORY ]"); attroff(A_BOLD);
        const char* res_n[] = {"None", "Aeth", "NeoT", "Void", "Grap", "Synp", "Gas ", "Comp", "Dark"};
        for(int r=1; r<9; r++) mvprintw(20+(r-1)/2, 37 + ((r-1)%2)*18, "%s: %-6d", res_n[r], st->inventory[r]);
        mvprintw(24, 37, "CARGO NRG: %-12" PRIu64, st->shm_cargo_energy);

        // --- COL 3: TACTICAL SCANNER ---
        attron(COLOR_PAIR(2));
        mvvline(2, 75, ACS_VLINE, 26);
        attroff(COLOR_PAIR(2));
        attron(COLOR_PAIR(6) | A_BOLD); mvprintw(2, 77, "[ TACTICAL INTELLIGENCE ]"); attroff(A_BOLD);
        int lock = st->shm_lock_target;
        if (lock > 0) {
            SharedObject *target = NULL;
            for(int o=0; o<st->object_count; o++) if(st->objects[o].id == lock) target = &st->objects[o];
            attron(COLOR_PAIR(7) | A_BOLD); mvprintw(3, 77, ">>> LOCKED ON: ID %-6d <<<", lock); attroff(A_BOLD);
            if (target) {
                mvprintw(4, 77, "NAME: %-15s | FRACT: %-10s", target->shm_name[0]?target->shm_name:"UNKNOWN", get_faction_name(target->faction));
                mvprintw(5, 77, "HULL: [%3d%%] | CLASS: %-15s", target->health_pct, get_ship_class_full(target->ship_class));
                double d = sqrt(pow(target->shm_x - st->shm_s[0], 2) + pow(target->shm_y - st->shm_s[1], 2) + pow(target->shm_z - st->shm_s[2], 2));
                mvprintw(6, 77, "DIST: %-6.1f | HEADING: %-6.1f", d, target->h);
                mvprintw(7, 77, "MARK: %-6.1f | ROLL: %-6.1f", target->m, target->r);
            }
        } else {
            mvprintw(3, 77, "SCANNER MODE: SEARCHING...");
            mvprintw(4, 77, "------------------------------------------");
        }
        
        attron(COLOR_PAIR(6)); mvprintw(9, 77, "ID     | NAME       | TYPE | FACTION    | DIST | HULL"); attroff(COLOR_PAIR(6));
        for (int o = 0; o < st->object_count && o < 16; o++) {
            SharedObject *obj = &st->objects[o];
            double d = sqrt(pow(obj->shm_x - st->shm_s[0], 2) + pow(obj->shm_y - st->shm_s[1], 2) + pow(obj->shm_z - st->shm_s[2], 2));
            int col = (obj->id == st->shm_lock_target)?7:(obj->type == 1?1:(obj->type >= 10?3:2));

            const char* type_n = (obj->type==1)?"SHIP":(obj->type==3?"BASE":(obj->type==4?"STAR":(obj->type==5?"PLAN":(obj->type==6?"BHOL":(obj->type==21?"ASTE":(obj->type==27?"TORP":"UNKN"))))));

            attron(COLOR_PAIR(col));
            mvprintw(10 + o, 77, "%6d | %-10.10s | %-4.4s | %-10.10s | %4.1f | %3d%%", obj->id, obj->shm_name[0]?obj->shm_name:"Alien", type_n, get_faction_name(obj->faction), d, obj->health_pct);
            attroff(COLOR_PAIR(col));
        }
        // --- BOTTOM DIAGNOSTICS (FOUR BOXES AT TERMINAL BOTTOM) ---
        int bot_y = 29;
        attron(COLOR_PAIR(2));
        mvhline(bot_y, 0, ACS_HLINE, 135);
        mvhline(bot_y + 9, 0, ACS_HLINE, 135);
        mvvline(bot_y + 1, 0, ACS_VLINE, 8); mvvline(bot_y + 1, 33, ACS_VLINE, 8); 
        mvvline(bot_y + 1, 67, ACS_VLINE, 8); mvvline(bot_y + 1, 101, ACS_VLINE, 8); mvvline(bot_y + 1, 134, ACS_VLINE, 8);
        attroff(COLOR_PAIR(2));
        
        // 1. DEEP SPACE UPLINK
        attron(COLOR_PAIR(1) | A_BOLD); mvprintw(bot_y + 1, 2, "[ DEEP SPACE UPLINK ]"); attroff(A_BOLD);
        mvprintw(bot_y + 2, 2, "BITRATE: %6.1f KBPS", st->net_kbps);
        mvprintw(bot_y + 3, 2, "LOSS   : %6.2f%%", 100.0 - st->net_integrity);
        mvprintw(bot_y + 4, 2, "PACKETS: %d", st->net_packet_count);
        mvprintw(bot_y + 5, 2, "JITTER : %6.3f ms", st->net_jitter);
        mvprintw(bot_y + 6, 2, "LINK   : %s", (st->net_integrity > 90)?"NOMINAL":"STORM");

        // 2. QUANTUM & LOGIC ANALYTICS
        attron(COLOR_PAIR(5) | A_BOLD); mvprintw(bot_y + 1, 35, "[ QUANTUM & LOGIC ]"); attroff(A_BOLD);
        mvprintw(bot_y + 2, 35, "FRAME ID : %-10lld", st->frame_id);
        const char *link_mode = "FLEET (A)";
        if (st->shm_radio_lock_target > 0) link_mode = "EXCLUSIVE (D)";
        else if (st->shm_encryption_flags & 0x04) link_mode = "PEER (C)";
        else if (st->shm_encryption_flags & 0x02) link_mode = "IDENTITY (B)";
        else if (st->shm_crypto_algo == 0) link_mode = "RAW / OPEN";

        mvprintw(bot_y + 3, 35, "ENC ALGO : %-10s", get_crypto_name(st->shm_crypto_algo));
        mvprintw(bot_y + 4, 35, "SIG AUTH : %-10s", (st->shm_encryption_flags & 0x01)?"VERIFIED":"NONE");
        mvprintw(bot_y + 5, 35, "LINK MODE: %-13s", link_mode);
        mvprintw(bot_y + 6, 35, "INTEGRITY: %6.1f%%", st->net_integrity);

        // 3. SPATIAL AWARENESS
        attron(COLOR_PAIR(2) | A_BOLD); mvprintw(bot_y + 1, 69, "[ SPATIAL AWARENESS ]"); attroff(A_BOLD);
        mvprintw(bot_y + 2, 69, "QUADRANT: [%d,%d,%d]", st->shm_q[0], st->shm_q[1], st->shm_q[2]);
        mvprintw(bot_y + 3, 69, "SECTOR  : %.1f,%.1f,%.1f", st->shm_s[0], st->shm_s[1], st->shm_s[2]);
        mvprintw(bot_y + 4, 69, "CONTACTS: %d ACTIVE", st->object_count);
        mvprintw(bot_y + 5, 69, "ION CHRG: %3.0f%%", st->shm_ion_beam_charge);
        mvprintw(bot_y + 6, 69, "ENVIRONMENT: %s", (st->shm_is_jammed)?"JAMMED":"CLEAR");

        // 4. SUBSPACE PROBES STATUS
        attron(COLOR_PAIR(1) | A_BOLD); mvprintw(bot_y + 1, 103, "[ SUBSPACE PROBES ]"); attroff(A_BOLD);
        for(int p=0; p<3; p++) {
            const char* p_st = (st->probes[p].status==0)?"FLYING":(st->probes[p].status==1?"SYNC":"TRANS");
            if (st->probes[p].active) {
                mvprintw(bot_y + 2 + p*2, 103, "PROBE %d: %-8s", p+1, p_st);
                mvprintw(bot_y + 3 + p*2, 103, "  ETA: %5.1f s | Q:[%d]", st->probes[p].eta, st->probes[p].q1);
            } else {
                mvprintw(bot_y + 2 + p*2, 103, "PROBE %d: OFFLINE", p+1);
            }
        }

        // --- ALERTS OVERLAY ---
        if (st->shm_red_alert) { attron(COLOR_PAIR(7) | A_BOLD | A_BLINK); mvprintw(bot_y - 2, 55, ">>> RED ALERT <<<"); attroff(A_BOLD | A_BLINK); }
        if (st->is_cloaked) { attron(COLOR_PAIR(1) | A_BOLD); mvprintw(bot_y - 2, 2, "CLOAKING: ACTIVE"); attroff(A_BOLD); }

        attron(COLOR_PAIR(2));
        mvprintw(bot_y + 10, 2, "CONTROLS: [ARROWS] View | [PGUP/PGDN] Zoom | [SPACE] Rotate | [C] Cycle Colors | [M] SHM Inspector | [Q] Quit");
        attroff(COLOR_PAIR(2));

        refresh(); usleep(REFRESH_RATE_USEC);
    }
    endwin(); return 0;
}
