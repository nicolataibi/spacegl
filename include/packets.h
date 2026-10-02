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

#ifndef PACKETS_H
#define PACKETS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "conn.h"

/*
 * Shared client-packet dispatcher — one implementation for both I/O modes.
 *
 * The 4-byte packet type is identified by the caller (the legacy loop with
 * a blocking read_all(), the async state machine with its RX buffer) and
 * handed here together with the body source:
 *
 *   - LEGACY (--io=legacy, default): io->rx == NULL. Body bytes are read
 *     from the socket with read_all() (blocking, bounded 5 s poll) exactly
 *     as the pre-refactor inline loop did;
 *   - ASYNC (--io=async): io->rx points at the bytes already staged by the
 *     conn state machine (starting just after the 4-byte type) and the
 *     handler never touches the socket at all.
 *
 * Returns 0 if the connection may continue, 1 if the dispatcher closed it
 * (the caller must stop processing the event).
 */

typedef struct {
    int epoll_fd;
    int fd;
    Conn *conn;        /* ASYNC: owning session (teardown via conn_close); LEGACY: NULL */
    const uint8_t *rx; /* ASYNC: body bytes, consumed by pkt_read_body();   LEGACY: NULL */
    size_t rx_len;     /* ASYNC: body bytes remaining                       LEGACY: 0   */
} PktIO;

int dispatch_packet(PktIO *io, int32_t type);

/* conn_dispatch_fn target wired into the async session layer by main(). */
int packets_conn_dispatch(int epoll_fd, int fd, Conn *c, int32_t type, const uint8_t *rx, size_t len);

/* Captain-name whitelist: [A-Za-z0-9_-]{1,32}. Anything else (in
 * particular '/', '\\' and '.') is refused BEFORE any file I/O uses the
 * name (path-injection guard). Moved here from spacegl_server.c, where it
 * was static and only reachable by the inline dispatcher. */
int sanitize_captain_name(const char *name);

#endif
