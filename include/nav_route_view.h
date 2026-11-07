/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * nav_route_view.h - how the collision-free course of `nav` looks, shared
 * by the three 3D views (OpenGL, Vulkan CPU-driven, Vulkan GPU-driven).
 *
 * The deck puts the course in shared memory (GameState.route: the waypoints
 * still to fly, galactic coordinates, and the obstacles it bends around).
 * nrv_build() turns it, for the quadrant on screen, into a list of four
 * kinds of primitives in viewer coordinates (the cube centred on the
 * origin, Y up: viewer = (sx - H, sz - H, H - sy), H = QG_HALF), unscaled:
 *
 *   NRV_TUBE  glowing segment a -> b, half thickness r   (additive)
 *   NRV_LINE  hairline a -> b                              (additive)
 *   NRV_RING  circle centred on a, normal b, radius r      (additive)
 *   NRV_DOT   glowing sphere centred on a, radius r        (additive)
 *
 * Each view only maps the four kinds onto its own draw calls, so the course
 * looks the same everywhere. What is drawn, from the ship to the point where
 * the course leaves the quadrant, or to its end when that is in the quadrant:
 *
 *   - the course: rounded corners (a fillet at every waypoint), a wide soft
 *     glow and a bright core, cyan near the ship turning violet toward the
 *     end (orange-red when the course stops short: NAV_RF_PARTIAL);
 *   - energy pulses with a short tail flowing along it, from the ship on,
 *     and arrowheads lit by a wave that runs the same way;
 *   - a spinning diamond and a ring across the course at every waypoint;
 *   - an amber keep-out bubble (three great circles, a scanning latitude
 *     ring and a gyroscope ring) around every obstacle the course bends
 *     around (violet for a black hole);
 *   - at the end: a beacon (vertical beam, expanding rings, crosshair), or a
 *     red stop mark when the course stops short; where the course leaves
 *     the quadrant: an exit gate on the face, with a tunnel of frames and
 *     arrowheads pointing out;
 *   - its shadow on the floor of the cube and drop lines from the waypoints,
 *     to read the height at a glance.
 *
 * Pure C99 + <math.h>: no GL, no Vulkan, no shared memory (the adapter at
 * the bottom is compiled only where shared_state.h was included first).
 * Tests: tests/nav_route_view_test.c.
 */
#ifndef NAV_ROUTE_VIEW_H
#define NAV_ROUTE_VIEW_H

#include <math.h>
#include <string.h>
#include "game_config.h"

#define NRV_MAX_WP          64
#define NRV_MAX_HAZ         16
#define NRV_MAX_PTS         768     /* points of the drawn (rounded, clipped) course  */
#define NRV_MAX_PRIMS       1600
#define NRV_FLAG_PARTIAL    (1 << 3)   /* = NAV_RF_PARTIAL (include/nav_route.h) */
#define NRV_KIND_BLACK_HOLE 7       /* = OBS_BLACK_HOLE (include/obstacles.h) */
#define NRV_KIND_SHIP       16      /* = OBS_SHIP                             */

enum { NRV_TUBE = 1,
       NRV_LINE = 2,
       NRV_RING = 3,
       NRV_DOT = 4 };

typedef struct {
    int type;      /* NRV_*                                            */
    float a[3];      /* TUBE/LINE: start; RING/DOT: centre                */
    float b[3];      /* TUBE/LINE: end;   RING: unit normal               */
    float r;         /* TUBE: half thickness; RING/DOT: radius            */
    float c[4];      /* colour, alpha                                     */
} NrvPrim;

typedef struct {
    int active;
    int flags;                     /* NAV_RF_*                               */
    int count;                     /* waypoints still to fly                 */
    float wp[NRV_MAX_WP][3];         /* galactic coordinates                   */
    int n_haz;
    float haz[NRV_MAX_HAZ][4];       /* galactic centre + keep-out radius      */
    int haz_kind[NRV_MAX_HAZ];
    int q[3];                      /* quadrant on screen                     */
    float ship[3];                   /* ship, sector coordinates (0..QUADRANT_SIZE) */
    float time;                      /* seconds, drives the animation          */
} NrvInput;

typedef struct NavRouteView {
    /* the course on screen, viewer coordinates */
    int n;
    float p[NRV_MAX_PTS][3];
    float s[NRV_MAX_PTS];            /* distance along it from the ship        */
    float length;
    int exits;                     /* 1: leaves the quadrant at exit_p       */
    float exit_p[3];
    int exit_axis, exit_sign;      /* sector axis 0..2 (x, y, z) and +1 / -1 */
    int exit_q[3];                 /* the quadrant it goes on to             */
    int ends_here;                 /* 1: the end of the course is on screen  */
    float end_p[3];
    int partial;
    int n_wp;                      /* waypoint markers on screen             */
    float wp[NRV_MAX_WP][3];
    int n_haz;                     /* keep-out bubbles on screen             */
    float haz[NRV_MAX_HAZ][4];
    int haz_kind[NRV_MAX_HAZ];
    /* what to draw */
    int np;
    NrvPrim prim[NRV_MAX_PRIMS];
} NavRouteView;

/* Name of an obstacle kind (OBS_* of include/obstacles.h), for labels. */
static inline const char *nrv_kind_name(int kind) {
    static const char *const names[17] = {
        "object", "planet", "star", "moon", "asteroid", "starbase", "derelict",
        "black hole", "pulsar", "neutron star", "brown dwarf", "rogue planet",
        "comet", "star", "planet", "moon", "ship"
    };
    return (kind > 0 && kind < 17) ? names[kind] : names[0];
}

/* --- helpers ------------------------------------------------------------ */

static inline void nrv_to_view(const float s[3], float v[3]) {
    const float H = (float)(QUADRANT_SIZE * 0.5);
    v[0] = s[0] - H;
    v[1] = s[2] - H;
    v[2] = H - s[1];
}

static inline float nrv_len3(const float a[3]) { return sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }

static inline float nrv_dist(const float a[3], const float b[3]) {
    float d[3] = { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
    return nrv_len3(d);
}

static inline void nrv_norm(float v[3]) {
    float l = nrv_len3(v);
    if (l > 1e-9f) {
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    }
}

static inline void nrv_cross(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

/* Two unit vectors perpendicular to the unit t (side s, up u). */
static inline void nrv_basis(const float t[3], float s[3], float u[3]) {
    float up[3] = { 0.0f, 1.0f, 0.0f };
    if (fabsf(t[1]) > 0.95f) {
        up[0] = 1.0f;
        up[1] = 0.0f;
    }
    nrv_cross(t, up, s);
    nrv_norm(s);
    nrv_cross(s, t, u);
    nrv_norm(u);
}

static inline void nrv_mix(const float a[3], const float b[3], float k, float o[3]) {
    o[0] = a[0] + (b[0] - a[0]) * k;
    o[1] = a[1] + (b[1] - a[1]) * k;
    o[2] = a[2] + (b[2] - a[2]) * k;
}

static inline NrvPrim *nrv_add(NavRouteView *v, int type, const float a[3], const float b[3], float r,
                               const float rgb[3], float alpha) {
    if (v->np >= NRV_MAX_PRIMS || alpha <= 0.003f) return NULL;
    NrvPrim *p = &v->prim[v->np++];
    p->type = type;
    p->a[0] = a[0];
    p->a[1] = a[1];
    p->a[2] = a[2];
    if (b) {
        p->b[0] = b[0];
        p->b[1] = b[1];
        p->b[2] = b[2];
    } else {
        p->b[0] = 0.0f;
        p->b[1] = 1.0f;
        p->b[2] = 0.0f;
    }
    p->r = r;
    p->c[0] = rgb[0];
    p->c[1] = rgb[1];
    p->c[2] = rgb[2];
    p->c[3] = alpha > 1.0f ? 1.0f : alpha;
    return p;
}

/* Point and unit tangent at distance s along the drawn course. */
static inline void nrv_at(const NavRouteView *v, float s, float p[3], float t[3]) {
    int k = 1;
    while (k < v->n - 1 && v->s[k] < s) k++;
    float seg = v->s[k] - v->s[k - 1];
    float f = (seg > 1e-6f) ? (s - v->s[k - 1]) / seg : 0.0f;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    nrv_mix(v->p[k - 1], v->p[k], f, p);
    t[0] = v->p[k][0] - v->p[k - 1][0];
    t[1] = v->p[k][1] - v->p[k - 1][1];
    t[2] = v->p[k][2] - v->p[k - 1][2];
    nrv_norm(t);
}

/* Colour of the course at fraction f of the drawn length. */
static inline void nrv_course_rgb(const NavRouteView *v, float f, float rgb[3]) {
    static const float c0[3] = { 0.20f, 0.90f, 1.00f };   /* cyan, at the ship   */
    static const float c1[3] = { 0.80f, 0.40f, 1.00f };   /* violet, at the end  */
    static const float cr[3] = { 1.00f, 0.35f, 0.15f };   /* stops short: red    */
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    if (v->partial && v->ends_here && f > 0.6f) {
        float mid[3];
        nrv_mix(c0, c1, 0.6f, mid);
        nrv_mix(mid, cr, (f - 0.6f) / 0.4f, rgb);
    } else {
        nrv_mix(c0, c1, f, rgb);
    }
}

/* Point list: append p unless it repeats the last one. */
static inline void nrv_push_pt(float (*pts)[3], int *n, int max, const float p[3]) {
    if (*n > 0) {
        const float *l = pts[*n - 1];
        if (fabsf(l[0] - p[0]) < 1e-5f && fabsf(l[1] - p[1]) < 1e-5f && fabsf(l[2] - p[2]) < 1e-5f) return;
    }
    if (*n >= max) return;
    pts[*n][0] = p[0];
    pts[*n][1] = p[1];
    pts[*n][2] = p[2];
    (*n)++;
}

/* Parameter in [0,1] where the segment a -> b leaves the box [0, S]^3
 * (a inside), -1 when it does not; *axis / *sign name the face. */
static inline float nrv_exit_t(const float a[3], const float b[3], float S, int *axis, int *sign) {
    float best = 2.0f;
    for (int k = 0; k < 3; k++) {
        float d = b[k] - a[k];
        if (d > 1e-9f && b[k] > S) {
            float t = (S - a[k]) / d;
            if (t < best) {
                best = t;
                *axis = k;
                *sign = 1;
            }
        } else if (d < -1e-9f && b[k] < 0.0f) {
            float t = (0.0f - a[k]) / d;
            if (t < best) {
                best = t;
                *axis = k;
                *sign = -1;
            }
        }
    }
    if (best > 1.0f) return -1.0f;
    return best < 0.0f ? 0.0f : best;
}

/* --- the course geometry ------------------------------------------------ */

static inline void nrv_build_course(NavRouteView *v, const NrvInput *in) {
    const float S = (float)QUADRANT_SIZE;
    float base[3] = { (float)((in->q[0] - 1) * QUADRANT_SIZE), (float)((in->q[1] - 1) * QUADRANT_SIZE),
                      (float)((in->q[2] - 1) * QUADRANT_SIZE) };
    /* raw course in sector coordinates: the ship, then the waypoints */
    static float raw[NRV_MAX_WP + 1][3];
    int nr = 0;
    float ship[3] = { in->ship[0], in->ship[1], in->ship[2] };
    for (int k = 0; k < 3; k++) {
        if (ship[k] < 0.0f) ship[k] = 0.0f;
        if (ship[k] > S) ship[k] = S;
    }
    nrv_push_pt(raw, &nr, NRV_MAX_WP + 1, ship);
    for (int k = 0; k < in->count && k < NRV_MAX_WP; k++) {
        float w[3] = { in->wp[k][0] - base[0], in->wp[k][1] - base[1], in->wp[k][2] - base[2] };
        nrv_push_pt(raw, &nr, NRV_MAX_WP + 1, w);
    }

    /* rounded corners: a quadratic fillet at every waypoint, straight legs cut
     * in pieces of at most 2.5 units (smooth colour, clipping, pulses) */
    static float dense[NRV_MAX_PTS][3];
    static int is_wp[NRV_MAX_PTS];
    int nd = 0;
    nrv_push_pt(dense, &nd, NRV_MAX_PTS, raw[0]);
    is_wp[0] = 0;
    float from[3] = { raw[0][0], raw[0][1], raw[0][2] };
    for (int k = 1; k < nr; k++) {
        float to[3], A[3] = { 0, 0, 0 }, B[3] = { 0, 0, 0 };
        int corner = (k < nr - 1);
        float din[3] = { raw[k][0] - raw[k - 1][0], raw[k][1] - raw[k - 1][1], raw[k][2] - raw[k - 1][2] };
        float lin = nrv_len3(din);
        nrv_norm(din);
        if (corner) {
            float dout[3] = { raw[k + 1][0] - raw[k][0], raw[k + 1][1] - raw[k][1], raw[k + 1][2] - raw[k][2] };
            float lout = nrv_len3(dout);
            nrv_norm(dout);
            float d = 1.6f;
            if (d > 0.45f * lin) d = 0.45f * lin;
            if (d > 0.45f * lout) d = 0.45f * lout;
            for (int q = 0; q < 3; q++) {
                A[q] = raw[k][q] - din[q] * d;
                B[q] = raw[k][q] + dout[q] * d;
            }
            to[0] = A[0];
            to[1] = A[1];
            to[2] = A[2];
        } else {
            to[0] = raw[k][0];
            to[1] = raw[k][1];
            to[2] = raw[k][2];
        }
        float L = nrv_dist(from, to);
        int pieces = (int)ceilf(L / 2.5f);
        if (pieces < 1) pieces = 1;
        for (int j = 1; j <= pieces && nd < NRV_MAX_PTS; j++) {
            float p[3];
            nrv_mix(from, to, (float)j / (float)pieces, p);
            int before = nd;
            nrv_push_pt(dense, &nd, NRV_MAX_PTS, p);
            if (nd > before) is_wp[nd - 1] = 0;
        }
        if (corner) {
            for (int j = 1; j <= 6 && nd < NRV_MAX_PTS; j++) {
                float t = (float)j / 6.0f, p[3];
                for (int q = 0; q < 3; q++)
                    p[q] = (1 - t) * (1 - t) * A[q] + 2 * (1 - t) * t * raw[k][q] + t * t * B[q];
                int before = nd;
                nrv_push_pt(dense, &nd, NRV_MAX_PTS, p);
                if (nd > before) is_wp[nd - 1] = (j == 3);   /* the middle of the fillet marks the waypoint */
            }
            from[0] = B[0];
            from[1] = B[1];
            from[2] = B[2];
        } else {
            from[0] = to[0];
            from[1] = to[1];
            from[2] = to[2];
        }
    }

    /* clip to the quadrant: from the ship to where the course leaves it */
    v->n = 0;
    v->exits = 0;
    v->n_wp = 0;
    float vp[3];
    nrv_to_view(dense[0], vp);
    nrv_push_pt(v->p, &v->n, NRV_MAX_PTS, vp);
    for (int k = 1; k < nd; k++) {
        int axis = 0, sign = 0;
        float t = nrv_exit_t(dense[k - 1], dense[k], S, &axis, &sign);
        if (t >= 0.0f) {
            float e[3];
            nrv_mix(dense[k - 1], dense[k], t, e);
            nrv_to_view(e, vp);
            nrv_push_pt(v->p, &v->n, NRV_MAX_PTS, vp);
            v->exits = 1;
            v->exit_axis = axis;
            v->exit_sign = sign;
            v->exit_p[0] = vp[0];
            v->exit_p[1] = vp[1];
            v->exit_p[2] = vp[2];
            for (int q = 0; q < 3; q++) v->exit_q[q] = in->q[q] + ((q == axis) ? sign : 0);
            break;
        }
        nrv_to_view(dense[k], vp);
        nrv_push_pt(v->p, &v->n, NRV_MAX_PTS, vp);
        if (is_wp[k] && v->n_wp < NRV_MAX_WP) {
            v->wp[v->n_wp][0] = vp[0];
            v->wp[v->n_wp][1] = vp[1];
            v->wp[v->n_wp][2] = vp[2];
            v->n_wp++;
        }
    }
    v->s[0] = 0.0f;
    for (int k = 1; k < v->n; k++) v->s[k] = v->s[k - 1] + nrv_dist(v->p[k - 1], v->p[k]);
    v->length = (v->n > 0) ? v->s[v->n - 1] : 0.0f;
    v->ends_here = !v->exits && v->n >= 2;
    if (v->n > 0) {
        v->end_p[0] = v->p[v->n - 1][0];
        v->end_p[1] = v->p[v->n - 1][1];
        v->end_p[2] = v->p[v->n - 1][2];
    }
    v->partial = (in->flags & NRV_FLAG_PARTIAL) != 0;

    /* keep-out bubbles that reach into the quadrant */
    v->n_haz = 0;
    for (int k = 0; k < in->n_haz && k < NRV_MAX_HAZ; k++) {
        float c[3] = { in->haz[k][0] - base[0], in->haz[k][1] - base[1], in->haz[k][2] - base[2] };
        float r = in->haz[k][3];
        int inside = 1;
        for (int q = 0; q < 3; q++) inside &= (c[q] > -r && c[q] < S + r);
        if (!inside) continue;
        nrv_to_view(c, vp);
        v->haz[v->n_haz][0] = vp[0];
        v->haz[v->n_haz][1] = vp[1];
        v->haz[v->n_haz][2] = vp[2];
        v->haz[v->n_haz][3] = r;
        v->haz_kind[v->n_haz] = in->haz_kind[k];
        v->n_haz++;
    }
}

/* --- the primitives ----------------------------------------------------- */

static inline void nrv_chevron(NavRouteView *v, const float p[3], const float t[3], float size,
                               const float rgb[3], float alpha) {
    float s[3], u[3];
    nrv_basis(t, s, u);
    float tip[3] = { p[0] + t[0] * size * 0.6f, p[1] + t[1] * size * 0.6f, p[2] + t[2] * size * 0.6f };
    const float *dirs[2] = { s, u };
    for (int d = 0; d < 2; d++)
        for (int sg = -1; sg <= 1; sg += 2) {
            float w[3];
            for (int q = 0; q < 3; q++) w[q] = p[q] - t[q] * size * 0.4f + dirs[d][q] * size * 0.6f * (float)sg;
            nrv_add(v, NRV_LINE, w, tip, 0.0f, rgb, alpha);
        }
}

static inline void nrv_square(NavRouteView *v, const float c[3], const float e1[3], const float e2[3], float half,
                              float ang, const float rgb[3], float alpha, int as_tube) {
    float pts[4][3];
    for (int k = 0; k < 4; k++) {
        float a = ang + (float)k * 1.5707963f + 0.7853982f;
        float ca = cosf(a) * half * 1.4142136f, sa = sinf(a) * half * 1.4142136f;
        for (int q = 0; q < 3; q++) pts[k][q] = c[q] + e1[q] * ca + e2[q] * sa;
    }
    for (int k = 0; k < 4; k++)
        nrv_add(v, as_tube ? NRV_TUBE : NRV_LINE, pts[k], pts[(k + 1) % 4], as_tube ? 0.07f : 0.0f, rgb, alpha);
}

static inline void nrv_emit(NavRouteView *v, float T) {
    const float floor_y = -(float)(QUADRANT_SIZE * 0.5);
    static const float white[3] = { 1.0f, 1.0f, 1.0f };
    static const float up[3] = { 0.0f, 1.0f, 0.0f };
    float L = v->length;
    static const float shadow[3] = { 0.20f, 0.70f, 0.90f };
    if (v->n < 2) return;
    /* most important first: when a huge course runs out of primitives, the
     * decoration goes, not the course */

    /* 1. the course: soft glow + bright core */
    for (int k = 1; k < v->n; k++) {
        float rgb[3], core[3];
        nrv_course_rgb(v, (L > 1e-6f) ? v->s[k] / L : 0.0f, rgb);
        nrv_mix(rgb, white, 0.45f, core);
        nrv_add(v, NRV_TUBE, v->p[k - 1], v->p[k], 0.26f, rgb, 0.16f);
        nrv_add(v, NRV_TUBE, v->p[k - 1], v->p[k], 0.055f, core, 0.90f);
    }

    /* 2. the end of the course */
    if (v->ends_here) {
        const float *e = v->end_p;
        if (v->partial) {
            static const float red[3] = { 1.0f, 0.18f, 0.12f };
            float blink = 0.55f + 0.45f * (sinf(T * 6.0f) > 0.0f ? 1.0f : 0.0f);
            float d = 0.9f;
            float a1[3] = { e[0] - d, e[1] - d, e[2] }, b1[3] = { e[0] + d, e[1] + d, e[2] };
            float a2[3] = { e[0] - d, e[1] + d, e[2] }, b2[3] = { e[0] + d, e[1] - d, e[2] };
            float a3[3] = { e[0], e[1] - d, e[2] - d }, b3[3] = { e[0], e[1] + d, e[2] + d };
            float a4[3] = { e[0], e[1] + d, e[2] - d }, b4[3] = { e[0], e[1] - d, e[2] + d };
            nrv_add(v, NRV_TUBE, a1, b1, 0.08f, red, blink);
            nrv_add(v, NRV_TUBE, a2, b2, 0.08f, red, blink);
            nrv_add(v, NRV_TUBE, a3, b3, 0.08f, red, blink);
            nrv_add(v, NRV_TUBE, a4, b4, 0.08f, red, blink);
            nrv_add(v, NRV_RING, e, up, 1.3f + 0.2f * sinf(T * 4.0f), red, 0.8f);
            nrv_add(v, NRV_DOT, e, NULL, 0.9f, red, 0.18f * blink);
        } else {
            static const float gold[3] = { 1.0f, 0.85f, 0.35f };
            float lo[3] = { e[0], e[1] - 2.0f, e[2] }, hi[3] = { e[0], e[1] + 7.0f, e[2] };
            nrv_add(v, NRV_TUBE, lo, hi, 0.30f, gold, 0.16f);
            nrv_add(v, NRV_TUBE, lo, hi, 0.06f, white, 0.75f);
            for (int k = 0; k < 3; k++) {                        /* expanding rings */
                float ph = fmodf(T * 0.6f + (float)k / 3.0f, 1.0f);
                nrv_add(v, NRV_RING, e, up, 0.4f + 3.2f * ph, gold, 0.85f * (1.0f - ph));
            }
            nrv_add(v, NRV_RING, e, up, 1.25f, white, 0.9f);
            float s1[3] = { 1, 0, 0 }, s2[3] = { 0, 0, 1 };      /* crosshair brackets, turning */
            nrv_square(v, e, s1, s2, 1.7f, T * 0.8f, gold, 0.75f, 1);
            nrv_add(v, NRV_DOT, e, NULL, 0.22f, white, 1.0f);
            nrv_add(v, NRV_DOT, e, NULL, 0.70f, gold, 0.25f + 0.15f * sinf(T * 3.0f));
            float foot[3] = { e[0], floor_y, e[2] };
            nrv_add(v, NRV_LINE, e, foot, 0.0f, gold, 0.25f);
            nrv_add(v, NRV_RING, foot, up, 0.9f, gold, 0.35f);
        }
    }

    /* 3. the exit gate where the course leaves the quadrant */
    if (v->exits) {
        static const float teal[3] = { 0.25f, 1.0f, 0.85f };
        float n[3] = { 0, 0, 0 };                                 /* outward normal, viewer coordinates */
        if (v->exit_axis == 0) n[0] = (float)v->exit_sign;
        else if (v->exit_axis == 1) n[2] = -(float)v->exit_sign;  /* sector y -> viewer -z */
        else n[1] = (float)v->exit_sign;                          /* sector z -> viewer +y */
        float e1[3], e2[3];
        nrv_basis(n, e1, e2);
        const float *g = v->exit_p;
        nrv_square(v, g, e1, e2, 2.2f, 0.0f, teal, 0.95f, 1);
        nrv_square(v, g, e1, e2, 1.5f, T * 1.6f, white, 0.65f, 0);
        nrv_add(v, NRV_RING, g, n, 2.9f + 0.15f * sinf(T * 3.0f), teal, 0.55f);
        for (int k = 0; k < 4; k++) {                             /* tunnel of frames moving out */
            float ph = fmodf(T * 0.9f + (float)k / 4.0f, 1.0f);
            float c[3] = { g[0] + n[0] * ph * 4.0f, g[1] + n[1] * ph * 4.0f, g[2] + n[2] * ph * 4.0f };
            nrv_square(v, c, e1, e2, 2.2f * (1.0f - 0.55f * ph), 0.0f, teal, 0.7f * (1.0f - ph), 0);
        }
        for (int k = 0; k < 3; k++) {                             /* arrowheads pointing out */
            float ph = fmodf(T * 1.2f + (float)k / 3.0f, 1.0f);
            float c[3] = { g[0] - n[0] * (2.5f - ph * 3.0f), g[1] - n[1] * (2.5f - ph * 3.0f), g[2] - n[2] * (2.5f - ph * 3.0f) };
            nrv_chevron(v, c, n, 0.9f, teal, 0.9f * sinf(ph * 3.1415927f));
        }
        nrv_add(v, NRV_DOT, g, NULL, 0.25f, white, 0.95f);
        float foot[3] = { g[0], floor_y, g[2] };
        nrv_add(v, NRV_LINE, g, foot, 0.0f, teal, 0.22f);
    }
    /* 4. keep-out bubbles of the obstacles the course bends around */
    for (int k = 0; k < v->n_haz; k++) {
        const float *c = v->haz[k];
        float R = c[3];
        float rgb[3] = { 1.0f, 0.58f, 0.12f };                  /* amber */
        if (v->haz_kind[k] == NRV_KIND_BLACK_HOLE) {
            rgb[0] = 0.75f;
            rgb[1] = 0.30f;
            rgb[2] = 1.0f;
        } else if (v->haz_kind[k] == NRV_KIND_SHIP) {
            rgb[0] = 0.95f;
            rgb[1] = 0.85f;
            rgb[2] = 0.25f;
        }
        float breathe = 0.5f + 0.5f * sinf(T * 2.2f + (float)k * 1.7f);
        static const float ax[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        for (int a = 0; a < 3; a++) nrv_add(v, NRV_RING, c, ax[a], R, rgb, 0.22f + 0.18f * breathe);
        /* latitude ring sweeping up and down the bubble */
        float phi = sinf(T * 1.3f + (float)k) * 1.25f;
        float lc[3] = { c[0], c[1] + R * sinf(phi), c[2] };
        nrv_add(v, NRV_RING, lc, up, R * cosf(phi), rgb, 0.65f);
        /* gyroscope ring, slowly turning */
        float g[3] = { cosf(T * 0.7f + (float)k), 0.55f, sinf(T * 0.7f + (float)k) };
        nrv_norm(g);
        nrv_add(v, NRV_RING, c, g, R * 1.04f, rgb, 0.40f);
        nrv_add(v, NRV_DOT, c, NULL, R, rgb, 0.05f + 0.04f * breathe);
    }

    /* 5. waypoints: a ring across the course and a spinning diamond */
    for (int k = 0; k < v->n_wp; k++) {
        float s_wp = 0.0f, best = 1e30f;
        for (int j = 0; j < v->n; j++) {
            float d = nrv_dist(v->p[j], v->wp[k]);
            if (d < best) {
                best = d;
                s_wp = v->s[j];
            }
        }
        float p[3], t[3], s[3], u[3], rgb[3];
        nrv_at(v, s_wp, p, t);
        nrv_basis(t, s, u);
        nrv_course_rgb(v, (L > 1e-6f) ? s_wp / L : 0.0f, rgb);
        float pr = 0.75f + 0.12f * sinf(T * 3.0f + (float)k);
        nrv_add(v, NRV_RING, v->wp[k], t, pr, rgb, 0.85f);
        nrv_add(v, NRV_DOT, v->wp[k], NULL, 0.12f, white, 0.9f);
        if (v->n_wp > 24) continue;                     /* a dense course: plain markers */
        nrv_add(v, NRV_RING, v->wp[k], t, pr * 1.45f, rgb, 0.22f);
        nrv_square(v, v->wp[k], s, u, 0.45f, T * 1.8f + (float)k, white, 0.85f, 0);
        /* drop line to the floor + its foot */
        float foot[3] = { v->wp[k][0], floor_y, v->wp[k][2] };
        nrv_add(v, NRV_LINE, v->wp[k], foot, 0.0f, shadow, 0.20f);
        nrv_add(v, NRV_RING, foot, up, 0.5f, shadow, 0.30f);
    }

    /* 6. energy pulses with a tail, flowing out of the ship */
    const float spacing = 6.0f, speed = 9.0f;
    for (float s = fmodf(T * speed, spacing); s < L; s += spacing) {
        float fade = 1.0f;
        if (s < 1.5f) fade = s / 1.5f;
        if (L - s < 2.0f) fade = (L - s) / 2.0f;
        if (fade <= 0.0f) continue;
        float p[3], t[3], tail[3], tt[3], rgb[3], hot[3];
        nrv_at(v, s, p, t);
        float s2 = s - 1.6f;
        if (s2 < 0.0f) s2 = 0.0f;
        nrv_at(v, s2, tail, tt);
        nrv_course_rgb(v, s / L, rgb);
        nrv_mix(rgb, white, 0.6f, hot);
        nrv_add(v, NRV_DOT, p, NULL, 0.16f, hot, 0.95f * fade);
        nrv_add(v, NRV_DOT, p, NULL, 0.38f, rgb, 0.22f * fade);
        nrv_add(v, NRV_TUBE, tail, p, 0.10f, rgb, 0.45f * fade);
    }

    /* 7. arrowheads lit by a wave running from the ship to the end */
    for (float s = 2.0f; s < L - 1.0f; s += 3.0f) {
        float p[3], t[3], rgb[3];
        nrv_at(v, s, p, t);
        float w = cosf((s - T * 7.0f) * (6.2831853f / 12.0f));
        w = (w > 0.0f) ? w * w * w * w : 0.0f;
        nrv_course_rgb(v, s / L, rgb);
        nrv_chevron(v, p, t, 0.42f, rgb, 0.18f + 0.75f * w);
    }

    /* 8. shadow on the floor of the cube (every third point is enough) */
    for (int k = 3; k < v->n + 2; k += 3) {
        int k1 = (k < v->n) ? k : v->n - 1;
        int k0 = k - 3;
        if (k0 >= k1) break;
        float a[3] = { v->p[k0][0], floor_y, v->p[k0][2] }, b[3] = { v->p[k1][0], floor_y, v->p[k1][2] };
        nrv_add(v, NRV_LINE, a, b, 0.0f, shadow, 0.16f);
    }
}

/* Builds the view of the course for the quadrant on screen. Returns the
 * number of primitives (0: nothing to draw). */
static inline int nrv_build(NavRouteView *v, const NrvInput *in) {
    v->np = 0;
    v->n = 0;
    v->n_wp = v->n_haz = 0;
    v->exits = v->ends_here = v->partial = 0;
    v->length = 0.0f;
    if (!in->active || in->count < 1) return 0;
    nrv_build_course(v, in);
    if (v->n < 2) return 0;
    nrv_emit(v, in->time);
    return v->np;
}

/* --- shared-memory adapter (views that include shared_state.h first) ----- */
#ifdef SHARED_STATE_H
static inline void nrv_input_from_shm(NrvInput *in, const SharedRoute *r, const int q[3],
                                      const float ship_sector[3], float seconds) {
    in->active = r->active;
    in->flags = r->flags;
    in->count = (r->count < NRV_MAX_WP) ? r->count : NRV_MAX_WP;
    for (int k = 0; k < in->count; k++)
        for (int a = 0; a < 3; a++) in->wp[k][a] = (float)r->wp[k][a];
    in->n_haz = (r->hazard_count < NRV_MAX_HAZ) ? r->hazard_count : NRV_MAX_HAZ;
    for (int k = 0; k < in->n_haz; k++) {
        in->haz[k][0] = (float)r->hazards[k].x;
        in->haz[k][1] = (float)r->hazards[k].y;
        in->haz[k][2] = (float)r->hazards[k].z;
        in->haz[k][3] = (float)r->hazards[k].r;
        in->haz_kind[k] = r->hazards[k].kind;
    }
    in->q[0] = q[0];
    in->q[1] = q[1];
    in->q[2] = q[2];
    in->ship[0] = ship_sector[0];
    in->ship[1] = ship_sector[1];
    in->ship[2] = ship_sector[2];
    in->time = seconds;
}
#endif

#endif /* NAV_ROUTE_VIEW_H */
