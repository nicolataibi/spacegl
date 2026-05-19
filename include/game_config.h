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

#define GALAXY_SIZE 40                  /* Galaxy side dimension in quadrants (40x40x40 = 64,000 quadrants) */
#define QUADRANT_SIZE 40.0              /* Single quadrant side dimension in sectors/units (40x40x40 units) */

#define TACTICAL_CUBE_W 1920            /* Native horizontal resolution of the 3D viewer (Full HD) */
#define TACTICAL_CUBE_H 1080            /* Native vertical resolution of the 3D viewer (Full HD) */

/* --- Performance & Timing (60Hz Pro-Performance) --- */
#define GAME_TICK_RATE          60      /* Server logic update frequency (60 Ticks Per Second) */
#define GAME_TICK_USEC          16666   /* Tick duration in microseconds for epoll and timers (1/60s) */
#define GAME_TICK_NSEC          16666666 /* Tick duration in nanoseconds for high-precision nanosleep */
#define GAME_MAX_PLAYERS        16      /* Maximum number of simultaneous commanders per server instance */

#define TIMER_DOCKING_SEQ       (10 * GAME_TICK_RATE / 2) /* Duration of the docking sequence (5 seconds total) */

/* --- Physics & Movement (Adjusted for 60Hz) --- */
#define SPEED_TORPEDO           0.225   /* Torpedo cruise speed (units per tick) */
#define SPEED_IMPULSE_MAX       1.0     /* Theoretical maximum impulse engine speed (units per tick) */
#define SPEED_HYPER_MAX         9.9     /* Maximum warp factor (Galactic diagonal in 10 seconds) */
#define COEFF_HYPER_DIVISOR     8.5736  /* Divisor for Hyperdrive speed calculation @ 60Hz (Diagonal in 40s) */
#define COEFF_IMPULSE_DIVISOR   500.0   /* Divisor for Impulse Drive percentage scale @ 60Hz */
#define SPEED_IMPULSE_TICK_MAX  0.2     /* Maximum impulse speed per tick (units/tick) */

#define RATIO_BASE_POWER        0.5     /* Base engine power multiplier for speed calculation */
#define RATIO_ENGINE_POWER      1.0     /* Speed scaling based on engine energy allocation */
#define RATIO_WEAPON_POWER      2.5     /* Ion Beam damage scaling based on weapon energy allocation */
#define RATIO_SHIELD_POWER      QUADRANT_SIZE /* Shield recharge scaling based on energy allocation */
#define RATIO_SENSOR_ERROR      2.5     /* Sensor noise multiplier when integrity is low */

#define DIST_BOUNDARY_MARGIN    0.1f    /* Safety margin from quadrant edges for collisions and UI */
#define DIST_QUADRANT_DIAGONAL_SQ (3.0 * QUADRANT_SIZE * QUADRANT_SIZE) /* Squared diagonal of a 40x40x40 cube (4800.0) */
#define DIST_APPROACH_MARGIN    0.1f    /* Arrival tolerance for the approach autopilot (APR) */
#define DIST_DECEL_START        1.0f    /* Distance from target at which the autopilot starts deceleration */
#define RATIO_DECEL_MIN         0.2f    /* Minimum residual speed during the automatic braking phase */
#define RATIO_DECEL_SLOPE       0.8f    /* Slope of the deceleration curve for smooth arrivals */
#define RATIO_COORD_RANDOM      10.0f   /* Precision factor for random coordinate generation */

/* --- Visual Interpolation (LERP) --- */
#define INTERP_SPEED_OBJECT     0.25    /* Ship position smoothing speed in the viewer (0.0 - 1.0) */
#define INTERP_SPEED_TORPEDO    0.01    /* Visual pursuit speed for torpedoes (Minimum for extreme smoothing) */
#define INTERP_SPEED_ROT        0.12    /* Bow rotation fluidity (Heading/Mark LERP) */

/* --- Resources & Limits --- */
#define MAX_ENERGY_CAPACITY     999999999999ULL /* Maximum reactor capacity (uint64_t for stellar economies) */
#define MAX_TORPEDO_CAPACITY    1000            /* Maximum reserve of torpedoes ready for launch in tubes */
#define MAX_CREW_EXPLORER         1012          /* Standard initial crew for the Explorer class */
#define ENERGY_BASE_RECHARGE    999999999999ULL /* Energy provided instantly during docking or respawn */

/* --- Combat Mechanics --- */
#define DMG_ION_BEAM_BASE         1500          /* Base phaser damage for each unit of energy spent */
#define DMG_ION_BEAM_NPC          10            /* Fixed base damage dealt by NPCs per fire tick */
#define DMG_TORPEDO             500             /* Destructive potential of a plasma torpedo (base) */
#define DMG_TORPEDO_PLATFORM    50000           /* Damage dealt by torpedoes to defense platforms */
#define DMG_TORPEDO_MONSTER     100000          /* Damage required to defeat a space monster */
#define SHIELD_MAX_STRENGTH     10000           /* Maximum absorption capacity for each shield quadrant */
#define TIMER_SHIELD_CHANGE_TICKS 180           /* Shield change duration in ticks (3 seconds at 60Hz) */
#define SHIELD_CHANGE_PER_TICK  (SHIELD_MAX_STRENGTH / TIMER_SHIELD_CHANGE_TICKS) /* Shield units per tick */
#define SHIELD_REGEN_DELAY      150             /* Wait ticks before post-impact regeneration starts */

#define PROB_MISFIRE_DEGRADED   30              /* Probability of torpedo misfire with systems at 50-75% */
#define PROB_NPC_ESCAPE         15              /* Percentage chance that an NPC attempts to flee if damaged */
#define PROB_SENSOR_FAIL_SRS    50              /* Integrity threshold below which SRS starts showing ghosts */
#define PROB_BOARD_SUCCESS_BASE 60              /* Base probability of success for a boarding party */
#define RATIO_BOARD_H_BONUS     0.2             /* Boarding success bonus for each % of enemy hull damage */

#define MAX_SYSTEMS             10              /* Number of managed subsystems (Engines, Sensors, etc.) */
#define DIST_NPC_PATROL_STEP    3.0f            /* Maximum length of each NPC patrol leg */
#define DIST_FLEE_LIMIT         8.5f            /* Distance beyond which an NPC stops fleeing from the player */

#define YIELD_BOARD_ENERGY_DIV  100             /* Divisor for calculating energy stolen during boarding */
#define YIELD_BOARD_CREW_MIN    5               /* Minimum number of capturable prisoners per boarding */
#define YIELD_BOARD_CREW_RANGE  25              /* Variable range of capturable prisoners */
#define YIELD_BOARD_NPC_MIN     50              /* Minimum stealable resources from an NPC cargo hold */
#define YIELD_BOARD_NPC_RANGE   150             /* Variable range of stealable resources */

#define MAX_RESOURCE_TYPES      8               /* Types of managed raw materials (Aetherium, Graphene, etc.) */

/* --- Distances (Sector Units) --- */
#define DIST_INTERACTION_MAX    3.1f            /* Maximum range for Solar Scooping and planetary Mining */
#define DIST_MINING_MAX         3.1f            /* Maximum range for mineral extraction from asteroids */
#define DIST_DOCKING_MAX        3.1f            /* Maximum distance to engage Starbase docking clamps */
#define DIST_SCOOPING_MAX       3.1f            /* Maximum distance for thermal recharge from a star */
#define DIST_BUOY_BOOST         1.2f            /* Radius within which a Comm Buoy boosts LRS sensors */
#define DIST_NEBULA_EFFECT      2.0f            /* Distance within which nebula effects are experienced */
#define DIST_EPSILON            0.05f           /* Numerical tolerance for proximity and stop calculations */
#define DIST_DISMANTLE_MAX      1.5f            /* Tractor beam range for wreck dismantling (DIS) */
#define DIST_BOARDING_MAX       1.0f            /* Maximum teleport range for boarding (BOR) */
#define DIST_COLLISION_SHIP     0.8f            /* Physical collision radius between ships and solid objects */
#define DIST_COLLISION_TORP     0.8f            /* Torpedo warhead activation radius for impact */
#define DIST_GRAVITY_WELL       3.0f            /* Range of action of the fatal attraction of Black Holes */
#define DIST_EVENT_HORIZON      0.6f            /* Critical distance for instantaneous destruction in a Black Hole */
#define DIST_MONSTER_ATTACK     4.0f            /* Range of the resonance beam from space monsters */
#define DIST_PULSAR_BEAM        2.5f            /* Range of lethal radiations emitted by Pulsars */

/* --- Timers (Ticks @ 60Hz) --- */
#define TIMER_TORP_LOAD         180             /* Torpedo tube reload time (exactly 3 seconds) */
#define TIMER_TORP_TIMEOUT      600             /* Maximum torpedo flight time before self-destruction (10s) */
#define TIMER_SUPERNOVA         3600            /* Catastrophic Supernova countdown duration (60 seconds) */
#define TIMER_WORMHOLE_SEQ      900             /* Total duration of Wormhole cinematic sequence (15s) */
#define TIMER_ALERT_LS          600             /* Critical life support alert message interval (10s) */
#define TIMER_MONSTER_PULSE     120             /* Frequency of biological entity attacks (2 seconds) */
#define TIMER_RENEGADE_INIT     (300 * GAME_TICK_RATE) /* Renegade status duration (10 minutes of game time) */

/* --- Base Rates (per tick) --- */
#define RATE_LS_REGEN           0.025           /* Life Support recharge rate if energy is present */
#define RATE_LS_DRAIN           0.05            /* Life Support degradation rate in the absence of energy */
#define RATE_REGEN_DRAIN_FACTOR 2.0             /* Energy/integrity conversion factor for repairs */
#define RATIO_ENERGY_REDUCTION  2.0             /* Energy reduction scaling during extreme maneuvers */

/* --- Buffer Sizes --- */
#define LARGE_DATA_BUFFER       65536           /* Buffer size for SRS/LRS output and saving */

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

#define GALAXY_OBJECT_MIN_QUASAR      27000
#define GALAXY_OBJECT_MAX_QUASAR      27999

#define GALAXY_OBJECT_MIN_DYSON       28000
#define GALAXY_OBJECT_MAX_DYSON       28999

#define GALAXY_OBJECT_MIN_HUB         29000
#define GALAXY_OBJECT_MAX_HUB         29999

#define GALAXY_OBJECT_MIN_RELIC       30000
#define GALAXY_OBJECT_MAX_RELIC       30999

#define GALAXY_OBJECT_MIN_RUPTURE     31000
#define GALAXY_OBJECT_MAX_RUPTURE     31999

#define GALAXY_OBJECT_MIN_SATELLITE   32000
#define GALAXY_OBJECT_MAX_SATELLITE   32999

#define GALAXY_OBJECT_MIN_STORM       33000
#define GALAXY_OBJECT_MAX_STORM       33999

#define GALAXY_OBJECT_MIN_ARTIFACT    34000
#define GALAXY_OBJECT_MAX_ARTIFACT    34999

#define GALAXY_OBJECT_MIN_WARP_GATE   35000
#define GALAXY_OBJECT_MAX_WARP_GATE   35999

#define GALAXY_OBJECT_MIN_NEUTRON_STAR 36000
#define GALAXY_OBJECT_MAX_NEUTRON_STAR 36999

#define GALAXY_OBJECT_MIN_MEGA_STRUCT 37000
#define GALAXY_OBJECT_MAX_MEGA_STRUCT 37999

#define GALAXY_OBJECT_MIN_DARK_CLOUD  38000
#define GALAXY_OBJECT_MAX_DARK_CLOUD  38999

#define GALAXY_OBJECT_MIN_SINGULARITY 39000
#define GALAXY_OBJECT_MAX_SINGULARITY 39999

#define GALAXY_OBJECT_MIN_PLASMA_STORM 40000
#define GALAXY_OBJECT_MAX_PLASMA_STORM 40999

#define GALAXY_OBJECT_MIN_ORBITAL_RING 41000
#define GALAXY_OBJECT_MAX_ORBITAL_RING 41999

#define GALAXY_OBJECT_MIN_TIME_ANOMALY 42000
#define GALAXY_OBJECT_MAX_TIME_ANOMALY 42999

#define GALAXY_OBJECT_MIN_VOID_CRYSTAL 43000
#define GALAXY_OBJECT_MAX_VOID_CRYSTAL 43999

#define GALAXY_OBJECT_MIN_SUBSPACE_ANOM 44000
#define GALAXY_OBJECT_MAX_SUBSPACE_ANOM 44999

#define GALAXY_OBJECT_MIN_DIFFUSE_NEBULA 45000
#define GALAXY_OBJECT_MAX_DIFFUSE_NEBULA 45999
#define GALAXY_OBJECT_MIN_DARK_NEBULA 46000
#define GALAXY_OBJECT_MAX_DARK_NEBULA 46999
#define GALAXY_OBJECT_MIN_PLANETARY_NEBULA 47000
#define GALAXY_OBJECT_MAX_PLANETARY_NEBULA 47999
#define GALAXY_OBJECT_MIN_SNR 48000
#define GALAXY_OBJECT_MAX_SNR 48999
#define GALAXY_OBJECT_MIN_GMC 49000
#define GALAXY_OBJECT_MAX_GMC 49999
#define GALAXY_OBJECT_MIN_INTERSTELLAR_FILAMENT 50000
#define GALAXY_OBJECT_MAX_INTERSTELLAR_FILAMENT 50999
#define GALAXY_OBJECT_MIN_INTERSTELLAR_BUBBLE 51000
#define GALAXY_OBJECT_MAX_INTERSTELLAR_BUBBLE 51999
#define GALAXY_OBJECT_MIN_BOK_GLOBULE 52000
#define GALAXY_OBJECT_MAX_BOK_GLOBULE 52999
#define GALAXY_OBJECT_MIN_CLUMP_CORE 53000
#define GALAXY_OBJECT_MAX_CLUMP_CORE 53999
#define GALAXY_OBJECT_MIN_ACCRETION_DISK 54000
#define GALAXY_OBJECT_MAX_ACCRETION_DISK 54999
#define GALAXY_OBJECT_MIN_RELATIVISTIC_JET 55000
#define GALAXY_OBJECT_MAX_RELATIVISTIC_JET 55999
#define GALAXY_OBJECT_MIN_SHOCK_WAVE 56000
#define GALAXY_OBJECT_MAX_SHOCK_WAVE 56999
#define GALAXY_OBJECT_MIN_STELLAR_BOW_SHOCK 57000
#define GALAXY_OBJECT_MAX_STELLAR_BOW_SHOCK 57999
#define GALAXY_OBJECT_MIN_COSMIC_VOID 58000
#define GALAXY_OBJECT_MAX_COSMIC_VOID 58999
#define GALAXY_OBJECT_MIN_COSMIC_FILAMENT 59000
#define GALAXY_OBJECT_MAX_COSMIC_FILAMENT 59999
#define GALAXY_OBJECT_MIN_EVENT_HORIZON 60000
#define GALAXY_OBJECT_MAX_EVENT_HORIZON 60999
#define GALAXY_OBJECT_MIN_KILONOVA 61000
#define GALAXY_OBJECT_MAX_KILONOVA 61999
#define GALAXY_OBJECT_MIN_GRAV_LENS 62000
#define GALAXY_OBJECT_MAX_GRAV_LENS 62999
#define GALAXY_OBJECT_MIN_GRB 63000
#define GALAXY_OBJECT_MAX_GRB 63999
#define GALAXY_OBJECT_MIN_GRAV_WAVE 64000
#define GALAXY_OBJECT_MAX_GRAV_WAVE 64999
#define GALAXY_OBJECT_MIN_PROTOPLANETARY_DISK 65000
#define GALAXY_OBJECT_MAX_PROTOPLANETARY_DISK 65999
#define GALAXY_OBJECT_MIN_DEBRIS_DISK 66000
#define GALAXY_OBJECT_MAX_DEBRIS_DISK 66999
#define GALAXY_OBJECT_MIN_PLANETESIMAL 67000
#define GALAXY_OBJECT_MAX_PLANETESIMAL 67999
#define GALAXY_OBJECT_MIN_ROGUE_PLANET 68000
#define GALAXY_OBJECT_MAX_ROGUE_PLANET 68999
#define GALAXY_OBJECT_MIN_BROWN_DWARF 69000
#define GALAXY_OBJECT_MAX_BROWN_DWARF 69999
#define GALAXY_OBJECT_MIN_ISO 70000
#define GALAXY_OBJECT_MAX_ISO 70999
#define GALAXY_OBJECT_MIN_MAG_RECONN 71000
#define GALAXY_OBJECT_MAX_MAG_RECONN 71999
#define GALAXY_OBJECT_MIN_CURRENT_SHEET 72000
#define GALAXY_OBJECT_MAX_CURRENT_SHEET 72999
#define GALAXY_OBJECT_MIN_HELIOSPHERE 73000
#define GALAXY_OBJECT_MAX_HELIOSPHERE 73999
#define GALAXY_OBJECT_MIN_TERM_SHOCK 74000
#define GALAXY_OBJECT_MAX_TERM_SHOCK 74999
#define GALAXY_OBJECT_MIN_MAGNETOSPHERE 75000
#define GALAXY_OBJECT_MAX_MAGNETOSPHERE 75999
#define GALAXY_OBJECT_MIN_COSMIC_STRING 76000
#define GALAXY_OBJECT_MAX_COSMIC_STRING 76999
#define GALAXY_OBJECT_MIN_DOMAIN_WALL 77000
#define GALAXY_OBJECT_MAX_DOMAIN_WALL 77999
#define GALAXY_OBJECT_MIN_DM_HALO 78000
#define GALAXY_OBJECT_MAX_DM_HALO 78999
#define GALAXY_OBJECT_MIN_IGM 79000
#define GALAXY_OBJECT_MAX_IGM 79999
#define GALAXY_OBJECT_MIN_CGM 80000
#define GALAXY_OBJECT_MAX_CGM 80999
#define GALAXY_OBJECT_MIN_LYMAN_ALPHA 81000
#define GALAXY_OBJECT_MAX_LYMAN_ALPHA 81999
#define GALAXY_OBJECT_MIN_CMB 82000
#define GALAXY_OBJECT_MAX_CMB 82999

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

#define GALAXY_CREATE_OBJECT_MIN_QUASAR              150
#define GALAXY_CREATE_OBJECT_MAX_QUASAR              200

#define GALAXY_CREATE_OBJECT_MIN_DYSON               100
#define GALAXY_CREATE_OBJECT_MAX_DYSON               150

#define GALAXY_CREATE_OBJECT_MIN_HUB                 50
#define GALAXY_CREATE_OBJECT_MAX_HUB                 80

#define GALAXY_CREATE_OBJECT_MIN_RELIC               150
#define GALAXY_CREATE_OBJECT_MAX_RELIC               200

#define GALAXY_CREATE_OBJECT_MIN_RUPTURE             100
#define GALAXY_CREATE_OBJECT_MAX_RUPTURE             150

#define GALAXY_CREATE_OBJECT_MIN_SATELLITE           400
#define GALAXY_CREATE_OBJECT_MAX_SATELLITE           600

#define GALAXY_CREATE_OBJECT_MIN_STORM               80
#define GALAXY_CREATE_OBJECT_MAX_STORM               120

#define GALAXY_CREATE_OBJECT_MIN_ARTIFACT            20
#define GALAXY_CREATE_OBJECT_MAX_ARTIFACT            40

#define GALAXY_CREATE_OBJECT_MIN_WARP_GATE           15
#define GALAXY_CREATE_OBJECT_MAX_WARP_GATE           30

#define GALAXY_CREATE_OBJECT_MIN_NEUTRON_STAR        50
#define GALAXY_CREATE_OBJECT_MAX_NEUTRON_STAR        80

#define GALAXY_CREATE_OBJECT_MIN_MEGA_STRUCT         10
#define GALAXY_CREATE_OBJECT_MAX_MEGA_STRUCT         25

#define GALAXY_CREATE_OBJECT_MIN_DARK_CLOUD          60
#define GALAXY_CREATE_OBJECT_MAX_DARK_CLOUD          100

#define GALAXY_CREATE_OBJECT_MIN_SINGULARITY         5
#define GALAXY_CREATE_OBJECT_MAX_SINGULARITY         15

#define GALAXY_CREATE_OBJECT_MIN_PLASMA_STORM        30
#define GALAXY_CREATE_OBJECT_MAX_PLASMA_STORM        60

#define GALAXY_CREATE_OBJECT_MIN_ORBITAL_RING        20
#define GALAXY_CREATE_OBJECT_MAX_ORBITAL_RING        40

#define GALAXY_CREATE_OBJECT_MIN_TIME_ANOMALY        10
#define GALAXY_CREATE_OBJECT_MAX_TIME_ANOMALY        20

#define GALAXY_CREATE_OBJECT_MIN_VOID_CRYSTAL        5
#define GALAXY_CREATE_OBJECT_MAX_VOID_CRYSTAL        15

#define GALAXY_CREATE_OBJECT_MIN_SUBSPACE_ANOM       10
#define GALAXY_CREATE_OBJECT_MAX_SUBSPACE_ANOM       25

#define GALAXY_CREATE_OBJECT_MIN_DIFFUSE_NEBULA      50
#define GALAXY_CREATE_OBJECT_MAX_DIFFUSE_NEBULA      100
#define GALAXY_CREATE_OBJECT_MIN_DARK_NEBULA         50
#define GALAXY_CREATE_OBJECT_MAX_DARK_NEBULA         100
#define GALAXY_CREATE_OBJECT_MIN_PLANETARY_NEBULA    30
#define GALAXY_CREATE_OBJECT_MAX_PLANETARY_NEBULA    60
#define GALAXY_CREATE_OBJECT_MIN_SNR                 20
#define GALAXY_CREATE_OBJECT_MAX_SNR                 40
#define GALAXY_CREATE_OBJECT_MIN_GMC                 40
#define GALAXY_CREATE_OBJECT_MAX_GMC                 80
#define GALAXY_CREATE_OBJECT_MIN_INTERSTELLAR_FILAMENT 30
#define GALAXY_CREATE_OBJECT_MAX_INTERSTELLAR_FILAMENT 60
#define GALAXY_CREATE_OBJECT_MIN_INTERSTELLAR_BUBBLE 25
#define GALAXY_CREATE_OBJECT_MAX_INTERSTELLAR_BUBBLE 50
#define GALAXY_CREATE_OBJECT_MIN_BOK_GLOBULE         40
#define GALAXY_CREATE_OBJECT_MAX_BOK_GLOBULE         80
#define GALAXY_CREATE_OBJECT_MIN_CLUMP_CORE          50
#define GALAXY_CREATE_OBJECT_MAX_CLUMP_CORE          100
#define GALAXY_CREATE_OBJECT_MIN_ACCRETION_DISK      20
#define GALAXY_CREATE_OBJECT_MAX_ACCRETION_DISK      40
#define GALAXY_CREATE_OBJECT_MIN_RELATIVISTIC_JET    15
#define GALAXY_CREATE_OBJECT_MAX_RELATIVISTIC_JET    30
#define GALAXY_CREATE_OBJECT_MIN_SHOCK_WAVE          20
#define GALAXY_CREATE_OBJECT_MAX_SHOCK_WAVE          40
#define GALAXY_CREATE_OBJECT_MIN_STELLAR_BOW_SHOCK   30
#define GALAXY_CREATE_OBJECT_MAX_STELLAR_BOW_SHOCK   60
#define GALAXY_CREATE_OBJECT_MIN_COSMIC_VOID         5
#define GALAXY_CREATE_OBJECT_MAX_COSMIC_VOID         10
#define GALAXY_CREATE_OBJECT_MIN_COSMIC_FILAMENT     10
#define GALAXY_CREATE_OBJECT_MAX_COSMIC_FILAMENT     20
#define GALAXY_CREATE_OBJECT_MIN_EVENT_HORIZON       20
#define GALAXY_OBJECT_MAX_EVENT_HORIZON_CREATE       40
#define GALAXY_CREATE_OBJECT_MIN_KILONOVA            5
#define GALAXY_CREATE_OBJECT_MAX_KILONOVA            15
#define GALAXY_CREATE_OBJECT_MIN_GRAV_LENS           10
#define GALAXY_CREATE_OBJECT_MAX_GRAV_LENS           25
#define GALAXY_CREATE_OBJECT_MIN_GRB                 5
#define GALAXY_CREATE_OBJECT_MAX_GRB                 15
#define GALAXY_CREATE_OBJECT_MIN_GRAV_WAVE           10
#define GALAXY_CREATE_OBJECT_MAX_GRAV_WAVE           25
#define GALAXY_CREATE_OBJECT_MIN_PROTOPLANETARY_DISK 30
#define GALAXY_CREATE_OBJECT_MAX_PROTOPLANETARY_DISK 60
#define GALAXY_CREATE_OBJECT_MIN_DEBRIS_DISK         40
#define GALAXY_CREATE_OBJECT_MAX_DEBRIS_DISK         80
#define GALAXY_CREATE_OBJECT_MIN_PLANETESIMAL        100
#define GALAXY_CREATE_OBJECT_MAX_PLANETESIMAL        200
#define GALAXY_CREATE_OBJECT_MIN_ROGUE_PLANET        50
#define GALAXY_CREATE_OBJECT_MAX_ROGUE_PLANET        100
#define GALAXY_CREATE_OBJECT_MIN_BROWN_DWARF         80
#define GALAXY_CREATE_OBJECT_MAX_BROWN_DWARF         150
#define GALAXY_CREATE_OBJECT_MIN_ISO                 20
#define GALAXY_CREATE_OBJECT_MAX_ISO                 50
#define GALAXY_CREATE_OBJECT_MIN_MAG_RECONN          30
#define GALAXY_CREATE_OBJECT_MAX_MAG_RECONN          60
#define GALAXY_CREATE_OBJECT_MIN_CURRENT_SHEET       30
#define GALAXY_CREATE_OBJECT_MAX_CURRENT_SHEET       60
#define GALAXY_CREATE_OBJECT_MIN_HELIOSPHERE         40
#define GALAXY_CREATE_OBJECT_MAX_HELIOSPHERE         80
#define GALAXY_CREATE_OBJECT_MIN_TERM_SHOCK          20
#define GALAXY_CREATE_OBJECT_MAX_TERM_SHOCK          40
#define GALAXY_CREATE_OBJECT_MIN_MAGNETOSPHERE       50
#define GALAXY_CREATE_OBJECT_MAX_MAGNETOSPHERE       100
#define GALAXY_CREATE_OBJECT_MIN_COSMIC_STRING       5
#define GALAXY_CREATE_OBJECT_MAX_COSMIC_STRING       10
#define GALAXY_CREATE_OBJECT_MIN_DOMAIN_WALL         2
#define GALAXY_CREATE_OBJECT_MAX_DOMAIN_WALL         5
#define GALAXY_CREATE_OBJECT_MIN_DM_HALO             10
#define GALAXY_CREATE_OBJECT_MAX_DM_HALO             20
#define GALAXY_CREATE_OBJECT_MIN_IGM                 10
#define GALAXY_CREATE_OBJECT_MAX_IGM                 30
#define GALAXY_CREATE_OBJECT_MIN_CGM                 15
#define GALAXY_CREATE_OBJECT_MAX_CGM                 40
#define GALAXY_CREATE_OBJECT_MIN_LYMAN_ALPHA         5
#define GALAXY_CREATE_OBJECT_MAX_LYMAN_ALPHA         15
#define GALAXY_CREATE_OBJECT_MIN_CMB                 1
#define GALAXY_CREATE_OBJECT_MAX_CMB                 1

/* --- Resource Costs (Energy) --- */
#define COST_ACTION_LOW          10             /* Base cost for minor operations (e.g., SRS, Lock) */
#define COST_ACTION_MED          50             /* Cost for standard operations (e.g., LRS, Map) */
#define COST_ACTION_HIGH         100            /* Cost for intensive operations (e.g., Shield Recharge) */
#define COST_ACTION_VERY_HIGH    250            /* Cost for torpedo launch and tactical procedures */
#define COST_ACTION_EXTREME      500            /* Cost for emergency maneuvers and resonance damage */
#define COST_CLOAK_INIT          500            /* Energy required to activate the cloaking field */
#define COST_HYPERDRIVE_INIT     5000           /* Energy required to engage the hyperspace route */
#define COST_WORMHOLE_INIT       5000           /* Energy required to generate an Einstein-Rosen bridge */
#define COST_BOARDING_INIT       5000           /* Energy requirement for boarding party teleportation */
#define COST_PROBE_LAUNCH        1000           /* Energy required for launching a subspace probe */
#define COST_MANEUVER_ADJUST     20             /* Cost for orientation micro-corrections (POS) */
#define COST_ENCRYPTION_SHIFT    50             /* Energy to change standard cryptographic frequency */
#define COST_PQC_INIT            250            /* Energy to initialize the Quantum-Resistant layer */

/* --- Resource Yields --- */
#define YIELD_HARVEST_MAX        100            /* Nominal 100% value for repaired hulls and systems */
#define YIELD_COMET_GAS          5              /* Amount of nebular gas extracted from a comet's tail */
#define YIELD_MINE_MIN           100            /* Minimum amount of minerals extracted from asteroids/planets */
#define YIELD_MINE_MAX           500            /* Maximum amount of minerals extracted per cycle */

#define YIELD_CONVERSION_AE      10             /* Energy multiplier from Aetherium (AE) */
#define YIELD_CONVERSION_DM      25             /* Energy multiplier from Dark Matter (DM) */
#define YIELD_CONVERSION_GAS     5              /* Energy multiplier from Nebular Gas */
#define YIELD_CONVERSION_COMP    4              /* Energy multiplier from Composite Materials */
#define YIELD_CONVERSION_TORP    20.0           /* Void-Essence units required for a torpedo warhead */

#define RATIO_CONVERSION_EFF_MIN 0.7            /* Minimum conversion efficiency with damaged systems */
#define RATIO_CONVERSION_EFF_MAX 1.0            /* Maximum nominal conversion efficiency (100%) */
#define RATIO_MINING_EFF_MIN     0.8            /* Minimum mining extraction efficiency */
#define RATIO_HARVEST_EFF_MIN    0.6            /* Minimum thermal/antimatter collection efficiency */

/* --- System Integrity Thresholds (%) --- */
#define THRESHOLD_SYS_CRITICAL   10.0           /* Integrity below which a system stops functioning */
#define THRESHOLD_SYS_DAMAGED    25.0           /* Threshold for severe malfunctions and telemetric noise */
#define THRESHOLD_SYS_DEGRADED   50.0           /* Start of performance loss and sensor inaccuracy */
#define THRESHOLD_SYS_STABLE     75.0           /* Minimum threshold for safe operations without hardware risks */

#endif
