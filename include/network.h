/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 */

#ifndef NETWORK_H
#define NETWORK_H

#include "game_config.h"
#include "game_state.h"

#pragma pack(push, 1)

#define DEFAULT_PORT 5000
#define MAX_CLIENTS GAME_MAX_PLAYERS
#define PKT_LOGIN 1
#define PKT_COMMAND 2
#define PKT_UPDATE 3
#define PKT_MESSAGE 4
#define PKT_QUERY 5
#define PKT_HANDSHAKE 6
#define PKT_UPDATE_DELTA 7
#define PKT_QUERY_KEY 8

/* Update Mask Bits for Delta Compression */
#define UPD_TRANSFORM (1ULL << 0)
#define UPD_VITALS    (1ULL << 1)
#define UPD_SHIELDS   (1ULL << 2)
#define UPD_SYSTEMS   (1ULL << 3)
#define UPD_INTERNAL  (1ULL << 4)
#define UPD_COMBAT    (1ULL << 5)
#define UPD_FLAGS     (1ULL << 6)
#define UPD_EFFECTS   (1ULL << 7)
#define UPD_PROBES    (1ULL << 8)
#define UPD_OBJECTS   (1ULL << 9)
#define UPD_MAP       (1ULL << 10)
#define UPD_FULL      (0xFFFFFFFFFFFFFFFFULL)

/* Delta Compression Blocks */
typedef struct {
    int32_t q1, q2, q3;
    double s1, s2, s3;
    double van_h, van_m, van_r;
    double eta;
} UpdateBlockTransform;

typedef struct {
    uint64_t energy;
    int32_t torpedoes;
    uint64_t cargo_energy;
    int32_t cargo_torpedoes;
    int32_t crew_count;
    int32_t prison_unit;
    int32_t composite_plating;
    double hull_integrity;
} UpdateBlockVitals;

typedef struct {
    int32_t shields[6];
} UpdateBlockShields;

typedef struct {
    double system_health[10];
} UpdateBlockSystems;

typedef struct {
    int32_t inventory[10];
    double power_dist[3];
    double life_support;
    int32_t anti_matter_count;
} UpdateBlockInternal;

typedef struct {
    int32_t lock_target;
    int32_t tube_state;
    int32_t tube_load_timers[4];
    int32_t tube_torpedo_etas[4];
    int32_t current_tube;
    double ion_beam_charge;
} UpdateBlockCombat;

typedef struct {
    uint8_t is_cloaked;
    uint8_t is_docked;
    uint8_t red_alert;
    uint8_t is_jammed;
    uint8_t nav_state;
    uint8_t show_axes;
    uint8_t show_grid;
    uint8_t show_bridge;
    uint8_t show_map;
    uint8_t map_filter;
    uint8_t force_shutdown;
    uint8_t encryption_algo;
    uint32_t encryption_flags;
    int32_t radio_lock_target;
} UpdateBlockFlags;

typedef struct {
    NetPoint supernova_pos; 
    int32_t supernova_q[3];
    NetPoint torps[4];
    NetPoint wormhole;
    int32_t event_count;
    NetEvent events[MAX_NET_EVENTS];
    int32_t torpedo_count;
    NetVisibleTorpedo visible_torpedoes[MAX_VISIBLE_TORPEDOES];
} UpdateBlockEffects;

typedef struct {
    NetProbe probes[3];
} UpdateBlockProbes;

typedef struct {
    int32_t beam_count;
    NetBeam beams[MAX_NET_BEAMS];
} UpdateBlockBeams;

typedef struct {
    int32_t object_count;
    NetObject objects[MAX_NET_OBJECTS];
} UpdateBlockObjects;

typedef struct {
    int64_t map_update_val;
    int32_t map_update_q[3];
    int64_t map_update_val2;
    int32_t map_update_q2[3];
} UpdateBlockMap;

typedef struct {
    int32_t type;
    int64_t frame_id;
    uint64_t update_mask;
    uint8_t data[]; /* Variable length payload */
} PacketUpdateDelta;

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
#define CRYPTO_MCELIECE 13
#define CRYPTO_DILITHIUM 14
#define CRYPTO_SERPENT   15
#define CRYPTO_TWOFISH   16
#define CRYPTO_SM4       17
#define CRYPTO_ASCON     18
#define CRYPTO_PRESENT   19
#define CRYPTO_GOST      20
#define CRYPTO_SALSA     21

#define MAX_CRYPTO_ALGOS 21

#define SCOPE_GLOBAL 0
#define SCOPE_FACTION 1
#define SCOPE_PRIVATE 2

typedef enum {
    FACTION_ALLIANCE = 0,
    FACTION_KORTHIAN = 10,
    FACTION_XYLARI = 11,
    FACTION_SWARM = 12,
    FACTION_VESPERIAN = 13,
    FACTION_JEM_HADAR = 14,
    FACTION_THOLIAN = 15,
    FACTION_GORN = 16,
    FACTION_GILDED = 17,
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
    SHIP_CLASS_EXPLORER,          /* Aegis-D style */
    SHIP_CLASS_FLAGSHIP,       /* Aegis-E style */
    SHIP_CLASS_SCIENCE,        /* Advanced Science style */
    SHIP_CLASS_CARRIER,           /* Heavy Escort / Carrier */
    SHIP_CLASS_TACTICAL,          /* Galaxy-variant with pod */
    SHIP_CLASS_DIPLOMATIC,      /* Aegis-C style */
    SHIP_CLASS_RESEARCH,          /* Small Science Ship */
    SHIP_CLASS_FRIGATE,     /* Specialized Escort */
    SHIP_CLASS_GENERIC_ALIEN
} ShipClass;

typedef struct {
    int32_t type;
    char target_name[64];
    uint8_t x25519_pubkey[32];
    int32_t found;
} PacketQueryKey;

typedef struct {
    int32_t type;
    char name[64];
    int32_t faction;
    int32_t ship_class;
    uint8_t pass_hash[32];
    uint8_t x25519_pubkey[32];
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
    uint8_t crypto_algo; /* 1-11:Standard, 12-21:Advanced/PQC */
    uint8_t iv[16];      /* Full 128-bit IV for CBC/CTR/GCM */
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
    double s1, s2, s3;
    double van_h, van_m, van_r;
    double eta;
    uint64_t energy;
    int32_t torpedoes;
    uint64_t cargo_energy;
    int32_t cargo_torpedoes;
    int32_t crew_count;
    int32_t prison_unit;
    int32_t composite_plating;
    double hull_integrity;
    int32_t shields[6];
    int32_t inventory[10];
    double system_health[10];
    double power_dist[3];
    double life_support;
    int32_t anti_matter_count;
    int32_t lock_target;
    int32_t tube_state;
    int32_t tube_load_timers[4];
    int32_t tube_torpedo_etas[4];
    int32_t current_tube;
    double ion_beam_charge;
    uint8_t is_cloaked;
    uint8_t is_docked;
    uint8_t show_axes;
    uint8_t show_grid;
    uint8_t show_bridge;
    uint8_t show_map;
    uint8_t map_filter;
    uint8_t force_shutdown;
    uint8_t shm_crypto_algo;
    uint32_t encryption_flags;
    int32_t radio_lock_target;
    uint8_t red_alert;
    uint8_t is_jammed;
    uint8_t nav_state;
    NetPoint supernova_pos; 
    int32_t supernova_q[3];
    int64_t map_update_val;
    int32_t map_update_q[3];
    int64_t map_update_val2;
    int32_t map_update_q2[3];
    NetPoint torps[4];
    NetPoint wormhole;
    int32_t event_count;
    NetEvent events[MAX_NET_EVENTS];
    int32_t torpedo_count;
    NetVisibleTorpedo visible_torpedoes[MAX_VISIBLE_TORPEDOES];
    NetProbe probes[3];
    int32_t beam_count;
    NetBeam beams[MAX_NET_BEAMS];
    int32_t object_count;
    NetObject objects[MAX_NET_OBJECTS];
} PacketUpdate;

#pragma pack(pop)

#endif
