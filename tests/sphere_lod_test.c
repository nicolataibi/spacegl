/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * sphere_lod_test — the levels of detail of the CPU-driven Vulkan sphere
 * (include/sphere_lod.h, 2026.11.07.01).
 *  1. Mesh: four levels (64x128 ... 8x16) in one array; level 0 is, index
 *     for index and vertex for vertex, the mesh the viewer drew before
 *     (rebuilt here with the old loops); every index of a level points
 *     into that level's own vertices; every vertex is on the unit sphere.
 *  2. Choice, filled: for radii from 0.5 to 2000 px, the level picked
 *     keeps the true silhouette error, r (1 - cos(pi / longs)), within
 *     0.5 px even with the 25 % margin used up, and the next coarser
 *     level would not; level 0 beyond 333 px.
 *  3. Choice, wireframe: the lines of the level picked are at most 1 px
 *     apart (so the disc stays covered as with the full mesh;
 *     2026.11.07.03, 0.5 px before).
 *  4. Edge cases: a sphere around, behind or very near the camera, a zero
 *     scale, no projection yet: level 0. A non-uniform scale takes its
 *     largest axis; a rotated or off-centre model and a moved camera give
 *     the depth along the view axis.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sphere_lod.h"

#define LATS  64
#define LONGS 128

static int g_checks = 0;
#define TEST_CHECK(cond) \
    do { \
        g_checks++; \
        if (!(cond)) { \
            fprintf(stderr, "TEST FAILED: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

static void identity(float m[4][4]) {
    memset(m, 0, sizeof(float) * 16);
    for (int i = 0; i < 4; i++) m[i][i] = 1.0f;
}

/* model: uniform scale r at (x, y, z); view: identity (camera at the
 * origin looking along -Z) unless given */
static int pick_at(float r, float x, float y, float z, float px_per_unit, bool wire) {
    float model[4][4], view[4][4];
    identity(model);
    identity(view);
    model[0][0] = model[1][1] = model[2][2] = r;
    model[3][0] = x;
    model[3][1] = y;
    model[3][2] = z;
    return sphere_lod_pick((const float(*)[4])model, (const float(*)[4])view, px_per_unit, wire, LONGS);
}

int main(void) {
    /* 1. mesh */
    size_t nv = 0, ni = 0;
    sphere_lod_mesh_size(LATS, LONGS, &nv, &ni);
    TEST_CHECK(nv == 65u * 129u + 33u * 65u + 17u * 33u + 9u * 17u);
    TEST_CHECK(ni == 6u * (64u * 128u + 32u * 64u + 16u * 32u + 8u * 16u));
    float *pos = malloc(nv * 3 * sizeof(float));
    uint32_t *idx = malloc(ni * sizeof(uint32_t));
    TEST_CHECK(pos && idx);
    uint32_t first[SPHERE_LOD_LEVELS], count[SPHERE_LOD_LEVELS];
    sphere_lod_build(LATS, LONGS, pos, idx, first, count);
    TEST_CHECK(first[0] == 0 && count[0] == LATS * LONGS * 6);
    /* level 0 = the old mesh (src/vulkan_scene.c before 2026.11.07.01) */
    int sv = 0;
    for (int i = 0; i <= LATS; i++) {
        float lat = (float)(M_PI * i / LATS);
        for (int j = 0; j <= LONGS; j++) {
            float lon = (float)(2.0f * M_PI * j / LONGS);
            float px = sinf(lat) * cosf(lon), py = cosf(lat), pz = sinf(lat) * sinf(lon);
            TEST_CHECK(pos[sv * 3] == px && pos[sv * 3 + 1] == py && pos[sv * 3 + 2] == pz);
            sv++;
        }
    }
    int k = 0;
    for (int i = 0; i < LATS; i++)
        for (int j = 0; j < LONGS; j++) {
            int f = i * (LONGS + 1) + j, s = f + LONGS + 1;
            const uint32_t want[6] = { (uint32_t)f, (uint32_t)s, (uint32_t)(f + 1), (uint32_t)s, (uint32_t)(s + 1), (uint32_t)(f + 1) };
            for (int q = 0; q < 6; q++) TEST_CHECK(idx[k++] == want[q]);
        }
    uint32_t vbase = 0;
    for (int l = 0; l < SPHERE_LOD_LEVELS; l++) {
        uint32_t lv = (uint32_t)(((LATS >> l) + 1) * ((LONGS >> l) + 1));
        TEST_CHECK(count[l] == (uint32_t)((LATS >> l) * (LONGS >> l) * 6));
        if (l > 0) TEST_CHECK(first[l] == first[l - 1] + count[l - 1]);
        for (uint32_t q = first[l]; q < first[l] + count[l]; q++) TEST_CHECK(idx[q] >= vbase && idx[q] < vbase + lv);
        vbase += lv;
    }
    TEST_CHECK(vbase == nv);
    for (size_t v = 0; v < nv; v++) {
        float len = sqrtf(pos[v * 3] * pos[v * 3] + pos[v * 3 + 1] * pos[v * 3 + 1] + pos[v * 3 + 2] * pos[v * 3 + 2]);
        TEST_CHECK(fabsf(len - 1.0f) < 1e-5f);
    }
    printf("  [1] mesh: 4 levels, %zu vertices, %zu indices; level 0 identical to the old sphere: OK\n", nv, ni);

    /* 2 + 3. the choice against the error it promises: radius r_px on
     * screen = r * ppu / z; place the sphere at z = 100, ppu = 1000 */
    const float ppu = 1000.0f, z = 100.0f;
    int seen[2][SPHERE_LOD_LEVELS] = { { 0 } };
    for (int wire = 0; wire <= 1; wire++)
        for (float r_px = 0.5f; r_px <= 2000.0f; r_px *= 1.03f) {
            float r = r_px * z / ppu;
            int l = pick_at(r, 0.0f, 0.0f, -z, ppu, wire != 0);
            TEST_CHECK(l >= 0 && l < SPHERE_LOD_LEVELS);
            seen[wire][l]++;
            float steps = (float)(LONGS >> l);
            float worst = r_px * SPHERE_LOD_MARGIN;   /* the stretched size the margin allows for */
            if (wire)
                TEST_CHECK(l == 0 || 2.0f * (float)M_PI * worst / steps <= SPHERE_LOD_WIRE_SPACING_PX + 1e-4f);
            else
                TEST_CHECK(l == 0 || worst * (1.0f - cosf((float)M_PI / steps)) <= SPHERE_LOD_MAX_ERR_PX + 1e-4f);
            if (l < SPHERE_LOD_LEVELS - 1) {   /* the next coarser level was not good enough */
                float coarse = (float)(LONGS >> (l + 1));
                float err = wire ? 2.0f * (float)M_PI * worst / coarse : worst * (1.0f - cosf((float)M_PI / coarse));
                TEST_CHECK(err > (wire ? SPHERE_LOD_WIRE_SPACING_PX : SPHERE_LOD_MAX_ERR_PX) - 1e-4f);
            }
        }
    for (int l = 0; l < SPHERE_LOD_LEVELS; l++) TEST_CHECK(seen[0][l] > 0 && seen[1][l] > 0);
    TEST_CHECK(pick_at(334.0f * z / ppu, 0, 0, -z, ppu, false) == 0);
    TEST_CHECK(pick_at(332.0f * z / ppu, 0, 0, -z, ppu, false) == 1);
    TEST_CHECK(pick_at(20.0f * z / ppu, 0, 0, -z, ppu, false) == 3);
    TEST_CHECK(pick_at(9.0f * z / ppu, 0, 0, -z, ppu, true) == 0);   /* a starbase core near the camera */
    TEST_CHECK(pick_at(7.0f * z / ppu, 0, 0, -z, ppu, true) == 1);   /* ... and at the usual distance */
    TEST_CHECK(pick_at(1.9f * z / ppu, 0, 0, -z, ppu, true) == 3);
    printf("  [2] filled: silhouette within %.1f px at every size (8x16 up to %.0f px, 16x32 up to %.0f, 32x64 up "
           "to %.0f): OK\n",
           (double)SPHERE_LOD_MAX_ERR_PX, (double)sphere_lod_limit_px(3, LONGS, false),
           (double)sphere_lod_limit_px(2, LONGS, false), (double)sphere_lod_limit_px(1, LONGS, false));
    printf("  [3] wireframe: lines at most %.1f px apart (8x16 up to %.1f px, 16x32 up to %.1f, 32x64 up to %.1f): "
           "OK\n",
           (double)SPHERE_LOD_WIRE_SPACING_PX, (double)sphere_lod_limit_px(3, LONGS, true),
           (double)sphere_lod_limit_px(2, LONGS, true), (double)sphere_lod_limit_px(1, LONGS, true));

    /* 4. edge cases */
    TEST_CHECK(pick_at(1.0f, 0, 0, 0, ppu, false) == 0);        /* camera inside */
    TEST_CHECK(pick_at(1.0f, 0, 0, 50.0f, ppu, false) == 0);    /* behind */
    TEST_CHECK(pick_at(1.0f, 0, 0, -1.9f, ppu, false) == 0);    /* nearer than 2 r */
    TEST_CHECK(pick_at(0.0f, 0, 0, -50.0f, ppu, false) == 0);   /* zero scale */
    TEST_CHECK(pick_at(0.01f, 0, 0, -50.0f, 0.0f, false) == 0); /* no projection yet */
    {   /* non-uniform: the largest axis counts (a flat disc 3.5 x 0.02 x 3.5) */
        float model[4][4], view[4][4];
        identity(model);
        identity(view);
        model[0][0] = 3.5f;
        model[1][1] = 0.02f;
        model[2][2] = 3.5f;
        model[3][2] = -100.0f;
        int l_disc = sphere_lod_pick((const float(*)[4])model, (const float(*)[4])view, ppu, false, LONGS);
        TEST_CHECK(l_disc == pick_at(3.5f, 0, 0, -100.0f, ppu, false));
        TEST_CHECK(l_disc != pick_at(0.02f, 0, 0, -100.0f, ppu, false));
        /* rotated 90 degrees about Y: the same radius */
        float rot[4][4];
        identity(rot);
        rot[0][0] = 0.0f;
        rot[0][2] = -3.5f;
        rot[1][1] = 0.02f;
        rot[2][0] = 3.5f;
        rot[2][2] = 0.0f;
        rot[3][2] = -100.0f;
        TEST_CHECK(sphere_lod_pick((const float(*)[4])rot, (const float(*)[4])view, ppu, false, LONGS) == l_disc);
        /* a camera moved back by 100 (view translation +100 on z... the
         * sphere at z = 0 seen from z = +100) gives the same depth */
        float m0[4][4];
        identity(m0);
        m0[0][0] = m0[1][1] = m0[2][2] = 3.5f;
        view[3][2] = -100.0f;
        TEST_CHECK(sphere_lod_pick((const float(*)[4])m0, (const float(*)[4])view, ppu, false, LONGS) == l_disc);
        /* off the view axis: the depth, not the distance */
        TEST_CHECK(pick_at(3.5f, 60.0f, 0.0f, -100.0f, ppu, false) == l_disc);
    }
    printf("  [4] near/behind/zero/no projection: full mesh; largest axis, depth along the view axis: OK\n");

    free(pos);
    free(idx);
    printf("PASS: sphere_lod (%d checks)\n", g_checks);
    return 0;
}
