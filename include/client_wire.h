/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * client_wire.h — how the command deck applies network-supplied counts and
 * blocks to its PacketUpdate state (src/spacegl_client.c). Kept apart from
 * the listener thread so tests/client_wire_test.c can drive it directly
 * under ASan/UBSan with hostile values.
 *
 * Two kinds of counts arrive from the server:
 *  - counts inside a FIXED-size block (UpdateBlockEffects.event_count and
 *    .torpedo_count, the fixed part of PKT_UPDATE): the block size does not
 *    depend on them, so a bad value cannot desynchronize the stream; it is
 *    clamped to [0, MAX] before it is used as a copy length or loop bound.
 *    (2026.10.31.06: they were clamped only from above, so a negative value
 *    became a memcpy of ~16 EB, or a loop that wrote before the shared
 *    memory torps[] array.)
 *  - counts that announce how many records FOLLOW on the wire (objects,
 *    kinematics, beams, LRS objects): a value outside [0, MAX] cannot be
 *    honoured without losing the stream alignment, so it is a protocol
 *    error and the listener closes the connection (wire_count_ok()).
 */
#ifndef CLIENT_WIRE_H
#define CLIENT_WIRE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "network.h"
#include "shared_state.h"

static inline int32_t wire_clamp_count(int32_t count, int32_t max) {
    if (count < 0) return 0;
    if (count > max) return max;
    return count;
}

/* A count that announces trailing records: in range or protocol error. */
static inline bool wire_count_ok(int32_t count, int32_t max) {
    return count >= 0 && count <= max;
}

/* Fixed-part counts of a full PKT_UPDATE (and of the state built from
 * deltas): clamp everything that is later used as a length or a bound. */
static inline void wire_sanitize_update_counts(PacketUpdate *st) {
    st->object_count  = wire_clamp_count(st->object_count, MAX_NET_OBJECTS);
    st->beam_count    = wire_clamp_count(st->beam_count, MAX_NET_BEAMS);
    st->event_count   = wire_clamp_count(st->event_count, MAX_NET_EVENTS);
    st->torpedo_count = wire_clamp_count(st->torpedo_count, MAX_VISIBLE_TORPEDOES);
}

/* UPD_EFFECTS block -> state. */
static inline void wire_apply_effects(PacketUpdate *st, const UpdateBlockEffects *b) {
    st->supernova_pos = b->supernova_pos;
    memcpy(st->supernova_q, b->supernova_q, sizeof(b->supernova_q));
    memcpy(st->torps, b->torps, sizeof(b->torps));
    st->wormhole = b->wormhole;
    st->event_count = wire_clamp_count(b->event_count, MAX_NET_EVENTS);
    memcpy(st->events, b->events, (size_t)st->event_count * sizeof(NetEvent));
    st->torpedo_count = wire_clamp_count(b->torpedo_count, MAX_VISIBLE_TORPEDOES);
    memcpy(st->visible_torpedoes, b->visible_torpedoes,
           (size_t)st->torpedo_count * sizeof(NetVisibleTorpedo));
}

/* UPD_ROUTE block -> shared memory for the 3D views (wire revision 7, the
 * `nav` course): the announced counts were checked by route_wire_read(),
 * clamped again here because they become loop bounds over fixed arrays. */
static inline void wire_route_to_shm(SharedRoute *sr, const UpdateBlockRoute *rb) {
    sr->seq = rb->seq;
    sr->active = rb->active;
    sr->flags = rb->flags;
    sr->count = wire_clamp_count(rb->count, SHM_ROUTE_MAX_WP);
    sr->passed = rb->passed;
    sr->total = rb->total;
    sr->length = rb->length;
    sr->direct = rb->direct;
    for (int k = 0; k < sr->count; k++)
        for (int a = 0; a < 3; a++) sr->wp[k][a] = rb->wp[k][a];
    sr->hazard_count = wire_clamp_count(rb->hazard_count, SHM_ROUTE_MAX_HAZ);
    for (int k = 0; k < sr->hazard_count; k++) {
        sr->hazards[k].x = rb->hazards[k].x;
        sr->hazards[k].y = rb->hazards[k].y;
        sr->hazards[k].z = rb->hazards[k].z;
        sr->hazards[k].r = rb->hazards[k].r;
        sr->hazards[k].kind = rb->hazards[k].kind;
    }
}

#endif
