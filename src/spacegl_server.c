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
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>
#include <stddef.h>
#include <signal.h>
#include <errno.h>
#include <math.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/err.h>
#include <openssl/provider.h>
#include "server_internal.h"
#include "conn.h"
#include "packets.h"

#define MAX_EVENTS (MAX_CLIENTS + GAME_MAX_PLAYERS)

pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
threadpool_t *g_pool = NULL;
int g_debug = 0;
int global_tick = 0;

/* Graceful shutdown state. Set by the signal handler (async-signal-safe);
 * the simulation thread and the main epoll loop both observe it and exit
 * cleanly, and main() performs the final save and resource cleanup. */
/* Exported (conn.h): the async event loop (src/server/conn.c) observes the
 * same flag as the legacy loop. */
volatile sig_atomic_t g_running = 1;

static void handle_shutdown_signal(int sig) {
    (void)sig;
    g_running = 0;
}
uint8_t ALGO_KEYS[MAX_CRYPTO_ALGOS + 1][32]; /* Global Default */
uint8_t MASTER_SESSION_KEY[32];
uint8_t SERVER_PUBKEY[32];
uint8_t SERVER_PRIVKEY[64];
uint8_t deep_space_key[32];
uint8_t GALAXY_VERIFY_KEY[32]; /* Stable galaxy-signature key, derived from the master key */

/* Derive the stable galaxy verification key from the master key.
 * This key (and only this key) is disclosed to clients at handshake time,
 * so they can verify the galaxy state HMAC even after their local session
 * key has rotated away from the master key. */
void derive_galaxy_verify_key() {
    unsigned int len = 32;
    HMAC(EVP_sha256(), MASTER_SESSION_KEY, 32,
         (const uint8_t*)"SPACEGL-GALAXY-VERIFY-V1", 24, GALAXY_VERIFY_KEY, &len);
}

/* Validate a captain name received from the network.
 * Allows only [A-Za-z0-9_-] and enforces MAX 32 chars.
 * Returns 1 if safe, 0 if the name must be rejected (path injection guard). */
void ensure_player_algo_key(int p_idx, int k, bool private_mode) {
    if (k < 1 || k > MAX_CRYPTO_ALGOS) return;
    /* We don't use a 'loaded' flag here to keep it simple, we just derive if needed 
       or check if the buffer is all zeros (unlikely for a valid key) */
    
    char rel_dir[160];
    snprintf(rel_dir, sizeof(rel_dir), "captains/%s", players[p_idx].name);
    char dir_path[1024];
    server_data_path(dir_path, sizeof(dir_path), rel_dir);
    char key_path[1088];
    if (private_mode) snprintf(key_path, sizeof(key_path), "%s/algo_%d_private.key", dir_path, k);
    else snprintf(key_path, sizeof(key_path), "%s/algo_%d.key", dir_path, k);

    /* Try to load from disk first (persistence) */
    FILE *fk = fopen(key_path, "r");
    if (fk) {
        /* Security: frequency keys must not be world-readable (also repairs
           legacy files created with the default umask). */
        if (fchmod(fileno(fk), 0600) != 0) { /* best effort */ }
        char hex[128];
        if (fgets(hex, sizeof(hex), fk)) {
            for (int b = 0; b < 32; b++) {
                unsigned int val;
                if (sscanf(hex + (b * 2), "%02x", &val) == 1) players[p_idx].algo_keys[k][b] = (uint8_t)val;
            }
        }
        fclose(fk);
        return;
    }

    /* Otherwise derive it exactly like the client does */
    char salt[256];
    if (private_mode) sprintf(salt, "SPACEGL-ALGO-PRIVATE-%s-SIG-%d", players[p_idx].name, k);
    else sprintf(salt, "SPACEGL-ALGO-FREQUENCY-GALAXY-WORMHOLE-SIG-%d", k);
    
    unsigned int len = 32;
    HMAC(EVP_sha256(), MASTER_SESSION_KEY, 32, (uint8_t*)salt, strlen(salt), players[p_idx].algo_keys[k], &len);
    
    /* Save for next time (mode 0600: owner read/write only) */
    fk = fopen(key_path, "w");
    if (fk) {
        if (fchmod(fileno(fk), 0600) != 0) { /* best effort */ }
        for (int b = 0; b < 32; b++) fprintf(fk, "%02x", players[p_idx].algo_keys[k][b]);
        fprintf(fk, "\n");
        fclose(fk);
    }
}

void derive_algo_keys(uint8_t *master_key, const char *name, uint8_t target_keys[MAX_CRYPTO_ALGOS + 1][32]) {
    char rel_dir[160];
    snprintf(rel_dir, sizeof(rel_dir), "captains/%s", name ? name : "DEFAULT");
    char dir_path[1024];
    server_data_path(dir_path, sizeof(dir_path), rel_dir);
    /* mkdir(dir_path) is usually handled by the client, but for server safety
       (the data dir itself is created in main, best effort): */
    char captains_path[1024];
    server_data_path(captains_path, sizeof(captains_path), "captains");
    mkdir(captains_path, 0700);
    mkdir(dir_path, 0700);

    for (int k = 1; k <= MAX_CRYPTO_ALGOS; k++) {
        char key_path[1088];
        snprintf(key_path, sizeof(key_path), "%s/algo_%d.key", dir_path, k);
        
        FILE *fk_read = fopen(key_path, "r");
        bool loaded = false;
        if (fk_read) {
            /* Security: tighten legacy permissions (best effort) */
            if (fchmod(fileno(fk_read), 0600) != 0) { /* best effort */ }
            char hex[128];
            if (fgets(hex, sizeof(hex), fk_read)) {
                for (int b = 0; b < 32; b++) {
                    unsigned int val;
                    if (sscanf(hex + (b * 2), "%02x", &val) == 1) {
                        target_keys[k][b] = (uint8_t)val;
                    }
                }
                loaded = true;
            }
            fclose(fk_read);
        }

        if (!loaded) {
            char salt[128];
            sprintf(salt, "SPACEGL-ALGO-FREQUENCY-GALAXY-WORMHOLE-SIG-%d", k);
            unsigned int len = 32;
            HMAC(EVP_sha256(), master_key, 32, (uint8_t*)salt, strlen(salt), target_keys[k], &len);
            
            /* Save with restrictive permissions (mode 0600) */
            FILE *fk = fopen(key_path, "w");
            if (fk) {
                if (fchmod(fileno(fk), 0600) != 0) { /* best effort */ }
                for (int b = 0; b < 32; b++) fprintf(fk, "%02x", target_keys[k][b]);
                fprintf(fk, "\n");
                fclose(fk);
            }
        }
    }
}

void sign_galaxy_data();
static void sign_game_state(SpaceGLGame *gs);

void sync_client_task(void *arg) {
    SyncTask *task = (SyncTask *)arg;
    if (!task) return;
    int slot = task->slot;
    int fd = task->fd;
    bool is_new = task->is_new;

    if (slot < 0 || slot >= MAX_CLIENTS) {
        LOG_DEBUG("Sync Error: Invalid slot %d\n", slot);
        free(task);
        return;
    }

    LOG_DEBUG("Asynchronous Sync: Sending Galaxy Master to FD %d (Slot %d)\n", fd, slot);
    
    /* 1. Take a consistent snapshot of the Galaxy Master and sign it under the
     *    game lock, so that the exact bytes the client receives are the bytes
     *    covered by the HMAC-SHA256 integrity signature. The (blocking) write
     *    of the ~1MB snapshot then runs on the private copy, outside the game
     *    lock, so a slow client cannot stall the 60Hz simulation. */
    SpaceGLGame *snapshot = malloc(sizeof(SpaceGLGame));
    if (!snapshot) {
        free(task);
        return;
    }
    pthread_mutex_lock(&game_mutex);
    if (players[slot].socket != fd || players[slot].generation != task->generation) {
        pthread_mutex_unlock(&game_mutex);
        free(snapshot);
        free(task);
        return;
    }
    memcpy(snapshot, &spacegl_master, sizeof(SpaceGLGame));
    sign_game_state(snapshot);
    pthread_mutex_unlock(&game_mutex);

    pthread_mutex_lock(&players[slot].socket_mutex);
    if (players[slot].socket != fd || players[slot].generation != task->generation) {
        pthread_mutex_unlock(&players[slot].socket_mutex);
        free(snapshot);
        free(task);
        return;
    }
    int w_res = write_all(fd, snapshot, sizeof(SpaceGLGame));
    pthread_mutex_unlock(&players[slot].socket_mutex);
    free(snapshot);

    if (w_res == sizeof(SpaceGLGame)) {
        pthread_mutex_lock(&game_mutex);
        if (players[slot].socket != fd || players[slot].generation != task->generation) {
            pthread_mutex_unlock(&game_mutex);
            free(task);
            return;
        }
        
        /* 2. Finalize player activation */
        bool needs_rescue = false;
        if (players[slot].state.energy == 0 || players[slot].state.crew_count <= 0) needs_rescue = true;
        
        int pq1 = players[slot].state.q1, pq2 = players[slot].state.q2, pq3 = players[slot].state.q3;
        if (IS_Q_VALID(pq1, pq2, pq3)) {
            QuadrantIndex *qi = &spatial_index[pq1][pq2][pq3];
            for (int s=0; s<qi->star_count; s++) {
                if (!qi->stars[s]) continue;
                double d = sqrt(pow(players[slot].state.s1 - qi->stars[s]->x, 2) + pow(players[slot].state.s2 - qi->stars[s]->y, 2) + pow(players[slot].state.s3 - qi->stars[s]->z, 2));
                if (d < 1.0) needs_rescue = true;
            }
            for (int p=0; p<qi->planet_count; p++) {
                if (!qi->planets[p]) continue;
                double d = sqrt(pow(players[slot].state.s1 - qi->planets[p]->x, 2) + pow(players[slot].state.s2 - qi->planets[p]->y, 2) + pow(players[slot].state.s3 - qi->planets[p]->z, 2));
                if (d < 1.0) needs_rescue = true;
            }
        }

        if (needs_rescue) {
            /* Reposition ship to center of a random safe quadrant */
            rescue_player(slot, &RESCUE_PARAMS_LOGIN, NULL);
            pthread_mutex_unlock(&game_mutex);
            send_server_msg(slot, "Alliance Command", "EMERGENCY RESCUE: Ship recovered and towed to safe sector.");

            /* Server-side logging of the rescue */
            time_t now_rescue = time(NULL);
            struct tm *t_rescue = localtime(&now_rescue);
            char time_rescue[64];
            strftime(time_rescue, sizeof(time_rescue), "%Y-%m-%d %H:%M:%S", t_rescue);
            slog("\033[1;31m[RESCUE]\033[0m     Captain \033[1;37m%-15s\033[0m was recovered from deep space. [\033[1;33m%s\033[0m]\n", 
                   players[slot].name, time_rescue);
        } else {
            players[slot].active = 1;
            pthread_mutex_unlock(&game_mutex);
            send_server_msg(slot, "SERVER", is_new ? "Welcome aboard, new Captain." : "Commander, welcome back.");
        }

        /* Server-side logging of the connection with timestamp */
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);
        slog("\033[1;32m[CONNECTION]\033[0m Captain \033[1;37m%-15s\033[0m has entered the galaxy. [\033[1;33m%s\033[0m]\n", 
               players[slot].name, time_str);
    } else {
        /* Failed to send master data, slot remains inactive */
        pthread_mutex_lock(&game_mutex);
        players[slot].socket = 0;
        players[slot].active = 0;
        pthread_mutex_unlock(&game_mutex);
        LOG_DEBUG("Sync Failed: Client FD %d disconnected during Galaxy Master transmission\n", fd);
    }
    
    free(task);
}

void *game_loop_thread(void *arg) {
    (void)arg;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    while (g_running) {
        ts.tv_nsec += GAME_TICK_NSEC; 
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        pthread_mutex_lock(&game_mutex);
        update_game_logic();
        global_tick++;
        spacegl_master.frame_id++;
        
        /* Sync telemetry state while we hold the game lock */
        extern void telemetry_sync_state();
        telemetry_sync_state();

        pthread_mutex_unlock(&game_mutex);

        /* Re-sign the galaxy state after every simulation tick so that the
         * integrity signature always reflects the current state (not just the
         * boot-time state). Runs outside the game lock: the login snapshot
         * path re-signs under the lock, so consistency is preserved where it
         * matters (client verification at full synchronization). */
        sign_galaxy_data();

        /* Send all pending player network updates OUTSIDE game_mutex.
         * This decouples blocking TCP writes from the 60Hz simulation lock,
         * preventing a slow client from stalling the entire game thread. */
        send_pending_updates();

        /* Broadcast telemetry OUTSIDE of the game lock to avoid stalling the main loop */
        extern void telemetry_broadcast();
        telemetry_broadcast();
    }
    return NULL;
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
    printf("%s | %s SIGNATURE ALGO:    %sHMAC-SHA256 (Galaxy State Integrity)           %s %s\n", B_MAGENTA, B_WHITE, B_GREEN, B_MAGENTA, RESET);
    printf("%s | %s ENCRYPTION FLAGS:  %s0x%08X (AES-GCM / PQC-ALIAS / INT)              %s %s\n", B_MAGENTA, B_WHITE, B_GREEN, 0x07, B_MAGENTA, RESET);

    /* Security: never print the master key. Display only a short, non-reversible
       fingerprint (first 4 bytes of the SHA-256 digest of the key material). */
    {
        uint8_t key_digest[32];
        SHA256(MASTER_SESSION_KEY, 32, key_digest);
        printf("%s | %s MASTER KEY:        %sPRESENT (fingerprint: %02X%02X%02X%02X)             %s %s\n",
               B_MAGENTA, B_WHITE, B_YELLOW, key_digest[0], key_digest[1], key_digest[2], key_digest[3], B_MAGENTA, RESET);
        memset(key_digest, 0, sizeof(key_digest));
    }
    
    printf("%s '-----------------------------------------------------------------------------------------'%s\n\n", B_MAGENTA, RESET);
}

/*
 * Sign the persistent galaxy state of the given snapshot.
 *
 * The signature covers the deterministic, client-recoverable part of the
 * state: frame_id || g[41][41][41] || z[41][41][41] (g and z are contiguous
 * in SpaceGLGame). It is computed with a two-level HMAC using the stable
 * GALAXY_VERIFY_KEY (never with the master key directly), so that clients
 * can recompute it locally after the handshake:
 *
 *   inner = HMAC-SHA256(GALAXY_VERIFY_KEY, "SPACEGL-SIG-V1" || LE64(frame_id))
 *   sig   = HMAC-SHA256(inner, g[] || z[])
 */
static void sign_game_state(SpaceGLGame *gs) {
    uint8_t inner[32];
    uint8_t head[14 + 8];
    unsigned int inner_len = 32;
    unsigned int sig_len = 32;

    memcpy(head, "SPACEGL-SIG-V1", 14);
    memcpy(head + 14, &gs->frame_id, 8);
    HMAC(EVP_sha256(), GALAXY_VERIFY_KEY, 32, head, sizeof(head), inner, &inner_len);
    HMAC(EVP_sha256(), inner, 32, (const uint8_t*)gs->g,
         sizeof(gs->g) + sizeof(gs->z), gs->server_signature, &sig_len);
    memset(gs->server_signature + 32, 0, sizeof(gs->server_signature) - 32);
}

void sign_galaxy_data() {
    /* Re-sign on every call: the galaxy signature must track state changes,
       not just the boot-time state. */
    sign_game_state(&spacegl_master);

    /* In a real scenario, we'd use an actual Ed25519 public key here.
       For this implementation, we use a derived key from the Master Session Key. */
    SHA256(MASTER_SESSION_KEY, 32, SERVER_PUBKEY);
    memcpy(spacegl_master.server_pubkey, SERVER_PUBKEY, 32);

    /* Encryption Details: 
       Bit 0: Integrity signature present (HMAC-SHA256, client-verifiable)
       Bit 1: Post-quantum-named slots active (EXPERIMENTAL aliases of AES-256-GCM)
       Bit 2: AES-256-GCM Active
    */
    spacegl_master.encryption_flags = 0x07; 
}

/* Legacy event loop: the original blocking per-packet read path
 * (read_all() bounded by the 5 s poll). Selected by --io=legacy,
 * the default; kept as-is alongside the async architecture
 * (run_epoll_loop_async, src/server/conn.c). */
static void run_legacy_loop(int server_fd, int epoll_fd) {
    struct sockaddr_in addr;
    socklen_t adlen = sizeof(addr);
    struct epoll_event ev, events[MAX_EVENTS];

    while (g_running) {
        /* 200ms timeout so the loop can observe g_running for a graceful stop */
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 200);
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
                SG_TRACE4(SG_CAT_NETWORK, "new connection accepted (fd %d)", new_socket);
                LOG_DEBUG("New connection accepted: FD %d\n", new_socket);
            } else {
                /* Handle data from a client */
                int type;
                int r = read_all(fd, &type, sizeof(int));
                
                if (r <= 0) {
                    /* Disconnect: Keep player record for persistence, just close socket */
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    pthread_mutex_lock(&game_mutex);
                    for (int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == fd) { 
                        /* Log the departure before clearing the socket */
                        time_t now_disc = time(NULL);
                        struct tm *t_disc = localtime(&now_disc);
                        char time_disc[64];
                        strftime(time_disc, sizeof(time_disc), "%Y-%m-%d %H:%M:%S", t_disc);
                        SG_NOTICE(SG_CAT_CLIENT, "captain %s disconnected (slot %d)",
                                  players[i].name[0] ? players[i].name : "?", i);
                        slog("\033[1;35m[DISCONNECT]\033[0m Captain \033[1;37m%-15s\033[0m has left the galaxy.    [\033[1;33m%s\033[0m]\n", 
                               players[i].name[0] ? players[i].name : "Unknown", time_disc);

                        players[i].socket = 0;
                        players[i].active = 0;
                        players[i].radio_lock_target = 0;                        memset(players[i].session_key, 0, 32);
                        save_galaxy();
                        break; 
                    }
                    pthread_mutex_unlock(&game_mutex);
                    close(fd);
                    LOG_DEBUG("Connection closed: FD %d\n", fd);
                    continue;
                }

                /* The per-packet body handling and every teardown path
                   live in the shared dispatcher (src/server/packets.c),
                   which is also the packet-complete callback of the
                   async mode (src/server/conn.c). LEGACY mode: io.conn
                   is NULL, so bodies are read from the socket with
                   read_all() (blocking, bounded by the 5 s poll) and
                   teardown is plain epoll_ctl + close - the
                   pre-refactor behavior, byte for byte. */
                PktIO io;
                io.epoll_fd = epoll_fd;
                io.fd = fd;
                io.conn = NULL;
                io.rx = NULL;
                io.rx_len = 0;
                if (dispatch_packet(&io, type) != 0) continue; /* dispatcher closed the connection */
            }
        }
    }
}

int main(int argc, char *argv[]) {
    int server_fd, epoll_fd;
    struct sockaddr_in addr;
    int opt = 1;

    /* Official logging (severity x category): env first, CLI overrides. */
    sglog_init("srv");
    sglog_apply_args(argc, argv);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [OPTIONS]\n", argv[0]);
            printf("Space GL Galactic Server Core\n\n");
            printf("Options:\n");
            printf("  -d             Enable debug mode\n");
            printf("  --log-level=L  Log level: TRACE1..TRACE4|DEBUG|INFO|NOTICE|"
                   "SUCCESS|WARNING|ERROR|CRITICAL|FATAL|OFF (default INFO)\n");
            printf("  --log-category=A,B  Filter by category (e.g. PHA,TOR,NETWORK,IPC)\n");
            printf("  --data-dir DIR Root directory for persistent state (captains/ tree\n");
            printf("                 and galaxy.dat). Default: current working directory.\n");
            printf("  --io=MODE      Event-loop I/O strategy: 'legacy' (default; blocking\n");
            printf("                 per-packet reads bounded by a 5 s poll) or 'async'\n");
            printf("                 (non-blocking per-connection RX/TX state machine: a\n");
            printf("                 stalled client can no longer block the loop)\n");
            printf("  --help, -h     Display this help and exit\n");
            printf("  --version      Display version information and exit\n\n");
            printf("Environment Variables:\n");
            printf("  SPACEGL_KEY    Master Key for cryptographic synchronization (required)\n");
            return 0;
        }
        if (strcmp(argv[i], "--data-dir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "ERROR: --data-dir requires a path argument.\n");
                exit(1);
            }
            server_set_data_dir(argv[++i]);
        }
        if (strncmp(argv[i], "--io=", 5) == 0) {
            const char *v = argv[i] + 5;
            if (strcmp(v, "legacy") == 0) {
                g_io_mode = SPACEGL_IO_LEGACY;
            } else if (strcmp(v, "async") == 0) {
                g_io_mode = SPACEGL_IO_ASYNC;
            } else {
                fprintf(stderr, "ERROR: --io= expects 'legacy' or 'async'.\n");
                exit(1);
            }
        }
        if (strcmp(argv[i], "--version") == 0) {
            printf("Space GL Server v2026.09.13.03\n");
            printf("Copyright (C) 2026 Nicola Taibi\n");
            printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n");
            return 0;
        }
        if (strcmp(argv[i], "-d") == 0) g_debug = 1;
    }

    SG_INFO(SG_CAT_SYSTEM, "SpaceGL Galactic Server Core (pid %d, log level %s)",
            (int)getpid(), sglog_sev_name(sglog_threshold()));
    SG_INFO(SG_CAT_SYSTEM, "I/O mode: %s",
            g_io_mode == SPACEGL_IO_ASYNC
                ? "ASYNC (non-blocking per-connection state machine)"
                : "LEGACY (blocking per-packet reads, bounded poll)");

    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, handle_shutdown_signal);
    signal(SIGINT, handle_shutdown_signal);
    log_init();

    /* Data directory: persistent state (captains/ tree + galaxy.dat) lives
       under it. Default "." preserves the legacy CWD behavior; the
       directory is created best-effort (it must exist as a directory). */
    if (mkdir(g_data_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "WARNING: cannot create data directory '%s': %s\n",
                g_data_dir, strerror(errno));
    }
    printf("\033[1;34m[DATA]\033[0m Data directory: %s (captains/ + galaxy.dat)\n",
           (g_data_dir[0] != '\0') ? g_data_dir : ".");

    /* Security Initialization */
    char *env_key = getenv("SPACEGL_KEY");
    if (!env_key) {
        SG_FATAL(SG_CAT_SECURITY, "SPACEGL_KEY not found in environment");
        fprintf(stderr, "\033[1;31mSECURITY ERROR: Deep Space Key (SPACEGL_KEY) not found in environment.\033[0m\n");
        fprintf(stderr, "The server requires a shared secret key to secure communications.\n");
        exit(1);
    }
    memset(MASTER_SESSION_KEY, 0, 32);
    size_t env_len = strlen(env_key);
    memcpy(MASTER_SESSION_KEY, env_key, (env_len > 32) ? 32 : env_len);
    
    /* Auto-generate Default Global Algorithm keys */
    derive_algo_keys(MASTER_SESSION_KEY, "DEFAULT", ALGO_KEYS);
    printf("\033[1;34m[SECURITY]\033[0m Default Global Frequencies derived.\n");

    /* Derive the stable galaxy verification key (used for the client-verifiable
       HMAC-SHA256 signature of the galaxy state). Must run before any sign. */
    derive_galaxy_verify_key();
    
    memset(players, 0, sizeof(players)); 
    memset(players_torpedoes, 0, sizeof(players_torpedoes));
    srand(time(NULL)); 
    for(int i=0; i<MAX_CLIENTS; i++) pthread_mutex_init(&players[i].socket_mutex, NULL);
    
    /* Server Welcome Screen */
    
    /* Clear screen */
    /* printf("\033[2J\033[H"); */      
    
    printf("\033[1;31m  ____________________________________________________________________________\n" );
    printf(" /                                                                            \\\n" );
    printf(" | \033[1;37m   ███████╗██████╗  █████╗  ██████╗███████╗     ██████╗ ██╗              \033[1;31m  |\n" );
    printf(" | \033[1;37m   ██╔════╝██╔══██╗██╔══██╗██╔════╝██╔════╝    ██╔════╝ ██║              \033[1;31m  |\n" );
    printf(" | \033[1;37m   ███████╗██████╔╝███████║██║     █████╗      ██║  ███╗██║              \033[1;31m  |\n" );
    printf(" | \033[1;37m   ╚════██║██╔═══╝ ██╔══██║██║     ██╔══╝      ██║   ██║██║              \033[1;31m  |\n" );
    printf(" | \033[1;37m   ███████║██║     ██║  ██║╚██████╗███████╗    ╚██████╔╝███████╗         \033[1;31m  |\n" );
    printf(" | \033[1;37m   ╚══════╝╚═╝     ╚═╝  ╚═╝ ╚═════╝╚══════╝     ╚═════╝ ╚══════╝         \033[1;31m  |\n" );
    printf(" |                                                                            |\n" );
    printf(" | \033[1;31m         ---  G A L A C T I C   S E R V E R   C O R E  ---                \033[1;31m |\n" );
    printf(" | \033[1;33m          \"Per Tenebras, Lumen\" (Through darkness, light)                 \033[1;31m |\n" );
    printf(" |                                                                            |\n" );
    printf(" | \033[1;37m  Copyright (C) 2026 \033[1;32mNicola Taibi\033[1;37m                                        \033[1;31m  |\n" );
    printf(" | \033[1;37m  AI Core Support by \033[1;34mGoogle Gemini\033[1;37m                                       \033[1;31m  |\n" );
    printf(" | \033[1;37m  License Type:      \033[1;33mGNU GPL v3.0\033[1;37m                                        \033[1;31m  |\n" );
    printf(" \\____________________________________________________________________________/\033[0m\n\n" );

    display_system_telemetry();

    /* Detailed Debug Output for Initialization */
    printf("%s | %s DEBUG METRICS (CORE):                                               %s %s\n", B_MAGENTA, B_WHITE, B_MAGENTA, RESET);
    printf("%s | %s [STRUCT] SpaceGLGame:   %s%-10zu bytes                             %s %s\n", B_MAGENTA, B_CYAN, B_GREEN, sizeof(SpaceGLGame), B_MAGENTA, RESET);
    printf("%s | %s [STRUCT] PacketUpdate:  %s%-10zu bytes                             %s %s\n", B_MAGENTA, B_CYAN, B_GREEN, sizeof(PacketUpdate), B_MAGENTA, RESET);
    printf("%s | %s [STRUCT] ConnectedPlayer: %s%-10zu bytes                           %s %s\n", B_MAGENTA, B_CYAN, B_GREEN, sizeof(ConnectedPlayer), B_MAGENTA, RESET);
    printf("%s |                                                                      %s\n", B_MAGENTA, RESET);

    /* OpenSSL Initialization for all algorithms (including legacy ones like SEED, CAST5, etc) */
    OSSL_PROVIDER_load(NULL, "legacy");
    OSSL_PROVIDER_load(NULL, "default");
    OpenSSL_add_all_algorithms();
    OpenSSL_add_all_ciphers();
    OpenSSL_add_all_digests();

    printf("%s | %s SECURITY:         %sCryptographic Subsystem Primed                %s %s\n", B_MAGENTA, B_WHITE, B_GREEN, B_MAGENTA, RESET);

    /* Initialize Thread Pool for Async Tasks (Crypto, Pathfinding, I/O) */
    int nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    g_pool = threadpool_create(nprocs);
    SG_DEBUG(SG_CAT_THREAD, "thread pool: %d worker threads", nprocs);
    printf("%s | %s THREAD POOL:       %s%d Worker Threads Active                      %s %s\n", B_MAGENTA, B_WHITE, B_GREEN, nprocs, B_MAGENTA, RESET);

    if (load_galaxy()) {
        SG_INFO(SG_CAT_ASSET, "galaxy state loaded");
    } else {
        SG_NOTICE(SG_CAT_ASSET, "no existing galaxy state: generating new galaxy");
        generate_galaxy(); save_galaxy();
    }
    sign_galaxy_data();
    init_static_spatial_index();
    
    telemetry_init();

    pthread_t tid; pthread_create(&tid, NULL, game_loop_thread, NULL);

    printf("%s | %s GALAXY ENGINE:     %sSectors mapped and synchronized               %s %s\n", B_MAGENTA, B_WHITE, B_GREEN, B_MAGENTA, RESET);
    printf("%s '-------------------------------------------------------------------------'%s\n\n", B_MAGENTA, RESET);

    printf("--- NETWORK INITIALIZATION ---\n");
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) { perror("socket failed"); exit(EXIT_FAILURE); }
    
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) { perror("setsockopt"); }
    
    addr.sin_family = AF_INET; 
    addr.sin_addr.s_addr = INADDR_ANY; 
    addr.sin_port = htons(DEFAULT_PORT);
    
    printf("Binding to port %d...\n", DEFAULT_PORT);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("BIND FAILED");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_fd, GAME_MAX_PLAYERS) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("STELLAR SERVER listening on port %d (EPOLL MODE)\n", DEFAULT_PORT);

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) { perror("epoll_create1"); exit(EXIT_FAILURE); }

    struct epoll_event listen_ev = { .events = EPOLLIN, .data = { .fd = server_fd } };
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &listen_ev) == -1) { perror("epoll_ctl: server_fd"); exit(EXIT_FAILURE); }

    SG_SUCCESS(SG_CAT_NETWORK, "listening on port %d (TCP/binary, EPOLL mode)", DEFAULT_PORT);
    printf("STELLAR SERVER started on port %d (EPOLL MODE)\n", DEFAULT_PORT);
    
    if (g_io_mode == SPACEGL_IO_ASYNC) {
        /* Professional mode: explicitly non-blocking sessions, the
           RX/TX state machine and EPOLLOUT staging
           (src/server/conn.c); the listener is made non-blocking
           inside the loop. */
        conn_set_dispatch(packets_conn_dispatch);
        run_epoll_loop_async(server_fd, epoll_fd);
    } else {
        run_legacy_loop(server_fd, epoll_fd);
    }

    /* --- Graceful Shutdown ---
     * Order matters:
     *  1. Stop the 60Hz simulation thread (let the current tick complete).
     *  2. Stop the telemetry uplink (no more state readers).
     *  3. Drain and destroy the thread pool (in-flight RESCUE/sync/broadcast
     *     tasks complete before the workers exit).
     *  4. Final persistence of the galaxy state.
     *  5. Close the listening socket and the epoll instance. */
    slog("\033[1;33m[SHUTDOWN]\033[0m Signal received: stopping simulation thread...\n");
    pthread_join(tid, NULL);

    telemetry_shutdown();

    slog("\033[1;33m[SHUTDOWN]\033[0m Draining thread pool...\n");
    if (g_pool) {
        threadpool_destroy(g_pool);
        g_pool = NULL;
    }

    slog("\033[1;33m[SHUTDOWN]\033[0m Saving galaxy state...\n");
    save_galaxy();

    close(server_fd);
    close(epoll_fd);

    slog("\033[1;32m[SHUTDOWN]\033[0m Server stopped cleanly. Goodbye, Commander.\n");
    return 0;
}
