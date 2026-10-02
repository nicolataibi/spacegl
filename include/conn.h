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

#ifndef CONN_H
#define CONN_H

#include <stdint.h>
#include <stddef.h>
#include <signal.h>
#include <stdbool.h>

/* Graceful-shutdown flag: defined in spacegl_server.c (set by the signal
 * handler). Exported here so the async event loop (run_epoll_loop_async)
 * can observe it exactly like the legacy loop does. */
extern volatile sig_atomic_t g_running;

/* Event-loop I/O strategy, selected with --io=legacy|async (default
 * legacy, the current bounded behavior). Set by main() before the loop
 * starts; read-only afterwards. */
typedef enum {
    SPACEGL_IO_LEGACY = 0,
    SPACEGL_IO_ASYNC  = 1
} SpaceglIOMode;
extern SpaceglIOMode g_io_mode;

/*
 * Async (non-blocking) per-connection I/O session layer — the --io=async
 * mode of the server's single-threaded epoll loop.
 *
 * The legacy loop reads every client packet with a blocking read_all()
 * (src/server/net.c) whose tactical mitigation is a poll() with a 5 s
 * timeout: a client that sends a header and then goes silent holds the
 * whole loop for up to 5 seconds, freezing every other player (an
 * app-level DoS in a 60 Hz real-time engine). This layer is the
 * professional alternative:
 *
 *   1. accepted client sockets are explicitly non-blocking
 *      (fcntl(fd, F_SETFL, O_NONBLOCK));
 *   2. the request handler never waits on a socket — no while/poll cycle
 *      inside it;
 *   3. a recv() that returns EAGAIN/EWOULDBLOCK mid-packet stages the
 *      bytes already read into the connection's persistent RX buffer and
 *      returns to the epoll loop immediately; when EPOLLIN fires again,
 *      the state machine (CONN_S_HEADER -> CONN_S_BODY -> CONN_S_TEXT)
 *      resumes from the saved cursor and hands the packet to the
 *      dispatcher only once it is complete.
 *
 * A stalled (or malicious) client can at most occupy its own session; it
 * can never block the loop, hence never any other player.
 *
 * Only the main (epoll) thread ever touches a Conn — no locking is
 * needed. Small outbound replies are staged in a per-connection TX buffer
 * and drained when epoll reports EPOLLOUT, so the handler never blocks on
 * send() either (see conn_send).
 */

typedef struct Conn Conn; /* opaque: only conn.c reads or writes it */

typedef enum {
    CONN_CLOSE_SILENT,    /* close only (pre-auth: bad handshake signature,
                             server full, unknown packet type) */
    CONN_CLOSE_REAP,      /* close + silently reclaim the bound player slot
                             (rejects after the slot was reserved) */
    CONN_CLOSE_DEPARTURE  /* close + departure log + reclaim + save_galaxy()
                             (peer FIN/RST, transport error) */
} ConnCloseReason;

/* Packet-complete callback, registered by main() (packets.c). Receives
 * the session fd, the owning session, the packet type and the body bytes
 * (starting just after the 4-byte type). Returns 0 if the connection may
 * continue, non-zero if it closed the connection. */
typedef int (*conn_dispatch_fn)(int epoll_fd, int fd, Conn *c, int32_t type, const uint8_t *rx, size_t len);
void conn_set_dispatch(conn_dispatch_fn fn);

/* Pool + session lifecycle (main thread only). */
Conn *conn_alloc(int fd);  /* also makes the fd non-blocking; NULL if the pool is exhausted */
Conn *conn_find(int fd);
void  conn_close(int epoll_fd, Conn *c, ConnCloseReason reason);

/* Shared slot-reclamation used by conn_close() and by the legacy dispatcher
 * teardown (src/server/packets.c): clears the player slot bound to fd
 * (socket, active, radio_lock_target, session_key); DEPARTURE additionally
 * logs the departure and persists the galaxy state. No-op for SILENT or
 * when no slot is bound to fd. */
void  reap_player_fd(int fd, ConnCloseReason reason);

/* Non-blocking pump: consumes whatever the socket currently has, advances
 * the RX state machine and dispatches complete packets. Never blocks. */
void  conn_pump(int epoll_fd, Conn *c);

/* Stage a small outbound reply and try to send it right away; whatever
 * does not fit in the kernel send buffer is kept staged and drained later
 * on EPOLLOUT (the loop registers EPOLLOUT for the session). Returns 0 on
 * success (fully sent or staged), -1 if the connection was dropped. */
int   conn_send(int epoll_fd, Conn *c, const void *data, size_t len);

/* The async event loop (listening socket + all client sessions). Runs
 * until g_running is cleared, then returns so main() performs the same
 * graceful shutdown as the legacy path. */
void  run_epoll_loop_async(int server_fd, int epoll_fd);

#endif
