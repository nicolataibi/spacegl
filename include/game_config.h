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

/* --- Resources & Limits --- */
#define MAX_ENERGY_CAPACITY     1000000
#define MAX_TORPEDO_CAPACITY    1000
#define MAX_CREW_EXPLORER         1012
#define ENERGY_BASE_RECHARGE    9999999 /* For docking/respawn */

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

#endif
