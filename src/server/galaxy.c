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
NPCQuasar quasars[MAX_QUASARS];
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
NPCDyson dysons[MAX_DYSON];
NPCHub hubs[MAX_HUBS];
NPCRelic relics[MAX_RELICS];
NPCRupture ruptures[MAX_RUPTURES];
NPCSatellite satellites[MAX_SATELLITES];
NPCStorm storms[MAX_STORMS];
NPCArtifact artifacts[MAX_ARTIFACTS];
NPCWarpGate warp_gates[MAX_WARP_GATES];
NPCNeutronStar neutron_stars[MAX_NEUTRON_STARS];
NPCMegaStructure mega_structs[MAX_MEGA_STRUCTS];
NPCDarkCloud dark_clouds[MAX_DARK_CLOUDS];
NPCSingularity singularities[MAX_SINGULARITIES];
NPCPlasmaStorm plasma_storms[MAX_PLASMA_STORMS];
NPCOrbitalRing orbital_rings[MAX_ORBITAL_RINGS];
NPCTimeAnomaly time_anomalies[MAX_TIME_ANOMALIES];
NPCVoidCrystal void_crystals[MAX_VOID_CRYSTALS];
PlayerTorpedo players_torpedoes[MAX_GLOBAL_TORPEDOES];
ConnectedPlayer players[MAX_CLIENTS];
SpaceGLGame spacegl_master;

static atomic_bool g_is_saving = false;

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
    for (int s = 0; s < MAX_STORMS; s++) {
        if (storms[s].active) {
            if (!IS_Q_VALID(storms[s].q1, storms[s].q2, storms[s].q3)) continue;
            QuadrantIndex *q = &spatial_index[storms[s].q1][storms[s].q2][storms[s].q3];
            if (q->storm_count < MAX_Q_STORMS) q->storms[q->storm_count++] = &storms[s];
        }
    }
    for (int d = 0; d < MAX_DYSON; d++) {
        if (dysons[d].active) {
            if (!IS_Q_VALID(dysons[d].q1, dysons[d].q2, dysons[d].q3)) continue;
            QuadrantIndex *q = &spatial_index[dysons[d].q1][dysons[d].q2][dysons[d].q3];
            if (q->dyson_count < MAX_Q_DYSON) q->dysons[q->dyson_count++] = &dysons[d];
        }
    }
    for (int h = 0; h < MAX_HUBS; h++) {
        if (hubs[h].active) {
            if (!IS_Q_VALID(hubs[h].q1, hubs[h].q2, hubs[h].q3)) continue;
            QuadrantIndex *q = &spatial_index[hubs[h].q1][hubs[h].q2][hubs[h].q3];
            if (q->hub_count < MAX_Q_HUBS) q->hubs[q->hub_count++] = &hubs[h];
        }
    }
    for (int r = 0; r < MAX_RELICS; r++) {
        if (relics[r].active) {
            if (!IS_Q_VALID(relics[r].q1, relics[r].q2, relics[r].q3)) continue;
            QuadrantIndex *q = &spatial_index[relics[r].q1][relics[r].q2][relics[r].q3];
            if (q->relic_count < MAX_Q_RELICS) q->relics[q->relic_count++] = &relics[r];
        }
    }
    for (int ru = 0; ru < MAX_RUPTURES; ru++) {
        if (ruptures[ru].active) {
            if (!IS_Q_VALID(ruptures[ru].q1, ruptures[ru].q2, ruptures[ru].q3)) continue;
            QuadrantIndex *q = &spatial_index[ruptures[ru].q1][ruptures[ru].q2][ruptures[ru].q3];
            if (q->rupture_count < MAX_Q_RUPTURES) q->ruptures[q->rupture_count++] = &ruptures[ru];
        }
    }
    for (int sa = 0; sa < MAX_SATELLITES; sa++) {
        if (satellites[sa].active) {
            if (!IS_Q_VALID(satellites[sa].q1, satellites[sa].q2, satellites[sa].q3)) continue;
            QuadrantIndex *q = &spatial_index[satellites[sa].q1][satellites[sa].q2][satellites[sa].q3];
            if (q->satellite_count < MAX_Q_SATELLITES) q->satellites[q->satellite_count++] = &satellites[sa];
        }
    }
}

static void mark_quad_dirty(int q1, int q2, int q3) {
    QuadrantIndex *q = &spatial_index[q1][q2][q3];
    if (q->npc_count == 0 && q->player_count == 0 && q->comet_count == 0 && q->asteroid_count == 0 && 
        q->derelict_count == 0 && q->mine_count == 0 && q->buoy_count == 0 && q->platform_count == 0 && 
        q->rift_count == 0 && q->monster_count == 0 && q->torpedo_count == 0) {
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
                int c_u = (q->player_count > 9) ? 9 : q->player_count;
                /* Torpedoes contribute to tactical signal noise in LRS */
                int c_tor = (q->torpedo_count > 9) ? 9 : q->torpedo_count;
                
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
                int c_b = (q->base_count > 9) ? 9 : q->base_count;
                int c_s = (q->star_count > 9) ? 9 : q->star_count;
                
                int c_qsr = (q->quasar_count > 9) ? 9 : q->quasar_count;
                
                long long storm_flag = (spacegl_master.g[i][j][l] / 10000000LL) % 10;
                spacegl_master.g[i][j][l] = (long long)c_qsr * 100000000000000000LL
                                          + (long long)c_mon * 10000000000000000LL 
                                          + (long long)c_u   * 1000000000000000LL
                                          + (long long)c_tor * 1000000000000000LL /* Combine torps with user activity for LRS */
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
                                          + c_bh * SHIELD_MAX_STRENGTH
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
        q->quasar_count = 0;
        q->torpedo_count = 0;
        q->dyson_count = 0;
        q->hub_count = 0;
        q->relic_count = 0;
        q->rupture_count = 0;
        q->satellite_count = 0;
        q->storm_count = 0;
        q->artifact_count = 0;
        q->warp_gate_count = 0;
        q->neutron_star_count = 0;
        q->mega_struct_count = 0;
        q->dark_cloud_count = 0;
        q->singularity_count = 0;
        q->plasma_storm_count = 0;
        q->orbital_ring_count = 0;
        q->time_anomaly_count = 0;
        q->void_crystal_count = 0;
    }
    dirty_count = 0;

    for(int n=0; n<MAX_NPC; n++) if(npcs[n].active) {
        if (IS_Q_VALID(npcs[n].q1, npcs[n].q2, npcs[n].q3)) {
            mark_quad_dirty(npcs[n].q1, npcs[n].q2, npcs[n].q3);
            QuadrantIndex *q = &spatial_index[npcs[n].q1][npcs[n].q2][npcs[n].q3];
            if (q->npc_count < MAX_Q_NPC) q->npcs[q->npc_count++] = &npcs[n];
        }
    }
    /* ... (rest of loops should be here, assuming they are in the truncated part) ... */
    /* Populating new types in spatial index */
    for(int d=0; d<MAX_DYSON; d++) if(dysons[d].active) {
        if (IS_Q_VALID(dysons[d].q1, dysons[d].q2, dysons[d].q3)) {
            mark_quad_dirty(dysons[d].q1, dysons[d].q2, dysons[d].q3);
            QuadrantIndex *q = &spatial_index[dysons[d].q1][dysons[d].q2][dysons[d].q3];
            if (q->dyson_count < MAX_Q_DYSON) q->dysons[q->dyson_count++] = &dysons[d];
        }
    }
    for(int h=0; h<MAX_HUBS; h++) if(hubs[h].active) {
        if (IS_Q_VALID(hubs[h].q1, hubs[h].q2, hubs[h].q3)) {
            mark_quad_dirty(hubs[h].q1, hubs[h].q2, hubs[h].q3);
            QuadrantIndex *q = &spatial_index[hubs[h].q1][hubs[h].q2][hubs[h].q3];
            if (q->hub_count < MAX_Q_HUBS) q->hubs[q->hub_count++] = &hubs[h];
        }
    }
    for(int r=0; r<MAX_RELICS; r++) if(relics[r].active) {
        if (IS_Q_VALID(relics[r].q1, relics[r].q2, relics[r].q3)) {
            mark_quad_dirty(relics[r].q1, relics[r].q2, relics[r].q3);
            QuadrantIndex *q = &spatial_index[relics[r].q1][relics[r].q2][relics[r].q3];
            if (q->relic_count < MAX_Q_RELICS) q->relics[q->relic_count++] = &relics[r];
        }
    }
    for(int ru=0; ru<MAX_RUPTURES; ru++) if(ruptures[ru].active) {
        if (IS_Q_VALID(ruptures[ru].q1, ruptures[ru].q2, ruptures[ru].q3)) {
            mark_quad_dirty(ruptures[ru].q1, ruptures[ru].q2, ruptures[ru].q3);
            QuadrantIndex *q = &spatial_index[ruptures[ru].q1][ruptures[ru].q2][ruptures[ru].q3];
            if (q->rupture_count < MAX_Q_RUPTURES) q->ruptures[q->rupture_count++] = &ruptures[ru];
        }
    }
    for(int sa=0; sa<MAX_SATELLITES; sa++) if(satellites[sa].active) {
        if (IS_Q_VALID(satellites[sa].q1, satellites[sa].q2, satellites[sa].q3)) {
            mark_quad_dirty(satellites[sa].q1, satellites[sa].q2, satellites[sa].q3);
            QuadrantIndex *q = &spatial_index[satellites[sa].q1][satellites[sa].q2][satellites[sa].q3];
            if (q->satellite_count < MAX_Q_SATELLITES) q->satellites[q->satellite_count++] = &satellites[sa];
        }
    }
    for(int st=0; st<MAX_STORMS; st++) if(storms[st].active) {
        if (IS_Q_VALID(storms[st].q1, storms[st].q2, storms[st].q3)) {
            mark_quad_dirty(storms[st].q1, storms[st].q2, storms[st].q3);
            QuadrantIndex *q = &spatial_index[storms[st].q1][storms[st].q2][storms[st].q3];
            if (q->storm_count < MAX_Q_STORMS) q->storms[q->storm_count++] = &storms[st];
        }
    }

    for(int a=0; a<MAX_ARTIFACTS; a++) if(artifacts[a].active) {
        if (IS_Q_VALID(artifacts[a].q1, artifacts[a].q2, artifacts[a].q3)) {
            mark_quad_dirty(artifacts[a].q1, artifacts[a].q2, artifacts[a].q3);
            QuadrantIndex *q = &spatial_index[artifacts[a].q1][artifacts[a].q2][artifacts[a].q3];
            if (q->artifact_count < MAX_Q_ARTIFACTS) q->artifacts[q->artifact_count++] = &artifacts[a];
        }
    }

    for(int a=0; a<MAX_WARP_GATES; a++) if(warp_gates[a].active) {
        if (IS_Q_VALID(warp_gates[a].q1, warp_gates[a].q2, warp_gates[a].q3)) {
            mark_quad_dirty(warp_gates[a].q1, warp_gates[a].q2, warp_gates[a].q3);
            QuadrantIndex *q = &spatial_index[warp_gates[a].q1][warp_gates[a].q2][warp_gates[a].q3];
            if (q->warp_gate_count < MAX_Q_WARP_GATES) q->warp_gates[q->warp_gate_count++] = &warp_gates[a];
        }
    }

    for(int a=0; a<MAX_NEUTRON_STARS; a++) if(neutron_stars[a].active) {
        if (IS_Q_VALID(neutron_stars[a].q1, neutron_stars[a].q2, neutron_stars[a].q3)) {
            mark_quad_dirty(neutron_stars[a].q1, neutron_stars[a].q2, neutron_stars[a].q3);
            QuadrantIndex *q = &spatial_index[neutron_stars[a].q1][neutron_stars[a].q2][neutron_stars[a].q3];
            if (q->neutron_star_count < MAX_Q_NEUTRON_STARS) q->neutron_stars[q->neutron_star_count++] = &neutron_stars[a];
        }
    }

    for(int a=0; a<MAX_MEGA_STRUCTS; a++) if(mega_structs[a].active) {
        if (IS_Q_VALID(mega_structs[a].q1, mega_structs[a].q2, mega_structs[a].q3)) {
            mark_quad_dirty(mega_structs[a].q1, mega_structs[a].q2, mega_structs[a].q3);
            QuadrantIndex *q = &spatial_index[mega_structs[a].q1][mega_structs[a].q2][mega_structs[a].q3];
            if (q->mega_struct_count < MAX_Q_MEGA_STRUCTS) q->mega_structs[q->mega_struct_count++] = &mega_structs[a];
        }
    }

    for(int a=0; a<MAX_DARK_CLOUDS; a++) if(dark_clouds[a].active) {
        if (IS_Q_VALID(dark_clouds[a].q1, dark_clouds[a].q2, dark_clouds[a].q3)) {
            mark_quad_dirty(dark_clouds[a].q1, dark_clouds[a].q2, dark_clouds[a].q3);
            QuadrantIndex *q = &spatial_index[dark_clouds[a].q1][dark_clouds[a].q2][dark_clouds[a].q3];
            if (q->dark_cloud_count < MAX_Q_DARK_CLOUDS) q->dark_clouds[q->dark_cloud_count++] = &dark_clouds[a];
        }
    }

    for(int a=0; a<MAX_SINGULARITIES; a++) if(singularities[a].active) {
        if (IS_Q_VALID(singularities[a].q1, singularities[a].q2, singularities[a].q3)) {
            mark_quad_dirty(singularities[a].q1, singularities[a].q2, singularities[a].q3);
            QuadrantIndex *q = &spatial_index[singularities[a].q1][singularities[a].q2][singularities[a].q3];
            if (q->singularity_count < MAX_Q_SINGULARITIES) q->singularities[q->singularity_count++] = &singularities[a];
        }
    }

    for(int a=0; a<MAX_PLASMA_STORMS; a++) if(plasma_storms[a].active) {
        if (IS_Q_VALID(plasma_storms[a].q1, plasma_storms[a].q2, plasma_storms[a].q3)) {
            mark_quad_dirty(plasma_storms[a].q1, plasma_storms[a].q2, plasma_storms[a].q3);
            QuadrantIndex *q = &spatial_index[plasma_storms[a].q1][plasma_storms[a].q2][plasma_storms[a].q3];
            if (q->plasma_storm_count < MAX_Q_PLASMA_STORMS) q->plasma_storms[q->plasma_storm_count++] = &plasma_storms[a];
        }
    }

    for(int a=0; a<MAX_ORBITAL_RINGS; a++) if(orbital_rings[a].active) {
        if (IS_Q_VALID(orbital_rings[a].q1, orbital_rings[a].q2, orbital_rings[a].q3)) {
            mark_quad_dirty(orbital_rings[a].q1, orbital_rings[a].q2, orbital_rings[a].q3);
            QuadrantIndex *q = &spatial_index[orbital_rings[a].q1][orbital_rings[a].q2][orbital_rings[a].q3];
            if (q->orbital_ring_count < MAX_Q_ORBITAL_RINGS) q->orbital_rings[q->orbital_ring_count++] = &orbital_rings[a];
        }
    }

    for(int a=0; a<MAX_TIME_ANOMALIES; a++) if(time_anomalies[a].active) {
        if (IS_Q_VALID(time_anomalies[a].q1, time_anomalies[a].q2, time_anomalies[a].q3)) {
            mark_quad_dirty(time_anomalies[a].q1, time_anomalies[a].q2, time_anomalies[a].q3);
            QuadrantIndex *q = &spatial_index[time_anomalies[a].q1][time_anomalies[a].q2][time_anomalies[a].q3];
            if (q->time_anomaly_count < MAX_Q_TIME_ANOMALIES) q->time_anomalies[q->time_anomaly_count++] = &time_anomalies[a];
        }
    }

    for(int a=0; a<MAX_VOID_CRYSTALS; a++) if(void_crystals[a].active) {
        if (IS_Q_VALID(void_crystals[a].q1, void_crystals[a].q2, void_crystals[a].q3)) {
            mark_quad_dirty(void_crystals[a].q1, void_crystals[a].q2, void_crystals[a].q3);
            QuadrantIndex *q = &spatial_index[void_crystals[a].q1][void_crystals[a].q2][void_crystals[a].q3];
            if (q->void_crystal_count < MAX_Q_VOID_CRYSTALS) q->void_crystals[q->void_crystal_count++] = &void_crystals[a];
        }
    }
    /* ... existing loops ... */
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
    for(int qsr=0; qsr<MAX_QUASARS; qsr++) if(quasars[qsr].active) {
        if (IS_Q_VALID(quasars[qsr].q1, quasars[qsr].q2, quasars[qsr].q3)) {
            mark_quad_dirty(quasars[qsr].q1, quasars[qsr].q2, quasars[qsr].q3);
            QuadrantIndex *q = &spatial_index[quasars[qsr].q1][quasars[qsr].q2][quasars[qsr].q3];
            if (q->quasar_count < MAX_Q_QUASARS) q->quasars[q->quasar_count++] = &quasars[qsr];
        }
    }
    for(int u=0; u<MAX_CLIENTS; u++) if(players[u].active && players[u].name[0] != '\0') {
        if (IS_Q_VALID(players[u].state.q1, players[u].state.q2, players[u].state.q3)) {
            mark_quad_dirty(players[u].state.q1, players[u].state.q2, players[u].state.q3);
            QuadrantIndex *q = &spatial_index[players[u].state.q1][players[u].state.q2][players[u].state.q3];
            if (q->player_count < MAX_Q_PLAYERS) q->players[q->player_count++] = &players[u];
        }
    }
    for(int t=0; t<MAX_GLOBAL_TORPEDOES; t++) if(players_torpedoes[t].active) {
        if (IS_Q_VALID(players_torpedoes[t].q1, players_torpedoes[t].q2, players_torpedoes[t].q3)) {
            mark_quad_dirty(players_torpedoes[t].q1, players_torpedoes[t].q2, players_torpedoes[t].q3);
            QuadrantIndex *q = &spatial_index[players_torpedoes[t].q1][players_torpedoes[t].q2][players_torpedoes[t].q3];
            if (q->torpedo_count < MAX_Q_TORPEDOES) q->torpedoes[q->torpedo_count++] = &players_torpedoes[t];
        }
    }
}

static void save_task(void* arg) {
    SpaceGLGame *master_copy = (SpaceGLGame*)arg;
    FILE *f = fopen("galaxy.dat", "wb");
    if (!f) { 
        perror("Failed to open galaxy.dat for writing"); 
        free(master_copy); 
        atomic_store(&g_is_saving, false);
        return; 
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
    fwrite(quasars, sizeof(NPCQuasar), MAX_QUASARS, f);
    fwrite(comets, sizeof(NPCComet), MAX_COMETS, f);
    fwrite(asteroids, sizeof(NPCAsteroid), MAX_ASTEROIDS, f);
    fwrite(derelicts, sizeof(NPCDerelict), MAX_DERELICTS, f);
    fwrite(mines, sizeof(NPCMine), MAX_MINES, f);
    fwrite(buoys, sizeof(NPCBuoy), MAX_BUOYS, f);
    fwrite(platforms, sizeof(NPCPlatform), MAX_PLATFORMS, f);
    fwrite(rifts, sizeof(NPCRift), MAX_RIFTS, f);
    fwrite(monsters, sizeof(NPCMonster), MAX_MONSTERS, f);
    fwrite(dysons, sizeof(NPCDyson), MAX_DYSON, f);
    fwrite(hubs, sizeof(NPCHub), MAX_HUBS, f);
    fwrite(relics, sizeof(NPCRelic), MAX_RELICS, f);
    fwrite(ruptures, sizeof(NPCRupture), MAX_RUPTURES, f);
    fwrite(satellites, sizeof(NPCSatellite), MAX_SATELLITES, f);
    fwrite(storms, sizeof(NPCStorm), MAX_STORMS, f);
    fwrite(artifacts, sizeof(NPCArtifact), MAX_ARTIFACTS, f);
    fwrite(warp_gates, sizeof(NPCWarpGate), MAX_WARP_GATES, f);
    fwrite(neutron_stars, sizeof(NPCNeutronStar), MAX_NEUTRON_STARS, f);
    fwrite(mega_structs, sizeof(NPCMegaStructure), MAX_MEGA_STRUCTS, f);
    fwrite(dark_clouds, sizeof(NPCDarkCloud), MAX_DARK_CLOUDS, f);
    fwrite(singularities, sizeof(NPCSingularity), MAX_SINGULARITIES, f);
    fwrite(plasma_storms, sizeof(NPCPlasmaStorm), MAX_PLASMA_STORMS, f);
    fwrite(orbital_rings, sizeof(NPCOrbitalRing), MAX_ORBITAL_RINGS, f);
    fwrite(time_anomalies, sizeof(NPCTimeAnomaly), MAX_TIME_ANOMALIES, f);
    fwrite(void_crystals, sizeof(NPCVoidCrystal), MAX_VOID_CRYSTALS, f);
    fwrite(players, sizeof(ConnectedPlayer), MAX_CLIENTS, f);
    fclose(f);
    
    time_t now = time(NULL);
    char *ts = ctime(&now);
    if (ts) ts[strlen(ts)-1] = '\0';
    printf("--- [%s] GALAXY SAVED ASYNCHRONOUSLY (POOL) ---\n", ts);
    
    free(master_copy);
    atomic_store(&g_is_saving, false);
}

void save_galaxy_async() {
    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_is_saving, &expected, true)) return;
    
    SpaceGLGame *copy = malloc(sizeof(SpaceGLGame));
    if (!copy) { atomic_store(&g_is_saving, false); return; }
    memcpy(copy, &spacegl_master, sizeof(SpaceGLGame));
    
    extern threadpool_t *g_pool;
    if (g_pool) {
        threadpool_add_task(g_pool, save_task, copy);
    } else {
        /* Fallback if pool not ready */
        save_task(copy);
    }
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
    CHECK_READ(quasars, sizeof(NPCQuasar), MAX_QUASARS, f);
    CHECK_READ(comets, sizeof(NPCComet), MAX_COMETS, f);
    CHECK_READ(asteroids, sizeof(NPCAsteroid), MAX_ASTEROIDS, f);
    CHECK_READ(derelicts, sizeof(NPCDerelict), MAX_DERELICTS, f);
    CHECK_READ(mines, sizeof(NPCMine), MAX_MINES, f);
    CHECK_READ(buoys, sizeof(NPCBuoy), MAX_BUOYS, f);
    CHECK_READ(platforms, sizeof(NPCPlatform), MAX_PLATFORMS, f);
    CHECK_READ(rifts, sizeof(NPCRift), MAX_RIFTS, f);
    CHECK_READ(monsters, sizeof(NPCMonster), MAX_MONSTERS, f);
    CHECK_READ(dysons, sizeof(NPCDyson), MAX_DYSON, f);
    CHECK_READ(hubs, sizeof(NPCHub), MAX_HUBS, f);
    CHECK_READ(relics, sizeof(NPCRelic), MAX_RELICS, f);
    CHECK_READ(ruptures, sizeof(NPCRupture), MAX_RUPTURES, f);
    CHECK_READ(satellites, sizeof(NPCSatellite), MAX_SATELLITES, f);
    CHECK_READ(storms, sizeof(NPCStorm), MAX_STORMS, f);
    CHECK_READ(artifacts, sizeof(NPCArtifact), MAX_ARTIFACTS, f);
    CHECK_READ(warp_gates, sizeof(NPCWarpGate), MAX_WARP_GATES, f);
    CHECK_READ(neutron_stars, sizeof(NPCNeutronStar), MAX_NEUTRON_STARS, f);
    CHECK_READ(mega_structs, sizeof(NPCMegaStructure), MAX_MEGA_STRUCTS, f);
    CHECK_READ(dark_clouds, sizeof(NPCDarkCloud), MAX_DARK_CLOUDS, f);
    CHECK_READ(singularities, sizeof(NPCSingularity), MAX_SINGULARITIES, f);
    CHECK_READ(plasma_storms, sizeof(NPCPlasmaStorm), MAX_PLASMA_STORMS, f);
    CHECK_READ(orbital_rings, sizeof(NPCOrbitalRing), MAX_ORBITAL_RINGS, f);
    CHECK_READ(time_anomalies, sizeof(NPCTimeAnomaly), MAX_TIME_ANOMALIES, f);
    CHECK_READ(void_crystals, sizeof(NPCVoidCrystal), MAX_VOID_CRYSTALS, f);
    CHECK_READ(players, sizeof(ConnectedPlayer), MAX_CLIENTS, f);
    fclose(f);
    
    for(int i=0; i<MAX_CLIENTS; i++) {
        players[i].active = 0;
        players[i].socket = 0;
        /* Preserve the name for login recognition, but initialize the mutex */
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
        case 28: return "Dyson Fragment";
        case 29: return "Trading Hub";
        case 30: return "Ancient Relic";
        case 31: return "Subspace Rupture";
        case 32: return "Satellite";
        case 33: return "Ion Storm";
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
    memset(quasars, 0, sizeof(quasars));
    memset(comets, 0, sizeof(comets));
    memset(asteroids, 0, sizeof(asteroids));
    memset(derelicts, 0, sizeof(derelicts));
    memset(mines, 0, sizeof(mines));
    memset(buoys, 0, sizeof(buoys));
    memset(platforms, 0, sizeof(platforms));
    memset(rifts, 0, sizeof(rifts));
    memset(monsters, 0, sizeof(monsters));
    memset(dysons, 0, sizeof(dysons));
    memset(hubs, 0, sizeof(hubs));
    memset(relics, 0, sizeof(relics));
    memset(ruptures, 0, sizeof(ruptures));
    memset(satellites, 0, sizeof(satellites));
    memset(storms, 0, sizeof(storms));
    memset(artifacts, 0, sizeof(artifacts));
    memset(warp_gates, 0, sizeof(warp_gates));
    memset(neutron_stars, 0, sizeof(neutron_stars));
    memset(mega_structs, 0, sizeof(mega_structs));
    memset(dark_clouds, 0, sizeof(dark_clouds));
    memset(singularities, 0, sizeof(singularities));
    memset(plasma_storms, 0, sizeof(plasma_storms));
    memset(orbital_rings, 0, sizeof(orbital_rings));
    memset(time_anomalies, 0, sizeof(time_anomalies));
    memset(void_crystals, 0, sizeof(void_crystals));

    int n_count = 0, b_count = 0, p_count = 0, s_count = 0, bh_count = 0, neb_count = 0, pul_count = 0, com_count = 0, ast_count = 0, der_count = 0, mine_count = 0, buoy_count = 0, plat_count = 0, rift_count = 0, mon_count = 0;
    int faction_counts[21] = {0};
    int class_der_counts[14] = {0};
    int faction_der_counts[21] = {0};
    int star_spectral_counts[7] = {0}; /* O, B, A, F, G, K, M */
    int nebula_type_counts[6] = {0};   /* Standard, High-Energy, Dark Matter, Ionic, Gravimetric, Temporal */
    int pulsar_type_counts[3] = {0};   /* Rotation-Powered, Accretion-Powered, Magnetar */
    int quasar_type_counts[7] = {0};   /* Radio-loud, Radio-quiet, BAL, Type 2, Red, OVV, Weak-Em */
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
            n->x = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
            n->y = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
            n->z = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
            n->gx = (n->q1 - 1) * QUADRANT_SIZE + n->x;
            n->gy = (n->q2 - 1) * QUADRANT_SIZE + n->y;
            n->gz = (n->q3 - 1) * QUADRANT_SIZE + n->z;
            
            uint64_t energy = 10000;
            if (faction == FACTION_SWARM) {
                energy = 80000 + (rand() % 20001);
            } else if (faction == FACTION_SPECIES_8472 || faction == FACTION_HIROGEN) {
                energy = 60000 + (rand() % 20001);
            } else if (faction == FACTION_KORTHIAN || faction == FACTION_XYLARI || faction == FACTION_JEM_HADAR) {
                energy = 30000 + (rand() % 20001);
            }
            n->energy = energy;
            n->engine_health = (float)YIELD_HARVEST_MAX;
            n->health = MAX_TORPEDO_CAPACITY;
            if (faction == FACTION_SWARM) {
                n->plating = 100000;
            } else if (faction == FACTION_HIROGEN || faction == FACTION_SPECIES_8472) {
                n->plating = 50000;
            } else {
                n->plating = 15000;
            }
            n->ship_class = SHIP_CLASS_GENERIC_ALIEN;
            n->nav_timer = (int)GAME_TICK_RATE + rand() % (int)(4 * GAME_TICK_RATE + 1);
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
            d->x = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
            d->y = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
            d->z = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
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
            d->x = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
            d->y = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
            d->z = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
            strncpy(d->name, get_random_ship_name(faction), 63);
            der_count++;
            faction_der_counts[faction]++;
        }
    }

    int target_bases = GALAXY_CREATE_OBJECT_MIN_BASE + (rand() % (GALAXY_CREATE_OBJECT_MAX_BASE - GALAXY_CREATE_OBJECT_MIN_BASE + 1));
    for (int i = 0; i < target_bases && b_count < MAX_BASES; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        bases[b_count] = (NPCBase){.id=b_count, .faction=FACTION_ALLIANCE, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .health=COST_HYPERDRIVE_INIT, .active=1};
        b_count++;
    }

    int target_planets = GALAXY_CREATE_OBJECT_MIN_PLANET + (rand() % (GALAXY_CREATE_OBJECT_MAX_PLANET - GALAXY_CREATE_OBJECT_MIN_PLANET + 1));
    for (int i = 0; i < target_planets && p_count < MAX_PLANETS; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        int r_type = (rand() % 8) + 1;
        planets[p_count] = (NPCPlanet){.id=p_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .resource_type=r_type, .amount=MAX_TORPEDO_CAPACITY, .active=1};
        planet_type_counts[r_type]++;
        p_count++;
    }

    int target_stars = GALAXY_CREATE_OBJECT_MIN_STAR + (rand() % (GALAXY_CREATE_OBJECT_MAX_STAR - GALAXY_CREATE_OBJECT_MIN_STAR + 1));
    for (int i = 0; i < target_stars && s_count < MAX_STARS; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        int spectral = rand() % 7;
        stars_data[s_count] = (NPCStar){.id=s_count, .faction=spectral, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
        star_spectral_counts[spectral]++;
        s_count++;
    }

    int target_bh = GALAXY_CREATE_OBJECT_MIN_BLACK_HOLE + (rand() % (GALAXY_CREATE_OBJECT_MAX_BLACK_HOLE - GALAXY_CREATE_OBJECT_MIN_BLACK_HOLE + 1));
    for (int i = 0; i < target_bh && bh_count < MAX_BH; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        black_holes[bh_count] = (NPCBlackHole){.id=bh_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
        bh_count++;
    }

    int target_neb = GALAXY_CREATE_OBJECT_MIN_NEBULA + (rand() % (GALAXY_CREATE_OBJECT_MAX_NEBULA - GALAXY_CREATE_OBJECT_MIN_NEBULA + 1));
    for (int i = 0; i < target_neb && neb_count < MAX_NEBULAS; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        int n_type = rand() % 6;
        nebulas[neb_count] = (NPCNebula){.id=neb_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .type=n_type, .active=1};
        nebula_type_counts[n_type]++;
        neb_count++;
    }

    int target_pul = GALAXY_CREATE_OBJECT_MIN_PULSAR + (rand() % (GALAXY_CREATE_OBJECT_MAX_PULSAR - GALAXY_CREATE_OBJECT_MIN_PULSAR + 1));
    for (int i = 0; i < target_pul && pul_count < MAX_PULSARS; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        int p_type = rand() % 3;
        pulsars[pul_count] = (NPCPulsar){.id=pul_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .type=p_type, .active=1};
        pulsar_type_counts[p_type]++;
        pul_count++;
    }

    int qsr_count = 0;
    int target_qsr = GALAXY_CREATE_OBJECT_MIN_QUASAR + (rand() % (GALAXY_CREATE_OBJECT_MAX_QUASAR - GALAXY_CREATE_OBJECT_MIN_QUASAR + 1));
    for (int i = 0; i < target_qsr && qsr_count < MAX_QUASARS; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        /* Types: 0:Radio-loud, 1:Radio-quiet, 2:BAL, 3:Type 2, 4:Red, 5:OVV, 6:Weak emission */
        int q_type = rand() % 7;
        quasars[qsr_count] = (NPCQuasar){.id=qsr_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .type=q_type, .active=1};
        quasar_type_counts[q_type]++;
        qsr_count++;
    }

    int target_com = GALAXY_CREATE_OBJECT_MIN_COMET + (rand() % (GALAXY_CREATE_OBJECT_MAX_COMET - GALAXY_CREATE_OBJECT_MIN_COMET + 1));
    for (int i = 0; i < target_com && com_count < MAX_COMETS; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE;
        int q2 = 1 + rand() % GALAXY_SIZE;
        int q3 = 1 + rand() % GALAXY_SIZE;
        double a = 10.0 + (rand() % (int)(QUADRANT_SIZE * 30)) / 10.0;
        double b = a * (0.5 + (rand() % 40) / 100.0);
        comets[com_count] = (NPCComet){
            .id = com_count, 
            .q1 = q1, 
            .q2 = q2, 
            .q3 = q3, 
                        .x = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM,
                        .y = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM,
                        .z = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM,
                        .a = a, 
                        .b = b, 
                        .angle = (rand() % 360) * M_PI / 180.0, 
                        .speed = 0.02 / a, 
                        .inc = (rand() % 360) * M_PI / 180.0, 
                                    .cx = (GALAXY_SIZE * QUADRANT_SIZE / RATE_REGEN_DRAIN_FACTOR) + (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM) - (QUADRANT_SIZE * (RATIO_COORD_RANDOM / 2.0))) / RATIO_COORD_RANDOM, 
                                    .cy = (GALAXY_SIZE * QUADRANT_SIZE / RATE_REGEN_DRAIN_FACTOR) + (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM) - (QUADRANT_SIZE * (RATIO_COORD_RANDOM / 2.0))) / RATIO_COORD_RANDOM, 
                                    .cz = (GALAXY_SIZE * QUADRANT_SIZE / RATE_REGEN_DRAIN_FACTOR) + (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM) - (QUADRANT_SIZE * (RATIO_COORD_RANDOM / 2.0))) / RATIO_COORD_RANDOM, 
                         
                        .active = 1
                    };
                    com_count++;
                }
            
                while (ast_count < MAX_ASTEROIDS - (int)THRESHOLD_SYS_CRITICAL) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    int cluster_size = GALAXY_CREATE_OBJECT_MIN_ASTEROID_CLUSTER + (rand() % (GALAXY_CREATE_OBJECT_MAX_ASTEROID_CLUSTER - GALAXY_CREATE_OBJECT_MIN_ASTEROID_CLUSTER + 1));
                    for (int k = 0; k < cluster_size && ast_count < MAX_ASTEROIDS; k++) {
                        int r_type = (rand() % 8) + 1;
                        asteroids[ast_count] = (NPCAsteroid){.id=ast_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .size=0.1f+(rand()%20)/100.0f, .resource_type=r_type, .amount=YIELD_MINE_MIN + rand()%(YIELD_MINE_MAX - YIELD_MINE_MIN + 1), .active=1};
                        asteroid_type_counts[r_type]++;
                        ast_count++;
                    }
                }
            
                while (mine_count < MAX_MINES - (int)(THRESHOLD_SYS_CRITICAL / 2.0)) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    int field_size = GALAXY_CREATE_OBJECT_MIN_MINE_FIELD + (rand() % (GALAXY_CREATE_OBJECT_MAX_MINE_FIELD - GALAXY_CREATE_OBJECT_MIN_MINE_FIELD + 1));
                    for (int k = 0; k < field_size && mine_count < MAX_MINES; k++) {
                        mines[mine_count] = (NPCMine){.id=mine_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .faction=FACTION_KORTHIAN, .active=1};
                        mine_count++;
                    }
                }
            
                int target_buoy = GALAXY_CREATE_OBJECT_MIN_BUOY + (rand() % (GALAXY_CREATE_OBJECT_MAX_BUOY - GALAXY_CREATE_OBJECT_MIN_BUOY + 1));
                for (int i = 0; i < target_buoy && buoy_count < MAX_BUOYS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    buoys[buoy_count] = (NPCBuoy){.id=buoy_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    buoy_count++;
                }
            
                int target_plat = GALAXY_CREATE_OBJECT_MIN_PLATFORM + (rand() % (GALAXY_CREATE_OBJECT_MAX_PLATFORM - GALAXY_CREATE_OBJECT_MIN_PLATFORM + 1));
                for (int i = 0; i < target_plat && plat_count < MAX_PLATFORMS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    platforms[plat_count] = (NPCPlatform){.id=plat_count, .faction=FACTION_KORTHIAN, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .health=COST_HYPERDRIVE_INIT, .energy=SHIELD_MAX_STRENGTH, .fire_cooldown=0, .active=1};
                    plat_count++;
                }
            
                int target_rift = GALAXY_CREATE_OBJECT_MIN_RIFT + (rand() % (GALAXY_CREATE_OBJECT_MAX_RIFT - GALAXY_CREATE_OBJECT_MIN_RIFT + 1));
                for (int i = 0; i < target_rift && rift_count < MAX_RIFTS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    rifts[rift_count] = (NPCRift){.id=rift_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    rift_count++;
                }
            
                int target_mon = GALAXY_CREATE_OBJECT_MIN_MONSTER + (rand() % (GALAXY_CREATE_OBJECT_MAX_MONSTER - GALAXY_CREATE_OBJECT_MIN_MONSTER + 1));
                for (int i = 0; i < target_mon && mon_count < MAX_MONSTERS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    int m_type_idx = (rand()%100 < 50) ? 0 : 1;
                    int m_type = (m_type_idx == 0) ? 30 : 31;
                    monsters[mon_count] = (NPCMonster){.id=mon_count, .type=m_type, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .health=DMG_TORPEDO_MONSTER, .energy=DMG_TORPEDO_MONSTER, .active=1, .behavior_timer=0};
                    monster_type_counts[m_type_idx]++;
                    mon_count++;
                }

                int dyson_count = 0;
                int target_dyson = GALAXY_CREATE_OBJECT_MIN_DYSON + (rand() % (GALAXY_CREATE_OBJECT_MAX_DYSON - GALAXY_CREATE_OBJECT_MIN_DYSON + 1));
                for (int i = 0; i < target_dyson && dyson_count < MAX_DYSON; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    dysons[dyson_count] = (NPCDyson){.id=dyson_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    dyson_count++;
                }

                int hub_count = 0;
                int target_hub = GALAXY_CREATE_OBJECT_MIN_HUB + (rand() % (GALAXY_CREATE_OBJECT_MAX_HUB - GALAXY_CREATE_OBJECT_MIN_HUB + 1));
                for (int i = 0; i < target_hub && hub_count < MAX_HUBS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    hubs[hub_count] = (NPCHub){.id=hub_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    hub_count++;
                }

                int relic_count = 0;
                int target_relic = GALAXY_CREATE_OBJECT_MIN_RELIC + (rand() % (GALAXY_CREATE_OBJECT_MAX_RELIC - GALAXY_CREATE_OBJECT_MIN_RELIC + 1));
                for (int i = 0; i < target_relic && relic_count < MAX_RELICS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    relics[relic_count] = (NPCRelic){.id=relic_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    relic_count++;
                }

                int rupture_count = 0;
                int target_rupture = GALAXY_CREATE_OBJECT_MIN_RUPTURE + (rand() % (GALAXY_CREATE_OBJECT_MAX_RUPTURE - GALAXY_CREATE_OBJECT_MIN_RUPTURE + 1));
                for (int i = 0; i < target_rupture && rupture_count < MAX_RUPTURES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    ruptures[rupture_count] = (NPCRupture){.id=rupture_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    rupture_count++;
                }

                int satellite_count = 0;
                int target_satellite = GALAXY_CREATE_OBJECT_MIN_SATELLITE + (rand() % (GALAXY_CREATE_OBJECT_MAX_SATELLITE - GALAXY_CREATE_OBJECT_MIN_SATELLITE + 1));
                for (int i = 0; i < target_satellite && satellite_count < MAX_SATELLITES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    satellites[satellite_count] = (NPCSatellite){.id=satellite_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    satellite_count++;
                }

                int storm_count = 0;
                int target_storm = GALAXY_CREATE_OBJECT_MIN_STORM + (rand() % (GALAXY_CREATE_OBJECT_MAX_STORM - GALAXY_CREATE_OBJECT_MIN_STORM + 1));
                for (int i = 0; i < target_storm && storm_count < MAX_STORMS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    storms[storm_count] = (NPCStorm){.id=storm_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    storm_count++;
                }

                int artifact_count = 0;
                int target_artifact = GALAXY_CREATE_OBJECT_MIN_ARTIFACT + (rand() % (GALAXY_CREATE_OBJECT_MAX_ARTIFACT - GALAXY_CREATE_OBJECT_MIN_ARTIFACT + 1));
                for (int i = 0; i < target_artifact && artifact_count < MAX_ARTIFACTS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    artifacts[artifact_count] = (NPCArtifact){.id=artifact_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    artifact_count++;
                }

                int warp_gate_count = 0;
                int target_warp_gate = GALAXY_CREATE_OBJECT_MIN_WARP_GATE + (rand() % (GALAXY_CREATE_OBJECT_MAX_WARP_GATE - GALAXY_CREATE_OBJECT_MIN_WARP_GATE + 1));
                for (int i = 0; i < target_warp_gate && warp_gate_count < MAX_WARP_GATES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    warp_gates[warp_gate_count] = (NPCWarpGate){.id=warp_gate_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    warp_gate_count++;
                }

                int neutron_star_count = 0;
                int target_neutron_star = GALAXY_CREATE_OBJECT_MIN_NEUTRON_STAR + (rand() % (GALAXY_CREATE_OBJECT_MAX_NEUTRON_STAR - GALAXY_CREATE_OBJECT_MIN_NEUTRON_STAR + 1));
                for (int i = 0; i < target_neutron_star && neutron_star_count < MAX_NEUTRON_STARS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    neutron_stars[neutron_star_count] = (NPCNeutronStar){.id=neutron_star_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    neutron_star_count++;
                }

                int mega_struct_count = 0;
                int target_mega_struct = GALAXY_CREATE_OBJECT_MIN_MEGA_STRUCT + (rand() % (GALAXY_CREATE_OBJECT_MAX_MEGA_STRUCT - GALAXY_CREATE_OBJECT_MIN_MEGA_STRUCT + 1));
                for (int i = 0; i < target_mega_struct && mega_struct_count < MAX_MEGA_STRUCTS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    mega_structs[mega_struct_count] = (NPCMegaStructure){.id=mega_struct_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    mega_struct_count++;
                }

                int dark_cloud_count = 0;
                int target_dark_cloud = GALAXY_CREATE_OBJECT_MIN_DARK_CLOUD + (rand() % (GALAXY_CREATE_OBJECT_MAX_DARK_CLOUD - GALAXY_CREATE_OBJECT_MIN_DARK_CLOUD + 1));
                for (int i = 0; i < target_dark_cloud && dark_cloud_count < MAX_DARK_CLOUDS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    dark_clouds[dark_cloud_count] = (NPCDarkCloud){.id=dark_cloud_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    dark_cloud_count++;
                }

                int singularity_count = 0;
                int target_singularity = GALAXY_CREATE_OBJECT_MIN_SINGULARITY + (rand() % (GALAXY_CREATE_OBJECT_MAX_SINGULARITY - GALAXY_CREATE_OBJECT_MIN_SINGULARITY + 1));
                for (int i = 0; i < target_singularity && singularity_count < MAX_SINGULARITIES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    singularities[singularity_count] = (NPCSingularity){.id=singularity_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    singularity_count++;
                }

                int plasma_storm_count = 0;
                int target_plasma_storm = GALAXY_CREATE_OBJECT_MIN_PLASMA_STORM + (rand() % (GALAXY_CREATE_OBJECT_MAX_PLASMA_STORM - GALAXY_CREATE_OBJECT_MIN_PLASMA_STORM + 1));
                for (int i = 0; i < target_plasma_storm && plasma_storm_count < MAX_PLASMA_STORMS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    plasma_storms[plasma_storm_count] = (NPCPlasmaStorm){.id=plasma_storm_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    plasma_storm_count++;
                }

                int orbital_ring_count = 0;
                int target_orbital_ring = GALAXY_CREATE_OBJECT_MIN_ORBITAL_RING + (rand() % (GALAXY_CREATE_OBJECT_MAX_ORBITAL_RING - GALAXY_CREATE_OBJECT_MIN_ORBITAL_RING + 1));
                for (int i = 0; i < target_orbital_ring && orbital_ring_count < MAX_ORBITAL_RINGS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    orbital_rings[orbital_ring_count] = (NPCOrbitalRing){.id=orbital_ring_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    orbital_ring_count++;
                }

                int time_anomaly_count = 0;
                int target_time_anomaly = GALAXY_CREATE_OBJECT_MIN_TIME_ANOMALY + (rand() % (GALAXY_CREATE_OBJECT_MAX_TIME_ANOMALY - GALAXY_CREATE_OBJECT_MIN_TIME_ANOMALY + 1));
                for (int i = 0; i < target_time_anomaly && time_anomaly_count < MAX_TIME_ANOMALIES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    time_anomalies[time_anomaly_count] = (NPCTimeAnomaly){.id=time_anomaly_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    time_anomaly_count++;
                }

                int void_crystal_count = 0;
                int target_void_crystal = GALAXY_CREATE_OBJECT_MIN_VOID_CRYSTAL + (rand() % (GALAXY_CREATE_OBJECT_MAX_VOID_CRYSTAL - GALAXY_CREATE_OBJECT_MIN_VOID_CRYSTAL + 1));
                for (int i = 0; i < target_void_crystal && void_crystal_count < MAX_VOID_CRYSTALS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    void_crystals[void_crystal_count] = (NPCVoidCrystal){.id=void_crystal_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    void_crystal_count++;
                }

                rebuild_spatial_index();
    refresh_lrs_grid();

    printf("\n%s .--- GALAXY GENERATION COMPLETED: ASTROMETRICS REPORT ----------.%s\n", B_CYAN, RESET);
    printf("%s | %s 🏚️ TOTAL WRECKS:  %s%-5d %s| %s 🛰️ Starbases:          %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, der_count, B_CYAN, B_WHITE, B_GREEN, b_count, B_CYAN);
    printf("%s | %s 🏛️ DYSON FRAGS:   %s%-5d %s| %s 🏢 TRADING HUBS:      %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, dyson_count, B_CYAN, B_WHITE, B_GREEN, hub_count, B_CYAN);
    printf("%s | %s 🏺 ANCIENT RELICS: %s%-5d %s| %s ⚡ ION STORMS:        %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, relic_count, B_CYAN, B_WHITE, B_GREEN, storm_count, B_CYAN);
    printf("%s | %s 🚀 TOTAL VESSELS: %s%-5d %s| %s 🪐 Planets:            %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, n_count, B_CYAN, B_WHITE, B_GREEN, p_count, B_CYAN);
    printf("%s | %s 🌋 SUBSPACE RUPT: %s%-5d %s| %s 📡 SATELLITES:        %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, rupture_count, B_CYAN, B_WHITE, B_GREEN, satellite_count, B_CYAN);
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
    printf("%s | %s [ QUASAR CLASSIFICATION ]                                    %s|\n", B_CYAN, B_YELLOW, B_CYAN);
    const char* q_classes_short[] = {"Radio-loud", "Radio-quiet", "BAL", "Type 2", "Red", "OVV", "Weak-Em"};
    for(int k=0; k<7; k+=2) {
        char q2_str[64] = "";
        if (k+1 < 7) sprintf(q2_str, "%s %-12s: %s%-4d", B_WHITE, q_classes_short[k+1], B_GREEN, quasar_type_counts[k+1]);
        printf("%s | %s %-12s: %s%-4d %s| %s %-28s %s|\n", B_CYAN, B_WHITE, q_classes_short[k], B_GREEN, quasar_type_counts[k], B_CYAN, B_CYAN, q2_str, B_CYAN);
    }

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
    printf("%s | %s [ ANCIENT STRUCTURES & ANOMALIES ]                         %s|\n", B_CYAN, B_YELLOW, B_CYAN);
    printf("%s | %s Alien Artifacts: %s%-4d %s| %s Warp Gates:      %s%-4d %s|\n", B_CYAN, B_WHITE, B_GREEN, artifact_count, B_CYAN, B_WHITE, B_GREEN, warp_gate_count, B_CYAN);
    printf("%s | %s Neutron Stars:   %s%-4d %s| %s Mega Structures: %s%-4d %s|\n", B_CYAN, B_WHITE, B_GREEN, neutron_star_count, B_CYAN, B_WHITE, B_GREEN, mega_struct_count, B_CYAN);
    printf("%s | %s Dark Matter Clds:%s%-4d %s| %s Q-Singularities: %s%-4d %s|\n", B_CYAN, B_WHITE, B_GREEN, dark_cloud_count, B_CYAN, B_WHITE, B_GREEN, singularity_count, B_CYAN);
    printf("%s | %s Plasma Storms:  %s%-4d %s| %s Orbital Rings:   %s%-4d %s|\n", B_CYAN, B_WHITE, B_GREEN, plasma_storm_count, B_CYAN, B_WHITE, B_GREEN, orbital_ring_count, B_CYAN);
    printf("%s | %s Time Anomalies: %s%-4d %s| %s Void Crystals:   %s%-4d %s|\n", B_CYAN, B_WHITE, B_GREEN, time_anomaly_count, B_CYAN, B_WHITE, B_GREEN, void_crystal_count, B_CYAN);

    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    printf("%s | %s ☀️ Total Stars:        %s%-5d %s| %s 🕳️ Black Holes:        %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, s_count, B_CYAN, B_WHITE, B_GREEN, bh_count, B_CYAN);
    printf("%s | %s ☁️ Total Nebulas:      %s%-5d %s| %s 🌟 Total Pulsars:      %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, neb_count, B_CYAN, B_WHITE, B_GREEN, pul_count, B_CYAN);
    printf("%s | %s 🔯 Total Quasars:      %s%-5d %s| %s ☄️ Comets:             %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, qsr_count, B_CYAN, B_WHITE, B_GREEN, com_count, B_CYAN);
    printf("%s | %s 💎 Asteroids:          %s%-5d %s| %s 💣 Mines:              %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, ast_count, B_CYAN, B_WHITE, B_GREEN, mine_count, B_CYAN);
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
