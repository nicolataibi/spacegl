/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * effects_wire.h — the UPD_EFFECTS block on the wire (wire revision 6,
 * 2026.11.01.14), written by the server (src/server/net.c) and read by the
 * deck (src/spacegl_client.c).
 *
 * Up to revision 5 the block went out as the whole UpdateBlockEffects,
 * 18 132 bytes, whatever it held: room for 64 events and 256 visible
 * torpedoes, and a typical block carries 0.3 events and no torpedo. The
 * server's traffic report measured it at 23% of all the bytes sent to the
 * decks, from one block every three seconds. Now only what is used:
 *
 *     the fixed part (supernova, its quadrant, the 4 own torpedoes, the
 *     wormhole) and event_count      EFFECTS_WIRE_HEAD bytes (208)
 *     events[event_count]            event_count * sizeof(NetEvent)
 *     torpedo_count                  4
 *     visible_torpedoes[torpedo_count]
 *
 * The two counts announce what follows, so a count outside [0, MAX] cannot
 * be skipped: the reader reports a protocol error (the deck closes the
 * link, as for the other announced counts, include/client_wire.h).
 */
#ifndef EFFECTS_WIRE_H
#define EFFECTS_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "network.h"

#define EFFECTS_WIRE_HEAD (offsetof(UpdateBlockEffects, events))   /* through event_count */
#define EFFECTS_WIRE_MAX \
    (EFFECTS_WIRE_HEAD + (size_t)MAX_NET_EVENTS * sizeof(NetEvent) + sizeof(int32_t) + \
     (size_t)MAX_VISIBLE_TORPEDOES * sizeof(NetVisibleTorpedo))

/* Bytes the block takes on the wire (counts clamped as the writer does). */
static inline size_t effects_wire_size(const UpdateBlockEffects *b) {
    int32_t ne = b->event_count < 0 ? 0 : (b->event_count > MAX_NET_EVENTS ? MAX_NET_EVENTS : b->event_count);
    int32_t nt = b->torpedo_count < 0 ? 0
                                      : (b->torpedo_count > MAX_VISIBLE_TORPEDOES ? MAX_VISIBLE_TORPEDOES : b->torpedo_count);
    return EFFECTS_WIRE_HEAD + (size_t)ne * sizeof(NetEvent) + sizeof(int32_t) +
           (size_t)nt * sizeof(NetVisibleTorpedo);
}

/* Writes the block at out (room for EFFECTS_WIRE_MAX bytes); returns the
 * bytes written. Counts out of range are clamped. */
static inline size_t effects_wire_write(const UpdateBlockEffects *b, uint8_t *out) {
    int32_t ne = b->event_count < 0 ? 0 : (b->event_count > MAX_NET_EVENTS ? MAX_NET_EVENTS : b->event_count);
    int32_t nt = b->torpedo_count < 0 ? 0
                                      : (b->torpedo_count > MAX_VISIBLE_TORPEDOES ? MAX_VISIBLE_TORPEDOES : b->torpedo_count);
    uint8_t *p = out;
    memcpy(p, b, offsetof(UpdateBlockEffects, event_count));
    p += offsetof(UpdateBlockEffects, event_count);
    memcpy(p, &ne, sizeof(ne));
    p += sizeof(ne);
    memcpy(p, b->events, (size_t)ne * sizeof(NetEvent));
    p += (size_t)ne * sizeof(NetEvent);
    memcpy(p, &nt, sizeof(nt));
    p += sizeof(nt);
    memcpy(p, b->visible_torpedoes, (size_t)nt * sizeof(NetVisibleTorpedo));
    p += (size_t)nt * sizeof(NetVisibleTorpedo);
    return (size_t)(p - out);
}

/* Reader: exactly len bytes into buf, false on a short read. */
typedef bool (*effects_read_fn)(void *ctx, void *buf, size_t len);

/* Reads one block into b (the unused entries are zeroed). Returns the
 * bytes consumed, or -1 on a short read or a count out of range (a
 * protocol error: the stream cannot be realigned). */
static inline long effects_wire_read(UpdateBlockEffects *b, effects_read_fn rd, void *ctx) {
    memset(b, 0, sizeof(*b));
    if (!rd(ctx, b, EFFECTS_WIRE_HEAD)) return -1;
    if (b->event_count < 0 || b->event_count > MAX_NET_EVENTS) return -1;
    size_t ev = (size_t)b->event_count * sizeof(NetEvent);
    if (ev && !rd(ctx, b->events, ev)) return -1;
    if (!rd(ctx, &b->torpedo_count, sizeof(b->torpedo_count))) return -1;
    if (b->torpedo_count < 0 || b->torpedo_count > MAX_VISIBLE_TORPEDOES) return -1;
    size_t tv = (size_t)b->torpedo_count * sizeof(NetVisibleTorpedo);
    if (tv && !rd(ctx, b->visible_torpedoes, tv)) return -1;
    return (long)(EFFECTS_WIRE_HEAD + ev + sizeof(int32_t) + tv);
}

#endif
