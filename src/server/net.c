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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <stddef.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include "network.h"
#include "server_internal.h"

extern uint8_t deep_space_key[32];
extern void ensure_player_algo_key(int p_idx, int k, bool private_mode);

int read_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    char *p = (char *)buf;
    while (total < len) {
        ssize_t n = recv(fd, p + total, len - total, 0);
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
    else { cipher = EVP_aes_256_gcm(); is_gcm = 1; }
    
    if (!cipher) { cipher = EVP_aes_256_gcm(); is_gcm = 1; }
    
    EVP_EncryptInit_ex(ctx, cipher, NULL, NULL, NULL);
    if (is_gcm) EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 16, NULL);
    
    if (EVP_EncryptInit_ex(ctx, NULL, NULL, key, msg->iv) <= 0) {
        msg->is_encrypted = 0;
        size_t plen = strlen(plaintext);
        if (plen > 65535) plen = 65535;
        memcpy(msg->text, plaintext, plen);
        msg->text[plen] = '\0';
        msg->length = plen;
        EVP_CIPHER_CTX_free(ctx);
        return;
    }
    
    int outlen;
    EVP_EncryptUpdate(ctx, (uint8_t*)msg->text, &outlen, (const uint8_t*)plaintext, plaintext_len);
    int final_len;
    if (EVP_EncryptFinal_ex(ctx, (uint8_t*)msg->text + outlen, &final_len) <= 0) {
        msg->is_encrypted = 0;
        size_t plen = strlen(plaintext);
        if (plen > 65535) plen = 65535;
        memcpy(msg->text, plaintext, plen);
        msg->text[plen] = '\0';
        msg->length = plen;
        EVP_CIPHER_CTX_free(ctx);
        return;
    }
    
    if (is_gcm) EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, msg->tag);
    else memset(msg->tag, 0, 16);
    
    msg->origin_frame = frame_id;
    msg->length = outlen + final_len;
    EVP_CIPHER_CTX_free(ctx);
}

void broadcast_task(void *arg) {
    PacketMessage *pkt = (PacketMessage *)arg;
    if (pkt) {
        broadcast_message(pkt);
        free(pkt);
    }
}

void broadcast_message(PacketMessage *msg) {
    char *plaintext = malloc(65536);
    if (!plaintext) return;
    memset(plaintext, 0, 65536);

    pthread_mutex_lock(&game_mutex);
    
    int sender_idx = -1;
    for(int j=0; j<MAX_CLIENTS; j++) {
        if (players[j].active && strcmp(players[j].name, msg->from) == 0) {
            sender_idx = j;
            break;
        }
    }

    /* 1. CLASSIFY AND VALIDATE */
    bool is_encrypted = (msg->is_encrypted != 0);
    bool is_private = (msg->is_encrypted & 0x02);
    bool is_peer = (msg->is_encrypted & 0x04);
    bool is_exclusive_msg = (msg->is_encrypted & 0x08);

    if (is_encrypted && sender_idx != -1) {
        if (is_peer || is_private || is_exclusive_msg) {
            /* PRIVACY ENFORCEMENT: We don't touch these. We relay the encrypted blob. */
            LOG_DEBUG("Encrypted Private/Peer/Exclusive message from %s. Relay mode: TRANSPARENT.\n", msg->from);
        } else {
            /* Level A (Fleet): Server must decrypt to allow cross-algorithm talk and cleartext fallback */
            EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
            const EVP_CIPHER *cipher;
            int is_gcm = 0;
            int algo = msg->crypto_algo;
            
            ensure_player_algo_key(sender_idx, algo, false);

            if (algo == CRYPTO_CHACHA) { cipher = EVP_chacha20_poly1305(); is_gcm = 1; }
            else if (algo == CRYPTO_ARIA) { cipher = EVP_aria_256_gcm(); is_gcm = 1; }
            else if (algo == CRYPTO_CAMELLIA) { cipher = EVP_camellia_256_ctr(); is_gcm = 0; }
            else { cipher = EVP_aes_256_gcm(); is_gcm = 1; }

            uint8_t *k = players[sender_idx].algo_keys[algo];
            EVP_DecryptInit_ex(ctx, cipher, NULL, NULL, NULL);
            if (is_gcm) EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 16, NULL);
            EVP_DecryptInit_ex(ctx, NULL, NULL, k, msg->iv);
            int outlen;
            EVP_DecryptUpdate(ctx, (uint8_t*)plaintext, &outlen, (const uint8_t*)msg->text, msg->length);
            if (is_gcm) EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, msg->tag);
            int final_len;
            if (EVP_DecryptFinal_ex(ctx, (uint8_t*)plaintext + outlen, &final_len) <= 0) {
                /* Decryption failed */
                send_server_msg(sender_idx, "COMPUTER", "TRANSMISSION ERROR: Frequency parity failure.");
                EVP_CIPHER_CTX_free(ctx); pthread_mutex_unlock(&game_mutex); free(plaintext); return;
            }
            plaintext[outlen + final_len] = '\0';
            EVP_CIPHER_CTX_free(ctx);
        }
    } else {
        /* Cleartext message */
        size_t c_len = (msg->length < 65535) ? msg->length : 65535;
        memcpy(plaintext, msg->text, c_len);
        plaintext[c_len] = '\0';
    }

    /* 2. BROADCAST LOOP */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].active && players[i].socket != 0) {
            
            /* LEVEL D: EXCLUSIVE MODE HARD-LOCK */
            if (is_exclusive_msg) {
                /* Resolve destination: Priority 1: Packet target_id, Priority 2: Sender Lock, Priority 3: Reciprocity */
                int target_idx = -1;
                
                if (msg->target_id > 0) {
                    target_idx = msg->target_id - 1;
                } else if (sender_idx != -1) {
                    target_idx = players[sender_idx].radio_lock_target - 1;
                    if (target_idx < 0) {
                        for (int j = 0; j < MAX_CLIENTS; j++) {
                            if (players[j].active && players[j].radio_lock_target == (sender_idx + 1)) {
                                target_idx = j;
                                break;
                            }
                        }
                    }
                }
                
                /* Deliver ONLY to sender (loopback) and resolved target */
                if (i != sender_idx && i != target_idx) {
                    continue; 
                }
            } else {
                /* STANDARD FILTERS (only for non-exclusive messages) */
                if (msg->scope == SCOPE_FACTION && players[i].faction != msg->faction) continue;
                if (msg->scope == SCOPE_PRIVATE && (i + 1) != msg->target_id && i != sender_idx) continue;

                /* Recipient-side Exclusive Lock: if recipient is in enc4, they only hear their partner */
                if (players[i].radio_lock_target > 0) {
                    int locked_partner_idx = players[i].radio_lock_target - 1;
                    bool is_sys = (strcmp(msg->from, "SERVER") == 0 || strcmp(msg->from, "COMPUTER") == 0 ||
                                   strcmp(msg->from, "SCIENCE") == 0 || strcmp(msg->from, "TACTICAL") == 0 ||
                                   strcmp(msg->from, "ENGINEERING") == 0 || strcmp(msg->from, "HELMSMAN") == 0 ||
                                   strcmp(msg->from, "WARNING") == 0 || strcmp(msg->from, "DAMAGE CONTROL") == 0 ||
                                   strcmp(msg->from, "STARBASE") == 0 || strcmp(msg->from, "Alliance Command") == 0);
                    
                    if (!is_sys && sender_idx != locked_partner_idx && i != sender_idx) {
                        continue;
                    }
                }
            }

            /* RELAY EXECUTION */
            if (is_peer || is_private || is_exclusive_msg) {
                /* STRICT RELAY: No one gets cleartext if they are not the intended recipient with the key.
                   This includes Exclusive Mode (0x08) to prevent noise on other frequencies. */
                size_t pkt_size = offsetof(PacketMessage, text) + msg->length;
                pthread_mutex_lock(&players[i].socket_mutex);
                if (players[i].socket != 0) write_all(players[i].socket, msg, pkt_size);
                pthread_mutex_unlock(&players[i].socket_mutex);
            } else {
                /* ADAPTIVE RELAY: Translate for recipient or send cleartext */
                PacketMessage *out_pkt = malloc(sizeof(PacketMessage));
                if (!out_pkt) continue;
                memcpy(out_pkt, msg, offsetof(PacketMessage, text));
                int recip_algo = players[i].state.shm_crypto_algo;

                if (recip_algo != CRYPTO_NONE) {
                    /* Recipient wants encryption: encrypt the validated plaintext */
                    /* Fallback to player's key if it exists */
                    uint8_t *rk = (recip_algo >= 1 && recip_algo <= MAX_CRYPTO_ALGOS) ? 
                                   players[i].algo_keys[recip_algo] : players[i].session_key;
                    
                    out_pkt->crypto_algo = recip_algo;
                    out_pkt->is_encrypted = 0x01;
                    encrypt_payload(out_pkt, plaintext, rk, spacegl_master.frame_id);
                } else {
                    /* Recipient wants cleartext */
                    out_pkt->is_encrypted = 0;
                    out_pkt->crypto_algo = CRYPTO_NONE;
                    size_t plen = strlen(plaintext);
                    if (plen > 65535) plen = 65535;
                    memcpy(out_pkt->text, plaintext, plen);
                    out_pkt->text[plen] = '\0';
                    out_pkt->length = plen;
                }

                size_t pkt_size = offsetof(PacketMessage, text) + out_pkt->length;
                pthread_mutex_lock(&players[i].socket_mutex);
                if (players[i].socket != 0) write_all(players[i].socket, out_pkt, pkt_size);
                pthread_mutex_unlock(&players[i].socket_mutex);
                free(out_pkt);
            }
        }
    }
    pthread_mutex_unlock(&game_mutex);
    free(plaintext);
}

void send_server_msg(int p_idx, const char *from, const char *text) {
    PacketMessage msg;
    memset(&msg, 0, sizeof(PacketMessage));
    msg.type = PKT_MESSAGE;
    strncpy(msg.from, from, 63);
    
    if (p_idx == -1) {
        /* Broadcast system message to all */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (players[i].active && players[i].socket != 0) {
                send_server_msg(i, from, text);
            }
        }
        return;
    }

    if (players[p_idx].state.shm_crypto_algo != CRYPTO_NONE) {
        msg.is_encrypted = 1;
        msg.crypto_algo = players[p_idx].state.shm_crypto_algo;
        
        uint8_t *k = (msg.crypto_algo >= 1 && msg.crypto_algo <= MAX_CRYPTO_ALGOS) ? players[p_idx].algo_keys[msg.crypto_algo] : players[p_idx].session_key;
        
        encrypt_payload(&msg, text, k, spacegl_master.frame_id);
    } else {
        msg.is_encrypted = 0;
        size_t plen = strlen(text);
        if (plen > 65535) plen = 65535;
        memcpy(msg.text, text, plen);
        msg.text[plen] = '\0';
        msg.length = plen;
    }
    
    size_t pkt_size = offsetof(PacketMessage, text) + msg.length;
    pthread_mutex_lock(&players[p_idx].socket_mutex);
    if (players[p_idx].socket != 0) write_all(players[p_idx].socket, &msg, pkt_size);
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
        memcmp(p->last_sent_state.tube_load_timers, upd->tube_load_timers, sizeof(upd->tube_load_timers)) != 0 ||
        memcmp(p->last_sent_state.tube_torpedo_etas, upd->tube_torpedo_etas, sizeof(upd->tube_torpedo_etas)) != 0) mask |= UPD_COMBAT;
    
    if (p->last_sent_state.is_cloaked != upd->is_cloaked || p->last_sent_state.is_docked != upd->is_docked ||
        p->last_sent_state.red_alert != upd->red_alert || p->last_sent_state.nav_state != upd->nav_state ||
        p->last_sent_state.show_axes != upd->show_axes || p->last_sent_state.show_grid != upd->show_grid ||
        p->last_sent_state.show_bridge != upd->show_bridge || p->last_sent_state.show_map != upd->show_map ||
        p->last_sent_state.map_filter != upd->map_filter ||
        p->last_sent_state.force_shutdown != upd->force_shutdown ||
        p->last_sent_state.shm_crypto_algo != upd->shm_crypto_algo ||
        p->last_sent_state.radio_lock_target != upd->radio_lock_target ||
        p->last_sent_state.encryption_flags != upd->encryption_flags) mask |= UPD_FLAGS;

    bool any_torp = upd->torps[0].active || upd->torps[1].active || upd->torps[2].active || upd->torps[3].active;
    bool last_any_torp = p->last_sent_state.torps[0].active || p->last_sent_state.torps[1].active || p->last_sent_state.torps[2].active || p->last_sent_state.torps[3].active;
    
    if (upd->event_count > 0 || upd->torpedo_count > 0 || any_torp || (any_torp != last_any_torp) || upd->wormhole.active || upd->supernova_pos.active) mask |= UPD_EFFECTS;
    if (upd->probes[0].active || upd->probes[1].active || upd->probes[2].active) mask |= UPD_PROBES;
    
    if (upd->beam_count > 0 || p->last_sent_state.beam_count > 0) mask |= UPD_COMBAT; 
    
    bool quadrant_changed = (p->last_q1 != upd->q1 || p->last_q2 != upd->q2 || p->last_q3 != upd->q3);
    p->full_update_timer++;
    if (quadrant_changed || p->full_update_timer > (5 * GAME_TICK_RATE)) {
        mask |= UPD_OBJECTS | UPD_MAP | UPD_FULL;
        p->full_update_timer = 0;
        p->last_q1 = upd->q1; p->last_q2 = upd->q2; p->last_q3 = upd->q3;
    } else if (p->last_sent_state.object_count != upd->object_count || memcmp(p->last_sent_state.objects, upd->objects, upd->object_count * sizeof(NetObject)) != 0) {
        mask |= UPD_OBJECTS;
    }

    if (mask == 0) return; 

    uint8_t *buffer = malloc(65536);
    if (!buffer) return;
    PacketUpdateDelta *delta = (PacketUpdateDelta *)buffer;
    delta->type = PKT_UPDATE_DELTA;
    delta->frame_id = upd->frame_id;
    delta->update_mask = mask;
    
    uint8_t *ptr = delta->data;
    
    if (mask & UPD_TRANSFORM) {
        UpdateBlockTransform b = {upd->q1, upd->q2, upd->q3, upd->s1, upd->s2, upd->s3, upd->van_h, upd->van_m, upd->van_r, upd->eta};
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }
    if (mask & UPD_VITALS) {
        UpdateBlockVitals b = {upd->energy, upd->torpedoes, upd->cargo_energy, upd->cargo_torpedoes, upd->crew_count, upd->prison_unit, upd->composite_plating, upd->hull_integrity};
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }
    if (mask & UPD_SHIELDS) {
        UpdateBlockShields b; memcpy(b.shields, upd->shields, sizeof(upd->shields));
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }
    if (mask & UPD_SYSTEMS) {
        UpdateBlockSystems b; memcpy(b.system_health, upd->system_health, sizeof(upd->system_health));
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }
    if (mask & UPD_INTERNAL) {
        UpdateBlockInternal b; memcpy(b.inventory, upd->inventory, sizeof(upd->inventory)); memcpy(b.power_dist, upd->power_dist, sizeof(upd->power_dist));
        b.life_support = upd->life_support; b.anti_matter_count = upd->anti_matter_count;
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }
    if (mask & UPD_COMBAT) {
        UpdateBlockCombat b = {upd->lock_target, upd->tube_state, {0}, {0}, upd->current_tube, upd->ion_beam_charge};
        memcpy(b.tube_load_timers, upd->tube_load_timers, sizeof(b.tube_load_timers));
        memcpy(b.tube_torpedo_etas, upd->tube_torpedo_etas, sizeof(b.tube_torpedo_etas));
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
        /* Append beams if any */
        int32_t bc = upd->beam_count; memcpy(ptr, &bc, sizeof(int32_t)); ptr += sizeof(int32_t);
        if (bc > 0) { memcpy(ptr, upd->beams, bc * sizeof(NetBeam)); ptr += bc * sizeof(NetBeam); }
    }
    if (mask & UPD_FLAGS) {
        UpdateBlockFlags b = {upd->is_cloaked, upd->is_docked, upd->red_alert, upd->is_jammed, upd->nav_state, upd->show_axes, upd->show_grid, upd->show_bridge, upd->show_map, upd->map_filter, upd->force_shutdown, upd->shm_crypto_algo, upd->encryption_flags, upd->radio_lock_target};
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }
    if (mask & UPD_EFFECTS) {
        UpdateBlockEffects b; memset(&b, 0, sizeof(b));
        b.supernova_pos = upd->supernova_pos; memcpy(b.supernova_q, upd->supernova_q, sizeof(upd->supernova_q));
        memcpy(b.torps, upd->torps, sizeof(upd->torps)); b.wormhole = upd->wormhole;
        b.event_count = upd->event_count; if (b.event_count > MAX_NET_EVENTS) b.event_count = MAX_NET_EVENTS;
        memcpy(b.events, upd->events, b.event_count * sizeof(NetEvent));
        b.torpedo_count = upd->torpedo_count; if (b.torpedo_count > MAX_VISIBLE_TORPEDOES) b.torpedo_count = MAX_VISIBLE_TORPEDOES;
        memcpy(b.visible_torpedoes, upd->visible_torpedoes, b.torpedo_count * sizeof(NetVisibleTorpedo));
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }
    if (mask & UPD_PROBES) {
        UpdateBlockProbes b; memcpy(b.probes, upd->probes, sizeof(upd->probes));
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }
    if (mask & UPD_OBJECTS) {
        int32_t oc = upd->object_count; memcpy(ptr, &oc, sizeof(int32_t)); ptr += sizeof(int32_t);
        if (oc > 0) { memcpy(ptr, upd->objects, oc * sizeof(NetObject)); ptr += oc * sizeof(NetObject); }
    }
    if (mask & UPD_MAP) {
        UpdateBlockMap b = {upd->map_update_val, {upd->map_update_q[0], upd->map_update_q[1], upd->map_update_q[2]}, upd->map_update_val2, {upd->map_update_q2[0], upd->map_update_q2[1], upd->map_update_q2[2]}};
        memcpy(ptr, &b, sizeof(b)); ptr += sizeof(b);
    }

    size_t total_size = ptr - buffer;
    pthread_mutex_lock(&p->socket_mutex);
    if (p->socket != 0) write_all(p->socket, buffer, total_size);
    pthread_mutex_unlock(&p->socket_mutex);
    memcpy(&p->last_sent_state, upd, sizeof(PacketUpdate));
    free(buffer);
}
