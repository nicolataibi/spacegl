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

#define GALAXY_SIZE 40                  /* Dimensione lato galassia in quadranti (40x40x40 = 64.000 quadranti) */
#define QUADRANT_SIZE 40.0              /* Dimensione lato singolo quadrante in settori/unita' (40x40x40 unita') */

#define TACTICAL_CUBE_W 1920            /* Risoluzione orizzontale nativa del visore 3D (Full HD) */
#define TACTICAL_CUBE_H 1080            /* Risoluzione verticale nativa del visore 3D (Full HD) */

/* --- Performance & Timing (60Hz Pro-Performance) --- */
#define GAME_TICK_RATE          60      /* Frequenza di aggiornamento logica del server (60 Ticks Per Second) */
#define GAME_TICK_USEC          16666   /* Durata tick in microsecondi per epoll e timer (1/60s) */
#define GAME_TICK_NSEC          16666666 /* Durata tick in nanosecondi per nanosleep ad alta precisione */
#define GAME_MAX_PLAYERS        16      /* Numero massimo di comandanti contemporanei per istanza server */

#define TIMER_DOCKING_SEQ       (10 * GAME_TICK_RATE / 2) /* Durata della sequenza di attracco (5 secondi totali) */

/* --- Physics & Movement (Adjusted for 60Hz) --- */
#define SPEED_TORPEDO           0.225   /* Velocita' di crociera dei siluri (unita' per tick) */
#define SPEED_IMPULSE_MAX       1.0     /* Velocita' massima teorica motori a impulso (unita' per tick) */
#define SPEED_HYPER_MAX         9.9     /* Fattore di curvatura massimo (Diagonale galattica in 10 secondi) */
#define COEFF_HYPER_DIVISOR     8.5736  /* Divisore per calcolo velocita' Hyperdrive @ 60Hz (Diagonal in 40s) */
#define COEFF_IMPULSE_DIVISOR   500.0   /* Divisore per scala percentuale Impulse Drive @ 60Hz */
#define SPEED_IMPULSE_TICK_MAX  0.2     /* Velocita' massima impulso per tick (unita'/tick) */

#define RATIO_BASE_POWER        0.5     /* Moltiplicatore base potenza motori per il calcolo della velocita' */
#define RATIO_ENGINE_POWER      1.0     /* Scaling della velocita' in base all'allocazione energia motori */
#define RATIO_WEAPON_POWER      2.5     /* Scaling danno Ion Beam in base all'allocazione energia armi */
#define RATIO_SHIELD_POWER      QUADRANT_SIZE /* Scaling ricarica scudi in base all'allocazione energia */
#define RATIO_SENSOR_ERROR      2.5     /* Moltiplicatore rumore sensori quando l'integrita' e' bassa */

#define DIST_BOUNDARY_MARGIN    0.1f    /* Margine di sicurezza dai bordi del quadrante per collisioni e UI */
#define DIST_QUADRANT_DIAGONAL_SQ (3.0 * QUADRANT_SIZE * QUADRANT_SIZE) /* Diagonale al quadrato di un cubo 40x40x40 (4800.0) */
#define DIST_APPROACH_MARGIN    0.1f    /* Tolleranza di arrivo per l'autopilota di avvicinamento (APR) */
#define DIST_DECEL_START        1.0f    /* Distanza dal target a cui l'autopilota inizia la decelerazione */
#define RATIO_DECEL_MIN         0.2f    /* Velocita' minima residua durante la fase di frenata automatica */
#define RATIO_DECEL_SLOPE       0.8f    /* Pendenza della curva di decelerazione per arrivi fluidi */
#define RATIO_COORD_RANDOM      10.0f   /* Fattore di precisione per la generazione di coordinate casuali */

/* --- Visual Interpolation (LERP) --- */
#define INTERP_SPEED_OBJECT     0.25    /* Velocita' di smoothing posizione navi nel visore (0.0 - 1.0) */
#define INTERP_SPEED_TORPEDO    0.85    /* Velocita' di inseguimento visivo siluri (Alta per Zero-Loss FX) */
#define INTERP_SPEED_ROT        0.12    /* Fluidita' della rotazione della prua (Heading/Mark LERP) */

/* --- Resources & Limits --- */
#define MAX_ENERGY_CAPACITY     999999999999ULL /* Capacita' massima reattore (uint64_t per economie stellari) */
#define MAX_TORPEDO_CAPACITY    1000            /* Riserva massima di siluri pronti al lancio nei tubi */
#define MAX_CREW_EXPLORER         1012          /* Equipaggio standard iniziale per la classe Explorer */
#define ENERGY_BASE_RECHARGE    999999999999ULL /* Energia fornita istantaneamente durante docking o respawn */

/* --- Combat Mechanics --- */
#define DMG_ION_BEAM_BASE         1500          /* Danno base dei fasatori per ogni unita' di energia spesa */
#define DMG_ION_BEAM_NPC          10            /* Danno base fisso inflitto dagli NPC per tick di fuoco */
#define DMG_TORPEDO             500             /* Potenziale distruttivo di un siluro al plasma (base) */
#define DMG_TORPEDO_PLATFORM    50000           /* Danno inflitto dai siluri alle piattaforme di difesa */
#define DMG_TORPEDO_MONSTER     100000          /* Danno necessario per abbattere un mostro spaziale */
#define SHIELD_MAX_STRENGTH     10000           /* Capacita' massima di assorbimento per ogni quadrante di scudo */
#define TIMER_SHIELD_CHANGE_TICKS 180           /* Durata cambio scudi in tick (3 secondi a 60Hz) */
#define SHIELD_CHANGE_PER_TICK  (SHIELD_MAX_STRENGTH / TIMER_SHIELD_CHANGE_TICKS) /* Unita' scudo per tick */
#define SHIELD_REGEN_DELAY      150             /* Ticks di attesa prima che inizi la rigenerazione post-impatto */

#define PROB_MISFIRE_DEGRADED   30              /* Probabilita' di cilecca siluri con sistemi al 50-75% */
#define PROB_NPC_ESCAPE         15              /* Chance percentuale che un NPC tenti la fuga se danneggiato */
#define PROB_SENSOR_FAIL_SRS    50              /* Soglia integrita' sotto cui SRS inizia a mostrare fantasmi */
#define PROB_BOARD_SUCCESS_BASE 60              /* Probabilita' base di successo di una squadra d'arrembaggio */
#define RATIO_BOARD_H_BONUS     0.2             /* Bonus successo arrembaggio per ogni % di danno scafo nemico */

#define MAX_SYSTEMS             10              /* Numero di sottosistemi gestiti (Motori, Sensori, etc.) */
#define DIST_NPC_PATROL_STEP    3.0f            /* Lunghezza massima di ogni tratta di pattugliamento NPC */
#define DIST_FLEE_LIMIT         8.5f            /* Distanza oltre la quale un NPC smette di scappare dal giocatore */

#define YIELD_BOARD_ENERGY_DIV  100             /* Divisore per il calcolo dell'energia rubata in arrembaggio */
#define YIELD_BOARD_CREW_MIN    5               /* Minimo numero di prigionieri catturabili per arrembaggio */
#define YIELD_BOARD_CREW_RANGE  25              /* Range variabile di prigionieri catturabili */
#define YIELD_BOARD_NPC_MIN     50              /* Minimo risorse rubabili da una stiva NPC */
#define YIELD_BOARD_NPC_RANGE   150             /* Range variabile risorse rubabili */

#define MAX_RESOURCE_TYPES      8               /* Tipi di materiali grezzi gestiti (Aetherium, Grafene, etc.) */

/* --- Distances (Sector Units) --- */
#define DIST_INTERACTION_MAX    3.1f            /* Portata massima per Solar Scooping e Mining planetario */
#define DIST_MINING_MAX         3.1f            /* Portata massima per estrazione minerali da asteroidi */
#define DIST_DOCKING_MAX        3.1f            /* Distanza massima per agganciare i morsetti di una Starbase */
#define DIST_SCOOPING_MAX       3.1f            /* Distanza massima per ricarica termica da una stella */
#define DIST_BUOY_BOOST         1.2f            /* Raggio entro cui una Boa Comm potenzia i sensori LRS */
#define DIST_NEBULA_EFFECT      2.0f            /* Distanza entro cui si subiscono gli effetti di una nebulosa */
#define DIST_EPSILON            0.05f           /* Tolleranza numerica per calcoli di prossimita' e stop */
#define DIST_DISMANTLE_MAX      1.5f            /* Portata raggio traente per smantellamento relitti (DIS) */
#define DIST_BOARDING_MAX       1.0f            /* Portata massima teletrasporto per arrembaggi (BOR) */
#define DIST_COLLISION_SHIP     0.8f            /* Raggio fisico di collisione tra navi e oggetti soliti */
#define DIST_COLLISION_TORP     0.8f            /* Raggio di attivazione testata siluro per impatto */
#define DIST_GRAVITY_WELL       3.0f            /* Raggio d'azione dell'attrazione fatale dei Buchi Neri */
#define DIST_EVENT_HORIZON      0.6f            /* Distanza critica di distruzione istantanea in Buco Nero */
#define DIST_MONSTER_ATTACK     4.0f            /* Gittata del raggio di risonanza dei mostri spaziali */
#define DIST_PULSAR_BEAM        2.5f            /* Portata delle radiazioni letali emesse dalle Pulsar */

/* --- Timers (Ticks @ 60Hz) --- */
#define TIMER_TORP_LOAD         180             /* Tempo ricarica tubi lanciasiluri (3 secondi esatti) */
#define TIMER_TORP_TIMEOUT      600             /* Tempo volo massimo siluro prima dell'autodistruzione (10s) */
#define TIMER_SUPERNOVA         3600            /* Durata countdown catastrofico Supernova (60 secondi) */
#define TIMER_WORMHOLE_SEQ      900             /* Durata totale sequenza cinematografica Wormhole (15s) */
#define TIMER_ALERT_LS          600             /* Intervallo messaggi allerta supporto vitale critico (10s) */
#define TIMER_MONSTER_PULSE     120             /* Frequenza attacchi entita' biologiche (2 secondi) */
#define TIMER_RENEGADE_INIT     (300 * GAME_TICK_RATE) /* Durata stato Traditore (10 minuti di gioco) */

/* --- Base Rates (per tick) --- */
#define RATE_LS_REGEN           0.025           /* Tasso ricarica Supporto Vitale se energia presente */
#define RATE_LS_DRAIN           0.05            /* Tasso degrado Supporto Vitale in assenza di energia */
#define RATE_REGEN_DRAIN_FACTOR 2.0             /* Fattore di conversione energia/integrita' per riparazioni */
#define RATIO_ENERGY_REDUCTION  2.0             /* Scaling riduzione energia durante manovre estreme */

/* --- Buffer Sizes --- */
#define LARGE_DATA_BUFFER       65536           /* Dimensione buffer per output SRS/LRS e salvataggio */

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

/* --- Resource Costs (Energy) --- */
#define COST_ACTION_LOW          10             /* Costo base operazioni minori (es. SRS, Lock) */
#define COST_ACTION_MED          50             /* Costo operazioni standard (es. LRS, Map) */
#define COST_ACTION_HIGH         100            /* Costo operazioni intensive (es. Ricarica Scudi) */
#define COST_ACTION_VERY_HIGH    250            /* Costo lancio siluri e procedure tattiche */
#define COST_ACTION_EXTREME      500            /* Costo manovre d'emergenza e danni da risonanza */
#define COST_CLOAK_INIT          500            /* Energia richiesta per attivare il campo di occultamento */
#define COST_HYPERDRIVE_INIT     5000           /* Energia richiesta per agganciare la rotta iperspaziale */
#define COST_WORMHOLE_INIT       5000           /* Energia necessaria per generare un ponte di Einstein-Rosen */
#define COST_BOARDING_INIT       5000           /* Requisito energetico per il teletrasporto squadre arrembaggio */
#define COST_PROBE_LAUNCH        1000           /* Energia necessaria per il lancio di una sonda subspaziale */
#define COST_MANEUVER_ADJUST     20             /* Costo micro-correzioni di orientamento (POS) */
#define COST_ENCRYPTION_SHIFT    50             /* Energia per cambiare frequenza crittografica standard */
#define COST_PQC_INIT            250            /* Energia per inizializzare il layer Quantum-Resistant */

/* --- Resource Yields --- */
#define YIELD_HARVEST_MAX        100            /* Valore nominale 100% per scafi e sistemi riparati */
#define YIELD_COMET_GAS          5              /* Quantita' di gas nebulare estratto dalla coda di una cometa */
#define YIELD_MINE_MIN           100            /* Quantita' minima minerali estratti da asteroidi/pianeti */
#define YIELD_MINE_MAX           500            /* Quantita' massima minerali estratti per ciclo */

#define YIELD_CONVERSION_AE      10             /* Moltiplicatore energia da Aetherium (AE) */
#define YIELD_CONVERSION_DM      25             /* Moltiplicatore energia da Materia Oscura (DM) */
#define YIELD_CONVERSION_GAS     5              /* Moltiplicatore energia da Gas Nebulare */
#define YIELD_CONVERSION_COMP    4              /* Moltiplicatore energia da Materiali Compositi */
#define YIELD_CONVERSION_TORP    20.0           /* Unita' Void-Essence necessarie per una testata siluro */

#define RATIO_CONVERSION_EFF_MIN 0.7            /* Efficienza minima conversione con sistemi danneggiati */
#define RATIO_CONVERSION_EFF_MAX 1.0            /* Efficienza massima nominale conversione (100%) */
#define RATIO_MINING_EFF_MIN     0.8            /* Efficienza minima estrazione mineraria */
#define RATIO_HARVEST_EFF_MIN    0.6            /* Efficienza minima raccolta termica/antimateria */

/* --- System Integrity Thresholds (%) --- */
#define THRESHOLD_SYS_CRITICAL   10.0           /* Integrita' sotto la quale un sistema smette di funzionare */
#define THRESHOLD_SYS_DAMAGED    25.0           /* Soglia per malfunzionamenti gravi e rumore telemetrico */
#define THRESHOLD_SYS_DEGRADED   50.0           /* Inizio perdita prestazioni e imprecisione sensori */
#define THRESHOLD_SYS_STABLE     75.0           /* Soglia minima per operazioni sicure senza rischi hardware */

#endif
