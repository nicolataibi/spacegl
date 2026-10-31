/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * server_commands_test.c — the command handlers on a REAL galaxy
 * (2026.10.31.29, roadmap phase 6).
 *
 * The production galaxy code is linked whole (galaxy.c, galaxy_index.c,
 * galaxy_names.c, galaxy_io.c) with the command, logic and target code;
 * only the network edge is replaced: send_server_msg() and write_all() are
 * recorders, the thread pool is absent. generate_galaxy() builds a galaxy
 * from a fixed seed in a throwaway data directory.
 *
 *   1. Every command of the registry answers: each line of a tour that
 *      covers all of them (valid and invalid parameters) produces at least
 *      one message to the captain or one packet on its socket.
 *   2. Every target range: for one active object of each range the captain
 *      is placed next to it and runs lock, scan, apr, the interaction
 *      commands (min, sco, har, dis, bor, orb, doc, cha) and the weapons
 *      (pha, tor); lock must hold the target and scan must answer.
 *   3. After every command and every tick the captain's state is sane:
 *      quadrant in 1..GALAXY_SIZE, finite sector, energy within capacity,
 *      no message sent to a slot outside [-1, MAX_CLIENTS).
 *   4. update_game_logic() runs between the commands (navigation,
 *      autopilot, combat, hazards).
 *
 * Under ASan/UBSan (the matrix Debug jobs) any memory or undefined
 * behaviour error on these paths fails the test.
 * Exit: 0 pass, 1 fail.
 */
#include "server_internal.h"
#include "game_state.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* --- the network edge -------------------------------------------------- */
pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
int global_tick = 0;
threadpool_t *g_pool = NULL;
int threadpool_add_task(threadpool_t *pool, void (*fn)(void *), void *arg) {
    (void)pool; (void)fn; (void)arg;
    return -1;   /* callers fall back to the synchronous path */
}

static int g_msgs = 0, g_bad_slot = 0, g_packets = 0;
static char g_last_text[512];
void send_server_msg(int p_idx, const char *from, const char *text) {
    (void)from;
    if (p_idx < -1 || p_idx >= MAX_CLIENTS) g_bad_slot++;
    g_msgs++;
    snprintf(g_last_text, sizeof(g_last_text), "%s", text ? text : "");
}
int write_all(int fd, const void *buf, size_t len) {
    (void)fd; (void)buf;
    g_packets++;
    return (int)len;
}

/* --- harness ------------------------------------------------------------- */
static int g_fail = 0, g_checks = 0;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while (0)

#define CAP 0          /* the captain under test */
#define FAKE_FD 1000   /* a socket number the recorders accept */

static void spawn_captain(int slot, const char *name, int faction, int ship_class,
                          int q1, int q2, int q3, double x, double y, double z) {
    ConnectedPlayer *p = &players[slot];
    memset(p, 0, sizeof(*p));
    pthread_mutex_init(&p->socket_mutex, NULL);
    p->active = 1;
    p->socket = FAKE_FD + slot;
    snprintf(p->name, sizeof(p->name), "%s", name);
    snprintf(p->state.captain_name, sizeof(p->state.captain_name), "%s", name);
    p->faction = faction;
    p->ship_class = ship_class;
    p->state.energy = MAX_ENERGY_CAPACITY;
    p->state.torpedoes = MAX_TORPEDO_CAPACITY;
    p->state.crew_count = 400;
    p->state.q1 = q1; p->state.q2 = q2; p->state.q3 = q3;
    p->state.s1 = x; p->state.s2 = y; p->state.s3 = z;
    p->gx = (q1 - 1) * QUADRANT_SIZE + x;
    p->gy = (q2 - 1) * QUADRANT_SIZE + y;
    p->gz = (q3 - 1) * QUADRANT_SIZE + z;
    p->state.inventory[1] = 1000000ULL;
    for (int k = 2; k < 10; k++) p->state.inventory[k] = 5000ULL;
    p->state.power_dist[0] = 0.333; p->state.power_dist[1] = 0.334; p->state.power_dist[2] = 0.333;
    for (int s = 0; s < 6; s++) { p->state.shields[s] = SHIELD_MAX_STRENGTH; p->state.target_shields[s] = SHIELD_MAX_STRENGTH; }
    p->state.hull_integrity = (float)YIELD_HARVEST_MAX;
    for (int s = 0; s < MAX_SYSTEMS; s++) p->state.system_health[s] = (float)YIELD_HARVEST_MAX;
    p->state.life_support = (float)YIELD_HARVEST_MAX;
    p->state.ion_beam_charge = (float)YIELD_HARVEST_MAX;
    p->jump_type = 2;
    p->full_update_timer = 5 * GAME_TICK_RATE + 1;
}

static void check_sane(const char *what) {
    const ConnectedPlayer *p = &players[CAP];
    CHECK(p->state.q1 >= 1 && p->state.q1 <= GALAXY_SIZE && p->state.q2 >= 1 && p->state.q2 <= GALAXY_SIZE &&
          p->state.q3 >= 1 && p->state.q3 <= GALAXY_SIZE,
          "%s: quadrant (%d,%d,%d) out of the galaxy", what, p->state.q1, p->state.q2, p->state.q3);
    CHECK(isfinite(p->state.s1) && isfinite(p->state.s2) && isfinite(p->state.s3),
          "%s: sector not finite", what);
    CHECK(p->state.energy <= (uint64_t)MAX_ENERGY_CAPACITY * 2, "%s: energy %llu over capacity", what,
          (unsigned long long)p->state.energy);
    CHECK(g_bad_slot == 0, "%s: a message to a slot outside [-1, %d)", what, MAX_CLIENTS);
}

static void ticks(int n) {
    for (int t = 0; t < n; t++) {
        global_tick++;
        spacegl_master.frame_id++;
        pthread_mutex_lock(&game_mutex);
        update_game_logic();
        pthread_mutex_unlock(&game_mutex);
    }
}

/* Runs one command line; returns 1 if the captain got an answer. */
static int run(const char *line) {
    int m0 = g_msgs, p0 = g_packets;
    bool deleted = process_command(CAP, line);
    (void)deleted;
    check_sane(line);
    return (g_msgs > m0 || g_packets > p0);
}

/* Keeps the captain fully supplied and healthy between steps. */
static void refuel(void) {
    ConnectedPlayer *p = &players[CAP];
    p->active = 1;
    p->socket = FAKE_FD;
    p->state.energy = MAX_ENERGY_CAPACITY;
    p->state.torpedoes = MAX_TORPEDO_CAPACITY;
    p->state.hull_integrity = (float)YIELD_HARVEST_MAX;
    for (int s = 0; s < MAX_SYSTEMS; s++) p->state.system_health[s] = (float)YIELD_HARVEST_MAX;
    p->state.ion_beam_charge = (float)YIELD_HARVEST_MAX;
    p->state.is_cloaked = 0;
    p->is_docked = 0;
    p->pending_bor_target = 0;
}

/* 1. The whole registry. */
static void test_tour(void) {
    static const char *tour[] = {
        "help", "sta", "inv", "dam", "srs", "lrs", "who",
        "cal 5 5 5", "cal 5 5 5 50 50 50", "cal", "ical 10 10 10", "ical",
        "pow 30 30 40", "pow 200 0 0", "pow",
        "she 100 100 100 100 100 100", "she 9000 9000 9000 9000 9000 9000", "she",
        "nav 90 0 2", "nav 90 0 2 5", "nav 45 10 0.5 9.9", "nav", "nav nan 0 1",
        "imp 50", "imp 90 0 50", "imp 90 0 50 3", "imp 0", "imp",
        "pos 90 10 0", "pos 90", "pos",
        "vec 90 0 50", "vec 90 0 0", "vec",
        "jum 5 5 5", "jum 1 5 5 5", "jum 2 6 6 6", "jum",
        "apr", "apr 99999999", "cha",
        "lock off", "lock 0", "lock 99999999", "lock",
        "scan", "scan 99999999",
        "pha 100", "pha", "tor", "tor 90 0", "tor 99999999",
        "clo", "clo", "red", "red",
        "map", "map st", "map pl", "map bs", "map en", "map bh", "map ne", "map pu", "map is",
        "map co", "map as", "map de", "map mi", "map bu", "map pf", "map ri", "map mo", "map qu",
        "map zz", "map",
        "axs", "axs", "grd", "grd",
        "bridge top", "bridge bottom", "bridge up", "bridge down", "bridge left", "bridge right",
        "bridge rear", "bridge bottom rear", "bridge off", "bridge",
        "pilot on", "pilot off", "pilot",
        "aux probe 1 1 1", "aux probe 40 40 40", "aux probe", "aux report 1", "aux report 9",
        "aux recover 1", "aux recover 9", "aux jettison", "aux", "aux zz",
        "con 1 100", "con 2 100", "con 3 100", "con 6 100", "con 7 100", "con 9 100", "con",
        "load 1 100", "load 2 5", "load 9 1", "load",
        "rep 1", "rep 99", "rep", "fix", "hull",
        "min", "sco", "har", "dis", "bor", "orb", "doc", "und", "undock",
        "psy", "xxx",
        "enc aes", "enc chacha", "enc twofish", "enc pqc", "enc off", "enc zz", "enc",
        "enc2 aes", "enc2", "enc3 NOBODY aes", "enc3", "enc4 NOBODY aes", "enc4",
        "link NOBODY", "link",
        "dbgnpc", "dbgspn",
        "nosuchcommand", "", "   ",
    };
    int silent = 0;
    for (size_t k = 0; k < sizeof(tour) / sizeof(tour[0]); k++) {
        refuel();
        int answered = run(tour[k]);
        if (!answered && tour[k][0] != '\0' && tour[k][0] != ' ') {
            silent++;
            fprintf(stderr, "  no answer to \"%s\"\n", tour[k]);
        }
        ticks(3);
        check_sane(tour[k]);
    }
    CHECK(silent == 0, "tour: %d commands gave no answer", silent);
    /* navigation runs to its end */
    refuel();
    run("nav 135 20 1.5 9");
    ticks(400);
    refuel();
    run("imp 200 10 80");
    ticks(200);
    run("imp 0");
    check_sane("after the flights");
    printf("  tour: %zu command lines\n", sizeof(tour) / sizeof(tour[0]));
}

/* All target ranges, in id order. */
static int collect_ranges(const TargetRangeDef **out, int cap) {
    int n = 0;
    for (int tid = 1; tid < 2000000 && n < cap;) {
        const TargetRangeDef *r = target_range_for(tid);
        if (!r) { tid++; continue; }
        out[n++] = r;
        tid = r->max_id + 1;
    }
    return n;
}

/* 2. One object of every range. */
static void test_ranges(void) {
    const TargetRangeDef *ranges[256];
    int nr = collect_ranges(ranges, 256);
    CHECK(nr >= 40, "only %d target ranges found", nr);
    int tested = 0, locked = 0, lockable_n = 0;
    for (int k = 0; k < nr; k++) {
        const TargetRangeDef *r = ranges[k];
        int tid = -1;
        const void *e = NULL;
        for (size_t s = 0; s < r->capacity; s++) {
            const void *c = (const char *)r->base + s * r->stride;
            if (r->is_active(c)) { tid = r->min_id + (int)s; e = c; break; }
        }
        if (tid < 0) continue;
        double ax, ay, az;
        target_abs_pos(r, e, &ax, &ay, &az);
        int q1 = (int)floor(ax / QUADRANT_SIZE) + 1, q2 = (int)floor(ay / QUADRANT_SIZE) + 1, q3 = (int)floor(az / QUADRANT_SIZE) + 1;
        if (q1 < 1 || q1 > GALAXY_SIZE || q2 < 1 || q2 > GALAXY_SIZE || q3 < 1 || q3 > GALAXY_SIZE) continue;
        double x = ax - (q1 - 1) * QUADRANT_SIZE, y = ay - (q2 - 1) * QUADRANT_SIZE, z = az - (q3 - 1) * QUADRANT_SIZE;
        double px = x + 0.7 < QUADRANT_SIZE - 1.0 ? x + 0.7 : x - 0.7;
        spawn_captain(CAP, "TESTCAP", FACTION_ALLIANCE, SHIP_CLASS_EXPLORER, q1, q2, q3, px, y, z);
        ticks(2);
        char line[64];
        refuel();
        snprintf(line, sizeof(line), "lock %d", tid);
        int ans = run(line);
        CHECK(ans, "%s (%d): lock gave no answer", r->name, tid);
        bool lockable = (r->flags & (TGT_F_CMD_LOCK | TGT_F_LOCK_GLOBAL)) != 0;
        if (lockable) {
            lockable_n++;
            if (players[CAP].state.lock_target == tid) locked++;
            else fprintf(stderr, "  lock %d (%s) not held: \"%s\"\n", tid, r->name, g_last_text);
        }
        snprintf(line, sizeof(line), "scan %d", tid);
        refuel();
        CHECK(run(line), "%s (%d): scan gave no answer", r->name, tid);
        static const char *near_cmds[] = { "min", "sco", "har", "dis", "bor", "orb", "doc", "und",
                                           "psy", "cha", "srs", "lrs" };
        for (size_t c = 0; c < sizeof(near_cmds) / sizeof(near_cmds[0]); c++) {
            refuel();
            players[CAP].state.lock_target = tid;
            run(near_cmds[c]);
            if (players[CAP].pending_bor_target > 0) { run("1"); run("2"); }
            ticks(1);
        }
        refuel();
        snprintf(line, sizeof(line), "apr %d 1.0", tid);
        run(line);
        ticks(40);
        refuel();
        players[CAP].state.lock_target = tid;
        run("pha 500");
        ticks(5);
        refuel();
        players[CAP].state.lock_target = tid;
        run("tor");
        ticks(30);
        check_sane(r->name);
        tested++;
    }
    CHECK(tested >= 40, "only %d ranges had an active object", tested);
    CHECK(locked == lockable_n, "lock held the target for %d of the %d lockable ranges", locked, lockable_n);
    printf("  ranges: %d found, %d tested, %d of %d lockable locked\n", nr, tested, locked, lockable_n);
}

/* Combat against an NPC ship and a second captain. */
static void test_combat(void) {
    int n = -1;
    for (int k = 0; k < MAX_NPC; k++) if (npcs[k].active) { n = k; break; }
    CHECK(n >= 0, "the galaxy has NPC ships");
    if (n < 0) return;
    NPCShip *s = &npcs[n];
    spawn_captain(CAP, "TESTCAP", FACTION_ALLIANCE, SHIP_CLASS_TACTICAL, s->q1, s->q2, s->q3,
                  s->x > 5 ? s->x - 3 : s->x + 3, s->y, s->z);
    spawn_captain(1, "RIVAL", FACTION_KORTHIAN, SHIP_CLASS_ESCORT, s->q1, s->q2, s->q3,
                  s->x > 5 ? s->x - 4 : s->x + 4, s->y, s->z);
    ticks(5);
    char line[64];
    int npc_id = s->id + GALAXY_OBJECT_MIN_NPC;
    for (int round = 0; round < 6; round++) {
        refuel();
        snprintf(line, sizeof(line), "lock %d", npc_id); run(line);
        run("pha 2000"); ticks(10);
        refuel();
        snprintf(line, sizeof(line), "lock %d", npc_id); run(line);
        run("tor"); ticks(60);
        refuel();
        run("lock 2");                 /* the rival captain */
        run("pha 1000"); run("scan 2"); run("bor"); ticks(20);
    }
    refuel();
    run("enc3 RIVAL aes"); run("enc4 RIVAL aes"); run("link RIVAL"); run("who");
    check_sane("combat");
    players[1].active = 0;
}

int main(void) {
    char tmpl[] = "/tmp/spacegl_cmdtest.XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) { perror("mkdtemp"); return 1; }
    server_set_data_dir(dir);
    srand(20261031u);
    generate_galaxy();
    init_static_spatial_index();
    printf("server_commands_test: galaxy generated in %s\n", dir);

    spawn_captain(CAP, "TESTCAP", FACTION_ALLIANCE, SHIP_CLASS_EXPLORER, 10, 10, 10, 50.0, 50.0, 50.0);
    test_tour();
    test_ranges();
    test_combat();

    /* the permanent profile deletion, last */
    refuel();
    run("zztop");
    run("y");

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    if (system(cmd) != 0) { /* best effort */ }
    printf("server_commands_test: %d checks, %d failed, %d messages, %d packets\n",
           g_checks, g_fail, g_msgs, g_packets);
    return g_fail ? 1 : 0;
}
