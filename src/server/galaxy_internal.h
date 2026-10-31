/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * galaxy_internal.h — functions shared by the galaxy translation units
 * (2026.10.31.16): galaxy.c (globals, generation, moons, star systems),
 * galaxy_io.c (save and load), galaxy_index.c (spatial index),
 * galaxy_names.c (names). Private to src/server/galaxy*.c.
 *
 * load_galaxy() completes a galaxy.dat written before some object families
 * existed (same layout, empty pools) with the generator's spawners, and
 * prints the star-system block of the astrometrics report.
 */
#ifndef GALAXY_INTERNAL_H
#define GALAXY_INTERNAL_H

#include <stdlib.h>
#include "game_config.h"

/* A random coordinate inside a quadrant: [0, QUADRANT_SIZE) in steps of
 * 1/RATIO_COORD_RANDOM. The int from rand() % 1000 is exact as a float, so
 * the explicit conversion changes no value (-Wconversion, 2026.10.31.26). */
static inline float galaxy_rand_coord(void) {
    return (float)(rand() % (int)(QUADRANT_SIZE * RATIO_COORD_RANDOM)) / RATIO_COORD_RANDOM;
}

/* A local coordinate forced inside the quadrant, [0, QUADRANT_SIZE). */
static inline double galaxy_clamp_local(double v) {
    const double hi = QUADRANT_SIZE - 1.0 / RATIO_COORD_RANDOM;
    if (!(v >= 0.0)) return 0.0;   /* NaN too */
    return v > hi ? hi : v;
}

void spawn_evolved_stars(void);
void spawn_protostellar_objects(void);
void spawn_moons(void);
void print_star_system_report(void);
const char *get_random_ship_name(int faction);   /* galaxy_names.c */

#endif
