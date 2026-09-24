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
 * Static per-quadrant spatial index (server).
 *
 * Every static galactic object (stars, planets, bases, black holes,
 * nebulas, pulsars, quasars, storms, dysons, hubs, relics, ruptures,
 * satellites, the anomaly types and the environment types) is placed
 * exactly once, at server startup, into the quadrant it lives in.
 * The per-frame update builder (logic.c), the LRS grid
 * (refresh_lrs_grid) and the LRS/SRS object lists (commands.c) all
 * read their quadrant contents from that index: a type that is never
 * inserted there has a count of 0 forever and is never sent to the
 * clients (B3: the quasar — and the other 49 static types — simply
 * did not appear in the quadrant / HUD object list).
 *
 * The insertion is table-driven (one descriptor per static type,
 * field offsets resolved with offsetof, so no per-type code to drift
 * out of sync with the struct definitions) and dependency-free on
 * purpose: the independent test project (tests/) compiles this TU
 * directly, so the regression test exercises the real production
 * table and algorithm.
 */

#ifndef QUAD_INDEX_H
#define QUAD_INDEX_H

#include <stddef.h>

#include "server_internal.h"

/* One static type as seen by the index builder. The object arrays
 * are the production globals (owned by galaxy.c); the offsets are
 * compile-time constants over the production structs. */
typedef struct {
    const char *name;        /* short name ("quasar", "planet", ...) */
    const void *objects;     /* global NPCX array (the real one) */
    size_t obj_size;         /* sizeof(NPCX) */
    int total;               /* length of the array (MAX_X) */
    int slot_max;            /* per-quadrant cap (MAX_Q_X) */
    ptrdiff_t off_q1;        /* offsetof(NPCX, q1)   */
    ptrdiff_t off_q2;        /* offsetof(NPCX, q2)   */
    ptrdiff_t off_q3;        /* offsetof(NPCX, q3)   */
    ptrdiff_t off_active;    /* offsetof(NPCX, active) */
    ptrdiff_t off_slots;     /* offsetof(QuadrantIndex, <slots array>) */
    ptrdiff_t off_count;     /* offsetof(QuadrantIndex, <count>)      */
} QuadStaticTypeInfo;

/*
 * Insert every static object (active, in a valid quadrant) into the
 * per-quadrant index, honoring the per-type per-quadrant caps.
 *
 * `grid` must be a freshly zeroed 41x41x41 QuadrantIndex volume
 * (init_static_spatial_index() does the allocate/memset before
 * calling this; quadrants are addressed 1..GALAXY_SIZE).
 */
void quad_index_insert_static_all(QuadrantIndex (*grid)[41][41]);

/* Number of static types in the production table. */
int quad_index_static_type_count(void);

/* Descriptor of type `index` (0-based), NULL if out of range. */
const QuadStaticTypeInfo *quad_index_static_type_info(int index);

/* 0-based index of the type with the given name, -1 if absent. */
int quad_index_static_type_find(const char *name);

#endif /* QUAD_INDEX_H */
