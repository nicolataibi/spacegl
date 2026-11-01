/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * sg_tls.c — TLS 1.3 on the game port (2026.11.01.11, wire revision 5).
 * See include/sg_tls.h for the design.
 */
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "sg_tls.h"

typedef struct {
    pthread_mutex_t lock;
    SSL *ssl;          /* NULL: no TLS session on this fd */
    bool ready;        /* handshake complete */
    bool server;       /* accepting side */
    bool peeked;       /* server: the first byte was checked (a TLS record?) */
    bool rd_wants_write; /* the last read EAGAIN needs POLLOUT (TLS can write on read) */
    bool wr_wants_read;  /* the last write EAGAIN needs POLLIN */
    bool hs_wants_write; /* server handshake: the flight is waiting for POLLOUT */
    uint64_t app_out, app_in; /* protocol bytes since the session started */
    time_t started;    /* server: handshake start */
} SgTlsConn;

static SgTlsConn *g_tab = NULL;
static int g_tab_n = 0;
static pthread_once_t g_tab_once = PTHREAD_ONCE_INIT;
static SSL_CTX *g_server_ctx = NULL;
static SSL_CTX *g_client_ctx = NULL;

static void tab_init(void) {
    struct rlimit rl;
    int n = 4096;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur > 4096)
        n = rl.rlim_cur > 65536 ? 65536 : (int)rl.rlim_cur;
    g_tab = calloc((size_t)n, sizeof(SgTlsConn));
    if (!g_tab) return;
    for (int i = 0; i < n; i++) pthread_mutex_init(&g_tab[i].lock, NULL);
    g_tab_n = n;
}

static SgTlsConn *entry(int fd) {
    pthread_once(&g_tab_once, tab_init);
    if (fd < 0 || fd >= g_tab_n) return NULL;
    return &g_tab[fd];
}

static void set_nonblocking(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl != -1) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

static void common_ctx_setup(SSL_CTX *ctx) {
    SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
    /* writes may be partial and resumed from a moved buffer (the callers
     * loop on sg_net_send like on send()); an EOF without close_notify is
     * an orderly close (a deck killed by its user), not an error */
    SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    SSL_CTX_set_options(ctx, SSL_OP_IGNORE_UNEXPECTED_EOF);
}

/* The self-signed certificate of the server identity. */
static X509 *make_certificate(EVP_PKEY *key) {
    X509 *x = X509_new();
    if (!x) return NULL;
    uint64_t serial = 0;
    bool ok = RAND_bytes((unsigned char *)&serial, sizeof(serial)) == 1;
    serial &= 0x7fffffffffffffffULL;
    ok = ok && X509_set_version(x, 2) &&
         ASN1_INTEGER_set_uint64(X509_get_serialNumber(x), serial) &&
         X509_gmtime_adj(X509_getm_notBefore(x), -86400L) &&
         X509_time_adj_ex(X509_getm_notAfter(x), 36500, 0, NULL) &&
         X509_set_pubkey(x, key);
    X509_NAME *name = X509_get_subject_name(x);
    ok = ok && name &&
         X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char *)"SpaceGL server", -1, -1, 0) &&
         X509_set_issuer_name(x, name) &&
         X509_sign(x, key, NULL) > 0;   /* Ed25519: no separate digest */
    if (!ok) {
        X509_free(x);
        return NULL;
    }
    return x;
}

bool sg_tls_server_init(const uint8_t identity_seed[32]) {
    if (g_server_ctx) return true;
    EVP_PKEY *key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, NULL, identity_seed, 32);
    if (!key) return false;
    X509 *cert = make_certificate(key);
    SSL_CTX *ctx = cert ? SSL_CTX_new(TLS_server_method()) : NULL;
    bool ok = ctx != NULL;
    if (ok) {
        common_ctx_setup(ctx);
        SSL_CTX_set_num_tickets(ctx, 0);   /* no resumption: one handshake per connection */
        ok = SSL_CTX_use_certificate(ctx, cert) == 1 && SSL_CTX_use_PrivateKey(ctx, key) == 1 &&
             SSL_CTX_check_private_key(ctx) == 1;
    }
    X509_free(cert);
    EVP_PKEY_free(key);
    if (!ok) {
        SSL_CTX_free(ctx);
        return false;
    }
    g_server_ctx = ctx;
    return true;
}

bool sg_tls_client_init(void) {
    if (g_client_ctx) return true;
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return false;
    common_ctx_setup(ctx);
    /* No certificate authority: the server's key is pinned by the caller
     * (sg_tls_peer_key -> known_servers). TLS 1.3 still makes the server
     * prove it holds that key (CertificateVerify). */
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    g_client_ctx = ctx;
    return true;
}

bool sg_tls_server_attach(int fd) {
    SgTlsConn *e = entry(fd);
    if (!e || !g_server_ctx) return false;
    SSL *ssl = SSL_new(g_server_ctx);
    if (!ssl) return false;
    if (SSL_set_fd(ssl, fd) != 1) {
        SSL_free(ssl);
        return false;
    }
    SSL_set_accept_state(ssl);
    set_nonblocking(fd);
    pthread_mutex_lock(&e->lock);
    if (e->ssl) SSL_free(e->ssl);   /* a session left on a recycled fd number */
    e->ssl = ssl;
    e->ready = false;
    e->server = true;
    e->peeked = false;
    e->rd_wants_write = e->wr_wants_read = e->hs_wants_write = false;
    e->app_out = e->app_in = 0;
    e->started = time(NULL);
    pthread_mutex_unlock(&e->lock);
    return true;
}

static void log_tls_error(const char *what, int fd) {
    unsigned long err = ERR_peek_last_error();
    char buf[256] = "no detail";
    if (err) ERR_error_string_n(err, buf, sizeof(buf));
    fprintf(stderr, "\033[1;33m[TLS]\033[0m fd %d: %s (%s)\n", fd, what, buf);
    ERR_clear_error();
}

int sg_tls_server_step(int fd) {
    SgTlsConn *e = entry(fd);
    if (!e) return -1;
    pthread_mutex_lock(&e->lock);
    if (!e->ssl) {
        pthread_mutex_unlock(&e->lock);
        return -1;
    }
    if (e->ready) {
        pthread_mutex_unlock(&e->lock);
        return 1;
    }
    if (!e->peeked) {
        /* A TLS connection starts with a handshake record (0x16). A deck
         * of wire revision 4 or older starts with its hello in clear (the
         * int32 PKT_HANDSHAKE = 6, little endian). */
        unsigned char b = 0;
        ssize_t n = recv(fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
            pthread_mutex_unlock(&e->lock);
            return -1;
        }
        if (n == 1) {
            e->peeked = true;
            if (b != 0x16) {
                pthread_mutex_unlock(&e->lock);
                fprintf(stderr, "\033[1;33m[TLS]\033[0m fd %d: the client does not speak TLS (a deck of wire "
                                "revision 4 or older?): connection refused\n",
                        fd);
                return -1;
            }
        } else {
            pthread_mutex_unlock(&e->lock);
            return 0;   /* nothing yet */
        }
    }
    ERR_clear_error();
    int r = SSL_do_handshake(e->ssl);
    int rc;
    e->hs_wants_write = false;
    if (r == 1) {
        e->ready = true;
        rc = 1;
    } else {
        int err = SSL_get_error(e->ssl, r);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            e->hs_wants_write = err == SSL_ERROR_WANT_WRITE;
            rc = 0;
        } else {
            rc = -1;
            log_tls_error("TLS handshake failed", fd);
        }
    }
    pthread_mutex_unlock(&e->lock);
    return rc;
}

bool sg_tls_client_connect(int fd, int timeout_ms, uint8_t peer_key[32]) {
    SgTlsConn *e = entry(fd);
    if (!e || !g_client_ctx) return false;
    SSL *ssl = SSL_new(g_client_ctx);
    if (!ssl) return false;
    if (SSL_set_fd(ssl, fd) != 1) {
        SSL_free(ssl);
        return false;
    }
    SSL_set_connect_state(ssl);
    set_nonblocking(fd);
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    bool ok = false;
    for (;;) {
        ERR_clear_error();
        int r = SSL_do_handshake(ssl);
        if (r == 1) {
            ok = true;
            break;
        }
        int err = SSL_get_error(ssl, r);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            log_tls_error("TLS handshake with the server failed", fd);
            break;
        }
        struct timespec t1;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long left = timeout_ms - ((t1.tv_sec - t0.tv_sec) * 1000L + (t1.tv_nsec - t0.tv_nsec) / 1000000L);
        if (left <= 0) {
            fprintf(stderr, "\033[1;33m[TLS]\033[0m fd %d: TLS handshake timed out\n", fd);
            break;
        }
        struct pollfd p = { .fd = fd, .events = (short)(err == SSL_ERROR_WANT_WRITE ? POLLOUT : POLLIN) };
        if (poll(&p, 1, (int)left) < 0 && errno != EINTR) break;
    }
    if (ok) {
        ok = false;
        X509 *cert = SSL_get1_peer_certificate(ssl);
        EVP_PKEY *pk = cert ? X509_get0_pubkey(cert) : NULL;
        size_t kl = 32;
        if (pk && EVP_PKEY_get_base_id(pk) == EVP_PKEY_ED25519 &&
            EVP_PKEY_get_raw_public_key(pk, peer_key, &kl) == 1 && kl == 32)
            ok = true;
        else
            fprintf(stderr, "\033[1;33m[TLS]\033[0m fd %d: the server's certificate has no Ed25519 key\n", fd);
        X509_free(cert);
    }
    if (!ok) {
        SSL_free(ssl);
        return false;
    }
    pthread_mutex_lock(&e->lock);
    if (e->ssl) SSL_free(e->ssl);
    e->ssl = ssl;
    e->ready = true;
    e->server = false;
    e->peeked = true;
    e->rd_wants_write = e->wr_wants_read = e->hs_wants_write = false;
    e->app_out = e->app_in = 0;
    pthread_mutex_unlock(&e->lock);
    return true;
}

bool sg_tls_active(int fd) {
    SgTlsConn *e = entry(fd);
    if (!e) return false;
    pthread_mutex_lock(&e->lock);
    bool a = e->ssl != NULL;
    pthread_mutex_unlock(&e->lock);
    return a;
}

bool sg_tls_ready(int fd) {
    SgTlsConn *e = entry(fd);
    if (!e) return false;
    pthread_mutex_lock(&e->lock);
    bool r = e->ssl && e->ready;
    pthread_mutex_unlock(&e->lock);
    return r;
}

const char *sg_tls_cipher(int fd) {
    SgTlsConn *e = entry(fd);
    if (!e) return "none";
    pthread_mutex_lock(&e->lock);
    const char *c = (e->ssl && e->ready) ? SSL_get_cipher_name(e->ssl) : NULL;
    pthread_mutex_unlock(&e->lock);
    return c ? c : "none";   /* a static string of OpenSSL's cipher table */
}

size_t sg_tls_pending(int fd) {
    SgTlsConn *e = entry(fd);
    if (!e) return 0;
    pthread_mutex_lock(&e->lock);
    size_t n = (e->ssl && e->ready) ? (size_t)SSL_pending(e->ssl) : 0;
    pthread_mutex_unlock(&e->lock);
    return n;
}

/* Map an SSL error of a read or write to the recv()/send() convention. */
static ssize_t map_error(SgTlsConn *e, int ret, int fd, bool writing) {
    int err = SSL_get_error(e->ssl, ret);
    switch (err) {
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            if (writing) e->wr_wants_read = err == SSL_ERROR_WANT_READ;
            else e->rd_wants_write = err == SSL_ERROR_WANT_WRITE;
            errno = EAGAIN;
            return -1;
        case SSL_ERROR_ZERO_RETURN:
            ERR_clear_error();
            return 0;
        case SSL_ERROR_SYSCALL:
            if (errno == 0) errno = ECONNRESET;
            ERR_clear_error();
            return -1;
        default:
            log_tls_error(writing ? "TLS write failed" : "TLS read failed", fd);
            errno = ECONNRESET;
            return -1;
    }
}

ssize_t sg_net_recv(int fd, void *buf, size_t len) {
    SgTlsConn *e = entry(fd);
    if (e) {
        pthread_mutex_lock(&e->lock);
        if (e->ssl) {
            if (!e->ready) {   /* no application data before the handshake */
                pthread_mutex_unlock(&e->lock);
                errno = EAGAIN;
                return -1;
            }
            size_t got = 0;
            ERR_clear_error();
            int r = SSL_read_ex(e->ssl, buf, len, &got);
            ssize_t out = r == 1 ? (ssize_t)got : map_error(e, r, fd, false);
            if (r == 1) {
                e->rd_wants_write = false;
                e->app_in += got;
            }
            int saved = errno;
            pthread_mutex_unlock(&e->lock);
            errno = saved;
            return out;
        }
        pthread_mutex_unlock(&e->lock);
    }
    ssize_t n = recv(fd, buf, len, MSG_DONTWAIT);
    if (n > 0 && e) {
        pthread_mutex_lock(&e->lock);
        e->app_in += (uint64_t)n;
        pthread_mutex_unlock(&e->lock);
    }
    return n;
}

ssize_t sg_net_send(int fd, const void *buf, size_t len) {
    SgTlsConn *e = entry(fd);
    if (e) {
        pthread_mutex_lock(&e->lock);
        if (e->ssl) {
            if (!e->ready) {
                pthread_mutex_unlock(&e->lock);
                errno = EAGAIN;
                return -1;
            }
            size_t put = 0;
            ERR_clear_error();
            int r = SSL_write_ex(e->ssl, buf, len, &put);
            ssize_t out = r == 1 ? (ssize_t)put : map_error(e, r, fd, true);
            if (r == 1) {
                e->wr_wants_read = false;
                e->app_out += put;
            }
            int saved = errno;
            pthread_mutex_unlock(&e->lock);
            errno = saved;
            return out;
        }
        pthread_mutex_unlock(&e->lock);
    }
    ssize_t n = send(fd, buf, len, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n > 0 && e) {
        pthread_mutex_lock(&e->lock);
        e->app_out += (uint64_t)n;
        pthread_mutex_unlock(&e->lock);
    }
    return n;
}

bool sg_net_wants_write(int fd) {
    SgTlsConn *e = entry(fd);
    if (!e) return false;
    pthread_mutex_lock(&e->lock);
    bool w = e->ssl && (e->ready ? e->rd_wants_write : e->hs_wants_write);
    pthread_mutex_unlock(&e->lock);
    return w;
}

int sg_net_wait(int fd, bool for_write, int timeout_ms) {
    if (!for_write && sg_tls_pending(fd) > 0) return 1;
    bool out = for_write;
    SgTlsConn *e = entry(fd);
    if (e) {
        pthread_mutex_lock(&e->lock);
        /* a TLS read that must first write, or a write that must first read */
        if (e->ssl) out = for_write ? !e->wr_wants_read : e->rd_wants_write;
        pthread_mutex_unlock(&e->lock);
    }
    struct pollfd p = { .fd = fd, .events = (short)(out ? POLLOUT : POLLIN) };
    int r = poll(&p, 1, timeout_ms);
    if (r > 0 && (p.revents & POLLNVAL)) return -1;
    return r;
}

void sg_tls_detach(int fd) {
    SgTlsConn *e = entry(fd);
    if (!e) return;
    pthread_mutex_lock(&e->lock);
    if (e->ssl) {
        if (e->ready) {
            ERR_clear_error();
            (void)SSL_shutdown(e->ssl);   /* close_notify if the socket takes it; never waits */
        }
        SSL_free(e->ssl);
        ERR_clear_error();
        e->ssl = NULL;
    }
    e->ready = false;
    e->rd_wants_write = e->wr_wants_read = e->hs_wants_write = false;
    e->app_out = e->app_in = 0;
    pthread_mutex_unlock(&e->lock);
}

void sg_net_counters(int fd, SgNetCounters *out) {
    memset(out, 0, sizeof(*out));
    SgTlsConn *e = entry(fd);
    if (!e) return;
    pthread_mutex_lock(&e->lock);
    out->app_out = e->app_out;
    out->app_in = e->app_in;
    if (e->ssl) {
        out->wire_out = BIO_number_written(SSL_get_wbio(e->ssl));
        out->wire_in = BIO_number_read(SSL_get_rbio(e->ssl));
    } else {
        out->wire_out = e->app_out;
        out->wire_in = e->app_in;
    }
    pthread_mutex_unlock(&e->lock);
}

int sg_tls_stale_handshakes(time_t now, int *fds, int max) {
    pthread_once(&g_tab_once, tab_init);
    int n = 0;
    for (int fd = 0; fd < g_tab_n && n < max; fd++) {
        SgTlsConn *e = &g_tab[fd];
        pthread_mutex_lock(&e->lock);
        if (e->ssl && e->server && !e->ready && now - e->started > SG_TLS_HANDSHAKE_TIMEOUT_S) fds[n++] = fd;
        pthread_mutex_unlock(&e->lock);
    }
    return n;
}
