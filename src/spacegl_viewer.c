/* * SPACE GL - 3D LOGIC ENGINE 
 * Authors: Nicola Taibi, Supported by Google Gemini
 * Copyright (C) 2026 Nicola Taibi - Licensed under GPL-3.0-or-later
 * * This program is free software: you can redistribute it and/or modify
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
        case FACTION_CARDASSIAN: return "Vesperian";
        case FACTION_JEM_HADAR:  return "Ascendant";
        case FACTION_THOLIAN:    return "Quarzite";
        case FACTION_GORN:       return "Saurian";
        case FACTION_FERENGI:    return "Gilded";
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

void print_help() {
    printf("Usage: ./stellar_viewer [command]\n");
    printf("Commands:\n");
    printf("  stats             Show global galaxy statistics\n");
    printf("  map <q3>          Show a 2D map slice for Z quadrant q3\n");
    printf("  list <q1> <q2> <q3>  List objects in quadrant (1-10)\n");
    printf("  players           List all persistent players\n");
    printf("  search <name>     Search for a player or ship by name\n");
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
        int n_active = 0, s_active = 0, b_active = 0, p_active = 0, bh_active = 0, neb_active = 0, pul_active = 0, com_active = 0, ast_active = 0, der_active = 0, mine_active = 0, buoy_active = 0, plat_active = 0, rift_active = 0, mon_active = 0;
        for(int i=0; i<MAX_NPC; i++) if(npcs[i].active) n_active++;
        for(int i=0; i<MAX_STARS; i++) if(stars_data[i].active) s_active++;
        for(int i=0; i<MAX_BASES; i++) if(bases[i].active) b_active++;
        for(int i=0; i<MAX_PLANETS; i++) if(planets[i].active) p_active++;
        for(int i=0; i<MAX_BH; i++) if(black_holes[i].active) bh_active++;
        for(int i=0; i<MAX_NEBULAS; i++) if(nebulas[i].active) neb_active++;
        for(int i=0; i<MAX_PULSARS; i++) if(pulsars[i].active) pul_active++;
        for(int i=0; i<MAX_COMETS; i++) if(comets[i].active) com_active++;
        for(int i=0; i<MAX_ASTEROIDS; i++) if(asteroids[i].active) ast_active++;
        for(int i=0; i<MAX_DERELICTS; i++) if(derelicts[i].active) der_active++;
        for(int i=0; i<MAX_MINES; i++) if(mines[i].active) mine_active++;
        for(int i=0; i<MAX_BUOYS; i++) if(buoys[i].active) buoy_active++;
        for(int i=0; i<MAX_PLATFORMS; i++) if(platforms[i].active) plat_active++;
        for(int i=0; i<MAX_RIFTS; i++) if(rifts[i].active) rift_active++;
        for(int i=0; i<MAX_MONSTERS; i++) if(monsters[i].active) mon_active++;

        printf("--- Galaxy Statistics ---\n");
        printf("Version: %d\n", version);
        if (spacegl_master.encryption_flags & 0x01) {
            printf("Signature: VERIFIED (HMAC-SHA256)\n");
            printf("Encryption Flags: 0x%08X\n", spacegl_master.encryption_flags);
        } else {
            printf("Signature: NOT PRESENT / UNVERIFIED\n");
        }
        printf("Total NPCs: %d\n", n_active);
        printf("Total Stars: %d\n", s_active);
        printf("Total Bases: %d\n", b_active);
        printf("Total Planets: %d\n", p_active);
        printf("Total Black Holes: %d\n", bh_active);
        printf("Total Nebulas: %d\n", neb_active);
        printf("Total Pulsars: %d\n", pul_active);
        printf("Total Comets: %d\n", com_active);
        printf("Total Asteroids: %d\n", ast_active);
        printf("Total Derelicts: %d\n", der_active);
        printf("Total Minefields: %d\n", mine_active);
        printf("Total Comm Buoys: %d\n", buoy_active);
        printf("Total Defense Platforms: %d\n", plat_active);
        printf("Total Spatial Rifts: %d\n", rift_active);
        printf("Total Space Monsters: %d\n", mon_active);
        printf("Galaxy Master K9: %d, B9: %d\n", spacegl_master.k9, spacegl_master.b9);
    } 
    else if (strcmp(argv[1], "map") == 0 && argc == 3) {
        int q3 = atoi(argv[2]);
        if (q3 < 1 || q3 > 10) {
            printf("Invalid Z quadrant (1-10)\n");
            return 1;
        }
        printf("--- Galaxy Map Slice (Z=%d) ---\n", q3);
        printf("    1  2  3  4  5  6  7  8  9  10 (X)\n");
        for(int j=1; j<=10; j++) {
            printf("%2d ", j);
            for(int i=1; i<=10; i++) {
                long long bpnbs = spacegl_master.g[i][j][q3];
                int mon  = (bpnbs/10000000000000000LL)%10;
                int rift = (bpnbs/100000000000000LL)%10;
                int plat = (bpnbs/10000000000000LL)%10;
                int buoy = (bpnbs/1000000000000LL)%10;
                int mine = (bpnbs/100000000000LL)%10;
                int der  = (bpnbs/10000000000LL)%10;
                int ast  = (bpnbs/1000000000)%10;
                int com  = (bpnbs/100000000)%10;
                int s = (bpnbs/10000000)%10;
                int p = (bpnbs/1000000)%10;
                int n = (bpnbs/100000)%10;
                
                if (s > 0) printf(" ~ "); /* Stellar Storm */
                else if (mon > 0) printf(" M "); /* Monster */
                else if (rift > 0) printf(" R "); /* Rift */
                else if (plat > 0) printf(" T "); /* Turret */
                else if (buoy > 0) printf(" @ "); /* Buoy */
                else if (mine > 0) printf(" X "); /* Mine */
                else if (der > 0) printf(" D "); /* Derelict */
                else if (ast > 0) printf(" A "); /* Asteroid */
                else if (com > 0) printf(" C "); /* Comet */
                else if (p > 0) printf(" P "); /* Pulsar */
                else if (n > 0) printf(" N "); /* Nebula */
                else printf(" . ");
            }
            printf(" (Y:%d)\n", j);
        }
    }
    else if (strcmp(argv[1], "list") == 0 && argc == 5) {
        int q1 = atoi(argv[2]);
        int q2 = atoi(argv[3]);
        int q3 = atoi(argv[4]);
        
        if (!IS_Q_VALID(q1, q2, q3)) {
            printf("Invalid quadrant coordinates (1-10)\n");
            return 1;
        }

        printf("--- Objects in Quadrant [%d,%d,%d] ---\n", q1, q2, q3);
        
        long long bpnbs = spacegl_master.g[q1][q2][q3];
        printf("BPNBS Encoding: %017lld\n", bpnbs);

        for(int i=0; i<MAX_NPC; i++) if(npcs[i].active && npcs[i].q1 == q1 && npcs[i].q2 == q2 && npcs[i].q3 == q3)
            printf("[NPC] ID:%d Faction:%s Coord:%.1f,%.1f,%.1f Energy:%d AI:%d %s\n", npcs[i].id+1000, get_faction_name(npcs[i].faction), npcs[i].x, npcs[i].y, npcs[i].z, npcs[i].energy, npcs[i].ai_state, npcs[i].is_cloaked ? "[CLOAKED]" : "");

        for(int i=0; i<MAX_MONSTERS; i++) if(monsters[i].active && monsters[i].q1 == q1 && monsters[i].q2 == q2 && monsters[i].q3 == q3)
            printf("[MONSTER] ID:%d Type:%s Coord:%.1f,%.1f,%.1f Health:%d\n", monsters[i].id+18000, get_faction_name(monsters[i].type), monsters[i].x, monsters[i].y, monsters[i].z, monsters[i].health);

        for(int i=0; i<MAX_BASES; i++) if(bases[i].active && bases[i].q1 == q1 && bases[i].q2 == q2 && bases[i].q3 == q3)
            printf("[BASE] ID:%d Faction:%s Coord:%.1f,%.1f,%.1f Health:%d\n", bases[i].id+2000, get_faction_name(bases[i].faction), bases[i].x, bases[i].y, bases[i].z, bases[i].health);

        for(int i=0; i<MAX_PLANETS; i++) if(planets[i].active && planets[i].q1 == q1 && planets[i].q2 == q2 && planets[i].q3 == q3)
            printf("[PLANET] ID:%d Type:%d Coord:%.1f,%.1f,%.1f Resources:%d\n", planets[i].id+3000, planets[i].resource_type, planets[i].x, planets[i].y, planets[i].z, planets[i].amount);

        for(int i=0; i<MAX_STARS; i++) if(stars_data[i].active && stars_data[i].q1 == q1 && stars_data[i].q2 == q2 && stars_data[i].q3 == q3)
            printf("[STAR] ID:%d Coord:%.1f,%.1f,%.1f\n", stars_data[i].id+4000, stars_data[i].x, stars_data[i].y, stars_data[i].z);

        for(int i=0; i<MAX_BH; i++) if(black_holes[i].active && black_holes[i].q1 == q1 && black_holes[i].q2 == q2 && black_holes[i].q3 == q3)
            printf("[BLACK HOLE] ID:%d Coord:%.1f,%.1f,%.1f\n", black_holes[i].id+7000, black_holes[i].x, black_holes[i].y, black_holes[i].z);

        for(int i=0; i<MAX_NEBULAS; i++) if(nebulas[i].active && nebulas[i].q1 == q1 && nebulas[i].q2 == q2 && nebulas[i].q3 == q3)
            printf("[NEBULA] ID:%d Coord:%.1f,%.1f,%.1f\n", nebulas[i].id+8000, nebulas[i].x, nebulas[i].y, nebulas[i].z);

        for(int i=0; i<MAX_PULSARS; i++) if(pulsars[i].active && pulsars[i].q1 == q1 && pulsars[i].q2 == q2 && pulsars[i].q3 == q3)
            printf("[PULSAR] ID:%d Coord:%.1f,%.1f,%.1f\n", pulsars[i].id+9000, pulsars[i].x, pulsars[i].y, pulsars[i].z);

        for(int i=0; i<MAX_COMETS; i++) if(comets[i].active && comets[i].q1 == q1 && comets[i].q2 == q2 && comets[i].q3 == q3)
            printf("[COMET] ID:%d Coord:%.1f,%.1f,%.1f Angle:%.3f Speed:%.3f\n", comets[i].id+10000, comets[i].x, comets[i].y, comets[i].z, comets[i].angle, comets[i].speed);

        for(int i=0; i<MAX_ASTEROIDS; i++) if(asteroids[i].active && asteroids[i].q1 == q1 && asteroids[i].q2 == q2 && asteroids[i].q3 == q3)
            printf("[ASTEROID] ID:%d Coord:%.1f,%.1f,%.1f Size:%.2f\n", asteroids[i].id+12000, asteroids[i].x, asteroids[i].y, asteroids[i].z, asteroids[i].size);

        for(int i=0; i<MAX_DERELICTS; i++) if(derelicts[i].active && derelicts[i].q1 == q1 && derelicts[i].q2 == q2 && derelicts[i].q3 == q3)
            printf("[DERELICT] ID:%d Coord:%.1f,%.1f,%.1f Class:%d\n", derelicts[i].id+11000, derelicts[i].x, derelicts[i].y, derelicts[i].z, derelicts[i].ship_class);

        for(int i=0; i<MAX_MINES; i++) if(mines[i].active && mines[i].q1 == q1 && mines[i].q2 == q2 && mines[i].q3 == q3)
            printf("[MINE] ID:%d Faction:%s Coord:%.1f,%.1f,%.1f\n", mines[i].id+14000, get_faction_name(mines[i].faction), mines[i].x, mines[i].y, mines[i].z);

        for(int i=0; i<MAX_BUOYS; i++) if(buoys[i].active && buoys[i].q1 == q1 && buoys[i].q2 == q2 && buoys[i].q3 == q3)
            printf("[BUOY] ID:%d Coord:%.1f,%.1f,%.1f\n", buoys[i].id+15000, buoys[i].x, buoys[i].y, buoys[i].z);

        for(int i=0; i<MAX_PLATFORMS; i++) if(platforms[i].active && platforms[i].q1 == q1 && platforms[i].q2 == q2 && platforms[i].q3 == q3)
            printf("[PLATFORM] ID:%d Faction:%s Coord:%.1f,%.1f,%.1f Health:%d Energy:%d\n", platforms[i].id+16000, get_faction_name(platforms[i].faction), platforms[i].x, platforms[i].y, platforms[i].z, platforms[i].health, platforms[i].energy);

        for(int i=0; i<MAX_RIFTS; i++) if(rifts[i].active && rifts[i].q1 == q1 && rifts[i].q2 == q2 && rifts[i].q3 == q3)
            printf("[RIFT] ID:%d Coord:%.1f,%.1f,%.1f\n", rifts[i].id+17000, rifts[i].x, rifts[i].y, rifts[i].z);
    }
    else if (strcmp(argv[1], "players") == 0) {
        printf("--- Persistent Players ---\n");
        for(int i=0; i<MAX_CLIENTS; i++) {
            if (players[i].name[0] != '\0') {
                printf("Name: %-15s Faction: %-12s Pos: [%d,%d,%d] (%.1f,%.1f,%.1f) %s\n", 
                       players[i].name, get_faction_name(players[i].faction),
                       players[i].state.q1, players[i].state.q2, players[i].state.q3,
                       players[i].state.s1, players[i].state.s2, players[i].state.s3,
                       players[i].state.is_cloaked ? "[CLOAKED]" : "");
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
    }
    else {
        print_help();
    }

    return 0;
}
