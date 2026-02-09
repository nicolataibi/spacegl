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

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <sys/socket.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include "server_internal.h"
#include "ui.h"

/* Master Key for Deep Space Communications (Loaded from ENV) */
uint8_t MASTER_SESSION_KEY[32];

/* Advanced Deep Space Encryption Engine */
void encrypt_payload(PacketMessage *msg, const char *plaintext, const uint8_t *key) {
    int plaintext_len = strlen(plaintext);
    if (plaintext_len > 65535) plaintext_len = 65535;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    RAND_bytes(msg->iv, 12); 
    
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
    
    /* We use the random IV for encryption. 
       THEN we will XOR the IV in the packet for transmission. */
    EVP_EncryptInit_ex(ctx, cipher, NULL, key, msg->iv);
    
    int outlen;
    EVP_EncryptUpdate(ctx, (uint8_t*)msg->text, &outlen, (const uint8_t*)plaintext, plaintext_len);
    int final_len;
    EVP_EncryptFinal_ex(ctx, (uint8_t*)msg->text + outlen, &final_len);
    
    /* Get Auth Tag if GCM */
    if (is_gcm) {
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, msg->tag);
    } else {
        memset(msg->tag, 0, 16);
    }
    
    /* Rotating Frequency Integration: XOR the packet's IV field with frame_id for transmission.
       The client MUST reverse this BEFORE calling DecryptInit using the embedded origin_frame. */
    msg->origin_frame = spacegl_master.frame_id;
    for(int i=0; i<8; i++) msg->iv[i] ^= ((msg->origin_frame >> (i*8)) & 0xFF);

    msg->length = outlen + final_len;
    EVP_CIPHER_CTX_free(ctx);
}

int read_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    char *p = (char *)buf;
    while (total < len) {
        ssize_t n = read(fd, p + total, len - total);
        if (n == 0) return 0;
        if (n < 0) return -1;
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

void broadcast_message(PacketMessage *msg) {
    char plaintext[65536];
    size_t plen = (msg->length < 65536) ? msg->length : 65535;
    memcpy(plaintext, msg->text, plen);
    plaintext[plen] = '\0';

    pthread_mutex_lock(&game_mutex);
    
    int sender_algo = CRYPTO_NONE;
    for(int j=0; j<MAX_CLIENTS; j++) {
        if (players[j].active && strcmp(players[j].name, msg->from) == 0) {
            sender_algo = players[j].crypto_algo;
            break;
        }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].active && players[i].socket != 0) {
            if (msg->scope == SCOPE_FACTION && players[i].faction != msg->faction) continue;
            if (msg->scope == SCOPE_PRIVATE) {
                bool is_target = ((i + 1) == msg->target_id);
                bool is_sender = (strcmp(players[i].name, msg->from) == 0);
                if (!is_target && !is_sender) continue;
            }
            
            PacketMessage individual_msg = *msg;
            if (sender_algo != CRYPTO_NONE) { 
                individual_msg.is_encrypted = 1;
                individual_msg.crypto_algo = sender_algo;
                const uint8_t *k = players[i].session_key;
                bool all_zero = true; for(int z=0; z<32; z++) if(k[z]!=0) all_zero=false;
                encrypt_payload(&individual_msg, plaintext, all_zero ? MASTER_SESSION_KEY : k);
            } else {
                individual_msg.is_encrypted = 0;
                size_t tlen = strlen(plaintext);
                if (tlen > 65535) tlen = 65535;
                memcpy(individual_msg.text, plaintext, tlen);
                individual_msg.text[tlen] = '\0';
                individual_msg.length = tlen;
            }
            
            size_t pkt_size = offsetof(PacketMessage, text) + individual_msg.length;
            pthread_mutex_lock(&players[i].socket_mutex);
            write_all(players[i].socket, &individual_msg, pkt_size);
            pthread_mutex_unlock(&players[i].socket_mutex);
        }
    }
    pthread_mutex_unlock(&game_mutex);
}

void send_server_msg(int p_idx, const char *from, const char *text) {
    PacketMessage msg;
    memset(&msg, 0, sizeof(PacketMessage));
    msg.type = PKT_MESSAGE;
    strncpy(msg.from, from, 63);
    
    if (players[p_idx].crypto_algo != CRYPTO_NONE) {
        msg.is_encrypted = 1;
        msg.crypto_algo = players[p_idx].crypto_algo;
        const uint8_t *k = players[p_idx].session_key;
        bool all_zero = true; for(int z=0; z<32; z++) if(k[z]!=0) all_zero=false;
        encrypt_payload(&msg, text, all_zero ? MASTER_SESSION_KEY : k);
    } else {
        msg.is_encrypted = 0;
        strncpy(msg.text, text, 65535);
        msg.length = strlen(msg.text);
    }
    
    size_t pkt_size = offsetof(PacketMessage, text) + msg.length;
    pthread_mutex_lock(&players[p_idx].socket_mutex);
    write_all(players[p_idx].socket, &msg, pkt_size);
    pthread_mutex_unlock(&players[p_idx].socket_mutex);
}
