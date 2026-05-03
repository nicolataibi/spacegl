/*
 * SPACE GL - TACTICAL TELEMETRY CLIENT
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <ncurses.h>
#include <errno.h>
#include <fcntl.h>
#include "telemetry.h"

TelemetryObject tel_objects[MAX_VISIBLE_TEL];
int tel_obj_count = 0;
TelemetryStats tel_stats;

const char* cat_names[] = {
    "SHIPS ALLIANCE", "SHIPS KORTHIAN", "SHIPS XYLARI", "SHIPS SWARM", 
    "SHIPS VESPERIAN", "SHIPS ASCENDANT", "SHIPS QUARZITE", "SHIPS SAURIAN",
    "SHIPS GILDED", "SHIPS FLUIDIC", "SHIPS CRYOS", "SHIPS APEX",
    "STARS", "PLANETS", "BASES", "BLACK HOLES", "NEBULAS", "PULSARS",
    "QUASARS", "COMETS", "ASTEROIDS", "DERELICTS", "MINES", "BUOYS", "PLATFORMS", "RIFTS",
    "MONSTERS", "DYSON", "HUBS", "RELICS", "RUPTURES", "SATELLITES", "STORMS", "TORPEDOES",
    "ARTIFACTS", "WARP GATES", "NEUTRON STARS", "MEGA STRUCTS", "DARK CLOUDS", "SINGULARITIES",
    "PLASMA STORMS", "ORBITAL RINGS", "TIME ANOMALIES", "VOID CRYSTALS", "ANOMALIES"
};

int main(int argc, char** argv) {
    int sock = -1;
    bool use_tcp = false;
    char* host = "127.0.0.1";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: spacegl_telemetry [OPTIONS]\n\n");
            printf("Space GL Tactical Telemetry Client - High-performance real-time monitoring.\n\n");
            printf("Options:\n");
            printf("  --tcp [HOST]      Connect via TCP to the specified host (default: 127.0.0.1).\n");
            printf("                    Uses port %d.\n", TELEMETRY_DEFAULT_PORT);
            printf("  -h, --help        Display this help message and exit.\n\n");
            printf("Controls:\n");
            printf("  [N]               Next category\n");
            printf("  [P]               Previous category\n");
            printf("  [UP/DOWN]         Scroll list\n");
            printf("  [Q]               Quit\n");
            return 0;
        }
    }

    if (argc > 1 && strcmp(argv[1], "--tcp") == 0) {
        use_tcp = true;
        if (argc > 2) host = argv[2];
    }

    if (use_tcp) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(TELEMETRY_DEFAULT_PORT);
        inet_pton(AF_INET, host, &addr.sin_addr);
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("TCP Connect failed"); return 1;
        }
    } else {
        sock = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr = {0};
        addr.sun_family = AF_UNIX;
        strcpy(addr.sun_path, TELEMETRY_UNIX_PATH);
        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("Unix Connect failed"); return 1;
        }
    }

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    initscr(); start_color(); curs_set(0); noecho(); nodelay(stdscr, TRUE); keypad(stdscr, TRUE);
    
    init_pair(1, COLOR_GREEN,   COLOR_BLACK);
    init_pair(2, COLOR_RED,     COLOR_BLACK);
    init_pair(3, COLOR_CYAN,    COLOR_BLACK);
    init_pair(4, COLOR_YELLOW,  COLOR_BLACK);
    init_pair(5, COLOR_BLUE,    COLOR_BLACK);
    init_pair(6, COLOR_MAGENTA, COLOR_BLACK);
    
    if (can_change_color()) {
        init_color(16, 1000, 500, 0); init_color(17, 500, 1000, 0); init_color(18, 1000, 843, 0);
        init_color(19, 600, 0, 1000); init_color(20, 0, 750, 1000); init_color(21, 500, 500, 500);
        init_pair(7, 16, COLOR_BLACK); init_pair(8, 17, COLOR_BLACK); init_pair(9, 18, COLOR_BLACK);
        init_pair(10, 19, COLOR_BLACK); init_pair(11, 20, COLOR_BLACK); init_pair(12, 21, COLOR_BLACK);
    } else {
        for(int i=7; i<=12; i++) init_pair(i, (i%6)+1, COLOR_BLACK);
    }
    init_pair(13, COLOR_WHITE, COLOR_BLACK);

    int current_cat = TEL_CAT_SHIP_ALLIANCE;
    int scroll_offset = 0;
    int ch;
    bool subscription_pending = true;

    while ((ch = getch()) != 'q') {
        if (ch == 'n') { 
            current_cat = (current_cat + 1) % TEL_CAT_COUNT; 
            scroll_offset = 0; tel_obj_count = 0;
            subscription_pending = true;
        }
        if (ch == 'p') { 
            current_cat = (current_cat - 1 + TEL_CAT_COUNT) % TEL_CAT_COUNT; 
            scroll_offset = 0; tel_obj_count = 0;
            subscription_pending = true;
        }
        
        if (subscription_pending) {
            TelemetryHeader h_sub = {TEL_PKT_SUBSCRIBE, sizeof(TelemetrySubscribe)};
            TelemetrySubscribe s_sub = {(uint32_t)current_cat};
            send(sock, &h_sub, sizeof(h_sub), 0);
            send(sock, &s_sub, sizeof(s_sub), 0);
            subscription_pending = false;
        }

        if (ch == KEY_DOWN) scroll_offset++;
        if (ch == KEY_UP && scroll_offset > 0) scroll_offset--;

        TelemetryHeader hdr;
        while (recv(sock, &hdr, sizeof(hdr), 0) == sizeof(hdr)) {
            if (hdr.type == TEL_PKT_DATA) {
                int count = hdr.length / sizeof(TelemetryObject);
                if (count > MAX_VISIBLE_TEL) count = MAX_VISIBLE_TEL;
                
                int total = 0;
                char* target = (char*)tel_objects;
                while(total < (int)hdr.length) {
                    int to_read = hdr.length - total;
                    int r = recv(sock, target + total, to_read, 0);
                    if (r <= 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                        break;
                    }
                    total += r;
                }
                tel_obj_count = count;
            } else if (hdr.type == TEL_PKT_STATS) {
                recv(sock, &tel_stats, sizeof(tel_stats), 0);
            } else {
                if (hdr.length > 0) {
                    char* dummy = malloc(hdr.length);
                    recv(sock, dummy, hdr.length, 0);
                    free(dummy);
                }
            }
        }

        erase();
        attron(COLOR_PAIR(2) | A_BOLD);
        mvprintw(1, 2, "SPACE GL REMOTE TELEMETRY | TICK: %u | CAT: %s | PL: %u | NPC: %u", 
                tel_stats.tick, cat_names[current_cat], tel_stats.active_players, tel_stats.active_npcs);
        mvhline(2, 0, ACS_HLINE, COLS); attroff(A_BOLD);

        attron(A_BOLD | COLOR_PAIR(13));
        mvprintw(4, 2, "%-6s | %-20s | %-15s | %-12s | %-12s | %-4s | %-12s", "ID", "NAME", "INFO", "QUADRANT", "SECTOR", "INTEG", "ENERGY/EXTRA");
        mvhline(5, 0, ACS_HLINE, COLS); attroff(A_BOLD);

        int y = 6;
        int max_y = LINES - 2;
        if (tel_obj_count == 0) {
            attron(COLOR_PAIR(4));
            mvprintw(y, COLS/2 - 15, "--- ACQUIRING DATA FOR %s ---", cat_names[current_cat]);
            attroff(COLOR_PAIR(4));
        } else {
            if (scroll_offset >= tel_obj_count) scroll_offset = (tel_obj_count > 0) ? tel_obj_count - 1 : 0;
            for (int i = 0; i < tel_obj_count - scroll_offset && y < max_y; i++) {
                TelemetryObject* o = &tel_objects[i + scroll_offset];
                attron(COLOR_PAIR(o->color_pair));
                mvprintw(y++, 2, "%-6s | %-20.20s | %-15.15s | [%2d,%2d,%2d] | %5.1f,%5.1f,%5.1f | %3u%% | %s", 
                        o->id, o->name, o->info, o->q1, o->q2, o->q3, o->x, o->y, o->z, o->integrity, 
                        (o->energy > 0) ? "ACTIVE" : o->extra);
                attroff(COLOR_PAIR(o->color_pair));
            }
        }

        mvprintw(LINES-1, 2, "[N/P] Category | [UP/DWN] Scroll | [Q] Quit | STREAM: %s", use_tcp ? "TCP" : "UNIX");
        refresh();
        usleep(16666);
    }

    endwin();
    close(sock);
    return 0;
}
