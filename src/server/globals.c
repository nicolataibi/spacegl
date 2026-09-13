#include <stdio.h>
#include <string.h>
#include "server_internal.h"
SupernovaState supernova_event = {0};

/* --- Configurable data directory (see server_internal.h) ---
 * Default "." preserves the legacy behavior: captains/ and galaxy.dat
 * live in the current working directory. main() calls
 * server_set_data_dir() after parsing --data-dir. */
char g_data_dir[SERVER_DATA_DIR_MAX] = ".";

void server_set_data_dir(const char *dir) {
    if (dir == NULL || dir[0] == '\0') return;
    snprintf(g_data_dir, sizeof(g_data_dir), "%s", dir);
    /* Normalize: strip trailing slashes (but keep the root "/"). */
    size_t len = strlen(g_data_dir);
    while (len > 1 && g_data_dir[len - 1] == '/') g_data_dir[--len] = '\0';
}

/* Resolve a resource path (e.g. "captains/NEO/identity.hash",
 * "galaxy.dat") against the configured data directory. With an empty
 * or "." data dir the relative path is returned unchanged. */
void server_data_path(char *out, size_t out_len, const char *rel) {
    if (out == NULL || out_len == 0) return;
    if (g_data_dir[0] == '\0' || strcmp(g_data_dir, ".") == 0) {
        snprintf(out, out_len, "%s", rel);
    } else {
        snprintf(out, out_len, "%s/%s", g_data_dir, rel);
    }
}
