/*
 * moon_orbits.h - deterministic orbits of the natural moons (types 89..98).
 *
 * Every moon orbits one planet, alone on its own orbit, and a planet has at
 * most MOON_MAX_PER_PLANET moons. Nothing here is saved in galaxy.dat: the
 * parent planet and the orbit of a moon are derived from its id (hash) and
 * from the order of the moons in cosmic_features[], so the same galaxy.dat
 * always gives the same orbits. The angle comes from the wall clock, so the
 * moons keep moving across server restarts and need no state.
 *
 * Orbits are fast enough to observe in real-time (60 seconds for the innermost
 * moon, longer outward, Kepler-like period ~ r^1.5) and always stay inside the
 * planet's quadrant: only planets at least MOON_ORBIT_MAX_RADIUS away from
 * every face of the quadrant can have moons, so a moon never changes quadrant
 * and the static per-quadrant index stays valid.
 *
 * Header-only, C99, depends only on <math.h>. Tests: tests/moon_orbit_test.c.
 */
#ifndef MOON_ORBITS_H
#define MOON_ORBITS_H

#include <math.h>

#define MOON_MAX_PER_PLANET      4      /* at most 4 moons around a planet     */
#define MOON_ORBIT_MIN_RADIUS    3.375  /* innermost orbit (sector units, +50% from 2.25) */
#define MOON_ORBIT_STEP          2.43   /* distance between two orbits (1.5x moon diameter: 1.5 * 1.62) */
#define MOON_ORBIT_JITTER        0.25   /* random extra radius, < STEP         */
#define MOON_ORBIT_MAX_RADIUS \
    (MOON_ORBIT_MIN_RADIUS + MOON_ORBIT_STEP * (MOON_MAX_PER_PLANET - 1) + MOON_ORBIT_JITTER)
#define MOON_ORBIT_BASE_PERIOD_S 60.0   /* period of a MIN_RADIUS orbit, seconds (1 min) */

typedef struct {
    double r;          /* orbit radius                                   */
    double omega;      /* rad/s, negative = retrograde                   */
    double phase;      /* angle at t = 0                                 */
    double u[3], v[3]; /* orthonormal basis of the orbital plane         */
} MoonOrbit;

static inline unsigned moon_hash(unsigned id, unsigned salt) {
    unsigned h = id * 2654435761u ^ (salt * 0x9E3779B9u);
    h ^= h >> 16; h *= 0x85EBCA6Bu;
    h ^= h >> 13; h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

/* Uniform value in [0, 1). */
static inline double moon_h01(unsigned id, unsigned salt) {
    return (double)(moon_hash(id, salt) & 0xFFFFFFu) / 16777216.0;
}

/* A planet can host moons when the largest orbit fits inside its quadrant. */
static inline int moon_planet_eligible(double x, double y, double z, double quadrant_size) {
    const double m = MOON_ORBIT_MAX_RADIUS;
    return x >= m && x <= quadrant_size - m &&
           y >= m && y <= quadrant_size - m &&
           z >= m && z <= quadrant_size - m;
}

/* Pick the parent among n_elig eligible planets: start from a hash of the
 * moon id. In the first pass, probe forward to the first planet with 0 moons
 * so that every planet receives at least one moon. In the second pass, probe
 * to planets with fewer than MOON_MAX_PER_PLANET moons. pcount[j] = moons
 * already given to eligible planet j. Returns j, or -1 when full. */
static inline int moon_pick_planet(unsigned moon_id, int n_elig, const int *pcount) {
    if (n_elig <= 0) return -1;
    int start = (int)(moon_hash(moon_id, 1u) % (unsigned)n_elig);
    for (int k = 0; k < n_elig; k++) {
        int j = (start + k) % n_elig;
        if (pcount[j] == 0) return j;
    }
    for (int k = 0; k < n_elig; k++) {
        int j = (start + k) % n_elig;
        if (pcount[j] < MOON_MAX_PER_PLANET) return j;
    }
    return -1;
}

/* Orbit of a moon. moon_type_or_index = type or (type - OBJ_MOON_FIRST),
 * ordinal = how many moons the planet already had (0..3): the radius grows with the
 * ordinal, so the orbits of one planet never coincide. Regular moons are
 * nearly equatorial, irregular moons (index 8) and retrograde/captured moons are
 * steeply inclined and retrograde. */
static inline void moon_orbit_make(unsigned moon_id, int moon_type_or_index, int ordinal, MoonOrbit *o) {
    const double TWO_PI = 6.283185307179586;
    double incl_max = 0.6;                      /* rad */
    double incl_min = 0.0;
    int retro = 0;
    if (moon_type_or_index == 7 || moon_type_or_index == 89 + 7) {
        incl_max = 0.2;   /* regular            */
    } else if (moon_type_or_index == 8 || moon_type_or_index == 89 + 8 ||
               moon_type_or_index == 165 + 0 ||  /* Retrograde Moon    */
               moon_type_or_index == 165 + 5) {  /* Captured Moon      */
        incl_min = 0.9; incl_max = 1.4; retro = 1; /* irregular / retrograde */
    } else if (moon_type_or_index == 165 + 4) {  /* Embedded Moonlet   */
        incl_max = 0.05;                         /* equatorial in ring */
    }

    double incl = incl_min + (incl_max - incl_min) * moon_h01(moon_id, 3u);
    double node = TWO_PI * moon_h01(moon_id, 4u);

    o->r = MOON_ORBIT_MIN_RADIUS + MOON_ORBIT_STEP * ordinal
         + MOON_ORBIT_JITTER * moon_h01(moon_id, 2u);
    double period = MOON_ORBIT_BASE_PERIOD_S * pow(o->r / MOON_ORBIT_MIN_RADIUS, 1.5);
    if (moon_type_or_index == 165 + 1) {         /* Resonant Moon      */
        period = MOON_ORBIT_BASE_PERIOD_S * (1.0 + (double)ordinal);
    }
    o->omega = (retro ? -1.0 : 1.0) * TWO_PI / period;
    o->phase = TWO_PI * moon_h01(moon_id, 5u);

    /* u = Rz(node) Rx(incl) x^, v = Rz(node) Rx(incl) y^ */
    double cn = cos(node), sn = sin(node), ci = cos(incl), si = sin(incl);
    o->u[0] = cn;        o->u[1] = sn;       o->u[2] = 0.0;
    o->v[0] = -sn * ci;  o->v[1] = cn * ci;  o->v[2] = si;
}

/* Offset of the moon from its planet at time t (seconds). */
static inline void moon_orbit_offset(const MoonOrbit *o, double t, double out[3]) {
    double a = fmod(o->phase + o->omega * t, 6.283185307179586);
    double c = cos(a) * o->r, s = sin(a) * o->r;
    for (int i = 0; i < 3; i++) out[i] = c * o->u[i] + s * o->v[i];
}

#endif /* MOON_ORBITS_H */
