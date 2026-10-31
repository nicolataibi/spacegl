/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * key_file_test — load_master_key() (include/network.h), used by the server
 * and the command deck:
 *
 *  1. no key yet: $HOME/.spacegl/key is created 0600 with 64 hex + '\n';
 *     a second load returns the same key.
 *  2. an existing file that is not 64 hex characters is an error and is
 *     left untouched (it used to be overwritten by a new key, voiding every
 *     captain identity).
 *  3. a group/world readable key file is tightened to 0600.
 *  4. SPACEGL_KEY: 64 hex parsed, >= 32 characters -> first 32 bytes,
 *     shorter -> refused (it used to leave the rest of the key zero).
 *  5. SPACEGL_KEY_FILE readable by others is refused (unchanged rule).
 *  6. 8 processes that create the key at the same time all get one key.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "network.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL: %s at %s:%d\n", #c, __FILE__, __LINE__); exit(1); } } while (0)

static char g_home[256];
static char g_key_path[512];

static void fresh_home(void) {
    snprintf(g_home, sizeof(g_home), "/tmp/sgkey-XXXXXX");
    CHECK(mkdtemp(g_home) != NULL);
    snprintf(g_key_path, sizeof(g_key_path), "%s/.spacegl/key", g_home);
    setenv("HOME", g_home, 1);
    unsetenv("SPACEGL_KEY");
    unsetenv("SPACEGL_KEY_FILE");
}

static void read_file(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    size_t n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
    fclose(f);
}

int main(void) {
    uint8_t k1[32], k2[32];

    /* 1. create, then reload */
    fresh_home();
    CHECK(load_master_key(k1) == 0);
    struct stat st;
    CHECK(stat(g_key_path, &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);
    char txt[256];
    read_file(g_key_path, txt, sizeof(txt));
    CHECK(strlen(txt) == 65 && txt[64] == '\n');
    CHECK(load_master_key(k2) == 0);
    CHECK(memcmp(k1, k2, 32) == 0);
    printf("[1] key created 0600, reloaded identical: OK\n");

    /* 2. corrupt file: refused, untouched */
    FILE *f = fopen(g_key_path, "w");
    CHECK(f != NULL);
    fputs("not-a-key\n", f);
    fclose(f);
    CHECK(load_master_key(k2) == -1);
    read_file(g_key_path, txt, sizeof(txt));
    CHECK(strcmp(txt, "not-a-key\n") == 0);
    printf("[2] corrupt key file refused and left untouched: OK\n");

    /* 3. mode 0644 tightened */
    fresh_home();
    CHECK(load_master_key(k1) == 0);
    CHECK(chmod(g_key_path, 0644) == 0);
    CHECK(load_master_key(k2) == 0);
    CHECK(memcmp(k1, k2, 32) == 0);
    CHECK(stat(g_key_path, &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);
    printf("[3] 0644 key file tightened to 0600: OK\n");

    /* 4. SPACEGL_KEY */
    fresh_home();
    setenv("SPACEGL_KEY", "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff", 1);
    CHECK(load_master_key(k1) == 0);
    CHECK(k1[0] == 0x00 && k1[1] == 0x11 && k1[15] == 0xff && k1[31] == 0xff);
    setenv("SPACEGL_KEY", "ci-smoke-shared-master-key-000000", 1);   /* 33 chars */
    CHECK(load_master_key(k1) == 0);
    CHECK(memcmp(k1, "ci-smoke-shared-master-key-00000", 32) == 0);
    setenv("SPACEGL_KEY", "short-key", 1);
    CHECK(load_master_key(k1) == -1);
    unsetenv("SPACEGL_KEY");
    CHECK(access(g_key_path, F_OK) != 0);   /* env keys never touch the file */
    printf("[4] SPACEGL_KEY hex / >=32 chars accepted, short refused: OK\n");

    /* 5. SPACEGL_KEY_FILE readable by others */
    fresh_home();
    char kf[512];
    snprintf(kf, sizeof(kf), "%s/k.hex", g_home);
    f = fopen(kf, "w");
    CHECK(f != NULL);
    fputs("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\n", f);
    fclose(f);
    CHECK(chmod(kf, 0644) == 0);
    setenv("SPACEGL_KEY_FILE", kf, 1);
    CHECK(load_master_key(k1) == -1);
    CHECK(chmod(kf, 0600) == 0);
    CHECK(load_master_key(k1) == 0 && k1[1] == 0x11);
    unsetenv("SPACEGL_KEY_FILE");
    printf("[5] SPACEGL_KEY_FILE must be 0600: OK\n");

    /* 6. concurrent first start */
    fresh_home();
    enum { N = 8 };
    int pipes[N][2];
    for (int i = 0; i < N; i++) {
        CHECK(pipe(pipes[i]) == 0);
        pid_t pid = fork();
        CHECK(pid >= 0);
        if (pid == 0) {
            uint8_t k[32];
            int r = load_master_key(k);
            if (r != 0) _exit(2);
            if (write(pipes[i][1], k, 32) != 32) _exit(3);
            _exit(0);
        }
        close(pipes[i][1]);
    }
    uint8_t first[32];
    for (int i = 0; i < N; i++) {
        int status = 0;
        uint8_t k[32];
        CHECK(read(pipes[i][0], k, 32) == 32);
        close(pipes[i][0]);
        if (i == 0) memcpy(first, k, 32);
        CHECK(memcmp(first, k, 32) == 0);
        wait(&status);
        CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    CHECK(load_master_key(k1) == 0 && memcmp(k1, first, 32) == 0);
    printf("[6] %d processes starting together share one key: OK\n", N);

    printf("key_file_test: all checks passed\n");
    return 0;
}
