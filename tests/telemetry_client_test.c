/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * telemetry_client_test.c — the telemetry protocol (include/telemetry.h)
 * against the REAL server (2026.10.31.28, roadmap phase 6). Driven by
 * tests/telemetry_test.sh, which starts spacegl_server on a throwaway data
 * directory and passes the per-category counts the server printed in its
 * "ACTIVE ENTITIES" table at boot.
 *
 * For every category, in order:
 *   1. TEL_PKT_SUBSCRIBE {category};
 *   2. the stream is read packet by packet: TEL_PKT_DATA (length a
 *      multiple of sizeof(TelemetryObject), at most 100 objects) and
 *      TEL_PKT_STATS (closes a frame). A frame is the DATA packets before
 *      a STATS; a STATS with no DATA before it is the reset marker sent
 *      when the category changes. The second complete frame after the
 *      subscription is certainly the new category; it is the one checked;
 *   3. every object of that frame: id and name NUL-terminated, quadrant in
 *      1..GALAXY_SIZE, finite coordinates inside the quadrant;
 *   4. the number of objects equals the boot count for the categories
 *      whose population does not change without players (stars, planets,
 *      star-system bodies, black holes, nebulae, pulsars, quasars, bases),
 *      and is in 0..TEL_MAX_FRAME_OBJECTS for the others;
 *   5. the stats tick grows between frames and active_players is 0.
 *
 * Usage: telemetry_client_test <port> <count_0> <count_1> ... (one count
 * per category, TEL_CAT_COUNT of them).  Exit: 0 pass, 1 fail.
 */
#include "telemetry.h"

#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail++; \
    fprintf(stderr, "FAIL: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

/* Read exactly len bytes, 10 s per wait. */
static bool read_exact(int fd, void *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        struct pollfd p = { .fd = fd, .events = POLLIN };
        if (poll(&p, 1, 10000) <= 0) return false;
        ssize_t n = recv(fd, (char *)buf + got, len - got, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        got += (size_t)n;
    }
    return true;
}

static bool static_category(int c) {
    return c == TEL_CAT_STAR || c == TEL_CAT_PLANET || c == TEL_CAT_SYS_STAR ||
           c == TEL_CAT_SYS_PLANET || c == TEL_CAT_SYS_MOON || c == TEL_CAT_BH ||
           c == TEL_CAT_NEBULA || c == TEL_CAT_PULSAR || c == TEL_CAT_QUASAR ||
           c == TEL_CAT_BASE;
}

/* Reads one frame (DATA... STATS). Returns the number of DATA packets
 * (0 for the reset marker), -1 on a stream error. Objects go to objs
 * (capacity cap), their count to *n. */
static int read_frame(int fd, TelemetryObject *objs, int cap, int *n, TelemetryStats *st) {
    int data_packets = 0;
    *n = 0;
    for (;;) {
        TelemetryHeader h;
        if (!read_exact(fd, &h, sizeof(h))) return -1;
        if (h.type == TEL_PKT_STATS) {
            if (h.length != sizeof(TelemetryStats) || !read_exact(fd, st, sizeof(*st))) return -1;
            return data_packets;
        }
        if (h.type != TEL_PKT_DATA) { fprintf(stderr, "unknown packet type %u\n", h.type); return -1; }
        if (h.length % sizeof(TelemetryObject) != 0 || h.length > 100 * sizeof(TelemetryObject)) {
            fprintf(stderr, "bad DATA length %u\n", h.length);
            return -1;
        }
        int k = (int)(h.length / sizeof(TelemetryObject));
        if (*n + k > cap) { fprintf(stderr, "frame larger than %d objects\n", cap); return -1; }
        if (k > 0 && !read_exact(fd, &objs[*n], h.length)) return -1;
        *n += k;
        data_packets++;
    }
}

int main(int argc, char **argv) {
    if (argc != 2 + TEL_CAT_COUNT) {
        fprintf(stderr, "usage: %s <port> <%d boot counts>\n", argv[0], TEL_CAT_COUNT);
        return 1;
    }
    int port = atoi(argv[1]);   /* NOLINT(cert-err34-c): test argument */
    int boot[TEL_CAT_COUNT];
    for (int c = 0; c < TEL_CAT_COUNT; c++) boot[c] = atoi(argv[2 + c]);   /* NOLINT(cert-err34-c) */

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port) };
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        fprintf(stderr, "FAIL: cannot connect to 127.0.0.1:%d\n", port);
        return 1;
    }

    const int cap = 200000;
    TelemetryObject *objs = calloc((size_t)cap, sizeof(TelemetryObject));
    if (!objs) return 1;
    uint32_t last_tick = 0;
    long total = 0;
    int nonempty = 0;

    for (int c = 0; c < TEL_CAT_COUNT; c++) {
        TelemetryHeader h = { TEL_PKT_SUBSCRIBE, sizeof(TelemetrySubscribe) };
        TelemetrySubscribe s = { (uint32_t)c };
        if (send(fd, &h, sizeof(h), MSG_NOSIGNAL) != (ssize_t)sizeof(h) ||
            send(fd, &s, sizeof(s), MSG_NOSIGNAL) != (ssize_t)sizeof(s)) {
            fprintf(stderr, "FAIL: subscribe %d not sent\n", c);
            return 1;
        }
        int frames = 0, n = 0;
        TelemetryStats st;
        while (frames < 2) {
            int d = read_frame(fd, objs, cap, &n, &st);
            if (d < 0) { fprintf(stderr, "FAIL: stream error in category %d\n", c); return 1; }
            if (d > 0) frames++;
        }
        CHECK(st.active_players == 0, "category %d: active_players %u (no captain logged in)", c, st.active_players);
        CHECK(st.tick >= last_tick, "category %d: stats tick went back (%u < %u)", c, st.tick, last_tick);
        last_tick = st.tick;

        int bad = 0;
        for (int k = 0; k < n && bad < 3; k++) {
            const TelemetryObject *o = &objs[k];
            bool ok = memchr(o->id, '\0', sizeof(o->id)) && memchr(o->name, '\0', sizeof(o->name)) &&
                      memchr(o->info, '\0', sizeof(o->info)) && memchr(o->extra, '\0', sizeof(o->extra)) &&
                      o->q1 >= 1 && o->q1 <= GALAXY_SIZE && o->q2 >= 1 && o->q2 <= GALAXY_SIZE &&
                      o->q3 >= 1 && o->q3 <= GALAXY_SIZE &&
                      isfinite(o->x) && isfinite(o->y) && isfinite(o->z) &&
                      o->x >= 0.0 && o->x <= QUADRANT_SIZE && o->y >= 0.0 && o->y <= QUADRANT_SIZE &&
                      o->z >= 0.0 && o->z <= QUADRANT_SIZE;
            if (!ok) {
                bad++;
                CHECK(false, "category %d object %d: id '%.15s' name '%.31s' q(%d,%d,%d) pos(%g,%g,%g)",
                      c, k, o->id, o->name, o->q1, o->q2, o->q3, o->x, o->y, o->z);
            }
        }
        if (static_category(c))
            CHECK(n == boot[c], "category %d: %d objects, the boot table said %d", c, n, boot[c]);
        CHECK(n >= 0 && n <= TEL_MAX_FRAME_OBJECTS, "category %d: %d objects", c, n);
        total += n;
        if (n > 0) nonempty++;
    }
    CHECK(boot[TEL_CAT_STAR] > 0, "the galaxy has stars");
    CHECK(nonempty >= TEL_CAT_COUNT / 2, "only %d of %d categories have objects", nonempty, TEL_CAT_COUNT);
    printf("telemetry: %d categories, %ld objects, %d non-empty, last tick %u\n",
           TEL_CAT_COUNT, total, nonempty, last_tick);
    free(objs);
    close(fd);
    if (g_fail) { printf("FAIL: telemetry_client_test (%d)\n", g_fail); return 1; }
    printf("PASS: telemetry_client_test\n");
    return 0;
}
