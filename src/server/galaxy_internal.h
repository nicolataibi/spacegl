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

void spawn_evolved_stars(void);
void spawn_protostellar_objects(void);
void spawn_moons(void);
void print_star_system_report(void);
const char *get_random_ship_name(int faction);   /* galaxy_names.c */

#endif
