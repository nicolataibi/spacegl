/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * nav_autopilot.h - the collision-free course of `nav` on the live galaxy.
 *
 * The geometry is include/nav_route.h; this header feeds it the obstacles
 * of the galaxy (include/obstacles.h: every solid object, the keep-out of
 * black holes and pulsars, the other ships) and flies the result:
 *
 *   nav_ap_plan     gathers the obstacles of the corridor start -> goal,
 *                   plans, gathers again along the course it found (a
 *                   detour may reach quadrants the straight line did not)
 *                   and plans again with them, at most NAV_AP_PASSES times;
 *   nav_ap_install  makes it the course of captain i (UPD_ROUTE to the deck);
 *   nav_ap_follow   moves the ship `step` units along the course (as many
 *                   waypoints as it passes in one tick), turning the ship
 *                   smoothly onto each new leg;
 *   nav_ap_watch    every NAV_AP_CHECK_TICKS looks at the course ahead for a
 *                   moving object (comet, orbiting moon, NPC) that came
 *                   onto it and plots a correction from where the ship is.
 *
 * Static inline on purpose (like obstacles.h): commands.c and logic.c are
 * linked by several test executables that must not need one more source
 * file. Tests: tests/nav_route_test.c (geometry), the safe-course part of
 * tests/server_commands_test.c (real galaxy).
 */
#ifndef NAV_AUTOPILOT_H
#define NAV_AUTOPILOT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "server_internal.h"
#include "obstacles.h"
#include "nav_route.h"
#include "nav_math.h"

#define NAV_AP_MARGIN       14.0   /* corridor half-width gathered around the course */
#define NAV_AP_PASSES       3      /* plan / re-gather rounds                         */
#define NAV_AP_CHECK_TICKS  12     /* look at the course ahead 5 times a second       */
#define NAV_AP_CHECK_EXTRA  0.6    /* contact radius + this = "on the course"         */
#define NAV_AP_REPLAN_COOL  30     /* ticks between two corrections                   */
#define NAV_AP_TURN_RATE    0.22   /* share of the heading error turned per tick      */
#define NAV_AP_QHASH        8192   /* quadrant set of one gather                      */
#define NAV_AP_PER_QUADRANT 768    /* room reserved for the obstacles of one quadrant */
#define NAV_AP_MAX_OBS      65536

typedef struct {
    ObsSphere *v;
    int n, cap;
} NavObsBuf;

static inline void nav_obs_free(NavObsBuf *b) {
    free(b->v);
    b->v = NULL;
    b->n = b->cap = 0;
}

static inline int nav_obs_reserve(NavObsBuf *b, int more) {
    if (b->n + more <= b->cap) return 1;
    int nc = b->cap ? b->cap : 1024;
    while (nc < b->n + more) nc *= 2;
    if (nc > NAV_AP_MAX_OBS) nc = NAV_AP_MAX_OBS;
    if (nc < b->n + more) return 0;
    ObsSphere *nv = (ObsSphere *)realloc(b->v, sizeof(ObsSphere) * (size_t)nc);
    if (!nv) return 0;
    b->v = nv;
    b->cap = nc;
    return 1;
}

/* Keep-out radius of the course around an object of each kind: at least
 * the obstacles.h keep-out (contact + margin), and at least the size the
 * 3D views draw the object at plus one unit (a planet is drawn with radius
 * 1.8, a star 2.5, a brown dwarf 2.0, a starbase with its ring ~1.5, the
 * accretion disk of a black hole ~5), so the course on screen never grazes
 * an object it is said to avoid. */
static inline double nav_ap_clearance(int kind) {
    switch (kind) {
        case OBS_PLANET:
        case OBS_ROGUE_PLANET: return 2.8;
        case OBS_STAR: return 3.5;
        case OBS_BROWN_DWARF: return 3.0;
        case OBS_BASE: return 2.5;
        case OBS_MOON: return 2.1;
        case OBS_BLACK_HOLE: return 6.0;
        default: return 0.0;
    }
}

/* Allowed box of a course: the galaxy, half a unit inside its faces (the
 * hyperdrive stops at DIST_EPSILON from them). */
static inline void nav_ap_cfg(int i, NavRouteCfg *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    double hi = GALAXY_SIZE * QUADRANT_SIZE - 0.5;
    for (int a = 0; a < 3; a++) {
        cfg->lo[a] = 0.5;
        cfg->hi[a] = hi;
    }
    cfg->extra = 0.0;
    cfg->seed = (unsigned)(i + 1) * 2654435761u;
}

/* Distance from p to the polyline pts[0..n-1]. */
static inline double nav_ap_poly_dist(const double p[3], const double (*pts)[3], int n) {
    if (n == 1) return nr_dist(p, pts[0]);
    double d = HUGE_VAL;
    for (int k = 1; k < n; k++) {
        double e = nr_point_seg_dist(p, pts[k - 1], pts[k]);
        if (e < d) d = e;
    }
    return d;
}

static inline int nav_ap_same_obs(const ObsSphere *a, const ObsSphere *b) {
    return a->kind == b->kind && a->id == b->id &&
           fabs(a->x - b->x) < 1e-9 && fabs(a->y - b->y) < 1e-9 && fabs(a->z - b->z) < 1e-9;
}

/*
 * Appends to b the obstacles (kinds in mask) within `margin` of the polyline
 * pts[0..n-1], each once (dedupe = 1 checks what b already holds). The
 * quadrants are those within `margin` of points sampled every half quadrant
 * along the polyline, each read once. `self` (the asking ship) is left out.
 */
static inline void nav_ap_gather(const double (*pts)[3], int n, double margin, unsigned mask,
                                 const void *self, NavObsBuf *b, int dedupe) {
    if (!spatial_index || n < 1) return;
    int *qset = (int *)calloc(NAV_AP_QHASH, sizeof(int));   /* key + 1, 0 = empty */
    if (!qset) return;
    int first_new = b->n;
    for (int k = 0; k < n; k++) {
        const double *a = pts[k];
        const double *c = (k + 1 < n) ? pts[k + 1] : pts[k];
        double L = nr_dist(a, c);
        int steps = (int)ceil(L / (QUADRANT_SIZE * 0.5));
        if (steps < 1) steps = 1;
        for (int s = 0; s <= steps; s++) {
            double t = (double)s / steps;
            double p[3] = { a[0] + (c[0] - a[0]) * t, a[1] + (c[1] - a[1]) * t, a[2] + (c[2] - a[2]) * t };
            int lo[3], hi[3];
            for (int q = 0; q < 3; q++) {
                lo[q] = get_q_from_g(p[q] - margin);
                hi[q] = get_q_from_g(p[q] + margin);
            }
            for (int q1 = lo[0]; q1 <= hi[0]; q1++)
                for (int q2 = lo[1]; q2 <= hi[1]; q2++)
                    for (int q3 = lo[2]; q3 <= hi[2]; q3++) {
                        int key = (q1 * 41 + q2) * 41 + q3;
                        unsigned h = ((unsigned)key * 2654435761u) & (NAV_AP_QHASH - 1);
                        int seen = 0;
                        for (int probe = 0; probe < NAV_AP_QHASH; probe++) {
                            if (qset[h] == 0) {
                                qset[h] = key + 1;
                                break;
                            }
                            if (qset[h] == key + 1) {
                                seen = 1;
                                break;
                            }
                            h = (h + 1) & (NAV_AP_QHASH - 1);
                        }
                        if (seen) continue;
                        if (!nav_obs_reserve(b, NAV_AP_PER_QUADRANT)) continue;
                        int before = b->n;
                        b->n = obs_gather_quadrant_ex(q1, q2, q3, mask, b->v, b->n,
                                                      b->n + NAV_AP_PER_QUADRANT, self);
                        /* keep only what is near the course, with the
                         * course's keep-out radius */
                        int w = before;
                        for (int r = before; r < b->n; r++) {
                            double cl = nav_ap_clearance(b->v[r].kind);
                            if (b->v[r].r_avoid < cl) b->v[r].r_avoid = cl;
                            double cc[3] = { b->v[r].x, b->v[r].y, b->v[r].z };
                            double reach = margin + (b->v[r].r_avoid > b->v[r].r_hit ? b->v[r].r_avoid : b->v[r].r_hit);
                            if (nav_ap_poly_dist(cc, pts, n) <= reach) b->v[w++] = b->v[r];
                        }
                        b->n = w;
                    }
        }
    }
    free(qset);
    if (dedupe && first_new > 0) {
        int w = first_new;
        for (int r = first_new; r < b->n; r++) {
            int dup = 0;
            for (int k = 0; k < first_new && !dup; k++) dup = nav_ap_same_obs(&b->v[k], &b->v[r]);
            if (!dup) b->v[w++] = b->v[r];
        }
        b->n = w;
    }
}

/* Hazards of a course as wire records (positions, radii, kinds). */
static inline int nav_ap_hazards(const NavRoute *r, const NavObsBuf *b, const NavRouteCfg *cfg,
                                 NetRouteHazard *out, int max) {
    int n = 0;
    for (int k = 0; k < r->n_hazards && n < max; k++) {
        int idx = r->hazard[k];
        if (idx < 0 || idx >= b->n) continue;
        const ObsSphere *o = &b->v[idx];
        out[n].x = (float)o->x;
        out[n].y = (float)o->y;
        out[n].z = (float)o->z;
        out[n].r = (float)nr_radius(cfg, o);
        out[n].kind = o->kind;
        n++;
    }
    return n;
}

/*
 * Plans the course of captain i from start to goal on the live galaxy.
 * Returns nav_route_plan's result; the hazards of the course go to haz[]
 * (*n_haz of them, at most NAV_WIRE_MAX_HAZ).
 */
typedef struct {
    int goal_kind;     /* GOAL_MOVED: kind of the obstacle that held the goal, else 0 */
    int block_kind;    /* PARTIAL / walled in: kind of the one that blocks, else 0    */
    int n_obstacles;   /* obstacles the planner looked at                             */
} NavApInfo;

static inline int nav_ap_plan_ex(int i, const double start[3], const double goal[3],
                                 NavRoute *r, NetRouteHazard *haz, int *n_haz, NavApInfo *info) {
    NavRouteCfg cfg;
    nav_ap_cfg(i, &cfg);
    NavObsBuf b = { NULL, 0, 0 };
    const double line[2][3] = { { start[0], start[1], start[2] }, { goal[0], goal[1], goal[2] } };
    nav_ap_gather(line, 2, NAV_AP_MARGIN, OBS_MASK_AVOID, &players[i], &b, 0);
    int rc = -1;
    for (int pass = 0; pass < NAV_AP_PASSES; pass++) {
        rc = nav_route_plan(start, goal, b.v, b.n, &cfg, r);
        if (rc < 0 || pass == NAV_AP_PASSES - 1) break;
        /* a detour may have left the corridor: gather along the course
         * itself and plan again if that brought anything new */
        int before = b.n;
        nav_ap_gather((const double(*)[3])r->p, r->n, NAV_AP_MARGIN * 0.5, OBS_MASK_AVOID, &players[i], &b, 1);
        if (b.n == before) break;
    }
    *n_haz = nav_ap_hazards(r, &b, &cfg, haz, NAV_WIRE_MAX_HAZ);
    if (info) {
        info->goal_kind = (r->goal_obstacle >= 0 && r->goal_obstacle < b.n) ? b.v[r->goal_obstacle].kind : 0;
        info->block_kind = (r->block_obstacle >= 0 && r->block_obstacle < b.n) ? b.v[r->block_obstacle].kind : 0;
        info->n_obstacles = b.n;
    }
    nav_obs_free(&b);
    return rc;
}

static inline int nav_ap_plan(int i, const double start[3], const double goal[3],
                              NavRoute *r, NetRouteHazard *haz, int *n_haz) {
    return nav_ap_plan_ex(i, start, goal, r, haz, n_haz, NULL);
}

/* Is captain i flying (or aligning for) its `nav` course right now? */
static inline int nav_ap_valid(const ConnectedPlayer *p) {
    return p->route_active && p->apr_target == 0 && p->nav_route.n >= 2 &&
           (p->nav_state == NAV_STATE_ALIGN || p->nav_state == NAV_STATE_HYPERDRIVE) &&
           p->target_gx == p->route_goal[0] && p->target_gy == p->route_goal[1] &&
           p->target_gz == p->route_goal[2];
}

static inline void nav_ap_clear(ConnectedPlayer *p) {
    if (p->route_active) p->route_seq++;
    p->route_active = 0;
    p->route_n_haz = 0;
}

/* Unit direction of the leg being flown from pos. */
static inline int nav_ap_leg_dir(const ConnectedPlayer *p, const double pos[3], double dir[3]) {
    if (p->route_next >= p->nav_route.n) return 0;
    double d[3];
    nr_sub(p->nav_route.p[p->route_next], pos, d);
    return sa_norm(d, dir) > 1e-9;
}

/* Makes r the course of captain i: the target becomes its end. */
static inline void nav_ap_install(int i, const NavRoute *r, const NetRouteHazard *haz, int n_haz) {
    ConnectedPlayer *p = &players[i];
    p->nav_route = *r;
    p->route_next = 1;
    p->route_active = 1;
    p->route_seq++;
    p->route_check_timer = NAV_AP_CHECK_TICKS;
    p->route_replan_cooldown = 0;
    const double *end = r->p[r->n - 1];
    p->target_gx = end[0];
    p->target_gy = end[1];
    p->target_gz = end[2];
    p->route_goal[0] = end[0];
    p->route_goal[1] = end[1];
    p->route_goal[2] = end[2];
    p->route_n_haz = n_haz > NAV_WIRE_MAX_HAZ ? NAV_WIRE_MAX_HAZ : n_haz;
    memcpy(p->route_haz, haz, sizeof(NetRouteHazard) * (size_t)p->route_n_haz);
    double pos[3] = { p->gx, p->gy, p->gz }, dir[3];
    if (nav_ap_leg_dir(p, pos, dir)) {
        p->dx = dir[0];
        p->dy = dir[1];
        p->dz = dir[2];
    }
}

/* "planet, 3 asteroids, black hole" from the hazards of a course. */
static inline void nav_ap_hazard_text(const NetRouteHazard *haz, int n, char *out, size_t cap) {
    int count[OBS_KIND_COUNT] = { 0 };
    for (int k = 0; k < n; k++)
        if (haz[k].kind > 0 && haz[k].kind < OBS_KIND_COUNT) count[haz[k].kind]++;
    size_t used = 0;
    out[0] = '\0';
    for (int kind = 1; kind < OBS_KIND_COUNT; kind++) {
        if (!count[kind]) continue;
        int w;
        if (count[kind] == 1) w = snprintf(out + used, cap - used, "%s%s", used ? ", " : "", obs_kind_name(kind));
        else w = snprintf(out + used, cap - used, "%s%d x %s", used ? ", " : "", count[kind], obs_kind_name(kind));
        if (w < 0 || (size_t)w >= cap - used) break;
        used += (size_t)w;
    }
}

/* Remaining length of the course from pos. */
static inline double nav_ap_remaining(const ConnectedPlayer *p, const double pos[3]) {
    if (p->route_next >= p->nav_route.n) return 0.0;
    double L = nr_dist(pos, p->nav_route.p[p->route_next]);
    for (int k = p->route_next + 1; k < p->nav_route.n; k++)
        L += nr_dist(p->nav_route.p[k - 1], p->nav_route.p[k]);
    return L;
}

/*
 * Moves captain i `step` units along its course. Returns 1 when the end
 * was reached (the ship is on it), 0 otherwise. Movement vector and
 * heading follow the leg being flown.
 */
static inline int nav_ap_follow(int i, double step) {
    ConnectedPlayer *p = &players[i];
    double pos[3] = { p->gx, p->gy, p->gz };
    double left = step;
    int guard = 0;
    while (left > 0.0 && p->route_next < p->nav_route.n && guard++ < NAV_ROUTE_MAX_WP + 2) {
        const double *wp = p->nav_route.p[p->route_next];
        double d = nr_dist(pos, wp);
        if (d <= left) {
            nr_copy(pos, wp);
            left -= d;
            p->route_next++;
        } else {
            for (int q = 0; q < 3; q++) pos[q] += (wp[q] - pos[q]) * (left / d);
            left = 0.0;
        }
    }
    p->gx = pos[0];
    p->gy = pos[1];
    p->gz = pos[2];
    if (p->route_next >= p->nav_route.n) return 1;

    double dir[3];
    if (nav_ap_leg_dir(p, pos, dir)) {
        p->dx = dir[0];
        p->dy = dir[1];
        p->dz = dir[2];
        double th, tm;
        nav_route_dir_to_hm(dir, &th, &tm);
        p->state.van_h = nav_heading_at(p->state.van_h, th, NAV_AP_TURN_RATE);
        p->state.van_m += (tm - p->state.van_m) * NAV_AP_TURN_RATE;
    }
    return 0;
}

/*
 * In-flight watch: a moving object on the course ahead (contact radius +
 * NAV_AP_CHECK_EXTRA) triggers a correction from the ship's position to the
 * same end. Returns 1 when the course changed, -1 when no safe course is
 * left (the caller drops out of hyperdrive), 0 otherwise.
 */
static inline int nav_ap_watch(int i, char *msg, size_t cap) {
    ConnectedPlayer *p = &players[i];
    if (p->route_replan_cooldown > 0) p->route_replan_cooldown--;
    if (--p->route_check_timer > 0) return 0;
    p->route_check_timer = NAV_AP_CHECK_TICKS;
    if (p->route_replan_cooldown > 0) return 0;

    double pos[3] = { p->gx, p->gy, p->gz };
    double speed_s = p->hyper_speed * (double)GAME_TICK_RATE;
    double look = speed_s * 1.5;
    if (look < 25.0) look = 25.0;

    /* the course ahead, cut at `look` */
    double pts[NAV_ROUTE_MAX_WP + 1][3];
    int np = 0;
    nr_copy(pts[np++], pos);
    double acc = 0.0;
    for (int k = p->route_next; k < p->nav_route.n && acc < look; k++) {
        acc += nr_dist(pts[np - 1], p->nav_route.p[k]);
        nr_copy(pts[np++], p->nav_route.p[k]);
    }
    NavObsBuf b = { NULL, 0, 0 };
    nav_ap_gather((const double(*)[3])pts, np, 4.0, OBS_MASK_ALL, &players[i], &b, 0);
    int hit = nav_route_first_hit(&p->nav_route, p->route_next, pos, look, b.v, b.n, NAV_AP_CHECK_EXTRA);
    int kind = (hit >= 0) ? b.v[hit].kind : 0;
    nav_obs_free(&b);
    if (hit < 0) return 0;

    p->route_replan_cooldown = NAV_AP_REPLAN_COOL;
    NavRoute r;
    NetRouteHazard haz[NAV_WIRE_MAX_HAZ];
    int nh = 0;
    double goal[3] = { p->route_goal[0], p->route_goal[1], p->route_goal[2] };
    int rc = nav_ap_plan(i, pos, goal, &r, haz, &nh);
    if (rc < 0 || r.length < 1e-6) {
        snprintf(msg, cap, "ALL STOP: a %s is on the course and no safe way round is left.", obs_kind_name(kind));
        return -1;
    }
    nav_ap_install(i, &r, haz, nh);
    snprintf(msg, cap, "COURSE CORRECTION: %s %s the course. New course: %d waypoint%s, %.1f units%s.",
             (kind == OBS_SHIP) ? "a ship" : obs_kind_name(kind),
             (kind == OBS_COMET || kind == OBS_SHIP) ? "crossing" : "drifting onto",
             r.n - 2, (r.n - 2) == 1 ? "" : "s", r.length,
             (r.flags & NAV_RF_PARTIAL) ? " (stops short: no way through)" : "");
    return 1;
}

/* Wire record of captain i's course (UPD_ROUTE): the waypoints still to fly. */
static inline void nav_ap_fill_wire(const ConnectedPlayer *p, UpdateBlockRoute *w) {
    memset(w, 0, sizeof(*w));
    w->seq = p->route_seq;
    if (!nav_ap_valid(p)) return;
    w->active = 1;
    w->flags = (uint8_t)p->nav_route.flags;
    int c = 0;
    for (int k = p->route_next; k < p->nav_route.n && c < NAV_WIRE_MAX_WP; k++, c++) {
        w->wp[c][0] = (float)p->nav_route.p[k][0];
        w->wp[c][1] = (float)p->nav_route.p[k][1];
        w->wp[c][2] = (float)p->nav_route.p[k][2];
    }
    w->count = (uint8_t)c;
    w->passed = (uint16_t)(p->route_next - 1);
    w->total = (uint16_t)(p->nav_route.n - 1);
    w->length = (float)p->nav_route.length;
    w->direct = (float)p->nav_route.direct;
    w->hazard_count = (uint8_t)p->route_n_haz;
    memcpy(w->hazards, p->route_haz, sizeof(NetRouteHazard) * (size_t)p->route_n_haz);
}

#endif /* NAV_AUTOPILOT_H */
