/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * sphere_lod.h — the levels of detail of the UV sphere of the CPU-driven
 * Vulkan path and the choice among them (2026.11.07.01). Plain C, no
 * Vulkan: the viewer (vulkan_cpu_lod.h, vulkan_scene.c) and
 * tests/sphere_lod_test.c use the same code.
 *
 * Level l has lats >> l by longs >> l steps (64x128, 32x64, 16x32, 8x16
 * for the viewer). All levels live in one vertex and one index array:
 * level 0 first, at index 0, exactly the mesh drawn before; the indices
 * of every level point into the shared vertex array.
 *
 * sphere_lod_pick() returns the coarsest level whose silhouette stays
 * within half a pixel of the true sphere at its size on screen:
 *   - filled: the chord of one step deviates from the circle by
 *     r (1 - cos(pi / longs)) pixels;
 *   - wireframe (the triangle edges drawn as lines): fewer triangles are
 *     fewer lines, so a level only while its lines are at most half a
 *     pixel apart (2 pi r / longs <= 0.5), where the full mesh already
 *     fills the disc;
 * with r the radius in pixels plus 25 % (an off-axis sphere is stretched
 * by the perspective). Near, around or behind the camera: level 0.
 */
#ifndef SPHERE_LOD_H
#define SPHERE_LOD_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SPHERE_LOD_LEVELS     4
#define SPHERE_LOD_MARGIN     1.25f    /* on the radius in pixels */
#define SPHERE_LOD_MAX_ERR_PX 0.5f /* silhouette error, or line spacing (wireframe) */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Vertices and indices of all levels together. */
static inline void sphere_lod_mesh_size(int lats, int longs, size_t *n_vertices, size_t *n_indices) {
    size_t nv = 0, ni = 0;
    for (int l = 0; l < SPHERE_LOD_LEVELS; l++) {
        size_t la = (size_t)(lats >> l), lo = (size_t)(longs >> l);
        nv += (la + 1) * (lo + 1);
        ni += la * lo * 6;
    }
    *n_vertices = nv;
    *n_indices = ni;
}

/* Fills pos (3 floats a vertex, on the unit sphere; the normal is the
 * same vector) and idx, and the index range of each level. Level 0 is
 * the lats x longs sphere the viewer always drew, in the same order. */
static inline void sphere_lod_build(int lats, int longs, float *pos, uint32_t *idx, uint32_t first[SPHERE_LOD_LEVELS],
                                    uint32_t count[SPHERE_LOD_LEVELS]) {
    uint32_t nv = 0, ni = 0;
    for (int l = 0; l < SPHERE_LOD_LEVELS; l++) {
        int la = lats >> l, lo = longs >> l;
        uint32_t base = nv;
        first[l] = ni;
        for (int i = 0; i <= la; i++) {
            float lat = (float)(M_PI * i / la);
            for (int j = 0; j <= lo; j++) {
                float lon = (float)(2.0f * M_PI * j / lo);
                pos[nv * 3 + 0] = sinf(lat) * cosf(lon);
                pos[nv * 3 + 1] = cosf(lat);
                pos[nv * 3 + 2] = sinf(lat) * sinf(lon);
                nv++;
            }
        }
        for (int i = 0; i < la; i++) {
            for (int j = 0; j < lo; j++) {
                uint32_t a = base + (uint32_t)(i * (lo + 1) + j);
                uint32_t b = a + (uint32_t)lo + 1;
                idx[ni++] = a;
                idx[ni++] = b;
                idx[ni++] = a + 1;
                idx[ni++] = b;
                idx[ni++] = b + 1;
                idx[ni++] = a + 1;
            }
        }
        count[l] = ni - first[l];
    }
}

/* The largest radius in pixels a level may be drawn at (level 0: any). */
static inline float sphere_lod_limit_px(int level, int longs, bool wire) {
    if (level <= 0) return INFINITY;
    float steps = (float)(longs >> level);
    float r_err = wire ? SPHERE_LOD_MAX_ERR_PX * steps / (2.0f * (float)M_PI)
                       : SPHERE_LOD_MAX_ERR_PX / (1.0f - cosf((float)M_PI / steps));
    return r_err / SPHERE_LOD_MARGIN;
}

/* model, view: row-vector matrices (v * model * view), as the viewer
 * pushes them; rows 0-2 of model are the images of the axes, row 3 the
 * centre. px_per_unit: |proj[1][1]| * height / 2. The camera looks along
 * -Z in view space. */
static inline int sphere_lod_pick(const float model[4][4], const float view[4][4], float px_per_unit, bool wire,
                                  int longs) {
    if (!(px_per_unit > 0.0f)) return 0;
    float r2 = 0.0f;
    for (int i = 0; i < 3; i++) {
        float a2 = model[i][0] * model[i][0] + model[i][1] * model[i][1] + model[i][2] * model[i][2];
        if (a2 > r2) r2 = a2;
    }
    float r = sqrtf(r2);
    const float *c = model[3];
    float z = -(c[0] * view[0][2] + c[1] * view[1][2] + c[2] * view[2][2] + view[3][2]);
    if (!(r > 0.0f) || !(z > 2.0f * r)) return 0;
    float r_px = r * px_per_unit / z;
    for (int l = SPHERE_LOD_LEVELS - 1; l > 0; l--)
        if (r_px <= sphere_lod_limit_px(l, longs, wire)) return l;
    return 0;
}

#endif
