/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * client_wire_test — the command deck's handling of server-supplied counts
 * (include/client_wire.h, used by src/spacegl_client.c), under ASan/UBSan.
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

    free(st); free(b);
    printf("client_wire_test: all checks passed\n");
    return 0;
}
