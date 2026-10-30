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

#endif /* QUADRANT_GEOM_H */
