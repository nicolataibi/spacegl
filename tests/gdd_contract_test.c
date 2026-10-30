/*
 * SPACE GL - GPU-DRIVEN RENDERING (GDD) - CPU contract test.
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
 *
 * CPU-side verification of the GDD contract (no Vulkan device needed):
 *
 *   1. Layout invariants: the CPU structs must keep the exact sizes,
 *      per-field offsets and vec4 16-byte alignment the GLSL side
 *      assumes (spacegl_gdd.h carries the compile-time _Static_asserts;
 *      this re-checks the full set at runtime so a mis-compiled TU
 *      cannot sneak through).
 *   2. Flag round-trip: gdd_make_flags / gdd_flags_to_uint /
 *      gdd_frag_mode for every (never_cull, additive, frag_mode).
 *   3. The pure-C instance builder (gdd_build_instances) against a
 *      synthetic scene: object classification, (X, Z, -Y) centered
 *      coordinate mapping, per-type companions, effects (boom/beam/
 *      torp), the AR compass, jump-arrival hiding, the tactical
 *      statics (starfield + grid), the galaxy map mode and the
 *      capacity guard ("drop when full").
 *
 * Exit: 0 = pass, 1 = fail.
 */

#include "object_types.h"
#include "quadrant_geom.h"
#include "spacegl_gdd.h"
#include "shared_state.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef GDD_TEST_STUB_GLFW
/* glfwGetTime is referenced by the device-side frame path of the
 * production TU; the contract tests never call it. */
double glfwGetTime(void) { return 0.0; }
#endif

/* recreateSwapChain lives in src/spacegl_vulkan.c (the app's resize
 * machinery) and is referenced by the device-side frame path of the
 * production TU; the contract tests never call it. */
void recreateSwapChain(VulkanApp *app) { (void)app; }

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
           fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } \
} while (0)

#define CHECK_F(c, a, b, tol, what) do { \
    if (fabs((a) - (b)) <= (tol)) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL %s:%d: %s: got %.9g want %.9g\n", \
            __FILE__, __LINE__, what, (double)(a), (double)(b)); } \
} while (0)

static int near_f(float a, float b, float tol) { return fabsf(a - b) <= tol; }

/* Identity in the GDD 12-float (3x4 padded) orientation layout: 1s at
 * 0/5/10, the three padding slots (3/7/11) and the off-diagonal zeros. */
static int orient_is_identity(const float o[12]) {
    for (int i = 0; i < 12; i++) {
        float want = (i == 0 || i == 5 || i == 10) ? 1.0f : 0.0f;
        if (fabsf(o[i] - want) > 1e-6f) return 0;
    }
    return 1;
}

/* ================================================================== */
/* 1. Layout invariants                                                */
/* ================================================================== */
static void test_layout(void) {
    CHECK(sizeof(GddInstance) == GDD_INSTANCE_STRIDE && GDD_INSTANCE_STRIDE == 112,
          "GddInstance 112B");
    CHECK(sizeof(GddVertex) == GDD_VERTEX_STRIDE && GDD_VERTEX_STRIDE == 80,
          "GddVertex 80B");
    CHECK(sizeof(GddCounts) == GDD_COUNTS_STRIDE && GDD_COUNTS_STRIDE == 16,
          "GddCounts 16B");
    CHECK(sizeof(GddPC) == GDD_PC_STRIDE && GDD_PC_STRIDE == 36, "GddPC 36B");
    CHECK(sizeof(GddScenePC) == GDD_SCENE_PC_STRIDE && GDD_SCENE_PC_STRIDE == 84,
          "GddScenePC 84B");
    /* GddInstance (GLSL: vec4 a; vec4 b; vec4 c; mat3 o; vec4 p;) —
     * every field at the offset the GLSL side reads it. */
    CHECK(offsetof(GddInstance, pos) == 0, "inst.pos@0 (a.xyz)");
    CHECK(offsetof(GddInstance, mesh) == 12, "inst.mesh@12 (a.w)");
    CHECK(offsetof(GddInstance, scale) == 16, "inst.scale@16 (b.xyz)");
    CHECK(offsetof(GddInstance, flags) == 28, "inst.flags@28 (b.w)");
    CHECK(offsetof(GddInstance, color) == 32, "inst.color@32 (c.xyz)");
    CHECK(offsetof(GddInstance, alpha) == 44, "inst.alpha@44 (c.w)");
    CHECK(offsetof(GddInstance, orient) == 48, "inst.orient@48 (mat3)");
    CHECK(offsetof(GddInstance, pad) == 96, "inst.pad@96 (vec4 p)");
    /* GddVertex (GLSL: 5 x vec4) — the full per-field layout, with the
     * vec4 starts on 16-byte boundaries (GLSL alignment rule): a silent
     * padding/offset drift here is the CPU/GPU contract failure this
     * test exists to catch. */
    CHECK(offsetof(GddVertex, pos) == 0, "vtx.pos@0 (vec4)");
    CHECK(offsetof(GddVertex, color) == 16, "vtx.color@16 (vec4)");
    CHECK(offsetof(GddVertex, normal) == 32, "vtx.normal@32 (vec4)");
    CHECK(offsetof(GddVertex, mode) == 44, "vtx.mode@44 (normal.w)");
    CHECK(offsetof(GddVertex, local) == 48, "vtx.local@48 (vec4)");
    CHECK(offsetof(GddVertex, metallic) == 64, "vtx.metallic@64 (params.x)");
    CHECK(offsetof(GddVertex, roughness) == 68, "vtx.roughness@68 (params.y)");
    CHECK(offsetof(GddVertex, pos) % 16 == 0, "vtx.pos 16B-aligned (vec4)");
    CHECK(offsetof(GddVertex, color) % 16 == 0, "vtx.color 16B-aligned (vec4)");
    CHECK(offsetof(GddVertex, normal) % 16 == 0, "vtx.normal 16B-aligned (vec4)");
    CHECK(offsetof(GddVertex, local) % 16 == 0, "vtx.local 16B-aligned (vec4)");
    CHECK(offsetof(GddVertex, metallic) % 16 == 0, "vtx.metallic 16B-aligned (params)");
    /* GddPC (GLSL: vec4 cull; uint list_count; uint group; uint
     * capacity; uint pad; float line_min_wu;). */
    CHECK(offsetof(GddPC, cull_center) == 0, "pc.cull_center@0 (cull.xyz)");
    CHECK(offsetof(GddPC, cull_radius) == 12, "pc.cull_radius@12 (cull.w)");
    CHECK(offsetof(GddPC, list_count) == 16, "pc.list_count@16 (uint after vec4)");
    CHECK(offsetof(GddPC, group) == 20, "pc.group@20");
    CHECK(offsetof(GddPC, capacity) == 24, "pc.capacity@24");
    CHECK(offsetof(GddPC, pad) == 28, "pc.pad@28");
    CHECK(offsetof(GddPC, line_min_wu) == 32, "pc.line_min_wu@32");
    /* GddCounts (GLSL: four uints in declaration order). */
    CHECK(offsetof(GddCounts, dyn_vis) == 0, "cnt.dyn_vis@0");
    CHECK(offsetof(GddCounts, map_vis) == 4, "cnt.map_vis@4");
    CHECK(offsetof(GddCounts, opaque_verts) == 8, "cnt.opaque_verts@8");
    CHECK(offsetof(GddCounts, additive_verts) == 12, "cnt.additive_verts@12");
    /* GddScenePC (GLSL: mat4 mvp; float time; vec3 cam; float pad;). */
    CHECK(offsetof(GddScenePC, mvp) == 0, "scpc.mvp@0 (mat4)");
    CHECK(offsetof(GddScenePC, time) == 64, "scpc.time@64");
    CHECK(offsetof(GddScenePC, cam) == 68, "scpc.cam@68");
    CHECK(offsetof(GddScenePC, pad) == 80, "scpc.pad@80");
}

/* ================================================================== */
/* 2. Flag round-trip                                                  */
/* ================================================================== */
static void test_flags(void) {
    for (uint32_t nc = 0; nc <= 1; nc++)
        for (uint32_t ad = 0; ad <= 1; ad++)
            for (uint32_t mode = 0; mode <= 10; mode++) {
                float f = gdd_make_flags(nc, ad, mode);
                uint32_t u = gdd_flags_to_uint(f);
                CHECK(u == ((nc & 1u) | ((ad & 1u) << 1) | ((mode & 0xFu) << 2)),
                      "flag round-trip nc=%u ad=%u mode=%u (f=%.1f u=%u)",
                      nc, ad, mode, (double)f, u);
                CHECK(gdd_flag_bit(f, 0) == nc, "bit0 nc=%u", nc);
                CHECK(gdd_flag_bit(f, 1) == ad, "bit1 ad=%u", ad);
                CHECK(gdd_frag_mode(f) == mode, "mode=%u", mode);
            }
}

/* ================================================================== */
/* 3. Instance builder                                                 */
/* ================================================================== */
#define T_MAX 8192

static GddInstance g_dyn[T_MAX];
static GddInstance g_map[T_MAX];
static uint32_t g_dyn_n, g_map_n;

/* One SmoothObj in the quadrant center, fully initialized (first=0). */
/* The test data is written in the 40-unit frame (centre 20, obj (10,15,25) maps to (-10,5,5)). The builder
 * centres on QG_HALF, so every quadrant position is translated by Q_SHIFT: the expected mapped values do not
 * depend on QUADRANT_SIZE. */
#define Q_SHIFT (QG_HALF - 20.0f)

/* Stars, planets and moons use GDD_MESH_SPHERE_HI when big enough on screen, the other spheres keep the
 * 360-vertex mesh: "a sphere" in the generic checks is either one. */
static int is_sphere_mesh(int m) { return m == GDD_MESH_SPHERE || m == GDD_MESH_SPHERE_HI; }

static void obj_at(SmoothObj *o, float x, float y, float z, float h, float m, float r) {
    x += Q_SHIFT; y += Q_SHIFT; z += Q_SHIFT;
    memset(o, 0, sizeof(*o));
    o->x = o->prev_x = o->target_x = x;
    o->y = o->prev_y = o->target_y = y;
    o->z = o->prev_z = o->target_z = z;
    o->h = o->prev_h = o->target_h = h;
    o->m = o->prev_m = o->target_m = m;
    o->r = o->prev_r = o->target_r = r;
    o->first = false;
}

static void run_builder(const GddBuildCtx *ctx) {
    g_dyn_n = g_map_n = 0;
    GddBuildCtx c = *ctx;
    c.dyn_out = g_dyn; c.dyn_cap = T_MAX; c.dyn_count = &g_dyn_n;
    c.map_out = g_map; c.map_cap = T_MAX; c.map_count = &g_map_n;
    gdd_build_instances(&c);
}

/* Find the first dyn instance with the given mesh at (px,py,pz). */
static const GddInstance *find_dyn(int mesh, float px, float py, float pz, float tol) {
    for (uint32_t i = 0; i < g_dyn_n; i++) {
        const GddInstance *it = &g_dyn[i];
        if ((int)(it->mesh + 0.5f) == mesh &&
            near_f(it->pos[0], px, tol) && near_f(it->pos[1], py, tol) &&
            near_f(it->pos[2], pz, tol))
            return it;
    }
    return NULL;
}

static int count_dyn(int mesh) {
    int n = 0;
    for (uint32_t i = 0; i < g_dyn_n; i++)
        if ((int)(g_dyn[i].mesh + 0.5f) == mesh) n++;
    return n;
}

static SmoothObj g_smooth[GDD_MAX_NET_OBJECTS];

/* Base context: tactical view, no shm effects unless set by the test. */
static GddBuildCtx base_ctx(void) {
    GddBuildCtx c;
    memset(&c, 0, sizeof(c));
    c.objs = g_smooth; /* filled by the tests */
    c.map_anim = 0.0f;
    c.pulse = 1.0f;
    c.camera_dist = QG_CAMERA_DEFAULT;
    return c;
}

/* ------------------------------------------------------------------ */
static void test_objects_mapping(void) {
    GddBuildCtx c = base_ctx();

    obj_at(&g_smooth[0], 20.0f, 20.0f, 20.0f, 0.0f, 0.0f, 0.0f); /* center */
    obj_at(&g_smooth[1], 10.0f, 15.0f, 25.0f, 0.0f, 0.0f, 0.0f);
    obj_at(&g_smooth[2], 30.0f, 10.0f, 5.0f, 0.0f, 0.0f, 0.0f);

    int types[3] = { 1, 4, 5 };            /* ship, star, planet   */
    int fac[3] = { 0, 0, 0 };
    int sclass[3] = { 0, 2, 0 };
    int cloak[3] = { 0, 0, 0 };
    int act[3] = { 1, 1, 1 };
    int plat[3] = { 30, 30, 30 };
    int ids[3] = { 100, 101, 102 };
    c.types = types; c.factions = fac; c.ship_classes = sclass;
    c.cloaked = cloak; c.active = act; c.platings = plat; c.ids = ids;
    c.object_count = 3;

    run_builder(&c);
    /* ship is Alliance (fac 0): +4 quantum instances (core + 3 rings) */
    CHECK(g_dyn_n == 23, "3 objects + quantum(4) + 16 bbox edges -> %u dyn instances", g_dyn_n);

    /* (X, Z, -Y) centered mapping: obj1 (10,15,25) -> (-10, 5, 5) */
    const GddInstance *star = find_dyn(GDD_MESH_SPHERE_HI, -10.0f, 5.0f, 5.0f, 1e-4f);   /* big sphere: HI mesh */
    CHECK(star != NULL, "star at mapped position (-10,5,5)");
    if (star) {
        CHECK_F("star scale", star->scale[0], 2.5f, 1e-5f, "star scale");
        CHECK_F("star color.r", star->color[0], 1.0f, 1e-5f, "star color.r");
        CHECK_F("star color.b", star->color[2], 1.0f, 1e-5f, "star color.b (class 2 = A, white)");
        CHECK(star->alpha == 1.0f, "star alpha");
        CHECK(gdd_flag_bit(star->flags, 1) == 1, "star is additive");
        CHECK(gdd_frag_mode(star->flags) == GDD_FRAG_HYPERWARP, "star frag mode");
        CHECK(gdd_flag_bit(star->flags, 0) == 0, "star is cullable");
        /* non-ship, non-spin: identity orientation */
        CHECK(star->orient[0] == 1.0f && star->orient[5] == 1.0f &&
              star->orient[10] == 1.0f && star->orient[1] == 0.0f,
              "star identity orientation");
    }

    /* Player ship: pyramid centered on the object in the CPU path, so
     * the GDD instance is shifted along the nose axis (identity heading:
     * RotY(90deg) -> nose +Z) by 0.7288 * GDD_SHIP_SCALE (CPU stern
     * alignment: the mesh spans local [-0.7288, +2.1866]). */
    const float ship_stern_off = 0.7288f * 0.45f * 0.55f;
    const GddInstance *ship = find_dyn(GDD_MESH_PYRAMID, 0.0f, 0.0f, ship_stern_off, 1e-4f);
    CHECK(ship != NULL, "ship (pyramid) at stern-aligned position");
    if (ship) {
        /* GDD_SHIP_SCALE = SCALE_SHIP * 0.55 = 0.45 * 0.55 */
        CHECK_F("ship sx", ship->scale[0], 0.45f * 0.55f * 2.9154f, 1e-4f, "ship sx");
        CHECK_F("ship sy", ship->scale[1], 0.45f * 0.55f, 1e-4f, "ship sy");
        CHECK_F("ship color.g", ship->color[1], 1.0f, 1e-5f, "ship color.g");
        /* CPU: wireframe pipeline + PBR (metal 0.9 / rough 0.25) */
        CHECK(gdd_frag_mode(ship->flags) == GDD_FRAG_WIREFRAME_PBR, "ship wireframe PBR");
        CHECK_F("ship metallic", ship->pad[0], 0.9f, 1e-5f, "ship metallic");
        CHECK_F("ship roughness", ship->pad[1], 0.25f, 1e-5f, "ship roughness");
        CHECK(gdd_flag_bit(ship->flags, 1) == 0, "ship opaque");
        /* Orientation = RotY(90deg): column 0 of the GLSL mat3 = nose (+Z) */
        CHECK(near_f(ship->orient[2], 1.0f, 1e-5f) && near_f(ship->orient[0], 0.0f, 1e-5f) &&
              near_f(ship->orient[5], 1.0f, 1e-5f) && near_f(ship->orient[8], -1.0f, 1e-5f),
              "ship RotY(90deg) orientation (nose +Z)");
    }

    /* Planet (30,10,5) -> (10, -15, 10) */
    const GddInstance *planet = find_dyn(GDD_MESH_SPHERE_HI, 10.0f, -15.0f, 10.0f, 1e-4f);   /* big sphere: HI mesh */
    CHECK(planet != NULL, "planet at mapped position (10,-15,10)");
    if (planet) {
        CHECK_F("planet scale", planet->scale[0], 1.8f, 1e-5f, "planet scale");
        CHECK_F("planet color.r", planet->color[0], 0.2f, 1e-5f, "planet color.r (class 0 = Terrestrial)");
        CHECK_F("planet color.b", planet->color[2], 1.0f, 1e-5f, "planet color.b (class 0 = Terrestrial)");
    }
    CHECK(obj_planet_class_rgb(-1) == obj_planet_class_rgb(PLANET_CLASS_TERRESTRIAL) &&
          obj_planet_class_rgb(99) == obj_planet_class_rgb(PLANET_CLASS_TERRESTRIAL),
          "planet class out of range falls back to Terrestrial");
    CHECK(obj_planet_class_rgb(PLANET_CLASS_MOLTEN)[0] == 0.8f &&
          obj_planet_class_rgb(PLANET_CLASS_ICE)[2] == 1.0f &&
          obj_planet_class_rgb(PLANET_CLASS_DESERT)[1] == 0.7f,
          "planet class palette (Molten red, Ice pale blue, Desert sand)");

    /* Quantum core: octahedron wireframe at -0.46*GDD_SHIP_SCALE along
     * the nose axis (here +Z), radius 0.20*GDD_SHIP_SCALE */
    const float q_off = -0.46f * 0.45f * 0.55f;
    const GddInstance *qcore = find_dyn(GDD_MESH_OCTA, 0.0f, 0.0f, q_off, 1e-4f);
    CHECK(qcore != NULL, "quantum core octahedron at the stern");
    if (qcore) {
        CHECK_F("quantum core radius", qcore->scale[0], 0.20f * 0.45f * 0.55f, 1e-4f,
                "quantum core radius");
        CHECK(gdd_frag_mode(qcore->flags) == GDD_FRAG_WIREFRAME, "quantum core wireframe");
        CHECK(gdd_flag_bit(qcore->flags, 1) == 0, "quantum core opaque");
    }
    /* Three orbiting rings (wireframe circles, cyan) */
    CHECK(count_dyn(GDD_MESH_CIRCLE) == 3, "quantum: 3 orbiting rings = %d", count_dyn(GDD_MESH_CIRCLE));
    const GddInstance *qr = find_dyn(GDD_MESH_CIRCLE, 0.0f, 0.0f, q_off, 1e-4f);
    CHECK(qr != NULL, "quantum ring at the stern");
    if (qr) {
        CHECK_F("quantum ring b", qr->color[2], 1.0f, 1e-5f, "quantum ring b");
        CHECK_F("quantum ring g", qr->color[1], 0.95f, 1e-5f, "quantum ring g");
    }

    /* Inactive objects must not be emitted */
    int act2[3] = { 1, 0, 1 };
    c.active = act2;
    run_builder(&c);
    CHECK(g_dyn_n == 22, "inactive object dropped + quantum(4) + 16 bbox = %u dyn", g_dyn_n);
}

static void test_effects(void) {
    GddBuildCtx c = base_ctx();
    obj_at(&g_smooth[0], 20.0f, 20.0f, 20.0f, 0.0f, 0.0f, 0.0f);
    int types[1] = { 1 }, fac[1] = { 0 }, sc[1] = { 0 }, cl[1] = { 0 },
        act[1] = { 1 }, pl[1] = { 30 }, id[1] = { 100 };
    c.types = types; c.factions = fac; c.ship_classes = sc; c.cloaked = cl;
    c.active = act; c.platings = pl; c.ids = id; c.object_count = 1;

    /* One explosion at (5,5,5), life 0.9: a single GDD_MESH_BOOM
     * instance; the 256-particle cloud is expanded on the GPU from
     * seed/style (CPU parity: 256 unlit spheres, offsets*exp). */
    static ActiveBoom boom[GDD_MAX_ACTIVE_BOOMS];
    memset(boom, 0, sizeof(boom));
    boom[0].x = boom[0].y = boom[0].z = 5.0f;
    boom[0].life = 0.9f;
    boom[0].seed = 42.0f;
    boom[0].style = 0;
    for (int p = 0; p < (int)GDD_EXPLOSION_PIXELS; p++) {
        boom[0].offsets[p][0] = 0.1f * p;
        boom[0].offsets[p][1] = 0.0f;
        boom[0].offsets[p][2] = 0.0f;
        boom[0].colors[p][0] = 1.0f; boom[0].colors[p][1] = 1.0f; boom[0].colors[p][2] = 0.0f;
    }
    c.booms = boom;

        /* Two phaser beams raw (20,20,20) -> (30,20,20), i.e. centered
     * (0,0,0) -> (10,0,0), life 1.0: the emitter-1 beam carries the
     * signature impact boom, the emitter-2 beam (the "pha" bottom
     * emitter) draws its wireframe line but NO second boom. owner_id/
     * extra = 0 disables the real-time tracking in the builder, so the
     * beams stay where the test puts them. */
    static ActiveBeam beam[GDD_MAX_ACTIVE_BEAMS];
    memset(beam, 0, sizeof(beam));
    beam[0].sx = 20.0f + Q_SHIFT; beam[0].sy = 20.0f + Q_SHIFT; beam[0].sz = 20.0f + Q_SHIFT;
    beam[0].tx = 30.0f + Q_SHIFT; beam[0].ty = 20.0f + Q_SHIFT; beam[0].tz = 20.0f + Q_SHIFT;
    beam[0].life = 1.0f;
    beam[0].owner_id = 0; beam[0].extra = 0; beam[0].emitter_id = 1;
    beam[1].sx = 20.0f + Q_SHIFT; beam[1].sy = 20.0f + Q_SHIFT; beam[1].sz = 20.0f + Q_SHIFT;
    beam[1].tx = 30.0f + Q_SHIFT; beam[1].ty = 20.0f + Q_SHIFT; beam[1].tz = 20.0f + Q_SHIFT;
    beam[1].life = 1.0f;
    beam[1].owner_id = 0; beam[1].extra = 0; beam[1].emitter_id = 2;
    c.beams = beam;

    /* One active torpedo */
    static ActiveTorp torp[GDD_MAX_ACTIVE_TORPS];
    memset(torp, 0, sizeof(torp));
    torp[0].x = 1.0f; torp[0].y = 2.0f; torp[0].z = 3.0f;
    torp[0].active = 500; torp[0].id = 7;
    c.torps = torp;

    run_builder(&c);

    /* ship (1+4 quantum) + boom (1 cloud) + beams (2 lines + 1 impact
     * boom) + torp (1) + 16 bbox */
    CHECK(g_dyn_n == 25 + GDD_TORP_INSTANCES, "effects: ship+quantum+boom+beams+torp+16bbox -> %u dyn (want %u)", g_dyn_n, 25 + GDD_TORP_INSTANCES);

    /* Booms: the torpedo cloud at (5,5,5) plus the phaser impact boom
     * at the target end (10,0,0) — the GPU expands each 256-particle
     * cloud from seed/style (pad.x/pad.y). The emitter-2 beam must
     * NOT add a second impact boom (one per shot). */
    CHECK(count_dyn(GDD_MESH_BOOM) == 2, "2 boom cloud instances, got %d",
          count_dyn(GDD_MESH_BOOM));
    const GddInstance *boomit = find_dyn(GDD_MESH_BOOM, 5.0f, 5.0f, 5.0f, 1e-4f);
    CHECK(boomit != NULL, "boom cloud instance at (5,5,5)");
    if (boomit) {
        CHECK_F("boom ts scale", boomit->scale[0], 1.0f, 1e-5f, "boom ts scale");
        CHECK_F("boom alpha=life", boomit->alpha, 0.9f, 1e-6f, "boom alpha");
        CHECK_F("boom seed", boomit->pad[0], 42.0f, 1e-6f, "boom seed");
        CHECK_F("boom style", boomit->pad[1], 0.0f, 1e-6f, "boom style");
        CHECK(gdd_flag_bit(boomit->flags, 0) == 1, "boom never-cull (CPU draws regardless of vision)");
        CHECK(gdd_flag_bit(boomit->flags, 1) == 0, "boom opaque");
        CHECK(gdd_frag_mode(boomit->flags) == GDD_FRAG_UNLIT, "boom unlit");
    }
    /* Phaser impact boom: style 2 at (10,0,0), white, additive, UNLIT,
     * alpha = beam life, deterministic per-beam seed. */
    const GddInstance *pb = find_dyn(GDD_MESH_BOOM, 10.0f, 0.0f, 0.0f, 1e-4f);
    CHECK(pb != NULL, "phaser impact boom at (10,0,0)");
    if (pb) {
        CHECK_F("phaser boom style", pb->pad[1], 2.0f, 1e-6f, "phaser boom style");
        CHECK_F("phaser boom seed", pb->pad[0], gdd_beam_boom_seed(0, 0, 1), 1e-6f,
                "phaser boom seed");
        CHECK_F("phaser boom alpha", pb->alpha, 1.0f, 1e-6f, "phaser boom alpha");
        CHECK_F("phaser boom ts", pb->scale[0], 1.0f, 1e-6f, "phaser boom scale");
        CHECK(pb->color[0] == 1.0f && pb->color[1] == 1.0f && pb->color[2] == 1.0f,
              "phaser boom white");
        CHECK(gdd_flag_bit(pb->flags, 1) == 1, "phaser boom additive");
        CHECK(gdd_frag_mode(pb->flags) == GDD_FRAG_UNLIT, "phaser boom unlit");
    }
    /* Phasers: one white wireframe segment (GDD_MESH_LINE tube) per
     * beam, ship end (0,0,0), scale = direction vector (+X, length 10),
     * additive UNLIT, alpha = life, tube half-thickness 0.05 * ts. */
    CHECK(count_dyn(GDD_MESH_LINE) == 18 + 24,
          "beams: 2 lines + 16 bbox + 24 torpedo ray segments = %d", count_dyn(GDD_MESH_LINE));
    const GddInstance *bl = find_dyn(GDD_MESH_LINE, 0.0f, 0.0f, 0.0f, 1e-4f);
    CHECK(bl != NULL, "beam line at the ship end (0,0,0)");
    if (bl) {
        CHECK_F("beam length", bl->scale[0], 10.0f, 1e-4f, "beam length");
        CHECK(bl->scale[1] == 0.0f && bl->scale[2] == 0.0f, "beam along +X");
        CHECK(bl->color[0] == 1.0f && bl->color[1] == 1.0f && bl->color[2] == 1.0f,
              "beam white");
        CHECK(bl->alpha == 1.0f, "beam alpha = life");
        CHECK_F("beam tube thickness", bl->pad[0], 0.05f, 1e-6f, "beam tube");
        CHECK(gdd_flag_bit(bl->flags, 1) == 1, "beam additive");
        CHECK(gdd_frag_mode(bl->flags) == GDD_FRAG_UNLIT, "beam unlit");
    }

    /* Torpedo (OpenGL look): core sphere + 5 additive halo shells + 24 ray segments at (1,2,3) */
    {
        int core = 0, halo = 0, rays = 0, rays_out = 0;
        for (uint32_t i = 0; i < g_dyn_n; i++) {
            const GddInstance *it = &g_dyn[i];
            int m = (int)(it->mesh + 0.5f);
            int at = near_f(it->pos[0], 1.0f, 1e-4f) && near_f(it->pos[1], 2.0f, 1e-4f) && near_f(it->pos[2], 3.0f, 1e-4f);
            if (m == GDD_MESH_SPHERE && at && near_f(it->scale[0], QG_TORP_CORE, 1e-5f) && !gdd_flag_bit(it->flags, 1)) core++;
            else if (m == GDD_MESH_SPHERE && at && gdd_flag_bit(it->flags, 1)) halo++;
            else if (m == GDD_MESH_LINE && near_f(it->color[0], 1.0f, 1e-5f) && (near_f(it->color[1], 0.4f, 1e-5f) || near_f(it->color[1], 0.75f, 1e-5f))) {
                rays++;
                float len = 2.0f * sqrtf(it->scale[0] * it->scale[0] + it->scale[1] * it->scale[1] + it->scale[2] * it->scale[2]);
                if (len < QG_TORP_RAY_LEN - 1e-4f || len > QG_TORP_RAY_LEN + 0.3f + 1e-4f) rays_out++;
            }
        }
        CHECK(core == 1 && halo == 5, "torpedo core + 5 halo shells at (1,2,3) (%d, %d)", core, halo);
        CHECK(rays == 24 && rays_out == 0, "torpedo: 12 rays x 2 segments of the OpenGL length (%d, %d out)", rays, rays_out);
    }

    /* Dismantle with life < 0.7: only the flash is skipped (CPU parity),
     * core + aura are still drawn -> 26 + 2 */
    static ActiveDismantle dm[GDD_MAX_ACTIVE_DISMANTLES];
    memset(dm, 0, sizeof(dm));
    dm[0].x = 9.0f; dm[0].life = 0.5f;
    c.dismantles = dm;
    run_builder(&c);
    CHECK(g_dyn_n == 27 + GDD_TORP_INSTANCES, "dead dismantle: flash skipped, core+aura drawn (%u)", g_dyn_n);
    dm[0].life = 0.9f;
    run_builder(&c);
    CHECK(g_dyn_n == 28 + GDD_TORP_INSTANCES, "live dismantle adds 3 spheres (%u)", g_dyn_n);
}

static void test_compass_and_jump(void) {
    GddBuildCtx c = base_ctx();
    obj_at(&g_smooth[0], 20.0f, 20.0f, 20.0f, 0.0f, 0.0f, 0.0f);
    int types[1] = { 1 }, fac[1] = { 0 }, sc[1] = { 0 }, cl[1] = { 0 },
        act[1] = { 1 }, pl[1] = { 30 }, id[1] = { 100 };
    c.types = types; c.factions = fac; c.ship_classes = sc; c.cloaked = cl;
    c.active = act; c.platings = pl; c.ids = id; c.object_count = 1;

    /* No compass: ship + quantum(4) + 16 bbox lines */
    run_builder(&c);
    CHECK(g_dyn_n == 21, "no compass: ship+quantum+16bbox = %u dyn", g_dyn_n);

    /* Compass on: +25 instances (3 axis lines, 3 circles, 1 mark arc,
     * 2 arrows x 9 lines) + 16 bbox — CPU compass parity */
    c.show_axes = 1;
    run_builder(&c);
    CHECK(g_dyn_n == 46, "compass: ship + quantum + 25 compass + 16 bbox = %u dyn", g_dyn_n);
    CHECK(count_dyn(GDD_MESH_LINE) == 37,
          "compass: 3 axes + 18 arrow lines + 16 bbox = %d", count_dyn(GDD_MESH_LINE));
    CHECK(count_dyn(GDD_MESH_CIRCLE) == 6,
          "circles: 3 compass (fixed/heading/roll) + 3 quantum rings = %d",
          count_dyn(GDD_MESH_CIRCLE));
    CHECK(count_dyn(GDD_MESH_ARC) == 1, "compass: 1 vertical mark arc = %d",
          count_dyn(GDD_MESH_ARC));
    CHECK(count_dyn(GDD_MESH_RING) == 0,
          "compass: no solid rings left = %d", count_dyn(GDD_MESH_RING));

    /* Mark arc: yellow, r = 2.8 (unrotated: scale.x is the radius) */
    const GddInstance *arc = find_dyn(GDD_MESH_ARC, 0.0f, 0.0f, 0.0f, 1e-4f);
    CHECK(arc != NULL, "mark arc present at the anchor");
    if (arc) {
        CHECK_F("mark arc radius", arc->scale[0], 2.8f, 1e-4f, "mark arc radius");
        CHECK_F("mark arc yellow r", arc->color[0], 1.0f, 1e-5f, "mark arc yellow r");
        CHECK_F("mark arc yellow g", arc->color[1], 1.0f, 1e-5f, "mark arc yellow g");
        CHECK_F("mark arc yellow b", arc->color[2], 0.0f, 1e-5f, "mark arc yellow b");
        CHECK(gdd_frag_mode(arc->flags) == GDD_FRAG_UNLIT, "mark arc unlit");
    }

    /* Circles: exactly one each of r = 3.0 (white), 2.5 (cyan),
     * 2.8 (yellow roll) */
    int r30 = 0, r25 = 0, r28 = 0;
    for (uint32_t i = 0; i < g_dyn_n; i++) {
        if ((int)(g_dyn[i].mesh + 0.5f) != GDD_MESH_CIRCLE) continue;
        if (near_f(g_dyn[i].scale[0], 3.0f, 1e-3f)) r30++;
        else if (near_f(g_dyn[i].scale[0], 2.5f, 1e-3f)) r25++;
        else if (near_f(g_dyn[i].scale[0], 2.8f, 1e-3f)) r28++;
    }
    CHECK(r30 == 1 && r25 == 1 && r28 == 1,
          "circle radii 3.0/2.5/2.8 one each (got %d/%d/%d)", r30, r25, r28);

    /* B1 regression: an enemy ship with a non-trivial Euler orientation
     * is the LAST object processed, so the shared `o` carries the
     * enemy's rotation into the compass section. The 3 axis lines and
     * the fixed white ring must stay in the identity (world) orientation. */
    {
        obj_at(&g_smooth[1], 10.0f, 30.0f, 25.0f, 137.0f, 42.0f, -63.0f);
        int t2[2] = { 1, 10 }, f2[2] = { 0, 5 }, sc2[2] = { 0, 3 },
            cl2[2] = { 0, 0 }, ac2[2] = { 1, 1 }, pl2[2] = { 30, 20 },
            id2[2] = { 100, 200 };
        c.types = t2; c.factions = f2; c.ship_classes = sc2; c.cloaked = cl2;
        c.active = ac2; c.platings = pl2; c.ids = id2; c.object_count = 2;
        run_builder(&c);
        CHECK(g_dyn_n == 47, "B1: ship+quantum(4)+enemy+25 compass+16 bbox = %u dyn", g_dyn_n);

        /* Sanity: the enemy pyramid is present and rotated */
        const GddInstance *enemy = NULL;
        for (uint32_t i = 0; i < g_dyn_n; i++) {
            const GddInstance *it = &g_dyn[i];
            if ((int)(it->mesh + 0.5f) != GDD_MESH_PYRAMID) continue;
            if (near_f(it->pos[0], 0.0f, 1e-3f) && near_f(it->pos[1], 0.0f, 1e-3f) &&
                near_f(it->pos[2], 0.0f, 0.2f)) continue; /* player at the origin */
            enemy = it; break;
        }
        CHECK(enemy != NULL, "B1: enemy ship pyramid present in the quadrant");
        if (enemy)
            CHECK(!orient_is_identity(enemy->orient),
                  "B1: enemy carries a non-identity orientation (sanity)");

        /* Fixed white ring (r = 3.0 at the anchor): identity orientation */
        const GddInstance *fring = find_dyn(GDD_MESH_CIRCLE, 0.0f, 0.0f, 0.0f, 1e-4f);
        CHECK(fring != NULL, "B1: fixed white ring present at the anchor");
        if (fring) {
            CHECK_F("B1: fixed ring radius", fring->scale[0], 3.0f, 1e-4f, "B1: fixed ring radius");
            CHECK(orient_is_identity(fring->orient),
                  "B1: fixed white ring stays world-aligned despite the enemy ship");
        }

        /* The 3 compass axis lines: identity orientation */
        const GddInstance *ax[3] = {
            find_dyn(GDD_MESH_LINE, -5.5f, 0.0f, 0.0f, 1e-4f),
            find_dyn(GDD_MESH_LINE, 0.0f, -5.5f, 0.0f, 1e-4f),
            find_dyn(GDD_MESH_LINE, 0.0f, 0.0f, -5.5f, 1e-4f)
        };
        CHECK(ax[0] && ax[1] && ax[2], "B1: the 3 compass axis lines are present");
        for (int k = 0; k < 3 && ax[k]; k++)
            CHECK(orient_is_identity(ax[k]->orient),
                  "B1: compass axis %d stays world-aligned", k);

        /* Restore the single-object scene for the checks below */
        c.types = types; c.factions = fac; c.ship_classes = sc; c.cloaked = cl;
        c.active = act; c.platings = pl; c.ids = id; c.object_count = 1;
    }

    /* Shield hit (sector 2 = top): 6 faithful instances (CPU
     * drawShieldEffect parity): PBR energy panel + shockwave surface
     * glow + 4 volumetric shells, oriented with the ship. Ship at the
     * origin with identity heading: R_ship = RotY(90), Rz(-90) maps the
     * sector +X axis to world +Y. */
    static int shields[6] = { 0, 0, 30, 0, 0, 0 };
    c.shield_timers = shields;
    run_builder(&c);
    CHECK(g_dyn_n == 52, "shield glow: ship+quantum+compass+6 panel + 16 bbox = %u", g_dyn_n);
    {
        float t = 30.0f / 80.0f;
        float alpha = (t > 0.5f) ? 1.0f : t * 2.0f;
        float scale = (1.2f + (1.0f - t) * 0.3f); /* ts = 1 (map_anim 0) */
        float off = 1.45f * scale;
        int n_sec = 0;
        const GddInstance *pbr = NULL, *pulse = NULL, *glow1 = NULL;
        for (uint32_t i = 0; i < g_dyn_n; i++) {
            const GddInstance *it = &g_dyn[i];
            if (!is_sphere_mesh((int)(it->mesh + 0.5f))) continue;
            if (!(near_f(it->pos[0], 0.0f, 1e-4f) && near_f(it->pos[1], off, 1e-4f) &&
                  near_f(it->pos[2], 0.0f, 1e-4f))) continue;
            n_sec++;
            int mode = gdd_frag_mode(it->flags);
            if (mode == GDD_FRAG_PBR) pbr = it;
            else if (mode == GDD_FRAG_SHOCKWAVE) {
                if (near_f(it->scale[0], 0.5f * scale, 1e-5f)) pulse = it;
                else if (near_f(it->scale[0], 0.5f * scale * 0.7f * scale * 1.6f, 1e-4f)) glow1 = it;
            }
        }
        CHECK(n_sec == 6, "shield sector 2: 6 instances (got %d)", n_sec);
        CHECK(pbr != NULL, "shield PBR panel present");
        if (pbr) {
            CHECK_F("shield panel scale x", pbr->scale[0], 0.5f * scale, 1e-5f, "shield panel scale x");
            CHECK_F("shield panel scale y", pbr->scale[1], 1.8f * scale, 1e-5f, "shield panel scale y");
            CHECK_F("shield panel g", pbr->color[1], 0.8f, 1e-5f, "shield panel g");
            CHECK_F("shield panel alpha", pbr->alpha, alpha * 0.8f, 1e-5f, "shield panel alpha");
            CHECK(gdd_flag_bit(pbr->flags, 1) == 0, "shield panel opaque");
        }
        CHECK(pulse != NULL, "shield shockwave pulse present");
        if (pulse) {
            CHECK_F("shield pulse alpha", pulse->alpha, alpha * 0.6f, 1e-5f, "shield pulse alpha");
            CHECK_F("shield pulse metallic", pulse->pad[0], 15.0f, 1e-5f, "shield pulse metallic (pulse*15)");
            CHECK(gdd_flag_bit(pulse->flags, 1) == 1, "shield pulse additive");
        }
        CHECK(glow1 != NULL, "shield glow shell 1 present");
        if (glow1) {
            CHECK_F("shield glow1 alpha", glow1->alpha, alpha * 0.9f / 1.2f, 1e-5f, "shield glow1 alpha");
            CHECK_F("shield glow1 metallic", glow1->pad[0], 10.0f, 1e-5f, "shield glow1 metallic (pulse*10)");
        }
    }
    c.shield_timers = NULL;

    /* Compass hidden far away (cameraDist >= 150): bbox still drawn */
    c.camera_dist = QG_GRID_HIDE_DIST;
    run_builder(&c);
    CHECK(g_dyn_n == 21, "compass hidden at dist>=150, bbox present (%u)", g_dyn_n);
    c.camera_dist = QG_CAMERA_DEFAULT;

    /* Jump arrival: only object 0 (the ship + its quantum core) is drawn */
    static JumpState jump;
    memset(&jump, 0, sizeof(jump));
    jump.active = 1; jump.timer = 500; jump.age = 3.0f;
    c.jump_arrival = &jump;
    run_builder(&c);
    CHECK(g_dyn_n == 21, "jump arrival (stable): ship + quantum(4) + gate(16) = %u", g_dyn_n);
    c.jump_arrival = NULL;
}

/* ---- Wormhole jump sequence: black/white hole pair at both ends ---- */
static int count_frag(int mode) {
    int n = 0;
    for (uint32_t i = 0; i < g_dyn_n; i++)
        if (gdd_frag_mode(g_dyn[i].flags) == (uint32_t)mode) n++;
    return n;
}
static float max_frag_scale(int mode) {
    float m = 0.0f;
    for (uint32_t i = 0; i < g_dyn_n; i++)
        if (gdd_frag_mode(g_dyn[i].flags) == (uint32_t)mode && g_dyn[i].scale[0] > m) m = g_dyn[i].scale[0];
    return m;
}
static int count_black_horizons(void) {
    int n = 0; /* opaque black PBR sphere = event horizon */
    for (uint32_t i = 0; i < g_dyn_n; i++) {
        const GddInstance *it = &g_dyn[i];
        if (is_sphere_mesh((int)it->mesh) && gdd_frag_mode(it->flags) == GDD_FRAG_PBR &&
            it->color[0] == 0.0f && it->color[1] == 0.0f && it->color[2] == 0.0f) n++;
    }
    return n;
}

static void test_jump_sequence(void) {
    GddBuildCtx c = base_ctx();
    static WormholeState wh;
    static JumpState jump;
    memset(&wh, 0, sizeof(wh));
    memset(&jump, 0, sizeof(jump));

    /* Departure, gate just born (age 0): nothing visible yet */
    wh.active = 1; wh.age = 0.0f;
    c.wormhole = &wh;
    run_builder(&c);
    CHECK(count_frag(GDD_FRAG_BH_DISC) == 0 && count_black_horizons() == 0,
          "departure age 0: gate is closed (no disk, no horizon)");

    /* Departure, open: black hole dominant + white hole companion + bridge */
    wh.age = 3.0f;
    run_builder(&c);
    CHECK(count_black_horizons() == 1, "departure: one black horizon (%d)", count_black_horizons());
    CHECK(count_frag(GDD_FRAG_BH_DISC) == 1, "departure: one red accretion disk");
    CHECK(count_frag(GDD_FRAG_RIMGLOW) == 2, "departure: black + white halo (%d)", count_frag(GDD_FRAG_RIMGLOW));
    CHECK(count_dyn(GDD_MESH_BOX) == 2, "departure: white hole polar jets (%d box)", count_dyn(GDD_MESH_BOX));
    CHECK(count_frag(GDD_FRAG_SHOCKWAVE) == 0, "departure: no swallow shells before 5.5 s");

    /* Departure, swallow phase: contracting shock shells appear */
    wh.age = 6.5f;
    run_builder(&c);
    CHECK(count_frag(GDD_FRAG_SHOCKWAVE) == 2, "departure swallow: 2 shells (%d)", count_frag(GDD_FRAG_SHOCKWAVE));
    c.wormhole = NULL;

    /* Arrival, stable: white hole dominant + black companion */
    jump.active = 1; jump.timer = 500; jump.age = 3.0f;
    c.jump_arrival = &jump;
    run_builder(&c);
    CHECK(count_black_horizons() == 1, "arrival: one black companion horizon (%d)", count_black_horizons());
    CHECK(count_frag(GDD_FRAG_BH_DISC) == 1, "arrival: black companion has its disk");
    CHECK(count_frag(GDD_FRAG_RIMGLOW) == 2, "arrival: white + black halo (%d)", count_frag(GDD_FRAG_RIMGLOW));
    CHECK(count_dyn(GDD_MESH_BOX) == 2, "arrival: white hole polar jets (%d box)", count_dyn(GDD_MESH_BOX));
    float halo_stable = max_frag_scale(GDD_FRAG_RIMGLOW);

    /* Arrival burst (age 0.4 s): flash + shock shells on top of the gate */
    jump.age = 0.4f;
    run_builder(&c);
    CHECK(count_frag(GDD_FRAG_SHOCKWAVE) == 2, "arrival burst: 2 expanding shells (%d)", count_frag(GDD_FRAG_SHOCKWAVE));

    /* Arrival closing (90 ticks left): gate shrinks, 5-layer final flash */
    jump.age = 8.0f; jump.timer = 90;
    run_builder(&c);
    CHECK(max_frag_scale(GDD_FRAG_RIMGLOW) < halo_stable, "closing: gate is smaller than when stable");
    CHECK(count_frag(GDD_FRAG_HYPERWARP) >= 5, "closing: final flash layers present (%d)", count_frag(GDD_FRAG_HYPERWARP));

    /* Inflow particles: only those inside the draw radius are emitted */
    static ArrivalParticle parts[3];
    memset(parts, 0, sizeof(parts));
    parts[0].active = 1; parts[0].x = 3.0f;               /* inside  */
    parts[1].active = 1; parts[1].x = 50.0f;              /* outside */
    parts[2].active = 0; parts[2].x = 2.0f;               /* inactive */
    jump.age = 3.0f; jump.timer = 500;
    run_builder(&c);
    int base_pts = count_dyn(GDD_MESH_POINT);
    c.arrival_particles = parts; c.arrival_particle_count = 3;
    run_builder(&c);
    CHECK(count_dyn(GDD_MESH_POINT) == base_pts + 1, "inflow: 1 of 3 particles drawn (%d -> %d)",
          base_pts, count_dyn(GDD_MESH_POINT));
    c.arrival_particles = NULL; c.arrival_particle_count = 0;
    c.jump_arrival = NULL;
}

static void test_statics(void) {
    GddBuildCtx c = base_ctx();

    /* No objects, grid on: 21x21x3 = 1323 line instances in the map group */
    c.show_grid = 1;
    run_builder(&c);
    CHECK(g_map_n == 1323, "grid only: %u map instances (want 1323)", g_map_n);
    CHECK(g_dyn_n == 16, "grid only: 16 bbox lines, no objects (%u)", g_dyn_n);
    CHECK(gdd_flag_bit(g_map[0].flags, 0) == 1, "grid lines are NEVER_CULL");
    CHECK(gdd_frag_mode(g_map[0].flags) == GDD_FRAG_UNLIT, "grid unlit");

    /* + 3 stars -> 1326 */
    static GddStar stars[3];
    for (int i = 0; i < 3; i++) {
        memset(&stars[i], 0, sizeof(stars[i]));
        stars[i].pos[0] = 100.0f + i; stars[i].pos[1] = 50.0f; stars[i].pos[2] = -80.0f;
        stars[i].color[0] = 0.5f; stars[i].color[1] = 0.6f; stars[i].color[2] = 0.7f;
        stars[i].scale = 0.3f;
    }
    c.stars = stars;
    c.star_count = 3;
    c.cam_world[0] = 20.0f; c.cam_world[1] = 120.0f; c.cam_world[2] = 150.0f;
    run_builder(&c);
    CHECK(g_map_n == 1326, "grid+stars: %u map instances (want 1326)", g_map_n);
    const GddInstance *st = &g_map[1323];
    CHECK((int)(st->mesh + 0.5f) == GDD_MESH_DISC, "stars are round discs (GDD_MESH_DISC)");
    {   /* the disc faces the camera: orientation column 2 points from the star to the camera */
        float d[3] = { c.cam_world[0] - st->pos[0], c.cam_world[1] - st->pos[1], c.cam_world[2] - st->pos[2] };
        float l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        float dotn = (d[0] * st->orient[8] + d[1] * st->orient[9] + d[2] * st->orient[10]) / l;
        float dotu = (d[0] * st->orient[0] + d[1] * st->orient[1] + d[2] * st->orient[2]) / l;
        CHECK(dotn > 0.9999f && fabsf(dotu) < 1e-4f, "star disc faces the camera (n.d=%.5f u.d=%.5f)", dotn, dotu);
    }
    CHECK_F("star twinkle scale", st->scale[0], 0.3f, 1e-5f, "star scale");
    CHECK(gdd_frag_mode(st->flags) == GDD_FRAG_TWINKLE, "stars twinkle");

    /* Grid off, stars on: 3 */
    c.show_grid = 0;
    run_builder(&c);
    CHECK(g_map_n == 3, "stars only: %u map instances (want 3)", g_map_n);
}

static void test_galaxy_map(void) {
    GddBuildCtx c = base_ctx();
    c.map_anim = 1.0f; /* pure map mode */
    c.map_filter = 0;
    c.player_q[0] = 2; c.player_q[1] = 2; c.player_q[2] = 2;

    /* 2x2x2 galaxy (stride 3): (1,1,1) = star system, (2,2,2) = my
     * quadrant (empty). offset = -(40*1.2)/2 = -24. */
    static int64_t gal[3][3][3];
    memset(gal, 0, sizeof(gal));
    gal[1][1][1] = 1; /* sst (digit 0) */
    c.galaxy = &gal[0][0][0];
    c.galaxy_size = 2;

    run_builder(&c);
    /* frame + (1,1,1) + my quadrant = 3 */
    CHECK(g_map_n == 3, "map: %u map instances (want 3)", g_map_n);
    CHECK(g_dyn_n == 0, "map mode: no dyn objects (%u)", g_dyn_n);

    if (g_map_n == 3) {
        const GddInstance *frame = &g_map[0];
        /* CPU parity: the frame is a WIREFRAME cube (LINE_LIST on
         * cubeIndices) -> GDD_MESH_BOXWIRE, unlit 0.4/0.4/1.0 a=0.8. */
        CHECK((int)(frame->mesh + 0.5f) == GDD_MESH_BOXWIRE, "galaxy frame is a wireframe box");
        CHECK_F("frame half-size", frame->scale[0], 40.0f * 1.2f * 0.5f, 1e-4f, "frame half-size");
        CHECK(gdd_flag_bit(frame->flags, 0) == 1, "frame NEVER_CULL");
        CHECK_F("frame alpha", frame->alpha, 0.8f, 1e-6f, "frame alpha");

        /* (1,1,1): pxm = -24 + 0.5*1.2 = -23.4 ; pym same ;
         * pzm = -24 + (40.5-1)*1.2 = 23.4 */
        const GddInstance *sst = &g_map[1];
        /* filter 0: sectors are wireframe (CPU: wireframe pipeline) */
        CHECK((int)(sst->mesh + 0.5f) == GDD_MESH_BOXWIRE, "filter-0 sector is wireframe");
        CHECK_F("sector x", sst->pos[0], -23.4f, 1e-4f, "sector (1,1,1) x");
        CHECK_F("sector z", sst->pos[2], 23.4f, 1e-4f, "sector (1,1,1) z");
        CHECK_F("sector scale", sst->scale[0], 0.15f, 1e-5f, "sector scale");
        CHECK_F("sector color.r (sst)", sst->color[0], 1.0f, 1e-5f, "sst color r");
        CHECK_F("sector color.g (sst)", sst->color[1], 1.0f, 1e-5f, "sst color g");

        /* My quadrant: (-22.2, -22.2, 22.2), white wireframe highlight */
        const GddInstance *me = &g_map[2];
        CHECK((int)(me->mesh + 0.5f) == GDD_MESH_BOXWIRE, "my-q highlight is wireframe");
        CHECK_F("my-q x", me->pos[0], -22.2f, 1e-4f, "my quadrant x");
        CHECK_F("my-q color", me->color[0], 1.0f, 1e-5f, "my quadrant white");
    }

    /* Filter > 0 (filter 1 = star systems): matching sectors become SOLID
     * PBR boxes (CPU: graphics pipeline, usePushColor=5, metallic 0.5,
     * roughness 0.5); the player-quadrant highlight stays wireframe. */
    c.map_filter = 1;
    run_builder(&c);
    CHECK(g_map_n == 3, "map filter 1: %u map instances (want 3)", g_map_n);
    if (g_map_n == 3) {
        const GddInstance *frame = &g_map[0];
        CHECK((int)(frame->mesh + 0.5f) == GDD_MESH_BOXWIRE, "frame stays wireframe (filter 1)");
        const GddInstance *sst = &g_map[1];
        CHECK((int)(sst->mesh + 0.5f) == GDD_MESH_BOX, "filter-1 sector is a solid box");
        CHECK(gdd_frag_mode(sst->flags) == GDD_FRAG_PBR, "filter-1 sector PBR mode");
        CHECK_F("filter-1 metallic", sst->pad[0], 0.5f, 1e-6f, "filter-1 metallic");
        CHECK_F("filter-1 roughness", sst->pad[1], 0.5f, 1e-6f, "filter-1 roughness");
        CHECK_F("filter-1 color.r", sst->color[0], 1.0f, 1e-5f, "filter-1 sst color r");
        CHECK_F("filter-1 color.g", sst->color[1], 1.0f, 1e-5f, "filter-1 sst color g");
        const GddInstance *me = &g_map[2];
        CHECK((int)(me->mesh + 0.5f) == GDD_MESH_BOXWIRE, "my-q highlight wireframe (filter 1)");

        /* A filter with no matching sector in this 2x2x2 galaxy draws
         * only the frame + the player highlight (2 instances). */
        c.map_filter = 5; /* black holes: none present */
        run_builder(&c);
        CHECK(g_map_n == 2, "map filter 5 (no match): %u instances (want 2)", g_map_n);
        c.map_filter = 0;
    }

    /* Tactical objects must not leak into map mode */
    obj_at(&g_smooth[0], 20.0f, 20.0f, 20.0f, 0.0f, 0.0f, 0.0f);
    int types[1] = { 1 }, fac[1] = { 0 }, sc[1] = { 0 }, cl[1] = { 0 },
        act[1] = { 1 }, pl[1] = { 30 }, id[1] = { 100 };
    c.types = types; c.factions = fac; c.ship_classes = sc; c.cloaked = cl;
    c.active = act; c.platings = pl; c.ids = id; c.object_count = 1;
    run_builder(&c);
    CHECK(g_dyn_n == 0, "map mode: objects hidden (%u)", g_dyn_n);
}

/* Regression: a densely populated 40^3 galaxy must (a) stay inside the
 * map vertex budget (LOD: BOXWIRE -> wireframe BOX -> POINT) and (b) have
 * every sector marker enclosed by the galaxy frame. */
static GddInstance g_map_big[GDD_MAP_MAX];
static int64_t g_gal_big[41][41][41];

static uint32_t map_vertex_cost(int mesh) {
    switch (mesh) {
    case GDD_MESH_BOXWIRE: return GDD_VCOUNT_BOXWIRE;
    case GDD_MESH_BOX:     return GDD_VCOUNT_BOX;
    case GDD_MESH_BOX3:    return GDD_VCOUNT_BOX3;
    case GDD_MESH_POINT:   return GDD_VCOUNT_POINT;
    default:               return 0;
    }
}

static void test_galaxy_map_dense(void) {
    /* 20300: a galaxy with its 10,000 star systems (about 10,300 other occupied quadrants + 10,000 systems) */
    static const uint32_t counts[] = { 500, 9200, 20300, 30000, 64000 };
    for (unsigned t = 0; t < sizeof(counts) / sizeof(counts[0]); t++) {
        GddBuildCtx c = base_ctx();
        c.map_anim = 1.0f;
        c.player_q[0] = 20; c.player_q[1] = 20; c.player_q[2] = 20;
        memset(g_gal_big, 0, sizeof(g_gal_big));
        uint32_t left = counts[t];
        for (int x = 1; x <= 40 && left; x++)
            for (int y = 1; y <= 40 && left; y++)
                for (int z = 1; z <= 40 && left; z++) {
                    if ((x == 20 && y == 20 && z == 20)) continue;
                    g_gal_big[x][y][z] = 1; left--;
                }
        c.galaxy = &g_gal_big[0][0][0];
        c.galaxy_size = 40;
        c.cam_world[0] = 30.0f; c.cam_world[1] = -12.0f; c.cam_world[2] = 70.0f;   /* off-axis camera */

        g_dyn_n = g_map_n = 0;
        c.dyn_out = g_dyn; c.dyn_cap = T_MAX; c.dyn_count = &g_dyn_n;
        c.map_out = g_map_big; c.map_cap = GDD_MAP_MAX; c.map_count = &g_map_n;
        gdd_build_instances(&c);

        uint64_t verts = 0;
        float frame_half = g_map_big[0].scale[0];
        CHECK((int)(g_map_big[0].mesh + 0.5f) == GDD_MESH_BOXWIRE,
              "dense map %u: instance 0 is the frame", counts[t]);
        CHECK(near_f(frame_half, 24.0f, 1e-3f),
              "dense map %u: frame half-extent %.3f (want 24)", counts[t], frame_half);
        int outside = 0;
        for (uint32_t i = 0; i < g_map_n; i++) {
            verts += map_vertex_cost((int)(g_map_big[i].mesh + 0.5f));
            if (i == 0) continue;
            for (int a = 0; a < 3; a++)
                if (fabsf(g_map_big[i].pos[a]) > frame_half + 1e-3f) outside++;
        }
        CHECK(outside == 0, "dense map %u: %d marker(s) outside the frame", counts[t], outside);
        CHECK(verts <= GDD_VERTEX_CAPACITY,
              "dense map %u: %llu vertices exceed capacity %u", counts[t],
              (unsigned long long)verts, GDD_VERTEX_CAPACITY);
        CHECK(verts <= GDD_MAP_VERT_BUDGET + 2u * GDD_VCOUNT_BOXWIRE + GDD_VCOUNT_BOXWIRE,
              "dense map %u: %llu vertices exceed the map budget", counts[t],
              (unsigned long long)verts);
        if (counts[t] == 20300) {
            /* too many for full boxes, few enough for 3-face boxes: the markers must be cubes, not triangles,
             * and each one shows exactly the 3 faces turned towards the camera */
            int box3 = 0, points = 0, bad_mask = 0;
            for (uint32_t i = 1; i < g_map_n; i++) {
                int m = (int)(g_map_big[i].mesh + 0.5f);
                if (m == GDD_MESH_POINT) points++;
                if (m != GDD_MESH_BOX3) continue;
                box3++;
                unsigned mask = (unsigned)(g_map_big[i].pad[2] + 0.5f);
                const float *p = g_map_big[i].pos;
                unsigned want = ((c.cam_world[1] >= p[1]) ? 1u : 2u) | ((c.cam_world[2] >= p[2]) ? 4u : 8u) |
                                ((c.cam_world[0] >= p[0]) ? 32u : 16u);
                if (mask != want) bad_mask++;
            }
            CHECK(box3 > 20000 && points == 0, "galaxy with star systems: %d 3-face cubes, %d triangles", box3, points);
            CHECK(bad_mask == 0, "galaxy with star systems: %d cubes show faces turned away from the camera", bad_mask);
        }
    }
}

/* Regression: a derelict (type 22, left by a ship destroyed with phasers)
 * must render as the ship hull in dark grey — not as the generic sphere —
 * and must not get the Alliance quantum core. */
static void test_derelict(void) {
    for (int fac_v = 0; fac_v <= 11; fac_v += 11) {
        GddBuildCtx c = base_ctx();
        obj_at(&g_smooth[0], 20.0f, 20.0f, 20.0f, 0.0f, 0.0f, 0.0f);
        int types[1] = { 22 }, fac[1] = { fac_v }, sclass[1] = { 3 }, cloak[1] = { 0 },
            act[1] = { 1 }, plat[1] = { 100 }, ids[1] = { 15000 };
        c.types = types; c.factions = fac; c.ship_classes = sclass;
        c.cloaked = cloak; c.active = act; c.platings = plat; c.ids = ids;
        c.object_count = 1;
        run_builder(&c);

        int pyr = 0, spheres = 0, octas = 0;
        const GddInstance *hull = NULL;
        for (uint32_t i = 0; i < g_dyn_n; i++) {
            int m = (int)(g_dyn[i].mesh + 0.5f);
            if (m == GDD_MESH_PYRAMID) { pyr++; hull = &g_dyn[i]; }
            else if (is_sphere_mesh(m)) spheres++;
            else if (m == GDD_MESH_OCTA) octas++;
        }
        CHECK(pyr == 1, "derelict (faction %d): %d hull mesh(es) (want 1)", fac_v, pyr);
        CHECK(spheres == 0, "derelict (faction %d): no generic sphere (%d)", fac_v, spheres);
        CHECK(octas == 0, "derelict (faction %d): no quantum core (%d)", fac_v, octas);
        if (hull) {
            CHECK(hull->color[0] < 0.4f && hull->color[1] < 0.4f && hull->color[2] < 0.4f &&
                  fabsf(hull->color[0] - hull->color[1]) < 0.05f,
                  "derelict (faction %d) is dark grey (%.2f %.2f %.2f)", fac_v,
                  hull->color[0], hull->color[1], hull->color[2]);
            CHECK(gdd_flag_bit(hull->flags, 1) == 0, "derelict hull is not additive");
        }
    }
}

/* Audit: every object that is (or becomes) a ship must render as the ship
 * hull — player ships (type 1), NPC ships (type = faction 10..20) and their
 * derelicts (type 22) — for every faction / class / cloak combination. A
 * type missing from gdd_visual_for falls into the generic white sphere. */
static void test_ship_visual_audit(void) {
    int bad = 0, total = 0;
    static const int types[] = { 1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 22 };
    for (unsigned ti = 0; ti < sizeof(types) / sizeof(types[0]); ti++) {
        for (int fac_v = 0; fac_v <= 20; fac_v++) {
            if (fac_v > 0 && fac_v < 10) continue; /* not a game faction */
            for (int cl_v = 0; cl_v <= 13; cl_v++) {
                for (int cloaked = 0; cloaked <= 1; cloaked++) {
                    if (cloaked && types[ti] == 22) continue; /* wrecks never cloak */
                    GddBuildCtx c = base_ctx();
                    obj_at(&g_smooth[0], 20.0f, 20.0f, 20.0f, 0.0f, 0.0f, 0.0f);
                    int t[1] = { types[ti] }, f[1] = { fac_v }, sc[1] = { cl_v },
                        ck[1] = { cloaked }, act[1] = { 1 }, pl[1] = { 100 }, id[1] = { 1 };
                    c.types = t; c.factions = f; c.ship_classes = sc;
                    c.cloaked = ck; c.active = act; c.platings = pl; c.ids = id;
                    c.object_count = 1;
                    run_builder(&c);
                    int pyr = 0, sph = 0;
                    for (uint32_t i = 0; i < g_dyn_n; i++) {
                        int m = (int)(g_dyn[i].mesh + 0.5f);
                        if (m == GDD_MESH_PYRAMID) pyr++;
                        else if (is_sphere_mesh(m)) sph++;
                    }
                    total++;
                    if (pyr != 1 || sph != 0) {
                        if (bad < 10)
                            fprintf(stderr, "  ship audit: type %d faction %d class %d cloaked %d -> %d pyramid, %d sphere\n",
                                    types[ti], fac_v, cl_v, cloaked, pyr, sph);
                        bad++;
                    }
                }
            }
        }
    }
    CHECK(bad == 0, "ship audit: %d/%d type/faction/class/cloak combos do not render as a ship hull", bad, total);
}

/* ---- object_types.h contract ------------------------------------- */
_Static_assert(OBJ_PLAYER == 1 && OBJ_BASE == 3 && OBJ_BLACK_HOLE == 6,
               "wire values of the base object types changed");
_Static_assert(OBJ_NPC_FIRST == 10 && OBJ_NPC_LAST == 20,
               "NPC ship types must coincide with FACTION_KORTHIAN..FACTION_HIROGEN");
_Static_assert(OBJ_DERELICT == 22 && OBJ_TORPEDO == 27 && OBJ_QUASAR == 29,
               "wire values of the object types changed");
_Static_assert(OBJ_EXOTIC_FIRST == 100 && OBJ_EXOTIC_LAST == 131,
               "exotic compact objects must span 100..131");

/* The visual a type gets when gdd_visual_for() has no dedicated case: the
 * generic cosmic object (unlit sphere 1.5, colour 0.6/0.6/0.8). */
static int is_generic_cosmic_look(const GddInstance *g) {
    return is_sphere_mesh((int)(g->mesh + 0.5f)) &&
           fabsf(g->scale[0] - 1.5f) < 1e-3f &&
           fabsf(g->color[0] - 0.6f) < 1e-3f && fabsf(g->color[1] - 0.6f) < 1e-3f &&
           fabsf(g->color[2] - 0.8f) < 1e-3f;
}

static void build_single_type(int type, GddInstance *first) {
    GddBuildCtx c = base_ctx();
    obj_at(&g_smooth[0], 20.0f, 20.0f, 20.0f, 0.0f, 0.0f, 0.0f);
    int t[1] = { type }, f[1] = { 0 }, sc[1] = { 0 }, ck[1] = { 0 },
        act[1] = { 1 }, pl[1] = { 100 }, id[1] = { 1 };
    c.types = t; c.factions = f; c.ship_classes = sc;
    c.cloaked = ck; c.active = act; c.platings = pl; c.ids = id;
    c.object_count = 1;
    run_builder(&c);
    memset(first, 0, sizeof(*first));
    if (g_dyn_n > 0) *first = g_dyn[0];
}

/* Regression for "a type nobody taught the builder falls into the generic
 * sphere" (derelict, type 22): every type in object_types.h that is not a
 * cosmic environment (64..88, which share the generic look on purpose) must
 * have a dedicated visual, and unknown types must be recognised as such. */
static void test_object_types(void) {
    /* 1. the list has unique values and every one has a name */
    int seen[256] = {0}, dup = 0, unnamed = 0, n = 0;
#define OT_CHECK(name, value, label) \
    do { n++; if (seen[value]++) dup++; if (!obj_type_name(value)) unnamed++; } while (0);
    OBJECT_TYPE_LIST(OT_CHECK)
#undef OT_CHECK
    CHECK(dup == 0, "object_types.h: %d duplicated type value(s)", dup);
    CHECK(unnamed == 0, "object_types.h: %d type(s) without a name", unnamed);
    CHECK(n >= 80, "object_types.h: only %d types listed", n);
    for (int t = OBJ_EXOTIC_FIRST; t <= OBJ_EXOTIC_LAST; t++)
        CHECK(obj_type_name(t) != NULL && !seen[t], "exotic type %d named and not shadowed", t);
    CHECK(OBJ_EVOLVED_FIRST > OBJ_EXOTIC_LAST, "evolved star block must not overlap the exotic block");
    for (int t = OBJ_EVOLVED_FIRST; t <= OBJ_EVOLVED_LAST; t++)
        CHECK(obj_type_name(t) != NULL && !seen[t], "evolved star type %d named and not shadowed", t);
    CHECK(OBJ_PROTOSTELLAR_FIRST > OBJ_EVOLVED_LAST, "protostellar block must not overlap the evolved star block");
    for (int t = OBJ_PROTOSTELLAR_FIRST; t <= OBJ_PROTOSTELLAR_LAST; t++)
        CHECK(obj_type_name(t) != NULL && !seen[t], "protostellar type %d named and not shadowed", t);
    CHECK(OBJ_MOON_EXT_FIRST > OBJ_PROTOSTELLAR_LAST, "extended moon block must not overlap the protostellar block");
    for (int t = OBJ_MOON_EXT_FIRST; t <= OBJ_MOON_EXT_LAST; t++)
        CHECK(obj_type_name(t) != NULL && !seen[t], "extended moon type %d named and not shadowed", t);

    /* 2. every known type outside the cosmic band has a dedicated look */
    int generic = 0;
    for (int t = 0; t < 256; t++) {
        if (!obj_type_is_known(t) || obj_type_is_exotic(t)) continue;
        if (t >= OBJ_COSMIC_VOID && t <= OBJ_CMB) continue;
        GddInstance g;
        build_single_type(t, &g);
        if (is_generic_cosmic_look(&g)) {
            fprintf(stderr, "  object type %d (%s) renders with the generic cosmic look\n",
                    t, obj_type_name(t));
            generic++;
        }
    }
    CHECK(generic == 0, "%d known object type(s) fall into the generic cosmic look", generic);

    /* 3. predicates agree with the builder */
    for (int t = 0; t < 256; t++) {
        GddInstance g;
        build_single_type(t, &g);
        int pyr = 0, black_sphere = 0;
        for (uint32_t i = 0; i < g_dyn_n; i++) {
            if ((int)(g_dyn[i].mesh + 0.5f) == GDD_MESH_PYRAMID) pyr++;
            if (is_sphere_mesh((int)(g_dyn[i].mesh + 0.5f)) &&
                g_dyn[i].color[0] == 0.0f && g_dyn[i].color[1] == 0.0f &&
                g_dyn[i].color[2] == 0.0f) black_sphere++;
        }
        if (obj_type_has_ship_hull(t))
            CHECK(pyr == 1, "hull type %d (%s): %d pyramid(s), want 1", t,
                  obj_type_name(t) ? obj_type_name(t) : "?", pyr);
        if (obj_type_is_exotic(t))
            CHECK(black_sphere >= 1, "exotic type %d has no black event-horizon sphere", t);
        if (t != 0 && !obj_type_is_known(t))
            CHECK(g_dyn_n >= 1, "unknown type %d must still produce a visible instance", t);
    }
    CHECK(!obj_type_is_known(0) && !obj_type_is_known(210) && !obj_type_is_known(255),
          "unassigned types 0/210/255 must not be known");

    /* 4. CPU Vulkan hull path: same set as before for every assigned type,
     * and the free slots (89..99, 149..255) must stay outside it. */
    for (int t = 0; t < 256; t++) {
        int legacy = (t == OBJ_PLAYER) || (t >= 10 && t <= 88);
        CHECK(obj_type_uses_cpu_hull_path(t) == legacy,
              "type %d: cpu hull path %d, want %d", t, obj_type_uses_cpu_hull_path(t), legacy);
        if (obj_type_is_exotic(t) || obj_type_is_evolved_star(t) || obj_type_is_protostellar(t) || obj_type_is_moon(t) || (t >= 89 && t <= 99) || t > OBJ_MOON_EXT_LAST)
            CHECK(!obj_type_uses_cpu_hull_path(t), "type %d must not use the cpu hull path", t);
        if (obj_type_has_ship_hull(t))
            CHECK(obj_type_uses_cpu_hull_path(t), "hull type %d must use the cpu hull path", t);
    }
}

/* Stars: ship_class is the spectral class 0..6 (O,B,A,F,G,K,M); the GDD
 * builder must use the same palette as the CPU Vulkan path and the OpenGL
 * viewer, and an out-of-range class must fall back to G. */
static void test_star_spectral_palette(void) {
    static const float want[7][3] = {
        {0.4f,0.6f,1.0f}, {0.7f,0.8f,1.0f}, {1.0f,1.0f,1.0f}, {1.0f,1.0f,0.7f},
        {1.0f,0.9f,0.1f}, {1.0f,0.6f,0.2f}, {1.0f,0.2f,0.1f} };
    for (int cls = -1; cls <= 8; cls++) {
        GddBuildCtx c = base_ctx();
        obj_at(&g_smooth[0], 20.0f, 20.0f, 20.0f, 0.0f, 0.0f, 0.0f);
        int t[1] = { OBJ_STAR }, f[1] = { 0 }, sc[1] = { cls }, ck[1] = { 0 },
            act[1] = { 1 }, pl[1] = { 100 }, id[1] = { 1 };
        c.types = t; c.factions = f; c.ship_classes = sc;
        c.cloaked = ck; c.active = act; c.platings = pl; c.ids = id;
        c.object_count = 1;
        run_builder(&c);
        const float *w = want[(cls >= 0 && cls < 7) ? cls : 4];
        int ok = 0;
        for (uint32_t i = 0; i < g_dyn_n; i++)
            if (is_sphere_mesh((int)(g_dyn[i].mesh + 0.5f)) &&
                fabsf(g_dyn[i].scale[0] - 2.5f) < 1e-4f &&
                fabsf(g_dyn[i].color[0] - w[0]) < 1e-5f &&
                fabsf(g_dyn[i].color[1] - w[1]) < 1e-5f &&
                fabsf(g_dyn[i].color[2] - w[2]) < 1e-5f) ok = 1;
        CHECK(ok, "star spectral class %d has the shared palette colour", cls);
    }
}

/* Evolved stars (132..148): every type has its own palette colour and size
 * (giants big, white dwarfs tiny, GDD builder == shared palette), the names
 * are unique and the predicates agree with the index ranges. */
static void test_evolved_stars(void) {
    CHECK(OBJ_EVOLVED_COUNT == 17, "17 evolved star types, got %d", OBJ_EVOLVED_COUNT);
    for (int t = OBJ_EVOLVED_FIRST; t <= OBJ_EVOLVED_LAST; t++) {
        int e = t - OBJ_EVOLVED_FIRST;
        CHECK(obj_type_is_evolved_star(t) && !obj_type_is_exotic(t) &&
              !obj_type_is_black_hole(t) && !obj_type_has_ship_hull(t),
              "evolved type %d is only an evolved star", t);
        CHECK(obj_type_is_white_dwarf(t) == (e > OBJ_EVOLVED_GIANT_LAST),
              "evolved type %d white-dwarf predicate", t);
        CHECK(obj_type_is_pulsating_wd(t) == (e >= OBJ_EVOLVED_PULSATOR_MIN),
              "evolved type %d pulsator predicate", t);
        for (int u = t + 1; u <= OBJ_EVOLVED_LAST; u++)
            CHECK(strcmp(obj_type_name(t), obj_type_name(u)) != 0,
                  "evolved names %d and %d are unique", t, u);
        build_single_type(t, &(GddInstance){0});   /* fills g_dyn[] with the grid + the object */
        const float *w = obj_evolved_star_rgb(e);
        int ok = 0, generic = 0;
        for (uint32_t i = 0; i < g_dyn_n; i++) {
            if (is_generic_cosmic_look(&g_dyn[i])) generic = 1;
            if (is_sphere_mesh((int)(g_dyn[i].mesh + 0.5f)) &&
                fabsf(g_dyn[i].scale[0] - 2.5f * obj_evolved_star_scale(e)) < 1e-4f &&
                fabsf(g_dyn[i].color[0] - w[0]) < 1e-5f &&
                fabsf(g_dyn[i].color[1] - w[1]) < 1e-5f &&
                fabsf(g_dyn[i].color[2] - w[2]) < 1e-5f) ok = 1;
        }
        CHECK(ok, "evolved type %d (%s) uses the shared palette and size", t, obj_type_name(t));
        CHECK(!generic, "evolved type %d must not use the generic cosmic look", t);
        if (obj_type_is_white_dwarf(t))
            CHECK(obj_evolved_star_scale(e) < 1.0f, "white dwarf %d is smaller than a main-sequence star", t);
        else
            CHECK(obj_evolved_star_scale(e) > 1.0f, "giant %d is larger than a main-sequence star", t);
    }
    /* out-of-range index falls back to the red giant */
    CHECK(obj_evolved_star_rgb(-1) == obj_evolved_star_rgb(0) &&
          obj_evolved_star_rgb(OBJ_EVOLVED_COUNT) == obj_evolved_star_rgb(0),
          "evolved palette: out-of-range index falls back to the red giant");
    CHECK(!obj_type_is_evolved_star(OBJ_EXOTIC_LAST) && !obj_type_is_evolved_star(OBJ_EVOLVED_LAST + 1),
          "evolved range edges");
}

/* Natural moons (types 89..98): the block sits in the free slots right after
 * the legacy types, stays clear of the CPU hull path, every moon has a unique
 * name and the GPU-driven builder draws it as a planet-style sphere with the
 * shared palette and size (never the generic cosmic look). 99 stays free. */
static void test_moons(void) {
    CHECK(OBJ_MOON_COUNT == 10, "10 moon types, got %d", OBJ_MOON_COUNT);
    CHECK(OBJ_MOON_FIRST > OBJ_CMB && OBJ_MOON_LAST < OBJ_EXOTIC_FIRST && OBJ_MOON_LAST == 98,
          "moon block 89..98 sits between the legacy types and the exotic block");
    CHECK(!obj_type_is_known(99) && !obj_type_is_moon(99), "type 99 stays free");
    for (int t = OBJ_MOON_FIRST; t <= OBJ_MOON_LAST; t++) {
        int m = t - OBJ_MOON_FIRST;
        CHECK(obj_type_is_moon(t) && obj_type_is_known(t) && !obj_type_is_exotic(t) &&
              !obj_type_is_evolved_star(t) && !obj_type_has_ship_hull(t) &&
              !obj_type_uses_cpu_hull_path(t),
              "moon type %d is only a moon", t);
        for (int u = t + 1; u <= OBJ_MOON_LAST; u++)
            CHECK(strcmp(obj_type_name(t), obj_type_name(u)) != 0,
                  "moon names %d and %d are unique", t, u);
        build_single_type(t, &(GddInstance){0});
        const float *w = obj_moon_rgb(m);
        int ok = 0, generic = 0;
        for (uint32_t i = 0; i < g_dyn_n; i++) {
            if (is_generic_cosmic_look(&g_dyn[i])) generic = 1;
            if (is_sphere_mesh((int)(g_dyn[i].mesh + 0.5f)) &&
                fabsf(g_dyn[i].scale[0] - 1.8f * obj_moon_scale(m)) < 1e-4f &&
                fabsf(g_dyn[i].color[0] - w[0]) < 1e-5f &&
                fabsf(g_dyn[i].color[1] - w[1]) < 1e-5f &&
                fabsf(g_dyn[i].color[2] - w[2]) < 1e-5f) ok = 1;
        }
        CHECK(ok, "moon type %d (%s) uses the shared palette and size", t, obj_type_name(t));
        CHECK(!generic, "moon type %d must not use the generic cosmic look", t);
        CHECK(obj_moon_scale(m) < 1.0f, "moon %d is smaller than a planet", t);
    }
    CHECK(obj_moon_rgb(-1) == obj_moon_rgb(7) && obj_moon_rgb(OBJ_MOON_COUNT) == obj_moon_rgb(7),
          "moon palette: out-of-range index falls back to the regular moon");
    CHECK(!obj_type_is_moon(OBJ_MOON_FIRST - 1) && !obj_type_is_moon(OBJ_MOON_LAST + 1),
          "moon range edges");
}

/* Extended natural moons (types 165..177): dedicated looks, unique names */
static void test_moons_ext(void) {
    CHECK(OBJ_MOON_EXT_COUNT == 13, "13 extended moon types, got %d", OBJ_MOON_EXT_COUNT);
    CHECK(OBJ_MOON_EXT_FIRST > OBJ_PROTOSTELLAR_LAST, "extended moon block sits after protostellar block");
    for (int t = OBJ_MOON_EXT_FIRST; t <= OBJ_MOON_EXT_LAST; t++) {
        CHECK(obj_type_is_moon(t) && obj_type_is_moon_ext(t) && obj_type_is_known(t) &&
              !obj_type_is_exotic(t) && !obj_type_is_evolved_star(t) && !obj_type_is_protostellar(t) &&
              !obj_type_has_ship_hull(t) && !obj_type_uses_cpu_hull_path(t),
              "ext moon type %d is only a moon", t);
        for (int u = t + 1; u <= OBJ_MOON_EXT_LAST; u++)
            CHECK(strcmp(obj_type_name(t), obj_type_name(u)) != 0,
                  "ext moon names %d and %d are unique", t, u);
        build_single_type(t, &(GddInstance){0});
        const float *w = obj_moon_any_rgb(t);
        int ok = 0, generic = 0;
        for (uint32_t i = 0; i < g_dyn_n; i++) {
            if (is_generic_cosmic_look(&g_dyn[i])) generic = 1;
            if (is_sphere_mesh((int)(g_dyn[i].mesh + 0.5f)) &&
                fabsf(g_dyn[i].scale[0] - 1.8f * obj_moon_any_scale(t)) < 1e-4f &&
                fabsf(g_dyn[i].color[0] - w[0]) < 1e-5f &&
                fabsf(g_dyn[i].color[1] - w[1]) < 1e-5f &&
                fabsf(g_dyn[i].color[2] - w[2]) < 1e-5f) ok = 1;
        }
        CHECK(ok, "ext moon type %d (%s) uses the shared palette and size", t, obj_type_name(t));
        CHECK(!generic, "ext moon type %d must not use the generic cosmic look", t);
        CHECK(obj_moon_any_scale(t) < 1.0f, "ext moon %d is smaller than a planet", t);
    }
}

static void test_capacity_guard(void) {
    GddBuildCtx c = base_ctx();
    for (int i = 0; i < 3; i++)
        obj_at(&g_smooth[i], 15.0f + i, 20.0f, 20.0f, 0.0f, 0.0f, 0.0f);
    int types[3] = { 4, 4, 4 }, fac[3] = { 0, 0, 0 }, sc[3] = { 0, 0, 0 },
        cl[3] = { 0, 0, 0 }, act[3] = { 1, 1, 1 }, pl[3] = { 30, 30, 30 },
        id[3] = { 1, 2, 3 };
    c.types = types; c.factions = fac; c.ship_classes = sc; c.cloaked = cl;
    c.active = act; c.platings = pl; c.ids = id; c.object_count = 3;

    /* Full capacity: 3 stars fit */
    run_builder(&c);
    CHECK(g_dyn_n == 19, "capacity: 3 stars + 16 bbox = %u dyn", g_dyn_n);

    /* dyn_cap = 2: drop when full (same policy as the CPU path) */
    GddBuildCtx c2 = c;
    static GddInstance small[T_MAX];
    static uint32_t small_n;
    c2.dyn_out = small; c2.dyn_cap = 2; c2.dyn_count = &small_n;
    gdd_build_instances(&c2);
    CHECK(small_n == 2, "capacity: drop when full (%u)", small_n);

    /* map group capacity is independent */
    c2.map_out = small; c2.map_cap = 1; c2.map_count = &small_n;
    c2.show_grid = 1; /* 1323 lines */
    gdd_build_instances(&c2);
    CHECK(small_n == 1, "map capacity guard (%u)", small_n);
}

/* ------------------------------------------------------------------ */
/* Test: raw_objs fallback — navi con so->first=true ma active=1.     */
/* Riproduce la race condition: il GDD builder legge un buffer IPC più */
/* recente di quello su cui il tracker SmoothObj si è sincronizzato.   */
/* La nave dell'altro giocatore (type=1, faction=10, Korthian) deve    */
/* comparire nella lista dyn usando le coordinate raw (non interpolate) */
/* ------------------------------------------------------------------ */
static void test_raw_objs_fallback(void) {
    GddBuildCtx c = base_ctx();

    /* SmoothObj slot 0: nave locale (first=false, pronta) */
    obj_at(&g_smooth[0], 20.0f, 20.0f, 20.0f, 0.0f, 0.0f, 0.0f);

    /* SmoothObj slot 1: altro giocatore Korthian — first=true (tracker non aggiornato) */
    memset(&g_smooth[1], 0, sizeof(g_smooth[1]));
    g_smooth[1].first = true;  /* ← simula la race: tracker non ha ancora questo oggetto */

    /* Raw SharedObject array (IPC frame corrente, più recente del tracker) */
    static SharedObject raw[2];
    memset(raw, 0, sizeof(raw));
    raw[0].shm_x = 20.0f + Q_SHIFT; raw[0].shm_y = 20.0f + Q_SHIFT; raw[0].shm_z = 20.0f + Q_SHIFT;
    raw[0].active = 1;
    raw[1].shm_x = 25.0f + Q_SHIFT; raw[1].shm_y = 20.0f + Q_SHIFT; raw[1].shm_z = 20.0f + Q_SHIFT; /* 5 unità a destra */
    raw[1].h = 0.0f; raw[1].m = 0.0f; raw[1].r = 0.0f;
    raw[1].active = 1;

    int types[2]  = { 1, 1 };   /* entrambi player */
    int fac[2]    = { 0, 10 };  /* locale=Alliance, avversario=Korthian */
    int sclass[2] = { 0, 0 };
    int cloak[2]  = { 0, 0 };
    int act[2]    = { 1, 1 };
    int plat[2]   = { 30, 30 };
    int ids[2]    = { 1, 2 };
    c.types = types; c.factions = fac; c.ship_classes = sclass;
    c.cloaked = cloak; c.active = act; c.platings = plat; c.ids = ids;
    c.object_count = 2;
    c.raw_objs = raw;

    run_builder(&c);

    /* La nave Korthian (i=1, first=true) deve comparire nel dyn grazie al fallback */
    int pyramids = count_dyn(GDD_MESH_PYRAMID);
    CHECK(pyramids >= 2,
          "raw_objs fallback: attesi >=2 PYRAMID (propria nave + Korthian avversario), trovati %d",
          pyramids);

    /* La nave Korthian avversaria deve essere posizionata a X=5 (25-20=5, ts=1) */
    const float ship_stern_off = 0.7288f * 0.45f * 0.55f;
    /* Heading=0 → RotY(90°) → nose=+Z → stern_shift lungo +Z */
    const GddInstance *korth = find_dyn(GDD_MESH_PYRAMID, 5.0f, 0.0f, ship_stern_off, 1e-3f);
    CHECK(korth != NULL,
          "raw_objs fallback: nave Korthian avversaria non trovata a (5,0,stern_off)");
    if (korth) {
        /* Colore rosso Korthian (faction=10) */
        CHECK_F("korth color.r", korth->color[0], 1.0f, 1e-5f, "Korthian color.r");
        CHECK_F("korth color.g", korth->color[1], 0.1f, 1e-5f, "Korthian color.g");
        CHECK_F("korth color.b", korth->color[2], 0.0f, 1e-5f, "Korthian color.b");
        /* Deve usare WIREFRAME_PBR come le altre navi */
        CHECK(gdd_frag_mode(korth->flags) == GDD_FRAG_WIREFRAME_PBR,
              "Korthian ship WIREFRAME_PBR (mode=12)");
        /* Opaca (non additiva) */
        CHECK(gdd_flag_bit(korth->flags, 1) == 0, "Korthian ship opaque (not additive)");
    }

    /* Senza raw_objs, la nave Korthian deve essere invisibile (first=true → skip) */
    GddBuildCtx c2 = c;
    c2.raw_objs = NULL;
    run_builder(&c2);
    int pyramids_no_raw = count_dyn(GDD_MESH_PYRAMID);
    CHECK(pyramids_no_raw < pyramids,
          "senza raw_objs: nave Korthian (first=true) deve essere saltata (%d vs %d piramidi)",
          pyramids_no_raw, pyramids);
}

/* Nebulae must stay spherical and black holes must not inherit the nebula
 * look. Regression: types 51-55 (inside the 10..199 "ship" range) used to
 * get the ship stretch (x2.9154), heading and stern offset -> egg shape. */
static void test_nebula_and_black_hole(void) {
    GddBuildCtx c = base_ctx();
    obj_at(&g_smooth[0], 20.0f, 20.0f, 20.0f, 0.0f, 0.0f, 0.0f);
    obj_at(&g_smooth[1], 10.0f, 15.0f, 25.0f, 33.0f, 12.0f, 5.0f);
    obj_at(&g_smooth[2], 30.0f, 10.0f, 5.0f, 0.0f, 0.0f, 0.0f);
    obj_at(&g_smooth[3], 5.0f, 5.0f, 5.0f, 0.0f, 0.0f, 0.0f);
    int types[4] = { 1, 51, 7, 6 };
    int fac[4] = { 0, 0, 0, 0 };
    int sclass[4] = { 0, 0, 0, 0 };
    int cloak[4] = { 0, 0, 0, 0 };
    int act[4] = { 1, 1, 1, 1 };
    int plat[4] = { 30, 30, 30, 30 };
    int ids[4] = { 100, 101, 102, 103 };
    c.types = types; c.factions = fac; c.ship_classes = sclass;
    c.cloaked = cloak; c.active = act; c.platings = plat; c.ids = ids;
    c.object_count = 4;
    run_builder(&c);

    /* nebula type 51 at the plain mapped position (X, Z, -Y), no offset */
    const GddInstance *neb = find_dyn(GDD_MESH_SPHERE, -10.0f, 5.0f, 5.0f, 1e-4f);
    CHECK(neb != NULL, "nebula 51 at mapped position, no ship stern offset");
    if (neb) {
        CHECK(gdd_frag_mode(neb->flags) == GDD_FRAG_NEBULA_IRIS, "nebula 51 uses the iridescent mode");
        CHECK(neb->scale[0] == neb->scale[1] && neb->scale[1] == neb->scale[2],
              "nebula 51 is spherical (uniform scale, not stretched like a ship)");
        CHECK(neb->orient[0] == 1.0f && neb->orient[5] == 1.0f && neb->orient[10] == 1.0f &&
              neb->orient[1] == 0.0f, "nebula 51 ignores the object heading");
    }
    const GddInstance *n7 = find_dyn(GDD_MESH_SPHERE, 10.0f, -15.0f, 10.0f, 1e-4f);
    CHECK(n7 != NULL, "nebula 7 present");
    if (n7) {
        CHECK(gdd_frag_mode(n7->flags) == GDD_FRAG_NEBULA_IRIS, "nebula 7 uses the iridescent mode");
        CHECK(n7->scale[0] == n7->scale[1] && n7->scale[1] == n7->scale[2], "nebula 7 spherical");
    }

    /* black hole at (5,5,5) -> (-15, -15, 15): horizon + disk + halo, no nebula mode */
    int bh_n = 0, bh_neb = 0, bh_disk = 0, bh_halo = 0, bh_core = 0;
    for (uint32_t i = 0; i < g_dyn_n; i++) {
        const GddInstance *it = &g_dyn[i];
        if (!near_f(it->pos[0], -15.0f, 1e-4f) || !near_f(it->pos[1], -15.0f, 1e-4f) ||
            !near_f(it->pos[2], 15.0f, 1e-4f)) continue;
        unsigned m = gdd_frag_mode(it->flags);
        bh_n++;
        if (m == GDD_FRAG_NEBULA_IRIS || m == GDD_FRAG_NEBULA) bh_neb++;
        if (m == GDD_FRAG_BH_DISC && (int)(it->mesh + 0.5f) == GDD_MESH_RING) bh_disk++;
        if (m == GDD_FRAG_RIMGLOW) bh_halo++;
        if (m == GDD_FRAG_UNLIT && it->color[0] == 0.0f && it->color[1] == 0.0f &&
            it->color[2] == 0.0f) bh_core++;
    }
    CHECK(bh_n == 3, "black hole emits 3 instances (horizon, disk, halo): %d", bh_n);
    CHECK(bh_neb == 0, "black hole never uses a nebula mode");
    CHECK(bh_core == 1 && bh_disk == 1 && bh_halo == 1, "horizon/disk/halo present once each");
}

/* Every black hole must emit its own accretion disk, and the disk must be
 * red whatever the hole's palette (the lensing halo keeps the per-hole
 * palette). Eight holes, one per x position, ids chosen to hit different
 * palette entries. */
static void test_black_hole_discs_red(void) {
    enum { NBH = 8 };
    GddBuildCtx c = base_ctx();
    int types[NBH], fac[NBH], sclass[NBH], cloak[NBH], act[NBH], plat[NBH], ids[NBH];
    for (int k = 0; k < NBH; k++) {
        obj_at(&g_smooth[k], 5.0f + 4.0f * (float)k, 5.0f, 5.0f, 0.0f, 0.0f, 0.0f);
        types[k] = 6; fac[k] = 0; sclass[k] = 0; cloak[k] = 0;
        act[k] = 1; plat[k] = 30; ids[k] = 200 + k;
    }
    c.types = types; c.factions = fac; c.ship_classes = sclass;
    c.cloaked = cloak; c.active = act; c.platings = plat; c.ids = ids;
    c.object_count = NBH;
    run_builder(&c);

    int discs_total = 0, red_ok = 0, one_each = 0, halo_differs = 0;
    float halo0[3] = { 0, 0, 0 };
    for (int k = 0; k < NBH; k++) {
        float hx = -15.0f + 4.0f * (float)k;
        int disk = 0, halo = 0, core = 0;
        const GddInstance *disk_it = NULL;
        float halo_c[3] = { 0, 0, 0 };
        for (uint32_t i = 0; i < g_dyn_n; i++) {
            const GddInstance *it = &g_dyn[i];
            if (!near_f(it->pos[0], hx, 1e-4f) || !near_f(it->pos[1], -15.0f, 1e-4f) ||
                !near_f(it->pos[2], 15.0f, 1e-4f)) continue;
            unsigned m = gdd_frag_mode(it->flags);
            if (m == GDD_FRAG_BH_DISC && (int)(it->mesh + 0.5f) == GDD_MESH_RING) {
                disk++; disk_it = it;
            }
            if (m == GDD_FRAG_RIMGLOW) {
                halo++;
                halo_c[0] = it->color[0]; halo_c[1] = it->color[1]; halo_c[2] = it->color[2];
            }
            if (m == GDD_FRAG_UNLIT && it->color[0] == 0.0f && it->color[1] == 0.0f &&
                it->color[2] == 0.0f) core++;
        }
        discs_total += disk;
        if (disk == 1 && halo == 1 && core == 1) one_each++;
        if (disk_it && disk_it->color[0] >= 0.9f && disk_it->color[1] <= 0.3f &&
            disk_it->color[2] <= 0.3f) red_ok++;
        if (k == 0) { halo0[0] = halo_c[0]; halo0[1] = halo_c[1]; halo0[2] = halo_c[2]; }
        else if (!near_f(halo_c[0], halo0[0], 1e-4f) || !near_f(halo_c[1], halo0[1], 1e-4f) ||
                 !near_f(halo_c[2], halo0[2], 1e-4f)) halo_differs++;
    }
    CHECK(discs_total == NBH, "every black hole emits exactly one accretion disk: %d/%d", discs_total, NBH);
    CHECK(one_each == NBH, "each hole has horizon + disk + halo once: %d/%d", one_each, NBH);
    CHECK(red_ok == NBH, "every accretion disk is red: %d/%d", red_ok, NBH);
    CHECK(halo_differs > 0, "halo keeps the per-hole palette (disk color is independent of it)");
}

/* The 32 exotic compact objects (types 100-131, e.g. Kerr = 107,
 * Kerr-Newman = 109) must look like black holes: black horizon + red
 * iridescent disk + halo, exactly once each, and no leftover metallic
 * additive sphere from the old generic exotic look. */
static void test_exotic_black_holes_have_discs(void) {
    enum { NEX = 32 };
    GddBuildCtx c = base_ctx();
    int types[NEX], fac[NEX], sclass[NEX], cloak[NEX], act[NEX], plat[NEX], ids[NEX];
    for (int k = 0; k < NEX; k++) {
        obj_at(&g_smooth[k], 1.0f + 1.2f * (float)k, 5.0f, 5.0f, 0.0f, 0.0f, 0.0f);
        types[k] = 100 + k; fac[k] = 0; sclass[k] = 0; cloak[k] = 0;
        act[k] = 1; plat[k] = 30; ids[k] = 83000 + k;
    }
    c.types = types; c.factions = fac; c.ship_classes = sclass;
    c.cloaked = cloak; c.active = act; c.platings = plat; c.ids = ids;
    c.object_count = NEX;
    run_builder(&c);

    int one_each = 0, red_ok = 0, total_inst = 0, pbr_left = 0;
    int kn_ok = 0; /* Kerr-Newman = type 109 */
    for (int k = 0; k < NEX; k++) {
        float hx = (1.0f + 1.2f * (float)k) - 20.0f;
        int disk = 0, halo = 0, core = 0, n = 0, red = 0;
        for (uint32_t i = 0; i < g_dyn_n; i++) {
            const GddInstance *it = &g_dyn[i];
            if (!near_f(it->pos[0], hx, 1e-4f) || !near_f(it->pos[1], -15.0f, 1e-4f) ||
                !near_f(it->pos[2], 15.0f, 1e-4f)) continue;
            unsigned m = gdd_frag_mode(it->flags);
            n++;
            if (m == GDD_FRAG_PBR) pbr_left++;
            if (m == GDD_FRAG_BH_DISC && (int)(it->mesh + 0.5f) == GDD_MESH_RING) {
                disk++;
                if (it->color[0] >= 0.9f && it->color[1] <= 0.3f && it->color[2] <= 0.3f) red++;
            }
            if (m == GDD_FRAG_RIMGLOW) halo++;
            if (m == GDD_FRAG_UNLIT && it->color[0] == 0.0f && it->color[1] == 0.0f &&
                it->color[2] == 0.0f) core++;
        }
        total_inst += n;
        int ok = (n == 3 && disk == 1 && halo == 1 && core == 1);
        if (ok) one_each++;
        if (red == 1) red_ok++;
        if (k == 9 && ok && red == 1) kn_ok = 1;
    }
    CHECK(one_each == NEX, "all 32 exotic types emit horizon + disk + halo once: %d/%d", one_each, NEX);
    CHECK(red_ok == NEX, "all 32 exotic accretion disks are red: %d/%d", red_ok, NEX);
    CHECK(kn_ok == 1, "Kerr-Newman (type 109) has its accretion disk");
    CHECK(pbr_left == 0, "no generic metallic exotic sphere is left");
    CHECK(total_inst == NEX * 3, "exactly 3 instances per exotic object: %d", total_inst);
}


/* ------------------------------------------------------------------ */
/* Stars, planets and moons use GDD_MESH_SPHERE_HI (16x32) when big    */
/* enough on screen; far ones fall back to the 6x10 sphere; the other  */
/* spheres (black holes, mines, FX) never use it.                      */
/* ------------------------------------------------------------------ */
static void test_sphere_hi_lod(void) {
    GddBuildCtx c = base_ctx();
    const int moon_type = OBJ_MOON_FIRST + 5;          /* atmosphere moon: radius 1.8 * 0.60 */
    const int small_moon = OBJ_MOON_FIRST + 9;         /* submoon: radius 1.8 * 0.20 */
    obj_at(&g_smooth[0], 20.0f, 20.0f, 20.0f, 0, 0, 0);
    obj_at(&g_smooth[1], 10.0f, 15.0f, 25.0f, 0, 0, 0);   /* star   -> (-10,  5,  5) */
    obj_at(&g_smooth[2], 30.0f, 10.0f,  5.0f, 0, 0, 0);   /* planet -> ( 10,-15, 10) */
    obj_at(&g_smooth[3], 15.0f, 25.0f, 15.0f, 0, 0, 0);   /* moon   -> ( -5, -5, -5) */
    obj_at(&g_smooth[4], 25.0f, 25.0f, 25.0f, 0, 0, 0);   /* small moon */
    obj_at(&g_smooth[5], 12.0f, 12.0f, 12.0f, 0, 0, 0);   /* black hole -> (-8, -8, 8) */
    int types[6] = { 1, 4, 5, moon_type, small_moon, 6 };
    int fac[6] = {0}, sclass[6] = { 0, 2, 0, 0, 0, 0 }, cloak[6] = {0};
    int act[6] = { 1, 1, 1, 1, 1, 1 }, plat[6] = { 30, 30, 30, 30, 30, 30 };
    int ids[6] = { 100, 101, 102, 103, 104, 105 };
    c.types = types; c.factions = fac; c.ship_classes = sclass; c.cloaked = cloak;
    c.active = act; c.platings = plat; c.ids = ids; c.object_count = 6;

    run_builder(&c);
    CHECK(count_dyn(GDD_MESH_SPHERE_HI) >= 3, "default camera: star, planet and moon use the HI sphere (%d)", count_dyn(GDD_MESH_SPHERE_HI));
    CHECK(find_dyn(GDD_MESH_SPHERE_HI, -10.0f, 5.0f, 5.0f, 1e-4f) != NULL, "star is HI");
    CHECK(find_dyn(GDD_MESH_SPHERE_HI, 10.0f, -15.0f, 10.0f, 1e-4f) != NULL, "planet is HI");
    CHECK(find_dyn(GDD_MESH_SPHERE_HI, -5.0f, -5.0f, -5.0f, 1e-4f) != NULL, "big moon is HI");
    CHECK(find_dyn(GDD_MESH_SPHERE, -8.0f, -8.0f, 8.0f, 1e-4f) != NULL, "black hole keeps the 360-vertex sphere");
    const GddInstance *star = find_dyn(GDD_MESH_SPHERE_HI, -10.0f, 5.0f, 5.0f, 1e-4f);
    CHECK(star && near_f(star->scale[0], 2.5f, 1e-5f), "HI star keeps scale 2.5");

    c.camera_dist = 2000.0f * QG_SCALE;   /* very far: below the threshold the moon falls back */
    run_builder(&c);
    CHECK(find_dyn(GDD_MESH_SPHERE_HI, -5.0f, -5.0f, -5.0f, 1e-4f) == NULL, "far camera: the moon falls back to the base sphere");
    CHECK(find_dyn(GDD_MESH_SPHERE, -5.0f, -5.0f, -5.0f, 1e-4f) != NULL, "far camera: the moon is still drawn (base sphere)");
    c.camera_dist = QG_CAMERA_DEFAULT;
}

int main(void) {
    printf("GDD contract test\n");
    test_layout();
    test_flags();
    test_objects_mapping();
    test_sphere_hi_lod();
    test_nebula_and_black_hole();
    test_black_hole_discs_red();
    test_exotic_black_holes_have_discs();
    test_effects();
    test_compass_and_jump();
    test_jump_sequence();
    test_statics();
    test_galaxy_map();
    test_galaxy_map_dense();
    test_derelict();
    test_ship_visual_audit();
    test_object_types();
    test_star_spectral_palette();
    test_evolved_stars();
    test_moons();
    test_moons_ext();
    test_capacity_guard();
    test_raw_objs_fallback();
    printf("  %d checks passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
