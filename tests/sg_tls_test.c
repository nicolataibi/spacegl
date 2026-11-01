/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * sg_tls_test — TLS 1.3 on the game port (src/sg_tls.c, 2026.11.01.11,
 * wire revision 5), on socketpairs:
 *  1. handshake: the server steps it on readiness, the client completes it
 *     and receives the server's Ed25519 identity key from the certificate;
 *     TLS 1.3 is negotiated; data both ways; several packets in one record
 *     stay buffered (sg_tls_pending) and are read without the socket being
 *     readable.
 *  2. a peer that speaks in clear (a deck of wire revision 4: its hello
 *     starts with the int32 PKT_HANDSHAKE) is refused at the first byte.
 *  3. a peer that connects and says nothing: in progress, then listed by
 *     sg_tls_stale_handshakes() after SG_TLS_HANDSHAKE_TIMEOUT_S.
 *  4. concurrency: one thread reads while two others write on the SAME
 *     session (the server's listener and broadcast threads, the deck's
 *     listener and main thread); 8 MB echoed, every byte checked. TSan
 *     builds run it too.
 *  5. without a session sg_net_recv/send are recv/send (the test
 *     socketpairs of the dispatcher tests); sg_tls_detach is harmless.
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include "sg_tls.h"

#define TEST_CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "TEST FAILED: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

typedef struct {
    int fd;
    bool ok;
    uint8_t key[32];
} ClientArg;

static void *client_thread(void *p) {
    ClientArg *a = p;
    a->ok = sg_tls_client_connect(a->fd, 10000, a->key);
    return NULL;
}

/* Drive the server handshake until done or failed (like the event loop). */
static int server_handshake(int fd) {
    for (int i = 0; i < 2000; i++) {
        int r = sg_tls_server_step(fd);
        if (r != 0) return r;
        struct pollfd p = { .fd = fd, .events = (short)(sg_net_wants_write(fd) ? POLLOUT : POLLIN) };
        (void)poll(&p, 1, 10);
    }
    return 0;
}

static bool send_all(int fd, const void *buf, size_t len) {
    const char *p = buf;
    size_t done = 0;
    while (done < len) {
        ssize_t n = sg_net_send(fd, p + done, len - done);
        if (n > 0) {
            done += (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            if (sg_net_wait(fd, true, 5000) <= 0) return false;
            continue;
        }
        return false;
    }
    return true;
}

static bool recv_all(int fd, void *buf, size_t len) {
    char *p = buf;
    size_t done = 0;
    while (done < len) {
        ssize_t n = sg_net_recv(fd, p + done, len - done);
        if (n > 0) {
            done += (size_t)n;
            continue;
        }
        if (n == 0) return false;
        if (errno == EAGAIN || errno == EINTR) {
            if (sg_net_wait(fd, false, 5000) <= 0) return false;
            continue;
        }
        return false;
    }
    return true;
}

/* A connected pair with TLS on both ends; server end in sfd. */
static void tls_pair(int *sfd, int *cfd, const uint8_t pub[32]) {
    int sv[2];
    TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    TEST_CHECK(sg_tls_server_attach(sv[0]));
    TEST_CHECK(sg_tls_active(sv[0]) && !sg_tls_ready(sv[0]));
    ClientArg a = { .fd = sv[1] };
    pthread_t th;
    TEST_CHECK(pthread_create(&th, NULL, client_thread, &a) == 0);
    TEST_CHECK(server_handshake(sv[0]) == 1);
    pthread_join(th, NULL);
    TEST_CHECK(a.ok);
    TEST_CHECK(memcmp(a.key, pub, 32) == 0);
    TEST_CHECK(sg_tls_ready(sv[0]) && sg_tls_ready(sv[1]));
    *sfd = sv[0];
    *cfd = sv[1];
}

static void close_pair(int a, int b) {
    sg_tls_detach(a);
    sg_tls_detach(b);
    close(a);
    close(b);
}

/* --- 4: concurrency ---------------------------------------------------- */
#define STRESS_CHUNK  1500u
#define STRESS_CHUNKS 5592u   /* even: half per writer; 8 MB in all */

typedef struct {
    int fd;
    unsigned id;          /* writer: 0 or 1, its bytes carry it */
    bool ok;
    size_t got[2];        /* reader: bytes seen per writer */
} StressArg;

static pthread_mutex_t g_msg_lock = PTHREAD_MUTEX_INITIALIZER;   /* message atomicity (socket_mutex) */

static void *stress_writer(void *p) {
    StressArg *a = p;
    uint8_t buf[STRESS_CHUNK];
    a->ok = true;
    for (size_t k = 0, sent = 0; k < STRESS_CHUNKS / 2; k++, sent += STRESS_CHUNK) {
        buf[0] = (uint8_t)a->id;
        for (size_t i = 1; i < STRESS_CHUNK; i++) buf[i] = (uint8_t)((sent + i) * 31u + a->id);
        pthread_mutex_lock(&g_msg_lock);
        bool ok = send_all(a->fd, buf, STRESS_CHUNK);
        pthread_mutex_unlock(&g_msg_lock);
        if (!ok) {
            a->ok = false;
            break;
        }
    }
    return NULL;
}

static void *stress_echo(void *p) {
    StressArg *a = p;
    uint8_t buf[STRESS_CHUNK];
    a->ok = true;
    for (size_t n = 0; n < STRESS_CHUNKS; n++)
        if (!recv_all(a->fd, buf, STRESS_CHUNK) || !send_all(a->fd, buf, STRESS_CHUNK)) {
            a->ok = false;
            break;
        }
    return NULL;
}

static void *stress_reader(void *p) {
    StressArg *a = p;
    uint8_t buf[STRESS_CHUNK];
    a->ok = true;
    for (size_t n = 0; n < STRESS_CHUNKS; n++) {
        if (!recv_all(a->fd, buf, STRESS_CHUNK) || buf[0] > 1) {
            a->ok = false;
            break;
        }
        unsigned id = buf[0];
        size_t base = a->got[id];
        for (size_t i = 1; i < STRESS_CHUNK; i++)
            if (buf[i] != (uint8_t)((base + i) * 31u + id)) {
                a->ok = false;
                return NULL;
            }
        a->got[id] += STRESS_CHUNK;
    }
    return NULL;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);   /* as the server and the deck (sg_tls.h) */
    setvbuf(stdout, NULL, _IOLBF, 0);
    uint8_t seed[32], pub[32];
    TEST_CHECK(RAND_bytes(seed, 32) == 1);
    EVP_PKEY *k = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, seed, 32);
    size_t pl = 32;
    TEST_CHECK(k && EVP_PKEY_get_raw_public_key(k, pub, &pl) == 1 && pl == 32);
    EVP_PKEY_free(k);
    TEST_CHECK(sg_tls_server_init(seed));
    TEST_CHECK(sg_tls_client_init());

    /* 1. handshake, data, buffered packets */
    {
        int s, c;
        tls_pair(&s, &c, pub);
        const char *suite = sg_tls_cipher(s);
        TEST_CHECK(strncmp(suite, "TLS_", 4) == 0);   /* a TLS 1.3 suite name */
        printf("  cipher suite: %s\n", suite);
        TEST_CHECK(send_all(c, "HELLO-SERVER", 12));
        char b[32] = { 0 };
        TEST_CHECK(recv_all(s, b, 12) && memcmp(b, "HELLO-SERVER", 12) == 0);
        /* three 4-byte "packets" in one record: the first read takes 4,
         * the other 8 stay in the session, readable with no socket data */
        TEST_CHECK(send_all(s, "AAAABBBBCCCC", 12));
        TEST_CHECK(recv_all(c, b, 4) && memcmp(b, "AAAA", 4) == 0);
        TEST_CHECK(sg_tls_pending(c) == 8);
        struct pollfd p = { .fd = c, .events = POLLIN };
        TEST_CHECK(poll(&p, 1, 0) == 0);              /* the socket has nothing */
        TEST_CHECK(sg_net_wait(c, false, 0) == 1);    /* but the seam knows */
        TEST_CHECK(recv_all(c, b, 8) && memcmp(b, "BBBBCCCC", 8) == 0);
        TEST_CHECK(sg_tls_pending(c) == 0);
        /* orderly close: the peer reads 0 */
        sg_tls_detach(s);
        close(s);
        ssize_t n;
        for (int i = 0; i < 100; i++) {
            n = sg_net_recv(c, b, sizeof(b));
            if (!(n < 0 && errno == EAGAIN)) break;
            (void)sg_net_wait(c, false, 50);
        }
        TEST_CHECK(n == 0);
        sg_tls_detach(c);
        close(c);
        printf("  [1] handshake, identity key, TLS 1.3, buffered packets: OK\n");
    }

    /* 2. a deck in clear (wire revision 4) is refused at the first byte */
    {
        int sv[2];
        TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
        TEST_CHECK(sg_tls_server_attach(sv[0]));
        TEST_CHECK(sg_tls_server_step(sv[0]) == 0);   /* nothing yet */
        int32_t hello = 6;                             /* PKT_HANDSHAKE */
        TEST_CHECK(write(sv[1], &hello, sizeof(hello)) == (ssize_t)sizeof(hello));
        TEST_CHECK(sg_tls_server_step(sv[0]) == -1);
        /* no application data reaches the dispatcher meanwhile */
        char b[4];
        TEST_CHECK(sg_net_recv(sv[0], b, 4) == -1 && errno == EAGAIN);
        close_pair(sv[0], sv[1]);
        printf("  [2] a deck in clear is refused: OK\n");
    }

    /* 3. silent peer: listed as stale after the timeout */
    {
        int sv[2];
        TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
        TEST_CHECK(sg_tls_server_attach(sv[0]));
        TEST_CHECK(sg_tls_server_step(sv[0]) == 0);
        int fds[8];
        time_t now = time(NULL);
        TEST_CHECK(sg_tls_stale_handshakes(now, fds, 8) == 0);
        int n = sg_tls_stale_handshakes(now + SG_TLS_HANDSHAKE_TIMEOUT_S + 2, fds, 8);
        TEST_CHECK(n == 1 && fds[0] == sv[0]);
        close_pair(sv[0], sv[1]);
        TEST_CHECK(sg_tls_stale_handshakes(now + 100, fds, 8) == 0);   /* detached: gone */
        printf("  [3] a silent peer is listed after %d s: OK\n", SG_TLS_HANDSHAKE_TIMEOUT_S);
    }

    /* 4. one reader and two writers on the same session */
    {
        int s, c;
        tls_pair(&s, &c, pub);
        StressArg echo = { .fd = s };
        StressArg w0 = { .fd = c, .id = 0 }, w1 = { .fd = c, .id = 1 };
        StressArg rd = { .fd = c };
        pthread_t te, t0, t1, tr;
        TEST_CHECK(pthread_create(&te, NULL, stress_echo, &echo) == 0);
        TEST_CHECK(pthread_create(&tr, NULL, stress_reader, &rd) == 0);
        TEST_CHECK(pthread_create(&t0, NULL, stress_writer, &w0) == 0);
        TEST_CHECK(pthread_create(&t1, NULL, stress_writer, &w1) == 0);
        pthread_join(t0, NULL);
        pthread_join(t1, NULL);
        pthread_join(tr, NULL);
        pthread_join(te, NULL);
        TEST_CHECK(w0.ok && w1.ok && echo.ok && rd.ok);
        TEST_CHECK(rd.got[0] == STRESS_CHUNKS / 2 * STRESS_CHUNK && rd.got[1] == rd.got[0]);
        close_pair(s, c);
        printf("  [4] 1 reader + 2 writers on one session, %u bytes echoed: OK\n", STRESS_CHUNKS * STRESS_CHUNK);
    }

    /* 5. no session: plain recv/send */
    {
        int sv[2];
        TEST_CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
        TEST_CHECK(!sg_tls_active(sv[0]) && sg_tls_pending(sv[0]) == 0);
        TEST_CHECK(strcmp(sg_tls_cipher(sv[0]), "none") == 0);
        TEST_CHECK(sg_net_send(sv[0], "plain", 5) == 5);
        char b[8] = { 0 };
        TEST_CHECK(sg_net_wait(sv[1], false, 1000) == 1);
        TEST_CHECK(sg_net_recv(sv[1], b, sizeof(b)) == 5 && memcmp(b, "plain", 5) == 0);
        sg_tls_detach(sv[0]);   /* harmless */
        close(sv[0]);
        close(sv[1]);
        printf("  [5] no session: plain I/O: OK\n");
    }

    printf("PASS: sg_tls\n");
    return 0;
}
