/*
 * star_system.h - star systems: one star, up to 4 planets orbiting it, up to
 * 4 moons around each planet, all inside ONE quadrant.
 *
 * Everything here is a pure function of (seed, slot, time): nothing about the
 * orbits is saved. galaxy.dat keeps only the star seed, the planet orbit
 * radius and the moon types (see NPCSysStar / NPCSysPlanet / NPCSysMoon in
 * server_internal.h). The angle comes from the wall clock, like the natural
 * moons of moon_orbits.h, so bodies keep moving across restarts.
 *
 * Containment: the layout is built so that the farthest point any body can
 * reach is at most `quadrant_size / 2 - SYS_FACE_MARGIN` from the star, and the
 * star is then placed in the cube where that sphere fits. Hence no body ever
 * leaves the quadrant (per-quadrant caps 1 / 4 / 16 hold and the static
 * quadrant index stays valid). When the drawn system does not fit, planets are
 * dropped (outermost first), then moons of the last planet.
 *
 * Header-only, C99. Depends on moon_orbits.h and object_types.h.
 * Tests: tests/star_system_test.c.
 */
#ifndef STAR_SYSTEM_H
#define STAR_SYSTEM_H

#include <math.h>
#include <time.h>
#include "game_config.h"
#include "object_types.h"
#include "moon_orbits.h"

/* Visual radii (world units, the `scale` of the unit-sphere mesh). */
#define SYS_STAR_RADIUS       2.5
#define SYS_PLANET_RADIUS     1.8
#define SYS_MOON_MAX_RADIUS   0.6
#define SYS_GAP               0.5     /* clearance between neighbouring envelopes */
#define SYS_FACE_MARGIN       0.5     /* clearance from the quadrant faces        */
#define SYS_PLANET_BASE_PERIOD_S 600.0 /* period of a planet at SYS_PLANET_REF_R  */
#define SYS_PLANET_REF_R      14.5
#define SYS_PLANET_MAX_INCL   0.12    /* rad: planets are nearly coplanar          */

/* --- slots and ids ----------------------------------------------------- */
static inline int sys_planet_slot(int s, int k)         { return SYS_PLANETS_PER_STAR * s + k; }
static inline int sys_moon_slot(int s, int k, int j)    { return SYS_PLANETS_PER_STAR * SYS_MOONS_PER_PLANET * s + SYS_MOONS_PER_PLANET * k + j; }
static inline int sys_planet_star(int pslot)            { return pslot / SYS_PLANETS_PER_STAR; }
static inline int sys_planet_ordinal(int pslot)         { return pslot % SYS_PLANETS_PER_STAR; }
static inline int sys_moon_planet(int mslot)            { return mslot / SYS_MOONS_PER_PLANET; }
static inline int sys_moon_star(int mslot)              { return mslot / (SYS_PLANETS_PER_STAR * SYS_MOONS_PER_PLANET); }
static inline int sys_moon_ordinal(int mslot)           { return mslot % SYS_MOONS_PER_PLANET; }

static inline int sys_id_is_star(int id)   { return id >= GALAXY_OBJECT_MIN_SYS_STAR   && id <= GALAXY_OBJECT_MAX_SYS_STAR; }
static inline int sys_id_is_planet(int id) { return id >= GALAXY_OBJECT_MIN_SYS_PLANET && id <= GALAXY_OBJECT_MAX_SYS_PLANET; }
static inline int sys_id_is_moon(int id)   { return id >= GALAXY_OBJECT_MIN_SYS_MOON   && id <= GALAXY_OBJECT_MAX_SYS_MOON; }

/* --- contact radii (r_hit): visual radius + DIST_COLLISION_SHIP ----------- */
static inline double sys_star_hit_radius(void)   { return SYS_STAR_RADIUS   + (double)DIST_COLLISION_SHIP; }
static inline double sys_planet_hit_radius(void) { return SYS_PLANET_RADIUS + (double)DIST_COLLISION_SHIP; }
static inline double sys_moon_hit_radius(int moon_type) {
    return (double)obj_moon_any_scale(moon_type) + (double)DIST_COLLISION_SHIP;
}

/* --- layout -------------------------------------------------------------- */
typedef struct {
    int    n_planets;                          /* 1..4                         */
    int    n_moons[SYS_PLANETS_PER_STAR];      /* 0..4 per planet              */
    double planet_r[SYS_PLANETS_PER_STAR];     /* orbit radius around the star */
    double r_sys;                              /* farthest reach from the star */
} SysLayout;

/* Radius of the outermost moon orbit of a planet with n moons (0 if none). */
static inline double sys_moon_envelope(int n) {
    return n <= 0 ? 0.0 : MOON_ORBIT_MIN_RADIUS + MOON_ORBIT_STEP * (n - 1) + MOON_ORBIT_JITTER;
}

static inline double sys_planet_envelope(int n_moons) {
    double e = sys_moon_envelope(n_moons);
    if (e < SYS_PLANET_RADIUS) e = SYS_PLANET_RADIUS;
    return e + SYS_GAP;
}

/* Orbit radii for the counts already in L (n_planets, n_moons). `overlap` in
 * [0,1]: 0 = planet envelopes do not overlap, 0.5 = they overlap by half. */
static inline void sys_layout_radii(SysLayout *L, double overlap) {
    double prev_e = 0.0, r = 0.0;
    for (int k = 0; k < L->n_planets; k++) {
        double e = sys_planet_envelope(L->n_moons[k]);
        /* Every planet keeps its whole envelope clear of the star (a moon of an
         * outer planet must never reach the star, even when envelopes overlap). */
        double clear = e + SYS_STAR_RADIUS + SYS_MOON_MAX_RADIUS + SYS_GAP;
        if (k == 0) r = clear;
        else {
            r += (prev_e + e) * (1.0 - overlap) + SYS_GAP;
            if (r < clear) r = clear;
        }
        L->planet_r[k] = r;
        prev_e = e;
    }
    L->r_sys = (L->n_planets > 0) ? L->planet_r[L->n_planets - 1] + prev_e : 0.0;
}

/* Draws and fits a layout for `seed` in a quadrant of side `quadrant_size`.
 * Returns 1 (always, for any quadrant_size >= 20), 0 if even a bare star
 * with one moonless planet does not fit. */
static inline int sys_layout_make(unsigned seed, double quadrant_size, SysLayout *L) {
    double u = moon_h01(seed, 11u);
    L->n_planets = u < 0.30 ? 1 : u < 0.65 ? 2 : u < 0.90 ? 3 : 4;
    for (int k = 0; k < SYS_PLANETS_PER_STAR; k++) {
        double m = moon_h01(seed, 20u + (unsigned)k);
        L->n_moons[k] = m < 0.25 ? 0 : m < 0.55 ? 1 : m < 0.80 ? 2 : m < 0.92 ? 3 : 4;
        L->planet_r[k] = 0.0;
    }
    const double budget = quadrant_size * 0.5 - SYS_FACE_MARGIN;
    for (;;) {
        sys_layout_radii(L, 0.0);
        if (L->r_sys <= budget) return 1;
        sys_layout_radii(L, 0.5);
        if (L->r_sys <= budget) return 1;
        if (L->n_planets > 1)            { L->n_planets--; L->n_moons[L->n_planets] = 0; }
        else if (L->n_moons[0] > 0)      { L->n_moons[0]--; }
        else return 0;
    }
}

/* Star position inside the cube where the whole system fits (quadrant units). */
static inline void sys_star_position(unsigned seed, double quadrant_size, double r_sys, double out[3]) {
    double lo = r_sys + SYS_FACE_MARGIN, hi = quadrant_size - r_sys - SYS_FACE_MARGIN;
    for (int i = 0; i < 3; i++) {
        out[i] = (hi > lo) ? lo + (hi - lo) * moon_h01(seed, 30u + (unsigned)i) : quadrant_size * 0.5;
    }
}

/* --- orbits --------------------------------------------------------------- */
/* Orbit of planet k around its star (MoonOrbit holds r, omega, phase, u, v). */
static inline void sys_planet_orbit_make(unsigned seed, int k, double r, MoonOrbit *o) {
    const double TWO_PI = 6.283185307179586;
    double node = TWO_PI * moon_h01(seed, 39u);                     /* shared ecliptic */
    double incl = SYS_PLANET_MAX_INCL * moon_h01(seed, 40u + (unsigned)k);
    double period = SYS_PLANET_BASE_PERIOD_S * pow(r / SYS_PLANET_REF_R, 1.5);
    double cn = cos(node), sn = sin(node), ci = cos(incl), si = sin(incl);
    o->r = r;
    o->omega = TWO_PI / period;
    o->phase = TWO_PI * moon_h01(seed, 50u + (unsigned)k);
    o->u[0] = cn;       o->u[1] = sn;      o->u[2] = 0.0;
    o->v[0] = -sn * ci; o->v[1] = cn * ci; o->v[2] = si;
}

/* Planet position (quadrant units) at time t: star + orbit offset. */
static inline void sys_planet_pos(unsigned seed, int k, double r, const double star[3], double t, double out[3]) {
    MoonOrbit o; double off[3];
    sys_planet_orbit_make(seed, k, r, &o);
    moon_orbit_offset(&o, t, off);
    for (int i = 0; i < 3; i++) out[i] = star[i] + off[i];
}

/* Moon position: planet + orbit offset (moon_orbits.h model, ordinal j). */
static inline void sys_moon_pos(unsigned moon_id, int moon_type, int j, const double planet[3], double t, double out[3]) {
    MoonOrbit o; double off[3];
    moon_orbit_make(moon_id, moon_type, j, &o);
    moon_orbit_offset(&o, t, off);
    for (int i = 0; i < 3; i++) out[i] = planet[i] + off[i];
}

static inline double sys_clock_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

#endif /* STAR_SYSTEM_H */
