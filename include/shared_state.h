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

#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>
#include "game_state.h"

/* Header-wide packing removed in favor of individual blocks */

#define MAX_OBJECTS 256
#define MAX_BEAMS 64
#define SHM_NAME "/spacegl_shm"
#define CMD_QUEUE_SIZE 32

/* 
 * Shared State Structure
 */

#pragma pack(push, 1)
typedef struct {
    double shm_x, shm_y, shm_z;
    double h, m, r;
    int type; /* 1=Player, 3=Base, 4=Star, 5=Planet, 6=BH, 10+=Enemies */
    int ship_class;
    int active;
    int health_pct;
    uint64_t energy;
    int plating;
    int hull_integrity;
    int faction;
    int id;
    int is_cloaked;
    char shm_name[64];
    double vx, vy, vz; /* Velocity vector */
} SharedObject;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    double shm_sx, shm_sy, shm_sz; 
    double shm_tx, shm_ty, shm_tz; 
    int active;
} SharedBeam;
#pragma pack(pop)

typedef struct {
    double shm_x, shm_y, shm_z;
    int active;
    int extra;
} SharedPoint;

typedef struct {
    double shm_x, shm_y, shm_z;
    int species;
    int active;
} SharedDismantle;

typedef struct {
    int active;
    int q1, q2, q3;
    double s1, s2, s3;
    double eta;
    int status;
    double gx, gy, gz;
    double vx, vy, vz;
} SharedProbe;

/* Single Game Frame data */
#pragma pack(push, 1)
typedef struct {
    uint64_t shm_energy;
    int shm_crew;
    int shm_prison_unit;
    int shm_torpedoes;
    int shm_composite_plating;
    double shm_hull_integrity;
    int shm_shields[6];
    uint64_t shm_cargo_energy;
    int shm_cargo_torpedoes;
    int inventory[10];
    double shm_system_health[10];
    double shm_power_dist[3];
    int shm_tube_state;
    int tube_load_timers[4];
    int tube_torpedo_etas[4];
    int current_tube;
    double shm_ion_beam_charge;
    double shm_life_support;
    int shm_anti_matter;
    int shm_lock_target;
    int Korthians;
    char quadrant[128];
    int shm_show_axes;
    int shm_show_grid;
    int shm_show_map;
    int shm_show_bridge;
    int shm_is_docked;
    int shm_red_alert;
    int shm_is_jammed;
    int shm_nav_state;
    int shm_map_filter;
    int shm_force_shutdown;
    int is_cloaked;
    int shm_crypto_algo;
    uint32_t shm_encryption_flags;
    int shm_radio_lock_target;
    uint8_t shm_server_signature[64];
    uint8_t shm_server_pubkey[32];
    int shm_q[3];
    double shm_s[3];
    double shm_h;
    double shm_m;
    double shm_r;
    double shm_eta;
    
    double net_kbps;
    double net_efficiency;
    double net_jitter;
    double net_integrity;
    int net_last_packet_size;
    int net_avg_packet_size;
    int net_packet_count;
    long net_uptime;

    int object_count;
    SharedObject objects[MAX_OBJECTS];

    int beam_count;
    SharedBeam beams[MAX_BEAMS];
    
    int torpedo_count;
    SharedPoint torps[MAX_VISIBLE_TORPEDOES];
    SharedPoint boom;
    SharedPoint wormhole;
    SharedPoint jump_arrival;
    SharedPoint supernova_pos;
    int shm_sn_q[3];
    SharedDismantle dismantle;
    SharedPoint recovery_fx;
    SharedProbe probes[3];
    
    char shm_captain_name[64];
    int shm_target_shields[6];
    long long frame_id;
} GameState;
#pragma pack(pop)

typedef struct {
    char cmd[128];
} IPCCommand;

#define IPC_EVENT_QUEUE_SIZE 4096
#define IPC_EV_BEAM      1
#define IPC_EV_BOOM      2
#define IPC_EV_DISMANTLE 3
#define IPC_EV_RECOVERY  4
#define IPC_EV_JUMP      5
#define IPC_EV_TORPEDO   6
#define IPC_EV_IMPACT    7   /* collision flash: comet or ship hitting a solid object */
#define IPC_TORPEDO_ID_OFFSET 1000

typedef struct {
    int type;
    double x1, y1, z1;
    double x2, y2, z2;
    int extra;
    int padding[2];
} IPCEvent;
/*
 * Shared IPC Structure (Mapped in /dev/shm)
 *
 * NOT packed (2026.10.31.12). It used to sit inside #pragma pack(1): the
 * process-shared pthread_mutex_t and sem_t landed at offsets 1027148 and
 * 1027188 (4 mod 8) and every atomic_int had alignment 1 in the type
 * system. On x86-64 that worked by luck (the semaphore's 64-bit atomic did
 * not cross a cache line); on AArch64 a misaligned 64-bit atomic is an
 * alignment fault. The inner payload structs stay packed; the
 * synchronization fields get their natural alignment, and the ring indices
 * written by different processes sit on their own 64-byte lines (no false
 * sharing between producer and consumer).
 *
 * The segment starts with a layout header written by the command deck that
 * creates it. A viewer or the HUD built from another source version would
 * read a differently laid out segment (or one shorter than its mmap, a
 * SIGBUS): they check the segment size with fstat() and the header with
 * shm_layout_check() before touching anything else.
 */
#define SHM_LAYOUT_MAGIC   0x50494753u   /* "SGIP" */
#define SHM_LAYOUT_VERSION 2u

typedef struct {
    uint32_t layout_magic;
    uint32_t layout_version;
    uint64_t layout_size;      /* sizeof(SharedIPC) of the creator */

    _Alignas(64) atomic_int read_index;  /* Buffer index for Viewer (0 or 1) */
    atomic_int write_index;              /* Buffer index for Client (1 or 0) */

    _Alignas(64) atomic_int event_head;  /* producer: command deck */
    _Alignas(64) atomic_int event_tail;  /* consumer: viewer */
    _Alignas(64) IPCEvent event_queue[IPC_EVENT_QUEUE_SIZE];

    /* Global galaxy state, single buffer (Persistent across frames) */
    _Alignas(64) int64_t shm_galaxy[41][41][41];

    /* Double Buffers */
    _Alignas(64) GameState buffers[3];

    /* Lock-Free Command Queue (Viewer -> Client) */
    _Alignas(64) atomic_int cmd_head;    /* producer: viewer */
    _Alignas(64) atomic_int cmd_tail;    /* consumer: command deck */
    _Alignas(64) IPCCommand cmd_queue[CMD_QUEUE_SIZE];

    /* Telemetry Bridge for Validation */
    _Alignas(64) struct {
        double x, y, z;
        int frame_id;
        atomic_int count;
        atomic_int vk_rcv_count;
    } dismantle_telemetry;

    /* Synchronization (process-shared, natural alignment required) */
    _Alignas(64) pthread_mutex_t mutex;
    _Alignas(64) sem_t data_ready;

    _Alignas(64) atomic_int force_shutdown; /* Immediate global exit signal */
} SharedIPC;

_Static_assert(offsetof(SharedIPC, mutex) % _Alignof(pthread_mutex_t) == 0, "SharedIPC.mutex misaligned");
_Static_assert(offsetof(SharedIPC, data_ready) % _Alignof(sem_t) == 0, "SharedIPC.data_ready misaligned");
_Static_assert(offsetof(SharedIPC, read_index) % _Alignof(atomic_int) == 0 &&
               offsetof(SharedIPC, event_head) % _Alignof(atomic_int) == 0 &&
               offsetof(SharedIPC, event_tail) % _Alignof(atomic_int) == 0 &&
               offsetof(SharedIPC, cmd_head) % _Alignof(atomic_int) == 0 &&
               offsetof(SharedIPC, cmd_tail) % _Alignof(atomic_int) == 0 &&
               offsetof(SharedIPC, force_shutdown) % _Alignof(atomic_int) == 0,
               "SharedIPC atomics misaligned");

/* Creator (command deck, standalone viewer): stamp the header. */
static inline void shm_layout_stamp(SharedIPC *s) {
    s->layout_magic = SHM_LAYOUT_MAGIC;
    s->layout_version = SHM_LAYOUT_VERSION;
    s->layout_size = (uint64_t)sizeof(SharedIPC);
}

/* Reader: segment_size from fstat() on the shm fd, BEFORE mmap (a segment
 * shorter than sizeof(SharedIPC) would SIGBUS on access). Returns NULL if
 * the segment is usable, else a reason for the error message. */
static inline const char *shm_layout_check_size(long long segment_size) {
    if (segment_size < (long long)sizeof(SharedIPC))
        return "shared memory segment smaller than this build's SharedIPC (deck and viewer built from different versions)";
    return NULL;
}

static inline const char *shm_layout_check(const SharedIPC *s) {
    if (s->layout_magic != SHM_LAYOUT_MAGIC) return "shared memory segment has no SpaceGL layout header (deck too old or not a SpaceGL segment)";
    if (s->layout_version != SHM_LAYOUT_VERSION) return "shared memory layout version differs between deck and viewer (rebuild both)";
    if (s->layout_size != (uint64_t)sizeof(SharedIPC)) return "shared memory layout size differs between deck and viewer (rebuild both)";
    return NULL;
}

#endif
