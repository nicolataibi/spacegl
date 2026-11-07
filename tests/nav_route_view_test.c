/*
 * nav_route_view_test.c - contract of include/nav_route_view.h, the look of
 * the `nav` course shared by the three 3D views.
 *
 * (a) no course, no primitive; (b) a course that ends in the quadrant ends
 * there with a beacon, one that leaves it is cut on the face it crosses,
 * with the next quadrant named (the sector -> viewer axis swap included);
 * (c) the rounded course stays close to the waypoints and has one marker per
 * waypoint on screen; (d) only the keep-out bubbles that reach into the
 * quadrant are drawn; (e) every primitive is finite, inside the cube (plus
 * the gate tunnel), with a unit normal for the rings and alpha in (0, 1];
 * (f) a course that stops short ends in red; (g) the budget holds with 64
 * waypoints and 16 hazards; (h) the pulses move with time.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "nav_route_view.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, ...) \
    do { \
        if (cond) g_pass++; \
        else { \
            g_fail++; \
            printf("  FAIL: "); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

static NavRouteView V;

static void base_input(NrvInput *in) {
    memset(in, 0, sizeof(*in));
    in->active = 1;
    in->q[0] = 5;
    in->q[1] = 6;
    in->q[2] = 7;
    in->ship[0] = 20;
    in->ship[1] = 50;
    in->ship[2] = 50;
    in->time = 1.25f;
}

/* galactic coordinates of a sector point of quadrant q */
static void gal(const NrvInput *in, float sx, float sy, float sz, float out[3]) {
    out[0] = (in->q[0] - 1) * 100.0f + sx;
    out[1] = (in->q[1] - 1) * 100.0f + sy;
    out[2] = (in->q[2] - 1) * 100.0f + sz;
}

static int prims_sane(const NavRouteView *v, float bound) {
    for (int k = 0; k < v->np; k++) {
        const NrvPrim *p = &v->prim[k];
        for (int a = 0; a < 3; a++) {
            if (!isfinite(p->a[a]) || !isfinite(p->b[a])) return 0;
            if (fabsf(p->a[a]) > bound) return 0;
            if ((p->type == NRV_TUBE || p->type == NRV_LINE) && fabsf(p->b[a]) > bound) return 0;
        }
        if (!(p->c[3] > 0.0f && p->c[3] <= 1.0f)) return 0;
        if (p->type == NRV_RING && fabsf(nrv_len3(p->b) - 1.0f) > 1e-3f) return 0;
        if (p->type < NRV_TUBE || p->type > NRV_DOT) return 0;
    }
    return 1;
}

int main(void) {
    printf("nav_route_view_test\n");
    NrvInput in;

    /* a. nothing */
    base_input(&in);
    in.active = 0;
    CHECK(nrv_build(&V, &in) == 0, "inactive course: no primitive");
    base_input(&in);
    in.count = 0;
    CHECK(nrv_build(&V, &in) == 0, "empty course: no primitive");

    /* b1. ends in the quadrant */
    base_input(&in);
    in.count = 1;
    gal(&in, 80, 50, 50, in.wp[0]);
    int np = nrv_build(&V, &in);
    CHECK(np > 20, "straight course: primitives (%d)", np);
    CHECK(V.ends_here && !V.exits, "straight course: ends on screen");
    CHECK(fabsf(V.end_p[0] - 30.0f) < 1e-3f && fabsf(V.end_p[1]) < 1e-3f && fabsf(V.end_p[2]) < 1e-3f,
          "end in viewer coordinates (%.2f %.2f %.2f)", V.end_p[0], V.end_p[1], V.end_p[2]);
    CHECK(fabsf(V.length - 60.0f) < 1e-2f, "drawn length %.3f", V.length);
    CHECK(prims_sane(&V, 60.0f), "straight course: primitives sane");

    /* b2. leaves through sector +x */
    base_input(&in);
    in.count = 1;
    gal(&in, 160, 50, 50, in.wp[0]);
    nrv_build(&V, &in);
    CHECK(V.exits && !V.ends_here && V.exit_axis == 0 && V.exit_sign == 1, "exit +x");
    CHECK(fabsf(V.exit_p[0] - 50.0f) < 1e-3f, "exit on the +x face (%.3f)", V.exit_p[0]);
    CHECK(V.exit_q[0] == 6 && V.exit_q[1] == 6 && V.exit_q[2] == 7, "next quadrant [%d,%d,%d]", V.exit_q[0],
          V.exit_q[1], V.exit_q[2]);
    CHECK(prims_sane(&V, 60.0f), "exit +x: primitives sane");

    /* b3. leaves through sector +y: viewer -z */
    base_input(&in);
    in.ship[0] = 50;
    in.count = 1;
    gal(&in, 50, 130, 50, in.wp[0]);
    nrv_build(&V, &in);
    CHECK(V.exits && V.exit_axis == 1 && V.exit_sign == 1 && fabsf(V.exit_p[2] + 50.0f) < 1e-3f,
          "exit +y -> viewer z = -50 (%.3f)", V.exit_p[2]);
    CHECK(V.exit_q[1] == 7, "next quadrant along y (%d)", V.exit_q[1]);

    /* b4. leaves through sector -z: viewer -y (the floor) */
    base_input(&in);
    in.count = 1;
    gal(&in, 30, 50, -40, in.wp[0]);
    nrv_build(&V, &in);
    CHECK(V.exits && V.exit_axis == 2 && V.exit_sign == -1 && fabsf(V.exit_p[1] + 50.0f) < 1e-3f,
          "exit -z -> viewer y = -50 (%.3f)", V.exit_p[1]);

    /* c. corners: two waypoints, then the end */
    base_input(&in);
    in.count = 3;
    gal(&in, 40, 50, 50, in.wp[0]);
    gal(&in, 50, 60, 52, in.wp[1]);
    gal(&in, 80, 60, 52, in.wp[2]);
    nrv_build(&V, &in);
    CHECK(V.n_wp == 2, "corner markers: %d (2 expected)", V.n_wp);
    float c1[3], s1[3] = { 40, 50, 50 };
    nrv_to_view(s1, c1);
    float dmin = 1e9f;
    for (int k = 0; k < V.n; k++) {
        float d = nrv_dist(V.p[k], c1);
        if (d < dmin) dmin = d;
    }
    CHECK(dmin < 1.0f, "rounded corner within 1 unit of the waypoint (%.3f)", dmin);
    CHECK(dmin > 0.05f, "the corner is rounded, not sharp (%.3f)", dmin);
    CHECK(V.ends_here, "corners: ends on screen");

    /* d. bubbles: one in, one far away */
    base_input(&in);
    in.count = 1;
    gal(&in, 80, 50, 50, in.wp[0]);
    in.n_haz = 2;
    gal(&in, 50, 50, 50, in.haz[0]);
    in.haz[0][3] = 2.0f;
    in.haz_kind[0] = 1;
    gal(&in, 250, 50, 50, in.haz[1]);
    in.haz[1][3] = 2.0f;
    in.haz_kind[1] = 1;
    nrv_build(&V, &in);
    CHECK(V.n_haz == 1, "only the bubble in the quadrant (%d)", V.n_haz);
    int rings_at = 0;
    for (int k = 0; k < V.np; k++)
        if (V.prim[k].type == NRV_RING && fabsf(V.prim[k].r - 2.0f) < 1e-4f && nrv_len3(V.prim[k].a) < 1e-3f) rings_at++;
    CHECK(rings_at >= 3, "the bubble has its three great circles (%d)", rings_at);

    /* f. stops short: red end */
    base_input(&in);
    in.count = 1;
    in.flags = NRV_FLAG_PARTIAL;
    gal(&in, 70, 50, 50, in.wp[0]);
    nrv_build(&V, &in);
    int red = 0;
    for (int k = 0; k < V.np; k++) {
        const NrvPrim *p = &V.prim[k];
        if (p->type == NRV_TUBE && p->c[0] > 0.9f && p->c[1] < 0.3f && nrv_dist(p->a, V.end_p) < 1.5f) red++;
    }
    CHECK(V.partial && red >= 4, "stop mark in red (%d tubes)", red);

    /* g. budget: 64 waypoints zig-zagging, 16 bubbles */
    base_input(&in);
    in.ship[0] = 5;
    in.count = 64;
    for (int k = 0; k < 64; k++) gal(&in, 6 + k * 1.4f, 50 + ((k & 1) ? 3 : -3), 50 + ((k & 2) ? 2 : -2), in.wp[k]);
    in.n_haz = 16;
    for (int k = 0; k < 16; k++) {
        gal(&in, 10 + k * 5, 50, 45, in.haz[k]);
        in.haz[k][3] = 2.2f;
        in.haz_kind[k] = (k == 3) ? 7 : 4;
    }
    np = nrv_build(&V, &in);
    CHECK(np > 0 && np <= NRV_MAX_PRIMS, "64 waypoints + 16 bubbles within the budget (%d of %d)", np, NRV_MAX_PRIMS);
    int cores = 0, gate_or_end = 0;
    for (int k = 0; k < V.np; k++) {
        if (V.prim[k].type == NRV_TUBE && fabsf(V.prim[k].r - 0.055f) < 1e-6f) cores++;
        if (V.prim[k].type == NRV_RING && fabsf(V.prim[k].r - 1.25f) < 1e-6f) gate_or_end++;
    }
    CHECK(cores == V.n - 1, "zig-zag: the whole course is drawn first (%d of %d legs)", cores, V.n - 1);
    CHECK(gate_or_end == 1, "zig-zag: the end beacon is drawn");
    CHECK(V.n_wp >= 60, "zig-zag: markers (%d)", V.n_wp);
    CHECK(prims_sane(&V, 60.0f), "zig-zag: primitives sane");

    /* h. animation */
    base_input(&in);
    in.count = 1;
    gal(&in, 80, 50, 50, in.wp[0]);
    nrv_build(&V, &in);
    float dot0 = 0.0f;
    for (int k = 0; k < V.np; k++)
        if (V.prim[k].type == NRV_DOT && V.prim[k].r < 0.2f) {
            dot0 = V.prim[k].a[0];
            break;
        }
    in.time += 0.1f;
    nrv_build(&V, &in);
    float dot1 = 0.0f;
    for (int k = 0; k < V.np; k++)
        if (V.prim[k].type == NRV_DOT && V.prim[k].r < 0.2f) {
            dot1 = V.prim[k].a[0];
            break;
        }
    CHECK(fabsf(dot1 - dot0) > 0.3f, "pulses move with time (%.3f -> %.3f)", dot0, dot1);

    printf("nav_route_view_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
