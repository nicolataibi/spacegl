/*
 * obstacles.h - solid objects of the galaxy as sphere obstacles.
 *
 * Reads the per-quadrant spatial index (read-only, so it is safe inside the
 * OpenMP loops of the tick) and returns the objects around a segment as
 * ObsSphere (include/steer_avoid.h) in galactic coordinates. One table gives
 * the contact distance (r_hit) and the steering keep-out distance (r_avoid)
 * of every kind:
 *
 *   solid kinds   r_hit = DIST_COLLISION_SHIP, r_avoid = r_hit + margin
 *   black hole    r_hit = 0 (its own rules decide the damage), r_avoid =
 *                 DIST_GRAVITY_WELL + margin
 *   pulsar        solid, r_avoid = DIST_PULSAR_BEAM + margin
 *
 * Header-only (static inline) on purpose: logic.c / logic_events.c are linked
 * by several test executables that must not need one more source file.
 * Tests: tests/steer_avoid_test.c (geometry), tests/obstacles_test.c (index).
 */
#ifndef OBSTACLES_H
#define OBSTACLES_H

#include "server_internal.h"
#include "object_types.h"
#include "steer_avoid.h"

enum {
    OBS_NONE = 0,
    OBS_PLANET, OBS_STAR, OBS_MOON, OBS_ASTEROID, OBS_BASE, OBS_DERELICT,
    OBS_BLACK_HOLE, OBS_PULSAR, OBS_NEUTRON_STAR, OBS_BROWN_DWARF,
    OBS_ROGUE_PLANET, OBS_COMET,
    OBS_KIND_COUNT
};

#define OBS_BIT(k)        (1u << (k))
#define OBS_MASK_ALL      ((1u << OBS_KIND_COUNT) - 1u)
#define OBS_MASK_NO_COMET (OBS_MASK_ALL & ~OBS_BIT(OBS_COMET))

#define OBS_AVOID_MARGIN  1.2     /* keep-out distance beyond the contact radius */
#define OBS_MAX_PER_CALL  192     /* obstacles returned by one gather            */
#define OBS_MAX_CELLS     64      /* quadrants scanned by one gather             */

static inline const char *obs_kind_name(int kind) {
    static const char *const names[OBS_KIND_COUNT] = {
        "object", "planet", "star", "moon", "asteroid", "starbase", "derelict",
        "black hole", "pulsar", "neutron star", "brown dwarf", "rogue planet",
        "comet"
    };
    return (kind > 0 && kind < OBS_KIND_COUNT) ? names[kind] : names[0];
}

static inline void obs_kind_radii(int kind, double *r_hit, double *r_avoid) {
    switch (kind) {
    case OBS_BLACK_HOLE:
        *r_hit = 0.0;
        *r_avoid = (double)DIST_GRAVITY_WELL + OBS_AVOID_MARGIN;
        break;
    case OBS_PULSAR:
        *r_hit = (double)DIST_COLLISION_SHIP;
        *r_avoid = (double)DIST_PULSAR_BEAM + OBS_AVOID_MARGIN;
        break;
    default:
        *r_hit = (double)DIST_COLLISION_SHIP;
        *r_avoid = (double)DIST_COLLISION_SHIP + OBS_AVOID_MARGIN;
        break;
    }
}

static inline int obs_is_moon_type(int type) {
    return (type >= OBJ_MOON_FIRST && type <= OBJ_MOON_LAST) ||
           (type >= OBJ_MOON_EXT_FIRST && type <= OBJ_MOON_EXT_LAST);
}

static inline int obs_push(ObsSphere *out, int n, int max, int q1, int q2, int q3,
                           double x, double y, double z, int kind, int id) {
    if (n >= max) return n;
    ObsSphere *o = &out[n];
    o->x = (q1 - 1) * QUADRANT_SIZE + x;
    o->y = (q2 - 1) * QUADRANT_SIZE + y;
    o->z = (q3 - 1) * QUADRANT_SIZE + z;
    obs_kind_radii(kind, &o->r_hit, &o->r_avoid);
    o->kind = kind;
    o->id = id;
    return n + 1;
}

#define OBS_SCAN(ARR, CNT, KIND) \
    do { \
        if (mask & OBS_BIT(KIND)) { \
            for (int k_ = 0; k_ < qi->CNT && n < max; k_++) { \
                if (qi->ARR[k_] && qi->ARR[k_]->active) \
                    n = obs_push(out, n, max, q1, q2, q3, qi->ARR[k_]->x, \
                                 qi->ARR[k_]->y, qi->ARR[k_]->z, (KIND), qi->ARR[k_]->id); \
            } \
        } \
    } while (0)

/* Obstacles of one quadrant (kinds in `mask`). Returns the new count. */
static inline int obs_gather_quadrant(int q1, int q2, int q3, unsigned mask,
                                      ObsSphere *out, int n, int max) {
    if (!spatial_index || !IS_Q_VALID(q1, q2, q3)) return n;
    const QuadrantIndex *qi = &spatial_index[q1][q2][q3];
    OBS_SCAN(planets, planet_count, OBS_PLANET);
    OBS_SCAN(stars, star_count, OBS_STAR);
    OBS_SCAN(asteroids, asteroid_count, OBS_ASTEROID);
    OBS_SCAN(bases, base_count, OBS_BASE);
    OBS_SCAN(derelicts, derelict_count, OBS_DERELICT);
    OBS_SCAN(black_holes, bh_count, OBS_BLACK_HOLE);
    OBS_SCAN(pulsars, pulsar_count, OBS_PULSAR);
    OBS_SCAN(neutron_stars, neutron_star_count, OBS_NEUTRON_STAR);
    OBS_SCAN(brown_dwarfs, brown_dwarf_count, OBS_BROWN_DWARF);
    OBS_SCAN(rogue_planets, rogue_planet_count, OBS_ROGUE_PLANET);
    OBS_SCAN(comets, comet_count, OBS_COMET);
    if (mask & OBS_BIT(OBS_MOON)) {
        for (int k = 0; k < qi->feature_count && n < max; k++) {
            const NPCCosmicFeature *f = qi->features[k];
            if (f && f->active && obs_is_moon_type(f->type))
                n = obs_push(out, n, max, q1, q2, q3, f->x, f->y, f->z, OBS_MOON, f->id);
        }
    }
    return n;
}

/* Obstacles in the quadrants touched by the segment p0 -> p1 widened by
 * `margin` (galactic coordinates). At most OBS_MAX_CELLS quadrants are read;
 * a longer segment is gathered from the cells around its start. */
static inline int obs_gather_segment(const double p0[3], const double p1[3],
                                     double margin, unsigned mask,
                                     ObsSphere *out, int max) {
    int lo[3], hi[3];
    for (int a = 0; a < 3; a++) {
        double mn = p0[a] < p1[a] ? p0[a] : p1[a];
        double mx = p0[a] < p1[a] ? p1[a] : p0[a];
        lo[a] = get_q_from_g(mn - margin);
        hi[a] = get_q_from_g(mx + margin);
    }
    while ((hi[0] - lo[0] + 1) * (hi[1] - lo[1] + 1) * (hi[2] - lo[2] + 1) > OBS_MAX_CELLS) {
        for (int a = 0; a < 3; a++) {
            if (hi[a] > lo[a]) hi[a]--;
        }
    }
    int n = 0;
    for (int a = lo[0]; a <= hi[0]; a++)
        for (int b = lo[1]; b <= hi[1]; b++)
            for (int c = lo[2]; c <= hi[2]; c++)
                n = obs_gather_quadrant(a, b, c, mask, out, n, max);
    return n;
}

typedef struct {
    int    kind;
    int    id;
    double t;           /* 0..1 along the segment               */
    double r_hit;       /* contact distance of the object       */
    double point[3];    /* contact point (galactic coordinates) */
    double centre[3];   /* centre of the object that was hit    */
} ObsHit;

/* First solid object hit by the segment p0 -> p1 (see sa_first_hit). */
static inline int obs_segment_hit(const double p0[3], const double p1[3],
                                  double extra, unsigned mask, int skip_id,
                                  ObsHit *hit) {
    ObsSphere obs[OBS_MAX_PER_CALL];
    int n = obs_gather_segment(p0, p1, (double)DIST_COLLISION_SHIP + extra + 0.5, mask,
                               obs, OBS_MAX_PER_CALL);
    double t;
    int i = sa_first_hit(p0, p1, obs, n, extra, skip_id, &t);
    if (i < 0) return 0;
    hit->kind = obs[i].kind;
    hit->id = obs[i].id;
    hit->t = t;
    hit->r_hit = obs[i].r_hit + extra;
    for (int a = 0; a < 3; a++)
        hit->point[a] = p0[a] + (p1[a] - p0[a]) * t;
    hit->centre[0] = obs[i].x; hit->centre[1] = obs[i].y; hit->centre[2] = obs[i].z;
    return 1;
}

#endif /* OBSTACLES_H */
