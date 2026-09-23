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
    /* ship is Alliance (fac 0): +4 quantum instances (core + 3 rings) */
    CHECK(g_dyn_n == 23, "3 objects + quantum(4) + 16 bbox edges -> %u dyn instances", g_dyn_n);

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

    /* Planet (30,10,5) -> (10, -15, 15) */
    const GddInstance *planet = find_dyn(GDD_MESH_SPHERE, 10.0f, -15.0f, 15.0f, 1e-4f);
    CHECK(planet != NULL, "planet at mapped position (10,-15,15)");
    if (planet)
        CHECK_F("planet scale", planet->scale[0], 1.8f, 1e-5f, "planet scale");

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

    /* ship (1+4 quantum) + boom (1 cloud) + beam (3) + torp (1) + 16 bbox */
    CHECK(g_dyn_n == 26, "effects: ship+quantum+boom+beam+torp+16bbox -> %u dyn (want 26)", g_dyn_n);

    /* Boom: exactly 1 GDD_MESH_BOOM instance at (5,5,5); the GPU expands
     * the 256-particle cloud from seed/style (pad.x/pad.y). */
    CHECK(count_dyn(GDD_MESH_BOOM) == 1, "1 boom cloud instance, got %d",
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

    /* Dismantle with life < 0.7: only the flash is skipped (CPU parity),
     * core + aura are still drawn -> 26 + 2 */
    static ActiveDismantle dm[GDD_MAX_ACTIVE_DISMANTLES];
    memset(dm, 0, sizeof(dm));
    dm[0].x = 9.0f; dm[0].life = 0.5f;
    c.dismantles = dm;
    run_builder(&c);
    CHECK(g_dyn_n == 28, "dead dismantle: flash skipped, core+aura drawn (%u)", g_dyn_n);
    dm[0].life = 0.9f;
    run_builder(&c);
    CHECK(g_dyn_n == 29, "live dismantle adds 3 spheres (%u)", g_dyn_n);
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
            if ((int)(it->mesh + 0.5f) != GDD_MESH_SPHERE) continue;
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
    c.camera_dist = 150.0f;
    run_builder(&c);
    CHECK(g_dyn_n == 21, "compass hidden at dist>=150, bbox present (%u)", g_dyn_n);
    c.camera_dist = 80.0f;

    /* Jump arrival: only object 0 (the ship + its quantum core) is drawn */
    static JumpState jump;
    memset(&jump, 0, sizeof(jump));
    jump.active = 1; jump.timer = 500;
    c.jump_arrival = &jump;
    run_builder(&c);
    CHECK(g_dyn_n == 8, "jump arrival: ship + quantum(4) + arrival glow + wormhole(2) = %u", g_dyn_n);
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
