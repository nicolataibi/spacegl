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

#pragma pack(push, 1)

#define MAX_OBJECTS 256
#define MAX_BEAMS 10
#define SHM_NAME "/spacegl_shm"

/* 
 * Shared State Structure
 * Replaces the textual format of /tmp/ultra_map.dat
 */

typedef struct {
    float shm_x, shm_y, shm_z;
    float h, m;
    int type; /* 1=Player, 3=Base, 4=Star, 5=Planet, 6=BH, 10+=Enemies */
    int ship_class;
    int active;
    int health_pct;
    int energy;
    int plating;
    int hull_integrity;
    int faction;
    int id;
    int is_cloaked;
    char shm_name[64];
} SharedObject;

typedef struct {
    float shm_sx, shm_sy, shm_sz; /* Source coordinates */
    float shm_tx, shm_ty, shm_tz; /* Target coordinates */
    int active;
} SharedBeam;

typedef struct {
    float shm_x, shm_y, shm_z;
    int active;
} SharedPoint;

typedef struct {
    float shm_x, shm_y, shm_z;
    int species;
    int active;
} SharedDismantle;

typedef struct {
    int active;
    int q1, q2, q3;
    float s1, s2, s3;
    float eta;
    int status;
    float gx, gy, gz;
    float vx, vy, vz;
} SharedProbe;

typedef struct {
    pthread_mutex_t mutex;
    sem_t data_ready;
    
    /* UI Info */
    int shm_energy;
    int shm_crew;
    int shm_prison_unit;
    int shm_torpedoes;
    int shm_composite_plating;
    float shm_hull_integrity;
    int shm_shields[6];
    int shm_cargo_energy;
    int shm_cargo_torpedoes;
    int inventory[10];
    float shm_system_health[10];
    float shm_power_dist[3];
    int shm_tube_state;
    float shm_ion_beam_charge;
    float shm_life_support;
    int shm_anti_matter;
    int shm_lock_target;
    int Korthians;
    char quadrant[128];
    int shm_show_axes;
    int shm_show_grid;
    int shm_show_map;
    int shm_show_bridge;
    int shm_is_docked;
    int shm_map_filter; /* 0=All, 1=Star, 2=Planet, 3=Base, 4=Hostile, 5=BH, 6=Nebula, 7=Pulsar, 8=Storm, 9=Comet, 10=Asteroid, 11=Derelict, 12=Mine, 13=Buoy, 14=Platform, 15=Rift, 16=Monster */
    int is_cloaked;
    int shm_crypto_algo;
    uint32_t shm_encryption_flags;
    uint8_t shm_server_signature[64];
    uint8_t shm_server_pubkey[32];
    int shm_q[3];
    float shm_s[3];
    float shm_h;
    float shm_m;
    int64_t shm_galaxy[11][11][11];
    
    /* Deep Space Telemetry Metrics */
    float net_kbps;
    float net_efficiency;
    float net_jitter;
    float net_integrity;
    int net_last_packet_size;
    int net_avg_packet_size;
    int net_packet_count;
    long net_uptime;

    int object_count;
    SharedObject objects[MAX_OBJECTS];

    /* FX */
    int beam_count;
    SharedBeam beams[MAX_BEAMS];
    
    SharedPoint torp;
    SharedPoint boom;
    SharedPoint wormhole;
    SharedPoint jump_arrival;
    SharedPoint supernova_pos;
    int shm_sn_q[3];
    SharedDismantle dismantle;
    SharedPoint recovery_fx;
    SharedProbe probes[3];
    
    /* Synchronization counter */
    long long frame_id;
} GameState;

#pragma pack(pop)

#endif
