/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * frame_bench_test — the viewers' frame-time helpers (2026.11.01.15):
 *  1. include/frame_bench.h: the warm-up frames are skipped, the timed
 *     ones give the right average and percentiles, completion is reported
 *     on the last frame, nothing is written past the array (ASan);
 *  2. include/bench_scene.h: the busy quadrant (the captain first, then
 *     objects of several kinds inside the quadrant), the same on every
 *     call and in the three buffers, clamped to MAX_OBJECTS.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_scene.h"
#include "frame_bench.h"

#define TEST_CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "TEST FAILED: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

int main(void) {
    /* 1. 5 warm-up frames of 100 ms, then 100 frames of 1..100 ms */
    FrameBench b;
    TEST_CHECK(frame_bench_init(&b, 100, 5));
    for (int i = 0; i < 5; i++) TEST_CHECK(!frame_bench_add(&b, 100.0));
    for (int i = 1; i <= 100; i++) {
        bool done = frame_bench_add(&b, (double)(101 - i));   /* in reverse: the stats sort */
        TEST_CHECK(done == (i == 100));
    }
    TEST_CHECK(frame_bench_add(&b, 1000.0));   /* past the end: ignored */
    FrameStats s = frame_bench_stats(&b);
    TEST_CHECK(s.n == 100);
    TEST_CHECK(s.avg > 50.49 && s.avg < 50.51);
    TEST_CHECK(s.p50 == 50.0 && s.p95 == 95.0 && s.p99 == 99.0 && s.max == 100.0);
    frame_bench_report(&b, "test", 640, 480, 7);
    frame_bench_free(&b);
    TEST_CHECK(!frame_bench_init(&b, 0, 0));
    printf("  [1] warm-up skipped, average and percentiles: OK\n");

    /* 2. the busy quadrant */
    SharedIPC *a = calloc(1, sizeof(SharedIPC));
    SharedIPC *c = calloc(1, sizeof(SharedIPC));
    TEST_CHECK(a && c);
    TEST_CHECK(bench_scene_fill(a, 256) == 256);
    TEST_CHECK(bench_scene_fill(c, 256) == 256);
    TEST_CHECK(memcmp(a->buffers, c->buffers, sizeof(a->buffers)) == 0);   /* the same scene */
    for (int bi = 0; bi < 3; bi++) {
        GameState *st = &a->buffers[bi];
        TEST_CHECK(st->object_count == 256 && st->frame_id == 1);
        TEST_CHECK(st->objects[0].type == 1 && st->objects[0].active && strcmp(st->objects[0].shm_name, "BENCH") == 0);
        int kinds[64] = { 0 };
        for (int i = 1; i < 256; i++) {
            SharedObject *o = &st->objects[i];
            TEST_CHECK(o->active && o->shm_x >= 5.0 && o->shm_x <= 95.0 && o->shm_y >= 5.0 && o->shm_y <= 95.0 &&
                       o->shm_z >= 5.0 && o->shm_z <= 95.0);
            TEST_CHECK(o->type > 0 && o->type < 64);
            kinds[o->type] = 1;
        }
        int n_kinds = 0;
        for (int k = 0; k < 64; k++) n_kinds += kinds[k];
        TEST_CHECK(n_kinds >= 15);
    }
    TEST_CHECK(atomic_load(&a->read_index) == 0);
    TEST_CHECK(bench_scene_fill(c, 100000) == MAX_OBJECTS);
    TEST_CHECK(bench_scene_fill(c, 0) == 1);
    free(a);
    free(c);
    printf("  [2] busy quadrant: 256 objects, %s: OK\n", "deterministic, in every buffer");

    printf("PASS: frame_bench\n");
    return 0;
}
