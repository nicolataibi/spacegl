/*
 * SPACE GL - TELEMETRY PROTOCOL
 */

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>
#include "game_config.h"

#define TELEMETRY_DEFAULT_PORT 5001
#define TELEMETRY_UNIX_PATH "/tmp/spacegl_telemetry.sock"

typedef enum {
    TEL_CAT_SHIP_ALLIANCE = 0,
    TEL_CAT_SHIP_KORTHIAN,
    TEL_CAT_SHIP_XYLARI,
    TEL_CAT_SHIP_SWARM,
    TEL_CAT_SHIP_VESPERIAN,
    TEL_CAT_SHIP_ASCENDANT,
    TEL_CAT_SHIP_QUARZITE,
    TEL_CAT_SHIP_SAURIAN,
    TEL_CAT_SHIP_GILDED,
    TEL_CAT_SHIP_FLUIDIC,
    TEL_CAT_SHIP_CRYOS,
    TEL_CAT_SHIP_APEX,
    TEL_CAT_STAR,
    TEL_CAT_PLANET,
    TEL_CAT_BASE,
    TEL_CAT_BH,
    TEL_CAT_NEBULA,
    TEL_CAT_PULSAR,
    TEL_CAT_QUASAR,
    TEL_CAT_COMET,
    TEL_CAT_ASTEROID,
    TEL_CAT_DERELICT,
    TEL_CAT_MINE,
    TEL_CAT_BUOY,
    TEL_CAT_PLATFORM,
    TEL_CAT_RIFT,
    TEL_CAT_MONSTER,
    TEL_CAT_DYSON,
    TEL_CAT_HUB,
    TEL_CAT_RELIC,
    TEL_CAT_RUPTURE,
    TEL_CAT_SATELLITE,
    TEL_CAT_STORM,
    TEL_CAT_TORPEDO,
    TEL_CAT_ARTIFACT,
    TEL_CAT_WARP_GATE,
    TEL_CAT_NEUTRON_STAR,
    TEL_CAT_MEGA_STRUCT,
    TEL_CAT_DARK_CLOUD,
    TEL_CAT_SINGULARITY,
    TEL_CAT_PLASMA_STORM,
    TEL_CAT_ORBITAL_RING,
    TEL_CAT_TIME_ANOMALY,
    TEL_CAT_VOID_CRYSTAL,
    TEL_CAT_ANOMALY,
    TEL_CAT_COUNT
} TelemetryCategory;

#pragma pack(push, 1)

typedef struct {
    uint32_t type;   /* Packet Type */
    uint32_t length; /* Payload length */
} TelemetryHeader;

#define TEL_PKT_SUBSCRIBE 1  /* Client -> Server: Select category */
#define TEL_PKT_DATA      2  /* Server -> Client: Object list data */
#define TEL_PKT_STATS     3  /* Server -> Client: Global server stats */

#define MAX_VISIBLE_TEL 5000

typedef struct {
    uint32_t category;
} TelemetrySubscribe;

typedef struct {
    uint32_t tick;
    uint32_t active_players;
    uint32_t active_npcs;
    uint32_t total_entities;
    float cpu_load;
    uint64_t ram_usage;
    uint32_t uptime;
} TelemetryStats;

typedef struct {
    char id[16];
    char name[32];
    char info[32];
    int32_t q1, q2, q3;
    double x, y, z;
    uint32_t integrity;
    uint64_t energy;
    char extra[32];
    uint32_t color_pair;
} TelemetryObject;

#pragma pack(pop)

#endif
