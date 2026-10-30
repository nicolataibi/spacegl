/*
 * quadrant_geom.h - geometry of the quadrant cube shared by the three viewers
 * (OpenGL, Vulkan CPU-driven, Vulkan GPU-driven / GDD).
 *
 * The cube is QUADRANT_SIZE units on a side and is drawn centred on the
 * origin: a point of the quadrant (0 .. QG_SIZE) is drawn at (v - QG_HALF).
 * The objects keep their size when the cube grows; what follows the cube is
 * the camera (distance, zoom step, clip planes, star shell), all of which were
 * tuned for a cube of QG_REF_SIZE units and are multiplied by QG_SCALE so that
 * the whole cube keeps the same framing on screen.
 */
#ifndef QUADRANT_GEOM_H
#define QUADRANT_GEOM_H

#include <math.h>
#include "game_config.h"

#define QG_SIZE      ((float)QUADRANT_SIZE)     /* side of the cube                            */
#define QG_HALF      (QG_SIZE * 0.5f)           /* half side: the origin offset of the cube    */
#define QG_REF_SIZE  40.0f                      /* cube the camera constants were tuned for    */
#define QG_SCALE     (QG_SIZE / QG_REF_SIZE)    /* camera scale factor (1.0 at 40, 2.5 at 100) */

#define QG_CAMERA_DEFAULT  (80.0f * QG_SCALE)   /* initial orbital camera distance (Vulkan)    */
#define QG_CAMERA_STEP     (5.0f * QG_SCALE)    /* PageUp / PageDown zoom step (Vulkan)        */
#define QG_GRID_HIDE_DIST  (150.0f * QG_SCALE)  /* grid and compass are hidden beyond this     */
#define QG_FAR_PLANE       (1000.0f * QG_SCALE) /* far clip plane of the Vulkan projections    */
#define QG_GRID_CELLS      20                   /* grid cells per axis (cell = QG_SIZE / 20)   */

/* Camera distance actually used by the Vulkan paths. The tactical view uses the
 * user's distance as is (it already scales with the cube, see QG_CAMERA_DEFAULT).
 * The galaxy map is drawn at a fixed size (GALAXY_SIZE cells, not quadrant
 * units), so while the map blends in, the camera moves back to the distance the
 * same zoom had for the reference cube. map_anim: 0 = tactical, 1 = map. */
static inline float qg_view_dist(float camera_dist, float map_anim) {
    return camera_dist * (1.0f - map_anim) + (camera_dist / QG_SCALE) * map_anim;
}

/* Torpedo look shared by the Vulkan paths, after drawTorpedo() of the OpenGL viewer: an orange core (radius
 * QG_TORP_CORE), a red halo of 5 additive shells around (QG_TORP_HALO + pulse), and 12 star rays (4 in each
 * of the XY, YZ and XZ planes) turning at 250 degrees per GL pulse unit, red at the centre and yellow fading
 * out at the tip. The OpenGL pulse runs at 3 units per second (0.05 per 60 Hz frame). */
#define QG_TORP_CORE        0.15f
#define QG_TORP_HALO        0.20f   /* OpenGL 0.12: larger here, the OpenGL viewer adds a bloom pass on top */
#define QG_TORP_CORE_R      1.0f    /* core colour: brighter than the OpenGL (1, 0.5, 0) for the same reason */
#define QG_TORP_CORE_G      0.7f
#define QG_TORP_CORE_B      0.25f
#define QG_TORP_RAY_LEN     0.8f
#define QG_GL_PULSE_PER_SEC 3.0f
static inline float qg_torp_wave(float seconds) {          /* 0..1, as (sin(pulse * 30) + 1) / 2 in OpenGL */
    return (sinf(seconds * QG_GL_PULSE_PER_SEC * 30.0f) + 1.0f) * 0.5f;
}
/* Direction of ray j (0..3) of plane p (0: XY, 1: YZ, 2: XZ) at time t, length included. */
static inline void qg_torp_ray(int p, int j, float seconds, float out[3]) {
    float a = (float)j * 1.5707963f + seconds * QG_GL_PULSE_PER_SEC * 250.0f * 0.017453293f;
    float len = QG_TORP_RAY_LEN + qg_torp_wave(seconds) * 0.3f;
    float c = cosf(a) * len, sn = sinf(a) * len;
    if (p == 0)      { out[0] = c;    out[1] = sn;   out[2] = 0.0f; }
    else if (p == 1) { out[0] = 0.0f; out[1] = sn;   out[2] = -c;   }   /* RotY(90) of the XY ray */
    else             { out[0] = c;    out[1] = 0.0f; out[2] = sn;   }   /* RotX(90) of the XY ray */
}

#endif /* QUADRANT_GEOM_H */
