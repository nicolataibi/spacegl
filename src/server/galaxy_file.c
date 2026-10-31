/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * galaxy_file.c — the galaxy.dat file format alone (2026.10.31.31): the
 * section table, the in-memory image of a save and the reader. No policy:
 * no completion of empty families, no repair, no report, no I/O threads.
 * Split from galaxy_io.c (which keeps the save scheduling and the load
 * policy) so that the offline inspector spacegl_viewer reads galaxy.dat
 * with the server's own code: it had kept a reader of format 1, the raw
 * dump replaced in 2026.10.31.19, and could not read any galaxy since.
 *
 * Needs the object pools, players[], spacegl_master, sys_star_count and
 * cosmic_feature_count (server_internal.h): the server defines them in
 * galaxy.c, the inspector in spacegl_viewer.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include "server_internal.h"
#include "galaxy_file.h"

/* ------------------------------------------------------------------------
 * galaxy.dat format 2 (2026.10.31.19)
 *
 *   GalaxyFileHeader   magic "SGGALAX2", format 2, GALAXY_VERSION, section
 *                      count
 *   then, per section: GalaxySectionHeader (tag, element size, layout,
 *                      element count) followed by element size x count bytes
 *
 * Format 1 was a raw dump of every pool in a fixed order behind a single
 * int (GALAXY_VERSION): any change to any persisted struct made the whole
 * file unreadable and the server generated a new galaxy, losing every
 * captain's ship and the state of the world. Now each pool is read on its
 * own: a section whose element size or layout number no longer matches is
 * skipped (that pool starts empty and the usual completion spawners refill
 * the families they know), an unknown section is skipped, a missing one
 * leaves its pool empty. Only "master" is required. Rule for developers:
 * changing the fields of a persisted struct without changing its size
 * needs its layout number bumped in GALAXY_SECTION_LAYOUT (below).
 *
 * Only format 2 is read (2026.10.31.25): a file in format 1 or any other
 * content is not loaded and the server generates a new galaxy.
 * ---------------------------------------------------------------------- */
#define GALAXY_FILE_MAGIC   "SGGALAX2"
#define GALAXY_FILE_FORMAT  2u
#define GALAXY_SECTION_LAYOUT 1u   /* bump per section when a struct changes in place */

typedef struct {
    char magic[8];
    uint32_t format;
    uint32_t galaxy_version;
    uint32_t section_count;
    uint32_t reserved;
} GalaxyFileHeader;

typedef struct {
    char tag[24];
    uint32_t elem_size;
    uint32_t layout;
    uint64_t count;
} GalaxySectionHeader;

_Static_assert(sizeof(GalaxyFileHeader) == 24, "galaxy.dat header layout");
_Static_assert(sizeof(GalaxySectionHeader) == 40, "galaxy.dat section header layout");

/* The fixed-size pools, in file order. */
#define GALAXY_FIXED_POOLS(X) \
    X(npcs, NPCShip, MAX_NPC) \
    X(stars_data, NPCStar, MAX_STARS) \
    X(black_holes, NPCBlackHole, MAX_BH) \
    X(planets, NPCPlanet, MAX_PLANETS) \
    X(bases, NPCBase, MAX_BASES) \
    X(nebulas, NPCNebula, MAX_NEBULAS) \
    X(pulsars, NPCPulsar, MAX_PULSARS) \
    X(quasars, NPCQuasar, MAX_QUASARS) \
    X(comets, NPCComet, MAX_COMETS) \
    X(asteroids, NPCAsteroid, MAX_ASTEROIDS) \
    X(derelicts, NPCDerelict, MAX_DERELICTS) \
    X(mines, NPCMine, MAX_MINES) \
    X(buoys, NPCBuoy, MAX_BUOYS) \
    X(platforms, NPCPlatform, MAX_PLATFORMS) \
    X(rifts, NPCRift, MAX_RIFTS) \
    X(monsters, NPCMonster, MAX_MONSTERS) \
    X(dysons, NPCDyson, MAX_DYSON) \
    X(hubs, NPCHub, MAX_HUBS) \
    X(relics, NPCRelic, MAX_RELICS) \
    X(ruptures, NPCRupture, MAX_RUPTURES) \
    X(satellites, NPCSatellite, MAX_SATELLITES) \
    X(storms, NPCStorm, MAX_STORMS) \
    X(artifacts, NPCArtifact, MAX_ARTIFACTS) \
    X(warp_gates, NPCWarpGate, MAX_WARP_GATES) \
    X(neutron_stars, NPCNeutronStar, MAX_NEUTRON_STARS) \
    X(mega_structs, NPCMegaStructure, MAX_MEGA_STRUCTS) \
    X(dark_clouds, NPCDarkCloud, MAX_DARK_CLOUDS) \
    X(singularities, NPCSingularity, MAX_SINGULARITIES) \
    X(plasma_storms, NPCPlasmaStorm, MAX_PLASMA_STORMS) \
    X(orbital_rings, NPCOrbitalRing, MAX_ORBITAL_RINGS) \
    X(time_anomalies, NPCTimeAnomaly, MAX_TIME_ANOMALIES) \
    X(void_crystals, NPCVoidCrystal, MAX_VOID_CRYSTALS) \
    X(subspace_anomalies, NPCSubspaceAnomaly, MAX_SUBSPACE_ANOMALIES) \
    X(diffuse_nebulae, NPCDiffuseNebula, MAX_DIFFUSE_NEBULAE) \
    X(dark_nebulae, NPCDarkNebula, MAX_DARK_NEBULAE) \
    X(planetary_nebulae, NPCPlanetaryNebula, MAX_PLANETARY_NEBULAE) \
    X(snrs, NPCSNR, MAX_SNR) \
    X(gmcs, NPCGMC, MAX_GMC) \
    X(interstellar_filaments, NPCInterstellarFilament, MAX_INTERSTELLAR_FILAMENTS) \
    X(interstellar_bubbles, NPCInterstellarBubble, MAX_INTERSTELLAR_BUBBLES) \
    X(bok_globules, NPCBokGlobule, MAX_BOK_GLOBULES) \
    X(clump_cores, NPCClumpCore, MAX_CLUMP_CORES) \
    X(accretion_disks, NPCAccretionDisk, MAX_ACCRETION_DISKS) \
    X(relativistic_jets, NPCRelativisticJet, MAX_RELATIVISTIC_JETS) \
    X(shock_waves, NPCShockWave, MAX_SHOCK_WAVES) \
    X(stellar_bow_shocks, NPCStellarBowShock, MAX_STELLAR_BOW_SHOCKS) \
    X(cosmic_voids, NPCCosmicVoid, MAX_COSMIC_VOIDS) \
    X(cosmic_filaments, NPCCosmicFilament, MAX_COSMIC_FILAMENTS) \
    X(event_horizons, NPCEventHorizon, MAX_EVENT_HORIZONS) \
    X(kilonovae, NPCKilonova, MAX_KILONOVAE) \
    X(grav_lenses, NPCGravLens, MAX_GRAV_LENSES) \
    X(grbs, NPCGRB, MAX_GRB) \
    X(grav_waves, NPCGravWave, MAX_GRAV_WAVES) \
    X(protoplanetary_disks, NPCProtoplanetaryDisk, MAX_PROTOPLANETARY_DISKS) \
    X(debris_disks, NPCDebrisDisk, MAX_DEBRIS_DISKS) \
    X(planetesimals, NPCPlanetesimal, MAX_PLANETESIMALS) \
    X(rogue_planets, NPCRoguePlanet, MAX_ROGUE_PLANETS) \
    X(brown_dwarfs, NPCBrownDwarf, MAX_BROWN_DWARFS) \
    X(isos, NPCISO, MAX_ISO) \
    X(mag_reconns, NPCMagReconn, MAX_MAG_RECONN) \
    X(current_sheets, NPCCurrentSheet, MAX_CURRENT_SHEETS) \
    X(heliospheres, NPCHeliosphere, MAX_HELIOSPHERES) \
    X(term_shocks, NPCTermShock, MAX_TERM_SHOCKS) \
    X(magnetospheres, NPCMagnetosphere, MAX_MAGNETOSPHERES) \
    X(cosmic_strings, NPCCosmicString, MAX_COSMIC_STRINGS) \
    X(domain_walls, NPCDomainWall, MAX_DOMAIN_WALLS) \
    X(dm_halos, NPCDMHalo, MAX_DM_HALO) \
    X(igms, NPCIGM, MAX_IGM) \
    X(cgms, NPCCGM, MAX_CGM) \
    X(lyman_alphas, NPCLymanAlpha, MAX_LYMAN_ALPHA) \
    X(cmbs, NPCCMB, MAX_CMB) \
    /* end */

typedef struct {
    const char *tag;
    void *ptr;
    size_t elem_size;
    size_t count;        /* elements written / capacity when reading */
    bool required;
} GalaxySection;

static int galaxy_sys_star_count_snapshot;   /* counts, as sections */
static int galaxy_cosmic_count_snapshot;

/* The sections, for saving (counts = what is stored now) or for loading
 * (counts = capacities). Returns how many were filled in. */
static int galaxy_sections(GalaxySection *sec, int cap, bool for_load) {
    int n = 0;
#define GALAXY_SEC(TAG, PTR, ESIZE, COUNT, REQ) \
    do { if (n < cap) sec[n++] = (GalaxySection){ (TAG), (PTR), (ESIZE), (COUNT), (REQ) }; } while (0)
    GALAXY_SEC("master", &spacegl_master, sizeof(SpaceGLGame), 1, true);
#define X(NAME, TYPE, MAX) GALAXY_SEC(#NAME, NAME, sizeof(TYPE), (MAX), false);
    GALAXY_FIXED_POOLS(X)
#undef X
    size_t ns = for_load ? (size_t)SYS_MAX_STARS : (size_t)sys_star_count;
    GALAXY_SEC("sys_star_count", &galaxy_sys_star_count_snapshot, sizeof(int), 1, false);
    GALAXY_SEC("sys_stars", sys_stars, sizeof(NPCSysStar), ns, false);
    GALAXY_SEC("sys_planets", sys_planets, sizeof(NPCSysPlanet), ns * SYS_PLANETS_PER_STAR, false);
    GALAXY_SEC("sys_moons", sys_moons, sizeof(NPCSysMoon), ns * SYS_PLANETS_PER_STAR * SYS_MOONS_PER_PLANET, false);
    GALAXY_SEC("cosmic_feature_count", &galaxy_cosmic_count_snapshot, sizeof(int), 1, false);
    GALAXY_SEC("cosmic_features", cosmic_features, sizeof(NPCCosmicFeature), MAX_COSMIC_FEATURES, false);
    GALAXY_SEC("players", players, sizeof(ConnectedPlayer), MAX_CLIENTS, false);
#undef GALAXY_SEC
    return n;
}
#define GALAXY_MAX_SECTIONS 96

/* Build the whole file in memory (caller holds game_mutex, or nothing else
 * runs: boot, shutdown). One memcpy per pool: the disk write happens later,
 * off the lock. */
GalaxyImage *galaxy_file_image(void) {
    GalaxySection sec[GALAXY_MAX_SECTIONS];
    galaxy_sys_star_count_snapshot = sys_star_count;
    galaxy_cosmic_count_snapshot = cosmic_feature_count;
    int n = galaxy_sections(sec, GALAXY_MAX_SECTIONS, false);
    size_t total = sizeof(GalaxyFileHeader);
    for (int i = 0; i < n; i++) total += sizeof(GalaxySectionHeader) + sec[i].elem_size * sec[i].count;

    GalaxyImage *img = malloc(sizeof(GalaxyImage));
    if (!img) return NULL;
    img->data = malloc(total);
    if (!img->data) { free(img); return NULL; }
    img->len = total;

    uint8_t *p = img->data;
    GalaxyFileHeader fh;
    memset(&fh, 0, sizeof(fh));
    memcpy(fh.magic, GALAXY_FILE_MAGIC, 8);
    fh.format = GALAXY_FILE_FORMAT;
    fh.galaxy_version = GALAXY_VERSION;
    fh.section_count = (uint32_t)n;
    memcpy(p, &fh, sizeof(fh)); p += sizeof(fh);
    for (int i = 0; i < n; i++) {
        GalaxySectionHeader sh;
        memset(&sh, 0, sizeof(sh));
        snprintf(sh.tag, sizeof(sh.tag), "%s", sec[i].tag);
        sh.elem_size = (uint32_t)sec[i].elem_size;
        sh.layout = GALAXY_SECTION_LAYOUT;
        sh.count = sec[i].count;
        memcpy(p, &sh, sizeof(sh)); p += sizeof(sh);
        size_t bytes = sec[i].elem_size * sec[i].count;
        if (bytes) { memcpy(p, sec[i].ptr, bytes); p += bytes; }
    }
    return img;
}

void galaxy_file_image_free(GalaxyImage *img) {
    if (!img) return;
    free(img->data);
    free(img);
}

static bool galaxy_skip(FILE *f, uint64_t bytes) {
    return fseeko(f, (off_t)bytes, SEEK_CUR) == 0;
}

int galaxy_file_read(FILE *f, uint32_t *galaxy_version) {
    GalaxyFileHeader fh;
    if (fread(&fh, sizeof(fh), 1, f) != 1 || memcmp(fh.magic, GALAXY_FILE_MAGIC, 8) != 0)
        return GALAXY_FILE_NOT_FORMAT2;   /* format 1 (before 2026.10.31.19) or not a galaxy file */
    if (galaxy_version) *galaxy_version = fh.galaxy_version;
    if (fh.format != GALAXY_FILE_FORMAT || fh.section_count > 4096) {
        printf("--- GALAXY FILE: bad header ---\n");
        return GALAXY_FILE_BAD;
    }
    GalaxySection sec[GALAXY_MAX_SECTIONS];
    int n = galaxy_sections(sec, GALAXY_MAX_SECTIONS, true);
    bool seen[GALAXY_MAX_SECTIONS] = { false };
    uint64_t file_count[GALAXY_MAX_SECTIONS] = { 0 };
    galaxy_sys_star_count_snapshot = 0;
    galaxy_cosmic_count_snapshot = 0;
    for (int i = 0; i < n; i++) memset(sec[i].ptr, 0, sec[i].elem_size * sec[i].count);

    int skipped = 0;
    for (uint32_t s = 0; s < fh.section_count; s++) {
        GalaxySectionHeader sh;
        if (fread(&sh, sizeof(sh), 1, f) != 1) { printf("--- GALAXY FILE: truncated ---\n"); return GALAXY_FILE_BAD; }
        sh.tag[sizeof(sh.tag) - 1] = '\0';
        if (sh.elem_size == 0 || sh.count > (uint64_t)1 << 32) { printf("--- GALAXY FILE: corrupt section '%s' ---\n", sh.tag); return GALAXY_FILE_BAD; }
        uint64_t bytes = (uint64_t)sh.elem_size * sh.count;
        int k = -1;
        for (int i = 0; i < n; i++) if (strcmp(sec[i].tag, sh.tag) == 0) { k = i; break; }
        if (k < 0 || seen[k]) {
            printf("[GALAXY] section '%s' not used by this version: skipped\n", sh.tag);
            if (!galaxy_skip(f, bytes)) return GALAXY_FILE_BAD;
            skipped++;
            continue;
        }
        seen[k] = true;
        if (sh.elem_size != sec[k].elem_size || sh.layout != GALAXY_SECTION_LAYOUT) {
            printf("[GALAXY] section '%s': layout changed (element %u B, layout %u; now %zu B, layout %u): pool starts empty\n",
                   sh.tag, sh.elem_size, sh.layout, sec[k].elem_size, GALAXY_SECTION_LAYOUT);
            if (sec[k].required) return GALAXY_FILE_BAD;
            seen[k] = false;
            if (!galaxy_skip(f, bytes)) return GALAXY_FILE_BAD;
            skipped++;
            continue;
        }
        file_count[k] = sh.count;
        uint64_t take = sh.count < sec[k].count ? sh.count : sec[k].count;
        if (take && fread(sec[k].ptr, sec[k].elem_size, (size_t)take, f) != (size_t)take) {
            printf("--- GALAXY FILE: section '%s' truncated ---\n", sh.tag);
            return GALAXY_FILE_BAD;
        }
        if (sh.count > take && !galaxy_skip(f, (sh.count - take) * sh.elem_size)) return GALAXY_FILE_BAD;
    }
    for (int i = 0; i < n; i++) {
        if (!seen[i] && sec[i].required) { printf("--- GALAXY FILE: section '%s' missing ---\n", sec[i].tag); return GALAXY_FILE_BAD; }
    }

    /* counts: validated against what the sections actually carried */
    int ns = galaxy_sys_star_count_snapshot;   /* 0 when the section is absent */
    bool sys_ok = ns >= 0 && ns <= SYS_MAX_STARS;
    for (int i = 0; i < n && sys_ok; i++) {
        uint64_t want;
        if (strcmp(sec[i].tag, "sys_star_count") == 0) want = 1;
        else if (strcmp(sec[i].tag, "sys_stars") == 0) want = (uint64_t)ns;
        else if (strcmp(sec[i].tag, "sys_planets") == 0) want = (uint64_t)ns * SYS_PLANETS_PER_STAR;
        else if (strcmp(sec[i].tag, "sys_moons") == 0) want = (uint64_t)ns * SYS_PLANETS_PER_STAR * SYS_MOONS_PER_PLANET;
        else continue;
        if (!seen[i] || file_count[i] != want) sys_ok = false;
    }
    if (!sys_ok) {
        if (ns != 0) printf("[GALAXY] star systems not loadable: they will be generated again\n");
        memset(sys_stars, 0, sizeof(sys_stars));
        memset(sys_planets, 0, sizeof(sys_planets));
        memset(sys_moons, 0, sizeof(sys_moons));
        ns = 0;
    }
    sys_star_count = ns;
    cosmic_feature_count = galaxy_cosmic_count_snapshot;
    if (cosmic_feature_count < 0 || cosmic_feature_count > MAX_COSMIC_FEATURES) cosmic_feature_count = 0;
    if (skipped) printf("[GALAXY] %d section(s) skipped: the galaxy is completed below\n", skipped);
    return GALAXY_FILE_OK;
}
