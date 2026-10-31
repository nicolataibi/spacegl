/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * galaxy_io.c — galaxy persistence: asynchronous save (snapshot under the lock, write on the pool), save requests from other threads, load of galaxy.dat.
 * Split from galaxy.c (2026.10.31.16) without code change.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <stdatomic.h>
#include <pthread.h>
#include "server_internal.h"
#include "object_types.h"
#include "quad_index.h"
#include "moon_orbits.h"
#include "ui.h"
#include "galaxy_internal.h"

static atomic_bool g_is_saving = false;

static void save_task(void* arg) {
    SpaceGLGame *master_copy = (SpaceGLGame*)arg;
    char galaxy_path[1088];
    server_data_path(galaxy_path, sizeof(galaxy_path), "galaxy.dat");
    FILE *f = fopen(galaxy_path, "wb");
    if (!f) { 
        slog("ERROR: Failed to open %s for writing: %s\n", galaxy_path, strerror(errno));
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
    fwrite(subspace_anomalies, sizeof(NPCSubspaceAnomaly), MAX_SUBSPACE_ANOMALIES, f);

    fwrite(diffuse_nebulae, sizeof(NPCDiffuseNebula), MAX_DIFFUSE_NEBULAE, f);
    fwrite(dark_nebulae, sizeof(NPCDarkNebula), MAX_DARK_NEBULAE, f);
    fwrite(planetary_nebulae, sizeof(NPCPlanetaryNebula), MAX_PLANETARY_NEBULAE, f);
    fwrite(snrs, sizeof(NPCSNR), MAX_SNR, f);
    fwrite(gmcs, sizeof(NPCGMC), MAX_GMC, f);
    fwrite(interstellar_filaments, sizeof(NPCInterstellarFilament), MAX_INTERSTELLAR_FILAMENTS, f);
    fwrite(interstellar_bubbles, sizeof(NPCInterstellarBubble), MAX_INTERSTELLAR_BUBBLES, f);
    fwrite(bok_globules, sizeof(NPCBokGlobule), MAX_BOK_GLOBULES, f);
    fwrite(clump_cores, sizeof(NPCClumpCore), MAX_CLUMP_CORES, f);
    fwrite(accretion_disks, sizeof(NPCAccretionDisk), MAX_ACCRETION_DISKS, f);
    fwrite(relativistic_jets, sizeof(NPCRelativisticJet), MAX_RELATIVISTIC_JETS, f);
    fwrite(shock_waves, sizeof(NPCShockWave), MAX_SHOCK_WAVES, f);
    fwrite(stellar_bow_shocks, sizeof(NPCStellarBowShock), MAX_STELLAR_BOW_SHOCKS, f);
    fwrite(cosmic_voids, sizeof(NPCCosmicVoid), MAX_COSMIC_VOIDS, f);
    fwrite(cosmic_filaments, sizeof(NPCCosmicFilament), MAX_COSMIC_FILAMENTS, f);
    fwrite(event_horizons, sizeof(NPCEventHorizon), MAX_EVENT_HORIZONS, f);
    fwrite(kilonovae, sizeof(NPCKilonova), MAX_KILONOVAE, f);
    fwrite(grav_lenses, sizeof(NPCGravLens), MAX_GRAV_LENSES, f);
    fwrite(grbs, sizeof(NPCGRB), MAX_GRB, f);
    fwrite(grav_waves, sizeof(NPCGravWave), MAX_GRAV_WAVES, f);
    fwrite(protoplanetary_disks, sizeof(NPCProtoplanetaryDisk), MAX_PROTOPLANETARY_DISKS, f);
    fwrite(debris_disks, sizeof(NPCDebrisDisk), MAX_DEBRIS_DISKS, f);
    fwrite(planetesimals, sizeof(NPCPlanetesimal), MAX_PLANETESIMALS, f);
    fwrite(rogue_planets, sizeof(NPCRoguePlanet), MAX_ROGUE_PLANETS, f);
    fwrite(brown_dwarfs, sizeof(NPCBrownDwarf), MAX_BROWN_DWARFS, f);
    fwrite(isos, sizeof(NPCISO), MAX_ISO, f);
    fwrite(mag_reconns, sizeof(NPCMagReconn), MAX_MAG_RECONN, f);
    fwrite(current_sheets, sizeof(NPCCurrentSheet), MAX_CURRENT_SHEETS, f);
    fwrite(heliospheres, sizeof(NPCHeliosphere), MAX_HELIOSPHERES, f);
    fwrite(term_shocks, sizeof(NPCTermShock), MAX_TERM_SHOCKS, f);
    fwrite(magnetospheres, sizeof(NPCMagnetosphere), MAX_MAGNETOSPHERES, f);
    fwrite(cosmic_strings, sizeof(NPCCosmicString), MAX_COSMIC_STRINGS, f);
    fwrite(domain_walls, sizeof(NPCDomainWall), MAX_DOMAIN_WALLS, f);
    fwrite(dm_halos, sizeof(NPCDMHalo), MAX_DM_HALO, f);
    fwrite(igms, sizeof(NPCIGM), MAX_IGM, f);
    fwrite(cgms, sizeof(NPCCGM), MAX_CGM, f);
    fwrite(lyman_alphas, sizeof(NPCLymanAlpha), MAX_LYMAN_ALPHA, f);
    fwrite(cmbs, sizeof(NPCCMB), MAX_CMB, f);
    fwrite(&sys_star_count, sizeof(int), 1, f);
    fwrite(sys_stars, sizeof(NPCSysStar), (size_t)sys_star_count, f);
    fwrite(sys_planets, sizeof(NPCSysPlanet), (size_t)sys_star_count * SYS_PLANETS_PER_STAR, f);
    fwrite(sys_moons, sizeof(NPCSysMoon), (size_t)sys_star_count * SYS_PLANETS_PER_STAR * SYS_MOONS_PER_PLANET, f);
    fwrite(&cosmic_feature_count, sizeof(int), 1, f);
    fwrite(cosmic_features, sizeof(NPCCosmicFeature), MAX_COSMIC_FEATURES, f);
    fwrite(players, sizeof(ConnectedPlayer), MAX_CLIENTS, f);
    fclose(f);
    
    time_t now = time(NULL);
    char *ts = ctime(&now);
    if (ts) ts[strlen(ts)-1] = '\0';
    slog("--- [%s] GALAXY SAVED ASYNCHRONOUSLY (POOL) ---\n", ts);
    
    free(master_copy);
    atomic_store(&g_is_saving, false);
}

void save_galaxy_async() {
    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_is_saving, &expected, true)) return;
    
    SpaceGLGame *copy = malloc(sizeof(SpaceGLGame));
    if (!copy) { atomic_store(&g_is_saving, false); return; }
    memcpy(copy, &spacegl_master, sizeof(SpaceGLGame));
    
    save_task(copy);
}

/* Who saves (2026.10.31.13, ThreadSanitizer). The save reads players[] and
 * every pool while the caller holds game_mutex, but the game thread writes
 * each player's send state (last_sent_state, full_update_timer, ...) in
 * send_pending_updates(), outside the lock: a save from another thread
 * (a departure on the epoll thread, the legacy loop, a command) raced with
 * it. While the game thread runs, such a save is a request that the game
 * thread carries out at its next tick, inside the lock, where nothing else
 * writes players[]; the game thread itself, boot and shutdown save at once. */
static atomic_bool g_save_requested = false;
static atomic_bool g_game_thread_set = false;
static pthread_t g_game_thread;

void save_galaxy_set_game_thread(bool running) {
    if (running) g_game_thread = pthread_self();
    atomic_store(&g_game_thread_set, running);
}

bool save_galaxy_take_request(void) {
    return atomic_exchange(&g_save_requested, false);
}

void save_galaxy() {
    if (atomic_load(&g_game_thread_set) && !pthread_equal(pthread_self(), g_game_thread)) {
        atomic_store(&g_save_requested, true);
        return;
    }
    save_galaxy_async();
}

int load_galaxy() {
    char galaxy_path[1088];
    server_data_path(galaxy_path, sizeof(galaxy_path), "galaxy.dat");
    FILE *f = fopen(galaxy_path, "rb");
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
    CHECK_READ(subspace_anomalies, sizeof(NPCSubspaceAnomaly), MAX_SUBSPACE_ANOMALIES, f);

    CHECK_READ(diffuse_nebulae, sizeof(NPCDiffuseNebula), MAX_DIFFUSE_NEBULAE, f);
    CHECK_READ(dark_nebulae, sizeof(NPCDarkNebula), MAX_DARK_NEBULAE, f);
    CHECK_READ(planetary_nebulae, sizeof(NPCPlanetaryNebula), MAX_PLANETARY_NEBULAE, f);
    CHECK_READ(snrs, sizeof(NPCSNR), MAX_SNR, f);
    CHECK_READ(gmcs, sizeof(NPCGMC), MAX_GMC, f);
    CHECK_READ(interstellar_filaments, sizeof(NPCInterstellarFilament), MAX_INTERSTELLAR_FILAMENTS, f);
    CHECK_READ(interstellar_bubbles, sizeof(NPCInterstellarBubble), MAX_INTERSTELLAR_BUBBLES, f);
    CHECK_READ(bok_globules, sizeof(NPCBokGlobule), MAX_BOK_GLOBULES, f);
    CHECK_READ(clump_cores, sizeof(NPCClumpCore), MAX_CLUMP_CORES, f);
    CHECK_READ(accretion_disks, sizeof(NPCAccretionDisk), MAX_ACCRETION_DISKS, f);
    CHECK_READ(relativistic_jets, sizeof(NPCRelativisticJet), MAX_RELATIVISTIC_JETS, f);
    CHECK_READ(shock_waves, sizeof(NPCShockWave), MAX_SHOCK_WAVES, f);
    CHECK_READ(stellar_bow_shocks, sizeof(NPCStellarBowShock), MAX_STELLAR_BOW_SHOCKS, f);
    CHECK_READ(cosmic_voids, sizeof(NPCCosmicVoid), MAX_COSMIC_VOIDS, f);
    CHECK_READ(cosmic_filaments, sizeof(NPCCosmicFilament), MAX_COSMIC_FILAMENTS, f);
    CHECK_READ(event_horizons, sizeof(NPCEventHorizon), MAX_EVENT_HORIZONS, f);
    CHECK_READ(kilonovae, sizeof(NPCKilonova), MAX_KILONOVAE, f);
    CHECK_READ(grav_lenses, sizeof(NPCGravLens), MAX_GRAV_LENSES, f);
    CHECK_READ(grbs, sizeof(NPCGRB), MAX_GRB, f);
    CHECK_READ(grav_waves, sizeof(NPCGravWave), MAX_GRAV_WAVES, f);
    CHECK_READ(protoplanetary_disks, sizeof(NPCProtoplanetaryDisk), MAX_PROTOPLANETARY_DISKS, f);
    CHECK_READ(debris_disks, sizeof(NPCDebrisDisk), MAX_DEBRIS_DISKS, f);
    CHECK_READ(planetesimals, sizeof(NPCPlanetesimal), MAX_PLANETESIMALS, f);
    CHECK_READ(rogue_planets, sizeof(NPCRoguePlanet), MAX_ROGUE_PLANETS, f);
    CHECK_READ(brown_dwarfs, sizeof(NPCBrownDwarf), MAX_BROWN_DWARFS, f);
    CHECK_READ(isos, sizeof(NPCISO), MAX_ISO, f);
    CHECK_READ(mag_reconns, sizeof(NPCMagReconn), MAX_MAG_RECONN, f);
    CHECK_READ(current_sheets, sizeof(NPCCurrentSheet), MAX_CURRENT_SHEETS, f);
    CHECK_READ(heliospheres, sizeof(NPCHeliosphere), MAX_HELIOSPHERES, f);
    CHECK_READ(term_shocks, sizeof(NPCTermShock), MAX_TERM_SHOCKS, f);
    CHECK_READ(magnetospheres, sizeof(NPCMagnetosphere), MAX_MAGNETOSPHERES, f);
    CHECK_READ(cosmic_strings, sizeof(NPCCosmicString), MAX_COSMIC_STRINGS, f);
    CHECK_READ(domain_walls, sizeof(NPCDomainWall), MAX_DOMAIN_WALLS, f);
    CHECK_READ(dm_halos, sizeof(NPCDMHalo), MAX_DM_HALO, f);
    CHECK_READ(igms, sizeof(NPCIGM), MAX_IGM, f);
    CHECK_READ(cgms, sizeof(NPCCGM), MAX_CGM, f);
    CHECK_READ(lyman_alphas, sizeof(NPCLymanAlpha), MAX_LYMAN_ALPHA, f);
    CHECK_READ(cmbs, sizeof(NPCCMB), MAX_CMB, f);
    memset(sys_stars, 0, sizeof(sys_stars));
    memset(sys_planets, 0, sizeof(sys_planets));
    memset(sys_moons, 0, sizeof(sys_moons));
    CHECK_READ(&sys_star_count, sizeof(int), 1, f);
    if (sys_star_count < 0 || sys_star_count > SYS_MAX_STARS) { printf("--- CORRUPT STAR SYSTEM COUNT ---\n"); sys_star_count = 0; fclose(f); return 0; }
    CHECK_READ(sys_stars, sizeof(NPCSysStar), (size_t)sys_star_count, f);
    CHECK_READ(sys_planets, sizeof(NPCSysPlanet), (size_t)sys_star_count * SYS_PLANETS_PER_STAR, f);
    CHECK_READ(sys_moons, sizeof(NPCSysMoon), (size_t)sys_star_count * SYS_PLANETS_PER_STAR * SYS_MOONS_PER_PLANET, f);
    { double t_ = sys_clock_s(); for (int s_ = 0; s_ < sys_star_count; s_++) sys_refresh_positions(s_, t_); }
    if (fread(&cosmic_feature_count, sizeof(int), 1, f) != 1) { cosmic_feature_count = 0; }
    /* Dynamically read cosmic_features handling legacy size (1024) and current size (MAX_COSMIC_FEATURES) */
    memset(cosmic_features, 0, sizeof(NPCCosmicFeature) * MAX_COSMIC_FEATURES);
    long feat_pos = ftell(f);
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, feat_pos, SEEK_SET);
    long remaining_bytes = file_size - feat_pos;
    long players_bytes = (long)sizeof(ConnectedPlayer) * MAX_CLIENTS;
    long available_feat_bytes = remaining_bytes - players_bytes;
    int feats_in_file = (available_feat_bytes > 0) ? (int)(available_feat_bytes / sizeof(NPCCosmicFeature)) : 0;
    if (feats_in_file > MAX_COSMIC_FEATURES) feats_in_file = MAX_COSMIC_FEATURES;
    if (feats_in_file > 0) {
        if (fread(cosmic_features, sizeof(NPCCosmicFeature), feats_in_file, f) != (size_t)feats_in_file) {
            memset(cosmic_features, 0, sizeof(NPCCosmicFeature) * MAX_COSMIC_FEATURES);
        }
    }
    CHECK_READ(players, sizeof(ConnectedPlayer), MAX_CLIENTS, f);
    fclose(f);

    /* galaxy.dat saved before the evolved stars existed: add them now (the
     * layout is unchanged, only the generic feature array gets new entries). */
    {
        int has_evolved = 0;
        for (int i = 0; i < MAX_COSMIC_FEATURES && !has_evolved; i++)
            if (cosmic_features[i].active && obj_type_is_evolved_star(cosmic_features[i].type)) has_evolved = 1;
        if (!has_evolved && cosmic_feature_count >= 0 && cosmic_feature_count <= MAX_COSMIC_FEATURES) {
            int before = cosmic_feature_count;
            spawn_evolved_stars();
            printf("[GALAXY] added %d evolved stars to the loaded galaxy\n", cosmic_feature_count - before);
        }
    }

    /* galaxy.dat saved before moons existed or with fewer moons than planets:
     * ensure that EVERY active planet gets at least one moon. */
    {
        int existing_moons = 0;
        for (int i = 0; i < MAX_COSMIC_FEATURES; i++)
            if (cosmic_features[i].active && obj_type_is_moon(cosmic_features[i].type)) existing_moons++;
        int active_planets = 0;
        for (int p = 0; p < MAX_PLANETS; p++)
            if (IS_Q_VALID(planets[p].q1, planets[p].q2, planets[p].q3) && planets[p].active) active_planets++;
        if (existing_moons < active_planets && cosmic_feature_count >= 0 && cosmic_feature_count <= MAX_COSMIC_FEATURES) {
            int before = cosmic_feature_count;
            spawn_moons();
            printf("[GALAXY] added %d moons to the loaded galaxy (total: %d for %d planets)\n",
                   cosmic_feature_count - before, existing_moons + (cosmic_feature_count - before), active_planets);
        }
    }
    /* Parent planet and orbit of every moon (deterministic assignment) */
    attach_moons_to_planets();

    /* galaxy.dat saved before protostellar objects existed: add them now */
    {
        int has_protostellar = 0;
        for (int i = 0; i < MAX_COSMIC_FEATURES && !has_protostellar; i++)
            if (cosmic_features[i].active && obj_type_is_protostellar(cosmic_features[i].type)) has_protostellar = 1;
        if (!has_protostellar && cosmic_feature_count >= 0 && cosmic_feature_count <= MAX_COSMIC_FEATURES) {
            int before = cosmic_feature_count;
            spawn_protostellar_objects();
            printf("[GALAXY] added %d protostellar objects to the loaded galaxy\n", cosmic_feature_count - before);
        }
    }

    /* galaxy.dat saved before extended moons existed: add them now */
    {
        int has_ext_moons = 0;
        for (int i = 0; i < MAX_COSMIC_FEATURES && !has_ext_moons; i++)
            if (cosmic_features[i].active && obj_type_is_moon_ext(cosmic_features[i].type)) has_ext_moons = 1;
        if (!has_ext_moons && cosmic_feature_count >= 0 && cosmic_feature_count < MAX_COSMIC_FEATURES) {
            int before = cosmic_feature_count;
            const char *moon_ext_names[OBJ_MOON_EXT_COUNT] = { OBJECT_MOON_EXT_NAME_LIST };
            for (int k = 0; k < OBJ_MOON_EXT_COUNT * 5; k++) {
                if (cosmic_feature_count >= MAX_COSMIC_FEATURES) break;
                int ext_idx = k % OBJ_MOON_EXT_COUNT;
                cosmic_features[cosmic_feature_count] = (NPCCosmicFeature){
                    .id = GALAXY_OBJECT_MIN_FEATURE + cosmic_feature_count,
                    .type = OBJ_MOON_EXT_FIRST + ext_idx,
                    .q1 = 1 + rand() % GALAXY_SIZE,
                    .q2 = 1 + rand() % GALAXY_SIZE,
                    .q3 = 1 + rand() % GALAXY_SIZE,
                    .x = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM,
                    .y = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM,
                    .z = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM,
                    .active = 1
                };
                strncpy(cosmic_features[cosmic_feature_count].name, moon_ext_names[ext_idx], 63);
                cosmic_features[cosmic_feature_count].name[63] = '\0';
                cosmic_feature_count++;
            }
            if (cosmic_feature_count > before) {
                printf("[GALAXY] added %d extended moons to the loaded galaxy\n", cosmic_feature_count - before);
                attach_moons_to_planets();
            }
        }
    }
    
    for(int i=0; i<MAX_CLIENTS; i++) {
        players[i].active = 0;
        players[i].socket = 0;
        /* Preserve the name for login recognition, but initialize the mutex */
        pthread_mutex_init(&players[i].socket_mutex, NULL);
    }
    
    printf("--- PERSISTENT GALAXY LOADED SUCCESSFULLY ---\n");
    
    int active_derelicts = 0;
    for (int i = 0; i < MAX_DERELICTS; i++) if (derelicts[i].active) active_derelicts++;
    
    int active_bases = 0;
    for (int i = 0; i < MAX_BASES; i++) if (bases[i].active) active_bases++;

    int active_planets = 0;
    for (int p = 0; p < MAX_PLANETS; p++)
        if (IS_Q_VALID(planets[p].q1, planets[p].q2, planets[p].q3) && planets[p].active) active_planets++;
    int active_stars = 0;
    for (int s = 0; s < MAX_STARS; s++)
        if (IS_Q_VALID(stars_data[s].q1, stars_data[s].q2, stars_data[s].q3) && stars_data[s].active) active_stars++;

    int moon_count = 0;
    int moon_type_counts[OBJ_MOON_COUNT] = {0};
    int moon_ext_type_counts[OBJ_MOON_EXT_COUNT] = {0};
    int evolved_star_count = 0;
    int evolved_type_counts[OBJ_EVOLVED_COUNT] = {0};
    int protostellar_count = 0;
    int proto_type_counts[OBJ_PROTOSTELLAR_COUNT] = {0};
    int exotic_feature_count = 0;
    for (int i = 0; i < cosmic_feature_count && i < MAX_COSMIC_FEATURES; i++) {
        if (!cosmic_features[i].active) continue;
        int t = cosmic_features[i].type;
        if (obj_type_is_moon_base(t)) {
            moon_count++;
            int idx = t - OBJ_MOON_FIRST;
            if (idx >= 0 && idx < OBJ_MOON_COUNT) moon_type_counts[idx]++;
        } else if (obj_type_is_moon_ext(t)) {
            moon_count++;
            int idx = t - OBJ_MOON_EXT_FIRST;
            if (idx >= 0 && idx < OBJ_MOON_EXT_COUNT) moon_ext_type_counts[idx]++;
        } else if (obj_type_is_evolved_star(t)) {
            evolved_star_count++;
            int idx = t - OBJ_EVOLVED_FIRST;
            if (idx >= 0 && idx < OBJ_EVOLVED_COUNT) evolved_type_counts[idx]++;
        } else if (obj_type_is_protostellar(t)) {
            protostellar_count++;
            int idx = t - OBJ_PROTOSTELLAR_FIRST;
            if (idx >= 0 && idx < OBJ_PROTOSTELLAR_COUNT) proto_type_counts[idx]++;
        } else if (obj_type_is_exotic(t)) {
            exotic_feature_count++;
        }
    }

    printf("\n%s .--- PERSISTENT GALAXY: ASTROMETRICS REPORT -----------------.%s\n", B_CYAN, RESET);
    printf("%s | %s 🏚️ TOTAL WRECKS:  %s%-5d %s| %s 🛰️ Starbases:          %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, active_derelicts, B_CYAN, B_WHITE, B_GREEN, active_bases, B_CYAN);
    printf("%s | %s 🪐 Planets:       %s%-5d %s| %s ✨ Stars:              %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, active_planets, B_CYAN, B_WHITE, B_GREEN, active_stars, B_CYAN);
    printf("%s | %s 🌙 NATURAL MOONS: %s%-5d %s| %s ⭐ EVOLVED STARS:      %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, moon_count, B_CYAN, B_WHITE, B_GREEN, evolved_star_count, B_CYAN);
    printf("%s | %s ☀️ PROTOSTELLAR:  %s%-5d %s| %s 🌌 EXOTIC FEAT.:       %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, protostellar_count, B_CYAN, B_WHITE, B_GREEN, exotic_feature_count, B_CYAN);
    printf("%s | %s 📦 TOTAL FEATURES: %s%-5d %s| %s ✨ STARS:              %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, cosmic_feature_count, B_CYAN, B_WHITE, B_GREEN, active_stars, B_CYAN);
    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    print_star_system_report();
    printf("%s | %s [ PROTOSTELLAR OBJECTS & YSO ]                                 %s|\n", B_CYAN, B_YELLOW, B_CYAN);
    const char *proto_names[OBJ_PROTOSTELLAR_COUNT] = { OBJECT_PROTOSTELLAR_NAME_LIST };
    for (int p = 0; p < OBJ_PROTOSTELLAR_COUNT; p += 2) {
        char p1[32], p2[32] = "";
        strncpy(p1, proto_names[p], 20); p1[20] = '\0';
        if (p + 1 < OBJ_PROTOSTELLAR_COUNT) { strncpy(p2, proto_names[p + 1], 20); p2[20] = '\0'; }
        printf("%s | %s %-20s: %s%-4d %s| %s %-20s: %s%-4d %s|\n",
               B_CYAN, B_WHITE, p1, B_GREEN, proto_type_counts[p], B_CYAN,
               B_WHITE, p2, B_GREEN, (p + 1 < OBJ_PROTOSTELLAR_COUNT) ? proto_type_counts[p + 1] : 0, B_CYAN);
    }
    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    printf("%s | %s [ NATURAL MOONS (IN ORBIT) ]                                   %s|\n", B_CYAN, B_YELLOW, B_CYAN);
    const char *moon_names[OBJ_MOON_COUNT] = { OBJECT_MOON_NAME_LIST };
    for (int m = 0; m < OBJ_MOON_COUNT; m += 2) {
        char m1[32], m2[32] = "";
        strncpy(m1, moon_names[m], 20); m1[20] = '\0';
        if (m + 1 < OBJ_MOON_COUNT) { strncpy(m2, moon_names[m + 1], 20); m2[20] = '\0'; }
        printf("%s | %s %-20s: %s%-4d %s| %s %-20s: %s%-4d %s|\n",
               B_CYAN, B_WHITE, m1, B_GREEN, moon_type_counts[m], B_CYAN,
               B_WHITE, m2, B_GREEN, (m + 1 < OBJ_MOON_COUNT) ? moon_type_counts[m + 1] : 0, B_CYAN);
    }
    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    printf("%s | %s [ EXTENDED NATURAL MOONS ]                                      %s|\n", B_CYAN, B_YELLOW, B_CYAN);
    const char *moon_ext_names[OBJ_MOON_EXT_COUNT] = { OBJECT_MOON_EXT_NAME_LIST };
    for (int m = 0; m < OBJ_MOON_EXT_COUNT; m += 2) {
        char m1[32], m2[32] = "";
        strncpy(m1, moon_ext_names[m], 20); m1[20] = '\0';
        if (m + 1 < OBJ_MOON_EXT_COUNT) { strncpy(m2, moon_ext_names[m + 1], 20); m2[20] = '\0'; }
        printf("%s | %s %-20s: %s%-4d %s| %s %-20s: %s%-4d %s|\n",
               B_CYAN, B_WHITE, m1, B_GREEN, moon_ext_type_counts[m], B_CYAN,
               B_WHITE, m2, B_GREEN, (m + 1 < OBJ_MOON_EXT_COUNT) ? moon_ext_type_counts[m + 1] : 0, B_CYAN);
    }
    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    printf("%s | %s [ EVOLVED STARS ]                                            %s|\n", B_CYAN, B_YELLOW, B_CYAN);
    const char *evolved_names[OBJ_EVOLVED_COUNT] = { OBJECT_EVOLVED_NAME_LIST };
    for (int e = 0; e < OBJ_EVOLVED_COUNT; e += 2) {
        char e1[32], e2[32] = "";
        strncpy(e1, evolved_names[e], 20); e1[20] = '\0';
        if (e + 1 < OBJ_EVOLVED_COUNT) { strncpy(e2, evolved_names[e + 1], 20); e2[20] = '\0'; }
        printf("%s | %s %-20s: %s%-4d %s| %s %-20s: %s%-4d %s|\n",
               B_CYAN, B_WHITE, e1, B_GREEN, evolved_type_counts[e], B_CYAN,
               B_WHITE, e2, B_GREEN, (e + 1 < OBJ_EVOLVED_COUNT) ? evolved_type_counts[e + 1] : 0, B_CYAN);
    }
    printf("%s '---------------------------------------------------------------'%s\n\n", B_CYAN, RESET);

    rebuild_spatial_index();
    return 1;
}
