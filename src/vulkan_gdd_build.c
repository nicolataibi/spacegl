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
 * SPACE GL - GPU-DRIVEN RENDERING (GDD) - CPU INSTANCE BUILDER
 *
 * Translation Unit: vulkan_gdd_build.c
 * Role: CPU-side instance builder (Part 1 of the GDD subsystem).
 * Classifies objects, effects, statics, compass, and galaxy map into
 * compact GddInstance descriptors for GPU indirect dispatch.
 * Peer modules: vulkan_gdd_render.c, vulkan_scene.c, vulkan_render.c.
 */

#include <stdio.h>

#include "spacegl_gdd.h"
#include "spacegl_vulkan_types.h"
#include "sglog.h"

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

/* World half-thickness requested by every "1-px class" GDD line
 * (quadrant cube, AR compass rings/arc/axes, direction vectors, grid,
 * quantum rings). Deliberately BELOW the screen-space floor
 * (GDD_LINE_MIN_PX = 1.0, defined below in Part 2) so the floor always
 * dominates: the square tube rasterizes at exactly 1 px at EVERY camera
 * distance — the same visual weight of the CPU path's 1-px MSAA
 * LINE_LIST lines (rast.lineWidth = 1.0f). A fixed world thickness
 * (e.g. the old 0.02 / 0.35) rendered as fat multi-pixel tubes at
 * close range instead of hairlines. */
#define GDD_HAIRLINE 0.004f

/* Ship visual scale (SCALE_SHIP * 0.55 from the CPU path) and the CPU
 * ship mesh length ratio (apex 2.1866 + base 0.7288 = 2.9154 units).
 * The CPU ship mesh spans local X in [-0.7288, +2.1866], so the object
 * position sits 0.7288 * GDD_SHIP_SCALE AHEAD of the stern; the centered
 * GDD pyramid must be shifted along the nose axis by that amount to put
 * stern/nose at the exact CPU world offsets. */
#define GDD_SHIP_SCALE    (0.45f * 0.55f)
#define GDD_SHIP_LEN_RATIO 2.9154f
#define GDD_SHIP_STERN_OFF (0.7288f * GDD_SHIP_SCALE)
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
                 cr, cg, cb, 1.0f, 0, 0, GDD_FRAG_UNLIT, GDD_ORIENT_ID, GDD_HAIRLINE, 0);
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        /* square edge i -> i+1 (CPU vectorIndices: 2-3, 3-4, 4-5, 5-2) */
        gdd_compass_pt(anchor, m, corners[i], s, p0);
        gdd_compass_pt(anchor, m, corners[j], s, p1);
        gdd_list_add(dyn, GDD_MESH_LINE, p0[0], p0[1], p0[2],
                     p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2],
                     cr, cg, cb, 1.0f, 0, 0, GDD_FRAG_UNLIT, GDD_ORIENT_ID, GDD_HAIRLINE, 0);
        /* tip spoke i -> tip (CPU vectorIndices: 6-x) */
        gdd_compass_pt(anchor, m, corners[i], s, p0);
        gdd_compass_pt(anchor, m, tip, s, p1);
        gdd_list_add(dyn, GDD_MESH_LINE, p0[0], p0[1], p0[2],
                     p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2],
                     cr, cg, cb, 1.0f, 0, 0, GDD_FRAG_UNLIT, GDD_ORIENT_ID, GDD_HAIRLINE, 0);
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

/* Characteristic palette of a black hole: a stable pick from a curated
 * set (hashed from the object id) so every hole keeps its own color for
 * its whole life, and neighbours differ. */
static void gdd_bh_palette(int id, float out[3]) {
    static const float pal[8][3] = {
        {1.00f, 0.62f, 0.15f}, /* amber    */
        {0.25f, 0.80f, 1.00f}, /* ice cyan */
        {1.00f, 0.25f, 0.75f}, /* magenta  */
        {0.20f, 1.00f, 0.50f}, /* emerald  */
        {0.60f, 0.35f, 1.00f}, /* violet   */
        {1.00f, 0.20f, 0.20f}, /* crimson  */
        {1.00f, 0.90f, 0.50f}, /* gold     */
        {0.20f, 0.50f, 1.00f}, /* cobalt   */
    };
    uint32_t h = (uint32_t)(id < 0 ? 0 : id) * 2654435761u;
    const float *c = pal[h >> 29];
    out[0] = c[0]; out[1] = c[1]; out[2] = c[2];
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
    if ((type == 1 || (type >= 10 && type < 200)) && faction >= 10 && faction <= 20) {
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
    else if (type >= 100 && type <= 131) {
        int e_id = type - 100;
        v->color[0] = (e_id % 3) * 0.5f;
        v->color[1] = (e_id % 4) * 0.33f;
        v->color[2] = (e_id % 5) * 0.25f + 0.2f;
    }

    switch (type) {
    case 100: case 101: case 102: case 103: case 104: case 105: case 106: case 107:
    case 108: case 109: case 110: case 111: case 112: case 113: case 114: case 115:
    case 116: case 117: case 118: case 119: case 120: case 121: case 122: case 123:
    case 124: case 125: case 126: case 127: case 128: case 129: case 130: case 131:
        v->mesh = GDD_MESH_SPHERE;
        v->scale = 2.0f;
        v->frag_mode = GDD_FRAG_PBR;
        v->additive = 1;
        v->metallic = 1.0f; v->roughness = 0.0f;
        break;
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
        /* any (type >= 10 && type < 200) is a ship in the CPU path (10..255) */
        if (type == 1 || (type >= 10 && type < 200)) {
            v->mesh = GDD_MESH_PYRAMID;
            v->scale = GDD_SHIP_SCALE;
            /* CPU: wireframe LINE_LIST pipeline + PBR (mode 5,
             * metal 0.9 / roughness 0.25) — the barycentric wireframe
             * fragment mode re-implements exactly that look. */
            v->frag_mode = GDD_FRAG_WIREFRAME_PBR;
            v->metallic = 0.9f; v->roughness = 0.25f;
            if (cloaked) {
                /* CPU: cyan wireframe, pulsing alpha, standard blending */
                v->color[0]=0.0f; v->color[1]=0.8f; v->color[2]=1.0f;
                v->additive = 1; v->frag_mode = GDD_FRAG_WIREFRAME;
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

    case 6: /* black hole: pure black event-horizon sphere (the colored
             * disk and the lensing halo are added by the caller) */
        v->scale = 2.0f; v->frag_mode = GDD_FRAG_UNLIT;
        v->color[0]=0.0f; v->color[1]=0.0f; v->color[2]=0.0f;
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

    case 7:  /* nebula (volumetric, additive): spherical + iridescent.
              * Sub-type colors mirror the CPU drawNebula (0 blue, 1 orange,
              * 2 dark purple); the dark one stays a plain FBM cloud. */
        v->scale = 5.0f; v->frag_mode = GDD_FRAG_NEBULA_IRIS; v->additive = 1;
        switch (ship_class) {
        case 0:  v->color[0]=0.55f; v->color[1]=0.72f; v->color[2]=1.0f; break;
        case 1:  v->color[0]=1.0f;  v->color[1]=0.40f; v->color[2]=0.2f; break;
        case 2:  v->color[0]=0.2f;  v->color[1]=0.0f;  v->color[2]=0.4f;
                 v->frag_mode = GDD_FRAG_NEBULA; break;
        default: v->color[0]=0.3f;  v->color[1]=0.5f;  v->color[2]=0.9f; break;
        }
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
        /* diffuse / high-energy / dark-matter / gravimetric / temporal nebulae:
         * same spherical iridescent look as type 7 */
        v->scale = 5.0f; v->frag_mode = GDD_FRAG_NEBULA_IRIS; v->additive = 1;
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
    /*    top→mid→bottom (yellow), 1-px hairlines (GDD_HAIRLINE).      */
    /* ---------------------------------------------------------------- */
    if (ctx->map_anim < 0.99f &&
        !(ctx->jump_arrival && ctx->jump_arrival->active)) {
        float hq = GDD_QUADRANT; /* 20.0 = QUADRANT_SIZE/2 */
        float ht = GDD_HAIRLINE; /* 1-px class: screen floor dominates (see define) */
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
    /* Diagnostica navi altri giocatori (TRACE3, rate-limited)          */
    {
        int other_ships_total = 0, other_ships_skipped_first = 0,
            other_ships_skipped_inactive = 0, other_ships_added = 0;
        char detbuf[1024]; int doff = 0;
        int ndet = ctx->object_count; if (ndet > 16) ndet = 16;
        for (int i = 0; i < ndet && i < (int)GDD_MAX_NET_OBJECTS; i++) {
            int tp = ctx->types ? ctx->types[i] : 1;
            int act = (ctx->active && !ctx->active[i]) ? 0 : 1;
            const SmoothObj *s2 = &ctx->objs[i];
            /* Per-index detail (smoke test): raw id/type/active from the
             * shm frame plus the SmoothObj tracker state at the SAME index.
             * sm_id must equal id and sm_f must be 0 for every tracked
             * object; a mismatch means the tracker's position/id mapping
             * drifted from the raw list (overwritten/aliased slot). */
            if (doff < (int)sizeof(detbuf) - 64)
                doff += snprintf(detbuf + doff, sizeof(detbuf) - (size_t)doff,
                                 " %d[id=%d,t=%d,a=%d,sm_id=%d,sm_f=%d]",
                                 i, ctx->ids ? ctx->ids[i] : -1, tp, act,
                                 s2->id, (int)s2->first);
            if (i == 0) continue;
            if (tp != 1) continue; /* solo giocatori (type=1 a i>0) */
            other_ships_total++;
            if (s2->first) { other_ships_skipped_first++; continue; }
            if (ctx->active && !ctx->active[i]) { other_ships_skipped_inactive++; continue; }
            other_ships_added++;
        }
        if ((other_ships_total > 0 || ctx->object_count > 1) && sglog_rate("gdd_other_ships", 2))
            SG_TRACE3(SG_CAT_VULKAN,
                "gdd other-player ships: total=%d added=%d skip_first=%d skip_inactive=%d (obj_count=%d)|%s",
                other_ships_total, other_ships_added,
                other_ships_skipped_first, other_ships_skipped_inactive,
                ctx->object_count, detbuf);
    }
    if (ctx->map_anim < 0.99f && ctx->object_count > 0 && ctx->objs) {
        for (int i = 0; i < ctx->object_count && i < (int)GDD_MAX_NET_OBJECTS; i++) {
            const SmoothObj *so = &ctx->objs[i];
            if (ctx->active && !ctx->active[i]) continue;

            /* Fallback per oggetti non ancora tracciati dallo SmoothObj tracker
             * (first=true con oggetto attivo): usa le coordinate IPC raw se disponibili.
             * Questo evita un frame (o più) di invisibilità per le navi che entrano
             * nel quadrante quando il tracker non ha ancora elaborato il nuovo buffer IPC. */
            SmoothObj raw_fallback;
            if (so->first) {
                if (!ctx->raw_objs) continue; /* senza fallback, salta come prima */
                const SharedObject *ro = (const SharedObject *)ctx->raw_objs + i;
                memset(&raw_fallback, 0, sizeof(raw_fallback));
                raw_fallback.first = false;
                raw_fallback.x = (float)ro->shm_x;
                raw_fallback.y = (float)ro->shm_y;
                raw_fallback.z = (float)ro->shm_z;
                raw_fallback.h = (float)ro->h;
                raw_fallback.m = (float)ro->m;
                raw_fallback.r = (float)ro->r;
                so = &raw_fallback;
            }

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

            /* Orientation: full Euler for ships (cloaked included — the
             * CPU computes R for every ship), spin for quasars */
            gdd_m3 m;
            /* Only objects whose visual is the oriented nose mesh (ships,
             * torpedo objects) get the ship stretch / stern offset /
             * heading. Cosmic types inside 10..199 (nebulae 51-55, dark
             * cloud 44, filaments, disks...) are symmetric volumes: without
             * this test a nebula was stretched 2.9x along X (an "egg") and
             * shifted along the nose axis. */
            int is_ship = ((type == 1 || (type >= 10 && type < 200)) &&
                           v.mesh == GDD_MESH_PYRAMID);
            if (is_ship) {
                gdd_ship_rotation(so, m);
                gdd_orient_from_m3(o, m);
            } else if (v.spin) {
                gdd_m3_identity(m);
                gdd_m3_rotate(m, pulse * 3.0f, 0.0f, 1.0f, 0.0f);
                gdd_m3_rotate(m, 20.0f * M_PI / 180.0f, 1.0f, 0.0f, 0.0f);
                gdd_orient_from_m3(o, m);
            } else {
                gdd_orient_identity(o);
            }

            float s = v.scale * ts;
            float sx = s, sy = s, sz = s;
            float nose[3] = { 1.0f, 0.0f, 0.0f };
            if (is_ship) {
                /* CPU ship mesh aspect: nose length 2.9154, base 1.0 */
                sx *= GDD_SHIP_LEN_RATIO;
                gdd_m3_xaxis(m, nose);
                /* CPU parity: the object position sits 0.7288*GDD_SHIP_SCALE
                 * ahead of the stern (mesh local [-0.7288, +2.1866]); the
                 * centered GDD pyramid is shifted along the nose axis so
                 * stern/nose land on the exact CPU world offsets. */
                float sox = GDD_SHIP_STERN_OFF * ts;
                px += nose[0] * sox; py += nose[1] * sox; pz += nose[2] * sox;
            }
            gdd_list_add(&dyn, v.mesh, px, py, pz, sx, sy, sz,
                         v.color[0], v.color[1], v.color[2], 1.0f,
                         0, v.additive, v.frag_mode, o,
                         v.metallic, v.roughness);

            /* --- QUANTUM CORE (Alliance ships: faction 0 or 1) --------- */
            /* CPU parity (recordCommandBuffer "QUANTUM CORE"): a pulsing
             * wireframe polyhedron core + 3 orbiting wireframe rings at
             * the stern. The CPU matrix product puts the core at
             * -0.46 * GDD_SHIP_SCALE * ts along the ship axis (the T_core
             * translation is inside the ship's 0.2475 scale) with core
             * radius 0.20 * GDD_SHIP_SCALE * ts — this reproduces the
             * ACTUAL CPU render, not the (wrong) code comment. The CPU
             * core is a class-dependent N-side bipyramid (N = 3+(13-cl)*6,
             * 81 for Legacy down to 3 for Sentinel); the GDD approximates
             * it with the 4-side bipyramid (octahedron) wireframe. */
            if (is_ship && (faction == 0 || faction == 1)) {
                int cl = sclass;
                if (cl < 0 || cl > 13) cl = 13;

                /* Stern anchor: object position (pre stern-shift) minus
                 * 0.46 * GDD_SHIP_SCALE * ts along the nose axis. */
                float qoff = -0.46f * GDD_SHIP_SCALE * ts;
                float qx = px - nose[0] * GDD_SHIP_STERN_OFF * ts + nose[0] * qoff;
                float qy = py - nose[1] * GDD_SHIP_STERN_OFF * ts + nose[1] * qoff;
                float qz = pz - nose[2] * GDD_SHIP_STERN_OFF * ts + nose[2] * qoff;

                /* 1. Core: wireframe polyhedron, spinning (Legacy fastest) */
                {
                    gdd_m3 rq;
                    gdd_m3_identity(rq);
                    float rotSpeed = 1.0f + (12 - cl) * 0.2f;
                    gdd_m3_rotate(rq, pulse * rotSpeed, 0.0f, 1.0f, 0.0f);
                    gdd_m3 mq;
                    gdd_m3_multiply(m, rq, mq); /* vertex: R_ship then R_q */
                    float qo[12];
                    gdd_orient_from_m3(qo, mq);
                    float cs = 0.20f * GDD_SHIP_SCALE * ts;
                    float cr = 0.8f + 0.2f * sinf(pulse * 2.0f);
                    float cg = 0.6f + 0.3f * cosf(pulse * 1.5f);
                    float cb = 0.2f + 0.2f * sinf(pulse * 3.0f);
                    gdd_list_add(&dyn, GDD_MESH_OCTA, qx, qy, qz,
                                 cs, cs, cs,
                                 cr, cg, cb, 1.0f,
                                 0, 0, GDD_FRAG_WIREFRAME, qo, 0.0f, 1.0f);
                }

                /* 2. Three orbiting rings (wireframe circles, cyan) */
                static const float r_speeds[3] = { 2.8f, -2.1f, 3.5f };
                static const float r_radii[3]  = { 0.15f, 0.18f, 0.21f };
                static const vec3 r_axes[3] = { {1,1,0.3f}, {0.3f,1,1}, {1,0.3f,1} };
                for (int r = 0; r < 3; r++) {
                    gdd_m3 rr;
                    gdd_m3_identity(rr);
                    gdd_m3_rotate(rr, pulse * r_speeds[r],
                                  r_axes[r][0], r_axes[r][1], r_axes[r][2]);
                    gdd_m3 mr;
                    gdd_m3_multiply(m, rr, mr); /* vertex: R_ship then R_r */
                    float ro[12];
                    gdd_orient_from_m3(ro, mr);
                    float rr2 = r_radii[r] * GDD_SHIP_SCALE * ts;
                    gdd_list_add(&dyn, GDD_MESH_CIRCLE, qx, qy, qz,
                                 rr2, rr2, rr2,
                                 0.0f, 0.95f, 1.0f, 1.0f,
                                 0, 0, GDD_FRAG_UNLIT, ro, GDD_HAIRLINE, 1.0f);
                }
            }

            /* --- per-type companions (CPU parity approximations) --- */
            if (type == 6) {
                /* Black hole: black sphere (above) + a flat circular disk
                 * in the hole's own palette + a thin lensing halo. The disk
                 * is a GDD ring: outer radius R = 3.6 s, inner edge at 1.3 s
                 * (so it never touches the horizon); p.x = ring half
                 * thickness, p.y = R (read by GDD_FRAG_BH_DISC). It is tilted
                 * and slowly precesses so it reads from any camera angle. */
                float bh_col[3];
                gdd_bh_palette(ctx->ids ? ctx->ids[i] : 0, bh_col);

                float R = s * 3.6f;
                float inner_ratio = (s * 1.3f) / R;
                gdd_m3 tilt, spin, md;
                gdd_m3_identity(tilt);
                gdd_m3_rotate(tilt, 0.35f, 1.0f, 0.0f, 0.0f);
                gdd_m3_identity(spin);
                gdd_m3_rotate(spin, pulse * 0.1f, 0.0f, 1.0f, 0.0f);
                gdd_m3_multiply(tilt, spin, md);
                float od[12];
                gdd_orient_from_m3(od, md);
                gdd_list_add(&dyn, GDD_MESH_RING, px, py, pz,
                             R, R, R,
                             bh_col[0], bh_col[1], bh_col[2], 1.0f,
                             0, 1, GDD_FRAG_BH_DISC, od,
                             R * (1.0f - inner_ratio), R);
                /* Lensing halo: fresnel shell just outside the horizon */
                gdd_list_add(&dyn, GDD_MESH_SPHERE, px, py, pz,
                             s * 1.22f, s * 1.22f, s * 1.22f,
                             bh_col[0], bh_col[1], bh_col[2], 1.0f,
                             0, 1, GDD_FRAG_RIMGLOW, o, 0.0f, 1.0f);
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

        /* The shared `o` still holds the orientation of the LAST object
         * processed in the loop above (e.g. an enemy ship). The global
         * axes and the fixed white ring must stay world-aligned, so
         * re-establish identity orientation before reusing `o`. */
        gdd_orient_identity(o);

        /* 1. Global axes (fixed to world orientation, pure R/G/B) */
        float a = 5.5f * ts;
        gdd_list_add(&dyn, GDD_MESH_LINE, cx - a, cy, cz, 2.0f*a, 0, 0,
                     1, 0, 0, 1.0f, 0, 0, GDD_FRAG_UNLIT, o, GDD_HAIRLINE, 0);
        gdd_list_add(&dyn, GDD_MESH_LINE, cx, cy - a, cz, 0, 2.0f*a, 0,
                     0, 1, 0, 1.0f, 0, 0, GDD_FRAG_UNLIT, o, GDD_HAIRLINE, 0);
        gdd_list_add(&dyn, GDD_MESH_LINE, cx, cy, cz - a, 0, 0, 2.0f*a,
                     0, 0, 1, 1.0f, 0, 0, GDD_FRAG_UNLIT, o, GDD_HAIRLINE, 0);

        /* 2. Fixed compass ring (white, world orientation) */
        gdd_list_add(&dyn, GDD_MESH_CIRCLE, cx, cy, cz,
                     3.0f*ts, 3.0f*ts, 3.0f*ts, 1, 1, 1, 1.0f,
                     0, 0, GDD_FRAG_UNLIT, o, GDD_HAIRLINE, 0);

        /* 3. Heading ring (cyan, level with pitch) */
        {
            gdd_m3 mring;
            gdd_m3_identity(mring);
            gdd_m3_rotate(mring, mrot, pitch_axis[0], pitch_axis[1], pitch_axis[2]);
            gdd_orient_from_m3(o, mring);
            gdd_list_add(&dyn, GDD_MESH_CIRCLE, cx, cy, cz,
                         2.5f*ts, 2.5f*ts, 2.5f*ts, 0, 1, 1, 1.0f,
                         0, 0, GDD_FRAG_UNLIT, o, GDD_HAIRLINE, 0);
        }

        /* 4. Mark arc (yellow vertical arc, aligned with ship heading) */
        {
            gdd_m3 mring;
            gdd_m3_identity(mring);
            gdd_m3_rotate(mring, head_yaw, 0, 1, 0);
            gdd_orient_from_m3(o, mring);
            gdd_list_add(&dyn, GDD_MESH_ARC, cx, cy, cz,
                         2.8f*ts, 2.8f*ts, 2.8f*ts, 1, 1, 0, 1.0f,
                         0, 0, GDD_FRAG_UNLIT, o, GDD_HAIRLINE, 0);
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
                         0, 0, GDD_FRAG_UNLIT, o, GDD_HAIRLINE, 0);
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
    /* 4. Phasers (FX): one white wireframe segment from the firing     */
    /*    ship to the target + the signature impact boom at the         */
    /*    endpoint (GDD_MESH_BOOM, style 2).                            */
    /*                                                                  */
    /*    The rendering is IDENTICAL for every phaser source: the       */
    /*    player's "pha" command, the NPC AI, the resonance monsters    */
    /*    and the star bases all push their shots into the same         */
    /*    NetUpdate.beams channel (server aggregation in logic.c), the  */
    /*    client relays them as IPC_EV_BEAM events into this very       */
    /*    ActiveBeam list, and this one section draws them all: a       */
    /*    white, unlit, additive GDD_MESH_LINE tube (the GDD            */
    /*    counterpart of the CPU path's 1-px LINE_LIST — the screen-    */
    /*    space floor in the expand pass keeps it at least 1 px at      */
    /*    any camera distance, so it can never rasterize invisible)     */
    /*    with alpha = beam life, ending in the style-2 impact boom.    */
    /*    The boom is drawn once per shot: "pha" fires two emitter      */
    /*    beams (top/bottom) at the same target, and only emitter 1     */
    /*    carries it (NPC/monster/base shots use emitter 1 as well).    */
    /* ---------------------------------------------------------------- */
    if (ctx->map_anim < 0.99f && ctx->beams) {
        uint32_t dyn_before = dyn.n;
        int tubes_added = 0, booms_added = 0;
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
                        float off_v = (b->emitter_id == 2) ? -0.12f : 0.12f;
                        bsx = ctx->objs[j].x - GDD_QUADRANT;
                        bsy = ctx->objs[j].z - GDD_QUADRANT + off_v;
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
            float dx = vtx - vsx, dy = vty - vsy, dz = vtz - vsz;
            float dist = sqrtf(dx*dx + dy*dy + dz*dz);
            if (dist < 0.1f) continue;

            /* White wireframe segment, ship -> target. The tube's
             * half-thickness is 0.05 * tactScale; gdd_expand_line
             * clamps it to the 1-px screen floor (pc.line_min_wu). */
            uint32_t n0 = dyn.n;
            gdd_list_add(&dyn, GDD_MESH_LINE, vsx, vsy, vsz, dx, dy, dz,
                         1.0f, 1.0f, 1.0f, b->life,
                         1, 1, GDD_FRAG_UNLIT, GDD_ORIENT_ID,
                         0.05f * ts, 0.0f);
            if (dyn.n > n0) tubes_added++;

            /* Signature impact boom at the target (once per shot). */
            if (b->emitter_id == 1) {
                uint32_t n1 = dyn.n;
                gdd_list_add_identity(&dyn, GDD_MESH_BOOM, vtx, vty, vtz,
                                      ts, ts, ts,
                                      1, 1, 1, b->life,
                                      1, 1, GDD_FRAG_UNLIT,
                                      gdd_beam_boom_seed(b->owner_id, b->extra, b->emitter_id),
                                      2.0f);
                if (dyn.n > n1) booms_added++;
            }
        }
        /* TRACE2 = per-frame summary (opt-in only). While a beam is alive
         * this prints once per frame: the whole GDD beam pipeline state. */
        if (tubes_added > 0) {
            SG_TRACE2(SG_CAT_VULKAN, "gdd: %d beam tubes + %d impact booms in dyn list (%d/%d used)",
                      tubes_added, booms_added, dyn.n, (int)GDD_DYN_MAX);
        }
        if (tubes_added == 0 && dyn_before < dyn.n) {
            /* instances were dropped: dyn list full */
            if (sglog_rate("gdd_dyn_full_beam", 2))
                SG_WARNING(SG_CAT_VULKAN, "gdd: dyn list FULL (%d) - beam instances DROPPED", (int)GDD_DYN_MAX);
        }
    }

    /* ---------------------------------------------------------------- */
    /* 5. Explosions (FX): one GDD_MESH_BOOM instance per boom.         */
    /*    The GPU expands the full 256-particle cloud procedurally      */
    /*    (gdd_expand_boom) from the per-boom seed + style, matching    */
    /*    the CPU path (recordCommandBuffer: 256 unlit spheres,         */
    /*    offsets*exp with exp = (1-life)*12*tactScale, per-boom        */
    /*    random offsets/colors, radius 0.25*tactScale).                */
    /* ---------------------------------------------------------------- */
    if (ctx->map_anim < 0.99f && ctx->booms) {
        for (int i = 0; i < (int)GDD_MAX_ACTIVE_BOOMS; i++) {
            const ActiveBoom *bm = &ctx->booms[i];
            if (bm->life <= 0.0f) continue;
            gdd_list_add_identity(&dyn, GDD_MESH_BOOM,
                                  bm->x * ts, bm->y * ts, bm->z * ts,
                                  ts, ts, ts,
                                  1, 1, 1, bm->life,
                                  1, 0, GDD_FRAG_UNLIT,
                                  bm->seed, (float)bm->style);
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
            /* CPU parity: the flash (j=0, mode 7) is drawn only while
             * life >= 0.7; the core (j=1, mode 6) and the expanding
             * cyan aura (j=2, mode 7) are always drawn. */
            if (life > 0.7f) {
                float s0 = (1.0f - life) * 3.0f * ts;
                gdd_list_add_identity(&dyn, GDD_MESH_SPHERE, dx, dy, dz, s0, s0, s0,
                                      1, 1, 1, (life - 0.7f) / 0.3f, 0, 1, GDD_FRAG_SHOCKWAVE, 0, 1);
            }
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
    /* 8. Shield hit illumination (CPU parity: drawShieldEffect).       */
    /*    Per hit sector: anisotropic PBR energy panel + shockwave      */
    /*    surface glow + 4 outer volumetric shells, all oriented with   */
    /*    the ship (R_sec = Ry(ry) * Rz(rx) * R_ship, offset 1.45*      */
    /*    scale along the sector axis, scale 0.5 x 1.8 x 1.8).         */
    /* ---------------------------------------------------------------- */
    if (ctx->map_anim < 0.99f && ctx->object_count > 0 && ctx->objs &&
        !ctx->objs[0].first && ctx->shield_timers) {
        /* Sector orientation: (rx around Z, ry around Y), CPU table */
        static const float SHIELD_ROT_RX[6] = {
            0.0f, 0.0f, -M_PI / 2.0f, M_PI / 2.0f, 0.0f, 0.0f
        };
        static const float SHIELD_ROT_RY[6] = {
            0.0f, M_PI, 0.0f, 0.0f, M_PI / 2.0f, -M_PI / 2.0f
        };

        int hit = 0;
        for (int i = 0; i < 6; i++) if (ctx->shield_timers[i] > 0) hit = 1;
        if (hit) {
            gdd_m3 m_ship;
            gdd_ship_rotation(&ctx->objs[0], m_ship);
            /* Ship position (plain object mapping, no stern shift — the
             * CPU shield uses T_ship from the object loop). */
            const SmoothObj *so0 = &ctx->objs[0];
            float sx0 = (so0->x - GDD_QUADRANT) * ts;
            float sy0 = (so0->z - GDD_QUADRANT) * ts;
            float sz0 = (GDD_QUADRANT - so0->y) * ts;
            for (int s = 0; s < 6; s++) {
                if (ctx->shield_timers[s] <= 0) continue;
                float t = (float)ctx->shield_timers[s] / 80.0f;
                float alpha = (t > 0.5f) ? 1.0f : t * 2.0f;
                float scale = (1.2f + (1.0f - t) * 0.3f) * ts;
                float bsc[3] = { 0.5f * scale, 1.8f * scale, 1.8f * scale };

                /* R_sec = Ry(ry) * Rz(rx), composed with the ship */
                gdd_m3 m_sec;
                gdd_m3_identity(m_sec);
                gdd_m3_rotate(m_sec, SHIELD_ROT_RY[s], 0.0f, 1.0f, 0.0f);
                gdd_m3_rotate(m_sec, SHIELD_ROT_RX[s], 0.0f, 0.0f, 1.0f);
                gdd_m3 m_full;
                gdd_m3_multiply(m_sec, m_ship, m_full);
                float orient[12];
                gdd_orient_from_m3(orient, m_full);

                /* Center = ship pos + R_ship * R_sec * (1.45*scale, 0, 0) */
                float off0 = 1.45f * scale;
                float px = sx0 + off0 * m_full[0][0];
                float py = sy0 + off0 * m_full[0][1];
                float pz = sz0 + off0 * m_full[0][2];

                /* 1. Shield energy grid (PBR panel, opaque) */
                gdd_list_add(&dyn, GDD_MESH_SPHERE, px, py, pz,
                             bsc[0], bsc[1], bsc[2],
                             0.0f, 0.8f, 1.0f, alpha * 0.8f,
                             0, 0, GDD_FRAG_PBR, orient, 0.0f, 0.0f);
                /* 2. Shield surface glow (shockwave, metallic = pulse*15) */
                gdd_list_add(&dyn, GDD_MESH_SPHERE, px, py, pz,
                             bsc[0], bsc[1], bsc[2],
                             0.0f, 0.6f, 1.0f, alpha * 0.6f,
                             0, 1, GDD_FRAG_SHOCKWAVE, orient, pulse * 15.0f, 0.0f);
                /* 3. Outer volumetric glow (4 shells, CPU drawShieldGlow) */
                for (int i = 1; i <= 4; i++) {
                    float gs = 0.7f * scale * (1.2f + (float)i * 0.4f);
                    gdd_list_add(&dyn, GDD_MESH_SPHERE, px, py, pz,
                                 bsc[0] * gs, bsc[1] * gs, bsc[2] * gs,
                                 0.0f, 0.5f, 1.0f, (alpha * 0.9f) / ((float)i * 1.2f),
                                 0, 1, GDD_FRAG_SHOCKWAVE, orient, pulse * 10.0f, 0.0f);
                }
            }
        }
    }

    /* ---------------------------------------------------------------- */
    /* 9. Galaxy map (map group, rebuilt every frame in map mode)       */
    /*    CPU parity (drawGalaxyMap): the global frame and every        */
    /*    sector drawn in filter-0 mode are WIREFRAME cubes (LINE_LIST  */
    /*    on cubeIndices) -> GDD_MESH_BOXWIRE, 1-px hairlines;          */
    /*    filter>0 sectors are solid PBR boxes (graphics pipeline,      */
    /*    usePushColor=5, metallic 0.5 / roughness 0.5) -> GDD_MESH_BOX */
    /*    + GDD_FRAG_PBR. The player-quadrant highlight is ALWAYS        */
    /*    wireframe (unlit white); the marker inside it follows the     */
    /*    same filter rule as the other sectors.                        */
    /* ---------------------------------------------------------------- */
    if (ctx->map_anim > 0.01f) {
        float gap = GDD_GALAXY_GAP;
        float offset = -((float)GDD_GALAXY_SIZE * gap) / 2.0f;
        int gs = ctx->galaxy_size > 0 ? ctx->galaxy_size : GDD_GALAXY_SIZE;
        int stride = gs + 1; /* shm_galaxy[gs+1][gs+1][gs+1], coords 1..gs */

        /* 1. Galaxy frame (CPU: wireframe cube, push color 0.4/0.4/1.0) */
        gdd_list_add_identity(&map, GDD_MESH_BOXWIRE, 0, 0, 0,
                              (float)GDD_GALAXY_SIZE * gap * ms * 0.5f,
                              (float)GDD_GALAXY_SIZE * gap * ms * 0.5f,
                              (float)GDD_GALAXY_SIZE * gap * ms * 0.5f,
                              0.4f, 0.4f, 1.0f, 0.8f, 1, 0, GDD_FRAG_UNLIT,
                              GDD_HAIRLINE, 0);

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

                        /* Sector marker: wireframe (filter 0) or solid
                         * PBR (filter > 0), CPU usePushColor 1/5 with
                         * metallic 0.5 / roughness 0.5. */
                        int wire = (ef == 0);
                        if (is_my_q) {
                            /* Player-quadrant highlight: ALWAYS wireframe,
                             * unlit white (CPU: wireframe pipeline even
                             * when mapFilter > 0). */
                            float meScale = (0.4f + sinf(pulse * 6.0f) * 0.15f) * ms;
                            gdd_list_add_identity(&map, GDD_MESH_BOXWIRE, pxm, pym, pzm,
                                                  meScale, meScale, meScale,
                                                  1, 1, 1, 0.8f, 1, 0, GDD_FRAG_UNLIT,
                                                  GDD_HAIRLINE, 0);
                            if (val != 0) {
                                if (wire)
                                    gdd_list_add_identity(&map, GDD_MESH_BOXWIRE, pxm, pym, pzm,
                                                          scale, scale, scale,
                                                          cr, cg, cb, 1.0f, 1, 0,
                                                          GDD_FRAG_UNLIT, GDD_HAIRLINE, 0);
                                else
                                    gdd_list_add_identity(&map, GDD_MESH_BOX, pxm, pym, pzm,
                                                          scale, scale, scale,
                                                          cr, cg, cb, 1.0f, 1, 0,
                                                          GDD_FRAG_PBR, 0.5f, 0.5f);
                            }
                        } else if (val != 0) {
                            if (wire)
                                gdd_list_add_identity(&map, GDD_MESH_BOXWIRE, pxm, pym, pzm,
                                                      scale, scale, scale,
                                                      cr, cg, cb, 1.0f, 1, 0,
                                                      GDD_FRAG_UNLIT, GDD_HAIRLINE, 0);
                            else
                                gdd_list_add_identity(&map, GDD_MESH_BOX, pxm, pym, pzm,
                                                      scale, scale, scale,
                                                      cr, cg, cb, 1.0f, 1, 0,
                                                      GDD_FRAG_PBR, 0.5f, 0.5f);
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
            /* 1-px class: the metallic field carries the requested
             * half-thickness; GDD_HAIRLINE sits below the 1-px screen
             * floor so the grid renders as 1-px hairlines at every
             * camera distance (CPU wireframe-pipeline parity). */
            float ht = GDD_HAIRLINE;
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

    /* ---------------------------------------------------------------- */
    /* 11. Pilot HUD: crosshair + throttle bar (pilot mode only)        */
    /* ---------------------------------------------------------------- */
    /* Both elements live in the dyn (dynamic) list, drawn in the       */
    /* tactical view plane (map_anim < 0.99) at the ship's world        */
    /* position with zero scale offset — they are screen-space proxies. */
    /* The crosshair is a small sphere at scale 0 placed far along the  */
    /* forward axis so it renders as a tiny dot at screen centre; the   */
    /* throttle bar is a thin GDD_MESH_BOX scaled on X by throttle.     */
    if (ctx->show_pilot && ctx->map_anim < 0.99f) {
        /* Crosshair: two perpendicular hairline boxes centred on the    */
        /* tactical view origin (0,0,0 in ship-relative space).         */
        /* Scale: thin enough to look like a reticle at typical FOV.    */
        float cw = 0.35f; /* half-width of each crosshair arm           */
        float ct = GDD_HAIRLINE * 6.0f; /* thickness (6× hairline)      */
        /* Horizontal arm */
        gdd_list_add_identity(&dyn, GDD_MESH_BOX,
                              0.0f, 0.0f, 0.0f,
                              cw, ct, ct,
                              0.0f, 1.0f, 0.0f, 0.85f,
                              1, 0, GDD_FRAG_UNLIT, 0, 0);
        /* Vertical arm */
        gdd_list_add_identity(&dyn, GDD_MESH_BOX,
                              0.0f, 0.0f, 0.0f,
                              ct, cw, ct,
                              0.0f, 1.0f, 0.0f, 0.85f,
                              1, 0, GDD_FRAG_UNLIT, 0, 0);

        /* Throttle bar: bottom-left HUD area.                          */
        /* Rendered as a thin horizontal box whose X scale is           */
        /* proportional to pilot_throttle (0..1).                       */
        float bar_max = 0.8f;   /* full-width half-extent               */
        float bar_h   = GDD_HAIRLINE * 10.0f;
        float bar_t   = GDD_HAIRLINE * 4.0f;
        float bar_w   = bar_max * ctx->pilot_throttle;
        if (bar_w > 0.001f) {
            /* Background (dim green full-width) */
            gdd_list_add_identity(&dyn, GDD_MESH_BOX,
                                  0.0f, -0.88f, 0.0f,
                                  bar_max, bar_h, bar_t,
                                  0.0f, 0.25f, 0.0f, 0.5f,
                                  1, 0, GDD_FRAG_UNLIT, 0, 0);
            /* Fill (bright green, width = throttle%) */
            gdd_list_add_identity(&dyn, GDD_MESH_BOX,
                                  (bar_w - bar_max), -0.88f, 0.0f,
                                  bar_w, bar_h, bar_t,
                                  0.0f, 1.0f, 0.2f, 0.9f,
                                  1, 0, GDD_FRAG_UNLIT, 0, 0);
        }
    }

    if (ctx->dyn_count) *ctx->dyn_count = dyn.n;
    if (ctx->map_count) *ctx->map_count = map.n;
}

