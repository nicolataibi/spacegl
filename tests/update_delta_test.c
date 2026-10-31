/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * update_delta_test.c — the REAL send_optimized_update() (src/server/net.c)
 * on a socketpair, decoded by a reference reader of the delta packet
 * (PROTOCOL.md section 6) and objdelta_apply() (2026.10.31.30).
 *
 * The server's object list changes over 900 ticks (motion and attitude of
 * some objects every tick, a field of the captain's own entry every tick as
 * in a real session, objects appearing and leaving, a scene that changes
 * everywhere). After every packet the deck's list must equal the server's
 * (motion to float precision), and in a quadrant with more than a handful
 * of objects most ticks must travel as UPD_OBJDELTA, not as the full list.
 * A one-object quadrant must use the full list (the delta would not be
 * smaller).
 *
 * Exit: 0 pass, 1 fail.
 */
#include "server_internal.h"
#include "conn.h"
#include "obj_delta.h"

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* --- what net.c needs from the rest of the server ---------------------- */
ConnectedPlayer players[MAX_CLIENTS];
SpaceGLGame spacegl_master;
int g_debug = 0;
SpaceglIOMode g_io_mode = SPACEGL_IO_LEGACY;
threadpool_t *g_pool = NULL;
pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
int threadpool_add_task(threadpool_t *pool, void (*fn)(void *), void *arg) { (void)pool; (void)fn; (void)arg; return -1; }
void ensure_player_algo_key(int p_idx, int k, bool private_mode) { (void)p_idx; (void)k; (void)private_mode; }
void slog(const char *fmt, ...) { (void)fmt; }

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

static int g_rx = -1;
static bool rx_exact(void *ctx, void *buf, size_t len) {
    (void)ctx;
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(g_rx, (char *)buf + got, len - got, 0);
        if (n <= 0) { if (n < 0 && errno == EINTR) continue; return false; }
        got += (size_t)n;
    }
    return true;
}
static bool rx_skip(size_t len) {
    uint8_t tmp[4096];
    while (len) {
        size_t k = len < sizeof(tmp) ? len : sizeof(tmp);
        if (!rx_exact(NULL, tmp, k)) return false;
        len -= k;
    }
    return true;
}
static bool rx_ready(void) {
    struct pollfd p = { .fd = g_rx, .events = POLLIN };
    return poll(&p, 1, 0) > 0;
}

static NetObject deck[MAX_NET_OBJECTS];
static int32_t deck_n = 0;
static int g_full = 0, g_delta = 0, g_packets = 0;
static size_t g_obj_bytes = 0;

/* Reads one PKT_UPDATE_DELTA; false on a protocol error. */
static bool read_packet(void) {
    int32_t type; int64_t frame; uint64_t mask;
    if (!rx_exact(NULL, &type, 4) || !rx_exact(NULL, &frame, 8) || !rx_exact(NULL, &mask, 8)) return false;
    if (type != PKT_UPDATE_DELTA) return false;
    g_packets++;
    if ((mask & UPD_TRANSFORM) && !rx_skip(sizeof(UpdateBlockTransform))) return false;
    if ((mask & UPD_VITALS) && !rx_skip(sizeof(UpdateBlockVitals))) return false;
    if ((mask & UPD_SHIELDS) && !rx_skip(sizeof(UpdateBlockShields))) return false;
    if ((mask & UPD_SYSTEMS) && !rx_skip(sizeof(UpdateBlockSystems))) return false;
    if ((mask & UPD_INTERNAL) && !rx_skip(sizeof(UpdateBlockInternal))) return false;
    if (mask & UPD_COMBAT) {
        int32_t bc;
        if (!rx_skip(sizeof(UpdateBlockCombat)) || !rx_exact(NULL, &bc, 4) || bc < 0 || bc > MAX_NET_BEAMS) return false;
        if (!rx_skip((size_t)bc * sizeof(NetBeam))) return false;
    }
    if ((mask & UPD_FLAGS) && !rx_skip(sizeof(UpdateBlockFlags))) return false;
    if ((mask & UPD_EFFECTS) && !rx_skip(sizeof(UpdateBlockEffects))) return false;
    if ((mask & UPD_PROBES) && !rx_skip(sizeof(UpdateBlockProbes))) return false;
    if (mask & UPD_OBJECTS) {
        int32_t oc;
        if (!rx_exact(NULL, &oc, 4) || oc < 0 || oc > MAX_NET_OBJECTS) return false;
        if (oc && !rx_exact(NULL, deck, (size_t)oc * sizeof(NetObject))) return false;
        deck_n = oc;
        g_full++;
        g_obj_bytes += 4 + (size_t)oc * sizeof(NetObject);
    } else if (mask & UPD_OBJDELTA) {
        long used = objdelta_apply(deck, &deck_n, rx_exact, NULL);
        if (used < 0) return false;
        g_delta++;
        g_obj_bytes += (size_t)used;
    }
    if ((mask & UPD_MAP) && !rx_skip(sizeof(UpdateBlockMap))) return false;
    return true;
}

static bool deck_matches(const PacketUpdate *u) {
    if (deck_n != u->object_count) return false;
    for (int32_t i = 0; i < deck_n; i++) {
        const NetObject *a = &deck[i], *b = &u->objects[i];
        if (!objdelta_meta_equal(a, b)) return false;
        const double x[9] = { a->net_x, a->net_y, a->net_z, a->vx, a->vy, a->vz, a->h, a->m, a->r };
        const double y[9] = { b->net_x, b->net_y, b->net_z, b->vx, b->vy, b->vz, b->h, b->m, b->r };
        for (int k = 0; k < 9; k++) if (x[k] != y[k] && x[k] != (double)(float)y[k]) return false;
    }
    return true;
}

static void make_obj(NetObject *o, int id, int type) {
    memset(o, 0, sizeof(*o));
    o->net_x = 10 + id % 80; o->net_y = 20 + id % 60; o->net_z = 30 + id % 50;
    o->type = type; o->active = 1; o->health_pct = 100; o->energy = 5000; o->id = id;
    snprintf(o->name, sizeof(o->name), "Object %d", id);
}

/* One tick: the server sends, the deck reads whatever arrived. */
static bool tick(PacketUpdate *u) {
    spacegl_master.frame_id++;
    u->frame_id = (int64_t)spacegl_master.frame_id;
    send_optimized_update(0, u);
    while (rx_ready()) if (!read_packet()) return false;
    return true;
}

int main(void) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) { perror("socketpair"); return 1; }
    int big = 4 * 1024 * 1024;
    setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &big, sizeof(big));
    setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &big, sizeof(big));
    g_rx = sv[1];
    ConnectedPlayer *p = &players[0];
    pthread_mutex_init(&p->socket_mutex, NULL);
    p->active = 1; p->socket = sv[0];
    snprintf(p->name, sizeof(p->name), "DELTA");
    PacketUpdate *u = &p->upd_packet;
    u->q1 = u->q2 = u->q3 = 7;

    /* 1. a busy quadrant: the captain + 120 objects */
    u->object_count = 121;
    make_obj(&u->objects[0], 1, 1);
    for (int i = 1; i < 121; i++) make_obj(&u->objects[i], 1000 + i, 10 + i % 40);
    int bad = 0, errors = 0;
    for (int t = 0; t < 900; t++) {
        u->objects[0].energy -= 3;                                /* the captain, every tick */
        for (int k = 0; k < 6; k++) {                             /* a few NPCs move and turn */
            NetObject *o = &u->objects[1 + (t * 7 + k * 13) % 120];
            o->net_x += 0.05; o->vx = 0.05; o->h = fmod(o->h + 2.0, 360.0); o->r = sin(t * 0.1) * 20.0;
        }
        if (t % 37 == 0) u->objects[60].health_pct = 100 - (t / 37) % 50;   /* combat damage */
        if (t % 50 == 10 && u->object_count < 200)                           /* arrivals */
            make_obj(&u->objects[u->object_count], 5000 + t, 12), u->object_count++;
        if (t % 50 == 30 && u->object_count > 100) u->object_count--;      /* departures */
        if (t == 600) for (int i = 1; i < u->object_count; i++) u->objects[i].energy += 1;  /* everything */
        if (!tick(u)) { errors++; break; }
        if (!deck_matches(u)) bad++;
    }
    CHECK(errors == 0, "busy quadrant: protocol error in the packet stream");
    CHECK(bad == 0, "busy quadrant: the deck differed from the server on %d ticks", bad);
    CHECK(g_delta > 800, "busy quadrant: only %d of %d packets used UPD_OBJDELTA (full %d)", g_delta, g_packets, g_full);
    CHECK(g_full >= 2, "busy quadrant: the first packet and the scene that changed everywhere are full lists (%d)", g_full);
    size_t rev2 = (size_t)900 * (4 + 121 * sizeof(NetObject));   /* revision 2: the captain changes every tick */
    printf("  busy quadrant: %d packets, %d delta, %d full, objects %zu bytes (revision 2: about %zu)\n",
           g_packets, g_delta, g_full, g_obj_bytes, rev2);
    CHECK(g_obj_bytes * 20 < rev2, "the object blocks are not 20 times smaller than with revision 2");

    /* 2. a one-object quadrant: the full list is the smaller encoding */
    g_full = g_delta = 0;
    u->q1 = 8;                                    /* a quadrant change: full list */
    u->object_count = 1;
    make_obj(&u->objects[0], 1, 1);
    for (int t = 0; t < 30; t++) {
        u->objects[0].energy -= 3;
        if (!tick(u) || !deck_matches(u)) { errors++; break; }
    }
    CHECK(errors == 0, "one-object quadrant: the deck follows the server");
    CHECK(g_delta == 0 && g_full == 30, "one-object quadrant: %d full, %d delta (the full list is smaller)", g_full, g_delta);

    /* 3. nothing changes: nothing is sent */
    int before = g_packets;
    for (int t = 0; t < 10; t++) if (!tick(u)) errors++;
    CHECK(g_packets == before, "an unchanged state sends no packet (%d sent)", g_packets - before);

    close(sv[0]); close(sv[1]);
    printf("update_delta: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
