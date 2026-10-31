/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * sg_str_test — include/sg_str.h (2026.10.31.18): the bounded replacements
 * for sprintf/strcat never write past the buffer, always NUL-terminate,
 * cut an over-long text, and take the capacity from the array type.
 * (A pointer passed to SG_SPRINTF/SG_STRCAT/SG_APPENDF does not compile:
 * checked when the header was written, not at run time.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sg_str.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

int main(void) {
    /* guard bytes around an 8-byte buffer catch any write past it */
    struct { char pre[8]; char buf[8]; char post[8]; } g;
    memset(&g, 0x5A, sizeof(g));

    g.buf[0] = '\0';
    CHECK(SG_CAP(g.buf) == 8);
    CHECK(SG_STRCAT(g.buf, "abc") == 3 && strcmp(g.buf, "abc") == 0);
    CHECK(SG_STRCAT(g.buf, "defghij") == 7 && strcmp(g.buf, "abcdefg") == 0);   /* cut to 7 + NUL */
    CHECK(SG_STRCAT(g.buf, "xyz") == 7 && strcmp(g.buf, "abcdefg") == 0);       /* full: unchanged */

    /* runtime text: the truncation is the point here, not a compile-time warning */
    const char *volatile long_text = "long text";
    CHECK(SG_SPRINTF(g.buf, "%d-%s", 42, long_text) == 12);   /* snprintf: untruncated length */
    CHECK(strcmp(g.buf, "42-long") == 0);

    g.buf[0] = '\0';
    SG_APPENDF(g.buf, "%s", "ab");
    SG_APPENDF(g.buf, "%d", 12345678);
    CHECK(strcmp(g.buf, "ab12345") == 0);

    for (int i = 0; i < 8; i++) CHECK(g.pre[i] == 0x5A && g.post[i] == 0x5A);

    /* heap buffer: explicit capacity */
    char *h = calloc(4, 1);
    CHECK(h);
    CHECK(sg_strcat_n(h, 4, "hello") == 3 && strcmp(h, "hel") == 0);
    h[0] = '\0';
    sg_appendf_n(h, 4, "%c%c", 'o', 'k');
    CHECK(strcmp(h, "ok") == 0);
    /* an unterminated buffer is terminated, not overread */
    memset(h, 'Q', 4);
    CHECK(sg_strcat_n(h, 4, "z") == 3 && h[3] == '\0');
    CHECK(sg_strcat_n(h, 0, "z") == 0);
    free(h);

    printf("PASS: sg_str_test\n");
    return 0;
}
