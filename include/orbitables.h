/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * orbitables.h - the objects the `orb` command can put a ship in orbit around.
 *
 * One row per ID range. The safe radius (the orbit never goes closer) is, in
 * order of precedence:
 *   - r_safe > 0 : fixed in the row (objects with no obstacle kind: their
 *                  size comes from how the client draws them);
 *   - otherwise  : the keep-out distance (r_avoid) of the row's obstacle kind
 *                  in obstacles.h, the one the NPC steering already respects.
 * The capture reach is the safe radius + DIST_ORBIT_CAPTURE_BAND.
 *
 * Adding an object to `orb` is one more row here: its position, the motion of
 * a moving body and the "target lost" case all come from the target registry
 * (targets.c), and tests/server_commands_test.c walks this table, so the new
 * row is tested without touching the test.
 *
 * Deliberately NOT here: comets (followed with `cha`), satellites and
 * starbases (approached with `apr` / `doc`), asteroids (too small and many).
 */
#ifndef ORBITABLES_H
#define ORBITABLES_H

#include "game_config.h"
#include "obstacles.h"

typedef struct {
    int min_id, max_id;
    const char *label;
    int obs_kind;     /* OBS_* of obstacles.h (radius source when r_safe is 0) */
    double r_safe;       /* fixed safe radius, or 0 = from obs_kind               */
    int moving;       /* 1 = the body moves (star systems): the centre is re-read every tick */
} OrbitableDef;

static const OrbitableDef ORBITABLES[] = {
    /* legacy objects, fixed in their sector */
    { GALAXY_OBJECT_MIN_PLANET, GALAXY_OBJECT_MAX_PLANET, "planet", OBS_PLANET, 0.0, 0 },
    { GALAXY_OBJECT_MIN_STAR, GALAXY_OBJECT_MAX_STAR, "star", OBS_STAR, 0.0, 0 },
    { GALAXY_OBJECT_MIN_BLACKHOLE, GALAXY_OBJECT_MAX_BLACKHOLE, "black hole", OBS_BLACK_HOLE, 0.0, 0 },
    { GALAXY_OBJECT_MIN_PULSAR, GALAXY_OBJECT_MAX_PULSAR, "pulsar", OBS_PULSAR, 0.0, 0 },
    { GALAXY_OBJECT_MIN_QUASAR, GALAXY_OBJECT_MAX_QUASAR, "quasar", OBS_NONE, 0.0, 0 },
    { GALAXY_OBJECT_MIN_NEUTRON_STAR, GALAXY_OBJECT_MAX_NEUTRON_STAR, "neutron star", OBS_NEUTRON_STAR, 2.5, 0 },  /* compact, intense field: wider than a planet */
    { GALAXY_OBJECT_MIN_BROWN_DWARF, GALAXY_OBJECT_MAX_BROWN_DWARF, "brown dwarf", OBS_BROWN_DWARF, 0.0, 0 },
    { GALAXY_OBJECT_MIN_ROGUE_PLANET, GALAXY_OBJECT_MAX_ROGUE_PLANET, "rogue planet", OBS_ROGUE_PLANET, 0.0, 0 },
    { GALAXY_OBJECT_MIN_ORBITAL_RING, GALAXY_OBJECT_MAX_ORBITAL_RING, "orbital ring", OBS_NONE, 4.0, 0 },  /* the ring is a wide annulus: clear of it */
    { GALAXY_OBJECT_MIN_MEGA_STRUCT, GALAXY_OBJECT_MAX_MEGA_STRUCT, "mega structure", OBS_NONE, 5.5, 0 },  /* spire 8 tall + 4-unit arms in the client */
    { GALAXY_OBJECT_MIN_SINGULARITY, GALAXY_OBJECT_MAX_SINGULARITY, "singularity", OBS_BLACK_HOLE, 0.0, 0 },  /* micro black hole: the gravity-well keep-out */
    /* star systems (star_system.h): bodies that move along their own orbits */
    { GALAXY_OBJECT_MIN_SYS_STAR, GALAXY_OBJECT_MAX_SYS_STAR, "system star", OBS_SYS_STAR, 0.0, 1 },
    { GALAXY_OBJECT_MIN_SYS_PLANET, GALAXY_OBJECT_MAX_SYS_PLANET, "system planet", OBS_SYS_PLANET, 0.0, 1 },
    { GALAXY_OBJECT_MIN_SYS_MOON, GALAXY_OBJECT_MAX_SYS_MOON, "system moon", OBS_SYS_MOON, 0.0, 1 },
};
#define ORBITABLES_COUNT ((int)(sizeof(ORBITABLES) / sizeof(ORBITABLES[0])))

static inline const OrbitableDef *orbitable_for(int tid) {
    for (int k = 0; k < ORBITABLES_COUNT; k++) {
        if (tid >= ORBITABLES[k].min_id && tid <= ORBITABLES[k].max_id) return &ORBITABLES[k];
    }
    return NULL;
}

/* Safe orbit radius of an object (never closer than this). */
static inline double orbitable_safe_radius(const OrbitableDef *d) {
    if (d->r_safe > 0.0) return d->r_safe;
    double r_hit, r_avoid;
    obs_kind_radii(d->obs_kind, &r_hit, &r_avoid);
    return r_avoid;
}

/* Reach of the orbital capture. */
static inline double orbitable_capture_reach(const OrbitableDef *d) {
    return orbitable_safe_radius(d) + (double)DIST_ORBIT_CAPTURE_BAND;
}

#endif /* ORBITABLES_H */
