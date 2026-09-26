/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * SPACE GL - GPU-DRIVEN RENDERING (GDD) - CPU<->GPU CONTRACT
 *
 * This header is the single source of truth for every structure that
 * crosses the CPU/GPU boundary of the GPU-driven path (SPACEGL_GPD=1).
 * Every layout documented here MUST stay in sync with the matching
 * GLSL struct in assets/shaders/gdd_common.glsl.
 *
 * ARCHITECTURE (mirrors the DRAGONGL GPD design, adapted to SpaceGL):
 *
 *   CPU (per frame, ONE small upload):
 *     - read the SharedIPC state (same IPC as the CPU-driven path) and
 *       run the same cubic-Hermite interpolation (network smoothing is
 *       data handling, it is NOT rendering);
 *     - classify every object/effect into a compact instance descriptor
 *       (GddInstance, 112 B: position, mesh id, scale, flags, color,
 *       3x3 orientation) and memcpy the list into a persistently mapped
 *       host-visible SSBO (one write, no per-object Vulkan calls);
 *
 *   GPU (compute, same queue, in-order; synchronization2 barriers):
 *     1. gdd_cull.comp   : instances -> compact visible index lists
 *                          (vision-radius sphere cull; static geometry
 *                          carries GDD_FLAG_NEVER_CULL);
 *     2. gdd_expand.comp : visible instances -> device-local vertex SSBO
 *                          (atomic counter + capacity guard, "drop when
 *                          full" like the legacy CPU path);
 *     3. gdd_final.comp  : counters -> two VkDrawIndirectCommand
 *                          (opaque pass, additive pass);
 *
 *   GPU (graphics, dynamic rendering):
 *     4. vkCmdBeginRendering (color + depth) and a single
 *        vkCmdDrawIndirect per pass; the scene vertex shader reads the
 *        GPU-generated vertices from storage (NO fixed-function vertex
 *        input), the fragment shader re-implements the 10 procedural
 *        color modes of the CPU path (shader.frag).
 *
 * Selection: the SPACEGL_GPD environment variable (0 or unset ->
 * legacy CPU-driven path, 1 -> GPU-driven). If the device cannot
 * support the GPU-driven path (no graphics+compute queue, feature
 * request failure, pipeline creation failure) the client falls back to
 * the CPU-driven path automatically and prints a banner.
 *
 * MATRIX CONVENTION (identical to the CPU-driven path, see the header
 * note in src/spacegl_vulkan.c): the engine builds Row-Major matrices
 * with the translation in the bottom row (m[3][0..2]). That byte layout
 * is exactly what GLSL expects for a column-major mat4, so the GDD MVP
 * is computed in C as  mvp = view * proj  (C row-major product) and
 * pushed verbatim: the shader applies it as  gl_Position = mvp * v.
 * Instance orientation (GddInstance.orient) is a GLSL mat3 stored
 * column-major with each column padded to 4 floats:
 *     orient[4*c + r] = R[r][c]   (c = column, r = row)
 */

#ifndef SPACEGL_GDD_H
#define SPACEGL_GDD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

/* ================================================================== */
/* Mesh ids (GddInstance.a.w). Primitives generated on the GPU by     */
/* gdd_expand.comp from a single unit description each.               */
/* ================================================================== */
#define GDD_MESH_POINT   0  /* 6 verts   : double-sided triangle (stars, FX pixels, markers) */
#define GDD_MESH_SPHERE  1  /* 360 verts : UV sphere 6x10 (stars, planets, BH, mines, FX) */
#define GDD_MESH_BOX     2  /* 36 verts  : unit box (structures, map sectors, beams via anisotropic scale) */
#define GDD_MESH_PYRAMID 3  /* 18 verts  : ship / torpedo nose (+X is the nose) */
#define GDD_MESH_OCTA    4  /* 24 verts  : starbase core, buoys */
#define GDD_MESH_RING    5  /* 96 verts  : flat 16-segment annulus in local XZ (gates, orbital rings) */
#define GDD_MESH_LINE    6  /* 24 verts  : square tube (4 faces x 6) along scale.xyz (grid, axes, quadrant cube) */
#define GDD_MESH_CIRCLE  7  /* 432 verts : thin 72-seg wireframe circle in local XZ (AR compass rings) */
#define GDD_MESH_ARC     8  /* 216 verts : thin 36-seg 180-deg wireframe arc in local XY (AR compass mark) */
/* Procedural explosion cloud (CPU boom parity: 256 expanding particles).
 * it.a.xyz = boom center (world, already tactScale-mapped); it.b.x =
 * tactScale; it.c.w (alpha) = life; it.p.x = per-boom seed (0..1000);
 * it.p.y = style (0 = torpedo boom: cubic offsets, 6-color cycle;
 * 1 = dismantle boom: spherical offsets, 3-color mix). */
#define GDD_MESH_BOOM    9  /* 1536 verts : 256 particles x 6 verts (double-sided triangle) */
/* Wireframe box: the 12 edges of the unit box as square tubes (same
 * tube construction as GDD_MESH_LINE), ONE instance per wireframe box
 * (the GDD counterpart of the CPU path's LINE_LIST cube, e.g. the
 * galaxy map frame/sectors). it.b.xyz = half-extents (GDD_MESH_BOX
 * convention); it.p.x (metallic) = tube half-thickness in world units
 * (0 -> 0.02), clamped to the same 1-px screen-space floor. */
#define GDD_MESH_BOXWIRE 10  /* 288 verts : 12 edges x 4 faces x 6 verts */

#define GDD_VCOUNT_POINT   6u
#define GDD_VCOUNT_SPHERE  360u
#define GDD_VCOUNT_BOX     36u
#define GDD_VCOUNT_PYRAMID 18u
#define GDD_VCOUNT_OCTA    24u
#define GDD_VCOUNT_RING    96u
#define GDD_VCOUNT_LINE    24u
#define GDD_VCOUNT_CIRCLE  432u
#define GDD_VCOUNT_ARC     216u
#define GDD_VCOUNT_BOOM    1536u
#define GDD_VCOUNT_BOXWIRE 288u

/* Fragment render modes (mirror the CPU path "usePushColor" ids,
 * assets/shaders/shader.frag — keep the numbers aligned). */
#define GDD_FRAG_UNLIT      1  /* constant instance color (wireframe/HUD look) */
#define GDD_FRAG_TWINKLE    2  /* starfield: vertex color * sin(time+pos) */
#define GDD_FRAG_VCOLOR     4  /* unlit vertex color (compass axes) */
#define GDD_FRAG_PBR        5  /* diffuse + specular + ambient (metallic/roughness) */
#define GDD_FRAG_HYPERWARP  6  /* pulsing chromatic glow (stars, quasars, Dyson...) */
#define GDD_FRAG_SHOCKWAVE  7  /* expanding wave pulse (beam impacts, dismantles) */
#define GDD_FRAG_ACCRETION  8  /* white->yellow->red radial gradient (black hole disks) */
#define GDD_FRAG_NEBULA     9  /* FBM volumetric cloud (nebulae, dark matter) */
#define GDD_FRAG_FILAMENT  10  /* electrical discharge plasma (filaments, storms) */
/* Barycentric wireframe (procedural line rendering on solid triangles):
 * the pyramid / box / octa expanders store per-vertex barycentric coords
 * in GddVertex.params (x,y,z) when the instance mode is one of these,
 * and the scene fragment shader draws the triangle edges only (per-sample
 * discard under MSAA = antialiased lines, the GDD counterpart of the CPU
 * path's fixed-function LINE_LIST wireframe pipeline). */
#define GDD_FRAG_WIREFRAME  11 /* wireframe, unlit instance color (CPU mode 1 on the wireframe pipeline) */
#define GDD_FRAG_WIREFRAME_PBR 12 /* wireframe, PBR metal 0.9 / rough 0.25 (CPU ships: wireframe + mode 5) */

/* Instance flags (GddInstance.b.w). The field is a float that must hold
 * exact small integer values (any integer < 2^24 is exact in float32);
 * the GLSL side decodes the bits with power-of-two divisions. */
#define GDD_FLAG_NEVER_CULL 1u  /* static geometry: skip vision culling */
#define GDD_FLAG_ADDITIVE   2u  /* route to the additive (glow) draw pass */
#define GDD_FLAG_MODE_BASE  4u  /* bits 2..5: fragment mode (0..15) */

static inline float gdd_make_flags(uint32_t never_cull, uint32_t additive, uint32_t frag_mode) {
    return (float)((never_cull & 1u) * 1u + (additive & 1u) * 2u + (frag_mode & 0xFu) * 4u);
}

/* Flag decoding (mirror of gdd_common.glsl). gdd_make_flags() only
 * produces exact small integers, so a single rounding recovers them. */
static inline uint32_t gdd_flags_to_uint(float flags) { return (uint32_t)(flags + 0.5f); }
static inline uint32_t gdd_flag_bit(float flags, uint32_t bit) { return (gdd_flags_to_uint(flags) >> bit) & 1u; }
static inline uint32_t gdd_frag_mode(float flags) { return (gdd_flags_to_uint(flags) >> 2) & 0xFu; }

/* ================================================================== */
/* GddInstance — 112 B == GLSL GddInstance (a, b, c, mat3, vec4)      */
/* ================================================================== */
typedef struct {
    float pos[3];    /* world position (tactical mapping already applied) */
    float mesh;      /* GDD_MESH_* id */
    float scale[3];  /* box/pyramid/octa: half-extents; sphere: radius (x);
                        ring/circle/arc: radius (x); line: full direction*length vector */
    float flags;     /* GDD_FLAG_* (exact integer bits) */
    float color[3];  /* rgb */
    float alpha;     /* 0..1 */
    float orient[12];/* GLSL mat3: column-major, columns padded to 4 floats */
    float pad[4];    /* [0]=metallic, [1]=roughness (PBR), rest unused */
} GddInstance;

#define GDD_INSTANCE_STRIDE 112

/* ================================================================== */
/* GddVertex — 80 B == GLSL GddVertex (5 x vec4). Generated by the    */
/* GPU (gdd_expand.comp); the CPU never writes it, it only reads it   */
/* back in the tests.                                                  */
/* ================================================================== */
typedef struct {
    float pos[3];    float pad0;
    float color[4];
    float normal[3]; float mode;     /* GDD_FRAG_* */
    float local[3];  float pad1;     /* local (pre-transform) position */
    /* params: (metallic, roughness, 0, 0) for the PBR / shockwave modes;
     * (bary_x, bary_y, bary_z, 0) for the GDD_FRAG_WIREFRAME(_PBR) modes
     * (per-vertex barycentric coords used by the fragment wireframe mask). */
    float metallic;  float roughness; float pad2; float pad3;
} GddVertex;

#define GDD_VERTEX_STRIDE 80

/* Per-frame GPU counters (16 B; zeroed with vkCmdFillBuffer at the
 * start of every frame, accumulated by the compute passes). */
typedef struct {
    uint32_t dyn_vis;        /* visible dynamic instances  (cull -> expand) */
    uint32_t map_vis;        /* visible map instances      (cull -> expand) */
    uint32_t opaque_verts;   /* total vertices of the opaque pass */
    uint32_t additive_verts; /* total vertices of the additive pass */
} GddCounts;

#define GDD_COUNTS_STRIDE 16

/* Compute push constants (36 B == GLSL GddPC). */
typedef struct {
    float cull_center[3]; /* vision sphere center (tactical: quadrant center) */
    float cull_radius;    /* vision sphere radius (cameraDist + margin) */
    uint32_t list_count;  /* valid instances in this dispatch */
    uint32_t group;       /* 0 = dynamic group, 1 = map/static group */
    uint32_t capacity;    /* total vertex capacity (expand pass guard) */
    uint32_t pad;         /* expand pass selector: 0 = opaque, 1 = additive */
    float line_min_wu;    /* GDD_MESH_LINE only: minimum half-thickness in
                            world units (screen-space floor). The CPU pushes
                            0.5 * GDD_LINE_MIN_PX * worldUnitsPerPixel for
                            the current camera; gdd_expand_line clamps the
                            tube to max(requested, line_min_wu) so lines
                            never rasterize sub-pixel (the GDD pipeline has
                            no 1-px LINE_LIST primitive). 0 = disabled
                            (unit tests). */
} GddPC;

#define GDD_PC_STRIDE 36

/* Scene push constants (84 B == GLSL ScenePC), shared by the opaque
 * and the additive pipeline. mvp is the C row-major product
 * view*proj (see the file header note) and is consumed verbatim. */
typedef struct {
    float mvp[16];
    float time;
    float cam[3]; /* camera position (world), for fragment use */
    float pad;
} GddScenePC;

#define GDD_SCENE_PC_STRIDE 84

/* ================================================================== */
/* Caps (keep in sync with the VulkanApp arrays — Phase 4 unifies the */
/* #defines so the two architectures share the same limits).          */
/* ================================================================== */
#define GDD_MAX_FRAMES          3
#define GDD_DYN_MAX             4096u  /* objects + torps + effects + compass */
/* Map/static group: starfield + grid (tactical) or the galaxy map
 * (frame + highlight + one instance per non-zero sector; a 40^3 galaxy
 * has 64,000 cells, so the budget covers a fully populated galaxy even
 * mid tactical<->map transition, where both static sets coexist: ~3,300
 * tactical + 64,013 map). */
#define GDD_MAP_MAX             72000u
/* Total generated vertices per frame (both passes). In map mode every
 * wireframe sector costs GDD_VCOUNT_BOXWIRE (288) vertices: the budget
 * covers ~14,500 wireframe sectors (a fully populated 64k-sector galaxy
 * in filter mode, where each sector is one 36-vertex PBR box, always
 * fits); beyond the budget the expand pass drops instances ("drop when
 * full", same policy as the legacy CPU path). */
#define GDD_VERTEX_CAPACITY     4194304u
#define GDD_WORKGROUP           256u
#define GDD_STAR_COUNT          2000u /* == MAX_STARS of the CPU path */

/* Persistent starfield entry (tactical background, built once at init
 * with the same distribution as the CPU path createStarfield()). */
typedef struct {
    float pos[3];
    float color[3];
    float scale;
} GddStar;

/* ================================================================== */
/* CPU-side per-object interpolated state and effect state.           */
/* These definitions are the CANONICAL ones: spacegl_vulkan.c adopts  */
/* them (Phase 4) so the CPU-driven and the GPU-driven paths share    */
/* exactly the same state layout.                                     */
/* ================================================================== */
#define GDD_MAX_NET_OBJECTS     1024u
#define GDD_MAX_ACTIVE_BEAMS    64u
#define GDD_MAX_ACTIVE_BOOMS    128u
#define GDD_MAX_ACTIVE_TORPS    256u
#define GDD_MAX_ACTIVE_DISMANTLES 64u
#define GDD_EXPLOSION_PIXELS    256u
#define GDD_MAX_ARRIVAL_PARTICLES 500u

typedef struct {
    float x, y, z, h, m, r;
    float prev_x, prev_y, prev_z, prev_h, prev_m, prev_r;
    float target_x, target_y, target_z, target_h, target_m, target_r;
    float vx, vy, vz;
    float prev_vx, prev_vy, prev_vz;
    int id;
    bool first;
} SmoothObj;

typedef struct { float sx, sy, sz, tx, ty, tz, life; int owner_id; int extra; int emitter_id; } ActiveBeam;
typedef struct { float x, y, z, life; float offsets[GDD_EXPLOSION_PIXELS][3]; float colors[GDD_EXPLOSION_PIXELS][3];
    /* GPU-driven path: the 256-particle cloud is generated on the GPU
     * from these (per-boom random seed + style) instead of the
     * CPU-side offsets/colors tables (which the CPU-driven path keeps).
     * style 0 = torpedo boom (cubic offsets, 6-color cycle),
     * style 1 = dismantle boom (spherical offsets, 3-color mix). */
    float seed; int style; } ActiveBoom;
typedef struct { float x, y, z, life; float scale; } ActiveDismantle;
typedef struct { float x, y, z; float dx, dy, dz; int active; int id; } ActiveTorp;
typedef struct { float x, y, z; float angle; float radius; float speed; int active; } ArrivalParticle;
typedef struct { float x, y, z; double h, m; int active; int timer; int jump_type; } JumpState;
typedef struct { float x, y, z; double h, m; int active; int jump_type; } WormholeState;

/* ================================================================== */
/* Instance builder (pure C, NO Vulkan): the CPU-side half of the     */
/* GPU-driven architecture. Unit-testable without a device.           */
/* ================================================================== */
typedef struct {
    /* --- object classification (extracted from the current shm frame) --- */
    const int *types;         /* object_count entries (SharedObject.type) */
    const int *factions;      /* object_count entries */
    const int *ship_classes;  /* object_count entries */
    const int *cloaked;       /* object_count entries */
    const int *active;        /* object_count entries (SharedObject.active) */
    const int *platings;      /* object_count entries (asteroid size) */
    const int *ids;           /* object_count entries (SharedObject.id) */
    int object_count;

    /* --- interpolated state (cubic Hermite, shared with the CPU path) --- */
    const SmoothObj *objs;    /* GDD_MAX_NET_OBJECTS entries */

    /* --- client-side effect state (event queue) --- */
    const ActiveBeam *beams;
    const ActiveBoom *booms;
    const ActiveDismantle *dismantles;
    const ActiveTorp *torps;
    const JumpState *jump_arrival;
    const WormholeState *wormhole;
    const int *shield_timers; /* 6 entries (shield hit flash) */

    /* --- view / camera state --- */
    float map_anim;    /* 0 = tactical view, 1 = galaxy map */
    int map_filter;    /* galaxy map filter (0..17) */
    float pulse;       /* seconds (glfwGetTime) */
    float camera_dist; /* orbital camera distance (vision radius) */
    int player_q[3];   /* player quadrant (map mode) */
    int show_axes;     /* shm_show_axes: draw the AR compass */
    int show_grid;     /* shm_show_grid: draw the quadrant grid */

    /* --- static geometry inputs --- */
    const GddStar *stars; int star_count;   /* persistent starfield */
    /* Pointer to shm_galaxy[gs+1][gs+1][gs+1] (C layout, stride gs+1);
     * cells are addressed with real coordinates x,y,z in 1..gs.
     * galaxy_size = 0 (or NULL) => no galaxy map. */
    const int64_t *galaxy; int galaxy_size;

    /* --- outputs (capacity-guarded; *count receives the written total) --- */
    GddInstance *dyn_out;  uint32_t dyn_cap;  uint32_t *dyn_count;
    GddInstance *map_out;  uint32_t map_cap;  uint32_t *map_count;
} GddBuildCtx;

void gdd_build_instances(const GddBuildCtx *ctx);

/* ================================================================== */
/* Device-side API (defined in src/spacegl_vulkan_gdd.c).             */
/* VulkanApp is opaque here; the full definition lives in            */
/* include/spacegl_vulkan_types.h.                                   */
/* ================================================================== */
typedef struct VulkanApp VulkanApp;
typedef struct GddState GddState;

/* TRUE if a queue family with BOTH graphics and compute is available
 * (required by the GPU-driven path; the family is returned). */
bool gdd_pick_queue(VkPhysicalDevice pd, uint32_t *family_out);

/* Create the GDD state: compute pipelines (cull/expand/final), scene
 * pipelines (dynamic rendering: opaque + additive), per-frame buffer
 * ring and descriptor sets. Returns FALSE (and leaves app->gdd NULL)
 * if the device cannot support the GPU-driven path — the caller then
 * falls back to the CPU-driven path. */
bool gdd_init(VulkanApp *app);

/* Build the instance lists of the current frame slot from the shm
 * state (classification + effects + statics + map mode). */
void gdd_build_frame(VulkanApp *app, float pulse);

/* Record the GDD command buffer for frame slot app->gdd_frame_idx:
 * counter fill -> cull -> expand -> final -> barrier2 -> dynamic
 * rendering with one DrawIndirect per pass. */
void gdd_record(VkCommandBuffer cb, VulkanApp *app, uint32_t image_idx);

/* Full GDD frame: fence wait, image acquire, instance upload, record,
 * submit, present (same triple-buffer invariant as the CPU path). */
void gdd_draw_frame(VulkanApp *app);

void gdd_cleanup(VulkanApp *app);

/* ================================================================== */
/* Layout invariants (fail at compile time if the CPU/GPU contract    */
/* drifts out of sync).                                               */
/* ================================================================== */
_Static_assert(sizeof(GddInstance) == GDD_INSTANCE_STRIDE, "GddInstance must be 112 B (GLSL a,b,c,mat3,vec4)");
_Static_assert(sizeof(GddVertex) == GDD_VERTEX_STRIDE, "GddVertex must be 80 B (GLSL 5 x vec4)");
_Static_assert(sizeof(GddCounts) == GDD_COUNTS_STRIDE, "GddCounts must be 16 B");
_Static_assert(sizeof(GddPC) == GDD_PC_STRIDE, "GddPC must be 32 B");
_Static_assert(sizeof(GddScenePC) == GDD_SCENE_PC_STRIDE, "GddScenePC must be 84 B");
_Static_assert(offsetof(GddVertex, local) - offsetof(GddVertex, normal) == 16, "GddVertex: mode sits at normal.w (vec4)");
_Static_assert(offsetof(GddInstance, orient) == 48, "GddInstance: mat3 must start at offset 48");

#endif /* SPACEGL_GDD_H */
