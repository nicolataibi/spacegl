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

/* NPC ships use their faction number as object type (FACTION_KORTHIAN ..
 * FACTION_HIROGEN in network.h). */
#define OBJ_NPC_FIRST    OBJ_SHIP_KORTHIAN
#define OBJ_NPC_LAST     OBJ_SHIP_APEX

typedef enum ObjectType {
#define OBJECT_TYPE_ENUM_ENTRY(name, value, label) name = value,
    OBJECT_TYPE_LIST(OBJECT_TYPE_ENUM_ENTRY)
#undef OBJECT_TYPE_ENUM_ENTRY
    OBJ_TYPE_LAST = OBJ_EXOTIC_LAST
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

/* Display name of a type, NULL when the type is not assigned. */
static inline const char *obj_type_name(int t) {
    static const char *const exotic_names[OBJ_EXOTIC_COUNT] = { OBJECT_EXOTIC_NAME_LIST };
    if (obj_type_is_exotic(t)) return exotic_names[t - OBJ_EXOTIC_FIRST];
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

#endif /* OBJECT_TYPES_H */
