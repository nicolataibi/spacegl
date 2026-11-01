/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * known_servers.h — the command deck's pins of server identity keys
 * (2026.10.31.43, wire revision 4; include/handshake_v2.h).
 *
 * Every server signs the handshake with its own Ed25519 identity key. The
 * deck remembers that key per server, as SSH does with known_hosts:
 *   - the first connection to a server stores its key (trust on first use)
 *     and prints its fingerprint;
 *   - every later connection must present the same key, or the deck
 *     refuses it;
 *   - SPACEGL_SERVER_KEY=<64 hex digits> pins the key explicitly instead
 *     (no trust on first use, the file is not consulted).
 *
 * File: $SPACEGL_KNOWN_SERVERS, else ~/.spacegl/known_servers (mode 0600,
 * the directory 0700). One line per server:
 *     <host>:<port> <64 hex digits of the Ed25519 public key>
 * '#' starts a comment line. The host is the name the captain typed:
 * "localhost" and "127.0.0.1" are two entries, as in SSH.
 */
#ifndef KNOWN_SERVERS_H
#define KNOWN_SERVERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <openssl/crypto.h>
#include <openssl/sha.h>

typedef enum {
    KS_PINNED_OK = 0,      /* the key matches the pinned one */
    KS_NEW_PINNED,         /* first contact: the key is now pinned */
    KS_NEW_NOT_SAVED,      /* first contact: accepted, but the file could not be written */
    KS_EXPLICIT_OK,        /* matches SPACEGL_SERVER_KEY */
    KS_MISMATCH,           /* another key than the pinned one: refuse */
    KS_EXPLICIT_MISMATCH,  /* another key than SPACEGL_SERVER_KEY: refuse */
    KS_EXPLICIT_BAD,       /* SPACEGL_SERVER_KEY is not 64 hex digits: refuse */
} KsResult;

static inline bool ks_refused(KsResult r) {
    return r == KS_MISMATCH || r == KS_EXPLICIT_MISMATCH || r == KS_EXPLICIT_BAD;
}

static inline void ks_hex(const uint8_t key[32], char out[65]) {
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; i++) {
        out[2 * i] = d[key[i] >> 4];
        out[2 * i + 1] = d[key[i] & 15];
    }
    out[64] = '\0';
}

static inline int ks_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Exactly 64 hex digits (surrounding blanks allowed). */
static inline bool ks_unhex(const char *s, uint8_t key[32]) {
    while (*s == ' ' || *s == '\t') s++;
    for (size_t i = 0; i < 32; i++) {
        int hi = ks_nibble(s[2 * i]), lo = (hi < 0) ? -1 : ks_nibble(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        key[i] = (uint8_t)(hi * 16 + lo);
    }
    const char *e = s + 64;
    while (*e == ' ' || *e == '\t' || *e == '\r' || *e == '\n') e++;
    return *e == '\0';
}

/* "SHA256:" and the first 16 bytes of SHA-256(key) as hex pairs with ':'
 * (the fingerprint the server prints at boot and with --print-identity). */
#define KS_FINGERPRINT_LEN (7 + 16 * 3 - 1)   /* "SHA256:" + 16 pairs + 15 colons */
static inline void ks_fingerprint(const uint8_t key[32], char out[KS_FINGERPRINT_LEN + 1]) {
    static const char d[] = "0123456789abcdef";
    uint8_t h[32];
    SHA256(key, 32, h);
    memcpy(out, "SHA256:", 7);  // NOLINT(bugprone-not-null-terminated-result): the loop below writes the rest and the terminator)
    char *p = out + 7;
    for (int i = 0; i < 16; i++) {
        *p++ = d[h[i] >> 4];
        *p++ = d[h[i] & 15];
        *p++ = (i < 15) ? ':' : '\0';
    }
}

/* The file of the pins: $SPACEGL_KNOWN_SERVERS, else ~/.spacegl/known_servers. */
static inline bool ks_default_path(char *out, size_t cap) {
    const char *env = getenv("SPACEGL_KNOWN_SERVERS");
    if (env && env[0]) return (size_t)snprintf(out, cap, "%s", env) < cap;
    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;
    return (size_t)snprintf(out, cap, "%s/.spacegl/known_servers", home) < cap;
}

/* 1: found (key filled), 0: no entry (or no file), -1: the file exists but
 * cannot be read. A line that cannot be parsed is skipped. */
static inline int ks_lookup(const char *path, const char *hostport, uint8_t key[32]) {
    FILE *f = fopen(path, "r");
    if (!f) return (errno == ENOENT) ? 0 : -1;
    char line[512];
    size_t hl = strlen(hostport);
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (strncmp(line, hostport, hl) != 0 || (line[hl] != ' ' && line[hl] != '\t')) continue;
        if (ks_unhex(line + hl, key)) {
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

/* Append a pin (the directory is created 0700, the file 0600). */
static inline bool ks_append(const char *path, const char *hostport, const uint8_t key[32]) {
    if (!path || !hostport) return false;
    char dir[1024];
    if ((size_t)snprintf(dir, sizeof(dir), "%s", path) >= sizeof(dir)) return false;
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = '\0';
        if (mkdir(dir, 0700) != 0 && errno != EEXIST) return false;
    }
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return false;
    char hex[65], line[600];
    ks_hex(key, hex);
    int n = snprintf(line, sizeof(line), "%s %s\n", hostport, hex);
    bool ok = n > 0 && (size_t)n < sizeof(line) && write(fd, line, (size_t)n) == (ssize_t)n;
    ok = (close(fd) == 0) && ok;
    return ok;
}

/* The pinning decision for a server key seen in a verified handshake.
 * explicit_hex: SPACEGL_SERVER_KEY, or NULL. pinned (optional) receives the
 * pinned key on a mismatch, for the message. */
static inline KsResult ks_check(const char *path, const char *hostport, const uint8_t key[32],
                                const char *explicit_hex, uint8_t pinned[32]) {
    uint8_t want[32];
    if (explicit_hex && explicit_hex[0]) {
        if (!ks_unhex(explicit_hex, want)) return KS_EXPLICIT_BAD;
        if (pinned) memcpy(pinned, want, 32);
        return CRYPTO_memcmp(want, key, 32) == 0 ? KS_EXPLICIT_OK : KS_EXPLICIT_MISMATCH;
    }
    int r = path ? ks_lookup(path, hostport, want) : -1;
    if (r == 1) {
        if (pinned) memcpy(pinned, want, 32);
        return CRYPTO_memcmp(want, key, 32) == 0 ? KS_PINNED_OK : KS_MISMATCH;
    }
    if (path && r == 0 && ks_append(path, hostport, key)) return KS_NEW_PINNED;
    return KS_NEW_NOT_SAVED;
}

#endif
