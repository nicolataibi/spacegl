/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * galaxy_io.c — galaxy persistence: the galaxy.dat file format, the save
 * (an in-memory image built under the lock, written by the thread pool to a
 * temporary file, fsync'd and renamed), the save requests of other threads
 * and the load, of the current format and of the previous one.
 * Split from galaxy.c (2026.10.31.16); file format 2 since 2026.10.31.19.
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

#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/stat.h>

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

static atomic_bool g_is_saving = false;

typedef struct {
    uint8_t *data;
    size_t len;
} GalaxyImage;

/* Build the whole file in memory (caller holds game_mutex, or nothing else
 * runs: boot, shutdown). One memcpy per pool: the disk write happens later,
 * off the lock. */
static GalaxyImage *galaxy_image_build(void) {
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

/* Write the image to galaxy.dat.tmp, fsync, rename over galaxy.dat: a
 * crash in the middle leaves the previous galaxy.dat intact. */
static bool galaxy_image_write(const GalaxyImage *img) {
    char path[1088], tmp[1100];
    server_data_path(path, sizeof(path), "galaxy.dat");
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        slog("ERROR: Failed to open %s for writing: %s\n", tmp, SG_STRERROR(errno));
        return false;
    }
    size_t done = 0;
    while (done < img->len) {
        ssize_t w = write(fd, img->data + done, img->len - done);
        if (w < 0) {
            if (errno == EINTR) continue;
            slog("ERROR: Failed to write %s: %s\n", tmp, SG_STRERROR(errno));
            close(fd); unlink(tmp);
            return false;
        }
        done += (size_t)w;
    }
    if (fsync(fd) != 0) slog("WARNING: fsync %s: %s\n", tmp, SG_STRERROR(errno));
    close(fd);
    if (rename(tmp, path) != 0) {
        slog("ERROR: Failed to rename %s to %s: %s\n", tmp, path, SG_STRERROR(errno));
        unlink(tmp);
        return false;
    }
    /* the rename itself must reach the disk: fsync the directory */
    char dir[1088];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        int dfd = open(dir[0] ? dir : "/", O_RDONLY | O_DIRECTORY);
        if (dfd >= 0) { (void)fsync(dfd); close(dfd); }
    }
    return true;
}

static void galaxy_save_finish(GalaxyImage *img) {
    bool ok = galaxy_image_write(img);
    if (ok) {
        time_t now = time(NULL);
        char ts_buf[32];
        char *ts = ctime_r(&now, ts_buf);
        if (ts) ts[strlen(ts)-1] = '\0';
        slog("--- [%s] GALAXY SAVED ASYNCHRONOUSLY (POOL) ---\n", ts);
    }
    free(img->data);
    free(img);
    atomic_store(&g_is_saving, false);
}

static void galaxy_save_task(void *arg) {
    galaxy_save_finish((GalaxyImage *)arg);
}

extern threadpool_t *g_pool;

/* Save the galaxy. Callers hold game_mutex (or run alone: boot, shutdown).
 * Under the lock only the in-memory image is built; the thread pool
 * writes it. Without a pool (shutdown: it is already drained and
 * destroyed) the write happens here. A save while another one is still
 * being written is skipped, as before. */
void save_galaxy_async() {
    bool expected = false;
    if (!atomic_compare_exchange_strong(&g_is_saving, &expected, true)) return;

    GalaxyImage *img = galaxy_image_build();
    if (!img) {
        slog("ERROR: galaxy save: out of memory for the save image\n");
        atomic_store(&g_is_saving, false);
        return;
    }
    if (g_pool && threadpool_add_task(g_pool, galaxy_save_task, img) == 0) return;
    galaxy_save_finish(img);
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

static bool galaxy_skip(FILE *f, uint64_t bytes) {
    return fseeko(f, (off_t)bytes, SEEK_CUR) == 0;
}

/* Format 2 reader. Returns 1 when the galaxy was loaded (possibly with
 * some pools left empty), 0 when it must be generated anew. */
static int load_galaxy_v2(FILE *f) {
    GalaxyFileHeader fh;
    if (fread(&fh, sizeof(fh), 1, f) != 1 || memcmp(fh.magic, GALAXY_FILE_MAGIC, 8) != 0 ||
        fh.format != GALAXY_FILE_FORMAT || fh.section_count > 4096) {
        printf("--- GALAXY FILE: bad header ---\n");
        return 0;
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
        if (fread(&sh, sizeof(sh), 1, f) != 1) { printf("--- GALAXY FILE: truncated ---\n"); return 0; }
        sh.tag[sizeof(sh.tag) - 1] = '\0';
        if (sh.elem_size == 0 || sh.count > (uint64_t)1 << 32) { printf("--- GALAXY FILE: corrupt section '%s' ---\n", sh.tag); return 0; }
        uint64_t bytes = (uint64_t)sh.elem_size * sh.count;
        int k = -1;
        for (int i = 0; i < n; i++) if (strcmp(sec[i].tag, sh.tag) == 0) { k = i; break; }
        if (k < 0 || seen[k]) {
            printf("[GALAXY] section '%s' not used by this version: skipped\n", sh.tag);
            if (!galaxy_skip(f, bytes)) return 0;
            skipped++;
            continue;
        }
        seen[k] = true;
        if (sh.elem_size != sec[k].elem_size || sh.layout != GALAXY_SECTION_LAYOUT) {
            printf("[GALAXY] section '%s': layout changed (element %u B, layout %u; now %zu B, layout %u): pool starts empty\n",
                   sh.tag, sh.elem_size, sh.layout, sec[k].elem_size, GALAXY_SECTION_LAYOUT);
            if (sec[k].required) return 0;
            seen[k] = false;
            if (!galaxy_skip(f, bytes)) return 0;
            skipped++;
            continue;
        }
        file_count[k] = sh.count;
        uint64_t take = sh.count < sec[k].count ? sh.count : sec[k].count;
        if (take && fread(sec[k].ptr, sec[k].elem_size, (size_t)take, f) != (size_t)take) {
            printf("--- GALAXY FILE: section '%s' truncated ---\n", sh.tag);
            return 0;
        }
        if (sh.count > take && !galaxy_skip(f, (sh.count - take) * sh.elem_size)) return 0;
    }
    for (int i = 0; i < n; i++) {
        if (!seen[i] && sec[i].required) { printf("--- GALAXY FILE: section '%s' missing ---\n", sec[i].tag); return 0; }
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
    { double t_ = sys_clock_s(); for (int s_ = 0; s_ < sys_star_count; s_++) sys_refresh_positions(s_, t_); }
    cosmic_feature_count = galaxy_cosmic_count_snapshot;
    if (cosmic_feature_count < 0 || cosmic_feature_count > MAX_COSMIC_FEATURES) cosmic_feature_count = 0;
    if (skipped) printf("[GALAXY] %d section(s) skipped: the galaxy is completed below\n", skipped);
    return 1;
}

/* After the load: complete the families a skipped section left empty, reset
 * the connection state of the captains, report and index. */
static void galaxy_complete_after_load(void) {
    /* No evolved stars after the load (their section was skipped): add them
     * (only the generic feature array gets new entries). */
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

    /* Fewer moons than planets after the load (a skipped section): ensure
     * that EVERY active planet gets at least one moon. */
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

    /* No protostellar objects after the load (a skipped section): add them */
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

    /* No extended moons after the load (a skipped section): add them */
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
                    .x = galaxy_rand_coord(),
                    .y = galaxy_rand_coord(),
                    .z = galaxy_rand_coord(),
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
}

int load_galaxy() {
    char galaxy_path[1088];
    server_data_path(galaxy_path, sizeof(galaxy_path), "galaxy.dat");
    FILE *f = fopen(galaxy_path, "rb");
    if (!f) return 0;
    char magic[8] = { 0 };
    size_t got = fread(magic, 1, sizeof(magic), f);
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    if (got != sizeof(magic) || memcmp(magic, GALAXY_FILE_MAGIC, 8) != 0) {
        /* format 1 (the raw dump before 2026.10.31.19) or not a galaxy file */
        printf("--- GALAXY FILE NOT IN FORMAT 2 (old format or corrupt): a new galaxy is generated ---\n");
        fclose(f);
        return 0;
    }
    int ok = load_galaxy_v2(f);
    fclose(f);
    if (!ok) return 0;
    /* no star systems after the load (a file whose star-system sections were
     * skipped): generate
     * them, in the quadrants that are still empty */
    if (sys_star_count == 0) {
        spawn_star_systems();
        printf("[GALAXY] star systems generated for the loaded galaxy: %d\n", sys_star_count);
    }
    galaxy_complete_after_load();
    return 1;
}
