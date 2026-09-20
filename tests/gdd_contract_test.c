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
 *   1. Layout invariants: the CPU structs must keep the exact sizes and
 *      field offsets the GLSL side assumes (spacegl_gdd.h carries the
 *      compile-time _Static_asserts; this re-checks them at runtime so
 *      a mis-compiled TU cannot sneak through).
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
    CHECK(offsetof(GddPC, line_min_wu) == 32, "pc.line_min_wu@32 (after pad)");
    CHECK(sizeof(GddScenePC) == GDD_SCENE_PC_STRIDE && GDD_SCENE_PC_STRIDE == 84,
          "GddScenePC 84B");
    /* GLSL struct field alignment (a/b/c, mat3@48, params@64...) */
    CHECK(offsetof(GddInstance, pos) == 0, "inst.pos@0");
    CHECK(offsetof(GddInstance, mesh) == 12, "inst.mesh@12 (a.w)");
    CHECK(offsetof(GddInstance, scale) == 16, "inst.scale@16 (b.xyz)");
    CHECK(offsetof(GddInstance, flags) == 28, "inst.flags@28 (b.w)");
    CHECK(offsetof(GddInstance, color) == 32, "inst.color@32 (c.xyz)");
    CHECK(offsetof(GddInstance, alpha) == 44, "inst.alpha@44 (c.w)");
    CHECK(offsetof(GddInstance, orient) == 48, "inst.orient@48 (mat3)");
    CHECK(offsetof(GddInstance, pad) == 96, "inst.pad@96 (vec4)");
    CHECK(offsetof(GddVertex, normal) == 32, "vtx.normal@32");
    CHECK(offsetof(GddVertex, local) - offsetof(GddVertex, normal) == 16,
          "vtx.mode at normal.w");
    CHECK(offsetof(GddVertex, metallic) == 64, "vtx.metallic@64 (params.x)");
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
static void obj_at(SmoothObj *o, float x, float y, float z, float h, float m, float r) {
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
    c.camera_dist = 80.0f;
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
    CHECK(g_dyn_n == 19, "3 objects + 16 bbox edges -> %u dyn instances", g_dyn_n);

    /* (X, Z, -Y) centered mapping: obj1 (10,15,25) -> (-10, 5, 5) */
    const GddInstance *star = find_dyn(GDD_MESH_SPHERE, -10.0f, 5.0f, 5.0f, 1e-4f);
    CHECK(star != NULL, "star at mapped position (-10,5,5)");
    if (star) {
        CHECK_F("star scale", star->scale[0], 2.5f, 1e-5f, "star scale");
        CHECK_F("star color.r", star->color[0], 1.0f, 1e-5f, "star color.r");
        CHECK_F("star color.b", star->color[2], 0.4f, 1e-5f, "star color.b (class 2)");
        CHECK(star->alpha == 1.0f, "star alpha");
        CHECK(gdd_flag_bit(star->flags, 1) == 1, "star is additive");
        CHECK(gdd_frag_mode(star->flags) == GDD_FRAG_HYPERWARP, "star frag mode");
        CHECK(gdd_flag_bit(star->flags, 0) == 0, "star is cullable");
        /* non-ship, non-spin: identity orientation */
        CHECK(star->orient[0] == 1.0f && star->orient[5] == 1.0f &&
              star->orient[10] == 1.0f && star->orient[1] == 0.0f,
              "star identity orientation");
    }

    /* Player ship: pyramid at the center, x stretched by the nose ratio */
    const GddInstance *ship = find_dyn(GDD_MESH_PYRAMID, 0.0f, 0.0f, 0.0f, 1e-4f);
    CHECK(ship != NULL, "ship (pyramid) at center");
    if (ship) {
        /* GDD_SHIP_SCALE = SCALE_SHIP * 0.55 = 0.45 * 0.55 */
        CHECK_F("ship sx", ship->scale[0], 0.45f * 0.55f * 2.9154f, 1e-4f, "ship sx");
        CHECK_F("ship sy", ship->scale[1], 0.45f * 0.55f, 1e-4f, "ship sy");
        CHECK_F("ship color.g", ship->color[1], 1.0f, 1e-5f, "ship color.g");
        CHECK(gdd_frag_mode(ship->flags) == GDD_FRAG_UNLIT, "ship unlit");
        CHECK(gdd_flag_bit(ship->flags, 1) == 0, "ship opaque");
    }

    /* Planet (30,10,5) -> (10, -15, 15) */
    const GddInstance *planet = find_dyn(GDD_MESH_SPHERE, 10.0f, -15.0f, 15.0f, 1e-4f);
    CHECK(planet != NULL, "planet at mapped position (10,-15,15)");
    if (planet)
        CHECK_F("planet scale", planet->scale[0], 1.8f, 1e-5f, "planet scale");

    /* Inactive objects must not be emitted */
    int act2[3] = { 1, 0, 1 };
    c.active = act2;
    run_builder(&c);
    CHECK(g_dyn_n == 18, "inactive object dropped + 16 bbox = %u dyn", g_dyn_n);
}

static void test_effects(void) {
    GddBuildCtx c = base_ctx();
    obj_at(&g_smooth[0], 20.0f, 20.0f, 20.0f, 0.0f, 0.0f, 0.0f);
    int types[1] = { 1 }, fac[1] = { 0 }, sc[1] = { 0 }, cl[1] = { 0 },
        act[1] = { 1 }, pl[1] = { 30 }, id[1] = { 100 };
    c.types = types; c.factions = fac; c.ship_classes = sc; c.cloaked = cl;
    c.active = act; c.platings = pl; c.ids = id; c.object_count = 1;

    /* One explosion at (5,5,5), life 0.9:
     * flash (life>0.7) + core + aura + 16 point pixels = 19 instances */
    static ActiveBoom boom[GDD_MAX_ACTIVE_BOOMS];
    memset(boom, 0, sizeof(boom));
    boom[0].x = boom[0].y = boom[0].z = 5.0f;
    boom[0].life = 0.9f;
    for (int p = 0; p < (int)GDD_EXPLOSION_PIXELS; p++) {
        boom[0].offsets[p][0] = 0.1f * p;
        boom[0].offsets[p][1] = 0.0f;
        boom[0].offsets[p][2] = 0.0f;
        boom[0].colors[p][0] = 1.0f; boom[0].colors[p][1] = 1.0f; boom[0].colors[p][2] = 0.0f;
    }
    c.booms = boom;

        /* One beam raw (20,20,20) -> (30,20,20), i.e. centered (0,0,0) -> (10,0,0),
     * life 1.0: beam + core + splash. owner_id/extra = 0 disables the
     * real-time tracking in the builder, so the beam stays where the test puts it. */
    static ActiveBeam beam[GDD_MAX_ACTIVE_BEAMS];
    memset(beam, 0, sizeof(beam));
    beam[0].sx = 20.0f; beam[0].sy = 20.0f; beam[0].sz = 20.0f;
    beam[0].tx = 30.0f; beam[0].ty = 20.0f; beam[0].tz = 20.0f;
    beam[0].life = 1.0f;
    beam[0].owner_id = 0; beam[0].extra = 0; beam[0].emitter_id = 1;
    c.beams = beam;

    /* One active torpedo */
    static ActiveTorp torp[GDD_MAX_ACTIVE_TORPS];
    memset(torp, 0, sizeof(torp));
    torp[0].x = 1.0f; torp[0].y = 2.0f; torp[0].z = 3.0f;
    torp[0].active = 500; torp[0].id = 7;
    c.torps = torp;

    run_builder(&c);

    /* ship (1) + boom (3+16) + beam (3) + torp (1) = 22 */
    CHECK(g_dyn_n == 40, "effects: ship+boom+beam+torp+16bbox -> %u dyn (want 40)", g_dyn_n);
    CHECK(count_dyn(GDD_MESH_POINT) == 16, "16 explosion pixels, got %d",
          count_dyn(GDD_MESH_POINT));

    /* Boom core at (5,5,5): sphere, additive, hyperwarp */
    const GddInstance *core = find_dyn(GDD_MESH_SPHERE, 5.0f, 5.0f, 5.0f, 1e-4f);
    CHECK(core != NULL, "boom core sphere at (5,5,5)");
    if (core) {
        CHECK_F("boom core scale", core->scale[0], 0.9f * 1.5f, 1e-4f, "boom core scale");
        CHECK(gdd_flag_bit(core->flags, 1) == 1, "boom core additive");
        CHECK(gdd_frag_mode(core->flags) == GDD_FRAG_HYPERWARP, "boom core mode");
    }
    /* Beam: midpoint (5,0,0), a box oriented along +X (x half-extent ~ dist/0.9) */
    int boxes = count_dyn(GDD_MESH_BOX);
    CHECK(boxes == 2, "beam: 2 boxes, got %d", boxes);
    const GddInstance *bl = find_dyn(GDD_MESH_BOX, 5.0f, 0.0f, 0.0f, 1e-3f);

    CHECK(bl != NULL, "beam box at midpoint (5,0,0)");
    if (bl)
                CHECK_F("beam length", bl->scale[0], 10.0f, 1e-3f, "beam length");

    /* Torpedo: pyramid (identity orientation) at (1,2,3) */
    const GddInstance *tp = find_dyn(GDD_MESH_PYRAMID, 1.0f, 2.0f, 3.0f, 1e-4f);
    CHECK(tp != NULL, "torpedo pyramid at (1,2,3)");

    /* Dismantle with life < 0.7 is skipped (CPU parity) */
    static ActiveDismantle dm[GDD_MAX_ACTIVE_DISMANTLES];
    memset(dm, 0, sizeof(dm));
    dm[0].x = 9.0f; dm[0].life = 0.5f;
    c.dismantles = dm;
    run_builder(&c);
    CHECK(g_dyn_n == 40, "dead dismantle skipped (%u)", g_dyn_n);
    dm[0].life = 0.9f;
    run_builder(&c);
    CHECK(g_dyn_n == 43, "live dismantle adds 3 spheres (%u)", g_dyn_n);
}

static void test_compass_and_jump(void) {
    GddBuildCtx c = base_ctx();
    obj_at(&g_smooth[0], 20.0f, 20.0f, 20.0f, 0.0f, 0.0f, 0.0f);
    int types[1] = { 1 }, fac[1] = { 0 }, sc[1] = { 0 }, cl[1] = { 0 },
        act[1] = { 1 }, pl[1] = { 30 }, id[1] = { 100 };
    c.types = types; c.factions = fac; c.ship_classes = sc; c.cloaked = cl;
    c.active = act; c.platings = pl; c.ids = id; c.object_count = 1;

    /* No compass: ship + 16 bbox lines */
    run_builder(&c);
    CHECK(g_dyn_n == 17, "no compass: ship+16bbox = %u dyn", g_dyn_n);

    /* Compass on: +10 instances (3 axes, 4 rings, 2 vectors) + 16 bbox */
    c.show_axes = 1;
    run_builder(&c);
    CHECK(g_dyn_n == 27, "compass: 11 dyn + 16 bbox = %u", g_dyn_n);
    CHECK(count_dyn(GDD_MESH_LINE) == 21, "compass: 5 lines + 16 bbox = %d", count_dyn(GDD_MESH_LINE));
    CHECK(count_dyn(GDD_MESH_RING) == 5, "compass: 5 rings (%d)", count_dyn(GDD_MESH_RING));

    /* Shield hit: +2 additive rings */
    static int shields[6] = { 0, 0, 30, 0, 0, 0 };
    c.shield_timers = shields;
    run_builder(&c);
    CHECK(g_dyn_n == 29, "shield glow: 13 dyn + 16 bbox = %u", g_dyn_n);
    c.shield_timers = NULL;

    /* Compass hidden far away (cameraDist >= 150): bbox still drawn */
    c.camera_dist = 150.0f;
    run_builder(&c);
    CHECK(g_dyn_n == 17, "compass hidden at dist>=150, bbox present (%u)", g_dyn_n);
    c.camera_dist = 80.0f;

    /* Jump arrival: only object 0 (the ship) is drawn */
    static JumpState jump;
    memset(&jump, 0, sizeof(jump));
    jump.active = 1; jump.timer = 500;
    c.jump_arrival = &jump;
    run_builder(&c);
    CHECK(g_dyn_n == 4, "jump arrival hides everything else (%u)", g_dyn_n);
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
    run_builder(&c);
    CHECK(g_map_n == 1326, "grid+stars: %u map instances (want 1326)", g_map_n);
    const GddInstance *st = &g_map[1323];
    CHECK((int)(st->mesh + 0.5f) == GDD_MESH_POINT, "stars are POINT meshes");
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
        CHECK((int)(frame->mesh + 0.5f) == GDD_MESH_BOX, "galaxy frame is a box");
        CHECK_F("frame half-size", frame->scale[0], 40.0f * 1.2f * 0.5f, 1e-4f, "frame half-size");
        CHECK(gdd_flag_bit(frame->flags, 0) == 1, "frame NEVER_CULL");

        /* (1,1,1): pxm = -24 + 0.5*1.2 = -23.4 ; pym same ;
         * pzm = -24 + (40.5-1)*1.2 = 23.4 */
        const GddInstance *sst = &g_map[1];
        CHECK_F("sector x", sst->pos[0], -23.4f, 1e-4f, "sector (1,1,1) x");
        CHECK_F("sector z", sst->pos[2], 23.4f, 1e-4f, "sector (1,1,1) z");
        CHECK_F("sector scale", sst->scale[0], 0.15f, 1e-5f, "sector scale");
        CHECK_F("sector color.r (sst)", sst->color[0], 1.0f, 1e-5f, "sst color r");
        CHECK_F("sector color.g (sst)", sst->color[1], 1.0f, 1e-5f, "sst color g");

        /* My quadrant: (-22.2, -22.2, 22.2), white */
        const GddInstance *me = &g_map[2];
        CHECK_F("my-q x", me->pos[0], -22.2f, 1e-4f, "my quadrant x");
        CHECK_F("my-q color", me->color[0], 1.0f, 1e-5f, "my quadrant white");
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

int main(void) {
    printf("GDD contract test\n");
    test_layout();
    test_flags();
    test_objects_mapping();
    test_effects();
    test_compass_and_jump();
    test_statics();
    test_galaxy_map();
    test_capacity_guard();
    printf("  %d checks passed, %d failed\n", g_pass, g_fail);
    if (g_fail == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
