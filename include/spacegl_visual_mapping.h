#ifndef SPACEGL_VISUAL_MAPPING_H
#define SPACEGL_VISUAL_MAPPING_H

#include "server_internal.h"
#include "spacegl_gdd.h"

/* Single Source of Truth for visual mapping */
typedef struct {
    int mesh_id;
    float color[4];
    int frag_mode;
    float scale;
} VisualMapping;

static inline VisualMapping get_visual_mapping(int object_type) {
    VisualMapping m = { GDD_MESH_SPHERE, {1.0f, 1.0f, 1.0f, 1.0f}, GDD_FRAG_PBR, 1.0f };
    // Example mapping
    if (object_type == 1) { /* Ship */
        m.mesh_id = GDD_MESH_OCTA;
        m.color[0] = 0.5f; m.color[1] = 0.5f; m.color[2] = 1.0f;
    }
    return m;
}

#endif
