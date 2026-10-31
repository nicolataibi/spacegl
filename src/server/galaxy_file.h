/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * galaxy_file.h — the galaxy.dat format (src/server/galaxy_file.c), shared
 * by the server (galaxy_io.c) and the offline inspector (spacegl_viewer).
 */
#ifndef GALAXY_FILE_H
#define GALAXY_FILE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint8_t *data;
    size_t len;
} GalaxyImage;

/* The whole file of the current galaxy, in memory (format 2). NULL when
 * out of memory. Free it with galaxy_file_image_free(). */
GalaxyImage *galaxy_file_image(void);
void galaxy_file_image_free(GalaxyImage *img);

#define GALAXY_FILE_OK           1    /* loaded (some pools may be empty) */
#define GALAXY_FILE_BAD          0    /* format 2 but unreadable: generate anew */
#define GALAXY_FILE_NOT_FORMAT2 (-1)  /* format 1 or not a galaxy file */

/* Reads a format-2 file from the start of f into the pools (each pool is
 * cleared first). Sets sys_star_count and cosmic_feature_count, and
 * *galaxy_version (if not NULL) to the GALAXY_VERSION the file was written
 * with. A section whose element size or layout differs is skipped and its
 * pool left empty (reported on stdout); an unknown section is skipped. */
int galaxy_file_read(FILE *f, uint32_t *galaxy_version);

#endif
