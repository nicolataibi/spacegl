/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * sg_tls.h — TLS 1.3 on the game port (2026.11.01.11, wire revision 5).
 *
 * Every deck <-> server connection on port 5000 runs inside TLS 1.3. The
 * server's certificate is generated at start-up, in memory, from its
 * Ed25519 identity key (server_identity.key, wire revision 4): the deck
 * trusts no certificate authority, it compares the certificate's key with
 * the key it pinned for that server (include/known_servers.h), so nothing
 * changes for the captain. Inside TLS everything is as before: handshake
 * v2 with the deployment key, login, radio frequencies, signatures.
 *
 * One seam for all the I/O on game sockets: sg_net_recv() / sg_net_send()
 * behave like recv()/send() on a NON-BLOCKING socket (EAGAIN when nothing
 * can be done now, 0 on an orderly close), with TLS when the fd carries a
 * TLS session and in clear otherwise (test socketpairs, which never
 * attach one). An OpenSSL SSL object must not be used by two threads at
 * once, and here one thread reads a connection while others write to it:
 * every SSL call runs under the connection's own lock, never while
 * waiting (the waits are poll() calls outside the lock), so a reader
 * waiting for data never holds back a writer.
 *
 * Sessions are kept in a table indexed by fd whose entries are never
 * freed (only their SSL object is), so a thread holding an fd number that
 * was just closed finds an entry with no session, not freed memory.
 */
#ifndef SG_TLS_H
#define SG_TLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

/* Seconds a server-side TLS handshake may take before the connection is
 * closed (a client that connects and says nothing). */
#define SG_TLS_HANDSHAKE_TIMEOUT_S 10

/* Server: the TLS context, with a self-signed certificate made from the
 * Ed25519 identity (seed = the 32-byte private key). false on failure. */
bool sg_tls_server_init(const uint8_t identity_seed[32]);

/* Client: the TLS context (no certificate authority: the caller pins the
 * server's key, sg_tls_peer_key). */
bool sg_tls_client_init(void);

/* Server, after accept(): the fd becomes non-blocking and gets a TLS
 * session in the accepting state. false on failure (close the fd). */
bool sg_tls_server_attach(int fd);

/* Server: advance the TLS handshake of fd. 1 done (application data may
 * follow), 0 in progress (wait for the next readiness event), -1 failed
 * or refused (close the connection). A peer that does not speak TLS (a
 * deck of wire revision 4 or older sends its hello in clear) is refused
 * with a log line. */
int sg_tls_server_step(int fd);

/* Client, after connect(): TLS handshake within timeout_ms; on success
 * peer_key receives the server's Ed25519 public key (from its
 * certificate) and the fd is left non-blocking. false on failure. */
bool sg_tls_client_connect(int fd, int timeout_ms, uint8_t peer_key[32]);

/* fd carries a TLS session (in handshake or established). */
bool sg_tls_active(int fd);
/* fd carries a TLS session whose handshake is complete. */
bool sg_tls_ready(int fd);
/* The negotiated cipher suite of fd's session ("none" without TLS). */
const char *sg_tls_cipher(int fd);
/* Decrypted bytes buffered in the session (readable without the socket
 * being readable: an event loop must drain them). 0 without TLS. */
size_t sg_tls_pending(int fd);

/* recv()/send() of the game protocol, see above. Without TLS send never
 * raises SIGPIPE (MSG_NOSIGNAL); OpenSSL writes with write(), so a process
 * using TLS ignores SIGPIPE (the server and the deck do). With TLS, -1/EAGAIN after a write that needs to read (or the
 * reverse) is reported with sg_net_wants_write(). */
ssize_t sg_net_recv(int fd, void *buf, size_t len);
ssize_t sg_net_send(int fd, const void *buf, size_t len);
/* The last EAGAIN of a read on fd (or of the server's TLS handshake)
 * needs the socket writable (POLLOUT) rather than readable: an event loop
 * adds EPOLLOUT interest and pumps again when it fires. */
bool sg_net_wants_write(int fd);

/* Wait until fd can make progress for reading (buffered TLS data counts)
 * or writing, at most timeout_ms. >0 ready, 0 timeout, <0 error. */
int sg_net_wait(int fd, bool for_write, int timeout_ms);

/* Bytes of fd since its session started (2026.11.01.14): app = what the
 * protocol sent and received (the arguments of sg_net_send/recv), wire =
 * what crossed the socket (TLS records, handshake included; = app without
 * TLS). Reset when a session is attached and when it is detached. */
typedef struct {
    uint64_t app_out, app_in;
    uint64_t wire_out, wire_in;
} SgNetCounters;
void sg_net_counters(int fd, SgNetCounters *out);

/* Before close(fd): end the TLS session (a close_notify if the socket can
 * take it, never blocking) and free it. Harmless without TLS. */
void sg_tls_detach(int fd);

/* Server: the fds whose TLS handshake started more than
 * SG_TLS_HANDSHAKE_TIMEOUT_S seconds ago and is not complete (up to max);
 * the caller closes them. */
int sg_tls_stale_handshakes(time_t now, int *fds, int max);

#endif
