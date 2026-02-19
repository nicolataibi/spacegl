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

#ifndef GAME_CONFIG_H
#define GAME_CONFIG_H

#define GALAXY_SIZE 40
#define QUADRANT_SIZE 40.0

#define TACTICAL_CUBE_W 1920
#define TACTICAL_CUBE_H 1080

/* --- Resources & Limits --- */
#define MAX_ENERGY_CAPACITY     999999999999ULL
#define MAX_TORPEDO_CAPACITY    1000
#define MAX_CREW_EXPLORER         1012
#define ENERGY_BASE_RECHARGE    999999999999ULL /* For docking/respawn */

/* --- Combat Mechanics --- */
#define DMG_ION_BEAM_BASE         1500
#define DMG_ION_BEAM_NPC          10      /* NPC base damage per tick */
#define DMG_TORPEDO             75000
#define DMG_TORPEDO_PLATFORM    50000
#define DMG_TORPEDO_MONSTER     100000
#define SHIELD_MAX_STRENGTH     10000
#define SHIELD_REGEN_DELAY      150     /* Ticks before regen after hit */

/* --- Distances (Sector Units) --- */
#define DIST_INTERACTION_MAX    3.1f
#define DIST_MINING_MAX         3.1f
#define DIST_DOCKING_MAX        3.1f
#define DIST_SCOOPING_MAX       3.1f
#define DIST_BUOY_BOOST         1.2f    /* Distance for LRS sensor boost */
#define DIST_NEBULA_EFFECT      2.0f    /* Distance for shield drain/sensor noise */
#define DIST_EPSILON            0.05f   /* Tolerance for floating point checks */
#define DIST_DISMANTLE_MAX      1.5f
#define DIST_BOARDING_MAX       1.0f
#define DIST_COLLISION_SHIP     0.8f
#define DIST_COLLISION_TORP     0.8f
#define DIST_GRAVITY_WELL       3.0f
#define DIST_EVENT_HORIZON      0.6f

/* --- Timers (Ticks @ 30Hz) --- */
#define TIMER_TORP_LOAD         150     /* 5 Seconds */
#define TIMER_TORP_TIMEOUT      300     /* 10 Seconds */
#define TIMER_SUPERNOVA         1800    /* 60 Seconds */
#define TIMER_WORMHOLE_SEQ      450     /* 15 Seconds */

/* --- Buffer Sizes --- */
#define LARGE_DATA_BUFFER       65536   /* For SRS/LRS scans */

/* --- Galaxy Object ID Ranges (Universal ID Schema) --- */
#define GALAXY_OBJECT_MIN_PLAYER      1
#define GALAXY_OBJECT_MAX_PLAYER      999

#define GALAXY_OBJECT_MIN_NPC         1000
#define GALAXY_OBJECT_MAX_NPC         3999

#define GALAXY_OBJECT_MIN_STARBASE    4000
#define GALAXY_OBJECT_MAX_STARBASE    4999

#define GALAXY_OBJECT_MIN_PLANET      5000
#define GALAXY_OBJECT_MAX_PLANET      6999

#define GALAXY_OBJECT_MIN_STAR        7000
#define GALAXY_OBJECT_MAX_STAR        10999

#define GALAXY_OBJECT_MIN_BLACKHOLE   11000
#define GALAXY_OBJECT_MAX_BLACKHOLE   11999

#define GALAXY_OBJECT_MIN_NEBULA      12000
#define GALAXY_OBJECT_MAX_NEBULA      12999

#define GALAXY_OBJECT_MIN_PULSAR      13000
#define GALAXY_OBJECT_MAX_PULSAR      13999

#define GALAXY_OBJECT_MIN_COMET       14000
#define GALAXY_OBJECT_MAX_COMET       14999

#define GALAXY_OBJECT_MIN_DERELICT    15000
#define GALAXY_OBJECT_MAX_DERELICT    17999

#define GALAXY_OBJECT_MIN_ASTEROID    18000
#define GALAXY_OBJECT_MAX_ASTEROID    20499

#define GALAXY_OBJECT_MIN_MINE        20500
#define GALAXY_OBJECT_MAX_MINE        21999

#define GALAXY_OBJECT_MIN_BUOY        22000
#define GALAXY_OBJECT_MAX_BUOY        22999

#define GALAXY_OBJECT_MIN_PLATFORM    23000
#define GALAXY_OBJECT_MAX_PLATFORM    23999

#define GALAXY_OBJECT_MIN_RIFT        24000
#define GALAXY_OBJECT_MAX_RIFT        24999

#define GALAXY_OBJECT_MIN_MONSTER     25000
#define GALAXY_OBJECT_MAX_MONSTER     25999

#define GALAXY_OBJECT_MIN_PROBE       26000
#define GALAXY_OBJECT_MAX_PROBE       26999

/* --- Galaxy Generation Ranges --- */
#define GALAXY_CREATE_OBJECT_MIN_NPC_FACTION        70
#define GALAXY_CREATE_OBJECT_MAX_NPC_FACTION        100

#define GALAXY_CREATE_OBJECT_MIN_ALLIANCE_WRECK      70
#define GALAXY_CREATE_OBJECT_MAX_ALLIANCE_WRECK      100

#define GALAXY_CREATE_OBJECT_MIN_ALIEN_WRECK         10
#define GALAXY_CREATE_OBJECT_MAX_ALIEN_WRECK         20

#define GALAXY_CREATE_OBJECT_MIN_BASE                150
#define GALAXY_CREATE_OBJECT_MAX_BASE                200

#define GALAXY_CREATE_OBJECT_MIN_PLANET              800
#define GALAXY_CREATE_OBJECT_MAX_PLANET              1000

#define GALAXY_CREATE_OBJECT_MIN_STAR                2000
#define GALAXY_CREATE_OBJECT_MAX_STAR                2500

#define GALAXY_CREATE_OBJECT_MIN_BLACK_HOLE          150
#define GALAXY_CREATE_OBJECT_MAX_BLACK_HOLE          200

#define GALAXY_CREATE_OBJECT_MIN_NEBULA              400
#define GALAXY_CREATE_OBJECT_MAX_NEBULA              500

#define GALAXY_CREATE_OBJECT_MIN_PULSAR              150
#define GALAXY_CREATE_OBJECT_MAX_PULSAR              200

#define GALAXY_CREATE_OBJECT_MIN_COMET               250
#define GALAXY_CREATE_OBJECT_MAX_COMET               300

#define GALAXY_CREATE_OBJECT_MIN_ASTEROID_CLUSTER    5
#define GALAXY_CREATE_OBJECT_MAX_ASTEROID_CLUSTER    15

#define GALAXY_CREATE_OBJECT_MIN_MINE_FIELD          3
#define GALAXY_CREATE_OBJECT_MAX_MINE_FIELD          8

#define GALAXY_CREATE_OBJECT_MIN_BUOY                80
#define GALAXY_CREATE_OBJECT_MAX_BUOY                100

#define GALAXY_CREATE_OBJECT_MIN_PLATFORM            150
#define GALAXY_CREATE_OBJECT_MAX_PLATFORM            200

#define GALAXY_CREATE_OBJECT_MIN_RIFT                40
#define GALAXY_CREATE_OBJECT_MAX_RIFT                50

#define GALAXY_CREATE_OBJECT_MIN_MONSTER             25
#define GALAXY_CREATE_OBJECT_MAX_MONSTER             30

#endif
