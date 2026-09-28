/*
 * SPACE GL - UNIFIED LOGGING / MESSAGING
 * Space GL - Copyright (C) 2026 Nicola Taibi
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

#define _GNU_SOURCE
#include "sglog.h"

#include <stdatomic.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define SGLOG_MAX_CATS      24
#define SGLOG_CAT_LEN       16
#define SGLOG_TAG_LEN       8
#define SGLOG_LINE_BUF      4096
#define SGLOG_RATE_SLOTS    16

static atomic_int sg_threshold = (atomic_int)SG_SEV_INFO; /* C23: no ATOMIC_INIT */
static char       sg_tag[SGLOG_TAG_LEN] = "";
static char       sg_cats[SGLOG_MAX_CATS][SGLOG_CAT_LEN];
static int        sg_cat_count = 0;             /* 0 = all categories */

/* ------------------------------------------------------------------ */
/* Severity names / parsing                                            */
/* ------------------------------------------------------------------ */
static const char *sev_names[] = {
    [SG_SEV_TRACE1]   = "TRACE1",
    [SG_SEV_TRACE2]   = "TRACE2",
    [SG_SEV_TRACE3]   = "TRACE3",
    [SG_SEV_TRACE4]   = "TRACE4",
    [SG_SEV_DEBUG]    = "DEBUG",
    [SG_SEV_INFO]     = "INFO",
    [SG_SEV_NOTICE]   = "NOTICE",
    [SG_SEV_SUCCESS]  = "SUCCESS",
    [SG_SEV_WARNING]  = "WARNING",
    [SG_SEV_ERROR]    = "ERROR",
    [SG_SEV_CRITICAL] = "CRITICAL",
    [SG_SEV_FATAL]    = "FATAL",
    [SG_SEV_OFF]      = "OFF"
};

const char *sglog_sev_name(int level) {
    if (level < SG_SEV_TRACE1 || level > SG_SEV_OFF) return "?";
    return sev_names[level];
}

int sglog_parse_severity(const char *name) {
    if (!name || !*name) return -1;
    if (strcasecmp(name, "TRACE") == 0) return SG_SEV_TRACE1;
    for (int i = SG_SEV_TRACE1; i <= SG_SEV_OFF; i++) {
        if (strcasecmp(name, sev_names[i]) == 0) return i;
    }
    if (strcasecmp(name, "WARN") == 0) return SG_SEV_WARNING;
    return -1;
}

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
int sglog_threshold(void) { return atomic_load(&sg_threshold); }

void sglog_set_threshold(int level) {
    if (level < SG_SEV_TRACE1) level = SG_SEV_TRACE1;
    if (level > SG_SEV_OFF) level = SG_SEV_OFF;
    atomic_store(&sg_threshold, level);
}

void sglog_set_categories(const char *csv) {
    sg_cat_count = 0;
    if (!csv || !*csv) return;                 /* empty = all */
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", csv);
    for (char *tok = strtok(tmp, ","); tok && sg_cat_count < SGLOG_MAX_CATS;
         tok = strtok(NULL, ",")) {
        while (*tok == ' ' || *tok == '\t') tok++;
        if (!*tok) continue;
        size_t n = strnlen(tok, SGLOG_CAT_LEN);
        memcpy(sg_cats[sg_cat_count], tok, n);
        sg_cats[sg_cat_count][n] = '\0';
        sg_cat_count++;
    }
}

bool sglog_cat_enabled(const char *cat) {
    if (!cat) return true;
    if (sg_cat_count == 0) return true;        /* no filter = all */
    for (int i = 0; i < sg_cat_count; i++) {
        if (strcasecmp(sg_cats[i], cat) == 0) return true;
    }
    return false;
}

const char *sglog_cmd_category(const char *cmd, char *out) {
    out[0] = '\0';
    if (!cmd) return out;
    int n = 0;
    for (const char *p = cmd; *p && *p != ' ' && *p != '\t' && n < 7; p++) {
        out[n++] = (char)toupper((unsigned char)*p);
    }
    out[n] = '\0';
    return out;
}

/* ------------------------------------------------------------------ */
/* Init / CLI                                                          */
/* ------------------------------------------------------------------ */
int sglog_init(const char *tag) {
    if (tag) {
        size_t n = strnlen(tag, SGLOG_TAG_LEN - 1);
        memcpy(sg_tag, tag, n);
        sg_tag[n] = '\0';
    }
    const char *lvl = getenv("SPACEGL_LOG_LEVEL");
    if (lvl && *lvl) {
        int p = sglog_parse_severity(lvl);
        if (p >= 0) sglog_set_threshold(p);
    }
    const char *cats = getenv("SPACEGL_LOG_CATEGORY");
    if (!cats || !*cats) cats = getenv("SPACEGL_LOG_CATEGORIES");
    if (cats && *cats) sglog_set_categories(cats);
    return sglog_threshold();
}

/* Match "--log-level=VAL" or "--log-level VAL" (same for
 * --log-category / --log-cats). Returns 1 on a match, advances *i. */
static int sglog_match_opt(int argc, char **argv, int *i,
                           const char *opt_eq, const char *opt_bare,
                           char *val, size_t val_sz) {
    if (strncmp(argv[*i], opt_eq, strlen(opt_eq)) == 0) {
        snprintf(val, val_sz, "%s", argv[*i] + strlen(opt_eq));
        return 1;
    }
    if (strcmp(argv[*i], opt_bare) == 0 && *i + 1 < argc) {
        snprintf(val, val_sz, "%s", argv[*i + 1]);
        (*i)++;
        return 1;
    }
    return 0;
}

int sglog_apply_args(int argc, char **argv) {
    char val[256];
    for (int i = 1; i < argc; i++) {
        if (sglog_match_opt(argc, argv, &i, "--log-level=", "--log-level",
                            val, sizeof(val))) {
            int p = sglog_parse_severity(val);
            if (p >= 0) sglog_set_threshold(p);
            else fprintf(stderr, "sglog: unknown --log-level '%s'\n", val);
            continue;
        }
        if (sglog_match_opt(argc, argv, &i, "--log-category=",
                            "--log-category", val, sizeof(val)) ||
            sglog_match_opt(argc, argv, &i, "--log-cats=", "--log-cats",
                            val, sizeof(val))) {
            sglog_set_categories(val);
            continue;
        }
    }
    return sglog_threshold();
}

/* ------------------------------------------------------------------ */
/* Emit                                                                */
/* ------------------------------------------------------------------ */
static const char *sev_color(int sev) {
    switch (sev) {
        case SG_SEV_TRACE1:   return "\033[38;5;244m";
        case SG_SEV_TRACE2:   return "\033[38;5;245m";
        case SG_SEV_TRACE3:   return "\033[38;5;246m";
        case SG_SEV_TRACE4:   return "\033[38;5;247m";
        case SG_SEV_DEBUG:    return "\033[36m";
        case SG_SEV_INFO:     return "\033[37m";
        case SG_SEV_NOTICE:   return "\033[32m";
        case SG_SEV_SUCCESS:  return "\033[1;32m";
        case SG_SEV_WARNING:  return "\033[1;33m";
        case SG_SEV_ERROR:    return "\033[1;31m";
        case SG_SEV_CRITICAL: return "\033[1;35m";
        case SG_SEV_FATAL:    return "\033[1;41;97m";
        default:              return "";
    }
}

void sglog_emit(int sev, const char *cat, const char *file, int line,
                const char *fmt, ...) {
    if (sev < sglog_threshold() || !sglog_cat_enabled(cat)) return;

    char line_buf[SGLOG_LINE_BUF];
    int pos = 0;

    /* timestamp: [YYYY-MM-DD HH:MM:SS.mmm] */
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    struct tm tm;
    localtime_r(&now.tv_sec, &tm);
    pos += snprintf(line_buf + pos, sizeof(line_buf) - (size_t)pos,
                    "[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec,
                    (int)(now.tv_nsec / 1000000));

    if (sg_tag[0])
        pos += snprintf(line_buf + pos, sizeof(line_buf) - (size_t)pos,
                        "[%s] ", sg_tag);

    int  tty = isatty(2);
    const char *pre = tty ? sev_color(sev) : "";
    const char *post = tty ? "\033[0m" : "";

    pos += snprintf(line_buf + pos, sizeof(line_buf) - (size_t)pos,
                    "%s[%-8s] [%-10s] %s", pre, sglog_sev_name(sev),
                    cat ? cat : "?", post);

    va_list ap;
    va_start(ap, fmt);
    pos += vsnprintf(line_buf + pos, sizeof(line_buf) - (size_t)pos,
                     fmt, ap);
    va_end(ap);

    /* location: only for anomalies and up (keeps the flow logs clean) */
    if (sev >= SG_SEV_WARNING && file)
        pos += snprintf(line_buf + pos, sizeof(line_buf) - (size_t)pos,
                        "  @ %s:%d", strrchr(file, '/') ? strrchr(file, '/') + 1 : file, line);

    if (pos < (int)sizeof(line_buf)) line_buf[pos++] = '\n';
    ssize_t r = write(2, line_buf, (size_t)pos);
    (void)r;
}

/* ------------------------------------------------------------------ */
/* Rate limiter (hot-loop anomalies)                                   */
/* ------------------------------------------------------------------ */
static atomic_flag sg_rate_lock = ATOMIC_FLAG_INIT;
struct rate_entry { uint64_t hash; double last; };
static struct rate_entry sg_rate_table[SGLOG_RATE_SLOTS];

bool sglog_rate(const char *key, int interval_sec) {
    if (!key) return true;
    uint64_t h = 1469598103934665603ULL;
    for (const unsigned char *p = (const unsigned char *)key; *p; p++) {
        h ^= *p;
        h *= 1099511628211ULL;
    }
    double now = (double)time(NULL);
    int slot = (int)(h % SGLOG_RATE_SLOTS);
    atomic_flag_test_and_set(&sg_rate_lock);
    struct rate_entry *e = &sg_rate_table[slot];
    bool ok;
    if (e->hash != h) {
        e->hash = h;
        e->last = now;
        ok = true;
    } else {
        ok = (now - e->last) >= (double)interval_sec;
        if (ok) e->last = now;
    }
    atomic_flag_clear(&sg_rate_lock);
    return ok;
}
