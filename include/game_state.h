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

#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <stdbool.h>
#include <stdint.h>

#pragma pack(push, 1)

#define MAX_NET_OBJECTS 256
#define MAX_NET_BEAMS 8

typedef struct {
    double net_sx, net_sy, net_sz; /* Source coordinates */
    double net_tx, net_ty, net_tz; /* Target coordinates */
    int32_t active;
} NetBeam;

typedef struct {
    double net_x, net_y, net_z;
    double h, m;
    int32_t type;       /* 1=Player, 3=Base, 4=Star, 5=Planet, etc */
    int32_t ship_class; /* Specifica il modello 3D (es. Galaxy, Constitution) */
    int32_t active;
    int32_t health_pct; /* 0-100% Health/Energy status for HUD */
    int32_t energy;     /* Remaining energy units */
    int32_t plating;    /* Composite Plating */
    int32_t hull_integrity; /* Physical Hull % */
    int32_t faction;    /* Faction ID */
    int32_t id;         /* Universal Target ID */
    uint8_t is_cloaked; /* Whether the ship is cloaked */
    char name[64];  /* Captain name or ship name */
} NetObject;

typedef struct {
    double net_x, net_y, net_z;
    int32_t active;
} NetPoint;

typedef struct {
    double net_x, net_y, net_z;
    int32_t species;
    int32_t active;
} NetDismantle;

typedef struct {
    int32_t active;
    int32_t q1, q2, q3;
    double s1, s2, s3;
    double eta;
    int32_t status; /* 0:LAUNCHED, 1:ARRIVED, 2:TRANSMITTING */
    double gx, gy, gz; /* Galactic Absolute Position */
    double vx, vy, vz; /* Galactic Velocity Vector */
} NetProbe;

typedef struct {
    /* Galaxy Data - Moved to TOP for reliable alignment and sync */
    int64_t g[11][11][11];    /* The Galaxy Cube (BPNBS Encoding) */
    int32_t z[11][11][11];          /* Scanned Map Cube */

    /* Coordinates */
    int32_t q1, q2, q3;             /* Quadrant Position (X, Y, Z) */
    int32_t old_q1, old_q2, old_q3; /* Persistence tracking */
    double s1, s2, s3;          /* Sector Position (X, Y, Z) */

    /* Metadata and Totals */
    int32_t k9, b9;
    int64_t frame_id;
    char captain_name[64];

    /* Resources & Status */
    int32_t energy;
    int32_t torpedoes;
    int32_t cargo_energy;
    int32_t cargo_torpedoes;
    int32_t crew_count;
    int32_t prison_unit;
    int32_t inventory[10];
    int32_t species_counts[11];
    int32_t shields[6];
    
    /* Current Quadrant counts */
    int32_t k3, b3, st3, p3, bh3;
    
    /* Ship Systems */
    double van_h, van_m;
    int32_t lock_target;
    int32_t tube_state; /* 0:READY, 1:FIRING, 2:LOADING, 3:OFFLINE */
    double ion_beam_charge;
    double power_dist[3];
    uint8_t is_playing_dead;
    uint8_t is_cloaked;
    uint8_t red_alert;
    uint8_t is_jammed;
    double system_health[10];
    double hull_integrity;
    double life_support;
    
    /* Time & Meta */
    double t, t0;
    int32_t t9;
    int32_t anti_matter_count;

    /* Visual preferences */
    uint8_t show_axes;
    uint8_t show_grid;
    uint8_t show_bridge;
    uint8_t show_map;
    uint8_t map_filter;
    uint8_t shm_crypto_algo;
    int32_t composite_plating;

    /* Cryptographic & Signature Data */
    uint8_t server_signature[64];
    uint8_t server_pubkey[32];
    uint32_t encryption_flags;

    /* Multi-user sync (Objects in current sector) */
    int32_t object_count;
    NetObject objects[MAX_NET_OBJECTS];
    int32_t beam_count;
    NetBeam beams[MAX_NET_BEAMS];
    NetPoint torp;
    NetPoint boom;
    NetPoint wormhole;
    NetPoint jump_arrival;
    NetDismantle dismantle;
    NetPoint recovery_fx;
    NetProbe probes[3];
} SpaceGLGame;

#pragma pack(pop)

#endif
