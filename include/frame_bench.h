/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * frame_bench.h — frame times of the viewers (2026.11.01.15), for
 * spacegl_vulkan and spacegl_3dview --bench-frames N: after W warm-up
 * frames, N frames are timed from the start of the frame until the GPU
 * has finished it (the viewer waits for the GPU after each frame: the
 * frame's whole cost, CPU and GPU, with no overlap between frames and no
 * vsync), and one line is printed:
 *
 *   [BENCH] <what> <W>x<H>, <n> objects: <N> frames, frame time average
 *   A ms, p50 B, p95 C, p99 D, max E (F fps)
 *
 * tests/gpu_golden_check.sh and tests/viewer_bench_test.sh read it.
 * Header-only.
 */
#ifndef FRAME_BENCH_H
#define FRAME_BENCH_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    double *ms;
    int frames;   /* to time */
    int warmup;   /* to skip first */
    int seen;     /* frames recorded, warm-up included */
} FrameBench;

static inline bool frame_bench_init(FrameBench *b, int frames, int warmup) {
    memset(b, 0, sizeof(*b));
    if (frames < 1) return false;
    b->ms = (double *)calloc((size_t)frames, sizeof(double));
    if (!b->ms) return false;
    b->frames = frames;
    b->warmup = warmup < 0 ? 0 : warmup;
    return true;
}

/* One frame; true once the last timed frame is in. */
static inline bool frame_bench_add(FrameBench *b, double ms) {
    if (!b->ms) return false;
    if (b->seen >= b->warmup && b->seen - b->warmup < b->frames) b->ms[b->seen - b->warmup] = ms;
    b->seen++;
    return b->seen >= b->warmup + b->frames;
}

static inline int frame_bench_cmp(const void *x, const void *y) {
    double a = *(const double *)x, c = *(const double *)y;
    return (a > c) - (a < c);
}

typedef struct {
    int n;
    double avg, p50, p95, p99, max;
} FrameStats;

static inline FrameStats frame_bench_stats(const FrameBench *b) {
    FrameStats s;
    memset(&s, 0, sizeof(s));
    int n = b->seen - b->warmup;
    if (n > b->frames) n = b->frames;
    if (n <= 0 || !b->ms) return s;
    double *v = (double *)malloc(sizeof(double) * (size_t)n);
    if (!v) return s;
    memcpy(v, b->ms, sizeof(double) * (size_t)n);
    qsort(v, (size_t)n, sizeof(double), frame_bench_cmp);
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += v[i];
    s.n = n;
    s.avg = sum / n;
    s.p50 = v[(int)((n - 1) * 0.50)];
    s.p95 = v[(int)((n - 1) * 0.95)];
    s.p99 = v[(int)((n - 1) * 0.99)];
    s.max = v[n - 1];
    free(v);
    return s;
}

static inline void frame_bench_report(const FrameBench *b, const char *what, int w, int h, int objects) {
    FrameStats s = frame_bench_stats(b);
    printf("[BENCH] %s %dx%d, %d objects: %d frames, frame time average %.3f ms, p50 %.3f, p95 %.3f, "
           "p99 %.3f, max %.3f (%.0f fps)\n",
           what, w, h, objects, s.n, s.avg, s.p50, s.p95, s.p99, s.max, s.avg > 0 ? 1000.0 / s.avg : 0.0);
    fflush(stdout);
}

static inline void frame_bench_free(FrameBench *b) {
    free(b->ms);
    memset(b, 0, sizeof(*b));
}

#endif
