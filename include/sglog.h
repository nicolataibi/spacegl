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

/*
 * SPACE GL - official logging and messaging system.
 *
 * A log line is the orthogonal product of a SEVERITY and a CATEGORY:
 *
 *   [2026-09-27 19:12:33.456] [srv] [TRACE3]   [PHA]      player 2 fires ...
 *    timestamp              tag   severity    category  message
 *
 * SEVERITY (finest -> coarsest; a message is printed when its severity
 * is >= the configured threshold):
 *   TRACE1   massima verbosita': dettaglio finissimo (per istanza / per
 *            elemento / per tick; solo con opt-in esplicito).
 *   TRACE2   dettaglio molto elevato (riassunti per-frame / per-packet).
 *   TRACE3   dettaglio elevato (per operazione logica: sparo, consegna
 *            beam a un client, evento IPC, ricezione update).
 *   TRACE4   dettaglio medio-alto (transizioni di stato: login, jump,
 *            init, fallback, selezione modalita').
 *   DEBUG    informazioni utili per il debugging e lo sviluppo.
 *   INFO     normale andamento dell'applicazione.
 *   NOTICE   eventi normali ma degni di nota.
 *   SUCCESS  completamento riuscito di un'operazione significativa.
 *   WARNING  condizioni anomale ma non necessariamente bloccanti.
 *   ERROR    errori che impediscono o compromettono un'operazione.
 *   CRITICAL guai seri: il sistema puo' continuare in stato degradato.
 *   FATAL    errore irrecoverabile: il processo sta per terminare.
 *
 * CATEGORY (ortogonale alla severita', vedi i #define SG_CAT_*):
 *   SYSTEM ENGINE RENDER OPENGL VULKAN GPU CPU MEMORY NETWORK SERVER
 *   CLIENT AUDIO INPUT PHYSICS ASSET SHADER RESOURCE THREAD IPC
 *   PROTOCOL PERFORMANCE SECURITY
 *   + una categoria per ogni comando di gioco: PHA TOR LRS SRS JUM
 *   APR CHA LOCK POW CLO DOC ... (la categoria e' il nome del comando
 *   in maiuscolo, es. "pha 2 100" -> categoria PHA).
 *
 * CONFIGURATION:
 *   --log-level=LEVEL        (oppure --log-level LEVEL)  o SPACEGL_LOG_LEVEL
 *   --log-category=A,B,C     (oppure --log-category A,B,C)  o
 *                             SPACEGL_LOG_CATEGORY / SPACEGL_LOG_CATEGORIES
 *   LEVEL    : TRACE1|TRACE2|TRACE3|TRACE4|TRACE (=TRACE1)|DEBUG|INFO|
 *              NOTICE|SUCCESS|WARN|WARNING|ERROR|CRITICAL|FATAL|OFF
 *              (default: INFO)
 *   CATEGORY : elenco separato da virgole; vuoto/omesso = tutte.
 *              Il confronto e' case-insensitive.
 *
 * OUTPUT: stderr, una riga per messaggio, singola write(2) (atomica
 * anche con piu' thread che loggano); ANSI colori solo se stderr e' un
 * TTY. Da WARNING in su viene aggiunto " @ file:line".
 *
 * REGOLE DI POSIZIONAMENTO (importanti per tenere il log catturabile):
 *   - Niente per-frame / per-tick / per-packet a TRACE4 o superiore:
 *     quel dettaglio vive in TRACE1/TRACE2 ed appare solo se
 *     l'operatore seleziona esplicitamente un livello trace.
 *   - Nessuna riga DEBUG+ all'interno di loop caldi (render, tick,
 *     packet) senza un guard "solo se qualcosa e' cambiato/accaduto".
 *   - Le anomalie nei loop caldi (drop, overflow, retry) vanno
 *     rate-limitate con sglog_rate() o aggregate in contatori.
 */

#ifndef SGLOG_H
#define SGLOG_H

#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* Severities                                                          */
/* ------------------------------------------------------------------ */
enum {
    SG_SEV_TRACE1 = 0,   /* massima verbosita' */
    SG_SEV_TRACE2 = 1,   /* dettaglio molto elevato */
    SG_SEV_TRACE3 = 2,   /* dettaglio elevato */
    SG_SEV_TRACE4 = 3,   /* dettaglio medio-alto */
    SG_SEV_DEBUG  = 4,
    SG_SEV_INFO   = 5,
    SG_SEV_NOTICE = 6,
    SG_SEV_SUCCESS = 7,
    SG_SEV_WARNING = 8,
    SG_SEV_ERROR  = 9,
    SG_SEV_CRITICAL = 10,
    SG_SEV_FATAL  = 11,
    SG_SEV_OFF    = 12   /* silenzia tutto (threshold) */
};

/* ------------------------------------------------------------------ */
/* Categories (base)                                                   */
/* ------------------------------------------------------------------ */
#define SG_CAT_SYSTEM      "SYSTEM"
#define SG_CAT_ENGINE      "ENGINE"
#define SG_CAT_RENDER      "RENDER"
#define SG_CAT_OPENGL      "OPENGL"
#define SG_CAT_VULKAN      "VULKAN"
#define SG_CAT_GPU         "GPU"
#define SG_CAT_CPU         "CPU"
#define SG_CAT_MEMORY      "MEMORY"
#define SG_CAT_NETWORK     "NETWORK"
#define SG_CAT_SERVER      "SERVER"
#define SG_CAT_CLIENT      "CLIENT"
#define SG_CAT_AUDIO       "AUDIO"
#define SG_CAT_INPUT       "INPUT"
#define SG_CAT_PHYSICS     "PHYSICS"
#define SG_CAT_ASSET       "ASSET"
#define SG_CAT_SHADER      "SHADER"
#define SG_CAT_RESOURCE    "RESOURCE"
#define SG_CAT_THREAD      "THREAD"
#define SG_CAT_IPC         "IPC"
#define SG_CAT_PROTOCOL    "PROTOCOL"
#define SG_CAT_PERFORMANCE "PERFORMANCE"
#define SG_CAT_SECURITY    "SECURITY"

/* Categories per comando di gioco (nome comando in maiuscolo). */
#define SG_CAT_PHA   "PHA"
#define SG_CAT_TOR   "TOR"
#define SG_CAT_LRS   "LRS"
#define SG_CAT_SRS   "SRS"
#define SG_CAT_JUM   "JUM"
#define SG_CAT_APR   "APR"
#define SG_CAT_CHA   "CHA"
#define SG_CAT_LOCK  "LOCK"
#define SG_CAT_POW   "POW"
#define SG_CAT_CLO   "CLO"
#define SG_CAT_DOC   "DOC"
#define SG_CAT_MIN   "MIN"
#define SG_CAT_HAR   "HAR"
#define SG_CAT_SCO   "SCO"
#define SG_CAT_CON   "CON"
#define SG_CAT_LOAD  "LOAD"
#define SG_CAT_SCAN  "SCAN"
#define SG_CAT_DIS   "DIS"
#define SG_CAT_BOR   "BOR"
#define SG_CAT_PSY   "PSY"
#define SG_CAT_RAD   "RAD"
#define SG_CAT_ENC   "ENC"
#define SG_CAT_LINK  "LINK"
#define SG_CAT_TUNER "TUNE"
#define SG_CAT_NAV   "NAV"
#define SG_CAT_IMP   "IMP"
#define SG_CAT_POS   "POS"
#define SG_CAT_SHE   "SHE"
#define SG_CAT_REP   "REP"
#define SG_CAT_FIX   "FIX"
#define SG_CAT_STA   "STA"
#define SG_CAT_INV   "INV"
#define SG_CAT_DAM   "DAM"
#define SG_CAT_CAL   "CAL"
#define SG_CAT_ICAL  "ICAL"
#define SG_CAT_WHO   "WHO"
#define SG_CAT_AUX   "AUX"
#define SG_CAT_RED   "RED"
#define SG_CAT_ORB   "ORB"
#define SG_CAT_UND   "UND"

/* ------------------------------------------------------------------ */
/* API                                                                 */
/* ------------------------------------------------------------------ */

/* Read the SPACEGL_LOG_LEVEL / SPACEGL_LOG_CATEGORY(IES) environment
 * variables and apply them (default threshold: INFO, all categories).
 * `tag` is a short per-process label printed before the severity
 * (e.g. "srv", "clt", "glv", "vkv", "hud"); may be NULL.
 * Returns the effective threshold. */
int sglog_init(const char *tag);

/* Scan argv for --log-level[=VAL] and --log-category[=VAL] (both the
 * "=" and the space-separated form) and apply them, overriding the
 * environment. Unknown options are left untouched. Returns the
 * effective threshold. */
int sglog_apply_args(int argc, char **argv);

int  sglog_threshold(void);
void sglog_set_threshold(int level);
const char *sglog_sev_name(int level);
int  sglog_parse_severity(const char *name); /* -1 if unknown */

void sglog_set_categories(const char *csv);  /* NULL or "" = all */
bool sglog_cat_enabled(const char *cat);

/* Emit one line (used by the macros; call directly only if you must). */
void sglog_emit(int sev, const char *cat, const char *file, int line,
                const char *fmt, ...) __attribute__((format(printf, 5, 6)));

/* Rate limiter for hot loops: true at most once per `interval_sec`
 * for this key (0 = always). Keeps anomaly logging (drops, overflows)
 * from flooding the log when it happens every frame. */
bool sglog_rate(const char *key, int interval_sec);

/* Build the category name for a game command: the leading command
 * word uppercased ("pha 2 100" -> "PHA", "lrs" -> "LRS"). `out` must
 * be at least 8 bytes. The returned pointer is `out`. */
const char *sglog_cmd_category(const char *cmd, char *out);

/* ------------------------------------------------------------------ */
/* Macros (arguments are NOT evaluated when the line is filtered)      */
/* ------------------------------------------------------------------ */
#define SG_LOG(sev, cat, ...)                                              \
    do {                                                                   \
        if ((sev) >= sglog_threshold() && sglog_cat_enabled(cat))          \
            sglog_emit((sev), (cat), __FILE__, __LINE__, __VA_ARGS__);     \
    } while (0)

#define SG_TRACE1(cat, ...) SG_LOG(SG_SEV_TRACE1, cat, __VA_ARGS__)
#define SG_TRACE2(cat, ...) SG_LOG(SG_SEV_TRACE2, cat, __VA_ARGS__)
#define SG_TRACE3(cat, ...) SG_LOG(SG_SEV_TRACE3, cat, __VA_ARGS__)
#define SG_TRACE4(cat, ...) SG_LOG(SG_SEV_TRACE4, cat, __VA_ARGS__)
/* generic trace: medium detail (alias of TRACE4) */
#define SG_TRACE(cat, ...)  SG_TRACE4(cat, __VA_ARGS__)
#define SG_DEBUG(cat, ...)  SG_LOG(SG_SEV_DEBUG,  cat, __VA_ARGS__)
#define SG_INFO(cat, ...)   SG_LOG(SG_SEV_INFO,   cat, __VA_ARGS__)
#define SG_NOTICE(cat, ...) SG_LOG(SG_SEV_NOTICE, cat, __VA_ARGS__)
#define SG_SUCCESS(cat, ...) SG_LOG(SG_SEV_SUCCESS, cat, __VA_ARGS__)
#define SG_WARNING(cat, ...) SG_LOG(SG_SEV_WARNING, cat, __VA_ARGS__)
#define SG_ERROR(cat, ...)  SG_LOG(SG_SEV_ERROR,  cat, __VA_ARGS__)
#define SG_CRITICAL(cat, ...) SG_LOG(SG_SEV_CRITICAL, cat, __VA_ARGS__)
#define SG_FATAL(cat, ...)  SG_LOG(SG_SEV_FATAL,  cat, __VA_ARGS__)

#endif /* SGLOG_H */
