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
NPCSysStar   sys_stars[SYS_MAX_STARS];
NPCSysPlanet sys_planets[SYS_MAX_PLANETS];
NPCSysMoon   sys_moons[SYS_MAX_MOONS];
int sys_star_count = 0;
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
NPCSubspaceAnomaly subspace_anomalies[MAX_SUBSPACE_ANOMALIES];

NPCDiffuseNebula diffuse_nebulae[MAX_DIFFUSE_NEBULAE];
NPCDarkNebula dark_nebulae[MAX_DARK_NEBULAE];
NPCPlanetaryNebula planetary_nebulae[MAX_PLANETARY_NEBULAE];
NPCSNR snrs[MAX_SNR];
NPCGMC gmcs[MAX_GMC];
NPCInterstellarFilament interstellar_filaments[MAX_INTERSTELLAR_FILAMENTS];
NPCInterstellarBubble interstellar_bubbles[MAX_INTERSTELLAR_BUBBLES];
NPCBokGlobule bok_globules[MAX_BOK_GLOBULES];
NPCClumpCore clump_cores[MAX_CLUMP_CORES];
NPCAccretionDisk accretion_disks[MAX_ACCRETION_DISKS];
NPCRelativisticJet relativistic_jets[MAX_RELATIVISTIC_JETS];
NPCShockWave shock_waves[MAX_SHOCK_WAVES];
NPCStellarBowShock stellar_bow_shocks[MAX_STELLAR_BOW_SHOCKS];
NPCCosmicVoid cosmic_voids[MAX_COSMIC_VOIDS];
NPCCosmicFilament cosmic_filaments[MAX_COSMIC_FILAMENTS];
NPCEventHorizon event_horizons[MAX_EVENT_HORIZONS];
NPCKilonova kilonovae[MAX_KILONOVAE];
NPCGravLens grav_lenses[MAX_GRAV_LENSES];
NPCGRB grbs[MAX_GRB];
NPCGravWave grav_waves[MAX_GRAV_WAVES];
NPCProtoplanetaryDisk protoplanetary_disks[MAX_PROTOPLANETARY_DISKS];
NPCDebrisDisk debris_disks[MAX_DEBRIS_DISKS];
NPCPlanetesimal planetesimals[MAX_PLANETESIMALS];
NPCRoguePlanet rogue_planets[MAX_ROGUE_PLANETS];
NPCBrownDwarf brown_dwarfs[MAX_BROWN_DWARFS];
NPCISO isos[MAX_ISO];
NPCMagReconn mag_reconns[MAX_MAG_RECONN];
NPCCurrentSheet current_sheets[MAX_CURRENT_SHEETS];
NPCHeliosphere heliospheres[MAX_HELIOSPHERES];
NPCTermShock term_shocks[MAX_TERM_SHOCKS];
NPCMagnetosphere magnetospheres[MAX_MAGNETOSPHERES];
NPCCosmicString cosmic_strings[MAX_COSMIC_STRINGS];
NPCDomainWall domain_walls[MAX_DOMAIN_WALLS];
NPCDMHalo dm_halos[MAX_DM_HALO];
NPCIGM igms[MAX_IGM];
NPCCGM cgms[MAX_CGM];
NPCLymanAlpha lyman_alphas[MAX_LYMAN_ALPHA];
NPCCMB cmbs[MAX_CMB];

NPCCosmicFeature cosmic_features[MAX_COSMIC_FEATURES];
int cosmic_feature_count = 0;

PlayerTorpedo players_torpedoes[MAX_GLOBAL_TORPEDOES];
ConnectedPlayer players[MAX_CLIENTS];
SpaceGLGame spacegl_master;


/* Evolved stars (red giants .. pulsating white dwarfs): 17 types stored in the
 * generic cosmic_features[] array after the exotic objects, type =
 * OBJ_EVOLVED_FIRST + index, GALAXY_CREATE_EVOLVED_STAR_PER_TYPE instances of
 * each. Used by the galaxy generator and, for a galaxy.dat saved before these
 * types existed (same file layout, no evolved stars in it), by load_galaxy(). */
static_assert(MAX_COSMIC_FEATURES >= OBJ_EXOTIC_COUNT * GALAXY_CREATE_EXOTIC_PER_TYPE +
                                     OBJ_EVOLVED_COUNT * GALAXY_CREATE_EVOLVED_STAR_PER_TYPE +
                                     OBJ_MOON_COUNT * GALAXY_CREATE_MOON_PER_TYPE +
                                     OBJ_PROTOSTELLAR_COUNT * GALAXY_CREATE_PROTOSTELLAR_PER_TYPE +
                                     OBJ_MOON_EXT_COUNT * 10,
              "cosmic_features[] too small for the exotic + evolved star + moon + protostellar instances");

void spawn_evolved_stars(void) {
    const char *evolved_names[OBJ_EVOLVED_COUNT] = { OBJECT_EVOLVED_NAME_LIST };
    for (int t = 0; t < OBJ_EVOLVED_COUNT; t++) {
        for (int i = 0; i < GALAXY_CREATE_EVOLVED_STAR_PER_TYPE; i++) {
            if (cosmic_feature_count >= MAX_COSMIC_FEATURES) return;
            cosmic_features[cosmic_feature_count] = (NPCCosmicFeature){
                .id = GALAXY_OBJECT_MIN_FEATURE + cosmic_feature_count,
                .type = OBJ_EVOLVED_FIRST + t,
                .q1 = 1 + rand() % GALAXY_SIZE,
                .q2 = 1 + rand() % GALAXY_SIZE,
                .q3 = 1 + rand() % GALAXY_SIZE,
                .x = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM,
                .y = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM,
                .z = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM,
                .active = 1
            };
            strncpy(cosmic_features[cosmic_feature_count].name, evolved_names[t], 63);
            cosmic_features[cosmic_feature_count].name[63] = '\0';
            cosmic_feature_count++;
        }
    }
}

/* Protostellar objects & YSO (Class 0 .. Accretion Columns): 16 types stored in the
 * generic cosmic_features[] array, type = OBJ_PROTOSTELLAR_FIRST + index,
 * GALAXY_CREATE_PROTOSTELLAR_PER_TYPE instances of each. Clustered near GMCs,
 * Dark Nebulae and Bok Globules when available, or seeded throughout the galaxy. */
void spawn_protostellar_objects(void) {
    const char *proto_names[OBJ_PROTOSTELLAR_COUNT] = { OBJECT_PROTOSTELLAR_NAME_LIST };
    for (int t = 0; t < OBJ_PROTOSTELLAR_COUNT; t++) {
        for (int i = 0; i < GALAXY_CREATE_PROTOSTELLAR_PER_TYPE; i++) {
            if (cosmic_feature_count >= MAX_COSMIC_FEATURES) return;
            int q1 = 1 + rand() % GALAXY_SIZE;
            int q2 = 1 + rand() % GALAXY_SIZE;
            int q3 = 1 + rand() % GALAXY_SIZE;
            double x = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
            double y = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
            double z = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;

            /* Cluster in star-forming nurseries (GMC, Dark Nebula) */
            int gmc_idx = (t * GALAXY_CREATE_PROTOSTELLAR_PER_TYPE + i) % MAX_GMC;
            int dark_idx = (t * GALAXY_CREATE_PROTOSTELLAR_PER_TYPE + i) % MAX_DARK_NEBULAE;
            if (gmcs[gmc_idx].active && IS_Q_VALID(gmcs[gmc_idx].q1, gmcs[gmc_idx].q2, gmcs[gmc_idx].q3)) {
                q1 = gmcs[gmc_idx].q1; q2 = gmcs[gmc_idx].q2; q3 = gmcs[gmc_idx].q3;
                x = gmcs[gmc_idx].x + ((rand() % 600) - 300);
                y = gmcs[gmc_idx].y + ((rand() % 600) - 300);
                z = gmcs[gmc_idx].z + ((rand() % 600) - 300);
            } else if (dark_nebulae[dark_idx].active && IS_Q_VALID(dark_nebulae[dark_idx].q1, dark_nebulae[dark_idx].q2, dark_nebulae[dark_idx].q3)) {
                q1 = dark_nebulae[dark_idx].q1; q2 = dark_nebulae[dark_idx].q2; q3 = dark_nebulae[dark_idx].q3;
                x = dark_nebulae[dark_idx].x + ((rand() % 400) - 200);
                y = dark_nebulae[dark_idx].y + ((rand() % 400) - 200);
                z = dark_nebulae[dark_idx].z + ((rand() % 400) - 200);
            }

            cosmic_features[cosmic_feature_count] = (NPCCosmicFeature){
                .id = GALAXY_OBJECT_MIN_FEATURE + cosmic_feature_count,
                .type = OBJ_PROTOSTELLAR_FIRST + t,
                .q1 = q1,
                .q2 = q2,
                .q3 = q3,
                .x = x,
                .y = y,
                .z = z,
                .active = 1
            };
            strncpy(cosmic_features[cosmic_feature_count].name, proto_names[t], 63);
            cosmic_features[cosmic_feature_count].name[63] = '\0';
            cosmic_feature_count++;
        }
    }
}

/* Moons (shepherd .. submoon): 10 types stored in the generic cosmic_features[]
 * array, type = OBJ_MOON_FIRST + index. Enough instances are created so that
 * every active planet has at least one moon, plus multi-moon systems up to
 * MOON_MAX_PER_PLANET. The position given here is only a placeholder:
 * attach_moons_to_planets() moves every moon onto an orbit around its planet. */
void spawn_moons(void) {
    const char *moon_names[OBJ_MOON_COUNT] = { OBJECT_MOON_NAME_LIST };
    const char *moon_ext_names[OBJ_MOON_EXT_COUNT] = { OBJECT_MOON_EXT_NAME_LIST };
    int total_types = OBJ_MOON_COUNT + OBJ_MOON_EXT_COUNT;
    int active_planets = 0;
    for (int p = 0; p < MAX_PLANETS; p++) {
        if (IS_Q_VALID(planets[p].q1, planets[p].q2, planets[p].q3) && planets[p].active)
            active_planets++;
    }
    int current_moons = 0;
    for (int i = 0; i < cosmic_feature_count && i < MAX_COSMIC_FEATURES; i++) {
        if (cosmic_features[i].active && obj_type_is_moon(cosmic_features[i].type))
            current_moons++;
    }
    int target_moons = active_planets + (active_planets / 5);
    if (target_moons < total_types * 45)
        target_moons = total_types * 45;

    int needed = target_moons - current_moons;
    for (int k = 0; k < needed; k++) {
        if (cosmic_feature_count >= MAX_COSMIC_FEATURES) break;
        int t = (current_moons + k) % total_types;
        int obj_type;
        const char *name;
        if (t < OBJ_MOON_COUNT) {
            obj_type = OBJ_MOON_FIRST + t;
            name = moon_names[t];
        } else {
            int ext_idx = t - OBJ_MOON_COUNT;
            obj_type = OBJ_MOON_EXT_FIRST + ext_idx;
            name = moon_ext_names[ext_idx];
        }
        cosmic_features[cosmic_feature_count] = (NPCCosmicFeature){
            .id = GALAXY_OBJECT_MIN_FEATURE + cosmic_feature_count,
            .type = obj_type,
            .q1 = 1 + rand() % GALAXY_SIZE,
            .q2 = 1 + rand() % GALAXY_SIZE,
            .q3 = 1 + rand() % GALAXY_SIZE,
            .x = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM,
            .y = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM,
            .z = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM,
            .active = 1
        };
        strncpy(cosmic_features[cosmic_feature_count].name, name, 63);
        cosmic_features[cosmic_feature_count].name[63] = '\0';
        cosmic_feature_count++;
    }
}

/* Moon orbits (runtime only, rebuilt from the galaxy after load/generation:
 * nothing of this is saved, see moon_orbits.h). Indexed like cosmic_features[]. */
typedef struct { int valid; int planet; MoonOrbit orbit; } MoonLink;
static MoonLink moon_links[MAX_COSMIC_FEATURES];

/* Give every moon a parent planet and its own orbit. Deterministic: the same
 * planets and the same moon slots always give the same result, so a restart
 * keeps every moon on the same planet. At most MOON_MAX_PER_PLANET per planet.
 * Moves each moon into its planet's quadrant, so it must run before
 * init_static_spatial_index(). A moon that cannot be placed is deactivated. */
void attach_moons_to_planets(void) {
    memset(moon_links, 0, sizeof(moon_links));

    static int elig[MAX_PLANETS];
    static int pcount[MAX_PLANETS];
    int ne = 0;
    for (int p = 0; p < MAX_PLANETS; p++) {
        if (!IS_Q_VALID(planets[p].q1, planets[p].q2, planets[p].q3)) continue;   /* empty slot */
        if (!planets[p].active) continue;
        /* Ensure planet is safely within bounds so moon orbits never cross quadrant boundaries */
        if (planets[p].x < MOON_ORBIT_MAX_RADIUS) planets[p].x = MOON_ORBIT_MAX_RADIUS;
        if (planets[p].x > QUADRANT_SIZE - MOON_ORBIT_MAX_RADIUS) planets[p].x = QUADRANT_SIZE - MOON_ORBIT_MAX_RADIUS;
        if (planets[p].y < MOON_ORBIT_MAX_RADIUS) planets[p].y = MOON_ORBIT_MAX_RADIUS;
        if (planets[p].y > QUADRANT_SIZE - MOON_ORBIT_MAX_RADIUS) planets[p].y = QUADRANT_SIZE - MOON_ORBIT_MAX_RADIUS;
        if (planets[p].z < MOON_ORBIT_MAX_RADIUS) planets[p].z = MOON_ORBIT_MAX_RADIUS;
        if (planets[p].z > QUADRANT_SIZE - MOON_ORBIT_MAX_RADIUS) planets[p].z = QUADRANT_SIZE - MOON_ORBIT_MAX_RADIUS;

        if (!moon_planet_eligible(planets[p].x, planets[p].y, planets[p].z, QUADRANT_SIZE)) continue;
        pcount[ne] = 0;
        elig[ne++] = p;
    }

    int attached = 0, dropped = 0;
    for (int i = 0; i < cosmic_feature_count && i < MAX_COSMIC_FEATURES; i++) {
        NPCCosmicFeature *f = &cosmic_features[i];
        if (!obj_type_is_moon(f->type)) continue;
        int j = moon_pick_planet((unsigned)f->id, ne, pcount);
        if (j < 0) { f->active = 0; dropped++; continue; }
        const NPCPlanet *pl = &planets[elig[j]];
        moon_orbit_make((unsigned)f->id, f->type, pcount[j]++, &moon_links[i].orbit);
        moon_links[i].valid = 1;
        moon_links[i].planet = elig[j];
        f->q1 = pl->q1; f->q2 = pl->q2; f->q3 = pl->q3;
        double off[3];
        moon_orbit_offset(&moon_links[i].orbit, 0.0, off);
        f->x = pl->x + off[0]; f->y = pl->y + off[1]; f->z = pl->z + off[2];
        attached++;
    }
    printf("[GALAXY] moons: %d in orbit around %d eligible planets, %d without a planet\n",
           attached, ne, dropped);
}

/* Move every moon along its orbit (called every tick). The angle comes from
 * the wall clock, so the orbits go on across restarts. A moon never leaves its
 * planet's quadrant (see moon_planet_eligible), so the per-quadrant index
 * needs no update. When the planet is destroyed (supernova), its moons go
 * with it. */
void update_moon_orbits(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    double t = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
    for (int i = 0; i < cosmic_feature_count && i < MAX_COSMIC_FEATURES; i++) {
        if (!moon_links[i].valid) continue;
        NPCCosmicFeature *f = &cosmic_features[i];
        if (!f->active) continue;
        const NPCPlanet *pl = &planets[moon_links[i].planet];
        if (!pl->active) { f->active = 0; continue; }
        double off[3];
        moon_orbit_offset(&moon_links[i].orbit, t, off);
        f->x = pl->x + off[0]; f->y = pl->y + off[1]; f->z = pl->z + off[2];
    }
}



/* "[ STAR SYSTEMS ]" block of the astrometrics report (generation and load). */
void print_star_system_report(void) {
    int planets_n = 0, moons_n = 0, hist[SYS_PLANETS_PER_STAR + 1] = {0}, moons_hist[SYS_MOONS_PER_PLANET + 1] = {0};
    for (int s = 0; s < sys_star_count; s++) {
        if (!sys_stars[s].active) continue;
        int np = 0;
        for (int k = 0; k < SYS_PLANETS_PER_STAR; k++) {
            const NPCSysPlanet *pl = &sys_planets[sys_planet_slot(s, k)];
            if (!pl->active) continue;
            np++; planets_n++; moons_n += pl->n_moons;
            if (pl->n_moons >= 0 && pl->n_moons <= SYS_MOONS_PER_PLANET) moons_hist[pl->n_moons]++;
        }
        hist[np > SYS_PLANETS_PER_STAR ? SYS_PLANETS_PER_STAR : np]++;
    }
    printf("%s | %s [ STAR SYSTEMS: 1 STAR, UP TO 4 PLANETS, UP TO 4 MOONS EACH ]      %s|\n", B_CYAN, B_YELLOW, B_CYAN);
    printf("%s | %s 🌟 SYSTEMS:      %s%-5d %s| %s 🪐 SYS PLANETS:      %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, sys_star_count, B_CYAN, B_WHITE, B_GREEN, planets_n, B_CYAN);
    printf("%s | %s 🌙 SYS MOONS:     %s%-5d %s| %s 📍 QUADRANTS W/ SYS: %s%4.1f%% %s|\n", B_CYAN, B_WHITE, B_GREEN, moons_n, B_CYAN, B_WHITE, B_GREEN,
           100.0 * sys_star_count / (double)(GALAXY_SIZE * GALAXY_SIZE * GALAXY_SIZE), B_CYAN);
    printf("%s | %s PLANETS/SYSTEM 1-4: %s%d / %d / %d / %d %s| %s MOONS/PLANET 0-4: %s%d/%d/%d/%d/%d %s|\n", B_CYAN, B_WHITE, B_GREEN,
           hist[1], hist[2], hist[3], hist[4], B_CYAN, B_WHITE, B_GREEN, moons_hist[0], moons_hist[1], moons_hist[2], moons_hist[3], moons_hist[4], B_CYAN);
    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
}

/* ===================== Star systems (include/star_system.h) =====================
 * One system per EMPTY quadrant. "Empty" is decided from the object arrays, not
 * from the spatial index (which is built later, by init_static_spatial_index()):
 * every static type of the quad_index table is scanned (so a type added to the
 * table is never forgotten, as the quasar once was) plus the dynamic arrays. */
#define SYS_OCC_MARK(a, b, c) do { int a_ = (a), b_ = (b), c_ = (c); \
    if (a_ >= 1 && a_ <= GALAXY_SIZE && b_ >= 1 && b_ <= GALAXY_SIZE && c_ >= 1 && c_ <= GALAXY_SIZE) \
        occ[a_][b_][c_] = 1; } while (0)
#define SYS_OCC_ARRAY(ARR, N) do { for (int i_ = 0; i_ < (N); i_++) \
    if (ARR[i_].active) SYS_OCC_MARK(ARR[i_].q1, ARR[i_].q2, ARR[i_].q3); } while (0)

void spawn_star_systems(void) {
    static unsigned char occ[GALAXY_SIZE + 1][GALAXY_SIZE + 1][GALAXY_SIZE + 1];
    memset(occ, 0, sizeof(occ));
    memset(sys_stars, 0, sizeof(sys_stars));
    memset(sys_planets, 0, sizeof(sys_planets));
    memset(sys_moons, 0, sizeof(sys_moons));
    sys_star_count = 0;

    /* static types: the whole quad_index table */
    int nt = quad_index_static_type_count();
    for (int d = 0; d < nt; d++) {
        const QuadStaticTypeInfo *ti = quad_index_static_type_info(d);
        for (int i = 0; i < ti->total; i++) {
            const char *e = (const char *)ti->objects + (size_t)i * ti->obj_size;
            if (!*(const int *)(e + ti->off_active)) continue;
            SYS_OCC_MARK(*(const int *)(e + ti->off_q1), *(const int *)(e + ti->off_q2), *(const int *)(e + ti->off_q3));
        }
    }
    /* dynamic types (the ones rebuild_spatial_index() handles per tick) */
    SYS_OCC_ARRAY(npcs, MAX_NPC);
    SYS_OCC_ARRAY(comets, MAX_COMETS);
    SYS_OCC_ARRAY(asteroids, MAX_ASTEROIDS);
    SYS_OCC_ARRAY(derelicts, MAX_DERELICTS);
    SYS_OCC_ARRAY(mines, MAX_MINES);
    SYS_OCC_ARRAY(buoys, MAX_BUOYS);
    SYS_OCC_ARRAY(platforms, MAX_PLATFORMS);
    SYS_OCC_ARRAY(rifts, MAX_RIFTS);
    SYS_OCC_ARRAY(monsters, MAX_MONSTERS);

    /* candidates: every empty quadrant, then a partial Fisher-Yates shuffle */
    int cap = GALAXY_SIZE * GALAXY_SIZE * GALAXY_SIZE;
    int *cand = (int *)malloc(sizeof(int) * (size_t)cap);
    if (!cand) return;
    int nc = 0;
    for (int a = 1; a <= GALAXY_SIZE; a++)
        for (int b = 1; b <= GALAXY_SIZE; b++)
            for (int c = 1; c <= GALAXY_SIZE; c++)
                if (!occ[a][b][c]) cand[nc++] = (a << 16) | (b << 8) | c;
    int want = GALAXY_CREATE_SYSTEMS < SYS_MAX_STARS ? GALAXY_CREATE_SYSTEMS : SYS_MAX_STARS;
    if (want > nc) want = nc;
    for (int i = 0; i < want; i++) {
        int j = i + rand() % (nc - i);
        int tmp = cand[i]; cand[i] = cand[j]; cand[j] = tmp;
    }

    double now = sys_clock_s();
    int planets_n = 0, moons_n = 0, trimmed = 0;
    for (int s = 0; s < want; s++) {
        int q1 = (cand[s] >> 16) & 0xFF, q2 = (cand[s] >> 8) & 0xFF, q3 = cand[s] & 0xFF;
        unsigned seed = ((unsigned)rand() << 16) ^ (unsigned)rand() ^ ((unsigned)s * 2654435761u);
        SysLayout L;
        if (!sys_layout_make(seed, (double)QUADRANT_SIZE, &L)) continue;
        double sp[3];
        sys_star_position(seed, (double)QUADRANT_SIZE, L.r_sys, sp);
        NPCSysStar *st = &sys_stars[s];
        *st = (NPCSysStar){ .id = GALAXY_OBJECT_MIN_SYS_STAR + s, .q1 = q1, .q2 = q2, .q3 = q3,
                            .x = sp[0], .y = sp[1], .z = sp[2], .seed = seed,
                            .star_class = rand() % 7, .n_planets = L.n_planets, .active = 1 };
        for (int k = 0; k < L.n_planets; k++) {
            int ps = sys_planet_slot(s, k);
            sys_planets[ps] = (NPCSysPlanet){ .id = GALAXY_OBJECT_MIN_SYS_PLANET + ps, .q1 = q1, .q2 = q2, .q3 = q3,
                .resource_type = (rand() % 8) + 1, .amount = MAX_TORPEDO_CAPACITY,
                .planet_class = rand() % PLANET_CLASS_COUNT, .n_moons = L.n_moons[k],
                .orbit_r = L.planet_r[k], .active = 1 };
            planets_n++;
            for (int j = 0; j < L.n_moons[k]; j++) {
                int ms = sys_moon_slot(s, k, j);
                int ti = rand() % (OBJ_MOON_COUNT + OBJ_MOON_EXT_COUNT);
                int type = ti < OBJ_MOON_COUNT ? OBJ_MOON_FIRST + ti : OBJ_MOON_EXT_FIRST + (ti - OBJ_MOON_COUNT);
                sys_moons[ms] = (NPCSysMoon){ .id = GALAXY_OBJECT_MIN_SYS_MOON + ms, .q1 = q1, .q2 = q2, .q3 = q3,
                                              .moon_type = type, .active = 1 };
                moons_n++;
            }
        }
        if (L.n_planets < 4) trimmed++;
        sys_refresh_positions(s, now);
        sys_star_count = s + 1;
    }
    free(cand);
    printf("[GALAXY] star systems: %d placed in %d empty quadrants (%d planets, %d moons)\n",
           sys_star_count, nc, planets_n, moons_n);
    (void)trimmed;
}

/* Tick: the cached x,y,z of the systems players are in (targeting, network).
 * Obstacles and collisions compute the positions themselves (obstacles.h). */
void update_star_systems(void) {
    if (sys_star_count <= 0 || !spatial_index) return;
    double t = sys_clock_s();
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!players[i].active) continue;
        int q1 = players[i].state.q1, q2 = players[i].state.q2, q3 = players[i].state.q3;
        if (!IS_Q_VALID(q1, q2, q3)) continue;
        const QuadrantIndex *qi = &spatial_index[q1][q2][q3];
        if (qi->sys_star_count <= 0 || !qi->sys_stars[0]) continue;
        sys_refresh_positions((int)(qi->sys_stars[0] - sys_stars), t);
    }
}

void generate_galaxy() {
    printf("DEBUG: generate_galaxy called\n"); printf("Generating Master Galaxy...\n");
    memset(&spacegl_master, 0, sizeof(SpaceGLGame));
    memset(npcs, 0, sizeof(npcs));
    memset(players, 0, sizeof(players));
    memset(stars_data, 0, sizeof(stars_data));
    memset(planets, 0, sizeof(planets));
    memset(sys_stars, 0, sizeof(sys_stars));
    memset(sys_planets, 0, sizeof(sys_planets));
    memset(sys_moons, 0, sizeof(sys_moons));
    sys_star_count = 0;
    memset(bases, 0, sizeof(bases));
    memset(black_holes, 0, sizeof(black_holes));
    memset(mega_structs, 0, sizeof(mega_structs));
    memset(dark_clouds, 0, sizeof(dark_clouds));
    memset(singularities, 0, sizeof(singularities));
    memset(plasma_storms, 0, sizeof(plasma_storms));
    memset(orbital_rings, 0, sizeof(orbital_rings));
    memset(time_anomalies, 0, sizeof(time_anomalies));
    memset(void_crystals, 0, sizeof(void_crystals));
    memset(subspace_anomalies, 0, sizeof(subspace_anomalies));

    memset(diffuse_nebulae, 0, sizeof(diffuse_nebulae));
    memset(dark_nebulae, 0, sizeof(dark_nebulae));
    memset(planetary_nebulae, 0, sizeof(planetary_nebulae));
    memset(snrs, 0, sizeof(snrs));
    memset(gmcs, 0, sizeof(gmcs));
    memset(interstellar_filaments, 0, sizeof(interstellar_filaments));
    memset(interstellar_bubbles, 0, sizeof(interstellar_bubbles));
    memset(bok_globules, 0, sizeof(bok_globules));
    memset(clump_cores, 0, sizeof(clump_cores));
    memset(accretion_disks, 0, sizeof(accretion_disks));
    memset(relativistic_jets, 0, sizeof(relativistic_jets));
    memset(shock_waves, 0, sizeof(shock_waves));
    memset(stellar_bow_shocks, 0, sizeof(stellar_bow_shocks));
    memset(cosmic_voids, 0, sizeof(cosmic_voids));
    memset(cosmic_filaments, 0, sizeof(cosmic_filaments));
    memset(event_horizons, 0, sizeof(event_horizons));
    memset(kilonovae, 0, sizeof(kilonovae));
    memset(grav_lenses, 0, sizeof(grav_lenses));
    memset(grbs, 0, sizeof(grbs));
    memset(grav_waves, 0, sizeof(grav_waves));
    memset(protoplanetary_disks, 0, sizeof(protoplanetary_disks));
    memset(debris_disks, 0, sizeof(debris_disks));
    memset(planetesimals, 0, sizeof(planetesimals));
    memset(rogue_planets, 0, sizeof(rogue_planets));
    memset(brown_dwarfs, 0, sizeof(brown_dwarfs));
    memset(isos, 0, sizeof(isos));
    memset(mag_reconns, 0, sizeof(mag_reconns));
    memset(current_sheets, 0, sizeof(current_sheets));
    memset(heliospheres, 0, sizeof(heliospheres));
    memset(term_shocks, 0, sizeof(term_shocks));
    memset(magnetospheres, 0, sizeof(magnetospheres));
    memset(cosmic_strings, 0, sizeof(cosmic_strings));
    memset(domain_walls, 0, sizeof(domain_walls));
    memset(dm_halos, 0, sizeof(dm_halos));
    memset(igms, 0, sizeof(igms));
    memset(cgms, 0, sizeof(cgms));
    memset(lyman_alphas, 0, sizeof(lyman_alphas));
    memset(cmbs, 0, sizeof(cmbs));

    int n_count = 0, b_count = 0, p_count = 0, s_count = 0, bh_count = 0, neb_count = 0, pul_count = 0, com_count = 0, ast_count = 0, der_count = 0, mine_count = 0, buoy_count = 0, plat_count = 0, rift_count = 0, mon_count = 0;
    int faction_counts[21] = {0};
    int faction_base_counts[21] = {0};
    int class_der_counts[14] = {0};
    int faction_der_counts[21] = {0};
    int star_spectral_counts[7] = {0}; /* O, B, A, F, G, K, M */
    int nebula_type_counts[6] = {0};   /* Standard, High-Energy, Dark Matter, Ionic, Gravimetric, Temporal */
    int pulsar_type_counts[3] = {0};   /* Rotation-Powered, Accretion-Powered, Magnetar */
    int quasar_type_counts[7] = {0};   /* Radio-loud, Radio-quiet, BAL, Type 2, Red, OVV, Weak-Em */
    int monster_type_counts[2] = {0};  /* Crystalline, Amoeba */
    int planet_type_counts[9] = {0};   /* None, Aetherium, Neo-Ti, Void-E, Graphene, Synaptics, Gas, Composite, Dark-Matter */
    int asteroid_type_counts[9] = {0}; /* Same resources as planets */

    /* New objects generation counts */
    int diffuse_nebula_count = 0;
    int dark_nebula_count = 0;
    int planetary_nebula_count = 0;
    int snr_count = 0;
    int gmc_count = 0;
    int interstellar_filament_count = 0;
    int interstellar_bubble_count = 0;
    int bok_globule_count = 0;
    int clump_core_count = 0;
    int accretion_disk_count = 0;
    int relativistic_jet_count = 0;
    int shock_wave_count = 0;
    int stellar_bow_shock_count = 0;
    int cosmic_void_count = 0;
    int cosmic_filament_count = 0;
    int event_horizon_count = 0;
    int kilonova_count = 0;
    int grav_lens_count = 0;
    int grb_count = 0;
    int grav_wave_count = 0;
    int protoplanetary_disk_count = 0;
    int debris_disk_count = 0;
    int planetesimal_count = 0;
    int rogue_planet_count = 0;
    int brown_dwarf_count = 0;
    int iso_count = 0;
    int mag_reconn_count = 0;
    int current_sheet_count = 0;
    int heliosphere_count = 0;
    int term_shock_count = 0;
    int magnetosphere_count = 0;
    int cosmic_string_count = 0;
    int domain_wall_count = 0;
    int dm_halo_count = 0;
    int igm_count = 0;
    int cgm_count = 0;
    int lyman_alpha_count = 0;
    int cmb_count = 0;
    int mega_struct_count = 0;
    int dark_cloud_count = 0;
    int singularity_count = 0;
    int plasma_storm_count = 0;
    int orbital_ring_count = 0;
    int time_anomaly_count = 0;
    int void_crystal_count = 0;
    int sub_anomaly_count = 0;
    int artifact_count = 0;
    int warp_gate_count = 0;
    int neutron_star_count = 0;
    int rupture_count = 0;
    int satellite_count = 0;
    int storm_count = 0;
    int relic_count = 0;
    int hub_count = 0;
    int dyson_count = 0;
    int target_qsr = 0;
    int target_artifact = 0, target_warp_gate = 0, target_neutron_star = 0, target_diffuse_nebula = 0, target_dark_nebula = 0;
    int target_planetary_nebula = 0, target_snr = 0, target_gmc = 0, target_interstellar_filament = 0, target_interstellar_bubble = 0;
    int target_bok_globule = 0, target_clump_core = 0, target_accretion_disk = 0, target_relativistic_jet = 0, target_shock_wave = 0;
    int target_stellar_bow_shock = 0, target_cosmic_void = 0, target_cosmic_filament = 0, target_event_horizon = 0, target_kilonova = 0;
    int target_grav_lens = 0, target_grb = 0, target_grav_wave = 0, target_protoplanetary_disk = 0, target_debris_disk = 0;
    int target_planetesimal = 0, target_rogue_planet = 0, target_brown_dwarf = 0, target_iso = 0, target_mag_reconn = 0;
    int target_current_sheet = 0, target_heliosphere = 0, target_term_shock = 0, target_magnetosphere = 0, target_cosmic_string = 0;
    int target_domain_wall = 0, target_dm_halo = 0, target_igm = 0, target_cgm = 0, target_lyman_alpha = 0, target_cmb = 0;
    int target_mega_struct = 0, target_dark_cloud = 0, target_singularity = 0, target_plasma_storm = 0, target_orbital_ring = 0;
    int target_time_anomaly = 0, target_void_crystal = 0, target_sub_anomaly = 0;
    
    for (int faction = 10; faction <= 20; faction++) {
        int count = GALAXY_CREATE_OBJECT_MIN_NPC_FACTION + (rand() % (GALAXY_CREATE_OBJECT_MAX_NPC_FACTION - GALAXY_CREATE_OBJECT_MIN_NPC_FACTION + 1));
        for (int k = 0; k < count && n_count < MAX_NPC; k++) {
            NPCShip *n = &npcs[n_count];
            npc_runtime_reset(n_count);
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
        int faction = (rand() % 12 == 0) ? FACTION_ALLIANCE : 10 + (rand() % 11);
        bases[b_count] = (NPCBase){.id=b_count, .faction=faction, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .health=COST_HYPERDRIVE_INIT, .active=1};
        faction_base_counts[faction]++;
        b_count++;
    }

    int target_planets = GALAXY_CREATE_OBJECT_MIN_PLANET + (rand() % (GALAXY_CREATE_OBJECT_MAX_PLANET - GALAXY_CREATE_OBJECT_MIN_PLANET + 1));
    for (int i = 0; i < target_planets && p_count < MAX_PLANETS; i++) {
        int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
        int r_type = (rand() % 8) + 1;
        int p_class = rand() % PLANET_CLASS_COUNT;
        double p_span = QUADRANT_SIZE - 2.0 * MOON_ORBIT_MAX_RADIUS;
        double px = MOON_ORBIT_MAX_RADIUS + (rand() % (int)(p_span * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
        double py = MOON_ORBIT_MAX_RADIUS + (rand() % (int)(p_span * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
        double pz = MOON_ORBIT_MAX_RADIUS + (rand() % (int)(p_span * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
        planets[p_count] = (NPCPlanet){.id=p_count, .planet_class=p_class, .q1=q1, .q2=q2, .q3=q3, .x=px, .y=py, .z=pz, .resource_type=r_type, .amount=MAX_TORPEDO_CAPACITY, .active=1};
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
    target_qsr = GALAXY_CREATE_OBJECT_MIN_QUASAR + (rand() % (GALAXY_CREATE_OBJECT_MAX_QUASAR - GALAXY_CREATE_OBJECT_MIN_QUASAR + 1));
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

                dyson_count = 0;
                int target_dyson = GALAXY_CREATE_OBJECT_MIN_DYSON + (rand() % (GALAXY_CREATE_OBJECT_MAX_DYSON - GALAXY_CREATE_OBJECT_MIN_DYSON + 1));
                for (int i = 0; i < target_dyson && dyson_count < MAX_DYSON; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    dysons[dyson_count] = (NPCDyson){.id=dyson_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    dyson_count++;
                }

                hub_count = 0;
                int target_hub = GALAXY_CREATE_OBJECT_MIN_HUB + (rand() % (GALAXY_CREATE_OBJECT_MAX_HUB - GALAXY_CREATE_OBJECT_MIN_HUB + 1));
                for (int i = 0; i < target_hub && hub_count < MAX_HUBS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    hubs[hub_count] = (NPCHub){.id=hub_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    hub_count++;
                }

                relic_count = 0;
                int target_relic = GALAXY_CREATE_OBJECT_MIN_RELIC + (rand() % (GALAXY_CREATE_OBJECT_MAX_RELIC - GALAXY_CREATE_OBJECT_MIN_RELIC + 1));
                for (int i = 0; i < target_relic && relic_count < MAX_RELICS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    relics[relic_count] = (NPCRelic){.id=relic_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    relic_count++;
                }

                rupture_count = 0;
                int target_rupture = GALAXY_CREATE_OBJECT_MIN_RUPTURE + (rand() % (GALAXY_CREATE_OBJECT_MAX_RUPTURE - GALAXY_CREATE_OBJECT_MIN_RUPTURE + 1));
                for (int i = 0; i < target_rupture && rupture_count < MAX_RUPTURES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    ruptures[rupture_count] = (NPCRupture){.id=rupture_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    rupture_count++;
                }

                satellite_count = 0;
                int target_satellite = GALAXY_CREATE_OBJECT_MIN_SATELLITE + (rand() % (GALAXY_CREATE_OBJECT_MAX_SATELLITE - GALAXY_CREATE_OBJECT_MIN_SATELLITE + 1));
                for (int i = 0; i < target_satellite && satellite_count < MAX_SATELLITES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    satellites[satellite_count] = (NPCSatellite){.id=satellite_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    satellite_count++;
                }

                storm_count = 0;
                int target_storm = GALAXY_CREATE_OBJECT_MIN_STORM + (rand() % (GALAXY_CREATE_OBJECT_MAX_STORM - GALAXY_CREATE_OBJECT_MIN_STORM + 1));
                for (int i = 0; i < target_storm && storm_count < MAX_STORMS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    storms[storm_count] = (NPCStorm){.id=storm_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    storm_count++;
                }

                target_artifact = GALAXY_CREATE_OBJECT_MIN_ARTIFACT + (rand() % (GALAXY_CREATE_OBJECT_MAX_ARTIFACT - GALAXY_CREATE_OBJECT_MIN_ARTIFACT + 1));
                for (int i = 0; i < target_artifact && artifact_count < MAX_ARTIFACTS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    artifacts[artifact_count] = (NPCArtifact){.id=artifact_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    artifact_count++;
                }

                target_warp_gate = GALAXY_CREATE_OBJECT_MIN_WARP_GATE + (rand() % (GALAXY_CREATE_OBJECT_MAX_WARP_GATE - GALAXY_CREATE_OBJECT_MIN_WARP_GATE + 1));
                for (int i = 0; i < target_warp_gate && warp_gate_count < MAX_WARP_GATES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    warp_gates[warp_gate_count] = (NPCWarpGate){.id=warp_gate_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    warp_gate_count++;
                }

                target_neutron_star = GALAXY_CREATE_OBJECT_MIN_NEUTRON_STAR + (rand() % (GALAXY_CREATE_OBJECT_MAX_NEUTRON_STAR - GALAXY_CREATE_OBJECT_MIN_NEUTRON_STAR + 1));
                for (int i = 0; i < target_neutron_star && neutron_star_count < MAX_NEUTRON_STARS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    neutron_stars[neutron_star_count] = (NPCNeutronStar){.id=neutron_star_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    neutron_star_count++;
                }

                /* New objects generation */
                target_diffuse_nebula = GALAXY_CREATE_OBJECT_MIN_DIFFUSE_NEBULA + (rand() % (GALAXY_CREATE_OBJECT_MAX_DIFFUSE_NEBULA - GALAXY_CREATE_OBJECT_MIN_DIFFUSE_NEBULA + 1));
                for (int i = 0; i < target_diffuse_nebula && diffuse_nebula_count < MAX_DIFFUSE_NEBULAE; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    diffuse_nebulae[diffuse_nebula_count] = (NPCDiffuseNebula){.id=diffuse_nebula_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    diffuse_nebula_count++;
                }

                target_dark_nebula = GALAXY_CREATE_OBJECT_MIN_DARK_NEBULA + (rand() % (GALAXY_CREATE_OBJECT_MAX_DARK_NEBULA - GALAXY_CREATE_OBJECT_MIN_DARK_NEBULA + 1));
                for (int i = 0; i < target_dark_nebula && dark_nebula_count < MAX_DARK_NEBULAE; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    dark_nebulae[dark_nebula_count] = (NPCDarkNebula){.id=dark_nebula_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    dark_nebula_count++;
                }

                target_planetary_nebula = GALAXY_CREATE_OBJECT_MIN_PLANETARY_NEBULA + (rand() % (GALAXY_CREATE_OBJECT_MAX_PLANETARY_NEBULA - GALAXY_CREATE_OBJECT_MIN_PLANETARY_NEBULA + 1));
                for (int i = 0; i < target_planetary_nebula && planetary_nebula_count < MAX_PLANETARY_NEBULAE; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    planetary_nebulae[planetary_nebula_count] = (NPCPlanetaryNebula){.id=planetary_nebula_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    planetary_nebula_count++;
                }

                target_snr = GALAXY_CREATE_OBJECT_MIN_SNR + (rand() % (GALAXY_CREATE_OBJECT_MAX_SNR - GALAXY_CREATE_OBJECT_MIN_SNR + 1));
                for (int i = 0; i < target_snr && snr_count < MAX_SNR; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    snrs[snr_count] = (NPCSNR){.id=snr_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    snr_count++;
                }

                target_gmc = GALAXY_CREATE_OBJECT_MIN_GMC + (rand() % (GALAXY_CREATE_OBJECT_MAX_GMC - GALAXY_CREATE_OBJECT_MIN_GMC + 1));
                for (int i = 0; i < target_gmc && gmc_count < MAX_GMC; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    gmcs[gmc_count] = (NPCGMC){.id=gmc_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    gmc_count++;
                }

                target_interstellar_filament = GALAXY_CREATE_OBJECT_MIN_INTERSTELLAR_FILAMENT + (rand() % (GALAXY_CREATE_OBJECT_MAX_INTERSTELLAR_FILAMENT - GALAXY_CREATE_OBJECT_MIN_INTERSTELLAR_FILAMENT + 1));
                for (int i = 0; i < target_interstellar_filament && interstellar_filament_count < MAX_INTERSTELLAR_FILAMENTS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    interstellar_filaments[interstellar_filament_count] = (NPCInterstellarFilament){.id=interstellar_filament_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    interstellar_filament_count++;
                }

                target_interstellar_bubble = GALAXY_CREATE_OBJECT_MIN_INTERSTELLAR_BUBBLE + (rand() % (GALAXY_CREATE_OBJECT_MAX_INTERSTELLAR_BUBBLE - GALAXY_CREATE_OBJECT_MIN_INTERSTELLAR_BUBBLE + 1));
                for (int i = 0; i < target_interstellar_bubble && interstellar_bubble_count < MAX_INTERSTELLAR_BUBBLES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    interstellar_bubbles[interstellar_bubble_count] = (NPCInterstellarBubble){.id=interstellar_bubble_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    interstellar_bubble_count++;
                }

                target_bok_globule = GALAXY_CREATE_OBJECT_MIN_BOK_GLOBULE + (rand() % (GALAXY_CREATE_OBJECT_MAX_BOK_GLOBULE - GALAXY_CREATE_OBJECT_MIN_BOK_GLOBULE + 1));
                for (int i = 0; i < target_bok_globule && bok_globule_count < MAX_BOK_GLOBULES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    bok_globules[bok_globule_count] = (NPCBokGlobule){.id=bok_globule_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    bok_globule_count++;
                }

                target_clump_core = GALAXY_CREATE_OBJECT_MIN_CLUMP_CORE + (rand() % (GALAXY_CREATE_OBJECT_MAX_CLUMP_CORE - GALAXY_CREATE_OBJECT_MIN_CLUMP_CORE + 1));
                for (int i = 0; i < target_clump_core && clump_core_count < MAX_CLUMP_CORES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    clump_cores[clump_core_count] = (NPCClumpCore){.id=clump_core_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    clump_core_count++;
                }

                target_accretion_disk = GALAXY_CREATE_OBJECT_MIN_ACCRETION_DISK + (rand() % (GALAXY_CREATE_OBJECT_MAX_ACCRETION_DISK - GALAXY_CREATE_OBJECT_MIN_ACCRETION_DISK + 1));
                for (int i = 0; i < target_accretion_disk && accretion_disk_count < MAX_ACCRETION_DISKS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    accretion_disks[accretion_disk_count] = (NPCAccretionDisk){.id=accretion_disk_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    accretion_disk_count++;
                }

                target_relativistic_jet = GALAXY_CREATE_OBJECT_MIN_RELATIVISTIC_JET + (rand() % (GALAXY_CREATE_OBJECT_MAX_RELATIVISTIC_JET - GALAXY_CREATE_OBJECT_MIN_RELATIVISTIC_JET + 1));
                for (int i = 0; i < target_relativistic_jet && relativistic_jet_count < MAX_RELATIVISTIC_JETS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    relativistic_jets[relativistic_jet_count] = (NPCRelativisticJet){.id=relativistic_jet_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    relativistic_jet_count++;
                }

                target_shock_wave = GALAXY_CREATE_OBJECT_MIN_SHOCK_WAVE + (rand() % (GALAXY_CREATE_OBJECT_MAX_SHOCK_WAVE - GALAXY_CREATE_OBJECT_MIN_SHOCK_WAVE + 1));
                for (int i = 0; i < target_shock_wave && shock_wave_count < MAX_SHOCK_WAVES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    shock_waves[shock_wave_count] = (NPCShockWave){.id=shock_wave_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    shock_wave_count++;
                }

                target_stellar_bow_shock = GALAXY_CREATE_OBJECT_MIN_STELLAR_BOW_SHOCK + (rand() % (GALAXY_CREATE_OBJECT_MAX_STELLAR_BOW_SHOCK - GALAXY_CREATE_OBJECT_MIN_STELLAR_BOW_SHOCK + 1));
                for (int i = 0; i < target_stellar_bow_shock && stellar_bow_shock_count < MAX_STELLAR_BOW_SHOCKS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    stellar_bow_shocks[stellar_bow_shock_count] = (NPCStellarBowShock){.id=stellar_bow_shock_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    stellar_bow_shock_count++;
                }

                target_cosmic_void = GALAXY_CREATE_OBJECT_MIN_COSMIC_VOID + (rand() % (GALAXY_CREATE_OBJECT_MAX_COSMIC_VOID - GALAXY_CREATE_OBJECT_MIN_COSMIC_VOID + 1));
                for (int i = 0; i < target_cosmic_void && cosmic_void_count < MAX_COSMIC_VOIDS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    cosmic_voids[cosmic_void_count] = (NPCCosmicVoid){.id=cosmic_void_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    cosmic_void_count++;
                }

                target_cosmic_filament = GALAXY_CREATE_OBJECT_MIN_COSMIC_FILAMENT + (rand() % (GALAXY_CREATE_OBJECT_MAX_COSMIC_FILAMENT - GALAXY_CREATE_OBJECT_MIN_COSMIC_FILAMENT + 1));
                for (int i = 0; i < target_cosmic_filament && cosmic_filament_count < MAX_COSMIC_FILAMENTS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    cosmic_filaments[cosmic_filament_count] = (NPCCosmicFilament){.id=cosmic_filament_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    cosmic_filament_count++;
                }

                target_event_horizon = GALAXY_CREATE_OBJECT_MIN_EVENT_HORIZON + (rand() % (GALAXY_OBJECT_MAX_EVENT_HORIZON_CREATE - GALAXY_CREATE_OBJECT_MIN_EVENT_HORIZON + 1));
                for (int i = 0; i < target_event_horizon && event_horizon_count < MAX_EVENT_HORIZONS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    event_horizons[event_horizon_count] = (NPCEventHorizon){.id=event_horizon_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    event_horizon_count++;
                }

                target_kilonova = GALAXY_CREATE_OBJECT_MIN_KILONOVA + (rand() % (GALAXY_CREATE_OBJECT_MAX_KILONOVA - GALAXY_CREATE_OBJECT_MIN_KILONOVA + 1));
                for (int i = 0; i < target_kilonova && kilonova_count < MAX_KILONOVAE; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    kilonovae[kilonova_count] = (NPCKilonova){.id=kilonova_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    kilonova_count++;
                }

                target_grav_lens = GALAXY_CREATE_OBJECT_MIN_GRAV_LENS + (rand() % (GALAXY_CREATE_OBJECT_MAX_GRAV_LENS - GALAXY_CREATE_OBJECT_MIN_GRAV_LENS + 1));
                for (int i = 0; i < target_grav_lens && grav_lens_count < MAX_GRAV_LENSES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    grav_lenses[grav_lens_count] = (NPCGravLens){.id=grav_lens_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    grav_lens_count++;
                }

                target_grb = GALAXY_CREATE_OBJECT_MIN_GRB + (rand() % (GALAXY_CREATE_OBJECT_MAX_GRB - GALAXY_CREATE_OBJECT_MIN_GRB + 1));
                for (int i = 0; i < target_grb && grb_count < MAX_GRB; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    grbs[grb_count] = (NPCGRB){.id=grb_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    grb_count++;
                }

                target_grav_wave = GALAXY_CREATE_OBJECT_MIN_GRAV_WAVE + (rand() % (GALAXY_CREATE_OBJECT_MAX_GRAV_WAVE - GALAXY_CREATE_OBJECT_MIN_GRAV_WAVE + 1));
                for (int i = 0; i < target_grav_wave && grav_wave_count < MAX_GRAV_WAVES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    grav_waves[grav_wave_count] = (NPCGravWave){.id=grav_wave_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    grav_wave_count++;
                }

                target_protoplanetary_disk = GALAXY_CREATE_OBJECT_MIN_PROTOPLANETARY_DISK + (rand() % (GALAXY_CREATE_OBJECT_MAX_PROTOPLANETARY_DISK - GALAXY_CREATE_OBJECT_MIN_PROTOPLANETARY_DISK + 1));
                for (int i = 0; i < target_protoplanetary_disk && protoplanetary_disk_count < MAX_PROTOPLANETARY_DISKS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    protoplanetary_disks[protoplanetary_disk_count] = (NPCProtoplanetaryDisk){.id=protoplanetary_disk_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    protoplanetary_disk_count++;
                }

                target_debris_disk = GALAXY_CREATE_OBJECT_MIN_DEBRIS_DISK + (rand() % (GALAXY_CREATE_OBJECT_MAX_DEBRIS_DISK - GALAXY_CREATE_OBJECT_MIN_DEBRIS_DISK + 1));
                for (int i = 0; i < target_debris_disk && debris_disk_count < MAX_DEBRIS_DISKS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    debris_disks[debris_disk_count] = (NPCDebrisDisk){.id=debris_disk_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    debris_disk_count++;
                }

                target_planetesimal = GALAXY_CREATE_OBJECT_MIN_PLANETESIMAL + (rand() % (GALAXY_CREATE_OBJECT_MAX_PLANETESIMAL - GALAXY_CREATE_OBJECT_MIN_PLANETESIMAL + 1));
                for (int i = 0; i < target_planetesimal && planetesimal_count < MAX_PLANETESIMALS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    planetesimals[planetesimal_count] = (NPCPlanetesimal){.id=planetesimal_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    planetesimal_count++;
                }

                target_rogue_planet = GALAXY_CREATE_OBJECT_MIN_ROGUE_PLANET + (rand() % (GALAXY_CREATE_OBJECT_MAX_ROGUE_PLANET - GALAXY_CREATE_OBJECT_MIN_ROGUE_PLANET + 1));
                for (int i = 0; i < target_rogue_planet && rogue_planet_count < MAX_ROGUE_PLANETS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    rogue_planets[rogue_planet_count] = (NPCRoguePlanet){.id=rogue_planet_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    rogue_planet_count++;
                }

                target_brown_dwarf = GALAXY_CREATE_OBJECT_MIN_BROWN_DWARF + (rand() % (GALAXY_CREATE_OBJECT_MAX_BROWN_DWARF - GALAXY_CREATE_OBJECT_MIN_BROWN_DWARF + 1));
                for (int i = 0; i < target_brown_dwarf && brown_dwarf_count < MAX_BROWN_DWARFS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    brown_dwarfs[brown_dwarf_count] = (NPCBrownDwarf){.id=brown_dwarf_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    brown_dwarf_count++;
                }

                target_iso = GALAXY_CREATE_OBJECT_MIN_ISO + (rand() % (GALAXY_CREATE_OBJECT_MAX_ISO - GALAXY_CREATE_OBJECT_MIN_ISO + 1));
                for (int i = 0; i < target_iso && iso_count < MAX_ISO; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    isos[iso_count] = (NPCISO){.id=iso_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    iso_count++;
                }

                target_mag_reconn = GALAXY_CREATE_OBJECT_MIN_MAG_RECONN + (rand() % (GALAXY_CREATE_OBJECT_MAX_MAG_RECONN - GALAXY_CREATE_OBJECT_MIN_MAG_RECONN + 1));
                for (int i = 0; i < target_mag_reconn && mag_reconn_count < MAX_MAG_RECONN; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    mag_reconns[mag_reconn_count] = (NPCMagReconn){.id=mag_reconn_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    mag_reconn_count++;
                }

                target_current_sheet = GALAXY_CREATE_OBJECT_MIN_CURRENT_SHEET + (rand() % (GALAXY_CREATE_OBJECT_MAX_CURRENT_SHEET - GALAXY_CREATE_OBJECT_MIN_CURRENT_SHEET + 1));
                for (int i = 0; i < target_current_sheet && current_sheet_count < MAX_CURRENT_SHEETS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    current_sheets[current_sheet_count] = (NPCCurrentSheet){.id=current_sheet_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    current_sheet_count++;
                }

                target_heliosphere = GALAXY_CREATE_OBJECT_MIN_HELIOSPHERE + (rand() % (GALAXY_CREATE_OBJECT_MAX_HELIOSPHERE - GALAXY_CREATE_OBJECT_MIN_HELIOSPHERE + 1));
                for (int i = 0; i < target_heliosphere && heliosphere_count < MAX_HELIOSPHERES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    heliospheres[heliosphere_count] = (NPCHeliosphere){.id=heliosphere_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    heliosphere_count++;
                }

                target_term_shock = GALAXY_CREATE_OBJECT_MIN_TERM_SHOCK + (rand() % (GALAXY_CREATE_OBJECT_MAX_TERM_SHOCK - GALAXY_CREATE_OBJECT_MIN_TERM_SHOCK + 1));
                for (int i = 0; i < target_term_shock && term_shock_count < MAX_TERM_SHOCKS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    term_shocks[term_shock_count] = (NPCTermShock){.id=term_shock_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    term_shock_count++;
                }

                target_magnetosphere = GALAXY_CREATE_OBJECT_MIN_MAGNETOSPHERE + (rand() % (GALAXY_CREATE_OBJECT_MAX_MAGNETOSPHERE - GALAXY_CREATE_OBJECT_MIN_MAGNETOSPHERE + 1));
                for (int i = 0; i < target_magnetosphere && magnetosphere_count < MAX_MAGNETOSPHERES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    magnetospheres[magnetosphere_count] = (NPCMagnetosphere){.id=magnetosphere_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    magnetosphere_count++;
                }

                target_cosmic_string = GALAXY_CREATE_OBJECT_MIN_COSMIC_STRING + (rand() % (GALAXY_CREATE_OBJECT_MAX_COSMIC_STRING - GALAXY_CREATE_OBJECT_MIN_COSMIC_STRING + 1));
                for (int i = 0; i < target_cosmic_string && cosmic_string_count < MAX_COSMIC_STRINGS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    cosmic_strings[cosmic_string_count] = (NPCCosmicString){.id=cosmic_string_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    cosmic_string_count++;
                }

                target_domain_wall = GALAXY_CREATE_OBJECT_MIN_DOMAIN_WALL + (rand() % (GALAXY_CREATE_OBJECT_MAX_DOMAIN_WALL - GALAXY_CREATE_OBJECT_MIN_DOMAIN_WALL + 1));
                for (int i = 0; i < target_domain_wall && domain_wall_count < MAX_DOMAIN_WALLS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    domain_walls[domain_wall_count] = (NPCDomainWall){.id=domain_wall_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    domain_wall_count++;
                }

                target_dm_halo = GALAXY_CREATE_OBJECT_MIN_DM_HALO + (rand() % (GALAXY_CREATE_OBJECT_MAX_DM_HALO - GALAXY_CREATE_OBJECT_MIN_DM_HALO + 1));
                for (int i = 0; i < target_dm_halo && dm_halo_count < MAX_DM_HALO; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    dm_halos[dm_halo_count] = (NPCDMHalo){.id=dm_halo_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    dm_halo_count++;
                }

                target_igm = GALAXY_CREATE_OBJECT_MIN_IGM + (rand() % (GALAXY_CREATE_OBJECT_MAX_IGM - GALAXY_CREATE_OBJECT_MIN_IGM + 1));
                for (int i = 0; i < target_igm && igm_count < MAX_IGM; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    igms[igm_count] = (NPCIGM){.id=igm_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    igm_count++;
                }

                target_cgm = GALAXY_CREATE_OBJECT_MIN_CGM + (rand() % (GALAXY_CREATE_OBJECT_MAX_CGM - GALAXY_CREATE_OBJECT_MIN_CGM + 1));
                for (int i = 0; i < target_cgm && cgm_count < MAX_CGM; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    cgms[cgm_count] = (NPCCGM){.id=cgm_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    cgm_count++;
                }

                target_lyman_alpha = GALAXY_CREATE_OBJECT_MIN_LYMAN_ALPHA + (rand() % (GALAXY_CREATE_OBJECT_MAX_LYMAN_ALPHA - GALAXY_CREATE_OBJECT_MIN_LYMAN_ALPHA + 1));
                for (int i = 0; i < target_lyman_alpha && lyman_alpha_count < MAX_LYMAN_ALPHA; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    lyman_alphas[lyman_alpha_count] = (NPCLymanAlpha){.id=lyman_alpha_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    lyman_alpha_count++;
                }

                target_cmb = GALAXY_CREATE_OBJECT_MIN_CMB + (rand() % (GALAXY_CREATE_OBJECT_MAX_CMB - GALAXY_CREATE_OBJECT_MIN_CMB + 1));
                for (int i = 0; i < target_cmb && cmb_count < MAX_CMB; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    cmbs[cmb_count] = (NPCCMB){.id=cmb_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    cmb_count++;
                }

                target_mega_struct = GALAXY_CREATE_OBJECT_MIN_MEGA_STRUCT + (rand() % (GALAXY_CREATE_OBJECT_MAX_MEGA_STRUCT - GALAXY_CREATE_OBJECT_MIN_MEGA_STRUCT + 1));
                for (int i = 0; i < target_mega_struct && mega_struct_count < MAX_MEGA_STRUCTS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    mega_structs[mega_struct_count] = (NPCMegaStructure){.id=mega_struct_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    mega_struct_count++;
                }

                target_dark_cloud = GALAXY_CREATE_OBJECT_MIN_DARK_CLOUD + (rand() % (GALAXY_CREATE_OBJECT_MAX_DARK_CLOUD - GALAXY_CREATE_OBJECT_MIN_DARK_CLOUD + 1));
                for (int i = 0; i < target_dark_cloud && dark_cloud_count < MAX_DARK_CLOUDS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    dark_clouds[dark_cloud_count] = (NPCDarkCloud){.id=dark_cloud_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    dark_cloud_count++;
                }

                target_singularity = GALAXY_CREATE_OBJECT_MIN_SINGULARITY + (rand() % (GALAXY_CREATE_OBJECT_MAX_SINGULARITY - GALAXY_CREATE_OBJECT_MIN_SINGULARITY + 1));
                for (int i = 0; i < target_singularity && singularity_count < MAX_SINGULARITIES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    singularities[singularity_count] = (NPCSingularity){.id=singularity_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    singularity_count++;
                }

                target_plasma_storm = GALAXY_CREATE_OBJECT_MIN_PLASMA_STORM + (rand() % (GALAXY_CREATE_OBJECT_MAX_PLASMA_STORM - GALAXY_CREATE_OBJECT_MIN_PLASMA_STORM + 1));
                for (int i = 0; i < target_plasma_storm && plasma_storm_count < MAX_PLASMA_STORMS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    plasma_storms[plasma_storm_count] = (NPCPlasmaStorm){.id=plasma_storm_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    plasma_storm_count++;
                }

                target_orbital_ring = GALAXY_CREATE_OBJECT_MIN_ORBITAL_RING + (rand() % (GALAXY_CREATE_OBJECT_MAX_ORBITAL_RING - GALAXY_CREATE_OBJECT_MIN_ORBITAL_RING + 1));
                for (int i = 0; i < target_orbital_ring && orbital_ring_count < MAX_ORBITAL_RINGS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    orbital_rings[orbital_ring_count] = (NPCOrbitalRing){.id=orbital_ring_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    orbital_ring_count++;
                }

                target_time_anomaly = GALAXY_CREATE_OBJECT_MIN_TIME_ANOMALY + (rand() % (GALAXY_CREATE_OBJECT_MAX_TIME_ANOMALY - GALAXY_CREATE_OBJECT_MIN_TIME_ANOMALY + 1));
                for (int i = 0; i < target_time_anomaly && time_anomaly_count < MAX_TIME_ANOMALIES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    time_anomalies[time_anomaly_count] = (NPCTimeAnomaly){.id=time_anomaly_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    time_anomaly_count++;
                }

                target_void_crystal = GALAXY_CREATE_OBJECT_MIN_VOID_CRYSTAL + (rand() % (GALAXY_CREATE_OBJECT_MAX_VOID_CRYSTAL - GALAXY_CREATE_OBJECT_MIN_VOID_CRYSTAL + 1));
                for (int i = 0; i < target_void_crystal && void_crystal_count < MAX_VOID_CRYSTALS; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    void_crystals[void_crystal_count] = (NPCVoidCrystal){.id=void_crystal_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    void_crystal_count++;
                }

                target_sub_anomaly = GALAXY_CREATE_OBJECT_MIN_SUBSPACE_ANOM + (rand() % (GALAXY_CREATE_OBJECT_MAX_SUBSPACE_ANOM - GALAXY_CREATE_OBJECT_MIN_SUBSPACE_ANOM + 1));
                for (int i = 0; i < target_sub_anomaly && sub_anomaly_count < MAX_SUBSPACE_ANOMALIES; i++) {
                    int q1 = 1 + rand() % GALAXY_SIZE, q2 = 1 + rand() % GALAXY_SIZE, q3 = 1 + rand() % GALAXY_SIZE;
                    subspace_anomalies[sub_anomaly_count] = (NPCSubspaceAnomaly){.id=sub_anomaly_count, .q1=q1, .q2=q2, .q3=q3, .x=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .y=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .z=(rand()%(int)(QUADRANT_SIZE * RATIO_COORD_RANDOM))/RATIO_COORD_RANDOM, .active=1};
                    sub_anomaly_count++;
                }

    /* Insert 32 new types of cosmic features (10 instances each) */
    const char *new_feature_names[OBJ_EXOTIC_COUNT] = { OBJECT_EXOTIC_NAME_LIST };

    for (int t = 0; t < OBJ_EXOTIC_COUNT; t++) {
        for (int i = 0; i < GALAXY_CREATE_EXOTIC_PER_TYPE; i++) {
            if (cosmic_feature_count >= MAX_COSMIC_FEATURES) break;
            int q1 = 1 + rand() % GALAXY_SIZE;
            int q2 = 1 + rand() % GALAXY_SIZE;
            int q3 = 1 + rand() % GALAXY_SIZE;
            double x = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
            double y = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
            double z = (rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
            
            cosmic_features[cosmic_feature_count] = (NPCCosmicFeature){
                .id = GALAXY_OBJECT_MIN_FEATURE + cosmic_feature_count, 
                .type = OBJ_EXOTIC_FIRST + t,                    
                .q1 = q1, .q2 = q2, .q3 = q3,
                .x = x, .y = y, .z = z,
                .active = 1
            };
            strncpy(cosmic_features[cosmic_feature_count].name, new_feature_names[t], 63);
            cosmic_features[cosmic_feature_count].name[63] = '\0';
            cosmic_feature_count++;
        }
    }

    /* Evolved stars (red giants .. pulsating white dwarfs): 17 types in the
     * same generic array. */
    spawn_evolved_stars();

    /* Protostellar objects & YSO: 16 types in the same array. */
    spawn_protostellar_objects();

    /* Natural moons (shepherd .. submoon): 10 types in the same array. */
    spawn_moons();
    attach_moons_to_planets();

    /* Star systems last: they go to quadrants that hold nothing else */
    spawn_star_systems();

    rebuild_spatial_index();
    refresh_lrs_grid();

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

    printf("\n%s .--- GALAXY GENERATION COMPLETED: ASTROMETRICS REPORT ----------.%s\n", B_CYAN, RESET);
    printf("%s | %s 🏚️ TOTAL WRECKS:  %s%-5d %s| %s 🛰️ Starbases:          %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, der_count, B_CYAN, B_WHITE, B_GREEN, b_count, B_CYAN);
    printf("%s | %s 🏛️ DYSON FRAGS:   %s%-5d %s| %s 🏢 TRADING HUBS:      %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, dyson_count, B_CYAN, B_WHITE, B_GREEN, hub_count, B_CYAN);
    printf("%s | %s 🏺 ANCIENT RELICS: %s%-5d %s| %s ⚡ ION STORMS:        %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, relic_count, B_CYAN, B_WHITE, B_GREEN, storm_count, B_CYAN);
    printf("%s | %s 🚀 TOTAL VESSELS: %s%-5d %s| %s 🪐 Planets:            %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, n_count, B_CYAN, B_WHITE, B_GREEN, p_count, B_CYAN);
    printf("%s | %s 🌋 SUBSPACE RUPT: %s%-5d %s| %s 📡 SATELLITES:        %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, rupture_count, B_CYAN, B_WHITE, B_GREEN, satellite_count, B_CYAN);
    printf("%s | %s 🌙 NATURAL MOONS: %s%-5d %s| %s ⭐ EVOLVED STARS:      %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, moon_count, B_CYAN, B_WHITE, B_GREEN, evolved_star_count, B_CYAN);
    printf("%s | %s ☀️ PROTOSTELLAR:  %s%-5d %s| %s 🌌 EXOTIC FEAT.:       %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, protostellar_count, B_CYAN, B_WHITE, B_GREEN, exotic_feature_count, B_CYAN);
    printf("%s | %s 📦 TOTAL FEATURES: %s%-5d %s| %s ✨ STARS:              %s%-5d %s|\n", B_CYAN, B_WHITE, B_GREEN, cosmic_feature_count, B_CYAN, B_WHITE, B_GREEN, s_count, B_CYAN);
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
    
    printf("%s | %s [ STAR SPECTRAL TYPES ]    %s| %s [ NEBULA CLASSIFICATION ]  %s|\n", B_CYAN, B_YELLOW, B_CYAN, B_YELLOW, B_CYAN);
    const char* spectrals[] = {"O (Blue)", "B (Blue)", "A (White)", "F (Y-White)", "G (Yellow)", "K (Orange)", "M (Red)"};
    const char* neb_classes[] = {"Standard", "High-Energy", "Dark Matter", "Ionic", "Gravimetric", "Temporal"};
    for(int k=0; k<7; k++) {
        const char* s_name = spectrals[k];
        const char* n_name = (k < 6) ? neb_classes[k] : "";
        char n_val_str[32] = "";
        if (k < 6) SG_SPRINTF(n_val_str, "%s%-4d", B_GREEN, nebula_type_counts[k]);
        printf("%s | %s %-12s: %s%-4d %s| %s %-12s: %-14s %s|\n", B_CYAN, B_WHITE, s_name, B_GREEN, star_spectral_counts[k], B_CYAN, B_WHITE, n_name, n_val_str, B_CYAN);
    }

    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    printf("%s | %s [ FACTION SHIPS ]          %s| %s [ ALLIANCE WRECKS ]        %s|\n", B_CYAN, B_YELLOW, B_CYAN, B_YELLOW, B_CYAN);
    
    const char* ship_classes_short[] = {"LEGACY", "SCOUT", "CRUISER", "ENGINE", "ESCORT", "EXPLORER", "FLAGSHIP", "SCIENCE", "CARRIER", "TACTICAL", "DIPLOMAT", "RESEARCH", "FRIGATE", "GENERIC"};
    for(int k=0; k<14; k++) {
        int faction = 10 + k;
        char f_line[64] = "";
        if (faction <= 20) {
            SG_SPRINTF(f_line, "%s %-12s: %s%-4d", B_WHITE, get_species_name(faction), B_GREEN, faction_counts[faction]);
        } else {
            SG_SPRINTF(f_line, "                       ");
        }
        printf("%s | %s %s| %s %-12s: %s%-4d %s|\n", B_CYAN, f_line, B_CYAN, B_WHITE, ship_classes_short[k], B_GREEN, class_der_counts[k], B_CYAN);
    }

    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    printf("%s | %s [ ALIEN WRECKS ]                                             %s|\n", B_CYAN, B_YELLOW, B_CYAN);
    for(int f=10; f<=20; f+=2) {
        char f1[64], f2[64] = "";
        SG_SPRINTF(f1, "%s %-12s: %s%-4d", B_WHITE, get_species_name(f), B_GREEN, faction_der_counts[f]);
        if (f+1 <= 20) {
            SG_SPRINTF(f2, "%s %-12s: %s%-4d", B_WHITE, get_species_name(f+1), B_GREEN, faction_der_counts[f+1]);
        }
        printf("%s | %s %s| %s %s %s|\n", B_CYAN, f1, B_CYAN, B_CYAN, f2, B_CYAN);
    }

    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    printf("%s | %s [ EXOTIC ENTITIES ]                                          %s|\n", B_CYAN, B_YELLOW, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4d  %s|\n", B_CYAN, B_WHITE, "Artifacts", B_GREEN, artifact_count, B_CYAN, B_WHITE, "Warp Gates", B_GREEN, warp_gate_count, B_CYAN, B_WHITE, "Neutron Sts", B_GREEN, neutron_star_count, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4d  %s|\n", B_CYAN, B_WHITE, "Mega Structs", B_GREEN, mega_struct_count, B_CYAN, B_WHITE, "Dark Clouds", B_GREEN, dark_cloud_count, B_CYAN, B_WHITE, "Singularit.", B_GREEN, singularity_count, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4d  %s|\n", B_CYAN, B_WHITE, "Plasma Strms", B_GREEN, plasma_storm_count, B_CYAN, B_WHITE, "Orbital Rngs", B_GREEN, orbital_ring_count, B_CYAN, B_WHITE, "Time Anom.", B_GREEN, time_anomaly_count, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4s  %s|\n", B_CYAN, B_WHITE, "Void Crystal", B_GREEN, void_crystal_count, B_CYAN, B_WHITE, "Subspace An.", B_GREEN, sub_anomaly_count, B_CYAN, B_WHITE, "", B_GREEN, "", B_CYAN);
    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    printf("%s | %s [ COSMOLOGICAL PHENOMENA ]                                   %s|\n", B_CYAN, B_YELLOW, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4d  %s|\n", B_CYAN, B_WHITE, "Diff Nebulae", B_GREEN, diffuse_nebula_count, B_CYAN, B_WHITE, "Dark Nebulae", B_GREEN, dark_nebula_count, B_CYAN, B_WHITE, "Planet Neb.", B_GREEN, planetary_nebula_count, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4d  %s|\n", B_CYAN, B_WHITE, "SNR", B_GREEN, snr_count, B_CYAN, B_WHITE, "GMC", B_GREEN, gmc_count, B_CYAN, B_WHITE, "Int. Filam.", B_GREEN, interstellar_filament_count, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4d  %s|\n", B_CYAN, B_WHITE, "Int. Bubbles", B_GREEN, interstellar_bubble_count, B_CYAN, B_WHITE, "Bok Globules", B_GREEN, bok_globule_count, B_CYAN, B_WHITE, "Clump/Cores", B_GREEN, clump_core_count, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4d  %s|\n", B_CYAN, B_WHITE, "Accret. Disk", B_GREEN, accretion_disk_count, B_CYAN, B_WHITE, "Relativ. Jet", B_GREEN, relativistic_jet_count, B_CYAN, B_WHITE, "Shock Waves", B_GREEN, shock_wave_count, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4d  %s|\n", B_CYAN, B_WHITE, "Bow Shocks", B_GREEN, stellar_bow_shock_count, B_CYAN, B_WHITE, "Cosmic Voids", B_GREEN, cosmic_void_count, B_CYAN, B_WHITE, "Cosmic Fil.", B_GREEN, cosmic_filament_count, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4d  %s|\n", B_CYAN, B_WHITE, "Event Horiz.", B_GREEN, event_horizon_count, B_CYAN, B_WHITE, "Kilonovae", B_GREEN, kilonova_count, B_CYAN, B_WHITE, "Grav Lenses", B_GREEN, grav_lens_count, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4d  %s|\n", B_CYAN, B_WHITE, "GRB", B_GREEN, grb_count, B_CYAN, B_WHITE, "Grav Waves", B_GREEN, grav_wave_count, B_CYAN, B_WHITE, "Protoplanet.", B_GREEN, protoplanetary_disk_count, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4d  %s|\n", B_CYAN, B_WHITE, "Debris Disks", B_GREEN, debris_disk_count, B_CYAN, B_WHITE, "Planetesims.", B_GREEN, planetesimal_count, B_CYAN, B_WHITE, "Rogue Plan.", B_GREEN, rogue_planet_count, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4d  %s|\n", B_CYAN, B_WHITE, "Brown Dwarfs", B_GREEN, brown_dwarf_count, B_CYAN, B_WHITE, "Interst. Obj", B_GREEN, iso_count, B_CYAN, B_WHITE, "Mag. Reconn.", B_GREEN, mag_reconn_count, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4d  %s|\n", B_CYAN, B_WHITE, "Cur. Sheets", B_GREEN, current_sheet_count, B_CYAN, B_WHITE, "Heliospheres", B_GREEN, heliosphere_count, B_CYAN, B_WHITE, "Term. Shocks", B_GREEN, term_shock_count, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4d  %s|\n", B_CYAN, B_WHITE, "Magnetosph.", B_GREEN, magnetosphere_count, B_CYAN, B_WHITE, "Cosm Strings", B_GREEN, cosmic_string_count, B_CYAN, B_WHITE, "Domain Walls", B_GREEN, domain_wall_count, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4d  %s|\n", B_CYAN, B_WHITE, "DM Halos", B_GREEN, dm_halo_count, B_CYAN, B_WHITE, "IGM", B_GREEN, igm_count, B_CYAN, B_WHITE, "CGM", B_GREEN, cgm_count, B_CYAN);
    printf("%s | %s%-12s: %s%-4d %s| %s%-12s: %s%-4d %s| %s%-12s: %s%-4s  %s|\n", B_CYAN, B_WHITE, "Lyman Alpha", B_GREEN, lyman_alpha_count, B_CYAN, B_WHITE, "CMB", B_GREEN, cmb_count, B_CYAN, B_WHITE, "", B_GREEN, "", B_CYAN);
    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    printf("%s | %s [ CLASSIFIED EXOTIC FEATURES ]                             %s|\n", B_CYAN, B_YELLOW, B_CYAN);
    for (int t = 0; t < 32; t+=2) {
        char n1[32], n2[32] = "";
        strncpy(n1, new_feature_names[t], 29); n1[29] = '\0';
        if (t+1 < 32) { strncpy(n2, new_feature_names[t+1], 29); n2[29] = '\0'; }
        printf("%s | %s %-29s: %s%-3d %s| %s %-29s: %s%-3d %s|\n", 
            B_CYAN, 
            B_WHITE, n1, B_GREEN, 10, B_CYAN, 
            B_WHITE, n2, B_GREEN, (t+1 < 32) ? 10 : 0, B_CYAN);
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
    printf("%s |---------------------------------------------------------------|\n", B_CYAN);
    printf("%s | %s [ STARBASES BY FACTION ]                                     %s|\n", B_CYAN, B_YELLOW, B_CYAN);
    for (int f = 10; f <= 20; f += 2) {
        char fb1[64] = "";
        char fb2[64] = "";
        SG_SPRINTF(fb1, "%s %-12s: %s%-4d", B_WHITE, get_species_name(f), B_GREEN, faction_base_counts[f]);
        if (f + 1 <= 20) {
            SG_SPRINTF(fb2, "%s %-12s: %s%-4d", B_WHITE, get_species_name(f + 1), B_GREEN, faction_base_counts[f + 1]);
        }
        printf("%s | %s %s| %s %s %s|\n", B_CYAN, fb1, B_CYAN, B_CYAN, fb2, B_CYAN);
    }
    printf("%s '---------------------------------------------------------------'%s\n", B_CYAN, RESET);
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
