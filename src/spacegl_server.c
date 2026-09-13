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

#define MAX_EVENTS (MAX_CLIENTS + GAME_MAX_PLAYERS)

pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
threadpool_t *g_pool = NULL;
int g_debug = 0;
int global_tick = 0;
uint8_t ALGO_KEYS[MAX_CRYPTO_ALGOS + 1][32]; /* Global Default */
uint8_t MASTER_SESSION_KEY[32];
uint8_t SERVER_PUBKEY[32];
uint8_t SERVER_PRIVKEY[64];
uint8_t deep_space_key[32];
uint8_t GALAXY_VERIFY_KEY[32]; /* Stable galaxy-signature key, derived from the master key */
int server_fd;

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
static int sanitize_captain_name(const char *name) {
    if (!name || name[0] == '\0') {
        return 0;
    }
    size_t len = 0;
    for (const char *p = name; *p != '\0'; p++) {
        len++;
        if (len > 32) {
            return 0;
        }
        if (!((*p >= 'A' && *p <= 'Z') ||
              (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') ||
              *p == '_' || *p == '-')) {
            return 0;
        }
    }
    return 1;
}

void ensure_player_algo_key(int p_idx, int k, bool private_mode) {
    if (k < 1 || k > MAX_CRYPTO_ALGOS) return;
    /* We don't use a 'loaded' flag here to keep it simple, we just derive if needed 
       or check if the buffer is all zeros (unlikely for a valid key) */
    
    char dir_path[128];
    sprintf(dir_path, "captains/%s", players[p_idx].name);
    char key_path[256];
    if (private_mode) sprintf(key_path, "%s/algo_%d_private.key", dir_path, k);
    else sprintf(key_path, "%s/algo_%d.key", dir_path, k);

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
    char dir_path[128];
    sprintf(dir_path, "captains/%s", name ? name : "DEFAULT");
    /* mkdir(dir_path) is usually handled by the client, but for server safety: */
    mkdir("captains", 0700);
    mkdir(dir_path, 0700);

    for (int k = 1; k <= MAX_CRYPTO_ALGOS; k++) {
        char key_path[256];
        sprintf(key_path, "%s/algo_%d.key", dir_path, k);
        
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

typedef struct {
    int slot;
    int fd;
    uint32_t generation;
    bool is_new;
} SyncTask;

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
            int rq1, rq2, rq3;
            do {
                rq1 = rand() % GALAXY_SIZE + 1;
                rq2 = rand() % GALAXY_SIZE + 1;
                rq3 = rand() % GALAXY_SIZE + 1;
            } while (supernova_event.supernova_timer > 0 && rq1 == supernova_event.supernova_q1 && rq2 == supernova_event.supernova_q2 && rq3 == supernova_event.supernova_q3);

            players[slot].state.q1 = rq1; players[slot].state.q2 = rq2; players[slot].state.q3 = rq3;
            players[slot].state.s1 = (QUADRANT_SIZE / 2.0); players[slot].state.s2 = (QUADRANT_SIZE / 2.0); players[slot].state.s3 = (QUADRANT_SIZE / 2.0);
            players[slot].state.energy = MAX_ENERGY_CAPACITY;
            players[slot].state.torpedoes = MAX_TORPEDO_CAPACITY;
            if (players[slot].state.crew_count <= 0) players[slot].state.crew_count = (MAX_CREW_EXPLORER / 10);
            players[slot].state.hull_integrity = (float)THRESHOLD_SYS_STABLE + 5.0f;
            for (int s = 0; s < MAX_SYSTEMS; s++) players[slot].state.system_health[s] = (float)THRESHOLD_SYS_STABLE + 5.0f;
            players[slot].gx = (rq1 - 1) * QUADRANT_SIZE + (QUADRANT_SIZE / 2.0);
            players[slot].gy = (rq2 - 1) * QUADRANT_SIZE + (QUADRANT_SIZE / 2.0);
            players[slot].gz = (rq3 - 1) * QUADRANT_SIZE + (QUADRANT_SIZE / 2.0);
            players[slot].nav_state = NAV_STATE_IDLE;
            players[slot].active = 1;
            pthread_mutex_unlock(&game_mutex);
            send_server_msg(slot, "Alliance Command", "EMERGENCY RESCUE: Ship recovered and towed to safe sector.");

            /* Server-side logging of the rescue */
            time_t now_rescue = time(NULL);
            struct tm *t_rescue = localtime(&now_rescue);
            char time_rescue[64];
            strftime(time_rescue, sizeof(time_rescue), "%Y-%m-%d %H:%M:%S", t_rescue);
            printf("\033[1;31m[RESCUE]\033[0m     Captain \033[1;37m%-15s\033[0m was recovered from deep space. [\033[1;33m%s\033[0m]\n", 
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
        printf("\033[1;32m[CONNECTION]\033[0m Captain \033[1;37m%-15s\033[0m has entered the galaxy. [\033[1;33m%s\033[0m]\n", 
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
    while (1) {
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

int main(int argc, char *argv[]) {
    int server_fd, epoll_fd;
    struct sockaddr_in addr;
    int opt = 1, adlen = sizeof(addr);
    struct epoll_event ev, events[MAX_EVENTS];

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [OPTIONS]\n", argv[0]);
            printf("Space GL Galactic Server Core\n\n");
            printf("Options:\n");
            printf("  -d             Enable debug mode\n");
            printf("  --help, -h     Display this help and exit\n");
            printf("  --version      Display version information and exit\n\n");
            printf("Environment Variables:\n");
            printf("  SPACEGL_KEY    Master Key for cryptographic synchronization (required)\n");
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            printf("Space GL Server v2026.04.02.01\n");
            printf("Copyright (C) 2026 Nicola Taibi\n");
            printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n");
            return 0;
        }
        if (strcmp(argv[i], "-d") == 0) g_debug = 1;
    }
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
    printf("%s | %s THREAD POOL:       %s%d Worker Threads Active                      %s %s\n", B_MAGENTA, B_WHITE, B_GREEN, nprocs, B_MAGENTA, RESET);

    if (!load_galaxy()) { generate_galaxy(); save_galaxy(); }
    sign_galaxy_data();
    init_static_spatial_index();
    
    extern void telemetry_init();
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
                    /* Disconnect: Keep player record for persistence, just close socket */
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    pthread_mutex_lock(&game_mutex);
                    for (int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == fd) { 
                        /* Log the departure before clearing the socket */
                        time_t now_disc = time(NULL);
                        struct tm *t_disc = localtime(&now_disc);
                        char time_disc[64];
                        strftime(time_disc, sizeof(time_disc), "%Y-%m-%d %H:%M:%S", t_disc);
                        printf("\033[1;35m[DISCONNECT]\033[0m Captain \033[1;37m%-15s\033[0m has left the galaxy.    [\033[1;33m%s\033[0m]\n", 
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

                /* Find player index if already logged in - MOVED AFTER HANDSHAKE CHECK */
                int p_idx = -1;

                if (type == PKT_HANDSHAKE) {
                    LOG_DEBUG("Handshake request received from FD %d\n", fd);
                    PacketHandshake h_pkt;
                    h_pkt.type = type;
                    int r_hand = read_all(fd, ((char*)&h_pkt) + sizeof(int), sizeof(PacketHandshake) - sizeof(int));
                    if (r_hand > 0) {
                        LOG_DEBUG("Handshake data read successfully (%d bytes)\n", r_hand);
                        /* First: Security verification WITHOUT locking game_mutex */
                        uint8_t sig[32];
                        for(int k=0; k<32; k++) sig[k] = h_pkt.pubkey[32+k] ^ MASTER_SESSION_KEY[k];
                        
                        if (memcmp(sig, HANDSHAKE_MAGIC_STRING, 32) != 0) {
                            fprintf(stderr, "\033[1;31m[SECURITY ALERT]\033[0m Handshake integrity failure on FD %d. Invalid Master Key.\n", fd);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                            close(fd);
                            continue;
                        }

                        LOG_DEBUG("Handshake signature verified. Attempting to lock game_mutex...\n");
                        /* Second: Now lock only to assign slot and key */
                        pthread_mutex_lock(&game_mutex);
                        LOG_DEBUG("game_mutex ACQUIRED for FD %d\n", fd);
                        int slot = -1;
                        for(int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == fd) { slot = i; break; }
                        if (slot == -1) {
                            for(int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == 0) { 
                                slot = i; 
                                players[i].socket = fd; 
                                players[i].active = 0; 
                                break; 
                            }
                        }
                        
                        if (slot != -1) {
                            for(int k=0; k<32; k++) {
                                players[slot].session_key[k] = h_pkt.pubkey[k] ^ MASTER_SESSION_KEY[k];
                            }
                            LOG_DEBUG("Secure Session Key negotiated for Client FD %d (Slot %d)\n", fd, slot);
                            int ack_type = PKT_HANDSHAKE;
                            write_all(fd, &ack_type, sizeof(int));
                            /* Deliver the stable galaxy verification key, bound to this
                             * session: the client XORs it back with the session key it
                             * generated. This lets the client verify the galaxy state
                             * HMAC-SHA256 signature even after its local key rotated. */
                            uint8_t verify_xt[32];
                            for(int k=0; k<32; k++) {
                                verify_xt[k] = GALAXY_VERIFY_KEY[k] ^ players[slot].session_key[k];
                            }
                            write_all(fd, verify_xt, sizeof(verify_xt));
                            memset(verify_xt, 0, sizeof(verify_xt));
                            LOG_DEBUG("Handshake ACK sent to FD %d (with galaxy verify key)\n", fd);
                        } else {
                            fprintf(stderr, "\033[1;33m[WARNING]\033[0m Connection rejected: Server full (FD %d).\n", fd);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                            close(fd);
                        }
                        pthread_mutex_unlock(&game_mutex);
                        LOG_DEBUG("game_mutex RELEASED for FD %d\n", fd);
                    } else {
                        LOG_DEBUG("Handshake read_all failed or partial: %d\n", r_hand);
                    }
                } else {
                    /* For all other packets, we need the player index and we need the mutex */
                    pthread_mutex_lock(&game_mutex);
                    for (int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == fd && players[i].active) { p_idx = i; break; }
                    pthread_mutex_unlock(&game_mutex);

                    /* MAIN PACKET DISPATCHER */
                    if (type == PKT_QUERY_KEY) {
                        PacketQueryKey qk;
                        if (read_all(fd, ((char*)&qk) + sizeof(int), sizeof(PacketQueryKey) - sizeof(int)) > 0) {
                            pthread_mutex_lock(&game_mutex);
                            qk.found = 0;
                            qk.type = PKT_QUERY_KEY;
                            for(int j=0; j<MAX_CLIENTS; j++) {
                                if (players[j].active && players[j].name[0] != '\0' && strcmp(players[j].name, qk.target_name) == 0) {
                                    memcpy(qk.x25519_pubkey, players[j].x25519_pubkey, 32);
                                    qk.found = 1; 
                                    LOG_DEBUG("Tactical Link: Found %s. Key starts with: %02X%02X%02X%02X\n", 
                                              qk.target_name, qk.x25519_pubkey[0], qk.x25519_pubkey[1], 
                                              qk.x25519_pubkey[2], qk.x25519_pubkey[3]);
                                    break;
                                }
                            }
                            pthread_mutex_unlock(&game_mutex);
                            write_all(fd, &qk, sizeof(PacketQueryKey));
                        }
                    } else if (type == PKT_QUERY || type == PKT_LOGIN) {
                        /* ... login logic remains here ... */

                        PacketLogin pkt;
                        if (read_all(fd, ((char*)&pkt) + sizeof(int), sizeof(PacketLogin) - sizeof(int)) > 0) {
                            /* 3.4 — Reject names that could cause path traversal.
                             * Must be checked before any file I/O uses pkt.name. */
                            pkt.name[sizeof(pkt.name) - 1] = '\0';
                            if (!sanitize_captain_name(pkt.name)) {
                                /* Unsafe captain name (path injection attempt):
                                   refuse the connection before any file I/O.
                                   Note: game_mutex is NOT held here. */
                                LOG_DEBUG("SECURITY: rejected login with unsafe name\n");
                                fprintf(stderr, "\033[1;33m[SECURITY]\033[0m Rejected login: unsafe captain name on FD %d (connection closed).\n", fd);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                                close(fd);
                                /* Reap the slot reserved by the handshake so that
                                   repeated rejects cannot exhaust the server (DoS). */
                                pthread_mutex_lock(&game_mutex);
                                for (int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == fd) {
                                    players[i].socket = 0;
                                    players[i].active = 0;
                                    memset(players[i].session_key, 0, 32);
                                    break;
                                }
                                pthread_mutex_unlock(&game_mutex);
                                break;
                            }
                            if (type == PKT_QUERY) {
                                pthread_mutex_lock(&game_mutex);
                                int status = 1; /* 1:Success (Known), 2:New, 3:WrongPass, 4:Duplicate */
                                uint8_t stored_salt[16] = {0}; /* Salted-scheme reply payload (status 5) */


                                /* 1. Check for Duplicate Name (already active session) */
                                for(int j=0; j<MAX_CLIENTS; j++) {
                                    if (players[j].active && players[j].name[0] != '\0' && strcmp(players[j].name, pkt.name) == 0) {
                                        status = 4;
                                        break;
                                    }
                                }

                                if (status != 4) {
                                    /* 2. Check for existence and password.
                                     * Identity scheme v2 (per-user salt):
                                     *   identity.hash = HMAC-SHA256(master, "SPACEGL-ID-V2" || name || 0 || salt || 0 || password)
                                     *   identity.salt = random 16-byte per-captain salt (issued at enrollment)
                                     * Legacy unsalted identity.hash files remain verifiable and are
                                     * transparently migrated to the salted scheme on first successful
                                     * login (the client proved the password and supplied a fresh salt). */
                                    char auth_path[256];
                                    char salt_path[256];
                                    sprintf(auth_path, "captains/%s/identity.hash", pkt.name);
                                    sprintf(salt_path, "captains/%s/identity.salt", pkt.name);

                                    bool have_stored_salt = false;
                                    bool auth_ok = false;

                                    /* A zero salt is the client's "I do not hold a salt for
                                       this account" probe. The server answers with status 5
                                       plus the salt to use (stored salt, or zeros, which tell
                                       a fresh client to generate its own random salt). */
                                    bool client_salt_zero = true;
                                    for(int b=0; b<16; b++) if (pkt.salt[b] != 0) { client_salt_zero = false; break; }

                                    FILE *fa = fopen(auth_path, "rb");
                                    if (fa) {
                                        uint8_t stored_hash[32] = {0};
                                        bool have_stored_hash = (fread(stored_hash, 1, 32, fa) == 32);
                                        fclose(fa);

                                        FILE *fs = fopen(salt_path, "rb");
                                        if (fs) {
                                            if (fread(stored_salt, 1, 16, fs) == 16) have_stored_salt = true;
                                            fclose(fs);
                                        }

                                        if (have_stored_salt) {
                                            /* Salted (v2) account */
                                            if (client_salt_zero) {
                                                status = PKT_ID_STATUS_SALT_REQUIRED; /* reply carries the stored salt */
                                            } else if (memcmp(pkt.salt, stored_salt, 16) != 0) {
                                                status = 3; /* Wrong salt: verification fails */
                                            } else {
                                                auth_ok = have_stored_hash && (memcmp(stored_hash, pkt.pass_hash, 32) == 0);
                                            }
                                        } else {
                                            /* Legacy account (no salt on disk) */
                                            auth_ok = have_stored_hash &&
                                                (memcmp(stored_hash, pkt.pass_hash_legacy, 32) == 0 ||
                                                 memcmp(stored_hash, pkt.pass_hash, 32) == 0);
                                            if (auth_ok && client_salt_zero) {
                                                /* Password proved, but no salt supplied yet: ask for one
                                                   so the record can be migrated to the salted scheme. */
                                                status = PKT_ID_STATUS_SALT_REQUIRED; /* reply carries zeros */
                                                auth_ok = false;
                                            } else if (auth_ok) {
                                                /* Transparent migration of the stored record */
                                                FILE *fm = fopen(auth_path, "wb");
                                                if (fm) {
                                                    fwrite(pkt.pass_hash, 1, 32, fm);
                                                    if (fchmod(fileno(fm), 0600) != 0) { /* best effort */ }
                                                    fclose(fm);
                                                }
                                                FILE *fsm = fopen(salt_path, "wb");
                                                if (fsm) {
                                                    fwrite(pkt.salt, 1, 16, fsm);
                                                    if (fchmod(fileno(fsm), 0600) != 0) { /* best effort */ }
                                                    fclose(fsm);
                                                }
                                            }
                                        }

                                        if (status != 3 && status != PKT_ID_STATUS_SALT_REQUIRED) {
                                            if (!auth_ok) {
                                                status = 3; /* Wrong Password */
                                            } else {
                                                /* Password correct: check if the commander is in the current galaxy persistent state */
                                                bool found_in_galaxy = false;
                                                for(int j=0; j<MAX_CLIENTS; j++) {
                                                    if (players[j].name[0] != '\0' && strcmp(players[j].name, pkt.name) == 0) {
                                                        found_in_galaxy = true;
                                                        break;
                                                    }
                                                }
                                                if (found_in_galaxy) status = 1; /* Success (Known) */
                                                else status = 2; /* New Recruit (Identity exists, but Galaxy was reset) */
                                            }
                                        }
                                    } else if (client_salt_zero) {
                                        /* New Captain, salt probe: do NOT create the account yet.
                                           Reply with status 5 + zeros; the client picks a random
                                           salt and re-queries, which creates the record. */
                                        status = PKT_ID_STATUS_SALT_REQUIRED;
                                    } else {
                                        /* New Captain (salt supplied): create directory and
                                           save salted hash + salt */
                                        char dir_path[128];
                                        sprintf(dir_path, "captains/%s", pkt.name);
                                        mkdir("captains", 0700);
                                        mkdir(dir_path, 0700);
                                        fa = fopen(auth_path, "wb");
                                        if (fa) {
                                            fwrite(pkt.pass_hash, 1, 32, fa);
                                            if (fchmod(fileno(fa), 0600) != 0) { /* best effort */ }
                                            fclose(fa);
                                        }
                                        FILE *fsm = fopen(salt_path, "wb");
                                        if (fsm) {
                                            fwrite(pkt.salt, 1, 16, fsm);
                                            if (fchmod(fileno(fsm), 0600) != 0) { /* best effort */ }
                                            fclose(fsm);
                                        }
                                        status = 2; /* New Captain */
                                    }
                                }

                                pthread_mutex_unlock(&game_mutex);
                                LOG_DEBUG("Security Check for '%s': Status %d\n", pkt.name, status);
                                write_all(fd, &status, sizeof(int));
                                if (status == PKT_ID_STATUS_SALT_REQUIRED) {
                                    /* Disclose the stored salt so the client can recompute the
                                     * salted identity hash (one extra round-trip; the salt is
                                     * public-parameters material, like a database salt). */
                                    write_all(fd, stored_salt, 16);
                                }
                            } else {
                                /* PKT_LOGIN: Re-using the same packet read for login */
                                pthread_mutex_lock(&game_mutex);
                                int slot = -1;
                                /* 1. Try to find a player with the same name (persistence) */
                                for(int j=0; j<MAX_CLIENTS; j++) { 
                                    if (players[j].name[0] != '\0' && strcmp(players[j].name, pkt.name) == 0) { 
                                        slot = j; 
                                        break; 
                                    } 
                                }
                                
                                /* 2. If not found, find a TRULY empty slot (no name) */
                                if (slot == -1) {
                                    for(int j=0; j<MAX_CLIENTS; j++) {
                                        if (players[j].name[0] == '\0') {
                                            slot = j;
                                            break;
                                        }
                                    }
                                }

                                /* 3. Fallback: if server is full of named players, reuse an inactive slot (socket == 0)
                                   but we MUST clear it first so it's treated as a new player. */
                                if (slot == -1) {
                                    for(int j=0; j<MAX_CLIENTS; j++) {
                                        if (players[j].socket == 0) {
                                            slot = j;
                                            memset(players[slot].name, 0, 64); /* Force is_new = true */
                                            break;
                                        }
                                    }
                                }
                                
                                if (slot != -1) {
                                    /* Handle Session Key transfer from the temporary handshake slot if needed */
                                    int handshake_slot = -1;
                                    for(int j=0; j<MAX_CLIENTS; j++) if (players[j].socket == fd) { handshake_slot = j; break; }

                                    if (handshake_slot != -1 && handshake_slot != slot) {
                                        memcpy(players[slot].session_key, players[handshake_slot].session_key, 32);
                                        /* If the temporary slot was just for handshake, clear it */
                                        if (players[handshake_slot].name[0] == '\0') {
                                            players[handshake_slot].socket = 0;
                                        }
                                    }

                                    players[slot].socket = fd;
                                    int is_new = (players[slot].name[0] == '\0');
                                    players[slot].active = 0; /* Block updates during sync */

                                    /* Always update keys on login, but only update faction/class for new players */
                                    memcpy(players[slot].x25519_pubkey, pkt.x25519_pubkey, 32);
                                    if (is_new) {
                                        players[slot].faction = pkt.faction;
                                        players[slot].ship_class = pkt.ship_class;
                                        strcpy(players[slot].name, pkt.name);
                                        
                                        /* Notify the fleet of the new X25519 public key */
                                        char key_info[256];
                                        sprintf(key_info, "[IDENTITY] Public Frequency for Captain %s: ", pkt.name);
                                        for(int k=0; k<8; k++) sprintf(key_info + strlen(key_info), "%02X", pkt.x25519_pubkey[k]);
                                        strcat(key_info, "... [UPLINK ACTIVE]");
                                        send_server_msg(-1, "COMPUTER", key_info);
                                        players[slot].state.energy = MAX_ENERGY_CAPACITY;
                                        players[slot].state.torpedoes = MAX_TORPEDO_CAPACITY;
                                        int crew = (MAX_CREW_EXPLORER / 5);
                                        switch(pkt.ship_class) {
                                            case SHIP_CLASS_EXPLORER:    crew = MAX_CREW_EXPLORER; break;
                                            case SHIP_CLASS_FLAGSHIP:    crew = 850; break;
                                            case SHIP_CLASS_LEGACY:      crew = 430; break;
                                            case SHIP_CLASS_HEAVY_CRUISER: crew = 750; break;
                                            case SHIP_CLASS_ESCORT:      crew = (MAX_CREW_EXPLORER / 20); break;
                                            case SHIP_CLASS_SCIENCE:     crew = (MAX_CREW_EXPLORER / 7); break;
                                            case SHIP_CLASS_RESEARCH:    crew = 80; break;
                                            case SHIP_CLASS_SCOUT:       crew = (MAX_CREW_EXPLORER / 33); break;
                                            case SHIP_CLASS_MULTI_ENGINE: crew = (MAX_CREW_EXPLORER / 2); break;
                                            case SHIP_CLASS_CARRIER:     crew = 1200; break;
                                            case SHIP_CLASS_TACTICAL:    crew = 800; break;
                                            case SHIP_CLASS_DIPLOMATIC:  crew = (MAX_CREW_EXPLORER / 3); break;
                                            case SHIP_CLASS_FRIGATE:     crew = 250; break;
                                            case SHIP_CLASS_SENTINEL:    crew = 950; break;
                                            default: crew = (MAX_CREW_EXPLORER / 5); break;
                                        }
                                        players[slot].state.crew_count = crew;
                                        players[slot].state.q1 = rand()%GALAXY_SIZE + 1;
                                        players[slot].state.q2 = rand()%GALAXY_SIZE + 1;
                                        players[slot].state.q3 = rand()%GALAXY_SIZE + 1;
                                        players[slot].state.s1 = (QUADRANT_SIZE / 2.0);
                                        players[slot].state.s2 = (QUADRANT_SIZE / 2.0);
                                        players[slot].state.s3 = (QUADRANT_SIZE / 2.0);
                                                                            
                                        /* Initialize Absolute Galactic Coordinates */
                                        players[slot].gx = (players[slot].state.q1 - 1) * QUADRANT_SIZE + players[slot].state.s1;
                                        players[slot].gy = (players[slot].state.q2 - 1) * QUADRANT_SIZE + players[slot].state.s2;
                                        players[slot].gz = (players[slot].state.q3 - 1) * QUADRANT_SIZE + players[slot].state.s3;
                                        
                                        players[slot].state.inventory[1] = 1000000ULL; /* Initial Aetherium for jumps */
                                                                            
                                        /* Default Balanced Power Distribution */
                                        players[slot].state.power_dist[0] = 0.333; /* Engines */
                                        players[slot].state.power_dist[1] = 0.334; /* Shields */
                                        players[slot].state.power_dist[2] = 0.333; /* Weapons */

                                        for (int s = 0; s < 6; s++) {
                                            players[slot].state.shields[s] = SHIELD_MAX_STRENGTH;
                                            players[slot].state.target_shields[s] = SHIELD_MAX_STRENGTH;
                                        }
                                        players[slot].state.shield_change_timer = 0;
                                        players[slot].state.shield_change_rate = 0.0f;

                                        players[slot].state.hull_integrity = (float)YIELD_HARVEST_MAX;
                                        for (int s = 0; s < MAX_SYSTEMS; s++) {
                                            players[slot].state.system_health[s] = (float)YIELD_HARVEST_MAX;
                                        }
                                        players[slot].state.life_support = (float)YIELD_HARVEST_MAX;
                                        players[slot].state.ion_beam_charge = (float)YIELD_HARVEST_MAX;
                                        memset(players[slot].state.probes, 0, sizeof(players[slot].state.probes));
                                    } else {
                                        /* RETURNING CAPTAIN: sync name to the game state for visual consistency */
                                        strcpy(players[slot].state.captain_name, players[slot].name);
                                        
                                        /* Default Power if zero (old accounts or corruption) */
                                        double p_total = players[slot].state.power_dist[0] + players[slot].state.power_dist[1] + players[slot].state.power_dist[2];
                                        if (p_total < 0.01) {
                                            players[slot].state.power_dist[0] = 0.333;
                                            players[slot].state.power_dist[1] = 0.334;
                                            players[slot].state.power_dist[2] = 0.333;
                                        }
                                    }
                                    
                                    /* WELCOME PACKAGE: Ensure all captains (new or returning) have at least 10 Aetherium for Jumps */
                                    if (players[slot].state.inventory[1] < COST_ACTION_LOW) {
                                        players[slot].state.inventory[1] = COST_ACTION_LOW;
                                    }

                                    /* SESSION INITIALIZATION: Reset transient event flags and force full sync */
                                    players[slot].renegade_timer = 0;
                                    players[slot].radio_lock_target = 0;
                                    players[slot].jump_type = 2;
                                    
                                    /* Derive Personal Algorithm keys for this Captain */
                                    derive_algo_keys(MASTER_SESSION_KEY, players[slot].name, players[slot].algo_keys);
                                    
                                    players[slot].state.beam_count = 0;
                                    players[slot].state.event_count = 0;
                                    players[slot].torp_active = false;
                                    players[slot].full_update_timer = (5 * GAME_TICK_RATE + 1); /* Force UPD_FULL on next network pulse */
                                    memset(&players[slot].last_sent_state, 0, sizeof(PacketUpdate));
                                    
                                    /* FORCE COORDINATE SYNC: Ensure HUD and Viewer align immediately */
                                    players[slot].state.q1 = get_q_from_g(players[slot].gx);
                                    players[slot].state.q2 = get_q_from_g(players[slot].gy);
                                    players[slot].state.q3 = get_q_from_g(players[slot].gz);
                                    players[slot].state.s1 = players[slot].gx - (players[slot].state.q1 - 1) * QUADRANT_SIZE;
                                    players[slot].state.s2 = players[slot].gy - (players[slot].state.q2 - 1) * QUADRANT_SIZE;
                                    players[slot].state.s3 = players[slot].gz - (players[slot].state.q3 - 1) * QUADRANT_SIZE;

                                    players[slot].crypto_algo = CRYPTO_NONE; 
                                    /* Delegate the giant Galaxy Master transmission to the Thread Pool */
                                    SyncTask *stask = malloc(sizeof(SyncTask));
                                    if (stask) {
                                        stask->slot = slot;
                                        stask->fd = fd;
                                        players[slot].generation++;
                                        stask->generation = players[slot].generation;
                                        stask->is_new = is_new;
                                        if (threadpool_add_task(g_pool, sync_client_task, stask) != 0) {
                                            /* Fallback if pool fails: sync synchronously */
                                            sync_client_task(stask);
                                        }
                                    }
                                    pthread_mutex_unlock(&game_mutex);
                                } else {
                                    pthread_mutex_unlock(&game_mutex);
                                }
                            }
                        }
                    } else if (type == PKT_COMMAND || type == PKT_MESSAGE) {
                        /* NOTE: the payload MUST always be consumed, even when the
                           player is not active yet (sync in flight), otherwise the
                           socket stream desynchronizes. Commands/messages arriving
                           in that narrow window are dropped without desync. */
                        if (type == PKT_COMMAND) {
                            PacketCommand pkt;
                            if (read_all(fd, ((char*)&pkt) + sizeof(int), sizeof(PacketCommand) - sizeof(int)) > 0) {
                                if (p_idx != -1 && process_command(p_idx, pkt.cmd)) {
                                    /* Profile was deleted (zztop), drop connection and
                                       reap the slot so repeated zztops cannot exhaust
                                       the server (DoS). game_mutex is NOT held here. */
                                    pthread_mutex_lock(&game_mutex);
                                    for (int i=0; i<MAX_CLIENTS; i++) if (players[i].socket == fd) {
                                        players[i].socket = 0;
                                        players[i].active = 0;
                                        memset(players[i].session_key, 0, 32);
                                        break;
                                    }
                                    pthread_mutex_unlock(&game_mutex);
                                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                                    close(fd);
                                    LOG_DEBUG("Connection dropped after zztop: FD %d\n", fd);
                                }
                            }
                        } else { /* PKT_MESSAGE */
                            PacketMessage *pkt = malloc(sizeof(PacketMessage));
                            if (pkt && read_all(fd, ((char*)pkt) + sizeof(int), offsetof(PacketMessage, text) - sizeof(int)) > 0) {
                                if (pkt->length > 0 && pkt->length < 65536) read_all(fd, pkt->text, pkt->length);
                                else pkt->text[0] = '\0';
                                pkt->type = type;

                                if (p_idx != -1) {
                                    extern void broadcast_task(void *arg);
                                    if (g_pool) threadpool_add_task(g_pool, broadcast_task, pkt);
                                    else { broadcast_message(pkt); free(pkt); }
                                } else {
                                    free(pkt); /* player not active yet: drop cleanly */
                                }
                            } else if (pkt) free(pkt);
                        }
                    } else {
                        /* Unknown/unsupported packet type: close the connection
                           (we cannot resynchronize the stream). */
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                        close(fd);
                    }
                }
            }
        }
    }
    return 0;
}
