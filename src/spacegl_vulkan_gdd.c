/*
 * SPACE GL - 3D LOGIC ENGINE
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
 */

/*
 * SPACE GL - GPU-DRIVEN RENDERING (GDD)
 *
 * Part 1 of 2: the CPU-side instance builder.
 *
 * gdd_build_instances() is the heart of the GPU-driven architecture:
 * instead of recording one draw call per object (CPU-driven path), it
 * classifies the whole frame into a compact list of GddInstance
 * descriptors (objects, effects, compass, statics, galaxy map) that
 * the GPU then culls, expands and draws with two indirect calls.
 *
 * The visual mapping (mesh / color / mode per object type) mirrors the
 * dispatcher in recordCommandBuffer() of src/spacegl_vulkan.c so the
 * two architectures render the same scene semantics. Where the CPU
 * path uses per-draw-call specialization (wireframe vs solid vs glow
 * pipelines), the GDD path encodes the same choice in the instance
 * flags (GDD_FLAG_ADDITIVE / fragment mode).
 *
 * Part 2 (Vulkan 1.4 device-side state, pipelines, per-frame ring and
 * frame recording) is added in the second half of this file.
 */

#include <stdio.h>

#include "spacegl_gdd.h"
#include "spacegl_vulkan_types.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Quadrant half-size (QUADRANT_SIZE / 2, game_config.h) and the galaxy
 * map sector gap (drawGalaxyMap) — kept here so the builder stays
 * free of project-header dependencies (unit-testable in isolation). */
#define GDD_QUADRANT      20.0f
#define GDD_QUADRANT_SPAN 40.0f
#define GDD_GALAXY_SIZE   40
#define GDD_GALAXY_GAP    1.2f
#define GDD_GRID_STEP     2.0f

/* Ship visual scale (SCALE_SHIP * 0.55 from the CPU path) and the CPU
 * ship mesh length ratio (apex 2.1866 + base 0.7288 = 2.9154 units). */
#define GDD_SHIP_SCALE    (0.45f * 0.55f)
#define GDD_SHIP_LEN_RATIO 2.9154f
#define GDD_GDD_BRIDGE_Y  0.30f  /* unused placeholder to keep parity */

/* ================================================================== */
/* mat3 helpers — engine convention (see the note in spacegl_gdd.h):  */
/* row-major matrices, transform v' = v * M (row vector).             */
/* ================================================================== */
typedef float gdd_m3[3][3];

static void gdd_m3_identity(gdd_m3 m) {
    memset(m, 0, sizeof(gdd_m3));
    m[0][0] = 1.0f; m[1][1] = 1.0f; m[2][2] = 1.0f;
}

static void gdd_m3_multiply(const gdd_m3 a, const gdd_m3 b, gdd_m3 out) {
    gdd_m3 tmp;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            tmp[i][j] = a[i][0]*b[0][j] + a[i][1]*b[1][j] + a[i][2]*b[2][j];
    memcpy(out, tmp, sizeof(gdd_m3));
}

/* Same formulas as mat4_rotate() in src/spacegl_vulkan.c, composed as
 * m = m * rot (the engine's right-multiplied composition order). */
static void gdd_m3_rotate(gdd_m3 m, float angle, float x, float y, float z) {
    float c = cosf(angle), s = sinf(angle), t = 1.0f - c;
    float len = sqrtf(x*x + y*y + z*z);
    if (len > 0.0f) { x /= len; y /= len; z /= len; }
    gdd_m3 rot;
    gdd_m3_identity(rot);
    rot[0][0] = t*x*x + c;   rot[0][1] = t*x*y - s*z; rot[0][2] = t*x*z + s*y;
    rot[1][0] = t*x*y + s*z; rot[1][1] = t*y*y + c;   rot[1][2] = t*y*z - s*x;
    rot[2][0] = t*x*z - s*y; rot[2][1] = t*y*z + s*x; rot[2][2] = t*z*z + c;
    gdd_m3_multiply(m, rot, m);
}

/* Fill a GLSL mat3 (12 floats, columns padded to 4) from the engine's
 * row-major rotation M so that the GLSL transform O * v is IDENTICAL to
 * the engine's v * M: O holds the rows of M stacked as GLSL columns
 * (O[c] = row c of M), i.e. the GLSL matrix equals M^T. */
static void gdd_orient_from_m3(float orient[12], const gdd_m3 m) {
    for (int c = 0; c < 3; c++) {
        orient[4*c + 0] = m[c][0];
        orient[4*c + 1] = m[c][1];
        orient[4*c + 2] = m[c][2];
        orient[4*c + 3] = 0.0f;
    }
}

static void gdd_orient_identity(float orient[12]) {
    memset(orient, 0, 12 * sizeof(float));
    orient[0] = orient[5] = orient[10] = 1.0f; /* M = I -> O = I */
}

/* World direction of the model +X axis for a row-major M (row 0). */
static void gdd_m3_xaxis(const gdd_m3 m, float out[3]) {
    out[0] = m[0][0]; out[1] = m[0][1]; out[2] = m[0][2];
}

/* ================================================================== */
/* Instance list helpers (capacity-guarded)                           */
/* ================================================================== */
typedef struct {
    GddInstance *arr;
    uint32_t cap;
    uint32_t n;
} GddList;

/* Forward: gdd_list_add is defined below, the compass arrow helper
 * (also below) needs it. */
static void gdd_list_add(GddList *L, int mesh,
                         float px, float py, float pz,
                         float sx, float sy, float sz,
                         float cr, float cg, float cb, float ca,
                         uint32_t never_cull, uint32_t additive, int frag_mode,
                         const float orient[12],
                         float metallic, float roughness);

/* Identity orientation (GDD mesh lines get their cross-section from
 * the direction vector, so an identity matrix is the right default). */
static const float GDD_ORIENT_ID[12] = { 1, 0, 0, 0,
                                         0, 1, 0, 0,
                                         0, 0, 1, 0 };

/* World position of an AR-compass local point: anchor + M * (lp * s)
 * (engine row-vector convention, same as gdd_xform in gdd_ops.glsl). */
static void gdd_compass_pt(const float anchor[3], const gdd_m3 m,
                           const float lp[3], float s, float out[3]) {
    for (int r = 0; r < 3; r++)
        out[r] = anchor[r]
               + (lp[0] * s) * m[0][r]
               + (lp[1] * s) * m[1][r]
               + (lp[2] * s) * m[2][r];
}

/* One AR-compass arrow with CPU parity (vectorVertices in
 * src/spacegl_vulkan.c): shaft (0.18,0,0)->(1.38,0,0), head square at
 * x = 1.38 (corners ±0.05 in Y/Z), tip (1.68,0,0). Emits 9 line
 * instances: 1 shaft + 4 square edges + 4 tip spokes. */
static void gdd_compass_arrow(GddList *dyn, const float anchor[3],
                              const gdd_m3 m, float s,
                              float cr, float cg, float cb) {
    static const float shaft0[3] = { 0.18f, 0.0f, 0.0f };
    static const float shaft1[3] = { 1.38f, 0.0f, 0.0f };
    static const float tip[3]    = { 1.68f, 0.0f, 0.0f };
    static const float corners[4][3] = {
        { 1.38f,  0.05f,  0.05f }, { 1.38f, -0.05f,  0.05f },
        { 1.38f, -0.05f, -0.05f }, { 1.38f,  0.05f, -0.05f },
    };
    float p0[3], p1[3];
    gdd_compass_pt(anchor, m, shaft0, s, p0);
    gdd_compass_pt(anchor, m, shaft1, s, p1);
    gdd_list_add(dyn, GDD_MESH_LINE, p0[0], p0[1], p0[2],
                 p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2],
                 cr, cg, cb, 1.0f, 0, 0, GDD_FRAG_UNLIT, GDD_ORIENT_ID, 0, 0);
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        /* square edge i -> i+1 (CPU vectorIndices: 2-3, 3-4, 4-5, 5-2) */
        gdd_compass_pt(anchor, m, corners[i], s, p0);
        gdd_compass_pt(anchor, m, corners[j], s, p1);
        gdd_list_add(dyn, GDD_MESH_LINE, p0[0], p0[1], p0[2],
                     p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2],
                     cr, cg, cb, 1.0f, 0, 0, GDD_FRAG_UNLIT, GDD_ORIENT_ID, 0, 0);
        /* tip spoke i -> tip (CPU vectorIndices: 6-x) */
        gdd_compass_pt(anchor, m, corners[i], s, p0);
        gdd_compass_pt(anchor, m, tip, s, p1);
        gdd_list_add(dyn, GDD_MESH_LINE, p0[0], p0[1], p0[2],
                     p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2],
                     cr, cg, cb, 1.0f, 0, 0, GDD_FRAG_UNLIT, GDD_ORIENT_ID, 0, 0);
    }
}

static void gdd_list_add(GddList *L, int mesh,
                         float px, float py, float pz,
                         float sx, float sy, float sz,
                         float cr, float cg, float cb, float ca,
                         uint32_t never_cull, uint32_t additive, int frag_mode,
                         const float orient[12],
                         float metallic, float roughness) {
    if (L->n >= L->cap) return; /* drop when full (same policy as CPU path) */
    GddInstance *it = &L->arr[L->n++];
    memset(it, 0, sizeof(*it));
    it->pos[0] = px; it->pos[1] = py; it->pos[2] = pz;
    it->mesh = (float)mesh;
    it->scale[0] = sx; it->scale[1] = sy; it->scale[2] = sz;
    it->flags = gdd_make_flags(never_cull, additive, (uint32_t)frag_mode);
    it->color[0] = cr; it->color[1] = cg; it->color[2] = cb;
    it->alpha = ca;
    memcpy(it->orient, orient, sizeof(it->orient));
    it->pad[0] = metallic; it->pad[1] = roughness;
}

static void gdd_list_add_identity(GddList *L, int mesh,
                                  float px, float py, float pz,
                                  float sx, float sy, float sz,
                                  float cr, float cg, float cb, float ca,
                                  uint32_t never_cull, uint32_t additive, int frag_mode,
                                  float metallic, float roughness) {
    float o[12];
    gdd_orient_identity(o);
    gdd_list_add(L, mesh, px, py, pz, sx, sy, sz, cr, cg, cb, ca,
                 never_cull, additive, frag_mode, o, metallic, roughness);
}

/* ================================================================== */
/* Ship orientation (mirrors the object loop of recordCommandBuffer): */
/*   R = RotY(90°) * RotY(-h°) * Rot(m°, (cos h, 0, -sin h))           */
/*       * Rot(r°, (sin h cos m, sin m, cos h cos m))                  */
/* (engine row-major composition order: first rotation is closest to  */
/* the vertex).                                                       */
/* ================================================================== */
static void gdd_ship_rotation(const SmoothObj *so, gdd_m3 out) {
    float h = so->h * M_PI / 180.0f;
    float m = so->m * M_PI / 180.0f;
    float r = so->r * M_PI / 180.0f;
    gdd_m3_identity(out);
    gdd_m3_rotate(out, 90.0f * M_PI / 180.0f, 0.0f, 1.0f, 0.0f);
    gdd_m3_rotate(out, -h, 0.0f, 1.0f, 0.0f);
    gdd_m3_rotate(out, m, cosf(h), 0.0f, -sinf(h));
    gdd_m3_rotate(out, r, sinf(h) * cosf(m), sinf(m), cosf(h) * cosf(m));
}

/* ================================================================== */
/* Per-type visual classification (mirrors the CPU dispatcher)        */
/* ================================================================== */
typedef struct {
    int mesh;
    float scale;     /* uniform base scale (pre tactScale) */
    float color[3];
    int frag_mode;
    int additive;
    float metallic;
    float roughness;
    int spin;        /* 0 = none, 1 = quasar spin (RotY(pulse*3) * RotX(20°)) */
} GddVisual;

static void gdd_visual_for(int type, int faction, int ship_class, int plating,
                           int cloaked, const GddBuildCtx *ctx, GddVisual *v) {
    (void)ctx;
    /* Default: generic unlit sphere (wireframe-like look of the CPU
     * fallback), like the CPU path default (usePushColor=5 / PBR). */
    v->mesh = GDD_MESH_SPHERE;
    v->scale = 0.45f; /* SCALE_SHIP */
    v->color[0] = v->color[1] = v->color[2] = 1.0f;
    v->frag_mode = GDD_FRAG_PBR;
    v->additive = 0;
    v->metallic = 0.5f; v->roughness = 0.5f;
    v->spin = 0;

    /* Base type colors (getObjectColor, src/spacegl_vulkan.c) */
    if ((type == 1 || type >= 10) && faction >= 10 && faction <= 20) {
        switch (faction) {
        case 10: v->color[0]=1.0f; v->color[1]=0.1f; v->color[2]=0.0f; break;
        case 11: v->color[0]=0.0f; v->color[1]=1.0f; v->color[2]=0.2f; break;
        case 12: v->color[0]=0.1f; v->color[1]=0.1f; v->color[2]=0.1f; break;
        case 13: v->color[0]=1.0f; v->color[1]=0.0f; v->color[2]=1.0f; break;
        case 14: v->color[0]=0.5f; v->color[1]=0.0f; v->color[2]=0.8f; break;
        case 15: v->color[0]=1.0f; v->color[1]=0.5f; v->color[2]=0.0f; break;
        case 16: v->color[0]=0.4f; v->color[1]=0.6f; v->color[2]=0.1f; break;
        case 17: v->color[0]=0.8f; v->color[1]=0.7f; v->color[2]=0.1f; break;
        case 18: v->color[0]=0.7f; v->color[1]=1.0f; v->color[2]=0.0f; break;
        case 19: v->color[0]=0.0f; v->color[1]=0.5f; v->color[2]=1.0f; break;
        case 20: v->color[0]=0.6f; v->color[1]=0.2f; v->color[2]=0.1f; break;
        default: v->color[0]=1.0f; v->color[1]=0.5f; v->color[2]=0.5f; break;
        }
    } else if (type == 1)            { v->color[0]=0.0f; v->color[1]=1.0f; v->color[2]=1.0f; }
    else if (type == 4)             { v->color[0]=1.0f; v->color[1]=1.0f; v->color[2]=0.0f; }
    else if (type == 5)             { v->color[0]=0.0f; v->color[1]=1.0f; v->color[2]=0.5f; }
    else if (type == 3)             { v->color[0]=0.0f; v->color[1]=1.0f; v->color[2]=0.0f; }
    else if (type == 6)             { v->color[0]=0.5f; v->color[1]=0.0f; v->color[2]=1.0f; }
    else if (type == 29)            { v->color[0]=1.0f; v->color[1]=0.0f; v->color[2]=1.0f; }
    else if (type == 21)            { v->color[0]=0.5f; v->color[1]=0.35f; v->color[2]=0.25f; }
    else if (type == 35)            { v->color[0]=0.6f; v->color[1]=0.6f; v->color[2]=0.7f; }
    else if (type == 23)            { v->color[0]=0.3f; v->color[1]=0.3f; v->color[2]=0.35f; }
    else if (type == 24)            { v->color[0]=0.8f; v->color[1]=0.8f; v->color[2]=0.9f; }
    else if (type == 25)            { v->color[0]=0.4f; v->color[1]=0.4f; v->color[2]=0.45f; }
    else if (type == 26)            { v->color[0]=0.5f; v->color[1]=0.0f; v->color[2]=0.8f; }
    else if (type == 40)            { v->color[0]=0.0f; v->color[1]=0.8f; v->color[2]=0.6f; }
    else if (type == 50)            { v->color[0]=0.0f; v->color[1]=1.0f; v->color[2]=0.5f; }

    switch (type) {
    case 1: /* player ship */
    case 10: case 11: case 12: case 13: case 14:
    case 15: case 16: case 17: case 18: case 19:
    case 20: case 210: case 211: case 212: case 213: case 214:
    case 215: case 216: case 217: case 218: case 219:
    case 220: case 221: case 222: case 223: case 224: case 225:
    case 226: case 227: case 228: case 229: case 230:
    case 231: case 232: case 233: case 234: case 235: case 236:
    case 237: case 238: case 239:
    case 240: case 241: case 242: case 243: case 244: case 245:
    case 246: case 247: case 248: case 249: case 250:
    case 251: case 252: case 253: case 254: case 255:
        /* any type >= 10 is a ship in the CPU path (10..255) */
        if (type == 1 || type >= 10) {
            v->mesh = GDD_MESH_PYRAMID;
            v->scale = GDD_SHIP_SCALE;
            v->frag_mode = GDD_FRAG_UNLIT; /* CPU: wireframe pipeline */
            v->metallic = 0.9f; v->roughness = 0.25f;
            if (cloaked) {
                v->color[0]=0.0f; v->color[1]=0.8f; v->color[2]=1.0f;
                v->additive = 1; v->frag_mode = GDD_FRAG_UNLIT;
            }
        }
        break;

    case 4: /* star */
        v->scale = 2.5f; v->frag_mode = GDD_FRAG_HYPERWARP;
        v->additive = 1; v->metallic = 0.0f; v->roughness = 1.0f;
        switch (ship_class) {
        case 0:  v->color[0]=0.4f; v->color[1]=0.6f; v->color[2]=1.0f; break;
        case 1:  v->color[0]=0.9f; v->color[1]=0.9f; v->color[2]=1.0f; break;
        case 2:  v->color[0]=1.0f; v->color[1]=1.0f; v->color[2]=0.4f; break;
        case 3:  v->color[0]=1.0f; v->color[1]=0.7f; v->color[2]=0.2f; break;
        default: v->color[0]=1.0f; v->color[1]=0.3f; v->color[2]=0.2f; break;
        }
        break;

    case 5: /* planet */
        v->scale = 1.8f; v->frag_mode = GDD_FRAG_PBR;
        v->metallic = 0.0f; v->roughness = 0.9f;
        switch (ship_class) {
        case 0:  v->color[0]=0.2f; v->color[1]=0.5f; v->color[2]=1.0f; break;
        case 1:  v->color[0]=0.8f; v->color[1]=0.6f; v->color[2]=0.4f; break;
        case 2:  v->color[0]=0.7f; v->color[1]=0.9f; v->color[2]=1.0f; break;
        case 3:  v->color[0]=0.8f; v->color[1]=0.2f; v->color[2]=0.1f; break;
        default: v->color[0]=0.8f; v->color[1]=0.7f; v->color[2]=0.5f; break;
        }
        break;

    case 6: /* black hole (core; the disk is added by the caller) */
        v->scale = 2.0f; v->frag_mode = GDD_FRAG_PBR;
        v->color[0]=0.05f; v->color[1]=0.0f; v->color[2]=0.1f;
        break;

    case 29: /* quasar (core; the disk is added by the caller) */
        v->scale = 2.2f; v->frag_mode = GDD_FRAG_HYPERWARP;
        v->additive = 1; v->spin = 1;
        switch (ship_class) {
        case 0:  v->color[0]=0.1f; v->color[1]=0.4f; v->color[2]=1.0f; break;
        case 1:  v->color[0]=1.0f; v->color[1]=0.8f; v->color[2]=0.2f; break;
        case 2:  v->color[0]=0.0f; v->color[1]=1.0f; v->color[2]=0.5f; break;
        case 3:  v->color[0]=1.0f; v->color[1]=0.4f; v->color[2]=0.0f; break;
        case 4:  v->color[0]=1.0f; v->color[1]=0.1f; v->color[2]=0.1f; break;
        case 5:  v->color[0]=0.7f; v->color[1]=0.0f; v->color[2]=1.0f; break;
        default: v->color[0]=0.0f; v->color[1]=0.9f; v->color[2]=1.0f; break;
        }
        break;

    case 21: /* asteroid */
        v->scale = 0.08f * (plating > 0 ? (float)plating / 100.0f : 0.3f);
        if (v->scale < 0.02f) v->scale = 0.08f;
        v->frag_mode = GDD_FRAG_PBR;
        break;

    case 3:  /* starbase: octahedron core (arms/ring added by caller) */
        v->mesh = GDD_MESH_OCTA; v->scale = 0.6f; v->frag_mode = GDD_FRAG_UNLIT;
        break;

    case 7:  /* nebula (volumetric, additive) */
        v->scale = 5.0f; v->frag_mode = GDD_FRAG_NEBULA; v->additive = 1;
        v->color[0]=0.3f; v->color[1]=0.5f; v->color[2]=0.9f;
        break;

    case 8:  /* pulsar (core; jets added by caller) */
        v->scale = 0.6f; v->frag_mode = GDD_FRAG_HYPERWARP; v->additive = 1;
        v->color[0]=1.0f; v->color[1]=0.5f; v->color[2]=0.0f;
        break;

    case 9:  /* comet (head; tail added by caller) */
        v->scale = 0.35f; v->frag_mode = GDD_FRAG_UNLIT;
        v->color[0]=0.5f; v->color[1]=0.8f; v->color[2]=1.0f;
        break;

    case 23: /* mine */
        v->scale = 0.35f; v->frag_mode = GDD_FRAG_PBR;
        break;

    case 24: /* comm buoy */
        v->mesh = GDD_MESH_OCTA; v->scale = 0.4f; v->frag_mode = GDD_FRAG_UNLIT;
        break;

    case 25: /* platform */
        v->mesh = GDD_MESH_BOX; v->scale = 0.6f; v->frag_mode = GDD_FRAG_PBR;
        break;

    case 26: /* rift */
        v->mesh = GDD_MESH_RING; v->scale = 1.2f; v->frag_mode = GDD_FRAG_UNLIT;
        v->additive = 1;
        break;

    case 27: /* torpedo (object form) */
        v->mesh = GDD_MESH_PYRAMID; v->scale = 0.3f; v->frag_mode = GDD_FRAG_UNLIT;
        v->color[0]=1.0f; v->color[1]=1.0f; v->color[2]=0.0f;
        v->metallic = 0.8f; v->roughness = 0.1f;
        break;

    case 30: /* monster */
    case 31:
        v->scale = 0.8f; v->frag_mode = GDD_FRAG_UNLIT;
        v->color[0]=1.0f; v->color[1]=1.0f; v->color[2]=1.0f;
        break;

    case 34: /* dyson fragment (glow) */
        v->scale = 1.5f; v->frag_mode = GDD_FRAG_HYPERWARP; v->additive = 1;
        v->color[0]=1.0f; v->color[1]=0.8f; v->color[2]=0.2f;
        break;

    case 35: /* trading hub (core; ring added by caller) */
        v->mesh = GDD_MESH_BOX; v->scale = 0.5f; v->frag_mode = GDD_FRAG_PBR;
        break;

    case 36: /* ancient relic (wireframe look) */
        v->scale = 0.8f; v->frag_mode = GDD_FRAG_UNLIT;
        v->color[0]=0.0f; v->color[1]=1.0f; v->color[2]=0.8f;
        break;

    case 37: /* subspace rupture (glow) */
        v->scale = 1.2f; v->frag_mode = GDD_FRAG_HYPERWARP; v->additive = 1;
        v->color[0]=0.6f; v->color[1]=0.0f; v->color[2]=0.8f;
        break;

    case 38: /* satellite (core; ring added by caller) */
        v->mesh = GDD_MESH_BOX; v->scale = 0.3f; v->frag_mode = GDD_FRAG_PBR;
        break;

    case 39: /* ion storm (glow) */
        v->scale = 1.4f; v->frag_mode = GDD_FRAG_HYPERWARP; v->additive = 1;
        v->color[0]=0.4f; v->color[1]=0.4f; v->color[2]=1.0f;
        break;

    case 40: /* alien artifact (wireframe look) */
        v->scale = 0.6f; v->frag_mode = GDD_FRAG_UNLIT;
        v->color[0]=1.0f; v->color[1]=0.5f; v->color[2]=0.0f;
        break;

    case 41: /* warp gate */
        v->mesh = GDD_MESH_RING; v->scale = 1.0f; v->frag_mode = GDD_FRAG_UNLIT;
        v->color[0]=0.0f; v->color[1]=0.5f; v->color[2]=1.0f;
        break;

    case 42: /* neutron star (core; jet ring added by caller) */
        v->scale = 0.5f; v->frag_mode = GDD_FRAG_UNLIT;
        v->color[0]=0.8f; v->color[1]=0.8f; v->color[2]=1.0f;
        break;

    case 43: /* mega structure (core; ring added by caller) */
        v->mesh = GDD_MESH_BOX; v->scale = 0.5f; v->frag_mode = GDD_FRAG_UNLIT;
        v->color[0]=0.5f; v->color[1]=0.5f; v->color[2]=0.5f;
        break;

    case 44: /* dark cloud (translucent) */
        v->scale = 2.5f; v->frag_mode = GDD_FRAG_NEBULA; v->additive = 1;
        v->color[0]=0.2f; v->color[1]=0.0f; v->color[2]=0.4f;
        break;

    case 45: /* singularity (core; ring added by caller) */
        v->scale = 2.0f; v->frag_mode = GDD_FRAG_PBR;
        v->color[0]=0.0f; v->color[1]=0.3f; v->color[2]=0.1f;
        break;

    case 46: /* plasma storm (glow) */
        v->scale = 1.6f; v->frag_mode = GDD_FRAG_HYPERWARP; v->additive = 1;
        v->color[0]=1.0f; v->color[1]=0.0f; v->color[2]=1.0f;
        break;

    case 47: /* orbital ring */
        v->mesh = GDD_MESH_RING; v->scale = 1.5f; v->frag_mode = GDD_FRAG_UNLIT;
        v->color[0]=0.7f; v->color[1]=0.7f; v->color[2]=0.0f;
        break;

    case 48: /* time anomaly (glow) */
        v->scale = 1.2f; v->frag_mode = GDD_FRAG_HYPERWARP; v->additive = 1;
        v->color[0]=0.0f; v->color[1]=1.0f; v->color[2]=0.5f;
        break;

    case 49: /* void crystal */
        v->mesh = GDD_MESH_OCTA; v->scale = 0.7f; v->frag_mode = GDD_FRAG_UNLIT;
        v->color[0]=0.9f; v->color[1]=0.0f; v->color[2]=0.9f;
        break;

    case 50: /* subspace anomaly */
        v->scale = 0.6f; v->frag_mode = GDD_FRAG_UNLIT;
        break;

    case 51: case 52: case 53: case 54: case 55:
        /* diffuse / high-energy / dark-matter / gravimetric / temporal nebulae */
        v->scale = 5.0f; v->frag_mode = GDD_FRAG_NEBULA; v->additive = 1;
        switch (type) {
        case 51: v->color[0]=0.4f; v->color[1]=0.5f; v->color[2]=0.9f; break;
        case 52: v->color[0]=0.9f; v->color[1]=0.4f; v->color[2]=0.2f; break;
        case 53: v->color[0]=0.3f; v->color[1]=0.1f; v->color[2]=0.5f; break;
        case 54: v->color[0]=0.1f; v->color[1]=0.5f; v->color[2]=0.2f; break;
        default: v->color[0]=0.8f; v->color[1]=0.3f; v->color[2]=0.8f; break;
        }
        break;

    case 56: case 57: case 58: case 59: case 62: case 63:
    case 65: case 67: case 69: case 70: case 77: case 78:
        /* filaments / shocks / waves: electrical discharge look */
        v->scale = 2.0f; v->frag_mode = GDD_FRAG_FILAMENT; v->additive = 1;
        v->color[0]=0.5f; v->color[1]=0.4f; v->color[2]=0.9f;
        break;

    case 60: case 61: case 71: case 72:
        /* accretion disks / relativistic jets / protoplanetary disks */
        v->scale = 2.5f; v->frag_mode = GDD_FRAG_ACCRETION; v->additive = 1;
        v->color[0]=1.0f; v->color[1]=0.8f; v->color[2]=0.5f;
        break;

    default:
        /* generic cosmic object (64-88 and friends): unlit sphere */
        v->scale = 1.5f; v->frag_mode = GDD_FRAG_UNLIT;
        v->color[0]=0.6f; v->color[1]=0.6f; v->color[2]=0.8f;
        break;
    }
}

/* ================================================================== */
/* Galaxy map sector decoding (mirrors drawGalaxyMap)                 */
/* ================================================================== */
static int gdd_sector_digit(int64_t uval, int divisor) {
    int64_t d = 1;
    for (int i = 0; i < divisor; i++) d *= 10;
    return (int)((uval / d) % 10);
}

static int gdd_sector_matches_filter(int filter, int sst, int pl, int bs, int en,
                                     int player, int bh, int neb, int pul,
                                     int storm, int comet, int asteroid,
                                     int derelict, int mine, int buoy,
                                     int platform, int rift, int monster,
                                     int quasar) {
    switch (filter) {
    case 1:  return sst > 0;
    case 2:  return pl > 0;
    case 3:  return bs > 0;
    case 4:  return en > 0 || player > 0;
    case 5:  return bh > 0;
    case 6:  return neb > 0;
    case 7:  return pul > 0;
    case 8:  return storm > 0;
    case 9:  return comet > 0;
    case 10: return asteroid > 0;
    case 11: return derelict > 0;
    case 12: return mine > 0;
    case 13: return buoy > 0;
    case 14: return platform > 0;
    case 15: return rift > 0;
    case 16: return monster > 0;
    case 17: return quasar > 0;
    default: return 1;
    }
}

/* ================================================================== */
/* The builder                                                        */
/* ================================================================== */
void gdd_build_instances(const GddBuildCtx *ctx) {
    GddList dyn = { ctx->dyn_out, ctx->dyn_cap, 0 };
    GddList map = { ctx->map_out, ctx->map_cap, 0 };

    float ts = 1.0f - ctx->map_anim; /* tactScale */
    float ms = ctx->map_anim;        /* mapScale */
    float pulse = ctx->pulse;
    float o[12];
    gdd_orient_identity(o);

    /* ---------------------------------------------------------------- */
    /* 0. Tactical bounding box — the quadrant wireframe cube drawn in  */
    /*    the CPU path from const vertices[] / indices[] (always on,    */
    /*    no show_grid flag, hidden only during map and jump arrival).  */
    /*    16 edges: top loop (green), bottom loop (red), 8 verticals    */
    /*    top→mid→bottom (yellow). Thickness 0.25 via metallic field.  */
    /* ---------------------------------------------------------------- */
    if (ctx->map_anim < 0.99f &&
        !(ctx->jump_arrival && ctx->jump_arrival->active)) {
        float hq = GDD_QUADRANT; /* 20.0 = QUADRANT_SIZE/2 */
        float ht = 0.02f;        /* same half-thickness as the AR compass axis lines */
        /* Top loop corners  (Y = +hq, green 0,1,0) */
        float tx[4] = { -hq*ts,  hq*ts,  hq*ts, -hq*ts };
        float ty[4] = {  hq*ts,  hq*ts,  hq*ts,  hq*ts };
        float tz[4] = { -hq*ts, -hq*ts,  hq*ts,  hq*ts };
        /* Bottom loop corners (Y = -hq, red 1,0,0) */
        float bx[4] = { -hq*ts,  hq*ts,  hq*ts, -hq*ts };
        float by[4] = { -hq*ts, -hq*ts, -hq*ts, -hq*ts };
        float bz[4] = { -hq*ts, -hq*ts,  hq*ts,  hq*ts };
        /* Mid corners (Y = 0, yellow 1,1,0) */
        float mx[4] = { -hq*ts,  hq*ts,  hq*ts, -hq*ts };
        float my[4] = {      0,       0,       0,       0 };
        float mz[4] = { -hq*ts, -hq*ts,  hq*ts,  hq*ts };
        /* Top loop: 4 edges */
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) % 4;
            gdd_list_add_identity(&dyn, GDD_MESH_LINE,
                tx[i], ty[i], tz[i],
                tx[j]-tx[i], ty[j]-ty[i], tz[j]-tz[i],
                0, 1, 0, 1, 1, 0, GDD_FRAG_UNLIT, ht, 0);
        }
        /* Bottom loop: 4 edges */
        for (int i = 0; i < 4; i++) {
            int j = (i + 1) % 4;
            gdd_list_add_identity(&dyn, GDD_MESH_LINE,
                bx[i], by[i], bz[i],
                bx[j]-bx[i], by[j]-by[i], bz[j]-bz[i],
                1, 0, 0, 1, 1, 0, GDD_FRAG_UNLIT, ht, 0);
        }
        /* Verticals top→mid: 4 edges (yellow) */
        for (int i = 0; i < 4; i++) {
            gdd_list_add_identity(&dyn, GDD_MESH_LINE,
                tx[i], ty[i], tz[i],
                mx[i]-tx[i], my[i]-ty[i], mz[i]-tz[i],
                1, 1, 0, 1, 1, 0, GDD_FRAG_UNLIT, ht, 0);
        }
        /* Verticals mid→bottom: 4 edges (yellow) */
        for (int i = 0; i < 4; i++) {
            gdd_list_add_identity(&dyn, GDD_MESH_LINE,
                mx[i], my[i], mz[i],
                bx[i]-mx[i], by[i]-my[i], bz[i]-mz[i],
                1, 1, 0, 1, 1, 0, GDD_FRAG_UNLIT, ht, 0);
        }
    }

    /* ---------------------------------------------------------------- */
    /* 1. Networked objects (tactical view)                             */
    /* ---------------------------------------------------------------- */
    if (ctx->map_anim < 0.99f && ctx->object_count > 0 && ctx->objs) {
        for (int i = 0; i < ctx->object_count && i < (int)GDD_MAX_NET_OBJECTS; i++) {
            const SmoothObj *so = &ctx->objs[i];
            if (so->first) continue;
            if (ctx->active && !ctx->active[i]) continue;
            
            /* Durante l'arrivo da salto, mostra solo l'oggetto 0 (la nave) ed escludi gli altri */
            if (ctx->jump_arrival && ctx->jump_arrival->active && i != 0) continue;

            int type = ctx->types ? ctx->types[i] : 1;
            int faction = ctx->factions ? ctx->factions[i] : 0;
            int sclass = ctx->ship_classes ? ctx->ship_classes[i] : 0;
            int cloaked = ctx->cloaked ? ctx->cloaked[i] : 0;
            int plating = ctx->platings ? ctx->platings[i] : 30;

            /* Centered mapping (X, Z, -Y) — CPU parity */
            float px = (so->x - GDD_QUADRANT) * ts;
            float py = (so->z - GDD_QUADRANT) * ts;
            float pz = (GDD_QUADRANT - so->y) * ts;
            if (type == 5) {
                pz = (GDD_QUADRANT - so->z) * ts;
            }
            
            

            GddVisual v;
            gdd_visual_for(type, faction, sclass, plating, cloaked, ctx, &v);

            /* Orientation: full Euler for ships, spin for quasars */
            if ((type == 1 || type >= 10) && !cloaked) {
                gdd_m3 m;
                gdd_ship_rotation(so, m);
                gdd_orient_from_m3(o, m);
            } else if (v.spin) {
                gdd_m3 m;
                gdd_m3_identity(m);
                gdd_m3_rotate(m, pulse * 3.0f, 0.0f, 1.0f, 0.0f);
                gdd_m3_rotate(m, 20.0f * M_PI / 180.0f, 1.0f, 0.0f, 0.0f);
                gdd_orient_from_m3(o, m);
            } else {
                gdd_orient_identity(o);
            }

            float s = v.scale * ts;
            float sx = s, sy = s, sz = s;
            if (type == 1 || type >= 10) {
                /* CPU ship mesh aspect: nose length 2.9154, base 1.0 */
                sx *= GDD_SHIP_LEN_RATIO;
            }
            gdd_list_add(&dyn, v.mesh, px, py, pz, sx, sy, sz,
                         v.color[0], v.color[1], v.color[2], 1.0f,
                         0, v.additive, v.frag_mode, o,
                         v.metallic, v.roughness);

            /* --- per-type companions (CPU parity approximations) --- */
            if (type == 6) { /* black hole accretion disk */
                gdd_list_add(&dyn, GDD_MESH_SPHERE, px, py, pz,
                             s * 3.5f, s * 0.02f, s * 3.5f,
                             1.0f, 0.9f, 0.6f, 0.8f,
                             0, 1, GDD_FRAG_ACCRETION, o, 0.0f, 1.0f);
            } else if (type == 29) { /* quasar accretion disk (spins with core) */
                gdd_list_add(&dyn, GDD_MESH_SPHERE, px, py, pz,
                             s * 3.5f, s * 0.02f, s * 3.5f,
                             v.color[0], v.color[1], v.color[2], 0.5f,
                             0, 1, GDD_FRAG_ACCRETION, o, 0.0f, 1.0f);
            } else if (type == 3) { /* starbase ring */
                gdd_list_add(&dyn, GDD_MESH_RING, px, py, pz,
                             2.0f * ts, 2.0f * ts, 2.0f * ts,
                             1.0f, 0.0f, 1.0f, 0.8f,
                             0, 1, GDD_FRAG_UNLIT, o, 0.0f, 1.0f);
            } else if (type == 8) { /* pulsar jets (two thin boxes) */
                gdd_list_add(&dyn, GDD_MESH_BOX, px, py + 1.6f * ts, pz,
                             0.12f * ts, 1.4f * ts, 0.12f * ts,
                             1.0f, 0.6f, 0.1f, 0.9f, 0, 1, GDD_FRAG_HYPERWARP, o, 0.0f, 1.0f);
                gdd_list_add(&dyn, GDD_MESH_BOX, px, py - 1.6f * ts, pz,
                             0.12f * ts, 1.4f * ts, 0.12f * ts,
                             1.0f, 0.6f, 0.1f, 0.9f, 0, 1, GDD_FRAG_HYPERWARP, o, 0.0f, 1.0f);
            } else if (type == 9) { /* comet tail (thin box along heading) */
                gdd_m3 m;
                float h = so->h * M_PI / 180.0f;
                float mrot = so->m * M_PI / 180.0f;
                gdd_m3_identity(m);
                gdd_m3_rotate(m, 90.0f * M_PI / 180.0f - h, 0, 1, 0);
                gdd_m3_rotate(m, mrot, cosf(h), 0.0f, -sinf(h));
                float d[3];
                gdd_m3_xaxis(m, d);
                float back = 1.2f * ts;
                gdd_list_add(&dyn, GDD_MESH_BOX,
                             px - d[0] * back, py - d[1] * back, pz - d[2] * back,
                             back, 0.15f * ts, 0.15f * ts,
                             0.5f, 0.8f, 1.0f, 0.7f, 0, 1, GDD_FRAG_UNLIT, o, 0.0f, 1.0f);
            } else if (type == 35) { /* trading hub ring */
                gdd_list_add(&dyn, GDD_MESH_RING, px, py, pz,
                             1.0f * ts, 1.0f * ts, 1.0f * ts,
                             v.color[0], v.color[1], v.color[2], 0.9f,
                             0, 0, GDD_FRAG_UNLIT, o, 0.5f, 0.5f);
            } else if (type == 38) { /* satellite ring (dish) */
                gdd_list_add(&dyn, GDD_MESH_RING, px, py + 0.35f * ts, pz,
                             0.45f * ts, 0.45f * ts, 0.45f * ts,
                             0.7f, 0.8f, 0.9f, 0.9f, 0, 0, GDD_FRAG_UNLIT, o, 0.5f, 0.5f);
            } else if (type == 42) { /* neutron star jet ring */
                gdd_list_add(&dyn, GDD_MESH_RING, px, py, pz,
                             1.2f * ts, 1.2f * ts, 1.2f * ts,
                             0.8f, 0.8f, 1.0f, 0.8f, 0, 1, GDD_FRAG_UNLIT, o, 0.0f, 1.0f);
            } else if (type == 43) { /* mega structure ring */
                gdd_list_add(&dyn, GDD_MESH_RING, px, py, pz,
                             1.2f * ts, 1.2f * ts, 1.2f * ts,
                             0.5f, 0.5f, 0.5f, 0.9f, 0, 0, GDD_FRAG_UNLIT, o, 0.5f, 0.5f);
            } else if (type == 45) { /* singularity ring */
                gdd_list_add(&dyn, GDD_MESH_RING, px, py, pz,
                             2.5f * ts, 2.5f * ts, 2.5f * ts,
                             0.0f, 1.0f, 0.3f, 0.9f, 0, 1, GDD_FRAG_ACCRETION, o, 0.0f, 1.0f);
            }
        }
    }

    /* ---------------------------------------------------------------- */
    /* 2. AR compass (anchored to the smoothed player) — dyn group      */
    /*    Exact parity with the CPU-driven compass ("Zero-Lag AR        */
    /*    Compass" in recordCommandBuffer): same 7 elements, same       */
    /*    planes, same rotations:                                      */
    /*      1. 3 axis lines, pure R/G/B (the CPU vertex colors)        */
    /*      2. fixed ring   — white circle, world XZ,  r = 3.0         */
    /*      3. heading ring — cyan circle, pitched,     r = 2.5        */
    /*      4. mark arc     — yellow vertical ARC (local XY), r = 2.8  */
    /*      5. roll circle  — yellow transverse circle (local YZ)      */
    /*      6. nose arrow   — green, shaft + head (roll-free)          */
    /*      7. top arrow    — blue, half size, full ship orientation   */
    /* ---------------------------------------------------------------- */
    int compass_ok = ctx->show_axes && ctx->object_count > 0 &&
                     ctx->objs && !ctx->objs[0].first &&
                     ctx->camera_dist < 150.0f &&
                     !(ctx->jump_arrival && ctx->jump_arrival->active);
    float cx = 0, cy = 0, cz = 0;
    if (compass_ok) {
        const SmoothObj *p0 = &ctx->objs[0];
        cx = (p0->x - GDD_QUADRANT) * ts;
        cy = (p0->z - GDD_QUADRANT) * ts;
        cz = (GDD_QUADRANT - p0->y) * ts;
        float anchor[3] = { cx, cy, cz };

        float h = p0->h * M_PI / 180.0f;      /* heading (CPU oh)  */
        float mrot = p0->m * M_PI / 180.0f;   /* pitch   (CPU om)  */
        float rrot = p0->r * M_PI / 180.0f;   /* roll    (CPU oro) */
        float pitch_axis[3] = { cosf(h), 0.0f, -sinf(h) };
        float nose[3] = { sinf(h) * cosf(mrot), sinf(mrot), cosf(h) * cosf(mrot) };
        float head_yaw = (90.0f * M_PI / 180.0f) - h;

        /* 1. Global axes (fixed to world orientation, pure R/G/B) */
        float a = 5.5f * ts;
        gdd_list_add(&dyn, GDD_MESH_LINE, cx - a, cy, cz, 2.0f*a, 0, 0,
                     1, 0, 0, 1.0f, 0, 0, GDD_FRAG_UNLIT, o, 0, 0);
        gdd_list_add(&dyn, GDD_MESH_LINE, cx, cy - a, cz, 0, 2.0f*a, 0,
                     0, 1, 0, 1.0f, 0, 0, GDD_FRAG_UNLIT, o, 0, 0);
        gdd_list_add(&dyn, GDD_MESH_LINE, cx, cy, cz - a, 0, 0, 2.0f*a,
                     0, 0, 1, 1.0f, 0, 0, GDD_FRAG_UNLIT, o, 0, 0);

        /* 2. Fixed compass ring (white, world orientation) */
        gdd_list_add(&dyn, GDD_MESH_CIRCLE, cx, cy, cz,
                     3.0f*ts, 3.0f*ts, 3.0f*ts, 1, 1, 1, 1.0f,
                     0, 0, GDD_FRAG_UNLIT, o, 0, 0);

        /* 3. Heading ring (cyan, level with pitch) */
        {
            gdd_m3 mring;
            gdd_m3_identity(mring);
            gdd_m3_rotate(mring, mrot, pitch_axis[0], pitch_axis[1], pitch_axis[2]);
            gdd_orient_from_m3(o, mring);
            gdd_list_add(&dyn, GDD_MESH_CIRCLE, cx, cy, cz,
                         2.5f*ts, 2.5f*ts, 2.5f*ts, 0, 1, 1, 1.0f,
                         0, 0, GDD_FRAG_UNLIT, o, 0, 0);
        }

        /* 4. Mark arc (yellow vertical arc, aligned with ship heading) */
        {
            gdd_m3 mring;
            gdd_m3_identity(mring);
            gdd_m3_rotate(mring, head_yaw, 0, 1, 0);
            gdd_orient_from_m3(o, mring);
            gdd_list_add(&dyn, GDD_MESH_ARC, cx, cy, cz,
                         2.8f*ts, 2.8f*ts, 2.8f*ts, 1, 1, 0, 1.0f,
                         0, 0, GDD_FRAG_UNLIT, o, 0, 0);
        }

        /* 5. Roll circle (yellow, transverse: the GDD circle lives in
         *    local XZ, so a RotZ(90) brings it to the CPU's local YZ,
         *    then the same heading + pitch rotations) */
        {
            gdd_m3 mring;
            gdd_m3_identity(mring);
            gdd_m3_rotate(mring, 90.0f * M_PI / 180.0f, 0, 0, 1);
            gdd_m3_rotate(mring, head_yaw, 0, 1, 0);
            gdd_m3_rotate(mring, mrot, pitch_axis[0], pitch_axis[1], pitch_axis[2]);
            gdd_orient_from_m3(o, mring);
            gdd_list_add(&dyn, GDD_MESH_CIRCLE, cx, cy, cz,
                         2.8f*ts, 2.8f*ts, 2.8f*ts, 1, 1, 0, 1.0f,
                         0, 0, GDD_FRAG_UNLIT, o, 0, 0);
        }

        /* 6. Directional vector (green arrow along the nose; a roll is
         *    a rotation around the nose and leaves the arrow unchanged) */
        {
            gdd_m3 mring;
            gdd_m3_identity(mring);
            gdd_m3_rotate(mring, head_yaw, 0, 1, 0);
            gdd_m3_rotate(mring, mrot, pitch_axis[0], pitch_axis[1], pitch_axis[2]);
            gdd_compass_arrow(&dyn, anchor, mring, ts, 0.0f, 1.0f, 0.0f);
        }

        /* 7. Top vector (blue arrow, half size, full ship orientation) */
        {
            gdd_m3 mring;
            gdd_m3_identity(mring);
            gdd_m3_rotate(mring, -90.0f * M_PI / 180.0f, 0, 0, 1);
            gdd_m3_rotate(mring, head_yaw, 0, 1, 0);
            gdd_m3_rotate(mring, mrot, pitch_axis[0], pitch_axis[1], pitch_axis[2]);
            gdd_m3_rotate(mring, rrot, nose[0], nose[1], nose[2]);
            gdd_compass_arrow(&dyn, anchor, mring, 0.5f * ts, 0.0f, 0.5f, 1.0f);
        }
    }

    /* ---------------------------------------------------------------- */
    /* 3. Torpedoes (FX)                                                 */
    /* ---------------------------------------------------------------- */
    if (ctx->map_anim < 0.99f && ctx->torps) {
        for (int i = 0; i < (int)GDD_MAX_ACTIVE_TORPS; i++) {
            const ActiveTorp *t = &ctx->torps[i];
            if (t->active <= 0) continue;
            gdd_list_add_identity(&dyn, GDD_MESH_PYRAMID,
                                  t->x * ts, t->y * ts, t->z * ts,
                                  0.3f * ts, 0.3f * ts, 0.3f * ts,
                                  1.0f, 1.0f, 0.0f, 1.0f,
                                  0, 0, GDD_FRAG_UNLIT, 0.9f, 0.1f);
        }
    }

    /* ---------------------------------------------------------------- */
    /* 4. Ion beams (FX): beam + white core + impact splash              */
    /* ---------------------------------------------------------------- */
    if (ctx->map_anim < 0.99f && ctx->beams) {
        for (int i = 0; i < (int)GDD_MAX_ACTIVE_BEAMS; i++) {
            const ActiveBeam *b = &ctx->beams[i];
            if (b->life <= 0.0f) continue;
            float bsx = b->sx - GDD_QUADRANT;
            float bsy = b->sz - GDD_QUADRANT;
            float bsz = GDD_QUADRANT - b->sy;
            
            float btx = b->tx - GDD_QUADRANT;
            float bty = b->tz - GDD_QUADRANT;
            float btz = GDD_QUADRANT - b->ty;
            /* Real-time tracking: snap endpoints to smoothed positions */
            for (int j = 0; j < ctx->object_count && j < (int)GDD_MAX_NET_OBJECTS; j++) {
                if (ctx->objs && ctx->active && ctx->active[j] && ctx->ids) {
                    if (b->owner_id > 0 && ctx->ids[j] == b->owner_id) {
                        bsx = ctx->objs[j].x - GDD_QUADRANT;
                        bsy = ctx->objs[j].z - GDD_QUADRANT;
                        bsz = GDD_QUADRANT - ctx->objs[j].y;
                    }
                    if (b->extra > 0 && ctx->ids[j] == b->extra) {
                        btx = ctx->objs[j].x - GDD_QUADRANT;
                        bty = ctx->objs[j].z - GDD_QUADRANT;
                        btz = GDD_QUADRANT - ctx->objs[j].y;
                    }
                }
            }
            float vsx = bsx * ts, vsy = bsy * ts, vsz = bsz * ts;
            float vtx = btx * ts, vty = bty * ts, vtz = btz * ts;
            float V[3] = { vtx - vsx, vty - vsy, vtz - vsz };
            float dist = sqrtf(V[0]*V[0] + V[1]*V[1] + V[2]*V[2]);
            if (dist < 0.1f) continue;
            V[0] /= dist; V[1] /= dist; V[2] /= dist;

            /* Direct basis: X = V, Y = actual_up, Z = right (CPU parity) */
            float up_ref[3] = { 0, 1, 0 };
            if (fabsf(V[1]) > 0.95f) { up_ref[0] = 1; up_ref[1] = 0; }
            float right[3] = {
                up_ref[1]*V[2] - up_ref[2]*V[1],
                up_ref[2]*V[0] - up_ref[0]*V[2],
                up_ref[0]*V[1] - up_ref[1]*V[0]
            };
            float rlen = sqrtf(right[0]*right[0] + right[1]*right[1] + right[2]*right[2]);
            if (rlen < 0.001f) { right[0] = 0; right[1] = 0; right[2] = 1; }
            else { right[0] /= rlen; right[1] /= rlen; right[2] /= rlen; }
            float a_up[3] = {
                V[1]*right[2] - V[2]*right[1],
                V[2]*right[0] - V[0]*right[2],
                V[0]*right[1] - V[1]*right[0]
            };
            gdd_m3 mb = { { V[0], V[1], V[2] },
                          { a_up[0], a_up[1], a_up[2] },
                          { right[0], right[1], right[2] } };
            float bo[12];
            gdd_orient_from_m3(bo, mb);
            float thick = 0.85f * ts * b->life;

            gdd_list_add(&dyn, GDD_MESH_BOX,
                         (vsx + vtx) * 0.5f, (vsy + vty) * 0.5f, (vsz + vtz) * 0.5f,
                         dist, thick, thick,
                         0.0f, 0.8f, 1.0f, b->life,
                         0, 1, GDD_FRAG_HYPERWARP, bo, 0.0f, 1.0f);
            gdd_list_add(&dyn, GDD_MESH_BOX,
                         (vsx + vtx) * 0.5f, (vsy + vty) * 0.5f, (vsz + vtz) * 0.5f,
                         dist, thick * 0.35f, thick * 0.35f,
                         1.0f, 1.0f, 1.0f, b->life,
                         0, 1, GDD_FRAG_HYPERWARP, bo, 0.0f, 1.0f);
            float is = 1.3f * ts * b->life;
            gdd_list_add_identity(&dyn, GDD_MESH_SPHERE, vtx, vty, vtz,
                                  is, is, is, 1.0f, 1.0f, 1.0f, b->life,
                                  0, 1, GDD_FRAG_SHOCKWAVE, 0.0f, 1.0f);
        }
    }

    /* ---------------------------------------------------------------- */
    /* 5. Explosions (FX): flash + core + aura + pixel cloud             */
    /* ---------------------------------------------------------------- */
    if (ctx->map_anim < 0.99f && ctx->booms) {
        for (int i = 0; i < (int)GDD_MAX_ACTIVE_BOOMS; i++) {
            const ActiveBoom *bm = &ctx->booms[i];
            if (bm->life <= 0.0f) continue;
            float life = bm->life;
            float bx = bm->x * ts, by = bm->y * ts, bz = bm->z * ts;

            if (life < 0.7f || 1.0f) { /* (flash below) */
            }
            gdd_list_add_identity(&dyn, GDD_MESH_SPHERE, bx, by, bz,
                                  life * 1.5f * ts, life * 1.5f * ts, life * 1.5f * ts,
                                  1, 1, 1, life, 0, 1, GDD_FRAG_HYPERWARP, 0, 1);
            if (life > 0.7f) {
                float s = (1.0f - life) * 3.0f * ts;
                gdd_list_add_identity(&dyn, GDD_MESH_SPHERE, bx, by, bz, s, s, s,
                                      1, 1, 1, (life - 0.7f) / 0.3f,
                                      0, 1, GDD_FRAG_UNLIT, 0, 1);
            }
            float sa = (1.0f + (1.0f - life) * 2.0f) * 2.5f * ts;
            gdd_list_add_identity(&dyn, GDD_MESH_SPHERE, bx, by, bz, sa, sa, sa,
                                  0, 1, 1, life * 0.7f, 0, 1, GDD_FRAG_SHOCKWAVE, 0, 1);

            /* Pixel cloud: subset of the CPU 256-pixel explosion */
            float exp = (1.0f - life) * 12.0f * ts;
            int px_n = 16;
            for (int p = 0; p < px_n; p++) {
                int idx = p * (int)GDD_EXPLOSION_PIXELS / px_n;
                float wx = (bm->x + bm->offsets[idx][0] * exp) * ts;
                float wy = (bm->y + bm->offsets[idx][1] * exp) * ts;
                float wz = (bm->z + bm->offsets[idx][2] * exp) * ts;
                gdd_list_add_identity(&dyn, GDD_MESH_POINT, wx, wy, wz,
                                      0.25f * ts, 0.25f * ts, 0.25f * ts,
                                      bm->colors[idx][0], bm->colors[idx][1], bm->colors[idx][2], 1.0f,
                                      0, 1, GDD_FRAG_UNLIT, 0, 1);
            }
        }
    }

    /* ---------------------------------------------------------------- */
    /* 6. Dismantle / dematerialization (FX)                             */
    /* ---------------------------------------------------------------- */
    if (ctx->map_anim < 0.99f && ctx->dismantles) {
        for (int i = 0; i < (int)GDD_MAX_ACTIVE_DISMANTLES; i++) {
            const ActiveDismantle *dm = &ctx->dismantles[i];
            if (dm->life <= 0.0f) continue;
            float life = dm->life;
            float dx = dm->x * ts, dy = dm->y * ts, dz = dm->z * ts;
            if (life < 0.7f) continue;
            float s0 = (1.0f - life) * 3.0f * ts;
            gdd_list_add_identity(&dyn, GDD_MESH_SPHERE, dx, dy, dz, s0, s0, s0,
                                  1, 1, 1, (life - 0.7f) / 0.3f, 0, 1, GDD_FRAG_UNLIT, 0, 1);
            float s1 = life * 1.5f * ts;
            gdd_list_add_identity(&dyn, GDD_MESH_SPHERE, dx, dy, dz, s1, s1, s1,
                                  1, 1, 1, life, 0, 1, GDD_FRAG_HYPERWARP, 0, 1);
            float s2 = (1.0f + (1.0f - life) * 2.0f) * 2.5f * ts;
            gdd_list_add_identity(&dyn, GDD_MESH_SPHERE, dx, dy, dz, s2, s2, s2,
                                  0, 1, 1, life * 0.7f, 0, 1, GDD_FRAG_SHOCKWAVE, 0, 1);
        }
    }

    /* ---------------------------------------------------------------- */
    /* 7. Wormholes (departure + arrival)                                */
    /* ---------------------------------------------------------------- */
    if (ctx->map_anim < 0.99f) {
        if (ctx->wormhole && ctx->wormhole->active) {
            float wx = ctx->wormhole->x * ts, wy = ctx->wormhole->y * ts, wz = ctx->wormhole->z * ts;
            gdd_list_add_identity(&dyn, GDD_MESH_SPHERE, wx, wy, wz,
                                  1.5f * ts, 1.5f * ts, 1.5f * ts,
                                  0.05f, 0.0f, 0.2f, 1.0f, 0, 0, GDD_FRAG_PBR, 0, 1);
            gdd_list_add_identity(&dyn, GDD_MESH_RING, wx, wy, wz,
                                  1.8f * ts, 1.8f * ts, 1.8f * ts,
                                  0.0f, 0.9f, 1.0f, 0.9f, 0, 1, GDD_FRAG_HYPERWARP, 0, 1);
        }
        if (ctx->jump_arrival && ctx->jump_arrival->active && ctx->jump_arrival->timer > 0) {
            float jx = ctx->jump_arrival->x * ts;
            float jy = ctx->jump_arrival->y * ts;
            float jz = ctx->jump_arrival->z * ts;
            float closing = (ctx->jump_arrival->timer < 540)
                          ? ((float)ctx->jump_arrival->timer / 540.0f) : 1.0f;
            gdd_list_add_identity(&dyn, GDD_MESH_SPHERE, jx, jy, jz,
                                  1.5f * closing * ts, 1.5f * closing * ts, 1.5f * closing * ts,
                                  0.05f, 0.0f, 0.2f, 1.0f, 0, 0, GDD_FRAG_PBR, 0, 1);
            gdd_list_add_identity(&dyn, GDD_MESH_RING, jx, jy, jz,
                                  1.8f * closing * ts, 1.8f * closing * ts, 1.8f * closing * ts,
                                  0.0f, 0.9f, 1.0f, 0.9f, 0, 1, GDD_FRAG_HYPERWARP, 0, 1);
            /* Arrival glow on the ship (CPU parity: timer > 360) */
            if (ctx->jump_arrival->timer > 360 && ctx->object_count > 0 &&
                ctx->objs && !ctx->objs[0].first) {
                float glow = 1.0f - (float)(ctx->jump_arrival->timer - 360) / 180.0f;
                gdd_list_add_identity(&dyn, GDD_MESH_SPHERE, cx, cy, cz,
                                      (1.5f + glow) * GDD_SHIP_SCALE * ts,
                                      (1.5f + glow) * GDD_SHIP_SCALE * ts,
                                      (1.5f + glow) * GDD_SHIP_SCALE * ts,
                                      0.8f, 0.8f, 1.0f, 1.0f - glow * 0.5f,
                                      0, 1, GDD_FRAG_HYPERWARP, 0, 1);
            }
        }
    }

    /* ---------------------------------------------------------------- */
    /* 8. Shield hit glow (around the player)                            */
    /* ---------------------------------------------------------------- */
    if (compass_ok && ctx->shield_timers) {
        int hit = 0;
        for (int i = 0; i < 6; i++) if (ctx->shield_timers[i] > 0) hit = 1;
        if (hit) {
            float rad = (1.2f + 0.3f * sinf(pulse * 10.0f)) * GDD_SHIP_SCALE * ts;
            gdd_list_add_identity(&dyn, GDD_MESH_RING, cx, cy, cz,
                                  rad * 2.0f, rad * 2.0f, rad * 2.0f,
                                  0.0f, 0.9f, 1.0f, 0.8f, 0, 1, GDD_FRAG_HYPERWARP, 0, 1);
            gdd_list_add_identity(&dyn, GDD_MESH_RING, cx, cy, cz,
                                  rad * 2.6f, rad * 2.6f, rad * 2.6f,
                                  1.0f, 1.0f, 1.0f, 0.5f, 0, 1, GDD_FRAG_HYPERWARP, 0, 1);
        }
    }

    /* ---------------------------------------------------------------- */
    /* 9. Galaxy map (map group, rebuilt every frame in map mode)        */
    /* ---------------------------------------------------------------- */
    if (ctx->map_anim > 0.01f) {
        float gap = GDD_GALAXY_GAP;
        float offset = -((float)GDD_GALAXY_SIZE * gap) / 2.0f;
        int gs = ctx->galaxy_size > 0 ? ctx->galaxy_size : GDD_GALAXY_SIZE;
        int stride = gs + 1; /* shm_galaxy[gs+1][gs+1][gs+1], coords 1..gs */

        /* 1. Galaxy frame */
        gdd_list_add_identity(&map, GDD_MESH_BOX, 0, 0, 0,
                              (float)GDD_GALAXY_SIZE * gap * ms * 0.5f,
                              (float)GDD_GALAXY_SIZE * gap * ms * 0.5f,
                              (float)GDD_GALAXY_SIZE * gap * ms * 0.5f,
                              0.4f, 0.4f, 1.0f, 0.8f, 1, 0, GDD_FRAG_UNLIT, 0, 1);

        /* 2. Sectors */
        if (ctx->galaxy && ctx->map_anim > 0.01f) {
            for (int z = 1; z <= gs; z++) {
                for (int y = 1; y <= gs; y++) {
                    for (int x = 1; x <= gs; x++) {
                        int64_t val = ctx->galaxy[x * stride * stride + y * stride + z];
                        int is_my_q = (x == ctx->player_q[0] && y == ctx->player_q[1] && z == ctx->player_q[2]);
                        if (val == 0 && !is_my_q) continue;

                        int64_t uval = (val < 0) ? -val : val;
                        int quasar   = gdd_sector_digit(uval, 17);
                        int monster  = gdd_sector_digit(uval, 16);
                        int player   = gdd_sector_digit(uval, 15);
                        int rift     = gdd_sector_digit(uval, 14);
                        int platform = gdd_sector_digit(uval, 13);
                        int buoy     = gdd_sector_digit(uval, 12);
                        int mine     = gdd_sector_digit(uval, 11);
                        int derelict = gdd_sector_digit(uval, 10);
                        int asteroid = gdd_sector_digit(uval, 9);
                        int comet    = gdd_sector_digit(uval, 8);
                        int storm    = gdd_sector_digit(uval, 7);
                        int pul      = gdd_sector_digit(uval, 6);
                        int neb      = gdd_sector_digit(uval, 5);
                        int bh       = gdd_sector_digit(uval, 4);
                        int pl       = gdd_sector_digit(uval, 3);
                        int en       = gdd_sector_digit(uval, 2);
                        int bs       = gdd_sector_digit(uval, 1);
                        int sst      = gdd_sector_digit(uval, 0);

                        if (ctx->map_filter > 0 && !is_my_q &&
                            !gdd_sector_matches_filter(ctx->map_filter, sst, pl, bs, en,
                                                       player, bh, neb, pul, storm,
                                                       comet, asteroid, derelict, mine,
                                                       buoy, platform, rift, monster,
                                                       quasar))
                            continue;

                        float pxm = (offset + (x - 0.5f) * gap) * ms;
                        float pym = (offset + (z - 0.5f) * gap) * ms;
                        float pzm = (offset + ((float)GDD_GALAXY_SIZE + 0.5f - y) * gap) * ms;

                        int ef = ctx->map_filter;
                        float cr = 0.4f, cg = 0.4f, cb = 0.4f;
                        if (ef == 17 || (ef == 0 && quasar > 0))     { cr=1;   cg=0;   cb=1; }
                        else if (ef == 16 || (ef == 0 && monster > 0)) { cr=1;   cg=1;   cb=1; }
                        else if (ef == 15 || (ef == 0 && rift > 0))    { cr=0;   cg=1;   cb=1; }
                        else if (ef == 14 || (ef == 0 && platform > 0)){ cr=0.8f;cg=0.4f;cb=0; }
                        else if (ef == 13 || (ef == 0 && buoy > 0))    { cr=0;   cg=0.5f;cb=1; }
                        else if (ef == 12 || (ef == 0 && mine > 0))    { cr=1;   cg=0;   cb=0; }
                        else if (ef == 11 || (ef == 0 && derelict > 0)){ cr=0.3f;cg=0.3f;cb=0.3f;}
                        else if (ef == 10 || (ef == 0 && asteroid > 0)){ cr=0.5f;cg=0.3f;cb=0.1f;}
                        else if (ef == 9  || (ef == 0 && comet > 0))   { cr=0.5f;cg=0.8f;cb=1; }
                        else if (ef == 8  || (ef == 0 && storm > 0))   { cr=1;   cg=1;   cb=1; }
                        else if (ef == 7  || (ef == 0 && pul > 0))     { cr=1;   cg=0.5f;cb=0; }
                        else if (ef == 6  || (ef == 0 && neb > 0))     { cr=0.7f;cg=0.7f;cb=0.7f;}
                        else if (ef == 5  || (ef == 0 && bh > 0))      { cr=0.6f;cg=0;   cb=1; }
                        else if (ef == 4  || (ef == 0 && (en > 0 || player > 0))) { cr=1;cg=0;cb=0; }
                        else if (ef == 3  || (ef == 0 && bs > 0))      { cr=0;   cg=1;   cb=0; }
                        else if (ef == 2  || (ef == 0 && pl > 0))      { cr=0;   cg=0.8f;cb=1; }
                        else if (ef == 1  || (ef == 0 && sst > 0))     { cr=1;   cg=1;   cb=0; }

                        float scale = 0.15f;
                        if (quasar > 0) scale = 0.2f + sinf(pulse * 15.0f) * 0.08f;
                        else if (pul > 0) scale += sinf(pulse * 8.0f) * 0.05f;
                        else if (monster > 0) scale = 0.25f + sinf(pulse * 5.0f) * 0.05f;
                        scale *= ms;

                        if (is_my_q) {
                            float meScale = (0.4f + sinf(pulse * 6.0f) * 0.15f) * ms;
                            gdd_list_add_identity(&map, GDD_MESH_BOX, pxm, pym, pzm,
                                                  meScale, meScale, meScale,
                                                  1, 1, 1, 0.8f, 1, 0, GDD_FRAG_UNLIT, 0, 1);
                            if (val != 0) {
                                gdd_list_add_identity(&map, GDD_MESH_BOX, pxm, pym, pzm,
                                                      scale, scale, scale,
                                                      cr, cg, cb, 1.0f, 1, 0, GDD_FRAG_UNLIT, 0, 1);
                            }
                        } else if (val != 0) {
                            gdd_list_add_identity(&map, GDD_MESH_BOX, pxm, pym, pzm,
                                                  scale, scale, scale,
                                                  cr, cg, cb, 1.0f, 1, 0, GDD_FRAG_UNLIT, 0, 1);
                        }
                    }
                }
            }
        }
    }

    /* ---------------------------------------------------------------- */
    /* 10. Tactical statics: grid + starfield (map group)                */
    /* ---------------------------------------------------------------- */
    if (ctx->map_anim < 0.99f) {
        /* Parity with CPU path: hide grid when zoomed out or during jump */
        int grid_visible = ctx->show_grid
                        && ctx->camera_dist < 150.0f
                        && !(ctx->jump_arrival && ctx->jump_arrival->active);
        if (grid_visible) {
            int steps = (int)GDD_QUADRANT_SPAN / 2; /* 20, CPU parity */
            float hq = GDD_QUADRANT_SPAN / 2.0f;
            /* Half-thickness passed via metallic field (it.p.x).
             * 0.35 world-units stays >=1 pixel at cameraDist up to 150. */
            float ht = 0.35f;
            for (int i = 0; i <= steps; i++) {
                float p = -hq + i * GDD_GRID_STEP;
                for (int j = 0; j <= steps; j++) {
                    float q = -hq + j * GDD_GRID_STEP;
                    /* Lines parallel to Z */
                    gdd_list_add_identity(&map, GDD_MESH_LINE, p*ts, q*ts, -hq*ts,
                                          0, 0, GDD_QUADRANT_SPAN*ts,
                                          0, 1, 0.2f, 1.0f, 1, 0, GDD_FRAG_UNLIT, ht, 0);
                    /* Lines parallel to Y */
                    gdd_list_add_identity(&map, GDD_MESH_LINE, p*ts, -hq*ts, q*ts,
                                          0, GDD_QUADRANT_SPAN*ts, 0,
                                          0, 1, 0.2f, 1.0f, 1, 0, GDD_FRAG_UNLIT, ht, 0);
                    /* Lines parallel to X */
                    gdd_list_add_identity(&map, GDD_MESH_LINE, -hq*ts, p*ts, q*ts,
                                          GDD_QUADRANT_SPAN*ts, 0, 0,
                                          0, 1, 0.2f, 1.0f, 1, 0, GDD_FRAG_UNLIT, ht, 0);
                }
            }
        }
        for (int i = 0; i < ctx->star_count && ctx->stars; i++) {
            const GddStar *st = &ctx->stars[i];
            gdd_list_add_identity(&map, GDD_MESH_POINT,
                                  st->pos[0], st->pos[1], st->pos[2],
                                  st->scale, st->scale, st->scale,
                                  st->color[0], st->color[1], st->color[2], 1.0f,
                                  1, 0, GDD_FRAG_TWINKLE, 0, 1);
        }
    }

    if (ctx->dyn_count) *ctx->dyn_count = dyn.n;
    if (ctx->map_count) *ctx->map_count = map.n;
}

/* ================================================================== */
/* ================================================================== */
/* Part 2: Vulkan 1.4 device-side state (GPD core)                    */
/* ================================================================== */
/* ================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Forward: VulkanApp fields used by the device-side functions (the
 * full definition lives in include/spacegl_vulkan_types.h; here only
 * the GDD-relevant subset is accessed through a small accessor struct
 * filled by gdd_init from the owning VulkanApp). */

struct GddState {
    /* --- persistent device-side state --- */
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    uint32_t queue_family; /* graphics+compute family */

    /* compute side */
    VkDescriptorSetLayout ds_cull;  /* {0 inst, 1 vis, 2 cnt} */
    VkDescriptorSetLayout ds_exp;   /* {0 inst, 1 vis, 2 cnt, 3 verts} */
    VkDescriptorSetLayout ds_final; /* {0 indirect, 1 cnt} */
    VkPipelineLayout pl_cull;
    VkPipelineLayout pl_expand;
    VkPipelineLayout pl_final;
    VkPipeline pipe_cull;
    VkPipeline pipe_expand;
    VkPipeline pipe_final;

    /* scene side (dynamic rendering: no size dependence) */
    VkDescriptorSetLayout ds_scene; /* {0 verts} */
    VkPipelineLayout pl_scene;
    VkPipeline pipe_scene_opaque;
    VkPipeline pipe_scene_add;

    VkDescriptorPool desc_pool;

    /* MSAA state (CPU-path parity: the whole scene — quadrant cube
     * wireframe included — is drawn with app->msaaSamples, capped at
     * 4x, then resolved to the swapchain image).
     * In this API the dynamic-rendering sample count is defined by the
     * attachments, not by VkRenderingInfo: the color attachment below
     * is multisampled (transient) and carries a per-attachment resolve
     * onto the 1-sample swapchain image; the depth image and the scene
     * pipelines' rasterizationSamples carry the same count (the depth
     * attachment's samples must equal the pipeline's —
     * VUID-VkRenderingInfo-pDepthAttachment-06467). The CPU-driven
     * path keeps its own colorImage/depthImage. */
    VkSampleCountFlagBits msaa;
    VkImage colorImage;
    VkDeviceMemory colorMemory;
    VkImageView colorView;
    VkImage depthImage;
    VkDeviceMemory depthMemory;
    VkImageView depthView;

    /* per-frame ring (same triple-buffer invariant as the CPU path) */
    GddFrame frames[GDD_MAX_FRAMES];
    uint32_t current_frame;

    /* persistent starfield (same distribution as the CPU path) */
    GddStar stars[GDD_STAR_COUNT];

    /* resolved shader directory (build/shaders or /usr/share/spacegl/shaders) */
    char spv_dir[256];

    /* owner back-pointer (VulkanApp) */
    void *app;
};

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */
/* Read an entire file into a malloc'd buffer (NULL on error). */
static uint8_t *gdd_read_file(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *size_out = (size_t)sz;
    return buf;
}

/* Resolve the directory holding the SPIR-V binaries, in the same order
 * the CPU-driven path uses (build/shaders in the dev tree, then the
 * installed /usr/share/spacegl/shaders). The dev path is kept as last
 * resort so the load-error message stays actionable. */
static void gdd_resolve_spv_dir(GddState *g) {
    const char *candidates[2] = { "build/shaders", "/usr/share/spacegl/shaders" };
    for (int i = 0; i < 2; i++) {
        char probe[320];
        snprintf(probe, sizeof(probe), "%s/gdd_cull.comp.spv", candidates[i]);
        FILE *f = fopen(probe, "rb");
        if (f) {
            fclose(f);
            snprintf(g->spv_dir, sizeof(g->spv_dir), "%s", candidates[i]);
            return;
        }
    }
    snprintf(g->spv_dir, sizeof(g->spv_dir), "%s", candidates[0]);
}

/* Load one SPIR-V module from g->spv_dir (prints the path on failure). */
static VkShaderModule gdd_load_module(VkDevice device, const GddState *g,
                                      const char *name, bool *ok) {
    *ok = true;
    char path[320];
    snprintf(path, sizeof(path), "%s/%s", g->spv_dir, name);
    size_t sz = 0;
    uint8_t *code = gdd_read_file(path, &sz);
    if (!code) {
        fprintf(stderr, "[GDD] cannot load shader %s\n", path);
        *ok = false;
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo mi = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sz,
        .pCode = (const uint32_t *)code,
    };
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &mi, NULL, &m) != VK_SUCCESS) {
        fprintf(stderr, "[GDD] vkCreateShaderModule failed for %s\n", path);
        *ok = false;
    }
    free(code);
    return m;
}

/* Buffer creation with the physical device (kept in GddState). */
static bool gdd_create_buffer(VkDevice device, VkPhysicalDevice pd,
                              VkDeviceSize size, VkBufferUsageFlags use,
                              VkMemoryPropertyFlags props,
                              VkBuffer *buf, VkDeviceMemory *mem,
                              void **mapped /* may be NULL */) {
    VkBufferCreateInfo bi = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = use,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(device, &bi, NULL, buf) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, *buf, &mr);
    VkPhysicalDeviceMemoryProperties mprops;
    vkGetPhysicalDeviceMemoryProperties(pd, &mprops);
    uint32_t idx = VK_MAX_MEMORY_TYPES;
    for (uint32_t i = 0; i < mprops.memoryTypeCount; i++) {
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mprops.memoryTypes[i].propertyFlags & props) == props) {
            idx = i;
            break;
        }
    }
    if (idx == VK_MAX_MEMORY_TYPES) {
        vkDestroyBuffer(device, *buf, NULL);
        *buf = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = idx,
    };
    if (vkAllocateMemory(device, &ai, NULL, mem) != VK_SUCCESS) {
        vkDestroyBuffer(device, *buf, NULL);
        *buf = VK_NULL_HANDLE;
        return false;
    }
    if (vkBindBufferMemory(device, *buf, *mem, 0) != VK_SUCCESS) {
        vkFreeMemory(device, *mem, NULL);
        vkDestroyBuffer(device, *buf, NULL);
        *buf = VK_NULL_HANDLE;
        return false;
    }
    if (mapped) {
        if (vkMapMemory(device, *mem, 0, size, 0, mapped) != VK_SUCCESS) {
            vkFreeMemory(device, *mem, NULL);
            vkDestroyBuffer(device, *buf, NULL);
            *buf = VK_NULL_HANDLE;
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* depth attachment (MSAA — see GddState.msaa note)                    */
/* ------------------------------------------------------------------ */
static bool gdd_create_depth_image(GddState *g, VulkanApp *app) {
    VkDevice d = g->device;
    VkImageCreateInfo ii = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .extent = { app->swapChainExtent.width, app->swapChainExtent.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = g->msaa,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(d, &ii, NULL, &g->depthImage) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(d, g->depthImage, &mr);
    VkPhysicalDeviceMemoryProperties mprops;
    vkGetPhysicalDeviceMemoryProperties(g->physicalDevice, &mprops);
    uint32_t idx = VK_MAX_MEMORY_TYPES;
    for (uint32_t i = 0; i < mprops.memoryTypeCount; i++)
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mprops.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            idx = i;
            break;
        }
    if (idx == VK_MAX_MEMORY_TYPES) return false;
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = idx,
    };
    if (vkAllocateMemory(d, &ai, NULL, &g->depthMemory) != VK_SUCCESS) return false;
    if (vkBindImageMemory(d, g->depthImage, g->depthMemory, 0) != VK_SUCCESS) return false;
    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = g->depthImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
    };
    if (vkCreateImageView(d, &vi, NULL, &g->depthView) != VK_SUCCESS) return false;
    return true;
}

/* Transient MSAA color attachment: rendered by the scene passes, then
 * resolved onto the swapchain image through the color attachment's
 * per-attachment resolve fields (see gdd_record). Same shape as the
 * CPU-driven path's createColorResources. */
static bool gdd_create_msaa_color_image(GddState *g, VulkanApp *app) {
    VkDevice d = g->device;
    VkImageCreateInfo ii = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = app->swapChainImageFormat,
        .extent = { app->swapChainExtent.width, app->swapChainExtent.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = g->msaa,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(d, &ii, NULL, &g->colorImage) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(d, g->colorImage, &mr);
    VkPhysicalDeviceMemoryProperties mprops;
    vkGetPhysicalDeviceMemoryProperties(g->physicalDevice, &mprops);
    uint32_t idx = VK_MAX_MEMORY_TYPES;
    for (uint32_t i = 0; i < mprops.memoryTypeCount; i++)
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mprops.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            idx = i;
            break;
        }
    if (idx == VK_MAX_MEMORY_TYPES) return false;
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = idx,
    };
    if (vkAllocateMemory(d, &ai, NULL, &g->colorMemory) != VK_SUCCESS) return false;
    if (vkBindImageMemory(d, g->colorImage, g->colorMemory, 0) != VK_SUCCESS) return false;
    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = g->colorImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = app->swapChainImageFormat,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    if (vkCreateImageView(d, &vi, NULL, &g->colorView) != VK_SUCCESS) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* queue selection                                                     */
/* ------------------------------------------------------------------ */
bool gdd_pick_queue(VkPhysicalDevice pd, uint32_t *family_out) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, NULL);
    if (count == 0) return false;
    VkQueueFamilyProperties *qf = malloc(sizeof(VkQueueFamilyProperties) * count);
    if (!qf) return false;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, qf);
    bool found = false;
    for (uint32_t i = 0; i < count; i++) {
        if ((qf[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
            (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
            *family_out = i;
            found = true;
            break;
        }
    }
    free(qf);
    return found;
}

/* ------------------------------------------------------------------ */
/* per-frame ring creation                                             */
/* ------------------------------------------------------------------ */
static bool gdd_create_frame_buffers(GddState *g, VulkanApp *app, uint32_t i) {
    (void)app;
    GddFrame *f = &g->frames[i];
    VkDevice d = g->device;
    VkPhysicalDevice pd = g->physicalDevice;
    bool ok = true;
    ok &= gdd_create_buffer(d, pd, GDD_DYN_MAX * GDD_INSTANCE_STRIDE,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &f->dyn_inst, &f->dyn_mem, &f->dyn_ptr);
    ok &= gdd_create_buffer(d, pd, GDD_MAP_MAX * GDD_INSTANCE_STRIDE,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &f->map_inst, &f->map_mem, &f->map_ptr);
    ok &= gdd_create_buffer(d, pd, GDD_DYN_MAX * sizeof(uint32_t),
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            &f->dyn_vis, &f->dvis_mem, NULL);
    ok &= gdd_create_buffer(d, pd, GDD_MAP_MAX * sizeof(uint32_t),
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            &f->map_vis, &f->mvis_mem, NULL);
    ok &= gdd_create_buffer(d, pd, GDD_VERTEX_CAPACITY * GDD_VERTEX_STRIDE,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            &f->verts, &f->vert_mem, NULL);
    ok &= gdd_create_buffer(d, pd, 2 * sizeof(VkDrawIndirectCommand),
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &f->indirect, &f->ind_mem, NULL);
    ok &= gdd_create_buffer(d, pd, GDD_COUNTS_STRIDE,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &f->counters, &f->cnt_mem, NULL);
    if (!ok) return false;
    /* one command buffer per slot (same triple-buffer invariant as CPU) */
    VkCommandBufferAllocateInfo cai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = app->commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(d, &cai, &f->cmd) != VK_SUCCESS) return false;
    return true;
}

/* Descriptor set allocation for one frame slot (6 sets, buffers are
 * stable per slot so the sets are written once at creation). */
static bool gdd_create_frame_descs(GddState *g, uint32_t i) {
    GddFrame *f = &g->frames[i];
    VkDevice d = g->device;
    VkDescriptorSet sets[6];
    VkDescriptorSetLayout layouts[6] = {
        g->ds_cull, g->ds_cull, g->ds_exp, g->ds_exp, g->ds_final, g->ds_scene,
    };
    VkDescriptorSetAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = g->desc_pool,
        .descriptorSetCount = 6,
        .pSetLayouts = layouts,
    };
    if (vkAllocateDescriptorSets(d, &ai, sets) != VK_SUCCESS) return false;
    f->desc_cull_dyn = sets[0];
    f->desc_cull_map = sets[1];
    f->desc_exp_dyn  = sets[2];
    f->desc_exp_map  = sets[3];
    f->desc_final    = sets[4];
    f->desc_scene    = sets[5];

    VkDescriptorBufferInfo bi[4];
    bi[0].buffer = f->dyn_inst;  bi[0].offset = 0; bi[0].range = GDD_DYN_MAX * GDD_INSTANCE_STRIDE;
    bi[1].buffer = f->dyn_vis;   bi[1].offset = 0; bi[1].range = GDD_DYN_MAX * sizeof(uint32_t);
    bi[2].buffer = f->counters;  bi[2].offset = 0; bi[2].range = GDD_COUNTS_STRIDE;
    bi[3].buffer = f->verts;     bi[3].offset = 0; bi[3].range = GDD_VERTEX_CAPACITY * GDD_VERTEX_STRIDE;
    VkDescriptorBufferInfo bmap[4];
    bmap[0].buffer = f->map_inst; bmap[0].offset = 0; bmap[0].range = GDD_MAP_MAX * GDD_INSTANCE_STRIDE;
    bmap[1].buffer = f->map_vis;  bmap[1].offset = 0; bmap[1].range = GDD_MAP_MAX * sizeof(uint32_t);
    bmap[2] = bi[2];
    bmap[3] = bi[3];

    VkWriteDescriptorSet w[24];
    int n = 0;
    /* cull dyn: 0 inst, 1 vis, 2 cnt */
    for (int k = 0; k < 3; k++) {
        w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[n].dstSet = f->desc_cull_dyn; w[n].dstBinding = k; w[n].dstArrayElement = 0;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[n].descriptorCount = 1; w[n].pBufferInfo = &bi[k];
        n++;
    }
    /* cull map */
    for (int k = 0; k < 3; k++) {
        w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[n].dstSet = f->desc_cull_map; w[n].dstBinding = k; w[n].dstArrayElement = 0;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[n].descriptorCount = 1; w[n].pBufferInfo = &bmap[k];
        n++;
    }
    /* expand dyn: 0 inst, 1 vis, 2 cnt, 3 verts */
    for (int k = 0; k < 4; k++) {
        w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[n].dstSet = f->desc_exp_dyn; w[n].dstBinding = k; w[n].dstArrayElement = 0;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[n].descriptorCount = 1; w[n].pBufferInfo = &bi[k];
        n++;
    }
    /* expand map */
    for (int k = 0; k < 4; k++) {
        w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[n].dstSet = f->desc_exp_map; w[n].dstBinding = k; w[n].dstArrayElement = 0;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[n].descriptorCount = 1; w[n].pBufferInfo = &bmap[k];
        n++;
    }
    /* final: 0 indirect, 1 cnt */
    VkDescriptorBufferInfo bfin[2];
    bfin[0].buffer = f->indirect; bfin[0].offset = 0; bfin[0].range = 2 * sizeof(VkDrawIndirectCommand);
    bfin[1] = bi[2];
    for (int k = 0; k < 2; k++) {
        w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[n].dstSet = f->desc_final; w[n].dstBinding = k; w[n].dstArrayElement = 0;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[n].descriptorCount = 1; w[n].pBufferInfo = &bfin[k];
        n++;
    }
    /* scene: 0 verts */
    w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[n].dstSet = f->desc_scene; w[n].dstBinding = 0; w[n].dstArrayElement = 0;
    w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[n].descriptorCount = 1; w[n].pBufferInfo = &bi[3];
    n++;

    vkUpdateDescriptorSets(d, n, w, 0, NULL);
    return true;
}

/* ------------------------------------------------------------------ */
/* pipelines                                                           */
/* ------------------------------------------------------------------ */
static bool gdd_create_compute_pipelines(GddState *g) {
    VkDevice d = g->device;
    bool ok = true;
    VkShaderModule mods[3] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    const char *names[3] = { "gdd_cull.comp.spv", "gdd_expand.comp.spv", "gdd_final.comp.spv" };
    VkPipelineLayout *pls[3] = { &g->pl_cull, &g->pl_expand, &g->pl_final };
    VkPipeline *pipes[3] = { &g->pipe_cull, &g->pipe_expand, &g->pipe_final };
    VkDescriptorSetLayout *dsls[3] = { &g->ds_cull, &g->ds_exp, &g->ds_final };
    uint32_t bindings[3] = { 3, 4, 2 };

    for (int i = 0; i < 3; i++) {
        bool mok = true;
        mods[i] = gdd_load_module(d, g, names[i], &mok);
        if (!mok) { ok = false; break; }
        /* descriptor layout: N storage bindings (compute) */
        VkDescriptorSetLayoutBinding lb[4];
        for (uint32_t b = 0; b < bindings[i]; b++) {
            lb[b] = (VkDescriptorSetLayoutBinding){
                .binding = b,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            };
        }
        VkDescriptorSetLayoutCreateInfo dl = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = bindings[i],
            .pBindings = lb,
        };
        if (vkCreateDescriptorSetLayout(d, &dl, NULL, dsls[i]) != VK_SUCCESS) { ok = false; break; }
        /* pipeline layout: set 0 + 32 B compute push constants */
        VkPushConstantRange pcr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, GDD_PC_STRIDE };
        VkPipelineLayoutCreateInfo plc = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = dsls[i],
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pcr,
        };
        if (vkCreatePipelineLayout(d, &plc, NULL, pls[i]) != VK_SUCCESS) { ok = false; break; }
        /* compute pipeline */
        VkPipelineShaderStageCreateInfo stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = mods[i],
            .pName = "main",
        };
        VkComputePipelineCreateInfo cpc = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = stage,
            .layout = *pls[i],
        };
        if (vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &cpc, NULL, pipes[i]) != VK_SUCCESS) { ok = false; break; }
    }
    for (int i = 0; i < 3; i++) if (mods[i]) vkDestroyShaderModule(d, mods[i], NULL);
    return ok;
}

static bool gdd_create_scene_pipelines(GddState *g) {
    VkDevice d = g->device;
    bool ok = true;
    VkShaderModule vmod = gdd_load_module(d, g, "gdd_scene.vert.spv", &ok);
    if (!ok) return false;
    bool fok = true;
    VkShaderModule fmod = gdd_load_module(d, g, "gdd_scene.frag.spv", &fok);
    if (!fok) { vkDestroyShaderModule(d, vmod, NULL); return false; }

    /* scene descriptor layout: 1 storage binding (vertex stage) */
    VkDescriptorSetLayoutBinding lb = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    };
    VkDescriptorSetLayoutCreateInfo dl = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &lb,
    };
    if (vkCreateDescriptorSetLayout(d, &dl, NULL, &g->ds_scene) != VK_SUCCESS) return false;

    VkPushConstantRange pcr = {
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, GDD_SCENE_PC_STRIDE,
    };
    VkPipelineLayoutCreateInfo plc = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &g->ds_scene,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcr,
    };
    if (vkCreatePipelineLayout(d, &plc, NULL, &g->pl_scene) != VK_SUCCESS) return false;

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vmod, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fmod, .pName = "main" },
    };

    VkPipelineVertexInputStateCreateInfo vin = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .vertexAttributeDescriptionCount = 0,
    };
    VkPipelineInputAssemblyStateCreateInfo ias = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkDynamicState dyn_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dyn_states,
    };
    VkPipelineViewportStateCreateInfo vpst = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rast = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE, /* GDD: double-sided by design */
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    /* MSAA (CPU-path parity): the sample count must equal the
     * multisampled color/depth attachments of the dynamic rendering —
     * in this API the rendering's sample count comes from the
     * attachments themselves. */
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = g->msaa,
    };

    VkPipeline pipelines[2];
    for (int p = 0; p < 2; p++) {
        VkPipelineDepthStencilStateCreateInfo depst = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = p == 0 ? VK_TRUE : VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
        };
        VkPipelineColorBlendAttachmentState cb = {
            .blendEnable = p == 0 ? VK_FALSE : VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };
        VkPipelineColorBlendStateCreateInfo cbs = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &cb,
        };
        VkFormat color_fmt = VK_FORMAT_B8G8R8A8_UNORM;
        VkFormat depth_fmt = VK_FORMAT_D32_SFLOAT;
        VkPipelineRenderingCreateInfo rendering = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &color_fmt,
            .depthAttachmentFormat = depth_fmt,
        };
        VkGraphicsPipelineCreateInfo gi = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2,
            .pStages = stages,
            .pVertexInputState = &vin,
            .pInputAssemblyState = &ias,
            .pViewportState = &vpst,
            .pDynamicState = &dyn,
            .pRasterizationState = &rast,
            .pMultisampleState = &ms,
            .pDepthStencilState = &depst,
            .pColorBlendState = &cbs,
            .layout = g->pl_scene,
            .pNext = &rendering,
        };
        if (vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &gi, NULL, &pipelines[p]) != VK_SUCCESS) return false;
    }
    g->pipe_scene_opaque = pipelines[0];
    g->pipe_scene_add = pipelines[1];
    vkDestroyShaderModule(d, vmod, NULL);
    vkDestroyShaderModule(d, fmod, NULL);
    return true;
}

/* ------------------------------------------------------------------ */
/* starfield (same distribution as the CPU path createStarfield)       */
/* ------------------------------------------------------------------ */
static void gdd_build_starfield(GddState *g) {
    for (uint32_t s = 0; s < GDD_STAR_COUNT; s++) {
        GddStar *st = &g->stars[s];
        float theta = (float)(rand() % 3600) * 0.1f * M_PI / 180.0f;
        float phi = (float)(rand() % 1800) * 0.1f * M_PI / 180.0f;
        float r = 120.0f + (float)(rand() % 1000) * 0.1f;
        st->pos[0] = r * sinf(phi) * cosf(theta);
        st->pos[1] = r * cosf(phi);
        st->pos[2] = r * sinf(phi) * sinf(theta);
        st->color[0] = 0.4f + (float)(rand() % 60) / 100.0f;
        st->color[1] = 0.4f + (float)(rand() % 60) / 100.0f;
        st->color[2] = 0.4f + (float)(rand() % 60) / 100.0f;
        st->scale = 0.1f + (float)(rand() % 50) * 0.01f;
    }
}

/* ------------------------------------------------------------------ */
/* gdd_init / gdd_cleanup                                              */
/* ------------------------------------------------------------------ */
bool gdd_init(VulkanApp *app) {
    GddState *g = calloc(1, sizeof(GddState));
    if (!g) return false;
    g->device = app->device;
    g->physicalDevice = app->physicalDevice;
    g->app = app;
    /* MSAA count shared by both architectures (capped at 4x in
     * pickPhysicalDevice) — must be set before the images/pipelines. */
    g->msaa = app->msaaSamples ? app->msaaSamples : VK_SAMPLE_COUNT_1_BIT;
    if (!gdd_pick_queue(app->physicalDevice, &g->queue_family)) {
        fprintf(stderr, "[GDD] no graphics+compute queue family: falling back to CPU-driven path\n");
        free(g);
        return false;
    }
    gdd_resolve_spv_dir(g);
    gdd_build_starfield(g);

    bool ok = gdd_create_compute_pipelines(g);
    ok &= gdd_create_scene_pipelines(g);
    ok &= gdd_create_depth_image(g, app);
    ok &= gdd_create_msaa_color_image(g, app);
    if (!ok) {
        fprintf(stderr, "[GDD] pipeline creation failed: falling back to CPU-driven path\n");
        gdd_cleanup(app);
        return false;
    }

    /* descriptor pool: 3 frames x 6 sets x 4 bindings (storage) */
    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GDD_MAX_FRAMES * 6 * 4 + 8 };
    VkDescriptorPoolCreateInfo dpc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = GDD_MAX_FRAMES * 6,
        .poolSizeCount = 1,
        .pPoolSizes = &ps,
    };
    if (vkCreateDescriptorPool(g->device, &dpc, NULL, &g->desc_pool) != VK_SUCCESS) {
        fprintf(stderr, "[GDD] descriptor pool creation failed\n");
        gdd_cleanup(app);
        return false;
    }

    for (uint32_t i = 0; i < GDD_MAX_FRAMES; i++) {
        if (!gdd_create_frame_buffers(g, app, i)) {
            fprintf(stderr, "[GDD] per-frame buffer creation failed (slot %u)\n", i);
            gdd_cleanup(app);
            return false;
        }
        if (!gdd_create_frame_descs(g, i)) {
            fprintf(stderr, "[GDD] per-frame descriptor creation failed (slot %u)\n", i);
            gdd_cleanup(app);
            return false;
        }
    }

    app->gdd = g;
    printf("[GDD] GPU-driven path initialized (queue family %u, %u instance slots, %u vertex capacity, %u sample MSAA)\n",
           g->queue_family, GDD_DYN_MAX, GDD_VERTEX_CAPACITY, (unsigned)g->msaa);
    return true;
}

void gdd_cleanup(VulkanApp *app) {
    GddState *g = (GddState *)app->gdd;
    if (!g) return;
    VkDevice d = g->device;
    if (g->colorView) vkDestroyImageView(d, g->colorView, NULL);
    if (g->colorImage) vkDestroyImage(d, g->colorImage, NULL);
    if (g->colorMemory) vkFreeMemory(d, g->colorMemory, NULL);
    if (g->depthView) vkDestroyImageView(d, g->depthView, NULL);
    if (g->depthImage) vkDestroyImage(d, g->depthImage, NULL);
    if (g->depthMemory) vkFreeMemory(d, g->depthMemory, NULL);
    for (uint32_t i = 0; i < GDD_MAX_FRAMES; i++) {
        GddFrame *f = &g->frames[i];
        if (f->cmd) vkFreeCommandBuffers(d, app->commandPool, 1, &f->cmd);
        if (f->dyn_inst) { if (f->dyn_ptr) vkUnmapMemory(d, f->dyn_mem); vkDestroyBuffer(d, f->dyn_inst, NULL); vkFreeMemory(d, f->dyn_mem, NULL); }
        if (f->map_inst) { if (f->map_ptr) vkUnmapMemory(d, f->map_mem); vkDestroyBuffer(d, f->map_inst, NULL); vkFreeMemory(d, f->map_mem, NULL); }
        if (f->dyn_vis)  { vkDestroyBuffer(d, f->dyn_vis, NULL); vkFreeMemory(d, f->dvis_mem, NULL); }
        if (f->map_vis)  { vkDestroyBuffer(d, f->map_vis, NULL); vkFreeMemory(d, f->mvis_mem, NULL); }
        if (f->verts)    { vkDestroyBuffer(d, f->verts, NULL); vkFreeMemory(d, f->vert_mem, NULL); }
        if (f->indirect) { vkDestroyBuffer(d, f->indirect, NULL); vkFreeMemory(d, f->ind_mem, NULL); }
        if (f->counters) { vkDestroyBuffer(d, f->counters, NULL); vkFreeMemory(d, f->cnt_mem, NULL); }
    }
    vkDestroyDescriptorPool(d, g->desc_pool, NULL);
    if (g->pipe_scene_opaque) vkDestroyPipeline(d, g->pipe_scene_opaque, NULL);
    if (g->pipe_scene_add) vkDestroyPipeline(d, g->pipe_scene_add, NULL);
    if (g->pl_scene) vkDestroyPipelineLayout(d, g->pl_scene, NULL);
    if (g->ds_scene) vkDestroyDescriptorSetLayout(d, g->ds_scene, NULL);
    if (g->pipe_cull) vkDestroyPipeline(d, g->pipe_cull, NULL);
    if (g->pipe_expand) vkDestroyPipeline(d, g->pipe_expand, NULL);
    if (g->pipe_final) vkDestroyPipeline(d, g->pipe_final, NULL);
    if (g->pl_cull) vkDestroyPipelineLayout(d, g->pl_cull, NULL);
    if (g->pl_expand) vkDestroyPipelineLayout(d, g->pl_expand, NULL);
    if (g->pl_final) vkDestroyPipelineLayout(d, g->pl_final, NULL);
    if (g->ds_cull) vkDestroyDescriptorSetLayout(d, g->ds_cull, NULL);
    if (g->ds_exp) vkDestroyDescriptorSetLayout(d, g->ds_exp, NULL);
    if (g->ds_final) vkDestroyDescriptorSetLayout(d, g->ds_final, NULL);
    app->gdd = NULL;
    free(g);
}

/* ================================================================== */
/* ================================================================== */
/* Part 3: the per-frame pipeline (build -> record -> draw)           */
/* ================================================================== */
/* ================================================================== */

#include <stdatomic.h>
#include <time.h>

/* BRIDGE_CAMERA_OFFSET_Y * SCALE_SHIP of the CPU path (kept here so the
 * GDD translation stays free of project-header dependencies). */
#define GDD_BRIDGE_CAMERA_OFFSET (0.30f * 0.45f)

/* Vision cull margin: the quadrant spans [-20,20]^3 (|p| <= 34.6), so
 * cameraDist + 60 always contains the whole scene for any camera
 * distance (min 10), keeping the GDD cull a pure optimization that can
 * never hide what the CPU path would draw. */
#define GDD_CULL_MARGIN 60.0f

/* Minimum screen-space width (pixels, total) enforced on the GDD_MESH_LINE
 * tube cross-section (pushed to the expand pass as GddPC.line_min_wu, see
 * gdd_record). The GDD scene pipeline is triangle-based (no line
 * rasterizer): without this floor the sub-pixel quadrant-cube edges
 * rasterize with stochastic sample coverage — the dashed, flickering
 * wireframe lines (the pipeline now MSAA's like the CPU path, but each
 * sample still covers the thin triangle independently). 1.5 px matches
 * the visual weight of the CPU-driven 1-px MSAA lines. */
#define GDD_LINE_MIN_PX 1.5f

/* ------------------------------------------------------------------ */
/* View matrix + camera position — mirrors drawFrame() of the         */
/* CPU-driven path (orbital view, bridge view and the blend) so the   */
/* two architectures produce the exact same camera.                    */
/* ------------------------------------------------------------------ */
static void gdd_compute_view(VulkanApp *app, mat4 view, float cam_world[3]) {
    /* 1. Standard orbital view (tactical) */
    mat4 m_std; mat4_identity(m_std);
    mat4_rotate(m_std, app->angleY * M_PI / 180.0f, (vec3){0, 1, 0});
    mat4_rotate(m_std, app->angleX * M_PI / 180.0f, (vec3){1, 0, 0});
    mat4 T_cam; mat4_translate(T_cam, (vec3){0, 0, -app->cameraDist});
    mat4_multiply(m_std, T_cam, m_std);

    /* Camera world position: C * (R*T) = 0  =>  C * R = (0,0,dist)
     *  =>  C[i] = dist * R[i][2] (R = top-left 3x3, unchanged by T). */
    float cam_orb[3] = {
        app->cameraDist * m_std[0][2],
        app->cameraDist * m_std[1][2],
        app->cameraDist * m_std[2][2],
    };

    /* 2. Bridge view (first person) */
    mat4 m_brg; mat4_identity(m_brg);
    float cam_brg[3] = {0.0f, 0.0f, 0.0f};
    if (app->bridgeAnim > 0.001f) {
        float tactScale = 1.0f - app->mapAnim;
        float px = (app->smoothObjs[0].x - 20.0f) * tactScale;
        float py = (app->smoothObjs[0].z - 20.0f) * tactScale;
        float pz = (20.0f - app->smoothObjs[0].y) * tactScale;
        float ph = app->smoothObjs[0].h;
        float pm = app->smoothObjs[0].m;

        mat4 R_ship; mat4_identity(R_ship);
        mat4_rotate(R_ship, 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});
        mat4_rotate(R_ship, -ph * M_PI / 180.0f, (vec3){0, 1, 0});
        float h_rad = ph * M_PI / 180.0f;
        mat4_rotate(R_ship, pm * M_PI / 180.0f, (vec3){cosf(h_rad), 0, -sinf(h_rad)});

        float ly = (app->showBridge >= 11)
                 ? (-GDD_BRIDGE_CAMERA_OFFSET * tactScale)
                 : ( GDD_BRIDGE_CAMERA_OFFSET * tactScale);
        float wx = ly * R_ship[1][0] + px;
        float wy = ly * R_ship[1][1] + py;
        float wz = ly * R_ship[1][2] + pz;
        cam_brg[0] = wx; cam_brg[1] = wy; cam_brg[2] = wz;

        mat4 R_base; mat4_identity(R_base);
        mat4_rotate(R_base, 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});
        int mode = app->showBridge % 10;
        if (mode == 2) mat4_rotate(R_base, M_PI / 2.0f, (vec3){0, 1, 0});
        else if (mode == 3) mat4_rotate(R_base, -M_PI / 2.0f, (vec3){0, 1, 0});
        else if (mode == 4) mat4_rotate(R_base, M_PI / 4.0f, (vec3){0, 0, 1});
        else if (mode == 5) mat4_rotate(R_base, -M_PI / 4.0f, (vec3){0, 0, 1});
        else if (mode == 6) mat4_rotate(R_base, M_PI, (vec3){0, 1, 0});

        mat4 R_cam_world;
        mat4_multiply(R_base, R_ship, R_cam_world);

        mat4 R_inv; mat4_identity(R_inv);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                R_inv[i][j] = R_cam_world[j][i];
        mat4 T_inv; mat4_translate(T_inv, (vec3){-wx, -wy, -wz});
        mat4_multiply(T_inv, R_inv, m_brg);
    }

    /* 3. Final view interpolation (+ the camera position follows it) */
    if (app->bridgeAnim <= 0.001f) {
        memcpy(view, m_std, sizeof(mat4));
        memcpy(cam_world, cam_orb, sizeof(cam_orb));
    } else if (app->bridgeAnim >= 0.999f) {
        memcpy(view, m_brg, sizeof(mat4));
        memcpy(cam_world, cam_brg, sizeof(cam_brg));
    } else {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                view[i][j] = m_std[i][j] * (1.0f - app->bridgeAnim)
                            + m_brg[i][j] * app->bridgeAnim;
        for (int i = 0; i < 3; i++)
            cam_world[i] = cam_orb[i] * (1.0f - app->bridgeAnim)
                         + cam_brg[i] * app->bridgeAnim;
    }
}

/* ------------------------------------------------------------------ */
/* gdd_build_frame — the ONE small per-frame upload of the GDD path.  */
/* Fills the current slot's host-visible instance lists from the shm  */
/* state (classification + effects + statics + map mode).             */
/* ------------------------------------------------------------------ */
void gdd_build_frame(VulkanApp *app, float pulse) {
    GddState *g = (GddState *)app->gdd;
    GddFrame *f = &g->frames[g->current_frame];

    GddBuildCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.objs = app->smoothObjs;
    ctx.beams = app->activeBeams;
    ctx.booms = app->activeBooms;
    ctx.dismantles = app->activeDismantles;
    ctx.torps = app->activeTorps;
    ctx.jump_arrival = &app->jumpArrival;
    ctx.wormhole = &app->departureWormhole;
    ctx.shield_timers = app->shieldHitTimers;
    ctx.map_anim = app->mapAnim;
    ctx.map_filter = app->mapFilter;
    ctx.pulse = pulse;
    ctx.camera_dist = app->cameraDist;
    ctx.stars = g->stars;
    ctx.star_count = (int)GDD_STAR_COUNT;
    ctx.dyn_out = (GddInstance *)f->dyn_ptr;
    ctx.dyn_cap = GDD_DYN_MAX;
    ctx.dyn_count = &f->dyn_count;
    ctx.map_out = (GddInstance *)f->map_ptr;
    ctx.map_cap = GDD_MAP_MAX;
    ctx.map_count = &f->map_count;

    if (app->shm) {
        int r_idx = atomic_load_explicit(&app->shm->read_index, memory_order_acquire);
        GameState *st = &app->shm->buffers[r_idx];

        int types[MAX_OBJECTS], factions[MAX_OBJECTS], ship_classes[MAX_OBJECTS];
        int cloaked[MAX_OBJECTS], active[MAX_OBJECTS], platings[MAX_OBJECTS], ids[MAX_OBJECTS];
        int n = st->object_count;
        if (n > MAX_OBJECTS) n = MAX_OBJECTS;
        for (int o = 0; o < n; o++) {
            SharedObject *obj = &st->objects[o];
            types[o] = obj->type;
            factions[o] = obj->faction;
            ship_classes[o] = obj->ship_class;
            cloaked[o] = obj->is_cloaked;
            active[o] = obj->active;
            platings[o] = obj->plating;
            ids[o] = obj->id;
        }
        ctx.types = types;
        ctx.factions = factions;
        ctx.ship_classes = ship_classes;
        ctx.cloaked = cloaked;
        ctx.active = active;
        ctx.platings = platings;
        ctx.ids = ids;
        ctx.object_count = n;
        ctx.player_q[0] = st->shm_q[0];
        ctx.player_q[1] = st->shm_q[1];
        ctx.player_q[2] = st->shm_q[2];
        ctx.show_axes = st->shm_show_axes;
        ctx.show_grid = st->shm_show_grid;
        ctx.galaxy = &app->shm->shm_galaxy[0][0][0];
        ctx.galaxy_size = 40; /* shm_galaxy[41][41][41], coords 1..40 */
    }

    gdd_build_instances(&ctx);
}

/* ------------------------------------------------------------------ */
/* Small synchronization2 helper (one memory barrier + dep info).     */
/* ------------------------------------------------------------------ */
static void gdd_mem_barrier(VkCommandBuffer cb,
                            VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                            VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkMemoryBarrier2 bar = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    bar.srcStageMask = src_stage;
    bar.srcAccessMask = src_access;
    bar.dstStageMask = dst_stage;
    bar.dstAccessMask = dst_access;
    VkDependencyInfo dep = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                             .memoryBarrierCount = 1, .pMemoryBarriers = &bar };
    vkCmdPipelineBarrier2(cb, &dep);
}

/* ------------------------------------------------------------------ */
/* gdd_record — the GPU side of the architecture. One command buffer: */
/*   fill counters -> cull (2 groups) -> expand (2 groups) -> final   */
/*   -> barriers -> dynamic rendering: 1 DrawIndirect per pass.       */
/* ------------------------------------------------------------------ */
void gdd_record(VkCommandBuffer cb, VulkanApp *app, uint32_t image_idx) {
    GddState *g = (GddState *)app->gdd;
    GddFrame *f = &g->frames[g->current_frame];

    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                    .flags = 0 };
    vkBeginCommandBuffer(cb, &bi);

    /* --- 1. zero the per-frame GPU counters (GPU-side fill: the CPU   */
    /*        never touches the counters after creation) -------------- */
    vkCmdFillBuffer(cb, f->counters, 0, GDD_COUNTS_STRIDE, 0);
    gdd_mem_barrier(cb,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

    /* --- 2. cull: one dispatch per group (same pipeline, the group    */
    /*        is selected through push constants) ---------------------- */
    GddPC pc;
    memset(&pc, 0, sizeof(pc));
    pc.cull_center[0] = 0.0f; pc.cull_center[1] = 0.0f; pc.cull_center[2] = 0.0f;
    pc.cull_radius = app->cameraDist + GDD_CULL_MARGIN;
    pc.capacity = GDD_VERTEX_CAPACITY;

    /* Screen-space floor for the GDD_MESH_LINE tubes (see GDD_LINE_MIN_PX):
     * world units per pixel at the current camera, so a line is never thinner
     * than GDD_LINE_MIN_PX pixels. The GDD rasterizer is triangle-based (no
     * 1-px LINE_LIST primitive) and a sub-pixel tube cross-section rasterizes
     * with stochastic coverage — the dashed, flickering quadrant cube lines.
     * The CPU-driven path gets the same guarantee for free from the
     * fixed-function line rasterizer + MSAA. Uses the ACTUAL camera position
     * (orbital / bridge blend), not cameraDist alone. */
    float fov = 45.0f * (1.0f - app->bridgeAnim) + 65.0f * app->bridgeAnim;
    mat4 view; float cam_world[3];
    gdd_compute_view(app, view, cam_world);
    float cam_dist = sqrtf(cam_world[0] * cam_world[0] +
                           cam_world[1] * cam_world[1] +
                           cam_world[2] * cam_world[2]);
    if (cam_dist < 1.0f) cam_dist = 1.0f;
    float wu_per_px = 2.0f * cam_dist * tanf(fov * M_PI / 360.0f)
                    / (float)app->swapChainExtent.height;
    pc.line_min_wu = 0.5f * GDD_LINE_MIN_PX * wu_per_px;

    uint32_t map_wg = (f->map_count + GDD_WORKGROUP - 1u) / GDD_WORKGROUP;
    uint32_t dyn_wg = (f->dyn_count + GDD_WORKGROUP - 1u) / GDD_WORKGROUP;

    if (map_wg > 0) {
        pc.list_count = f->map_count;
        pc.group = 1;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe_cull);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl_cull, 0, 1,
                                &f->desc_cull_map, 0, NULL);
        vkCmdPushConstants(cb, g->pl_cull, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           GDD_PC_STRIDE, &pc);
        vkCmdDispatch(cb, map_wg, 1, 1);
    }
    if (dyn_wg > 0) {
        pc.list_count = f->dyn_count;
        pc.group = 0;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe_cull);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl_cull, 0, 1,
                                &f->desc_cull_dyn, 0, NULL);
        vkCmdPushConstants(cb, g->pl_cull, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           GDD_PC_STRIDE, &pc);
        vkCmdDispatch(cb, dyn_wg, 1, 1);
    }

    gdd_mem_barrier(cb,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

    /* --- 3. expand: same dispatch shape (the entry guards on the      */
    /*        visible count, only known through the counters) ---------- */
    /* --- Passaggio 3A: Espansione Opaque (pc.pad = 0) --- */
    /* --- Passaggio 3A: Espansione Opaque (pc.pad = 0) --- */
    pc.pad = 0u;
    if (map_wg > 0) {
        pc.list_count = f->map_count; pc.group = 1;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe_expand);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl_expand, 0, 1, &f->desc_exp_map, 0, NULL);
        vkCmdPushConstants(cb, g->pl_expand, VK_SHADER_STAGE_COMPUTE_BIT, 0, GDD_PC_STRIDE, &pc);
        vkCmdDispatch(cb, map_wg, 1, 1);
    }
    if (dyn_wg > 0) {
        pc.list_count = f->dyn_count; pc.group = 0;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe_expand);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl_expand, 0, 1, &f->desc_exp_dyn, 0, NULL);
        vkCmdPushConstants(cb, g->pl_expand, VK_SHADER_STAGE_COMPUTE_BIT, 0, GDD_PC_STRIDE, &pc);
        vkCmdDispatch(cb, dyn_wg, 1, 1);
    }

    /* Barriera di sincronizzazione tra opaque e additive */
    gdd_mem_barrier(cb,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

    /* --- Passaggio 3B: Espansione Additive (pc.pad = 1) --- */
    pc.pad = 1u;
    if (map_wg > 0) {
        pc.list_count = f->map_count; pc.group = 1;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe_expand);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl_expand, 0, 1, &f->desc_exp_map, 0, NULL);
        vkCmdPushConstants(cb, g->pl_expand, VK_SHADER_STAGE_COMPUTE_BIT, 0, GDD_PC_STRIDE, &pc);
        vkCmdDispatch(cb, map_wg, 1, 1);
    }
    if (dyn_wg > 0) {
        pc.list_count = f->dyn_count; pc.group = 0;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe_expand);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl_expand, 0, 1, &f->desc_exp_dyn, 0, NULL);
        vkCmdPushConstants(cb, g->pl_expand, VK_SHADER_STAGE_COMPUTE_BIT, 0, GDD_PC_STRIDE, &pc);
        vkCmdDispatch(cb, dyn_wg, 1, 1);
    }

    /* --- 4. final: publish the two VkDrawIndirectCommands ------------ */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe_final);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl_final, 0, 1,
                            &f->desc_final, 0, NULL);
    vkCmdDispatch(cb, 1, 1, 1);

    /* --- 5. compute -> graphics (buffer barrier + attachment setups) - */
    {
        VkMemoryBarrier2 mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        mb.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mb.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        mb.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;

        VkImageMemoryBarrier2 ib_color = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        ib_color.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        ib_color.srcAccessMask = 0;
        ib_color.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        ib_color.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        ib_color.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ib_color.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ib_color.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib_color.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib_color.image = app->swapChainImages[image_idx];
        ib_color.subresourceRange = (VkImageSubresourceRange){
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 };

        VkImageMemoryBarrier2 ib_msaa = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        ib_msaa.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        ib_msaa.srcAccessMask = 0;
        ib_msaa.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        ib_msaa.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        ib_msaa.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ib_msaa.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ib_msaa.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib_msaa.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib_msaa.image = g->colorImage;
        ib_msaa.subresourceRange = (VkImageSubresourceRange){
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 };

        VkImageMemoryBarrier2 ib_depth = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        ib_depth.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        ib_depth.srcAccessMask = 0;
        ib_depth.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        ib_depth.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        ib_depth.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ib_depth.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        ib_depth.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib_depth.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib_depth.image = g->depthImage;
        ib_depth.subresourceRange = (VkImageSubresourceRange){
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 };

        VkImageMemoryBarrier2 barriers[3] = { ib_color, ib_msaa, ib_depth };
        VkDependencyInfo dep = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                 .memoryBarrierCount = 1, .pMemoryBarriers = &mb,
                                 .imageMemoryBarrierCount = 3,
                                 .pImageMemoryBarriers = barriers };
        vkCmdPipelineBarrier2(cb, &dep);
    }

    /* --- 6. dynamic rendering with MSAA (Vulkan 1.4): one render     */
    /*        pass, one DrawIndirect per pass, hardware resolve -------
    *        The scene passes draw into a TRANSIENT multisampled color
    *        attachment; the per-attachment resolve (VkRenderingAttach-
    *        mentInfo resolve fields) averages the samples onto the
    *        1-sample swapchain image — the same MSAA + resolve the
    *        CPU-driven render pass does. In this API the rendering's
    *        sample count is defined by the attachments (VkRendering-
    *        Info has no samples member); the depth attachment and the
    *        pipeline rasterizationSamples carry the same count. */
    VkRenderingAttachmentInfo color_att = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    color_att.imageView = g->colorView; /* MSAA transient color */
    color_att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    /* AVERAGE (this SDK's name for the 0x2 SAMPLE_AVERAGE bit): average
     * the MSAA samples onto the 1-sample swapchain image. */
    color_att.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    color_att.resolveImageView = app->swapChainImageViews[image_idx];
    color_att.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; /* transient: only the resolve consumes it */
    color_att.clearValue.color.float32[3] = 1.0f; /* black background */

    VkRenderingAttachmentInfo depth_att = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depth_att.imageView = g->depthView; /* GDD MSAA depth (matches rasterizationSamples) */
    depth_att.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_att.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_att.clearValue.depthStencil.depth = 1.0f;

    VkRenderingInfo ri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO };
    ri.renderArea.extent = app->swapChainExtent;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color_att;
    ri.pDepthAttachment = &depth_att;
    vkCmdBeginRendering(cb, &ri);

    VkViewport vp = { 0.0f, 0.0f, (float)app->swapChainExtent.width,
                      (float)app->swapChainExtent.height, 0.0f, 1.0f };
    VkRect2D sc = { {0, 0}, app->swapChainExtent };
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);

    /* Scene push constants: mvp = view * proj (C row-major product,
     * consumed verbatim by gdd_scene.vert), time, camera position.
     * view/cam_world/fov are computed once at the top (line floor). */
    GddScenePC spc;
    memset(&spc, 0, sizeof(spc));
    memcpy(spc.cam, cam_world, sizeof(spc.cam));
    mat4 proj; mat4 mvp;
    mat4_perspective(fov * M_PI / 180.0f,
                     (float)app->swapChainExtent.width / (float)app->swapChainExtent.height,
                     0.1f, 1000.0f, proj);
    proj[1][1] *= -1.0f; /* CPU-path Y flip, kept for exact parity */
    mat4_multiply(view, proj, mvp);
    memcpy(spc.mvp, mvp, sizeof(mvp));
    spc.time = (float)glfwGetTime();

    /* Pass A: opaque (depth write on) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g->pipe_scene_opaque);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g->pl_scene, 0, 1,
                            &f->desc_scene, 0, NULL);
    vkCmdPushConstants(cb, g->pl_scene,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       GDD_SCENE_PC_STRIDE, &spc);
    vkCmdDrawIndirect(cb, f->indirect, 0, 1, sizeof(VkDrawIndirectCommand));

    /* Pass B: additive glow (depth write off, src-alpha + one) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g->pipe_scene_add);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g->pl_scene, 0, 1,
                            &f->desc_scene, 0, NULL);
    vkCmdPushConstants(cb, g->pl_scene,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       GDD_SCENE_PC_STRIDE, &spc);
    vkCmdDrawIndirect(cb, f->indirect, sizeof(VkDrawIndirectCommand), 1,
                      sizeof(VkDrawIndirectCommand));

    vkCmdEndRendering(cb);

    /* --- 7. present barrier (color -> PRESENT_SRC_KHR) --------------- */
    {
        VkImageMemoryBarrier2 ib = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        ib.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        ib.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        ib.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        ib.dstAccessMask = 0;
        ib.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ib.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        ib.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib.image = app->swapChainImages[image_idx];
        ib.subresourceRange = (VkImageSubresourceRange){
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 };
        VkDependencyInfo dep = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                 .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &ib };
        vkCmdPipelineBarrier2(cb, &dep);
    }

    vkEndCommandBuffer(cb);
}

/* ------------------------------------------------------------------ */
/* gdd_draw_frame — full GDD frame with the SAME triple-buffer        */
/* invariant as the CPU-driven path (one fence / one acquire semaphore */
/* / one render semaphore per slot; the wait happens at the TOP, so   */
/* the CPU and the GPU overlap).                                      */
/* ------------------------------------------------------------------ */
void gdd_draw_frame(VulkanApp *app) {
    GddState *g = (GddState *)app->gdd;
    uint32_t slot = g->current_frame;
    GddFrame *f = &g->frames[slot];

    /* The GPU must be done with this slot's buffers (last use was
     * GDD_MAX_FRAMES frames ago). From here on the fence is UNSIGNALED,
     * so every early-exit path below re-signals it with an empty
     * submit or the next use of the slot would wait forever. */
    vkWaitForFences(app->device, 1, &app->inFlightFences[slot], VK_TRUE, UINT64_MAX);

    uint32_t imgIdx = 0;
    VkResult acq = vkAcquireNextImageKHR(app->device, app->swapChain, 100000000ULL,
                                         app->imageAvailableSemaphores[slot],
                                         VK_NULL_HANDLE, &imgIdx);
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
        VkSubmitInfo empty = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
        vkQueueSubmit(app->graphicsQueue, 1, &empty, app->inFlightFences[slot]);
        g->current_frame = (slot + 1) % GDD_MAX_FRAMES;
        return;
    }

    vkResetFences(app->device, 1, &app->inFlightFences[slot]);
    vkResetCommandBuffer(f->cmd, 0);

    /* The ONE per-frame CPU upload, then the GPU takes over. */
    gdd_build_frame(app, (float)glfwGetTime());
    gdd_record(f->cmd, app, imgIdx);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .waitSemaphoreCount = 1,
                        .pWaitSemaphores = &app->imageAvailableSemaphores[slot],
                        .pWaitDstStageMask = &wait_stage,
                        .commandBufferCount = 1,
                        .pCommandBuffers = &f->cmd,
                        .signalSemaphoreCount = 1,
                        .pSignalSemaphores = &app->renderFinishedSemaphores[slot] };
    vkQueueSubmit(app->graphicsQueue, 1, &si, app->inFlightFences[slot]);

    VkPresentInfoKHR pi = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                            .waitSemaphoreCount = 1,
                            .pWaitSemaphores = &app->renderFinishedSemaphores[slot],
                            .swapchainCount = 1,
                            .pSwapchains = &app->swapChain,
                            .pImageIndices = &imgIdx };
    VkResult pres = vkQueuePresentKHR(app->graphicsQueue, &pi);
    if (pres != VK_SUCCESS && pres != VK_SUBOPTIMAL_KHR &&
        pres != VK_ERROR_OUT_OF_DATE_KHR) {
        fprintf(stderr, "[GDD] vkQueuePresentKHR failed: %d\n", (int)pres);
    }

    g->current_frame = (slot + 1) % GDD_MAX_FRAMES;

    /* Same frame limiter as the CPU path (~144 fps cap). */
    struct timespec frame_limit = { 0, 6944444L };
    nanosleep(&frame_limit, NULL);

    /* Decrement shield hit timers (CPU-path parity, done per frame). */
    for (int s = 0; s < 6; s++)
        if (app->shieldHitTimers[s] > 0) app->shieldHitTimers[s]--;
}
