/*
 * SPACE GL - GDD compute pass operations (gdd_ops).
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * Included by gdd_expand.comp AFTER the global buffer declarations
 * (GLSL requires the buffers to be in scope before the functions that
 * use them). Requires gdd_common.glsl (structs/tables) and the entry
 * shader's declarations:
 *   layout(binding = 2) buffer GddCntB { GddCounts c; } gdd_counts;
 *   layout(binding = 3) buffer GddVtxB { GddVertex v[]; } gdd_verts;
 * (GPL-3.0-or-later, see the full license header in gdd_common.glsl)
 */

/*
 * Reserve `n` vertex slots in the pass (opaque/additive) owned by the
 * instance. The reservation is atomicAdd-based: the counter may be
 * TRANSIENTLY inflated while a concurrent reservation rolls back, but
 * the final value is exact, and a rolled-back reservation never writes
 * any vertex, so no two live regions can overlap. If the capacity
 * guard fails the reservation is rolled back and the instance is
 * dropped (same "drop when full" behavior as the legacy CPU path).
 */
bool gdd_reserve(GddInstance it, uint n, out uint base) {
    bool add = gdd_additive(it);
    if (add) {
        uint local_idx = atomicAdd(gdd_counts.c.additive_verts, n);
        base = gdd_counts.c.opaque_verts + local_idx;
    } else {
        base = atomicAdd(gdd_counts.c.opaque_verts, n);
    }
    uint total = gdd_counts.c.opaque_verts + gdd_counts.c.additive_verts;
    if (total > pc.capacity) {
        if (add) {
            atomicAdd(gdd_counts.c.additive_verts, uint(-n));
        } else {
            atomicAdd(gdd_counts.c.opaque_verts, uint(-n));
        }
        return false;
    }
    return true;
}

/* Write one generated vertex (world pos, local pos, world normal,
 * instance color/mode/material). */
void gdd_put(uint idx, vec3 wpos, vec3 lpos, vec3 nrm, GddInstance it) {
    GddVertex v;
    v.pos    = vec4(wpos, 0.0);
    v.color  = vec4(it.c.xyz, it.c.w);
    v.normal = vec4(nrm, gdd_frag_mode(it));
    v.local  = vec4(lpos, 0.0);
    v.params = vec4(it.p.x, it.p.y, 0.0, 0.0);
    gdd_verts.v[idx] = v;
}

/* Same as gdd_put, but params carries the per-vertex BARYCENTRIC coords
 * (used by the scene fragment shader for the GDD_FRAG_WIREFRAME(_PBR)
 * procedural line rendering — the triangle counterpart of the CPU path's
 * LINE_LIST wireframe pipeline). */
void gdd_put_bary(uint idx, vec3 wpos, vec3 lpos, vec3 nrm, GddInstance it, vec3 bary) {
    GddVertex v;
    v.pos    = vec4(wpos, 0.0);
    v.color  = vec4(it.c.xyz, it.c.w);
    v.normal = vec4(nrm, gdd_frag_mode(it));
    v.local  = vec4(lpos, 0.0);
    v.params = vec4(bary, 0.0);
    gdd_verts.v[idx] = v;
}

/* TRUE when the instance wants the barycentric wireframe variants
 * (GDD_FRAG_WIREFRAME = 11, GDD_FRAG_WIREFRAME_PBR = 12). */
bool gdd_want_bary(GddInstance it) {
    float m = gdd_frag_mode(it);
    return (m == 11.0) || (m == 12.0);
}

/* world = pos + orient * (local * scale) */
vec3 gdd_xform(GddInstance it, vec3 lp) {
    return it.a.xyz + it.o * (lp * it.b.xyz);
}

void gdd_expand_box(uint base, GddInstance it) {
    if (gdd_want_bary(it)) {
        /* Barycentric wireframe variant: same 36-vertex layout, params =
         * barycentric coords (per triangle, 3 verts each), face normals
         * kept (unlit modes do not use them). */
        for (int i = 0; i < 36; i++) {
            vec3 unscaled_lp = vec3(GDD_BOX_SIGNS[i]);
            vec3 lp = unscaled_lp * it.b.xyz;
            vec3 bary = (uint(i) % 3u == 0u) ? vec3(1, 0, 0)
                       : (uint(i) % 3u == 1u) ? vec3(0, 1, 0)
                       : vec3(0, 0, 1);
            gdd_put_bary(base + uint(i), gdd_xform(it, unscaled_lp), lp,
                         it.o * GDD_BOX_FACE_N[i / 6], it, bary);
        }
        return;
    }
    for (int i = 0; i < 36; i++) {
        vec3 unscaled_lp = vec3(GDD_BOX_SIGNS[i]);
        vec3 lp = unscaled_lp * it.b.xyz;
        gdd_put(base + uint(i), gdd_xform(it, unscaled_lp), lp,
                it.o * GDD_BOX_FACE_N[i / 6], it);
    }
}

void gdd_expand_sphere(uint base, GddInstance it) {
    float PI = 3.141592653589793;
    float r = it.b.x;
    for (int i = 0; i < GDD_SPHERE_LATS; i++) {
        float lat0 = PI * float(i) / float(GDD_SPHERE_LATS);
        float lat1 = PI * float(i + 1) / float(GDD_SPHERE_LATS);
        float cl0 = cos(lat0), sl0 = sin(lat0);
        float cl1 = cos(lat1), sl1 = sin(lat1);
        for (int j = 0; j < GDD_SPHERE_LONS; j++) {
            float lon0 = 2.0 * PI * float(j) / float(GDD_SPHERE_LONS);
            float lon1 = 2.0 * PI * float(j + 1) / float(GDD_SPHERE_LONS);
            float c0 = cos(lon0), s0 = sin(lon0);
            float c1 = cos(lon1), s1 = sin(lon1);
            vec3 d00 = vec3(cl0 * c0, sl0, cl0 * s0);
            vec3 d10 = vec3(cl1 * c0, sl1, cl1 * s0);
            vec3 d01 = vec3(cl0 * c1, sl0, cl0 * s1);
            vec3 d11 = vec3(cl1 * c1, sl1, cl1 * s1);
            uint b0 = base + uint(i * GDD_SPHERE_LONS + j) * 6u;
            gdd_put(b0 + 0u, it.a.xyz + d00 * r, d00 * r, d00, it);
            gdd_put(b0 + 1u, it.a.xyz + d10 * r, d10 * r, d10, it);
            gdd_put(b0 + 2u, it.a.xyz + d01 * r, d01 * r, d01, it);
            gdd_put(b0 + 3u, it.a.xyz + d10 * r, d10 * r, d10, it);
            gdd_put(b0 + 4u, it.a.xyz + d11 * r, d11 * r, d11, it);
            gdd_put(b0 + 5u, it.a.xyz + d01 * r, d01 * r, d01, it);
        }
    }
}

void gdd_expand_pyramid(uint base, GddInstance it) {
    /* Base: unit YZ square at local x = -0.5 (half extent 0.5, so the
     * whole primitive lives in [-0.5, 0.5]^3); nose at (0.5, 0, 0). */
    vec3 c0 = vec3(-0.5, -0.5, -0.5);
    vec3 c1 = vec3(-0.5,  0.5, -0.5);
    vec3 c2 = vec3(-0.5,  0.5,  0.5);
    vec3 c3 = vec3(-0.5, -0.5,  0.5);
    vec3 ap = vec3(0.5, 0.0, 0.0);

    if (gdd_want_bary(it)) {
        /* Barycentric wireframe variant (GDD_FRAG_WIREFRAME(_PBR)):
         * same 18-vertex layout, params = barycentric coords. Mode 12
         * (CPU ships) uses the CPU ship's constant vertex normal
         * (1,0,0) rotated by the instance orientation for the PBR
         * shading; mode 11 keeps the face normals (unlit, unused). */
        vec3 n12 = it.o * vec3(1, 0, 0);
        /* Base (normal -X): 2 triangles */
        gdd_put_bary(base + 0u, gdd_xform(it, c0), c0, vec3(-1, 0, 0), it, vec3(1, 0, 0));
        gdd_put_bary(base + 1u, gdd_xform(it, c3), c3, vec3(-1, 0, 0), it, vec3(0, 1, 0));
        gdd_put_bary(base + 2u, gdd_xform(it, c2), c2, vec3(-1, 0, 0), it, vec3(0, 0, 1));
        gdd_put_bary(base + 3u, gdd_xform(it, c0), c0, vec3(-1, 0, 0), it, vec3(1, 0, 0));
        gdd_put_bary(base + 4u, gdd_xform(it, c2), c2, vec3(-1, 0, 0), it, vec3(0, 1, 0));
        gdd_put_bary(base + 5u, gdd_xform(it, c1), c1, vec3(-1, 0, 0), it, vec3(0, 0, 1));
        /* Sides: 4 triangles (bary per triangle; mode 12: normal n12) */
        vec3 sn = it.o * vec3(0, 0, -1);
        gdd_put_bary(base + 6u,  gdd_xform(it, ap), ap, gdd_frag_mode(it) == 12.0 ? n12 : sn, it, vec3(1, 0, 0));
        gdd_put_bary(base + 7u,  gdd_xform(it, c0), c0, gdd_frag_mode(it) == 12.0 ? n12 : sn, it, vec3(0, 1, 0));
        gdd_put_bary(base + 8u,  gdd_xform(it, c1), c1, gdd_frag_mode(it) == 12.0 ? n12 : sn, it, vec3(0, 0, 1));
        sn = it.o * vec3(0, 1, 0);
        gdd_put_bary(base + 9u,  gdd_xform(it, ap), ap, gdd_frag_mode(it) == 12.0 ? n12 : sn, it, vec3(1, 0, 0));
        gdd_put_bary(base + 10u, gdd_xform(it, c1), c1, gdd_frag_mode(it) == 12.0 ? n12 : sn, it, vec3(0, 1, 0));
        gdd_put_bary(base + 11u, gdd_xform(it, c2), c2, gdd_frag_mode(it) == 12.0 ? n12 : sn, it, vec3(0, 0, 1));
        sn = it.o * vec3(0, 0, 1);
        gdd_put_bary(base + 12u, gdd_xform(it, ap), ap, gdd_frag_mode(it) == 12.0 ? n12 : sn, it, vec3(1, 0, 0));
        gdd_put_bary(base + 13u, gdd_xform(it, c2), c2, gdd_frag_mode(it) == 12.0 ? n12 : sn, it, vec3(0, 1, 0));
        gdd_put_bary(base + 14u, gdd_xform(it, c3), c3, gdd_frag_mode(it) == 12.0 ? n12 : sn, it, vec3(0, 0, 1));
        sn = it.o * vec3(0, -1, 0);
        gdd_put_bary(base + 15u, gdd_xform(it, ap), ap, gdd_frag_mode(it) == 12.0 ? n12 : sn, it, vec3(1, 0, 0));
        gdd_put_bary(base + 16u, gdd_xform(it, c3), c3, gdd_frag_mode(it) == 12.0 ? n12 : sn, it, vec3(0, 1, 0));
        gdd_put_bary(base + 17u, gdd_xform(it, c0), c0, gdd_frag_mode(it) == 12.0 ? n12 : sn, it, vec3(0, 0, 1));
        return;
    }

    /* Base (normal -X): 2 triangles */
    gdd_put(base + 0u, gdd_xform(it, c0), c0, vec3(-1, 0, 0), it);
    gdd_put(base + 1u, gdd_xform(it, c3), c3, vec3(-1, 0, 0), it);
    gdd_put(base + 2u, gdd_xform(it, c2), c2, vec3(-1, 0, 0), it);
    gdd_put(base + 3u, gdd_xform(it, c0), c0, vec3(-1, 0, 0), it);
    gdd_put(base + 4u, gdd_xform(it, c2), c2, vec3(-1, 0, 0), it);
    gdd_put(base + 5u, gdd_xform(it, c1), c1, vec3(-1, 0, 0), it);
    /* Sides: 4 triangles (flat face normals) */
    gdd_put(base + 6u,  gdd_xform(it, ap), ap, vec3(0, 0, -1), it);
    gdd_put(base + 7u,  gdd_xform(it, c0), c0, vec3(0, 0, -1), it);
    gdd_put(base + 8u,  gdd_xform(it, c1), c1, vec3(0, 0, -1), it);
    gdd_put(base + 9u,  gdd_xform(it, ap), ap, vec3(0, 1, 0), it);
    gdd_put(base + 10u, gdd_xform(it, c1), c1, vec3(0, 1, 0), it);
    gdd_put(base + 11u, gdd_xform(it, c2), c2, vec3(0, 1, 0), it);
    gdd_put(base + 12u, gdd_xform(it, ap), ap, vec3(0, 0, 1), it);
    gdd_put(base + 13u, gdd_xform(it, c2), c2, vec3(0, 0, 1), it);
    gdd_put(base + 14u, gdd_xform(it, c3), c3, vec3(0, 0, 1), it);
    gdd_put(base + 15u, gdd_xform(it, ap), ap, vec3(0, -1, 0), it);
    gdd_put(base + 16u, gdd_xform(it, c3), c3, vec3(0, -1, 0), it);
    gdd_put(base + 17u, gdd_xform(it, c0), c0, vec3(0, -1, 0), it);
}

void gdd_expand_octa(uint base, GddInstance it) {
    /* 6-axis octahedron in unit space (corners scaled by it.b.xyz via
     * gdd_xform); 8 faces x 3 verts. Normals = face diagonals. */
    vec3 v0 = vec3( 1, 0, 0); vec3 v1 = vec3(-1, 0, 0);
    vec3 v2 = vec3( 0, 1, 0); vec3 v3 = vec3( 0,-1, 0);
    vec3 v4 = vec3( 0, 0, 1); vec3 v5 = vec3( 0, 0,-1);
    float nrm = 0.5773502691896258; /* 1/sqrt(3) */
    vec3 n[8];
    n[0] = vec3( 1, 1, 1) * nrm; n[1] = vec3( 1,-1, 1) * nrm;
    n[2] = vec3( 1, 1,-1) * nrm; n[3] = vec3( 1,-1,-1) * nrm;
    n[4] = vec3(-1, 1, 1) * nrm; n[5] = vec3(-1,-1, 1) * nrm;
    n[6] = vec3(-1, 1,-1) * nrm; n[7] = vec3(-1,-1,-1) * nrm;
    vec3 f[24];
    f[0] = v0; f[1] = v2; f[2] = v4;
    f[3] = v2; f[4] = v5; f[5] = v0;
    f[6] = v0; f[7] = v4; f[8] = v3;
    f[9] = v0; f[10] = v3; f[11] = v5;
    f[12] = v1; f[13] = v4; f[14] = v2;
    f[15] = v1; f[16] = v5; f[17] = v4;
    f[18] = v1; f[19] = v2; f[20] = v3;
    f[21] = v1; f[22] = v3; f[23] = v5;
    if (gdd_want_bary(it)) {
        /* Barycentric wireframe variant: same 24-vertex layout, params =
         * barycentric coords (per triangle, 3 verts each). */
        for (int i = 0; i < 24; i++) {
            vec3 lp = f[i] * it.b.xyz;
            vec3 bary = (uint(i) % 3u == 0u) ? vec3(1, 0, 0)
                       : (uint(i) % 3u == 1u) ? vec3(0, 1, 0)
                       : vec3(0, 0, 1);
            gdd_put_bary(base + uint(i), gdd_xform(it, f[i]), lp, it.o * n[i / 3], it, bary);
        }
        return;
    }
    for (int i = 0; i < 24; i++) {
        vec3 lp = f[i] * it.b.xyz;
        gdd_put(base + uint(i), gdd_xform(it, f[i]), lp, it.o * n[i / 3], it);
    }
}

void gdd_expand_ring(uint base, GddInstance it) {
    /* Flat annulus in local XZ; 16 segments x 2 triangles.
     * it.p.x (metallic field): when > 0, interpreted as half-thickness
     * in world units (same semantic as gdd_expand_line), converted to
     * inner radius ratio as  inner = 1 - ht / radius.
     * When 0 → default ratio 0.75 (thick decorative rings). */
    float ht = it.p.x;
    float inner = (ht > 1e-4)
                ? clamp(1.0 - ht / max(it.b.x, 1e-4), 0.0, 0.99)
                : 0.75;
    for (int j = 0; j < GDD_RING_SEGS; j++) {
        float a0 = 6.283185307179586 * float(j) / float(GDD_RING_SEGS);
        float a1 = 6.283185307179586 * float(j + 1) / float(GDD_RING_SEGS);
        float c0 = cos(a0), s0 = sin(a0);
        float c1 = cos(a1), s1 = sin(a1);
        vec3 i0 = vec3(c0 * inner, 0.0, s0 * inner);
        vec3 i1 = vec3(c1 * inner, 0.0, s1 * inner);
        vec3 o0 = vec3(c0, 0.0, s0);
        vec3 o1 = vec3(c1, 0.0, s1);
        uint b0 = base + uint(j) * 6u;
        gdd_put(b0 + 0u, gdd_xform(it, i0), i0 * it.b.x, vec3(0, 1, 0), it);
        gdd_put(b0 + 1u, gdd_xform(it, i1), i1 * it.b.x, vec3(0, 1, 0), it);
        gdd_put(b0 + 2u, gdd_xform(it, o1), o1 * it.b.x, vec3(0, 1, 0), it);
        gdd_put(b0 + 3u, gdd_xform(it, i0), i0 * it.b.x, vec3(0, 1, 0), it);
        gdd_put(b0 + 4u, gdd_xform(it, o1), o1 * it.b.x, vec3(0, 1, 0), it);
        gdd_put(b0 + 5u, gdd_xform(it, o0), o0 * it.b.x, vec3(0, 1, 0), it);
    }
}

void gdd_expand_circle(uint base, GddInstance it) {
    /* Thin wireframe circle in the local XZ plane (unit space; the
     * instance's x-scale is the radius, same convention as GDD_MESH_RING):
     * 72 segments x 2 triangles — the same 5-degree tessellation as the
     * CPU compass circle (CIRCLE_SEGMENTS = 72), so the two paths render
     * the same smooth ring instead of the faceted 16-seg annulus.
     * it.p.x (metallic) = half-thickness of the band in world units;
     * clamped to the same screen-space floor as gdd_expand_line
     * (pc.line_min_wu) so the ring keeps the 1-px visual weight of the
     * CPU-driven MSAA lines. 0 -> default 0.02. */
    float r = max(it.b.x, 1e-4);
    float t = (it.p.x > 1e-4) ? it.p.x : 0.02;
    t = max(t, pc.line_min_wu);
    float rin  = clamp((r - t) / r, 1e-4, 1.0);
    float rout = (r + t) / r;
    for (int j = 0; j < GDD_CIRCLE_SEGS; j++) {
        float a0 = 6.283185307179586 * float(j) / float(GDD_CIRCLE_SEGS);
        float a1 = 6.283185307179586 * float(j + 1) / float(GDD_CIRCLE_SEGS);
        vec3 i0 = vec3(cos(a0), 0.0, sin(a0)) * rin;
        vec3 i1 = vec3(cos(a1), 0.0, sin(a1)) * rin;
        vec3 o0 = vec3(cos(a0), 0.0, sin(a0)) * rout;
        vec3 o1 = vec3(cos(a1), 0.0, sin(a1)) * rout;
        uint b0 = base + uint(j) * 6u;
        gdd_put(b0 + 0u, gdd_xform(it, i0), i0 * it.b.x, it.o * vec3(0, 1, 0), it);
        gdd_put(b0 + 1u, gdd_xform(it, i1), i1 * it.b.x, it.o * vec3(0, 1, 0), it);
        gdd_put(b0 + 2u, gdd_xform(it, o1), o1 * it.b.x, it.o * vec3(0, 1, 0), it);
        gdd_put(b0 + 3u, gdd_xform(it, i0), i0 * it.b.x, it.o * vec3(0, 1, 0), it);
        gdd_put(b0 + 4u, gdd_xform(it, o1), o1 * it.b.x, it.o * vec3(0, 1, 0), it);
        gdd_put(b0 + 5u, gdd_xform(it, o0), o0 * it.b.x, it.o * vec3(0, 1, 0), it);
    }
}

void gdd_expand_arc(uint base, GddInstance it) {
    /* Thin 180-degree wireframe arc in the local XY (vertical) plane:
     * sweeps from -90 degrees (local -Y) through 0 degrees (local +X,
     * the nose direction) to +90 degrees (+Y), 36 segments of 5 degrees
     * — the same sweep/tessellation as the CPU compass mark arc
     * (ARC_SEGMENTS = 37 points). The instance orientation tilts the
     * whole arc (the CPU path applies RotY(90-deg - heading) to it).
     * Half-thickness semantics identical to gdd_expand_circle. */
    float r = max(it.b.x, 1e-4);
    float t = (it.p.x > 1e-4) ? it.p.x : 0.02;
    t = max(t, pc.line_min_wu);
    float rin  = clamp((r - t) / r, 1e-4, 1.0);
    float rout = (r + t) / r;
    float PI = 3.141592653589793;
    for (int j = 0; j < GDD_ARC_SEGS; j++) {
        float a0 = PI * float(j) / float(GDD_ARC_SEGS) - PI * 0.5;
        float a1 = PI * float(j + 1) / float(GDD_ARC_SEGS) - PI * 0.5;
        vec3 i0 = vec3(cos(a0), sin(a0), 0.0) * rin;
        vec3 i1 = vec3(cos(a1), sin(a1), 0.0) * rin;
        vec3 o0 = vec3(cos(a0), sin(a0), 0.0) * rout;
        vec3 o1 = vec3(cos(a1), sin(a1), 0.0) * rout;
        uint b0 = base + uint(j) * 6u;
        gdd_put(b0 + 0u, gdd_xform(it, i0), i0 * it.b.x, it.o * vec3(0, 0, 1), it);
        gdd_put(b0 + 1u, gdd_xform(it, i1), i1 * it.b.x, it.o * vec3(0, 0, 1), it);
        gdd_put(b0 + 2u, gdd_xform(it, o1), o1 * it.b.x, it.o * vec3(0, 0, 1), it);
        gdd_put(b0 + 3u, gdd_xform(it, i0), i0 * it.b.x, it.o * vec3(0, 0, 1), it);
        gdd_put(b0 + 4u, gdd_xform(it, o1), o1 * it.b.x, it.o * vec3(0, 0, 1), it);
        gdd_put(b0 + 5u, gdd_xform(it, o0), o0 * it.b.x, it.o * vec3(0, 0, 1), it);
    }
}

void gdd_expand_line(uint base, GddInstance it) {
    /* Square-tube cross-section: 4 rectangular faces × 6 verts = 24.
     * Visible from any camera angle, no Z-fighting, clean corners.
     * it.p.x (metallic) = half-thickness in world units (0 → 0.02). */
    vec3 e = it.b.xyz;
    float len = length(e);
    vec3 d = (len > 1e-6) ? (e / len) : vec3(1, 0, 0);
    vec3 ref = (abs(d.x) > 0.9) ? vec3(0, 1, 0) : vec3(1, 0, 0);
    vec3 p1 = normalize(cross(d, ref));
    vec3 p2 = normalize(cross(d, p1));
    float t = (it.p.x > 1e-4) ? it.p.x : 0.02;
    vec3 s  = it.a.xyz;
    vec3 f  = it.a.xyz + e;
    /* Screen-space floor: the CPU pushes 0.5 * GDD_LINE_MIN_PX * wu/px
     * for the current camera, so the tube is never thinner than
     * GDD_LINE_MIN_PX pixels — same semantics as the 1-px MSAA lines of
     * the CPU-driven path (a sub-pixel tube would rasterize as gaps /
     * flicker). 0 in the unit tests (floor disabled). */
    t = max(t, pc.line_min_wu);
    /* 4 tube corners at start (s*) and end (f*) */
    vec3 sA = s + p1*t + p2*t;
    vec3 sB = s - p1*t + p2*t;
    vec3 sC = s - p1*t - p2*t;
    vec3 sD = s + p1*t - p2*t;
    vec3 fA = f + p1*t + p2*t;
    vec3 fB = f - p1*t + p2*t;
    vec3 fC = f - p1*t - p2*t;
    vec3 fD = f + p1*t - p2*t;
    /* Face +p1 */
    gdd_put(base +  0u, sA, sA,  p1, it);
    gdd_put(base +  1u, sD, sD,  p1, it);
    gdd_put(base +  2u, fD, fD,  p1, it);
    gdd_put(base +  3u, sA, sA,  p1, it);
    gdd_put(base +  4u, fD, fD,  p1, it);
    gdd_put(base +  5u, fA, fA,  p1, it);
    /* Face -p1 */
    gdd_put(base +  6u, sB, sB, -p1, it);
    gdd_put(base +  7u, sC, sC, -p1, it);
    gdd_put(base +  8u, fC, fC, -p1, it);
    gdd_put(base +  9u, sB, sB, -p1, it);
    gdd_put(base + 10u, fC, fC, -p1, it);
    gdd_put(base + 11u, fB, fB, -p1, it);
    /* Face +p2 */
    gdd_put(base + 12u, sA, sA,  p2, it);
    gdd_put(base + 13u, sB, sB,  p2, it);
    gdd_put(base + 14u, fB, fB,  p2, it);
    gdd_put(base + 15u, sA, sA,  p2, it);
    gdd_put(base + 16u, fB, fB,  p2, it);
    gdd_put(base + 17u, fA, fA,  p2, it);
    /* Face -p2 */
    gdd_put(base + 18u, sC, sC, -p2, it);
    gdd_put(base + 19u, sD, sD, -p2, it);
    gdd_put(base + 20u, fD, fD, -p2, it);
    gdd_put(base + 21u, sC, sC, -p2, it);
    gdd_put(base + 22u, fD, fD, -p2, it);
    gdd_put(base + 23u, fC, fC, -p2, it);
}

void gdd_expand_boxwire(uint base, GddInstance it) {
    /* The 12 edges of the unit box (±0.5)^3 (it.b.xyz = half-extents,
     * same convention as GDD_MESH_BOX), each edge a square tube built
     * EXACTLY like gdd_expand_line — the GDD counterpart of the CPU
     * path's LINE_LIST cube (cubeIndices, 24 indices = 12 edges). One
     * instance per wireframe box (288 verts) instead of 12
     * GDD_MESH_LINE instances: the galaxy map holds up to 64,000
     * non-zero sectors, which must fit the map instance budget.
     * it.p.x (metallic) = tube half-thickness in world units (0 → 0.02),
     * clamped to the same 1-px screen-space floor (pc.line_min_wu). */
    float t = (it.p.x > 1e-4) ? it.p.x : 0.02;
    t = max(t, pc.line_min_wu);
    for (int e = 0; e < 12; e++) {
        vec3 la = GDD_BOXWIRE_EDGES[2 * e];
        vec3 lb = GDD_BOXWIRE_EDGES[2 * e + 1];
        vec3 s = it.a.xyz + it.o * (la * it.b.xyz);
        vec3 f = it.a.xyz + it.o * (lb * it.b.xyz);
        vec3 dv = f - s;
        float len = length(dv);
        vec3 d = (len > 1e-6) ? (dv / len) : vec3(1, 0, 0);
        vec3 ref = (abs(d.x) > 0.9) ? vec3(0, 1, 0) : vec3(1, 0, 0);
        vec3 p1 = normalize(cross(d, ref));
        vec3 p2 = normalize(cross(d, p1));
        vec3 sA = s + p1*t + p2*t;
        vec3 sB = s - p1*t + p2*t;
        vec3 sC = s - p1*t - p2*t;
        vec3 sD = s + p1*t - p2*t;
        vec3 fA = f + p1*t + p2*t;
        vec3 fB = f - p1*t + p2*t;
        vec3 fC = f - p1*t - p2*t;
        vec3 fD = f + p1*t - p2*t;
        uint b = base + uint(e) * 24u;
        /* Face +p1 */
        gdd_put(b +  0u, sA, sA,  p1, it);
        gdd_put(b +  1u, sD, sD,  p1, it);
        gdd_put(b +  2u, fD, fD,  p1, it);
        gdd_put(b +  3u, sA, sA,  p1, it);
        gdd_put(b +  4u, fD, fD,  p1, it);
        gdd_put(b +  5u, fA, fA,  p1, it);
        /* Face -p1 */
        gdd_put(b +  6u, sB, sB, -p1, it);
        gdd_put(b +  7u, sC, sC, -p1, it);
        gdd_put(b +  8u, fC, fC, -p1, it);
        gdd_put(b +  9u, sB, sB, -p1, it);
        gdd_put(b + 10u, fC, fC, -p1, it);
        gdd_put(b + 11u, fB, fB, -p1, it);
        /* Face +p2 */
        gdd_put(b + 12u, sA, sA,  p2, it);
        gdd_put(b + 13u, sB, sB,  p2, it);
        gdd_put(b + 14u, fB, fB,  p2, it);
        gdd_put(b + 15u, sA, sA,  p2, it);
        gdd_put(b + 16u, fB, fB,  p2, it);
        gdd_put(b + 17u, fA, fA,  p2, it);
        /* Face -p2 */
        gdd_put(b + 18u, sC, sC, -p2, it);
        gdd_put(b + 19u, sD, sD, -p2, it);
        gdd_put(b + 20u, fD, fD, -p2, it);
        gdd_put(b + 21u, sC, sC, -p2, it);
        gdd_put(b + 22u, fD, fD, -p2, it);
        gdd_put(b + 23u, fC, fC, -p2, it);
    }
}

void gdd_expand_boom(uint base, GddInstance it) {
    /* CPU boom parity (recordCommandBuffer): 256 particles at
     *     world_p = boom_pos + offset_p * exp,
     * where exp = (1 - life) * 12 * tactScale. The CPU tables hold
     * per-boom RANDOM offsets/colors; the GDD regenerates the SAME
     * distribution procedurally from the per-boom seed (it.p.x):
     *   style 0 (torpedo boom): uniform cubic offsets in [-4,4]^3,
     *     colors cycle by p%6: (0,1,1) (1,0,1) (1,1,0) (1,0,0)
     *     (0,1,0) (1,0.5,0) — CPU IPC_EV_BOOM;
     *   style 1 (dismantle boom): spherical offsets, per-particle speed
     *     3.5..5.5 for p<32 else 1.0..3.5 (CPU IPC_EV_DISMANTLE),
     *     colors: p%3==0 white, p%5==0 (0,0.5,1), else (0.2,0.8,1).
     * Each particle = the same double-sided triangle as GDD_MESH_POINT
     * (radius 0.25 * tactScale, the CPU sphere radius). Instance fields:
     * it.a.xyz = center (world), it.b.x = tactScale, it.c.w = life,
     * it.p.x = seed, it.p.y = style. The per-vertex color is written
     * directly (gdd_put would carry the instance color). */
    float life = it.c.w;
    float ts = max(it.b.x, 1e-4);
    float exp = (1.0 - life) * 12.0 * ts;
    float style = it.p.y;
    float r = 0.25f * ts;
    for (int p = 0; p < 256; p++) {
        float u = float(p);
        float h1 = gdd_boom_hash(u, it.p.x, 1.0);
        float h2 = gdd_boom_hash(u, it.p.x, 2.0);
        float h3 = gdd_boom_hash(u, it.p.x, 3.0);
        vec3 off;
        vec3 col;
        if (style > 0.5) {
            /* spherical: direction from (h1, h2), speed from h3 */
            float theta = h1 * 6.283185307179586;
            float phi = (h2 - 0.5) * 3.141592653589793;
            float speed = (u < 32.0) ? (3.5 + h3 * 2.0) : (1.0 + h3 * 2.5);
            off = vec3(cos(phi) * cos(theta), sin(phi), cos(phi) * sin(theta)) * speed;
            if (mod(u, 3.0) < 0.5)      col = vec3(1.0, 1.0, 1.0);
            else if (mod(u, 5.0) < 0.5) col = vec3(0.0, 0.5, 1.0);
            else                         col = vec3(0.2, 0.8, 1.0);
        } else {
            off = vec3(h1, h2, h3) * 8.0 - 4.0;
            float k = mod(u, 6.0);
            if (k < 0.5)      col = vec3(0.0, 1.0, 1.0);
            else if (k < 1.5) col = vec3(1.0, 0.0, 1.0);
            else if (k < 2.5) col = vec3(1.0, 1.0, 0.0);
            else if (k < 3.5) col = vec3(1.0, 0.0, 0.0);
            else if (k < 4.5) col = vec3(0.0, 1.0, 0.0);
            else              col = vec3(1.0, 0.5, 0.0);
        }
        vec3 wp = it.a.xyz + off * exp;
        /* deterministic pseudo-billboard (same rule as gdd_expand_point) */
        vec3 n, uu, vv;
        if (abs(wp.x) >= abs(wp.y) && abs(wp.x) >= abs(wp.z)) {
            n = vec3(1, 0, 0); uu = vec3(0, 1, 0); vv = vec3(0, 0, 1);
        } else if (abs(wp.y) >= abs(wp.z)) {
            n = vec3(0, 1, 0); uu = vec3(1, 0, 0); vv = vec3(0, 0, 1);
        } else {
            n = vec3(0, 0, 1); uu = vec3(0, 1, 0); vv = vec3(0, 0, 1);
        }
        vec3 a0 = wp + uu * r;
        vec3 a1 = wp - uu * (r * 0.8660254) - vv * (r * 0.5);
        vec3 a2 = wp - uu * (r * 0.8660254) + vv * (r * 0.5);
        GddVertex v;
        v.normal = vec4(n, gdd_frag_mode(it));
        v.local  = vec4(off * exp, 0.0);
        v.params = vec4(0.0, 0.0, 0.0, 0.0);
        v.color  = vec4(col, 1.0);
        v.pos    = vec4(a0, 0.0); gdd_verts.v[base + uint(p) * 6u + 0u] = v;
        v.pos    = vec4(a1, 0.0); gdd_verts.v[base + uint(p) * 6u + 1u] = v;
        v.pos    = vec4(a2, 0.0); gdd_verts.v[base + uint(p) * 6u + 2u] = v;
        v.pos    = vec4(a0, 0.0); gdd_verts.v[base + uint(p) * 6u + 3u] = v;
        v.pos    = vec4(a2, 0.0); gdd_verts.v[base + uint(p) * 6u + 4u] = v;
        v.pos    = vec4(a1, 0.0); gdd_verts.v[base + uint(p) * 6u + 5u] = v;
    }
}

void gdd_expand_point(uint base, GddInstance it) {
    /* Small double-sided triangle (radius it.b.x), plane chosen by the
     * dominant axis of the position (deterministic pseudo-billboard —
     * good enough for far stars and FX pixels). */
    float r = max(it.b.x, 1e-4);
    vec3 p = it.a.xyz;
    vec3 n, u, v;
    if (abs(p.x) >= abs(p.y) && abs(p.x) >= abs(p.z)) {
        n = vec3(1, 0, 0); u = vec3(0, 1, 0); v = vec3(0, 0, 1);
    } else if (abs(p.y) >= abs(p.z)) {
        n = vec3(0, 1, 0); u = vec3(1, 0, 0); v = vec3(0, 0, 1);
    } else {
        n = vec3(0, 0, 1); u = vec3(1, 0, 0); v = vec3(0, 1, 0);
    }
    vec3 a0 = p + u * r;
    vec3 a1 = p - u * (r * 0.8660254) - v * (r * 0.5);
    vec3 a2 = p - u * (r * 0.8660254) + v * (r * 0.5);
    gdd_put(base + 0u, a0, a0, n, it);
    gdd_put(base + 1u, a1, a1, n, it);
    gdd_put(base + 2u, a2, a2, n, it);
    gdd_put(base + 3u, a0, a0, n, it);
    gdd_put(base + 4u, a2, a2, n, it);
    gdd_put(base + 5u, a1, a1, n, it);
}
