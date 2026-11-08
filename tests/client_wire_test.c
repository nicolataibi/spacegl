/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * client_wire_test — the command deck's handling of server-supplied counts
 * (include/client_wire.h, used by src/spacegl_client.c), under ASan/UBSan;
 * since wire revision 7 also the UPD_ROUTE block (include/route_wire.h)
 * and its copy into shared memory (wire_route_to_shm).
 *
 * Before 2026.10.31.06 event_count and torpedo_count of UPD_EFFECTS were
 * clamped only from above and those of a full PKT_UPDATE not at all: a
 * negative value from a faulty or hostile server became a memcpy length of
 * ~16 EB, and the shared-memory loop "for (s = torpedo_count; s < MAX; s++)"
 * wrote before the torps[] array. Every state here lives on the heap, so
 * ASan reports any byte copied or written outside it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include "client_wire.h"
#include "effects_wire.h"
#include "route_wire.h"

typedef struct {
    const uint8_t *buf;
    size_t len, off;
} MemReader;
static bool mem_read(void *ctx, void *out, size_t n) {
    MemReader *r = ctx;
    if (r->len - r->off < n) return false;
    memcpy(out, r->buf + r->off, n);
    r->off += n;
    return true;
}

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s at %s:%d\n", #c, __FILE__, __LINE__); exit(1); } } while (0)

static const int32_t hostile[] = { INT32_MIN, -1000000, -1, 0, 1, 63, 64, 65, 255, 256, 257, 1023, 1024, 1025, INT32_MAX };
#define N_HOSTILE ((int)(sizeof(hostile) / sizeof(hostile[0])))

int main(void) {
    /* 1. clamp and range check */
    for (int i = 0; i < N_HOSTILE; i++) {
        int32_t v = hostile[i];
        int32_t c = wire_clamp_count(v, MAX_NET_EVENTS);
        CHECK(c >= 0 && c <= MAX_NET_EVENTS);
        if (v >= 0 && v <= MAX_NET_EVENTS) CHECK(c == v);
        CHECK(wire_count_ok(v, MAX_NET_OBJECTS) == (v >= 0 && v <= MAX_NET_OBJECTS));
    }
    printf("[1] wire_clamp_count / wire_count_ok on %d hostile values: OK\n", N_HOSTILE);

    /* 2. UPD_EFFECTS with hostile event and torpedo counts */
    PacketUpdate *st = malloc(sizeof(PacketUpdate));
    UpdateBlockEffects *b = malloc(sizeof(UpdateBlockEffects));
    CHECK(st && b);
    for (int i = 0; i < N_HOSTILE; i++) {
        for (int j = 0; j < N_HOSTILE; j++) {
            memset(st, 0, sizeof(PacketUpdate));
            memset(b, 0x5a, sizeof(UpdateBlockEffects));
            b->event_count = hostile[i];
            b->torpedo_count = hostile[j];
            wire_apply_effects(st, b);
            CHECK(st->event_count >= 0 && st->event_count <= MAX_NET_EVENTS);
            CHECK(st->torpedo_count >= 0 && st->torpedo_count <= MAX_VISIBLE_TORPEDOES);
        }
    }
    printf("[2] wire_apply_effects with %d x %d hostile counts: OK\n", N_HOSTILE, N_HOSTILE);

    /* 3. full PKT_UPDATE fixed part */
    for (int i = 0; i < N_HOSTILE; i++) {
        memset(st, 0, sizeof(PacketUpdate));
        st->object_count = hostile[i];
        st->beam_count = hostile[i];
        st->event_count = hostile[i];
        st->torpedo_count = hostile[i];
        wire_sanitize_update_counts(st);
        CHECK(st->object_count >= 0 && st->object_count <= MAX_NET_OBJECTS);
        CHECK(st->beam_count >= 0 && st->beam_count <= MAX_NET_BEAMS);
        CHECK(st->event_count >= 0 && st->event_count <= MAX_NET_EVENTS);
        CHECK(st->torpedo_count >= 0 && st->torpedo_count <= MAX_VISIBLE_TORPEDOES);

        /* the client's consumers, as written in spacegl_client.c */
        int *torps_active = calloc(MAX_VISIBLE_TORPEDOES, sizeof(int));
        CHECK(torps_active);
        for (int s = 0; s < st->torpedo_count && s < MAX_VISIBLE_TORPEDOES; s++) torps_active[s] = 1;
        for (int s = st->torpedo_count; s < MAX_VISIBLE_TORPEDOES; s++) torps_active[s] = 0;
        volatile int sum = 0;
        for (int e = 0; e < st->event_count; e++) sum += st->events[e].type;
        (void)sum;
        free(torps_active);
    }
    printf("[3] wire_sanitize_update_counts + the client's loops: OK\n");

    /* 4. the UPD_EFFECTS block on the wire (wire revision 6,
     * effects_wire.h): round trip, sizes, and announced counts out of range
     * refused (they cannot be skipped) */
    {
        UpdateBlockEffects *src = calloc(1, sizeof(UpdateBlockEffects));
        UpdateBlockEffects *dst = malloc(sizeof(UpdateBlockEffects));
        uint8_t *buf = malloc(EFFECTS_WIRE_MAX);
        CHECK(src && dst && buf);
        for (int ne = 0; ne <= MAX_NET_EVENTS; ne += 21) {
            for (int nt = 0; nt <= MAX_VISIBLE_TORPEDOES; nt += 85) {
                memset(src, 0, sizeof(UpdateBlockEffects));
                src->supernova_pos.active = 1;
                src->supernova_q[1] = 7;
                src->torps[3].net_y = 2.5;
                src->event_count = ne;
                for (int e = 0; e < ne; e++) src->events[e].type = e + 1;
                src->torpedo_count = nt;
                for (int k = 0; k < nt; k++) src->visible_torpedoes[k].id = k + 1;
                size_t len = effects_wire_write(src, buf);
                CHECK(len == effects_wire_size(src));
                CHECK(len == EFFECTS_WIRE_HEAD + (size_t)ne * sizeof(NetEvent) + 4 + (size_t)nt * sizeof(NetVisibleTorpedo));
                MemReader r = { buf, len, 0 };
                memset(dst, 0x77, sizeof(UpdateBlockEffects));
                CHECK(effects_wire_read(dst, mem_read, &r) == (long)len && r.off == len);
                CHECK(memcmp(src, dst, sizeof(UpdateBlockEffects)) == 0);   /* unused entries zeroed */
            }
        }
        CHECK(effects_wire_size(src) < EFFECTS_WIRE_MAX);
        memset(src, 0, sizeof(UpdateBlockEffects));
        CHECK(effects_wire_write(src, buf) == EFFECTS_WIRE_HEAD + 4);   /* empty: 212 bytes */
        for (int i = 0; i < N_HOSTILE; i++) {
            /* a hostile announced count: refused unless in range */
            memset(buf, 0, EFFECTS_WIRE_MAX);
            memcpy(buf + offsetof(UpdateBlockEffects, event_count), &hostile[i], 4);
            MemReader r = { buf, EFFECTS_WIRE_MAX, 0 };
            long got = effects_wire_read(dst, mem_read, &r);
            CHECK((got >= 0) == (hostile[i] >= 0 && hostile[i] <= MAX_NET_EVENTS));
            memset(buf, 0, EFFECTS_WIRE_MAX);
            memcpy(buf + EFFECTS_WIRE_HEAD, &hostile[i], 4);   /* torpedo_count after 0 events */
            r.off = 0;
            got = effects_wire_read(dst, mem_read, &r);
            CHECK((got >= 0) == (hostile[i] >= 0 && hostile[i] <= MAX_VISIBLE_TORPEDOES));
        }
        MemReader shortr = { buf, EFFECTS_WIRE_HEAD - 1, 0 };   /* a short read */
        CHECK(effects_wire_read(dst, mem_read, &shortr) < 0);
        free(src);
        free(dst);
        free(buf);
    }
    printf("[4] UPD_EFFECTS on the wire: round trip, sizes, %d hostile counts refused: OK\n", N_HOSTILE);

    /* 5. the UPD_ROUTE block (wire revision 7, route_wire.h): round trip,
     * sizes, announced counts out of range refused, and the copy into
     * shared memory (wire_route_to_shm) with hostile counts */
    {
        UpdateBlockRoute *src = calloc(1, sizeof(UpdateBlockRoute));
        UpdateBlockRoute *dst = malloc(sizeof(UpdateBlockRoute));
        SharedRoute *sr = malloc(sizeof(SharedRoute));
        uint8_t *buf = malloc(ROUTE_WIRE_MAX);
        CHECK(src && dst && sr && buf);
        for (int nw = 0; nw <= NAV_WIRE_MAX_WP; nw += 16) {
            for (int nh = 0; nh <= NAV_WIRE_MAX_HAZ; nh += 4) {
                memset(src, 0, sizeof(UpdateBlockRoute));
                src->seq = 42;
                src->active = 1;
                src->flags = 9;
                src->count = (uint8_t)nw;
                src->hazard_count = (uint8_t)nh;
                src->passed = 3;
                src->total = 7;
                src->length = 123.5f;
                src->direct = 120.25f;
                for (int k = 0; k < nw; k++) {
                    src->wp[k][0] = 100.0f + k;
                    src->wp[k][1] = 200.0f - k;
                    src->wp[k][2] = 0.5f * k;
                }
                for (int k = 0; k < nh; k++) {
                    src->hazards[k].x = 1.0f + k;
                    src->hazards[k].r = 2.8f;
                    src->hazards[k].kind = k + 1;
                }
                size_t len = route_wire_write(src, buf);
                CHECK(len == route_wire_size(src));
                CHECK(len == ROUTE_WIRE_HEAD + (size_t)nw * 12 + (size_t)nh * sizeof(NetRouteHazard));
                MemReader r = { buf, len, 0 };
                memset(dst, 0x77, sizeof(UpdateBlockRoute));
                CHECK(route_wire_read(dst, mem_read, &r) == (long)len && r.off == len);
                CHECK(memcmp(src, dst, sizeof(UpdateBlockRoute)) == 0);   /* unused entries zeroed */
                memset(sr, 0x55, sizeof(SharedRoute));
                wire_route_to_shm(sr, dst);
                CHECK(sr->seq == 42 && sr->active == 1 && sr->flags == 9 && sr->passed == 3 && sr->total == 7);
                CHECK(sr->count == nw && sr->hazard_count == nh);
                CHECK(sr->length == 123.5 && sr->direct == 120.25);
                for (int k = 0; k < nw; k++)
                    CHECK(sr->wp[k][0] == 100.0 + k && sr->wp[k][1] == 200.0 - k && sr->wp[k][2] == 0.5 * k);
                for (int k = 0; k < nh; k++)
                    CHECK(sr->hazards[k].x == 1.0 + k && sr->hazards[k].r == (double)2.8f && sr->hazards[k].kind == k + 1);
            }
        }
        memset(src, 0, sizeof(UpdateBlockRoute));
        CHECK(route_wire_write(src, buf) == ROUTE_WIRE_HEAD);   /* no course: 20 bytes */
        for (int v = 0; v < 256; v += 5) {
            /* hostile announced counts: refused unless in range */
            memset(buf, 0, ROUTE_WIRE_MAX);
            buf[offsetof(UpdateBlockRoute, count)] = (uint8_t)v;
            MemReader r = { buf, ROUTE_WIRE_MAX, 0 };
            CHECK((route_wire_read(dst, mem_read, &r) >= 0) == (v <= NAV_WIRE_MAX_WP));
            memset(buf, 0, ROUTE_WIRE_MAX);
            buf[offsetof(UpdateBlockRoute, hazard_count)] = (uint8_t)v;
            r.off = 0;
            CHECK((route_wire_read(dst, mem_read, &r) >= 0) == (v <= NAV_WIRE_MAX_HAZ));
            /* and the shared-memory copy of a block that skipped the reader */
            memset(src, 0, sizeof(UpdateBlockRoute));
            src->count = (uint8_t)v;
            src->hazard_count = (uint8_t)v;
            wire_route_to_shm(sr, src);
            CHECK(sr->count >= 0 && sr->count <= SHM_ROUTE_MAX_WP);
            CHECK(sr->hazard_count >= 0 && sr->hazard_count <= SHM_ROUTE_MAX_HAZ);
        }
        MemReader shortr = { buf, ROUTE_WIRE_HEAD - 1, 0 };   /* a short read */
        CHECK(route_wire_read(dst, mem_read, &shortr) < 0);
        free(src);
        free(dst);
        free(sr);
        free(buf);
    }
    printf("[5] UPD_ROUTE on the wire and into shared memory: round trip, sizes, hostile counts: OK\n");

    free(st); free(b);
    printf("client_wire_test: all checks passed\n");
    return 0;
}
