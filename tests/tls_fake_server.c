/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * tls_fake_server — the two servers a deck of wire revision 5 must refuse,
 * for tests/deck_e2e_test.sh step 10 (2026.11.01.12). It listens on the
 * game port (DEFAULT_PORT), serves ONE connection and exits:
 *
 *   plain  a server of wire revision 4 or older: no TLS. It reads what
 *          the deck sends (its TLS ClientHello) and closes, as an old
 *          server does with an unknown packet type. The deck must report
 *          "No TLS 1.3 session with the server".
 *   relay  a man in the middle that terminates TLS with an identity of its
 *          own (A) and answers the handshake v2 with the deployment key
 *          K, signed by ANOTHER identity (B) — what a holder of K relaying
 *          a real server's signed handshake through its own TLS tunnel
 *          would present. The deck accepts A at TLS (first contact) and
 *          must refuse the handshake: "signed by another identity than
 *          the TLS certificate".
 *   reset  a server that dies in the middle: TLS with one identity, the
 *          deck's hello read, then the connection reset (RST). The deck
 *          must report the broken link ("read_all failed") and the failed
 *          handshake, not hang.
 *
 * K comes from the environment exactly as for the deck (load_master_key,
 * include/network.h). Prints "READY" once listening.
 * Usage: tls_fake_server plain|relay|reset
 */
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/rand.h>

#include "handshake_v2.h"
#include "network.h"
#include "sg_tls.h"

static int listen_game_port(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(DEFAULT_PORT);
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(fd, 4) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool recv_exact(int fd, void *buf, size_t len) {
    char *p = buf;
    size_t done = 0;
    while (done < len) {
        ssize_t n = sg_net_recv(fd, p + done, len - done);
        if (n > 0) {
            done += (size_t)n;
            continue;
        }
        if (n == 0) return false;
        if (sg_net_wait(fd, false, 10000) <= 0) return false;
    }
    return true;
}

static bool send_exact(int fd, const void *buf, size_t len) {
    const char *p = buf;
    size_t done = 0;
    while (done < len) {
        ssize_t n = sg_net_send(fd, p + done, len - done);
        if (n > 0) {
            done += (size_t)n;
            continue;
        }
        if (n < 0 && sg_net_wait(fd, true, 10000) > 0) continue;
        return false;
    }
    return true;
}

/* Wait until the deck closes (or 20 s). */
static void drain(int fd) {
    char b[512];
    for (int i = 0; i < 200; i++) {
        ssize_t n = sg_net_recv(fd, b, sizeof(b));
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) return;
        if (n < 0) (void)sg_net_wait(fd, false, 100);
    }
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    bool reset = argc > 1 && strcmp(argv[1], "reset") == 0;
    bool relay = reset || (argc > 1 && strcmp(argv[1], "relay") == 0);   /* both speak TLS */
    if (argc < 2 || (!relay && strcmp(argv[1], "plain") != 0)) {
        fprintf(stderr, "usage: %s plain|relay|reset\n", argv[0]);
        return 2;
    }
    uint8_t k[32], seed_a[32], seed_b[32];
    if (load_master_key(k) != 0) return 2;
    if (RAND_bytes(seed_a, 32) != 1 || RAND_bytes(seed_b, 32) != 1) return 2;
    if (relay && !sg_tls_server_init(seed_a)) {
        fprintf(stderr, "tls_fake_server: no TLS context\n");
        return 2;
    }
    int lfd = listen_game_port();
    if (lfd < 0) {
        perror("tls_fake_server: listen");
        return 2;
    }
    printf("READY\n");
    int fd = accept(lfd, NULL, NULL);
    if (fd < 0) return 2;

    if (!relay) {
        /* an old server: the ClientHello is an unknown packet type */
        char b[512];
        struct pollfd p = { .fd = fd, .events = POLLIN };
        if (poll(&p, 1, 10000) > 0) (void)recv(fd, b, sizeof(b), 0);
        close(fd);
        close(lfd);
        printf("DONE plain\n");
        return 0;
    }

    int rc = 1;
    if (sg_tls_server_attach(fd)) {
        int hs = 0;
        for (int i = 0; i < 1000 && hs == 0; i++) {
            hs = sg_tls_server_step(fd);
            if (hs == 0) {
                struct pollfd p = { .fd = fd, .events = (short)(sg_net_wants_write(fd) ? POLLOUT : POLLIN) };
                (void)poll(&p, 1, 10);
            }
        }
        PacketHandshake hello;
        uint8_t gvk[32], reply[HANDSHAKE_V2_REPLY_LEN], session[32];
        (void)RAND_bytes(gvk, 32);
        if (reset) {
            if (hs == 1 && recv_exact(fd, &hello, sizeof(hello))) {
                struct linger lg = { .l_onoff = 1, .l_linger = 0 };   /* close() sends RST */
                (void)setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
                rc = 0;
            }
            close(fd);   /* no close_notify: the session just dies */
            close(lfd);
            printf("DONE reset\n");
            return rc;
        }
        if (hs == 1 && recv_exact(fd, &hello, sizeof(hello)) && hello.type == PKT_HANDSHAKE &&
            hello.pubkey_len == HANDSHAKE_V2_MARKER &&
            hs_v2_server_reply(k, hello.pubkey, gvk, seed_b, reply, session)) {
            int32_t t = PKT_HANDSHAKE;
            if (send_exact(fd, &t, sizeof(t)) && send_exact(fd, reply, sizeof(reply))) rc = 0;
        }
        drain(fd);
    }
    sg_tls_detach(fd);
    close(fd);
    close(lfd);
    printf("DONE relay %s\n", rc == 0 ? "answered" : "failed");
    return rc;
}
