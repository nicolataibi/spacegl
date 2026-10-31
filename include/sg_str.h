/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * sg_str.h — bounded string building for the server (2026.10.31.18,
 * roadmap phase 3): the replacements for sprintf() and strcat().
 *
 * Fixed-size char arrays (the common case): the capacity comes from the
 * array type.
 *   SG_SPRINTF(arr, fmt, ...)   snprintf(arr, sizeof arr, fmt, ...)
 *   SG_STRCAT(arr, src)         append src, truncated to fit
 *   SG_APPENDF(arr, fmt, ...)   append formatted text, truncated to fit
 * Passing a pointer instead of an array does not compile (negative array
 * size in SG_CAP): a call can never silently take sizeof(char *) as the
 * capacity. Heap buffers use the _n functions with an explicit capacity.
 *
 * All of them always NUL-terminate inside the buffer and never write past
 * it: an over-long text is cut, where sprintf()/strcat() wrote out of
 * bounds. The server texts fit their buffers, so nothing is cut in practice.
 */
#ifndef SG_STR_H
#define SG_STR_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* 1 if a is an array, 0 if it is a pointer (GNU C: gcc and clang). */
#define SG_IS_ARRAY(a) (!__builtin_types_compatible_p(__typeof__(a), __typeof__(&(a)[0])))

/* sizeof the array a; a compile error if a is a pointer. */
#define SG_CAP(a) (sizeof(a) + 0 * sizeof(char[SG_IS_ARRAY(a) ? 1 : -1]))

/* Append src to the NUL-terminated string in dst (capacity cap), cutting
 * it to fit. Returns the new length. */
static inline size_t sg_strcat_n(char *dst, size_t cap, const char *src) {
    if (cap == 0) return 0;
    size_t len = strnlen(dst, cap);
    if (len >= cap) { dst[cap - 1] = '\0'; return cap - 1; }
    size_t room = cap - len - 1;
    size_t n = strlen(src);
    if (n > room) n = room;
    memcpy(dst + len, src, n);
    dst[len + n] = '\0';
    return len + n;
}

/* Append formatted text to the NUL-terminated string in dst (capacity
 * cap), cutting it to fit. Returns what vsnprintf returns for the tail
 * (the untruncated length of the appended text, or < 0 on error). */
static inline int sg_appendf_n(char *dst, size_t cap, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
static inline int sg_appendf_n(char *dst, size_t cap, const char *fmt, ...) {
    if (cap == 0) return 0;
    size_t len = strnlen(dst, cap);
    if (len >= cap) { dst[cap - 1] = '\0'; return 0; }
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(dst + len, cap - len, fmt, ap);
    va_end(ap);
    return r;
}

/* ------------------------------------------------------------------------
 * Thread-safe replacements (2026.10.31.21, clang-tidy concurrency-mt-unsafe):
 * strerror() and localtime() return a static buffer shared by every thread;
 * the server formats errors and timestamps from the game thread, the epoll
 * thread and the pool at once.
 *   SG_STRERROR(e)      strerror_r into a block-local buffer
 *   SG_LOCALTIME(tp)    localtime_r into a block-local struct tm
 * Both use a compound literal: it lives until the end of the enclosing
 * block, so the returned pointer can be used as the old one was (in the
 * same expression, or kept in a local of the same block).
 * ---------------------------------------------------------------------- */
static inline const char *sg_strerror(int err, char *buf, size_t len) {
#if defined(_GNU_SOURCE) && defined(__GLIBC__)
    return strerror_r(err, buf, len);          /* GNU variant: returns the text */
#else
    if (strerror_r(err, buf, len) != 0)        /* XSI variant: fills buf */
        snprintf(buf, len, "error %d", err);
    return buf;
#endif
}
#define SG_STRERROR(e)   sg_strerror((e), (char[128]){0}, 128)
#define SG_LOCALTIME(tp) localtime_r((tp), &(struct tm){0})

#define SG_SPRINTF(arr, ...) snprintf((arr), SG_CAP(arr), __VA_ARGS__)
#define SG_STRCAT(arr, src)  sg_strcat_n((arr), SG_CAP(arr), (src))
#define SG_APPENDF(arr, ...) sg_appendf_n((arr), SG_CAP(arr), __VA_ARGS__)

#endif
