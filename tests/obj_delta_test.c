/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * obj_delta_test.c — include/obj_delta.h (UPD_OBJDELTA, 2026.10.31.30).
 *
 *   1. Round trip: 3000 random steps (objects that move, turn, change a
 *      field, appear at the end, are cut from the end, are replaced in the
 *      middle); after each step the deck's list, updated by the encoded
 *      block (or by the full list when the encoder asks for it), equals the
 *      server's list, the motion to float precision.
 *   2. The attitude travels: a turning object (h, m, r) reaches the deck
 *      (the former UPD_KINEMATICS dropped it).
 *   3. Size: one changed field among 200 objects costs one record, not the
 *      list; an unchanged list costs nothing; a block that would not be
 *      smaller than the full list is refused.
 *   4. Hostile blocks: counts out of range, indices not increasing or past
 *      object_count, a new slot sent as motion or not sent at all, and every
 *      truncation of a valid block are refused (-1), never a write out of
 *      the list (ASan/UBSan).
 *
 * Exit: 0 pass, 1 fail.
 */
#include "obj_delta.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

typedef struct { const uint8_t *p; size_t len, off; } Cursor;
static bool cur_read(void *ctx, void *buf, size_t len) {
    Cursor *c = (Cursor *)ctx;
    if (c->off + len > c->len) return false;
    memcpy(buf, c->p + c->off, len);
    c->off += len;
    return true;
}

static uint32_t g_rng = 12345u;
static uint32_t rnd(void) { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }
static double rndf(double lo, double hi) { return lo + (hi - lo) * (double)(rnd() % 100000) / 100000.0; }

static void make_obj(NetObject *o, int id) {
    memset(o, 0, sizeof(*o));
    o->net_x = rndf(0, 100); o->net_y = rndf(0, 100); o->net_z = rndf(0, 100);
    o->h = rndf(0, 360); o->m = rndf(-90, 90);
    o->type = 1 + (int32_t)(rnd() % 30); o->active = 1; o->health_pct = 100;
    o->energy = 1000 + rnd() % 5000; o->faction = (int32_t)(rnd() % 12); o->id = id;
    snprintf(o->name, sizeof(o->name), "Object %d", id);
}

/* deck == server, motion compared as the float the wire carries */
static bool lists_match(const NetObject *deck, int32_t dn, const NetObject *srv, int32_t sn) {
    if (dn != sn) return false;
    for (int32_t i = 0; i < sn; i++) {
        if (!objdelta_meta_equal(&deck[i], &srv[i])) return false;
        const double a[9] = { deck[i].net_x, deck[i].net_y, deck[i].net_z, deck[i].vx, deck[i].vy, deck[i].vz,
                              deck[i].h, deck[i].m, deck[i].r };
        const double b[9] = { srv[i].net_x, srv[i].net_y, srv[i].net_z, srv[i].vx, srv[i].vy, srv[i].vz,
                              srv[i].h, srv[i].m, srv[i].r };
        for (int k = 0; k < 9; k++)
            if (a[k] != b[k] && a[k] != (double)(float)b[k]) return false;
    }
    return true;
}

static NetObject srv[MAX_NET_OBJECTS], sent[MAX_NET_OBJECTS], deck[MAX_NET_OBJECTS];
static uint8_t buf[OBJDELTA_MAX_SIZE];

/* Bytes the wire revision 2 encoder spent on the same step: the whole list
 * on any change but motion, 28 bytes per object (UPD_KINEMATICS) when only
 * the motion changed. */
static size_t rev2_bytes(int32_t sent_n, int32_t srv_n) {
    bool meta = (sent_n != srv_n), motion = false;
    for (int32_t i = 0; i < srv_n && !meta; i++) {
        if (!objdelta_meta_equal(&sent[i], &srv[i])) meta = true;
        else if (!objdelta_motion_equal(&sent[i], &srv[i])) motion = true;
    }
    if (meta) return sizeof(int32_t) + (size_t)srv_n * sizeof(NetObject);
    if (motion) return sizeof(int32_t) + (size_t)srv_n * 28u;
    return 0;
}
static size_t g_rev2 = 0;

/* One network pulse as in send_optimized_update(): delta, full or nothing. */
static int pulse(int32_t *sent_n, int32_t srv_n, int32_t *deck_n, size_t *bytes) {
    size_t size = 0;
    g_rev2 += rev2_bytes(*sent_n, srv_n);
    ObjDeltaKind k = objdelta_encode(sent, *sent_n, srv, srv_n, buf, sizeof(buf), &size);
    if (k == OBJDELTA_DELTA) {
        Cursor c = { buf, size, 0 };
        long used = objdelta_apply(deck, deck_n, cur_read, &c);
        if (used != (long)size) return -1;
        *bytes += size;
    } else if (k == OBJDELTA_FULL) {
        memcpy(deck, srv, (size_t)srv_n * sizeof(NetObject));
        *deck_n = srv_n;
        *bytes += sizeof(int32_t) + (size_t)srv_n * sizeof(NetObject);
    }
    memcpy(sent, srv, (size_t)srv_n * sizeof(NetObject));
    *sent_n = srv_n;
    return (int)k;
}

static void test_round_trip(void) {
    int32_t n = 150, sent_n = 0, deck_n = 0;
    for (int i = 0; i < n; i++) make_obj(&srv[i], 1000 + i);
    size_t bytes = 0;
    int deltas = 0, fulls = 0, mismatches = 0, errors = 0;
    for (int step = 0; step < 3000; step++) {
        int what = (int)(rnd() % 100);
        if (what < 50) {                          /* motion of a few */
            for (int k = 0; k < 5 && n > 0; k++) {
                NetObject *o = &srv[rnd() % (uint32_t)n];
                o->net_x += 0.01; o->vx = 0.2; o->h = fmod(o->h + 1.5, 360.0); o->r = rndf(-30, 30);
            }
        } else if (what < 75 && n > 0) {          /* one field of one object */
            srv[rnd() % (uint32_t)n].energy += 7;
        } else if (what < 85 && n < MAX_NET_OBJECTS - 5) {  /* appear at the end */
            for (int k = 0; k < 3; k++, n++) make_obj(&srv[n], 5000 + step * 3 + k);
        } else if (what < 93 && n > 3) {          /* cut from the end */
            n -= 1 + (int32_t)(rnd() % 3);
        } else if (n > 2) {                       /* replaced in the middle */
            make_obj(&srv[rnd() % (uint32_t)n], 9000 + step);
        }
        int k = pulse(&sent_n, n, &deck_n, &bytes);
        if (k < 0) errors++;
        if (k == OBJDELTA_DELTA) deltas++;
        if (k == OBJDELTA_FULL) fulls++;
        if (!lists_match(deck, deck_n, srv, n)) mismatches++;
    }
    CHECK(errors == 0, "round trip: %d blocks not consumed exactly", errors);
    CHECK(mismatches == 0, "round trip: the deck differed from the server after %d steps", mismatches);
    CHECK(deltas > 2000, "round trip: only %d steps used the delta (full %d)", deltas, fulls);
    printf("  round trip: 3000 steps, %d delta, %d full: %zu bytes (wire revision 2: %zu bytes)\n",
           deltas, fulls, bytes, g_rev2);
    CHECK(bytes * 10 < g_rev2, "the delta is not ten times smaller than revision 2 (%zu vs %zu)", bytes, g_rev2);
}

static void test_attitude_and_size(void) {
    int32_t n = 200;
    for (int i = 0; i < n; i++) make_obj(&srv[i], 100 + i);
    memcpy(sent, srv, sizeof(NetObject) * (size_t)n);
    memcpy(deck, srv, sizeof(NetObject) * (size_t)n);
    int32_t deck_n = n;
    size_t size = 0;
    CHECK(objdelta_encode(sent, n, srv, n, buf, sizeof(buf), &size) == OBJDELTA_NONE && size == 0,
          "an unchanged list costs nothing");

    srv[17].h = 123.5; srv[17].m = -12.25; srv[17].r = 45.0;    /* a ship turns */
    CHECK(objdelta_encode(sent, n, srv, n, buf, sizeof(buf), &size) == OBJDELTA_DELTA &&
          size == OBJDELTA_HEADER_SIZE + OBJDELTA_MOTION_SIZE, "a turn is one motion record (%zu bytes)", size);
    Cursor c = { buf, size, 0 };
    CHECK(objdelta_apply(deck, &deck_n, cur_read, &c) == (long)size &&
          deck[17].h == 123.5 && deck[17].m == -12.25 && deck[17].r == 45.0,
          "the attitude reaches the deck");
    memcpy(sent, srv, sizeof(NetObject) * (size_t)n);

    srv[150].energy -= 250;                                    /* one field */
    CHECK(objdelta_encode(sent, n, srv, n, buf, sizeof(buf), &size) == OBJDELTA_DELTA &&
          size == OBJDELTA_HEADER_SIZE + OBJDELTA_FULL_SIZE,
          "one changed field is one record: %zu bytes instead of %zu", size,
          sizeof(int32_t) + (size_t)n * sizeof(NetObject));
    c = (Cursor){ buf, size, 0 };
    CHECK(objdelta_apply(deck, &deck_n, cur_read, &c) == (long)size && deck[150].energy == srv[150].energy,
          "the field reaches the deck");
    memcpy(sent, srv, sizeof(NetObject) * (size_t)n);

    for (int i = 0; i < n; i++) srv[i].energy++;               /* everything */
    CHECK(objdelta_encode(sent, n, srv, n, buf, sizeof(buf), &size) == OBJDELTA_FULL,
          "a block not smaller than the list is refused");
    CHECK(objdelta_encode(sent, n, srv, n, buf, 64, &size) == OBJDELTA_FULL,
          "a block larger than the buffer is refused");
}

/* Builds a block by hand. */
static size_t put_i32(uint8_t *p, int32_t v) { memcpy(p, &v, 4); return 4; }

static long apply_bytes(const uint8_t *b, size_t len, int32_t *count) {
    Cursor c = { b, len, 0 };
    return objdelta_apply(deck, count, cur_read, &c);
}

static void test_hostile(void) {
    static uint8_t b[4096];
    NetObject o; make_obj(&o, 1);
    NetMotion mo = {0};
    int32_t cnt;
    size_t l;

    struct { int32_t count, n_full; } bad_counts[] = {
        { -1, 0 }, { MAX_NET_OBJECTS + 1, 0 }, { 5, -1 }, { 5, 6 }, { INT32_MAX, 0 },
    };
    for (size_t t = 0; t < sizeof(bad_counts) / sizeof(bad_counts[0]); t++) {
        l = 0; l += put_i32(b + l, bad_counts[t].count); l += put_i32(b + l, bad_counts[t].n_full);
        l += put_i32(b + l, 0);
        cnt = 0;
        CHECK(apply_bytes(b, l, &cnt) == -1, "count %d / n_full %d refused", bad_counts[t].count, bad_counts[t].n_full);
    }
    /* index past object_count */
    l = 0; l += put_i32(b + l, 2); l += put_i32(b + l, 1); l += put_i32(b + l, 2);
    memcpy(b + l, &o, sizeof(o)); l += sizeof(o); l += put_i32(b + l, 0);
    cnt = 2; CHECK(apply_bytes(b, l, &cnt) == -1, "full index == object_count refused");
    /* indices not increasing */
    l = 0; l += put_i32(b + l, 4); l += put_i32(b + l, 2);
    l += put_i32(b + l, 1); memcpy(b + l, &o, sizeof(o)); l += sizeof(o);
    l += put_i32(b + l, 1); memcpy(b + l, &o, sizeof(o)); l += sizeof(o);
    l += put_i32(b + l, 0);
    cnt = 4; CHECK(apply_bytes(b, l, &cnt) == -1, "repeated full index refused");
    /* a new slot sent as motion */
    l = 0; l += put_i32(b + l, 3); l += put_i32(b + l, 0); l += put_i32(b + l, 1);
    l += put_i32(b + l, 2); memcpy(b + l, &mo, sizeof(mo)); l += sizeof(mo);
    cnt = 2; CHECK(apply_bytes(b, l, &cnt) == -1, "a new slot as motion refused");
    /* a new slot not sent */
    l = 0; l += put_i32(b + l, 3); l += put_i32(b + l, 0); l += put_i32(b + l, 0);
    cnt = 2; CHECK(apply_bytes(b, l, &cnt) == -1, "a new slot missing refused");
    /* negative motion index */
    l = 0; l += put_i32(b + l, 3); l += put_i32(b + l, 0); l += put_i32(b + l, 1);
    l += put_i32(b + l, -1); memcpy(b + l, &mo, sizeof(mo)); l += sizeof(mo);
    cnt = 3; CHECK(apply_bytes(b, l, &cnt) == -1, "a negative motion index refused");
    /* a corrupted current count */
    cnt = -5; l = 0; l += put_i32(b + l, 0); l += put_i32(b + l, 0); l += put_i32(b + l, 0);
    CHECK(apply_bytes(b, l, &cnt) == -1, "a corrupted list length refused");
    /* the list shrinks to 0 */
    cnt = 3; CHECK(apply_bytes(b, l, &cnt) == (long)l && cnt == 0, "an empty list is accepted");

    /* every truncation of a valid block */
    int32_t n = 40;
    for (int i = 0; i < n; i++) make_obj(&srv[i], 300 + i);
    memcpy(sent, srv, sizeof(NetObject) * (size_t)n);
    srv[3].energy++; srv[9].h += 10; srv[20].net_y += 1;
    for (int i = n; i < n + 4; i++) make_obj(&srv[i], 700 + i);
    size_t size = 0;
    CHECK(objdelta_encode(sent, n, srv, n + 4, buf, sizeof(buf), &size) == OBJDELTA_DELTA, "test block encoded");
    int refused = 0;
    for (size_t cut = 0; cut < size; cut++) {
        memcpy(deck, sent, sizeof(NetObject) * (size_t)n);
        cnt = n;
        if (apply_bytes(buf, cut, &cnt) == -1) refused++;
    }
    CHECK(refused == (int)size, "every truncation refused (%d of %zu)", refused, size);
}

int main(void) {
    test_round_trip();
    test_attitude_and_size();
    test_hostile();
    printf("obj_delta: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
