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
#include <stdatomic.h>
#include <pthread.h>
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

static atomic_bool g_is_saving = false;

uint8_t SERVER_PUBKEY[32];
uint8_t SERVER_PRIVKEY[64];

QuadrantIndex (*spatial_index)[41][41] = NULL;

/* Optimization: Tracking only quadrants that contain dynamic objects to avoid triple-loop resets */
typedef struct { uint16_t q1, q2, q3; } DirtyQuad;
static DirtyQuad dirty_quads[10000]; 
static int dirty_count = 0;

void init_static_spatial_index() {
    if (!spatial_index) {
        spatial_index = calloc(41 * 41 * 41, sizeof(QuadrantIndex));
        if (!spatial_index) { perror("Failed to allocate spatial_index"); exit(1); }
    }
    memset(spatial_index, 0, 41 * 41 * 41 * sizeof(QuadrantIndex));

    for (int p = 0; p < MAX_PLANETS; p++) {
        if (planets[p].active) {
            if (!IS_Q_VALID(planets[p].q1, planets[p].q2, planets[p].q3)) {
                continue;
            }
            QuadrantIndex *q = &spatial_index[planets[p].q1][planets[p].q2][planets[p].q3];
            if (q->planet_count < MAX_Q_PLANETS) { 
                q->planets[q->planet_count] = &planets[p]; 
                q->planet_count++;
                q->static_planet_count = q->planet_count;
            }
        }
    }
    for (int b = 0; b < MAX_BASES; b++) {
        if (bases[b].active) {
            if (!IS_Q_VALID(bases[b].q1, bases[b].q2, bases[b].q3)) {
                continue;
            }
            QuadrantIndex *q = &spatial_index[bases[b].q1][bases[b].q2][bases[b].q3];
            if (q->base_count < MAX_Q_BASES) { 
                q->bases[q->base_count] = &bases[b]; 
                q->base_count++;
                q->static_base_count = q->base_count;
            }
        }
    }
    for (int s = 0; s < MAX_STARS; s++) {
        if (stars_data[s].active) {
            if (!IS_Q_VALID(stars_data[s].q1, stars_data[s].q2, stars_data[s].q3)) {
                continue;
            }
            QuadrantIndex *q = &spatial_index[stars_data[s].q1][stars_data[s].q2][stars_data[s].q3];
            if (q->star_count < MAX_Q_STARS) { 
                q->stars[q->star_count] = &stars_data[s]; 
                q->star_count++;
                q->static_star_count = q->star_count;
            }
        }
    }
    for (int h = 0; h < MAX_BH; h++) {
        if (black_holes[h].active) {
            if (!IS_Q_VALID(black_holes[h].q1, black_holes[h].q2, black_holes[h].q3)) {
                continue;
            }
            QuadrantIndex *q = &spatial_index[black_holes[h].q1][black_holes[h].q2][black_holes[h].q3];
            if (q->bh_count < MAX_Q_BH) { 
                q->black_holes[q->bh_count] = &black_holes[h]; 
                q->bh_count++;
                q->static_bh_count = q->bh_count;
            }
        }
    }
    for (int n = 0; n < MAX_NEBULAS; n++) {
        if (nebulas[n].active) {
            if (!IS_Q_VALID(nebulas[n].q1, nebulas[n].q2, nebulas[n].q3)) {
                continue;
            }
            QuadrantIndex *q = &spatial_index[nebulas[n].q1][nebulas[n].q2][nebulas[n].q3];
            if (q->nebula_count < MAX_Q_NEBULAS) { 
                q->nebulas[q->nebula_count] = &nebulas[n]; 
                q->nebula_count++;
                q->static_nebula_count = q->nebula_count;
            }
        }
    }
    for (int p = 0; p < MAX_PULSARS; p++) {
        if (pulsars[p].active) {
            if (!IS_Q_VALID(pulsars[p].q1, pulsars[p].q2, pulsars[p].q3)) {
                continue;
            }
            QuadrantIndex *q = &spatial_index[pulsars[p].q1][pulsars[p].q2][pulsars[p].q3];
            if (q->pulsar_count < MAX_Q_PULSARS) { 
                q->pulsars[q->pulsar_count] = &pulsars[p]; 
                q->pulsar_count++;
                q->static_pulsar_count = q->pulsar_count;
            }
        }
    }
}

static void mark_quad_dirty(int q1, int q2, int q3) {
    QuadrantIndex *q = &spatial_index[q1][q2][q3];
    if (q->npc_count == 0 && q->player_count == 0 && q->comet_count == 0 && q->asteroid_count == 0 && 
        q->derelict_count == 0 && q->mine_count == 0 && q->buoy_count == 0 && q->platform_count == 0 && 
        q->rift_count == 0 && q->monster_count == 0) {
        if (dirty_count < 10000) {
            dirty_quads[dirty_count++] = (DirtyQuad){(uint16_t)q1, (uint16_t)q2, (uint16_t)q3};
        }
    }
}

void refresh_lrs_grid() {
    for (int i = 1; i <= GALAXY_SIZE; i++) {
        for (int j = 1; j <= GALAXY_SIZE; j++) {
            for (int l = 1; l <= GALAXY_SIZE; l++) {
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
                                          + c_pul * 1000000 
                                          + c_neb * 100000 
                                          + c_bh * 10000 
                                          + c_p * 1000 
                                          + c_npc * 100 
                                          + c_b * 10 
                                          + c_s;
            }
        }
    }
}

void rebuild_spatial_index() {
    if (!spatial_index) {
        spatial_index = calloc(41 * 41 * 41, sizeof(QuadrantIndex));
        if (!spatial_index) { perror("Failed to allocate spatial_index"); exit(1); }
    }
    
    for(int i=0; i<dirty_count; i++) {
        QuadrantIndex *q = &spatial_index[dirty_quads[i].q1][dirty_quads[i].q2][dirty_quads[i].q3];
        q->npc_count = 0;
        q->player_count = 0;
        q->comet_count = 0;
        q->asteroid_count = 0;
        q->derelict_count = 0;
        q->mine_count = 0;
        q->buoy_count = 0;
        q->platform_count = 0;
        q->rift_count = 0;
        q->monster_count = 0;
    }
    dirty_count = 0;

    for(int n=0; n<MAX_NPC; n++) if(npcs[n].active) {
        if (IS_Q_VALID(npcs[n].q1, npcs[n].q2, npcs[n].q3)) {
            mark_quad_dirty(npcs[n].q1, npcs[n].q2, npcs[n].q3);
            QuadrantIndex *q = &spatial_index[npcs[n].q1][npcs[n].q2][npcs[n].q3];
            if (q->npc_count < MAX_Q_NPC) q->npcs[q->npc_count++] = &npcs[n];
        }
    }
    for(int c=0; c<MAX_COMETS; c++) if(comets[c].active) {
        if (IS_Q_VALID(comets[c].q1, comets[c].q2, comets[c].q3)) {
            mark_quad_dirty(comets[c].q1, comets[c].q2, comets[c].q3);
            QuadrantIndex *q = &spatial_index[comets[c].q1][comets[c].q2][comets[c].q3];
            if (q->comet_count < MAX_Q_COMETS) q->comets[q->comet_count++] = &comets[c];
        }
    }
    for(int a=0; a<MAX_ASTEROIDS; a++) if(asteroids[a].active) {
        if (IS_Q_VALID(asteroids[a].q1, asteroids[a].q2, asteroids[a].q3)) {
            mark_quad_dirty(asteroids[a].q1, asteroids[a].q2, asteroids[a].q3);
            QuadrantIndex *q = &spatial_index[asteroids[a].q1][asteroids[a].q2][asteroids[a].q3];
            if (q->asteroid_count < MAX_Q_ASTEROIDS) q->asteroids[q->asteroid_count++] = &asteroids[a];
        }
    }
    for(int d=0; d<MAX_DERELICTS; d++) if(derelicts[d].active) {
        if (IS_Q_VALID(derelicts[d].q1, derelicts[d].q2, derelicts[d].q3)) {
            mark_quad_dirty(derelicts[d].q1, derelicts[d].q2, derelicts[d].q3);
            QuadrantIndex *q = &spatial_index[derelicts[d].q1][derelicts[d].q2][derelicts[d].q3];
            if (q->derelict_count < MAX_Q_DERELICTS) q->derelicts[q->derelict_count++] = &derelicts[d];
        }
    }
    for(int m=0; m<MAX_MINES; m++) if(mines[m].active) {
        if (IS_Q_VALID(mines[m].q1, mines[m].q2, mines[m].q3)) {
            mark_quad_dirty(mines[m].q1, mines[m].q2, mines[m].q3);
            QuadrantIndex *q = &spatial_index[mines[m].q1][mines[m].q2][mines[m].q3];
            if (q->mine_count < MAX_Q_MINES) q->mines[q->mine_count++] = &mines[m];
        }
    }
    for(int b=0; b<MAX_BUOYS; b++) if(buoys[b].active) {
        if (IS_Q_VALID(buoys[b].q1, buoys[b].q2, buoys[b].q3)) {
            mark_quad_dirty(buoys[b].q1, buoys[b].q2, buoys[b].q3);
            QuadrantIndex *q = &spatial_index[buoys[b].q1][buoys[b].q2][buoys[b].q3];
            if (q->buoy_count < MAX_Q_BUOYS) q->buoys[q->buoy_count++] = &buoys[b];
        }
    }
    for(int pl=0; pl<MAX_PLATFORMS; pl++) if(platforms[pl].active) {
        if (IS_Q_VALID(platforms[pl].q1, platforms[pl].q2, platforms[pl].q3)) {
            mark_quad_dirty(platforms[pl].q1, platforms[pl].q2, platforms[pl].q3);
            QuadrantIndex *q = &spatial_index[platforms[pl].q1][platforms[pl].q2][platforms[pl].q3];
            if (q->platform_count < MAX_Q_PLATFORMS) q->platforms[q->platform_count++] = &platforms[pl];
        }
    }
    for(int r=0; r<MAX_RIFTS; r++) if(rifts[r].active) {
        if (IS_Q_VALID(rifts[r].q1, rifts[r].q2, rifts[r].q3)) {
            mark_quad_dirty(rifts[r].q1, rifts[r].q2, rifts[r].q3);
            QuadrantIndex *q = &spatial_index[rifts[r].q1][rifts[r].q2][rifts[r].q3];
            if (q->rift_count < MAX_Q_RIFTS) q->rifts[q->rift_count++] = &rifts[r];
        }
    }
    for(int m=0; m<MAX_MONSTERS; m++) if(monsters[m].active) {
        if (IS_Q_VALID(monsters[m].q1, monsters[m].q2, monsters[m].q3)) {
            mark_quad_dirty(monsters[m].q1, monsters[m].q2, monsters[m].q3);
            QuadrantIndex *q = &spatial_index[monsters[m].q1][monsters[m].q2][monsters[m].q3];
            if (q->monster_count < MAX_Q_MONSTERS) q->monsters[q->monster_count++] = &monsters[m];
        }
    }
    for(int u=0; u<MAX_CLIENTS; u++) if(players[u].active && players[u].name[0] != '\0') {
        if (IS_Q_VALID(players[u].state.q1, players[u].state.q2, players[u].state.q3)) {
            mark_quad_dirty(players[u].state.q1, players[u].state.q2, players[u].state.q3);
            QuadrantIndex *q = &spatial_index[players[u].state.q1][players[u].state.q2][players[u].state.q3];
            if (q->player_count < MAX_Q_PLAYERS) q->players[q->player_count++] = &players[u];
        }
    }
}

static void* save_thread(void* arg) {
    SpaceGLGame *master_copy = (SpaceGLGame*)arg;
    FILE *f = fopen("galaxy.dat", "wb");
    if (!f) { 
        perror("Failed to open galaxy.dat for writing"); 
        free(master_copy); 
        atomic_store(&g_is_saving, false);
        return NULL; 
    }
    
    int version = GALAXY_VERSION;
    fwrite(&version, sizeof(int), 1, f);
    fwrite(master_copy, sizeof(SpaceGLGame), 1, f);
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
    ts[strlen(ts)-1] = '\0';
    printf("--- [%s] GALAXY SAVED ASYNCHRONOUSLY ---\n", ts);
    
    free(master_copy);
    atomic_store(&g_is_saving, false);
    return NULL;
}

void save_galaxy_async() {
    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_is_saving, &expected, true)) return;
    
    SpaceGLGame *copy = malloc(sizeof(SpaceGLGame));
    if (!copy) { atomic_store(&g_is_saving, false); return; }
    memcpy(copy, &spacegl_master, sizeof(SpaceGLGame));
    
    pthread_t tid;
    if (pthread_create(&tid, NULL, save_thread, copy) != 0) {
        free(copy);
        atomic_store(&g_is_saving, false);
    } else pthread_detach(tid);
}

void save_galaxy() { save_galaxy_async(); }

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
        pthread_mutex_init(&players[i].socket_mutex, NULL);
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

static const char* ALLIANCE_NAMES[] = {"Enterprise", "Defiant", "Voyager", "Discovery", "Stargazer", "Reliant", "Saratoga", "Yorktown", "Intrepid", "Constellation", "Excelsior", "Hood", "Potemkin", "Victory", "Valiant", "Thunderchild", "Yeager", "Appalachia", "Bellerophon", "Budapest", "Curry", "Grissom", "Hathaway", "Kyushu", "Melbourne", "New Orleans", "Niagara", "Oberth", "Prometheus", "Rutledge", "Ticonderoga", "Zodiac", "Endeavour", "Ahwahnee", "Chekov", "Clement", "Drake", "Equinox", "Farragut", "Gettysburg", "Horatio", "Kearsarge", "Lantree", "Maryland", "Nautilus", "Pasteur", "Quark", "Renegade", "Solstice", "Trieste", "Universal", "Venture", "Whorfin", "Xerxes", "Yamato", "Zapata", "Aries", "Bismarck", "Cairo", "Dante", "Excalibur", "Fearless", "Gallant", "Helsinki", "Iwo Jima", "Justin", "Kongo", "Lexington", "Musashi", "Nova", "Olympic", "Phoenix", "Repulse", "Sutherland", "Tian An Men", "Ulysses", "Valley Forge", "Washington", "Ajax", "Berlin", "Centaur", "Dreadnought", "Enterprise-A", "Freedom", "Grafton", "Honshu", "Independence", "Janus", "Korolev", "Lakota", "Majestic", "Nebula", "Orion", "Peregrine", "Rigel", "Surak", "Thunderbolt", "Unity", "Vesta", "Wyoming"};
static const char* KORTHIAN_NAMES[] = {"Gorkon", "Negh'Var", "Vor'cha", "K'mpec", "Kronos One", "B'Moth", "Ch'Tang", "D'Kyr", "Gr'oth", "Hegh'ta", "IKS Bortas", "IKS Pagh", "IKS Rotarran", "IKS Wo'r'iv", "K'Ehleyr", "M'Char", "Ni'Var", "Pagh'tem-far", "Qu'Vat", "Somraw", "T'Ong", "Vorn", "Ya'Vang", "Zapola", "Amar", "Buruk", "D'k Tahg", "Fek'lhr", "Gron", "H'chak", "I'dan", "J'Dan", "K'lar", "L'Kor", "Maht-H'a", "N'Garen", "O'Tang", "P'Trell", "Q'Mok", "R'Ur", "S'Vak", "T'Kumbra", "U'Gak", "V'Rek", "W'Tang", "X'Kor", "Y'Dan", "Z'Kor", "K'Vort", "B'rel"};
static const char* XYLARI_NAMES[] = {"Gal Gath'thong", "Haakona", "Khazara", "Decius", "Belak", "Makar", "T'Vran", "Vahladore", "Apnex", "Bortas", "Centurion", "D'deridex", "Elite", "Firehawk", "Goroth", "H'ros", "Icarus", "Jae'rod", "K'Vort", "L'Vak", "Mogai", "Norexan", "O'Ran", "P'Vak", "Qu'Vat", "R'Vak", "S'Vak", "T'Vak", "U'Vak", "V'Vak", "W'Vak", "X'Vak", "Y'Vak", "Z'Vak", "Shadow One", "Nightshade", "Black Talon", "Void Stalker"};
static const char* SWARM_NAMES[] = {"Cube 01", "Sphere 22", "Diamond 4", "Tactical Cube 13", "Probe 9", "Queen's Vessel", "Unimatrice 0", "Unimatrice 1", "Node Alpha", "Node Beta", "Interceptor 5", "Scout 8", "Assimilation Unit", "Regeneration Hub", "Hive Ship", "Matrix One"};
static const char* VESPERIAN_NAMES[] = {"Trager", "Vetar", "Groumall", "Cuirass", "Aldara", "Bok'Nor", "Ch'Targh", "Dreadnought", "Ekar", "Fek'lhr", "Galor", "Heka", "Indra", "Janissary", "Keldon", "Lektop", "Makar", "Naprem", "Obsidian", "Praetor", "Quark", "Rakal", "Selin", "Tain", "Ulave", "Vrax", "Warlock", "Xenon", "Yarin", "Zarth"};
static const char* ASCENDANT_NAMES[] = {"Vorta's Command", "Jem'Hadar Scout", "Dominion Heavy", "Founder's Guard", "Gamma Attack", "Alpha Strike", "Beta Patrol", "Ketracel's Hope", "White Guard", "Shapeshifter"};
static const char* QUARZITE_NAMES[] = {"Crystal Web", "Prism", "Refraction", "Net Weaver", "Grid Guardian", "Spectrum", "Geometry", "Lattice", "Crystalline", "Array"};
static const char* SAURIAN_NAMES[] = {"Hunter", "Stalker", "Lizard", "Cold Blood", "Desert Fang", "Sand King", "Apex Predator", "Tail Strike", "Green Claw", "Scale"};
static const char* GILDED_NAMES[] = {"Gold Pressed", "Latinum Dream", "Profit Margin", "Rule of Acquisition", "Grand Nagus", "Marauder", "Trade Wind", "Market Force", "Credit", "Greed"};
static const char* FLUIDIC_NAMES[] = {"Bio-Ship 1", "Organism", "Cellular", "Enzyme", "Protector", "Void Swimmer", "Fluidic Alpha", "Genetic", "Mutant", "Anomaly"};
static const char* CRYOS_NAMES[] = {"Freezer", "Cold Death", "Zero Kelvin", "Ice Stalker", "Snow Blind", "Blizzard", "Glacier", "Frost Bite", "Hibernate", "Sub-Zero"};
static const char* APEX_NAMES[] = {"Hirogen Alpha", "Great Hunter", "Trophy Room", "Predator", "Prey Finder", "Tracking", "Kill Stroke", "Honor Bound", "Spirit", "Ancient"};

const char* get_random_ship_name(int faction) {
    const char** list = NULL;
    int size = 0;
    switch(faction) {
        case FACTION_ALLIANCE: list = ALLIANCE_NAMES; size = sizeof(ALLIANCE_NAMES)/sizeof(char*); break;
        case FACTION_KORTHIAN: list = KORTHIAN_NAMES; size = sizeof(KORTHIAN_NAMES)/sizeof(char*); break;
        case FACTION_XYLARI: list = XYLARI_NAMES; size = sizeof(XYLARI_NAMES)/sizeof(char*); break;
        case FACTION_SWARM: list = SWARM_NAMES; size = sizeof(SWARM_NAMES)/sizeof(char*); break;
        case FACTION_VESPERIAN: list = VESPERIAN_NAMES; size = sizeof(VESPERIAN_NAMES)/sizeof(char*); break;
        case FACTION_JEM_HADAR: list = ASCENDANT_NAMES; size = sizeof(ASCENDANT_NAMES)/sizeof(char*); break;
        case FACTION_THOLIAN: list = QUARZITE_NAMES; size = sizeof(QUARZITE_NAMES)/sizeof(char*); break;
        case FACTION_GORN: list = SAURIAN_NAMES; size = sizeof(SAURIAN_NAMES)/sizeof(char*); break;
        case FACTION_GILDED: list = GILDED_NAMES; size = sizeof(GILDED_NAMES)/sizeof(char*); break;
        case FACTION_SPECIES_8472: list = FLUIDIC_NAMES; size = sizeof(FLUIDIC_NAMES)/sizeof(char*); break;
        case FACTION_BREEN: list = CRYOS_NAMES; size = sizeof(CRYOS_NAMES)/sizeof(char*); break;
        case FACTION_HIROGEN: list = APEX_NAMES; size = sizeof(APEX_NAMES)/sizeof(char*); break;
        default: return "Unknown Vessel";
    }
    return list[rand() % size];
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
    int faction_counts[21] = {0};
    int class_der_counts[14] = {0};
    int faction_der_counts[21] = {0};
    int star_spectral_counts[7] = {0}; /* O, B, A, F, G, K, M */
    int nebula_type_counts[6] = {0};   /* Standard, High-Energy, Dark Matter, Ionic, Gravimetric, Temporal */
    int pulsar_type_counts[3] = {0};   /* Rotation-Powered, Accretion-Powered, Magnetar */
    int monster_type_counts[2] = {0};  /* Crystalline, Amoeba */
    int planet_type_counts[9] = {0};   /* None, Aetherium, Neo-Ti, Void-E, Graphene, Synaptics, Gas, Composite, Dark-Matter */
    int asteroid_type_counts[9] = {0}; /* Same resources as planets */
    
    for (int faction = 10; faction <= 20; faction++) {
        int count = GALAXY_CREATE_OBJECT_MIN_NPC_FACTION + (rand() % (GALAXY_CREATE_OBJECT_MAX_NPC_FACTION - GALAXY_CREATE_OBJECT_MIN_NPC_FACTION + 1));
        for (int k = 0; k < count && n_count < MAX_NPC; k++) {
            NPCShip *n = &npcs[n_count];
            n->id = n_count;
            n->faction = faction;
            n->active = 1;
            n->q1 = 1 + rand() % GALAXY_SIZE;
            n->q2 = 1 + rand() % GALAXY_SIZE;
            n->q3 = 1 + rand() % GALAXY_SIZE;
            n->x = (rand() % 100) / 10.0;
            n->y = (rand() % 100) / 10.0;
            n->z = (rand() % 100) / 10.0;
            n->gx = (n->q1 - 1) * 10.0 + n->x;
            n->gy = (n->q2 - 1) * 10.0 + n->y;
            n->gz = (n->q3 - 1) * 10.0 + n->z;
            
            uint64_t energy = 10000;
            if (faction == FACTION_SWARM) {
                energy = 80000 + (rand() % 20001);
            } else if (faction == FACTION_SPECIES_8472 || faction == FACTION_HIROGEN) {
                energy = 60000 + (rand() % 20001);
            } else if (faction == FACTION_KORTHIAN || faction == FACTION_XYLARI || faction == FACTION_JEM_HADAR) {
                energy = 30000 + (rand() % 20001);
            }
            n->energy = energy;
            n->engine_health = 100.0f;
            n->health = 1000;
            if (faction == FACTION_SWARM) {
                n->plating = 100000;
            } else if (faction == FACTION_HIROGEN || faction == FACTION_SPECIES_8472) {
                n->plating = 50000;
            } else {
                n->plating = 15000;
            }
            n->ship_class = SHIP_CLASS_GENERIC_ALIEN;
            n->nav_timer = 60 + rand() % 241;
            n->ai_state = AI_STATE_PATROL;
            strncpy(n->name, get_random_ship_name(faction), 63);
            n_count++;
            faction_counts[faction]++;
        }
    }

    for (int sclass = 0; sclass <= 12; sclass++) {
        int count = GALAXY_CREATE_OBJECT_MIN_ALLIANCE_WRECK + (rand() % (GALAXY_CREATE_OBJECT_MAX_ALLIANCE_WRECK - GALAXY_CREATE_OBJECT_MIN_ALLIANCE_WRECK + 1));
        for (int k = 0; k < count && der_count < MAX_DERELICTS; k++) {
            NPCDerelict *d = &derelicts[der_count];
            d->id = der_count;
            d->faction = FACTION_ALLIANCE;
            d->active = 1;
            d->ship_class = sclass;
            d->q1 = 1 + rand() % GALAXY_SIZE;
            d->q2 = 1 + rand() % GALAXY_SIZE;
            d->q3 = 1 + rand() % GALAXY_SIZE;
            d->x = (rand() % 100) / 10.0;
            d->y = (rand() % 100) / 10.0;
            d->z = (rand() % 100) / 10.0;
            strncpy(d->name, get_random_ship_name(FACTION_ALLIANCE), 63);
            der_count++;
            class_der_counts[sclass]++;
        }
    }

    for (int faction = 10; faction <= 20; faction++) {
        int count = GALAXY_CREATE_OBJECT_MIN_ALIEN_WRECK + (rand() % (GALAXY_CREATE_OBJECT_MAX_ALIEN_WRECK - GALAXY_CREATE_OBJECT_MIN_ALIEN_WRECK + 1));
        for (int k = 0; k < count && der_count < MAX_DERELICTS; k++) {
            NPCDerelict *d = &derelicts[der_count];
            d->id = der_count;
            d->faction = faction;
            d->active = 1;
            d->ship_class = SHIP_CLASS_GENERIC_ALIEN;
            d->q1 = 1 + rand() % GALAXY_SIZE;
            d->q2 = 1 + rand() % GALAXY_SIZE;
            d->q3 = 1 + rand() % GALAXY_SIZE;
            d->x = (rand() % 100) / 10.0;
            d->y = (rand() % 100) / 10.0;
            d->z = (rand() % 100) / 10.0;
            strncpy(d->name, get_random_ship_name(faction), 63);
            der_count++;
            faction_der_counts[faction]++;
        }
    }

    int target_bases = GALAXY_CREATE_OBJECT_MIN_BASE + (rand() % (GALAXY_CREATE_OBJECT_MAX_BASE - GALAXY_CREATE_OBJECT_MIN_BASE + 1));
    for (int i = 0; i < target_bases && b_count < MAX_BASES; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        bases[b_count] = (NPCBase){.id=b_count, .faction=FACTION_ALLIANCE, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .health=5000, .active=1};
        b_count++;
    }

    int target_planets = GALAXY_CREATE_OBJECT_MIN_PLANET + (rand() % (GALAXY_CREATE_OBJECT_MAX_PLANET - GALAXY_CREATE_OBJECT_MIN_PLANET + 1));
    for (int i = 0; i < target_planets && p_count < MAX_PLANETS; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        int r_type = (rand() % 8) + 1;
        planets[p_count] = (NPCPlanet){.id=p_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .resource_type=r_type, .amount=1000, .active=1};
        planet_type_counts[r_type]++;
        p_count++;
    }

    int target_stars = GALAXY_CREATE_OBJECT_MIN_STAR + (rand() % (GALAXY_CREATE_OBJECT_MAX_STAR - GALAXY_CREATE_OBJECT_MIN_STAR + 1));
    for (int i = 0; i < target_stars && s_count < MAX_STARS; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        int spectral = rand() % 7;
        stars_data[s_count] = (NPCStar){.id=s_count, .faction=spectral, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .active=1};
        star_spectral_counts[spectral]++;
        s_count++;
    }

    int target_bh = GALAXY_CREATE_OBJECT_MIN_BLACK_HOLE + (rand() % (GALAXY_CREATE_OBJECT_MAX_BLACK_HOLE - GALAXY_CREATE_OBJECT_MIN_BLACK_HOLE + 1));
    for (int i = 0; i < target_bh && bh_count < MAX_BH; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        black_holes[bh_count] = (NPCBlackHole){.id=bh_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .active=1};
        bh_count++;
    }

    int target_neb = GALAXY_CREATE_OBJECT_MIN_NEBULA + (rand() % (GALAXY_CREATE_OBJECT_MAX_NEBULA - GALAXY_CREATE_OBJECT_MIN_NEBULA + 1));
    for (int i = 0; i < target_neb && neb_count < MAX_NEBULAS; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        int n_type = rand() % 6;
        nebulas[neb_count] = (NPCNebula){.id=neb_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .type=n_type, .active=1};
        nebula_type_counts[n_type]++;
        neb_count++;
    }

    int target_pul = GALAXY_CREATE_OBJECT_MIN_PULSAR + (rand() % (GALAXY_CREATE_OBJECT_MAX_PULSAR - GALAXY_CREATE_OBJECT_MIN_PULSAR + 1));
    for (int i = 0; i < target_pul && pul_count < MAX_PULSARS; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        int p_type = rand() % 3;
        pulsars[pul_count] = (NPCPulsar){.id=pul_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .type=p_type, .active=1};
        pulsar_type_counts[p_type]++;
        pul_count++;
    }

    int target_com = GALAXY_CREATE_OBJECT_MIN_COMET + (rand() % (GALAXY_CREATE_OBJECT_MAX_COMET - GALAXY_CREATE_OBJECT_MIN_COMET + 1));
    for (int i = 0; i < target_com && com_count < MAX_COMETS; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE;
        int q2 = 1 + rand() % GALAXY_SIZE;
        int q3 = 1 + rand() % GALAXY_SIZE;
        double a = 10.0 + (rand() % 300) / 10.0;
        double b = a * (0.5 + (rand() % 40) / 100.0);
        comets[com_count] = (NPCComet){
            .id = com_count, 
            .q1 = q1, 
            .q2 = q2, 
            .q3 = q3, 
            .x = (rand() % 100) / 10.0, 
            .y = (rand() % 100) / 10.0, 
            .z = (rand() % 100) / 10.0, 
            .a = a, 
            .b = b, 
            .angle = (rand() % 360) * M_PI / 180.0, 
            .speed = 0.02 / a, 
            .inc = (rand() % 360) * M_PI / 180.0, 
            .cx = 50.0 + (rand() % 100 - 50) / 10.0, 
            .cy = 50.0 + (rand() % 100 - 50) / 10.0, 
            .cz = 50.0 + (rand() % 100 - 50) / 10.0, 
            .active = 1
        };
        com_count++;
    }

    while (ast_count < MAX_ASTEROIDS - 10) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        int cluster_size = GALAXY_CREATE_OBJECT_MIN_ASTEROID_CLUSTER + (rand() % (GALAXY_CREATE_OBJECT_MAX_ASTEROID_CLUSTER - GALAXY_CREATE_OBJECT_MIN_ASTEROID_CLUSTER + 1));
        for (int k = 0; k < cluster_size && ast_count < MAX_ASTEROIDS; k++) {
            int r_type = (rand() % 8) + 1;
            asteroids[ast_count] = (NPCAsteroid){.id=ast_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .size=0.1f+(rand()%20)/100.0f, .resource_type=r_type, .amount=100 + rand()%401, .active=1};
            asteroid_type_counts[r_type]++;
            ast_count++;
        }
    }

    while (mine_count < MAX_MINES - 5) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        int field_size = GALAXY_CREATE_OBJECT_MIN_MINE_FIELD + (rand() % (GALAXY_CREATE_OBJECT_MAX_MINE_FIELD - GALAXY_CREATE_OBJECT_MIN_MINE_FIELD + 1));
        for (int k = 0; k < field_size && mine_count < MAX_MINES; k++) {
            mines[mine_count] = (NPCMine){.id=mine_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .faction=FACTION_KORTHIAN, .active=1};
            mine_count++;
        }
    }

    int target_buoy = GALAXY_CREATE_OBJECT_MIN_BUOY + (rand() % (GALAXY_CREATE_OBJECT_MAX_BUOY - GALAXY_CREATE_OBJECT_MIN_BUOY + 1));
    for (int i = 0; i < target_buoy && buoy_count < MAX_BUOYS; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        buoys[buoy_count] = (NPCBuoy){.id=buoy_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .active=1};
        buoy_count++;
    }

    int target_plat = GALAXY_CREATE_OBJECT_MIN_PLATFORM + (rand() % (GALAXY_CREATE_OBJECT_MAX_PLATFORM - GALAXY_CREATE_OBJECT_MIN_PLATFORM + 1));
    for (int i = 0; i < target_plat && plat_count < MAX_PLATFORMS; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        platforms[plat_count] = (NPCPlatform){.id=plat_count, .faction=FACTION_KORTHIAN, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .health=5000, .energy=10000, .fire_cooldown=0, .active=1};
        plat_count++;
    }

    int target_rift = GALAXY_CREATE_OBJECT_MIN_RIFT + (rand() % (GALAXY_CREATE_OBJECT_MAX_RIFT - GALAXY_CREATE_OBJECT_MIN_RIFT + 1));
    for (int i = 0; i < target_rift && rift_count < MAX_RIFTS; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        rifts[rift_count] = (NPCRift){.id=rift_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .active=1};
        rift_count++;
    }

    int target_mon = GALAXY_CREATE_OBJECT_MIN_MONSTER + (rand() % (GALAXY_CREATE_OBJECT_MAX_MONSTER - GALAXY_CREATE_OBJECT_MIN_MONSTER + 1));
    for (int i = 0; i < target_mon && mon_count < MAX_MONSTERS; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        int m_type_idx = (rand()%100 < 50) ? 0 : 1;
        int m_type = (m_type_idx == 0) ? 30 : 31;
        monsters[mon_count] = (NPCMonster){.id=mon_count, .type=m_type, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%100)/10.0, .y=(rand()%100)/10.0, .z=(rand()%100)/10.0, .health=100000, .energy=100000, .active=1, .behavior_timer=0};
        monster_type_counts[m_type_idx]++;
        mon_count++;
    }

    rebuild_spatial_index();
    refresh_lrs_grid();

    printf("\n%s .--- GALAXY GENERATION COMPLETED: ASTROMETRICS REPORT ----------.%s\n", B_CYAN, RESET);
    printf("%s | %s 🚀 TOTAL VESSELS: %s%-5d %s| %s 🪐 Planets:            %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, n_count, B_CYAN, B_WHITE, B_GREEN, p_count, B_CYAN);
    printf("%s | %s 🏚️ TOTAL WRECKS:  %s%-5d %s| %s 🛰️ Starbases:          %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, der_count, B_CYAN, B_WHITE, B_GREEN, b_count, B_CYAN);
    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    
    printf("%s | %s [ STAR SPECTRAL TYPES ]    %s| %s [ NEBULA CLASSIFICATION ]  %s|\n", B_CYAN, B_YELLOW, B_CYAN, B_YELLOW, B_CYAN);
    const char* spectrals[] = {"O (Blue)", "B (White)", "A (White)", "F (Yellow)", "G (Yellow)", "K (Orange)", "M (Red)"};
    const char* neb_classes[] = {"Standard", "High-Energy", "Dark Matter", "Ionic", "Gravimetric", "Temporal"};
    for(int k=0; k<7; k++) {
        const char* s_name = spectrals[k];
        const char* n_name = (k < 6) ? neb_classes[k] : "";
        char n_val_str[32] = "";
        if (k < 6) sprintf(n_val_str, "%s%-4d", B_GREEN, nebula_type_counts[k]);
        printf("%s | %s %-12s: %s%-4d %s| %s %-12s: %-14s %s|\n", B_CYAN, B_WHITE, s_name, B_GREEN, star_spectral_counts[k], B_CYAN, B_WHITE, n_name, n_val_str, B_CYAN);
    }

    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    printf("%s | %s [ FACTION SHIPS ]          %s| %s [ ALLIANCE WRECKS ]        %s|\n", B_CYAN, B_YELLOW, B_CYAN, B_YELLOW, B_CYAN);
    
    const char* ship_classes_short[] = {"LEGACY", "SCOUT", "CRUISER", "ENGINE", "ESCORT", "EXPLORER", "FLAGSHIP", "SCIENCE", "CARRIER", "TACTICAL", "DIPLOMAT", "RESEARCH", "FRIGATE", "GENERIC"};
    for(int k=0; k<14; k++) {
        int faction = 10 + k;
        char f_line[64] = "";
        if (faction <= 20) {
            sprintf(f_line, "%s %-12s: %s%-4d", B_WHITE, get_species_name(faction), B_GREEN, faction_counts[faction]);
        } else {
            sprintf(f_line, "                       ");
        }
        printf("%s | %s %s| %s %-12s: %s%-4d %s|\n", B_CYAN, f_line, B_CYAN, B_WHITE, ship_classes_short[k], B_GREEN, class_der_counts[k], B_CYAN);
    }

    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    printf("%s | %s [ ALIEN WRECKS ]                                             %s|\n", B_CYAN, B_YELLOW, B_CYAN);
    for(int f=10; f<=20; f+=2) {
        char f1[64], f2[64] = "";
        sprintf(f1, "%s %-12s: %s%-4d", B_WHITE, get_species_name(f), B_GREEN, faction_der_counts[f]);
        if (f+1 <= 20) {
            sprintf(f2, "%s %-12s: %s%-4d", B_WHITE, get_species_name(f+1), B_GREEN, faction_der_counts[f+1]);
        }
        printf("%s | %s %s| %s %s %s|\n", B_CYAN, f1, B_CYAN, B_CYAN, f2, B_CYAN);
    }

    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    printf("%s | %s [ CLASS-OMEGA THREATS ]    %s| %s [ PULSAR CLASSIFICATION ]  %s|\n", B_CYAN, B_YELLOW, B_CYAN, B_YELLOW, B_CYAN);
    const char* p_classes[] = {"Rotation-Pwr", "Accretion-Pwr", "Magnetar"};
    printf("%s | %s Crystalline Ent: %s%-4d %s| %s %-12s: %s%-4d %s|\n", B_CYAN, B_WHITE, B_RED, monster_type_counts[0], B_CYAN, B_WHITE, p_classes[0], B_GREEN, pulsar_type_counts[0], B_CYAN);
    printf("%s | %s Space Amoebas:   %s%-4d %s| %s %-12s: %s%-4d %s|\n", B_CYAN, B_WHITE, B_RED, monster_type_counts[1], B_CYAN, B_WHITE, p_classes[1], B_GREEN, pulsar_type_counts[1], B_CYAN);
    printf("%s | %s                       %s| %s %-12s: %s%-4d %s|\n", B_CYAN, "", B_CYAN, B_WHITE, p_classes[2], B_GREEN, pulsar_type_counts[2], B_CYAN);

    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    printf("%s | %s [ PLANETARY RESOURCES ]                                      %s|\n", B_CYAN, B_YELLOW, B_CYAN);
    const char* res_names[] = {"None", "Aetherium", "Neo-Titanium", "Void-Essence", "Graphene", "Synaptics", "Nebular Gas", "Composite", "Dark-Matter"};
    for(int k=1; k<=8; k+=2) {
        printf("%s | %s %-14s: %s%-4d %s| %s %-14s: %s%-4d %s|\n", 
               B_CYAN, B_WHITE, res_names[k], B_GREEN, planet_type_counts[k], B_CYAN, 
               B_WHITE, (k+1 <= 8) ? res_names[k+1] : "", B_GREEN, (k+1 <= 8) ? planet_type_counts[k+1] : 0, B_CYAN);
    }

    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    printf("%s | %s [ ASTEROID RESOURCES ]                                      %s|\n", B_CYAN, B_YELLOW, B_CYAN);
    for(int k=1; k<=8; k+=2) {
        printf("%s | %s %-14s: %s%-4d %s| %s %-14s: %s%-4d %s|\n", 
               B_CYAN, B_WHITE, res_names[k], B_GREEN, asteroid_type_counts[k], B_CYAN, 
               B_WHITE, (k+1 <= 8) ? res_names[k+1] : "", B_GREEN, (k+1 <= 8) ? asteroid_type_counts[k+1] : 0, B_CYAN);
    }

    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    printf("%s | %s ☀️ Total Stars:        %s%-5d %s| %s 🕳️ Black Holes:        %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, s_count, B_CYAN, B_WHITE, B_GREEN, bh_count, B_CYAN);
    printf("%s | %s ☁️ Total Nebulas:      %s%-5d %s| %s 🌟 Total Pulsars:      %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, neb_count, B_CYAN, B_WHITE, B_GREEN, pul_count, B_CYAN);
    printf("%s | %s ☄️ Comets:             %s%-5d %s| %s 💎 Asteroids:          %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, com_count, B_CYAN, B_WHITE, B_GREEN, ast_count, B_CYAN);
    printf("%s | %s 💣 Mines:              %s%-5d %s| %s 📡 Buoys:              %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, mine_count, B_CYAN, B_WHITE, B_GREEN, buoy_count, B_CYAN);
    printf("%s | %s 🛡️ Defense Platforms:  %s%-5d %s| %s 🌀 Spacetime Rifts:    %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, plat_count, B_CYAN, B_WHITE, B_GREEN, rift_count, B_CYAN);
    printf("%s '---------------------------------------------------------------'%s\n\n", B_CYAN, RESET);
}

void spawn_derelict(int q1, int q2, int q3, double x, double y, double z, int faction, int ship_class, const char* name) {
    for (int i = 0; i < MAX_DERELICTS; i++) {
        if (!derelicts[i].active) {
            derelicts[i].id = i;
            derelicts[i].q1 = q1;
            derelicts[i].q2 = q2;
            derelicts[i].q3 = q3;
            derelicts[i].x = x;
            derelicts[i].y = y;
            derelicts[i].z = z;
            derelicts[i].faction = faction;
            derelicts[i].ship_class = ship_class;
            derelicts[i].active = 1;
            strncpy(derelicts[i].name, name, 63);
            derelicts[i].name[63] = '\0';
            return;
        }
    }
}
