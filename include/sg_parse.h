/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * sg_parse.h — checked number parsing for the server (2026.10.31.27,
 * roadmap phase 6): the replacements for sscanf("%d"/"%lf"/"%x").
 *
 * sscanf() cannot report an out-of-range number: "%d" on 99999999999 is
 * undefined behaviour (in practice a wrapped value), and "%lf" accepts
 * "nan" and "inf", which then flow into the navigation and combat maths.
 * These functions use strtol()/strtod() and refuse such a field.
 *
 *   int sg_scan_nums(const char *s, const char *spec, ...)
 *       Reads the numbers of a command's parameters in order. spec has one
 *       letter per field: 'i' stores an int (int *), 'f' a double
 *       (double *). Like sscanf(s, " %d %lf ..."), blanks before each
 *       number are skipped and the scan stops at the first field that is
 *       not a number ("12abc" gives 12 and stops at "abc"). Unlike sscanf:
 *         - an int outside [INT_MIN, INT_MAX], a double that overflows,
 *           NaN and infinity are not numbers: the scan stops there;
 *         - an empty or blank string gives 0, never EOF (-1);
 *         - a field that is not stored is left unchanged.
 *       Returns the number of fields stored, 0..strlen(spec).
 *
 *   bool sg_hex_decode(const char *hex, uint8_t *out, size_t n)
 *       Decodes exactly 2n hex digits (upper or lower case) into n bytes.
 *       false, with out unchanged, when one of the 2n characters is not a
 *       hex digit (a sign, a blank or the end of the string included;
 *       sscanf("%2hhx") accepted " f", "+f" and "-1").
 */
#ifndef SG_PARSE_H
#define SG_PARSE_H

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* One int at *ps (blanks skipped); on success *ps moves past it. */
static inline bool sg_parse_int_at(const char **ps, int *out) {
    const char *s = *ps;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return false;
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || errno == ERANGE || v < INT_MIN || v > INT_MAX) return false;
    *out = (int)v;
    *ps = end;
    return true;
}

/* One finite double at *ps (blanks skipped); on success *ps moves past it. */
static inline bool sg_parse_double_at(const char **ps, double *out) {
    const char *s = *ps;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return false;
    char *end = NULL;
    errno = 0;
    double v = strtod(s, &end);
    if (end == s || !isfinite(v)) return false;   /* overflow gives +-HUGE_VAL */
    *out = v;
    *ps = end;
    return true;
}

static inline int sg_scan_nums(const char *s, const char *spec, ...) {
    if (!s || !spec) return 0;
    va_list ap;
    va_start(ap, spec);
    int stored = 0;
    for (const char *k = spec; *k; k++) {
        bool ok = false;
        if (*k == 'i') {
            int *p = va_arg(ap, int *);
            int v;
            ok = sg_parse_int_at(&s, &v);
            if (ok) *p = v;
        } else if (*k == 'f') {
            double *p = va_arg(ap, double *);
            double v;
            ok = sg_parse_double_at(&s, &v);
            if (ok) *p = v;
        }
        if (!ok) break;
        stored++;
    }
    va_end(ap);
    return stored;
}

static inline int sg_hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static inline bool sg_hex_decode(const char *hex, uint8_t *out, size_t n) {
    if (!hex || !out) return false;
    for (size_t i = 0; i < 2 * n; i++) {
        if (sg_hex_digit(hex[i]) < 0) return false;   /* stops at the NUL too */
    }
    for (size_t i = 0; i < n; i++) {
        out[i] = (uint8_t)(sg_hex_digit(hex[2 * i]) * 16 + sg_hex_digit(hex[2 * i + 1]));
    }
    return true;
}

#endif
