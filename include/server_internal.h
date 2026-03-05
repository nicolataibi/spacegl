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
    int crypto_algo; /* 0:None, 1-11:Legacy, 12:PQC (Quantum Secure) */
    uint8_t session_key[32]; /* Derived via ECDH/ML-KEM */
    uint8_t algo_keys[13][32]; /* Personal Frequency Set */
    
    /* Navigation & Physics State */
    double gx, gy, gz;      /* Absolute Galactic Coordinates */
    double target_gx, target_gy, target_gz;
    double dx, dy, dz;      /* Movement Vector */
    double target_h, target_m;
    double start_h, start_m;
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
    int current_tube;
    
    /* Global/Legacy compatibility (optional, but keep structure clean) */
    int torp_load_timer; 
    bool torp_active; /* Still used to signal "any torpedo active" for HUD simplify */
    
    /* Jump Visuals */
    double wx, wy, wz;      /* Wormhole entrance coords */
    int is_docked;          /* Persistent docking state */
    int shield_regen_delay;
    int renegade_timer;     /* Ticks until faction forgives friendly fire */
    
    /* Boarding Interaction State */
    int pending_bor_target; /* ID of target player */
    int pending_bor_type;   /* 1: Ally, 2: Enemy */

    int death_timer;        /* Ticks until final destruction explosion */
    
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
    int faction;
    int q1;
    int q2;
    int q3; 
    double x;
    double y;
    double z; 
    int health;
    int active; 
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
    bool active;
} __attribute__((aligned(64))) PlayerTorpedo;

#define MAX_GLOBAL_TORPEDOES (MAX_CLIENTS * 4)

#pragma pack(pop)

/* --- Limits --- */

#define MAX_NPC 2500
#define MAX_PLANETS 1000
#define MAX_BASES 200
#define MAX_STARS 3000
#define MAX_BH 200
#define MAX_NEBULAS 500
#define MAX_PULSARS 200
#define MAX_QUASARS 200
#define MAX_COMETS 300
#define MAX_ASTEROIDS 2000
#define MAX_DERELICTS 2500
#define MAX_MINES 1000
#define MAX_BUOYS 100
#define MAX_PLATFORMS 200
#define MAX_RIFTS 50
#define MAX_MONSTERS 30

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
extern PlayerTorpedo players_torpedoes[MAX_GLOBAL_TORPEDOES];
extern ConnectedPlayer players[MAX_CLIENTS];
extern SpaceGLGame spacegl_master;
extern pthread_mutex_t game_mutex;
extern int g_debug;
extern int global_tick;
extern uint8_t MASTER_SESSION_KEY[32];
extern uint8_t ALGO_KEYS[13][32]; /* Keys for algorithms 1-12 */
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

#define GALAXY_VERSION 20260221

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
                           double target_h, double target_m);

int read_all(int fd, void *buf, size_t len);
int write_all(int fd, const void *buf, size_t len);

#endif
