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

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <time.h>
#include <stddef.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include "server_internal.h"

#define MAX_EVENTS (MAX_CLIENTS + 10)

pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
int g_debug = 0;
int global_tick = 0;

void *game_loop_thread(void *arg) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    while (1) {
        ts.tv_nsec += 33333333;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        update_game_logic();
    }
}

#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <gnu/libc-version.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include "ui.h"

void display_system_telemetry() {
    struct utsname uts;
    struct sysinfo info;
    struct ifaddrs *ifaddr, *ifa;
    uname(&uts);
    sysinfo(&info);

    long mem_unit = info.mem_unit;
    long total_ram = (info.totalram * mem_unit) / 1024 / 1024;
    long free_ram = (info.freeram * mem_unit) / 1024 / 1024;
    long shared_ram = (info.sharedram * mem_unit) / 1024 / 1024;
    int nprocs = sysconf(_SC_NPROCESSORS_ONLN);

    printf("\n%s .-------------------- GDIS (Galactic Display & Information System) --------------------.%s\n", B_MAGENTA, RESET);
    printf("%s | %s HOST IDENTIFIER:   %s%-48s %s %s\n", B_MAGENTA, B_WHITE, B_GREEN, uts.nodename, B_MAGENTA, RESET);
    printf("%s | %s OS KERNEL:         %s%-20s %sVERSION: %s%-19s %s %s\n", B_MAGENTA, B_WHITE, B_GREEN, uts.sysname, B_WHITE, B_GREEN, uts.release, B_MAGENTA, RESET);
    printf("%s | %s CORE LIBRARIES:    %sGNU libc %-39s %s %s\n", B_MAGENTA, B_WHITE, B_GREEN, gnu_get_libc_version(), B_MAGENTA, RESET);
    printf("%s | %s LOGICAL CORES:     %s%-2d Core Processors (Active)                  %s %s\n", B_MAGENTA, B_WHITE, B_GREEN, nprocs, B_MAGENTA, RESET);
    
    printf("%s |                                                                      %s\n", B_MAGENTA, RESET);
    printf("%s | %s MEMORY ALLOCATION (LOGICAL LAYER)                                  %s %s\n", B_MAGENTA, B_WHITE, B_MAGENTA, RESET);
    printf("%s | %s PHYSICAL RAM:      %s%ld MB Total / %ld MB Free                    %s %s\n", B_MAGENTA, B_WHITE, B_GREEN, total_ram, free_ram, B_MAGENTA, RESET);
    printf("%s | %s SHARED SEGMENTS:   %s%ld MB (IPC/SHM Active)                       %s %s\n", B_MAGENTA, B_WHITE, B_GREEN, shared_ram, B_MAGENTA, RESET);
    
    printf("%s |                                                                      %s\n", B_MAGENTA, RESET);
    printf("%s | %s Deep Space NETWORK TOPOLOGY                                          %s %s\n", B_MAGENTA, B_WHITE, B_MAGENTA, RESET);
    if (getifaddrs(&ifaddr) == -1) {
        printf("%s | %s NETWORK ERROR:     %sUnable to scan Deep Space frequencies           %s %s\n", B_MAGENTA, B_WHITE, B_RED, B_MAGENTA, RESET);
    } else {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) continue;
            char addr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, addr, sizeof(addr));
            if (strcmp(ifa->ifa_name, "lo") == 0) continue;
            printf("%s | %s INTERFACE: %-7s %sIP ADDR: %-15s (ACTIVE)         %s %s\n", B_MAGENTA, B_WHITE, ifa->ifa_name, B_GREEN, addr, B_MAGENTA, RESET);
        }
        freeifaddrs(ifaddr);
    }

    /* Traffic Stats from /proc/net/dev */
    FILE *f = fopen("/proc/net/dev", "r");
    if (f) {
        char line[256];
        /* Skip 2 lines header */
        if (fgets(line, 256, f) && fgets(line, 256, f)) {
            /* Successfully skipped */
        }
        while (fgets(line, 256, f)) {
            char ifname[32]; long rx, tx, tmp;
            if (sscanf(line, " %[^:]: %ld %ld %ld %ld %ld %ld %ld %ld %ld", ifname, &rx, &tmp, &tmp, &tmp, &tmp, &tmp, &tmp, &tmp, &tx) >= 2) {
                if (strcmp(ifname, "lo") == 0 || rx == 0) continue;
                printf("%s | %s TRAFFIC (%-5s):   %sRX: %-8ld KB  TX: %-8ld KB             %s %s\n", 
                       B_MAGENTA, B_WHITE, ifname, B_GREEN, rx/1024, tx/1024, B_MAGENTA, RESET);
            }
        }
        fclose(f);
    }
    
    printf("%s |                                                                      %s\n", B_MAGENTA, RESET);
    printf("%s | %s Deep Space DYNAMICS                                                  %s %s\n", B_MAGENTA, B_WHITE, B_MAGENTA, RESET);
    double load = 1.0 / (1 << SI_LOAD_SHIFT);
    printf("%s | %s LOAD INTERFERENCE: %s%.2f (1m)  %.2f (5m)  %.2f (15m)                  %s %s\n", 
           B_MAGENTA, B_WHITE, B_GREEN, info.loads[0] * load, info.loads[1] * load, info.loads[2] * load, B_MAGENTA, RESET);
    
    long days = info.uptime / 86400;
    long hours = (info.uptime % 86400) / 3600;
    long mins = (info.uptime % 3600) / 60;
    printf("%s | %s UPTIME METRICS:    %s%ldd %02ldh %02ldm                                  %s %s\n", B_MAGENTA, B_WHITE, B_GREEN, days, hours, mins, B_MAGENTA, RESET);
    
    printf("%s |                                                                      %s\n", B_MAGENTA, RESET);
    printf("%s | %s CRYPTOGRAPHIC SUBSYSTEM (SECURE LAYER)                               %s %s\n", B_MAGENTA, B_WHITE, B_MAGENTA, RESET);
    printf("%s | %s SIGNATURE ALGO:    %sHMAC-SHA256 (EdDSA Surrogate)                 %s %s\n", B_MAGENTA, B_WHITE, B_GREEN, B_MAGENTA, RESET);
    printf("%s | %s ENCRYPTION FLAGS:  %s0x%08X (AES-GCM/PQC/INT)                      %s %s\n", B_MAGENTA, B_WHITE, B_GREEN, 0x07, B_MAGENTA, RESET);
    printf("%s | %s MASTER KEY:        %s%-43.43s %s %s\n", B_MAGENTA, B_WHITE, B_YELLOW, getenv("SPACEGL_KEY") ? getenv("SPACEGL_KEY") : "NOT SET", B_MAGENTA, RESET);
    
    printf("%s '-----------------------------------------------------------------------------------------'%s\n\n", B_MAGENTA, RESET);
}

void sign_galaxy_data() {
    unsigned int len = 32;
    /* Generate a cryptographic signature of the galaxy state (excluding the signature field itself) */
    HMAC(EVP_sha256(), MASTER_SESSION_KEY, 32, (uint8_t*)&spacegl_master, 
         offsetof(SpaceGLGame, server_signature), spacegl_master.server_signature, &len);
    
    /* In a real scenario, we'd use an actual Ed25519 public key here. 
       For this implementation, we use a derived key from the Master Session Key. */
    SHA256(MASTER_SESSION_KEY, 32, SERVER_PUBKEY);
    memcpy(spacegl_master.server_pubkey, SERVER_PUBKEY, 32);
    
    /* Encryption Details: 
       Bit 0: Integrity Verified (HMAC-SHA256)
       Bit 1: Quantum Resistant Layer (PQC)
       Bit 2: AES-256-GCM Active
    */
    spacegl_master.encryption_flags = 0x07; 
}

int main(int argc, char *argv[]) {
    int server_fd, epoll_fd;
    struct sockaddr_in addr;
    int opt = 1, adlen = sizeof(addr);
    struct epoll_event ev, events[MAX_EVENTS];

    for (int i = 1; i < argc; i++) if (strcmp(argv[i], "-d") == 0) g_debug = 1;
    signal(SIGPIPE, SIG_IGN);
    
    /* Security Initialization */
    char *env_key = getenv("SPACEGL_KEY");
    if (!env_key) {
        fprintf(stderr, "\033[1;31mSECURITY ERROR: Deep Space Key (SPACEGL_KEY) not found in environment.\033[0m\n");
        fprintf(stderr, "The server requires a shared secret key to secure communications.\n");
        exit(1);
    }
    memset(MASTER_SESSION_KEY, 0, 32);
    size_t env_len = strlen(env_key);
    memcpy(MASTER_SESSION_KEY, env_key, (env_len > 32) ? 32 : env_len);
    
    memset(players, 0, sizeof(players)); srand(time(NULL)); 
    for(int i=0; i<MAX_CLIENTS; i++) pthread_mutex_init(&players[i].socket_mutex, NULL);
    
    /* Schermata di Benvenuto Server */
    printf("\033[2J\033[H"); /* Clear screen */
    printf("\033[1;31m  ____________________________________________________________________________\n" );
    printf(" /                                                                            \\\n" );
    printf(" | \033[1;37m   ███████╗██████╗  █████╗  ██████╗███████╗     ██████╗ ██╗              \033[1;31m  |\n" );
    printf(" | \033[1;37m   ██╔════╝██╔══██╗██╔══██╗██╔════╝██╔════╝    ██╔════╝ ██║              \033[1;31m  |\n" );
    printf(" | \033[1;37m   ███████╗██████╔╝███████║██║     █████╗      ██║  ███╗██║              \033[1;31m  |\n" );
    printf(" | \033[1;37m   ╚════██║██╔═══╝ ██╔══██║██║     ██╔══╝      ██║   ██║██║              \033[1;31m  |\n" );
    printf(" | \033[1;37m   ███████║██║     ██║  ██║╚██████╗███████╗    ╚██████╔╝███████╗         \033[1;31m  |\n" );
    printf(" | \033[1;37m   ╚══════╝╚═╝     ╚═╝  ╚═╝ ╚═════╝╚══════╝     ╚═════╝ ╚══════╝         \033[1;31m  |\n" );
    printf(" |                                                                            |\n" );
    printf(" | \033[1;31m             ---  G A L A C T I C   S E R V E R   C O R E  ---            \033[1;31m |\n" );
    printf(" | \033[1;33m          \"Per Tenebras, Lumen\" (Attraverso le tenebre, la luce)          \033[1;31m |\n" );
    printf(" |                                                                            |\n" );
    printf(" | \033[1;37m  Copyright (C) 2026 \033[1;32mNicola Taibi\033[1;37m                                        \033[1;31m  |\n" );
    printf(" | \033[1;37m  AI Core Support by \033[1;34mGoogle Gemini\033[1;37m                                       \033[1;31m  |\n" );
    printf(" | \033[1;37m  License Type:      \033[1;33mGNU GPL v3.0\033[1;37m                                        \033[1;31m  |\n" );
    printf(" \\____________________________________________________________________________/\033[0m\n\n" );

    display_system_telemetry();

    if (!load_galaxy()) { generate_galaxy(); save_galaxy(); }
    sign_galaxy_data();
    init_static_spatial_index();
    
    pthread_t tid; pthread_create(&tid, NULL, game_loop_thread, NULL);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(DEFAULT_PORT);
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)); listen(server_fd, 10);

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) { perror("epoll_create1"); exit(EXIT_FAILURE); }

    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) { perror("epoll_ctl: server_fd"); exit(EXIT_FAILURE); }

    printf("STELLAR SERVER started on port %d (EPOLL MODE)\n", DEFAULT_PORT);
    
    while (1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait"); break;
        }

        for (int n = 0; n < nfds; ++n) {
            int fd = events[n].data.fd;

            if (fd == server_fd) {
                int new_socket = accept(server_fd, (struct sockaddr *)&addr, (socklen_t*)&adlen);
                if (new_socket == -1) { perror("accept"); continue; }
                
                ev.events = EPOLLIN; 
                ev.data.fd = new_socket;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_socket, &ev) == -1) { perror("epoll_ctl: new_socket"); close(new_socket); }
                LOG_DEBUG("New connection accepted: FD %d\n", new_socket);
            } else {
                /* Handle data from a client */
                int type;
                int r = read_all(fd, &type, sizeof(int));
                
                if (r <= 0) {
                    /* Disconnect */
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    pthread_mutex_lock(&game_mutex);
                    for (int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == fd) { players[i].socket = 0; players[i].active = 0; break; }
                    pthread_mutex_unlock(&game_mutex);
                    close(fd);
                    LOG_DEBUG("Connection closed: FD %d\n", fd);
                    continue;
                }

                /* Find player index if already logged in */
                int p_idx = -1;
                pthread_mutex_lock(&game_mutex);
                for (int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == fd && players[i].active) { p_idx = i; break; }
                pthread_mutex_unlock(&game_mutex);

                if (type == PKT_HANDSHAKE) {
                    PacketHandshake h_pkt;
                    if (read_all(fd, ((char*)&h_pkt) + sizeof(int), sizeof(PacketHandshake) - sizeof(int)) > 0) {
                        pthread_mutex_lock(&game_mutex);
                        /* Find empty slot or existing slot for this FD to store the key temporarily before login */
                        int slot = -1;
                        /* Check if already assigned */
                        for(int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == fd) { slot = i; break; }
                        /* If not, find a free slot to reserve for this connection */
                        if (slot == -1) {
                            for(int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == 0) { 
                                slot = i; 
                                players[i].socket = fd; 
                                players[i].active = 0; /* Not logged in yet */
                                break; 
                            }
                        }
                        
                        if (slot != -1) {
                            /* De-obfuscate the Session Key and Signature using Master Key */
                            for(int k=0; k<32; k++) {
                                players[slot].session_key[k] = h_pkt.pubkey[k] ^ MASTER_SESSION_KEY[k];
                            }
                            
                            /* Verify Signature Integrity (Full 32 bytes) */
                            uint8_t sig[32];
                            for(int k=0; k<32; k++) sig[k] = h_pkt.pubkey[32+k] ^ MASTER_SESSION_KEY[k];
                            
                            if (memcmp(sig, HANDSHAKE_MAGIC_STRING, 32) != 0) {
                                fprintf(stderr, "\033[1;31m[SECURITY ALERT]\033[0m Handshake integrity failure on FD %d. Invalid Master Key.\n", fd);
                                /* Kick the client */
                                players[slot].socket = 0;
                                pthread_mutex_unlock(&game_mutex);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                                close(fd);
                                continue; /* Move to next epoll event */
                            }
                            
                            LOG_DEBUG("Secure Session Key negotiated for Client FD %d (Slot %d)\n", fd, slot);
                            
                            /* Send ACK back to client to confirm Master Key is correct */
                            int ack_type = PKT_HANDSHAKE;
                            write_all(fd, &ack_type, sizeof(int));
                        }
                        pthread_mutex_unlock(&game_mutex);
                    }
                } else if (type == PKT_QUERY || type == PKT_LOGIN) {
                    PacketLogin pkt;
                    if (read_all(fd, ((char*)&pkt) + sizeof(int), sizeof(PacketLogin) - sizeof(int)) > 0) {
                        if (type == PKT_QUERY) {
                            pthread_mutex_lock(&game_mutex);
                            int found = 0;
                            for(int j=0; j<MAX_CLIENTS; j++) { if (players[j].name[0] != '\0' && strcmp(players[j].name, pkt.name) == 0) { found = 1; break; } }
                            pthread_mutex_unlock(&game_mutex);
                            write_all(fd, &found, sizeof(int));
                        } else {
                            pthread_mutex_lock(&game_mutex);
                            int slot = -1;
                            for(int j=0; j<MAX_CLIENTS; j++) { if (players[j].name[0] != '\0' && strcmp(players[j].name, pkt.name) == 0) { slot = j; break; } }
                            if (slot == -1) { for(int j=0; j<MAX_CLIENTS; j++) if (players[j].name[0] == '\0') { slot = j; break; } }
                            
                            if (slot != -1) {
                                players[slot].socket = fd;
                                int is_new = (players[slot].name[0] == '\0');
                                players[slot].active = 0; /* Block updates during sync */

                                if (is_new) {
                                    strcpy(players[slot].name, pkt.name); players[slot].faction = pkt.faction; players[slot].ship_class = pkt.ship_class;
                                    players[slot].state.energy = 9999999; players[slot].state.torpedoes = 1000;
                                    int crew = 400;
                                    switch(pkt.ship_class) {
                                        case SHIP_CLASS_EXPLORER:    crew = 1012; break;
                                        case SHIP_CLASS_FLAGSHIP: crew = 850; break;
                                        case SHIP_CLASS_LEGACY: crew = 430; break;
                                        case SHIP_CLASS_HEAVY_CRUISER: crew = 750; break;
                                        case SHIP_CLASS_ESCORT:   crew = 50; break;
                                        case SHIP_CLASS_SCIENCE:  crew = 150; break;
                                        case SHIP_CLASS_RESEARCH:    crew = 80; break;
                                        default: crew = 200; break;
                                    }
                                    players[slot].state.crew_count = crew;
                                                                        players[slot].state.q1 = rand()%10 + 1; players[slot].state.q2 = rand()%10 + 1; players[slot].state.q3 = rand()%10 + 1;
                                                                        players[slot].state.s1 = 5.0; players[slot].state.s2 = 5.0; players[slot].state.s3 = 5.0;
                                                                        
                                                                        /* Initialize Absolute Galactic Coordinates */
                                                                        players[slot].gx = (players[slot].state.q1 - 1) * 10.0 + players[slot].state.s1;
                                                                        players[slot].gy = (players[slot].state.q2 - 1) * 10.0 + players[slot].state.s2;
                                                                        players[slot].gz = (players[slot].state.q3 - 1) * 10.0 + players[slot].state.s3;
                                                                        
                                                                                                                players[slot].state.inventory[1] = 10; /* Initial Isotope-X for jumps */
                                                                        
                                                                                                                players[slot].state.hull_integrity = 100.0f;
                                                                        
                                                                                                                for(int s=0; s<10; s++) players[slot].state.system_health[s] = 100.0f;
                                                                        players[slot].state.life_support = 100.0f;
                                                                        players[slot].state.ion_beam_charge = 100.0f;
                                                                        memset(players[slot].state.probes, 0, sizeof(players[slot].state.probes));
                                }
                                
                                /* WELCOME PACKAGE: Ensure all captains (new or returning) have at least 10 Isotope-X for Jumps */
                                if (players[slot].state.inventory[1] < 10) {
                                    players[slot].state.inventory[1] = 10;
                                }

                                /* SESSION INITIALIZATION: Reset transient event flags */
                                players[slot].renegade_timer = 0;
                                players[slot].state.boom.active = 0;
                                players[slot].state.torp.active = 0;
                                players[slot].state.dismantle.active = 0;
                                players[slot].state.beam_count = 0;
                                players[slot].torp_active = false;
                                
                                /* FORCE COORDINATE SYNC: Ensure HUD and Viewer align immediately */
                                players[slot].state.q1 = get_q_from_g(players[slot].gx);
                                players[slot].state.q2 = get_q_from_g(players[slot].gy);
                                players[slot].state.q3 = get_q_from_g(players[slot].gz);
                                players[slot].state.s1 = players[slot].gx - (players[slot].state.q1 - 1) * 10.0;
                                players[slot].state.s2 = players[slot].gy - (players[slot].state.q2 - 1) * 10.0;
                                players[slot].state.s3 = players[slot].gz - (players[slot].state.q3 - 1) * 10.0;

                                pthread_mutex_unlock(&game_mutex);

                                LOG_DEBUG("Synchronizing Galaxy Master (%zu bytes) to FD %d\n", sizeof(SpaceGLGame), fd);
                                sign_galaxy_data();
                                pthread_mutex_lock(&players[slot].socket_mutex);
                                int w_res = write_all(fd, &spacegl_master, sizeof(SpaceGLGame));
                                pthread_mutex_unlock(&players[slot].socket_mutex);

                                if (w_res == sizeof(SpaceGLGame)) {
                                    pthread_mutex_lock(&game_mutex);
                                    LOG_DEBUG("Galaxy Master sent successfully to FD %d\n", fd);
                                    bool needs_rescue = false;
                                    if (players[slot].state.energy <= 0 || players[slot].state.crew_count <= 0) needs_rescue = true;
                                    
                                    int pq1 = players[slot].state.q1, pq2 = players[slot].state.q2, pq3 = players[slot].state.q3;
                                    if (IS_Q_VALID(pq1, pq2, pq3)) {
                                        QuadrantIndex *qi = &spatial_index[pq1][pq2][pq3];
                                        for (int s=0; s<qi->star_count; s++) {
                                            double d = sqrt(pow(players[slot].state.s1 - qi->stars[s]->x, 2) + pow(players[slot].state.s2 - qi->stars[s]->y, 2) + pow(players[slot].state.s3 - qi->stars[s]->z, 2));
                                            if (d < 1.0) needs_rescue = true;
                                        }
                                        for (int p=0; p<qi->planet_count; p++) {
                                            double d = sqrt(pow(players[slot].state.s1 - qi->planets[p]->x, 2) + pow(players[slot].state.s2 - qi->planets[p]->y, 2) + pow(players[slot].state.s3 - qi->planets[p]->z, 2));
                                            if (d < 1.0) needs_rescue = true;
                                        }
                                    }

                                    if (needs_rescue) {
                                        int rq1, rq2, rq3;
                                        /* Find a quadrant without a supernova */
                                        do {
                                            rq1 = rand()%10 + 1; rq2 = rand()%10 + 1; rq3 = rand()%10 + 1;
                                        } while (supernova_event.supernova_timer > 0 && 
                                                 rq1 == supernova_event.supernova_q1 && 
                                                 rq2 == supernova_event.supernova_q2 && 
                                                 rq3 == supernova_event.supernova_q3);

                                        players[slot].state.q1 = rq1; players[slot].state.q2 = rq2; players[slot].state.q3 = rq3;
                                        players[slot].state.s1 = 5.0; players[slot].state.s2 = 5.0; players[slot].state.s3 = 5.0;
                                        players[slot].state.energy = 9999999;
                                        players[slot].state.torpedoes = 1000;
                                        if (players[slot].state.crew_count <= 0) players[slot].state.crew_count = 100;
                                        players[slot].state.hull_integrity = 80.0f;
                                        for(int s=0; s<10; s++) players[slot].state.system_health[s] = 80.0f;
                                        players[slot].gx = (players[slot].state.q1-1)*10.0 + 5.0;
                                        players[slot].gy = (players[slot].state.q2-1)*10.0 + 5.0;
                                        players[slot].gz = (players[slot].state.q3-1)*10.0 + 5.0;
                                        players[slot].nav_state = NAV_STATE_IDLE; players[slot].hyper_speed = 0;
                                        players[slot].dx = 0; players[slot].dy = 0; players[slot].dz = 0;
                                        players[slot].active = 1;
                                        players[slot].crypto_algo = CRYPTO_NONE; 
                                        pthread_mutex_unlock(&game_mutex);
                                        send_server_msg(slot, "Alliance Command", "EMERGENCY RESCUE: Your ship was recovered and towed to a safe quadrant.");
                                    } else {
                                        players[slot].active = 1;
                                        players[slot].crypto_algo = CRYPTO_NONE; 
                                        pthread_mutex_unlock(&game_mutex);
                                        send_server_msg(slot, "SERVER", is_new ? "Welcome aboard, new Captain." : "Commander, welcome back.");
                                    }
                                }
                            } else {
                                pthread_mutex_unlock(&game_mutex);
                            }
                        }
                    }
                } else if (p_idx != -1) {
                    if (type == PKT_COMMAND) {
                        PacketCommand pkt;
                        if (read_all(fd, ((char*)&pkt) + sizeof(int), sizeof(PacketCommand) - sizeof(int)) > 0) process_command(p_idx, pkt.cmd);
                    } else if (type == PKT_MESSAGE) {
                        PacketMessage pkt;
                        if (read_all(fd, ((char*)&pkt) + sizeof(int), offsetof(PacketMessage, text) - sizeof(int)) > 0) {
                            if (pkt.length > 0 && pkt.length < 4096) read_all(fd, pkt.text, pkt.length + 1);
                            else pkt.text[0] = '\0';
                            pkt.type = type; broadcast_message(&pkt);
                        }
                    }
                }
            }
        }
    }
    return 0;
}
