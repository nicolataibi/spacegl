/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * sg_parse_test.c — include/sg_parse.h (2026.10.31.27): sg_scan_nums()
 * and sg_hex_decode(), the checked replacements for sscanf() in the
 * server's command parsers and key loaders.
 *
 *   1. valid input reads exactly what sscanf(" %d %lf ...") reads (a table
 *      compared field by field against the real sscanf);
 *   2. out-of-range ints, overflowing doubles, NaN and infinity stop the
 *      scan and leave the field untouched;
 *   3. empty, blank and NULL input give 0 (sscanf gave EOF, -1);
 *   4. sg_hex_decode() takes exactly 2n hex digits and leaves the output
 *      untouched on any other character (a sign, a blank, a short string).
 *
 * Exit: 0 pass, 1 fail.
 */
#include "sg_parse.h"

#include <stdio.h>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

/* 1. Same result as sscanf on valid input. */
static void test_same_as_sscanf(void) {
    static const char *ints[] = {
        "5", "  42", "-7 8", "+3 4 5", "12abc 9", "5-6", "0x1A", "1 2 3 4 5 6 7",
        "2147483647", "-2147483648", "7,8", "\t9\n10", "abc", "1 x 2",
    };
    for (size_t t = 0; t < sizeof(ints) / sizeof(ints[0]); t++) {
        int a[3] = {111, 222, 333}, b[3] = {111, 222, 333};
        int n1 = sg_scan_nums(ints[t], "iii", &a[0], &a[1], &a[2]);
        int n2 = sscanf(ints[t], " %d %d %d", &b[0], &b[1], &b[2]);
        if (n2 < 0) n2 = 0;
        CHECK(n1 == n2, "ints \"%s\": count %d, sscanf %d", ints[t], n1, n2);
        for (int k = 0; k < 3; k++)
            CHECK(a[k] == b[k], "ints \"%s\" field %d: %d, sscanf %d", ints[t], k, a[k], b[k]);
    }
    static const char *mixed[] = {
        "16 2.0", "16", "3 2.5e1", "-4 .5", "8 1e-3 junk", "1 0x1p3", "2 -0", "9 3.", "x 1",
    };
    for (size_t t = 0; t < sizeof(mixed) / sizeof(mixed[0]); t++) {
        int ia = -1, ib = -1; double da = -1.5, db = -1.5;
        int n1 = sg_scan_nums(mixed[t], "if", &ia, &da);
        int n2 = sscanf(mixed[t], " %d %lf", &ib, &db);
        if (n2 < 0) n2 = 0;
        CHECK(n1 == n2, "mixed \"%s\": count %d, sscanf %d", mixed[t], n1, n2);
        CHECK(ia == ib && da == db, "mixed \"%s\": (%d, %g), sscanf (%d, %g)",
              mixed[t], ia, da, ib, db);
    }
    double h = 0, m = 0, w = 0;
    CHECK(sg_scan_nums("90 -45.5 1e2", "fff", &h, &m, &w) == 3 &&
          h == 90.0 && m == -45.5 && w == 100.0, "doubles: 90 -45.5 1e2");
}

/* 2. Out of range and non-finite. */
static void test_refused(void) {
    struct { const char *s; int want; } ic[] = {
        { "2147483648", 0 }, { "-2147483649", 0 }, { "99999999999999999999", 0 },
        { "5 99999999999", 1 },
    };
    for (size_t t = 0; t < sizeof(ic) / sizeof(ic[0]); t++) {
        int a = 77, b = 88;
        int n = sg_scan_nums(ic[t].s, "ii", &a, &b);
        CHECK(n == ic[t].want, "int range \"%s\": count %d, want %d", ic[t].s, n, ic[t].want);
        CHECK(b == 88 && (ic[t].want == 1 ? a == 5 : a == 77),
              "int range \"%s\": refused field untouched (a=%d b=%d)", ic[t].s, a, b);
    }
    struct { const char *s; int want; } dc[] = {
        { "nan", 0 }, { "NAN 1", 0 }, { "inf", 0 }, { "-Infinity", 0 }, { "1e999", 0 },
        { "-1e400", 0 }, { "1 nan 2", 1 }, { "1 2 inf", 2 }, { "nan(0x1)", 0 },
    };
    for (size_t t = 0; t < sizeof(dc) / sizeof(dc[0]); t++) {
        double v[3] = {7.0, 7.0, 7.0};
        int n = sg_scan_nums(dc[t].s, "fff", &v[0], &v[1], &v[2]);
        CHECK(n == dc[t].want, "double \"%s\": count %d, want %d", dc[t].s, n, dc[t].want);
        for (int k = n; k < 3; k++)
            CHECK(v[k] == 7.0, "double \"%s\": field %d untouched", dc[t].s, k);
    }
    double tiny = 7.0;
    CHECK(sg_scan_nums("1e-400", "f", &tiny) == 1 && tiny >= 0.0 && tiny < 1e-300,
          "an underflow is a (tiny) number");
}

/* 3. Nothing to read. */
static void test_empty(void) {
    int a = 5; double d = 5.0;
    CHECK(sg_scan_nums("", "i", &a) == 0 && a == 5, "empty string: 0");
    CHECK(sg_scan_nums("   \t\n", "if", &a, &d) == 0 && a == 5 && d == 5.0, "blank string: 0");
    CHECK(sg_scan_nums(NULL, "i", &a) == 0 && a == 5, "NULL string: 0");
    CHECK(sg_scan_nums("4", "", &a) == 0 && a == 5, "empty spec: 0");
}

/* 4. Hex keys. */
static void test_hex(void) {
    uint8_t out[4] = {0xEE, 0xEE, 0xEE, 0xEE};
    CHECK(sg_hex_decode("00ff10Ab", out, 4) && out[0] == 0x00 && out[1] == 0xFF &&
          out[2] == 0x10 && out[3] == 0xAB, "hex: 00ff10Ab");
    uint8_t two[2] = {0x11, 0x22};
    CHECK(sg_hex_decode("a1b2c3", two, 2) && two[0] == 0xA1 && two[1] == 0xB2,
          "hex: only 2n digits are read, the rest is ignored");
    const char *bad[] = { "0g12", " f12", "+f12", "-112", "ab", "", "12 4", "0x12" };
    for (size_t t = 0; t < sizeof(bad) / sizeof(bad[0]); t++) {
        uint8_t o[2] = {0x5A, 0x5A};
        CHECK(!sg_hex_decode(bad[t], o, 2) && o[0] == 0x5A && o[1] == 0x5A,
              "hex \"%s\": refused, output untouched", bad[t]);
    }
    uint8_t k[32];
    char key[65];
    for (int i = 0; i < 32; i++) snprintf(key + 2 * i, 3, "%02x", (unsigned)(i * 7 + 3) & 0xFFu);
    CHECK(sg_hex_decode(key, k, 32), "hex: a 64-digit key");
    int same = 1;
    for (int i = 0; i < 32; i++) if (k[i] != (uint8_t)((i * 7 + 3) & 0xFF)) same = 0;
    CHECK(same, "hex: the 64-digit key round-trips");
    CHECK(!sg_hex_decode(NULL, k, 1) && !sg_hex_decode("00", NULL, 1), "hex: NULL refused");
}

int main(void) {
    test_same_as_sscanf();
    test_refused();
    test_empty();
    test_hex();
    printf("sg_parse: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
