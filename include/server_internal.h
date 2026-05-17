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
    int pending_bor_type;   /* 1: Ally, 2: Enemy */

    int radio_lock_target;  /* ID of locked captain (1-based), 0 if none */

    int death_timer;        /* Ticks until final destruction explosion */
    
    bool fire_requested_this_tick;

    /* Optimization State */
    PacketUpdate last_sent_state;
    int last_q1, last_q2, last_q3;
    uint64_t full_update_timer;

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
#define MAX_COSMIC_VOIDS 200
#define MAX_COSMIC_FILAMENTS 400
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
#define MAX_DOMAIN_WALLS 200
#define MAX_DM_HALO 1000
#define MAX_IGM 1000
#define MAX_CGM 1000
#define MAX_LYMAN_ALPHA 1000
#define MAX_CMB 200

/* Local Quadrant Limits for Spatial Index (Optimization) */
#define MAX_Q_NPC 32
#define MAX_Q_PLANETS 32
#define MAX_Q_BASES 16
#define MAX_Q_STARS 64
#define MAX_Q_BH 8
#define MAX_Q_NEBULAS 16
#define MAX_Q_PULSARS 8
#define MAX_Q_QUASARS 8
#define MAX_Q_COMETS 8
#define MAX_Q_ASTEROIDS 40
#define MAX_Q_DERELICTS 8
#define MAX_Q_MINES 32
#define MAX_Q_BUOYS 8
#define MAX_Q_PLATFORMS 16
#define MAX_Q_RIFTS 4
#define MAX_Q_MONSTERS 4
#define MAX_Q_PLAYERS 32
#define MAX_Q_TORPEDOES 32
#define MAX_Q_DYSON 4
#define MAX_Q_HUBS 4
#define MAX_Q_RELICS 4
#define MAX_Q_RUPTURES 4
#define MAX_Q_SATELLITES 8
#define MAX_Q_STORMS 4
#define MAX_Q_ARTIFACTS 4
#define MAX_Q_WARP_GATES 4
#define MAX_Q_NEUTRON_STARS 4
#define MAX_Q_MEGA_STRUCTS 4
#define MAX_Q_DARK_CLOUDS 8
#define MAX_Q_SINGULARITIES 4
#define MAX_Q_PLASMA_STORMS 4
#define MAX_Q_ORBITAL_RINGS 4
#define MAX_Q_TIME_ANOMALIES 4
#define MAX_Q_VOID_CRYSTALS 4
#define MAX_Q_SUBSPACE_ANOMALIES 4
#define MAX_Q_DIFFUSE_NEBULA 4
#define MAX_Q_DARK_NEBULA 4
#define MAX_Q_PLANETARY_NEBULA 4
#define MAX_Q_SNR 4
#define MAX_Q_GMC 4
#define MAX_Q_INTERSTELLAR_FILAMENT 4
#define MAX_Q_INTERSTELLAR_BUBBLE 4
#define MAX_Q_BOK_GLOBULE 4
#define MAX_Q_CLUMP_CORE 4
#define MAX_Q_ACCRETION_DISK 4
#define MAX_Q_RELATIVISTIC_JET 4
#define MAX_Q_SHOCK_WAVE 4
#define MAX_Q_STELLAR_BOW_SHOCK 4
#define MAX_Q_COSMIC_VOID 2
#define MAX_Q_COSMIC_FILAMENT 4
#define MAX_Q_EVENT_HORIZON 4
#define MAX_Q_KILONOVA 2
#define MAX_Q_GRAV_LENS 4
#define MAX_Q_GRB 2
#define MAX_Q_GRAV_WAVE 4
#define MAX_Q_PROTOPLANETARY_DISK 4
#define MAX_Q_DEBRIS_DISK 4
#define MAX_Q_PLANETESIMAL 8
#define MAX_Q_ROGUE_PLANET 4
#define MAX_Q_BROWN_DWARF 4
#define MAX_Q_ISO 4
#define MAX_Q_MAG_RECONN 4
#define MAX_Q_CURRENT_SHEET 4
#define MAX_Q_HELIOSPHERE 4
#define MAX_Q_TERM_SHOCK 4
#define MAX_Q_MAGNETOSPHERE 4
#define MAX_Q_COSMIC_STRING 4
#define MAX_Q_DOMAIN_WALL 2
#define MAX_Q_DM_HALO 4
#define MAX_Q_IGM 4
#define MAX_Q_CGM 4
#define MAX_Q_LYMAN_ALPHA 4
#define MAX_Q_CMB 2

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

extern PlayerTorpedo players_torpedoes[MAX_GLOBAL_TORPEDOES];
extern ConnectedPlayer players[MAX_CLIENTS];
extern SpaceGLGame spacegl_master;
extern pthread_mutex_t game_mutex;
extern int g_debug;
extern int global_tick;
extern uint8_t MASTER_SESSION_KEY[32];
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

#define LOG_DEBUG(...) do { if (g_debug) { printf("DEBUG: " __VA_ARGS__); fflush(stdout); } } while (0)

#define GALAXY_VERSION 20260422

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
void send_server_msg(int p_idx, const char *from, const char *text);
void broadcast_server_event(int q1, int q2, int q3, int type, double x1, double y1, double z1, double x2, double y2, double z2, int extra);

bool process_command(int p_idx, const char *cmd);
void update_game_logic();
void push_server_event(int p_idx, int type, double x1, double y1, double z1, double x2, double y2, double z2, int extra);
bool is_player_in_nebula(int p_idx);
void apply_hull_damage(int p_idx, double amount);
void send_optimized_update(int p_idx, PacketUpdate *upd);

int calculate_shield_index(double shooter_x, double shooter_y, double shooter_z, 
                           double target_x, double target_y, double target_z,
                           double target_h, double target_m, double target_r);

int read_all(int fd, void *buf, size_t len);
int write_all(int fd, const void *buf, size_t len);

#endif
