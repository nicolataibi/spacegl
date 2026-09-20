/*
 * SPACE GL - GPU-DRIVEN RENDERING (GDD) - shared compute definitions
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
 * Common definitions shared by all gdd_*.comp shaders (see the contract
 * in include/spacegl_gdd.h — the CPU side of every layout below lives
 * there, keep them in sync).
 *
 * NOTE: included by the gdd_*.comp entry points — no #version here (it
 * must appear only in the entry file). NOTE 2: this glslang build
 * requires a block instance name on every buffer declaration, so all
 * blocks below carry one.
 *
 * Pipeline: the CPU uploads compact instance descriptors (GddInstance);
 * gdd_cull.comp writes the visible ones to a compact index list
 * (vision-radius sphere cull); gdd_expand.comp expands each visible
 * instance into the device-local vertex SSBO (atomic counter + capacity
 * guard, "drop when full"); gdd_final.comp publishes the two
 * VkDrawIndirectCommands (opaque / additive) from the counters.
 */

/* Vertices generated into gdd_verts (80 B stride == GDD_VERTEX_STRIDE). */
struct GddVertex {
    vec4 pos;
    vec4 color;
    vec4 normal;
    vec4 local;
    vec4 params;
};

/* Instance descriptor (112 B == GddInstance).
 * a = (pos.xyz, mesh), b = (scale.xyz, flags), c = (color.xyz, alpha),
 * o = 3x3 orientation (column-major mat3), p = (metallic, roughness, ..). */
struct GddInstance {
    vec4 a;
    vec4 b;
    vec4 c;
    mat3 o;
    vec4 p;
};

/* Per-frame GPU counters (16 B buffer; the CPU zeroes it with
 * vkCmdFillBuffer at the start of every frame). */
struct GddCounts {
    uint dyn_vis;
    uint map_vis;
    uint opaque_verts;
    uint additive_verts;
};

/* Compute push constants (36 B == GddPC). cull = (center.xyz, radius). */
layout(push_constant) uniform GddPC {
    vec4 cull;
    uint list_count;
    uint group;
    uint capacity;
    uint pad;         /* expand pass selector: 0 = opaque, 1 = additive */
    float line_min_wu; /* GDD_MESH_LINE: screen-space half-thickness floor (world units) */
} pc;

const int GDD_MESH_POINT   = 0;
const int GDD_MESH_SPHERE  = 1;
const int GDD_MESH_BOX     = 2;
const int GDD_MESH_PYRAMID = 3;
const int GDD_MESH_OCTA    = 4;
const int GDD_MESH_RING    = 5;
const int GDD_MESH_LINE    = 6;

const uint GDD_VC_POINT   = 6u;
const uint GDD_VC_SPHERE  = 360u;
const uint GDD_VC_BOX     = 36u;
const uint GDD_VC_PYRAMID = 18u;
const uint GDD_VC_OCTA    = 24u;
const uint GDD_VC_RING    = 96u;
const uint GDD_VC_LINE    = 24u; /* square tube: 4 faces × 6 verts, no Z-fighting */

const int  GDD_SPHERE_LATS = 6;
const int  GDD_SPHERE_LONS = 10;
const int  GDD_RING_SEGS   = 16;

/* ------------------------------------------------------------------ */
/* Flag decoding (GddInstance.b.w is a float holding exact small      */
/* integer bits: bit0 = never cull, bit1 = additive, bits 2..5 =      */
/* fragment mode — see gdd_make_flags in spacegl_gdd.h).              */
/* ------------------------------------------------------------------ */
uint gdd_flags(float f) { return uint(f + 0.5); }
bool gdd_never_cull(GddInstance it) { return ((gdd_flags(it.b.w) >> 0) & 1u) != 0u; }
bool gdd_additive(GddInstance it)   { return ((gdd_flags(it.b.w) >> 1) & 1u) != 0u; }
float gdd_frag_mode(GddInstance it) { return float((gdd_flags(it.b.w) >> 2) & 0xFu); }

/* ------------------------------------------------------------------ */
/* Box table: entry i belongs to face i/6 (two triangles per face),   */
/* whose normal is GDD_BOX_FACE_N[i/6]. Face order: +Y, -Y, +Z, -Z,   */
/* -X, +X. (Culling is disabled on the GDD pipelines, so winding      */
/* only affects depth order, never visibility.)                       */
/* ------------------------------------------------------------------ */
const ivec3 GDD_BOX_SIGNS[36] = {
    /* top (n = 0,1,0) */
    ivec3(-1, 1, -1), ivec3( 1, 1, -1), ivec3( 1, 1, 1),
    ivec3(-1, 1, -1), ivec3( 1, 1,  1), ivec3(-1, 1,  1),
    /* bottom (n = 0,-1,0) */
    ivec3( 1,-1, -1), ivec3(-1,-1, -1), ivec3(-1,-1, 1),
    ivec3( 1,-1, -1), ivec3(-1,-1,  1), ivec3( 1,-1, 1),
    /* front (n = 0,0,1) */
    ivec3(-1,-1, 1), ivec3( 1,-1, 1), ivec3( 1, 1, 1),
    ivec3(-1,-1, 1), ivec3( 1, 1, 1), ivec3(-1, 1, 1),
    /* back (n = 0,0,-1) */
    ivec3( 1,-1,-1), ivec3(-1,-1,-1), ivec3(-1, 1,-1),
    ivec3( 1,-1,-1), ivec3(-1, 1,-1), ivec3( 1, 1,-1),
    /* left (n = -1,0,0) */
    ivec3(-1,-1, 1), ivec3(-1,-1,-1), ivec3(-1, 1,-1),
    ivec3(-1,-1, 1), ivec3(-1, 1,-1), ivec3(-1, 1, 1),
    /* right (n = 1,0,0) */
    ivec3( 1,-1,-1), ivec3( 1,-1, 1), ivec3( 1, 1, 1),
    ivec3( 1,-1,-1), ivec3( 1, 1, 1), ivec3( 1, 1,-1)
};
const vec3 GDD_BOX_FACE_N[6] = {
    vec3( 0.0,  1.0,  0.0),
    vec3( 0.0, -1.0,  0.0),
    vec3( 0.0,  0.0,  1.0),
    vec3( 0.0,  0.0, -1.0),
    vec3(-1.0,  0.0,  0.0),
    vec3( 1.0,  0.0,  0.0)
};

/* ------------------------------------------------------------------ */
/* The expand passes' global buffers are declared by the entry shader */
/* (GLSL 450 has no pointers or references): every gdd_expand.comp    */
/* must define                                                          */
/*   layout(binding = 2) buffer GddCntB  { GddCounts c; } gdd_counts;  */
/*   layout(binding = 3) buffer GddVtxB  { GddVertex v[]; } gdd_verts; */
/* ------------------------------------------------------------------ */

