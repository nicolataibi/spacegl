/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * Async per-connection I/O session layer (the --io=async mode).
 * See include/conn.h for the design rationale.
 */

#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include "server_internal.h"
#include "conn.h"

/* Same event capacity as the legacy loop (spacegl_server.c). */
#define MAX_EVENTS (MAX_CLIENTS + GAME_MAX_PLAYERS)

/* Largest inbound packet: a full-length PKT_MESSAGE (packed wire layout:
 * 223 fixed bytes + up to 65535 bytes of payload). */
#define CONN_RX_BUF_SIZE  (sizeof(PacketMessage))

/* Staged outbound replies, drained when epoll reports EPOLLOUT. The
 * largest reply the dispatcher stages is a PacketQueryKey (104 bytes);
 * 4 KiB covers a dozen of them before a not-reading receiver is dropped. */
#define CONN_TX_BUF_SIZE  4096

/* Session pool: the 16 authenticated players plus headroom for
 * unauthenticated (pre-handshake) connections. Exhaustion is also a DoS
 * guard: the newest connection is dropped instead of letting open fds
 * grow without bound. */
#define MAX_CONNS (4 * MAX_CLIENTS)

typedef enum {
    CONN_S_HEADER,   /* accumulating the 4-byte packet type           */
    CONN_S_BODY,     /* accumulating the fixed-size packet body       */
    CONN_S_TEXT      /* accumulating the PKT_MESSAGE text payload     */
} ConnState;

struct Conn {
    int fd;
    bool in_use;
    ConnState state;
    int32_t type;      /* valid once CONN_S_HEADER completes */
    size_t need;       /* bytes still expected in the current state */
    size_t have;       /* bytes staged in rx_buf for the current packet */
    uint8_t rx_buf[CONN_RX_BUF_SIZE];
    bool tx_pending;   /* true while tx_buf holds unsent bytes */
    size_t tx_have;    /* bytes of tx_buf already sent */
    size_t tx_total;   /* bytes staged in tx_buf */
    uint8_t tx_buf[CONN_TX_BUF_SIZE];
};

static Conn g_conns[MAX_CONNS];
static conn_dispatch_fn g_dispatch = NULL;

SpaceglIOMode g_io_mode = SPACEGL_IO_LEGACY; /* main() may select ASYNC */

void conn_set_dispatch(conn_dispatch_fn fn) {
    g_dispatch = fn;
}

/* ------------------------------------------------------------------ */
/* Session pool                                                       */
/* ------------------------------------------------------------------ */

Conn *conn_alloc(int fd) {
    Conn *c = NULL;
    for (int i = 0; i < MAX_CONNS; i++) {
        if (!g_conns[i].in_use) { c = &g_conns[i]; break; }
    }
    if (!c) return NULL;

    /* Explicit non-blocking I/O for the session: conn_pump() relies on
     * EAGAIN/EWOULDBLOCK to yield to the epoll loop instead of blocking
     * it (fcntl(2)). */
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl != -1) fcntl(fd, F_SETFL, fl | O_NONBLOCK);

    memset(c, 0, sizeof(*c));
    c->fd = fd;
    c->in_use = true;
    c->state = CONN_S_HEADER;
    c->need = sizeof(int32_t);
    return c;
}

Conn *conn_find(int fd) {
    for (int i = 0; i < MAX_CONNS; i++)
        if (g_conns[i].in_use && g_conns[i].fd == fd) return &g_conns[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Teardown                                                           */
/* ------------------------------------------------------------------ */

void reap_player_fd(int fd, ConnCloseReason reason) {
    if (reason == CONN_CLOSE_SILENT) return;
    pthread_mutex_lock(&game_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (players[i].socket != fd) continue;
        if (reason == CONN_CLOSE_DEPARTURE) {
            /* Log the departure before clearing the socket */
            time_t now = time(NULL);
            struct tm *t = localtime(&now);
            char time_str[64];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);
            SG_NOTICE(SG_CAT_CLIENT, "captain %s disconnected (slot %d)",
                      players[i].name[0] ? players[i].name : "?", i);
            slog("\033[1;35m[DISCONNECT]\033[0m Captain \033[1;37m%-15s\033[0m has left the galaxy.    [\033[1;33m%s\033[0m]\n",
                 players[i].name[0] ? players[i].name : "Unknown", time_str);
        }
        players[i].socket = 0;
        players[i].active = 0;
        players[i].radio_lock_target = 0;
        memset(players[i].session_key, 0, 32);
        if (reason == CONN_CLOSE_DEPARTURE) save_galaxy();
        break;
    }
    pthread_mutex_unlock(&game_mutex);
}

void conn_close(int epoll_fd, Conn *c, ConnCloseReason reason) {
    if (!c || !c->in_use) return;
    c->in_use = false;
    c->tx_pending = false;
    int fd = c->fd;
    c->fd = -1;
    reap_player_fd(fd, reason);
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
}

/* ------------------------------------------------------------------ */
/* Outbound staging (never blocks the loop)                           */
/* ------------------------------------------------------------------ */

static void conn_set_events(int epoll_fd, Conn *c, bool want_out) {
    struct epoll_event ev;
    ev.events = EPOLLIN | (want_out ? EPOLLOUT : 0);
    ev.data.fd = c->fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, c->fd, &ev) == -1) {
        /* Losing the EPOLLOUT interest only delays the pending reply;
         * the session stays alive and the next transport event (the
         * pump) will still process it or close it. Never close from
         * here: conn_drain_tx() is also reachable from conn_send()
         * while the dispatcher holds game_mutex (the handshake ACK),
         * and conn_close() re-acquires it via reap_player_fd(). */
        return;
    }
}

static void conn_drain_tx(int epoll_fd, Conn *c) {
    if (!c->tx_pending) return;
    for (;;) {
        ssize_t n = send(c->fd, c->tx_buf + c->tx_have, c->tx_total - c->tx_have, MSG_NOSIGNAL);
        if (n > 0) { c->tx_have += (size_t)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        /* Hard error (EPIPE/ECONNRESET...): the session is doomed, but
         * the close is deferred to the next transport event — the pump's
         * recv() will see the same error or the peer FIN. Closing here
         * would re-acquire game_mutex from a context that may already
         * hold it (the handshake ACK is staged under the lock). */
        c->tx_pending = false;
        c->tx_have = c->tx_total = 0;
        return;
    }
    if (c->tx_have >= c->tx_total) {
        c->tx_have = c->tx_total = 0;
        c->tx_pending = false;
        conn_set_events(epoll_fd, c, false);
    } else {
        conn_set_events(epoll_fd, c, true);
    }
}

int conn_send(int epoll_fd, Conn *c, const void *data, size_t len) {
    if (!c || !c->in_use) return -1;
    if (len > CONN_TX_BUF_SIZE) return -1;
    if (c->tx_total + len > CONN_TX_BUF_SIZE) {
        /* The receiver is not draining its socket: drop it instead of
         * stalling the loop. (The handshake ACK — the only conn_send
         * issued while game_mutex is held — always sees an empty
         * staging buffer, so the close below is lock-safe.) */
        SG_WARNING(SG_CAT_NETWORK, "fd %d: reply staging overflow (%zu bytes pending): dropping connection",
                   c->fd, c->tx_total);
        conn_close(epoll_fd, c, CONN_CLOSE_DEPARTURE);
        return -1;
    }
    memcpy(c->tx_buf + c->tx_total, data, len);
    c->tx_total += len;
    c->tx_pending = true;
    conn_drain_tx(epoll_fd, c);
    return c->in_use ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Inbound state machine (never blocks the loop)                      */
/* ------------------------------------------------------------------ */

/* Fixed-size body expected after the 4-byte type, per packet kind.
 * 0 marks an unknown/unsupported type: the connection is dropped, as in
 * the legacy path (the stream cannot be resynchronized from there). */
static size_t conn_body_size(int32_t type) {
    switch (type) {
    case PKT_HANDSHAKE: return sizeof(PacketHandshake) - sizeof(int32_t);
    case PKT_QUERY_KEY: return sizeof(PacketQueryKey) - sizeof(int32_t);
    case PKT_QUERY:
    case PKT_LOGIN:     return sizeof(PacketLogin) - sizeof(int32_t);
    case PKT_COMMAND:   return sizeof(PacketCommand) - sizeof(int32_t);
    case PKT_MESSAGE:   return (size_t)(offsetof(PacketMessage, text) - (ptrdiff_t)sizeof(int32_t));
    default:            return 0;
    }
}

/* Hand a complete packet to the dispatcher and restart the machine at
 * CONN_S_HEADER. Returns 0 if the connection may continue, 1 if it was
 * closed (by the dispatcher, or because no dispatcher is registered). */
static int conn_finish_packet(int epoll_fd, Conn *c) {
    if (!g_dispatch) {
        SG_WARNING(SG_CAT_NETWORK, "fd %d: complete packet but no dispatcher registered: dropping connection",
                   c->fd);
        conn_close(epoll_fd, c, CONN_CLOSE_SILENT);
        return 1;
    }
    int closed = g_dispatch(epoll_fd, c->fd, c, c->type, c->rx_buf + sizeof(int32_t),
                            c->have - sizeof(int32_t));
    if (closed) return 1;
    c->state = CONN_S_HEADER;
    c->need = sizeof(int32_t);
    c->have = 0;
    return 0;
}

void conn_pump(int epoll_fd, Conn *c) {
    for (;;) {
        /* A packet phase is complete when `need` reaches zero. */
        if (c->need == 0) {
            switch (c->state) {
            case CONN_S_HEADER: {
                /* Header complete: interpret the type. */
                int32_t t;
                memcpy(&t, c->rx_buf, sizeof(t));
                size_t body = conn_body_size(t);
                if (body == 0) {
                    SG_WARNING(SG_CAT_NETWORK, "fd %d: unknown packet type %d: dropping connection",
                               c->fd, t);
                    conn_close(epoll_fd, c, CONN_CLOSE_SILENT);
                    return;
                }
                c->type = t;
                c->state = CONN_S_BODY;
                c->need = body;
                continue;
            }
            case CONN_S_BODY:
                if (c->type == PKT_MESSAGE) {
                    /* Validate the attacker-controlled text length BEFORE
                     * staging the variable payload — same canonical check
                     * as the legacy path and the client
                     * (packet_message_length_valid, network.h). */
                    int32_t mlen = 0;
                    memcpy(&mlen, c->rx_buf + offsetof(PacketMessage, length), sizeof(mlen));
                    if (!packet_message_length_valid(mlen)) {
                        SG_WARNING(SG_CAT_SECURITY,
                                   "fd %d: PKT_MESSAGE length out of range (%d): dropping connection",
                                   c->fd, mlen);
                        conn_close(epoll_fd, c, CONN_CLOSE_REAP);
                        return;
                    }
                    if (mlen > 0) {
                        c->state = CONN_S_TEXT;
                        c->need = (size_t)mlen;
                        continue;
                    }
                    /* mlen == 0: the packet is already complete. */
                }
                if (conn_finish_packet(epoll_fd, c)) return;
                continue;
            case CONN_S_TEXT:
                if (conn_finish_packet(epoll_fd, c)) return;
                continue;
            }
        }

        /* Fill the current phase from whatever is available now. */
        ssize_t n = recv(c->fd, c->rx_buf + c->have, c->need, 0);
        if (n > 0) {
            c->have += (size_t)n;
            c->need -= (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /* No data right now: the partial packet stays staged in
             * rx_buf and the next EPOLLIN resumes exactly here. This is
             * the whole point of the state machine — the loop is never
             * blocked by a slow (or malicious) client. */
            return;
        }
        /* n == 0: orderly peer close; n < 0: transport error
         * (ECONNRESET, or a send failure deferred by conn_drain_tx).
         * Either way the session ends; the partial packet is discarded
         * with it. */
        conn_close(epoll_fd, c, CONN_CLOSE_DEPARTURE);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Async event loop                                                   */
/* ------------------------------------------------------------------ */

void run_epoll_loop_async(int server_fd, int epoll_fd) {
    struct sockaddr_in addr;
    socklen_t adlen = sizeof(addr);
    struct epoll_event ev, events[MAX_EVENTS];

    /* The listener is non-blocking too: a spurious EPOLLIN (e.g. after a
     * peer RST) must not be able to block the loop on accept(). */
    int fl = fcntl(server_fd, F_GETFL, 0);
    if (fl != -1) fcntl(server_fd, F_SETFL, fl | O_NONBLOCK);
    SG_SUCCESS(SG_CAT_NETWORK, "event loop: ASYNC mode (non-blocking per-connection state machine)");

    while (g_running) {
        /* 200ms timeout so the loop can observe g_running for a graceful stop */
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 200);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int n = 0; n < nfds; ++n) {
            int fd = events[n].data.fd;

            if (fd == server_fd) {
                /* Accept everything pending on the backlog. */
                for (;;) {
                    int new_socket = accept(server_fd, (struct sockaddr *)&addr, (socklen_t *)&adlen);
                    if (new_socket == -1) {
                        if (errno == EAGAIN || errno == ECONNABORTED) break;
                        perror("accept");
                        break;
                    }
                    Conn *c = conn_alloc(new_socket);
                    if (!c) {
                        /* Connection pool exhausted: drop the newest
                         * connection (bounded-resource DoS guard). */
                        SG_WARNING(SG_CAT_NETWORK, "connection pool exhausted: dropping fd %d", new_socket);
                        close(new_socket);
                        continue;
                    }
                    ev.events = EPOLLIN;
                    ev.data.fd = new_socket;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_socket, &ev) == -1) {
                        perror("epoll_ctl: new_socket");
                        c->in_use = false;
                        c->fd = -1;
                        close(new_socket);
                    } else {
                        SG_TRACE4(SG_CAT_NETWORK, "new connection accepted (fd %d)", new_socket);
                        LOG_DEBUG("New connection accepted: FD %d\n", new_socket);
                    }
                }
                continue;
            }

            Conn *c = conn_find(fd);
            if (!c) {
                /* Stale or recycled fd with no live session: nothing
                 * safe to do (conn_close() already closed the fd). */
                continue;
            }
            if ((events[n].events & EPOLLOUT) && c->in_use) {
                conn_drain_tx(epoll_fd, c);
                if (!c->in_use) continue;
            }
            if (c->in_use && (events[n].events & (EPOLLIN | EPOLLHUP | EPOLLERR)))
                conn_pump(epoll_fd, c);
        }
    }
}
