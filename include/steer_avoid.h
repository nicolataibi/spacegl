/*
 * steer_avoid.h - sphere obstacles: segment collision test and steering.
 *
 * Pure geometry shared by every moving object that must not fly through
 * solid objects: ships (collision), comets (collision) and, later, the
 * "safe" navigation and the NPC obstacle avoidance. Each obstacle is a
 * sphere in galactic coordinates with two radii:
 *
 *   r_hit    contact distance; the segment test reports a collision when a
 *            moving point comes this close (0 = not solid, never a hit);
 *   r_avoid  distance to keep when steering around it (>= r_hit).
 *
 * Header-only, C99, depends only on <math.h>; no server globals, so the
 * unit test (tests/steer_avoid_test.c) links nothing else.
 */
#ifndef STEER_AVOID_H
#define STEER_AVOID_H

#include <math.h>

typedef struct {
    double x, y, z;     /* centre (galactic coordinates)                  */
    double r_hit;       /* contact distance, 0 = never a collision        */
    double r_avoid;     /* steering keep-out distance                     */
    int    kind;        /* OBS_* kind (see obstacles.h)                   */
    int    id;          /* object id, 0 = none (used to skip one object)  */
} ObsSphere;

#define SA_AVOID_SAFETY 1.05   /* grazing path clears r_avoid by 5% */

static inline double sa_dot(const double a[3], const double b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* out = v / |v|, returns |v|; out = v when |v| is ~0. */
static inline double sa_norm(const double v[3], double out[3]) {
    double l = sqrt(sa_dot(v, v));
    if (l > 1e-12) {
        out[0] = v[0] / l; out[1] = v[1] / l; out[2] = v[2] / l;
    } else {
        out[0] = v[0]; out[1] = v[1]; out[2] = v[2];
    }
    return l;
}

/* Unit vector perpendicular to the unit vector u; `seed` rotates it around u
 * so that a head-on approach picks a stable but varied side. */
static inline void sa_perp(const double u[3], unsigned seed, double out[3]) {
    double ax[3] = {0, 0, 0};
    double ax_abs = fabs(u[0]);
    int k = 0;
    if (fabs(u[1]) < ax_abs) { k = 1; ax_abs = fabs(u[1]); }
    if (fabs(u[2]) < ax_abs) { k = 2; }
    ax[k] = 1.0;
    double p[3] = { u[1] * ax[2] - u[2] * ax[1],
                    u[2] * ax[0] - u[0] * ax[2],
                    u[0] * ax[1] - u[1] * ax[0] };
    double pn[3];
    sa_norm(p, pn);
    double q[3] = { u[1] * pn[2] - u[2] * pn[1],
                    u[2] * pn[0] - u[0] * pn[2],
                    u[0] * pn[1] - u[1] * pn[0] };
    unsigned h = seed * 2654435761u;
    h ^= h >> 15;
    double a = (double)(h % 3600u) * (0.1 * 3.14159265358979323846 / 180.0);
    double c = cos(a), s = sin(a);
    out[0] = pn[0] * c + q[0] * s;
    out[1] = pn[1] * c + q[1] * s;
    out[2] = pn[2] * c + q[2] * s;
}

/*
 * Does the segment p0 -> p1 ENTER the sphere (c, r)? Returns 1 and the
 * parameter t in [0,1] of the first contact. A start that is already inside
 * only counts while moving toward the centre (t = 0): a ship that was left
 * inside an object can fly away, and is not hit again on every tick.
 */
static inline int sa_segment_enter(const double p0[3], const double p1[3],
                                   const double c[3], double r, double *t_out) {
    if (r <= 0.0) return 0;
    double d[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
    double f[3] = { p0[0] - c[0], p0[1] - c[1], p0[2] - c[2] };
    double a = sa_dot(d, d);
    if (a < 1e-18) return 0;
    double b = sa_dot(f, d);
    double cc = sa_dot(f, f) - r * r;
    if (cc <= 0.0) {
        if (b < 0.0) { *t_out = 0.0; return 1; }
        return 0;
    }
    double disc = b * b - a * cc;
    if (disc < 0.0) return 0;
    double t = (-b - sqrt(disc)) / a;
    if (t < 0.0 || t > 1.0) return 0;
    *t_out = t;
    return 1;
}

/*
 * Earliest collision of the segment p0 -> p1 with the solid obstacles
 * (r_hit > 0). `extra` is added to every contact distance, `skip_id` (when
 * non-zero) names one object to ignore (e.g. the target of an approach).
 * Returns the index of the obstacle hit, or -1; *t_out receives t in [0,1].
 */
static inline int sa_first_hit(const double p0[3], const double p1[3],
                               const ObsSphere *obs, int n, double extra,
                               int skip_id, double *t_out) {
    int best = -1;
    double best_t = 2.0;
    for (int i = 0; i < n; i++) {
        const ObsSphere *o = &obs[i];
        if (o->r_hit <= 0.0) continue;
        if (skip_id != 0 && o->id == skip_id) continue;
        double c[3] = { o->x, o->y, o->z };
        double t;
        if (sa_segment_enter(p0, p1, c, o->r_hit + extra, &t) && t < best_t) {
            best_t = t;
            best = i;
        }
    }
    if (best >= 0) *t_out = best_t;
    return best;
}

/*
 * Steering. A ship at p moving along dir_in (any length) looks `lookahead`
 * units ahead for the keep-out spheres (r_avoid + extra). Returns 0 and the
 * normalized dir_in when the way is free. Otherwise returns 1 and in out[]
 * the direction of the tangent to the nearest blocking sphere, on the side
 * the current path already leans to (a head-on approach picks the side from
 * `seed`), so the new path grazes the keep-out distance with a 5% margin.
 * A point already inside a keep-out sphere and moving toward its centre is
 * pushed out along the sphere (radial + tangential component of dir).
 * The caller blends out[] with its previous direction (sa_blend) to get a
 * smooth, visible curve instead of a kink.
 */
static inline int sa_steer(const double p[3], const double dir_in[3],
                           double lookahead, const ObsSphere *obs, int n,
                           double extra, unsigned seed, int skip_id,
                           double out[3]) {
    double dir[3];
    double len = sa_norm(dir_in, dir);
    out[0] = dir[0]; out[1] = dir[1]; out[2] = dir[2];
    if (len < 1e-12) return 0;

    int best = -1;
    double best_t = 1e300;
    for (int i = 0; i < n; i++) {
        const ObsSphere *o = &obs[i];
        double R = o->r_avoid + extra;
        if (R <= 0.0) continue;
        if (skip_id != 0 && o->id == skip_id) continue;
        double rel[3] = { o->x - p[0], o->y - p[1], o->z - p[2] };
        double d2 = sa_dot(rel, rel);
        double entry;
        if (d2 < R * R) {
            if (d2 > 1e-18 && sa_dot(rel, dir) <= 0.0) continue;   /* already leaving */
            entry = 0.0;
        } else {
            double t = sa_dot(rel, dir);
            if (t <= 0.0 || t > lookahead + R) continue;
            double dp2 = d2 - t * t;
            if (dp2 >= R * R) continue;
            entry = t - sqrt(R * R - dp2);
            if (entry < 0.0) entry = 0.0;
            if (entry > lookahead) continue;
        }
        if (entry < best_t) { best_t = entry; best = i; }
    }
    if (best < 0) return 0;

    const ObsSphere *o = &obs[best];
    double R = o->r_avoid + extra;
    double rel[3] = { o->x - p[0], o->y - p[1], o->z - p[2] };
    double D = sqrt(sa_dot(rel, rel));
    double relu[3];
    sa_norm(rel, relu);

    if (D < R) {
        /* inside the keep-out sphere: radial push plus the tangential
         * part of the current direction */
        double away[3] = { -relu[0], -relu[1], -relu[2] };
        if (D < 1e-9) sa_perp(dir, seed, away);
        double dn = sa_dot(dir, away);
        double tang[3] = { dir[0] - away[0] * dn, dir[1] - away[1] * dn, dir[2] - away[2] * dn };
        double v[3] = { away[0] + tang[0], away[1] + tang[1], away[2] + tang[2] };
        sa_norm(v, out);
        return 1;
    }

    /* side: where the straight path passes relative to the centre */
    double t = sa_dot(rel, dir);
    double lat[3] = { p[0] + dir[0] * t - o->x, p[1] + dir[1] * t - o->y, p[2] + dir[2] * t - o->z };
    double ld = sa_dot(lat, relu);
    double lp[3] = { lat[0] - relu[0] * ld, lat[1] - relu[1] * ld, lat[2] - relu[2] * ld };
    double lpn[3];
    double ll = sa_norm(lp, lpn);
    if (ll < 1e-6) sa_perp(relu, seed, lpn);

    double Rs = R * SA_AVOID_SAFETY;
    double s = Rs / D;
    if (s > 1.0) s = 1.0;
    double theta = asin(s);
    double v[3] = { relu[0] * cos(theta) + lpn[0] * sin(theta),
                    relu[1] * cos(theta) + lpn[1] * sin(theta),
                    relu[2] * cos(theta) + lpn[2] * sin(theta) };
    sa_norm(v, out);
    return 1;
}

/* out = normalize(old * (1 - k) + new * k): smooth turn, 0 < k <= 1. */
static inline void sa_blend(const double old_dir[3], const double new_dir[3],
                            double k, double out[3]) {
    double v[3] = { old_dir[0] * (1.0 - k) + new_dir[0] * k,
                    old_dir[1] * (1.0 - k) + new_dir[1] * k,
                    old_dir[2] * (1.0 - k) + new_dir[2] * k };
    if (sa_dot(v, v) < 1e-12) { out[0] = new_dir[0]; out[1] = new_dir[1]; out[2] = new_dir[2]; return; }
    sa_norm(v, out);
}

#endif /* STEER_AVOID_H */
