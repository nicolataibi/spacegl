/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * galaxy_io.c — galaxy persistence: the save (an in-memory image built
 * under the lock, written by the thread pool to a temporary file, fsync'd
 * and renamed), the save requests of other threads and the load policy
 * (repairs, completion of empty families, report, index). The file format
 * itself is galaxy_file.c (2026.10.31.31).
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

/* The file format (header, sections, image, reader): galaxy_file.c. */
#include "galaxy_file.h"

static atomic_bool g_is_saving = false;

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
    galaxy_file_image_free(img);
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

    GalaxyImage *img = galaxy_file_image();
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

/* Load repairs of galaxies written by older versions (the format is the
 * same; only some positions were wrong). */
static void galaxy_repair_after_read(void) {
    /* A system generated before 2026.10.31.28 may reach past a quadrant
     * face (its radius took only the outermost planet's envelope): move
     * its star inward until the whole reach fits, orbits unchanged. */
    {
        int moved = 0;
        for (int s_ = 0; s_ < sys_star_count; s_++) {
            NPCSysStar *st = &sys_stars[s_];
            if (!st->active) continue;
            double orbit_r[SYS_PLANETS_PER_STAR] = {0};
            int n_moons[SYS_PLANETS_PER_STAR] = {0};
            int np = st->n_planets < SYS_PLANETS_PER_STAR ? st->n_planets : SYS_PLANETS_PER_STAR;
            if (np < 0) np = 0;
            for (int k = 0; k < np; k++) {
                const NPCSysPlanet *pl = &sys_planets[sys_planet_slot(s_, k)];
                if (!pl->active) continue;
                orbit_r[k] = pl->orbit_r;
                n_moons[k] = pl->n_moons < SYS_MOONS_PER_PLANET ? pl->n_moons : SYS_MOONS_PER_PLANET;
            }
            double reach = sys_reach(orbit_r, n_moons, np);
            double lo = reach + SYS_FACE_MARGIN, hi = (double)QUADRANT_SIZE - reach - SYS_FACE_MARGIN;
            if (lo > hi) continue;   /* cannot fit at all: left as it is */
            double *c[3] = { &st->x, &st->y, &st->z };
            bool out = false;
            for (int i_ = 0; i_ < 3; i_++) {
                if (*c[i_] < lo) { *c[i_] = lo; out = true; }
                else if (*c[i_] > hi) { *c[i_] = hi; out = true; }
            }
            if (out) moved++;
        }
        if (moved > 0) printf("[GALAXY] %d star systems moved inside their quadrant\n", moved);
    }
    { double t_ = sys_clock_s(); for (int s_ = 0; s_ < sys_star_count; s_++) sys_refresh_positions(s_, t_); }
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

    /* Protostellar objects of a galaxy generated before 2026.10.31.28 lie up
     * to 300 units from their nursery, outside their quadrant: put each
     * coordinate that is out of range back at a random place inside it. */
    {
        int moved = 0;
        for (int i = 0; i < cosmic_feature_count && i < MAX_COSMIC_FEATURES; i++) {
            NPCCosmicFeature *f = &cosmic_features[i];
            if (!f->active || !obj_type_is_protostellar(f->type)) continue;
            bool out = false;
            if (!(f->x >= 0.0 && f->x < QUADRANT_SIZE)) { f->x = galaxy_rand_coord(); out = true; }
            if (!(f->y >= 0.0 && f->y < QUADRANT_SIZE)) { f->y = galaxy_rand_coord(); out = true; }
            if (!(f->z >= 0.0 && f->z < QUADRANT_SIZE)) { f->z = galaxy_rand_coord(); out = true; }
            if (out) moved++;
        }
        if (moved > 0)
            printf("[GALAXY] %d protostellar objects moved back inside their quadrant\n", moved);
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
    int r = galaxy_file_read(f, NULL);
    fclose(f);
    if (r == GALAXY_FILE_NOT_FORMAT2) {
        /* format 1 (the raw dump before 2026.10.31.19) or not a galaxy file */
        printf("--- GALAXY FILE NOT IN FORMAT 2 (old format or corrupt): a new galaxy is generated ---\n");
        return 0;
    }
    if (r != GALAXY_FILE_OK) return 0;
    galaxy_repair_after_read();
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
