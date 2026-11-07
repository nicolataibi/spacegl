/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * route_wire.h — the UPD_ROUTE block on the wire (wire revision 7,
 * 2026.11.07.09), written by the server (src/server/net.c) and read by the
 * deck (src/spacegl_client.c): the collision-free course of `nav`
 * (include/nav_route.h, include/nav_autopilot.h).
 *
 *     the fixed head (seq, active, flags, counts, lengths)   ROUTE_WIRE_HEAD (20)
 *     wp[count]                                              count * 12
 *     hazards[hazard_count]                                  hazard_count * 20
 *
 * The two counts announce what follows, so a count out of range cannot be
 * skipped: the reader reports a protocol error and the deck closes the
 * link, like for the other announced counts (include/client_wire.h).
 */
#ifndef ROUTE_WIRE_H
#define ROUTE_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "network.h"

#define ROUTE_WIRE_HEAD (offsetof(UpdateBlockRoute, wp))
_Static_assert(offsetof(UpdateBlockRoute, wp) == 20, "UPD_ROUTE head is 20 bytes on the wire");
_Static_assert(sizeof(NetRouteHazard) == 20, "NetRouteHazard is 20 bytes on the wire");

#define ROUTE_WIRE_MAX (ROUTE_WIRE_HEAD + sizeof(((UpdateBlockRoute *)0)->wp) + \
                        sizeof(((UpdateBlockRoute *)0)->hazards))

static inline uint8_t route_wire_count(uint8_t c, int max) {
    return (c > max) ? (uint8_t)max : c;
}

/* Bytes the block takes on the wire. */
static inline size_t route_wire_size(const UpdateBlockRoute *b) {
    return ROUTE_WIRE_HEAD + (size_t)route_wire_count(b->count, NAV_WIRE_MAX_WP) * sizeof(b->wp[0]) +
           (size_t)route_wire_count(b->hazard_count, NAV_WIRE_MAX_HAZ) * sizeof(NetRouteHazard);
}

/* Writes the block at out (room for ROUTE_WIRE_MAX bytes); returns the
 * bytes written. Counts out of range are clamped. */
static inline size_t route_wire_write(const UpdateBlockRoute *b, uint8_t *out) {
    UpdateBlockRoute h;
    memcpy(&h, b, ROUTE_WIRE_HEAD);
    h.count = route_wire_count(b->count, NAV_WIRE_MAX_WP);
    h.hazard_count = route_wire_count(b->hazard_count, NAV_WIRE_MAX_HAZ);
    uint8_t *p = out;
    memcpy(p, &h, ROUTE_WIRE_HEAD);
    p += ROUTE_WIRE_HEAD;
    memcpy(p, b->wp, (size_t)h.count * sizeof(b->wp[0]));
    p += (size_t)h.count * sizeof(b->wp[0]);
    memcpy(p, b->hazards, (size_t)h.hazard_count * sizeof(NetRouteHazard));
    p += (size_t)h.hazard_count * sizeof(NetRouteHazard);
    return (size_t)(p - out);
}

/* Reader: exactly len bytes into buf, false on a short read. */
typedef bool (*route_read_fn)(void *ctx, void *buf, size_t len);

/* Reads one block into b (unused entries zeroed). Returns the bytes
 * consumed, or -1 on a short read or a count out of range. */
static inline long route_wire_read(UpdateBlockRoute *b, route_read_fn rd, void *ctx) {
    memset(b, 0, sizeof(*b));
    if (!rd(ctx, b, ROUTE_WIRE_HEAD)) return -1;
    if (b->count > NAV_WIRE_MAX_WP || b->hazard_count > NAV_WIRE_MAX_HAZ) return -1;
    size_t w = (size_t)b->count * sizeof(b->wp[0]);
    if (w && !rd(ctx, b->wp, w)) return -1;
    size_t hz = (size_t)b->hazard_count * sizeof(NetRouteHazard);
    if (hz && !rd(ctx, b->hazards, hz)) return -1;
    return (long)(ROUTE_WIRE_HEAD + w + hz);
}

#endif
