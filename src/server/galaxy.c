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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "server_internal.h"
#include "ui.h"

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
NPCShip npcs[MAX_NPC];
ConnectedPlayer players[MAX_CLIENTS];
SpaceGLGame spacegl_master;
SupernovaState supernova_event = {0,0,0,0};

uint8_t SERVER_PUBKEY[32];
uint8_t SERVER_PRIVKEY[64];

QuadrantIndex (*spatial_index)[11][11] = NULL;

void init_static_spatial_index() {
    if (!spatial_index) {
        spatial_index = calloc(11 * 11 * 11, sizeof(QuadrantIndex));
        if (!spatial_index) { perror("Failed to allocate spatial_index"); exit(1); }
    }
    memset(spatial_index, 0, 11 * 11 * 11 * sizeof(QuadrantIndex));

    for(int p=0; p<MAX_PLANETS; p++) if(planets[p].active) {
        if (!IS_Q_VALID(planets[p].q1, planets[p].q2, planets[p].q3)) continue;
        QuadrantIndex *q = &spatial_index[planets[p].q1][planets[p].q2][planets[p].q3];
        if (q->planet_count < MAX_Q_PLANETS) { 
            q->planets[q->planet_count++] = &planets[p]; 
            q->static_planet_count = q->planet_count;
        }
    }
    for(int b=0; b<MAX_BASES; b++) if(bases[b].active) {
        if (!IS_Q_VALID(bases[b].q1, bases[b].q2, bases[b].q3)) continue;
        QuadrantIndex *q = &spatial_index[bases[b].q1][bases[b].q2][bases[b].q3];
        if (q->base_count < MAX_Q_BASES) { 
            q->bases[q->base_count++] = &bases[b]; 
            q->static_base_count = q->base_count;
        }
    }
    for(int s=0; s<MAX_STARS; s++) if(stars_data[s].active) {
        if (!IS_Q_VALID(stars_data[s].q1, stars_data[s].q2, stars_data[s].q3)) continue;
        QuadrantIndex *q = &spatial_index[stars_data[s].q1][stars_data[s].q2][stars_data[s].q3];
        if (q->star_count < MAX_Q_STARS) { 
            q->stars[q->star_count++] = &stars_data[s]; 
            q->static_star_count = q->star_count;
        }
    }
    for(int h=0; h<MAX_BH; h++) if(black_holes[h].active) {
        if (!IS_Q_VALID(black_holes[h].q1, black_holes[h].q2, black_holes[h].q3)) continue;
        QuadrantIndex *q = &spatial_index[black_holes[h].q1][black_holes[h].q2][black_holes[h].q3];
        if (q->bh_count < MAX_Q_BH) { 
            q->black_holes[q->bh_count++] = &black_holes[h]; 
            q->static_bh_count = q->bh_count;
        }
    }
    for(int n=0; n<MAX_NEBULAS; n++) if(nebulas[n].active) {
        if (!IS_Q_VALID(nebulas[n].q1, nebulas[n].q2, nebulas[n].q3)) continue;
        QuadrantIndex *q = &spatial_index[nebulas[n].q1][nebulas[n].q2][nebulas[n].q3];
        if (q->nebula_count < MAX_Q_NEBULAS) { 
            q->nebulas[q->nebula_count++] = &nebulas[n]; 
            q->static_nebula_count = q->nebula_count;
        }
    }
    for(int p=0; p<MAX_PULSARS; p++) if(pulsars[p].active) {
        if (!IS_Q_VALID(pulsars[p].q1, pulsars[p].q2, pulsars[p].q3)) continue;
        QuadrantIndex *q = &spatial_index[pulsars[p].q1][pulsars[p].q2][pulsars[p].q3];
        if (q->pulsar_count < MAX_Q_PULSARS) { 
            q->pulsars[q->pulsar_count++] = &pulsars[p]; 
            q->static_pulsar_count = q->pulsar_count;
        }
    }
    for(int a=0; a<MAX_ASTEROIDS; a++) if(asteroids[a].active) {
        if (!IS_Q_VALID(asteroids[a].q1, asteroids[a].q2, asteroids[a].q3)) continue;
        QuadrantIndex *q = &spatial_index[asteroids[a].q1][asteroids[a].q2][asteroids[a].q3];
        if (q->asteroid_count < MAX_Q_ASTEROIDS) { 
            q->asteroids[q->asteroid_count++] = &asteroids[a]; 
        }
    }
    for(int d=0; d<MAX_DERELICTS; d++) if(derelicts[d].active) {
        if (!IS_Q_VALID(derelicts[d].q1, derelicts[d].q2, derelicts[d].q3)) continue;
        QuadrantIndex *q = &spatial_index[derelicts[d].q1][derelicts[d].q2][derelicts[d].q3];
        if (q->derelict_count < MAX_Q_DERELICTS) { 
            q->derelicts[q->derelict_count++] = &derelicts[d]; 
        }
    }
    for(int m=0; m<MAX_MINES; m++) if(mines[m].active) {
        if (!IS_Q_VALID(mines[m].q1, mines[m].q2, mines[m].q3)) continue;
        QuadrantIndex *q = &spatial_index[mines[m].q1][mines[m].q2][mines[m].q3];
        if (q->mine_count < MAX_Q_MINES) { 
            q->mines[q->mine_count++] = &mines[m]; 
        }
    }
    for(int b=0; b<MAX_BUOYS; b++) if(buoys[b].active) {
        if (!IS_Q_VALID(buoys[b].q1, buoys[b].q2, buoys[b].q3)) continue;
        QuadrantIndex *q = &spatial_index[buoys[b].q1][buoys[b].q2][buoys[b].q3];
        if (q->buoy_count < MAX_Q_BUOYS) { 
            q->buoys[q->buoy_count++] = &buoys[b]; 
        }
    }
    for(int pl=0; pl<MAX_PLATFORMS; pl++) if(platforms[pl].active) {
        if (!IS_Q_VALID(platforms[pl].q1, platforms[pl].q2, platforms[pl].q3)) continue;
        QuadrantIndex *q = &spatial_index[platforms[pl].q1][platforms[pl].q2][platforms[pl].q3];
        if (q->platform_count < MAX_Q_PLATFORMS) { 
            q->platforms[q->platform_count++] = &platforms[pl]; 
        }
    }
    for(int r=0; r<MAX_RIFTS; r++) if(rifts[r].active) {
        if (!IS_Q_VALID(rifts[r].q1, rifts[r].q2, rifts[r].q3)) continue;
        QuadrantIndex *q = &spatial_index[rifts[r].q1][rifts[r].q2][rifts[r].q3];
        if (q->rift_count < MAX_Q_RIFTS) { 
            q->rifts[q->rift_count++] = &rifts[r]; 
        }
    }
    for(int m=0; m<MAX_MONSTERS; m++) if(monsters[m].active) {
        if (!IS_Q_VALID(monsters[m].q1, monsters[m].q2, monsters[m].q3)) continue;
        QuadrantIndex *q = &spatial_index[monsters[m].q1][monsters[m].q2][monsters[m].q3];
        if (q->monster_count < MAX_Q_MONSTERS) { 
            q->monsters[q->monster_count++] = &monsters[m]; 
        }
    }
}

void rebuild_spatial_index() {
    if (!spatial_index) {
        spatial_index = calloc(11 * 11 * 11, sizeof(QuadrantIndex));
        if (!spatial_index) { perror("Failed to allocate spatial_index"); exit(1); }
    }
    
    /* Step 1: Wipe the entire index to start fresh */
    memset(spatial_index, 0, 11 * 11 * 11 * sizeof(QuadrantIndex));

    /* Step 2: Populate Static Objects (Stars, Planets, Bases, etc.) */
    for(int p=0; p<MAX_PLANETS; p++) if(planets[p].active) {
        if (IS_Q_VALID(planets[p].q1, planets[p].q2, planets[p].q3)) {
            QuadrantIndex *q = &spatial_index[planets[p].q1][planets[p].q2][planets[p].q3];
            if (q->planet_count < MAX_Q_PLANETS) q->planets[q->planet_count++] = &planets[p];
        }
    }
    for(int b=0; b<MAX_BASES; b++) if(bases[b].active) {
        if (IS_Q_VALID(bases[b].q1, bases[b].q2, bases[b].q3)) {
            QuadrantIndex *q = &spatial_index[bases[b].q1][bases[b].q2][bases[b].q3];
            if (q->base_count < MAX_Q_BASES) q->bases[q->base_count++] = &bases[b];
        }
    }
    for(int s=0; s<MAX_STARS; s++) if(stars_data[s].active) {
        if (IS_Q_VALID(stars_data[s].q1, stars_data[s].q2, stars_data[s].q3)) {
            QuadrantIndex *q = &spatial_index[stars_data[s].q1][stars_data[s].q2][stars_data[s].q3];
            if (q->star_count < MAX_Q_STARS) q->stars[q->star_count++] = &stars_data[s];
        }
    }
    for(int h=0; h<MAX_BH; h++) if(black_holes[h].active) {
        if (IS_Q_VALID(black_holes[h].q1, black_holes[h].q2, black_holes[h].q3)) {
            QuadrantIndex *q = &spatial_index[black_holes[h].q1][black_holes[h].q2][black_holes[h].q3];
            if (q->bh_count < MAX_Q_BH) q->black_holes[q->bh_count++] = &black_holes[h];
        }
    }
    for(int n=0; n<MAX_NEBULAS; n++) if(nebulas[n].active) {
        if (IS_Q_VALID(nebulas[n].q1, nebulas[n].q2, nebulas[n].q3)) {
            QuadrantIndex *q = &spatial_index[nebulas[n].q1][nebulas[n].q2][nebulas[n].q3];
            if (q->nebula_count < MAX_Q_NEBULAS) q->nebulas[q->nebula_count++] = &nebulas[n];
        }
    }
    for(int p=0; p<MAX_PULSARS; p++) if(pulsars[p].active) {
        if (IS_Q_VALID(pulsars[p].q1, pulsars[p].q2, pulsars[p].q3)) {
            QuadrantIndex *q = &spatial_index[pulsars[p].q1][pulsars[p].q2][pulsars[p].q3];
            if (q->pulsar_count < MAX_Q_PULSARS) q->pulsars[q->pulsar_count++] = &pulsars[p];
        }
    }

    /* Step 3: Populate Dynamic Objects (NPCs, Players, Comets, etc.) */
    for(int n=0; n<MAX_NPC; n++) if(npcs[n].active) {
        if (IS_Q_VALID(npcs[n].q1, npcs[n].q2, npcs[n].q3)) {
            QuadrantIndex *q = &spatial_index[npcs[n].q1][npcs[n].q2][npcs[n].q3];
            if (q->npc_count < MAX_Q_NPC) q->npcs[q->npc_count++] = &npcs[n];
        }
    }
    for(int c=0; c<MAX_COMETS; c++) if(comets[c].active) {
        if (IS_Q_VALID(comets[c].q1, comets[c].q2, comets[c].q3)) {
            QuadrantIndex *q = &spatial_index[comets[c].q1][comets[c].q2][comets[c].q3];
            if (q->comet_count < MAX_Q_COMETS) q->comets[q->comet_count++] = &comets[c];
        }
    }
    for(int a=0; a<MAX_ASTEROIDS; a++) if(asteroids[a].active) {
        if (IS_Q_VALID(asteroids[a].q1, asteroids[a].q2, asteroids[a].q3)) {
            QuadrantIndex *q = &spatial_index[asteroids[a].q1][asteroids[a].q2][asteroids[a].q3];
            if (q->asteroid_count < MAX_Q_ASTEROIDS) q->asteroids[q->asteroid_count++] = &asteroids[a];
        }
    }
    for(int d=0; d<MAX_DERELICTS; d++) if(derelicts[d].active) {
        if (IS_Q_VALID(derelicts[d].q1, derelicts[d].q2, derelicts[d].q3)) {
            QuadrantIndex *q = &spatial_index[derelicts[d].q1][derelicts[d].q2][derelicts[d].q3];
            if (q->derelict_count < MAX_Q_DERELICTS) q->derelicts[q->derelict_count++] = &derelicts[d];
        }
    }
    for(int m=0; m<MAX_MINES; m++) if(mines[m].active) {
        if (IS_Q_VALID(mines[m].q1, mines[m].q2, mines[m].q3)) {
            QuadrantIndex *q = &spatial_index[mines[m].q1][mines[m].q2][mines[m].q3];
            if (q->mine_count < MAX_Q_MINES) q->mines[q->mine_count++] = &mines[m];
        }
    }
    for(int b=0; b<MAX_BUOYS; b++) if(buoys[b].active) {
        if (IS_Q_VALID(buoys[b].q1, buoys[b].q2, buoys[b].q3)) {
            QuadrantIndex *q = &spatial_index[buoys[b].q1][buoys[b].q2][buoys[b].q3];
            if (q->buoy_count < MAX_Q_BUOYS) q->buoys[q->buoy_count++] = &buoys[b];
        }
    }
    for(int pl=0; pl<MAX_PLATFORMS; pl++) if(platforms[pl].active) {
        if (IS_Q_VALID(platforms[pl].q1, platforms[pl].q2, platforms[pl].q3)) {
            QuadrantIndex *q = &spatial_index[platforms[pl].q1][platforms[pl].q2][platforms[pl].q3];
            if (q->platform_count < MAX_Q_PLATFORMS) q->platforms[q->platform_count++] = &platforms[pl];
        }
    }
    for(int r=0; r<MAX_RIFTS; r++) if(rifts[r].active) {
        if (IS_Q_VALID(rifts[r].q1, rifts[r].q2, rifts[r].q3)) {
            QuadrantIndex *q = &spatial_index[rifts[r].q1][rifts[r].q2][rifts[r].q3];
            if (q->rift_count < MAX_Q_RIFTS) q->rifts[q->rift_count++] = &rifts[r];
        }
    }
    for(int m=0; m<MAX_MONSTERS; m++) if(monsters[m].active) {
        if (IS_Q_VALID(monsters[m].q1, monsters[m].q2, monsters[m].q3)) {
            QuadrantIndex *q = &spatial_index[monsters[m].q1][monsters[m].q2][monsters[m].q3];
            if (q->monster_count < MAX_Q_MONSTERS) q->monsters[q->monster_count++] = &monsters[m];
        }
    }
    for(int u=0; u<MAX_CLIENTS; u++) if(players[u].active && players[u].name[0] != '\0') {
        if (IS_Q_VALID(players[u].state.q1, players[u].state.q2, players[u].state.q3)) {
            QuadrantIndex *q = &spatial_index[players[u].state.q1][players[u].state.q2][players[u].state.q3];
            if (q->player_count < MAX_Q_PLAYERS) q->players[q->player_count++] = &players[u];
        }
    }

    /* Step 4: Refresh BPNBS Grid for LRS Display */
    for(int i=1; i<=10; i++)
        for(int j=1; j<=10; j++)
            for(int l=1; l<=10; l++) {
                QuadrantIndex *q = &spatial_index[i][j][l];
                int c_mon = (q->monster_count > 9) ? 9 : q->monster_count;
                int c_rift = (q->rift_count > 9) ? 9 : q->rift_count;
                int c_plat = (q->platform_count > 9) ? 9 : q->platform_count;
                int c_buoy = (q->buoy_count > 9) ? 9 : q->buoy_count;
                int c_mine = (q->mine_count > 9) ? 9 : q->mine_count;
                int c_der = (q->derelict_count > 9) ? 9 : q->derelict_count;
                int c_ast = (q->asteroid_count > 9) ? 9 : q->asteroid_count;
                int c_com = (q->comet_count > 9) ? 9 : q->comet_count;
                int c_pul = (q->pulsar_count > 9) ? 9 : q->pulsar_count;
                int c_neb = (q->nebula_count > 9) ? 9 : q->nebula_count;
                int c_bh = (q->bh_count > 9) ? 9 : q->bh_count;
                int c_p = (q->planet_count > 9) ? 9 : q->planet_count;
                int c_npc = (q->npc_count > 9) ? 9 : q->npc_count;
                int c_u = (q->player_count > 9) ? 9 : q->player_count;
                int c_b = (q->base_count > 9) ? 9 : q->base_count;
                int c_s = (q->star_count > 9) ? 9 : q->star_count;
                
                /* Preserve Ion Storm flag (10^7 digit) */
                long long storm_flag = (spacegl_master.g[i][j][l] / 10000000LL) % 10;

                spacegl_master.g[i][j][l] = (long long)c_mon * 10000000000000000LL 
                                          + (long long)c_u   * 1000000000000000LL
                                          + (long long)c_rift * 100000000000000LL 
                                          + (long long)c_plat * 10000000000000LL 
                                          + (long long)c_buoy * 1000000000000LL 
                                          + (long long)c_mine * 100000000000LL 
                                          + (long long)c_der * 10000000000LL 
                                          + (long long)c_ast * 1000000000LL 
                                          + (long long)c_com * 100000000LL 
                                          + (long long)storm_flag * 10000000LL
                                          + c_pul * 1000000 + c_neb * 100000 + c_bh * 10000 + c_p * 1000 + c_npc * 100 + c_b * 10 + c_s;
            }
}

void save_galaxy() {
    FILE *f = fopen("galaxy.dat", "wb");
    if (!f) { perror("Failed to open galaxy.dat for writing"); return; }
    int version = GALAXY_VERSION;
    fwrite(&version, sizeof(int), 1, f);
    fwrite(&spacegl_master, sizeof(SpaceGLGame), 1, f);
    fwrite(npcs, sizeof(NPCShip), MAX_NPC, f);
    fwrite(stars_data, sizeof(NPCStar), MAX_STARS, f);
    fwrite(black_holes, sizeof(NPCBlackHole), MAX_BH, f);
    fwrite(planets, sizeof(NPCPlanet), MAX_PLANETS, f);
    fwrite(bases, sizeof(NPCBase), MAX_BASES, f);
    fwrite(nebulas, sizeof(NPCNebula), MAX_NEBULAS, f);
    fwrite(pulsars, sizeof(NPCPulsar), MAX_PULSARS, f);
    fwrite(comets, sizeof(NPCComet), MAX_COMETS, f);
    fwrite(asteroids, sizeof(NPCAsteroid), MAX_ASTEROIDS, f);
    fwrite(derelicts, sizeof(NPCDerelict), MAX_DERELICTS, f);
    fwrite(mines, sizeof(NPCMine), MAX_MINES, f);
    fwrite(buoys, sizeof(NPCBuoy), MAX_BUOYS, f);
    fwrite(platforms, sizeof(NPCPlatform), MAX_PLATFORMS, f);
    fwrite(rifts, sizeof(NPCRift), MAX_RIFTS, f);
    fwrite(monsters, sizeof(NPCMonster), MAX_MONSTERS, f);
    fwrite(players, sizeof(ConnectedPlayer), MAX_CLIENTS, f);
    fclose(f);
    time_t now = time(NULL);
    char *ts = ctime(&now);
    ts[strlen(ts)-1] = '\0'; /* Remove newline */
    printf("--- [%s] GALAXY SAVED TO galaxy.dat SUCCESSFULLY ---\n", ts);
}

int load_galaxy() {
    FILE *f = fopen("galaxy.dat", "rb");
    if (!f) return 0;
    int version;
    if (fread(&version, sizeof(int), 1, f) != 1 || version != GALAXY_VERSION) {
        printf("--- GALAXY VERSION MISMATCH OR CORRUPT FILE ---\n");
        fclose(f);
        return 0;
    }

#define CHECK_READ(ptr, sz, count, stream) \
    if (fread(ptr, sz, count, stream) != (size_t)(count)) { perror("fread data"); fclose(stream); return 0; }

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
    
    for(int i=0; i<MAX_CLIENTS; i++) {
        players[i].active = 0;
        players[i].socket = 0;
    }
    
    printf("--- PERSISTENT GALAXY LOADED SUCCESSFULLY ---\n");
    rebuild_spatial_index();
    return 1;
}

const char* get_species_name(int s) {
    switch(s) {
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
        case 4: return "Star"; case 5: return "Planet"; case 6: return "Black Hole";
        case 7: return "Tactical Cruiser"; case 8: return "Pulsar"; case 9: return "Comet";
        case 21: return "Asteroid";
        case 23: return "Mine";
        case 24: return "Comm Buoy";
        case 25: return "Defense Platform";
        case 26: return "Spatial Rift";
        case 30: return "Crystalline Entity";
        case 31: return "Space Amoeba";
        default: return "Unknown";
    }
}

void generate_galaxy() {
    printf("Generating Master Galaxy...\n");
    memset(&spacegl_master, 0, sizeof(SpaceGLGame));
    memset(npcs, 0, sizeof(npcs));
    memset(players, 0, sizeof(players));
    memset(stars_data, 0, sizeof(stars_data));
    memset(planets, 0, sizeof(planets));
    memset(bases, 0, sizeof(bases));
    memset(black_holes, 0, sizeof(black_holes));
    memset(nebulas, 0, sizeof(nebulas));
    memset(pulsars, 0, sizeof(pulsars));
    memset(comets, 0, sizeof(comets));
    memset(asteroids, 0, sizeof(asteroids));
    memset(derelicts, 0, sizeof(derelicts));
    memset(mines, 0, sizeof(mines));
    memset(buoys, 0, sizeof(buoys));
    memset(platforms, 0, sizeof(platforms));
    memset(rifts, 0, sizeof(rifts));
    memset(monsters, 0, sizeof(monsters));

    int n_count = 0, b_count = 0, p_count = 0, s_count = 0, bh_count = 0, neb_count = 0, pul_count = 0, com_count = 0, ast_count = 0, der_count = 0, mine_count = 0, buoy_count = 0, plat_count = 0, rift_count = 0, mon_count = 0;
    
    for(int i=1; i<=10; i++)
        for(int j=1; j<=10; j++)
            for(int l=1; l<=10; l++) {
                int r = rand()%100;
                int kling = (r > 96) ? 3 : (r > 92) ? 2 : (r > 85) ? 1 : 0;
                int base = (rand()%100 > 98) ? 1 : 0;
                int planets_cnt = (rand()%100 > 90) ? (rand()%2 + 1) : 0;
                int star = (rand()%100 < 40) ? (rand()%3 + 1) : 0;
                int bh = (rand()%100 < 10) ? 1 : 0;
                int neb = (rand()%100 < 15) ? 1 : 0;
                int pul = (rand()%100 < 5) ? 1 : 0;
                int com = (rand()%100 < 10) ? 1 : 0;
                int ast_field = (rand()%100 < 20) ? (rand()%10 + 5) : 0;
                int der = (rand()%100 < 5) ? 1 : 0;
                int mine_field = (kling > 0 && rand()%100 < 30) ? (rand()%5 + 3) : 0;
                int buoy = (rand()%100 < 8) ? 1 : 0;
                int plat = (kling > 0 && rand()%100 < 40) ? (rand()%2 + 1) : 0;
                int rift = (rand()%100 < 5) ? 1 : 0;
                int mon = (rand()%100 < 2) ? 1 : 0;
                
                int actual_k = 0, actual_b = 0, actual_p = 0, actual_s = 0, actual_bh = 0, actual_neb = 0, actual_pul = 0, actual_com = 0, actual_ast = 0, actual_der = 0, actual_mine = 0, actual_buoy = 0, actual_plat = 0, actual_rift = 0, actual_mon = 0;
                
                for(int e=0; e<kling && n_count < MAX_NPC; e++) {
                    int faction = 10+(rand()%11);
                    int energy = 10000;
                    if (faction == FACTION_SWARM) energy = 80000 + (rand()%20001);
                    else if (faction == FACTION_SPECIES_8472 || faction == FACTION_HIROGEN) energy = 60000 + (rand()%20001);
                    else if (faction == FACTION_KORTHIAN || faction == FACTION_XYLARI || faction == FACTION_JEM_HADAR) energy = 30000 + (rand()%20001);
                    
                    NPCShip *n = &npcs[n_count];
                    n->id = n_count; n->faction = faction; n->active = 1;
                    n->q1 = i; n->q2 = j; n->q3 = l;
                    n->x = (rand()%100)/10.0; n->y = (rand()%100)/10.0; n->z = (rand()%100)/10.0;
                    n->gx = (i-1)*10.0 + n->x; n->gy = (j-1)*10.0 + n->y; n->gz = (l-1)*10.0 + n->z;
                    n->energy = energy; n->engine_health = 100.0f;
                    n->health = 1000;
                    if (faction == FACTION_SWARM) n->plating = 100000;
                    else if (faction == FACTION_HIROGEN || faction == FACTION_SPECIES_8472) n->plating = 50000;
                    else n->plating = 15000;
                    n->nav_timer = 60 + rand()%241; n->ai_state = AI_STATE_PATROL;
                    n_count++; actual_k++;
                }
                for(int b=0; b<base && b_count < MAX_BASES; b++) {
                    bases[b_count] = (NPCBase){.id=b_count, .faction=FACTION_ALLIANCE, .q1=i, .q2=j, .q3=l, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .health=5000, .active=1}; b_count++; actual_b++;
                }
                for(int p=0; p<planets_cnt && p_count < MAX_PLANETS; p++) {
                    planets[p_count] = (NPCPlanet){.id=p_count, .q1=i, .q2=j, .q3=l, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .resource_type=(rand()%8)+1, .amount=1000, .active=1}; p_count++; actual_p++;
                }
                for(int s=0; s<star && s_count < MAX_STARS; s++) {
                    stars_data[s_count] = (NPCStar){.id=s_count, .faction=4, .q1=i, .q2=j, .q3=l, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .active=1}; s_count++; actual_s++;
                }
                for(int h=0; h<bh && bh_count < MAX_BH; h++) {
                    black_holes[bh_count] = (NPCBlackHole){.id=bh_count, .q1=i, .q2=j, .q3=l, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .active=1}; bh_count++; actual_bh++;
                }
                for(int n=0; n<neb && neb_count < MAX_NEBULAS; n++) {
                    int n_type = rand() % 6; /* 0=Standard, 1=High-Energy, 2=Dark Matter, 3=Ionic, 4=Gravimetric, 5=Temporal */
                    nebulas[neb_count] = (NPCNebula){.id=neb_count, .q1=i, .q2=j, .q3=l, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .type=n_type, .active=1}; neb_count++; actual_neb++;
                }
                for(int p=0; p<pul && pul_count < MAX_PULSARS; p++) {
                    pulsars[pul_count] = (NPCPulsar){.id=pul_count, .q1=i, .q2=j, .q3=l, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .active=1}; pul_count++; actual_pul++;
                }
                for(int c=0; c<com && com_count < MAX_COMETS; c++) {
                    double a = 10.0 + (rand()%300)/10.0; /* Semi-major axis 10-40 */
                    double b = a * (0.5 + (rand()%40)/100.0); /* Elliptical (eccentricity) */
                    double inc = (rand()%360) * M_PI/180.0;
                    double angle = (rand()%360) * M_PI/180.0;
                    double speed = 0.02 / a; /* Linear speed ~0.02 */
                    
                    comets[com_count] = (NPCComet){
                        .id=com_count, .q1=i, .q2=j, .q3=l, 
                        .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, 
                        .a=a, .b=b, .angle=angle, .speed=speed, .inc=inc,
                        .cx=50.0 + (rand()%100-50)/10.0, .cy=50.0 + (rand()%100-50)/10.0, .cz=50.0 + (rand()%100-50)/10.0,
                        .active=1
                    }; 
                    com_count++; actual_com++;
                }
                for(int a=0; a<ast_field && ast_count < MAX_ASTEROIDS; a++) {
                    asteroids[ast_count] = (NPCAsteroid){
                        .id=ast_count, .q1=i, .q2=j, .q3=l, 
                        .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, 
                        .size=0.1f+(rand()%20)/100.0f, 
                        .resource_type=(rand()%8)+1, /* Random type 1-8 */
                        .amount=100 + rand()%401,
                        .active=1
                    }; 
                    ast_count++; actual_ast++;
                }
                for(int d=0; d<der && der_count < MAX_DERELICTS; d++) {
                    int dfac = FACTION_ALLIANCE;
                    if (rand()%100 < 20) dfac = 10 + (rand()%11); /* 20% chance of alien wreck */
                    derelicts[der_count] = (NPCDerelict){.id=der_count, .q1=i, .q2=j, .q3=l, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .ship_class=rand()%13, .active=1, .faction=dfac}; der_count++; actual_der++;
                }
                for(int m=0; m<mine_field && mine_count < MAX_MINES; m++) {
                    mines[mine_count] = (NPCMine){.id=mine_count, .q1=i, .q2=j, .q3=l, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .faction=FACTION_KORTHIAN, .active=1}; mine_count++; actual_mine++;
                }
                for(int bu=0; bu<buoy && buoy_count < MAX_BUOYS; bu++) {
                    buoys[buoy_count] = (NPCBuoy){.id=buoy_count, .q1=i, .q2=j, .q3=l, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .active=1}; buoy_count++; actual_buoy++;
                }
                for(int pt=0; pt<plat && plat_count < MAX_PLATFORMS; pt++) {
                    platforms[plat_count] = (NPCPlatform){.id=plat_count, .faction=FACTION_KORTHIAN, .q1=i, .q2=j, .q3=l, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .health=5000, .energy=10000, .fire_cooldown=0, .active=1}; plat_count++; actual_plat++;
                }
                for(int rf=0; rf<rift && rift_count < MAX_RIFTS; rf++) {
                    rifts[rift_count] = (NPCRift){.id=rift_count, .q1=i, .q2=j, .q3=l, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .active=1}; rift_count++; actual_rift++;
                }
                for(int mo=0; mo<mon && mon_count < MAX_MONSTERS; mo++) {
                    int type = (rand()%100 < 50) ? 30 : 31; /* 30=Crystalline, 31=Amoeba */
                    monsters[mon_count] = (NPCMonster){.id=mon_count, .type=type, .q1=i, .q2=j, .q3=l, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .health=100000, .energy=100000, .active=1, .behavior_timer=0}; mon_count++; actual_mon++;
                }

                /* Cap values to 9 for BPNBS encoding */
                int c_mon = actual_mon > 9 ? 9 : actual_mon;
                int c_rift = actual_rift > 9 ? 9 : actual_rift;
                int c_plat = actual_plat > 9 ? 9 : actual_plat;
                int c_buoy = actual_buoy > 9 ? 9 : actual_buoy;
                int c_mine = actual_mine > 9 ? 9 : actual_mine;
                int c_der = actual_der > 9 ? 9 : actual_der;
                int c_ast = actual_ast > 9 ? 9 : actual_ast;
                int c_com = actual_com > 9 ? 9 : actual_com;
                int c_pul = actual_pul > 9 ? 9 : actual_pul;
                int c_neb = actual_neb > 9 ? 9 : actual_neb;
                int c_bh = actual_bh > 9 ? 9 : actual_bh;
                int c_p = actual_p > 9 ? 9 : actual_p;
                int c_k = actual_k > 9 ? 9 : actual_k;
                int c_b = actual_b > 9 ? 9 : actual_b;
                int c_s = actual_s > 9 ? 9 : actual_s;

                spacegl_master.g[i][j][l] = (long long)c_mon * 10000000000000000LL + (long long)c_rift * 100000000000000LL + (long long)c_plat * 10000000000000LL + (long long)c_buoy * 1000000000000LL + (long long)c_mine * 100000000000LL + (long long)c_der * 10000000000LL + (long long)c_ast * 1000000000LL + (long long)c_com * 100000000LL + c_pul * 1000000 + c_neb * 100000 + c_bh * 10000 + c_p * 1000 + c_k * 100 + c_b * 10 + c_s;
                spacegl_master.k9 += actual_k;
                spacegl_master.b9 += actual_b;
            }

    printf("\n%s .--- GALAXY GENERATION COMPLETED: ASTROMETRICS REPORT ----------.%s\n", B_CYAN, RESET);
    printf("%s | %s 🚀 Vessels (NPCs):     %s%-5d %s| %s 🪐 Planets:            %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, n_count, B_CYAN, B_WHITE, B_GREEN, p_count, B_CYAN);
    printf("%s | %s ☀️ Stars:              %s%-5d %s| %s 🛰️ Starbases:          %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, s_count, B_CYAN, B_WHITE, B_GREEN, b_count, B_CYAN);
    printf("%s | %s 🕳️ Black Holes:        %s%-5d %s| %s ☁️ Nebulas:            %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, bh_count, B_CYAN, B_WHITE, B_GREEN, neb_count, B_CYAN);
    printf("%s | %s 🌟 Pulsars:            %s%-5d %s| %s ☄️ Comets:             %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, pul_count, B_CYAN, B_WHITE, B_GREEN, com_count, B_CYAN);
    printf("%s | %s 💎 Asteroids:          %s%-5d %s| %s 🏚️ Derelicts:          %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, ast_count, B_CYAN, B_WHITE, B_GREEN, der_count, B_CYAN);
    printf("%s | %s 💣 Mines:              %s%-5d %s| %s 📡 Buoys:              %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, mine_count, B_CYAN, B_WHITE, B_GREEN, buoy_count, B_CYAN);
    printf("%s | %s 🛡️ Defense Platforms:  %s%-5d %s| %s 🌀 Spacetime Rifts:    %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, plat_count, B_CYAN, B_WHITE, B_GREEN, rift_count, B_CYAN);
    printf("%s | %s 👾 Class-Omega Threats:%s%-5d                                 %s|\n", B_CYAN, B_WHITE, B_RED, mon_count, B_CYAN);
    printf("%s '---------------------------------------------------------------'%s\n\n", B_CYAN, RESET);
}
