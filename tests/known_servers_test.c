/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * known_servers_test — the deck's pins of server identity keys
 * (include/known_servers.h, 2026.10.31.43, wire revision 4):
 *  1. hex and fingerprint: round trip, 64 digits exactly, the fingerprint
 *     format the server prints.
 *  2. first contact: the key is pinned (directory 0700, file 0600);
 *     the same key again: PINNED; another key: MISMATCH (the pinned key is
 *     reported); another server keeps its own pin.
 *  3. lookup: comments, blank and damaged lines are skipped; a host that
 *     is a prefix of another ("...:5000" / "...:50001") is not confused.
 *  4. SPACEGL_SERVER_KEY: the right key, another key, a malformed value;
 *     the file is not consulted.
 *  5. a file that cannot be written: first contact accepted, NOT saved.
 */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <openssl/rand.h>
#include "known_servers.h"

#define TEST_CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "TEST FAILED: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

int main(void) {
    uint8_t a[32], b[32], c[32], got[32];
    RAND_bytes(a, 32);
    RAND_bytes(b, 32);
    RAND_bytes(c, 32);

    /* 1 */
    char hex[65];
    ks_hex(a, hex);
    TEST_CHECK(strlen(hex) == 64 && ks_unhex(hex, got) && memcmp(got, a, 32) == 0);
    char upper[80];
    for (int i = 0; i < 64; i++) upper[i] = (char)((hex[i] >= 'a') ? hex[i] - 32 : hex[i]);
    snprintf(upper + 64, sizeof(upper) - 64, " \n");
    TEST_CHECK(ks_unhex(upper, got) && memcmp(got, a, 32) == 0);   /* case, trailing blanks */
    TEST_CHECK(!ks_unhex("abcd", got));
    hex[63] = '\0';
    TEST_CHECK(!ks_unhex(hex, got));                                 /* 63 digits */
    ks_hex(a, hex);
    char longer[80];
    snprintf(longer, sizeof(longer), "%s0", hex);
    TEST_CHECK(!ks_unhex(longer, got));                              /* 65 digits */
    char fp[KS_FINGERPRINT_LEN + 1];
    ks_fingerprint(a, fp);
    TEST_CHECK(strlen(fp) == KS_FINGERPRINT_LEN && strncmp(fp, "SHA256:", 7) == 0 && fp[9] == ':');
    printf("[1] hex round trip, 64 digits exactly, fingerprint %s: OK\n", fp);

    char dir[] = "/tmp/ks_test.XXXXXX";
    TEST_CHECK(mkdtemp(dir) != NULL);
    char path[256];
    snprintf(path, sizeof(path), "%s/sub/known_servers", dir);

    /* 2 */
    TEST_CHECK(ks_check(path, "127.0.0.1:5000", a, NULL, NULL) == KS_NEW_PINNED);
    struct stat st;
    TEST_CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);
    char sub[300];
    snprintf(sub, sizeof(sub), "%s/sub", dir);
    TEST_CHECK(stat(sub, &st) == 0 && (st.st_mode & 0777) == 0700);
    TEST_CHECK(ks_check(path, "127.0.0.1:5000", a, NULL, NULL) == KS_PINNED_OK);
    memset(got, 0, 32);
    TEST_CHECK(ks_check(path, "127.0.0.1:5000", b, NULL, got) == KS_MISMATCH);
    TEST_CHECK(memcmp(got, a, 32) == 0);                       /* the pinned key, for the message */
    TEST_CHECK(ks_refused(KS_MISMATCH) && !ks_refused(KS_PINNED_OK) && !ks_refused(KS_NEW_PINNED));
    TEST_CHECK(ks_check(path, "galaxy.example:5000", b, NULL, NULL) == KS_NEW_PINNED);
    TEST_CHECK(ks_check(path, "galaxy.example:5000", b, NULL, NULL) == KS_PINNED_OK);
    TEST_CHECK(ks_check(path, "127.0.0.1:5000", a, NULL, NULL) == KS_PINNED_OK);   /* untouched */
    printf("[2] first contact pinned (0700/0600), same key PINNED, another key MISMATCH, per server: OK\n");

    /* 3 */
    char path2[256];
    snprintf(path2, sizeof(path2), "%s/known2", dir);
    FILE *f = fopen(path2, "w");
    TEST_CHECK(f != NULL);
    ks_hex(c, hex);
    fprintf(f, "# a comment 127.0.0.1:5000 %s\n\n", hex);
    fprintf(f, "127.0.0.1:5000 not-a-key\n");
    fprintf(f, "127.0.0.1:50001 %s\n", hex);
    ks_hex(a, hex);
    fprintf(f, "127.0.0.1:5000\t%s\n", hex);
    fclose(f);
    TEST_CHECK(ks_lookup(path2, "127.0.0.1:5000", got) == 1 && memcmp(got, a, 32) == 0);
    TEST_CHECK(ks_lookup(path2, "127.0.0.1:500", got) == 0);
    TEST_CHECK(ks_lookup(path2, "10.0.0.1:5000", got) == 0);
    char missing[300];
    snprintf(missing, sizeof(missing), "%s/none", dir);
    TEST_CHECK(ks_lookup(missing, "127.0.0.1:5000", got) == 0);
    printf("[3] comments, blank and damaged lines skipped, no prefix confusion: OK\n");

    /* 4 */
    ks_hex(b, hex);
    TEST_CHECK(ks_check(path, "127.0.0.1:5000", b, hex, NULL) == KS_EXPLICIT_OK);   /* file says a */
    TEST_CHECK(ks_check(path, "127.0.0.1:5000", a, hex, NULL) == KS_EXPLICIT_MISMATCH);
    TEST_CHECK(ks_check(path, "127.0.0.1:5000", a, "SHA256:00:11", NULL) == KS_EXPLICIT_BAD);
    TEST_CHECK(ks_check(path, "new.host:5000", a, hex, NULL) == KS_EXPLICIT_MISMATCH);
    TEST_CHECK(ks_lookup(path, "new.host:5000", got) == 0);   /* nothing written */
    printf("[4] SPACEGL_SERVER_KEY: right key accepted, another or malformed refused, file untouched: OK\n");

    /* 5 */
    char ro[300];
    snprintf(ro, sizeof(ro), "%s/ro", dir);
    TEST_CHECK(mkdir(ro, 0500) == 0);
    char ropath[400];
    snprintf(ropath, sizeof(ropath), "%s/known_servers", ro);
    KsResult r = ks_check(ropath, "127.0.0.1:5000", a, NULL, NULL);
    if (geteuid() == 0) {
        TEST_CHECK(r == KS_NEW_PINNED);   /* root writes anyway */
        printf("[5] (root: the read-only directory is writable, pinned)\n");
    } else {
        TEST_CHECK(r == KS_NEW_NOT_SAVED && !ks_refused(r));
        printf("[5] file that cannot be written: first contact accepted, not saved: OK\n");
    }
    TEST_CHECK(ks_check(NULL, "127.0.0.1:5000", a, NULL, NULL) == KS_NEW_NOT_SAVED);   /* no path at all */

    char cmd[400];
    snprintf(cmd, sizeof(cmd), "chmod -R u+w %s && rm -rf %s", dir, dir);
    if (system(cmd) != 0) { /* best effort cleanup */
    }
    printf("known_servers_test: all checks passed\n");
    return 0;
}
