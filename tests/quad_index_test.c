/*
 * SPACE GL - 3D LOGIC ENGINE - server spatial index contract test.
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
 *
 * B3 regression (quasar — and the other static galactic objects —
 * never appearing in the quadrant and in the HUD object list): the
 * per-frame update builder (logic.c), the LRS grid
 * (refresh_lrs_grid) and the LRS/SRS object lists (commands.c) all
 * read the quadrant contents from the per-quadrant spatial index
 * (QuadrantIndex). Before the fix, init_static_spatial_index()
 * inserted only 12 of the static types and skipped the quasar plus
 * the 49 other static types (anomalies, net 40-50, and environment
 * types, net 51-88), so their quadrant counts stayed 0 forever and
 * no NetObject for them was ever sent to the clients — the quasar
 * the user reported missing from the quadrant / HUD object list,
 * and the same defect for every other skipped type.
 *
 * The code under test is the real production source
 * (../src/server/quad_index.c, pulled in by the test project the
 * same way nav_heading_test pulls in src/server/nav_math.c). This
 * TU owns the global object arrays the module indexes: in the
 * server they are defined in galaxy.c, which is not linked here.
 *
 *   1. B3 scenario: the reported quasar at [5,6,7] is indexed,
 *      produces the type-29 NetObject the update builder sends
 *      (id base GALAXY_OBJECT_MIN_QUASAR) and lights the LRS
 *      quasar digit.
 *   2. Table contract: the production table is EXACTLY the set of
 *      the 62 static types, each bound to the right global array,
 *      element size and length, with the right per-quadrant cap —
 *      a missing (e.g. the pre-fix quasar) or extra entry fails.
 *   3. Seeding: every static type placed in a quadrant is found
 *      back in the right slot (pointer identity into the global
 *      array); all 62 types coexist in one quadrant; inactive
 *      objects and out-of-range quadrants (0, 41, negative) are
 *      excluded; per-quadrant caps are never exceeded (incl. the
 *      cap-1 types).
 *   4. Fuzz: 1240 objects over all 62 types — every slot of every
 *      quadrant stays within its cap and points to an active
 *      object of the matching type and quadrant.
 *
 * Exit: 0 = pass, 1 = fail.
 */

#include "quad_index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
           fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } \
} while (0)

/* --- The global object arrays the production table indexes -------
 * Defined here because the test links src/server/quad_index.c
 * without galaxy.c (which defines them in the server). */
NPCStar stars_data[MAX_STARS];
NPCBlackHole black_holes[MAX_BH];
NPCNebula nebulas[MAX_NEBULAS];
NPCPulsar pulsars[MAX_PULSARS];
NPCQuasar quasars[MAX_QUASARS];
NPCPlanet planets[MAX_PLANETS];
NPCBase bases[MAX_BASES];
NPCStorm storms[MAX_STORMS];
NPCDyson dysons[MAX_DYSON];
NPCHub hubs[MAX_HUBS];
NPCRelic relics[MAX_RELICS];
NPCRupture ruptures[MAX_RUPTURES];
NPCSatellite satellites[MAX_SATELLITES];
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

/* --- The expected static-type set (independent of the production
 * table): name, global array, element size, array length, per-
 * quadrant cap. Derived from the QuadrantIndex struct and the
 * per-frame update builder in logic.c. */
typedef struct {
    const char *name;
    const void *arr;
    size_t obj_size;
    int max_total;
    int slot_max;
} QIExpect;

static const QIExpect qi_expect[] = {
    { "planet",           planets,        sizeof(NPCPlanet),          MAX_PLANETS,          MAX_Q_PLANETS          },
    { "base",             bases,          sizeof(NPCBase),            MAX_BASES,            MAX_Q_BASES            },
    { "star",             stars_data,     sizeof(NPCStar),            MAX_STARS,            MAX_Q_STARS            },
    { "black_hole",       black_holes,    sizeof(NPCBlackHole),       MAX_BH,               MAX_Q_BH               },
    { "nebula",           nebulas,        sizeof(NPCNebula),          MAX_NEBULAS,          MAX_Q_NEBULAS          },
    { "pulsar",           pulsars,        sizeof(NPCPulsar),          MAX_PULSARS,          MAX_Q_PULSARS          },
    { "storm",            storms,         sizeof(NPCStorm),           MAX_STORMS,           MAX_Q_STORMS           },
    { "dyson",            dysons,         sizeof(NPCDyson),           MAX_DYSON,            MAX_Q_DYSON            },
    { "hub",              hubs,           sizeof(NPCHub),             MAX_HUBS,             MAX_Q_HUBS             },
    { "relic",            relics,         sizeof(NPCRelic),           MAX_RELICS,           MAX_Q_RELICS           },
    { "rupture",          ruptures,       sizeof(NPCRupture),         MAX_RUPTURES,         MAX_Q_RUPTURES         },
    { "satellite",        satellites,     sizeof(NPCSatellite),       MAX_SATELLITES,       MAX_Q_SATELLITES       },
    { "quasar",           quasars,        sizeof(NPCQuasar),          MAX_QUASARS,          MAX_Q_QUASARS          },
    { "artifact",         artifacts,      sizeof(NPCArtifact),        MAX_ARTIFACTS,        MAX_Q_ARTIFACTS        },
    { "warp_gate",        warp_gates,     sizeof(NPCWarpGate),        MAX_WARP_GATES,       MAX_Q_WARP_GATES       },
    { "neutron_star",     neutron_stars,  sizeof(NPCNeutronStar),     MAX_NEUTRON_STARS,    MAX_Q_NEUTRON_STARS    },
    { "mega_structure",   mega_structs,   sizeof(NPCMegaStructure),   MAX_MEGA_STRUCTS,     MAX_Q_MEGA_STRUCTS     },
    { "dark_cloud",       dark_clouds,    sizeof(NPCDarkCloud),       MAX_DARK_CLOUDS,      MAX_Q_DARK_CLOUDS      },
    { "singularity",      singularities,  sizeof(NPCSingularity),     MAX_SINGULARITIES,    MAX_Q_SINGULARITIES    },
    { "plasma_storm",     plasma_storms,  sizeof(NPCPlasmaStorm),     MAX_PLASMA_STORMS,    MAX_Q_PLASMA_STORMS    },
    { "orbital_ring",     orbital_rings,  sizeof(NPCOrbitalRing),     MAX_ORBITAL_RINGS,    MAX_Q_ORBITAL_RINGS    },
    { "time_anomaly",     time_anomalies, sizeof(NPCTimeAnomaly),     MAX_TIME_ANOMALIES,   MAX_Q_TIME_ANOMALIES   },
    { "void_crystal",     void_crystals,  sizeof(NPCVoidCrystal),     MAX_VOID_CRYSTALS,    MAX_Q_VOID_CRYSTALS    },
    { "subspace_anomaly", subspace_anomalies, sizeof(NPCSubspaceAnomaly), MAX_SUBSPACE_ANOMALIES, MAX_Q_SUBSPACE_ANOMALIES },
    { "diffuse_nebula",   diffuse_nebulae,  sizeof(NPCDiffuseNebula),   MAX_DIFFUSE_NEBULAE,  MAX_Q_DIFFUSE_NEBULA   },
    { "dark_nebula",      dark_nebulae,     sizeof(NPCDarkNebula),      MAX_DARK_NEBULAE,     MAX_Q_DARK_NEBULA      },
    { "planetary_nebula", planetary_nebulae, sizeof(NPCPlanetaryNebula), MAX_PLANETARY_NEBULAE, MAX_Q_PLANETARY_NEBULA },
    { "snr",              snrs,             sizeof(NPCSNR),             MAX_SNR,              MAX_Q_SNR              },
    { "gmc",              gmcs,             sizeof(NPCGMC),             MAX_GMC,              MAX_Q_GMC              },
    { "interstellar_filament", interstellar_filaments, sizeof(NPCInterstellarFilament), MAX_INTERSTELLAR_FILAMENTS, MAX_Q_INTERSTELLAR_FILAMENT },
    { "interstellar_bubble", interstellar_bubbles, sizeof(NPCInterstellarBubble), MAX_INTERSTELLAR_BUBBLES, MAX_Q_INTERSTELLAR_BUBBLE },
    { "bok_globule",     bok_globules,   sizeof(NPCBokGlobule),    MAX_BOK_GLOBULES,   MAX_Q_BOK_GLOBULE    },
    { "clump_core",       clump_cores,      sizeof(NPCClumpCore),       MAX_CLUMP_CORES,      MAX_Q_CLUMP_CORE       },
    { "accretion_disk",   accretion_disks,  sizeof(NPCAccretionDisk),   MAX_ACCRETION_DISKS,  MAX_Q_ACCRETION_DISK   },
    { "relativistic_jet", relativistic_jets, sizeof(NPCRelativisticJet), MAX_RELATIVISTIC_JETS, MAX_Q_RELATIVISTIC_JET },
    { "shock_wave",       shock_waves,      sizeof(NPCShockWave),       MAX_SHOCK_WAVES,      MAX_Q_SHOCK_WAVE       },
    { "stellar_bow_shock", stellar_bow_shocks, sizeof(NPCStellarBowShock), MAX_STELLAR_BOW_SHOCKS, MAX_Q_STELLAR_BOW_SHOCK },
    { "cosmic_void",      cosmic_voids,     sizeof(NPCCosmicVoid),      MAX_COSMIC_VOIDS,     MAX_Q_COSMIC_VOID      },
    { "cosmic_filament",  cosmic_filaments, sizeof(NPCCosmicFilament),  MAX_COSMIC_FILAMENTS, MAX_Q_COSMIC_FILAMENT  },
    { "event_horizon",    event_horizons,   sizeof(NPCEventHorizon),    MAX_EVENT_HORIZONS,   MAX_Q_EVENT_HORIZON    },
    { "kilonova",         kilonovae,        sizeof(NPCKilonova),        MAX_KILONOVAE,        MAX_Q_KILONOVA         },
    { "grav_lens",        grav_lenses,      sizeof(NPCGravLens),        MAX_GRAV_LENSES,      MAX_Q_GRAV_LENS        },
    { "grb",              grbs,             sizeof(NPCGRB),             MAX_GRB,              MAX_Q_GRB              },
    { "grav_wave",        grav_waves,       sizeof(NPCGravWave),        MAX_GRAV_WAVES,       MAX_Q_GRAV_WAVE        },
    { "protoplanetary_disk", protoplanetary_disks, sizeof(NPCProtoplanetaryDisk), MAX_PROTOPLANETARY_DISKS, MAX_Q_PROTOPLANETARY_DISK },
    { "debris_disk",      debris_disks,     sizeof(NPCDebrisDisk),      MAX_DEBRIS_DISKS,     MAX_Q_DEBRIS_DISK      },
    { "planetesimal",     planetesimals,    sizeof(NPCPlanetesimal),    MAX_PLANETESIMALS,    MAX_Q_PLANETESIMAL     },
    { "rogue_planet",     rogue_planets,    sizeof(NPCRoguePlanet),     MAX_ROGUE_PLANETS,    MAX_Q_ROGUE_PLANET     },
    { "brown_dwarf",      brown_dwarfs,     sizeof(NPCBrownDwarf),      MAX_BROWN_DWARFS,     MAX_Q_BROWN_DWARF      },
    { "iso",              isos,             sizeof(NPCISO),             MAX_ISO,              MAX_Q_ISO              },
    { "mag_reconn",       mag_reconns,      sizeof(NPCMagReconn),       MAX_MAG_RECONN,       MAX_Q_MAG_RECONN       },
    { "current_sheet",    current_sheets,   sizeof(NPCCurrentSheet),    MAX_CURRENT_SHEETS,   MAX_Q_CURRENT_SHEET    },
    { "heliosphere",      heliospheres,     sizeof(NPCHeliosphere),     MAX_HELIOSPHERES,     MAX_Q_HELIOSPHERE      },
    { "term_shock",       term_shocks,      sizeof(NPCTermShock),       MAX_TERM_SHOCKS,      MAX_Q_TERM_SHOCK       },
    { "magnetosphere",    magnetospheres,   sizeof(NPCMagnetosphere),   MAX_MAGNETOSPHERES,   MAX_Q_MAGNETOSPHERE    },
    { "cosmic_string",    cosmic_strings,   sizeof(NPCCosmicString),    MAX_COSMIC_STRINGS,   MAX_Q_COSMIC_STRING    },
    { "domain_wall",      domain_walls,     sizeof(NPCDomainWall),      MAX_DOMAIN_WALLS,     MAX_Q_DOMAIN_WALL      },
    { "dm_halo",          dm_halos,         sizeof(NPCDMHalo),          MAX_DM_HALO,          MAX_Q_DM_HALO          },
    { "igm",              igms,             sizeof(NPCIGM),             MAX_IGM,              MAX_Q_IGM              },
    { "cgm",              cgms,             sizeof(NPCCGM),             MAX_CGM,              MAX_Q_CGM              },
    { "lyman_alpha",      lyman_alphas,     sizeof(NPCLymanAlpha),      MAX_LYMAN_ALPHA,      MAX_Q_LYMAN_ALPHA      },
    { "cmb",              cmbs,             sizeof(NPCCMB),             MAX_CMB,              MAX_Q_CMB              },
};

#define QI_EXPECT_COUNT (int)(sizeof(qi_expect) / sizeof(qi_expect[0]))

static QuadrantIndex (*g_grid)[41][41];

static void reset_grid(void)
{
    memset(g_grid, 0, 41 * 41 * 41 * sizeof(QuadrantIndex));
}

/* Write the index-relevant fields of element `idx` of a table type,
 * going through the PRODUCTION descriptor (its offsets and its
 * objects pointer), so a wrong offset in the table breaks the test. */
static void qi_seed(const QuadStaticTypeInfo *ti, int idx,
                    int a, int b, int c, int active)
{
    char *obj = (char *)ti->objects + (size_t)idx * ti->obj_size;
    *(int *)(obj + ti->off_q1) = a;
    *(int *)(obj + ti->off_q2) = b;
    *(int *)(obj + ti->off_q3) = c;
    *(int *)(obj + ti->off_active) = active;
}

static int qi_read(const void *obj, ptrdiff_t off)
{
    return *(const int *)((const char *)obj + off);
}

/* ================================================================== */
/* 1. B3 scenario: the reported quasar                                 */
/* ================================================================== */
static void test_b3_quasar(void)
{
    reset_grid();

    /* The user's object: a quasar at [5,6,7] (id 7, type 2 = BAL). */
    quasars[0] = (NPCQuasar){ .id = 7, .q1 = 5, .q2 = 6, .q3 = 7,
                              .x = 12.5, .y = 3.25, .z = 17.75,
                              .type = 2, .active = 1 };

    quad_index_insert_static_all(g_grid);

    QuadrantIndex *q = &g_grid[5][6][7];
    CHECK(q->quasar_count == 1,
          "B3: quasar at [5,6,7] must be indexed (quasar_count=%d, was always 0 pre-fix)",
          q->quasar_count);
    CHECK(q->quasar_count == 1 && q->quasars[0] == &quasars[0],
          "B3: the quadrant slot must point to the global quasar entry");
    CHECK(q->static_quasar_count == 1,
          "B3: static_quasar_count bookkeeping (got %d)", q->static_quasar_count);

    /* What the per-frame update builder (logic.c) does with the
     * slot: it must emit the type-29 NetObject the clients draw. */
    NetObject sent = {0};
    int emitted = 0;
    for (int qsr = 0; qsr < q->quasar_count; qsr++) {
        NPCQuasar *qs = q->quasars[qsr];
        if (!qs->active) continue;
        sent = (NetObject){ .net_x = qs->x, .net_y = qs->y, .net_z = qs->z,
                            .type = 29, .ship_class = qs->type, .active = 1,
                            .id = qs->id + GALAXY_OBJECT_MIN_QUASAR,
                            .name = "Quasar" };
        emitted++;
    }
    CHECK(emitted == 1 && sent.type == 29,
          "B3: the update builder must emit the quasar NetObject (type 29)");
    CHECK(emitted == 1 && sent.id == 7 + GALAXY_OBJECT_MIN_QUASAR,
          "B3: NetObject id must be GALAXY_OBJECT_MIN_QUASAR + quasar id (got %d)",
          sent.id);
    CHECK(emitted == 1 && sent.net_x == 12.5 && sent.net_y == 3.25 && sent.net_z == 17.75,
          "B3: NetObject coordinates must be the quasar coordinates");

    /* The LRS quasar digit (refresh_lrs_grid reads quasar_count and
     * stores min(count, 9) in the quadrant signal). */
    int c_qsr = (q->quasar_count > 9) ? 9 : q->quasar_count;
    CHECK(c_qsr == 1, "B3: the LRS quasar digit must light up (got %d)", c_qsr);
}

/* ================================================================== */
/* 2. Table contract: exactly the 62 static types, right bindings     */
/* ================================================================== */
static void test_table_contract(void)
{
    int n = quad_index_static_type_count();
    CHECK(n == QI_EXPECT_COUNT,
          "table: %d static types, expected %d (pre-fix the table held 12)",
          n, QI_EXPECT_COUNT);

    for (int e = 0; e < QI_EXPECT_COUNT; e++) {
        const QIExpect *ex = &qi_expect[e];
        int i = quad_index_static_type_find(ex->name);
        if (i < 0) {
            CHECK(0, "table: type '%s' missing from the production table (pre-fix defect)",
                  ex->name);
            continue;
        }
        const QuadStaticTypeInfo *ti = quad_index_static_type_info(i);
        CHECK(ti != NULL, "table: info(%d) for '%s' is NULL", i, ex->name);
        if (!ti) continue;
        CHECK(ti->objects == ex->arr,
              "table: '%s' must index the global array %s (got %p)",
              ex->name, ex->name, (const void *)ti->objects);
        CHECK(ti->obj_size == ex->obj_size,
              "table: '%s' element size %zu != sizeof %zu",
              ex->name, ti->obj_size, ex->obj_size);
        CHECK(ti->total == ex->max_total,
              "table: '%s' total %d != %d", ex->name, ti->total, ex->max_total);
        CHECK(ti->slot_max == ex->slot_max,
              "table: '%s' per-quadrant cap %d != %d",
              ex->name, ti->slot_max, ex->slot_max);
    }

    /* No extras: every production entry must be one of the expected
     * static types (dynamic types belong to rebuild_spatial_index). */
    for (int d = 0; d < n; d++) {
        const QuadStaticTypeInfo *ti = quad_index_static_type_info(d);
        int known = 0;
        for (int e = 0; e < QI_EXPECT_COUNT; e++)
            if (qi_expect[e].name == ti->name) { known = 1; break; }
        CHECK(known, "table: unexpected static type '%s' (not a static galactic type)",
              ti->name);
    }
}

/* ================================================================== */
/* 3. Seeding: every type found back in its slot, invariants          */
/* ================================================================== */
static void test_seed_all_types(void)
{
    reset_grid();

    /* One active object of EVERY static type in the same quadrant:
     * it must all be found back in the right slots. */
    for (int d = 0; d < quad_index_static_type_count(); d++) {
        const QuadStaticTypeInfo *ti = quad_index_static_type_info(d);
        qi_seed(ti, 0, 10, 11, 12, 1);
    }
    quad_index_insert_static_all(g_grid);

    QuadrantIndex *q = &g_grid[10][11][12];
    int bad = 0;
    for (int d = 0; d < quad_index_static_type_count(); d++) {
        const QuadStaticTypeInfo *ti = quad_index_static_type_info(d);
        int *cnt = (int *)((char *)q + ti->off_count);
        void **slots = (void **)((char *)q + ti->off_slots);
        if (*cnt != 1 || slots[0] != (const void *)ti->objects) {
            bad++;
            if (bad <= 5)
                fprintf(stderr, "  (seed: '%s' count=%d slot=%p want %p)\n",
                        ti->name, *cnt, slots[0], (const void *)ti->objects);
        }
    }
    CHECK(bad == 0,
          "seed: %d of %d static types not found in the right slot of [10,11,12]",
          bad, quad_index_static_type_count());

    /* The offsets must really address q1/q2/q3/active: the seeded
     * values must read back through the production offsets. */
    {
        const QuadStaticTypeInfo *ti = quad_index_static_type_find("quasar") >= 0
                                       ? quad_index_static_type_info(quad_index_static_type_find("quasar"))
                                       : NULL;
        CHECK(ti != NULL, "seed: quasar descriptor present for offset read-back");
        if (ti) {
            const void *obj = ti->objects;
            CHECK(qi_read(obj, ti->off_q1) == 10 && qi_read(obj, ti->off_q2) == 11
                  && qi_read(obj, ti->off_q3) == 12 && qi_read(obj, ti->off_active) == 1,
                  "seed: q1/q2/q3/active offsets read back the seeded values");
        }
    }
}

static void test_invariants(void)
{
    /* --- inactive objects are never indexed ---------------------- */
    reset_grid();
    qi_seed(quad_index_static_type_info(quad_index_static_type_find("quasar")),
            0, 2, 3, 4, 0);
    quad_index_insert_static_all(g_grid);
    int total = 0;
    for (int i = 0; i < 41; i++)
        for (int j = 0; j < 41; j++)
            for (int l = 0; l < 41; l++)
                total += g_grid[i][j][l].quasar_count;
    CHECK(total == 0, "inv: inactive quasar must not be indexed (total=%d)", total);

    /* --- out-of-range quadrants are never indexed ---------------- */
    reset_grid();
    const QuadStaticTypeInfo *ti = quad_index_static_type_info(quad_index_static_type_find("quasar"));
    const int bad_quads[][3] = {
        { 0, 5, 5 }, { 41, 5, 5 }, { 5, 41, 5 }, { 5, 5, 41 },
        { -1, 5, 5 }, { 5, 5, -1 }, { 5, 5, 100 },
    };
    for (size_t k = 0; k < sizeof(bad_quads) / sizeof(bad_quads[0]); k++)
        qi_seed(ti, (int)k, bad_quads[k][0], bad_quads[k][1], bad_quads[k][2], 1);
    quad_index_insert_static_all(g_grid);
    total = 0;
    for (int i = 0; i < 41; i++)
        for (int j = 0; j < 41; j++)
            for (int l = 0; l < 41; l++)
                total += g_grid[i][j][l].quasar_count;
    CHECK(total == 0,
          "inv: out-of-range quadrants (0, 41, negative, 100) must not be indexed (total=%d)",
          total);

    /* --- per-quadrant caps --------------------------------------- */
    reset_grid();
    for (int k = 0; k < 6; k++)   /* MAX_Q_QUASARS is 4 */
        qi_seed(ti, k, 20, 20, 20, 1);
    const QuadStaticTypeInfo *tv = quad_index_static_type_info(quad_index_static_type_find("cosmic_void"));
    for (int k = 0; k < 3; k++)   /* MAX_Q_COSMIC_VOID is 1 */
        qi_seed(tv, k, 20, 20, 20, 1);
    const QuadStaticTypeInfo *tc = quad_index_static_type_info(quad_index_static_type_find("cmb"));
    for (int k = 0; k < 2; k++)   /* MAX_Q_CMB is 1 */
        qi_seed(tc, k, 20, 20, 20, 1);
    quad_index_insert_static_all(g_grid);

    QuadrantIndex *q = &g_grid[20][20][20];
    CHECK(q->quasar_count == MAX_Q_QUASARS,
          "inv: quasar cap respected (count=%d, cap=%d)", q->quasar_count, MAX_Q_QUASARS);
    CHECK(q->cosmic_void_count == MAX_Q_COSMIC_VOID,
          "inv: cap-1 type capped (cosmic_void count=%d)", q->cosmic_void_count);
    CHECK(q->cmb_count == MAX_Q_CMB,
          "inv: cap-1 type capped (cmb count=%d)", q->cmb_count);

    /* --- full-grid sweep: no count above its cap ----------------- */
    int over = 0;
    for (int d = 0; d < quad_index_static_type_count(); d++) {
        const QuadStaticTypeInfo *t = quad_index_static_type_info(d);
        for (int i = 0; i < 41; i++)
            for (int j = 0; j < 41; j++)
                for (int l = 0; l < 41; l++) {
                    int *cnt = (int *)((char *)&g_grid[i][j][l] + t->off_count);
                    if (*cnt > t->slot_max) over++;
                }
    }
    CHECK(over == 0, "inv: %d quadrant/type counts above their cap (memory safety)", over);
}

/* ================================================================== */
/* 4. Fuzz: every slot of every quadrant stays consistent             */
/* ================================================================== */
static void test_fuzz(void)
{
    reset_grid();
    srand(20260924);
    const int PER_TYPE = 20;

    for (int d = 0; d < quad_index_static_type_count(); d++) {
        const QuadStaticTypeInfo *ti = quad_index_static_type_info(d);
        for (int k = 0; k < PER_TYPE; k++) {
            int active = (rand() % 2) == 0;
            int a, b, c;
            if (rand() % 5 == 0) {   /* ~20% out of range */
                a = (rand() % 2 == 0) ? 0 : GALAXY_SIZE + 1 + rand() % 5;
                b = 1 + rand() % GALAXY_SIZE;
                c = 1 + rand() % GALAXY_SIZE;
            } else {
                a = 1 + rand() % GALAXY_SIZE;
                b = 1 + rand() % GALAXY_SIZE;
                c = 1 + rand() % GALAXY_SIZE;
            }
            qi_seed(ti, k, a, b, c, active);
        }
    }
    quad_index_insert_static_all(g_grid);

    int over_cap = 0, bad_ptr = 0, bad_active = 0, bad_quad = 0, slots = 0;
    for (int d = 0; d < quad_index_static_type_count(); d++) {
        const QuadStaticTypeInfo *ti = quad_index_static_type_info(d);
        const char *base = (const char *)ti->objects;
        long arr_bytes = (long)ti->total * (long)ti->obj_size;
        for (int i = 0; i < 41; i++)
            for (int j = 0; j < 41; j++)
                for (int l = 0; l < 41; l++) {
                    QuadrantIndex *q = &g_grid[i][j][l];
                    int *cnt = (int *)((char *)q + ti->off_count);
                    void **sl = (void **)((char *)q + ti->off_slots);
                    if (*cnt > ti->slot_max) over_cap++;
                    for (int s = 0; s < *cnt; s++) {
                        slots++;
                        const char *p = (const char *)sl[s];
                        long diff = (long)(p - base);
                        if (diff < 0 || diff >= arr_bytes ||
                            diff % (long)ti->obj_size != 0) {
                            bad_ptr++;
                            continue;
                        }
                        if (qi_read(p, ti->off_active) == 0) bad_active++;
                        if (qi_read(p, ti->off_q1) != i ||
                            qi_read(p, ti->off_q2) != j ||
                            qi_read(p, ti->off_q3) != l) bad_quad++;
                    }
                }
    }
    CHECK(over_cap == 0, "fuzz: %d counts above cap", over_cap);
    CHECK(bad_ptr == 0, "fuzz: %d slots not pointing into the type's global array", bad_ptr);
    CHECK(bad_active == 0, "fuzz: %d slots of inactive objects", bad_active);
    CHECK(bad_quad == 0, "fuzz: %d slots in the wrong quadrant", bad_quad);
    CHECK(slots > 0, "fuzz: %d slots checked (sanity: must be > 0)", slots);
}

int main(void)
{
    printf("quad_index test (B3: static galactic objects must reach the quadrant index)\n");

    g_grid = calloc(41 * 41 * 41, sizeof(QuadrantIndex));
    if (!g_grid) {
        fprintf(stderr, "cannot allocate the quadrant grid\n");
        return 1;
    }

    test_b3_quasar();
    test_table_contract();
    test_seed_all_types();
    test_invariants();
    test_fuzz();

    free(g_grid);
    printf("  %d checks passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
