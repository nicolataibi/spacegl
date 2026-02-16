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
    
    /* Navigation & Physics State */
    double gx, gy, gz;      /* Absolute Galactic Coordinates */
    double target_gx, target_gy, target_gz;
    double dx, dy, dz;      /* Movement Vector */
    double target_h, target_m;
    double start_h, start_m;
    int nav_state;
    int nav_timer;
    double hyper_speed;
    double approach_dist;
    int apr_target;
    
    /* Torpedo State */
    bool torp_active;
    int torp_load_timer; /* Global/main tube timer (kept for compatibility) */
    int tube_load_timers[4];
    int current_tube;
    int torp_timeout;
    double tx, ty, tz;      /* Torpedo Current Position */
    double tdx, tdy, tdz;   /* Torpedo Vector */
    int torp_target;        /* ID of target */
    
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
} ConnectedPlayer;

typedef enum {
    AI_STATE_PATROL = 0,
    AI_STATE_CHASE,
    AI_STATE_FLEE,
    AI_STATE_ATTACK_RUN,
    AI_STATE_ATTACK_POSITION
} AIState;

/* --- Celestial and Tactical Entities --- */

typedef struct { int id, faction, q1, q2, q3; double x, y, z; int active; } NPCStar;
typedef struct { int id, q1, q2, q3; double x, y, z; int active; } NPCBlackHole;
typedef struct { int id, q1, q2, q3; double x, y, z; int type, active; } NPCNebula;
typedef struct { int id, q1, q2, q3; double x, y, z; int type, active; } NPCPulsar;
typedef struct { int id, q1, q2, q3; double x, y, z, h, m; double a, b, angle, speed, inc; double cx, cy, cz; int active; } NPCComet;
typedef struct { int id, q1, q2, q3; double x, y, z; float size; int resource_type, amount, active; } NPCAsteroid;
typedef struct { int id, q1, q2, q3; double x, y, z; int ship_class; int active; int faction; char name[64]; } NPCDerelict;
typedef struct { int id, q1, q2, q3; double x, y, z; int faction; int active; } NPCMine;
typedef struct { int id, q1, q2, q3; double x, y, z; int active; } NPCBuoy;
typedef struct { int id, faction, q1, q2, q3; double x, y, z; int health, energy, active; int fire_cooldown; } NPCPlatform;
typedef struct { int id, q1, q2, q3; double x, y, z; int active; } NPCRift;
typedef struct { int id, type, q1, q2, q3; double x, y, z; int health, energy, active; int behavior_timer; } NPCMonster;

typedef struct { 
    int id, faction, q1, q2, q3; 
    double x, y, z, h, m; 
    double gx, gy, gz; /* Absolute Galactic Coordinates 0-100 */
    int energy, active; 
    int health, plating;
    float engine_health; 
    int fire_cooldown; 
    AIState ai_state; 
    int target_player_idx; 
    int nav_timer; 
    double dx, dy, dz;
    double tx, ty, tz; 
    uint8_t is_cloaked;
    int ship_class;
    int death_timer;
    char name[64];
} NPCShip;

typedef struct { int id, q1, q2, q3; double x, y, z; int resource_type, amount, active; } NPCPlanet;
typedef struct { int id, faction, q1, q2, q3; double x, y, z; int health, active; } NPCBase;

/* --- Limits --- */

#define MAX_NPC 2500
#define MAX_PLANETS 1000
#define MAX_BASES 200
#define MAX_STARS 3000
#define MAX_BH 200
#define MAX_NEBULAS 500
#define MAX_PULSARS 200
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
#define MAX_Q_COMETS 8
#define MAX_Q_ASTEROIDS 40
#define MAX_Q_DERELICTS 8
#define MAX_Q_MINES 32
#define MAX_Q_BUOYS 8
#define MAX_Q_PLATFORMS 16
#define MAX_Q_RIFTS 4
#define MAX_Q_MONSTERS 4
#define MAX_Q_PLAYERS 32

/* Global Data accessed by modules */
extern NPCStar stars_data[MAX_STARS];
extern NPCBlackHole black_holes[MAX_BH];
extern NPCNebula nebulas[MAX_NEBULAS];
extern NPCPulsar pulsars[MAX_PULSARS];
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
extern ConnectedPlayer players[MAX_CLIENTS];
extern SpaceGLGame spacegl_master;
extern pthread_mutex_t game_mutex;
extern int g_debug;
extern int global_tick;
extern uint8_t MASTER_SESSION_KEY[32];
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

#define GALAXY_VERSION 20260216

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
} QuadrantIndex;

extern QuadrantIndex (*spatial_index)[41][41];
void rebuild_spatial_index();
void init_static_spatial_index();

#define IS_Q_VALID(q1,q2,q3) ((q1)>=1 && (q1)<=GALAXY_SIZE && (q2)>=1 && (q2)<=GALAXY_SIZE && (q3)>=1 && (q3)<=GALAXY_SIZE)

/* Helper to safely calculate quadrant from absolute coordinate (0-400) */
static inline int get_q_from_g(double g) {
    /* Use a small epsilon to avoid jitter jumping exactly on the boundary */
    int q = (int)((g + 1e-6) / 10.0) + 1;
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

void process_command(int p_idx, const char *cmd);
void update_game_logic();
bool is_player_in_nebula(int p_idx);
void apply_hull_damage(int p_idx, double amount);
void send_optimized_update(int p_idx, PacketUpdate *upd);

int read_all(int fd, void *buf, size_t len);
int write_all(int fd, const void *buf, size_t len);

#endif
