/*
 * object_types.h - single source of truth for the `type` field of the
 * objects exchanged between the server and the clients (NetObject,
 * SharedObject, LRS lists).
 *
 * The list below is the only place where a type number is bound to an
 * object. The server (logic.c), the HUD, the OpenGL viewer, the CPU Vulkan
 * path and the GPU-driven builder (vulkan_gdd_build.c) must use the OBJ_*
 * names and the predicates below instead of bare numbers: before this
 * header the same mapping was duplicated in several tables and a type
 * missing from one of them (the derelict, type 22) silently rendered as a
 * generic sphere.
 *
 * Adding an object kind:
 *   1. add one X(...) line to OBJECT_TYPE_LIST (the number must be unused);
 *   2. give it a visual in gdd_visual_for() (src/vulkan_gdd_build.c) and in
 *      the CPU / OpenGL drawing code;
 *   3. tests/gdd_contract_test.c (test_object_types) fails until the GPU
 *      builder knows how to draw it.
 *
 * Header-only, C99, no dependencies.
 */
#ifndef OBJECT_TYPES_H
#define OBJECT_TYPES_H

/*            enum name                  type  display name                */
#define OBJECT_TYPE_LIST(X) \
    X(OBJ_PLAYER,                   1,  "Player")                 \
    X(OBJ_BASE,                     3,  "Starbase")               \
    X(OBJ_STAR,                     4,  "Star")                   \
    X(OBJ_PLANET,                   5,  "Planet")                 \
    X(OBJ_BLACK_HOLE,               6,  "Black Hole")             \
    X(OBJ_NEBULA,                   7,  "Nebula")                 \
    X(OBJ_PULSAR,                   8,  "Pulsar")                 \
    X(OBJ_COMET,                    9,  "Comet")                  \
    X(OBJ_SHIP_KORTHIAN,           10,  "Korthian")               \
    X(OBJ_SHIP_XYLARI,             11,  "Xylari")                 \
    X(OBJ_SHIP_SWARM,              12,  "Swarm")                  \
    X(OBJ_SHIP_VESPERIAN,          13,  "Vesperian")              \
    X(OBJ_SHIP_ASCENDANT,          14,  "Ascendant")              \
    X(OBJ_SHIP_QUARZITE,           15,  "Quarzite")               \
    X(OBJ_SHIP_SAURIAN,            16,  "Saurian")                \
    X(OBJ_SHIP_GILDED,             17,  "Gilded")                 \
    X(OBJ_SHIP_FLUIDIC_VOID,       18,  "Fluidic Void")           \
    X(OBJ_SHIP_CRYOS,              19,  "Cryos")                  \
    X(OBJ_SHIP_APEX,               20,  "Apex")                   \
    X(OBJ_ASTEROID,                21,  "Asteroid")               \
    X(OBJ_DERELICT,                22,  "Derelict")               \
    X(OBJ_MINE,                    23,  "Mine")                   \
    X(OBJ_BUOY,                    24,  "Comm Buoy")              \
    X(OBJ_PLATFORM,                25,  "Defense Platform")       \
    X(OBJ_RIFT,                    26,  "Spatial Rift")           \
    X(OBJ_TORPEDO,                 27,  "Torpedo")                \
    X(OBJ_QUASAR,                  29,  "Quasar")                 \
    X(OBJ_CRYSTALLINE_ENTITY,      30,  "Crystalline Entity")     \
    X(OBJ_SPACE_AMOEBA,            31,  "Space Amoeba")           \
    X(OBJ_DYSON_FRAGMENT,          34,  "Dyson Fragment")         \
    X(OBJ_TRADING_HUB,             35,  "Trading Hub")            \
    X(OBJ_ANCIENT_RELIC,           36,  "Ancient Relic")          \
    X(OBJ_SUBSPACE_RUPTURE,        37,  "Subspace Rupture")       \
    X(OBJ_SATELLITE,               38,  "Satellite")              \
    X(OBJ_ION_STORM,               39,  "Ion Storm")              \
    X(OBJ_ALIEN_ARTIFACT,          40,  "Alien Artifact")         \
    X(OBJ_WARP_GATE,               41,  "Warp Gate")              \
    X(OBJ_NEUTRON_STAR,            42,  "Neutron Star")           \
    X(OBJ_MEGA_STRUCTURE,          43,  "Mega Structure")         \
    X(OBJ_DARK_CLOUD,              44,  "Dark Matter Cloud")      \
    X(OBJ_SINGULARITY,             45,  "Quantum Singularity")    \
    X(OBJ_PLASMA_STORM,            46,  "Plasma Storm")           \
    X(OBJ_ORBITAL_RING,            47,  "Orbital Ring")           \
    X(OBJ_TIME_ANOMALY,            48,  "Time Anomaly")           \
    X(OBJ_VOID_CRYSTAL,            49,  "Void Crystal")           \
    X(OBJ_SUBSPACE_ANOMALY,        50,  "Subspace Anomaly")       \
    X(OBJ_DIFFUSE_NEBULA,          51,  "Diffuse Nebula")         \
    X(OBJ_DARK_NEBULA,             52,  "Dark Nebula")            \
    X(OBJ_PLANETARY_NEBULA,        53,  "Planetary Nebula")       \
    X(OBJ_SUPERNOVA_REMNANT,       54,  "Supernova Remnant")      \
    X(OBJ_GIANT_MOLECULAR_CLOUD,   55,  "Giant Molecular Cloud")  \
    X(OBJ_INTERSTELLAR_FILAMENT,   56,  "Interstellar Filament")  \
    X(OBJ_INTERSTELLAR_BUBBLE,     57,  "Interstellar Bubble")    \
    X(OBJ_BOK_GLOBULE,             58,  "Bok Globule")            \
    X(OBJ_CLUMP_CORE,              59,  "Clump Core")             \
    X(OBJ_ACCRETION_DISK,          60,  "Accretion Disk")         \
    X(OBJ_RELATIVISTIC_JET,        61,  "Relativistic Jet")       \
    X(OBJ_SHOCK_WAVE,              62,  "Shock Wave")             \
    X(OBJ_STELLAR_BOW_SHOCK,       63,  "Stellar Bow Shock")      \
    X(OBJ_COSMIC_VOID,             64,  "Cosmic Void")            \
    X(OBJ_COSMIC_FILAMENT,         65,  "Cosmic Filament")        \
    X(OBJ_EVENT_HORIZON,           66,  "Event Horizon")          \
    X(OBJ_KILONOVA,                67,  "Kilonova")               \
    X(OBJ_GRAV_LENS,               68,  "Gravitational Lens")     \
    X(OBJ_GRB,                     69,  "Gamma-Ray Burst")        \
    X(OBJ_GRAV_WAVE,               70,  "Gravitational Wave")     \
    X(OBJ_PROTOPLANETARY_DISK,     71,  "Protoplanetary Disk")    \
    X(OBJ_DEBRIS_DISK,             72,  "Debris Disk")            \
    X(OBJ_PLANETESIMAL,            73,  "Planetesimal")           \
    X(OBJ_ROGUE_PLANET,            74,  "Rogue Planet")           \
    X(OBJ_BROWN_DWARF,             75,  "Brown Dwarf")            \
    X(OBJ_INTERSTELLAR_OBJECT,     76,  "Interstellar Object")    \
    X(OBJ_MAG_RECONNECTION,        77,  "Magnetic Reconnection")  \
    X(OBJ_CURRENT_SHEET,           78,  "Current Sheet")          \
    X(OBJ_HELIOSPHERE,             79,  "Heliosphere")            \
    X(OBJ_TERMINATION_SHOCK,       80,  "Termination Shock")      \
    X(OBJ_MAGNETOSPHERE,           81,  "Magnetosphere")          \
    X(OBJ_COSMIC_STRING,           82,  "Cosmic String")          \
    X(OBJ_DOMAIN_WALL,             83,  "Domain Wall")            \
    X(OBJ_DM_HALO,                 84,  "Dark Matter Halo")       \
    X(OBJ_IGM,                     85,  "Intergalactic Medium")   \
    X(OBJ_CGM,                     86,  "Circumgalactic Medium")  \
    X(OBJ_LYMAN_ALPHA,             87,  "Lyman-alpha Forest")     \
    X(OBJ_CMB,                     88,  "CMB Cold Spot")

/* Exotic compact objects: 32 consecutive types, one per name below
 * (type = OBJ_EXOTIC_FIRST + index). Also used by the server galaxy
 * generator, so the names exist in exactly one place. */
#define OBJECT_EXOTIC_NAME_LIST \
    "Stellar-mass black hole", "Intermediate-mass black hole (IMBH)", \
    "Supermassive black hole (SMBH)", "Ultramassive black hole (UMBH)", \
    "Stupendously large black hole (SLAB)", "Primordial black hole (PBH)", \
    "Schwarzschild black hole", "Kerr black hole", \
    "Reissner-Nordstrom black hole", "Kerr-Newman black hole", \
    "Extremal black hole", "Regular black hole", \
    "Bardeen black hole", "Hayward black hole", \
    "Dymnikova black hole", "Fan-Wang black hole", \
    "Firewall black hole", "Fuzzball", \
    "Quantum black hole", "Micro black hole", \
    "Planck black hole", "Gravastar", \
    "Boson star", "Dark star", \
    "Exotic compact object (ECO)", "Black-hole mimicker", \
    "Wormhole", "White hole", \
    "Naked singularity", "Super-extremal solution black hole", \
    "Super-extremal Reissner-Nordstrom solution black hole", \
    "Super-extremal Kerr solution black hole"

#define OBJ_EXOTIC_FIRST 100
#define OBJ_EXOTIC_COUNT 32
#define OBJ_EXOTIC_LAST  (OBJ_EXOTIC_FIRST + OBJ_EXOTIC_COUNT - 1)   /* 131 */

/* Evolved stars: 17 consecutive types (type = OBJ_EVOLVED_FIRST + index), stored
 * in the generic cosmic_features[] array like the exotic objects. Order:
 *   0..4   post-main-sequence giants
 *   5..6   white dwarfs by core composition
 *   7..13  white dwarfs by atmosphere (spectral class)
 *   14..16 pulsating white dwarfs / pre-white dwarfs (non-radial pulsations)
 * Names live here only: the server galaxy generator and the viewers use
 * this list. */
#define OBJECT_EVOLVED_NAME_LIST \
    "Red Giant", "Red Clump Star", "Horizontal Branch Star", \
    "AGB Star (Asymptotic Giant Branch)", "Red Supergiant", \
    "White Dwarf (C-O core)", "White Dwarf (O-Ne-Mg core)", \
    "White Dwarf DA (hydrogen)", "White Dwarf DB (neutral helium)", \
    "White Dwarf DO (ionized helium)", "White Dwarf DQ (carbon)", \
    "White Dwarf DZ (metals)", "White Dwarf DC (continuous)", \
    "White Dwarf DX (unclassified lines)", \
    "ZZ Ceti Pulsator (DAV)", "V777 Her Pulsator (DBV)", \
    "GW Vir Pulsator (DOV)"

#define OBJ_EVOLVED_FIRST 132
#define OBJ_EVOLVED_COUNT 17
#define OBJ_EVOLVED_LAST  (OBJ_EVOLVED_FIRST + OBJ_EVOLVED_COUNT - 1)   /* 148 */

/* Index of the sub-groups inside the evolved block. */
#define OBJ_EVOLVED_GIANT_LAST   4    /* 0..4  giants / supergiants      */
#define OBJ_EVOLVED_PULSATOR_MIN 14   /* 14..16 pulsating white dwarfs   */

/* Protostellar objects & Young Stellar Objects (YSO): 16 consecutive types
 * (type = OBJ_PROTOSTELLAR_FIRST + index) in slots 149..164, stored in the
 * generic cosmic_features[] array like exotic objects, evolved stars and moons.
 * Order:
 *   0..1   Class 0 (Dense Core, Bipolar Outflow)
 *   2..3   Class I (Infalling Envelope, Protostellar Jet)
 *   4..5   Herbig-Haro (HH Shock Knot, HH Bow Shock)
 *   6..7   T Tauri (Classical CTTS, Weak-line WTTS)
 *   8..9   Herbig Ae/Be (PMS Intermediate-Mass)
 *   10..11 Accretion Outbursts (FU Orionis, EX Lupi)
 *   12..15 Circumstellar Structures (Collapsing Core, Cavity, Disk, Column)
 * Names live here only: the server galaxy generator and the viewers use this list. */
#define OBJECT_PROTOSTELLAR_NAME_LIST \
    "Class 0 Protostar (Dense Core)", \
    "Class 0 Bipolar Outflow", \
    "Class I Protostar (Infalling Envelope)", \
    "Class I Protostellar Jet", \
    "Herbig-Haro Object (HH Shock Knot)", \
    "Herbig-Haro Bow Shock", \
    "Classical T Tauri Star (CTTS)", \
    "Weak-line T Tauri Star (WTTS)", \
    "Herbig Ae Star (PMS Intermediate)", \
    "Herbig Be Star (PMS Intermediate)", \
    "FU Orionis Star (Accretion Outburst)", \
    "EX Lupi Star (Episodic Accretion)", \
    "Pre-Stellar Collapsing Core", \
    "Protostellar Envelope Cavity", \
    "Protoplanetary Accretion Disk", \
    "Magnetospheric Accretion Column"

#define OBJ_PROTOSTELLAR_FIRST 149
#define OBJ_PROTOSTELLAR_COUNT 16
#define OBJ_PROTOSTELLAR_LAST  (OBJ_PROTOSTELLAR_FIRST + OBJ_PROTOSTELLAR_COUNT - 1)   /* 164 */

/* Natural moons: 10 consecutive types (type = OBJ_MOON_FIRST + index) in the
 * free slots 89..98, stored in the generic cosmic_features[] array like the
 * exotic objects and the evolved stars. 99 stays free. Order:
 *   0 Shepherd, 1 Trojan, 2 Co-orbital, 3 Volcanic, 4 Ocean, 5 With
 *   atmosphere, 6 Exomoon, 7 Regular, 8 Irregular, 9 Submoon.
 * Names live here only: the server galaxy generator and the viewers use
 * this list. Every moon orbits a planet (max 4 per planet, one orbit each,
 * slow): the server derives parent and orbit from the moon id, nothing is
 * saved, see include/moon_orbits.h. */
#define OBJECT_MOON_NAME_LIST \
    "Shepherd Moon", "Trojan Moon", "Co-orbital Moon", "Volcanic Moon", \
    "Ocean Moon", "Moon with Atmosphere", "Exomoon", "Regular Moon", \
    "Irregular Moon", "Submoon"

#define OBJ_MOON_FIRST 89
#define OBJ_MOON_COUNT 10
#define OBJ_MOON_LAST  (OBJ_MOON_FIRST + OBJ_MOON_COUNT - 1)   /* 98 */

/* Extended natural moons: 13 consecutive types (type = OBJ_MOON_EXT_FIRST + index)
 * in slots 165..177, stored in the generic cosmic_features[] array like the
 * base moons and evolved stars.
 * Order:
 *   0 Retrograde Moon, 1 Resonant Moon, 2 Tidally Locked Moon,
 *   3 Chaotic Rotator Moon, 4 Embedded Moonlet, 5 Captured Moon,
 *   6 Cryovolcanic Moon, 7 Tidal-Heated Moon, 8 Differentiated Moon,
 *   9 Binary Moon, 10 Ringed Moon, 11 Subsurface Ocean Moon, 12 Disrupted Moon.
 */
#define OBJECT_MOON_EXT_NAME_LIST \
    "Retrograde Moon", "Resonant Moon", "Tidally Locked Moon", \
    "Chaotic Rotator Moon", "Embedded Moonlet", "Captured Moon", \
    "Cryovolcanic Moon", "Tidal-Heated Moon", "Differentiated Moon", \
    "Binary Moon", "Ringed Moon", "Subsurface Ocean Moon", \
    "Disrupted Moon"

#define OBJ_MOON_EXT_FIRST 165
#define OBJ_MOON_EXT_COUNT 13
#define OBJ_MOON_EXT_LAST  (OBJ_MOON_EXT_FIRST + OBJ_MOON_EXT_COUNT - 1)   /* 177 */

/* Index of the moon that carries a visible atmosphere halo. */
#define OBJ_MOON_ATMOSPHERE_INDEX 5

/* NPC ships use their faction number as object type (FACTION_KORTHIAN ..
 * FACTION_HIROGEN in network.h). */
#define OBJ_NPC_FIRST    OBJ_SHIP_KORTHIAN
#define OBJ_NPC_LAST     OBJ_SHIP_APEX

typedef enum ObjectType {
#define OBJECT_TYPE_ENUM_ENTRY(name, value, label) name = (value),
    OBJECT_TYPE_LIST(OBJECT_TYPE_ENUM_ENTRY)
#undef OBJECT_TYPE_ENUM_ENTRY
    OBJ_TYPE_LAST = OBJ_MOON_EXT_LAST
} ObjectType;

/* ------------------------------------------------------------------ */
/* Predicates                                                          */
/* ------------------------------------------------------------------ */

/* NPC ship (type == faction, 10..20). */
static inline int obj_type_is_npc_ship(int t) {
    return t >= OBJ_NPC_FIRST && t <= OBJ_NPC_LAST;
}

/* One of the 32 exotic compact objects (100..131). */
static inline int obj_type_is_exotic(int t) {
    return t >= OBJ_EXOTIC_FIRST && t <= OBJ_EXOTIC_LAST;
}

/* One of the 17 evolved stars (132..148). */
static inline int obj_type_is_evolved_star(int t) {
    return t >= OBJ_EVOLVED_FIRST && t <= OBJ_EVOLVED_LAST;
}

/* One of the 16 protostellar objects / YSO (149..164). */
static inline int obj_type_is_protostellar(int t) {
    return t >= OBJ_PROTOSTELLAR_FIRST && t <= OBJ_PROTOSTELLAR_LAST;
}

/* One of the 10 base natural moons (89..98). */
static inline int obj_type_is_moon_base(int t) {
    return t >= OBJ_MOON_FIRST && t <= OBJ_MOON_LAST;
}

/* One of the 13 extended natural moons (165..177). */
static inline int obj_type_is_moon_ext(int t) {
    return t >= OBJ_MOON_EXT_FIRST && t <= OBJ_MOON_EXT_LAST;
}

/* Any natural moon (base 89..98 or extended 165..177). */
static inline int obj_type_is_moon(int t) {
    return (t >= OBJ_MOON_FIRST && t <= OBJ_MOON_LAST) ||
           (t >= OBJ_MOON_EXT_FIRST && t <= OBJ_MOON_EXT_LAST);
}

/* Predicates for shared astronomical properties */
static inline int obj_type_is_retrograde_moon(int t) {
    return t == (OBJ_MOON_FIRST + 8) || t == (OBJ_MOON_EXT_FIRST + 0) || t == (OBJ_MOON_EXT_FIRST + 5);
}

static inline int obj_type_is_ocean_moon(int t) {
    return t == (OBJ_MOON_FIRST + 4) || t == (OBJ_MOON_EXT_FIRST + 11);
}

static inline int obj_type_is_volcanic_moon(int t) {
    return t == (OBJ_MOON_FIRST + 3) || t == (OBJ_MOON_EXT_FIRST + 6) || t == (OBJ_MOON_EXT_FIRST + 7);
}

static inline int obj_type_is_tidally_locked_moon(int t) {
    return t == (OBJ_MOON_FIRST + 7) || t == (OBJ_MOON_EXT_FIRST + 2) || t == (OBJ_MOON_EXT_FIRST + 8);
}

static inline int obj_type_is_chaotic_rotator_moon(int t) {
    return t == (OBJ_MOON_EXT_FIRST + 3);
}

static inline int obj_type_is_embedded_moon(int t) {
    return t == (OBJ_MOON_FIRST + 0) || t == (OBJ_MOON_EXT_FIRST + 4);
}

static inline int obj_type_is_captured_moon(int t) {
    return t == (OBJ_MOON_FIRST + 8) || t == (OBJ_MOON_EXT_FIRST + 5);
}

static inline int obj_type_is_binary_moon(int t) {
    return t == (OBJ_MOON_EXT_FIRST + 9);
}

static inline int obj_type_is_ringed_moon(int t) {
    return t == (OBJ_MOON_EXT_FIRST + 10);
}

static inline int obj_type_is_disrupted_moon(int t) {
    return t == (OBJ_MOON_EXT_FIRST + 12);
}

/* White dwarf or pre-white dwarf (evolved index 5..16). */
static inline int obj_type_is_white_dwarf(int t) {
    return obj_type_is_evolved_star(t) && (t - OBJ_EVOLVED_FIRST) > OBJ_EVOLVED_GIANT_LAST;
}

/* Pulsating white dwarf (DAV, DBV, GW Vir): its brightness oscillates. */
static inline int obj_type_is_pulsating_wd(int t) {
    return obj_type_is_evolved_star(t) && (t - OBJ_EVOLVED_FIRST) >= OBJ_EVOLVED_PULSATOR_MIN;
}

/* Black hole look: the classic black hole and the exotic compact objects. */
static inline int obj_type_is_black_hole(int t) {
    return t == OBJ_BLACK_HOLE || obj_type_is_exotic(t);
}

/* Objects drawn with a ship hull: player ships, NPC ships and the wreck
 * (derelict) that a destroyed ship leaves behind. */
static inline int obj_type_has_ship_hull(int t) {
    return t == OBJ_PLAYER || obj_type_is_npc_ship(t) || t == OBJ_DERELICT;
}

/* Ships that are alive and carry their faction colour (no derelict). */
static inline int obj_type_is_live_ship(int t) {
    return t == OBJ_PLAYER || obj_type_is_npc_ship(t);
}

/* CPU Vulkan "hull path" (src/vulkan_render.c): ship PBR look, wireframe
 * pipeline, 0.55 scale, swarm cube and quantum core. It covers the player
 * and every type from OBJ_NPC_FIRST up to OBJ_CMB, i.e. the legacy block
 * 10..88 the renderer has always sent down that path. It stops at the last
 * assigned legacy type on purpose: the free slots 89..99 and 149..255 must
 * NOT be swallowed by it (before, 89..99 and >= 200 were). Tests:
 * tests/gdd_contract_test.c (test_object_types). */
static inline int obj_type_uses_cpu_hull_path(int t) {
    return t == OBJ_PLAYER || (t >= OBJ_NPC_FIRST && t <= OBJ_CMB);
}

/* Display name of a type, NULL when the type is not assigned. */
static inline const char *obj_type_name(int t) {
    static const char *const exotic_names[OBJ_EXOTIC_COUNT] = { OBJECT_EXOTIC_NAME_LIST };
    if (obj_type_is_exotic(t)) return exotic_names[t - OBJ_EXOTIC_FIRST];
    static const char *const evolved_names[OBJ_EVOLVED_COUNT] = { OBJECT_EVOLVED_NAME_LIST };
    if (obj_type_is_evolved_star(t)) return evolved_names[t - OBJ_EVOLVED_FIRST];
    static const char *const proto_names[OBJ_PROTOSTELLAR_COUNT] = { OBJECT_PROTOSTELLAR_NAME_LIST };
    if (obj_type_is_protostellar(t)) return proto_names[t - OBJ_PROTOSTELLAR_FIRST];
    static const char *const moon_ext_names[OBJ_MOON_EXT_COUNT] = { OBJECT_MOON_EXT_NAME_LIST };
    if (obj_type_is_moon_ext(t)) return moon_ext_names[t - OBJ_MOON_EXT_FIRST];
    static const char *const moon_names[OBJ_MOON_COUNT] = { OBJECT_MOON_NAME_LIST };
    if (obj_type_is_moon_base(t)) return moon_names[t - OBJ_MOON_FIRST];
    switch (t) {
#define OBJECT_TYPE_NAME_CASE(name, value, label) case value: return label;
    OBJECT_TYPE_LIST(OBJECT_TYPE_NAME_CASE)
#undef OBJECT_TYPE_NAME_CASE
    default: return (const char *)0;
    }
}

static inline int obj_type_is_known(int t) {
    return obj_type_name(t) != (const char *)0;
}

/* Protostellar objects & YSO: colour and scale palette (149..164). */
static inline const float *obj_protostellar_rgb(int index) {
    static const float rgb[OBJ_PROTOSTELLAR_COUNT][3] = {
        {1.0f, 0.45f, 0.10f},   /* 0 Class 0 Protostar        infrared amber    */
        {0.2f, 0.80f, 1.00f},   /* 1 Class 0 Outflow          shock blue-cyan   */
        {1.0f, 0.65f, 0.15f},   /* 2 Class I Protostar        bright amber-gold */
        {0.3f, 0.90f, 0.90f},   /* 3 Class I Jet              collimated teal   */
        {1.0f, 0.20f, 0.40f},   /* 4 Herbig-Haro Knot         H-alpha pink-red  */
        {0.8f, 1.00f, 0.20f},   /* 5 Herbig-Haro Bow Shock    shock green-amber */
        {1.0f, 0.80f, 0.30f},   /* 6 CTTS                     solar warm orange */
        {1.0f, 0.50f, 0.20f},   /* 7 WTTS                     reddish orange    */
        {0.8f, 0.90f, 1.00f},   /* 8 Herbig Ae Star           white-cyan        */
        {0.4f, 0.60f, 1.00f},   /* 9 Herbig Be Star           electric blue     */
        {1.0f, 0.95f, 0.70f},   /* 10 FUor Star               radiant white-gold*/
        {1.0f, 0.70f, 0.20f},   /* 11 EXor Star               pulsing yellow-org*/
        {0.6f, 0.35f, 0.20f},   /* 12 Pre-Stellar Core        dark dusty brown  */
        {0.7f, 0.40f, 0.90f},   /* 13 Envelope Cavity         ethereal violet   */
        {0.85f, 0.55f, 0.30f},  /* 14 Protoplanet Disk        dusty terracotta  */
        {0.9f, 0.30f, 0.80f}    /* 15 Accretion Column        magnetic magenta  */
    };
    if (index < 0 || index >= OBJ_PROTOSTELLAR_COUNT) index = 0;
    return rgb[index];
}

static inline float obj_protostellar_scale(int index) {
    static const float scale[OBJ_PROTOSTELLAR_COUNT] = {
        1.2f, 1.8f, 1.3f, 1.5f, 0.9f, 1.6f, 1.4f, 1.1f,
        2.0f, 2.4f, 2.8f, 1.5f, 1.7f, 2.2f, 1.9f, 1.3f
    };
    if (index < 0 || index >= OBJ_PROTOSTELLAR_COUNT) index = 0;
    return scale[index];
}

/* Moons: colour and size (relative to a planet = 1.0) shared by the OpenGL
 * viewer, the CPU Vulkan path and the GPU-driven builder. Index = type -
 * OBJ_MOON_FIRST; out of range falls back to the regular moon. */
static inline const float *obj_moon_rgb(int index) {
    static const float rgb[OBJ_MOON_COUNT][3] = {
        {0.65f, 0.60f, 0.55f},   /* 0 Shepherd       dusty grey-brown  */
        {0.55f, 0.52f, 0.50f},   /* 1 Trojan         dark grey         */
        {0.70f, 0.68f, 0.65f},   /* 2 Co-orbital     light grey        */
        {0.90f, 0.60f, 0.15f},   /* 3 Volcanic       sulphur orange    */
        {0.45f, 0.65f, 0.85f},   /* 4 Ocean          icy blue          */
        {0.80f, 0.60f, 0.30f},   /* 5 Atmosphere     hazy amber        */
        {0.55f, 0.80f, 0.70f},   /* 6 Exomoon        teal              */
        {0.78f, 0.78f, 0.80f},   /* 7 Regular        silver            */
        {0.50f, 0.45f, 0.42f},   /* 8 Irregular      dark rock         */
        {0.60f, 0.60f, 0.62f}    /* 9 Submoon        grey              */
    };
    if (index < 0 || index >= OBJ_MOON_COUNT) index = 7;
    return rgb[index];
}

static inline float obj_moon_scale(int index) {
    static const float scale[OBJ_MOON_COUNT] = {
        0.30f, 0.35f, 0.40f, 0.50f, 0.55f,   /* shepherd .. ocean        */
        0.60f, 0.60f, 0.45f, 0.30f, 0.20f    /* atmosphere .. submoon    */
    };
    if (index < 0 || index >= OBJ_MOON_COUNT) index = 7;
    return scale[index];
}

/* Extended moons: colour and size (165..177). */
static inline const float *obj_moon_ext_rgb(int index) {
    static const float rgb[OBJ_MOON_EXT_COUNT][3] = {
        {0.60f, 0.65f, 0.75f},   /* 0 Retrograde        rocky grey-blue        */
        {0.78f, 0.72f, 0.58f},   /* 1 Resonant          pale ochre             */
        {0.75f, 0.75f, 0.75f},   /* 2 Tidally Locked    cratered silver-grey   */
        {0.72f, 0.55f, 0.42f},   /* 3 Chaotic Rotator   sponge tan-red         */
        {0.85f, 0.88f, 0.95f},   /* 4 Embedded          bright ring icy white  */
        {0.45f, 0.45f, 0.48f},   /* 5 Captured          dark carbonaceous      */
        {0.65f, 0.85f, 0.98f},   /* 6 Cryovolcanic      cyan geyser ice        */
        {0.92f, 0.75f, 0.25f},   /* 7 Tidal-Heated      sulfur magma yellow    */
        {0.68f, 0.62f, 0.55f},   /* 8 Differentiated    silicate basalt brown  */
        {0.55f, 0.65f, 0.72f},   /* 9 Binary            twin slate-blue        */
        {0.82f, 0.80f, 0.88f},   /* 10 Ringed           pearlescent moonlet    */
        {0.70f, 0.80f, 0.92f},   /* 11 Subsurface Ocean fractured cerulean ice */
        {0.58f, 0.45f, 0.40f}    /* 12 Disrupted        rubble-pile dark red   */
    };
    if (index < 0 || index >= OBJ_MOON_EXT_COUNT) index = 2;
    return rgb[index];
}

static inline float obj_moon_ext_scale(int index) {
    static const float scale[OBJ_MOON_EXT_COUNT] = {
        0.30f, 0.35f, 0.38f, 0.22f, 0.15f, 0.28f, 0.32f, 0.36f, 0.42f, 0.25f, 0.34f, 0.35f, 0.20f
    };
    if (index < 0 || index >= OBJ_MOON_EXT_COUNT) index = 2;
    return scale[index];
}

/* Unified RGB for any moon type (base 89..98 or ext 165..177). */
static inline const float *obj_moon_any_rgb(int t) {
    if (t >= OBJ_MOON_FIRST && t <= OBJ_MOON_LAST)
        return obj_moon_rgb(t - OBJ_MOON_FIRST);
    if (t >= OBJ_MOON_EXT_FIRST && t <= OBJ_MOON_EXT_LAST)
        return obj_moon_ext_rgb(t - OBJ_MOON_EXT_FIRST);
    return obj_moon_rgb(7);
}

/* Unified scale for any moon type (base 89..98 or ext 165..177). */
static inline float obj_moon_any_scale(int t) {
    if (t >= OBJ_MOON_FIRST && t <= OBJ_MOON_LAST)
        return obj_moon_scale(t - OBJ_MOON_FIRST);
    if (t >= OBJ_MOON_EXT_FIRST && t <= OBJ_MOON_EXT_LAST)
        return obj_moon_ext_scale(t - OBJ_MOON_EXT_FIRST);
    return obj_moon_scale(7);
}

/* Star spectral classes carried in NetObject.ship_class of an OBJ_STAR:
 * 0..6 = O, B, A, F, G, K, M (assigned by the server). One palette for the
 * CPU Vulkan path and the GPU-driven builder (the OpenGL viewer draws the
 * same colours in drawStar). Out-of-range values fall back to class G. */
#define OBJ_STAR_CLASS_COUNT   7
#define OBJ_STAR_CLASS_DEFAULT 4

static inline const float *obj_star_class_rgb(int spectral_class) {
    static const float rgb[OBJ_STAR_CLASS_COUNT][3] = {
        {0.4f, 0.6f, 1.0f},   /* O  blue         */
        {0.7f, 0.8f, 1.0f},   /* B  blue-white   */
        {1.0f, 1.0f, 1.0f},   /* A  white        */
        {1.0f, 1.0f, 0.7f},   /* F  yellow-white */
        {1.0f, 0.9f, 0.1f},   /* G  yellow       */
        {1.0f, 0.6f, 0.2f},   /* K  orange       */
        {1.0f, 0.2f, 0.1f}    /* M  red          */
    };
    if (spectral_class < 0 || spectral_class >= OBJ_STAR_CLASS_COUNT)
        spectral_class = OBJ_STAR_CLASS_DEFAULT;
    return rgb[spectral_class];
}

/* Planet classes carried in NetObject.ship_class of an OBJ_PLANET (and
 * stored in NPCPlanet.planet_class, saved in galaxy.dat). The class is the
 * physical kind of the planet and is independent of its mineable resource,
 * which travels in NetObject.plating for planets. One palette for the
 * OpenGL viewer, the CPU Vulkan path and the GPU-driven builder; out-of-range
 * values fall back to Terrestrial. */
enum {
    PLANET_CLASS_TERRESTRIAL = 0,
    PLANET_CLASS_GAS_GIANT   = 1,
    PLANET_CLASS_ICE         = 2,
    PLANET_CLASS_MOLTEN      = 3,
    PLANET_CLASS_DESERT      = 4,
    PLANET_CLASS_COUNT       = 5
};

static inline const char *planet_class_name(int c) {
    static const char *const names[PLANET_CLASS_COUNT] =
        { "Terrestrial", "Gas Giant", "Ice", "Molten", "Desert" };
    return (c >= 0 && c < PLANET_CLASS_COUNT) ? names[c] : "Unknown";
}

static inline const float *obj_planet_class_rgb(int planet_class) {
    static const float rgb[PLANET_CLASS_COUNT][3] = {
        {0.2f, 0.5f, 1.0f},   /* Terrestrial  blue       */
        {0.8f, 0.6f, 0.4f},   /* Gas Giant    brown      */
        {0.7f, 0.9f, 1.0f},   /* Ice          pale blue  */
        {0.8f, 0.2f, 0.1f},   /* Molten       red        */
        {0.8f, 0.7f, 0.5f}    /* Desert       sand       */
    };
    if (planet_class < 0 || planet_class >= PLANET_CLASS_COUNT)
        planet_class = PLANET_CLASS_TERRESTRIAL;
    return rgb[planet_class];
}

/* Evolved stars: colour and size (relative to a main-sequence star = 1.0)
 * shared by the OpenGL viewer, the CPU Vulkan path and the GPU-driven
 * builder. Index = type - OBJ_EVOLVED_FIRST; out of range falls back to the
 * red giant. */
static inline const float *obj_evolved_star_rgb(int index) {
    static const float rgb[OBJ_EVOLVED_COUNT][3] = {
        {1.00f, 0.35f, 0.10f},   /*  0 Red Giant             red-orange  */
        {1.00f, 0.50f, 0.20f},   /*  1 Red Clump Star        orange      */
        {1.00f, 0.85f, 0.40f},   /*  2 Horizontal Branch     gold        */
        {1.00f, 0.25f, 0.05f},   /*  3 AGB Star              deep red    */
        {1.00f, 0.15f, 0.05f},   /*  4 Red Supergiant        crimson     */
        {0.85f, 0.90f, 1.00f},   /*  5 WD C-O core           white-blue  */
        {0.70f, 0.85f, 1.00f},   /*  6 WD O-Ne-Mg core       pale blue   */
        {0.90f, 0.95f, 1.00f},   /*  7 DA  hydrogen          white       */
        {0.75f, 0.85f, 1.00f},   /*  8 DB  neutral helium    blue-white  */
        {0.50f, 0.65f, 1.00f},   /*  9 DO  ionized helium    hot blue    */
        {0.85f, 0.80f, 0.70f},   /* 10 DQ  carbon            sand        */
        {0.90f, 0.75f, 0.60f},   /* 11 DZ  metals            copper      */
        {0.80f, 0.80f, 0.85f},   /* 12 DC  continuous        grey-white  */
        {0.75f, 0.60f, 0.85f},   /* 13 DX  unidentified      lilac       */
        {0.80f, 1.00f, 1.00f},   /* 14 DAV (ZZ Ceti)         cyan-white  */
        {0.60f, 0.90f, 1.00f},   /* 15 DBV (V777 Her)        cyan        */
        {0.40f, 0.70f, 1.00f}    /* 16 GW Vir (incl. DOV)    hot blue    */
    };
    if (index < 0 || index >= OBJ_EVOLVED_COUNT) index = 0;
    return rgb[index];
}

static inline float obj_evolved_star_scale(int index) {
    static const float scale[OBJ_EVOLVED_COUNT] = {
        1.6f, 1.3f, 1.2f, 1.8f, 2.2f,              /* giants / supergiants  */
        0.35f, 0.35f,                              /* white dwarfs by core  */
        0.35f, 0.35f, 0.40f, 0.35f, 0.35f, 0.35f, 0.35f, /* by atmosphere   */
        0.40f, 0.40f, 0.50f                        /* pulsators             */
    };
    if (index < 0 || index >= OBJ_EVOLVED_COUNT) index = 0;
    return scale[index];
}

#endif /* OBJECT_TYPES_H */
