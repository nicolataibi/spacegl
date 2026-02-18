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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include "../include/game_config.h"
#include "../include/game_state.h"
#include "../include/server_internal.h"
#include "../include/network.h"

SpaceGLGame spacegl_master;
NPCShip npcs[MAX_NPC];
NPCStar stars_data[MAX_STARS];
NPCBlackHole black_holes[MAX_BH];
NPCNebula nebulas[MAX_NEBULAS];
NPCPulsar pulsars[MAX_PULSARS];
NPCComet comets[MAX_COMETS];
NPCAsteroid asteroids[MAX_ASTEROIDS];
NPCDerelict derelicts[MAX_DERELICTS];
NPCMine mines[MAX_MINES];
NPCBuoy buoys[MAX_BUOYS];
NPCPlatform platforms[MAX_PLATFORMS];
NPCRift rifts[MAX_RIFTS];
NPCMonster monsters[MAX_MONSTERS];
NPCPlanet planets[MAX_PLANETS];
NPCBase bases[MAX_BASES];
ConnectedPlayer players[MAX_CLIENTS];

const char* get_faction_name(int faction) {
    switch(faction) {
        case FACTION_ALLIANCE: return "Alliance";
        case FACTION_KORTHIAN:    return "Korthian";
        case FACTION_XYLARI:    return "Xylari";
        case FACTION_SWARM:       return "Swarm";
        case FACTION_VESPERIAN: return "Vesperian";
        case FACTION_JEM_HADAR:  return "Ascendant";
        case FACTION_THOLIAN:    return "Quarzite";
        case FACTION_GORN:       return "Saurian";
        case FACTION_GILDED:    return "Gilded";
        case FACTION_SPECIES_8472: return "Fluidic Void";
        case FACTION_BREEN:      return "Cryos";
        case FACTION_HIROGEN:    return "Apex";
        case 21: return "Asteroid";
        case 22: return "Derelict";
        case 23: return "Mine";
        case 24: return "Comm Buoy";
        case 25: return "Defense Platform";
        case 26: return "Spatial Rift";
        case 30: return "Crystalline Entity";
        case 31: return "Space Amoeba";
        default: return "Unknown";
    }
}

const char* get_resource_name(int type) {
    const char* res[] = {"None", "Aetherium", "Neo-Titanium", "Void-Essence", "Graphene", "Synaptics", "Nebular Gas", "Composite", "Dark-Matter"};
    if (type >= 0 && type <= 8) return res[type];
    return "Unknown";
}

const char* get_nebula_name(int type) {
    const char* neb[] = {"Standard", "High-Energy", "Dark Matter", "Ionic", "Gravimetric", "Temporal"};
    if (type >= 0 && type <= 5) return neb[type];
    return "Standard";
}

const char* get_crypto_name(int algo) {
    switch(algo) {
        case 0: return "NONE";
        case 12: return "PQC (Quantum Secure)";
        default: return "Legacy (HMAC/AES)";
    }
}

const char* get_ship_class_name(int ship_class) {
    switch(ship_class) {
        case SHIP_CLASS_LEGACY: return "Legacy Class";
        case SHIP_CLASS_SCOUT:      return "Scout Class";
        case SHIP_CLASS_HEAVY_CRUISER:    return "Heavy Cruiser";
        case SHIP_CLASS_MULTI_ENGINE: return "Multi-Engine Cruiser";
        case SHIP_CLASS_ESCORT:      return "Escort Class";
        case SHIP_CLASS_EXPLORER:       return "Explorer Class";
        case SHIP_CLASS_FLAGSHIP:    return "Flagship Class";
        case SHIP_CLASS_SCIENCE:     return "Science Vessel";
        case SHIP_CLASS_CARRIER:        return "Carrier Class";
        case SHIP_CLASS_TACTICAL:       return "Tactical Cruiser";
        case SHIP_CLASS_DIPLOMATIC:   return "Diplomatic Cruiser";
        case SHIP_CLASS_RESEARCH:       return "Research Vessel";
        case SHIP_CLASS_FRIGATE:  return "Frigate Class";
        case SHIP_CLASS_GENERIC_ALIEN: return "Vessel";
        default: return "Unknown";
    }
}

void print_help() {
    printf("Usage: ./stellar_viewer [command]\n");
    printf("Commands:\n");
    printf("  stats             Show global galaxy statistics and security status\n");
    printf("  master            Show global Master Ship status and resources\n");
    printf("  map <q3>          Show a 2D map slice for Z quadrant q3 (1-40)\n");
    printf("  list <q1> <q2> <q3>  List objects in quadrant (1-40)\n");
    printf("  players           List all persistent players\n");
    printf("  search <name>     Search for a player or ship by name\n");
    printf("  report            Generate a full localization report of all active objects\n");
}

int main(int argc, char *argv[]) {
    FILE *f = fopen("galaxy.dat", "rb");
    if (!f) {
        perror("Could not open galaxy.dat");
        return 1;
    }

    int version;
    if (fread(&version, sizeof(int), 1, f) != 1) {
        fprintf(stderr, "Failed to read version\n");
        fclose(f);
        return 1;
    }

    if (version != GALAXY_VERSION) {
        printf("Warning: Version mismatch. File: %d, Expected: %d\n", version, GALAXY_VERSION);
    }

    /* Diagnostics: check file size against expected size of data blocks */
    fseek(f, 0, SEEK_END);
    long actual_size = ftell(f);
    fseek(f, sizeof(int), SEEK_SET); /* Reset after version */
    long expected_data = sizeof(SpaceGLGame) + 
                         sizeof(NPCShip) * MAX_NPC + 
                         sizeof(NPCStar) * MAX_STARS + 
                         sizeof(NPCBlackHole) * MAX_BH + 
                         sizeof(NPCPlanet) * MAX_PLANETS + 
                         sizeof(NPCBase) * MAX_BASES + 
                         sizeof(NPCNebula) * MAX_NEBULAS + 
                         sizeof(NPCPulsar) * MAX_PULSARS + 
                         sizeof(NPCComet) * MAX_COMETS + 
                         sizeof(NPCAsteroid) * MAX_ASTEROIDS + 
                         sizeof(NPCDerelict) * MAX_DERELICTS + 
                         sizeof(NPCMine) * MAX_MINES + 
                         sizeof(NPCBuoy) * MAX_BUOYS + 
                         sizeof(NPCPlatform) * MAX_PLATFORMS + 
                         sizeof(NPCRift) * MAX_RIFTS + 
                         sizeof(NPCMonster) * MAX_MONSTERS + 
                         sizeof(ConnectedPlayer) * MAX_CLIENTS;
    
    if (actual_size < expected_data + (long)sizeof(int)) {
        printf("CRITICAL: galaxy.dat is smaller than expected (%ld < %ld). Data corruption likely.\n", actual_size, expected_data + (long)sizeof(int));
    }

#define CHECK_READ(ptr, size, count, stream) \
    if (fread(ptr, size, count, stream) != (size_t)(count)) { \
        fprintf(stderr, "Error reading from galaxy.dat\n"); \
        fclose(stream); return 1; \
    }

    CHECK_READ(&spacegl_master, sizeof(SpaceGLGame), 1, f);
    CHECK_READ(npcs, sizeof(NPCShip), MAX_NPC, f);
    CHECK_READ(stars_data, sizeof(NPCStar), MAX_STARS, f);
    CHECK_READ(black_holes, sizeof(NPCBlackHole), MAX_BH, f);
    CHECK_READ(planets, sizeof(NPCPlanet), MAX_PLANETS, f);
    CHECK_READ(bases, sizeof(NPCBase), MAX_BASES, f);
    CHECK_READ(nebulas, sizeof(NPCNebula), MAX_NEBULAS, f);
    CHECK_READ(pulsars, sizeof(NPCPulsar), MAX_PULSARS, f);
    CHECK_READ(comets, sizeof(NPCComet), MAX_COMETS, f);
    CHECK_READ(asteroids, sizeof(NPCAsteroid), MAX_ASTEROIDS, f);
    CHECK_READ(derelicts, sizeof(NPCDerelict), MAX_DERELICTS, f);
    CHECK_READ(mines, sizeof(NPCMine), MAX_MINES, f);
    CHECK_READ(buoys, sizeof(NPCBuoy), MAX_BUOYS, f);
    CHECK_READ(platforms, sizeof(NPCPlatform), MAX_PLATFORMS, f);
    CHECK_READ(rifts, sizeof(NPCRift), MAX_RIFTS, f);
    CHECK_READ(monsters, sizeof(NPCMonster), MAX_MONSTERS, f);
    CHECK_READ(players, sizeof(ConnectedPlayer), MAX_CLIENTS, f);
    fclose(f);

    if (argc < 2) {
        print_help();
        return 0;
    }

    if (strcmp(argv[1], "stats") == 0) {
        int n_active = 0;
        int s_active = 0;
        int b_active = 0;
        int p_active = 0;
        int bh_active = 0;
        int neb_active = 0;
        int pul_active = 0;
        int com_active = 0;
        int ast_active = 0;
        int der_active = 0;
        int mine_active = 0;
        int buoy_active = 0;
        int plat_active = 0;
        int rift_active = 0;
        int mon_active = 0;
        for (int i = 0; i < MAX_NPC; i++) {
            if (npcs[i].active) {
                n_active++;
            }
        }
        for (int i = 0; i < MAX_STARS; i++) {
            if (stars_data[i].active) {
                s_active++;
            }
        }
        for (int i = 0; i < MAX_BASES; i++) {
            if (bases[i].active) {
                b_active++;
            }
        }
        for (int i = 0; i < MAX_PLANETS; i++) {
            if (planets[i].active) {
                p_active++;
            }
        }
        for (int i = 0; i < MAX_BH; i++) {
            if (black_holes[i].active) {
                bh_active++;
            }
        }
        for (int i = 0; i < MAX_NEBULAS; i++) {
            if (nebulas[i].active) {
                neb_active++;
            }
        }
        for (int i = 0; i < MAX_PULSARS; i++) {
            if (pulsars[i].active) {
                pul_active++;
            }
        }
        for (int i = 0; i < MAX_COMETS; i++) {
            if (comets[i].active) {
                com_active++;
            }
        }
        for (int i = 0; i < MAX_ASTEROIDS; i++) {
            if (asteroids[i].active) {
                ast_active++;
            }
        }
        for (int i = 0; i < MAX_DERELICTS; i++) {
            if (derelicts[i].active) {
                der_active++;
            }
        }
        for (int i = 0; i < MAX_MINES; i++) {
            if (mines[i].active) {
                mine_active++;
            }
        }
        for (int i = 0; i < MAX_BUOYS; i++) {
            if (buoys[i].active) {
                buoy_active++;
            }
        }
        for (int i = 0; i < MAX_PLATFORMS; i++) {
            if (platforms[i].active) {
                plat_active++;
            }
        }
        for (int i = 0; i < MAX_RIFTS; i++) {
            if (rifts[i].active) {
                rift_active++;
            }
        }
        for (int i = 0; i < MAX_MONSTERS; i++) {
            if (monsters[i].active) {
                mon_active++;
            }
        }

        printf("--- Galaxy Statistics ---\n");
        printf("Version: %d\n", version);
        printf("Security Status:\n");
        if (spacegl_master.encryption_flags & 0x01) {
            printf("  Signature: VERIFIED (HMAC-SHA256)\n");
            printf("  Encryption: ENABLED (Flags: 0x%08X)\n", spacegl_master.encryption_flags);
            printf("  Crypto Algorithm: %s\n", get_crypto_name(spacegl_master.shm_crypto_algo));
        } else {
            printf("  Signature: NOT PRESENT / UNVERIFIED\n");
            printf("  Encryption: DISABLED\n");
        }
        
        printf("\nObject Totals:\n");
        printf("  Active NPCs:       %d\n", n_active);
        printf("  Stars:             %d\n", s_active);
        printf("  Bases:             %d\n", b_active);
        printf("  Planets:           %d\n", p_active);
        printf("  Black Holes:       %d\n", bh_active);
        printf("  Nebulas:           %d\n", neb_active);
        printf("  Pulsars:           %d\n", pul_active);
        printf("  Comets:            %d\n", com_active);
        printf("  Asteroids:         %d\n", ast_active);
        printf("  Derelicts:         %d\n", der_active);
        printf("  Minefields:        %d\n", mine_active);
        printf("  Comm Buoys:        %d\n", buoy_active);
        printf("  Defense Platforms: %d\n", plat_active);
        printf("  Spatial Rifts:     %d\n", rift_active);
        printf("  Space Monsters:    %d\n", mon_active);

        printf("\nFaction Counts (SpaceGL Master):\n");
        for (int i = 0; i <= 10; i++) {
            if (spacegl_master.species_counts[i] > 0) {
                printf("  %-12s: %d\n", get_faction_name(i), spacegl_master.species_counts[i]);
            }
        }

        printf("\nQuick Player Summary (First 5):\n");
        int p_shown = 0;
        for (int i = 0; i < MAX_CLIENTS && p_shown < 5; i++) {
            if (players[i].name[0] != '\0') {
                printf("  #%d: %s [%d,%d,%d] %s\n", i, players[i].name, players[i].state.q1, players[i].state.q2, players[i].state.q3, players[i].state.is_cloaked ? "[CLOAKED]" : "");
                p_shown++;
            }
        }

        printf("\nGalaxy Master Metrics: K9=%d, B9=%d, FrameID=%lld\n", spacegl_master.k9, spacegl_master.b9, (long long)spacegl_master.frame_id);
    } 
    else if (strcmp(argv[1], "master") == 0) {
        printf("--- Galaxy Master Ship Status ---\n");
        
        /* If master captain is empty, try to show the first active player as 'Master' */
        SpaceGLGame *display_state = &spacegl_master;
        const char *name = spacegl_master.captain_name;
        int faction = FACTION_ALLIANCE;
        
        if (name[0] == '\0') {
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (players[i].name[0] != '\0') {
                    display_state = &players[i].state;
                    name = players[i].name;
                    faction = players[i].faction;
                    printf("(Note: Global master is empty, showing Player #%d as Master)\n\n", i);
                    break;
                }
            }
        }

        printf("Captain/Ship: %s (%s)\n", name[0] ? name : "Unknown", get_faction_name(faction));
        printf("Position: [%d,%d,%d] Sector: %.1f, %.1f, %.1f\n", 
               display_state->q1, display_state->q2, display_state->q3,
               display_state->s1, display_state->s2, display_state->s3);
        printf("Vitals: Energy=%" PRIu64 " Torpedoes=%d Hull=%.1f%% Crew=%d\n", 
               display_state->energy, display_state->torpedoes, display_state->hull_integrity, display_state->crew_count);
        printf("Systems: LifeSupport=%.1f%% IonBeamCharge=%.1f%%\n", 
               display_state->life_support, display_state->ion_beam_charge);
        printf("Shields: [%d, %d, %d, %d, %d, %d]\n", 
               display_state->shields[0], display_state->shields[1], display_state->shields[2],
               display_state->shields[3], display_state->shields[4], display_state->shields[5]);
        printf("Status: %s %s %s\n", 
               display_state->red_alert ? "[RED ALERT]" : "[NORMAL]",
               display_state->is_cloaked ? "[CLOAKED]" : "[VISIBLE]",
               display_state->is_jammed ? "[JAMMED]" : "");
        
        printf("\nInventory:\n");
        for (int i = 1; i <= 8; i++) {
            if (display_state->inventory[i] > 0) {
                printf("  %-15s: %d\n", get_resource_name(i), display_state->inventory[i]);
            }
        }
    }
    else if (strcmp(argv[1], "report") == 0) {
        printf("--- GALAXY LOCALIZATION REPORT ---\n");
        printf("%-10s %-8s %-15s %-12s %s\n", "TYPE", "ID", "COORD", "FACTION", "NAME/INFO");
        
        int counts[20] = {0};

        for(int i=0; i<MAX_CLIENTS; i++) if(players[i].name[0] != '\0') {
            printf("%-10s %-8d [%2d,%2d,%2d] %-12s %s\n", "PLAYER", i, players[i].state.q1, players[i].state.q2, players[i].state.q3, get_faction_name(players[i].faction), players[i].name);
            counts[0]++;
        }

        for(int i=0; i<MAX_NPC; i++) if(npcs[i].active) {
            printf("%-10s %-8d [%2d,%2d,%2d] %-12s %s\n", "NPC", npcs[i].id+GALAXY_OBJECT_MIN_NPC, npcs[i].q1, npcs[i].q2, npcs[i].q3, get_faction_name(npcs[i].faction), npcs[i].name);
            counts[1]++;
        }

        for(int i=0; i<MAX_BASES; i++) if(bases[i].active) {
            printf("%-10s %-8d [%2d,%2d,%2d] %-12s %s\n", "BASE", bases[i].id+GALAXY_OBJECT_MIN_STARBASE, bases[i].q1, bases[i].q2, bases[i].q3, get_faction_name(bases[i].faction), "Starbase");
            counts[2]++;
        }

        for(int i=0; i<MAX_PLANETS; i++) if(planets[i].active) {
            printf("%-10s %-8d [%2d,%2d,%2d] %-12s Resource: %s\n", "PLANET", planets[i].id+GALAXY_OBJECT_MIN_PLANET, planets[i].q1, planets[i].q2, planets[i].q3, "None", get_resource_name(planets[i].resource_type));
            counts[3]++;
        }

        for(int i=0; i<MAX_STARS; i++) if(stars_data[i].active) {
            printf("%-10s %-8d [%2d,%2d,%2d] %-12s %s\n", "STAR", stars_data[i].id+GALAXY_OBJECT_MIN_STAR, stars_data[i].q1, stars_data[i].q2, stars_data[i].q3, "None", "Star");
            counts[4]++;
        }

        for(int i=0; i<MAX_BH; i++) if(black_holes[i].active) {
            printf("%-10s %-8d [%2d,%2d,%2d] %-12s %s\n", "BH", black_holes[i].id+GALAXY_OBJECT_MIN_BLACKHOLE, black_holes[i].q1, black_holes[i].q2, black_holes[i].q3, "None", "Black Hole");
            counts[5]++;
        }

        for(int i=0; i<MAX_NEBULAS; i++) if(nebulas[i].active) {
            printf("%-10s %-8d [%2d,%2d,%2d] %-12s %s\n", "NEBULA", nebulas[i].id+GALAXY_OBJECT_MIN_NEBULA, nebulas[i].q1, nebulas[i].q2, nebulas[i].q3, "None", get_nebula_name(nebulas[i].type));
            counts[6]++;
        }

        for(int i=0; i<MAX_PULSARS; i++) if(pulsars[i].active) {
            printf("%-10s %-8d [%2d,%2d,%2d] %-12s %s\n", "PULSAR", pulsars[i].id+GALAXY_OBJECT_MIN_PULSAR, pulsars[i].q1, pulsars[i].q2, pulsars[i].q3, "None", "Pulsar");
            counts[7]++;
        }

        for(int i=0; i<MAX_COMETS; i++) if(comets[i].active) {
            printf("%-10s %-8d [%2d,%2d,%2d] %-12s %s\n", "COMET", comets[i].id+GALAXY_OBJECT_MIN_COMET, comets[i].q1, comets[i].q2, comets[i].q3, "None", "Comet");
            counts[8]++;
        }

        for(int i=0; i<MAX_ASTEROIDS; i++) if(asteroids[i].active) {
            printf("%-10s %-8d [%2d,%2d,%2d] %-12s Resource: %s\n", "ASTEROID", asteroids[i].id+GALAXY_OBJECT_MIN_ASTEROID, asteroids[i].q1, asteroids[i].q2, asteroids[i].q3, "None", get_resource_name(asteroids[i].resource_type));
            counts[9]++;
        }

        for(int i=0; i<MAX_DERELICTS; i++) if(derelicts[i].active) {
            printf("%-10s %-8d [%2d,%2d,%2d] %-12s Class: %s Name: %s\n", "DERELICT", derelicts[i].id+GALAXY_OBJECT_MIN_DERELICT, derelicts[i].q1, derelicts[i].q2, derelicts[i].q3, get_faction_name(derelicts[i].faction), get_ship_class_name(derelicts[i].ship_class), derelicts[i].name);
            counts[10]++;
        }

        for(int i=0; i<MAX_MINES; i++) if(mines[i].active) {
            printf("%-10s %-8d [%2d,%2d,%2d] %-12s %s\n", "MINE", mines[i].id+GALAXY_OBJECT_MIN_MINE, mines[i].q1, mines[i].q2, mines[i].q3, get_faction_name(mines[i].faction), "Mine");
            counts[11]++;
        }

        for(int i=0; i<MAX_BUOYS; i++) if(buoys[i].active) {
            printf("%-10s %-8d [%2d,%2d,%2d] %-12s %s\n", "BUOY", buoys[i].id+GALAXY_OBJECT_MIN_BUOY, buoys[i].q1, buoys[i].q2, buoys[i].q3, "None", "Comm Buoy");
            counts[12]++;
        }

        for(int i=0; i<MAX_PLATFORMS; i++) if(platforms[i].active) {
            printf("%-10s %-8d [%2d,%2d,%2d] %-12s %s\n", "PLATFORM", platforms[i].id+GALAXY_OBJECT_MIN_PLATFORM, platforms[i].q1, platforms[i].q2, platforms[i].q3, get_faction_name(platforms[i].faction), "Defense Platform");
            counts[13]++;
        }

        for(int i=0; i<MAX_RIFTS; i++) if(rifts[i].active) {
            printf("%-10s %-8d [%2d,%2d,%2d] %-12s %s\n", "RIFT", rifts[i].id+GALAXY_OBJECT_MIN_RIFT, rifts[i].q1, rifts[i].q2, rifts[i].q3, "None", "Spatial Rift");
            counts[14]++;
        }

        for(int i=0; i<MAX_MONSTERS; i++) if(monsters[i].active) {
            printf("%-10s %-8d [%2d,%2d,%2d] %-12s %s\n", "MONSTER", monsters[i].id+GALAXY_OBJECT_MIN_MONSTER, monsters[i].q1, monsters[i].q2, monsters[i].q3, get_faction_name(monsters[i].type), "Space Monster");
            counts[15]++;
        }
        
        printf("\n--- Summary ---\n");
        printf("Players: %d, NPCs: %d, Bases: %d, Planets: %d, Stars: %d\n", counts[0], counts[1], counts[2], counts[3], counts[4]);
        printf("BlackHoles: %d, Nebulas: %d, Pulsars: %d, Comets: %d, Asteroids: %d\n", counts[5], counts[6], counts[7], counts[8], counts[9]);
        printf("Derelicts: %d, Mines: %d, Buoys: %d, Platforms: %d, Rifts: %d, Monsters: %d\n", counts[10], counts[11], counts[12], counts[13], counts[14], counts[15]);
    }
    else if (strcmp(argv[1], "map") == 0 && argc == 3) {
        int q3 = atoi(argv[2]);
        if (q3 < 1 || q3 > GALAXY_SIZE) {
            printf("Invalid Z quadrant (1-%d)\n", GALAXY_SIZE);
            return 1;
        }
        printf("--- Galaxy Map Slice (Z=%d) ---\n", q3);
        printf("    ");
        for (int x = 1; x <= GALAXY_SIZE; x++) {
            printf("%2d ", x % 100);
        }
        printf(" (X)\n");
        for (int j = 1; j <= GALAXY_SIZE; j++) {
            printf("%2d ", j);
            for (int i = 1; i <= GALAXY_SIZE; i++) {
                long long bpnbs = spacegl_master.g[i][j][q3];
                int mon = (bpnbs / 10000000000000000LL) % 10;
                int user = (bpnbs / 1000000000000000LL) % 10;
                int rift = (bpnbs / 100000000000000LL) % 10;
                int plat = (bpnbs / 10000000000000LL) % 10;
                int buoy = (bpnbs / 1000000000000LL) % 10;
                int mine = (bpnbs / 100000000000LL) % 10;
                int der = (bpnbs / 10000000000LL) % 10;
                int ast = (bpnbs / 1000000000LL) % 10;
                int com = (bpnbs / 100000000LL) % 10;
                int stm = (bpnbs / 10000000LL) % 10;
                int pul = (bpnbs / 1000000LL) % 10;
                int neb = (bpnbs / 100000LL) % 10;
                int bh = (bpnbs / 10000LL) % 10;
                int p = (bpnbs / 1000LL) % 10;
                int n = (bpnbs / 100LL) % 10;
                int b = (bpnbs / 10LL) % 10;
                int s = bpnbs % 10;
                
                if (stm > 0) {
                    printf(" ! "); /* Ion Storm */
                } else if (mon > 0) {
                    printf(" M "); /* Monster */
                } else if (n > 0) {
                    printf(" N ");   /* NPC */
                } else if (user > 0) {
                    printf(" U "); /* Player */
                } else if (rift > 0) {
                    printf(" R "); /* Rift */
                } else if (plat > 0) {
                    printf(" T "); /* Turret */
                } else if (buoy > 0) {
                    printf(" @ "); /* Buoy */
                } else if (mine > 0) {
                    printf(" X "); /* Mine */
                } else if (der > 0) {
                    printf(" D "); /* Derelict */
                } else if (ast > 0) {
                    printf(" A "); /* Asteroid */
                } else if (com > 0) {
                    printf(" C "); /* Comet */
                } else if (pul > 0) {
                    printf(" * "); /* Pulsar */
                } else if (neb > 0) {
                    printf(" ~ "); /* Nebula */
                } else if (bh > 0) {
                    printf(" H ");  /* Black Hole */
                } else if (p > 0) {
                    printf(" P ");   /* Planet */
                } else if (b > 0) {
                    printf(" B ");   /* Base */
                } else if (s > 0) {
                    printf(" S ");   /* Star */
                } else {
                    printf(" . ");
                }
            }
            printf(" (Y:%d)\n", j);
        }
    }
    else if (strcmp(argv[1], "list") == 0 && argc == 5) {
        int q1 = atoi(argv[2]);
        int q2 = atoi(argv[3]);
        int q3 = atoi(argv[4]);
        
        if (!IS_Q_VALID(q1, q2, q3)) {
            printf("Invalid quadrant coordinates (1-40)\n");
            return 1;
        }

        printf("--- Objects in Quadrant [%d,%d,%d] ---\n", q1, q2, q3);
        
        long long bpnbs = spacegl_master.g[q1][q2][q3];
        printf("BPNBS Encoding: %017lld\n", bpnbs);

        for(int i=0; i<MAX_NPC; i++) if(npcs[i].active && npcs[i].q1 == q1 && npcs[i].q2 == q2 && npcs[i].q3 == q3)
            printf("[NPC] ID:%d Faction:%s Coord:%.1f,%.1f,%.1f Energy:%" PRIu64 " AI:%d %s\n", npcs[i].id+GALAXY_OBJECT_MIN_NPC, get_faction_name(npcs[i].faction), npcs[i].x, npcs[i].y, npcs[i].z, npcs[i].energy, npcs[i].ai_state, npcs[i].is_cloaked ? "[CLOAKED]" : "");

        for(int i=0; i<MAX_MONSTERS; i++) if(monsters[i].active && monsters[i].q1 == q1 && monsters[i].q2 == q2 && monsters[i].q3 == q3)
            printf("[MONSTER] ID:%d Type:%s Coord:%.1f,%.1f,%.1f Health:%d\n", monsters[i].id+GALAXY_OBJECT_MIN_MONSTER, get_faction_name(monsters[i].type), monsters[i].x, monsters[i].y, monsters[i].z, monsters[i].health);

        for(int i=0; i<MAX_BASES; i++) if(bases[i].active && bases[i].q1 == q1 && bases[i].q2 == q2 && bases[i].q3 == q3)
            printf("[BASE] ID:%d Faction:%s Coord:%.1f,%.1f,%.1f Health:%d\n", bases[i].id+GALAXY_OBJECT_MIN_STARBASE, get_faction_name(bases[i].faction), bases[i].x, bases[i].y, bases[i].z, bases[i].health);

        for(int i=0; i<MAX_PLANETS; i++) if(planets[i].active && planets[i].q1 == q1 && planets[i].q2 == q2 && planets[i].q3 == q3)
            printf("[PLANET] ID:%d Resource:%s Coord:%.1f,%.1f,%.1f Reserves:%d\n", planets[i].id+GALAXY_OBJECT_MIN_PLANET, get_resource_name(planets[i].resource_type), planets[i].x, planets[i].y, planets[i].z, planets[i].amount);

        for(int i=0; i<MAX_STARS; i++) if(stars_data[i].active && stars_data[i].q1 == q1 && stars_data[i].q2 == q2 && stars_data[i].q3 == q3)
            printf("[STAR] ID:%d Coord:%.1f,%.1f,%.1f\n", stars_data[i].id+GALAXY_OBJECT_MIN_STAR, stars_data[i].x, stars_data[i].y, stars_data[i].z);

        for(int i=0; i<MAX_BH; i++) if(black_holes[i].active && black_holes[i].q1 == q1 && black_holes[i].q2 == q2 && black_holes[i].q3 == q3)
            printf("[BLACK HOLE] ID:%d Coord:%.1f,%.1f,%.1f\n", black_holes[i].id+GALAXY_OBJECT_MIN_BLACKHOLE, black_holes[i].x, black_holes[i].y, black_holes[i].z);

        for(int i=0; i<MAX_NEBULAS; i++) if(nebulas[i].active && nebulas[i].q1 == q1 && nebulas[i].q2 == q2 && nebulas[i].q3 == q3)
            printf("[NEBULA] ID:%d Type:%s Coord:%.1f,%.1f,%.1f\n", nebulas[i].id+GALAXY_OBJECT_MIN_NEBULA, get_nebula_name(nebulas[i].type), nebulas[i].x, nebulas[i].y, nebulas[i].z);

        for(int i=0; i<MAX_PULSARS; i++) if(pulsars[i].active && pulsars[i].q1 == q1 && pulsars[i].q2 == q2 && pulsars[i].q3 == q3)
            printf("[PULSAR] ID:%d Coord:%.1f,%.1f,%.1f\n", pulsars[i].id+GALAXY_OBJECT_MIN_PULSAR, pulsars[i].x, pulsars[i].y, pulsars[i].z);

        for(int i=0; i<MAX_COMETS; i++) if(comets[i].active && comets[i].q1 == q1 && comets[i].q2 == q2 && comets[i].q3 == q3)
            printf("[COMET] ID:%d Coord:%.1f,%.1f,%.1f Angle:%.3f Speed:%.3f\n", comets[i].id+GALAXY_OBJECT_MIN_COMET, comets[i].x, comets[i].y, comets[i].z, comets[i].angle, comets[i].speed);

        for(int i=0; i<MAX_ASTEROIDS; i++) if(asteroids[i].active && asteroids[i].q1 == q1 && asteroids[i].q2 == q2 && asteroids[i].q3 == q3)
            printf("[ASTEROID] ID:%d Resource:%s Coord:%.1f,%.1f,%.1f Size:%.2f\n", asteroids[i].id+GALAXY_OBJECT_MIN_ASTEROID, get_resource_name(asteroids[i].resource_type), asteroids[i].x, asteroids[i].y, asteroids[i].z, asteroids[i].size);

        for(int i=0; i<MAX_DERELICTS; i++) if(derelicts[i].active && derelicts[i].q1 == q1 && derelicts[i].q2 == q2 && derelicts[i].q3 == q3)
            printf("[DERELICT] ID:%d Class:%s Coord:%.1f,%.1f,%.1f\n", derelicts[i].id+GALAXY_OBJECT_MIN_DERELICT, get_ship_class_name(derelicts[i].ship_class), derelicts[i].x, derelicts[i].y, derelicts[i].z);

        for(int i=0; i<MAX_MINES; i++) if(mines[i].active && mines[i].q1 == q1 && mines[i].q2 == q2 && mines[i].q3 == q3)
            printf("[MINE] ID:%d Faction:%s Coord:%.1f,%.1f,%.1f\n", mines[i].id+GALAXY_OBJECT_MIN_MINE, get_faction_name(mines[i].faction), mines[i].x, mines[i].y, mines[i].z);

        for(int i=0; i<MAX_BUOYS; i++) if(buoys[i].active && buoys[i].q1 == q1 && buoys[i].q2 == q2 && buoys[i].q3 == q3)
            printf("[BUOY] ID:%d Coord:%.1f,%.1f,%.1f\n", buoys[i].id+GALAXY_OBJECT_MIN_BUOY, buoys[i].x, buoys[i].y, buoys[i].z);

        for(int i=0; i<MAX_PLATFORMS; i++) if(platforms[i].active && platforms[i].q1 == q1 && platforms[i].q2 == q2 && platforms[i].q3 == q3)
            printf("[PLATFORM] ID:%d Faction:%s Coord:%.1f,%.1f,%.1f Health:%d Energy:%" PRIu64 "\n", platforms[i].id+GALAXY_OBJECT_MIN_PLATFORM, get_faction_name(platforms[i].faction), platforms[i].x, platforms[i].y, platforms[i].z, platforms[i].health, platforms[i].energy);

        for(int i=0; i<MAX_RIFTS; i++) if(rifts[i].active && rifts[i].q1 == q1 && rifts[i].q2 == q2 && rifts[i].q3 == q3)
            printf("[RIFT] ID:%d Coord:%.1f,%.1f,%.1f\n", rifts[i].id+GALAXY_OBJECT_MIN_RIFT, rifts[i].x, rifts[i].y, rifts[i].z);

        for(int i=0; i<3; i++) if(spacegl_master.probes[i].active && spacegl_master.probes[i].q1 == q1 && spacegl_master.probes[i].q2 == q2 && spacegl_master.probes[i].q3 == q3)
            printf("[PROBE] Index:%d Status:%d ETA:%.1f Coord:%.1f,%.1f,%.1f\n", i, spacegl_master.probes[i].status, spacegl_master.probes[i].eta, spacegl_master.probes[i].s1, spacegl_master.probes[i].s2, spacegl_master.probes[i].s3);
    }
    else if (strcmp(argv[1], "players") == 0) {
        printf("--- Persistent Players ---\n");
        printf("%-15s %-12s %-15s %-10s %-10s %s\n", "NAME", "FACTION", "SHIP CLASS", "CRYPTO", "POSITION", "STATUS");
        for(int i=0; i<MAX_CLIENTS; i++) {
            if (players[i].name[0] != '\0') {
                char pos[32];
                snprintf(pos, 32, "[%d,%d,%d]", players[i].state.q1, players[i].state.q2, players[i].state.q3);
                printf("%-15s %-12s %-15s %-10s %-10s %s\n", 
                       players[i].name, 
                       get_faction_name(players[i].faction),
                       get_ship_class_name(players[i].ship_class),
                       get_crypto_name(players[i].crypto_algo),
                       pos,
                       players[i].state.is_cloaked ? "[CLOAKED]" : "[VISIBLE]");
            }
        }
    }
    else if (strcmp(argv[1], "search") == 0 && argc == 3) {
        const char *name = argv[2];
        printf("Searching for '%s'..\n", name);
        for(int i=0; i<MAX_CLIENTS; i++) {
            if (players[i].name[0] != '\0' && strcasestr(players[i].name, name)) {
                printf("[PLAYER] Found: %s in Quadrant [%d,%d,%d] %s\n", players[i].name, players[i].state.q1, players[i].state.q2, players[i].state.q3,
                       players[i].state.is_cloaked ? "[CLOAKED]" : "");
            }
        }
        for(int i=0; i<MAX_NPC; i++) {
            if (npcs[i].active && npcs[i].name[0] != '\0' && strcasestr(npcs[i].name, name)) {
                printf("[NPC] Found: %s ID:%d in Quadrant [%d,%d,%d] %s\n", npcs[i].name, npcs[i].id+GALAXY_OBJECT_MIN_NPC, npcs[i].q1, npcs[i].q2, npcs[i].q3,
                       npcs[i].is_cloaked ? "[CLOAKED]" : "");
            }
        }
    }
    else {
        print_help();
    }

    return 0;
}
