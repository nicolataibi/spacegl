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

/* world = pos + orient * (local * scale) */
vec3 gdd_xform(GddInstance it, vec3 lp) {
    return it.a.xyz + it.o * (lp * it.b.xyz);
}

void gdd_expand_box(uint base, GddInstance it) {
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
