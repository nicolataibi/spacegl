/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * Thread-safe console logging.
 *
 * The server emits colored (ANSI) log lines from several threads at the same
 * time: the 60 Hz game-tick thread, the main epoll/accept loop, the thread
 * pool tasks (RESCUE / CONNECTION / DISCONNECT / broadcast / save) and the
 * telemetry uplink.  Uncoordinated printf() calls from those threads can
 * interleave and corrupt the console output.
 *
 * Every runtime log event is therefore aggregated through slog(): the whole
 * message is formatted into a single buffer and emitted with a single
 * write(2) to stdout while holding a dedicated logging mutex.  This makes
 * each log event atomic and guarantees a total, stable ordering of the
 * output, regardless of the emitting thread.
 */

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "server_internal.h"

#define SLOG_BUF_SIZE 4096

static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Thread-safe, atomic console log.
 *
 * Equivalent to printf() for the caller, but:
 *   - formats the entire message into one buffer,
 *   - writes it to stdout with a single write(2) (no partial lines),
 *   - serializes all log events through g_log_mutex.
 *
 * Must only be used for console diagnostics, never for network I/O.
 */
void slog(const char *fmt, ...) {
    char buf[SLOG_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n <= 0) {
        return;
    }
    if (n >= (int)sizeof(buf)) {
        n = (int)sizeof(buf) - 1;
    }

    pthread_mutex_lock(&g_log_mutex);
    size_t off = 0;
    while (off < (size_t)n) {
        ssize_t w = write(STDOUT_FILENO, buf + off, (size_t)n - off);
        if (w <= 0) {
            if (errno == EINTR) continue;
            break;
        }
        off += (size_t)w;
    }
    pthread_mutex_unlock(&g_log_mutex);
}

/*
 * Make stdout line-buffered even when redirected to a pipe or file.
 * Called once from main() before any logging thread starts, so that
 * (occasional) legacy printf() output is flushed as soon as a newline is
 * written and cannot be reordered against slog() events.
 */
void log_init(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
}
