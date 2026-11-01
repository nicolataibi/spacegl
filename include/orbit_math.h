/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * orbit_math.h - the geometry of the `orb` command: a ship in a real
 * circular orbit around a celestial object.
 *
 * Before this header `orb` only set NAV_STATE_ORBIT; no tick handled that
 * state, so the ship stayed where it was. Now the ship is carried round the
 * object on a circle:
 *
 *   position = centre + r * (cos(theta) * e1 + sin(theta) * e2)
 *
 *   - e1, e2  orthonormal basis of the orbital plane, fixed at capture:
 *             e1 points from the object to the ship, e2 follows the ship's
 *             own motion (prograde) when it was moving, otherwise the
 *             horizontal tangent (z x e1);
 *   - r       constant: the distance at capture, or the safe minimum radius
 *             of the object when the ship was closer (it then spirals out at
 *             ORBIT_RADIAL_SLEW per tick, no jump);
 *   - theta   advances by orbit_omega(r) per tick (Kepler-like: the period
 *             grows with r^1.5, ORBIT_PERIOD_REF_S at ORBIT_RADIUS_REF).
 *
 * The centre is re-read every tick by the caller, so the ship follows an
 * object that moves (star-system planets and moons) without a drift.
 *
 * Header-only and free of the server types so tests/orbit_test.c can use it
 * on its own.  Tests: tests/orbit_test.c (geometry), tests/server_commands_test.c
 * (the command on a real galaxy).
 */
#ifndef ORBIT_MATH_H
#define ORBIT_MATH_H

#include <math.h>

#define ORBIT_PERIOD_REF_S 20.0   /* period of an orbit of radius ORBIT_RADIUS_REF, seconds */
#define ORBIT_RADIUS_REF   3.0    /* reference radius, sector units                         */
#define ORBIT_RADIAL_SLEW  0.02   /* radius change per tick while settling onto the orbit   */
#define ORBIT_MIN_VEL_TICK 1e-4   /* below this a ship counts as "not moving" at capture    */
#define ORBIT_TICK_RATE    60.0   /* ticks per second (GAME_TICK_RATE)                      */

typedef struct {
    int active;       /* 1 while the ship is in orbit                           */
    int target;       /* id of the object being orbited                          */
    double e1[3];        /* orbital plane, axis towards the ship at capture         */
    double e2[3];        /* orbital plane, axis in the direction of travel          */
    double theta;        /* current phase, radians                                  */
    double r;            /* current radius                                          */
    double r_goal;       /* radius the orbit settles on                             */
} OrbitState;

static inline double orb_dot(const double a[3], const double b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
static inline double orb_len(const double a[3]) { return sqrt(orb_dot(a, a)); }
static inline void orb_cross(const double a[3], const double b[3], double o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
/* o = a / |a|; returns |a| (o is left untouched when |a| is ~0) */
static inline double orb_unit(const double a[3], double o[3]) {
    double l = orb_len(a);
    if (l > 1e-12) {
        o[0] = a[0] / l;
        o[1] = a[1] / l;
        o[2] = a[2] / l;
    }
    return l;
}

/* Angular advance per tick for an orbit of radius r. */
static inline double orbit_omega(double r) {
    if (r < 0.05) r = 0.05;
    double period_s = ORBIT_PERIOD_REF_S * pow(r / ORBIT_RADIUS_REF, 1.5);
    return (2.0 * M_PI) / (period_s * ORBIT_TICK_RATE);
}

/* Period of an orbit of radius r, in seconds (for the helmsman's report). */
static inline double orbit_period_s(double r) {
    if (r < 0.05) r = 0.05;
    return ORBIT_PERIOD_REF_S * pow(r / ORBIT_RADIUS_REF, 1.5);
}

/* Captures a ship into orbit.
 *   rel   ship position minus the object's centre
 *   vel   the ship's movement of the last tick (galactic units / tick)
 *   r_min safe minimum radius of the object
 * The orbit passes through the ship's present point (theta = 0), so the
 * capture does not move the ship. */
static inline void orbit_start(OrbitState *o, int target, const double rel[3],
                               const double vel[3], double r_min) {
    double d = orb_len(rel);
    o->active = 1;
    o->target = target;
    o->theta = 0.0;

    if (d > 1e-9) {
        orb_unit(rel, o->e1);
    } else {                       /* exactly on the centre: any axis will do */
        o->e1[0] = 1.0;
        o->e1[1] = 0.0;
        o->e1[2] = 0.0;
    }

    /* e2: the part of the velocity that is perpendicular to e1 ... */
    double e2[3] = { 0.0, 0.0, 0.0 };
    double vr = orb_dot(vel, o->e1);
    double vt[3] = { vel[0] - vr * o->e1[0], vel[1] - vr * o->e1[1], vel[2] - vr * o->e1[2] };
    if (orb_len(vt) > ORBIT_MIN_VEL_TICK) {
        orb_unit(vt, e2);
    } else {
        /* ... or, for a ship at rest, the horizontal tangent z x e1 */
        static const double zax[3] = { 0.0, 0.0, 1.0 };
        static const double xax[3] = { 1.0, 0.0, 0.0 };
        double t[3];
        orb_cross(zax, o->e1, t);
        if (orb_len(t) < 1e-6) orb_cross(xax, o->e1, t);   /* e1 along z */
        orb_unit(t, e2);
    }
    o->e2[0] = e2[0];
    o->e2[1] = e2[1];
    o->e2[2] = e2[2];

    o->r = d > 1e-9 ? d : r_min;
    o->r_goal = d > r_min ? d : r_min;
}

/* One tick: settles the radius, advances the phase and returns the ship's
 * offset from the centre (`off`) and the unit direction of travel (`dir`). */
static inline void orbit_advance(OrbitState *o, double off[3], double dir[3]) {
    if (o->r < o->r_goal) {
        o->r += ORBIT_RADIAL_SLEW;
        if (o->r > o->r_goal) o->r = o->r_goal;
    } else if (o->r > o->r_goal) {
        o->r -= ORBIT_RADIAL_SLEW;
        if (o->r < o->r_goal) o->r = o->r_goal;
    }
    o->theta += orbit_omega(o->r);
    if (o->theta > 2.0 * M_PI) o->theta -= 2.0 * M_PI;   /* keeps the phase small and precise */

    double c = cos(o->theta), s = sin(o->theta);
    for (int k = 0; k < 3; k++) {
        off[k] = o->r * (c * o->e1[k] + s * o->e2[k]);
        dir[k] = -s * o->e1[k] + c * o->e2[k];
    }
}

#endif /* ORBIT_MATH_H */
