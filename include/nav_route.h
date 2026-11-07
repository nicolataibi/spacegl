/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * nav_route.h - collision-free course for the `nav` command.
 *
 * Pure geometry over the sphere obstacles of include/steer_avoid.h: given a
 * start, a goal and the obstacles of the corridor between them, it returns a
 * polyline (start, waypoints, goal) whose every leg stays outside the
 * keep-out sphere (r_avoid + extra) of every obstacle.
 *
 *   1. The goal is clamped into the allowed box (the galaxy) and, when it
 *      falls inside a keep-out sphere (a `nav` aimed at a planet), moved to
 *      the edge of that sphere on the side it was asked from.
 *   2. A start inside a keep-out sphere (a ship parked next to a planet)
 *      first leaves it radially, unless the course already moves away.
 *   3. Each leg A->B that enters a keep-out sphere is bent around it: in
 *      NAV_ROUTE_SIDES planes that contain A->B (the side the straight line
 *      already passes on first), the sphere cuts a circle; the detour goes
 *      along the tangent from A, along a polygon circumscribed to that circle
 *      and along the tangent to B, so it grazes the circle at
 *      r * NAV_ROUTE_SAFETY + NAV_ROUTE_SAFETY_ADD and never enters it. The
 *      cheapest plane wins (length + a penalty for every other obstacle its
 *      new legs run into); a detour whose waypoint would fall inside another
 *      sphere merges that sphere into a cluster and bends around the sphere
 *      that bounds them all. The new legs are planned the same way.
 *   4. String pulling: every waypoint that can be skipped without entering a
 *      keep-out sphere is dropped, so the course hugs the obstacles instead of
 *      zig-zagging.
 *   5. When no safe detour exists (the goal walled in, the start inside a
 *      cluster, too many waypoints) the course stops short of the obstacle
 *      and is flagged NAV_RF_PARTIAL.
 *
 * Header-only, C99, depends only on <math.h>/<string.h> and steer_avoid.h:
 * no server globals, so tests/nav_route_test.c links nothing else.
 */
#ifndef NAV_ROUTE_H
#define NAV_ROUTE_H

#include <math.h>
#include <string.h>
#include "steer_avoid.h"

#define NAV_ROUTE_MAX_WP      64      /* points of a course, start and goal included */
#define NAV_ROUTE_MAX_HAZARDS 16      /* obstacles reported as "bent around"          */
#define NAV_ROUTE_SAFETY      1.08    /* detours graze r * 1.08 + 0.1                */
#define NAV_ROUTE_SAFETY_ADD  0.10
#define NAV_ROUTE_SIDES       12      /* candidate planes per detour                  */
#define NAV_ROUTE_ARC_STEP    (3.14159265358979323846 / 6.0)  /* 30 deg per polygon edge */
#define NAV_ROUTE_MAX_DEPTH   12      /* nested detours                               */
#define NAV_ROUTE_CLUSTER_MAX 24      /* spheres merged into one detour               */
#define NAV_ROUTE_ARC_MAX     16      /* waypoints of one detour                      */

#define NAV_RF_DETOUR       (1u << 0) /* the course bends around something           */
#define NAV_RF_GOAL_MOVED   (1u << 1) /* goal was inside a keep-out sphere: moved     */
#define NAV_RF_START_INSIDE (1u << 2) /* start was inside one: leaves it first        */
#define NAV_RF_PARTIAL      (1u << 3) /* no safe way through: stops short             */
#define NAV_RF_BOUNDED      (1u << 4) /* goal clamped into the allowed box            */

typedef struct {
    double lo[3], hi[3];  /* waypoints must stay inside this box          */
    double extra;         /* added to every keep-out radius (r_avoid)     */
    unsigned seed;          /* side of an exactly head-on approach          */
} NavRouteCfg;

typedef struct {
    int n;                               /* points, p[0] = start, p[n-1] = end */
    double p[NAV_ROUTE_MAX_WP][3];
    unsigned flags;                           /* NAV_RF_*                           */
    int n_hazards;
    int hazard[NAV_ROUTE_MAX_HAZARDS];   /* indices into obs[]: bent around    */
    int goal_obstacle;                   /* GOAL_MOVED: the sphere that held it, else -1 */
    int block_obstacle;                  /* PARTIAL: the one it could not pass, else -1  */
    double length;                          /* along the course                   */
    double direct;                          /* straight start -> requested goal   */
} NavRoute;

/* --- small vector helpers (nr_ prefix: no clash with the rest of the tree) */

static inline void nr_sub(const double a[3], const double b[3], double o[3]) {
    o[0] = a[0] - b[0];
    o[1] = a[1] - b[1];
    o[2] = a[2] - b[2];
}
static inline void nr_cross(const double a[3], const double b[3], double o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
static inline double nr_dist(const double a[3], const double b[3]) {
    double d[3];
    nr_sub(a, b, d);
    return sqrt(sa_dot(d, d));
}
static inline void nr_copy(double o[3], const double a[3]) {
    o[0] = a[0];
    o[1] = a[1];
    o[2] = a[2];
}

/* Keep-out radius of an obstacle, 0 = not an obstacle for the course. */
static inline double nr_radius(const NavRouteCfg *cfg, const ObsSphere *o) {
    return (o->r_avoid > 0.0) ? o->r_avoid + cfg->extra : 0.0;
}

/* Distance from point p to the segment a-b. */
static inline double nr_point_seg_dist(const double p[3], const double a[3], const double b[3]) {
    double ab[3], ap[3];
    nr_sub(b, a, ab);
    nr_sub(p, a, ap);
    double l2 = sa_dot(ab, ab);
    double t = (l2 > 1e-18) ? sa_dot(ap, ab) / l2 : 0.0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    double q[3] = { a[0] + ab[0] * t, a[1] + ab[1] * t, a[2] + ab[2] * t };
    return nr_dist(p, q);
}

static inline int nr_in_box(const NavRouteCfg *cfg, const double p[3]) {
    for (int a = 0; a < 3; a++)
        if (p[a] < cfg->lo[a] || p[a] > cfg->hi[a]) return 0;
    return 1;
}

/* First keep-out sphere entered by the segment a -> b (sa_segment_enter: a
 * start already inside only counts while moving toward the centre).
 * Returns its index or -1; *t_out = parameter of the contact. */
static inline int nr_first_block(const NavRouteCfg *cfg, const ObsSphere *obs, int n,
                                 const double a[3], const double b[3], double *t_out) {
    int best = -1;
    double best_t = 2.0;
    for (int i = 0; i < n; i++) {
        double R = nr_radius(cfg, &obs[i]);
        if (R <= 0.0) continue;
        double c[3] = { obs[i].x, obs[i].y, obs[i].z };
        double t;
        if (sa_segment_enter(a, b, c, R, &t) && t < best_t) {
            best_t = t;
            best = i;
        }
    }
    if (best >= 0 && t_out) *t_out = best_t;
    return best;
}

/* First keep-out sphere that contains p (scaled radius), or -1. */
static inline int nr_inside(const NavRouteCfg *cfg, const ObsSphere *obs, int n,
                            const double p[3], double scale) {
    for (int i = 0; i < n; i++) {
        double R = nr_radius(cfg, &obs[i]) * scale;
        if (R <= 0.0) continue;
        double c[3] = { obs[i].x, obs[i].y, obs[i].z };
        double d[3];
        nr_sub(p, c, d);
        if (sa_dot(d, d) < R * R) return i;
    }
    return -1;
}

/* Sphere bounding the keep-out spheres ids[0..k-1] (Ritter, incremental). */
static inline void nr_cluster_sphere(const NavRouteCfg *cfg, const ObsSphere *obs,
                                     const int *ids, int k, double C[3], double *R) {
    const ObsSphere *o = &obs[ids[0]];
    C[0] = o->x;
    C[1] = o->y;
    C[2] = o->z;
    *R = nr_radius(cfg, o);
    for (int j = 1; j < k; j++) {
        o = &obs[ids[j]];
        double c[3] = { o->x, o->y, o->z };
        double r = nr_radius(cfg, o);
        double d[3];
        nr_sub(c, C, d);
        double dl = sqrt(sa_dot(d, d));
        if (dl + r <= *R) continue;               /* already inside */
        if (*R + dl <= r) {                       /* swallows the current one */
            nr_copy(C, c);
            *R = r;
            continue;
        }
        double nr = 0.5 * (*R + dl + r);
        double shift = nr - *R;
        if (dl > 1e-12) {
            C[0] += d[0] / dl * shift;
            C[1] += d[1] / dl * shift;
            C[2] += d[2] / dl * shift;
        }
        *R = nr;
    }
}

/*
 * Detour of a -> b around the sphere (C, Rk) inside the plane that contains
 * a -> b and the side vector s (unit, perpendicular to the unit u = dir a->b),
 * going over the +s side. Writes the waypoints (not a, not b) to V and
 * returns how many, or -1 when a or b is too close to the sphere for a
 * tangent detour in that plane.
 */
static inline int nr_detour_plane(const double a[3], const double b[3], const double C[3], double Rk,
                                  const double u[3], const double s[3],
                                  double V[][3], int max) {
    double w[3];
    nr_cross(u, s, w);
    double ac[3];
    nr_sub(a, C, ac);
    double h = sa_dot(ac, w);
    if (fabs(h) >= Rk) return -1;
    double Rs = Rk * NAV_ROUTE_SAFETY + NAV_ROUTE_SAFETY_ADD;
    double rho_k = sqrt(Rk * Rk - h * h);
    double rho = sqrt(Rs * Rs - h * h);
    double O[3] = { C[0] + w[0] * h, C[1] + w[1] * h, C[2] + w[2] * h };
    double ao[3], bo[3];
    nr_sub(a, O, ao);
    nr_sub(b, O, bo);
    double ax = sa_dot(ao, u), ay = sa_dot(ao, s);
    double bx = sa_dot(bo, u), by = sa_dot(bo, s);
    double dA = sqrt(ax * ax + ay * ay), dB = sqrt(bx * bx + by * by);
    /* a or b in the safety band (outside the sphere, inside the graze
     * radius): graze a little closer, never inside the sphere itself */
    if (rho > 0.999 * dA) rho = 0.999 * dA;
    if (rho > 0.999 * dB) rho = 0.999 * dB;
    if (rho <= rho_k * 1.0005) return -1;
    const double two_pi = 2.0 * 3.14159265358979323846;
    double tA = atan2(ay, ax) - acos(rho / dA);   /* tangent point seen from a, +s side */
    double tB = atan2(by, bx) + acos(rho / dB);   /* tangent point seen from b, +s side */
    double sweep = tA - tB;                       /* clockwise from tA to tB */
    while (sweep < 0.0) sweep += two_pi;
    while (sweep >= two_pi) sweep -= two_pi;
    if (sweep > 0.95 * two_pi) return -1;
    int m = (int)ceil(sweep / NAV_ROUTE_ARC_STEP);
    if (m < 1) m = 1;
    if (m > max) return -1;
    double delta = sweep / m;
    double rv = rho / cos(0.5 * delta);           /* circumscribed polygon */
    for (int k = 0; k < m; k++) {
        double ang = tA - delta * (k + 0.5);
        double x = rv * cos(ang), y = rv * sin(ang);
        for (int q = 0; q < 3; q++) V[k][q] = O[q] + u[q] * x + s[q] * y;
    }
    return m;
}

typedef struct {
    const ObsSphere *obs;
    int n_obs;
    const NavRouteCfg *cfg;
    NavRoute *r;
    int fail;
    int fail_obs;
    double fail_a[3], fail_b[3], fail_t;
    int n_touched;
    int touched[NAV_ROUTE_MAX_HAZARDS * 2];
} NrCtx;

static inline void nr_touch(NrCtx *x, int idx) {
    for (int k = 0; k < x->n_touched; k++)
        if (x->touched[k] == idx) return;
    if (x->n_touched < NAV_ROUTE_MAX_HAZARDS * 2) x->touched[x->n_touched++] = idx;
}

static inline void nr_push(NrCtx *x, const double p[3]) {
    if (x->fail) return;
    if (x->r->n >= NAV_ROUTE_MAX_WP) {
        x->fail = 1;
        return;
    }
    nr_copy(x->r->p[x->r->n], p);
    x->r->n++;
}

/* Cost of the course a -> V[0..m-1] -> b; HUGE_VAL when a waypoint is out of
 * the box or inside a keep-out sphere (*conflict = that sphere). */
static inline double nr_eval(const NrCtx *x, const double a[3], const double b[3],
                             double V[][3], int m, int *conflict) {
    *conflict = -1;
    for (int k = 0; k < m; k++) {
        if (!nr_in_box(x->cfg, V[k])) return HUGE_VAL;
        int j = nr_inside(x->cfg, x->obs, x->n_obs, V[k], 1.0);
        if (j >= 0) {
            *conflict = j;
            return HUGE_VAL;
        }
    }
    double len = 0.0, pen = 0.0;
    const double *prev = a;
    for (int k = 0; k <= m; k++) {
        const double *nx = (k < m) ? V[k] : b;
        len += nr_dist(prev, nx);
        double t;
        int j = nr_first_block(x->cfg, x->obs, x->n_obs, prev, nx, &t);
        if (j >= 0) pen += 2.5 * nr_radius(x->cfg, &x->obs[j]) + 1.0;
        prev = nx;
    }
    return len + pen;
}

static inline void nr_seg(NrCtx *x, const double a[3], const double b[3], int depth);

/* a -> b enters keep-out sphere `first`: bend around it (or the cluster it
 * belongs to) and plan the new legs. */
static inline void nr_detour(NrCtx *x, const double a[3], const double b[3], int first, double t_first, int depth) {
    int ids[NAV_ROUTE_CLUSTER_MAX];
    int k = 0;
    ids[k++] = first;

    double ab[3], u[3];
    nr_sub(b, a, ab);
    double L = sa_norm(ab, u);

    double bestV[NAV_ROUTE_ARC_MAX][3];
    int best_m = -1;

    for (int grow = 0; grow < 8 && best_m < 0; grow++) {
        double C[3], Rk;
        nr_cluster_sphere(x->cfg, x->obs, ids, k, C, &Rk);

        /* base side: where the straight line passes relative to the centre */
        double ac[3];
        nr_sub(C, a, ac);
        double along = sa_dot(ac, u);
        double f[3] = { a[0] + u[0] * along - C[0], a[1] + u[1] * along - C[1], a[2] + u[2] * along - C[2] };
        double s0[3];
        if (sa_norm(f, s0) < 1e-6 * (Rk + 1.0)) sa_perp(u, x->cfg->seed + (unsigned)depth, s0);
        double s1[3];
        nr_cross(u, s0, s1);

        double best_cost = HUGE_VAL;
        int new_conf[NAV_ROUTE_SIDES];
        int n_conf = 0;
        for (int side = 0; side < NAV_ROUTE_SIDES; side++) {
            double phi = side * (2.0 * 3.14159265358979323846 / NAV_ROUTE_SIDES);
            double c = cos(phi), sn = sin(phi);
            double s[3] = { s0[0] * c + s1[0] * sn, s0[1] * c + s1[1] * sn, s0[2] * c + s1[2] * sn };
            double V[NAV_ROUTE_ARC_MAX][3];
            int m = nr_detour_plane(a, b, C, Rk, u, s, V, NAV_ROUTE_ARC_MAX);
            if (m <= 0) continue;
            int conf;
            double cost = nr_eval(x, a, b, V, m, &conf);
            if (conf >= 0) {
                int dup = 0;
                for (int q = 0; q < k; q++) dup |= (ids[q] == conf);
                for (int q = 0; q < n_conf; q++) dup |= (new_conf[q] == conf);
                if (!dup) new_conf[n_conf++] = conf;
            }
            if (cost < best_cost) {
                best_cost = cost;
                best_m = m;
                memcpy(bestV, V, sizeof(double) * 3 * (size_t)m);
            }
        }
        if (best_m >= 0) {
            for (int q = 0; q < k; q++) nr_touch(x, ids[q]);
            break;
        }
        if (n_conf == 0) break;               /* nothing to merge: no way round */
        for (int q = 0; q < n_conf && k < NAV_ROUTE_CLUSTER_MAX; q++) ids[k++] = new_conf[q];
    }
    (void)L;

    if (best_m < 0) {
        x->fail = 1;
        x->fail_obs = first;
        nr_copy(x->fail_a, a);
        nr_copy(x->fail_b, b);
        x->fail_t = t_first;
        return;
    }
    x->r->flags |= NAV_RF_DETOUR;
    const double *prev = a;
    double P[NAV_ROUTE_ARC_MAX][3];
    memcpy(P, bestV, sizeof(double) * 3 * (size_t)best_m);
    for (int q = 0; q < best_m && !x->fail; q++) {
        nr_seg(x, prev, P[q], depth + 1);
        prev = P[q];
    }
    if (!x->fail) nr_seg(x, prev, b, depth + 1);
}

/* Plan the leg a -> b (a already in the course): pushes b, and every
 * waypoint before it, or sets x->fail. */
static inline void nr_seg(NrCtx *x, const double a[3], const double b[3], int depth) {
    if (x->fail) return;
    double t;
    int i = nr_first_block(x->cfg, x->obs, x->n_obs, a, b, &t);
    if (i < 0) {
        nr_push(x, b);
        return;
    }
    if (depth >= NAV_ROUTE_MAX_DEPTH) {
        x->fail = 1;
        x->fail_obs = i;
        nr_copy(x->fail_a, a);
        nr_copy(x->fail_b, b);
        x->fail_t = t;
        return;
    }
    nr_detour(x, a, b, i, t, depth);
}

/* Drop every waypoint that can be skipped without entering a keep-out sphere. */
static inline void nr_simplify(const NavRouteCfg *cfg, const ObsSphere *obs, int n_obs, NavRoute *r) {
    if (r->n <= 2) return;
    double out[NAV_ROUTE_MAX_WP][3];
    int no = 0;
    int i = 0;
    nr_copy(out[no++], r->p[0]);
    while (i < r->n - 1) {
        int j = r->n - 1;
        for (; j > i + 1; j--) {
            double t;
            if (nr_first_block(cfg, obs, n_obs, r->p[i], r->p[j], &t) < 0) break;
        }
        nr_copy(out[no++], r->p[j]);
        i = j;
    }
    memcpy(r->p, out, sizeof(double) * 3 * (size_t)no);
    r->n = no;
}

/*
 * Plan the course start -> goal. Returns 0 with a course of at least two
 * points (possibly flagged PARTIAL), or -1 when not even a first leg is
 * possible (r->n = 1: the ship is walled in).
 */
static inline int nav_route_plan(const double start[3], const double goal_in[3],
                                 const ObsSphere *obs, int n_obs,
                                 const NavRouteCfg *cfg, NavRoute *r) {
    memset(r, 0, sizeof(*r));
    r->goal_obstacle = -1;
    r->block_obstacle = -1;

    double goal[3];
    nr_copy(goal, goal_in);
    for (int a = 0; a < 3; a++) {
        if (goal[a] < cfg->lo[a]) {
            goal[a] = cfg->lo[a];
            r->flags |= NAV_RF_BOUNDED;
        }
        if (goal[a] > cfg->hi[a]) {
            goal[a] = cfg->hi[a];
            r->flags |= NAV_RF_BOUNDED;
        }
    }
    r->direct = nr_dist(start, goal);

    /* 1. goal inside a keep-out sphere: to its edge, on the side asked from */
    for (int iter = 0; iter < 6; iter++) {
        int j = nr_inside(cfg, obs, n_obs, goal, 1.0);
        if (j < 0) break;
        const ObsSphere *o = &obs[j];
        double c[3] = { o->x, o->y, o->z };
        double d[3], dn[3];
        nr_sub(goal, c, d);
        if (sa_norm(d, dn) < 1e-6) {
            nr_sub(start, c, d);
            if (sa_norm(d, dn) < 1e-6) {
                double z[3] = { 0, 0, 1 };
                sa_perp(z, cfg->seed, dn);
            }
        }
        double Rs = nr_radius(cfg, o) * NAV_ROUTE_SAFETY + NAV_ROUTE_SAFETY_ADD;
        for (int a = 0; a < 3; a++) goal[a] = c[a] + dn[a] * Rs;
        if (r->goal_obstacle < 0) r->goal_obstacle = j;
        r->flags |= NAV_RF_GOAL_MOVED;
    }

    NrCtx x;
    memset(&x, 0, sizeof(x));
    x.obs = obs;
    x.n_obs = n_obs;
    x.cfg = cfg;
    x.r = r;
    x.fail_obs = -1;
    if (r->goal_obstacle >= 0) nr_touch(&x, r->goal_obstacle);

    /* everything the straight line runs into: what the course bends around */
    for (int i = 0; i < n_obs; i++) {
        double R = nr_radius(cfg, &obs[i]);
        if (R <= 0.0) continue;
        double c[3] = { obs[i].x, obs[i].y, obs[i].z };
        double t;
        if (sa_segment_enter(start, goal, c, R, &t)) nr_touch(&x, i);
    }

    nr_push(&x, start);
    double a[3];
    nr_copy(a, start);

    /* 2. start inside a keep-out sphere and heading into it: leave radially */
    for (int iter = 0; iter < 3; iter++) {
        double t;
        int j = nr_first_block(cfg, obs, n_obs, a, goal, &t);
        if (j < 0 || t > 0.0) break;
        const ObsSphere *o = &obs[j];
        double c[3] = { o->x, o->y, o->z };
        double d[3], dn[3];
        nr_sub(a, c, d);
        if (sa_norm(d, dn) < 1e-6) {
            double g[3];
            nr_sub(goal, a, g);
            sa_perp(g, cfg->seed, dn);
        }
        double Rs = nr_radius(cfg, o) * NAV_ROUTE_SAFETY + NAV_ROUTE_SAFETY_ADD;
        double e[3] = { c[0] + dn[0] * Rs, c[1] + dn[1] * Rs, c[2] + dn[2] * Rs };
        if (!nr_in_box(cfg, e)) break;
        nr_push(&x, e);
        nr_copy(a, e);
        nr_touch(&x, j);
        r->flags |= NAV_RF_START_INSIDE;
    }

    /* 3. the course itself */
    int base_n = r->n;
    nr_seg(&x, a, goal, 0);

    if (x.fail) {
        /* keep what was planned up to the leg that could not be passed, then
         * stop one unit short of the keep-out sphere on that leg */
        if (r->n > NAV_ROUTE_MAX_WP - 1) r->n = NAV_ROUTE_MAX_WP - 1;
        if (r->n < base_n) r->n = base_n;   /* never drop the exit legs of step 2 */
        r->flags |= NAV_RF_PARTIAL;
        r->block_obstacle = x.fail_obs;
        if (x.fail_obs >= 0) nr_touch(&x, x.fail_obs);
        double len = nr_dist(x.fail_a, x.fail_b);
        if (len > 1e-9) {
            double tb = x.fail_t - 1.0 / len;
            if (tb > 0.0) {
                double s[3];
                for (int q = 0; q < 3; q++) s[q] = x.fail_a[q] + (x.fail_b[q] - x.fail_a[q]) * tb;
                if (nr_inside(cfg, obs, n_obs, s, 1.0) < 0 && nr_dist(s, r->p[r->n - 1]) > 1e-6) {
                    x.fail = 0;
                    nr_push(&x, s);
                }
            }
        }
    }

    /* 4. string pulling */
    nr_simplify(cfg, obs, n_obs, r);

    r->length = 0.0;
    for (int k = 1; k < r->n; k++) r->length += nr_dist(r->p[k - 1], r->p[k]);

    /* hazards: what the straight line ran into, plus the spheres every
     * chosen detour bent around (the goal / blocking obstacle first) */
    for (int q = 0; q < x.n_touched && r->n_hazards < NAV_ROUTE_MAX_HAZARDS; q++)
        r->hazard[r->n_hazards++] = x.touched[q];
    if (r->n == 2 && !(r->flags & (NAV_RF_START_INSIDE)))
        r->flags &= ~NAV_RF_DETOUR;
    return (r->n >= 2) ? 0 : -1;
}

/*
 * Safety check of the rest of a course: the polyline pos -> p[next] -> ... ->
 * p[n-1], cut after `lookahead` units, against the CONTACT spheres (r_hit +
 * extra, the real collisions). Returns the index of the first obstacle hit,
 * or -1. Used while flying to see a moving object (comet, orbiting moon)
 * cross the course.
 */
static inline int nav_route_first_hit(const NavRoute *r, int next, const double pos[3],
                                      double lookahead, const ObsSphere *obs, int n_obs,
                                      double extra) {
    double a[3];
    nr_copy(a, pos);
    double left = lookahead;
    for (int k = next; k < r->n && left > 0.0; k++) {
        double b[3];
        nr_copy(b, r->p[k]);
        double l = nr_dist(a, b);
        if (l > left && l > 1e-9) {
            for (int q = 0; q < 3; q++) b[q] = a[q] + (b[q] - a[q]) * (left / l);
            l = left;
        }
        double t;
        int i = sa_first_hit(a, b, obs, n_obs, extra, 0, &t);
        if (i >= 0) return i;
        left -= l;
        nr_copy(a, b);
    }
    return -1;
}

/* Heading / mark (degrees, the convention of `nav`: dx = cos m sin h,
 * dy = -cos m cos h, dz = sin m) of a unit direction. */
static inline void nav_route_dir_to_hm(const double d[3], double *h, double *m) {
    double dz = d[2];
    if (dz > 1.0) dz = 1.0;
    if (dz < -1.0) dz = -1.0;
    *m = asin(dz) * 180.0 / 3.14159265358979323846;
    double hh = atan2(d[0], -d[1]) * 180.0 / 3.14159265358979323846;
    if (hh < 0.0) hh += 360.0;
    if (hh >= 360.0) hh -= 360.0;
    *h = hh;
}

#endif /* NAV_ROUTE_H */
