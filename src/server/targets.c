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

/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * Universal target resolution.
 *
 * Before this module, the if/else chain over the GALAXY_OBJECT_MIN_* /
 * GALAXY_OBJECT_MAX_* ID ranges (one branch per object type) was
 * duplicated in four places:
 *   - per-tick lock validity      (logic.c, update_game_logic)
 *   - NAV_STATE_APPROACH autopilot (logic.c, local + global fallback)
 *   - handle_apr                  (commands.c)
 *   - handle_lock                 (commands.c)
 *
 * All ranges now live in a single table (TARGET_RANGES): {name, min_id,
 * max_id, array, stride, capacity, flags} plus per-type accessors for the
 * few layout/behavior differences (absolute position of NPC ships, species
 * names, cloak visibility).  The historical chains are preserved exactly by
 * the capability flags: each call site only sees the types it used to see.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "server_internal.h"

/*
 * All static object types share the common (id, q1, q2, q3, x, y, z, active)
 * layout, so one macro generates the four standard accessors per type.
 *   T      : element struct type (NPCStar, NPCBase, ...)
 *   QFIELD : pointer-array field in QuadrantIndex
 *   QCOUNT : count field in QuadrantIndex
 *   IDMIN  : GALAXY_OBJECT_MIN_<TYPE> token
 */
#define TGT_DEFINE_STANDARD(T, QFIELD, QCOUNT, IDMIN) \
    static bool tgt_aiq_##T(const void *e, int q1, int q2, int q3) { \
        const T *o = (const T *)e; \
        return o->active && o->q1 == q1 && o->q2 == q2 && o->q3 == q3; \
    } \
    static bool tgt_act_##T(const void *e) { \
        const T *o = (const T *)e; \
        return o->active; \
    } \
    static void tgt_pos_##T(const void *e, double *ax, double *ay, double *az) { \
        const T *o = (const T *)e; \
        *ax = (o->q1 - 1) * QUADRANT_SIZE + o->x; \
        *ay = (o->q2 - 1) * QUADRANT_SIZE + o->y; \
        *az = (o->q3 - 1) * QUADRANT_SIZE + o->z; \
    } \
    static void *tgt_find_##T(const QuadrantIndex *q, int tid) { \
        for (int k = 0; k < q->QCOUNT; k++) { \
            const T *o = q->QFIELD[k]; \
            if (o && o->id + (int)IDMIN == tid) return (void *)o; \
        } \
        return NULL; \
    }

TGT_DEFINE_STANDARD(NPCStar, stars, star_count, GALAXY_OBJECT_MIN_STAR)
TGT_DEFINE_STANDARD(NPCBlackHole, black_holes, bh_count, GALAXY_OBJECT_MIN_BLACKHOLE)
TGT_DEFINE_STANDARD(NPCNebula, nebulas, nebula_count, GALAXY_OBJECT_MIN_NEBULA)
TGT_DEFINE_STANDARD(NPCPulsar, pulsars, pulsar_count, GALAXY_OBJECT_MIN_PULSAR)
TGT_DEFINE_STANDARD(NPCQuasar, quasars, quasar_count, GALAXY_OBJECT_MIN_QUASAR)
TGT_DEFINE_STANDARD(NPCComet, comets, comet_count, GALAXY_OBJECT_MIN_COMET)
TGT_DEFINE_STANDARD(NPCAsteroid, asteroids, asteroid_count, GALAXY_OBJECT_MIN_ASTEROID)
TGT_DEFINE_STANDARD(NPCDerelict, derelicts, derelict_count, GALAXY_OBJECT_MIN_DERELICT)
TGT_DEFINE_STANDARD(NPCMine, mines, mine_count, GALAXY_OBJECT_MIN_MINE)
TGT_DEFINE_STANDARD(NPCBuoy, buoys, buoy_count, GALAXY_OBJECT_MIN_BUOY)
TGT_DEFINE_STANDARD(NPCPlatform, platforms, platform_count, GALAXY_OBJECT_MIN_PLATFORM)
TGT_DEFINE_STANDARD(NPCRift, rifts, rift_count, GALAXY_OBJECT_MIN_RIFT)
TGT_DEFINE_STANDARD(NPCMonster, monsters, monster_count, GALAXY_OBJECT_MIN_MONSTER)
TGT_DEFINE_STANDARD(NPCPlanet, planets, planet_count, GALAXY_OBJECT_MIN_PLANET)
TGT_DEFINE_STANDARD(NPCBase, bases, base_count, GALAXY_OBJECT_MIN_STARBASE)
TGT_DEFINE_STANDARD(NPCDyson, dysons, dyson_count, GALAXY_OBJECT_MIN_DYSON)
TGT_DEFINE_STANDARD(NPCHub, hubs, hub_count, GALAXY_OBJECT_MIN_HUB)
TGT_DEFINE_STANDARD(NPCRelic, relics, relic_count, GALAXY_OBJECT_MIN_RELIC)
TGT_DEFINE_STANDARD(NPCRupture, ruptures, rupture_count, GALAXY_OBJECT_MIN_RUPTURE)
TGT_DEFINE_STANDARD(NPCSatellite, satellites, satellite_count, GALAXY_OBJECT_MIN_SATELLITE)
TGT_DEFINE_STANDARD(NPCStorm, storms, storm_count, GALAXY_OBJECT_MIN_STORM)
TGT_DEFINE_STANDARD(NPCArtifact, artifacts, artifact_count, GALAXY_OBJECT_MIN_ARTIFACT)
TGT_DEFINE_STANDARD(NPCWarpGate, warp_gates, warp_gate_count, GALAXY_OBJECT_MIN_WARP_GATE)
TGT_DEFINE_STANDARD(NPCNeutronStar, neutron_stars, neutron_star_count, GALAXY_OBJECT_MIN_NEUTRON_STAR)
TGT_DEFINE_STANDARD(NPCMegaStructure, mega_structs, mega_struct_count, GALAXY_OBJECT_MIN_MEGA_STRUCT)
TGT_DEFINE_STANDARD(NPCDarkCloud, dark_clouds, dark_cloud_count, GALAXY_OBJECT_MIN_DARK_CLOUD)
TGT_DEFINE_STANDARD(NPCSingularity, singularities, singularity_count, GALAXY_OBJECT_MIN_SINGULARITY)
TGT_DEFINE_STANDARD(NPCPlasmaStorm, plasma_storms, plasma_storm_count, GALAXY_OBJECT_MIN_PLASMA_STORM)
TGT_DEFINE_STANDARD(NPCOrbitalRing, orbital_rings, orbital_ring_count, GALAXY_OBJECT_MIN_ORBITAL_RING)
TGT_DEFINE_STANDARD(NPCTimeAnomaly, time_anomalies, time_anomaly_count, GALAXY_OBJECT_MIN_TIME_ANOMALY)
TGT_DEFINE_STANDARD(NPCVoidCrystal, void_crystals, void_crystal_count, GALAXY_OBJECT_MIN_VOID_CRYSTAL)
TGT_DEFINE_STANDARD(NPCSubspaceAnomaly, subspace_anomalies, subspace_anomaly_count, GALAXY_OBJECT_MIN_SUBSPACE_ANOM)
TGT_DEFINE_STANDARD(NPCDiffuseNebula, diffuse_nebulae, diffuse_nebula_count, GALAXY_OBJECT_MIN_DIFFUSE_NEBULA)
TGT_DEFINE_STANDARD(NPCDarkNebula, dark_nebulae, dark_nebula_count, GALAXY_OBJECT_MIN_DARK_NEBULA)
TGT_DEFINE_STANDARD(NPCPlanetaryNebula, planetary_nebulae, planetary_nebula_count, GALAXY_OBJECT_MIN_PLANETARY_NEBULA)
TGT_DEFINE_STANDARD(NPCSNR, snrs, snr_count, GALAXY_OBJECT_MIN_SNR)
TGT_DEFINE_STANDARD(NPCGMC, gmcs, gmc_count, GALAXY_OBJECT_MIN_GMC)
TGT_DEFINE_STANDARD(NPCInterstellarFilament, interstellar_filaments, interstellar_filament_count, GALAXY_OBJECT_MIN_INTERSTELLAR_FILAMENT)
TGT_DEFINE_STANDARD(NPCInterstellarBubble, interstellar_bubbles, interstellar_bubble_count, GALAXY_OBJECT_MIN_INTERSTELLAR_BUBBLE)
TGT_DEFINE_STANDARD(NPCBokGlobule, bok_globules, bok_globule_count, GALAXY_OBJECT_MIN_BOK_GLOBULE)
TGT_DEFINE_STANDARD(NPCClumpCore, clump_cores, clump_core_count, GALAXY_OBJECT_MIN_CLUMP_CORE)
TGT_DEFINE_STANDARD(NPCAccretionDisk, accretion_disks, accretion_disk_count, GALAXY_OBJECT_MIN_ACCRETION_DISK)
TGT_DEFINE_STANDARD(NPCRelativisticJet, relativistic_jets, relativistic_jet_count, GALAXY_OBJECT_MIN_RELATIVISTIC_JET)
TGT_DEFINE_STANDARD(NPCShockWave, shock_waves, shock_wave_count, GALAXY_OBJECT_MIN_SHOCK_WAVE)
TGT_DEFINE_STANDARD(NPCStellarBowShock, stellar_bow_shocks, stellar_bow_shock_count, GALAXY_OBJECT_MIN_STELLAR_BOW_SHOCK)
TGT_DEFINE_STANDARD(NPCCosmicVoid, cosmic_voids, cosmic_void_count, GALAXY_OBJECT_MIN_COSMIC_VOID)
TGT_DEFINE_STANDARD(NPCCosmicFilament, cosmic_filaments, cosmic_filament_count, GALAXY_OBJECT_MIN_COSMIC_FILAMENT)
TGT_DEFINE_STANDARD(NPCEventHorizon, event_horizons, event_horizon_count, GALAXY_OBJECT_MIN_EVENT_HORIZON)
TGT_DEFINE_STANDARD(NPCKilonova, kilonovae, kilonova_count, GALAXY_OBJECT_MIN_KILONOVA)
TGT_DEFINE_STANDARD(NPCGravLens, grav_lenses, grav_lens_count, GALAXY_OBJECT_MIN_GRAV_LENS)
TGT_DEFINE_STANDARD(NPCGRB, grbs, grb_count, GALAXY_OBJECT_MIN_GRB)
TGT_DEFINE_STANDARD(NPCGravWave, grav_waves, grav_wave_count, GALAXY_OBJECT_MIN_GRAV_WAVE)
TGT_DEFINE_STANDARD(NPCProtoplanetaryDisk, protoplanetary_disks, protoplanetary_disk_count, GALAXY_OBJECT_MIN_PROTOPLANETARY_DISK)
TGT_DEFINE_STANDARD(NPCDebrisDisk, debris_disks, debris_disk_count, GALAXY_OBJECT_MIN_DEBRIS_DISK)
TGT_DEFINE_STANDARD(NPCPlanetesimal, planetesimals, planetesimal_count, GALAXY_OBJECT_MIN_PLANETESIMAL)
TGT_DEFINE_STANDARD(NPCRoguePlanet, rogue_planets, rogue_planet_count, GALAXY_OBJECT_MIN_ROGUE_PLANET)
TGT_DEFINE_STANDARD(NPCBrownDwarf, brown_dwarfs, brown_dwarf_count, GALAXY_OBJECT_MIN_BROWN_DWARF)
TGT_DEFINE_STANDARD(NPCISO, isos, iso_count, GALAXY_OBJECT_MIN_ISO)
TGT_DEFINE_STANDARD(NPCMagReconn, mag_reconns, mag_reconn_count, GALAXY_OBJECT_MIN_MAG_RECONN)
TGT_DEFINE_STANDARD(NPCCurrentSheet, current_sheets, current_sheet_count, GALAXY_OBJECT_MIN_CURRENT_SHEET)
TGT_DEFINE_STANDARD(NPCHeliosphere, heliospheres, heliosphere_count, GALAXY_OBJECT_MIN_HELIOSPHERE)
TGT_DEFINE_STANDARD(NPCTermShock, term_shocks, term_shock_count, GALAXY_OBJECT_MIN_TERM_SHOCK)
TGT_DEFINE_STANDARD(NPCMagnetosphere, magnetospheres, magnetosphere_count, GALAXY_OBJECT_MIN_MAGNETOSPHERE)
TGT_DEFINE_STANDARD(NPCCosmicString, cosmic_strings, cosmic_string_count, GALAXY_OBJECT_MIN_COSMIC_STRING)
TGT_DEFINE_STANDARD(NPCDomainWall, domain_walls, domain_wall_count, GALAXY_OBJECT_MIN_DOMAIN_WALL)
TGT_DEFINE_STANDARD(NPCDMHalo, dm_halos, dm_halo_count, GALAXY_OBJECT_MIN_DM_HALO)
TGT_DEFINE_STANDARD(NPCIGM, igms, igm_count, GALAXY_OBJECT_MIN_IGM)
TGT_DEFINE_STANDARD(NPCCGM, cgms, cgm_count, GALAXY_OBJECT_MIN_CGM)
TGT_DEFINE_STANDARD(NPCLymanAlpha, lyman_alphas, lyman_alpha_count, GALAXY_OBJECT_MIN_LYMAN_ALPHA)
TGT_DEFINE_STANDARD(NPCCMB, cmbs, cmb_count, GALAXY_OBJECT_MIN_CMB)

/* NPC ships: absolute position is gx/gy/gz; name is the species; cloak check. */
static bool tgt_aiq_NPCShip(const void *e, int q1, int q2, int q3) {
    const NPCShip *o = (const NPCShip *)e;
    return o->active && o->q1 == q1 && o->q2 == q2 && o->q3 == q3;
}
static bool tgt_act_NPCShip(const void *e) {
    const NPCShip *o = (const NPCShip *)e;
    return o->active;
}
static void tgt_pos_NPCShip(const void *e, double *ax, double *ay, double *az) {
    const NPCShip *o = (const NPCShip *)e;
    *ax = o->gx;
    *ay = o->gy;
    *az = o->gz;
}
static void *tgt_find_NPCShip(const QuadrantIndex *q, int tid) {
    for (int k = 0; k < q->npc_count; k++) {
        const NPCShip *o = q->npcs[k];
        if (o && o->id + GALAXY_OBJECT_MIN_NPC == tid) return (void *)o;
    }
    return NULL;
}
static void tgt_name_NPCShip(const void *e, char *buf, size_t len) {
    const NPCShip *o = (const NPCShip *)e;
    snprintf(buf, len, "%s", get_species_name(o->faction));
}
static bool tgt_vis_NPCShip(const void *e, int my_faction) {
    const NPCShip *o = (const NPCShip *)e;
    return !o->is_cloaked || o->faction == my_faction;
}

/* Starbases: name depends on the owning faction. */
static void tgt_name_NPCBase(const void *e, char *buf, size_t len) {
    const NPCBase *o = (const NPCBase *)e;
    snprintf(buf, len, "%s Starbase", get_species_name(o->faction));
}

/* --- The single source of truth for all object ID ranges ---
 * Flags encode exactly which historical chain each range participated in, so
 * replacing the four if/else chains with this table changes no behavior. */
#define TGT_ENTRY(T, ARR, MAXV, FLAGS, IDMIN, IDMAX, NAME) \
    { NAME, (int)IDMIN, (int)IDMAX, (const void *)(ARR), sizeof(T), (size_t)(MAXV), (FLAGS), \
      tgt_aiq_##T, tgt_act_##T, tgt_pos_##T, tgt_find_##T, NULL, NULL }

static const TargetRangeDef TARGET_RANGES[] = {
    { "Tactical Cruiser", GALAXY_OBJECT_MIN_NPC, GALAXY_OBJECT_MAX_NPC, (const void *)npcs,
      sizeof(NPCShip), MAX_NPC, TGT_F_LOCK_VALID | TGT_F_CMD_APR,
      tgt_aiq_NPCShip, tgt_act_NPCShip, tgt_pos_NPCShip, tgt_find_NPCShip, tgt_name_NPCShip, tgt_vis_NPCShip },
    { "Starbase", GALAXY_OBJECT_MIN_STARBASE, GALAXY_OBJECT_MAX_STARBASE, (const void *)bases,
      sizeof(NPCBase), MAX_BASES, TGT_F_LOCK_VALID | TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR | TGT_F_CMD_LOCK,
      tgt_aiq_NPCBase, tgt_act_NPCBase, tgt_pos_NPCBase, tgt_find_NPCBase, tgt_name_NPCBase, NULL },
    TGT_ENTRY(NPCPlanet, planets, MAX_PLANETS, TGT_F_LOCK_VALID | TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR | TGT_F_CMD_LOCK,
              GALAXY_OBJECT_MIN_PLANET, GALAXY_OBJECT_MAX_PLANET, "Planet"),
    TGT_ENTRY(NPCStar, stars_data, MAX_STARS, TGT_F_LOCK_VALID | TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR | TGT_F_CMD_LOCK,
              GALAXY_OBJECT_MIN_STAR, GALAXY_OBJECT_MAX_STAR, "Star"),
    TGT_ENTRY(NPCBlackHole, black_holes, MAX_BH, TGT_F_LOCK_VALID | TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR | TGT_F_CMD_LOCK,
              GALAXY_OBJECT_MIN_BLACKHOLE, GALAXY_OBJECT_MAX_BLACKHOLE, "Black Hole"),
    TGT_ENTRY(NPCNebula, nebulas, MAX_NEBULAS, TGT_F_LOCK_VALID | TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_NEBULA, GALAXY_OBJECT_MAX_NEBULA, "Nebula"),
    TGT_ENTRY(NPCPulsar, pulsars, MAX_PULSARS, TGT_F_LOCK_VALID | TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_PULSAR, GALAXY_OBJECT_MAX_PULSAR, "Pulsar"),
    TGT_ENTRY(NPCQuasar, quasars, MAX_QUASARS, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_QUASAR, GALAXY_OBJECT_MAX_QUASAR, "Quasar"),
    TGT_ENTRY(NPCComet, comets, MAX_COMETS, TGT_F_LOCK_VALID | TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR | TGT_F_CMD_LOCK | TGT_F_CHASE,
              GALAXY_OBJECT_MIN_COMET, GALAXY_OBJECT_MAX_COMET, "Comet"),
    TGT_ENTRY(NPCAsteroid, asteroids, MAX_ASTEROIDS, TGT_F_LOCK_VALID | TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR | TGT_F_CMD_LOCK,
              GALAXY_OBJECT_MIN_ASTEROID, GALAXY_OBJECT_MAX_ASTEROID, "Asteroid"),
    TGT_ENTRY(NPCDerelict, derelicts, MAX_DERELICTS, TGT_F_LOCK_VALID | TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR | TGT_F_CMD_LOCK | TGT_F_LOCK_GLOBAL,
              GALAXY_OBJECT_MIN_DERELICT, GALAXY_OBJECT_MAX_DERELICT, "Derelict"),
    TGT_ENTRY(NPCMine, mines, MAX_MINES, TGT_F_LOCK_VALID | TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR | TGT_F_CMD_LOCK,
              GALAXY_OBJECT_MIN_MINE, GALAXY_OBJECT_MAX_MINE, "Mine"),
    TGT_ENTRY(NPCBuoy, buoys, MAX_BUOYS, TGT_F_LOCK_VALID | TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR | TGT_F_CMD_LOCK,
              GALAXY_OBJECT_MIN_BUOY, GALAXY_OBJECT_MAX_BUOY, "Comm Buoy"),
    TGT_ENTRY(NPCPlatform, platforms, MAX_PLATFORMS, TGT_F_LOCK_VALID | TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR | TGT_F_CMD_LOCK,
              GALAXY_OBJECT_MIN_PLATFORM, GALAXY_OBJECT_MAX_PLATFORM, "Defense Platform"),
    TGT_ENTRY(NPCRift, rifts, MAX_RIFTS, TGT_F_LOCK_VALID | TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR | TGT_F_CMD_LOCK,
              GALAXY_OBJECT_MIN_RIFT, GALAXY_OBJECT_MAX_RIFT, "Spatial Rift"),
    TGT_ENTRY(NPCMonster, monsters, MAX_MONSTERS, TGT_F_LOCK_VALID | TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR | TGT_F_CMD_LOCK | TGT_F_CHASE,
              GALAXY_OBJECT_MIN_MONSTER, GALAXY_OBJECT_MAX_MONSTER, "Monster"),
    TGT_ENTRY(NPCDyson, dysons, MAX_DYSON, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_DYSON, GALAXY_OBJECT_MAX_DYSON, "Dyson Fragment"),
    TGT_ENTRY(NPCHub, hubs, MAX_HUBS, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_HUB, GALAXY_OBJECT_MAX_HUB, "Trading Hub"),
    TGT_ENTRY(NPCRelic, relics, MAX_RELICS, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_RELIC, GALAXY_OBJECT_MAX_RELIC, "Ancient Relic"),
    TGT_ENTRY(NPCRupture, ruptures, MAX_RUPTURES, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_RUPTURE, GALAXY_OBJECT_MAX_RUPTURE, "Subspace Rupture"),
    TGT_ENTRY(NPCSatellite, satellites, MAX_SATELLITES, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_SATELLITE, GALAXY_OBJECT_MAX_SATELLITE, "Satellite"),
    TGT_ENTRY(NPCStorm, storms, MAX_STORMS, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_STORM, GALAXY_OBJECT_MAX_STORM, "Ion Storm"),
    TGT_ENTRY(NPCArtifact, artifacts, MAX_ARTIFACTS, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_ARTIFACT, GALAXY_OBJECT_MAX_ARTIFACT, "Alien Artifact"),
    TGT_ENTRY(NPCWarpGate, warp_gates, MAX_WARP_GATES, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_WARP_GATE, GALAXY_OBJECT_MAX_WARP_GATE, "Warp Gate"),
    TGT_ENTRY(NPCNeutronStar, neutron_stars, MAX_NEUTRON_STARS, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_NEUTRON_STAR, GALAXY_OBJECT_MAX_NEUTRON_STAR, "Neutron Star"),
    TGT_ENTRY(NPCMegaStructure, mega_structs, MAX_MEGA_STRUCTS, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_MEGA_STRUCT, GALAXY_OBJECT_MAX_MEGA_STRUCT, "Mega Structure"),
    TGT_ENTRY(NPCDarkCloud, dark_clouds, MAX_DARK_CLOUDS, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_DARK_CLOUD, GALAXY_OBJECT_MAX_DARK_CLOUD, "Dark Cloud"),
    TGT_ENTRY(NPCSingularity, singularities, MAX_SINGULARITIES, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_SINGULARITY, GALAXY_OBJECT_MAX_SINGULARITY, "Quantum Singularity"),
    TGT_ENTRY(NPCPlasmaStorm, plasma_storms, MAX_PLASMA_STORMS, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_PLASMA_STORM, GALAXY_OBJECT_MAX_PLASMA_STORM, "Plasma Storm"),
    TGT_ENTRY(NPCOrbitalRing, orbital_rings, MAX_ORBITAL_RINGS, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_ORBITAL_RING, GALAXY_OBJECT_MAX_ORBITAL_RING, "Orbital Ring"),
    TGT_ENTRY(NPCTimeAnomaly, time_anomalies, MAX_TIME_ANOMALIES, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_TIME_ANOMALY, GALAXY_OBJECT_MAX_TIME_ANOMALY, "Time Anomaly"),
    TGT_ENTRY(NPCVoidCrystal, void_crystals, MAX_VOID_CRYSTALS, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_VOID_CRYSTAL, GALAXY_OBJECT_MAX_VOID_CRYSTAL, "Void Crystal"),
    TGT_ENTRY(NPCSubspaceAnomaly, subspace_anomalies, MAX_SUBSPACE_ANOMALIES, TGT_F_APR_LOCAL | TGT_F_APR_GLOBAL | TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_SUBSPACE_ANOM, GALAXY_OBJECT_MAX_SUBSPACE_ANOM, "Subspace Anomaly"),
    TGT_ENTRY(NPCDiffuseNebula, diffuse_nebulae, MAX_DIFFUSE_NEBULAE, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_DIFFUSE_NEBULA, GALAXY_OBJECT_MAX_DIFFUSE_NEBULA, "Diffuse Nebula"),
    TGT_ENTRY(NPCDarkNebula, dark_nebulae, MAX_DARK_NEBULAE, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_DARK_NEBULA, GALAXY_OBJECT_MAX_DARK_NEBULA, "Dark Nebula"),
    TGT_ENTRY(NPCPlanetaryNebula, planetary_nebulae, MAX_PLANETARY_NEBULAE, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_PLANETARY_NEBULA, GALAXY_OBJECT_MAX_PLANETARY_NEBULA, "Planetary Neb"),
    TGT_ENTRY(NPCSNR, snrs, MAX_SNR, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_SNR, GALAXY_OBJECT_MAX_SNR, "SNR"),
    TGT_ENTRY(NPCGMC, gmcs, MAX_GMC, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_GMC, GALAXY_OBJECT_MAX_GMC, "GMC"),
    TGT_ENTRY(NPCInterstellarFilament, interstellar_filaments, MAX_INTERSTELLAR_FILAMENTS, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_INTERSTELLAR_FILAMENT, GALAXY_OBJECT_MAX_INTERSTELLAR_FILAMENT, "Int Filament"),
    TGT_ENTRY(NPCInterstellarBubble, interstellar_bubbles, MAX_INTERSTELLAR_BUBBLES, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_INTERSTELLAR_BUBBLE, GALAXY_OBJECT_MAX_INTERSTELLAR_BUBBLE, "Int Bubble"),
    TGT_ENTRY(NPCBokGlobule, bok_globules, MAX_BOK_GLOBULES, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_BOK_GLOBULE, GALAXY_OBJECT_MAX_BOK_GLOBULE, "Bok Globule"),
    TGT_ENTRY(NPCClumpCore, clump_cores, MAX_CLUMP_CORES, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_CLUMP_CORE, GALAXY_OBJECT_MAX_CLUMP_CORE, "Clump/Core"),
    TGT_ENTRY(NPCAccretionDisk, accretion_disks, MAX_ACCRETION_DISKS, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_ACCRETION_DISK, GALAXY_OBJECT_MAX_ACCRETION_DISK, "Accretion Disk"),
    TGT_ENTRY(NPCRelativisticJet, relativistic_jets, MAX_RELATIVISTIC_JETS, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_RELATIVISTIC_JET, GALAXY_OBJECT_MAX_RELATIVISTIC_JET, "Relativ Jet"),
    TGT_ENTRY(NPCShockWave, shock_waves, MAX_SHOCK_WAVES, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_SHOCK_WAVE, GALAXY_OBJECT_MAX_SHOCK_WAVE, "Shock Wave"),
    TGT_ENTRY(NPCStellarBowShock, stellar_bow_shocks, MAX_STELLAR_BOW_SHOCKS, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_STELLAR_BOW_SHOCK, GALAXY_OBJECT_MAX_STELLAR_BOW_SHOCK, "Bow Shock"),
    TGT_ENTRY(NPCCosmicVoid, cosmic_voids, MAX_COSMIC_VOIDS, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_COSMIC_VOID, GALAXY_OBJECT_MAX_COSMIC_VOID, "Cosmic Void"),
    TGT_ENTRY(NPCCosmicFilament, cosmic_filaments, MAX_COSMIC_FILAMENTS, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_COSMIC_FILAMENT, GALAXY_OBJECT_MAX_COSMIC_FILAMENT, "Cosmic Fil"),
    TGT_ENTRY(NPCEventHorizon, event_horizons, MAX_EVENT_HORIZONS, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_EVENT_HORIZON, GALAXY_OBJECT_MAX_EVENT_HORIZON, "Event Horizon"),
    TGT_ENTRY(NPCKilonova, kilonovae, MAX_KILONOVAE, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_KILONOVA, GALAXY_OBJECT_MAX_KILONOVA, "Kilonova"),
    TGT_ENTRY(NPCGravLens, grav_lenses, MAX_GRAV_LENSES, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_GRAV_LENS, GALAXY_OBJECT_MAX_GRAV_LENS, "Grav Lens"),
    TGT_ENTRY(NPCGRB, grbs, MAX_GRB, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_GRB, GALAXY_OBJECT_MAX_GRB, "GRB"),
    TGT_ENTRY(NPCGravWave, grav_waves, MAX_GRAV_WAVES, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_GRAV_WAVE, GALAXY_OBJECT_MAX_GRAV_WAVE, "Grav Wave"),
    TGT_ENTRY(NPCProtoplanetaryDisk, protoplanetary_disks, MAX_PROTOPLANETARY_DISKS, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_PROTOPLANETARY_DISK, GALAXY_OBJECT_MAX_PROTOPLANETARY_DISK, "Protoplanetary"),
    TGT_ENTRY(NPCDebrisDisk, debris_disks, MAX_DEBRIS_DISKS, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_DEBRIS_DISK, GALAXY_OBJECT_MAX_DEBRIS_DISK, "Debris Disk"),
    TGT_ENTRY(NPCPlanetesimal, planetesimals, MAX_PLANETESIMALS, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_PLANETESIMAL, GALAXY_OBJECT_MAX_PLANETESIMAL, "Planetesimal"),
    TGT_ENTRY(NPCRoguePlanet, rogue_planets, MAX_ROGUE_PLANETS, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_ROGUE_PLANET, GALAXY_OBJECT_MAX_ROGUE_PLANET, "Rogue Planet"),
    TGT_ENTRY(NPCBrownDwarf, brown_dwarfs, MAX_BROWN_DWARFS, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_BROWN_DWARF, GALAXY_OBJECT_MAX_BROWN_DWARF, "Brown Dwarf"),
    TGT_ENTRY(NPCISO, isos, MAX_ISO, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_ISO, GALAXY_OBJECT_MAX_ISO, "Interst Obj"),
    TGT_ENTRY(NPCMagReconn, mag_reconns, MAX_MAG_RECONN, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_MAG_RECONN, GALAXY_OBJECT_MAX_MAG_RECONN, "Mag Reconn"),
    TGT_ENTRY(NPCCurrentSheet, current_sheets, MAX_CURRENT_SHEETS, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_CURRENT_SHEET, GALAXY_OBJECT_MAX_CURRENT_SHEET, "Cur Sheet"),
    TGT_ENTRY(NPCHeliosphere, heliospheres, MAX_HELIOSPHERES, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_HELIOSPHERE, GALAXY_OBJECT_MAX_HELIOSPHERE, "Heliosphere"),
    TGT_ENTRY(NPCTermShock, term_shocks, MAX_TERM_SHOCKS, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_TERM_SHOCK, GALAXY_OBJECT_MAX_TERM_SHOCK, "Term Shock"),
    TGT_ENTRY(NPCMagnetosphere, magnetospheres, MAX_MAGNETOSPHERES, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_MAGNETOSPHERE, GALAXY_OBJECT_MAX_MAGNETOSPHERE, "Magnetosphere"),
    TGT_ENTRY(NPCCosmicString, cosmic_strings, MAX_COSMIC_STRINGS, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_COSMIC_STRING, GALAXY_OBJECT_MAX_COSMIC_STRING, "Cosmic String"),
    TGT_ENTRY(NPCDomainWall, domain_walls, MAX_DOMAIN_WALLS, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_DOMAIN_WALL, GALAXY_OBJECT_MAX_DOMAIN_WALL, "Domain Wall"),
    TGT_ENTRY(NPCDMHalo, dm_halos, MAX_DM_HALO, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_DM_HALO, GALAXY_OBJECT_MAX_DM_HALO, "DM Halo"),
    TGT_ENTRY(NPCIGM, igms, MAX_IGM, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_IGM, GALAXY_OBJECT_MAX_IGM, "IGM"),
    TGT_ENTRY(NPCCGM, cgms, MAX_CGM, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_CGM, GALAXY_OBJECT_MAX_CGM, "CGM"),
    TGT_ENTRY(NPCLymanAlpha, lyman_alphas, MAX_LYMAN_ALPHA, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_LYMAN_ALPHA, GALAXY_OBJECT_MAX_LYMAN_ALPHA, "Lyman Alpha"),
    TGT_ENTRY(NPCCMB, cmbs, MAX_CMB, TGT_F_CMD_APR,
              GALAXY_OBJECT_MIN_CMB, GALAXY_OBJECT_MAX_CMB, "CMB"),
};

#undef TGT_ENTRY

/* --- Generic API --------------------------------------------------------- */

/* Range descriptor for a target ID (players and probes are NOT in the
 * table; they are resolved by the call sites, which have their own rules). */
const TargetRangeDef *target_range_for(int tid) {
    for (size_t k = 0; k < sizeof(TARGET_RANGES) / sizeof(TARGET_RANGES[0]); k++) {
        if (tid >= TARGET_RANGES[k].min_id && tid <= TARGET_RANGES[k].max_id) {
            return &TARGET_RANGES[k];
        }
    }
    return NULL;
}

/* Per-tick lock validity: is the target active in the given quadrant?
 * Only the ranges flagged TGT_F_LOCK_VALID participate (behavior of the
 * historical lock-validity chain, which covered 15 static types). */
bool target_is_active_in_quadrant(int tid, int q1, int q2, int q3) {
    const TargetRangeDef *r = target_range_for(tid);
    if (!r || !(r->flags & TGT_F_LOCK_VALID)) return false;
    if ((size_t)(tid - r->min_id) >= r->capacity) return false;
    const void *e = (const char *)r->base + (size_t)(tid - r->min_id) * r->stride;
    return r->active_in_quadrant(e, q1, q2, q3);
}

/* Spatial-index lookup by ID inside a quadrant (no .active check: the
 * historical chains trusted the spatial index content on this path). */
void *target_find_local(const TargetRangeDef *r, const QuadrantIndex *q, int tid) {
    if (!r || !r->find_in_quadrant) return NULL;
    return r->find_in_quadrant(q, tid);
}

/* Global-array lookup: element must be .active (cross-quadrant tracking). */
const void *target_find_global_active(int tid) {
    const TargetRangeDef *r = target_range_for(tid);
    if (!r) return NULL;
    if ((size_t)(tid - r->min_id) >= r->capacity) return NULL;
    const void *e = (const char *)r->base + (size_t)(tid - r->min_id) * r->stride;
    return r->is_active(e) ? e : NULL;
}

void target_abs_pos(const TargetRangeDef *r, const void *e, double *ax, double *ay, double *az) {
    r->abs_pos(e, ax, ay, az);
}

void target_make_name(const TargetRangeDef *r, const void *e, char *buf, size_t len) {
    if (r->make_name) {
        r->make_name(e, buf, len);
    } else {
        snprintf(buf, len, "%s", r->name);
    }
}

bool target_visible(const TargetRangeDef *r, const void *e, int my_faction) {
    return r->visible ? r->visible(e, my_faction) : true;
}
