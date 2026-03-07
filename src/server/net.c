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
    
    /* Strict OpenSSL GCM sequence for 16-byte IV */
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

void broadcast_task(void *arg) {
    PacketMessage *msg = (PacketMessage *)arg;
    if (msg) {
        broadcast_message(msg);
        free(msg);
    }
}

void broadcast_message(PacketMessage *msg) {
    char plaintext[65536];
    memset(plaintext, 0, sizeof(plaintext));

    pthread_mutex_lock(&game_mutex);
    
    int sender_idx = -1;
    for(int j=0; j<MAX_CLIENTS; j++) {
        if (players[j].active && strcmp(players[j].name, msg->from) == 0) {
            sender_idx = j;
            break;
        }
    }

    /* 1. SERVER-SIDE DECRYPTION: The server must read the sender's message first */
    if (msg->is_encrypted && sender_idx != -1) {
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        const EVP_CIPHER *cipher;
        int is_gcm = 0;
        int algo = msg->crypto_algo;

        if (algo == CRYPTO_CHACHA) { cipher = EVP_chacha20_poly1305(); is_gcm = 1; }
        else if (algo == CRYPTO_ARIA) { cipher = EVP_aria_256_gcm(); is_gcm = 1; }
        else if (algo == CRYPTO_CAMELLIA) { cipher = EVP_camellia_256_ctr(); is_gcm = 0; }
        else if (algo == CRYPTO_SEED) { cipher = EVP_seed_cbc(); is_gcm = 0; }
        else if (algo == CRYPTO_CAST5) { cipher = EVP_cast5_cbc(); is_gcm = 0; }
        else if (algo == CRYPTO_IDEA) { cipher = EVP_idea_cbc(); is_gcm = 0; }
        else if (algo == CRYPTO_3DES) { cipher = EVP_des_ede3_cbc(); is_gcm = 0; }
        else if (algo == CRYPTO_BLOWFISH) { cipher = EVP_bf_cbc(); is_gcm = 0; }
        else if (algo == CRYPTO_RC4) { cipher = EVP_rc4(); is_gcm = 0; }
        else if (algo == CRYPTO_DES) { cipher = EVP_des_cbc(); is_gcm = 0; }
        else if (algo == CRYPTO_PQC) { cipher = EVP_aes_256_gcm(); is_gcm = 1; }
        else { cipher = EVP_aes_256_gcm(); is_gcm = 1; }

        uint8_t *k = (algo >= 1 && algo <= 12) ? players[sender_idx].algo_keys[algo] : players[sender_idx].session_key;
        
        EVP_DecryptInit_ex(ctx, cipher, NULL, NULL, NULL);
        if (is_gcm) {
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 16, NULL);
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, msg->tag);
        }
        
        if (EVP_DecryptInit_ex(ctx, NULL, NULL, k, msg->iv) <= 0) {
            send_server_msg(sender_idx, "COMPUTER", "CRITICAL ERROR: Decryption Initialization failure. Check key sync.");
            pthread_mutex_unlock(&game_mutex);
            EVP_CIPHER_CTX_free(ctx);
            return;
        }

        int outlen;
        EVP_DecryptUpdate(ctx, (uint8_t*)plaintext, &outlen, (const uint8_t*)msg->text, msg->length);
        int final_len;
        if (EVP_DecryptFinal_ex(ctx, (uint8_t*)plaintext + outlen, &final_len) > 0) {
            plaintext[outlen + final_len] = '\0';
        } else {
            /* Decryption failed at server level */
            send_server_msg(sender_idx, "COMPUTER", "TRANSMISSION BLOCKED: Cryptographic parity mismatch at server level.");
            pthread_mutex_unlock(&game_mutex);
            EVP_CIPHER_CTX_free(ctx);
            return;
        }
        EVP_CIPHER_CTX_free(ctx);
    } else {
        /* Message was already in cleartext */
        size_t plen = (msg->length < 65536) ? msg->length : 65535;
        memcpy(plaintext, msg->text, plen);
        plaintext[plen] = '\0';
    }

    /* Integrity and Energy Checks for the Sender */
    if (sender_idx != -1) {
        if (players[sender_idx].state.system_health[9] < 5.0f) {
            pthread_mutex_unlock(&game_mutex); 
            send_server_msg(sender_idx, "COMPUTER", "COMMUNICATIONS FAILURE: Subspace array damaged.");
            return;
        }
        if (players[sender_idx].state.energy < 5) {
            pthread_mutex_unlock(&game_mutex);
            send_server_msg(sender_idx, "COMPUTER", "Insufficient energy for transmission.");
            return;
        }
        players[sender_idx].state.energy -= 5;
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].active && players[i].socket != 0) {
            if (msg->scope == SCOPE_FACTION && players[i].faction != msg->faction) continue;
            if (msg->scope == SCOPE_PRIVATE) {
                bool is_target = ((i + 1) == msg->target_id);
                bool is_sender = (sender_idx == i);
                if (!is_target && !is_sender) continue;
            }
            
            /* RELAY PROTOCOL: Send the original packet as-is. 
               Recipients tuned to a different frequency will see noise. */
            size_t pkt_size = offsetof(PacketMessage, text) + msg->length;
            pthread_mutex_lock(&players[i].socket_mutex);
            write_all(players[i].socket, msg, pkt_size);
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
    
    if (players[p_idx].state.shm_crypto_algo != CRYPTO_NONE) {
        msg.is_encrypted = 1;
        msg.crypto_algo = players[p_idx].state.shm_crypto_algo;
        
        uint8_t *k = (msg.crypto_algo >= 1 && msg.crypto_algo <= 12) ? players[p_idx].algo_keys[msg.crypto_algo] : players[p_idx].session_key;
        
        encrypt_payload(&msg, text, k, spacegl_master.frame_id);
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

void send_optimized_update(int p_idx, PacketUpdate *upd) {
    ConnectedPlayer *p = &players[p_idx];
    uint64_t mask = 0;
    
    /* 1. Determine what changed */
    if (p->last_sent_state.q1 != upd->q1 || p->last_sent_state.q2 != upd->q2 || p->last_sent_state.q3 != upd->q3 ||
        p->last_sent_state.s1 != upd->s1 || p->last_sent_state.s2 != upd->s2 || p->last_sent_state.s3 != upd->s3 ||
        p->last_sent_state.van_h != upd->van_h || p->last_sent_state.van_m != upd->van_m || p->last_sent_state.van_r != upd->van_r ||
        p->last_sent_state.eta != upd->eta) mask |= UPD_TRANSFORM;

    if (p->last_sent_state.energy != upd->energy || p->last_sent_state.torpedoes != upd->torpedoes || 
        p->last_sent_state.hull_integrity != upd->hull_integrity || p->last_sent_state.cargo_energy != upd->cargo_energy ||
        p->last_sent_state.cargo_torpedoes != upd->cargo_torpedoes || p->last_sent_state.crew_count != upd->crew_count ||
        p->last_sent_state.prison_unit != upd->prison_unit || p->last_sent_state.composite_plating != upd->composite_plating) mask |= UPD_VITALS;

    if (memcmp(p->last_sent_state.shields, upd->shields, sizeof(upd->shields)) != 0) mask |= UPD_SHIELDS;
    if (memcmp(p->last_sent_state.system_health, upd->system_health, sizeof(upd->system_health)) != 0) mask |= UPD_SYSTEMS;
    if (memcmp(p->last_sent_state.inventory, upd->inventory, sizeof(upd->inventory)) != 0 || 
        p->last_sent_state.life_support != upd->life_support || p->last_sent_state.anti_matter_count != upd->anti_matter_count ||
        memcmp(p->last_sent_state.power_dist, upd->power_dist, sizeof(upd->power_dist)) != 0) mask |= UPD_INTERNAL;
    
    if (p->last_sent_state.lock_target != upd->lock_target || p->last_sent_state.tube_state != upd->tube_state ||
        p->last_sent_state.current_tube != upd->current_tube || p->last_sent_state.ion_beam_charge != upd->ion_beam_charge ||
        memcmp(p->last_sent_state.tube_load_timers, upd->tube_load_timers, sizeof(upd->tube_load_timers)) != 0) mask |= UPD_COMBAT;
    
    if (p->last_sent_state.is_cloaked != upd->is_cloaked || p->last_sent_state.is_docked != upd->is_docked || 
        p->last_sent_state.red_alert != upd->red_alert || p->last_sent_state.nav_state != upd->nav_state ||
        p->last_sent_state.show_axes != upd->show_axes || p->last_sent_state.show_grid != upd->show_grid ||
        p->last_sent_state.show_bridge != upd->show_bridge || p->last_sent_state.show_map != upd->show_map ||
        p->last_sent_state.map_filter != upd->map_filter || 
        p->last_sent_state.shm_crypto_algo != upd->shm_crypto_algo || 
        p->last_sent_state.encryption_flags != upd->encryption_flags) mask |= UPD_FLAGS;

    /* Effects, Probes and Beams are transient or frequently updated, usually we send them if not empty */
    bool any_torp = upd->torps[0].active || upd->torps[1].active || upd->torps[2].active || upd->torps[3].active;
    bool last_any_torp = p->last_sent_state.torps[0].active || p->last_sent_state.torps[1].active || p->last_sent_state.torps[2].active || p->last_sent_state.torps[3].active;
    
    if (upd->event_count > 0 || upd->torpedo_count > 0 || any_torp || (any_torp != last_any_torp) || upd->wormhole.active || upd->supernova_pos.active) mask |= UPD_EFFECTS;
    if (upd->probes[0].active || upd->probes[1].active || upd->probes[2].active) mask |= UPD_PROBES;
    if (upd->beam_count > 0 || p->last_sent_state.beam_count > 0) mask |= UPD_COMBAT; 
    
    /* Force update if beams are present, to ensure they are rendered */
    if (upd->beam_count > 0) mask |= UPD_COMBAT;
    /* Interest Management: Only send objects if quadrant changed or enough time passed or object count changed significantly */
    bool quadrant_changed = (p->last_q1 != upd->q1 || p->last_q2 != upd->q2 || p->last_q3 != upd->q3);
    p->full_update_timer++;
    if (quadrant_changed || p->full_update_timer > (5 * GAME_TICK_RATE)) {
        mask |= UPD_OBJECTS | UPD_MAP | UPD_FULL;
        p->full_update_timer = 0;
        p->last_q1 = upd->q1; p->last_q2 = upd->q2; p->last_q3 = upd->q3;
    } else if (p->last_sent_state.object_count != upd->object_count || memcmp(p->last_sent_state.objects, upd->objects, upd->object_count * sizeof(NetObject)) != 0) {
        mask |= UPD_OBJECTS;
    }

    if (mask == 0) return; /* Nothing to send */

    /* 2. Serialize Packet - Need enough space for 64 beams and 256 objects */
    uint8_t buffer[65536]; 
    PacketUpdateDelta *delta = (PacketUpdateDelta *)buffer;
    delta->type = PKT_UPDATE_DELTA;
    delta->frame_id = upd->frame_id;
    delta->update_mask = mask;
    
    uint8_t *ptr = delta->data;
    
    if (mask & UPD_TRANSFORM) {
        UpdateBlockTransform b = {
            upd->q1, 
            upd->q2, 
            upd->q3, 
            upd->s1, 
            upd->s2, 
            upd->s3, 
            upd->van_h, 
            upd->van_m,
            upd->van_r,
            upd->eta
        };
        memcpy(ptr, &b, sizeof(b));
        ptr += sizeof(b);
    }
    if (mask & UPD_VITALS) {
        UpdateBlockVitals b = {
            upd->energy, 
            upd->torpedoes, 
            upd->cargo_energy, 
            upd->cargo_torpedoes, 
            upd->crew_count, 
            upd->prison_unit, 
            upd->composite_plating, 
            upd->hull_integrity
        };
        memcpy(ptr, &b, sizeof(b));
        ptr += sizeof(b);
    }
    if (mask & UPD_SHIELDS) {
        UpdateBlockShields b;
        memcpy(b.shields, upd->shields, sizeof(upd->shields));
        memcpy(ptr, &b, sizeof(b));
        ptr += sizeof(b);
    }
    if (mask & UPD_SYSTEMS) {
        UpdateBlockSystems b;
        memcpy(b.system_health, upd->system_health, sizeof(upd->system_health));
        memcpy(ptr, &b, sizeof(b));
        ptr += sizeof(b);
    }
    if (mask & UPD_INTERNAL) {
        UpdateBlockInternal b;
        memcpy(b.inventory, upd->inventory, sizeof(upd->inventory));
        memcpy(b.power_dist, upd->power_dist, sizeof(upd->power_dist));
        b.life_support = upd->life_support;
        b.anti_matter_count = upd->anti_matter_count;
        memcpy(ptr, &b, sizeof(b));
        ptr += sizeof(b);
    }
    if (mask & UPD_COMBAT) {
        UpdateBlockCombat b = {
            upd->lock_target, 
            upd->tube_state, 
            {0}, 
            upd->current_tube, 
            upd->ion_beam_charge
        };
        memcpy(b.tube_load_timers, upd->tube_load_timers, sizeof(upd->tube_load_timers));
        memcpy(ptr, &b, sizeof(b));
        ptr += sizeof(b);
        /* Beams inside combat or separate? Let's add beams if count > 0 */
        int32_t bc = upd->beam_count;
        memcpy(ptr, &bc, sizeof(int32_t));
        ptr += sizeof(int32_t);
        if (bc > 0) {
            memcpy(ptr, upd->beams, bc * sizeof(NetBeam));
            ptr += bc * sizeof(NetBeam);
        }
    }
    if (mask & UPD_FLAGS) {
        UpdateBlockFlags b = {
            upd->is_cloaked, 
            upd->is_docked, 
            upd->red_alert, 
            upd->is_jammed, 
            upd->nav_state, 
            upd->show_axes, 
            upd->show_grid, 
            upd->show_bridge, 
            upd->show_map, 
            upd->map_filter,
            upd->shm_crypto_algo,
            upd->encryption_flags
        };
        memcpy(ptr, &b, sizeof(b));
        ptr += sizeof(b);
    }
    if (mask & UPD_EFFECTS) {
        UpdateBlockEffects b;
        memset(&b, 0, sizeof(b));
        b.supernova_pos = upd->supernova_pos;
        memcpy(b.supernova_q, upd->supernova_q, sizeof(upd->supernova_q));
        memcpy(b.torps, upd->torps, sizeof(upd->torps));
        b.wormhole = upd->wormhole;
        b.event_count = upd->event_count;
        if (b.event_count > MAX_NET_EVENTS) b.event_count = MAX_NET_EVENTS;
        memcpy(b.events, upd->events, b.event_count * sizeof(NetEvent));
        b.torpedo_count = upd->torpedo_count;
        if (b.torpedo_count > MAX_VISIBLE_TORPEDOES) b.torpedo_count = MAX_VISIBLE_TORPEDOES;
        memcpy(b.visible_torpedoes, upd->visible_torpedoes, b.torpedo_count * sizeof(NetVisibleTorpedo));
        memcpy(ptr, &b, sizeof(b));
        ptr += sizeof(b);
    }
    if (mask & UPD_PROBES) {
        UpdateBlockProbes b;
        memcpy(b.probes, upd->probes, sizeof(upd->probes));
        memcpy(ptr, &b, sizeof(b));
        ptr += sizeof(b);
    }
    if (mask & UPD_OBJECTS) {
        int32_t oc = upd->object_count;
        memcpy(ptr, &oc, sizeof(int32_t));
        ptr += sizeof(int32_t);
        if (oc > 0) {
            memcpy(ptr, upd->objects, oc * sizeof(NetObject));
            ptr += oc * sizeof(NetObject);
        }
    }
    if (mask & UPD_MAP) {
        UpdateBlockMap b = {
            upd->map_update_val, 
            {upd->map_update_q[0], upd->map_update_q[1], upd->map_update_q[2]}, 
            upd->map_update_val2, 
            {upd->map_update_q2[0], upd->map_update_q2[1], upd->map_update_q2[2]}
        };
        memcpy(ptr, &b, sizeof(b));
        ptr += sizeof(b);
    }

    size_t total_size = ptr - buffer;
    pthread_mutex_lock(&p->socket_mutex);
    write_all(p->socket, buffer, total_size);
    pthread_mutex_unlock(&p->socket_mutex);
    
    /* 3. Store state for next delta */
    memcpy(&p->last_sent_state, upd, sizeof(PacketUpdate));
}
