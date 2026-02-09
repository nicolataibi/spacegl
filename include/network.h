/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
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

#ifndef NETWORK_H
#define NETWORK_H

#include "game_state.h"

#pragma pack(push, 1)

#define DEFAULT_PORT 5000
#define MAX_CLIENTS 32
#define PKT_LOGIN 1
#define PKT_COMMAND 2
#define PKT_UPDATE 3
#define PKT_MESSAGE 4
#define PKT_QUERY 5
#define PKT_HANDSHAKE 6

/* Magic Signature for Key Verification (32 bytes) */
#define HANDSHAKE_MAGIC_STRING "SPACEGL-KEY-VERIFICATION-SIG-32B"

#define CRYPTO_NONE 0
#define CRYPTO_AES  1
#define CRYPTO_CHACHA 2
#define CRYPTO_ARIA 3
#define CRYPTO_CAMELLIA 4
#define CRYPTO_SEED     5
#define CRYPTO_CAST5    6
#define CRYPTO_IDEA     7
#define CRYPTO_3DES     8
#define CRYPTO_BLOWFISH 9
#define CRYPTO_RC4      10
#define CRYPTO_DES      11
#define CRYPTO_PQC      12

#define SCOPE_GLOBAL 0
#define SCOPE_FACTION 1
#define SCOPE_PRIVATE 2

typedef enum {
    FACTION_ALLIANCE = 0,
    FACTION_KORTHIAN = 10,
    FACTION_XYLARI = 11,
    FACTION_SWARM = 12,
    FACTION_CARDASSIAN = 13,
    FACTION_JEM_HADAR = 14,
    FACTION_THOLIAN = 15,
    FACTION_GORN = 16,
    FACTION_FERENGI = 17,
    FACTION_SPECIES_8472 = 18,
    FACTION_BREEN = 19,
    FACTION_HIROGEN = 20
} Faction;

typedef enum {
    SHIP_CLASS_LEGACY = 0,
    SHIP_CLASS_SCOUT,
    SHIP_CLASS_HEAVY_CRUISER,
    SHIP_CLASS_MULTI_ENGINE,
    SHIP_CLASS_ESCORT,
    SHIP_CLASS_EXPLORER,          /* Vanguard-D style */
    SHIP_CLASS_FLAGSHIP,       /* Vanguard-E style */
    SHIP_CLASS_SCIENCE,        /* Voyager style */
    SHIP_CLASS_CARRIER,           /* Heavy Escort / Carrier */
    SHIP_CLASS_TACTICAL,          /* Galaxy-variant with pod */
    SHIP_CLASS_DIPLOMATIC,      /* Vanguard-C style */
    SHIP_CLASS_RESEARCH,          /* Small Science Ship */
    SHIP_CLASS_FRIGATE,     /* Specialized Escort */
    SHIP_CLASS_GENERIC_ALIEN
} ShipClass;

typedef struct {
    int32_t type;
    char name[64];
    int32_t faction;
    int32_t ship_class;
} PacketLogin;

typedef struct {
    int32_t type;
    char cmd[256];
} PacketCommand;

typedef struct {
    int32_t type;
    int32_t pubkey_len;
    uint8_t pubkey[256]; /* Standard EC Public Key */
} PacketHandshake;

typedef struct {
    int32_t type;
    char from[64];
    int32_t faction;
    int32_t scope; /* 0: Global, 1: Faction, 2: Private */
    int32_t target_id; /* Player ID (1-based) for Private Message */
    int32_t length;
    int64_t origin_frame; /* Server frame used for frequency scrambling */
    uint8_t is_encrypted;
    uint8_t crypto_algo; /* 1:AES... 11:DES, 12:PQC (ML-KEM/Kyber) */
    uint8_t iv[12];      /* GCM/Poly/CTR/CBC IV */
    uint8_t tag[16];     /* Auth Tag */
    uint8_t has_signature;
    uint8_t signature[64]; /* Ed25519 Signature */
    uint8_t sender_pubkey[32]; 
    char text[65536];
} PacketMessage;

/* Update Packet: Optimized for variable length transmission */
typedef struct {
    int32_t type;
    int64_t frame_id;
    int32_t q1, q2, q3;
    float s1, s2, s3;
    float van_h, van_m;
    int32_t energy;
    int32_t torpedoes;
    int32_t cargo_energy;
    int32_t cargo_torpedoes;
    int32_t crew_count;
    int32_t prison_unit;
    int32_t composite_plating;
    float hull_integrity;
    int32_t shields[6];
    int32_t inventory[10];
    float system_health[10];
    float power_dist[3];
    float life_support;
    int32_t anti_matter_count;
    int32_t lock_target;
    int32_t tube_state;
    float ion_beam_charge;
    uint8_t is_cloaked;
    uint8_t encryption_enabled;
    NetPoint supernova_pos; 
    int32_t supernova_q[3];
    int64_t map_update_val;
    int32_t map_update_q[3];
    NetPoint torp;
    NetPoint boom;
    NetPoint wormhole;
    NetPoint jump_arrival;
    NetDismantle dismantle;
    NetPoint recovery_fx;
    NetProbe probes[3];
    int32_t beam_count;
    NetBeam beams[MAX_NET_BEAMS];
    int32_t object_count;
    NetObject objects[MAX_NET_OBJECTS];
} PacketUpdate;

#pragma pack(pop)

#endif
