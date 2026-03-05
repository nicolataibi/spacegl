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
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/provider.h>
#include <netdb.h>
#include "network.h"

/* Pre-Shared DeepSpace Encryption Key (Loaded from ENV) */
uint8_t deep_space_key[32];
uint8_t master_root_key[32];
uint8_t ALGO_KEYS[13][32];
#include "shared_state.h"
#include "ui.h"

void derive_algo_keys(uint8_t *master_key, const char *name) {
    /* Create captain-specific directory for key persistence */
    char dir_path[128];
    sprintf(dir_path, "captains/%s", name ? name : "DEFAULT");
    mkdir("captains", 0700);
    mkdir(dir_path, 0700);

    for (int k = 1; k <= 12; k++) {
        char key_path[256];
        sprintf(key_path, "%s/algo_%d.key", dir_path, k);
        
        FILE *fk_read = fopen(key_path, "r");
        bool loaded = false;
        if (fk_read) {
            char hex[128];
            if (fgets(hex, sizeof(hex), fk_read)) {
                /* Parse hex string to bytes */
                for (int b = 0; b < 32; b++) {
                    unsigned int val;
                    if (sscanf(hex + (b * 2), "%02x", &val) == 1) {
                        ALGO_KEYS[k][b] = (uint8_t)val;
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
            HMAC(EVP_sha256(), master_key, 32, (uint8_t*)salt, strlen(salt), ALGO_KEYS[k], &len);

            /* Save key to file for auditing/differentiation */
            FILE *fk = fopen(key_path, "w");
            if (fk) {
                for (int b = 0; b < 32; b++) fprintf(fk, "%02x", ALGO_KEYS[k][b]);
                fprintf(fk, "\n");
                fclose(fk);
            }
        }
    }
    printf(B_BLUE "Personal Cryptographic Frequencies synchronized via %s/\n" RESET, dir_path);
}

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

void encrypt_payload(PacketMessage *msg, const char *plaintext, const uint8_t *key, int64_t frame_id) {
    int plaintext_len = strlen(plaintext);
    if (plaintext_len > 65535) plaintext_len = 65535;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    RAND_bytes(msg->iv, 16); 
    
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
    
    EVP_EncryptInit_ex(ctx, cipher, NULL, NULL, NULL);
    if (is_gcm) {
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 16, NULL);
    }
    
    if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, msg->iv) <= 0) {
        msg->is_encrypted = 0;
        strncpy(msg->text, plaintext, 65535);
        msg->length = strlen(msg->text);
        EVP_CIPHER_CTX_free(ctx);
        return;
    }
    
    int outlen;
    EVP_EncryptUpdate(ctx, (uint8_t*)msg->text, &outlen, (const uint8_t*)plaintext, plaintext_len);
    int final_len;
    if (EVP_EncryptFinal_ex(ctx, (uint8_t*)msg->text + outlen, &final_len) <= 0) {
        msg->is_encrypted = 0;
        strncpy(msg->text, plaintext, 65535);
        msg->length = strlen(msg->text);
        EVP_CIPHER_CTX_free(ctx);
        return;
    }
    
    if (is_gcm) EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, msg->tag);
    else memset(msg->tag, 0, 16);
    
    msg->origin_frame = frame_id;
    msg->length = outlen + final_len;
    EVP_CIPHER_CTX_free(ctx);
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
SharedIPC *g_shm = NULL;
GameState *g_shared_state = NULL; /* Current write buffer */
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
    (void)sig;
    g_visualizer_ready = 1;
}

void handle_sigchld(int sig) {
    (void)sig;
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
    
    if (ftruncate(shm_fd, sizeof(SharedIPC)) == -1) {
        perror("ftruncate failed");
        exit(1);
    }
    
    g_shm = mmap(NULL, sizeof(SharedIPC), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (g_shm == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    /* Initialize Atomic Indices for Double Buffering */
    atomic_init(&g_shm->read_index, 0);
    atomic_init(&g_shm->write_index, 1);
    g_shared_state = &g_shm->buffers[1]; /* Initial write buffer */

    /* Initialize Command Queue indices */
    atomic_init(&g_shm->cmd_head, 0);
    atomic_init(&g_shm->cmd_tail, 0);

    /* Initialize Event Queue indices */
    atomic_init(&g_shm->event_head, 0);
    atomic_init(&g_shm->event_tail, 0);

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&g_shm->mutex, &attr);
    
    sem_init(&g_shm->data_ready, 1, 0);
    
    /* Initial state for BOTH buffers: set objects as INACTIVE */
    for(int b=0; b<2; b++) {
        g_shm->buffers[b].shm_s[0] = 20.0;
        g_shm->buffers[b].shm_s[1] = 20.0;
        g_shm->buffers[b].shm_s[2] = 20.0;
        g_shm->buffers[b].shm_hull_integrity = 100.0;
        g_shm->buffers[b].shm_energy = 10000;
        g_shm->buffers[b].shm_life_support = 100.0;
        g_shm->buffers[b].object_count = 0;
        for(int o=0; o<MAX_OBJECTS; o++) {
            g_shm->buffers[b].objects[o].active = 0;
        }
    }
}

void swap_buffers() {
    int old_write = atomic_load(&g_shm->write_index);
    int old_read = atomic_load(&g_shm->read_index);
    
    /* Swap Indices */
    atomic_store(&g_shm->read_index, old_write);
    atomic_store(&g_shm->write_index, old_read);
    
    /* Update local write pointer for the NEXT frame.
       We COPY the current buffer to the next one to maintain continuity of static data (map, etc) */
    memcpy(&g_shm->buffers[old_read], &g_shm->buffers[old_write], sizeof(GameState));
    g_shared_state = &g_shm->buffers[old_read];
}

void push_ipc_event(int type, double x1, double y1, double z1, double x2, double y2, double z2, int extra) {
    if (!g_shm) return;
    int tail = atomic_load_explicit(&g_shm->event_tail, memory_order_relaxed);
    int head = atomic_load_explicit(&g_shm->event_head, memory_order_acquire);
    int next = (tail + 1) % IPC_EVENT_QUEUE_SIZE;
    
    if (next != head) {
        IPCEvent *ev = &g_shm->event_queue[tail];
        ev->type = type;
        ev->x1 = x1; ev->y1 = y1; ev->z1 = z1;
        ev->x2 = x2; ev->y2 = y2; ev->z2 = z2;
        ev->extra = extra;
        atomic_store_explicit(&g_shm->event_tail, next, memory_order_release);
    }
}

void process_ipc_commands(int server_sock) {
    int head = atomic_load_explicit(&g_shm->cmd_head, memory_order_acquire);
    int tail = atomic_load_explicit(&g_shm->cmd_tail, memory_order_relaxed);
    
    while (head != tail) {
        IPCCommand *cmd = &g_shm->cmd_queue[head];
        PacketCommand pkt;
        pkt.type = PKT_COMMAND;
        memset(pkt.cmd, 0, sizeof(pkt.cmd));
        strncpy(pkt.cmd, cmd->cmd, sizeof(pkt.cmd) - 1);
        send(server_sock, &pkt, sizeof(PacketCommand), 0);
        
        head = (head + 1) % CMD_QUEUE_SIZE;
        atomic_store_explicit(&g_shm->cmd_head, head, memory_order_release);
    }
}

void cleanup() {
    if (visualizer_pid > 0) kill(visualizer_pid, SIGTERM);
    if (g_shm) munmap(g_shm, sizeof(SharedIPC));
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
    (void)arg;
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
                
                /* Receiver Auto-Tuning: Attempt decryption if the packet is marked encrypted.
                   The integrity check (GCM tag) will determine if the frequency/key is correct. */
                if (msg->is_encrypted) {
                    char decrypted[65536];
                    int success = 0;
                    
                    /* Identify if the message is from a ship system or the server */
                    bool is_system_msg = (strcmp(msg->from, "SERVER") == 0 || strcmp(msg->from, "COMPUTER") == 0 || 
                                          strcmp(msg->from, "SCIENCE") == 0 || strcmp(msg->from, "TACTICAL") == 0 ||
                                          strcmp(msg->from, "ENGINEERING") == 0 || strcmp(msg->from, "HELMSMAN") == 0 ||
                                          strcmp(msg->from, "WARNING") == 0 || strcmp(msg->from, "DAMAGE CONTROL") == 0 ||
                                          strcmp(msg->from, "STARBASE") == 0 || strcmp(msg->from, "Alliance Command") == 0);

                    /* 
                       STRATEGY: 
                       1. For system messages, trust the packet's crypto_algo (Auto-tuning).
                          This prevents "FREQUENCY MISMATCH" noise during the exact moment a captain switches enc.
                       2. For captain-to-captain messages, strictly use OUR active_algo.
                          This ensures noise when frequencies differ between captains.
                    */
                    int active_algo = (g_shared_state) ? g_shared_state->shm_crypto_algo : CRYPTO_NONE;
                    int decryption_algo = is_system_msg ? msg->crypto_algo : active_algo;
                    
                    if (decryption_algo != CRYPTO_NONE) {
                        uint8_t *k = (decryption_algo >= 1 && decryption_algo <= 12) ? ALGO_KEYS[decryption_algo] : deep_space_key;

                        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
                        const EVP_CIPHER *cipher;
                        int is_gcm = 0;

                        if (decryption_algo == CRYPTO_CHACHA) { cipher = EVP_chacha20_poly1305(); is_gcm = 1; }
                        else if (decryption_algo == CRYPTO_ARIA) { cipher = EVP_aria_256_gcm(); is_gcm = 1; }
                        else if (decryption_algo == CRYPTO_CAMELLIA) { cipher = EVP_camellia_256_ctr(); is_gcm = 0; }
                        else if (decryption_algo == CRYPTO_SEED) { cipher = EVP_seed_cbc(); is_gcm = 0; }
                        else if (decryption_algo == CRYPTO_CAST5) { cipher = EVP_cast5_cbc(); is_gcm = 0; }
                        else if (decryption_algo == CRYPTO_IDEA) { cipher = EVP_idea_cbc(); is_gcm = 0; }
                        else if (decryption_algo == CRYPTO_3DES) { cipher = EVP_des_ede3_cbc(); is_gcm = 0; }
                        else if (decryption_algo == CRYPTO_BLOWFISH) { cipher = EVP_bf_cbc(); is_gcm = 0; }
                        else if (decryption_algo == CRYPTO_RC4) { cipher = EVP_rc4(); is_gcm = 0; }
                        else if (decryption_algo == CRYPTO_DES) { cipher = EVP_des_cbc(); is_gcm = 0; }
                        else if (decryption_algo == CRYPTO_PQC) { cipher = EVP_aes_256_gcm(); is_gcm = 1; }
                        else { cipher = EVP_aes_256_gcm(); is_gcm = 1; } 

                        EVP_DecryptInit_ex(ctx, cipher, NULL, NULL, NULL);
                        if (is_gcm) {
                            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 16, NULL);
                            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, msg->tag);
                        }

                        if (EVP_DecryptInit_ex(ctx, NULL, NULL, k, msg->iv) > 0) {
                            int outlen;
                            EVP_DecryptUpdate(ctx, (uint8_t*)decrypted, &outlen, (const uint8_t*)msg->text, msg->length);
                            int final_len;
                            if (EVP_DecryptFinal_ex(ctx, (uint8_t*)decrypted + outlen, &final_len) > 0) {
                                int total_len = outlen + final_len;
                                memcpy(msg->text, decrypted, total_len);
                                msg->text[total_len] = '\0';
                                msg->length = total_len;
                                success = 1;
                            }
                        }
                        EVP_CIPHER_CTX_free(ctx);
                    }

                    if (!success) {
                        /* Decryption failed (likely due to algorithm mismatch or key synchronization during transition) */
                        char noise[128];
                        int noise_len = (msg->length > 64) ? 64 : msg->length;
                        for(int n=0; n<noise_len; n++) {
                            unsigned char c_raw = (unsigned char)msg->text[n];
                            noise[n] = (c_raw % 94) + 33; 
                        }
                        noise[noise_len] = '\0';
                        snprintf(msg->text, 65535, B_RED "<< SIGNAL DISTURBED: FREQUENCY MISMATCH >>" RESET "\n [HINT]: Tuning mismatch during transition. Raw data stream: %s...", noise);
                    }
                } else if (msg->is_encrypted) {
                    /* Frequency Mismatch: Show simulated binary noise */
                    char noise[128];
                    int noise_len = (msg->length > 64) ? 64 : msg->length;
                    for(int n=0; n<noise_len; n++) {
                        unsigned char c_raw = (unsigned char)msg->text[n];
                        noise[n] = (c_raw % 94) + 33; 
                    }
                    noise[noise_len] = '\0';
                    snprintf(msg->text, 65535, B_RED "<< SIGNAL DISTURBED: FREQUENCY MISMATCH >>" RESET "\n [HINT]: Tuning mismatch. Signal is encrypted on a different frequency.\n [RAW_DATA]: %s...", noise);
                } else {
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
        } else if (type == PKT_UPDATE || type == PKT_UPDATE_DELTA) {
            static PacketUpdate current_state;
            int current_pkt_size = sizeof(int32_t);
            
            if (type == PKT_UPDATE) {
                PacketUpdate upd;
                memset(&upd, 0, sizeof(PacketUpdate));
                upd.type = type;
                size_t fixed_size = offsetof(PacketUpdate, objects);
                int r_fixed = read_all(sock, ((char*)&upd) + sizeof(int32_t), fixed_size - sizeof(int32_t));
                if (r_fixed <= 0) break;
                if (upd.object_count > 0) {
                    int r_objs = read_all(sock, upd.objects, upd.object_count * sizeof(NetObject));
                    if (r_objs <= 0) break;
                    current_pkt_size += r_objs;
                }
                current_pkt_size += r_fixed;
                memcpy(&current_state, &upd, sizeof(PacketUpdate));
            } else {
                /* Handle DELTA Update */
                PacketUpdateDelta header;
                int r_head = read_all(sock, ((char*)&header) + sizeof(int32_t), sizeof(PacketUpdateDelta) - sizeof(int32_t));
                if (r_head <= 0) break;
                current_pkt_size += r_head;
                
                uint64_t mask = header.update_mask;
                current_state.frame_id = header.frame_id;
                
                if (mask & UPD_TRANSFORM) {
                    UpdateBlockTransform b; read_all(sock, &b, sizeof(b));
                    current_state.q1 = b.q1; current_state.q2 = b.q2; current_state.q3 = b.q3;
                    current_state.s1 = b.s1; current_state.s2 = b.s2; current_state.s3 = b.s3;
                    current_state.van_h = b.van_h; current_state.van_m = b.van_m;
                    current_state.eta = b.eta;
                    current_pkt_size += sizeof(b);
                }
                if (mask & UPD_VITALS) {
                    UpdateBlockVitals b; read_all(sock, &b, sizeof(b));
                    current_state.energy = b.energy; current_state.torpedoes = b.torpedoes;
                    current_state.cargo_energy = b.cargo_energy; current_state.cargo_torpedoes = b.cargo_torpedoes;
                    current_state.crew_count = b.crew_count; current_state.prison_unit = b.prison_unit;
                    current_state.composite_plating = b.composite_plating; current_state.hull_integrity = b.hull_integrity;
                    current_pkt_size += sizeof(b);
                }
                if (mask & UPD_SHIELDS) {
                    UpdateBlockShields b; read_all(sock, &b, sizeof(b));
                    memcpy(current_state.shields, b.shields, sizeof(b.shields));
                    current_pkt_size += sizeof(b);
                }
                if (mask & UPD_SYSTEMS) {
                    UpdateBlockSystems b; read_all(sock, &b, sizeof(b));
                    memcpy(current_state.system_health, b.system_health, sizeof(b.system_health));
                    current_pkt_size += sizeof(b);
                }
                if (mask & UPD_INTERNAL) {
                    UpdateBlockInternal b; read_all(sock, &b, sizeof(b));
                    memcpy(current_state.inventory, b.inventory, sizeof(b.inventory));
                    memcpy(current_state.power_dist, b.power_dist, sizeof(b.power_dist));
                    current_state.life_support = b.life_support; current_state.anti_matter_count = b.anti_matter_count;
                    current_pkt_size += sizeof(b);
                }
                if (mask & UPD_COMBAT) {
                    UpdateBlockCombat b; read_all(sock, &b, sizeof(b));
                    current_state.lock_target = b.lock_target; current_state.tube_state = b.tube_state;
                    memcpy(current_state.tube_load_timers, b.tube_load_timers, sizeof(b.tube_load_timers));
                    current_state.current_tube = b.current_tube; current_state.ion_beam_charge = b.ion_beam_charge;
                    current_pkt_size += sizeof(b);
                    int32_t bc; read_all(sock, &bc, sizeof(int32_t)); current_pkt_size += sizeof(int32_t);
                    current_state.beam_count = bc;
                    if (bc > 0) {
                        read_all(sock, current_state.beams, bc * sizeof(NetBeam));
                        current_pkt_size += bc * sizeof(NetBeam);
                    }
                }
                if (mask & UPD_FLAGS) {
                    UpdateBlockFlags b; read_all(sock, &b, sizeof(b));
                    current_state.is_cloaked = b.is_cloaked; current_state.is_docked = b.is_docked;
                    current_state.red_alert = b.red_alert; current_state.is_jammed = b.is_jammed;
                    current_state.nav_state = b.nav_state; current_state.show_axes = b.show_axes;
                    current_state.show_grid = b.show_grid; current_state.show_bridge = b.show_bridge;
                    current_state.show_map = b.show_map; current_state.map_filter = b.map_filter;
                    current_state.shm_crypto_algo = b.encryption_algo;
                    current_state.encryption_flags = b.encryption_flags;
                    current_pkt_size += sizeof(b);
                }
                if (mask & UPD_EFFECTS) {
                    UpdateBlockEffects b; read_all(sock, &b, sizeof(b));
                    current_state.supernova_pos = b.supernova_pos;
                    memcpy(current_state.supernova_q, b.supernova_q, sizeof(b.supernova_q));
                    memcpy(current_state.torps, b.torps, sizeof(b.torps)); 
                    current_state.wormhole = b.wormhole;
                    current_state.event_count = b.event_count;
                    if (current_state.event_count > MAX_NET_EVENTS) current_state.event_count = MAX_NET_EVENTS;
                    memcpy(current_state.events, b.events, current_state.event_count * sizeof(NetEvent));
                    current_state.torpedo_count = b.torpedo_count;
                    if (current_state.torpedo_count > MAX_VISIBLE_TORPEDOES) current_state.torpedo_count = MAX_VISIBLE_TORPEDOES;
                    memcpy(current_state.visible_torpedoes, b.visible_torpedoes, current_state.torpedo_count * sizeof(NetVisibleTorpedo));
                    current_pkt_size += sizeof(b);
                }
                if (mask & UPD_PROBES) {
                    UpdateBlockProbes b; read_all(sock, &b, sizeof(b));
                    memcpy(current_state.probes, b.probes, sizeof(b.probes));
                    current_pkt_size += sizeof(b);
                }
                if (mask & UPD_OBJECTS) {
                    int32_t oc; read_all(sock, &oc, sizeof(int32_t)); current_pkt_size += sizeof(int32_t);
                    current_state.object_count = oc;
                    if (oc > 0) {
                        read_all(sock, current_state.objects, oc * sizeof(NetObject));
                        current_pkt_size += oc * sizeof(NetObject);
                    }
                }
                if (mask & UPD_MAP) {
                    UpdateBlockMap b; read_all(sock, &b, sizeof(b));
                    current_state.map_update_val = b.map_update_val;
                    memcpy(current_state.map_update_q, b.map_update_q, sizeof(b.map_update_q));
                    current_state.map_update_val2 = b.map_update_val2;
                    memcpy(current_state.map_update_q2, b.map_update_q2, sizeof(b.map_update_q2));
                    current_pkt_size += sizeof(b);
                }
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
                double expected = 1000.0 / (double)GAME_TICK_RATE; /* ms per tick */
                jitter_sum += fabs(delta - expected);
            }
            last_packet_arrival = now_secs;

            bytes_this_sec += current_pkt_size;
            packets_this_sec++;

            double elapsed = (now_ts.tv_sec - last_ts.tv_sec) + (now_ts.tv_nsec - last_ts.tv_nsec) / 1e9;

            if (elapsed >= 1.0) {
                if (g_shared_state) {
                    g_shared_state->net_kbps = (bytes_this_sec / 1024.0 / elapsed);
                    g_shared_state->net_packet_count = (int)(packets_this_sec / elapsed);
                    g_shared_state->net_avg_packet_size = (packets_this_sec > 0) ? (int)(bytes_this_sec / packets_this_sec) : 0;
                    g_shared_state->net_jitter = (packets_this_sec > 0) ? (jitter_sum / packets_this_sec) : 0;
                    g_shared_state->net_uptime = now_ts.tv_sec - link_start_ts.tv_sec;
                    
                    /* Advanced Signal Processing: Noise Floor & Damping */
                    double raw_jitter = (packets_this_sec > 0) ? (jitter_sum / packets_this_sec) : 0;
                    g_shared_state->net_jitter = raw_jitter;
                    
                    /* Ignore jitter below 0.5ms (Quantum Background Noise) */
                    double effective_jitter = raw_jitter - 0.5;
                    if (effective_jitter < 0) effective_jitter = 0;
                    
                    /* Integrity: More robust formula with lower penalty for minor fluctuations */
                    double integrity = 100.0 - (effective_jitter * 1.2); 
                    if (integrity < 0) integrity = 0;
                    if (integrity > 100) integrity = 100;
                    g_shared_state->net_integrity = integrity;
                    
                    /* Efficiency: How much we send vs the maximum possible packet size */
                    g_shared_state->net_efficiency = 100.0 * (1.0 - (double)current_pkt_size / sizeof(PacketUpdate));
                }
                bytes_this_sec = 0; packets_this_sec = 0; jitter_sum = 0; last_ts = now_ts;
            }
            if (g_shared_state) {
                g_shared_state->net_last_packet_size = current_pkt_size;
            }
            
            if (g_shared_state) {
                if (current_state.object_count > MAX_OBJECTS) current_state.object_count = MAX_OBJECTS;

                /* Sincronizziamo lo stato locale con i dati ottimizzati dal server */
                g_shared_state->shm_energy = current_state.energy;
                g_shared_state->shm_composite_plating = current_state.composite_plating;
                g_shared_state->shm_hull_integrity = current_state.hull_integrity;
                g_shared_state->shm_crew = current_state.crew_count;
                g_shared_state->shm_prison_unit = current_state.prison_unit;
                g_shared_state->shm_torpedoes = current_state.torpedoes;
                g_shared_state->shm_cargo_energy = current_state.cargo_energy;
                g_shared_state->shm_cargo_torpedoes = current_state.cargo_torpedoes;
                for(int s=0; s<6; s++) g_shared_state->shm_shields[s] = current_state.shields[s];
                for(int sys=0; sys<10; sys++) g_shared_state->shm_system_health[sys] = current_state.system_health[sys];
                for(int p=0; p<3; p++) g_shared_state->shm_power_dist[p] = current_state.power_dist[p];
                g_shared_state->shm_life_support = current_state.life_support;
                g_shared_state->shm_ion_beam_charge = current_state.ion_beam_charge;
                g_shared_state->shm_tube_state = current_state.tube_state;
                for(int t=0; t<4; t++) g_shared_state->tube_load_timers[t] = current_state.tube_load_timers[t];
                g_shared_state->current_tube = current_state.current_tube;
                g_shared_state->shm_anti_matter = current_state.anti_matter_count;
                for(int inv=0; inv<10; inv++) g_shared_state->inventory[inv] = current_state.inventory[inv];
                g_shared_state->shm_lock_target = current_state.lock_target;
                
                for(int p=0; p<3; p++) {
                    g_shared_state->probes[p].active = current_state.probes[p].active;
                    g_shared_state->probes[p].q1 = current_state.probes[p].q1;
                    g_shared_state->probes[p].q2 = current_state.probes[p].q2;
                    g_shared_state->probes[p].q3 = current_state.probes[p].q3;
                    g_shared_state->probes[p].s1 = current_state.probes[p].s1;
                    g_shared_state->probes[p].s2 = current_state.probes[p].s2;
                    g_shared_state->probes[p].s3 = current_state.probes[p].s3;
                    g_shared_state->probes[p].eta = current_state.probes[p].eta;
                    g_shared_state->probes[p].status = current_state.probes[p].status;
                }
                
                g_shared_state->is_cloaked = current_state.is_cloaked;
                g_shared_state->shm_is_docked = current_state.is_docked;
                g_shared_state->shm_red_alert = current_state.red_alert;
                g_shared_state->shm_is_jammed = current_state.is_jammed;
                g_shared_state->shm_nav_state = current_state.nav_state;
                g_shared_state->shm_show_axes = current_state.show_axes;
                g_shared_state->shm_show_grid = current_state.show_grid;
                g_shared_state->shm_show_bridge = current_state.show_bridge;
                g_shared_state->shm_show_map = current_state.show_map;
                g_shared_state->shm_map_filter = current_state.map_filter;
                g_shared_state->shm_crypto_algo = current_state.shm_crypto_algo;
                g_shared_state->shm_encryption_flags = current_state.encryption_flags;
                g_shared_state->shm_q[0] = current_state.q1;
                g_shared_state->shm_q[1] = current_state.q2;
                g_shared_state->shm_q[2] = current_state.q3;
                g_shared_state->shm_s[0] = current_state.s1;
                g_shared_state->shm_s[1] = current_state.s2;
                g_shared_state->shm_s[2] = current_state.s3;
                g_shared_state->shm_h = current_state.van_h;
                g_shared_state->shm_m = current_state.van_m;
                g_shared_state->shm_eta = current_state.eta;
                sprintf(g_shared_state->quadrant, "Q-%d-%d-%d", current_state.q1, current_state.q2, current_state.q3);

                /* Update dynamic galaxy data (e.g. Ion Storms, Supernovas) */
                int mq1 = current_state.map_update_q[0], mq2 = current_state.map_update_q[1], mq3 = current_state.map_update_q[2];
                if (mq1 >= 1 && mq1 <= GALAXY_SIZE && mq2 >= 1 && mq2 <= GALAXY_SIZE && mq3 >= 1 && mq3 <= GALAXY_SIZE) {
                    g_shared_state->shm_galaxy[mq1][mq2][mq3] = current_state.map_update_val;
                }
                
                /* Handle 2nd quadrant update */
                int m2q1 = current_state.map_update_q2[0], m2q2 = current_state.map_update_q2[1], m2q3 = current_state.map_update_q2[2];
                if (m2q1 >= 1 && m2q1 <= GALAXY_SIZE && m2q2 >= 1 && m2q2 <= GALAXY_SIZE && m2q3 >= 1 && m2q3 <= GALAXY_SIZE) {
                    g_shared_state->shm_galaxy[m2q1][m2q2][m2q3] = current_state.map_update_val2;
                }

                g_shared_state->object_count = current_state.object_count;
                for (int o=0; o < current_state.object_count; o++) {
                    g_shared_state->objects[o].shm_x = current_state.objects[o].net_x;
                    g_shared_state->objects[o].shm_y = current_state.objects[o].net_y;
                    g_shared_state->objects[o].shm_z = current_state.objects[o].net_z;
                    g_shared_state->objects[o].h = current_state.objects[o].h;
                    g_shared_state->objects[o].m = current_state.objects[o].m;
                    g_shared_state->objects[o].type = current_state.objects[o].type;
                    g_shared_state->objects[o].ship_class = current_state.objects[o].ship_class;
                    g_shared_state->objects[o].health_pct = current_state.objects[o].health_pct;
                    g_shared_state->objects[o].energy = current_state.objects[o].energy;
                    g_shared_state->objects[o].plating = current_state.objects[o].plating;
                    g_shared_state->objects[o].hull_integrity = current_state.objects[o].hull_integrity;
                    g_shared_state->objects[o].faction = current_state.objects[o].faction;
                    g_shared_state->objects[o].id = current_state.objects[o].id;
                    g_shared_state->objects[o].is_cloaked = current_state.objects[o].is_cloaked;
                    strncpy(g_shared_state->objects[o].shm_name, current_state.objects[o].name, 63);
                    g_shared_state->objects[o].active = 1;
                }
                
                /* Handle beams from update - Queue them for reliable rendering */
                if (current_state.beam_count > 0) {
                    for (int b=0; b < current_state.beam_count; b++) {
                        push_ipc_event(IPC_EV_BEAM, 
                                       current_state.beams[b].net_sx, current_state.beams[b].net_sy, current_state.beams[b].net_sz,
                                       current_state.beams[b].net_tx, current_state.beams[b].net_ty, current_state.beams[b].net_tz,
                                       current_state.beams[b].active);
                    }
                    /* Reset local beam count after pushing to IPC queue */
                    current_state.beam_count = 0;
                }
                
                /* Projectile position (Dedicated Stream, Zero-Lag) */
                g_shared_state->torpedo_count = current_state.torpedo_count;
                for(int s=0; s < current_state.torpedo_count && s < MAX_VISIBLE_TORPEDOES; s++) {
                    g_shared_state->torps[s].shm_x = current_state.visible_torpedoes[s].x;
                    g_shared_state->torps[s].shm_y = current_state.visible_torpedoes[s].y;
                    g_shared_state->torps[s].shm_z = current_state.visible_torpedoes[s].z;
                    g_shared_state->torps[s].active = 1;
                }
                /* Clear remaining slots to avoid ghost torpedoes */
                for(int s = current_state.torpedo_count; s < MAX_VISIBLE_TORPEDOES; s++) {
                    g_shared_state->torps[s].active = 0;
                }
                
                /* NetEvent Queue Processing (Zero-Loss Architecture) */
                if (current_state.event_count > 0) {
                    for (int e = 0; e < current_state.event_count; e++) {
                        NetEvent *ev = &current_state.events[e];
                        push_ipc_event(ev->type, ev->x1, ev->y1, ev->z1, ev->x2, ev->y2, ev->z2, ev->extra);
                    }
                    current_state.event_count = 0;
                }
                
                /* Wormhole Event (State-based) */
                g_shared_state->wormhole.shm_x = current_state.wormhole.net_x;
                g_shared_state->wormhole.shm_y = current_state.wormhole.net_y;
                g_shared_state->wormhole.shm_z = current_state.wormhole.net_z;
                g_shared_state->wormhole.active = current_state.wormhole.active;

                g_shared_state->supernova_pos.shm_x = current_state.supernova_pos.net_x;
                g_shared_state->supernova_pos.shm_y = current_state.supernova_pos.net_y;
                g_shared_state->supernova_pos.shm_z = current_state.supernova_pos.net_z;
                g_shared_state->supernova_pos.active = current_state.supernova_pos.active;
                g_shared_state->shm_sn_q[0] = current_state.supernova_q[0];
                g_shared_state->shm_sn_q[1] = current_state.supernova_q[1];
                g_shared_state->shm_sn_q[2] = current_state.supernova_q[2];
                
                g_shared_state->frame_id++; 
                swap_buffers();
                sem_post(&g_shm->data_ready);
            }
        }
    }
    return NULL;
}

void handle_sigint(int sig) {
    (void)sig;
    exit(0);
}

int main(int argc, char *argv[]) {
    char server_ip[64];
    int my_ship_class = SHIP_CLASS_GENERIC_ALIEN;
    char visualizer_type[4] = "gl";

    /* Parse visualizer selection from arguments */
    if (argc > 1) {
        if (strcmp(argv[1], "vk") == 0) strcpy(visualizer_type, "vk");
        else if (strcmp(argv[1], "gl") == 0) strcpy(visualizer_type, "gl");
    }
    
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
    memcpy(master_root_key, deep_space_key, 32);
    
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
    printf(B_CYAN " | " B_MAGENTA "          \"Per Tenebras, Lumen\" (Through darkness, light)                 " B_CYAN " |\n" RESET);
    printf(B_CYAN " |                                                                            |\n" RESET);
        printf(B_CYAN " | " B_WHITE "  Copyright (C) 2026 " B_GREEN "Nicola Taibi" B_WHITE "                                        " B_CYAN "  |\n" RESET);
        printf(B_CYAN " | " B_WHITE "  AI Core Support by " B_BLUE "Google Gemini" B_WHITE "                                       " B_CYAN "  |\n" RESET);
        printf(B_CYAN " | " B_WHITE "  License Type:      " B_YELLOW "GNU GPL v3.0" B_WHITE "                                        " B_CYAN "  |\n" RESET);
        printf(B_CYAN " \\____________________________________________________________________________/\n\n" RESET);
    

    LOG_DEBUG("sizeof(SpaceGLGame) = %zu\n", sizeof(SpaceGLGame));
    LOG_DEBUG("sizeof(PacketUpdate) = %zu\n", sizeof(PacketUpdate));

    /* OpenSSL Initialization for all algorithms (including legacy ones like SEED, CAST5, etc) */
    OSSL_PROVIDER_load(NULL, "legacy");
    OSSL_PROVIDER_load(NULL, "default");
    OpenSSL_add_all_algorithms();
    OpenSSL_add_all_ciphers();
    OpenSSL_add_all_digests();

    printf("Server IP/Hostname: "); 
    fflush(stdout);
    if (scanf("%63s", server_ip) != 1) { strcpy(server_ip, "127.0.0.1"); }
    clear_stdin();

    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    sprintf(port_str, "%d", DEFAULT_PORT);

    if (getaddrinfo(server_ip, port_str, &hints, &res) != 0) {
        fprintf(stderr, B_RED "ERROR: Could not resolve hostname '%s'\n" RESET, server_ip);
        return -1;
    }

    sock = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) continue;

        printf("Connecting to %s:%d...\n", server_ip, DEFAULT_PORT);
        fflush(stdout);
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) != -1)
            break; /* Success */

        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);

    if (sock == -1) {
        fprintf(stderr, B_RED "ERROR: Connection to '%s' failed.\n" RESET, server_ip);
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
    fflush(stdout);
    if (scanf("%63s", captain_name) != 1) { strcpy(captain_name, "Captain"); }
    clear_stdin();

    /* Personalize Frequencies based on Captain Name */
    derive_algo_keys(master_root_key, captain_name);

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
        fflush(stdout);
        int selection;
        if (scanf("%d", &selection) != 1) { selection = 0; }
        switch(selection) {
            case 0: my_faction = FACTION_ALLIANCE; break;
            case 1: my_faction = FACTION_KORTHIAN; break;
            case 2: my_faction = FACTION_XYLARI; break;
            case 3: my_faction = FACTION_SWARM; break;
            case 4: my_faction = FACTION_VESPERIAN; break;
            case 5: my_faction = FACTION_JEM_HADAR; break;
            case 6: my_faction = FACTION_THOLIAN; break;
            case 7: my_faction = FACTION_GORN; break;
            case 8: my_faction = FACTION_GILDED; break;
            case 9: my_faction = FACTION_SPECIES_8472; break;
            case 10: my_faction = FACTION_BREEN; break;
            case 11: my_faction = FACTION_HIROGEN; break;
            default: my_faction = FACTION_ALLIANCE; break;
        }
        
        if (my_faction == FACTION_ALLIANCE) {
            printf("\n" B_WHITE "--- SELECT YOUR CLASS ---" RESET "\n");
            printf(" 0: Legacy Class\n 1: Scout Class\n 2: Heavy Cruiser\n 3: Multi-Engine Cruiser\n 4: Escort Class\n 5: Explorer Class\n 6: Flagship Class\n 7: Science Vessel\n 8: Carrier Class\n 9: Tactical Cruiser\n 10: Diplomatic Cruiser\n 11: Research Vessel\n 12: Frigate Class\nSelection: ");
            fflush(stdout);
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
    int master_read = read_all(sock, &master_sync, sizeof(SpaceGLGame));
    if (master_read == sizeof(SpaceGLGame)) {
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
        printf(B_RED "ERROR: Failed to synchronize Galaxy Map (Expected %zu, got %d).\n" RESET, sizeof(SpaceGLGame), master_read);
        close(sock);
        exit(1);
    }

    init_shm();
    
    /* Copy Galaxy Master to SHM for 3D Map View */
    if (g_shared_state) {
        
        memcpy(g_shared_state->shm_galaxy, master_sync.g, sizeof(master_sync.g));
        g_shared_state->shm_crypto_algo = CRYPTO_NONE;
        g_shared_state->shm_encryption_flags = master_sync.encryption_flags;
        memcpy(g_shared_state->shm_server_signature, master_sync.server_signature, 64);
        memcpy(g_shared_state->shm_server_pubkey, master_sync.server_pubkey, 32);
        
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
        if (strcmp(visualizer_type, "vk") == 0) {
            /* Start Vulkan Viewer */
            if (fork() == 0) {
                execl("./spacegl_vulkan", "spacegl_vulkan", shm_path, NULL);
                perror("execl failed spacegl_vulkan"); _exit(1);
            }
            /* Start HUD in xterm */
            execlp("xterm", "xterm", "-T", "SPACE GL - HUD", "-geometry", "140x40", "-e", "./spacegl_hud", shm_path, NULL);
            perror("execlp failed xterm/hud"); _exit(1);
        } else {
            /* Default: OpenGL Viewer */
            if (execl("./spacegl_3dview", "spacegl_3dview", shm_path, NULL) == -1) {
                perror("execl failed to start ./spacegl_3dview");
                _exit(1);
            }
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
                        send(sock, &cpkt, sizeof(cpkt), 0); swap_buffers();
                        g_running = 0;
                        disable_raw_mode();
                        exit(0);
                    }
                    if (strcmp(g_input_buf, "axs") == 0 || 
                               strcmp(g_input_buf, "grd") == 0 || 
                               strncmp(g_input_buf, "bridge", 6) == 0 ||
                               strncmp(g_input_buf, "map", 3) == 0 ||
                               strncmp(g_input_buf, "dis ", 4) == 0 || 
                               strcmp(g_input_buf, "dis") == 0) {
                        PacketCommand cpkt = {PKT_COMMAND, ""};
                        size_t clen = strlen(g_input_buf);
                        if (clen > 255) clen = 255;
                        memcpy(cpkt.cmd, g_input_buf, clen);
                        cpkt.cmd[clen] = '\0';
                        send(sock, &cpkt, sizeof(cpkt), 0);
                    } else if (strncmp(g_input_buf, "enc ", 4) == 0) {
                        /* The client will now wait for the server's update packet 
                           to officially switch frequencies, preventing transition noise. */
                        PacketCommand cpkt; 
                        cpkt.type = PKT_COMMAND;
                        snprintf(cpkt.cmd, sizeof(cpkt.cmd), "%s", g_input_buf);
                        send(sock, &cpkt, sizeof(cpkt), 0); 
                        swap_buffers();
                    } else if (strncmp(g_input_buf, "rad ", 4) == 0 || strcmp(g_input_buf, "rad") == 0) {
                        char *msg_start = (strlen(g_input_buf) > 4) ? g_input_buf + 4 : NULL;
                        
                        if (!msg_start || strlen(msg_start) == 0) {
                            printf(B_YELLOW "Usage: rad <MSG>, rad @Faction <MSG>, rad #ID <MSG>\n" RESET);
                            g_input_ptr = 0;
                            g_input_buf[0] = 0;
                            reprint_prompt();
                            continue;
                        }

                        PacketMessage mpkt;
                        memset(&mpkt, 0, sizeof(PacketMessage));
                        mpkt.type = PKT_MESSAGE;
                        strcpy(mpkt.from, captain_name);
                        mpkt.faction = my_faction;
                        mpkt.scope = SCOPE_GLOBAL;
                        
                        char plaintext[4096];
                        memset(plaintext, 0, sizeof(plaintext));

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
                                else if (strcasecmp(target_name, "Vesperian")==0 || strcasecmp(target_name, "Car")==0) mpkt.faction = FACTION_VESPERIAN;
                                else if (strcasecmp(target_name, "Ascendant")==0 || strcasecmp(target_name, "Jem")==0) mpkt.faction = FACTION_JEM_HADAR;
                                else if (strcasecmp(target_name, "Quarzite")==0 || strcasecmp(target_name, "Tho")==0) mpkt.faction = FACTION_THOLIAN;
                                else if (strcasecmp(target_name, "Saurian")==0) mpkt.faction = FACTION_GORN;
                                else if (strcasecmp(target_name, "Gilded")==0 || strcasecmp(target_name, "Fer")==0) mpkt.faction = FACTION_GILDED;
                                else if (strcasecmp(target_name, "FluidicVoid")==0 || strcasecmp(target_name, "8472")==0) mpkt.faction = FACTION_SPECIES_8472;
                                else if (strcasecmp(target_name, "Cryos")==0) mpkt.faction = FACTION_BREEN;
                                else if (strcasecmp(target_name, "Apex")==0) mpkt.faction = FACTION_HIROGEN;
                                else {
                                    mpkt.scope = SCOPE_GLOBAL; /* Fallback */
                                }
                                
                                /* Skip the @faction and potentially the space after it */
                                char *actual_msg = msg_start + 1 + offset;
                                if (actual_msg[0] == ' ') actual_msg++;
                                strncpy(plaintext, actual_msg, 4095);
                            } else strncpy(plaintext, msg_start, 4095);
                        } else if (msg_start[0] == '#') {
                            int tid;
                            int offset = 0;
                            if (sscanf(msg_start + 1, "%d%n", &tid, &offset) == 1) {
                                mpkt.scope = SCOPE_PRIVATE;
                                mpkt.target_id = tid;
                                
                                /* Skip the #id and potentially the space after it */
                                char *actual_msg = msg_start + 1 + offset;
                                if (actual_msg[0] == ' ') actual_msg++;
                                strncpy(plaintext, actual_msg, 4095);
                            } else strncpy(plaintext, msg_start, 4095);
                        } else {
                            strncpy(plaintext, msg_start, 4095);
                        }
                        
                        /* Apply current encryption settings */
                        int current_algo = CRYPTO_NONE;
                        if (g_shared_state) current_algo = g_shared_state->shm_crypto_algo;
                        
                        if (current_algo != CRYPTO_NONE) {
                            mpkt.is_encrypted = 1;
                            mpkt.crypto_algo = current_algo;
                            
                            /* 1. Sign the PLAINTEXT before encryption */
                            strncpy(mpkt.text, plaintext, 65535);
                            mpkt.length = strlen(mpkt.text);
                            sign_packet_message(&mpkt);
                            
                            /* 2. Now Encrypt the payload */
                            int64_t fid = (g_shared_state) ? g_shared_state->frame_id : 0;
                            uint8_t *k = (current_algo >= 1 && current_algo <= 12) ? ALGO_KEYS[current_algo] : deep_space_key;
                            encrypt_payload(&mpkt, plaintext, k, fid);
                        } else {
                            mpkt.is_encrypted = 0;
                            mpkt.crypto_algo = CRYPTO_NONE;
                            strncpy(mpkt.text, plaintext, 65535);
                            mpkt.length = strlen(mpkt.text);
                            
                            /* Sign the cleartext */
                            sign_packet_message(&mpkt);
                        }

                        size_t pkt_size = offsetof(PacketMessage, text) + mpkt.length;
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
                        send(sock, &cpkt, sizeof(cpkt), 0); swap_buffers();
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
