/*
 * SPACE GL - TELEMETRY SERVER ENGINE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include "telemetry.h"
#include "server_internal.h"

#define MAX_TEL_CLIENTS 32
#define TEL_MAX_EVENTS (MAX_TEL_CLIENTS + 2)

typedef struct {
    int fd;
    uint32_t category;
    bool active;
} TelClient;

static TelClient tel_clients[MAX_TEL_CLIENTS];
static int tel_tcp_fd = -1;
static int tel_unix_fd = -1;
static int tel_epoll_fd = -1;
static pthread_t tel_thread;
static bool tel_running = false;

static TelemetryObject broadcast_buffer[MAX_VISIBLE_TEL];

const char* cat_names_diag[] = {
    "SHIPS ALLIANCE", "SHIPS KORTHIAN", "SHIPS XYLARI", "SHIPS SWARM", 
    "SHIPS VESPERIAN", "SHIPS ASCENDANT", "SHIPS QUARZITE", "SHIPS SAURIAN",
    "SHIPS GILDED", "SHIPS FLUIDIC", "SHIPS CRYOS", "SHIPS APEX",
    "STARS", "PLANETS", "BASES", "BLACK HOLES", "NEBULAS", "PULSARS",
    "QUASARS", "COMETS", "ASTEROIDS", "DERELICTS", "MINES", "BUOYS", "PLATFORMS", "RIFTS",
    "MONSTERS", "DYSON", "HUBS", "RELICS", "RUPTURES", "SATELLITES", "STORMS", "TORPEDOES",
    "ARTIFACTS", "WARP GATES", "NEUTRON STARS", "MEGA STRUCTS", "DARK CLOUDS", "SINGULARITIES",
    "PLASMA STORMS", "ORBITAL RINGS", "TIME ANOMALIES", "VOID CRYSTALS", "ANOMALIES"
};

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int get_faction_from_cat(int cat) {
    switch(cat) {
        case TEL_CAT_SHIP_ALLIANCE: return FACTION_ALLIANCE;
        case TEL_CAT_SHIP_KORTHIAN: return FACTION_KORTHIAN;
        case TEL_CAT_SHIP_XYLARI:   return FACTION_XYLARI;
        case TEL_CAT_SHIP_SWARM:    return FACTION_SWARM;
        case TEL_CAT_SHIP_VESPERIAN: return FACTION_VESPERIAN;
        case TEL_CAT_SHIP_ASCENDANT: return FACTION_JEM_HADAR;
        case TEL_CAT_SHIP_QUARZITE:  return FACTION_THOLIAN;
        case TEL_CAT_SHIP_SAURIAN:   return FACTION_GORN;
        case TEL_CAT_SHIP_GILDED:    return FACTION_GILDED;
        case TEL_CAT_SHIP_FLUIDIC:   return FACTION_SPECIES_8472;
        case TEL_CAT_SHIP_CRYOS:     return FACTION_BREEN;
        case TEL_CAT_SHIP_APEX:      return FACTION_HIROGEN;
        default: return -1;
    }
}

static int get_color_from_faction(int faction) {
    switch(faction) {
        case FACTION_ALLIANCE:     return 1;
        case FACTION_KORTHIAN:     return 2;
        case FACTION_XYLARI:       return 3;
        case FACTION_SWARM:        return 4;
        case FACTION_VESPERIAN:    return 5;
        case FACTION_JEM_HADAR:    return 6;
        case FACTION_THOLIAN:      return 7;
        case FACTION_GORN:         return 8;
        case FACTION_GILDED:       return 9;
        case FACTION_SPECIES_8472: return 10;
        case FACTION_BREEN:        return 11;
        case FACTION_HIROGEN:      return 12;
        default:                   return 13;
    }
}

static void* telemetry_worker(void* arg) {
    (void)arg;
    struct epoll_event ev, events[TEL_MAX_EVENTS];

    while (tel_running) {
        int nfds = epoll_wait(tel_epoll_fd, events, TEL_MAX_EVENTS, 100);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == tel_tcp_fd || fd == tel_unix_fd) {
                struct sockaddr_storage addr;
                socklen_t addrlen = sizeof(addr);
                int client_fd = accept(fd, (struct sockaddr*)&addr, &addrlen);
                if (client_fd == -1) continue;

                set_nonblocking(client_fd);
                ev.events = EPOLLIN;
                ev.data.fd = client_fd;
                epoll_ctl(tel_epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);

                for (int j = 0; j < MAX_TEL_CLIENTS; j++) {
                    if (!tel_clients[j].active) {
                        tel_clients[j].fd = client_fd;
                        tel_clients[j].category = TEL_CAT_SHIP_ALLIANCE;
                        tel_clients[j].active = true;
                        break;
                    }
                }
            } else {
                TelemetryHeader hdr;
                ssize_t r = read(fd, &hdr, sizeof(hdr));
                if (r <= 0) {
                    epoll_ctl(tel_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    for (int j = 0; j < MAX_TEL_CLIENTS; j++) {
                        if (tel_clients[j].active && tel_clients[j].fd == fd) {
                            tel_clients[j].active = false;
                            break;
                        }
                    }
                } else if (hdr.type == TEL_PKT_SUBSCRIBE) {
                    TelemetrySubscribe sub;
                    if (read(fd, &sub, sizeof(sub)) == sizeof(sub)) {
                        for (int j = 0; j < MAX_TEL_CLIENTS; j++) {
                            if (tel_clients[j].active && tel_clients[j].fd == fd) {
                                tel_clients[j].category = sub.category;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    return NULL;
}

void telemetry_init() {
    printf("\033[1;36m[TELEMETRY]\033[0m Initializing Subsystem...\n");
    tel_epoll_fd = epoll_create1(0);
    bool tcp_ok = false;
    bool unix_ok = false;
    
    tel_tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tel_tcp_fd >= 0) {
        int opt = 1;
        setsockopt(tel_tcp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in tcp_addr = {0};
        tcp_addr.sin_family = AF_INET;
        tcp_addr.sin_addr.s_addr = INADDR_ANY;
        tcp_addr.sin_port = htons(TELEMETRY_DEFAULT_PORT);
        if (bind(tel_tcp_fd, (struct sockaddr*)&tcp_addr, sizeof(tcp_addr)) == 0) {
            listen(tel_tcp_fd, 5);
            set_nonblocking(tel_tcp_fd);
            struct epoll_event ev = {EPOLLIN, {.fd = tel_tcp_fd}};
            epoll_ctl(tel_epoll_fd, EPOLL_CTL_ADD, tel_tcp_fd, &ev);
            printf("\033[1;34m[TELEMETRY]\033[0m TCP Uplink established on port %d\n", TELEMETRY_DEFAULT_PORT);
            tcp_ok = true;
        }
    }

    unlink(TELEMETRY_UNIX_PATH);
    tel_unix_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (tel_unix_fd >= 0) {
        struct sockaddr_un unix_addr = {0, ""};
        unix_addr.sun_family = AF_UNIX;
        strcpy(unix_addr.sun_path, TELEMETRY_UNIX_PATH);
        if (bind(tel_unix_fd, (struct sockaddr*)&unix_addr, sizeof(unix_addr)) == 0) {
            chmod(TELEMETRY_UNIX_PATH, 0666);
            listen(tel_unix_fd, 5);
            set_nonblocking(tel_unix_fd);
            struct epoll_event ev = {EPOLLIN, {.fd = tel_unix_fd}};
            epoll_ctl(tel_epoll_fd, EPOLL_CTL_ADD, tel_unix_fd, &ev);
            printf("\033[1;34m[TELEMETRY]\033[0m Unix Socket created at %s\n", TELEMETRY_UNIX_PATH);
            unix_ok = true;
        }
    }

    if (tcp_ok || unix_ok) {
        memset(tel_clients, 0, sizeof(tel_clients));
        tel_running = true;
        pthread_create(&tel_thread, NULL, telemetry_worker, NULL);

        printf("\033[1;35m[TELEMETRY]\033[0m Scanning Implementation Coverage...\n");
        printf("\033[1;37m CATEGORY                | STATUS  | ACTIVE ENTITIES \033[0m\n");
        printf("-------------------------|---------|-----------------\n");
        
        for (int c = 0; c < TEL_CAT_COUNT; c++) {
            int count = 0;
            int faction = get_faction_from_cat(c);
            
            if (faction != -1) {
                for (int j = 0; j < MAX_CLIENTS; j++) if (players[j].active && players[j].faction == faction) count++;
                for (int j = 0; j < MAX_NPC; j++) if (npcs[j].active && npcs[j].faction == faction) count++;
                for (int j = 0; j < MAX_BASES; j++) if (bases[j].active && bases[j].faction == faction) count++;
            } else {
                #define COUNT_CAT(arr, max) for(int j=0; j<max; j++) if(arr[j].active) count++
                switch(c) {
                    case TEL_CAT_STAR:      COUNT_CAT(stars_data, MAX_STARS); break;
                    case TEL_CAT_PLANET:    COUNT_CAT(planets, MAX_PLANETS); break;
                    case TEL_CAT_BASE:      COUNT_CAT(bases, MAX_BASES); break;
                    case TEL_CAT_BH:        COUNT_CAT(black_holes, MAX_BH); break;
                    case TEL_CAT_NEBULA:    COUNT_CAT(nebulas, MAX_NEBULAS); break;
                    case TEL_CAT_PULSAR:    COUNT_CAT(pulsars, MAX_PULSARS); break;
                    case TEL_CAT_QUASAR:    COUNT_CAT(quasars, MAX_QUASARS); break;
                    case TEL_CAT_COMET:     COUNT_CAT(comets, MAX_COMETS); break;
                    case TEL_CAT_ASTEROID:  COUNT_CAT(asteroids, MAX_ASTEROIDS); break;
                    case TEL_CAT_DERELICT:  COUNT_CAT(derelicts, MAX_DERELICTS); break;
                    case TEL_CAT_MINE:      COUNT_CAT(mines, MAX_MINES); break;
                    case TEL_CAT_BUOY:      COUNT_CAT(buoys, MAX_BUOYS); break;
                    case TEL_CAT_PLATFORM:  COUNT_CAT(platforms, MAX_PLATFORMS); break;
                    case TEL_CAT_RIFT:      COUNT_CAT(rifts, MAX_RIFTS); break;
                    case TEL_CAT_MONSTER:   COUNT_CAT(monsters, MAX_MONSTERS); break;
                    case TEL_CAT_DYSON:     COUNT_CAT(dysons, MAX_DYSON); break;
                    case TEL_CAT_HUB:       COUNT_CAT(hubs, MAX_HUBS); break;
                    case TEL_CAT_RELIC:     COUNT_CAT(relics, MAX_RELICS); break;
                    case TEL_CAT_RUPTURE:   COUNT_CAT(ruptures, MAX_RUPTURES); break;
                    case TEL_CAT_SATELLITE: COUNT_CAT(satellites, MAX_SATELLITES); break;
                    case TEL_CAT_STORM:     COUNT_CAT(storms, MAX_STORMS); break;
                    case TEL_CAT_TORPEDO:   COUNT_CAT(players_torpedoes, MAX_GLOBAL_TORPEDOES); break;
                    case TEL_CAT_ARTIFACT:  COUNT_CAT(artifacts, MAX_ARTIFACTS); break;
                    case TEL_CAT_WARP_GATE: COUNT_CAT(warp_gates, MAX_WARP_GATES); break;
                    case TEL_CAT_NEUTRON_STAR: COUNT_CAT(neutron_stars, MAX_NEUTRON_STARS); break;
                    case TEL_CAT_MEGA_STRUCT: COUNT_CAT(mega_structs, MAX_MEGA_STRUCTS); break;
                    case TEL_CAT_DARK_CLOUD: COUNT_CAT(dark_clouds, MAX_DARK_CLOUDS); break;
                    case TEL_CAT_SINGULARITY: COUNT_CAT(singularities, MAX_SINGULARITIES); break;
                    case TEL_CAT_PLASMA_STORM: COUNT_CAT(plasma_storms, MAX_PLASMA_STORMS); break;
                    case TEL_CAT_ORBITAL_RING: COUNT_CAT(orbital_rings, MAX_ORBITAL_RINGS); break;
                    case TEL_CAT_TIME_ANOMALY: COUNT_CAT(time_anomalies, MAX_TIME_ANOMALIES); break;
                    case TEL_CAT_VOID_CRYSTAL: COUNT_CAT(void_crystals, MAX_VOID_CRYSTALS); break;
                    case TEL_CAT_ANOMALY:   COUNT_CAT(subspace_anomalies, MAX_SUBSPACE_ANOMALIES); break;
                }
            }
            printf(" %-23s | \033[1;32mREADY\033[0m   | %-15d\n", cat_names_diag[c], count);
        }
        printf("-------------------------|---------|-----------------\n");
        printf("\033[1;32m[TELEMETRY]\033[0m Subsystem fully OPERATIONAL (%d Factional Monitors).\n", TEL_CAT_COUNT);
    }
}

static void fill_obj(TelemetryObject* to, int cat, int idx) {
    memset(to, 0, sizeof(TelemetryObject));
    to->integrity = 100;
    to->color_pair = 13;
    strcpy(to->extra, "-");

    int faction = get_faction_from_cat(cat);
    if (faction != -1) {
        if (idx < MAX_CLIENTS) {
            ConnectedPlayer* p = &players[idx];
            sprintf(to->id, "P%02d", idx + 1);
            strncpy(to->name, p->name, 31);
            sprintf(to->info, "%s", get_species_name(p->faction));
            to->q1 = p->state.q1; to->q2 = p->state.q2; to->q3 = p->state.q3;
            to->x = p->state.s1; to->y = p->state.s2; to->z = p->state.s3;
            to->integrity = (uint32_t)p->state.hull_integrity;
            to->energy = p->state.energy;
            to->color_pair = get_color_from_faction(p->faction);
        } else if (idx < MAX_CLIENTS + MAX_NPC) {
            NPCShip* n = &npcs[idx - MAX_CLIENTS];
            sprintf(to->id, "N%04d", n->id);
            strncpy(to->name, n->name, 31);
            sprintf(to->info, "%s", get_species_name(n->faction));
            to->q1 = n->q1; to->q2 = n->q2; to->q3 = n->q3;
            to->x = n->x; to->y = n->y; to->z = n->z;
            to->integrity = (uint32_t)n->health;
            to->energy = n->energy;
            to->color_pair = get_color_from_faction(n->faction);
        } else {
            NPCBase* b = &bases[idx - MAX_CLIENTS - MAX_NPC];
            sprintf(to->id, "B%04d", b->id);
            sprintf(to->name, "%s Starbase", get_species_name(b->faction));
            sprintf(to->info, "BASE");
            to->q1 = b->q1; to->q2 = b->q2; to->q3 = b->q3;
            to->x = b->x; to->y = b->y; to->z = b->z;
            to->integrity = b->health;
            to->color_pair = get_color_from_faction(b->faction);
        }
        return;
    }

    switch(cat) {
        case TEL_CAT_STAR: {
            NPCStar* s = &stars_data[idx];
            sprintf(to->id, "S%04d", s->id);
            strcpy(to->name, "Star");
            sprintf(to->info, "Spectral %d", s->faction);
            to->q1 = s->q1; to->q2 = s->q2; to->q3 = s->q3;
            to->x = s->x; to->y = s->y; to->z = s->z;
            to->color_pair = 4;
        } break;
        case TEL_CAT_PLANET: {
            NPCPlanet* p = &planets[idx];
            sprintf(to->id, "PL%04d", p->id);
            strcpy(to->name, "Planet");
            sprintf(to->info, "Res:%d", p->resource_type);
            to->q1 = p->q1; to->q2 = p->q2; to->q3 = p->q3;
            to->x = p->x; to->y = p->y; to->z = p->z;
            sprintf(to->extra, "Amt:%d", p->amount);
            to->color_pair = 3;
        } break;
        case TEL_CAT_BASE: {
            NPCBase* b = &bases[idx];
            sprintf(to->id, "B%04d", b->id);
            sprintf(to->name, "%s Base", get_species_name(b->faction));
            sprintf(to->info, "Faction %d", b->faction);
            to->q1 = b->q1; to->q2 = b->q2; to->q3 = b->q3;
            to->x = b->x; to->y = b->y; to->z = b->z;
            to->integrity = b->health;
            to->color_pair = get_color_from_faction(b->faction);
        } break;
        case TEL_CAT_BH: {
            NPCBlackHole* b = &black_holes[idx];
            sprintf(to->id, "BH%04d", b->id);
            strcpy(to->name, "Black Hole");
            strcpy(to->info, "Singularity");
            to->q1 = b->q1; to->q2 = b->q2; to->q3 = b->q3;
            to->x = b->x; to->y = b->y; to->z = b->z;
            to->color_pair = 2;
        } break;
        case TEL_CAT_NEBULA: {
            NPCNebula* n = &nebulas[idx];
            sprintf(to->id, "NEB%04d", n->id);
            strcpy(to->name, "Nebula");
            sprintf(to->info, "Type:%d", n->type);
            to->q1 = n->q1; to->q2 = n->q2; to->q3 = n->q3;
            to->x = n->x; to->y = n->y; to->z = n->z;
            to->color_pair = 13;
        } break;
        case TEL_CAT_PULSAR: {
            NPCPulsar* p = &pulsars[idx];
            sprintf(to->id, "PUL%04d", p->id);
            strcpy(to->name, "Pulsar");
            sprintf(to->info, "Type:%d", p->type);
            to->q1 = p->q1; to->q2 = p->q2; to->q3 = p->q3;
            to->x = p->x; to->y = p->y; to->z = p->z;
            to->color_pair = 4;
        } break;
        case TEL_CAT_QUASAR: {
            NPCQuasar* q = &quasars[idx];
            sprintf(to->id, "QSR%04d", q->id);
            strcpy(to->name, "Quasar");
            sprintf(to->info, "Type:%d", q->type);
            to->q1 = q->q1; to->q2 = q->q2; to->q3 = q->q3;
            to->x = q->x; to->y = q->y; to->z = q->z;
            to->color_pair = 4;
        } break;
        case TEL_CAT_COMET: {
            NPCComet* c = &comets[idx];
            sprintf(to->id, "COM%04d", c->id);
            strcpy(to->name, "Comet");
            strcpy(to->info, "Moving");
            to->q1 = c->q1; to->q2 = c->q2; to->q3 = c->q3;
            to->x = c->x; to->y = c->y; to->z = c->z;
            to->color_pair = 3;
        } break;
        case TEL_CAT_ASTEROID: {
            NPCAsteroid* a = &asteroids[idx];
            sprintf(to->id, "AST%04d", a->id);
            strcpy(to->name, "Asteroid");
            sprintf(to->info, "Res:%d", a->resource_type);
            to->q1 = a->q1; to->q2 = a->q2; to->q3 = a->q3;
            to->x = a->x; to->y = a->y; to->z = a->z;
            sprintf(to->extra, "Amt:%d", a->amount);
            to->color_pair = 4;
        } break;
        case TEL_CAT_DERELICT: {
            NPCDerelict* d = &derelicts[idx];
            sprintf(to->id, "DE%04d", d->id);
            strncpy(to->name, d->name, 31);
            sprintf(to->info, "Class %d", d->ship_class);
            to->q1 = d->q1; to->q2 = d->q2; to->q3 = d->q3;
            to->x = d->x; to->y = d->y; to->z = d->z;
            to->color_pair = 13;
        } break;
        case TEL_CAT_MINE: {
            NPCMine* m = &mines[idx];
            sprintf(to->id, "MIN%04d", m->id);
            strcpy(to->name, "Mine");
            sprintf(to->info, "Faction %d", m->faction);
            to->q1 = m->q1; to->q2 = m->q2; to->q3 = m->q3;
            to->x = m->x; to->y = m->y; to->z = m->z;
            to->color_pair = 2;
        } break;
        case TEL_CAT_BUOY: {
            NPCBuoy* b = &buoys[idx];
            sprintf(to->id, "BUY%04d", b->id);
            strcpy(to->name, "Comm Buoy");
            strcpy(to->info, "Active");
            to->q1 = b->q1; to->q2 = b->q2; to->q3 = b->q3;
            to->x = b->x; to->y = b->y; to->z = b->z;
            to->color_pair = 3;
        } break;
        case TEL_CAT_PLATFORM: {
            NPCPlatform* p = &platforms[idx];
            sprintf(to->id, "PF%04d", p->id);
            strcpy(to->name, "Platform");
            sprintf(to->info, "Fac:%d", p->faction);
            to->q1 = p->q1; to->q2 = p->q2; to->q3 = p->q3;
            to->x = p->x; to->y = p->y; to->z = p->z;
            to->integrity = p->health;
            to->energy = p->energy;
            to->color_pair = 2;
        } break;
        case TEL_CAT_RIFT: {
            NPCRift* r = &rifts[idx];
            sprintf(to->id, "RIF%04d", r->id);
            strcpy(to->name, "Spatial Rift");
            strcpy(to->info, "Active");
            to->q1 = r->q1; to->q2 = r->q2; to->q3 = r->q3;
            to->x = r->x; to->y = r->y; to->z = r->z;
            to->color_pair = 3;
        } break;
        case TEL_CAT_MONSTER: {
            NPCMonster* m = &monsters[idx];
            sprintf(to->id, "M%02d", m->id);
            strcpy(to->name, (m->type == 30) ? "Crystalline" : "Amoeba");
            strcpy(to->info, "OMEGA");
            to->q1 = m->q1; to->q2 = m->q2; to->q3 = m->q3;
            to->x = m->x; to->y = m->y; to->z = m->z;
            to->integrity = m->health;
            to->color_pair = 2;
        } break;
        case TEL_CAT_DYSON: {
            NPCDyson* d = &dysons[idx];
            sprintf(to->id, "DY%04d", d->id);
            strcpy(to->name, "Dyson Frag");
            strcpy(to->info, "Ancient");
            to->q1 = d->q1; to->q2 = d->q2; to->q3 = d->q3;
            to->x = d->x; to->y = d->y; to->z = d->z;
            to->color_pair = 4;
        } break;
        case TEL_CAT_HUB: {
            NPCHub* h = &hubs[idx];
            sprintf(to->id, "HB%04d", h->id);
            strcpy(to->name, "Trading Hub");
            strcpy(to->info, "Neutral");
            to->q1 = h->q1; to->q2 = h->q2; to->q3 = h->q3;
            to->x = h->x; to->y = h->y; to->z = h->z;
            to->color_pair = 3;
        } break;
        case TEL_CAT_RELIC: {
            NPCRelic* r = &relics[idx];
            sprintf(to->id, "RE%04d", r->id);
            strcpy(to->name, "Ancient Relic");
            strcpy(to->info, "Tech");
            to->q1 = r->q1; to->q2 = r->q2; to->q3 = r->q3;
            to->x = r->x; to->y = r->y; to->z = r->z;
            to->color_pair = 3;
        } break;
        case TEL_CAT_RUPTURE: {
            NPCRupture* r = &ruptures[idx];
            sprintf(to->id, "RU%04d", r->id);
            strcpy(to->name, "Subspace Rup");
            strcpy(to->info, "Anomaly");
            to->q1 = r->q1; to->q2 = r->q2; to->q3 = r->q3;
            to->x = r->x; to->y = r->y; to->z = r->z;
            to->color_pair = 2;
        } break;
        case TEL_CAT_SATELLITE: {
            NPCSatellite* s = &satellites[idx];
            sprintf(to->id, "SA%04d", s->id);
            strcpy(to->name, "Satellite");
            strcpy(to->info, "Relay");
            to->q1 = s->q1; to->q2 = s->q2; to->q3 = s->q3;
            to->x = s->x; to->y = s->y; to->z = s->z;
            to->color_pair = 13;
        } break;
        case TEL_CAT_STORM: {
            NPCStorm* s = &storms[idx];
            sprintf(to->id, "SO%04d", s->id);
            strcpy(to->name, "Ion Storm");
            strcpy(to->info, "Meteo");
            to->q1 = s->q1; to->q2 = s->q2; to->q3 = s->q3;
            to->x = s->x; to->y = s->y; to->z = s->z;
            to->color_pair = 13;
        } break;
        case TEL_CAT_TORPEDO: {
            PlayerTorpedo* t = &players_torpedoes[idx];
            sprintf(to->id, "T%04d", t->id);
            strcpy(to->name, "Torpedo");
            sprintf(to->info, "Owner %d", t->owner_idx);
            to->q1 = t->q1; to->q2 = t->q2; to->q3 = t->q3;
            to->x = t->x; to->y = t->y; to->z = t->z;
            sprintf(to->extra, "TO:%d", t->timeout);
            to->color_pair = 2;
        } break;
        case TEL_CAT_ARTIFACT: {
            NPCArtifact* a = &artifacts[idx];
            sprintf(to->id, "AA%04d", a->id);
            strcpy(to->name, "Alien Artifact");
            strcpy(to->info, "Exotic");
            to->q1 = a->q1; to->q2 = a->q2; to->q3 = a->q3;
            to->x = a->x; to->y = a->y; to->z = a->z;
            to->color_pair = 4;
        } break;
        case TEL_CAT_WARP_GATE: {
            NPCWarpGate* w = &warp_gates[idx];
            sprintf(to->id, "WG%04d", w->id);
            strcpy(to->name, "Warp Gate");
            strcpy(to->info, "Active");
            to->q1 = w->q1; to->q2 = w->q2; to->q3 = w->q3;
            to->x = w->x; to->y = w->y; to->z = w->z;
            to->color_pair = 3;
        } break;
        case TEL_CAT_NEUTRON_STAR: {
            NPCNeutronStar* n = &neutron_stars[idx];
            sprintf(to->id, "NS%04d", n->id);
            strcpy(to->name, "Neutron Star");
            strcpy(to->info, "Degenerate");
            to->q1 = n->q1; to->q2 = n->q2; to->q3 = n->q3;
            to->x = n->x; to->y = n->y; to->z = n->z;
            to->color_pair = 13;
        } break;
        case TEL_CAT_MEGA_STRUCT: {
            NPCMegaStructure* m = &mega_structs[idx];
            sprintf(to->id, "MS%04d", m->id);
            strcpy(to->name, "Mega Struct");
            strcpy(to->info, "Unknown");
            to->q1 = m->q1; to->q2 = m->q2; to->q3 = m->q3;
            to->x = m->x; to->y = m->y; to->z = m->z;
            to->color_pair = 1;
        } break;
        case TEL_CAT_DARK_CLOUD: {
            NPCDarkCloud* d = &dark_clouds[idx];
            sprintf(to->id, "DC%04d", d->id);
            strcpy(to->name, "Dark Matter");
            strcpy(to->info, "Obscured");
            to->q1 = d->q1; to->q2 = d->q2; to->q3 = d->q3;
            to->x = d->x; to->y = d->y; to->z = d->z;
            to->color_pair = 2;
        } break;
        case TEL_CAT_SINGULARITY: {
            NPCSingularity* s = &singularities[idx];
            sprintf(to->id, "QS%04d", s->id);
            strcpy(to->name, "Singularity");
            strcpy(to->info, "Quantum");
            to->q1 = s->q1; to->q2 = s->q2; to->q3 = s->q3;
            to->x = s->x; to->y = s->y; to->z = s->z;
            to->color_pair = 2;
        } break;
        case TEL_CAT_PLASMA_STORM: {
            NPCPlasmaStorm* p = &plasma_storms[idx];
            sprintf(to->id, "PS%04d", p->id);
            strcpy(to->name, "Plasma Storm");
            strcpy(to->info, "Unstable");
            to->q1 = p->q1; to->q2 = p->q2; to->q3 = p->q3;
            to->x = p->x; to->y = p->y; to->z = p->z;
            to->color_pair = 4;
        } break;
        case TEL_CAT_ORBITAL_RING: {
            NPCOrbitalRing* o = &orbital_rings[idx];
            sprintf(to->id, "OR%04d", o->id);
            strcpy(to->name, "Orbital Ring");
            strcpy(to->info, "Planetary");
            to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
            to->x = o->x; to->y = o->y; to->z = o->z;
            to->color_pair = 3;
        } break;
        case TEL_CAT_TIME_ANOMALY: {
            NPCTimeAnomaly* t = &time_anomalies[idx];
            sprintf(to->id, "TA%04d", t->id);
            strcpy(to->name, "Time Anomaly");
            strcpy(to->info, "Temporal");
            to->q1 = t->q1; to->q2 = t->q2; to->q3 = t->q3;
            to->x = t->x; to->y = t->y; to->z = t->z;
            to->color_pair = 1;
        } break;
        case TEL_CAT_VOID_CRYSTAL: {
            NPCVoidCrystal* v = &void_crystals[idx];
            sprintf(to->id, "VC%04d", v->id);
            strcpy(to->name, "Void Crystal");
            strcpy(to->info, "Crystalline");
            to->q1 = v->q1; to->q2 = v->q2; to->q3 = v->q3;
            to->x = v->x; to->y = v->y; to->z = v->z;
            to->color_pair = 13;
        } break;
        case TEL_CAT_ANOMALY: {
            NPCSubspaceAnomaly* s = &subspace_anomalies[idx];
            sprintf(to->id, "AN%04d", s->id);
            strcpy(to->name, "Subspace Anom");
            strcpy(to->info, "Unstable");
            to->q1 = s->q1; to->q2 = s->q2; to->q3 = s->q3;
            to->x = s->x; to->y = s->y; to->z = s->z;
            to->color_pair = 3;
        } break;
        default: break;
    }
}

void telemetry_broadcast() {
    if (!tel_running) return;

    TelemetryHeader hdr;
    hdr.type = TEL_PKT_DATA;

    for (int i = 0; i < MAX_TEL_CLIENTS; i++) {
        if (!tel_clients[i].active) continue;

        uint32_t cat = tel_clients[i].category;
        int count = 0;

        int faction = get_faction_from_cat(cat);
        if (faction != -1) {
            for (int j = 0; j < MAX_CLIENTS && count < MAX_VISIBLE_TEL; j++) {
                if (players[j].active && players[j].faction == faction) fill_obj(&broadcast_buffer[count++], cat, j);
            }
            for (int j = 0; j < MAX_NPC && count < MAX_VISIBLE_TEL; j++) {
                if (npcs[j].active && npcs[j].faction == faction) fill_obj(&broadcast_buffer[count++], cat, j + MAX_CLIENTS);
            }
            for (int j = 0; j < MAX_BASES && count < MAX_VISIBLE_TEL; j++) {
                if (bases[j].active && bases[j].faction == faction) fill_obj(&broadcast_buffer[count++], cat, j + MAX_CLIENTS + MAX_NPC);
            }
        } else {
            #define FILL_CAT(arr, max) for(int j=0; j<max && count<MAX_VISIBLE_TEL; j++) if(arr[j].active) fill_obj(&broadcast_buffer[count++], cat, j)
            switch(cat) {
                case TEL_CAT_STAR:      FILL_CAT(stars_data, MAX_STARS); break;
                case TEL_CAT_PLANET:    FILL_CAT(planets, MAX_PLANETS); break;
                case TEL_CAT_BASE:      FILL_CAT(bases, MAX_BASES); break;
                case TEL_CAT_BH:        FILL_CAT(black_holes, MAX_BH); break;
                case TEL_CAT_NEBULA:    FILL_CAT(nebulas, MAX_NEBULAS); break;
                case TEL_CAT_PULSAR:    FILL_CAT(pulsars, MAX_PULSARS); break;
                case TEL_CAT_QUASAR:    FILL_CAT(quasars, MAX_QUASARS); break;
                case TEL_CAT_COMET:     FILL_CAT(comets, MAX_COMETS); break;
                case TEL_CAT_ASTEROID:  FILL_CAT(asteroids, MAX_ASTEROIDS); break;
                case TEL_CAT_DERELICT:  FILL_CAT(derelicts, MAX_DERELICTS); break;
                case TEL_CAT_MINE:      FILL_CAT(mines, MAX_MINES); break;
                case TEL_CAT_BUOY:      FILL_CAT(buoys, MAX_BUOYS); break;
                case TEL_CAT_PLATFORM:  FILL_CAT(platforms, MAX_PLATFORMS); break;
                case TEL_CAT_RIFT:      FILL_CAT(rifts, MAX_RIFTS); break;
                case TEL_CAT_MONSTER:   FILL_CAT(monsters, MAX_MONSTERS); break;
                case TEL_CAT_DYSON:     FILL_CAT(dysons, MAX_DYSON); break;
                case TEL_CAT_HUB:       FILL_CAT(hubs, MAX_HUBS); break;
                case TEL_CAT_RELIC:     FILL_CAT(relics, MAX_RELICS); break;
                case TEL_CAT_RUPTURE:   FILL_CAT(ruptures, MAX_RUPTURES); break;
                case TEL_CAT_SATELLITE: FILL_CAT(satellites, MAX_SATELLITES); break;
                case TEL_CAT_STORM:     FILL_CAT(storms, MAX_STORMS); break;
                case TEL_CAT_TORPEDO:   FILL_CAT(players_torpedoes, MAX_GLOBAL_TORPEDOES); break;
                case TEL_CAT_ARTIFACT:  FILL_CAT(artifacts, MAX_ARTIFACTS); break;
                case TEL_CAT_WARP_GATE: FILL_CAT(warp_gates, MAX_WARP_GATES); break;
                case TEL_CAT_NEUTRON_STAR: FILL_CAT(neutron_stars, MAX_NEUTRON_STARS); break;
                case TEL_CAT_MEGA_STRUCT: FILL_CAT(mega_structs, MAX_MEGA_STRUCTS); break;
                case TEL_CAT_DARK_CLOUD: FILL_CAT(dark_clouds, MAX_DARK_CLOUDS); break;
                case TEL_CAT_SINGULARITY: FILL_CAT(singularities, MAX_SINGULARITIES); break;
                case TEL_CAT_PLASMA_STORM: FILL_CAT(plasma_storms, MAX_PLASMA_STORMS); break;
                case TEL_CAT_ORBITAL_RING: FILL_CAT(orbital_rings, MAX_ORBITAL_RINGS); break;
                case TEL_CAT_TIME_ANOMALY: FILL_CAT(time_anomalies, MAX_TIME_ANOMALIES); break;
                case TEL_CAT_VOID_CRYSTAL: FILL_CAT(void_crystals, MAX_VOID_CRYSTALS); break;
                case TEL_CAT_ANOMALY:   FILL_CAT(subspace_anomalies, MAX_SUBSPACE_ANOMALIES); break;
            }
        }

        hdr.length = count * sizeof(TelemetryObject);
        if (send(tel_clients[i].fd, &hdr, sizeof(hdr), MSG_NOSIGNAL) == sizeof(hdr)) {
            if (hdr.length > 0) {
                send(tel_clients[i].fd, broadcast_buffer, hdr.length, MSG_NOSIGNAL);
            }
        }

        TelemetryHeader shdr = {TEL_PKT_STATS, sizeof(TelemetryStats)};
        TelemetryStats stats = {
            .tick = global_tick,
            .active_players = 0,
            .active_npcs = 0
        };
        for(int j=0; j<MAX_CLIENTS; j++) if(players[j].active) stats.active_players++;
        for(int j=0; j<MAX_NPC; j++) if(npcs[j].active) stats.active_npcs++;
        
        if (send(tel_clients[i].fd, &shdr, sizeof(shdr), MSG_NOSIGNAL) == sizeof(shdr)) {
            send(tel_clients[i].fd, &stats, sizeof(stats), MSG_NOSIGNAL);
        }
    }
}
