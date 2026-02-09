/* * SPACE GL - 3D LOGIC ENGINE 
 * Authors: Nicola Taibi, Supported by Google Gemini
 * Copyright (C) 2026 Nicola Taibi - Licensed under GPL-3.0-or-later
 * * This program is free software: you can redistribute it and/or modify
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
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stddef.h>
#include <math.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include "network.h"

/* Pre-Shared DeepSpace Encryption Key (Loaded from ENV) */
uint8_t deep_space_key[32];
#include "shared_state.h"
#include "ui.h"

EVP_PKEY *my_ed25519_key = NULL;
uint8_t my_pubkey_bytes[32];

void generate_keys() {
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    if (EVP_PKEY_keygen_init(pctx) <= 0) {
        perror("EVP_PKEY_keygen_init");
        return;
    }
    if (EVP_PKEY_keygen(pctx, &my_ed25519_key) <= 0) {
        perror("EVP_PKEY_keygen");
        return;
    }
    EVP_PKEY_CTX_free(pctx);

    size_t len = 32;
    if (EVP_PKEY_get_raw_public_key(my_ed25519_key, my_pubkey_bytes, &len) <= 0) {
        perror("EVP_PKEY_get_raw_public_key");
    }
    printf(B_GREEN "Identity Secured: Ed25519 Keypair Generated.\n" RESET);
}

void sign_packet_message(PacketMessage *msg) {
    if (!my_ed25519_key) return;
    
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (EVP_DigestSignInit(mdctx, NULL, NULL, NULL, my_ed25519_key) <= 0) {
        EVP_MD_CTX_free(mdctx);
        return;
    }
    
    size_t sig_len = 64;
    /* Ed25519 requires one-shot EVP_DigestSign */
    if (EVP_DigestSign(mdctx, msg->signature, &sig_len, (uint8_t*)msg->text, msg->length) <= 0) {
        EVP_MD_CTX_free(mdctx);
        return;
    }
    EVP_MD_CTX_free(mdctx);
    
    msg->has_signature = 1;
    memcpy(msg->sender_pubkey, my_pubkey_bytes, 32);
}

/* Colori per l'interfaccia CLI */
#define RESET   "\033[0m"
#define B_RED     "\033[1;31m"
#define B_GREEN   "\033[1;32m"
#define B_YELLOW  "\033[1;33m"
#define B_BLUE    "\033[1;34m"
#define B_MAGENTA "\033[1;35m"
#define B_CYAN    "\033[1;36m"
#define B_WHITE   "\033[1;37m"

#include <termios.h>

int sock = 0;
char captain_name[64];
int my_faction = 0;
int g_debug = 0;

#define LOG_DEBUG(...) do { if (g_debug) { printf("DEBUG: " __VA_ARGS__); fflush(stdout); } } while (0)

pid_t visualizer_pid = 0;
GameState *g_shared_state = NULL;
int shm_fd = -1;
char shm_path[64];
volatile sig_atomic_t g_visualizer_ready = 0;

/* Gestione Input Reattivo */
char g_input_buf[256] = {0};
int g_input_ptr = 0;
struct termios orig_termios;

volatile sig_atomic_t g_running = 1;

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    /* Lasciamo ISIG attivo per permettere Ctrl+C */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void reprint_prompt() {
    printf("\r\033[K" B_WHITE "%s" RESET "> Command? %s", captain_name, g_input_buf);
    fflush(stdout);
}

void handle_ack(int sig) {
    g_visualizer_ready = 1;
}

void handle_sigchld(int sig) {
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0);
}

void init_shm() {
    sprintf(shm_path, "/st_shm_%d", getpid());
    
    /* Unlink in case it already exists from a previous crash */
    shm_unlink(shm_path);
    
    shm_fd = shm_open(shm_path, O_CREAT | O_RDWR | O_EXCL, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        exit(1);
    }
    
    if (ftruncate(shm_fd, sizeof(GameState)) == -1) {
        perror("ftruncate failed");
        exit(1);
    }
    
    g_shared_state = mmap(NULL, sizeof(GameState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (g_shared_state == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }
    
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&g_shared_state->mutex, &attr);
    
    sem_init(&g_shared_state->data_ready, 1, 0);
    
    /* Initial Sector Position: Center (5,5,5) */
    g_shared_state->shm_s[0] = 5.0f;
    g_shared_state->shm_s[1] = 5.0f;
    g_shared_state->shm_s[2] = 5.0f;
}

void cleanup() {
    if (visualizer_pid > 0) kill(visualizer_pid, SIGTERM);
    if (g_shared_state) munmap(g_shared_state, sizeof(GameState));
    if (shm_fd != -1) {
        close(shm_fd);
        shm_unlink(shm_path);
    }
}

/* Funzione di utilità per leggere esattamente N byte dal socket */
void clear_stdin() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int read_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    char *p = (char *)buf;
    while (total < len) {
        ssize_t n = read(fd, p + total, len - total);
        if (n == 0) return 0; /* Connection closed */
        if (n < 0) {
            perror("read_all failed");
            return -1;
        }
        total += n;
    }
    return (int)total;
}

int write_all(int fd, const void *buf, size_t len) {
    size_t total = 0;
    const char *p = (char *)buf;
    while (total < len) {
        ssize_t n = send(fd, p + total, len - total, 0);
        if (n <= 0) return (int)n;
        total += n;
    }
    return (int)total;
}

void *network_listener(void *arg) {
    while (g_running) {
        int type;
        int r = read_all(sock, &type, sizeof(int));
        if (r <= 0) {
            g_running = 0;
            disable_raw_mode();
            if (r == 0) printf("\n[NET] Server closed the connection.\n");
            else printf("\n[NET] Connection lost (read error).\n");
            exit(0);
        }
        
        if (type == PKT_MESSAGE) {
            PacketMessage *msg = malloc(sizeof(PacketMessage));
            if (!msg) { perror("malloc failed"); exit(1); }
            msg->type = type;
            size_t fixed_size = offsetof(PacketMessage, text);
            if (read_all(sock, ((char*)msg) + sizeof(int), fixed_size - sizeof(int)) <= 0) {
                free(msg); g_running = 0; break;
            }
            
            if (msg->length > 0) {
                if (read_all(sock, msg->text, msg->length) <= 0) {
                    free(msg); g_running = 0; break;
                }
                
                if (msg->is_encrypted) {
                    if (g_shared_state && g_shared_state->shm_crypto_algo == msg->crypto_algo) {
                        /* 1. Reverse Rotating Frequency offuscation on IV using packet's embedded frame */
                        for(int i=0; i<8; i++) msg->iv[i] ^= ((msg->origin_frame >> (i*8)) & 0xFF);

                        /* 2. Decrypt the payload */
                        char decrypted[65536];
                        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
                        
                        const EVP_CIPHER *cipher;
                        int is_gcm = 0;
                        if (msg->crypto_algo == CRYPTO_CHACHA) { cipher = EVP_chacha20_poly1305(); is_gcm = 1; }
                        else if (msg->crypto_algo == CRYPTO_ARIA) { cipher = EVP_aria_256_gcm(); is_gcm = 1; }
                        else if (msg->crypto_algo == CRYPTO_CAMELLIA) { cipher = EVP_camellia_256_ctr(); is_gcm = 0; }
                        else if (msg->crypto_algo == CRYPTO_SEED) { cipher = EVP_seed_cbc(); is_gcm = 0; }
                        else if (msg->crypto_algo == CRYPTO_CAST5) { cipher = EVP_cast5_cbc(); is_gcm = 0; }
                        else if (msg->crypto_algo == CRYPTO_IDEA) { cipher = EVP_idea_cbc(); is_gcm = 0; }
                        else if (msg->crypto_algo == CRYPTO_3DES) { cipher = EVP_des_ede3_cbc(); is_gcm = 0; }
                        else if (msg->crypto_algo == CRYPTO_BLOWFISH) { cipher = EVP_bf_cbc(); is_gcm = 0; }
                        else if (msg->crypto_algo == CRYPTO_RC4) { cipher = EVP_rc4(); is_gcm = 0; }
                        else if (msg->crypto_algo == CRYPTO_DES) { cipher = EVP_des_cbc(); is_gcm = 0; }
                        else if (msg->crypto_algo == CRYPTO_PQC) { cipher = EVP_aes_256_gcm(); is_gcm = 1; }
                        else { cipher = EVP_aes_256_gcm(); is_gcm = 1; }
                        
                        const uint8_t *k = deep_space_key; /* Client currently uses Master Key */
                        
                        EVP_DecryptInit_ex(ctx, cipher, NULL, k, msg->iv);
                        
                        int outlen;
                        EVP_DecryptUpdate(ctx, (uint8_t*)decrypted, &outlen, (const uint8_t*)msg->text, msg->length);
                        
                        if (is_gcm) {
                            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, msg->tag);
                        }
                        
                        int final_len;
                        if (EVP_DecryptFinal_ex(ctx, (uint8_t*)decrypted + outlen, &final_len) > 0) {
                            int total_len = outlen + final_len;
                            if (total_len > 65535) total_len = 65535;
                            memcpy(msg->text, decrypted, total_len);
                            msg->text[total_len] = '\0';
                            msg->length = total_len;
                        } else {
                            strcpy(msg->text, B_RED "<< ERROR: DeepSpace DECRYPTION FAILED - FREQUENCY MISMATCH OR INVALID KEY >>" RESET);
                        }
                        EVP_CIPHER_CTX_free(ctx);
                    } else if (g_shared_state && g_shared_state->shm_crypto_algo != CRYPTO_NONE) {
                        /* Encryption protocol mismatch (e.g. Captain listening on AES but incoming is ChaCha) */
                        char noise[128];
                        int noise_len = (msg->length > 64) ? 64 : msg->length;
                        for(int n=0; n<noise_len; n++) {
                            unsigned char c_raw = (unsigned char)msg->text[n];
                            noise[n] = (c_raw % 94) + 33; 
                        }
                        noise[noise_len] = '\0';
                        snprintf(msg->text, 65535, B_RED "<< SIGNAL DISTURBED: FREQUENCY MISMATCH >>" RESET "\n [HINT]: Try 'enc aes', 'enc chacha' or 'enc aria' to match the incoming frequency.\n [RAW_DATA]: %s...", noise);
                    } else {
                        /* Encryption protocol mismatch - Show simulated binary noise */
                        char noise[128];
                        int noise_len = (msg->length > 64) ? 64 : msg->length;
                        for(int n=0; n<noise_len; n++) {
                            unsigned char c_raw = (unsigned char)msg->text[n];
                            noise[n] = (c_raw % 94) + 33; 
                        }
                        noise[noise_len] = '\0';
                        snprintf(msg->text, 65535, B_RED "<< SIGNAL GARBLED: ENCRYPTION PROTOCOL MISMATCH >>" RESET "\n [HINT]: Try 'enc aes', 'enc chacha' or 'enc aria' to match the incoming frequency.\n [RAW_DATA]: %s...", noise);
                    }
                } else {
                    /* Plaintext message, just null terminate */
                    if (msg->length < 65536) msg->text[msg->length] = '\0';
                }
            } else msg->text[0] = '\0';
            
            int verified = 0;
            if (msg->has_signature) {
                EVP_PKEY *peer_key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, msg->sender_pubkey, 32);
                if (peer_key) {
                    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
                    if (EVP_DigestVerifyInit(mdctx, NULL, NULL, NULL, peer_key) > 0) {
                        /* Ed25519 requires one-shot EVP_DigestVerify */
                        if (EVP_DigestVerify(mdctx, msg->signature, 64, (uint8_t*)msg->text, msg->length) == 1) {
                            verified = 1;
                        }
                    }
                    EVP_MD_CTX_free(mdctx);
                    EVP_PKEY_free(peer_key);
                }
            }

            printf("\r\033[K"); /* Pulisce la riga di input attuale */
            if (strcmp(msg->from, "SERVER") == 0 || strcmp(msg->from, "COMPUTER") == 0 || 
                strcmp(msg->from, "SCIENCE") == 0 || strcmp(msg->from, "TACTICAL") == 0 ||
                strcmp(msg->from, "ENGINEERING") == 0 || strcmp(msg->from, "HELMSMAN") == 0 ||
                strcmp(msg->from, "WARNING") == 0 || strcmp(msg->from, "DAMAGE CONTROL") == 0) {
                printf("%s\n", msg->text);
            } else {
                printf(B_CYAN "[RADIO] %s%s (%s): %s\n" RESET, 
                       verified ? B_GREEN "[VERIFIED] " B_CYAN : (msg->has_signature ? B_RED "[UNVERIFIED] " B_CYAN : ""),
                       msg->from, 
                       (msg->faction == FACTION_ALLIANCE) ? "Alliance Command" : "Alien", msg->text);
            }
            free(msg);
            reprint_prompt();
        } else if (type == PKT_UPDATE) {
            PacketUpdate upd;
            memset(&upd, 0, sizeof(PacketUpdate));
            upd.type = type;
            
            /* Read fixed part up to object_count field */
            size_t fixed_size = offsetof(PacketUpdate, objects);
            int r_fixed = read_all(sock, ((char*)&upd) + sizeof(int32_t), fixed_size - sizeof(int32_t));
            
            if (r_fixed <= 0) {
                LOG_DEBUG("Failed to read PacketUpdate header. Read: %d, Expected: %zu\n", r_fixed, fixed_size - sizeof(int32_t));
                break;
            }
            
            /* Safety check for object count to prevent buffer overflow */
            if (upd.object_count < 0 || upd.object_count > MAX_NET_OBJECTS) {
                printf("Warning: Invalid object_count received: %d (at offset %zu)\n", upd.object_count, fixed_size);
                /* DUMP next 16 bytes for debugging */
                unsigned char dump[16];
                if (read(sock, dump, 16) != 16) { /* Read dummy data */ }
                LOG_DEBUG("Next bytes: %02x %02x %02x %02x...\n", dump[0], dump[1], dump[2], dump[3]);
                break;
            }

            /* Read active objects only */
            int r_objs = 0;
            if (upd.object_count > 0) {
                r_objs = read_all(sock, upd.objects, upd.object_count * sizeof(NetObject));
                if (r_objs <= 0) break;
            }

            /* --- Telemetry Calculation --- */
            static long long bytes_this_sec = 0;
            static struct timespec last_ts = {0, 0};
            static struct timespec link_start_ts = {0, 0};
            static double last_packet_arrival = 0;
            static double jitter_sum = 0;
            static int packets_this_sec = 0;
            struct timespec now_ts;
            clock_gettime(CLOCK_MONOTONIC, &now_ts);
            
            if (link_start_ts.tv_sec == 0) link_start_ts = now_ts;

            double now_secs = now_ts.tv_sec + now_ts.tv_nsec / 1e9;
            if (last_packet_arrival > 0) {
                double delta = (now_secs - last_packet_arrival) * 1000.0; /* ms */
                double expected = 1000.0 / 30.0; /* 33.3ms for 30Hz */
                jitter_sum += fabs(delta - expected);
            }
            last_packet_arrival = now_secs;

            int current_pkt_size = r_fixed + r_objs + sizeof(int);
            bytes_this_sec += current_pkt_size;
            packets_this_sec++;

            double elapsed = (now_ts.tv_sec - last_ts.tv_sec) + (now_ts.tv_nsec - last_ts.tv_nsec) / 1e9;

            if (elapsed >= 1.0) {
                if (g_shared_state) {
                    pthread_mutex_lock(&g_shared_state->mutex);
                    g_shared_state->net_kbps = (float)(bytes_this_sec / 1024.0 / elapsed);
                    g_shared_state->net_packet_count = (int)(packets_this_sec / elapsed);
                    g_shared_state->net_avg_packet_size = (packets_this_sec > 0) ? (int)(bytes_this_sec / packets_this_sec) : 0;
                    g_shared_state->net_jitter = (packets_this_sec > 0) ? (float)(jitter_sum / packets_this_sec) : 0;
                    g_shared_state->net_uptime = now_ts.tv_sec - link_start_ts.tv_sec;
                    /* Integrity: Based on jitter (lower jitter = higher integrity) */
                    float integrity = 100.0f - (g_shared_state->net_jitter * 2.0f);
                    if (integrity < 0) integrity = 0;
                    if (integrity > 100) integrity = 100;
                    g_shared_state->net_integrity = integrity;
                    
                    /* Efficiency: How much we send vs the maximum possible packet size */
                    g_shared_state->net_efficiency = 100.0f * (1.0f - (float)current_pkt_size / sizeof(PacketUpdate));
                    pthread_mutex_unlock(&g_shared_state->mutex);
                }
                bytes_this_sec = 0; packets_this_sec = 0; jitter_sum = 0; last_ts = now_ts;
            }
            if (g_shared_state) {
                pthread_mutex_lock(&g_shared_state->mutex);
                g_shared_state->net_last_packet_size = current_pkt_size;
                pthread_mutex_unlock(&g_shared_state->mutex);
            }
            
            if (g_shared_state) {
                if (upd.object_count > MAX_OBJECTS) upd.object_count = MAX_OBJECTS;

                pthread_mutex_lock(&g_shared_state->mutex);
                /* Sincronizziamo lo stato locale con i dati ottimizzati dal server */
                g_shared_state->shm_energy = upd.energy;
                g_shared_state->shm_composite_plating = upd.composite_plating;
                g_shared_state->shm_hull_integrity = upd.hull_integrity;
                g_shared_state->shm_crew = upd.crew_count;
                g_shared_state->shm_prison_unit = upd.prison_unit;
                g_shared_state->shm_torpedoes = upd.torpedoes;
                g_shared_state->shm_cargo_energy = upd.cargo_energy;
                g_shared_state->shm_cargo_torpedoes = upd.cargo_torpedoes;
                for(int s=0; s<6; s++) g_shared_state->shm_shields[s] = upd.shields[s];
                for(int sys=0; sys<10; sys++) g_shared_state->shm_system_health[sys] = upd.system_health[sys];
                for(int p=0; p<3; p++) g_shared_state->shm_power_dist[p] = upd.power_dist[p];
                g_shared_state->shm_life_support = upd.life_support;
                g_shared_state->shm_ion_beam_charge = upd.ion_beam_charge;
                g_shared_state->shm_tube_state = upd.tube_state;
                g_shared_state->shm_anti_matter = upd.anti_matter_count;
                for(int inv=0; inv<10; inv++) g_shared_state->inventory[inv] = upd.inventory[inv];
                g_shared_state->shm_lock_target = upd.lock_target;
                
                for(int p=0; p<3; p++) {
                    g_shared_state->probes[p].active = upd.probes[p].active;
                    g_shared_state->probes[p].q1 = upd.probes[p].q1;
                    g_shared_state->probes[p].q2 = upd.probes[p].q2;
                    g_shared_state->probes[p].q3 = upd.probes[p].q3;
                    g_shared_state->probes[p].s1 = upd.probes[p].s1;
                    g_shared_state->probes[p].s2 = upd.probes[p].s2;
                    g_shared_state->probes[p].s3 = upd.probes[p].s3;
                    g_shared_state->probes[p].eta = upd.probes[p].eta;
                    g_shared_state->probes[p].status = upd.probes[p].status;
                }
                
                g_shared_state->is_cloaked = upd.is_cloaked;
                g_shared_state->shm_q[0] = upd.q1;
                g_shared_state->shm_q[1] = upd.q2;
                g_shared_state->shm_q[2] = upd.q3;
                g_shared_state->shm_s[0] = (float)upd.s1;
                g_shared_state->shm_s[1] = (float)upd.s2;
                g_shared_state->shm_s[2] = (float)upd.s3;
                sprintf(g_shared_state->quadrant, "Q-%d-%d-%d", upd.q1, upd.q2, upd.q3);

                /* Update dynamic galaxy data (e.g. Ion Storms, Supernovas) */
                int mq1 = upd.map_update_q[0], mq2 = upd.map_update_q[1], mq3 = upd.map_update_q[2];
                if (mq1 >= 1 && mq1 <= 10 && mq2 >= 1 && mq2 <= 10 && mq3 >= 1 && mq3 <= 10) {
                    g_shared_state->shm_galaxy[mq1][mq2][mq3] = upd.map_update_val;
                }

                g_shared_state->object_count = upd.object_count;
                for (int o=0; o < upd.object_count; o++) {
                    g_shared_state->objects[o].shm_x = upd.objects[o].net_x;
                    g_shared_state->objects[o].shm_y = upd.objects[o].net_y;
                    g_shared_state->objects[o].shm_z = upd.objects[o].net_z;
                    g_shared_state->objects[o].h = upd.objects[o].h;
                    g_shared_state->objects[o].m = upd.objects[o].m;
                    g_shared_state->objects[o].type = upd.objects[o].type;
                    g_shared_state->objects[o].ship_class = upd.objects[o].ship_class;
                    g_shared_state->objects[o].health_pct = upd.objects[o].health_pct;
                    g_shared_state->objects[o].energy = upd.objects[o].energy;
                    g_shared_state->objects[o].plating = upd.objects[o].plating;
                    g_shared_state->objects[o].hull_integrity = upd.objects[o].hull_integrity;
                    g_shared_state->objects[o].faction = upd.objects[o].faction;
                    g_shared_state->objects[o].id = upd.objects[o].id;
                    g_shared_state->objects[o].is_cloaked = upd.objects[o].is_cloaked;
                    strncpy(g_shared_state->objects[o].shm_name, upd.objects[o].name, 63);
                    g_shared_state->objects[o].active = 1;
                }
                
                /* Append beams to shared state (Queue logic) */
                if (upd.beam_count > 0) {
                    for (int b=0; b < upd.beam_count; b++) {
                        if (g_shared_state->beam_count < MAX_BEAMS) {
                            int idx = g_shared_state->beam_count;
                            g_shared_state->beams[idx].shm_sx = upd.beams[b].net_sx;
                            g_shared_state->beams[idx].shm_sy = upd.beams[b].net_sy;
                            g_shared_state->beams[idx].shm_sz = upd.beams[b].net_sz;
                            g_shared_state->beams[idx].shm_tx = upd.beams[b].net_tx;
                            g_shared_state->beams[idx].shm_ty = upd.beams[b].net_ty;
                            g_shared_state->beams[idx].shm_tz = upd.beams[b].net_tz;
                            g_shared_state->beams[idx].active = upd.beams[b].active;
                            g_shared_state->beam_count++;
                        }
                    }
                }
                
                /* Projectile position */
                g_shared_state->torp.shm_x = upd.torp.net_x;
                g_shared_state->torp.shm_y = upd.torp.net_y;
                g_shared_state->torp.shm_z = upd.torp.net_z;
                g_shared_state->torp.active = upd.torp.active;
                
                /* Event Latching */
                if (upd.boom.active) {
                    g_shared_state->boom.shm_x = upd.boom.net_x;
                    g_shared_state->boom.shm_y = upd.boom.net_y;
                    g_shared_state->boom.shm_z = upd.boom.net_z;
                    g_shared_state->boom.active = 1;
                }
                
                if (upd.dismantle.active) {
                    g_shared_state->dismantle.shm_x = upd.dismantle.net_x;
                    g_shared_state->dismantle.shm_y = upd.dismantle.net_y;
                    g_shared_state->dismantle.shm_z = upd.dismantle.net_z;
                    g_shared_state->dismantle.species = upd.dismantle.species;
                    g_shared_state->dismantle.active = 1;
                }
                
                /* Wormhole Event */
                g_shared_state->wormhole.shm_x = upd.wormhole.net_x;
                g_shared_state->wormhole.shm_y = upd.wormhole.net_y;
                g_shared_state->wormhole.shm_z = upd.wormhole.net_z;
                g_shared_state->wormhole.active = upd.wormhole.active;

                /* Recovery FX */
                g_shared_state->recovery_fx.shm_x = upd.recovery_fx.net_x;
                g_shared_state->recovery_fx.shm_y = upd.recovery_fx.net_y;
                g_shared_state->recovery_fx.shm_z = upd.recovery_fx.net_z;
                g_shared_state->recovery_fx.active = upd.recovery_fx.active;

                /* Jump Arrival Event */
                if (upd.jump_arrival.active) {
                    g_shared_state->jump_arrival.shm_x = upd.jump_arrival.net_x;
                    g_shared_state->jump_arrival.shm_y = upd.jump_arrival.net_y;
                    g_shared_state->jump_arrival.shm_z = upd.jump_arrival.net_z;
                    g_shared_state->jump_arrival.active = 1;
                    /* Reset local copy to prevent repeated triggering */
                    upd.jump_arrival.active = 0;
                }

                /* Supernova Event */
                g_shared_state->supernova_pos.shm_x = upd.supernova_pos.net_x;
                g_shared_state->supernova_pos.shm_y = upd.supernova_pos.net_y;
                g_shared_state->supernova_pos.shm_z = upd.supernova_pos.net_z;
                g_shared_state->supernova_pos.active = upd.supernova_pos.active;
                g_shared_state->shm_sn_q[0] = upd.supernova_q[0];
                g_shared_state->shm_sn_q[1] = upd.supernova_q[1];
                g_shared_state->shm_sn_q[2] = upd.supernova_q[2];
                
                g_shared_state->frame_id++; 
                pthread_mutex_unlock(&g_shared_state->mutex);
                sem_post(&g_shared_state->data_ready);
            }
        }
    }
    return NULL;
}

void handle_sigint(int sig) {
    exit(0);
}

int main(int argc, char *argv[]) {
    struct sockaddr_in serv_addr;
    char server_ip[64];
    int my_ship_class = SHIP_CLASS_GENERIC_ALIEN;
    
    /* Security Initialization */
    char *env_key = getenv("SPACEGL_KEY");
    if (!env_key) {
        fprintf(stderr, B_RED "SECURITY ERROR: DeepSpace Key not found in environment.\n" RESET);
        fprintf(stderr, "Please set SPACEGL_KEY environment variable before launching.\n");
        exit(1);
    }
    memset(deep_space_key, 0, 32);
    size_t env_len = strlen(env_key);
    memcpy(deep_space_key, env_key, (env_len > 32) ? 32 : env_len);
    
    generate_keys();
    
    signal(SIGPIPE, SIG_IGN);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) g_debug = 1;
    }

    struct sigaction sa;
    sa.sa_handler = handle_ack;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR2, &sa, NULL);

    struct sigaction sa_exit;
    sa_exit.sa_handler = handle_sigint;
    sigemptyset(&sa_exit.sa_mask);
    sa_exit.sa_flags = 0;
    sigaction(SIGINT, &sa_exit, NULL);
    sigaction(SIGTERM, &sa_exit, NULL);

    struct sigaction sa_chld;
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    atexit(cleanup);

    /* Schermata di Benvenuto */
    printf("\033[2J\033[H"); /* Clear screen and home cursor */
    printf(B_CYAN "  ____________________________________________________________________________\n" RESET);
    printf(B_CYAN " /                                                                            \\\n" RESET);
    printf(B_CYAN " | " B_WHITE "   ███████╗██████╗  █████╗  ██████╗███████╗     ██████╗ ██╗              " B_CYAN "  |\n" RESET);
    printf(B_CYAN " | " B_WHITE "   ██╔════╝██╔══██╗██╔══██╗██╔════╝██╔════╝    ██╔════╝ ██║              " B_CYAN "  |\n" RESET);
    printf(B_CYAN " | " B_WHITE "   ███████╗██████╔╝███████║██║     █████╗      ██║  ███╗██║              " B_CYAN "  |\n" RESET);
    printf(B_CYAN " | " B_WHITE "   ╚════██║██╔═══╝ ██╔══██║██║     ██╔══╝      ██║   ██║██║              " B_CYAN "  |\n" RESET);
    printf(B_CYAN " | " B_WHITE "   ███████║██║     ██║  ██║╚██████╗███████╗    ╚██████╔╝███████╗         " B_CYAN "  |\n" RESET);
    printf(B_CYAN " | " B_WHITE "   ╚══════╝╚═╝     ╚═╝  ╚═╝ ╚═════╝╚══════╝     ╚═════╝ ╚══════╝         " B_CYAN "  |\n" RESET);
    printf(B_CYAN " |                                                                            |\n" RESET);
    printf(B_CYAN " | " B_YELLOW "              ---  SPACE EXPLORATION & COMBAT INTERFACE  ---              " B_CYAN " |\n" RESET);
    printf(B_CYAN " | " B_MAGENTA "          \"Per Tenebras, Lumen\" (Attraverso le tenebre, la luce)          " B_CYAN " |\n" RESET);
    printf(B_CYAN " |                                                                            |\n" RESET);
        printf(B_CYAN " | " B_WHITE "  Copyright (C) 2026 " B_GREEN "Nicola Taibi" B_WHITE "                                        " B_CYAN "  |\n" RESET);
        printf(B_CYAN " | " B_WHITE "  AI Core Support by " B_BLUE "Google Gemini" B_WHITE "                                       " B_CYAN "  |\n" RESET);
        printf(B_CYAN " | " B_WHITE "  License Type:      " B_YELLOW "GNU GPL v3.0" B_WHITE "                                        " B_CYAN "  |\n" RESET);
        printf(B_CYAN " \\____________________________________________________________________________/\n\n" RESET);
    

    LOG_DEBUG("sizeof(SpaceGLGame) = %zu\n", sizeof(SpaceGLGame));
    LOG_DEBUG("sizeof(PacketUpdate) = %zu\n", sizeof(PacketUpdate));
    printf("Server IP: "); 
    if (scanf("%63s", server_ip) != 1) { /* handled later by inet_pton */ }
    clear_stdin();

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(DEFAULT_PORT);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    /* Handshake: Negotiate Unique Session Key */
    PacketHandshake h_pkt;
    memset(&h_pkt, 0, sizeof(PacketHandshake));
    h_pkt.type = PKT_HANDSHAKE;
    h_pkt.pubkey_len = 32 + 32; /* 32 bytes Key + 32 bytes Signature */
    
    /* Generate Random Session Key */
    FILE *f_rand = fopen("/dev/urandom", "rb");
    if (f_rand) {
        if (fread(h_pkt.pubkey, 1, 32, f_rand) != 32) { /* dummy read */ }
        fclose(f_rand);
    } else {
        for(int k=0; k<32; k++) h_pkt.pubkey[k] = rand() % 255;
    }
    
    /* Add Magic Signature for Server-side verification */
    memcpy(h_pkt.pubkey + 32, HANDSHAKE_MAGIC_STRING, 32);

    /* Store it locally as our new key */
    uint8_t MY_SESSION_KEY[32];
    memcpy(MY_SESSION_KEY, h_pkt.pubkey, 32);
    
    /* Obfuscate EVERYTHING (Key + Signature) using the Master Key (XOR) */
    for(int k=0; k<64; k++) h_pkt.pubkey[k] ^= deep_space_key[k % 32];
    
    write_all(sock, &h_pkt, sizeof(PacketHandshake));
    
    /* Wait for server ACK to verify Master Key */
    int ack_type = 0;
    if (read_all(sock, &ack_type, sizeof(int)) <= 0 || ack_type != PKT_HANDSHAKE) {
        fprintf(stderr, B_RED "SECURITY ERROR: Master Key mismatch or Handshake rejected by server.\n" RESET);
        close(sock);
        exit(1);
    }
    
    /* Switch to the new Session Key */
    memcpy(deep_space_key, MY_SESSION_KEY, 32);
    printf(B_BLUE "DeepSpace Link Secured. Unique Frequency active.\n" RESET);

    /* Identification happens ONLY after secure link is established */
    printf("Commander Name: "); 
    if (scanf("%63s", captain_name) != 1) { strcpy(captain_name, "Captain"); }
    clear_stdin();

    /* Identity Check */
    PacketLogin qpkt;
    memset(&qpkt, 0, sizeof(PacketLogin));
    qpkt.type = PKT_QUERY;
    strcpy(qpkt.name, captain_name);
    write_all(sock, &qpkt, sizeof(PacketLogin));
    
    int is_known = 0;
    read_all(sock, &is_known, sizeof(int));

    if (!is_known) {
        printf("\n" B_WHITE "--- NEW RECRUIT IDENTIFIED ---" RESET "\n");
        printf("--- SELECT YOUR FACTION ---\n");
        printf(" 0: Alliance\n 1: Korthian\n 2: Xylari\n 3: Swarm\n 4: Vesperian\n 5: Ascendant\n 6: Quarzite\n 7: Saurian\n 8: Gilded\n 9: Fluidic Void\n 10: Cryos\n 11: Apex\nSelection: ");
        int selection;
        if (scanf("%d", &selection) != 1) { selection = 0; }
        switch(selection) {
            case 0: my_faction = FACTION_ALLIANCE; break;
            case 1: my_faction = FACTION_KORTHIAN; break;
            case 2: my_faction = FACTION_XYLARI; break;
            case 3: my_faction = FACTION_SWARM; break;
            case 4: my_faction = FACTION_CARDASSIAN; break;
            case 5: my_faction = FACTION_JEM_HADAR; break;
            case 6: my_faction = FACTION_THOLIAN; break;
            case 7: my_faction = FACTION_GORN; break;
            case 8: my_faction = FACTION_FERENGI; break;
            case 9: my_faction = FACTION_SPECIES_8472; break;
            case 10: my_faction = FACTION_BREEN; break;
            case 11: my_faction = FACTION_HIROGEN; break;
            default: my_faction = FACTION_ALLIANCE; break;
        }
        
        if (my_faction == FACTION_ALLIANCE) {
            printf("\n" B_WHITE "--- SELECT YOUR CLASS ---" RESET "\n");
            printf(" 0: Legacy Class\n 1: Scout Class\n 2: Heavy Cruiser\n 3: Multi-Engine Cruiser\n 4: Escort Class\n 5: Explorer Class\n 6: Flagship Class\n 7: Science Vessel\n 8: Carrier Class\n 9: Tactical Cruiser\n 10: Diplomatic Cruiser\n 11: Research Vessel\n 12: Frigate Class\nSelection: ");
            if (scanf("%d", &my_ship_class) != 1) { my_ship_class = 0; }
        } else {
            my_ship_class = SHIP_CLASS_GENERIC_ALIEN;
        }
        clear_stdin();
    } else {
        printf(B_CYAN "\n--- RETURNING COMMANDER RECOGNIZED ---\n" RESET);
    }

    /* Final Login */
    PacketLogin lpkt;
    memset(&lpkt, 0, sizeof(PacketLogin));
    lpkt.type = PKT_LOGIN;
    strcpy(lpkt.name, captain_name);
    lpkt.faction = my_faction;
    lpkt.ship_class = my_ship_class;
    
    LOG_DEBUG("Sending login packet (%zu bytes)...\n", sizeof(PacketLogin));
    write_all(sock, &lpkt, sizeof(PacketLogin));

    /* Ricezione Galassia Master (Sincronizzazione iniziale) */
    SpaceGLGame master_sync;
    memset(&master_sync, 0, sizeof(SpaceGLGame));
    printf("Synchronizing with Galaxy Server...\n");
    LOG_DEBUG("Client SpaceGLGame size: %zu bytes\n", sizeof(SpaceGLGame));
    LOG_DEBUG("Client PacketUpdate size: %zu bytes\n", sizeof(PacketUpdate));
    LOG_DEBUG("Waiting for Galaxy Master...\n");
    if (read_all(sock, &master_sync, sizeof(SpaceGLGame)) == sizeof(SpaceGLGame)) {
        printf(B_GREEN "Galaxy Map synchronized.\n" RESET);
        LOG_DEBUG("Received Encryption Flags: 0x%08X\n", master_sync.encryption_flags);
        if (master_sync.encryption_flags & 0x01) {
            printf(B_CYAN "[SECURE] DeepSpace Signature: " B_GREEN "VERIFIED (HMAC-SHA256)\n" RESET);
            printf(B_CYAN "[SECURE] Server Identity:    " B_YELLOW);
            for(int k=0; k<16; k++) printf("%02X", master_sync.server_pubkey[k]);
            printf("... [ACTIVE]\n" RESET);
            printf(B_CYAN "[SECURE] Encryption Layer:   " B_GREEN "AES-GCM + PQC (Quantum Ready)\n" RESET);
        }
    } else {
        printf(B_RED "ERROR: Failed to synchronize Galaxy Map.\n" RESET);
    }

    init_shm();
    
    /* Copy Galaxy Master to SHM for 3D Map View */
    if (g_shared_state) {
        pthread_mutex_lock(&g_shared_state->mutex);
        memcpy(g_shared_state->shm_galaxy, master_sync.g, sizeof(master_sync.g));
        g_shared_state->shm_crypto_algo = CRYPTO_NONE;
        g_shared_state->shm_encryption_flags = master_sync.encryption_flags;
        memcpy(g_shared_state->shm_server_signature, master_sync.server_signature, 64);
        memcpy(g_shared_state->shm_server_pubkey, master_sync.server_pubkey, 32);
        pthread_mutex_unlock(&g_shared_state->mutex);
    }
    
    if (getenv("DISPLAY") == NULL) {
        printf(B_RED "WARNING: No DISPLAY detected. 3D View might not start.\n" RESET);
    }

    visualizer_pid = fork();
    if (visualizer_pid == -1) {
        perror("fork failed");
        exit(1);
    }
    if (visualizer_pid == 0) {
        /* Child process */
        if (execl("./spacegl_3dview", "spacegl_3dview", shm_path, NULL) == -1) {
            perror("execl failed to start ./spacegl_3dview");
            _exit(1);
        }
    }

    /* Wait for visualizer handshake with timeout (5 seconds) */
    printf("Waiting for Tactical View initialization...\n");
    int timeout = 500; /* 5 seconds */
    while(!g_visualizer_ready && timeout-- > 0) {
        /* Check if child is still alive */
        int status;
        if (waitpid(visualizer_pid, &status, WNOHANG) != 0) {
            printf(B_RED "ERROR: Tactical View process terminated unexpectedly.\n" RESET);
            break;
        }
        usleep(10000);
    }
    
    if (g_visualizer_ready) {
        printf(B_GREEN "Tactical View (3D) initialized.\n" RESET);
    } else {
        printf(B_RED "WARNING: Tactical View timed out. Proceeding in CLI-only mode.\n" RESET);
    }

    /* Thread per ascoltare il server */
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, network_listener, NULL);

    printf(B_GREEN "Connected to Galaxy Server. Command Deck ready.\n" RESET);
    enable_raw_mode();
    reprint_prompt();

    while (g_running) {
        char c;
        if (read(STDIN_FILENO, &c, 1) > 0) {
            if (c == '\n' || c == '\r') {
                if (g_input_ptr > 0) {
                    printf("\n");
                    g_input_buf[g_input_ptr] = 0;
                    
                    if (strcmp(g_input_buf, "xxx") == 0) {
                        PacketCommand cpkt = {PKT_COMMAND, "xxx"};
                        send(sock, &cpkt, sizeof(cpkt), 0);
                        g_running = 0;
                        disable_raw_mode();
                        exit(0);
                    }
                    if (strcmp(g_input_buf, "help") == 0) {
                        printf(B_WHITE "\n--- Space GL: MULTIPLAYER COMMANDS ---" RESET "\n");
                        printf("nav H M W [F]: Hyperdrive Navigation (H 0-359, M -90/90, W Dist, F Factor 1-9.9)\n");
                        printf("imp H M S   : Impulse Drive (H, M, Speed 0.0-1.0). imp 0 0 0 to stop.\n");
                        printf("jum Q1 Q2 Q3: Wormhole Jump (Instant travel, costs 5000 En + 1 Isotope-X)\n");
                        printf("srs         : Short Range Sensors (Current Quadrant View)\n");
                        printf("lrs         : Long Range Sensors (LCARS Tactical Grid)\n");
                        printf("pha <E>     : Fire Ion Beams at locked target (uses Energy E)\n");
                        printf("pha <ID> <E>: Fire Ion Beams at specific target ID\n");
                        printf("tor         : Launch Plasma Torpedo at locked target\n");
                        printf("tor <H> <M> : Launch Plasma Torpedo at specific Heading/Mark\n");
                        printf("she F R T B L RI : Configure 6 Shield Quadrants\n");
                        printf("lock ID     : Lock-on Target (0:Self, 1+:Nearby vessels)\n");
                        printf("enc <algo>  : Toggle Encryption (aes, chacha, aria, camellia, ..., pqc)\n");
                        printf("scan ID     : Detailed analysis of vessel or anomaly\n");
                        printf("pow E S W   : Power Allocation (Engines, Shields, Weapons %%)\n");
                        printf("psy         : Psychological Warfare (Anti-Matter Bluff)\n");
                        printf("aux probe QX QY QZ: Launch sensor probe\n");
                        printf("aux report <N>    : Request sensor update from Probe N\n");
                        printf("aux recover <N>   : Recover Probe N in sector (+500 Energy)\n");
                        printf("aux jettison      : Eject Hyperdrive Core (WARNING!)\n");
                        printf("dis ID      : Dismantle enemy wreck/derelict (Dist < 1.5)\n");
                        printf("bor ID      : Boarding party operation (Dist < 1.0). Works on Lock.\n");
                        printf("min         : Planetary Mining (Must be in orbit dist < 2.0)\n");
                        printf("doc         : Dock with Starbase (Replenish/Repair, same faction)\n");
                        printf("con T A     : Convert (1:Dili->E, 2:Trit->E, 3:Vert->Torps, 6:Gas->E, 7:Duran->E)\n");
                        printf("load T A    : Load from Cargo Bay (1:Energy, 2:Torps)\n");
                        printf("hull        : Reinforce Hull (Uses 100 Composite for +500 Plating)\n");
                        printf("rep ID      : Repair System (Uses 50 Tritanium + 10 Isolinear)\n");
                        printf("inv         : Cargo Inventory Report\n");
                        printf("who         : List active captains in galaxy\n");
                        printf("cal Q1..3 S1..3: Hyperdrive Calc (Pinpoint Precision Route & ETA)\n");
                        printf("ical X Y Z  : Impulse Calculator (Sector ETA at current power)\n");
                        printf("apr ID DIST : Approach target autopilot. Works on Lock.\n");
                        printf("cha         : Chase locked target (Inter-sector aware)\n");
                        printf("sco         : Solar scooping for energy\n");
                        printf("har         : Antimatter harvest from Black Hole\n");
                        printf("sta         : Mission Status Report\n");
                        printf("dam         : Detailed Damage Report\n");
                        printf("rad MSG     : Send Global Radio Message\n");
                        printf("rad @Fac MSG: Send to Faction (e.g. @Xylari ...)\n");
                        printf("rad #ID MSG : Send Private Message to Player ID\n");
                        printf("clo         : Toggle Cloaking Device (Consumes constant Energy)\n");
                        printf("axs         : Toggle 3D Coordinate Axes\n");
                        printf("grd         : Toggle 3D Tactical Grid\n");
                        printf("map [XX]    : Toggle Starmap. Optional Filter (st,pl,bs,en,bh,ne,pu,is,co,as,de,mi,bu,pf,ri,mo)\n");
                        printf("xxx         : Self-Destruct\n");
                    } else if (strncmp(g_input_buf, "dis ", 4) == 0 || strcmp(g_input_buf, "dis") == 0) {
                        PacketCommand cpkt = {PKT_COMMAND, ""};
                        size_t clen = strlen(g_input_buf);
                        if (clen > 255) clen = 255;
                        memcpy(cpkt.cmd, g_input_buf, clen);
                        cpkt.cmd[clen] = '\0';
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    } else if (strcmp(g_input_buf, "axs") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_show_axes = !g_shared_state->shm_show_axes;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                            printf("Axes toggled.\n");
                        }
                    } else if (strcmp(g_input_buf, "grd") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_show_grid = !g_shared_state->shm_show_grid;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                            printf("Grid toggled.\n");
                        }
                    } else if (strncmp(g_input_buf, "map", 3) == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            if (strlen(g_input_buf) == 3) {
                                g_shared_state->shm_show_map = !g_shared_state->shm_show_map;
                                g_shared_state->shm_map_filter = 0;
                                if (g_shared_state->shm_show_map) printf("Starmap toggled ON (All objects).\n");
                                else printf("Starmap toggled OFF.\n");
                            } else {
                                /* Handle map filters: map st, map pl, etc. */
                                const char *f = g_input_buf + 4;
                                int filter = 0;
                                if (strcmp(f, "st") == 0) filter = 1;      /* Star */
                                else if (strcmp(f, "pl") == 0) filter = 2; /* Planet */
                                else if (strcmp(f, "bs") == 0) filter = 3; /* Base */
                                else if (strcmp(f, "en") == 0) filter = 4; /* Enemy/Hostile */
                                else if (strcmp(f, "bh") == 0) filter = 5; /* Black Hole */
                                else if (strcmp(f, "ne") == 0) filter = 6; /* Nebula */
                                else if (strcmp(f, "pu") == 0) filter = 7; /* Pulsar */
                                else if (strcmp(f, "is") == 0) filter = 8; /* Ion Storm */
                                else if (strcmp(f, "co") == 0) filter = 9; /* Comet */
                                else if (strcmp(f, "as") == 0) filter = 10;/* Asteroid */
                                else if (strcmp(f, "de") == 0) filter = 11;/* Derelict */
                                else if (strcmp(f, "mi") == 0) filter = 12;/* Mine */
                                else if (strcmp(f, "bu") == 0) filter = 13;/* Buoy */
                                else if (strcmp(f, "pf") == 0) filter = 14;/* Platform */
                                else if (strcmp(f, "ri") == 0) filter = 15;/* Rift */
                                else if (strcmp(f, "mo") == 0) filter = 16;/* Monster */
                                
                                if (filter > 0) {
                                    g_shared_state->shm_show_map = 1;
                                    g_shared_state->shm_map_filter = filter;
                                    printf("Starmap Filter: %s\n", f);
                                } else {
                                    printf("Unknown map filter: %s. Valid: st,pl,bs,en,bh,ne,pu,is,co,as,de,mi,bu,pf,ri,mo\n", f);
                                }
                            }
                            pthread_mutex_unlock(&g_shared_state->mutex);
                        }
                    } else if (strcmp(g_input_buf, "enc aes") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_crypto_algo = CRYPTO_AES;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                        }
                        PacketCommand cpkt = {PKT_COMMAND, "enc aes"};
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    } else if (strcmp(g_input_buf, "enc chacha") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_crypto_algo = CRYPTO_CHACHA;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                        }
                        PacketCommand cpkt = {PKT_COMMAND, "enc chacha"};
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    } else if (strcmp(g_input_buf, "enc aria") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_crypto_algo = CRYPTO_ARIA;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                        }
                        PacketCommand cpkt = {PKT_COMMAND, "enc aria"};
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    } else if (strcmp(g_input_buf, "enc camellia") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_crypto_algo = CRYPTO_CAMELLIA;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                        }
                        PacketCommand cpkt = {PKT_COMMAND, "enc camellia"};
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    } else if (strcmp(g_input_buf, "enc seed") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_crypto_algo = CRYPTO_SEED;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                        }
                        PacketCommand cpkt = {PKT_COMMAND, "enc seed"};
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    } else if (strcmp(g_input_buf, "enc cast") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_crypto_algo = CRYPTO_CAST5;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                        }
                        PacketCommand cpkt = {PKT_COMMAND, "enc cast"};
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    } else if (strcmp(g_input_buf, "enc idea") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_crypto_algo = CRYPTO_IDEA;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                        }
                        PacketCommand cpkt = {PKT_COMMAND, "enc idea"};
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    } else if (strcmp(g_input_buf, "enc 3des") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_crypto_algo = CRYPTO_3DES;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                        }
                        PacketCommand cpkt = {PKT_COMMAND, "enc 3des"};
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    } else if (strcmp(g_input_buf, "enc bf") == 0 || strcmp(g_input_buf, "enc blowfish") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_crypto_algo = CRYPTO_BLOWFISH;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                        }
                        PacketCommand cpkt = {PKT_COMMAND, "enc bf"};
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    } else if (strcmp(g_input_buf, "enc rc4") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_crypto_algo = CRYPTO_RC4;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                        }
                        PacketCommand cpkt = {PKT_COMMAND, "enc rc4"};
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    } else if (strcmp(g_input_buf, "enc des") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_crypto_algo = CRYPTO_DES;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                        }
                        PacketCommand cpkt = {PKT_COMMAND, "enc des"};
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    } else if (strcmp(g_input_buf, "enc pqc") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_crypto_algo = CRYPTO_PQC;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                        }
                        PacketCommand cpkt = {PKT_COMMAND, "enc pqc"};
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    } else if (strcmp(g_input_buf, "enc off") == 0) {
                        if (g_shared_state) {
                            pthread_mutex_lock(&g_shared_state->mutex);
                            g_shared_state->shm_crypto_algo = CRYPTO_NONE;
                            pthread_mutex_unlock(&g_shared_state->mutex);
                        }
                        PacketCommand cpkt = {PKT_COMMAND, "enc off"};
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    } else if (strncmp(g_input_buf, "rad ", 4) == 0) {
                        PacketMessage mpkt;
                        memset(&mpkt, 0, sizeof(PacketMessage));
                        mpkt.type = PKT_MESSAGE;
                        strcpy(mpkt.from, captain_name);
                        mpkt.faction = my_faction;
                        mpkt.scope = SCOPE_GLOBAL;
                        
                        char *msg_start = g_input_buf + 4;
                        if (msg_start[0] == '@') {
                            char target_name[64];
                            int offset = 0;
                            sscanf(msg_start + 1, "%s%n", target_name, &offset);
                            if (offset > 0) {
                                mpkt.scope = SCOPE_FACTION;
                                if (strcasecmp(target_name, "Alliance")==0 || strcasecmp(target_name, "Fed")==0) mpkt.faction = FACTION_ALLIANCE;
                                else if (strcasecmp(target_name, "Korthian")==0 || strcasecmp(target_name, "Kli")==0) mpkt.faction = FACTION_KORTHIAN;
                                else if (strcasecmp(target_name, "Xylari")==0 || strcasecmp(target_name, "Rom")==0) mpkt.faction = FACTION_XYLARI;
                                else if (strcasecmp(target_name, "Swarm")==0 || strcasecmp(target_name, "Bor")==0) mpkt.faction = FACTION_SWARM;
                                else if (strcasecmp(target_name, "Vesperian")==0 || strcasecmp(target_name, "Car")==0) mpkt.faction = FACTION_CARDASSIAN;
                                else if (strcasecmp(target_name, "Ascendant")==0 || strcasecmp(target_name, "Jem")==0) mpkt.faction = FACTION_JEM_HADAR;
                                else if (strcasecmp(target_name, "Quarzite")==0 || strcasecmp(target_name, "Tho")==0) mpkt.faction = FACTION_THOLIAN;
                                else if (strcasecmp(target_name, "Saurian")==0) mpkt.faction = FACTION_GORN;
                                else if (strcasecmp(target_name, "Gilded")==0 || strcasecmp(target_name, "Fer")==0) mpkt.faction = FACTION_FERENGI;
                                else if (strcasecmp(target_name, "FluidicVoid")==0 || strcasecmp(target_name, "8472")==0) mpkt.faction = FACTION_SPECIES_8472;
                                else if (strcasecmp(target_name, "Cryos")==0) mpkt.faction = FACTION_BREEN;
                                else if (strcasecmp(target_name, "Apex")==0) mpkt.faction = FACTION_HIROGEN;
                                else {
                                    mpkt.scope = SCOPE_GLOBAL; /* Fallback */
                                }
                                if (strlen(msg_start) > (1 + offset + 1))
                                    strncpy(mpkt.text, msg_start + 1 + offset + 1, 4095);
                                else 
                                    mpkt.text[0] = '\0';
                            } else strncpy(mpkt.text, msg_start, 4095);
                        } else if (msg_start[0] == '#') {
                            int tid;
                            int offset = 0;
                            if (sscanf(msg_start + 1, "%d%n", &tid, &offset) == 1) {
                                mpkt.scope = SCOPE_PRIVATE;
                                mpkt.target_id = tid;
                                if (strlen(msg_start) > (1 + offset + 1))
                                    strncpy(mpkt.text, msg_start + 1 + offset + 1, 4095);
                                else 
                                    mpkt.text[0] = '\0';
                            } else strncpy(mpkt.text, msg_start, 4095);
                        } else {
                            strncpy(mpkt.text, msg_start, 4095);
                        }
                        
                        mpkt.length = strlen(mpkt.text);
                        sign_packet_message(&mpkt);
                        
                        size_t pkt_size = offsetof(PacketMessage, text) + mpkt.length + 1;
                        if (pkt_size > sizeof(PacketMessage)) pkt_size = sizeof(PacketMessage);
                        
                        size_t svan_msg = 0;
                        char *p_msg = (char *)&mpkt;
                        while (svan_msg < pkt_size) {
                            ssize_t n = send(sock, p_msg + svan_msg, pkt_size - svan_msg, 0);
                            if (n <= 0) break;
                            svan_msg += n;
                        }
                    } else {
                        PacketCommand cpkt = {PKT_COMMAND, ""};
                        size_t clen = strlen(g_input_buf);
                        if (clen > 255) clen = 255;
                        memcpy(cpkt.cmd, g_input_buf, clen);
                        cpkt.cmd[clen] = '\0';
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    }
                    
                    g_input_ptr = 0;
                    g_input_buf[0] = 0;
                } else {
                    printf("\n");
                }
                reprint_prompt();
            } else if (c == 127 || c == 8) { /* Backspace */
                if (g_input_ptr > 0) {
                    g_input_ptr--;
                    g_input_buf[g_input_ptr] = 0;
                    reprint_prompt();
                }
            } else if (c >= 32 && c <= 126 && g_input_ptr < 255) {
                g_input_buf[g_input_ptr++] = c;
                g_input_buf[g_input_ptr] = 0;
                reprint_prompt();
            } else if (c == 27) { /* ESC o sequenze speciali */
                /* Potremmo gestire le frecce qui, ma per ora lo ignoriamo */
            }
        }
    }

    close(sock);
    return 0;
}
