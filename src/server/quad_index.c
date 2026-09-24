/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
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
 * Static per-quadrant spatial index.
 *
 * B3: before the extraction, init_static_spatial_index() in galaxy.c
 * inserted only 12 of the static types (planets, bases, stars, black
 * holes, nebulas, pulsars, storms, dysons, hubs, relics, ruptures,
 * satellites) and skipped the quasar — so `quasar_count` stayed 0 in
 * every quadrant and the per-frame update, the LRS grid and the
 * LRS/SRS lists never sent a single quasar to the clients (the object
 * the user reported missing from the quadrant and the HUD object
 * list).  The same defect affected the other 49 static types (the
 * anomaly types, net 40-50, and the environment types, net 51-88):
 * generated with a quadrant, read from the quadrant index every
 * frame, inserted nowhere.
 *
 * The table below is the complete list of static types; the insertion
 * algorithm matches the original per-type loops exactly (active
 * check, IS_Q_VALID, per-quadrant cap, and the legacy
 * static_X_count bookkeeping for the types that carry it).
 * Dynamic types (npcs, players, torpedoes, monsters, comets,
 * asteroids, derelicts, mines, buoys, platforms, rifts) are NOT here:
 * they move each tick and are handled by rebuild_spatial_index().
 */

#include "quad_index.h"
#include <stddef.h>

typedef struct {
    QuadStaticTypeInfo pub;
    ptrdiff_t off_static_count; /* -1: the type has no static_X_count */
} QuadStaticDef;

/* name, type, global array, array max, quadrant slot member,
 * quadrant count member, per-quadrant cap. */
#define QI_DEF(name, ty, arr, arr_max, slot_member, cnt_member, slot_max) \
    { { name, (const void *)(arr), sizeof(ty), (arr_max), (slot_max), \
        offsetof(ty, q1), offsetof(ty, q2), offsetof(ty, q3), \
        offsetof(ty, active), \
        offsetof(QuadrantIndex, slot_member), \
        offsetof(QuadrantIndex, cnt_member) }, \
      -1 }

/* Same, for the types whose QuadrantIndex slot carries the legacy
 * static_X_count bookkeeping (written after each successful insert). */
#define QI_DEF_STATIC(name, ty, arr, arr_max, slot_member, cnt_member, \
                      static_member, slot_max) \
    { { name, (const void *)(arr), sizeof(ty), (arr_max), (slot_max), \
        offsetof(ty, q1), offsetof(ty, q2), offsetof(ty, q3), \
        offsetof(ty, active), \
        offsetof(QuadrantIndex, slot_member), \
        offsetof(QuadrantIndex, cnt_member) }, \
      offsetof(QuadrantIndex, static_member) }

/* Every static object type, in the order the QuadrantIndex struct
 * lays out its slots. The first twelve are the legacy set (inserted
 * before the B3 fix); the rest were missing. */
static const QuadStaticDef quad_static_defs[] = {
    /* --- legacy 12 (original order of init_static_spatial_index) --- */
    QI_DEF_STATIC("planet",       NPCPlanet,        planets,        MAX_PLANETS,        planets,        planet_count,        static_planet_count,  MAX_Q_PLANETS),
    QI_DEF_STATIC("base",         NPCBase,          bases,          MAX_BASES,          bases,          base_count,          static_base_count,    MAX_Q_BASES),
    QI_DEF_STATIC("star",         NPCStar,          stars_data,     MAX_STARS,          stars,          star_count,          static_star_count,    MAX_Q_STARS),
    QI_DEF_STATIC("black_hole",   NPCBlackHole,     black_holes,    MAX_BH,             black_holes,    bh_count,            static_bh_count,      MAX_Q_BH),
    QI_DEF_STATIC("nebula",       NPCNebula,        nebulas,        MAX_NEBULAS,        nebulas,        nebula_count,        static_nebula_count,  MAX_Q_NEBULAS),
    QI_DEF_STATIC("pulsar",       NPCPulsar,        pulsars,        MAX_PULSARS,        pulsars,        pulsar_count,        static_pulsar_count,  MAX_Q_PULSARS),
    QI_DEF("storm",        NPCStorm,         storms,         MAX_STORMS,         storms,         storm_count,         MAX_Q_STORMS),
    QI_DEF("dyson",        NPCDyson,         dysons,         MAX_DYSON,          dysons,         dyson_count,         MAX_Q_DYSON),
    QI_DEF("hub",          NPCHub,           hubs,           MAX_HUBS,           hubs,           hub_count,           MAX_Q_HUBS),
    QI_DEF("relic",        NPCRelic,         relics,         MAX_RELICS,         relics,         relic_count,         MAX_Q_RELICS),
    QI_DEF("rupture",      NPCRupture,       ruptures,       MAX_RUPTURES,       ruptures,       rupture_count,       MAX_Q_RUPTURES),
    QI_DEF("satellite",    NPCSatellite,     satellites,     MAX_SATELLITES,     satellites,     satellite_count,     MAX_Q_SATELLITES),
    /* --- B3: the reported missing type --- */
    QI_DEF_STATIC("quasar",       NPCQuasar,        quasars,        MAX_QUASARS,        quasars,        quasar_count,        static_quasar_count,  MAX_Q_QUASARS),
    /* --- anomaly types (net 40-50) --- */
    QI_DEF("artifact",       NPCArtifact,        artifacts,        MAX_ARTIFACTS,        artifacts,        artifact_count,        MAX_Q_ARTIFACTS),
    QI_DEF("warp_gate",      NPCWarpGate,        warp_gates,       MAX_WARP_GATES,       warp_gates,       warp_gate_count,       MAX_Q_WARP_GATES),
    QI_DEF("neutron_star",   NPCNeutronStar,     neutron_stars,    MAX_NEUTRON_STARS,    neutron_stars,    neutron_star_count,    MAX_Q_NEUTRON_STARS),
    QI_DEF("mega_structure", NPCMegaStructure,   mega_structs,     MAX_MEGA_STRUCTS,     mega_structs,     mega_struct_count,     MAX_Q_MEGA_STRUCTS),
    QI_DEF("dark_cloud",     NPCDarkCloud,       dark_clouds,      MAX_DARK_CLOUDS,      dark_clouds,      dark_cloud_count,      MAX_Q_DARK_CLOUDS),
    QI_DEF("singularity",    NPCSingularity,     singularities,    MAX_SINGULARITIES,    singularities,    singularity_count,     MAX_Q_SINGULARITIES),
    QI_DEF("plasma_storm",   NPCPlasmaStorm,     plasma_storms,    MAX_PLASMA_STORMS,    plasma_storms,    plasma_storm_count,    MAX_Q_PLASMA_STORMS),
    QI_DEF("orbital_ring",   NPCOrbitalRing,     orbital_rings,    MAX_ORBITAL_RINGS,    orbital_rings,    orbital_ring_count,    MAX_Q_ORBITAL_RINGS),
    QI_DEF("time_anomaly",   NPCTimeAnomaly,     time_anomalies,   MAX_TIME_ANOMALIES,   time_anomalies,   time_anomaly_count,    MAX_Q_TIME_ANOMALIES),
    QI_DEF("void_crystal",   NPCVoidCrystal,     void_crystals,    MAX_VOID_CRYSTALS,    void_crystals,    void_crystal_count,    MAX_Q_VOID_CRYSTALS),
    QI_DEF("subspace_anomaly", NPCSubspaceAnomaly, subspace_anomalies, MAX_SUBSPACE_ANOMALIES, subspace_anomalies, subspace_anomaly_count, MAX_Q_SUBSPACE_ANOMALIES),
    /* --- environment types (net 51-88) --- */
    QI_DEF("diffuse_nebula", NPCDiffuseNebula,   diffuse_nebulae,  MAX_DIFFUSE_NEBULAE,  diffuse_nebulae,  diffuse_nebula_count,  MAX_Q_DIFFUSE_NEBULA),
    QI_DEF("dark_nebula",    NPCDarkNebula,      dark_nebulae,     MAX_DARK_NEBULAE,     dark_nebulae,     dark_nebula_count,     MAX_Q_DARK_NEBULA),
    QI_DEF("planetary_nebula", NPCPlanetaryNebula, planetary_nebulae, MAX_PLANETARY_NEBULAE, planetary_nebulae, planetary_nebula_count, MAX_Q_PLANETARY_NEBULA),
    QI_DEF("snr",            NPCSNR,             snrs,             MAX_SNR,              snrs,             snr_count,             MAX_Q_SNR),
    QI_DEF("gmc",            NPCGMC,             gmcs,             MAX_GMC,              gmcs,             gmc_count,             MAX_Q_GMC),
    QI_DEF("interstellar_filament", NPCInterstellarFilament, interstellar_filaments, MAX_INTERSTELLAR_FILAMENTS, interstellar_filaments, interstellar_filament_count, MAX_Q_INTERSTELLAR_FILAMENT),
    QI_DEF("interstellar_bubble", NPCInterstellarBubble, interstellar_bubbles, MAX_INTERSTELLAR_BUBBLES, interstellar_bubbles, interstellar_bubble_count, MAX_Q_INTERSTELLAR_BUBBLE),
    QI_DEF("bok_globule",   NPCBokGlobule,      bok_globules,     MAX_BOK_GLOBULES,     bok_globules,     bok_globule_count,     MAX_Q_BOK_GLOBULE),
    QI_DEF("clump_core",     NPCClumpCore,       clump_cores,      MAX_CLUMP_CORES,      clump_cores,      clump_core_count,      MAX_Q_CLUMP_CORE),
    QI_DEF("accretion_disk", NPCAccretionDisk,   accretion_disks,  MAX_ACCRETION_DISKS,  accretion_disks,  accretion_disk_count,  MAX_Q_ACCRETION_DISK),
    QI_DEF("relativistic_jet", NPCRelativisticJet, relativistic_jets, MAX_RELATIVISTIC_JETS, relativistic_jets, relativistic_jet_count, MAX_Q_RELATIVISTIC_JET),
    QI_DEF("shock_wave",     NPCShockWave,       shock_waves,      MAX_SHOCK_WAVES,      shock_waves,      shock_wave_count,      MAX_Q_SHOCK_WAVE),
    QI_DEF("stellar_bow_shock", NPCStellarBowShock, stellar_bow_shocks, MAX_STELLAR_BOW_SHOCKS, stellar_bow_shocks, stellar_bow_shock_count, MAX_Q_STELLAR_BOW_SHOCK),
    QI_DEF("cosmic_void",    NPCCosmicVoid,      cosmic_voids,     MAX_COSMIC_VOIDS,     cosmic_voids,     cosmic_void_count,     MAX_Q_COSMIC_VOID),
    QI_DEF("cosmic_filament", NPCCosmicFilament,  cosmic_filaments, MAX_COSMIC_FILAMENTS, cosmic_filaments,  cosmic_filament_count,  MAX_Q_COSMIC_FILAMENT),
    QI_DEF("event_horizon",  NPCEventHorizon,    event_horizons,   MAX_EVENT_HORIZONS,   event_horizons,   event_horizon_count,   MAX_Q_EVENT_HORIZON),
    QI_DEF("kilonova",       NPCKilonova,        kilonovae,        MAX_KILONOVAE,        kilonovae,        kilonova_count,        MAX_Q_KILONOVA),
    QI_DEF("grav_lens",      NPCGravLens,        grav_lenses,      MAX_GRAV_LENSES,      grav_lenses,      grav_lens_count,       MAX_Q_GRAV_LENS),
    QI_DEF("grb",            NPCGRB,             grbs,             MAX_GRB,              grbs,             grb_count,             MAX_Q_GRB),
    QI_DEF("grav_wave",      NPCGravWave,        grav_waves,       MAX_GRAV_WAVES,       grav_waves,       grav_wave_count,       MAX_Q_GRAV_WAVE),
    QI_DEF("protoplanetary_disk", NPCProtoplanetaryDisk, protoplanetary_disks, MAX_PROTOPLANETARY_DISKS, protoplanetary_disks, protoplanetary_disk_count, MAX_Q_PROTOPLANETARY_DISK),
    QI_DEF("debris_disk",    NPCDebrisDisk,      debris_disks,     MAX_DEBRIS_DISKS,     debris_disks,     debris_disk_count,     MAX_Q_DEBRIS_DISK),
    QI_DEF("planetesimal",   NPCPlanetesimal,    planetesimals,    MAX_PLANETESIMALS,    planetesimals,    planetesimal_count,    MAX_Q_PLANETESIMAL),
    QI_DEF("rogue_planet",   NPCRoguePlanet,     rogue_planets,    MAX_ROGUE_PLANETS,    rogue_planets,    rogue_planet_count,    MAX_Q_ROGUE_PLANET),
    QI_DEF("brown_dwarf",    NPCBrownDwarf,      brown_dwarfs,     MAX_BROWN_DWARFS,     brown_dwarfs,     brown_dwarf_count,     MAX_Q_BROWN_DWARF),
    QI_DEF("iso",            NPCISO,             isos,             MAX_ISO,              isos,             iso_count,             MAX_Q_ISO),
    QI_DEF("mag_reconn",     NPCMagReconn,       mag_reconns,      MAX_MAG_RECONN,       mag_reconns,      mag_reconn_count,      MAX_Q_MAG_RECONN),
    QI_DEF("current_sheet",  NPCCurrentSheet,    current_sheets,   MAX_CURRENT_SHEETS,   current_sheets,   current_sheet_count,   MAX_Q_CURRENT_SHEET),
    QI_DEF("heliosphere",    NPCHeliosphere,     heliospheres,     MAX_HELIOSPHERES,     heliospheres,     heliosphere_count,     MAX_Q_HELIOSPHERE),
    QI_DEF("term_shock",     NPCTermShock,       term_shocks,      MAX_TERM_SHOCKS,      term_shocks,      term_shock_count,      MAX_Q_TERM_SHOCK),
    QI_DEF("magnetosphere",  NPCMagnetosphere,   magnetospheres,   MAX_MAGNETOSPHERES,   magnetospheres,   magnetosphere_count,   MAX_Q_MAGNETOSPHERE),
    QI_DEF("cosmic_string",  NPCCosmicString,    cosmic_strings,   MAX_COSMIC_STRINGS,   cosmic_strings,   cosmic_string_count,   MAX_Q_COSMIC_STRING),
    QI_DEF("domain_wall",    NPCDomainWall,      domain_walls,     MAX_DOMAIN_WALLS,     domain_walls,     domain_wall_count,     MAX_Q_DOMAIN_WALL),
    QI_DEF("dm_halo",        NPCDMHalo,          dm_halos,         MAX_DM_HALO,          dm_halos,         dm_halo_count,         MAX_Q_DM_HALO),
    QI_DEF("igm",            NPCIGM,             igms,             MAX_IGM,              igms,             igm_count,             MAX_Q_IGM),
    QI_DEF("cgm",            NPCCGM,             cgms,             MAX_CGM,              cgms,             cgm_count,             MAX_Q_CGM),
    QI_DEF("lyman_alpha",    NPCLymanAlpha,      lyman_alphas,     MAX_LYMAN_ALPHA,      lyman_alphas,     lyman_alpha_count,     MAX_Q_LYMAN_ALPHA),
    QI_DEF("cmb",            NPCCMB,             cmbs,             MAX_CMB,              cmbs,             cmb_count,             MAX_Q_CMB),
};

#define QI_STATIC_TYPE_COUNT (int)(sizeof(quad_static_defs) / sizeof(quad_static_defs[0]))

/* Read an int field at a compile-time offset (the field really is an
 * int in the production struct; the offset was resolved with
 * offsetof at table build time). */
static int qi_field(const void *obj, ptrdiff_t off)
{
    return *(const int *)((const char *)obj + off);
}

void quad_index_insert_static_all(QuadrantIndex (*grid)[41][41])
{
    for (int d = 0; d < QI_STATIC_TYPE_COUNT; d++) {
        const QuadStaticDef *def = &quad_static_defs[d];
        const QuadStaticTypeInfo *ti = &def->pub;
        const char *base = (const char *)ti->objects;
        for (int i = 0; i < ti->total; i++) {
            const char *obj = base + (size_t)i * ti->obj_size;
            if (qi_field(obj, ti->off_active) == 0) continue;
            int a = qi_field(obj, ti->off_q1);
            int b = qi_field(obj, ti->off_q2);
            int c = qi_field(obj, ti->off_q3);
            if (!IS_Q_VALID(a, b, c)) continue;
            QuadrantIndex *q = &grid[a][b][c];
            void **slots = (void **)((char *)q + ti->off_slots);
            int *count = (int *)((char *)q + ti->off_count);
            if (*count >= ti->slot_max) continue;
            slots[(*count)++] = (void *)obj;
            if (def->off_static_count >= 0)
                *(int *)((char *)q + def->off_static_count) = *count;
        }
    }
}

int quad_index_static_type_count(void)
{
    return QI_STATIC_TYPE_COUNT;
}

const QuadStaticTypeInfo *quad_index_static_type_info(int index)
{
    if (index < 0 || index >= QI_STATIC_TYPE_COUNT) return NULL;
    return &quad_static_defs[index].pub;
}

int quad_index_static_type_find(const char *name)
{
    for (int d = 0; d < QI_STATIC_TYPE_COUNT; d++)
        if (quad_static_defs[d].pub.name == name) return d;
    return -1;
}
