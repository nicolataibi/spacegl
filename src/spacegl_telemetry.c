/*
 * SPACE GL - TACTICAL TELEMETRY CLIENT
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <ncurses.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <errno.h>
#include "telemetry.h"

/* Helper to receive exactly N bytes from non-blocking socket with robust timeout */
static int recv_all(int sock, void* buf, size_t len) {
    if (len == 0) return 0;
    size_t received = 0;
    int timeouts = 0;
    const int max_timeouts = 200; // Increased to 1 second total timeout (200 * 5ms)

    while (received < len) {
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLIN;
        int res = poll(&pfd, 1, 5);
        if (res < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (res == 0) {
            if (++timeouts > max_timeouts) return -1;
            continue;
        }

        ssize_t r = recv(sock, (char*)buf + received, len - received, 0);
        if (r == 0) return 0; // EOF (Server disconnected)
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            if (errno == EINTR) continue;
            return -1;
        }
        received += r;
        timeouts = 0; // Reset timeouts on successful read
    }
    return (int)received;
}

/* Helper to discard N bytes from socket safely without large allocations */
static void discard_bytes(int sock, size_t len) {
    if (len == 0) return;
    char dummy[4096];
    size_t total = 0;
    while (total < len) {
        size_t to_read = (len - total > sizeof(dummy)) ? sizeof(dummy) : len - total;
        if (recv_all(sock, dummy, to_read) <= 0) break;
        total += to_read;
    }
}

/* Drain the socket completely to resynchronize stream */
static void flush_socket(int sock) {
    char dummy[4096];
    while (recv(sock, dummy, sizeof(dummy), 0) > 0);
}

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

typedef enum {
    MODE_HUD = 0,
    MODE_MENU
} TelemetryMode;

void draw_tel_menu(int selection) {
    attron(COLOR_PAIR(3) | A_BOLD);
    mvprintw(2, 2, "[ SPACE GL TELEMETRY - CATEGORY SELECTION ]");
    attroff(A_BOLD);
    
    mvprintw(4, 2, "Use ARROWS to select and ENTER to view. Press 'M' to return to HUD.");
    
    int start_y = 6;
    for (int i = 0; i < TEL_CAT_COUNT; i++) {
        int col = i / 23;
        int row = i % 23;
        int x_pos = 4 + (col * 40);
        
        if (i == selection) {
            attron(COLOR_PAIR(1) | A_REVERSE);
            mvprintw(start_y + row, x_pos, " > %-34.34s ", cat_names[i]);
            attroff(COLOR_PAIR(1) | A_REVERSE);
        } else {
            mvprintw(start_y + row, x_pos, "   %-34.34s ", cat_names[i]);
        }
    }
    
    attron(COLOR_PAIR(3));
    mvprintw(30, 2, "[UP/DOWN/LEFT/RIGHT] Select | [ENTER] Open | [M] Exit to HUD | [Q] Quit");
    attroff(COLOR_PAIR(3));
}

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
            printf("  [M] or [ESC]      Category Menu (Initial Screen)\n");
            printf("  [UP/DOWN]         Scroll list / Select in menu\n");
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

    int rcvbuf = 4 * 1024 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

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

    TelemetryMode mode = MODE_MENU;
    int current_cat = TEL_CAT_SHIP_ALLIANCE;
    int menu_selection = current_cat;
    int last_cat = current_cat;
    int scroll_offset = 0;
    int ch;
    bool subscription_pending = true;
    bool new_frame = true;
    int pending_obj_count = 0;

    while (1) {
        /* Robust Input Handling: Discard redundant keys if held down */
        int first_ch = getch();
        if (first_ch == 'q') break;
        
        int next_ch;
        ch = first_ch;
        if (ch != ERR) {
            while ((next_ch = getch()) != ERR) {
                if (next_ch == 'q') { ch = 'q'; break; }
                ch = next_ch; /* Keep only the latest key in this frame */
            }
        }
        if (ch == 'q') break;

        bool cat_changed = false;
        
        if (ch == 'm') {
            mode = (mode == MODE_HUD) ? MODE_MENU : MODE_HUD;
            menu_selection = current_cat;
            erase();
        }

        if (mode == MODE_MENU) {
            if (ch == KEY_UP && menu_selection > 0) menu_selection--;
            if (ch == KEY_DOWN && menu_selection < TEL_CAT_COUNT - 1) menu_selection++;
            if (ch == KEY_LEFT && menu_selection >= 23) menu_selection -= 23;
            if (ch == KEY_RIGHT && menu_selection + 23 < TEL_CAT_COUNT) menu_selection += 23;
            if (ch == 10 || ch == 13 || ch == KEY_ENTER) {
                current_cat = menu_selection;
                cat_changed = true;
                mode = MODE_HUD;
                erase();
            }
            if (ch == 27) { mode = MODE_HUD; erase(); }
        } else {
            if (ch == KEY_DOWN) scroll_offset += 5;
            if (ch == KEY_UP && scroll_offset > 0) {
                scroll_offset -= 5;
                if (scroll_offset < 0) scroll_offset = 0;
            }
            if (ch == 27) { mode = MODE_MENU; erase(); }
        }


        if (cat_changed && current_cat != last_cat) {
            flush_socket(sock); /* Clear old data from socket */
            scroll_offset = 0; 
            tel_obj_count = 0;
            pending_obj_count = 0; 
            new_frame = true;
            memset(tel_objects, 0, sizeof(tel_objects));
            subscription_pending = true;
            last_cat = current_cat;
        }

        if (subscription_pending) {
            TelemetryHeader h_sub = {TEL_PKT_SUBSCRIBE, sizeof(TelemetrySubscribe)};
            TelemetrySubscribe s_sub = {(uint32_t)current_cat};
            send(sock, &h_sub, sizeof(h_sub), 0);
            send(sock, &s_sub, sizeof(s_sub), 0);
            subscription_pending = false;
        }

        /* Drain the socket buffer in each frame to keep up with server speed */
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLIN;

        int pkts_processed = 0;
        /* Process up to 500 packets per frame to avoid falling behind the server */
        while (pkts_processed++ < 500 && poll(&pfd, 1, 0) > 0) {
            TelemetryHeader hdr;
            int r = recv_all(sock, &hdr, sizeof(hdr));
            if (r == 0) { endwin(); printf("Server disconnected.\n"); close(sock); return 0; }
            if (r < 0) {
                /* If read fails, try to flush and continue instead of exiting if possible, 
                   or exit if it's a permanent error */
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
                endwin(); perror("Read error"); close(sock); return 1; 
            }

            /* Validate packet type to check for stream corruption */
            if (hdr.type < TEL_PKT_SUBSCRIBE || hdr.type > TEL_PKT_STATS) {
                /* On corruption, flush the socket and wait for next clean burst */
                flush_socket(sock);
                new_frame = true;
                pending_obj_count = 0;
                break;
            }

            /* Detect suspiciously large packets (e.g. > 1MB) which suggest desync */
            if (hdr.length > 1024 * 1024) {
                discard_bytes(sock, hdr.length);
                continue;
            }

            if (hdr.type == TEL_PKT_DATA) {
                if (new_frame) {
                    pending_obj_count = 0;
                    new_frame = false;
                }

                /* Safety: only process if length is a multiple of the struct size */
                if (hdr.length > 0 && hdr.length % sizeof(TelemetryObject) != 0) {
                    discard_bytes(sock, hdr.length);
                    continue;
                }

                int count = hdr.length / sizeof(TelemetryObject);
                int can_fit = MAX_VISIBLE_TEL - pending_obj_count;
                
                if (count > 0) {
                    if (count <= can_fit) {
                        if (recv_all(sock, &tel_objects[pending_obj_count], hdr.length) != (int)hdr.length) {
                            endwin(); printf("Stream synchronization lost.\n"); close(sock); return 1;
                        }
                        pending_obj_count += count;
                    } else {
                        /* Read what fits and discard the rest */
                        if (can_fit > 0) {
                            size_t fit_size = can_fit * sizeof(TelemetryObject);
                            if (recv_all(sock, &tel_objects[pending_obj_count], fit_size) != (int)fit_size) {
                                endwin(); printf("Stream synchronization lost.\n"); close(sock); return 1;
                            }
                            pending_obj_count += can_fit;
                        }
                        discard_bytes(sock, hdr.length - (can_fit > 0 ? can_fit * sizeof(TelemetryObject) : 0));
                    }
                }
            } else if (hdr.type == TEL_PKT_STATS) {
                /* Safety: only process if length matches expected stats size */
                if (hdr.length == sizeof(TelemetryStats)) {
                    if (recv_all(sock, &tel_stats, sizeof(tel_stats)) == (int)sizeof(tel_stats)) {
                        tel_obj_count = pending_obj_count;
                        pending_obj_count = 0; /* Reset for next frame */
                        new_frame = true;
                    }
                } else {
                    discard_bytes(sock, hdr.length);
                }
                /* Frame complete, exit loop to allow rendering */
                break; 
            } else if (hdr.length > 0) {
                discard_bytes(sock, hdr.length);
            }
        }

        /* Rate limiting for display - increase refresh rate to 60Hz (16ms) */
        static long last_refresh_time = 0;
        const int refresh_interval_ms = 16;

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        long current_time = (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);

        if (current_time - last_refresh_time >= refresh_interval_ms) {
            erase();
            if (mode == MODE_MENU) {
                draw_tel_menu(menu_selection);
                refresh();
                last_refresh_time = current_time;
                continue;
            }
            attron(COLOR_PAIR(2) | A_BOLD);
            const char* display_cat = (current_cat >= 0 && current_cat < TEL_CAT_COUNT) ? cat_names[current_cat] : "UNKNOWN";
            mvprintw(1, 2, "SPACE GL REMOTE TELEMETRY | TICK: %u | CAT: %s | PL: %u | NPC: %u", 
                    tel_stats.tick, display_cat, tel_stats.active_players, tel_stats.active_npcs);
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
                    int color = (o->color_pair >= 1 && o->color_pair <= 13) ? o->color_pair : 13;
                    
                    /* Create safe null-terminated copies for display */
                    char safe_id[17] = {0};   memcpy(safe_id, o->id, 16);
                    char safe_name[33] = {0}; memcpy(safe_name, o->name, 32);
                    char safe_info[33] = {0}; memcpy(safe_info, o->info, 32);
                    char safe_extra[33] = {0}; memcpy(safe_extra, o->extra, 32);

                    attron(COLOR_PAIR(color));
                    mvprintw(y++, 2, "%-6.6s | %-20.20s | %-15.15s | [%2d,%2d,%2d] | %5.1f,%5.1f,%5.1f | %3u%% | %s", 
                            safe_id, safe_name, safe_info, o->q1, o->q2, o->q3, o->x, o->y, o->z, o->integrity, 
                            (o->energy > 0) ? "ACTIVE" : safe_extra);
                    attroff(COLOR_PAIR(color));
                }
            }
            mvprintw(LINES-1, 2, "[M/ESC] Menu | [UP/DWN] Scroll | [Q] Quit | STREAM: %s", use_tcp ? "TCP" : "UNIX");
            refresh();
            last_refresh_time = current_time;
        }
        usleep(5000); // reduced sleep for higher responsiveness
    }

    endwin();
    close(sock);
    return 0;
}
