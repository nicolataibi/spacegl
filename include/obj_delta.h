/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * obj_delta.h — per-object delta of the quadrant object list
 * (UPD_OBJDELTA, wire revision 3, 2026.10.31.30; roadmap phase 5).
 *
 * Until wire revision 2 a change of ANY field other than the motion of
 * ANY object of the quadrant (an NPC's energy, a planet's resources, a
 * name) re-sent the whole list: object_count x 177 bytes, up to 181 KB,
 * possibly every tick during a fight. The motion-only block (the former
 * UPD_KINEMATICS) carried positions and velocities but not the attitude,
 * so a turning ship kept its old heading on the decks until the next
 * full refresh (every 5 s).
 *
 * The block, after the other update blocks of the same mask bit order:
 *
 *   int32 object_count        length of the list after this update
 *   int32 n_full              then n_full x { int32 index; NetObject }
 *   int32 n_motion            then n_motion x { int32 index; NetMotion }
 *
 * index is the slot in the list, strictly increasing inside each run and
 * below object_count. Slots at or past the previous length always come
 * as full records; the list is cut to object_count. The server sends the
 * full list (UPD_OBJECTS) instead whenever this block would not be
 * smaller, on a quadrant change and every 5 s, so a deck that missed
 * nothing and one that just joined end with the same list.
 *
 * Header-only, used by the server (src/server/net.c) and by the deck
 * (src/spacegl_client.c). Tests: tests/obj_delta_test.c.
 */
#ifndef OBJ_DELTA_H
#define OBJ_DELTA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "game_state.h"

#pragma pack(push, 1)
typedef struct {
    float net_x, net_y, net_z;   /* sector position */
    float vx, vy, vz;            /* velocity */
    float h, m, r;               /* heading, mark, roll */
} NetMotion;
#pragma pack(pop)

#define OBJDELTA_HEADER_SIZE  (3 * sizeof(int32_t))
#define OBJDELTA_FULL_SIZE    (sizeof(int32_t) + sizeof(NetObject))
#define OBJDELTA_MOTION_SIZE  (sizeof(int32_t) + sizeof(NetMotion))
/* worst case of a block the encoder accepts: below the full list */
#define OBJDELTA_MAX_SIZE     (sizeof(int32_t) + (size_t)MAX_NET_OBJECTS * sizeof(NetObject))

/* Every field but the motion. */
static inline bool objdelta_meta_equal(const NetObject *a, const NetObject *b) {
    return a->type == b->type && a->ship_class == b->ship_class && a->active == b->active &&
           a->health_pct == b->health_pct && a->energy == b->energy && a->plating == b->plating &&
           a->hull_integrity == b->hull_integrity && a->faction == b->faction && a->id == b->id &&
           a->is_cloaked == b->is_cloaked && memcmp(a->name, b->name, sizeof(a->name)) == 0;
}

static inline bool objdelta_motion_equal(const NetObject *a, const NetObject *b) {
    return a->net_x == b->net_x && a->net_y == b->net_y && a->net_z == b->net_z &&
           a->vx == b->vx && a->vy == b->vy && a->vz == b->vz &&
           a->h == b->h && a->m == b->m && a->r == b->r;
}

typedef enum {
    OBJDELTA_NONE = 0,   /* the list did not change: nothing to send */
    OBJDELTA_DELTA,      /* the block was written to out */
    OBJDELTA_FULL        /* send UPD_OBJECTS: the block would not be smaller */
} ObjDeltaKind;

typedef struct {
    int32_t prev_n, cur_n;     /* clamped to [0, MAX_NET_OBJECTS] */
    int32_t n_full, n_motion;
    size_t size;               /* bytes of the block */
} ObjDeltaPlan;

/* Compares cur[0..cur_n) with prev[0..prev_n) (what the deck holds) and
 * says what to send. cap: the room the block may take. */
static inline ObjDeltaKind objdelta_plan(const NetObject *prev, int32_t prev_n,
                                         const NetObject *cur, int32_t cur_n,
                                         size_t cap, ObjDeltaPlan *pl) {
    if (prev_n < 0) prev_n = 0;
    if (cur_n < 0) cur_n = 0;
    if (prev_n > MAX_NET_OBJECTS) prev_n = MAX_NET_OBJECTS;
    if (cur_n > MAX_NET_OBJECTS) cur_n = MAX_NET_OBJECTS;
    pl->prev_n = prev_n; pl->cur_n = cur_n; pl->n_full = 0; pl->n_motion = 0; pl->size = 0;
    for (int32_t i = 0; i < cur_n; i++) {
        if (i >= prev_n || !objdelta_meta_equal(&prev[i], &cur[i])) pl->n_full++;
        else if (!objdelta_motion_equal(&prev[i], &cur[i])) pl->n_motion++;
    }
    if (pl->n_full == 0 && pl->n_motion == 0 && cur_n == prev_n) return OBJDELTA_NONE;
    pl->size = OBJDELTA_HEADER_SIZE + (size_t)pl->n_full * OBJDELTA_FULL_SIZE +
               (size_t)pl->n_motion * OBJDELTA_MOTION_SIZE;
    size_t full = sizeof(int32_t) + (size_t)cur_n * sizeof(NetObject);
    if (pl->size >= full || pl->size > cap) return OBJDELTA_FULL;
    return OBJDELTA_DELTA;
}

/* Writes the block planned by objdelta_plan() (OBJDELTA_DELTA) to out,
 * which has room for pl->size bytes; returns pl->size. */
static inline size_t objdelta_write(const NetObject *prev, const NetObject *cur,
                                    const ObjDeltaPlan *pl, uint8_t *out) {
    uint8_t *p = out;
    int32_t prev_n = pl->prev_n, cur_n = pl->cur_n;
    memcpy(p, &cur_n, sizeof(int32_t)); p += sizeof(int32_t);
    memcpy(p, &pl->n_full, sizeof(int32_t)); p += sizeof(int32_t);
    for (int32_t i = 0; i < cur_n; i++) {
        if (i >= prev_n || !objdelta_meta_equal(&prev[i], &cur[i])) {
            memcpy(p, &i, sizeof(int32_t)); p += sizeof(int32_t);
            memcpy(p, &cur[i], sizeof(NetObject)); p += sizeof(NetObject);
        }
    }
    memcpy(p, &pl->n_motion, sizeof(int32_t)); p += sizeof(int32_t);
    for (int32_t i = 0; i < cur_n && i < prev_n; i++) {
        if (objdelta_meta_equal(&prev[i], &cur[i]) && !objdelta_motion_equal(&prev[i], &cur[i])) {
            NetMotion mo = { (float)cur[i].net_x, (float)cur[i].net_y, (float)cur[i].net_z,
                             (float)cur[i].vx, (float)cur[i].vy, (float)cur[i].vz,
                             (float)cur[i].h, (float)cur[i].m, (float)cur[i].r };
            memcpy(p, &i, sizeof(int32_t)); p += sizeof(int32_t);
            memcpy(p, &mo, sizeof(NetMotion)); p += sizeof(NetMotion);
        }
    }
    return (size_t)(p - out);
}

/* objdelta_plan() + objdelta_write() into out (capacity cap). On
 * OBJDELTA_DELTA the block is in out and *size is its length. */
static inline ObjDeltaKind objdelta_encode(const NetObject *prev, int32_t prev_n,
                                           const NetObject *cur, int32_t cur_n,
                                           uint8_t *out, size_t cap, size_t *size) {
    ObjDeltaPlan pl;
    ObjDeltaKind k = objdelta_plan(prev, prev_n, cur, cur_n, cap, &pl);
    *size = (k == OBJDELTA_DELTA) ? objdelta_write(prev, cur, &pl, out) : 0;
    return k;
}

/* Reads exactly len bytes into buf; false on a short read. */
typedef bool (*objdelta_read_fn)(void *ctx, void *buf, size_t len);

/* Applies a block read through rd to objs[0..*count) (capacity
 * MAX_NET_OBJECTS). Returns the number of bytes read, or -1 when the block
 * is malformed (a count out of range, an index that is not increasing or
 * not below object_count, a slot past the previous length sent as motion)
 * or the stream is short: the stream cannot be resynchronized, the caller
 * closes the connection. On -1 the list may be partly updated. */
static inline long objdelta_apply(NetObject *objs, int32_t *count, objdelta_read_fn rd, void *ctx) {
    int32_t prev_n = *count, cur_n, n;
    long used = 0;
    if (prev_n < 0 || prev_n > MAX_NET_OBJECTS) return -1;
    if (!rd(ctx, &cur_n, sizeof(cur_n))) return -1;
    used += (long)sizeof(cur_n);
    if (cur_n < 0 || cur_n > MAX_NET_OBJECTS) return -1;
    if (!rd(ctx, &n, sizeof(n))) return -1;
    used += (long)sizeof(n);
    if (n < 0 || n > cur_n) return -1;
    int32_t last = -1, new_slots = 0;
    for (int32_t k = 0; k < n; k++) {
        int32_t idx;
        NetObject o;
        if (!rd(ctx, &idx, sizeof(idx)) || !rd(ctx, &o, sizeof(o))) return -1;
        used += (long)(sizeof(idx) + sizeof(o));
        if (idx <= last || idx >= cur_n) return -1;
        objs[idx] = o;
        if (idx >= prev_n) new_slots++;
        last = idx;
    }
    /* every slot past the previous length must have come as a full record
     * (the indices are distinct, so counting them is enough) */
    if (cur_n > prev_n && new_slots != cur_n - prev_n) return -1;
    if (!rd(ctx, &n, sizeof(n))) return -1;
    used += (long)sizeof(n);
    if (n < 0 || n > cur_n) return -1;
    last = -1;
    for (int32_t k = 0; k < n; k++) {
        int32_t idx;
        NetMotion mo;
        if (!rd(ctx, &idx, sizeof(idx)) || !rd(ctx, &mo, sizeof(mo))) return -1;
        used += (long)(sizeof(idx) + sizeof(mo));
        if (idx <= last || idx >= cur_n || idx >= prev_n) return -1;
        NetObject *o = &objs[idx];
        o->net_x = mo.net_x; o->net_y = mo.net_y; o->net_z = mo.net_z;
        o->vx = mo.vx; o->vy = mo.vy; o->vz = mo.vz;
        o->h = mo.h; o->m = mo.m; o->r = mo.r;
        last = idx;
    }
    *count = cur_n;
    return used;
}

#endif
