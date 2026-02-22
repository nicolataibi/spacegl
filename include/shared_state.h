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

#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdatomic.h>
#include "game_state.h"

#pragma pack(push, 1)

#define MAX_OBJECTS 256
#define MAX_BEAMS 64
#define SHM_NAME "/spacegl_shm"
#define CMD_QUEUE_SIZE 32

/* 
 * Shared State Structure
 */

typedef struct {
    double shm_x, shm_y, shm_z;
    double h, m;
    int type; /* 1=Player, 3=Base, 4=Star, 5=Planet, 6=BH, 10+=Enemies */
    int ship_class;
    int active;
    int health_pct;
    uint64_t energy;
    int plating;
    int hull_integrity;
    int faction;
    int id;
    int is_cloaked;
    char shm_name[64];
} SharedObject;

typedef struct {
    double shm_sx, shm_sy, shm_sz; 
    double shm_tx, shm_ty, shm_tz; 
    int active;
} SharedBeam;

typedef struct {
    double shm_x, shm_y, shm_z;
    int active;
} SharedPoint;

typedef struct {
    double shm_x, shm_y, shm_z;
    int species;
    int active;
} SharedDismantle;

typedef struct {
    int active;
    int q1, q2, q3;
    double s1, s2, s3;
    double eta;
    int status;
    double gx, gy, gz;
    double vx, vy, vz;
} SharedProbe;

/* Single Game Frame data */
typedef struct {
    uint64_t shm_energy;
    int shm_crew;
    int shm_prison_unit;
    int shm_torpedoes;
    int shm_composite_plating;
    double shm_hull_integrity;
    int shm_shields[6];
    uint64_t shm_cargo_energy;
    int shm_cargo_torpedoes;
    int inventory[10];
    double shm_system_health[10];
    double shm_power_dist[3];
    int shm_tube_state;
    int tube_load_timers[4];
    int current_tube;
    double shm_ion_beam_charge;
    double shm_life_support;
    int shm_anti_matter;
    int shm_lock_target;
    int Korthians;
    char quadrant[128];
    int shm_show_axes;
    int shm_show_grid;
    int shm_show_map;
    int shm_show_bridge;
    int shm_is_docked;
    int shm_red_alert;
    int shm_is_jammed;
    int shm_nav_state;
    int shm_map_filter;
    int is_cloaked;
    int shm_crypto_algo;
    uint32_t shm_encryption_flags;
    uint8_t shm_server_signature[64];
    uint8_t shm_server_pubkey[32];
    int shm_q[3];
    double shm_s[3];
    double shm_h;
    double shm_m;
    double shm_eta;
    int64_t shm_galaxy[41][41][41];
    
    double net_kbps;
    double net_efficiency;
    double net_jitter;
    double net_integrity;
    int net_last_packet_size;
    int net_avg_packet_size;
    int net_packet_count;
    long net_uptime;

    int object_count;
    SharedObject objects[MAX_OBJECTS];

    int beam_count;
    SharedBeam beams[MAX_BEAMS];
    
    int torpedo_count;
    SharedPoint torps[MAX_VISIBLE_TORPEDOES];
    SharedPoint boom;
    SharedPoint wormhole;
    SharedPoint jump_arrival;
    SharedPoint supernova_pos;
    int shm_sn_q[3];
    SharedDismantle dismantle;
    SharedPoint recovery_fx;
    SharedProbe probes[3];
    
    long long frame_id;
} __attribute__((aligned(64))) GameState;

typedef struct {
    char cmd[128];
} IPCCommand;

#define IPC_EVENT_QUEUE_SIZE 1024
#define IPC_EV_BEAM      1
#define IPC_EV_BOOM      2
#define IPC_EV_DISMANTLE 3
#define IPC_EV_RECOVERY  4
#define IPC_EV_JUMP      5
#define IPC_EV_TORPEDO   6

typedef struct {
    int type;
    double x1, y1, z1;
    double x2, y2, z2;
    int extra;
    int padding[2];
} __attribute__((aligned(64))) IPCEvent;

/* 
 * Shared IPC Structure (Mapped in /dev/shm)
 */
typedef struct {
    atomic_int read_index;  /* Buffer index for Viewer (0 or 1) */
    atomic_int write_index; /* Buffer index for Client (1 or 0) */
    
    atomic_int event_head;
    atomic_int event_tail;
    IPCEvent event_queue[IPC_EVENT_QUEUE_SIZE];

    /* Double Buffers */
    GameState buffers[2];
    
    /* Lock-Free Command Queue (Viewer -> Client) */
    atomic_int cmd_head;
    atomic_int cmd_tail;
    IPCCommand cmd_queue[CMD_QUEUE_SIZE];

    /* Legacy Mutex (Optional, for full segment protection if needed) */
    pthread_mutex_t mutex;
    sem_t data_ready;
} __attribute__((aligned(64))) SharedIPC;

#pragma pack(pop)

#endif
