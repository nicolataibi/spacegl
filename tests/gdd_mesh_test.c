/*
 * SPACE GL - GPU-DRIVEN RENDERING (GDD) - headless Vulkan compute test.
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Verifies the GDD compute passes (the GPU side of the architecture)
 * against a CPU reference, in three isolated stages, like the client:
 *
 *   Stage CULL   : gdd_cull.comp on the dynamic and the map group. The
 *                  visible-index lists and counters are read back and
 *                  compared (as sets) with the CPU cull reference:
 *                  vision-radius sphere cull, GDD_FLAG_NEVER_CULL
 *                  instances always kept, boundary (dist == radius)
 *                  kept (strict >).
 *   Stage EXPAND : gdd_expand.comp consuming the stage-1 lists. The
 *                  generated vertex SSBO is read back: every expected
 *                  primitive block must be found intact inside it
 *                  (identity-orientation blocks bit-exact — this file
 *                  is compiled with -ffp-contract=off to match the
 *                  non-FMA shader; transcendentals within 1e-4), and
 *                  the opaque/additive counters must carry the exact
 *                  per-pass totals.
 *   Stage FINAL  : gdd_final.comp publishes the two
 *                  VkDrawIndirectCommands: they must match the
 *                  counters exactly.
 *   Stage GUARD  : the whole chain re-run with a reduced vertex
 *                  capacity: totals must stay <= capacity (drop when
 *                  full, same policy as the legacy CPU path) and the
 *                  final indirect must remain consistent.
 *
 * Usage:  gdd_mesh_test [spv_dir]      (default: ./build/shaders)
 * Exit:   0 = pass, 1 = fail, 77 = skip (no Vulkan ICD / missing .spv).
 *
 * The CPU reference geometry intentionally mirrors gdd_ops.glsl — if
 * those change, update this file too (the contract struct comes from
 * include/spacegl_gdd.h, the same one the production code uses).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <vulkan/vulkan.h>
#include "spacegl_gdd.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Test data sizes (declared first: the buffer setup below uses them) */
#define N_DYN  10
#define N_MAP  4
#define VMAX   2048

/* ------------------------------------------------------------------ */
/* CPU mirrors of the GLSL expand ops (gdd_ops.glsl)                   */
/* ------------------------------------------------------------------ */

static const int BOX_SIGNS[36][3] = {
    /* face +Y */ {-1, 1, -1}, { 1, 1, -1}, { 1, 1, 1}, {-1, 1, -1}, { 1, 1,  1}, {-1, 1,  1},
    /* face -Y */ { 1,-1, -1}, {-1,-1, -1}, {-1,-1, 1}, { 1,-1, -1}, {-1,-1,  1}, { 1,-1,  1},
    /* face +Z */ {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1,-1, 1}, { 1, 1, 1}, {-1, 1,  1},
    /* face -Z */ { 1,-1,-1}, {-1,-1,-1}, {-1, 1,-1}, { 1,-1,-1}, {-1, 1,-1}, { 1, 1,-1},
    /* face -X */ {-1,-1, 1}, {-1,-1,-1}, {-1, 1,-1}, {-1,-1, 1}, {-1, 1,-1}, {-1, 1,  1},
    /* face +X */ { 1,-1,-1}, { 1,-1, 1}, { 1, 1, 1}, { 1,-1,-1}, { 1, 1, 1}, { 1, 1,-1}
};
static const float BOX_FACE_N[6][3] = {
    { 0,  1,  0}, { 0, -1,  0},
    { 0,  0,  1}, { 0,  0, -1},
    {-1,  0,  0}, { 1,  0,  0}
};

/* world = pos + (M * (lp * scale)), engine row-vector convention:
 * (v*M)[r] = sum_c v[c] * M[c][r], with M[c][r] stored at
 * orient[4*c + r] (GLSL O holds the rows of M as its columns). */
static void m_xform(const GddInstance *it, const float lp[3], float out[3]) {
    float s[3] = { lp[0] * it->scale[0], lp[1] * it->scale[1], lp[2] * it->scale[2] };
    for (int r = 0; r < 3; r++) {
        out[r] = it->pos[r]
               + s[0] * it->orient[0 + r]
               + s[1] * it->orient[4 + r]
               + s[2] * it->orient[8 + r];
    }
}

/* normal = M * n (no translation, no scale) */
static void m_xform_n(const GddInstance *it, const float n[3], float out[3]) {
    for (int r = 0; r < 3; r++) {
        out[r] = n[0] * it->orient[0 + r]
               + n[1] * it->orient[4 + r]
               + n[2] * it->orient[8 + r];
    }
}

/* Fill one expected vertex (mirror of gdd_put). */
static void put_exp(GddVertex *v, const float wpos[3], const float lpos[3],
                    const float nrm[3], const GddInstance *it) {
    memset(v, 0, sizeof(*v));
    v->pos[0] = wpos[0]; v->pos[1] = wpos[1]; v->pos[2] = wpos[2];
    v->color[0] = it->color[0]; v->color[1] = it->color[1];
    v->color[2] = it->color[2]; v->color[3] = it->alpha;
    v->normal[0] = nrm[0]; v->normal[1] = nrm[1]; v->normal[2] = nrm[2];
    v->mode = (float)gdd_frag_mode(it->flags);
    v->local[0] = lpos[0]; v->local[1] = lpos[1]; v->local[2] = lpos[2];
    v->metallic = it->pad[0]; v->roughness = it->pad[1];
}

static uint32_t expand_box(const GddInstance *it, GddVertex *out) {
    for (int i = 0; i < 36; i++) {
        float unscaled_lp[3] = { BOX_SIGNS[i][0], BOX_SIGNS[i][1], BOX_SIGNS[i][2] };
        float lp[3] = { unscaled_lp[0] * it->scale[0],
                        unscaled_lp[1] * it->scale[1],
                        unscaled_lp[2] * it->scale[2] };
        float w[3], n[3];
        m_xform(it, unscaled_lp, w);
        m_xform_n(it, BOX_FACE_N[i / 6], n);
        put_exp(&out[i], w, lp, n, it);
    }
    return 36;
}

static uint32_t expand_sphere(const GddInstance *it, GddVertex *out) {
    const float PI = 3.141592653589793f;
    const float r = it->scale[0];
    int i, j;
    for (i = 0; i < 6; i++) {
        float lat0 = PI * (float)i / 6.0f;
        float lat1 = PI * (float)(i + 1) / 6.0f;
        float cl0 = cosf(lat0), sl0 = sinf(lat0);
        float cl1 = cosf(lat1), sl1 = sinf(lat1);
        for (j = 0; j < 10; j++) {
            float lon0 = 2.0f * PI * (float)j / 10.0f;
            float lon1 = 2.0f * PI * (float)(j + 1) / 10.0f;
            float c0 = cosf(lon0), s0 = sinf(lon0);
            float c1 = cosf(lon1), s1 = sinf(lon1);
            float d00[3] = { cl0 * c0, sl0, cl0 * s0 };
            float d10[3] = { cl1 * c0, sl1, cl1 * s0 };
            float d01[3] = { cl0 * c1, sl0, cl0 * s1 };
            float d11[3] = { cl1 * c1, sl1, cl1 * s1 };
            const float *d[6] = { d00, d10, d01, d10, d11, d01 };
            for (int k = 0; k < 6; k++) {
                float w[3] = { it->pos[0] + d[k][0] * r,
                               it->pos[1] + d[k][1] * r,
                               it->pos[2] + d[k][2] * r };
                float lp[3] = { d[k][0] * r, d[k][1] * r, d[k][2] * r };
                put_exp(&out[i * 60 + j * 6 + k], w, lp, d[k], it);
            }
        }
    }
    return 360;
}

static uint32_t expand_pyramid(const GddInstance *it, GddVertex *out) {
    const float c0[3] = {-0.5f, -0.5f, -0.5f};
    const float c1[3] = {-0.5f,  0.5f, -0.5f};
    const float c2[3] = {-0.5f,  0.5f,  0.5f};
    const float c3[3] = {-0.5f, -0.5f,  0.5f};
    const float ap[3] = { 0.5f,  0.0f,  0.0f};
    const float *f[18] = {
        c0, c3, c2, c0, c2, c1,
        ap, c0, c1, ap, c1, c2, ap, c2, c3, ap, c3, c0
    };
    const float n[18][3] = {
        {-1,0,0}, {-1,0,0}, {-1,0,0}, {-1,0,0}, {-1,0,0}, {-1,0,0},
        {0,0,-1}, {0,0,-1}, {0,0,-1}, {0,1,0}, {0,1,0}, {0,1,0},
        {0,0,1}, {0,0,1}, {0,0,1}, {0,-1,0}, {0,-1,0}, {0,-1,0}
    };
    for (int i = 0; i < 18; i++) {
        float w[3], wn[3];
        m_xform(it, f[i], w);
        m_xform_n(it, n[i], wn);
        put_exp(&out[i], w, f[i], wn, it);
    }
    return 18;
}

static uint32_t expand_octa(const GddInstance *it, GddVertex *out) {
    const float v0[3] = { 1,0,0}, v1[3] = {-1,0,0}, v2[3] = {0, 1,0},
              v3[3] = {0,-1,0}, v4[3] = {0,0, 1}, v5[3] = {0,0,-1};
    const float nr = 0.5773502691896258f; /* 1/sqrt(3) */
    const float n[8][3] = {
        { 1, 1, 1}, { 1,-1, 1}, { 1, 1,-1}, { 1,-1,-1},
        {-1, 1, 1}, {-1,-1, 1}, {-1, 1,-1}, {-1,-1,-1}
    };
    const float *f[24] = {
        v0, v2, v4, v2, v5, v0, v0, v4, v3, v0, v3, v5,
        v1, v4, v2, v1, v5, v4, v1, v2, v3, v1, v3, v5
    };
    for (int i = 0; i < 24; i++) {
        float lp[3] = { f[i][0] * it->scale[0], f[i][1] * it->scale[1],
                        f[i][2] * it->scale[2] };
        float w[3], nn[3];
        m_xform(it, f[i], w);
        float nrm[3] = { n[i / 3][0] * nr, n[i / 3][1] * nr, n[i / 3][2] * nr };
        m_xform_n(it, nrm, nn);
        put_exp(&out[i], w, lp, nn, it);
    }
    return 24;
}

static uint32_t expand_ring(const GddInstance *it, GddVertex *out) {
    for (int j = 0; j < 16; j++) {
        float a0 = 6.283185307179586f * (float)j / 16.0f;
        float a1 = 6.283185307179586f * (float)(j + 1) / 16.0f;
        float c0 = cosf(a0), s0 = sinf(a0);
        float c1 = cosf(a1), s1 = sinf(a1);
        float i0[3] = { c0 * 0.75f, 0.0f, s0 * 0.75f };
        float i1[3] = { c1 * 0.75f, 0.0f, s1 * 0.75f };
        float o0[3] = { c0, 0.0f, s0 };
        float o1[3] = { c1, 0.0f, s1 };
        const float *lp[6] = { i0, i1, o1, i0, o1, o0 };
        const float n[3] = { 0, 1, 0 };
        for (int k = 0; k < 6; k++) {
            float w[3], nn[3];
            m_xform(it, lp[k], w);
            m_xform_n(it, n, nn);
            /* CORRETTO: usa uniformemente it->scale[0] (equivalente a it.b.x della GPU) */
            float lsc[3] = { lp[k][0] * it->scale[0], lp[k][1] * it->scale[0],
                             lp[k][2] * it->scale[0] };
            put_exp(&out[j * 6 + k], w, lsc, nn, it);
        }
    }
    return 96;
}

static uint32_t expand_line(const GddInstance *it, GddVertex *out) {
    float e[3] = { it->scale[0], it->scale[1], it->scale[2] };
    float len = sqrtf(e[0]*e[0] + e[1]*e[1] + e[2]*e[2]);
    float d[3];
    if (len > 1e-6f) {
        d[0] = e[0]/len;
        d[1] = e[1]/len;
        d[2] = e[2]/len;
    } else {
        d[0] = 1;
        d[1] = 0;
        d[2] = 0;
    }
    float ref[3];
    if (fabsf(d[0]) > 0.9f) {
        ref[0] = 0;
        ref[1] = 1;
        ref[2] = 0;
    } else {
        ref[0] = 1;
        ref[1] = 0;
        ref[2] = 0;
    }
    /* p1 = normalize(cross(d, ref)) */
    float p1[3] = {
        d[1]*ref[2] - d[2]*ref[1],
        d[2]*ref[0] - d[0]*ref[2],
        d[0]*ref[1] - d[1]*ref[0]
    };
    float pl = sqrtf(p1[0]*p1[0] + p1[1]*p1[1] + p1[2]*p1[2]);
    p1[0] /= pl;
    p1[1] /= pl;
    p1[2] /= pl;
    /* p2 = normalize(cross(d, p1)) */
    float p2[3] = {
        d[1]*p1[2] - d[2]*p1[1],
        d[2]*p1[0] - d[0]*p1[2],
        d[0]*p1[1] - d[1]*p1[0]
    };
    float p2l = sqrtf(p2[0]*p2[0] + p2[1]*p2[1] + p2[2]*p2[2]);
    p2[0] /= p2l;
    p2[1] /= p2l;
    p2[2] /= p2l;
    float t = (it->pad[0] > 1e-4f) ? it->pad[0] : 0.02f; /* metallic = half-thickness */
    float s[3] = { it->pos[0], it->pos[1], it->pos[2] };
    float f[3] = { it->pos[0] + e[0], it->pos[1] + e[1], it->pos[2] + e[2] };
    /* 4 tube corners */
    float sA[3] = { s[0]+p1[0]*t+p2[0]*t, s[1]+p1[1]*t+p2[1]*t, s[2]+p1[2]*t+p2[2]*t };
    float sB[3] = { s[0]-p1[0]*t+p2[0]*t, s[1]-p1[1]*t+p2[1]*t, s[2]-p1[2]*t+p2[2]*t };
    float sC[3] = { s[0]-p1[0]*t-p2[0]*t, s[1]-p1[1]*t-p2[1]*t, s[2]-p1[2]*t-p2[2]*t };
    float sD[3] = { s[0]+p1[0]*t-p2[0]*t, s[1]+p1[1]*t-p2[1]*t, s[2]+p1[2]*t-p2[2]*t };
    float fA[3] = { f[0]+p1[0]*t+p2[0]*t, f[1]+p1[1]*t+p2[1]*t, f[2]+p1[2]*t+p2[2]*t };
    float fB[3] = { f[0]-p1[0]*t+p2[0]*t, f[1]-p1[1]*t+p2[1]*t, f[2]-p1[2]*t+p2[2]*t };
    float fC[3] = { f[0]-p1[0]*t-p2[0]*t, f[1]-p1[1]*t-p2[1]*t, f[2]-p1[2]*t-p2[2]*t };
    float fD[3] = { f[0]+p1[0]*t-p2[0]*t, f[1]+p1[1]*t-p2[1]*t, f[2]+p1[2]*t-p2[2]*t };
    float np1[3] = { p1[0], p1[1], p1[2] };
    float nm1[3] = {-p1[0],-p1[1],-p1[2] };
    float np2[3] = { p2[0], p2[1], p2[2] };
    float nm2[3] = {-p2[0],-p2[1],-p2[2] };
    /* Face +p1 */
    const float *fp1[6] = { sA, sD, fD, sA, fD, fA };
    for (int k = 0; k < 6; k++) put_exp(&out[ 0+k], fp1[k], fp1[k], np1, it);
    /* Face -p1 */
    const float *fm1[6] = { sB, sC, fC, sB, fC, fB };
    for (int k = 0; k < 6; k++) put_exp(&out[ 6+k], fm1[k], fm1[k], nm1, it);
    /* Face +p2 */
    const float *fp2[6] = { sA, sB, fB, sA, fB, fA };
    for (int k = 0; k < 6; k++) put_exp(&out[12+k], fp2[k], fp2[k], np2, it);
    /* Face -p2 */
    const float *fm2[6] = { sC, sD, fD, sC, fD, fC };
    for (int k = 0; k < 6; k++) put_exp(&out[18+k], fm2[k], fm2[k], nm2, it);
    return 24;
}

static uint32_t expand_point(const GddInstance *it, GddVertex *out) {
    float r = it->scale[0] > 1e-4f ? it->scale[0] : 1e-4f;
    float p[3] = { it->pos[0], it->pos[1], it->pos[2] };
    float n[3], u[3], v[3];
    if (fabsf(p[0]) >= fabsf(p[1]) && fabsf(p[0]) >= fabsf(p[2])) {
        n[0]=1; n[1]=0; n[2]=0; u[0]=0; u[1]=1; u[2]=0; v[0]=0; v[1]=0; v[2]=1;
    } else if (fabsf(p[1]) >= fabsf(p[2])) {
        n[0]=0; n[1]=1; n[2]=0; u[0]=1; u[1]=0; u[2]=0; v[0]=0; v[1]=0; v[2]=1;
    } else {
        n[0]=0; n[1]=0; n[2]=1; u[0]=1; u[1]=0; u[2]=0; v[0]=0; v[1]=1; v[2]=0;
    }
    float a0[3] = { p[0] + u[0]*r, p[1] + u[1]*r, p[2] + u[2]*r };
    float a1[3] = { p[0] - u[0]*(r*0.8660254f) - v[0]*(r*0.5f),
                    p[1] - u[1]*(r*0.8660254f) - v[1]*(r*0.5f),
                    p[2] - u[2]*(r*0.8660254f) - v[2]*(r*0.5f) };
    float a2[3] = { p[0] - u[0]*(r*0.8660254f) + v[0]*(r*0.5f),
                    p[1] - u[1]*(r*0.8660254f) + v[1]*(r*0.5f),
                    p[2] - u[2]*(r*0.8660254f) + v[2]*(r*0.5f) };
    const float *vv[6] = { a0, a1, a2, a0, a2, a1 };
    for (int k = 0; k < 6; k++)
        put_exp(&out[k], vv[k], vv[k], n, it);
    return 6;
}

static uint32_t expand_mesh(const GddInstance *it, GddVertex *out) {
    switch ((int)(it->mesh + 0.5f)) {
    case GDD_MESH_POINT:   return expand_point(it, out);
    case GDD_MESH_SPHERE:  return expand_sphere(it, out);
    case GDD_MESH_BOX:     return expand_box(it, out);
    case GDD_MESH_PYRAMID: return expand_pyramid(it, out);
    case GDD_MESH_OCTA:    return expand_octa(it, out);
    case GDD_MESH_RING:    return expand_ring(it, out);
    case GDD_MESH_LINE:    return expand_line(it, out);
    default:               return expand_box(it, out);
    }
}

/* ------------------------------------------------------------------ */
/* Check infrastructure                                                */
/* ------------------------------------------------------------------ */

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
           fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } \
} while (0)

/* Field-level mismatch report: returns true when the vertices agree
 * (within tol for the geometric fields, exact for the rest) and records
 * which field failed otherwise (for the diagnostics in find_block). */
static int g_fail_field = -1;
static const char *g_fail_field_name = "";
static float g_fail_exp = 0.0f, g_fail_act = 0.0f;

static bool vtx_close(const GddVertex *a, const GddVertex *b, float tol) {
    g_fail_field = -1;
    for (int i = 0; i < 3; i++) {
        if (fabsf(a->pos[i] - b->pos[i]) > tol) {
            g_fail_field = i; g_fail_field_name = "pos"; g_fail_exp = a->pos[i]; g_fail_act = b->pos[i];
            return false;
        }
        if (fabsf(a->normal[i] - b->normal[i]) > tol) {
            g_fail_field = i; g_fail_field_name = "normal"; g_fail_exp = a->normal[i]; g_fail_act = b->normal[i];
            return false;
        }
        if (fabsf(a->local[i] - b->local[i]) > tol) {
            g_fail_field = i; g_fail_field_name = "local"; g_fail_exp = a->local[i]; g_fail_act = b->local[i];
            return false;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (a->color[i] != b->color[i]) {
            g_fail_field = i; g_fail_field_name = "color"; g_fail_exp = a->color[i]; g_fail_act = b->color[i];
            return false;
        }
    }
    if (a->mode != b->mode) {
        g_fail_field = -2; g_fail_field_name = "mode"; g_fail_exp = a->mode; g_fail_act = b->mode;
        return false;
    }
    if (a->metallic != b->metallic) {
        g_fail_field = -3; g_fail_field_name = "metallic"; g_fail_exp = a->metallic; g_fail_act = b->metallic;
        return false;
    }
    if (a->roughness != b->roughness) {
        g_fail_field = -4; g_fail_field_name = "roughness"; g_fail_exp = a->roughness; g_fail_act = b->roughness;
        return false;
    }
    return true;
}

static void dump_vertex(const char *tag, const GddVertex *v) {
    fprintf(stderr, "  %s pos: %.9g %.9g %.9g | nrm: %.9g %.9g %.9g (mode %.0f) | "
                    "loc: %.9g %.9g %.9g | col: %.9g %.9g %.9g %.9g | mat: %.9g %.9g\n",
            tag,
            (double)v->pos[0], (double)v->pos[1], (double)v->pos[2],
            (double)v->normal[0], (double)v->normal[1], (double)v->normal[2], (double)v->mode,
            (double)v->local[0], (double)v->local[1], (double)v->local[2],
            (double)v->color[0], (double)v->color[1], (double)v->color[2], (double)v->color[3],
            (double)v->metallic, (double)v->roughness);
}

static const float TOL_EXACT = 0.0f;   /* identity orientation, no transcendentals */
static const float TOL_TRIG = 1e-4f;   /* sin/cos ulp drift */

/* Best-leading-match diagnostics for a failed find_block (offset of the
 * candidate block whose first vertices matched the most, and how many
 * matched before the first failure). */
static uint32_t max_matched = 0;
static uint32_t best_match = 0;
static int g_best_field = -1;
static const char *g_best_field_name = "";
static float g_best_exp = 0.0f, g_best_act = 0.0f;

/* Find the expected block inside the readback; mark used slots. */
static uint32_t find_block(const GddVertex *buf, uint32_t total, uint32_t *used,
                           const GddVertex *exp, uint32_t n, float tol) {
    max_matched = 0;
    best_match = 0;
    for (uint32_t i = 0; i + n <= total; i++) {
        if (used[i]) continue;
        bool ok = true;
        uint32_t run = 0;
        for (uint32_t k = 0; k < n; k++) {
            if (!vtx_close(&buf[i + k], &exp[k], tol)) { ok = false; break; }
            run++;
        }
        if (run > max_matched) {
            max_matched = run; best_match = i;
            g_best_field = g_fail_field; g_best_field_name = g_fail_field_name;
            g_best_exp = g_fail_exp; g_best_act = g_fail_act;
        }
        if (ok) {
            for (uint32_t k = 0; k < n; k++) used[i + k] = 1;
            return i;
        }
    }
    if (max_matched > 0) {
        fprintf(stderr, "Best match at %u: %u/%u vertices matched.\n", best_match, max_matched, n);
        fprintf(stderr, "Failed at vertex %u (%s[%d]: exp %.9g act %.9g):\n",
                max_matched, g_best_field_name, g_best_field,
                (double)g_best_exp, (double)g_best_act);
        dump_vertex("EXP", &exp[max_matched]);
        dump_vertex("ACT", &buf[best_match + max_matched]);
    } else if (n > 0) {
        fprintf(stderr, "No leading match anywhere; first expected vertex:\n");
        dump_vertex("EXP", &exp[0]);
    }
    return UINT32_MAX;
}

/* ------------------------------------------------------------------ */
/* Vulkan scaffolding (headless: no surface, compute queue only)       */
/* ------------------------------------------------------------------ */

static VkInstance g_inst;
static VkPhysicalDevice g_phys;
static VkDevice g_dev;
static VkQueue g_queue;
static VkCommandPool g_pool;
static VkDescriptorPool g_pool_desc;

static VkDescriptorSetLayout g_ds_cull, g_ds_exp, g_ds_final;
static VkPipelineLayout g_pl_cull, g_pl_exp, g_pl_final;
static VkPipeline g_pipe_cull, g_pipe_exp, g_pipe_final;

static char g_spv_dir[512] = "./build/shaders";

static bool check(VkResult r, const char *what) {
    if (r != VK_SUCCESS) {
        fprintf(stderr, "  [VK] %s failed: %d\n", what, (int)r);
        return false;
    }
    return true;
}

static bool make_ds_layout(uint32_t nb, const uint32_t *bind,
                           VkDescriptorSetLayout *out) {
    VkDescriptorSetLayoutBinding *b = malloc(sizeof(*b) * nb);
    VkDescriptorSetLayoutCreateInfo ci = {0};
    for (uint32_t i = 0; i < nb; i++) {
        b[i].binding = bind[i];
        b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = nb;
    ci.pBindings = b;
    bool ok = check(vkCreateDescriptorSetLayout(g_dev, &ci, NULL, out), "ds layout");
    free(b);
    return ok;
}

static bool make_compute_pipeline(const char *name, VkPipelineLayout layout,
                                  VkPipeline *out) {
    char path[600];
    snprintf(path, sizeof(path), "%s/%s", g_spv_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "  [VK] cannot open %s\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint32_t *code = malloc((size_t)sz);
    if (fread(code, 1, (size_t)sz, f) != (size_t)sz) {
        free(code); fclose(f);
        return false;
    }
    fclose(f);
    VkShaderModule mod;
    VkShaderModuleCreateInfo sm = {0};
    sm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    sm.codeSize = (size_t)sz;
    sm.pCode = code;
    bool ok = check(vkCreateShaderModule(g_dev, &sm, NULL, &mod), "shader module");
    free(code);
    if (!ok) return false;

    VkPipelineShaderStageCreateInfo st = {0};
    st.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    st.module = mod;
    st.pName = "main";
    VkComputePipelineCreateInfo ci = {0};
    ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage = st;
    ci.layout = layout;
    ok = check(vkCreateComputePipelines(g_dev, VK_NULL_HANDLE, 1, &ci, NULL, out),
               "compute pipeline");
    vkDestroyShaderModule(g_dev, mod, NULL);
    return ok;
}

static bool init_vulkan(void) {
    uint32_t pdn = 0, i;
    VkPhysicalDevice *pds;
    uint32_t fam = UINT32_MAX;
    int dev = -1;

    VkApplicationInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "spacegl_gdd_mesh_test";
    ai.apiVersion = VK_API_VERSION_1_2; /* only compute is needed here */
    VkInstanceCreateInfo ici = {0};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;
    if (vkCreateInstance(&ici, NULL, &g_inst) != VK_SUCCESS) return false;
    if (vkEnumeratePhysicalDevices(g_inst, &pdn, NULL) != VK_SUCCESS || pdn == 0)
        return false;
    pds = malloc(sizeof(VkPhysicalDevice) * pdn);
    vkEnumeratePhysicalDevices(g_inst, &pdn, pds);
    /* Same selection rule as the client's gdd_pick_queue: first family
     * with BOTH graphics and compute bits. */
    for (i = 0; i < pdn && dev < 0; i++) {
        uint32_t qfn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &qfn, NULL);
        VkQueueFamilyProperties *qf = malloc(sizeof(*qf) * qfn);
        vkGetPhysicalDeviceQueueFamilyProperties(pds[i], &qfn, qf);
        for (uint32_t k = 0; k < qfn; k++) {
            if ((qf[k].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
                dev = (int)i;
                fam = k;
                break;
            }
        }
        free(qf);
    }
    if (dev < 0) {
        free(pds);
        return false;
    }
    g_phys = pds[dev];
    free(pds);

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qc = {0};
    qc.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qc.queueFamilyIndex = fam;
    qc.queueCount = 1;
    qc.pQueuePriorities = &prio;
    VkDeviceCreateInfo dc = {0};
    dc.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dc.queueCreateInfoCount = 1;
    dc.pQueueCreateInfos = &qc;
    if (vkCreateDevice(g_phys, &dc, NULL, &g_dev) != VK_SUCCESS) return false;
    vkGetDeviceQueue(g_dev, fam, 0, &g_queue);

    VkCommandPoolCreateInfo pc = {0};
    pc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pc.queueFamilyIndex = fam;
    if (!check(vkCreateCommandPool(g_dev, &pc, NULL, &g_pool), "command pool"))
        return false;

    /* Layouts mirror the production ones (spacegl_vulkan_gdd.c):
     * cull {0 inst, 1 vis, 2 cnt}, expand {0 inst, 1 vis, 2 cnt, 3 verts},
     * final {0 indirect, 1 cnt}; all with the 32 B compute push constants. */
    static const uint32_t b3[3] = {0, 1, 2};
    static const uint32_t b4[4] = {0, 1, 2, 3};
    static const uint32_t b2[2] = {0, 1};
    if (!make_ds_layout(3, b3, &g_ds_cull)) return false;
    if (!make_ds_layout(4, b4, &g_ds_exp)) return false;
    if (!make_ds_layout(2, b2, &g_ds_final)) return false;

    VkDescriptorSetLayout sls[3] = { g_ds_cull, g_ds_exp, g_ds_final };
    VkPipelineLayout *pls[3] = { &g_pl_cull, &g_pl_exp, &g_pl_final };
    for (int p = 0; p < 3; p++) {
        VkPushConstantRange pcr = {0};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcr.offset = 0;
        pcr.size = GDD_PC_STRIDE;
        VkPipelineLayoutCreateInfo ci = {0};
        ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        ci.setLayoutCount = 1;
        ci.pSetLayouts = &sls[p];
        ci.pushConstantRangeCount = 1;
        ci.pPushConstantRanges = &pcr;
        if (!check(vkCreatePipelineLayout(g_dev, &ci, NULL, pls[p]), "pipeline layout"))
            return false;
    }

    if (!make_compute_pipeline("gdd_cull.comp.spv", g_pl_cull, &g_pipe_cull)) return false;
    if (!make_compute_pipeline("gdd_expand.comp.spv", g_pl_exp, &g_pipe_exp)) return false;
    if (!make_compute_pipeline("gdd_final.comp.spv", g_pl_final, &g_pipe_final)) return false;

    VkDescriptorPoolSize ps = {0};
    ps.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps.descriptorCount = 3 * 3 + 2 * 4 + 2 * 2 + 8; /* slack */
    VkDescriptorPoolCreateInfo dpc = {0};
    dpc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpc.maxSets = 8;
    dpc.poolSizeCount = 1;
    dpc.pPoolSizes = &ps;
    return check(vkCreateDescriptorPool(g_dev, &dpc, NULL, &g_pool_desc), "desc pool");
}

/* All test buffers are host-visible persistently mapped (readback-first). */
static bool make_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                        VkBuffer *buf, VkDeviceMemory *mem, void **host) {
    VkBufferCreateInfo bc = {0};
    bc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bc.size = size;
    bc.usage = usage;
    if (!check(vkCreateBuffer(g_dev, &bc, NULL, buf), "buffer")) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_dev, *buf, &req);
    uint32_t type = 0, found = 0;
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(g_phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((req.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
            type = i;
            found = 1;
            break;
        }
    }
    if (!found) return false;
    VkMemoryAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    if (!check(vkAllocateMemory(g_dev, &ai, NULL, mem), "alloc")) return false;
    if (!check(vkBindBufferMemory(g_dev, *buf, *mem, 0), "bind")) return false;
    if (host && !check(vkMapMemory(g_dev, *mem, 0, req.size, 0, host), "map"))
        return false;
    return true;
}

static VkBuffer b_inst_dyn, b_inst_map, b_vis_dyn, b_vis_map,
                b_verts, b_indirect, b_counts;
static VkDeviceMemory m_inst_dyn, m_inst_map, m_vis_dyn, m_vis_map,
                      m_verts, m_indirect, m_counts;

static GddInstance *u_inst_dyn, *u_inst_map;
static uint32_t *u_vis_dyn, *u_vis_map;
static GddVertex *u_verts;
static VkDrawIndirectCommand *u_indirect;
static GddCounts *u_counts;

static VkDescriptorSet set_cull_dyn, set_cull_map, set_exp_dyn, set_exp_map, set_final;

static bool create_buffers_and_sets(void) {
    VkBufferUsageFlags ssbo = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (!make_buffer(N_DYN * GDD_INSTANCE_STRIDE, ssbo, &b_inst_dyn, &m_inst_dyn, (void **)&u_inst_dyn)) return false;
    if (!make_buffer(N_MAP * GDD_INSTANCE_STRIDE, ssbo, &b_inst_map, &m_inst_map, (void **)&u_inst_map)) return false;
    if (!make_buffer(N_DYN * sizeof(uint32_t), ssbo, &b_vis_dyn, &m_vis_dyn, (void **)&u_vis_dyn)) return false;
    if (!make_buffer(N_MAP * sizeof(uint32_t), ssbo, &b_vis_map, &m_vis_map, (void **)&u_vis_map)) return false;
    if (!make_buffer(VMAX * GDD_VERTEX_STRIDE, ssbo, &b_verts, &m_verts, (void **)&u_verts)) return false;
    if (!make_buffer(2 * sizeof(VkDrawIndirectCommand),
                     ssbo | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                     &b_indirect, &m_indirect, (void **)&u_indirect)) return false;
    if (!make_buffer(GDD_COUNTS_STRIDE, ssbo, &b_counts, &m_counts, (void **)&u_counts)) return false;

    VkDescriptorSet sets[5];
    VkDescriptorSetLayout layouts[5] = { g_ds_cull, g_ds_cull, g_ds_exp, g_ds_exp, g_ds_final };
    VkDescriptorSetAllocateInfo ai = {0};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = g_pool_desc;
    ai.descriptorSetCount = 5;
    ai.pSetLayouts = layouts;
    if (!check(vkAllocateDescriptorSets(g_dev, &ai, sets), "alloc sets")) return false;
    set_cull_dyn = sets[0]; set_cull_map = sets[1]; set_exp_dyn = sets[2];
    set_exp_map = sets[3]; set_final = sets[4];

    VkDescriptorBufferInfo bi[4];
    bi[0].buffer = b_inst_dyn;  bi[0].offset = 0; bi[0].range = N_DYN * GDD_INSTANCE_STRIDE;
    bi[1].buffer = b_vis_dyn;   bi[1].offset = 0; bi[1].range = N_DYN * sizeof(uint32_t);
    bi[2].buffer = b_counts;    bi[2].offset = 0; bi[2].range = GDD_COUNTS_STRIDE;
    bi[3].buffer = b_verts;     bi[3].offset = 0; bi[3].range = VMAX * GDD_VERTEX_STRIDE;
    VkDescriptorBufferInfo bm[4];
    bm[0].buffer = b_inst_map;  bm[0].offset = 0; bm[0].range = N_MAP * GDD_INSTANCE_STRIDE;
    bm[1].buffer = b_vis_map;   bm[1].offset = 0; bm[1].range = N_MAP * sizeof(uint32_t);
    bm[2] = bi[2];
    bm[3] = bi[3];
    VkDescriptorBufferInfo bfin[2];
    bfin[0].buffer = b_indirect; bfin[0].offset = 0; bfin[0].range = 2 * sizeof(VkDrawIndirectCommand);
    bfin[1] = bi[2];

    VkWriteDescriptorSet w[24];
    int n = 0;
    for (int k = 0; k < 3; k++) {
        w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[n].dstSet = set_cull_dyn; w[n].dstBinding = k; w[n].dstArrayElement = 0;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[n].descriptorCount = 1; w[n].pBufferInfo = &bi[k]; n++;
    }
    for (int k = 0; k < 3; k++) {
        w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[n].dstSet = set_cull_map; w[n].dstBinding = k; w[n].dstArrayElement = 0;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[n].descriptorCount = 1; w[n].pBufferInfo = &bm[k]; n++;
    }
    for (int k = 0; k < 4; k++) {
        w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[n].dstSet = set_exp_dyn; w[n].dstBinding = k; w[n].dstArrayElement = 0;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[n].descriptorCount = 1; w[n].pBufferInfo = &bi[k]; n++;
    }
    for (int k = 0; k < 4; k++) {
        w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[n].dstSet = set_exp_map; w[n].dstBinding = k; w[n].dstArrayElement = 0;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[n].descriptorCount = 1; w[n].pBufferInfo = &bm[k]; n++;
    }
    for (int k = 0; k < 2; k++) {
        w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[n].dstSet = set_final; w[n].dstBinding = k; w[n].dstArrayElement = 0;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[n].descriptorCount = 1; w[n].pBufferInfo = &bfin[k]; n++;
    }
    /* vkUpdateDescriptorSets is void: a bad set write surfaces later as
     * wrong readback (caught by the checks), not as a VkResult. */
    vkUpdateDescriptorSets(g_dev, n, w, 0, NULL);
    return true;
}

/* One command buffer, submit, wait on a fence. */
static bool submit_wait(VkCommandBuffer cb) {
    VkSubmitInfo si = {0};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    VkFence fence;
    VkFenceCreateInfo fc = {0};
    fc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(g_dev, &fc, NULL, &fence) != VK_SUCCESS) return false;
    if (vkQueueSubmit(g_queue, 1, &si, fence) != VK_SUCCESS) {
        vkDestroyFence(g_dev, fence, NULL);
        return false;
    }
    bool ok = false;
    for (int tries = 0; tries < 5; tries++) {
        if (vkWaitForFences(g_dev, 1, &fence, VK_TRUE, 2000000000ULL) == VK_SUCCESS) {
            ok = true;
            break;
        }
    }
    vkDestroyFence(g_dev, fence, NULL);
    return ok;
}

static bool begin_cb(VkCommandBuffer *cb) {
    VkCommandBufferAllocateInfo cai = {0};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = g_pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    if (!check(vkAllocateCommandBuffers(g_dev, &cai, cb), "alloc cb")) return false;
    VkCommandBufferBeginInfo bi = {0};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    return check(vkBeginCommandBuffer(*cb, &bi), "begin cb");
}

/* ------------------------------------------------------------------ */
/* Test data                                                           */
/* ------------------------------------------------------------------ */

#define CULL_CX 0.0f
#define CULL_CY 0.0f
#define CULL_CZ 0.0f
#define CULL_R  10.0f

static void set_identity_orient(float o[12]) {
    memset(o, 0, 12 * sizeof(float));
    o[0] = o[5] = o[10] = 1.0f;
}

static void fill_test_data(void) {
    memset(u_inst_dyn, 0, N_DYN * sizeof(GddInstance));
    memset(u_inst_map, 0, N_MAP * sizeof(GddInstance));

    /* --- dynamic group -------------------------------------------- */
    /* 0: box, IN range, rotated 90 deg around Y (exact matrix) */
    u_inst_dyn[0].pos[0] = 1.0f; u_inst_dyn[0].pos[1] = 2.0f; u_inst_dyn[0].pos[2] = 3.0f;
    u_inst_dyn[0].mesh = GDD_MESH_BOX;
    u_inst_dyn[0].scale[0] = 0.5f; u_inst_dyn[0].scale[1] = 0.25f; u_inst_dyn[0].scale[2] = 1.0f;
    u_inst_dyn[0].flags = gdd_make_flags(0, 0, GDD_FRAG_PBR);
    u_inst_dyn[0].color[0] = 0.9f; u_inst_dyn[0].color[1] = 0.2f; u_inst_dyn[0].color[2] = 0.4f;
    u_inst_dyn[0].alpha = 1.0f;
    u_inst_dyn[0].pad[0] = 0.5f; u_inst_dyn[0].pad[1] = 0.25f;
    /* M = RotY(90) row-major: row 0 is (0,0,-1) */
    float ory[12] = { 0, 0, -1, 0,   0, 1, 0, 0,   1, 0, 0, 0 };
    memcpy(u_inst_dyn[0].orient, ory, sizeof(ory));

    /* 1: sphere, IN range, additive */
    u_inst_dyn[1].pos[0] = 4.0f; u_inst_dyn[1].pos[2] = -3.0f;
    u_inst_dyn[1].mesh = GDD_MESH_SPHERE;
    u_inst_dyn[1].scale[0] = 1.5f;
    u_inst_dyn[1].flags = gdd_make_flags(0, 1, GDD_FRAG_HYPERWARP);
    u_inst_dyn[1].color[0] = 1.0f; u_inst_dyn[1].color[1] = 0.8f; u_inst_dyn[1].color[2] = 0.3f;
    u_inst_dyn[1].alpha = 1.0f;
    set_identity_orient(u_inst_dyn[1].orient);

    /* 2: point, FAR, NEVER_CULL, additive */
    u_inst_dyn[2].pos[0] = 99.0f; u_inst_dyn[2].pos[1] = 99.0f; u_inst_dyn[2].pos[2] = 99.0f;
    u_inst_dyn[2].mesh = GDD_MESH_POINT;
    u_inst_dyn[2].scale[0] = 0.1f;
    u_inst_dyn[2].flags = gdd_make_flags(1, 1, GDD_FRAG_UNLIT);
    u_inst_dyn[2].color[0] = 0.3f; u_inst_dyn[2].color[1] = 0.9f; u_inst_dyn[2].color[2] = 0.7f;
    u_inst_dyn[2].alpha = 1.0f;
    set_identity_orient(u_inst_dyn[2].orient);

    /* 3: box, FAR -> culled */
    u_inst_dyn[3].pos[0] = 30.0f;
    u_inst_dyn[3].mesh = GDD_MESH_BOX;
    u_inst_dyn[3].scale[0] = 0.3f; u_inst_dyn[3].scale[1] = 0.4f; u_inst_dyn[3].scale[2] = 0.3f;
    u_inst_dyn[3].flags = gdd_make_flags(0, 0, GDD_FRAG_UNLIT);
    u_inst_dyn[3].alpha = 1.0f;
    set_identity_orient(u_inst_dyn[3].orient);

    /* 4: box, ON the boundary (dist == 10) -> kept (strict >) */
    u_inst_dyn[4].pos[0] = 10.0f;
    u_inst_dyn[4].mesh = GDD_MESH_BOX;
    u_inst_dyn[4].scale[0] = 0.2f; u_inst_dyn[4].scale[1] = 0.2f; u_inst_dyn[4].scale[2] = 0.2f;
    u_inst_dyn[4].flags = gdd_make_flags(0, 0, GDD_FRAG_UNLIT);
    u_inst_dyn[4].alpha = 1.0f;
    set_identity_orient(u_inst_dyn[4].orient);

    /* 5: box, just OUTSIDE (10.5) -> culled */
    u_inst_dyn[5].pos[0] = 10.5f;
    u_inst_dyn[5].mesh = GDD_MESH_BOX;
    u_inst_dyn[5].scale[0] = 0.2f; u_inst_dyn[5].scale[1] = 0.2f; u_inst_dyn[5].scale[2] = 0.2f;
    u_inst_dyn[5].flags = gdd_make_flags(0, 0, GDD_FRAG_UNLIT);
    u_inst_dyn[5].alpha = 1.0f;
    set_identity_orient(u_inst_dyn[5].orient);

    /* 6: pyramid, IN range */
    u_inst_dyn[6].pos[1] = 5.0f; u_inst_dyn[6].pos[2] = 5.0f;
    u_inst_dyn[6].mesh = GDD_MESH_PYRAMID;
    u_inst_dyn[6].scale[0] = 0.4f; u_inst_dyn[6].scale[1] = 0.3f; u_inst_dyn[6].scale[2] = 0.4f;
    u_inst_dyn[6].flags = gdd_make_flags(0, 0, GDD_FRAG_UNLIT);
    u_inst_dyn[6].alpha = 1.0f;
    set_identity_orient(u_inst_dyn[6].orient);

    /* 7: line (0,0,0) -> (0,0,5), IN range */
    u_inst_dyn[7].mesh = GDD_MESH_LINE;
    u_inst_dyn[7].scale[2] = 5.0f;
    u_inst_dyn[7].flags = gdd_make_flags(0, 0, GDD_FRAG_UNLIT);
    u_inst_dyn[7].alpha = 1.0f;
    set_identity_orient(u_inst_dyn[7].orient);

    /* 8: octa, ON the boundary (8,6,0): dist == 10 -> kept */
    u_inst_dyn[8].pos[0] = 8.0f; u_inst_dyn[8].pos[1] = 6.0f;
    u_inst_dyn[8].mesh = GDD_MESH_OCTA;
    u_inst_dyn[8].scale[0] = 0.7f; u_inst_dyn[8].scale[1] = 0.7f; u_inst_dyn[8].scale[2] = 0.7f;
    u_inst_dyn[8].flags = gdd_make_flags(0, 0, GDD_FRAG_PBR);
    u_inst_dyn[8].color[0] = 0.2f; u_inst_dyn[8].color[1] = 0.7f; u_inst_dyn[8].color[2] = 0.2f;
    u_inst_dyn[8].alpha = 1.0f;
    u_inst_dyn[8].pad[0] = 0.9f; u_inst_dyn[8].pad[1] = 0.1f;
    set_identity_orient(u_inst_dyn[8].orient);

    /* 9: ring, ON the boundary (-8,0,6): dist == 10 -> kept, additive */
    u_inst_dyn[9].pos[0] = -8.0f; u_inst_dyn[9].pos[2] = 6.0f;
    u_inst_dyn[9].mesh = GDD_MESH_RING;
    u_inst_dyn[9].scale[0] = 0.6f;
    u_inst_dyn[9].flags = gdd_make_flags(0, 1, GDD_FRAG_TWINKLE);
    u_inst_dyn[9].alpha = 1.0f;
    set_identity_orient(u_inst_dyn[9].orient);

    /* --- map group (static geometry: all NEVER_CULL) --------------- */
    u_inst_map[0].pos[0] = 5.0f; u_inst_map[0].pos[2] = -5.0f;
    u_inst_map[0].mesh = GDD_MESH_BOX;
    u_inst_map[0].scale[0] = 1.0f; u_inst_map[0].scale[1] = 1.0f; u_inst_map[0].scale[2] = 1.0f;
    u_inst_map[0].flags = gdd_make_flags(1, 0, GDD_FRAG_UNLIT);
    u_inst_map[0].alpha = 1.0f;
    set_identity_orient(u_inst_map[0].orient);

    u_inst_map[1].pos[0] = -5.0f; u_inst_map[1].pos[1] = 2.0f; u_inst_map[1].pos[2] = 5.0f;
    u_inst_map[1].mesh = GDD_MESH_BOX;
    u_inst_map[1].scale[0] = 0.5f; u_inst_map[1].scale[1] = 0.5f; u_inst_map[1].scale[2] = 2.0f;
    u_inst_map[1].flags = gdd_make_flags(1, 0, GDD_FRAG_PBR);
    u_inst_map[1].color[0] = 0.4f; u_inst_map[1].color[1] = 0.6f; u_inst_map[1].color[2] = 0.9f;
    u_inst_map[1].alpha = 1.0f;
    u_inst_map[1].pad[0] = 0.3f; u_inst_map[1].pad[1] = 0.7f;
    set_identity_orient(u_inst_map[1].orient);

    u_inst_map[2].mesh = GDD_MESH_LINE;
    u_inst_map[2].pos[1] = 1.0f;
    u_inst_map[2].scale[0] = 4.0f;
    u_inst_map[2].flags = gdd_make_flags(1, 0, GDD_FRAG_UNLIT);
    u_inst_map[2].alpha = 1.0f;
    set_identity_orient(u_inst_map[2].orient);

    u_inst_map[3].mesh = GDD_MESH_POINT;
    u_inst_map[3].scale[0] = 0.2f;
    u_inst_map[3].flags = gdd_make_flags(1, 1, GDD_FRAG_UNLIT);
    u_inst_map[3].alpha = 1.0f;
    set_identity_orient(u_inst_map[3].orient);
}

/* ------------------------------------------------------------------ */
/* CPU cull reference (exact copy of the shader comparison)            */
/* ------------------------------------------------------------------ */

static bool cpu_cull_visible(const GddInstance *it) {
    if (gdd_flag_bit(it->flags, 0)) return true; /* NEVER_CULL */
    float dx = it->pos[0] - CULL_CX;
    float dy = it->pos[1] - CULL_CY;
    float dz = it->pos[2] - CULL_CZ;
    return !(dx*dx + dy*dy + dz*dz > CULL_R * CULL_R);
}

/* ------------------------------------------------------------------ */
/* Stage runners                                                       */
/* ------------------------------------------------------------------ */

static bool run_cull(uint32_t capacity) {
    VkCommandBuffer cb;
    if (!begin_cb(&cb)) return false;
    vkCmdFillBuffer(cb, b_counts, 0, GDD_COUNTS_STRIDE, 0);
    GddPC pc;
    memset(&pc, 0, sizeof(pc));
    pc.cull_center[0] = CULL_CX; pc.cull_center[1] = CULL_CY; pc.cull_center[2] = CULL_CZ;
    pc.cull_radius = CULL_R;
    pc.capacity = capacity;

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g_pipe_cull);
    pc.list_count = N_MAP; pc.group = 1;
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl_cull, 0, 1,
                            &set_cull_map, 0, NULL);
    vkCmdPushConstants(cb, g_pl_cull, VK_SHADER_STAGE_COMPUTE_BIT, 0, GDD_PC_STRIDE, &pc);
    vkCmdDispatch(cb, (N_MAP + GDD_WORKGROUP - 1u) / GDD_WORKGROUP, 1, 1);

    pc.list_count = N_DYN; pc.group = 0;
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl_cull, 0, 1,
                            &set_cull_dyn, 0, NULL);
    vkCmdPushConstants(cb, g_pl_cull, VK_SHADER_STAGE_COMPUTE_BIT, 0, GDD_PC_STRIDE, &pc);
    vkCmdDispatch(cb, (N_DYN + GDD_WORKGROUP - 1u) / GDD_WORKGROUP, 1, 1);

    bool ok = check(vkEndCommandBuffer(cb), "end cb");
    if (ok) ok = submit_wait(cb);
    return ok;
}

static bool run_expand(uint32_t capacity) {
    VkCommandBuffer cb;
    if (!begin_cb(&cb)) return false;
    GddPC pc;
    memset(&pc, 0, sizeof(pc));
    pc.cull_center[0] = CULL_CX; pc.cull_center[1] = CULL_CY; pc.cull_center[2] = CULL_CZ;
    pc.cull_radius = CULL_R;
    pc.capacity = capacity;

    /* --- Passaggio 3A: Espansione Opaque (pc.pad = 0) --- */
    pc.pad = 0u;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g_pipe_exp);
    
    pc.list_count = N_MAP; pc.group = 1;
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl_exp, 0, 1,
                            &set_exp_map, 0, NULL);
    vkCmdPushConstants(cb, g_pl_exp, VK_SHADER_STAGE_COMPUTE_BIT, 0, GDD_PC_STRIDE, &pc);
    vkCmdDispatch(cb, (N_MAP + GDD_WORKGROUP - 1u) / GDD_WORKGROUP, 1, 1);

    pc.list_count = N_DYN; pc.group = 0;
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl_exp, 0, 1,
                            &set_exp_dyn, 0, NULL);
    vkCmdPushConstants(cb, g_pl_exp, VK_SHADER_STAGE_COMPUTE_BIT, 0, GDD_PC_STRIDE, &pc);
    vkCmdDispatch(cb, (N_DYN + GDD_WORKGROUP - 1u) / GDD_WORKGROUP, 1, 1);

    /* Barriera di memoria classica (Vulkan 1.0) per sincronizzare i contatori atomici */
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
    };
    vkCmdPipelineBarrier(
        cb,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        1, &barrier,
        0, NULL,
        0, NULL
    );

    /* --- Passaggio 3B: Espansione Additive (pc.pad = 1) --- */
    pc.pad = 1u;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g_pipe_exp);

    pc.list_count = N_MAP; pc.group = 1;
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl_exp, 0, 1,
                            &set_exp_map, 0, NULL);
    vkCmdPushConstants(cb, g_pl_exp, VK_SHADER_STAGE_COMPUTE_BIT, 0, GDD_PC_STRIDE, &pc);
    vkCmdDispatch(cb, (N_MAP + GDD_WORKGROUP - 1u) / GDD_WORKGROUP, 1, 1);

    pc.list_count = N_DYN; pc.group = 0;
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl_exp, 0, 1,
                            &set_exp_dyn, 0, NULL);
    vkCmdPushConstants(cb, g_pl_exp, VK_SHADER_STAGE_COMPUTE_BIT, 0, GDD_PC_STRIDE, &pc);
    vkCmdDispatch(cb, (N_DYN + GDD_WORKGROUP - 1u) / GDD_WORKGROUP, 1, 1);

    bool ok = check(vkEndCommandBuffer(cb), "end cb");
    if (ok) ok = submit_wait(cb);
    return ok;
}

static bool run_final(void) {
    VkCommandBuffer cb;
    if (!begin_cb(&cb)) return false;
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g_pipe_final);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g_pl_final, 0, 1,
                            &set_final, 0, NULL);
    vkCmdDispatch(cb, 1, 1, 1);
    bool ok = check(vkEndCommandBuffer(cb), "end cb");
    if (ok) ok = submit_wait(cb);
    return ok;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    if (argc > 1) {
        strncpy(g_spv_dir, argv[1], sizeof(g_spv_dir) - 1);
        g_spv_dir[sizeof(g_spv_dir) - 1] = 0;
    }

    printf("GDD mesh test (spv dir: %s)\n", g_spv_dir);

    if (!init_vulkan()) {
        printf("SKIP: no Vulkan ICD / no graphics+compute queue family\n");
        return 77;
    }
    if (!create_buffers_and_sets()) {
        fprintf(stderr, "FAIL: buffer/descriptor setup\n");
        return 1;
    }
    fill_test_data();

    /* ---------------- Stage 1: CULL ---------------- */
    if (!run_cull(4096)) {
        fprintf(stderr, "FAIL: cull stage did not complete\n");
        return 1;
    }
    int dyn_vis_ref = 0, map_vis_ref = 0;
    bool dyn_vis[N_DYN] = {0}, map_vis[N_MAP] = {0};
    for (int i = 0; i < N_DYN; i++) {
        dyn_vis_ref += cpu_cull_visible(&u_inst_dyn[i]);
        dyn_vis[i] = cpu_cull_visible(&u_inst_dyn[i]);
    }
    for (int i = 0; i < N_MAP; i++) {
        map_vis_ref += cpu_cull_visible(&u_inst_map[i]);
        map_vis[i] = cpu_cull_visible(&u_inst_map[i]);
    }
    CHECK(u_counts->dyn_vis == (uint32_t)dyn_vis_ref,
          "dyn_vis: gpu=%u cpu=%d", u_counts->dyn_vis, dyn_vis_ref);
    CHECK(u_counts->map_vis == (uint32_t)map_vis_ref,
          "map_vis: gpu=%u cpu=%d", u_counts->map_vis, map_vis_ref);
    CHECK(u_counts->opaque_verts == 0 && u_counts->additive_verts == 0,
          "counters zeroed by fill");
    if (u_counts->dyn_vis == (uint32_t)dyn_vis_ref) {
        int seen = 0;
        for (uint32_t s = 0; s < u_counts->dyn_vis; s++) {
            uint32_t idx = u_vis_dyn[s];
            CHECK(idx < N_DYN, "vis dyn index in range (%u)", idx);
            if (idx < N_DYN) {
                CHECK(dyn_vis[idx], "vis dyn[%u]=%u expected culled", s, idx);
                if (dyn_vis[idx] && !seen) { seen = 1; }
            }
        }
        CHECK(seen, "dyn vis list non-empty");
    }
    if (u_counts->map_vis == (uint32_t)map_vis_ref) {
        for (uint32_t s = 0; s < u_counts->map_vis; s++) {
            uint32_t idx = u_vis_map[s];
            CHECK(idx < N_MAP, "vis map index in range (%u)", idx);
            if (idx < N_MAP)
                CHECK(map_vis[idx], "vis map[%u]=%u expected culled", s, idx);
        }
    }

    /* ---------------- Stage 2: EXPAND ---------------- */
    memset(u_verts, 0, VMAX * sizeof(GddVertex));
    if (!run_expand(4096)) {
        fprintf(stderr, "FAIL: expand stage did not complete\n");
        return 1;
    }
    if (getenv("GDD_DUMP")) {
        uint32_t tot = u_counts->opaque_verts + u_counts->additive_verts;
        fprintf(stderr, "DUMP: opaque=%u additive=%u tot=%u\n",
                u_counts->opaque_verts, u_counts->additive_verts, tot);
        for (uint32_t k = 0; k < tot && k < VMAX; k++) {
            const GddVertex *v = &u_verts[k];
            fprintf(stderr, "[%4u] col(%.3g,%.3g,%.3g,%.3g) mat(%.3g,%.3g) pos(%.6g,%.6g,%.6g) loc(%.6g,%.6g,%.6g)\n",
                    k, (double)v->color[0], (double)v->color[1], (double)v->color[2], (double)v->color[3],
                    (double)v->metallic, (double)v->roughness,
                    (double)v->pos[0], (double)v->pos[1], (double)v->pos[2],
                    (double)v->local[0], (double)v->local[1], (double)v->local[2]);
        }
    }
    /* Expected per-pass totals */
    uint32_t opaque_ref = 0, additive_ref = 0;
    GddVertex exp_block[384];
    static uint32_t used[VMAX];
    int blocks_checked = 0;

    for (int i = 0; i < N_MAP; i++) {
        if (!map_vis[i]) continue;
        uint32_t n = expand_mesh(&u_inst_map[i], exp_block);
        float tol = (u_inst_dyn[i].mesh == GDD_MESH_SPHERE ||
                     u_inst_dyn[i].mesh == GDD_MESH_OCTA ||
                     u_inst_dyn[i].mesh == GDD_MESH_RING ||
                     u_inst_dyn[i].mesh == GDD_MESH_POINT ||
                     u_inst_dyn[i].mesh == GDD_MESH_LINE || i == 0) ? TOL_TRIG : TOL_EXACT;
        uint32_t at = find_block(u_verts, VMAX, used, exp_block, n, tol);
        CHECK(at != UINT32_MAX, "map block %d (mesh %u, %u verts) found in readback",
              i, (uint32_t)u_inst_map[i].mesh, n);
        blocks_checked++;
        if (gdd_flag_bit(u_inst_map[i].flags, 1))
            additive_ref += n;
        else
            opaque_ref += n;
    }
    
    for (int i = 0; i < N_DYN; i++) {
        if (!dyn_vis[i]) continue;
        uint32_t n = expand_mesh(&u_inst_dyn[i], exp_block);
        /* trig meshes (sin/cos) and rotated blocks get the ulp tolerance */
        float tol = (u_inst_dyn[i].mesh == GDD_MESH_SPHERE ||
                     u_inst_dyn[i].mesh == GDD_MESH_OCTA ||
                     u_inst_dyn[i].mesh == GDD_MESH_RING || i == 0) ? TOL_TRIG : TOL_EXACT;
        uint32_t at = find_block(u_verts, VMAX, used, exp_block, n, tol);
        
        if (at == UINT32_MAX) {
            fprintf(stderr, "FAILED BLOCK %d (mesh %d). First expected vertex:\n", i, (int)u_inst_dyn[i].mesh);
            fprintf(stderr, "  pos: %f %f %f\n", exp_block[0].pos[0], exp_block[0].pos[1], exp_block[0].pos[2]);
            fprintf(stderr, "  nrm: %f %f %f\n", exp_block[0].normal[0], exp_block[0].normal[1], exp_block[0].normal[2]);
            fprintf(stderr, "  loc: %f %f %f\n", exp_block[0].local[0], exp_block[0].local[1], exp_block[0].local[2]);
            fprintf(stderr, "  col: %f %f %f %f\n", exp_block[0].color[0], exp_block[0].color[1], exp_block[0].color[2], exp_block[0].color[3]);
            fprintf(stderr, "First 5 actual vertices in readback (unused):\n");
            int printed = 0;
            for (uint32_t k = 0; k < VMAX && printed < 5; k++) {
                if (!used[k] && (u_verts[k].pos[0] != 0.0f || u_verts[k].pos[1] != 0.0f)) {
                    if (u_inst_dyn[i].color[0] > 0 && fabsf(u_verts[k].color[0] - u_inst_dyn[i].color[0]) > 0.001f) continue;
                    fprintf(stderr, "  [%d] ", k);
                    dump_vertex("", &u_verts[k]);
                    printed++;
                }
            }
        }

        CHECK(at != UINT32_MAX, "dyn block %d (mesh %u, %u verts) found in readback",
              i, (uint32_t)u_inst_dyn[i].mesh, n);
        blocks_checked++;
        if (gdd_flag_bit(u_inst_dyn[i].flags, 1))
            additive_ref += n;
        else
            opaque_ref += n;
    }
    
    CHECK(u_counts->opaque_verts == opaque_ref,
          "opaque_verts: gpu=%u cpu=%u", u_counts->opaque_verts, opaque_ref);
    CHECK(u_counts->additive_verts == additive_ref,
          "additive_verts: gpu=%u cpu=%u", u_counts->additive_verts, additive_ref);
    CHECK(blocks_checked == dyn_vis_ref + map_vis_ref,
          "all %d visible blocks matched (checked %d)",
          dyn_vis_ref + map_vis_ref, blocks_checked);

    /* ---------------- Stage 3: FINAL ---------------- */
    if (!run_final()) {
        fprintf(stderr, "FAIL: final stage did not complete\n");
        return 1;
    }
    CHECK(u_indirect[0].vertexCount == u_counts->opaque_verts,
          "indirect[0].vertexCount: %u vs %u",
          u_indirect[0].vertexCount, u_counts->opaque_verts);
    CHECK(u_indirect[0].instanceCount == 1 && u_indirect[0].firstVertex == 0 &&
          u_indirect[0].firstInstance == 0, "indirect[0] shape");
    CHECK(u_indirect[1].vertexCount == u_counts->additive_verts,
          "indirect[1].vertexCount: %u vs %u",
          u_indirect[1].vertexCount, u_counts->additive_verts);
    CHECK(u_indirect[1].instanceCount == 1, "indirect[1] shape");

    /* ---------------- Stage 4: CAPACITY GUARD ---------------- */
    uint32_t full_total = u_counts->opaque_verts + u_counts->additive_verts;
    uint32_t cap = full_total / 2 + 1; /* forces at least one drop */
    if (!run_cull(cap)) {
        fprintf(stderr, "FAIL: guard cull stage did not complete\n");
        return 1;
    }
    if (!run_expand(cap)) {
        fprintf(stderr, "FAIL: guard expand stage did not complete\n");
        return 1;
    }
    uint32_t g_tot = u_counts->opaque_verts + u_counts->additive_verts;
    CHECK(g_tot <= cap, "guard: totals %u <= capacity %u", g_tot, cap);
    CHECK(g_tot < full_total,
          "guard: something dropped (%u < %u)", g_tot, (unsigned)full_total);
    if (!run_final()) {
        fprintf(stderr, "FAIL: guard final stage did not complete\n");
        return 1;
    }
    CHECK(u_indirect[0].vertexCount == u_counts->opaque_verts &&
          u_indirect[1].vertexCount == u_counts->additive_verts,
          "guard: final consistent with counters");

    printf("  %d checks passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
