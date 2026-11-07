/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * vulkan_cpu_lod.h — level of detail of the sphere on the CPU-driven
 * Vulkan path (2026.11.07.01).
 *
 * Every sphere of that path (stars, planets, moons, the black hole, the
 * comet's nucleus and coma, shells, halos, cores...) was the one
 * SPHERE_LATS x SPHERE_LONGS mesh, 64 x 128 = 16 384 triangles, also when
 * it covered a few pixels: on an Arc A770 a busy quadrant (256 objects,
 * 1920x1080) spent 7.5 ms of GPU time on the objects, 0.03 on the rest.
 * The sphere index buffer now holds four levels, 64x128, 32x64, 16x32 and
 * 8x16 (include/sphere_lod.h), and vk_draw_sphere() draws the coarsest
 * one whose silhouette stays within half a pixel of the true sphere at
 * the size the sphere has on screen:
 *   - filled pipelines (lit, glow, alpha): the chord of one step deviates
 *     from the circle by r (1 - cos(pi / longs)); with a 25 % margin for
 *     off-axis stretching, 8x16 up to 21 px of radius, 16x32 up to 83,
 *     32x64 up to 333, the full mesh beyond;
 *   - the wireframe pipeline draws the triangle edges as lines, so fewer
 *     triangles means fewer lines: a level is used only while its lines
 *     stay at most one pixel apart (2 pi r / longs <= 1), where they still
 *     cover the disc as the dense full mesh does: 8x16 up to 2 px, 16x32
 *     up to 4, 32x64 up to 8 (2026.11.07.03; half a pixel before).
 * The size on screen comes from the model matrix last pushed (the
 * sphere's centre and largest axis) and the view and projection of the
 * previous frame (drawFrame computes them after recording): a frame of
 * lag on the level, never on the image. Near or behind the camera, and
 * on the first frame, the full mesh. SPACEGL_SPHERE_LOD=0 draws the full
 * mesh always (for comparison).
 *
 * To know the last pushed model and the bound pipeline without touching
 * the 183 push sites, this header wraps vkCmdPushConstants and
 * vkCmdBindPipeline for the files that include it (vulkan_render.c and
 * vulkan_scene.c with its .inc/.inl), and only there; since 2026.11.07.02
 * also vkCmdDrawIndexed and vkCmdDraw, to count the commands (CpuCmdStats).
 */
#ifndef VULKAN_CPU_LOD_H
#define VULKAN_CPU_LOD_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "sphere_lod.h"   /* the levels and the choice (plain C, tested) */

typedef struct {
    bool enabled;                                  /* SPACEGL_SPHERE_LOD != 0 */
    uint32_t first[SPHERE_LOD_LEVELS];             /* index range of each level in sphereIndexBuffer */
    uint32_t count[SPHERE_LOD_LEVELS];             /* level 0: SPHERE_LATS x SPHERE_LONGS, first 0 */
    float view[4][4];                              /* previous frame */
    float px_per_unit;                             /* |proj[1][1]| * height / 2 */
    bool view_valid;
    float model[4][4];                             /* last push at offset 0 */
    bool model_valid;
    VkPipeline pipe;                               /* last graphics pipeline bound */
    VkPipeline wire_pipe;                          /* app->wireframePipeline */
    uint64_t drawn[SPHERE_LOD_LEVELS];             /* spheres per level since the last reset */
} SphereLod;

extern SphereLod g_sphere_lod;

/* What the CPU-driven path records a frame, by kind of object
 * (2026.11.07.02): draws, indices, pipeline binds (and real changes of
 * pipeline), push constants. The object loop sets g_cpu_cmds.kind; the
 * wrappers below count. --bench-frames prints them ([CPU-CMDS]), and with
 * SPACEGL_VK_GPU_TIMES=1 the GPU time of each kind is measured too
 * (vulkan_render.c). */
enum { CPUK_SCENE,
       CPUK_SHIPS,
       CPUK_STARS,
       CPUK_PLANETS,
       CPUK_BASES,
       CPUK_ASTEROIDS,
       CPUK_COMETS,
       CPUK_NEBULAE,
       CPUK_OTHER,
       CPUK_EFFECTS,
       CPUK_N };
typedef struct {
    int kind;                 /* what is being recorded now */
    uint64_t objects[CPUK_N]; /* objects of the kind */
    uint64_t draws[CPUK_N], indices[CPUK_N], binds[CPUK_N], changes[CPUK_N], pushes[CPUK_N];
} CpuCmdStats;
extern CpuCmdStats g_cpu_cmds;
extern const char *const g_cpu_kind_names[CPUK_N];
int cpu_kind_of(int type);   /* CPUK_SHIPS ... CPUK_OTHER for an object type */

static inline void sg_lod_push_constants(VkCommandBuffer cb, VkPipelineLayout layout, VkShaderStageFlags stages,
                                         uint32_t offset, uint32_t size, const void *data) {
    if (offset == 0 && size >= sizeof(g_sphere_lod.model)) {
        memcpy(g_sphere_lod.model, data, sizeof(g_sphere_lod.model));
        g_sphere_lod.model_valid = true;
    }
    g_cpu_cmds.pushes[g_cpu_cmds.kind]++;
    vkCmdPushConstants(cb, layout, stages, offset, size, data);
}

static inline void sg_lod_bind_pipeline(VkCommandBuffer cb, VkPipelineBindPoint point, VkPipeline pipe) {
    if (point == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        g_cpu_cmds.binds[g_cpu_cmds.kind]++;
        if (pipe != g_sphere_lod.pipe) g_cpu_cmds.changes[g_cpu_cmds.kind]++;
        g_sphere_lod.pipe = pipe;
    }
    vkCmdBindPipeline(cb, point, pipe);
}

static inline void sg_lod_draw_indexed(VkCommandBuffer cb, uint32_t index_count, uint32_t instances, uint32_t first_index,
                                       int32_t vertex_offset, uint32_t first_instance) {
    g_cpu_cmds.draws[g_cpu_cmds.kind]++;
    g_cpu_cmds.indices[g_cpu_cmds.kind] += (uint64_t)index_count * instances;
    vkCmdDrawIndexed(cb, index_count, instances, first_index, vertex_offset, first_instance);
}

static inline void sg_lod_draw(VkCommandBuffer cb, uint32_t vertex_count, uint32_t instances, uint32_t first_vertex,
                               uint32_t first_instance) {
    g_cpu_cmds.draws[g_cpu_cmds.kind]++;
    g_cpu_cmds.indices[g_cpu_cmds.kind] += (uint64_t)vertex_count * instances;
    vkCmdDraw(cb, vertex_count, instances, first_vertex, first_instance);
}

#define vkCmdPushConstants sg_lod_push_constants
#define vkCmdBindPipeline  sg_lod_bind_pipeline
#define vkCmdDrawIndexed   sg_lod_draw_indexed
#define vkCmdDraw          sg_lod_draw

/* The level for the sphere the last pushed model places (0 = full mesh). */
int sphere_lod_level(void);
/* Start of a frame's recording: forget the previous frame's model. */
void sphere_lod_begin_frame(VkPipeline wire_pipe);
/* After drawFrame has computed them: the view and projection of the frame. */
void sphere_lod_set_view(const float view[4][4], float proj11, uint32_t height);

/* Draws the bound sphere mesh (sphereVertexBuffer / sphereIndexBuffer)
 * at its level of detail. */
static inline void vk_draw_sphere(VkCommandBuffer cb) {
    int l = sphere_lod_level();
    g_sphere_lod.drawn[l]++;
    vkCmdDrawIndexed(cb, g_sphere_lod.count[l], 1, g_sphere_lod.first[l], 0, 0);
}

#endif
