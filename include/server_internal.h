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

#ifndef SERVER_INTERNAL_H
#define SERVER_INTERNAL_H

#include <stdbool.h>
#include <pthread.h>
#include "network.h"
#include "game_config.h"
#include "sglog.h"

typedef enum { 
    NAV_STATE_IDLE, 
    NAV_STATE_ALIGN, 
    NAV_STATE_HYPERDRIVE, 
    NAV_STATE_REALIGN, 
    NAV_STATE_IMPULSE, 
    NAV_STATE_CHASE,
    NAV_STATE_ALIGN_IMPULSE,
    NAV_STATE_WORMHOLE,
    NAV_STATE_ALIGN_ONLY,
    NAV_STATE_DOCKING,
    NAV_STATE_DRIFT,
    NAV_STATE_ORBIT,
    NAV_STATE_SLINGSHOT,
    NAV_STATE_APPROACH
} NavState;

typedef struct {
    int socket;
    pthread_mutex_t socket_mutex;
    char name[64];
    int32_t faction;
    int ship_class;
    int active;
    int crypto_algo; /* 0:None, 1-11:Standard, 12-21:Advanced */
    uint8_t session_key[32]; /* Derived via ECDH/ML-KEM */
    uint8_t x25519_pubkey[32]; /* Tactical Peer Link Public Key */
    uint8_t algo_keys[MAX_CRYPTO_ALGOS + 1][32]; /* Personal Frequency Set */
    
    /* Navigation & Physics State */
    double gx, gy, gz;      /* Absolute Galactic Coordinates */
    double target_gx, target_gy, target_gz;
    double dx, dy, dz;      /* Movement Vector */
    double vx, vy, vz;      /* Delta velocity per tick */
    double target_h, target_m, target_r;
    double start_h, start_m, start_r;
    int nav_state;
    int nav_timer;
    double hyper_speed;
    double eta;
    double approach_dist;
    int apr_target;
    
    /* Torpedo System (4-Tube Rotary System) */
    struct {
        bool active;
        double tx, ty, tz;      /* Position */
        double tdx, tdy, tdz;   /* Vector */
        int target;
        int timeout;
    } torp_slots[4];
    
    int tube_load_timers[4];
    int tube_torpedo_etas[4];
    int current_tube;
    
    /* Global/Legacy compatibility (optional, but keep structure clean) */
    int torp_load_timer; 
    bool torp_active; /* Still used to signal "any torpedo active" for HUD simplify */
    
    /* Jump Visuals */
    double wx, wy, wz;      /* Wormhole entrance coords */
    int jump_type;          /* 1: Legacy, 2: Current */
    int is_docked;          /* Persistent docking state */
    int shield_regen_delay;
    int renegade_timer;     /* Ticks until faction forgives friendly fire */
    
    /* Boarding Interaction State */
    int pending_bor_target; /* ID of target player */
    int pending_bor_type;   /* 1: Ally, 2: Enemy, 3: Platform, 4: Derelict (NEVER a timer) */

    /* Alignment LERP initial duration (ticks) for the ALIGN / ALIGN_IMPULSE /
     * ALIGN_ONLY navigation states. Previously abused the pending_bor_type
     * field, which gave it a dual (board-type vs timer) meaning; now a
     * dedicated field. */
    int align_timer;

    int radio_lock_target;  /* ID of locked captain (1-based), 0 if none */

    int death_timer;        /* Ticks until final destruction explosion */
    
    bool fire_requested_this_tick;

    /* Optimization State */
    PacketUpdate last_sent_state;
    int last_q1, last_q2, last_q3;
    uint64_t full_update_timer;

    /* Per-player static PacketUpdate buffer: eliminates per-tick malloc at 60Hz */
    PacketUpdate upd_packet;
    /* Set to true by update_game_logic when a packet is ready; cleared by send_pending_updates */
    bool pending_send;

    uint32_t generation;
    SpaceGLGame state;
} __attribute__((aligned(64))) ConnectedPlayer;

#pragma pack(push, 1)

typedef enum {
    AI_STATE_PATROL = 0,
    AI_STATE_CHASE,
    AI_STATE_FLEE,
    AI_STATE_ATTACK_RUN,
    AI_STATE_ATTACK_POSITION
} AIState;

/* ThreadPool System */
typedef void (*thread_task_fn)(void *arg);

typedef struct thread_task {
    thread_task_fn function;
    void *arg;
    struct thread_task *next;
} thread_task_t;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t notify;
    pthread_t *threads;
    thread_task_t *queue_head;
    int thread_count;
    int queue_size;
    bool shutdown;
} threadpool_t;

/* ThreadPool Prototypes */
threadpool_t *threadpool_create(int thread_count);
int threadpool_add_task(threadpool_t *pool, thread_task_fn function, void *arg);
void threadpool_destroy(threadpool_t *pool);

/* Login sync task: once authentication succeeds, the giant Galaxy Master
 * transmission is delegated to the thread pool (dispatch_packet, PKT_LOGIN)
 * so the ~1 MB blocking write cannot stall the 60 Hz simulation. The worker
 * re-checks slot/generation before writing and before activating. */
typedef struct {
    int slot;
    int fd;
    uint32_t generation;
    bool is_new;
} SyncTask;
void sync_client_task(void *arg);

/* --- Celestial and Tactical Entities --- */

typedef struct { 
    int id;
    int faction;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int active; 
} NPCStar;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int active; 
} NPCBlackHole;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int type;
    int active; 
} NPCNebula;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int type;
    int active; 
} NPCPulsar;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int type; /* 0:Radio-loud, 1:Radio-quiet, 2:BAL, 3:Type 2, 4:Red, 5:OVV, 6:Weak emission */
    int active; 
} NPCQuasar;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z;
    double h;
    double m; 
    double a;
    double b;
    double angle;
    double speed;
    double inc; 
    double cx;
    double cy;
    double cz; 
    int active; 
} NPCComet;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    float size; 
    int resource_type;
    int amount;
    int active; 
} NPCAsteroid;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int ship_class; 
    int active; 
    int faction; 
    char name[64]; 
} NPCDerelict;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int faction; 
    int active; 
} NPCMine;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int active; 
} NPCBuoy;

typedef struct { 
    int id;
    int faction;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int health;
    uint64_t energy;
    int active; 
    int fire_cooldown; 
    int beam_count;
    NetBeam beams[4];
} NPCPlatform;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int active; 
} NPCRift;

typedef struct { 
    int id;
    int type;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int health;
    uint64_t energy;
    int active; 
    int behavior_timer; 
    int beam_count;
    NetBeam beams[4];
} NPCMonster;

typedef struct { 
    int id;
    int faction;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z;
    double h;
    double m; 
    double gx;
    double gy;
    double gz; /* Absolute Galactic Coordinates 0-100 */
    uint64_t energy;
    int active; 
    int health;
    int plating;
    float engine_health; 
    int fire_cooldown; 
    AIState ai_state; 
    int target_player_idx; 
    int nav_timer; 
    double dx;
    double dy;
    double dz;
    double vx;
    double vy;
    double vz;
    double tx;
    double ty;
    double tz; 
    uint8_t is_cloaked;
    int ship_class;
    int death_timer;
    char name[64];
    int beam_count;
    NetBeam beams[4];
} __attribute__((aligned(64))) NPCShip;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int resource_type;
    int amount;
    int active; 
} NPCPlanet;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int active; 
} NPCDyson;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int active; 
} NPCHub;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int active; 
} NPCRelic;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int active; 
} NPCRupture;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int active; 
} NPCSatellite;

typedef struct { 
    int id;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int active; 
} NPCStorm;

typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCArtifact;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCWarpGate;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCNeutronStar;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCMegaStructure;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCDarkCloud;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCSingularity;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCPlasmaStorm;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCOrbitalRing;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCTimeAnomaly;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCVoidCrystal;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCSubspaceAnomaly;

/* New Cosmic Objects */
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCDiffuseNebula;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCDarkNebula;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCPlanetaryNebula;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCSNR;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCGMC;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCInterstellarFilament;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCInterstellarBubble;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCBokGlobule;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCClumpCore;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCAccretionDisk;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCRelativisticJet;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCShockWave;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCStellarBowShock;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCCosmicVoid;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCCosmicFilament;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCEventHorizon;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCKilonova;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCGravLens;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCGRB;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCGravWave;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCProtoplanetaryDisk;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCDebrisDisk;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCPlanetesimal;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCRoguePlanet;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCBrownDwarf;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCISO;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCMagReconn;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCCurrentSheet;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCHeliosphere;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCTermShock;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCMagnetosphere;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCCosmicString;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCDomainWall;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCDMHalo;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCIGM;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCCGM;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCLymanAlpha;
typedef struct { int id; int q1; int q2; int q3; double x; double y; double z; int active; } NPCCMB;

/* Generic Cosmic Feature (Data-Oriented, Replaces individual structs) */
typedef struct {
    int id;
    int type;      /* Determines behavior, rendering class, faction etc. */
    int q1; int q2; int q3;
    double x; double y; double z;
    int active;
    char name[64]; /* Optional override, or handled globally by type */
} NPCCosmicFeature;

#define MAX_COSMIC_FEATURES 1024
#define MAX_Q_FEATURES 32

typedef struct { 
    int id;
    int faction;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int health;
    uint64_t energy;
    int active;
    int fire_cooldown;
    int beam_count;
    NetBeam beams[4];
} NPCBase;

typedef struct {
    int id;
    int owner_idx;
    int faction;
    int q1, q2, q3;
    double x, y, z;      /* Local coordinates */
    double gx, gy, gz;    /* Absolute coordinates */
    double dx, dy, dz;    /* Direction vector */
    int target_id;
    int timeout;
    int origin_tube;
    bool active;
} __attribute__((aligned(64))) PlayerTorpedo;

#define MAX_GLOBAL_TORPEDOES (MAX_CLIENTS * 4)

#pragma pack(pop)

/* --- Limits --- */

#define MAX_NPC 4000
#define MAX_PLANETS 2000
#define MAX_BASES 1000
#define MAX_STARS 4000
#define MAX_BH 1000
#define MAX_NEBULAS 1000
#define MAX_PULSARS 1000
#define MAX_QUASARS 1000
#define MAX_COMETS 1000
#define MAX_ASTEROIDS 3000
#define MAX_DERELICTS 3000
#define MAX_MINES 1500
#define MAX_BUOYS 1000
#define MAX_PLATFORMS 1000
#define MAX_RIFTS 1000
#define MAX_MONSTERS 1000
#define MAX_DYSON 1000
#define MAX_HUBS 1000
#define MAX_RELICS 1000
#define MAX_RUPTURES 1000
#define MAX_SATELLITES 1000
#define MAX_STORMS 1000
#define MAX_ARTIFACTS 1000
#define MAX_WARP_GATES 1000
#define MAX_NEUTRON_STARS 1000
#define MAX_MEGA_STRUCTS 1000
#define MAX_DARK_CLOUDS 1000
#define MAX_SINGULARITIES 1000
#define MAX_PLASMA_STORMS 1000
#define MAX_ORBITAL_RINGS 1000
#define MAX_TIME_ANOMALIES 1000
#define MAX_VOID_CRYSTALS 1000
#define MAX_SUBSPACE_ANOMALIES 1000

#define MAX_DIFFUSE_NEBULAE 1000
#define MAX_DARK_NEBULAE 1000
#define MAX_PLANETARY_NEBULAE 1000
#define MAX_SNR 1000
#define MAX_GMC 1000
#define MAX_INTERSTELLAR_FILAMENTS 1000
#define MAX_INTERSTELLAR_BUBBLES 1000
#define MAX_BOK_GLOBULES 1000
#define MAX_CLUMP_CORES 1000
#define MAX_ACCRETION_DISKS 1000
#define MAX_RELATIVISTIC_JETS 1000
#define MAX_SHOCK_WAVES 1000
#define MAX_STELLAR_BOW_SHOCKS 1000
#define MAX_COSMIC_VOIDS 1000    /* was 200: unified with the full 1000-wide ID range */
#define MAX_COSMIC_FILAMENTS 1000 /* was 400: unified with the full 1000-wide ID range */
#define MAX_EVENT_HORIZONS 1000
#define MAX_KILONOVAE 1000
#define MAX_GRAV_LENSES 1000
#define MAX_GRB 1000
#define MAX_GRAV_WAVES 1000
#define MAX_PROTOPLANETARY_DISKS 1000
#define MAX_DEBRIS_DISKS 1000
#define MAX_PLANETESIMALS 1000
#define MAX_ROGUE_PLANETS 1000
#define MAX_BROWN_DWARFS 1000
#define MAX_ISO 1000
#define MAX_MAG_RECONN 1000
#define MAX_CURRENT_SHEETS 1000
#define MAX_HELIOSPHERES 1000
#define MAX_TERM_SHOCKS 1000
#define MAX_MAGNETOSPHERES 1000
#define MAX_COSMIC_STRINGS 1000
#define MAX_DOMAIN_WALLS 1000 /* was 200: unified with the full 1000-wide ID range */
#define MAX_DM_HALO 1000
#define MAX_IGM 1000
#define MAX_CGM 1000
#define MAX_LYMAN_ALPHA 1000
#define MAX_CMB 1000          /* was 200: unified with the full 1000-wide ID range */

/* --- Compile-time consistency: pool limits vs universal ID ranges ---
 * Every static object type has a pool (above) and a universal ID range
 * (GALAXY_OBJECT_MIN_<TYPE> / GALAXY_OBJECT_MAX_<TYPE> in game_config.h).
 * Target resolution converts a universal ID to a pool slot as
 * slot = id - GALAXY_OBJECT_MIN_<TYPE> (commands.c, logic.c, targets.c),
 * guarded only by the ID range, so each pool must span its entire range:
 * every ID in the range must map to a valid slot. These asserts make any
 * drift between a limit and its range a build error.
 * A pool may legitimately exceed its range (headroom: NPC ships,
 * asteroids); it must never fall short of it. */
static_assert(MAX_NPC >= GALAXY_OBJECT_MAX_NPC - GALAXY_OBJECT_MIN_NPC + 1,
              "npcs[] must span the whole NPC ID range (slot = id - GALAXY_OBJECT_MIN_NPC)");
static_assert(MAX_PLANETS >= GALAXY_OBJECT_MAX_PLANET - GALAXY_OBJECT_MIN_PLANET + 1,
              "planets[] must span the whole PLANET ID range (slot = id - GALAXY_OBJECT_MIN_PLANET)");
static_assert(MAX_BASES >= GALAXY_OBJECT_MAX_STARBASE - GALAXY_OBJECT_MIN_STARBASE + 1,
              "bases[] must span the whole STARBASE ID range (slot = id - GALAXY_OBJECT_MIN_STARBASE)");
static_assert(MAX_STARS >= GALAXY_OBJECT_MAX_STAR - GALAXY_OBJECT_MIN_STAR + 1,
              "stars_data[] must span the whole STAR ID range (slot = id - GALAXY_OBJECT_MIN_STAR)");
static_assert(MAX_BH >= GALAXY_OBJECT_MAX_BLACKHOLE - GALAXY_OBJECT_MIN_BLACKHOLE + 1,
              "black_holes[] must span the whole BLACKHOLE ID range (slot = id - GALAXY_OBJECT_MIN_BLACKHOLE)");
static_assert(MAX_NEBULAS >= GALAXY_OBJECT_MAX_NEBULA - GALAXY_OBJECT_MIN_NEBULA + 1,
              "nebulas[] must span the whole NEBULA ID range (slot = id - GALAXY_OBJECT_MIN_NEBULA)");
static_assert(MAX_PULSARS >= GALAXY_OBJECT_MAX_PULSAR - GALAXY_OBJECT_MIN_PULSAR + 1,
              "pulsars[] must span the whole PULSAR ID range (slot = id - GALAXY_OBJECT_MIN_PULSAR)");
static_assert(MAX_QUASARS >= GALAXY_OBJECT_MAX_QUASAR - GALAXY_OBJECT_MIN_QUASAR + 1,
              "quasars[] must span the whole QUASAR ID range (slot = id - GALAXY_OBJECT_MIN_QUASAR)");
static_assert(MAX_COMETS >= GALAXY_OBJECT_MAX_COMET - GALAXY_OBJECT_MIN_COMET + 1,
              "comets[] must span the whole COMET ID range (slot = id - GALAXY_OBJECT_MIN_COMET)");
static_assert(MAX_ASTEROIDS >= GALAXY_OBJECT_MAX_ASTEROID - GALAXY_OBJECT_MIN_ASTEROID + 1,
              "asteroids[] must span the whole ASTEROID ID range (slot = id - GALAXY_OBJECT_MIN_ASTEROID)");
static_assert(MAX_DERELICTS >= GALAXY_OBJECT_MAX_DERELICT - GALAXY_OBJECT_MIN_DERELICT + 1,
              "derelicts[] must span the whole DERELICT ID range (slot = id - GALAXY_OBJECT_MIN_DERELICT)");
static_assert(MAX_MINES >= GALAXY_OBJECT_MAX_MINE - GALAXY_OBJECT_MIN_MINE + 1,
              "mines[] must span the whole MINE ID range (slot = id - GALAXY_OBJECT_MIN_MINE)");
static_assert(MAX_BUOYS >= GALAXY_OBJECT_MAX_BUOY - GALAXY_OBJECT_MIN_BUOY + 1,
              "buoys[] must span the whole BUOY ID range (slot = id - GALAXY_OBJECT_MIN_BUOY)");
static_assert(MAX_PLATFORMS >= GALAXY_OBJECT_MAX_PLATFORM - GALAXY_OBJECT_MIN_PLATFORM + 1,
              "platforms[] must span the whole PLATFORM ID range (slot = id - GALAXY_OBJECT_MIN_PLATFORM)");
static_assert(MAX_RIFTS >= GALAXY_OBJECT_MAX_RIFT - GALAXY_OBJECT_MIN_RIFT + 1,
              "rifts[] must span the whole RIFT ID range (slot = id - GALAXY_OBJECT_MIN_RIFT)");
static_assert(MAX_MONSTERS >= GALAXY_OBJECT_MAX_MONSTER - GALAXY_OBJECT_MIN_MONSTER + 1,
              "monsters[] must span the whole MONSTER ID range (slot = id - GALAXY_OBJECT_MIN_MONSTER)");
static_assert(MAX_DYSON >= GALAXY_OBJECT_MAX_DYSON - GALAXY_OBJECT_MIN_DYSON + 1,
              "dysons[] must span the whole DYSON ID range (slot = id - GALAXY_OBJECT_MIN_DYSON)");
static_assert(MAX_HUBS >= GALAXY_OBJECT_MAX_HUB - GALAXY_OBJECT_MIN_HUB + 1,
              "hubs[] must span the whole HUB ID range (slot = id - GALAXY_OBJECT_MIN_HUB)");
static_assert(MAX_RELICS >= GALAXY_OBJECT_MAX_RELIC - GALAXY_OBJECT_MIN_RELIC + 1,
              "relics[] must span the whole RELIC ID range (slot = id - GALAXY_OBJECT_MIN_RELIC)");
static_assert(MAX_RUPTURES >= GALAXY_OBJECT_MAX_RUPTURE - GALAXY_OBJECT_MIN_RUPTURE + 1,
              "ruptures[] must span the whole RUPTURE ID range (slot = id - GALAXY_OBJECT_MIN_RUPTURE)");
static_assert(MAX_SATELLITES >= GALAXY_OBJECT_MAX_SATELLITE - GALAXY_OBJECT_MIN_SATELLITE + 1,
              "satellites[] must span the whole SATELLITE ID range (slot = id - GALAXY_OBJECT_MIN_SATELLITE)");
static_assert(MAX_STORMS >= GALAXY_OBJECT_MAX_STORM - GALAXY_OBJECT_MIN_STORM + 1,
              "storms[] must span the whole STORM ID range (slot = id - GALAXY_OBJECT_MIN_STORM)");
static_assert(MAX_ARTIFACTS >= GALAXY_OBJECT_MAX_ARTIFACT - GALAXY_OBJECT_MIN_ARTIFACT + 1,
              "artifacts[] must span the whole ARTIFACT ID range (slot = id - GALAXY_OBJECT_MIN_ARTIFACT)");
static_assert(MAX_WARP_GATES >= GALAXY_OBJECT_MAX_WARP_GATE - GALAXY_OBJECT_MIN_WARP_GATE + 1,
              "warp_gates[] must span the whole WARP_GATE ID range (slot = id - GALAXY_OBJECT_MIN_WARP_GATE)");
static_assert(MAX_NEUTRON_STARS >= GALAXY_OBJECT_MAX_NEUTRON_STAR - GALAXY_OBJECT_MIN_NEUTRON_STAR + 1,
              "neutron_stars[] must span the whole NEUTRON_STAR ID range (slot = id - GALAXY_OBJECT_MIN_NEUTRON_STAR)");
static_assert(MAX_MEGA_STRUCTS >= GALAXY_OBJECT_MAX_MEGA_STRUCT - GALAXY_OBJECT_MIN_MEGA_STRUCT + 1,
              "mega_structs[] must span the whole MEGA_STRUCT ID range (slot = id - GALAXY_OBJECT_MIN_MEGA_STRUCT)");
static_assert(MAX_DARK_CLOUDS >= GALAXY_OBJECT_MAX_DARK_CLOUD - GALAXY_OBJECT_MIN_DARK_CLOUD + 1,
              "dark_clouds[] must span the whole DARK_CLOUD ID range (slot = id - GALAXY_OBJECT_MIN_DARK_CLOUD)");
static_assert(MAX_SINGULARITIES >= GALAXY_OBJECT_MAX_SINGULARITY - GALAXY_OBJECT_MIN_SINGULARITY + 1,
              "singularities[] must span the whole SINGULARITY ID range (slot = id - GALAXY_OBJECT_MIN_SINGULARITY)");
static_assert(MAX_PLASMA_STORMS >= GALAXY_OBJECT_MAX_PLASMA_STORM - GALAXY_OBJECT_MIN_PLASMA_STORM + 1,
              "plasma_storms[] must span the whole PLASMA_STORM ID range (slot = id - GALAXY_OBJECT_MIN_PLASMA_STORM)");
static_assert(MAX_ORBITAL_RINGS >= GALAXY_OBJECT_MAX_ORBITAL_RING - GALAXY_OBJECT_MIN_ORBITAL_RING + 1,
              "orbital_rings[] must span the whole ORBITAL_RING ID range (slot = id - GALAXY_OBJECT_MIN_ORBITAL_RING)");
static_assert(MAX_TIME_ANOMALIES >= GALAXY_OBJECT_MAX_TIME_ANOMALY - GALAXY_OBJECT_MIN_TIME_ANOMALY + 1,
              "time_anomalies[] must span the whole TIME_ANOMALY ID range (slot = id - GALAXY_OBJECT_MIN_TIME_ANOMALY)");
static_assert(MAX_VOID_CRYSTALS >= GALAXY_OBJECT_MAX_VOID_CRYSTAL - GALAXY_OBJECT_MIN_VOID_CRYSTAL + 1,
              "void_crystals[] must span the whole VOID_CRYSTAL ID range (slot = id - GALAXY_OBJECT_MIN_VOID_CRYSTAL)");
static_assert(MAX_SUBSPACE_ANOMALIES >= GALAXY_OBJECT_MAX_SUBSPACE_ANOM - GALAXY_OBJECT_MIN_SUBSPACE_ANOM + 1,
              "subspace_anomalies[] must span the whole SUBSPACE_ANOM ID range (slot = id - GALAXY_OBJECT_MIN_SUBSPACE_ANOM)");
static_assert(MAX_DIFFUSE_NEBULAE >= GALAXY_OBJECT_MAX_DIFFUSE_NEBULA - GALAXY_OBJECT_MIN_DIFFUSE_NEBULA + 1,
              "diffuse_nebulae[] must span the whole DIFFUSE_NEBULA ID range (slot = id - GALAXY_OBJECT_MIN_DIFFUSE_NEBULA)");
static_assert(MAX_DARK_NEBULAE >= GALAXY_OBJECT_MAX_DARK_NEBULA - GALAXY_OBJECT_MIN_DARK_NEBULA + 1,
              "dark_nebulae[] must span the whole DARK_NEBULA ID range (slot = id - GALAXY_OBJECT_MIN_DARK_NEBULA)");
static_assert(MAX_PLANETARY_NEBULAE >= GALAXY_OBJECT_MAX_PLANETARY_NEBULA - GALAXY_OBJECT_MIN_PLANETARY_NEBULA + 1,
              "planetary_nebulae[] must span the whole PLANETARY_NEBULA ID range (slot = id - GALAXY_OBJECT_MIN_PLANETARY_NEBULA)");
static_assert(MAX_SNR >= GALAXY_OBJECT_MAX_SNR - GALAXY_OBJECT_MIN_SNR + 1,
              "snrs[] must span the whole SNR ID range (slot = id - GALAXY_OBJECT_MIN_SNR)");
static_assert(MAX_GMC >= GALAXY_OBJECT_MAX_GMC - GALAXY_OBJECT_MIN_GMC + 1,
              "gmcs[] must span the whole GMC ID range (slot = id - GALAXY_OBJECT_MIN_GMC)");
static_assert(MAX_INTERSTELLAR_FILAMENTS >= GALAXY_OBJECT_MAX_INTERSTELLAR_FILAMENT - GALAXY_OBJECT_MIN_INTERSTELLAR_FILAMENT + 1,
              "interstellar_filaments[] must span the whole INTERSTELLAR_FILAMENT ID range (slot = id - GALAXY_OBJECT_MIN_INTERSTELLAR_FILAMENT)");
static_assert(MAX_INTERSTELLAR_BUBBLES >= GALAXY_OBJECT_MAX_INTERSTELLAR_BUBBLE - GALAXY_OBJECT_MIN_INTERSTELLAR_BUBBLE + 1,
              "interstellar_bubbles[] must span the whole INTERSTELLAR_BUBBLE ID range (slot = id - GALAXY_OBJECT_MIN_INTERSTELLAR_BUBBLE)");
static_assert(MAX_BOK_GLOBULES >= GALAXY_OBJECT_MAX_BOK_GLOBULE - GALAXY_OBJECT_MIN_BOK_GLOBULE + 1,
              "bok_globules[] must span the whole BOK_GLOBULE ID range (slot = id - GALAXY_OBJECT_MIN_BOK_GLOBULE)");
static_assert(MAX_CLUMP_CORES >= GALAXY_OBJECT_MAX_CLUMP_CORE - GALAXY_OBJECT_MIN_CLUMP_CORE + 1,
              "clump_cores[] must span the whole CLUMP_CORE ID range (slot = id - GALAXY_OBJECT_MIN_CLUMP_CORE)");
static_assert(MAX_ACCRETION_DISKS >= GALAXY_OBJECT_MAX_ACCRETION_DISK - GALAXY_OBJECT_MIN_ACCRETION_DISK + 1,
              "accretion_disks[] must span the whole ACCRETION_DISK ID range (slot = id - GALAXY_OBJECT_MIN_ACCRETION_DISK)");
static_assert(MAX_RELATIVISTIC_JETS >= GALAXY_OBJECT_MAX_RELATIVISTIC_JET - GALAXY_OBJECT_MIN_RELATIVISTIC_JET + 1,
              "relativistic_jets[] must span the whole RELATIVISTIC_JET ID range (slot = id - GALAXY_OBJECT_MIN_RELATIVISTIC_JET)");
static_assert(MAX_SHOCK_WAVES >= GALAXY_OBJECT_MAX_SHOCK_WAVE - GALAXY_OBJECT_MIN_SHOCK_WAVE + 1,
              "shock_waves[] must span the whole SHOCK_WAVE ID range (slot = id - GALAXY_OBJECT_MIN_SHOCK_WAVE)");
static_assert(MAX_STELLAR_BOW_SHOCKS >= GALAXY_OBJECT_MAX_STELLAR_BOW_SHOCK - GALAXY_OBJECT_MIN_STELLAR_BOW_SHOCK + 1,
              "stellar_bow_shocks[] must span the whole STELLAR_BOW_SHOCK ID range (slot = id - GALAXY_OBJECT_MIN_STELLAR_BOW_SHOCK)");
static_assert(MAX_COSMIC_VOIDS >= GALAXY_OBJECT_MAX_COSMIC_VOID - GALAXY_OBJECT_MIN_COSMIC_VOID + 1,
              "cosmic_voids[] must span the whole COSMIC_VOID ID range (slot = id - GALAXY_OBJECT_MIN_COSMIC_VOID)");
static_assert(MAX_COSMIC_FILAMENTS >= GALAXY_OBJECT_MAX_COSMIC_FILAMENT - GALAXY_OBJECT_MIN_COSMIC_FILAMENT + 1,
              "cosmic_filaments[] must span the whole COSMIC_FILAMENT ID range (slot = id - GALAXY_OBJECT_MIN_COSMIC_FILAMENT)");
static_assert(MAX_EVENT_HORIZONS >= GALAXY_OBJECT_MAX_EVENT_HORIZON - GALAXY_OBJECT_MIN_EVENT_HORIZON + 1,
              "event_horizons[] must span the whole EVENT_HORIZON ID range (slot = id - GALAXY_OBJECT_MIN_EVENT_HORIZON)");
static_assert(MAX_KILONOVAE >= GALAXY_OBJECT_MAX_KILONOVA - GALAXY_OBJECT_MIN_KILONOVA + 1,
              "kilonovae[] must span the whole KILONOVA ID range (slot = id - GALAXY_OBJECT_MIN_KILONOVA)");
static_assert(MAX_GRAV_LENSES >= GALAXY_OBJECT_MAX_GRAV_LENS - GALAXY_OBJECT_MIN_GRAV_LENS + 1,
              "grav_lenses[] must span the whole GRAV_LENS ID range (slot = id - GALAXY_OBJECT_MIN_GRAV_LENS)");
static_assert(MAX_GRB >= GALAXY_OBJECT_MAX_GRB - GALAXY_OBJECT_MIN_GRB + 1,
              "grbs[] must span the whole GRB ID range (slot = id - GALAXY_OBJECT_MIN_GRB)");
static_assert(MAX_GRAV_WAVES >= GALAXY_OBJECT_MAX_GRAV_WAVE - GALAXY_OBJECT_MIN_GRAV_WAVE + 1,
              "grav_waves[] must span the whole GRAV_WAVE ID range (slot = id - GALAXY_OBJECT_MIN_GRAV_WAVE)");
static_assert(MAX_PROTOPLANETARY_DISKS >= GALAXY_OBJECT_MAX_PROTOPLANETARY_DISK - GALAXY_OBJECT_MIN_PROTOPLANETARY_DISK + 1,
              "protoplanetary_disks[] must span the whole PROTOPLANETARY_DISK ID range (slot = id - GALAXY_OBJECT_MIN_PROTOPLANETARY_DISK)");
static_assert(MAX_DEBRIS_DISKS >= GALAXY_OBJECT_MAX_DEBRIS_DISK - GALAXY_OBJECT_MIN_DEBRIS_DISK + 1,
              "debris_disks[] must span the whole DEBRIS_DISK ID range (slot = id - GALAXY_OBJECT_MIN_DEBRIS_DISK)");
static_assert(MAX_PLANETESIMALS >= GALAXY_OBJECT_MAX_PLANETESIMAL - GALAXY_OBJECT_MIN_PLANETESIMAL + 1,
              "planetesimals[] must span the whole PLANETESIMAL ID range (slot = id - GALAXY_OBJECT_MIN_PLANETESIMAL)");
static_assert(MAX_ROGUE_PLANETS >= GALAXY_OBJECT_MAX_ROGUE_PLANET - GALAXY_OBJECT_MIN_ROGUE_PLANET + 1,
              "rogue_planets[] must span the whole ROGUE_PLANET ID range (slot = id - GALAXY_OBJECT_MIN_ROGUE_PLANET)");
static_assert(MAX_BROWN_DWARFS >= GALAXY_OBJECT_MAX_BROWN_DWARF - GALAXY_OBJECT_MIN_BROWN_DWARF + 1,
              "brown_dwarfs[] must span the whole BROWN_DWARF ID range (slot = id - GALAXY_OBJECT_MIN_BROWN_DWARF)");
static_assert(MAX_ISO >= GALAXY_OBJECT_MAX_ISO - GALAXY_OBJECT_MIN_ISO + 1,
              "isos[] must span the whole ISO ID range (slot = id - GALAXY_OBJECT_MIN_ISO)");
static_assert(MAX_MAG_RECONN >= GALAXY_OBJECT_MAX_MAG_RECONN - GALAXY_OBJECT_MIN_MAG_RECONN + 1,
              "mag_reconns[] must span the whole MAG_RECONN ID range (slot = id - GALAXY_OBJECT_MIN_MAG_RECONN)");
static_assert(MAX_CURRENT_SHEETS >= GALAXY_OBJECT_MAX_CURRENT_SHEET - GALAXY_OBJECT_MIN_CURRENT_SHEET + 1,
              "current_sheets[] must span the whole CURRENT_SHEET ID range (slot = id - GALAXY_OBJECT_MIN_CURRENT_SHEET)");
static_assert(MAX_HELIOSPHERES >= GALAXY_OBJECT_MAX_HELIOSPHERE - GALAXY_OBJECT_MIN_HELIOSPHERE + 1,
              "heliospheres[] must span the whole HELIOSPHERE ID range (slot = id - GALAXY_OBJECT_MIN_HELIOSPHERE)");
static_assert(MAX_TERM_SHOCKS >= GALAXY_OBJECT_MAX_TERM_SHOCK - GALAXY_OBJECT_MIN_TERM_SHOCK + 1,
              "term_shocks[] must span the whole TERM_SHOCK ID range (slot = id - GALAXY_OBJECT_MIN_TERM_SHOCK)");
static_assert(MAX_MAGNETOSPHERES >= GALAXY_OBJECT_MAX_MAGNETOSPHERE - GALAXY_OBJECT_MIN_MAGNETOSPHERE + 1,
              "magnetospheres[] must span the whole MAGNETOSPHERE ID range (slot = id - GALAXY_OBJECT_MIN_MAGNETOSPHERE)");
static_assert(MAX_COSMIC_STRINGS >= GALAXY_OBJECT_MAX_COSMIC_STRING - GALAXY_OBJECT_MIN_COSMIC_STRING + 1,
              "cosmic_strings[] must span the whole COSMIC_STRING ID range (slot = id - GALAXY_OBJECT_MIN_COSMIC_STRING)");
static_assert(MAX_DOMAIN_WALLS >= GALAXY_OBJECT_MAX_DOMAIN_WALL - GALAXY_OBJECT_MIN_DOMAIN_WALL + 1,
              "domain_walls[] must span the whole DOMAIN_WALL ID range (slot = id - GALAXY_OBJECT_MIN_DOMAIN_WALL)");
static_assert(MAX_DM_HALO >= GALAXY_OBJECT_MAX_DM_HALO - GALAXY_OBJECT_MIN_DM_HALO + 1,
              "dm_halos[] must span the whole DM_HALO ID range (slot = id - GALAXY_OBJECT_MIN_DM_HALO)");
static_assert(MAX_IGM >= GALAXY_OBJECT_MAX_IGM - GALAXY_OBJECT_MIN_IGM + 1,
              "igms[] must span the whole IGM ID range (slot = id - GALAXY_OBJECT_MIN_IGM)");
static_assert(MAX_CGM >= GALAXY_OBJECT_MAX_CGM - GALAXY_OBJECT_MIN_CGM + 1,
              "cgms[] must span the whole CGM ID range (slot = id - GALAXY_OBJECT_MIN_CGM)");
static_assert(MAX_LYMAN_ALPHA >= GALAXY_OBJECT_MAX_LYMAN_ALPHA - GALAXY_OBJECT_MIN_LYMAN_ALPHA + 1,
              "lyman_alphas[] must span the whole LYMAN_ALPHA ID range (slot = id - GALAXY_OBJECT_MIN_LYMAN_ALPHA)");
static_assert(MAX_CMB >= GALAXY_OBJECT_MAX_CMB - GALAXY_OBJECT_MIN_CMB + 1,
              "cmbs[] must span the whole CMB ID range (slot = id - GALAXY_OBJECT_MIN_CMB)");

/* Players are the inverse case: the pool is deliberately smaller than the
 * ID range (only MAX_CLIENTS slots are active, IDs 1..MAX_CLIENTS, see
 * (p - players) + GALAXY_OBJECT_MIN_PLAYER in logic.c), so the invariant
 * runs the other way: every assignable player ID must stay inside the
 * range reserved for players. */
static_assert(MAX_CLIENTS <= GALAXY_OBJECT_MAX_PLAYER - GALAXY_OBJECT_MIN_PLAYER + 1,
              "every player ID (1..MAX_CLIENTS) must stay inside the player ID range 1-999");

/* Local Quadrant Limits for Spatial Index (Optimization) */
/* Per-quadrant capacity limits — calibrated to real galaxy density:
 * 4000 NPC / 64000 quadrants ≈ 0.06 avg; caps set at safe peak multiples.
 * Reduction from original values saves ~182 MB of spatial_index RAM. */
#define MAX_Q_NPC 8       /* was 32: 4000 NPC / 64000 quads; 8 is a safe peak */
#define MAX_Q_PLANETS 8   /* was 32 */
#define MAX_Q_BASES 4     /* was 16: bases are rare, 1 per populated quad */
#define MAX_Q_STARS 16    /* was 64: 4000 stars / 64000 quads */
#define MAX_Q_BH 4        /* was 8 */
#define MAX_Q_NEBULAS 4   /* was 16 */
#define MAX_Q_PULSARS 4   /* was 8 */
#define MAX_Q_QUASARS 4   /* was 8 */
#define MAX_Q_COMETS 4    /* was 8 */
#define MAX_Q_ASTEROIDS 8 /* was 40: asteroids cluster but 8 is safe per quad */
#define MAX_Q_DERELICTS 4 /* was 8 */
#define MAX_Q_MINES 8     /* was 32 */
#define MAX_Q_BUOYS 4     /* was 8 */
#define MAX_Q_PLATFORMS 4 /* was 16 */
#define MAX_Q_RIFTS 2     /* was 4 */
#define MAX_Q_MONSTERS 2  /* was 4 */
#define MAX_Q_PLAYERS 16  /* was 32: MAX_CLIENTS=16, never more than 16 */
#define MAX_Q_TORPEDOES 8 /* was 32: MAX_GLOBAL_TORPEDOES=64, rarely in same quad */
#define MAX_Q_DYSON 2     /* was 4 */
#define MAX_Q_HUBS 2      /* was 4 */
#define MAX_Q_RELICS 2    /* was 4 */
#define MAX_Q_RUPTURES 2  /* was 4 */
#define MAX_Q_SATELLITES 4 /* was 8 */
#define MAX_Q_STORMS 2    /* was 4 */
#define MAX_Q_ARTIFACTS 2 /* was 4 */
#define MAX_Q_WARP_GATES 2 /* was 4 */
#define MAX_Q_NEUTRON_STARS 2 /* was 4 */
#define MAX_Q_MEGA_STRUCTS 2 /* was 4 */
#define MAX_Q_DARK_CLOUDS 4 /* was 8 */
#define MAX_Q_SINGULARITIES 2 /* was 4 */
#define MAX_Q_PLASMA_STORMS 2 /* was 4 */
#define MAX_Q_ORBITAL_RINGS 2 /* was 4 */
#define MAX_Q_TIME_ANOMALIES 2 /* was 4 */
#define MAX_Q_VOID_CRYSTALS 2 /* was 4 */
#define MAX_Q_SUBSPACE_ANOMALIES 2 /* was 4 */
#define MAX_Q_DIFFUSE_NEBULA 2 /* was 4 */
#define MAX_Q_DARK_NEBULA 2 /* was 4 */
#define MAX_Q_PLANETARY_NEBULA 2 /* was 4 */
#define MAX_Q_SNR 2 /* was 4 */
#define MAX_Q_GMC 2 /* was 4 */
#define MAX_Q_INTERSTELLAR_FILAMENT 2 /* was 4 */
#define MAX_Q_INTERSTELLAR_BUBBLE 2 /* was 4 */
#define MAX_Q_BOK_GLOBULE 2 /* was 4 */
#define MAX_Q_CLUMP_CORE 2 /* was 4 */
#define MAX_Q_ACCRETION_DISK 2 /* was 4 */
#define MAX_Q_RELATIVISTIC_JET 2 /* was 4 */
#define MAX_Q_SHOCK_WAVE 2 /* was 4 */
#define MAX_Q_STELLAR_BOW_SHOCK 2 /* was 4 */
#define MAX_Q_COSMIC_VOID 1 /* was 2: at most 1 cosmic void per quadrant */
#define MAX_Q_COSMIC_FILAMENT 2 /* was 4 */
#define MAX_Q_EVENT_HORIZON 2 /* was 4 */
#define MAX_Q_KILONOVA 1 /* was 2 */
#define MAX_Q_GRAV_LENS 2 /* was 4 */
#define MAX_Q_GRB 1 /* was 2 */
#define MAX_Q_GRAV_WAVE 2 /* was 4 */
#define MAX_Q_PROTOPLANETARY_DISK 2 /* was 4 */
#define MAX_Q_DEBRIS_DISK 2 /* was 4 */
#define MAX_Q_PLANETESIMAL 4 /* was 8 */
#define MAX_Q_ROGUE_PLANET 2 /* was 4 */
#define MAX_Q_BROWN_DWARF 2 /* was 4 */
#define MAX_Q_ISO 2 /* was 4 */
#define MAX_Q_MAG_RECONN 2 /* was 4 */
#define MAX_Q_CURRENT_SHEET 2 /* was 4 */
#define MAX_Q_HELIOSPHERE 2 /* was 4 */
#define MAX_Q_TERM_SHOCK 2 /* was 4 */
#define MAX_Q_MAGNETOSPHERE 2 /* was 4 */
#define MAX_Q_COSMIC_STRING 2 /* was 4 */
#define MAX_Q_DOMAIN_WALL 1 /* was 2 */
#define MAX_Q_DM_HALO 2 /* was 4 */
#define MAX_Q_IGM 2 /* was 4 */
#define MAX_Q_CGM 2 /* was 4 */
#define MAX_Q_LYMAN_ALPHA 2 /* was 4 */
#define MAX_Q_CMB 1 /* was 2 */

/* Global Data accessed by modules */
extern NPCStar stars_data[MAX_STARS];
extern NPCBlackHole black_holes[MAX_BH];
extern NPCNebula nebulas[MAX_NEBULAS];
extern NPCPulsar pulsars[MAX_PULSARS];
extern NPCQuasar quasars[MAX_QUASARS];
extern NPCComet comets[MAX_COMETS];
extern NPCAsteroid asteroids[MAX_ASTEROIDS];
extern NPCDerelict derelicts[MAX_DERELICTS];
extern NPCMine mines[MAX_MINES];
extern NPCBuoy buoys[MAX_BUOYS];
extern NPCPlatform platforms[MAX_PLATFORMS];
extern NPCRift rifts[MAX_RIFTS];
extern NPCMonster monsters[MAX_MONSTERS];
extern NPCPlanet planets[MAX_PLANETS];
extern NPCBase bases[MAX_BASES];
extern NPCShip npcs[MAX_NPC];
extern NPCDyson dysons[MAX_DYSON];
extern NPCHub hubs[MAX_HUBS];
extern NPCRelic relics[MAX_RELICS];
extern NPCRupture ruptures[MAX_RUPTURES];
extern NPCSatellite satellites[MAX_SATELLITES];
extern NPCStorm storms[MAX_STORMS];
extern NPCArtifact artifacts[MAX_ARTIFACTS];
extern NPCWarpGate warp_gates[MAX_WARP_GATES];
extern NPCNeutronStar neutron_stars[MAX_NEUTRON_STARS];
extern NPCMegaStructure mega_structs[MAX_MEGA_STRUCTS];
extern NPCDarkCloud dark_clouds[MAX_DARK_CLOUDS];
extern NPCSingularity singularities[MAX_SINGULARITIES];
extern NPCPlasmaStorm plasma_storms[MAX_PLASMA_STORMS];
extern NPCOrbitalRing orbital_rings[MAX_ORBITAL_RINGS];
extern NPCTimeAnomaly time_anomalies[MAX_TIME_ANOMALIES];
extern NPCVoidCrystal void_crystals[MAX_VOID_CRYSTALS];

extern NPCSubspaceAnomaly subspace_anomalies[MAX_SUBSPACE_ANOMALIES];

extern NPCDiffuseNebula diffuse_nebulae[MAX_DIFFUSE_NEBULAE];
extern NPCDarkNebula dark_nebulae[MAX_DARK_NEBULAE];
extern NPCPlanetaryNebula planetary_nebulae[MAX_PLANETARY_NEBULAE];
extern NPCSNR snrs[MAX_SNR];
extern NPCGMC gmcs[MAX_GMC];
extern NPCInterstellarFilament interstellar_filaments[MAX_INTERSTELLAR_FILAMENTS];
extern NPCInterstellarBubble interstellar_bubbles[MAX_INTERSTELLAR_BUBBLES];
extern NPCBokGlobule bok_globules[MAX_BOK_GLOBULES];
extern NPCClumpCore clump_cores[MAX_CLUMP_CORES];
extern NPCAccretionDisk accretion_disks[MAX_ACCRETION_DISKS];
extern NPCRelativisticJet relativistic_jets[MAX_RELATIVISTIC_JETS];
extern NPCShockWave shock_waves[MAX_SHOCK_WAVES];
extern NPCStellarBowShock stellar_bow_shocks[MAX_STELLAR_BOW_SHOCKS];
extern NPCCosmicVoid cosmic_voids[MAX_COSMIC_VOIDS];
extern NPCCosmicFilament cosmic_filaments[MAX_COSMIC_FILAMENTS];
extern NPCEventHorizon event_horizons[MAX_EVENT_HORIZONS];
extern NPCKilonova kilonovae[MAX_KILONOVAE];
extern NPCGravLens grav_lenses[MAX_GRAV_LENSES];
extern NPCGRB grbs[MAX_GRB];
extern NPCGravWave grav_waves[MAX_GRAV_WAVES];
extern NPCProtoplanetaryDisk protoplanetary_disks[MAX_PROTOPLANETARY_DISKS];
extern NPCDebrisDisk debris_disks[MAX_DEBRIS_DISKS];
extern NPCPlanetesimal planetesimals[MAX_PLANETESIMALS];
extern NPCRoguePlanet rogue_planets[MAX_ROGUE_PLANETS];
extern NPCBrownDwarf brown_dwarfs[MAX_BROWN_DWARFS];
extern NPCISO isos[MAX_ISO];
extern NPCMagReconn mag_reconns[MAX_MAG_RECONN];
extern NPCCurrentSheet current_sheets[MAX_CURRENT_SHEETS];
extern NPCHeliosphere heliospheres[MAX_HELIOSPHERES];
extern NPCTermShock term_shocks[MAX_TERM_SHOCKS];
extern NPCMagnetosphere magnetospheres[MAX_MAGNETOSPHERES];
extern NPCCosmicString cosmic_strings[MAX_COSMIC_STRINGS];
extern NPCDomainWall domain_walls[MAX_DOMAIN_WALLS];
extern NPCDMHalo dm_halos[MAX_DM_HALO];
extern NPCIGM igms[MAX_IGM];
extern NPCCGM cgms[MAX_CGM];
extern NPCLymanAlpha lyman_alphas[MAX_LYMAN_ALPHA];
extern NPCCMB cmbs[MAX_CMB];

/* Global Generic Features Array */
extern NPCCosmicFeature cosmic_features[MAX_COSMIC_FEATURES];
extern int cosmic_feature_count;

extern PlayerTorpedo players_torpedoes[MAX_GLOBAL_TORPEDOES];

extern ConnectedPlayer players[MAX_CLIENTS];
extern SpaceGLGame spacegl_master;
extern pthread_mutex_t game_mutex;
extern int g_debug;
extern int global_tick;
extern uint8_t MASTER_SESSION_KEY[32];
extern uint8_t GALAXY_VERIFY_KEY[32]; /* Stable galaxy-signature key (derived from the master key) */
extern uint8_t ALGO_KEYS[MAX_CRYPTO_ALGOS + 1][32]; /* Keys for algorithms 1-MAX */
extern uint8_t SERVER_PUBKEY[32];
extern uint8_t SERVER_PRIVKEY[64];

typedef struct {
    int supernova_q1, supernova_q2, supernova_q3;
    double x, y, z; /* Epicenter of the explosion (The star) */
    int supernova_timer; /* Ticks remaining, 0 = inactive */
    int star_id; /* ID of the star exploding */
} SupernovaState;
extern SupernovaState supernova_event;

/* Thread-safe console logging: every event is formatted once and emitted
 * with a single write(2) under a dedicated mutex, so log lines from the
 * game thread, the main epoll loop, the thread pool and the telemetry
 * thread can never interleave (see src/server/log.c). */
void slog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_init(void);

#define LOG_DEBUG(...) do { if (g_debug) { slog("DEBUG: " __VA_ARGS__); } } while (0)

#define GALAXY_VERSION 20261006  /* 2026-10-06: four exotic pools unified to 1000 -> galaxy.dat layout change */

/* Spatial Partitioning Index */
typedef struct {
    NPCShip *npcs[MAX_Q_NPC];
    int npc_count;
    NPCPlanet *planets[MAX_Q_PLANETS];
    int planet_count;
    int static_planet_count; 
    NPCBase *bases[MAX_Q_BASES];
    int base_count;
    int static_base_count;   
    NPCStar *stars[MAX_Q_STARS];
    int star_count;
    int static_star_count;   
    NPCBlackHole *black_holes[MAX_Q_BH];
    int bh_count;
    int static_bh_count;     
    NPCNebula *nebulas[MAX_Q_NEBULAS];
    int nebula_count;
    int static_nebula_count; 
    NPCPulsar *pulsars[MAX_Q_PULSARS];
    int pulsar_count;
    int static_pulsar_count; 
    NPCQuasar *quasars[MAX_Q_QUASARS];
    int quasar_count;
    int static_quasar_count; 
    NPCComet *comets[MAX_Q_COMETS];
    int comet_count;
    NPCAsteroid *asteroids[MAX_Q_ASTEROIDS];
    int asteroid_count;
    NPCDerelict *derelicts[MAX_Q_DERELICTS];
    int derelict_count;
    NPCMine *mines[MAX_Q_MINES];
    int mine_count;
    NPCBuoy *buoys[MAX_Q_BUOYS];
    int buoy_count;
    NPCPlatform *platforms[MAX_Q_PLATFORMS];
    int platform_count;
    NPCRift *rifts[MAX_Q_RIFTS];
    int rift_count;
    NPCMonster *monsters[MAX_Q_MONSTERS];
    int monster_count;
    NPCDyson *dysons[MAX_Q_DYSON];
    int dyson_count;
    NPCHub *hubs[MAX_Q_HUBS];
    int hub_count;
    NPCRelic *relics[MAX_Q_RELICS];
    int relic_count;
    NPCRupture *ruptures[MAX_Q_RUPTURES];
    int rupture_count;
    NPCSatellite *satellites[MAX_Q_SATELLITES];
    int satellite_count;
    NPCStorm *storms[MAX_Q_STORMS];
    int storm_count;
    NPCArtifact *artifacts[MAX_Q_ARTIFACTS]; int artifact_count;
    NPCWarpGate *warp_gates[MAX_Q_WARP_GATES]; int warp_gate_count;
    NPCNeutronStar *neutron_stars[MAX_Q_NEUTRON_STARS]; int neutron_star_count;
    NPCMegaStructure *mega_structs[MAX_Q_MEGA_STRUCTS]; int mega_struct_count;
    NPCDarkCloud *dark_clouds[MAX_Q_DARK_CLOUDS]; int dark_cloud_count;
    NPCSingularity *singularities[MAX_Q_SINGULARITIES]; int singularity_count;
    NPCPlasmaStorm *plasma_storms[MAX_Q_PLASMA_STORMS]; int plasma_storm_count;
    NPCOrbitalRing *orbital_rings[MAX_Q_ORBITAL_RINGS]; int orbital_ring_count;
    NPCTimeAnomaly *time_anomalies[MAX_Q_TIME_ANOMALIES]; int time_anomaly_count;
    NPCVoidCrystal *void_crystals[MAX_Q_VOID_CRYSTALS]; int void_crystal_count;
    NPCSubspaceAnomaly *subspace_anomalies[MAX_Q_SUBSPACE_ANOMALIES]; int subspace_anomaly_count;

    NPCDiffuseNebula *diffuse_nebulae[MAX_Q_DIFFUSE_NEBULA]; int diffuse_nebula_count;
    NPCDarkNebula *dark_nebulae[MAX_Q_DARK_NEBULA]; int dark_nebula_count;
    NPCPlanetaryNebula *planetary_nebulae[MAX_Q_PLANETARY_NEBULA]; int planetary_nebula_count;
    NPCSNR *snrs[MAX_Q_SNR]; int snr_count;
    NPCGMC *gmcs[MAX_Q_GMC]; int gmc_count;
    NPCInterstellarFilament *interstellar_filaments[MAX_Q_INTERSTELLAR_FILAMENT]; int interstellar_filament_count;
    NPCInterstellarBubble *interstellar_bubbles[MAX_Q_INTERSTELLAR_BUBBLE]; int interstellar_bubble_count;
    NPCBokGlobule *bok_globules[MAX_Q_BOK_GLOBULE]; int bok_globule_count;
    NPCClumpCore *clump_cores[MAX_Q_CLUMP_CORE]; int clump_core_count;
    NPCAccretionDisk *accretion_disks[MAX_Q_ACCRETION_DISK]; int accretion_disk_count;
    NPCRelativisticJet *relativistic_jets[MAX_Q_RELATIVISTIC_JET]; int relativistic_jet_count;
    NPCShockWave *shock_waves[MAX_Q_SHOCK_WAVE]; int shock_wave_count;
    NPCStellarBowShock *stellar_bow_shocks[MAX_Q_STELLAR_BOW_SHOCK]; int stellar_bow_shock_count;
    NPCCosmicVoid *cosmic_voids[MAX_Q_COSMIC_VOID]; int cosmic_void_count;
    NPCCosmicFilament *cosmic_filaments[MAX_Q_COSMIC_FILAMENT]; int cosmic_filament_count;
    NPCEventHorizon *event_horizons[MAX_Q_EVENT_HORIZON]; int event_horizon_count;
    NPCKilonova *kilonovae[MAX_Q_KILONOVA]; int kilonova_count;
    NPCGravLens *grav_lenses[MAX_Q_GRAV_LENS]; int grav_lens_count;
    NPCGRB *grbs[MAX_Q_GRB]; int grb_count;
    NPCGravWave *grav_waves[MAX_Q_GRAV_WAVE]; int grav_wave_count;
    NPCProtoplanetaryDisk *protoplanetary_disks[MAX_Q_PROTOPLANETARY_DISK]; int protoplanetary_disk_count;
    NPCDebrisDisk *debris_disks[MAX_Q_DEBRIS_DISK]; int debris_disk_count;
    NPCPlanetesimal *planetesimals[MAX_Q_PLANETESIMAL]; int planetesimal_count;
    NPCRoguePlanet *rogue_planets[MAX_Q_ROGUE_PLANET]; int rogue_planet_count;
    NPCBrownDwarf *brown_dwarfs[MAX_Q_BROWN_DWARF]; int brown_dwarf_count;
    NPCISO *isos[MAX_Q_ISO]; int iso_count;
    NPCMagReconn *mag_reconns[MAX_Q_MAG_RECONN]; int mag_reconn_count;
    NPCCurrentSheet *current_sheets[MAX_Q_CURRENT_SHEET]; int current_sheet_count;
    NPCHeliosphere *heliospheres[MAX_Q_HELIOSPHERE]; int heliosphere_count;
    NPCTermShock *term_shocks[MAX_Q_TERM_SHOCK]; int term_shock_count;
    NPCMagnetosphere *magnetospheres[MAX_Q_MAGNETOSPHERE]; int magnetosphere_count;
    NPCCosmicString *cosmic_strings[MAX_Q_COSMIC_STRING]; int cosmic_string_count;
    NPCDomainWall *domain_walls[MAX_Q_DOMAIN_WALL]; int domain_wall_count;
    NPCDMHalo *dm_halos[MAX_Q_DM_HALO]; int dm_halo_count;
    NPCIGM *igms[MAX_Q_IGM]; int igm_count;
    NPCCGM *cgms[MAX_Q_CGM]; int cgm_count;
    NPCLymanAlpha *lyman_alphas[MAX_Q_LYMAN_ALPHA]; int lyman_alpha_count;
    NPCCMB *cmbs[MAX_Q_CMB]; int cmb_count;

    /* Generic Cosmic Feature System (Data-Oriented approach for new types) */
    NPCCosmicFeature *features[MAX_Q_FEATURES];
    int feature_count;

    ConnectedPlayer *players[MAX_Q_PLAYERS];
    int player_count;
    PlayerTorpedo *torpedoes[MAX_Q_TORPEDOES];
    int torpedo_count;
} __attribute__((aligned(64))) QuadrantIndex;

extern QuadrantIndex (*spatial_index)[41][41];
void rebuild_spatial_index();
void init_static_spatial_index();

#define IS_Q_VALID(q1,q2,q3) ((q1)>=1 && (q1)<=GALAXY_SIZE && (q2)>=1 && (q2)<=GALAXY_SIZE && (q3)>=1 && (q3)<=GALAXY_SIZE)

/* Helper to safely calculate quadrant from absolute coordinate (0-1600) */
static inline int get_q_from_g(double g) {
    /* Use a small epsilon to avoid jitter jumping exactly on the boundary */
    int q = (int)((g + 1e-6) / QUADRANT_SIZE) + 1;
    if (q < 1) q = 1;
    if (q > GALAXY_SIZE) q = GALAXY_SIZE;
    return q;
}

/* Function Prototypes */
void normalize_upright(double *h, double *m);
void generate_galaxy();
int load_galaxy();
void save_galaxy();
void save_galaxy_async();
void refresh_lrs_grid();
void spawn_derelict(int q1, int q2, int q3, double x, double y, double z, int faction, int ship_class, const char* name);
const char* get_species_name(int s);

void broadcast_message(PacketMessage *msg);
void broadcast_task(void *arg);
void send_server_msg(int p_idx, const char *from, const char *text);
void derive_algo_keys(uint8_t *master_key, const char *name, uint8_t target_keys[MAX_CRYPTO_ALGOS + 1][32]);
void broadcast_server_event(int q1, int q2, int q3, int type, double x1, double y1, double z1, double x2, double y2, double z2, int extra);
void telemetry_init(void);
void telemetry_shutdown(void);

bool process_command(int p_idx, const char *cmd);
void update_game_logic();

/* Shared "EMERGENCY REENTRY" protocol: relocate the player to a random safe
 * quadrant (never the supernova epicenter) and restore escape-vessel state.
 * The four historical call sites (death countdown, crew depletion, the "xxx"
 * command and the login sync rescue) share this single implementation; the
 * small behavioral differences are captured in RescueParams. */
typedef struct {
    double sector_offset;       /* Sector coordinate inside the safe quadrant */
    bool full_torpedo_reload;   /* true: MAX_TORPEDO_CAPACITY, false: MAX/10 */
    bool always_reset_crew;     /* true: crew always MAX_CREW_EXPLORER/10,
                                   false: only when crew_count <= 0 */
    bool force_shutdown;        /* set state.force_shutdown = 1 ("xxx" cmd) */
    bool reset_motion;          /* reset hyper_speed/velocity/dock/torpedoes */
    bool reactivate;            /* set players[i].active = 1 (login sync) */
} RescueParams;

extern const RescueParams RESCUE_PARAMS_STANDARD;  /* death countdown */
extern const RescueParams RESCUE_PARAMS_CREW;      /* crew depletion */
extern const RescueParams RESCUE_PARAMS_TACTICAL;  /* "xxx" command */
extern const RescueParams RESCUE_PARAMS_LOGIN;     /* login sync rescue */
void rescue_player(int i, const RescueParams *p, unsigned int *seed);

/* Universal target resolution (single source of truth for the
 * GALAXY_OBJECT_MIN_* / GALAXY_OBJECT_MAX_* ID ranges;
 * see src/server/targets.c). */
#define TGT_F_LOCK_VALID  (1u << 0) /* per-tick lock validity chain */
#define TGT_F_APR_LOCAL   (1u << 1) /* NAV_STATE_APPROACH: spatial-index lookup */
#define TGT_F_APR_GLOBAL  (1u << 2) /* NAV_STATE_APPROACH: global-array fallback */
#define TGT_F_CMD_APR     (1u << 3) /* handle_apr: spatial-index lookup */
#define TGT_F_CMD_LOCK    (1u << 4) /* handle_lock: lookup */
#define TGT_F_LOCK_GLOBAL (1u << 5) /* handle_lock: resolve via global array */
#define TGT_F_CHASE       (1u << 6) /* NAV_STATE_CHASE: global-array lookup */

typedef struct TargetRangeDef {
    const char *name;  /* Default display name (SRS/APR readout) */
    int min_id;
    int max_id;
    const void *base;  /* Global object array */
    size_t stride;     /* sizeof(element) */
    size_t capacity;   /* MAX_* of the array */
    unsigned flags;    /* TGT_F_* capability flags */
    bool (*active_in_quadrant)(const void *e, int q1, int q2, int q3);
    bool (*is_active)(const void *e);
    void (*abs_pos)(const void *e, double *ax, double *ay, double *az);
    void *(*find_in_quadrant)(const QuadrantIndex *q, int tid);
    void (*make_name)(const void *e, char *buf, size_t len); /* NULL: use name */
    bool (*visible)(const void *e, int my_faction);          /* NULL: visible */
} TargetRangeDef;

const TargetRangeDef *target_range_for(int tid);
bool target_is_active_in_quadrant(int tid, int q1, int q2, int q3);
void *target_find_local(const TargetRangeDef *r, const QuadrantIndex *q, int tid);
const void *target_find_global_active(int tid);
void target_abs_pos(const TargetRangeDef *r, const void *e, double *ax, double *ay, double *az);
void target_make_name(const TargetRangeDef *r, const void *e, char *buf, size_t len);
bool target_visible(const TargetRangeDef *r, const void *e, int my_faction);
void push_server_event(int p_idx, int type, double x1, double y1, double z1, double x2, double y2, double z2, int extra);
bool is_player_in_nebula(int p_idx);
void apply_hull_damage(int p_idx, double amount);
void send_optimized_update(int p_idx, PacketUpdate *upd);
void send_pending_updates(void);

int calculate_shield_index(double shooter_x, double shooter_y, double shooter_z, 
                           double target_x, double target_y, double target_z,
                           double target_h, double target_m, double target_r);

int read_all(int fd, void *buf, size_t len);
int write_all(int fd, const void *buf, size_t len);

/* --- Configurable data directory (captains/ tree + galaxy.dat) ---
 * The server used to read and write its persistent state relative to the
 * current working directory. The --data-dir option (parsed in main)
 * relocates it: g_data_dir is the configured root (default "." = legacy
 * CWD behavior) and server_data_path() resolves a relative resource
 * path against it. */
#define SERVER_DATA_DIR_MAX 512
extern char g_data_dir[SERVER_DATA_DIR_MAX];
void server_set_data_dir(const char *dir);
void server_data_path(char *out, size_t out_len, const char *rel);

static inline ConnectedPlayer *player_by_id(int id) {
    if (id < 1 || id > MAX_CLIENTS) return NULL;
    return &players[id - 1];
}
static inline NPCShip *npc_by_id(int id) {
    if (id < GALAXY_OBJECT_MIN_NPC || id > GALAXY_OBJECT_MAX_NPC) return NULL;
    return &npcs[id - GALAXY_OBJECT_MIN_NPC];
}
static inline NPCPlanet *planet_by_id(int id) {
    if (id < GALAXY_OBJECT_MIN_PLANET || id > GALAXY_OBJECT_MAX_PLANET) return NULL;
    return &planets[id - GALAXY_OBJECT_MIN_PLANET];
}
static inline NPCBase *base_by_id(int id) {
    if (id < GALAXY_OBJECT_MIN_STARBASE || id > GALAXY_OBJECT_MAX_STARBASE) return NULL;
    return &bases[id - GALAXY_OBJECT_MIN_STARBASE];
}
static inline NPCStar *star_by_id(int id) {
    if (id < GALAXY_OBJECT_MIN_STAR || id > GALAXY_OBJECT_MAX_STAR) return NULL;
    return &stars_data[id - GALAXY_OBJECT_MIN_STAR];
}

#endif
