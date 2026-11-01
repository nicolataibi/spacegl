/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * bench_scene.h — a busy quadrant for the viewers' frame-time runs
 * (2026.11.01.15, --bench-scene N): the captain's ship at the centre and
 * N - 1 objects of every common kind (ships of each faction, starbases,
 * stars, planets, asteroids, wrecks, mines, buoys, platforms, comets,
 * nebulae) at fixed pseudo-random places, written into an empty shared
 * memory segment as if a command deck had sent them. The same scene on
 * every run (no motion, one frame id). Header-only.
 */
#ifndef BENCH_SCENE_H
#define BENCH_SCENE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "shared_state.h"

static inline int bench_scene_fill(SharedIPC *s, int n) {
    static const int kinds[] = { 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 3, 4, 5, 5, 21, 21, 22, 23, 24, 25, 9, 7 };
    const int nk = (int)(sizeof(kinds) / sizeof(kinds[0]));
    if (n < 1) n = 1;
    if (n > MAX_OBJECTS) n = MAX_OBJECTS;
    uint32_t rng = 20261101u;
    for (int b = 0; b < 3; b++) {
        GameState *st = &s->buffers[b];
        rng = 20261101u;
        st->shm_q[0] = st->shm_q[1] = st->shm_q[2] = 20;
        st->shm_s[0] = st->shm_s[1] = st->shm_s[2] = 50.0;
        st->object_count = n;
        for (int i = 0; i < n; i++) {
            SharedObject *o = &st->objects[i];
            memset(o, 0, sizeof(*o));
            o->active = 1;
            o->id = i == 0 ? 1 : 1000 + i;
            o->health_pct = 100;
            o->hull_integrity = 100;
            o->energy = 50000;
            if (i == 0) {
                o->type = 1;
                o->shm_x = o->shm_y = o->shm_z = 50.0;
                snprintf(o->shm_name, sizeof(o->shm_name), "BENCH");
                continue;
            }
            rng = rng * 1664525u + 1013904223u;
            o->shm_x = 5.0 + (double)(rng >> 8) / (double)(1u << 24) * 90.0;
            rng = rng * 1664525u + 1013904223u;
            o->shm_y = 5.0 + (double)(rng >> 8) / (double)(1u << 24) * 90.0;
            rng = rng * 1664525u + 1013904223u;
            o->shm_z = 5.0 + (double)(rng >> 8) / (double)(1u << 24) * 90.0;
            o->h = (double)((i * 37) % 360);
            o->m = (double)((i * 11) % 60) - 30.0;
            o->type = kinds[i % nk];
            o->ship_class = i % 7;
            o->faction = i % 12;
            snprintf(o->shm_name, sizeof(o->shm_name), "Object %d", i);
        }
        st->frame_id = 1;
    }
    atomic_store(&s->read_index, 0);
    return n;
}

#endif
