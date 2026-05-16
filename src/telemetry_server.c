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
#include <netinet/tcp.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include "telemetry.h"
#include "server_internal.h"

#define MAX_TEL_CLIENTS 32
#define TEL_MAX_EVENTS (MAX_TEL_CLIENTS + 2)

typedef struct {
    int fd;
    uint32_t category;
    uint32_t category_index;
    bool active;
} TelClient;

static TelClient tel_clients[MAX_TEL_CLIENTS];
static pthread_mutex_t tel_clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static int tel_tcp_fd = -1;
static int tel_unix_fd = -1;
static int tel_epoll_fd = -1;
static pthread_t tel_thread;
static bool tel_running = false;

const char* cat_names_diag[] = {
    "SHIPS ALLIANCE", "SHIPS KORTHIAN", "SHIPS XYLARI", "SHIPS SWARM", 
    "SHIPS VESPERIAN", "SHIPS ASCENDANT", "SHIPS QUARZITE", "SHIPS SAURIAN",
    "SHIPS GILDED", "SHIPS FLUIDIC", "SHIPS CRYOS", "SHIPS APEX",
    "STARS", "PLANETS", "BASES", "BLACK HOLES", "NEBULAS", "PULSARS",
    "QUASARS", "COMETS", "ASTEROIDS", "DERELICTS", "MINES", "BUOYS", "PLATFORMS", "RIFTS",
    "MONSTERS", "DYSON", "HUBS", "RELICS", "RUPTURES", "SATELLITES", "STORMS", "TORPEDOES",
    "ARTIFACTS", "WARP GATES", "NEUTRON STARS", "MEGA STRUCTS", "DARK CLOUDS", "SINGULARITIES",
    "PLASMA STORMS", "ORBITAL RINGS", "TIME ANOMALIES", "VOID CRYSTALS", "ANOMALIES",
    "DIFFUSE NEBULAE", "DARK NEBULAE", "PLANETARY NEBULAE", "SNR", "GMC",
    "INTERSTELLAR FILAMENTS", "INTERSTELLAR BUBBLES", "BOK GLOBULES", "CLUMPS AND CORES", "ACCRETION DISKS",
    "RELATIVISTIC JETS", "SHOCK WAVES", "STELLAR BOW SHOCKS", "COSMIC VOIDS", "COSMIC FILAMENTS",
    "EVENT HORIZONS", "KILONOVAE", "GRAVITATIONAL LENSES", "GAMMA-RAY BURSTS", "GRAVITATIONAL WAVES",
    "PROTOPLANETARY DISKS", "DEBRIS DISKS", "PLANETESIMALS", "ROGUE PLANETS", "BROWN DWARFS",
    "INTERSTELLAR OBJECTS", "MAGNETIC RECONNECTION", "CURRENT SHEETS", "HELIOSPHERES", "TERMINATION SHOCKS",
    "MAGNETOSPHERES", "COSMIC STRINGS", "DOMAIN WALLS", "DARK MATTER HALOS", "IGM",
    "CGM", "LYMAN-ALPHA FOREST", "CMB"
};

static pthread_mutex_t cat_mutexes[TEL_CAT_COUNT];

/* Robust read that handles EAGAIN for non-blocking telemetry control socket */
static int robust_read_all(int fd, void* buf, size_t len) {
    size_t total = 0;
    char* p = (char*)buf;
    while (total < len) {
        ssize_t n = recv(fd, p + total, len - total, 0);
        if (n == 0) return 0;
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = { .fd = fd, .events = POLLIN };
                if (poll(&pfd, 1, 100) > 0) continue;
                return -1;
            }
            if (errno == EINTR) continue;
            return -1;
        }
        total += n;
    }
    return (int)total;
}

static int send_all(int fd, const void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t r = send(fd, (const char*)buf + total, len - total, MSG_NOSIGNAL);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Wait up to 100ms for buffer space */
                struct pollfd pfd = { .fd = fd, .events = POLLOUT };
                if (poll(&pfd, 1, 100) > 0) continue;
                return -1; 
            }
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;
        total += r;
    }
    return (int)total;
}

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
                int sndbuf = 4 * 1024 * 1024; /* 4MB buffer for high-volume categories like STARS */
                setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
                ev.events = EPOLLIN;
                ev.data.fd = client_fd;
                epoll_ctl(tel_epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);

                pthread_mutex_lock(&tel_clients_mutex);
                for (int j = 0; j < MAX_TEL_CLIENTS; j++) {
                    if (!tel_clients[j].active) {
                        tel_clients[j].fd = client_fd;
                        tel_clients[j].category = TEL_CAT_SHIP_ALLIANCE;
                        tel_clients[j].active = true;
                        break;
                    }
                }
                pthread_mutex_unlock(&tel_clients_mutex);
            } else {
                TelemetryHeader hdr;
                int r = robust_read_all(fd, &hdr, sizeof(hdr));
                if (r <= 0) {
                    epoll_ctl(tel_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    pthread_mutex_lock(&tel_clients_mutex);
                    for (int j = 0; j < MAX_TEL_CLIENTS; j++) {
                        if (tel_clients[j].active && tel_clients[j].fd == fd) {
                            tel_clients[j].active = false;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&tel_clients_mutex);
                } else if (hdr.type == TEL_PKT_SUBSCRIBE) {
                    TelemetrySubscribe sub;
                    if (robust_read_all(fd, &sub, sizeof(sub)) == sizeof(sub)) {
                        if (sub.category < TEL_CAT_COUNT) {
                            pthread_mutex_lock(&tel_clients_mutex);
                            for (int j = 0; j < MAX_TEL_CLIENTS; j++) {
                                if (tel_clients[j].active && tel_clients[j].fd == fd) {
                                    tel_clients[j].category = sub.category;
                                    break;
                                }
                            }
                            pthread_mutex_unlock(&tel_clients_mutex);
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
    
    for(int i=0; i<TEL_CAT_COUNT; i++) pthread_mutex_init(&cat_mutexes[i], NULL);

    tel_tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (tel_tcp_fd >= 0) {
        int opt = 1;
        setsockopt(tel_tcp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        int one = 1; setsockopt(tel_tcp_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
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
                    case TEL_CAT_DIFFUSE_NEBULA: COUNT_CAT(diffuse_nebulae, MAX_DIFFUSE_NEBULAE); break;
                    case TEL_CAT_DARK_NEBULA: COUNT_CAT(dark_nebulae, MAX_DARK_NEBULAE); break;
                    case TEL_CAT_PLANETARY_NEBULA: COUNT_CAT(planetary_nebulae, MAX_PLANETARY_NEBULAE); break;
                    case TEL_CAT_SNR: COUNT_CAT(snrs, MAX_SNR); break;
                    case TEL_CAT_GMC: COUNT_CAT(gmcs, MAX_GMC); break;
                    case TEL_CAT_INTERSTELLAR_FILAMENT: COUNT_CAT(interstellar_filaments, MAX_INTERSTELLAR_FILAMENTS); break;
                    case TEL_CAT_INTERSTELLAR_BUBBLE: COUNT_CAT(interstellar_bubbles, MAX_INTERSTELLAR_BUBBLES); break;
                    case TEL_CAT_BOK_GLOBULE: COUNT_CAT(bok_globules, MAX_BOK_GLOBULES); break;
                    case TEL_CAT_CLUMP_CORE: COUNT_CAT(clump_cores, MAX_CLUMP_CORES); break;
                    case TEL_CAT_ACCRETION_DISK: COUNT_CAT(accretion_disks, MAX_ACCRETION_DISKS); break;
                    case TEL_CAT_RELATIVISTIC_JET: COUNT_CAT(relativistic_jets, MAX_RELATIVISTIC_JETS); break;
                    case TEL_CAT_SHOCK_WAVE: COUNT_CAT(shock_waves, MAX_SHOCK_WAVES); break;
                    case TEL_CAT_STELLAR_BOW_SHOCK: COUNT_CAT(stellar_bow_shocks, MAX_STELLAR_BOW_SHOCKS); break;
                    case TEL_CAT_COSMIC_VOID: COUNT_CAT(cosmic_voids, MAX_COSMIC_VOIDS); break;
                    case TEL_CAT_COSMIC_FILAMENT: COUNT_CAT(cosmic_filaments, MAX_COSMIC_FILAMENTS); break;
                    case TEL_CAT_EVENT_HORIZON: COUNT_CAT(event_horizons, MAX_EVENT_HORIZONS); break;
                    case TEL_CAT_KILONOVA: COUNT_CAT(kilonovae, MAX_KILONOVAE); break;
                    case TEL_CAT_GRAV_LENS: COUNT_CAT(grav_lenses, MAX_GRAV_LENSES); break;
                    case TEL_CAT_GRB: COUNT_CAT(grbs, MAX_GRB); break;
                    case TEL_CAT_GRAV_WAVE: COUNT_CAT(grav_waves, MAX_GRAV_WAVES); break;
                    case TEL_CAT_PROTOPLANETARY_DISK: COUNT_CAT(protoplanetary_disks, MAX_PROTOPLANETARY_DISKS); break;
                    case TEL_CAT_DEBRIS_DISK: COUNT_CAT(debris_disks, MAX_DEBRIS_DISKS); break;
                    case TEL_CAT_PLANETESIMAL: COUNT_CAT(planetesimals, MAX_PLANETESIMALS); break;
                    case TEL_CAT_ROGUE_PLANET: COUNT_CAT(rogue_planets, MAX_ROGUE_PLANETS); break;
                    case TEL_CAT_BROWN_DWARF: COUNT_CAT(brown_dwarfs, MAX_BROWN_DWARFS); break;
                    case TEL_CAT_ISO: COUNT_CAT(isos, MAX_ISO); break;
                    case TEL_CAT_MAG_RECONN: COUNT_CAT(mag_reconns, MAX_MAG_RECONN); break;
                    case TEL_CAT_CURRENT_SHEET: COUNT_CAT(current_sheets, MAX_CURRENT_SHEETS); break;
                    case TEL_CAT_HELIOSPHERE: COUNT_CAT(heliospheres, MAX_HELIOSPHERES); break;
                    case TEL_CAT_TERM_SHOCK: COUNT_CAT(term_shocks, MAX_TERM_SHOCKS); break;
                    case TEL_CAT_MAGNETOSPHERE: COUNT_CAT(magnetospheres, MAX_MAGNETOSPHERES); break;
                    case TEL_CAT_COSMIC_STRING: COUNT_CAT(cosmic_strings, MAX_COSMIC_STRINGS); break;
                    case TEL_CAT_DOMAIN_WALL: COUNT_CAT(domain_walls, MAX_DOMAIN_WALLS); break;
                    case TEL_CAT_DM_HALO: COUNT_CAT(dm_halos, MAX_DM_HALO); break;
                    case TEL_CAT_IGM: COUNT_CAT(igms, MAX_IGM); break;
                    case TEL_CAT_CGM: COUNT_CAT(cgms, MAX_CGM); break;
                    case TEL_CAT_LYMAN_ALPHA: COUNT_CAT(lyman_alphas, MAX_LYMAN_ALPHA); break;
                    case TEL_CAT_CMB: COUNT_CAT(cmbs, MAX_CMB); break;
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
    strncpy(to->extra, "-", sizeof(to->extra)-1);

    int faction = get_faction_from_cat(cat);
    if (faction != -1) {
        if (idx >= 0 && idx < MAX_CLIENTS) {
            ConnectedPlayer* p = &players[idx];
            snprintf(to->id, sizeof(to->id), "P%02d", idx + 1);
            strncpy(to->name, p->name, sizeof(to->name)-1);
            strncpy(to->info, get_species_name(p->faction), sizeof(to->info)-1);
            to->q1 = p->state.q1; to->q2 = p->state.q2; to->q3 = p->state.q3;
            to->x = p->state.s1; to->y = p->state.s2; to->z = p->state.s3;
            to->integrity = (uint32_t)p->state.hull_integrity;
            to->energy = p->state.energy;
            to->color_pair = get_color_from_faction(p->faction);
        } else if (idx >= MAX_CLIENTS && idx < MAX_CLIENTS + MAX_NPC) {
            int n_idx = idx - MAX_CLIENTS;
            if (n_idx < MAX_NPC) {
                NPCShip* n = &npcs[n_idx];
                snprintf(to->id, sizeof(to->id), "N%04d", n->id);
                strncpy(to->name, n->name, sizeof(to->name)-1);
                strncpy(to->info, get_species_name(n->faction), sizeof(to->info)-1);
                to->q1 = n->q1; to->q2 = n->q2; to->q3 = n->q3;
                to->x = n->x; to->y = n->y; to->z = n->z;
                to->integrity = (uint32_t)n->health;
                to->energy = n->energy;
                to->color_pair = get_color_from_faction(n->faction);
            }
        } else {
            int b_idx = idx - MAX_CLIENTS - MAX_NPC;
            if (b_idx >= 0 && b_idx < MAX_BASES) {
                NPCBase* b = &bases[b_idx];
                snprintf(to->id, sizeof(to->id), "B%04d", b->id);
                snprintf(to->name, sizeof(to->name), "%s Starbase", get_species_name(b->faction));
                strncpy(to->info, "BASE", sizeof(to->info)-1);
                to->q1 = b->q1; to->q2 = b->q2; to->q3 = b->q3;
                to->x = b->x; to->y = b->y; to->z = b->z;
                to->integrity = b->health;
                to->color_pair = get_color_from_faction(b->faction);
            }
        }
    } else {
        switch(cat) {
            case TEL_CAT_STAR: {
                if (idx >= 0 && idx < MAX_STARS) {
                    NPCStar* s = &stars_data[idx];
                    snprintf(to->id, sizeof(to->id), "S%04d", s->id);
                    strncpy(to->name, "Star", sizeof(to->name)-1);
                    snprintf(to->info, sizeof(to->info), "Spectral %d", s->faction);
                    to->q1 = s->q1; to->q2 = s->q2; to->q3 = s->q3;
                    to->x = s->x; to->y = s->y; to->z = s->z;
                    to->color_pair = 4;
                }
            } break;
            case TEL_CAT_PLANET: {
                if (idx >= 0 && idx < MAX_PLANETS) {
                    NPCPlanet* p = &planets[idx];
                    snprintf(to->id, sizeof(to->id), "PL%04d", p->id);
                    strncpy(to->name, "Planet", sizeof(to->name)-1);
                    snprintf(to->info, sizeof(to->info), "Res:%d", p->resource_type);
                    to->q1 = p->q1; to->q2 = p->q2; to->q3 = p->q3;
                    to->x = p->x; to->y = p->y; to->z = p->z;
                    snprintf(to->extra, sizeof(to->extra), "Amt:%d", p->amount);
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_BASE: {
                if (idx >= 0 && idx < MAX_BASES) {
                    NPCBase* b = &bases[idx];
                    snprintf(to->id, sizeof(to->id), "B%04d", b->id);
                    snprintf(to->name, sizeof(to->name), "%s Base", get_species_name(b->faction));
                    snprintf(to->info, sizeof(to->info), "Faction %d", b->faction);
                    to->q1 = b->q1; to->q2 = b->q2; to->q3 = b->q3;
                    to->x = b->x; to->y = b->y; to->z = b->z;
                    to->integrity = b->health;
                    to->color_pair = get_color_from_faction(b->faction);
                }
            } break;
            case TEL_CAT_BH: {
                if (idx >= 0 && idx < MAX_BH) {
                    NPCBlackHole* b = &black_holes[idx];
                    snprintf(to->id, sizeof(to->id), "BH%04d", b->id);
                    strncpy(to->name, "Black Hole", sizeof(to->name)-1);
                    strncpy(to->info, "Singularity", sizeof(to->info)-1);
                    to->q1 = b->q1; to->q2 = b->q2; to->q3 = b->q3;
                    to->x = b->x; to->y = b->y; to->z = b->z;
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_NEBULA: {
                if (idx >= 0 && idx < MAX_NEBULAS) {
                    NPCNebula* n = &nebulas[idx];
                    snprintf(to->id, sizeof(to->id), "NEB%04d", n->id);
                    strncpy(to->name, "Nebula", sizeof(to->name)-1);
                    snprintf(to->info, sizeof(to->info), "Type:%d", n->type);
                    to->q1 = n->q1; to->q2 = n->q2; to->q3 = n->q3;
                    to->x = n->x; to->y = n->y; to->z = n->z;
                    to->color_pair = 13;
                }
            } break;
            case TEL_CAT_PULSAR: {
                if (idx >= 0 && idx < MAX_PULSARS) {
                    NPCPulsar* p = &pulsars[idx];
                    snprintf(to->id, sizeof(to->id), "PUL%04d", p->id);
                    strncpy(to->name, "Pulsar", sizeof(to->name)-1);
                    snprintf(to->info, sizeof(to->info), "Type:%d", p->type);
                    to->q1 = p->q1; to->q2 = p->q2; to->q3 = p->q3;
                    to->x = p->x; to->y = p->y; to->z = p->z;
                    to->color_pair = 4;
                }
            } break;
            case TEL_CAT_QUASAR: {
                if (idx >= 0 && idx < MAX_QUASARS) {
                    NPCQuasar* q = &quasars[idx];
                    snprintf(to->id, sizeof(to->id), "QSR%04d", q->id);
                    strncpy(to->name, "Quasar", sizeof(to->name)-1);
                    snprintf(to->info, sizeof(to->info), "Type:%d", q->type);
                    to->q1 = q->q1; to->q2 = q->q2; to->q3 = q->q3;
                    to->x = q->x; to->y = q->y; to->z = q->z;
                    to->color_pair = 4;
                }
            } break;
            case TEL_CAT_COMET: {
                if (idx >= 0 && idx < MAX_COMETS) {
                    NPCComet* c = &comets[idx];
                    snprintf(to->id, sizeof(to->id), "COM%04d", c->id);
                    strncpy(to->name, "Comet", sizeof(to->name)-1);
                    strncpy(to->info, "Moving", sizeof(to->info)-1);
                    to->q1 = c->q1; to->q2 = c->q2; to->q3 = c->q3;
                    to->x = c->x; to->y = c->y; to->z = c->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_ASTEROID: {
                if (idx >= 0 && idx < MAX_ASTEROIDS) {
                    NPCAsteroid* a = &asteroids[idx];
                    snprintf(to->id, sizeof(to->id), "AST%04d", a->id);
                    strncpy(to->name, "Asteroid", sizeof(to->name)-1);
                    snprintf(to->info, sizeof(to->info), "Res:%d", a->resource_type);
                    to->q1 = a->q1; to->q2 = a->q2; to->q3 = a->q3;
                    to->x = a->x; to->y = a->y; to->z = a->z;
                    snprintf(to->extra, sizeof(to->extra), "Amt:%d", a->amount);
                    to->color_pair = 4;
                }
            } break;
            case TEL_CAT_DERELICT: {
                if (idx >= 0 && idx < MAX_DERELICTS) {
                    NPCDerelict* d = &derelicts[idx];
                    snprintf(to->id, sizeof(to->id), "DE%04d", d->id);
                    strncpy(to->name, d->name, sizeof(to->name)-1);
                    snprintf(to->info, sizeof(to->info), "Class %d", d->ship_class);
                    to->q1 = d->q1; to->q2 = d->q2; to->q3 = d->q3;
                    to->x = d->x; to->y = d->y; to->z = d->z;
                    to->color_pair = 13;
                }
            } break;
            case TEL_CAT_MINE: {
                if (idx >= 0 && idx < MAX_MINES) {
                    NPCMine* m = &mines[idx];
                    snprintf(to->id, sizeof(to->id), "MIN%04d", m->id);
                    strncpy(to->name, "Mine", sizeof(to->name)-1);
                    snprintf(to->info, sizeof(to->info), "Faction %d", m->faction);
                    to->q1 = m->q1; to->q2 = m->q2; to->q3 = m->q3;
                    to->x = m->x; to->y = m->y; to->z = m->z;
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_BUOY: {
                if (idx >= 0 && idx < MAX_BUOYS) {
                    NPCBuoy* b = &buoys[idx];
                    snprintf(to->id, sizeof(to->id), "BUY%04d", b->id);
                    strncpy(to->name, "Comm Buoy", sizeof(to->name)-1);
                    strncpy(to->info, "Active", sizeof(to->info)-1);
                    to->q1 = b->q1; to->q2 = b->q2; to->q3 = b->q3;
                    to->x = b->x; to->y = b->y; to->z = b->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_PLATFORM: {
                if (idx >= 0 && idx < MAX_PLATFORMS) {
                    NPCPlatform* p = &platforms[idx];
                    snprintf(to->id, sizeof(to->id), "PF%04d", p->id);
                    strncpy(to->name, "Platform", sizeof(to->name)-1);
                    snprintf(to->info, sizeof(to->info), "Fac:%d", p->faction);
                    to->q1 = p->q1; to->q2 = p->q2; to->q3 = p->q3;
                    to->x = p->x; to->y = p->y; to->z = p->z;
                    to->integrity = p->health;
                    to->energy = p->energy;
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_RIFT: {
                if (idx >= 0 && idx < MAX_RIFTS) {
                    NPCRift* r = &rifts[idx];
                    snprintf(to->id, sizeof(to->id), "RIF%04d", r->id);
                    strncpy(to->name, "Spatial Rift", sizeof(to->name)-1);
                    strncpy(to->info, "Active", sizeof(to->info)-1);
                    to->q1 = r->q1; to->q2 = r->q2; to->q3 = r->q3;
                    to->x = r->x; to->y = r->y; to->z = r->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_MONSTER: {
                if (idx >= 0 && idx < MAX_MONSTERS) {
                    NPCMonster* m = &monsters[idx];
                    snprintf(to->id, sizeof(to->id), "M%02d", m->id);
                    strncpy(to->name, (m->type == 30) ? "Crystalline" : "Amoeba", sizeof(to->name)-1);
                    strncpy(to->info, "OMEGA", sizeof(to->info)-1);
                    to->q1 = m->q1; to->q2 = m->q2; to->q3 = m->q3;
                    to->x = m->x; to->y = m->y; to->z = m->z;
                    to->integrity = (uint32_t)m->health;
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_DYSON: {
                if (idx >= 0 && idx < MAX_DYSON) {
                    NPCDyson* d = &dysons[idx];
                    snprintf(to->id, sizeof(to->id), "DY%04d", d->id);
                    strncpy(to->name, "Dyson Frag", sizeof(to->name)-1);
                    strncpy(to->info, "Ancient", sizeof(to->info)-1);
                    to->q1 = d->q1; to->q2 = d->q2; to->q3 = d->q3;
                    to->x = d->x; to->y = d->y; to->z = d->z;
                    to->color_pair = 4;
                }
            } break;
            case TEL_CAT_HUB: {
                if (idx >= 0 && idx < MAX_HUBS) {
                    NPCHub* h = &hubs[idx];
                    snprintf(to->id, sizeof(to->id), "HB%04d", h->id);
                    strncpy(to->name, "Trading Hub", sizeof(to->name)-1);
                    strncpy(to->info, "Neutral", sizeof(to->info)-1);
                    to->q1 = h->q1; to->q2 = h->q2; to->q3 = h->q3;
                    to->x = h->x; to->y = h->y; to->z = h->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_RELIC: {
                if (idx >= 0 && idx < MAX_RELICS) {
                    NPCRelic* r = &relics[idx];
                    snprintf(to->id, sizeof(to->id), "RE%04d", r->id);
                    strncpy(to->name, "Ancient Relic", sizeof(to->name)-1);
                    strncpy(to->info, "Tech", sizeof(to->info)-1);
                    to->q1 = r->q1; to->q2 = r->q2; to->q3 = r->q3;
                    to->x = r->x; to->y = r->y; to->z = r->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_RUPTURE: {
                if (idx >= 0 && idx < MAX_RUPTURES) {
                    NPCRupture* r = &ruptures[idx];
                    snprintf(to->id, sizeof(to->id), "RU%04d", r->id);
                    strncpy(to->name, "Subspace Rup", sizeof(to->name)-1);
                    strncpy(to->info, "Anomaly", sizeof(to->info)-1);
                    to->q1 = r->q1; to->q2 = r->q2; to->q3 = r->q3;
                    to->x = r->x; to->y = r->y; to->z = r->z;
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_SATELLITE: {
                if (idx >= 0 && idx < MAX_SATELLITES) {
                    NPCSatellite* s = &satellites[idx];
                    snprintf(to->id, sizeof(to->id), "SA%04d", s->id);
                    strncpy(to->name, "Satellite", sizeof(to->name)-1);
                    strncpy(to->info, "Relay", sizeof(to->info)-1);
                    to->q1 = s->q1; to->q2 = s->q2; to->q3 = s->q3;
                    to->x = s->x; to->y = s->y; to->z = s->z;
                    to->color_pair = 13;
                }
            } break;
            case TEL_CAT_STORM: {
                if (idx >= 0 && idx < MAX_STORMS) {
                    NPCStorm* s = &storms[idx];
                    snprintf(to->id, sizeof(to->id), "SO%04d", s->id);
                    strncpy(to->name, "Ion Storm", sizeof(to->name)-1);
                    strncpy(to->info, "Meteo", sizeof(to->info)-1);
                    to->q1 = s->q1; to->q2 = s->q2; to->q3 = s->q3;
                    to->x = s->x; to->y = s->y; to->z = s->z;
                    to->color_pair = 13;
                }
            } break;
            case TEL_CAT_TORPEDO: {
                if (idx >= 0 && idx < MAX_GLOBAL_TORPEDOES) {
                    PlayerTorpedo* t = &players_torpedoes[idx];
                    snprintf(to->id, sizeof(to->id), "T%04d", t->id);
                    strncpy(to->name, "Torpedo", sizeof(to->name)-1);
                    snprintf(to->info, sizeof(to->info), "Owner %d", t->owner_idx);
                    to->q1 = t->q1; to->q2 = t->q2; to->q3 = t->q3;
                    to->x = t->x; to->y = t->y; to->z = t->z;
                    snprintf(to->extra, sizeof(to->extra), "TO:%d", t->timeout);
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_ARTIFACT: {
                if (idx >= 0 && idx < MAX_ARTIFACTS) {
                    NPCArtifact* a = &artifacts[idx];
                    snprintf(to->id, sizeof(to->id), "AA%04d", a->id);
                    strncpy(to->name, "Alien Artifact", sizeof(to->name)-1);
                    strncpy(to->info, "Exotic", sizeof(to->info)-1);
                    to->q1 = a->q1; to->q2 = a->q2; to->q3 = a->q3;
                    to->x = a->x; to->y = a->y; to->z = a->z;
                    to->color_pair = 4;
                }
            } break;
            case TEL_CAT_WARP_GATE: {
                if (idx >= 0 && idx < MAX_WARP_GATES) {
                    NPCWarpGate* w = &warp_gates[idx];
                    snprintf(to->id, sizeof(to->id), "WG%04d", w->id);
                    strncpy(to->name, "Warp Gate", sizeof(to->name)-1);
                    strncpy(to->info, "Active", sizeof(to->info)-1);
                    to->q1 = w->q1; to->q2 = w->q2; to->q3 = w->q3;
                    to->x = w->x; to->y = w->y; to->z = w->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_NEUTRON_STAR: {
                if (idx >= 0 && idx < MAX_NEUTRON_STARS) {
                    NPCNeutronStar* n = &neutron_stars[idx];
                    snprintf(to->id, sizeof(to->id), "NS%04d", n->id);
                    strncpy(to->name, "Neutron Star", sizeof(to->name)-1);
                    strncpy(to->info, "Degenerate", sizeof(to->info)-1);
                    to->q1 = n->q1; to->q2 = n->q2; to->q3 = n->q3;
                    to->x = n->x; to->y = n->y; to->z = n->z;
                    to->color_pair = 13;
                }
            } break;
            case TEL_CAT_MEGA_STRUCT: {
                if (idx >= 0 && idx < MAX_MEGA_STRUCTS) {
                    NPCMegaStructure* m = &mega_structs[idx];
                    snprintf(to->id, sizeof(to->id), "MS%04d", m->id);
                    strncpy(to->name, "Mega Struct", sizeof(to->name)-1);
                    strncpy(to->info, "Unknown", sizeof(to->info)-1);
                    to->q1 = m->q1; to->q2 = m->q2; to->q3 = m->q3;
                    to->x = m->x; to->y = m->y; to->z = m->z;
                    to->color_pair = 1;
                }
            } break;
            case TEL_CAT_DARK_CLOUD: {
                if (idx >= 0 && idx < MAX_DARK_CLOUDS) {
                    NPCDarkCloud* d = &dark_clouds[idx];
                    snprintf(to->id, sizeof(to->id), "DC%04d", d->id);
                    strncpy(to->name, "Dark Matter", sizeof(to->name)-1);
                    strncpy(to->info, "Obscured", sizeof(to->info)-1);
                    to->q1 = d->q1; to->q2 = d->q2; to->q3 = d->q3;
                    to->x = d->x; to->y = d->y; to->z = d->z;
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_SINGULARITY: {
                if (idx >= 0 && idx < MAX_SINGULARITIES) {
                    NPCSingularity* s = &singularities[idx];
                    snprintf(to->id, sizeof(to->id), "QS%04d", s->id);
                    strncpy(to->name, "Singularity", sizeof(to->name)-1);
                    strncpy(to->info, "Quantum", sizeof(to->info)-1);
                    to->q1 = s->q1; to->q2 = s->q2; to->q3 = s->q3;
                    to->x = s->x; to->y = s->y; to->z = s->z;
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_PLASMA_STORM: {
                if (idx >= 0 && idx < MAX_PLASMA_STORMS) {
                    NPCPlasmaStorm* p = &plasma_storms[idx];
                    snprintf(to->id, sizeof(to->id), "PS%04d", p->id);
                    strncpy(to->name, "Plasma Storm", sizeof(to->name)-1);
                    strncpy(to->info, "Unstable", sizeof(to->info)-1);
                    to->q1 = p->q1; to->q2 = p->q2; to->q3 = p->q3;
                    to->x = p->x; to->y = p->y; to->z = p->z;
                    to->color_pair = 4;
                }
            } break;
            case TEL_CAT_ORBITAL_RING: {
                if (idx >= 0 && idx < MAX_ORBITAL_RINGS) {
                    NPCOrbitalRing* o = &orbital_rings[idx];
                    snprintf(to->id, sizeof(to->id), "OR%04d", o->id);
                    strncpy(to->name, "Orbital Ring", sizeof(to->name)-1);
                    strncpy(to->info, "Planetary", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_TIME_ANOMALY: {
                if (idx >= 0 && idx < MAX_TIME_ANOMALIES) {
                    NPCTimeAnomaly* t = &time_anomalies[idx];
                    snprintf(to->id, sizeof(to->id), "TA%04d", t->id);
                    strncpy(to->name, "Time Anomaly", sizeof(to->name)-1);
                    strncpy(to->info, "Temporal", sizeof(to->info)-1);
                    to->q1 = t->q1; to->q2 = t->q2; to->q3 = t->q3;
                    to->x = t->x; to->y = t->y; to->z = t->z;
                    to->color_pair = 1;
                }
            } break;
            case TEL_CAT_VOID_CRYSTAL: {
                if (idx >= 0 && idx < MAX_VOID_CRYSTALS) {
                    NPCVoidCrystal* v = &void_crystals[idx];
                    snprintf(to->id, sizeof(to->id), "VC%04d", v->id);
                    strncpy(to->name, "Void Crystal", sizeof(to->name)-1);
                    strncpy(to->info, "Crystalline", sizeof(to->info)-1);
                    to->q1 = v->q1; to->q2 = v->q2; to->q3 = v->q3;
                    to->x = v->x; to->y = v->y; to->z = v->z;
                    to->color_pair = 13;
                }
            } break;
            case TEL_CAT_ANOMALY: {
                if (idx >= 0 && idx < MAX_SUBSPACE_ANOMALIES) {
                    NPCSubspaceAnomaly* s = &subspace_anomalies[idx];
                    snprintf(to->id, sizeof(to->id), "AN%04d", s->id);
                    strncpy(to->name, "Subspace Anom", sizeof(to->name)-1);
                    strncpy(to->info, "Unstable", sizeof(to->info)-1);
                    to->q1 = s->q1; to->q2 = s->q2; to->q3 = s->q3;
                    to->x = s->x; to->y = s->y; to->z = s->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_DIFFUSE_NEBULA: {
                if (idx >= 0 && idx < MAX_DIFFUSE_NEBULAE) {
                    NPCDiffuseNebula* o = &diffuse_nebulae[idx];
                    snprintf(to->id, sizeof(to->id), "DF%04d", o->id);
                    strncpy(to->name, "Diff Nebula", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_DARK_NEBULA: {
                if (idx >= 0 && idx < MAX_DARK_NEBULAE) {
                    NPCDarkNebula* o = &dark_nebulae[idx];
                    snprintf(to->id, sizeof(to->id), "DN%04d", o->id);
                    strncpy(to->name, "Dark Nebula", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 4;
                }
            } break;
            case TEL_CAT_PLANETARY_NEBULA: {
                if (idx >= 0 && idx < MAX_PLANETARY_NEBULAE) {
                    NPCPlanetaryNebula* o = &planetary_nebulae[idx];
                    snprintf(to->id, sizeof(to->id), "PN%04d", o->id);
                    strncpy(to->name, "Planet Neb", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 1;
                }
            } break;
            case TEL_CAT_SNR: {
                if (idx >= 0 && idx < MAX_SNR) {
                    NPCSNR* o = &snrs[idx];
                    snprintf(to->id, sizeof(to->id), "SR%04d", o->id);
                    strncpy(to->name, "SNR", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_GMC: {
                if (idx >= 0 && idx < MAX_GMC) {
                    NPCGMC* o = &gmcs[idx];
                    snprintf(to->id, sizeof(to->id), "GM%04d", o->id);
                    strncpy(to->name, "GMC", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_INTERSTELLAR_FILAMENT: {
                if (idx >= 0 && idx < MAX_INTERSTELLAR_FILAMENTS) {
                    NPCInterstellarFilament* o = &interstellar_filaments[idx];
                    snprintf(to->id, sizeof(to->id), "IF%04d", o->id);
                    strncpy(to->name, "Int Filament", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 4;
                }
            } break;
            case TEL_CAT_INTERSTELLAR_BUBBLE: {
                if (idx >= 0 && idx < MAX_INTERSTELLAR_BUBBLES) {
                    NPCInterstellarBubble* o = &interstellar_bubbles[idx];
                    snprintf(to->id, sizeof(to->id), "IB%04d", o->id);
                    strncpy(to->name, "Int Bubble", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 1;
                }
            } break;
            case TEL_CAT_BOK_GLOBULE: {
                if (idx >= 0 && idx < MAX_BOK_GLOBULES) {
                    NPCBokGlobule* o = &bok_globules[idx];
                    snprintf(to->id, sizeof(to->id), "BG%04d", o->id);
                    strncpy(to->name, "Bok Globule", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_CLUMP_CORE: {
                if (idx >= 0 && idx < MAX_CLUMP_CORES) {
                    NPCClumpCore* o = &clump_cores[idx];
                    snprintf(to->id, sizeof(to->id), "CC%04d", o->id);
                    strncpy(to->name, "Clump/Core", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_ACCRETION_DISK: {
                if (idx >= 0 && idx < MAX_ACCRETION_DISKS) {
                    NPCAccretionDisk* o = &accretion_disks[idx];
                    snprintf(to->id, sizeof(to->id), "AD%04d", o->id);
                    strncpy(to->name, "Accret Disk", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 4;
                }
            } break;
            case TEL_CAT_RELATIVISTIC_JET: {
                if (idx >= 0 && idx < MAX_RELATIVISTIC_JETS) {
                    NPCRelativisticJet* o = &relativistic_jets[idx];
                    snprintf(to->id, sizeof(to->id), "RJ%04d", o->id);
                    strncpy(to->name, "Relativ Jet", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 1;
                }
            } break;
            case TEL_CAT_SHOCK_WAVE: {
                if (idx >= 0 && idx < MAX_SHOCK_WAVES) {
                    NPCShockWave* o = &shock_waves[idx];
                    snprintf(to->id, sizeof(to->id), "SW%04d", o->id);
                    strncpy(to->name, "Shock Wave", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_STELLAR_BOW_SHOCK: {
                if (idx >= 0 && idx < MAX_STELLAR_BOW_SHOCKS) {
                    NPCStellarBowShock* o = &stellar_bow_shocks[idx];
                    snprintf(to->id, sizeof(to->id), "BS%04d", o->id);
                    strncpy(to->name, "Bow Shock", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_COSMIC_VOID: {
                if (idx >= 0 && idx < MAX_COSMIC_VOIDS) {
                    NPCCosmicVoid* o = &cosmic_voids[idx];
                    snprintf(to->id, sizeof(to->id), "CV%04d", o->id);
                    strncpy(to->name, "Cosmic Void", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 4;
                }
            } break;
            case TEL_CAT_COSMIC_FILAMENT: {
                if (idx >= 0 && idx < MAX_COSMIC_FILAMENTS) {
                    NPCCosmicFilament* o = &cosmic_filaments[idx];
                    snprintf(to->id, sizeof(to->id), "CF%04d", o->id);
                    strncpy(to->name, "Cosmic Fil", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 1;
                }
            } break;
            case TEL_CAT_EVENT_HORIZON: {
                if (idx >= 0 && idx < MAX_EVENT_HORIZONS) {
                    NPCEventHorizon* o = &event_horizons[idx];
                    snprintf(to->id, sizeof(to->id), "EH%04d", o->id);
                    strncpy(to->name, "Event Horiz", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_KILONOVA: {
                if (idx >= 0 && idx < MAX_KILONOVAE) {
                    NPCKilonova* o = &kilonovae[idx];
                    snprintf(to->id, sizeof(to->id), "KN%04d", o->id);
                    strncpy(to->name, "Kilonova", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_GRAV_LENS: {
                if (idx >= 0 && idx < MAX_GRAV_LENSES) {
                    NPCGravLens* o = &grav_lenses[idx];
                    snprintf(to->id, sizeof(to->id), "GL%04d", o->id);
                    strncpy(to->name, "Grav Lens", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 4;
                }
            } break;
            case TEL_CAT_GRB: {
                if (idx >= 0 && idx < MAX_GRB) {
                    NPCGRB* o = &grbs[idx];
                    snprintf(to->id, sizeof(to->id), "GB%04d", o->id);
                    strncpy(to->name, "GRB", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 1;
                }
            } break;
            case TEL_CAT_GRAV_WAVE: {
                if (idx >= 0 && idx < MAX_GRAV_WAVES) {
                    NPCGravWave* o = &grav_waves[idx];
                    snprintf(to->id, sizeof(to->id), "GW%04d", o->id);
                    strncpy(to->name, "Grav Wave", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_PROTOPLANETARY_DISK: {
                if (idx >= 0 && idx < MAX_PROTOPLANETARY_DISKS) {
                    NPCProtoplanetaryDisk* o = &protoplanetary_disks[idx];
                    snprintf(to->id, sizeof(to->id), "PD%04d", o->id);
                    strncpy(to->name, "Protoplanet", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_DEBRIS_DISK: {
                if (idx >= 0 && idx < MAX_DEBRIS_DISKS) {
                    NPCDebrisDisk* o = &debris_disks[idx];
                    snprintf(to->id, sizeof(to->id), "DD%04d", o->id);
                    strncpy(to->name, "Debris Disk", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 4;
                }
            } break;
            case TEL_CAT_PLANETESIMAL: {
                if (idx >= 0 && idx < MAX_PLANETESIMALS) {
                    NPCPlanetesimal* o = &planetesimals[idx];
                    snprintf(to->id, sizeof(to->id), "PT%04d", o->id);
                    strncpy(to->name, "Planetesimal", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 1;
                }
            } break;
            case TEL_CAT_ROGUE_PLANET: {
                if (idx >= 0 && idx < MAX_ROGUE_PLANETS) {
                    NPCRoguePlanet* o = &rogue_planets[idx];
                    snprintf(to->id, sizeof(to->id), "RP%04d", o->id);
                    strncpy(to->name, "Rogue Planet", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_BROWN_DWARF: {
                if (idx >= 0 && idx < MAX_BROWN_DWARFS) {
                    NPCBrownDwarf* o = &brown_dwarfs[idx];
                    snprintf(to->id, sizeof(to->id), "BD%04d", o->id);
                    strncpy(to->name, "Brown Dwarf", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_ISO: {
                if (idx >= 0 && idx < MAX_ISO) {
                    NPCISO* o = &isos[idx];
                    snprintf(to->id, sizeof(to->id), "IS%04d", o->id);
                    strncpy(to->name, "Intst Obj", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 4;
                }
            } break;
            case TEL_CAT_MAG_RECONN: {
                if (idx >= 0 && idx < MAX_MAG_RECONN) {
                    NPCMagReconn* o = &mag_reconns[idx];
                    snprintf(to->id, sizeof(to->id), "MR%04d", o->id);
                    strncpy(to->name, "Mag Reconn", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 1;
                }
            } break;
            case TEL_CAT_CURRENT_SHEET: {
                if (idx >= 0 && idx < MAX_CURRENT_SHEETS) {
                    NPCCurrentSheet* o = &current_sheets[idx];
                    snprintf(to->id, sizeof(to->id), "CS%04d", o->id);
                    strncpy(to->name, "Curr Sheet", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_HELIOSPHERE: {
                if (idx >= 0 && idx < MAX_HELIOSPHERES) {
                    NPCHeliosphere* o = &heliospheres[idx];
                    snprintf(to->id, sizeof(to->id), "HS%04d", o->id);
                    strncpy(to->name, "Heliosphere", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_TERM_SHOCK: {
                if (idx >= 0 && idx < MAX_TERM_SHOCKS) {
                    NPCTermShock* o = &term_shocks[idx];
                    snprintf(to->id, sizeof(to->id), "TS%04d", o->id);
                    strncpy(to->name, "Term Shock", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 4;
                }
            } break;
            case TEL_CAT_MAGNETOSPHERE: {
                if (idx >= 0 && idx < MAX_MAGNETOSPHERES) {
                    NPCMagnetosphere* o = &magnetospheres[idx];
                    snprintf(to->id, sizeof(to->id), "MS%04d", o->id);
                    strncpy(to->name, "Magnetosph", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 1;
                }
            } break;
            case TEL_CAT_COSMIC_STRING: {
                if (idx >= 0 && idx < MAX_COSMIC_STRINGS) {
                    NPCCosmicString* o = &cosmic_strings[idx];
                    snprintf(to->id, sizeof(to->id), "CX%04d", o->id);
                    strncpy(to->name, "Cosm String", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_DOMAIN_WALL: {
                if (idx >= 0 && idx < MAX_DOMAIN_WALLS) {
                    NPCDomainWall* o = &domain_walls[idx];
                    snprintf(to->id, sizeof(to->id), "DW%04d", o->id);
                    strncpy(to->name, "Domain Wall", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_DM_HALO: {
                if (idx >= 0 && idx < MAX_DM_HALO) {
                    NPCDMHalo* o = &dm_halos[idx];
                    snprintf(to->id, sizeof(to->id), "DH%04d", o->id);
                    strncpy(to->name, "DM Halo", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 4;
                }
            } break;
            case TEL_CAT_IGM: {
                if (idx >= 0 && idx < MAX_IGM) {
                    NPCIGM* o = &igms[idx];
                    snprintf(to->id, sizeof(to->id), "IG%04d", o->id);
                    strncpy(to->name, "IGM", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 1;
                }
            } break;
            case TEL_CAT_CGM: {
                if (idx >= 0 && idx < MAX_CGM) {
                    NPCCGM* o = &cgms[idx];
                    snprintf(to->id, sizeof(to->id), "CG%04d", o->id);
                    strncpy(to->name, "CGM", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 2;
                }
            } break;
            case TEL_CAT_LYMAN_ALPHA: {
                if (idx >= 0 && idx < MAX_LYMAN_ALPHA) {
                    NPCLymanAlpha* o = &lyman_alphas[idx];
                    snprintf(to->id, sizeof(to->id), "LA%04d", o->id);
                    strncpy(to->name, "Lyman Alpha", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 3;
                }
            } break;
            case TEL_CAT_CMB: {
                if (idx >= 0 && idx < MAX_CMB) {
                    NPCCMB* o = &cmbs[idx];
                    snprintf(to->id, sizeof(to->id), "CB%04d", o->id);
                    strncpy(to->name, "CMB", sizeof(to->name)-1);
                    strncpy(to->info, "Cosmic", sizeof(to->info)-1);
                    to->q1 = o->q1; to->q2 = o->q2; to->q3 = o->q3;
                    to->x = o->x; to->y = o->y; to->z = o->z;
                    to->color_pair = 4;
                }
            } break;
            default: break;
        }
    }
    
    /* Ensure all strings are null-terminated */
    to->id[sizeof(to->id)-1] = '\0';
    to->name[sizeof(to->name)-1] = '\0';
    to->info[sizeof(to->info)-1] = '\0';
    to->extra[sizeof(to->extra)-1] = '\0';
}

static TelemetryObject cat_cache[TEL_CAT_COUNT][MAX_VISIBLE_TEL];
static int cat_cache_count[TEL_CAT_COUNT];
static uint32_t cat_cache_tick[TEL_CAT_COUNT];

static void update_category_cache(int cat) {
    if (cat < 0 || cat >= TEL_CAT_COUNT) return;
    if (cat_cache_tick[cat] == (uint32_t)global_tick) return;

    pthread_mutex_lock(&cat_mutexes[cat]);
    int count = 0;
    int faction = get_faction_from_cat(cat);
    
    if (faction != -1) {
        for (int j = 0; j < MAX_CLIENTS && count < MAX_VISIBLE_TEL; j++) {
            if (players[j].active && players[j].faction == faction) fill_obj(&cat_cache[cat][count++], cat, j);
        }
        for (int j = 0; j < MAX_NPC && count < MAX_VISIBLE_TEL; j++) {
            if (npcs[j].active && npcs[j].faction == faction) fill_obj(&cat_cache[cat][count++], cat, j + MAX_CLIENTS);
        }
        for (int j = 0; j < MAX_BASES && count < MAX_VISIBLE_TEL; j++) {
            if (bases[j].active && bases[j].faction == faction) fill_obj(&cat_cache[cat][count++], cat, j + MAX_CLIENTS + MAX_NPC);
        }
    } else {
        #define FILL_CACHE_CAT(arr, max) for(int j=0; j<max && count<MAX_VISIBLE_TEL; j++) if(arr[j].active) fill_obj(&cat_cache[cat][count++], cat, j)
        switch(cat) {
            case TEL_CAT_STAR:      FILL_CACHE_CAT(stars_data, MAX_STARS); break;
            case TEL_CAT_PLANET:    FILL_CACHE_CAT(planets, MAX_PLANETS); break;
            case TEL_CAT_BASE:      FILL_CACHE_CAT(bases, MAX_BASES); break;
            case TEL_CAT_BH:        FILL_CACHE_CAT(black_holes, MAX_BH); break;
            case TEL_CAT_NEBULA:    FILL_CACHE_CAT(nebulas, MAX_NEBULAS); break;
            case TEL_CAT_PULSAR:    FILL_CACHE_CAT(pulsars, MAX_PULSARS); break;
            case TEL_CAT_QUASAR:    FILL_CACHE_CAT(quasars, MAX_QUASARS); break;
            case TEL_CAT_COMET:     FILL_CACHE_CAT(comets, MAX_COMETS); break;
            case TEL_CAT_ASTEROID:  FILL_CACHE_CAT(asteroids, MAX_ASTEROIDS); break;
            case TEL_CAT_DERELICT:  FILL_CACHE_CAT(derelicts, MAX_DERELICTS); break;
            case TEL_CAT_MINE:      FILL_CACHE_CAT(mines, MAX_MINES); break;
            case TEL_CAT_BUOY:      FILL_CACHE_CAT(buoys, MAX_BUOYS); break;
            case TEL_CAT_PLATFORM:  FILL_CACHE_CAT(platforms, MAX_PLATFORMS); break;
            case TEL_CAT_RIFT:      FILL_CACHE_CAT(rifts, MAX_RIFTS); break;
            case TEL_CAT_MONSTER:   FILL_CACHE_CAT(monsters, MAX_MONSTERS); break;
            case TEL_CAT_DYSON:     FILL_CACHE_CAT(dysons, MAX_DYSON); break;
            case TEL_CAT_HUB:       FILL_CACHE_CAT(hubs, MAX_HUBS); break;
            case TEL_CAT_RELIC:     FILL_CACHE_CAT(relics, MAX_RELICS); break;
            case TEL_CAT_RUPTURE:   FILL_CACHE_CAT(ruptures, MAX_RUPTURES); break;
            case TEL_CAT_SATELLITE: FILL_CACHE_CAT(satellites, MAX_SATELLITES); break;
            case TEL_CAT_STORM:     FILL_CACHE_CAT(storms, MAX_STORMS); break;
            case TEL_CAT_TORPEDO:   FILL_CACHE_CAT(players_torpedoes, MAX_GLOBAL_TORPEDOES); break;
            case TEL_CAT_ARTIFACT:  FILL_CACHE_CAT(artifacts, MAX_ARTIFACTS); break;
            case TEL_CAT_WARP_GATE: FILL_CACHE_CAT(warp_gates, MAX_WARP_GATES); break;
            case TEL_CAT_NEUTRON_STAR: FILL_CACHE_CAT(neutron_stars, MAX_NEUTRON_STARS); break;
            case TEL_CAT_MEGA_STRUCT: FILL_CACHE_CAT(mega_structs, MAX_MEGA_STRUCTS); break;
            case TEL_CAT_DARK_CLOUD: FILL_CACHE_CAT(dark_clouds, MAX_DARK_CLOUDS); break;
            case TEL_CAT_SINGULARITY: FILL_CACHE_CAT(singularities, MAX_SINGULARITIES); break;
            case TEL_CAT_PLASMA_STORM: FILL_CACHE_CAT(plasma_storms, MAX_PLASMA_STORMS); break;
            case TEL_CAT_ORBITAL_RING: FILL_CACHE_CAT(orbital_rings, MAX_ORBITAL_RINGS); break;
            case TEL_CAT_TIME_ANOMALY: FILL_CACHE_CAT(time_anomalies, MAX_TIME_ANOMALIES); break;
            case TEL_CAT_VOID_CRYSTAL: FILL_CACHE_CAT(void_crystals, MAX_VOID_CRYSTALS); break;
            case TEL_CAT_ANOMALY:   FILL_CACHE_CAT(subspace_anomalies, MAX_SUBSPACE_ANOMALIES); break;
            case TEL_CAT_DIFFUSE_NEBULA: FILL_CACHE_CAT(diffuse_nebulae, MAX_DIFFUSE_NEBULAE); break;
            case TEL_CAT_DARK_NEBULA: FILL_CACHE_CAT(dark_nebulae, MAX_DARK_NEBULAE); break;
            case TEL_CAT_PLANETARY_NEBULA: FILL_CACHE_CAT(planetary_nebulae, MAX_PLANETARY_NEBULAE); break;
            case TEL_CAT_SNR: FILL_CACHE_CAT(snrs, MAX_SNR); break;
            case TEL_CAT_GMC: FILL_CACHE_CAT(gmcs, MAX_GMC); break;
            case TEL_CAT_INTERSTELLAR_FILAMENT: FILL_CACHE_CAT(interstellar_filaments, MAX_INTERSTELLAR_FILAMENTS); break;
            case TEL_CAT_INTERSTELLAR_BUBBLE: FILL_CACHE_CAT(interstellar_bubbles, MAX_INTERSTELLAR_BUBBLES); break;
            case TEL_CAT_BOK_GLOBULE: FILL_CACHE_CAT(bok_globules, MAX_BOK_GLOBULES); break;
            case TEL_CAT_CLUMP_CORE: FILL_CACHE_CAT(clump_cores, MAX_CLUMP_CORES); break;
            case TEL_CAT_ACCRETION_DISK: FILL_CACHE_CAT(accretion_disks, MAX_ACCRETION_DISKS); break;
            case TEL_CAT_RELATIVISTIC_JET: FILL_CACHE_CAT(relativistic_jets, MAX_RELATIVISTIC_JETS); break;
            case TEL_CAT_SHOCK_WAVE: FILL_CACHE_CAT(shock_waves, MAX_SHOCK_WAVES); break;
            case TEL_CAT_STELLAR_BOW_SHOCK: FILL_CACHE_CAT(stellar_bow_shocks, MAX_STELLAR_BOW_SHOCKS); break;
            case TEL_CAT_COSMIC_VOID: FILL_CACHE_CAT(cosmic_voids, MAX_COSMIC_VOIDS); break;
            case TEL_CAT_COSMIC_FILAMENT: FILL_CACHE_CAT(cosmic_filaments, MAX_COSMIC_FILAMENTS); break;
            case TEL_CAT_EVENT_HORIZON: FILL_CACHE_CAT(event_horizons, MAX_EVENT_HORIZONS); break;
            case TEL_CAT_KILONOVA: FILL_CACHE_CAT(kilonovae, MAX_KILONOVAE); break;
            case TEL_CAT_GRAV_LENS: FILL_CACHE_CAT(grav_lenses, MAX_GRAV_LENSES); break;
            case TEL_CAT_GRB: FILL_CACHE_CAT(grbs, MAX_GRB); break;
            case TEL_CAT_GRAV_WAVE: FILL_CACHE_CAT(grav_waves, MAX_GRAV_WAVES); break;
            case TEL_CAT_PROTOPLANETARY_DISK: FILL_CACHE_CAT(protoplanetary_disks, MAX_PROTOPLANETARY_DISKS); break;
            case TEL_CAT_DEBRIS_DISK: FILL_CACHE_CAT(debris_disks, MAX_DEBRIS_DISKS); break;
            case TEL_CAT_PLANETESIMAL: FILL_CACHE_CAT(planetesimals, MAX_PLANETESIMALS); break;
            case TEL_CAT_ROGUE_PLANET: FILL_CACHE_CAT(rogue_planets, MAX_ROGUE_PLANETS); break;
            case TEL_CAT_BROWN_DWARF: FILL_CACHE_CAT(brown_dwarfs, MAX_BROWN_DWARFS); break;
            case TEL_CAT_ISO: FILL_CACHE_CAT(isos, MAX_ISO); break;
            case TEL_CAT_MAG_RECONN: FILL_CACHE_CAT(mag_reconns, MAX_MAG_RECONN); break;
            case TEL_CAT_CURRENT_SHEET: FILL_CACHE_CAT(current_sheets, MAX_CURRENT_SHEETS); break;
            case TEL_CAT_HELIOSPHERE: FILL_CACHE_CAT(heliospheres, MAX_HELIOSPHERES); break;
            case TEL_CAT_TERM_SHOCK: FILL_CACHE_CAT(term_shocks, MAX_TERM_SHOCKS); break;
            case TEL_CAT_MAGNETOSPHERE: FILL_CACHE_CAT(magnetospheres, MAX_MAGNETOSPHERES); break;
            case TEL_CAT_COSMIC_STRING: FILL_CACHE_CAT(cosmic_strings, MAX_COSMIC_STRINGS); break;
            case TEL_CAT_DOMAIN_WALL: FILL_CACHE_CAT(domain_walls, MAX_DOMAIN_WALLS); break;
            case TEL_CAT_DM_HALO: FILL_CACHE_CAT(dm_halos, MAX_DM_HALO); break;
            case TEL_CAT_IGM: FILL_CACHE_CAT(igms, MAX_IGM); break;
            case TEL_CAT_CGM: FILL_CACHE_CAT(cgms, MAX_CGM); break;
            case TEL_CAT_LYMAN_ALPHA: FILL_CACHE_CAT(lyman_alphas, MAX_LYMAN_ALPHA); break;
            case TEL_CAT_CMB: FILL_CACHE_CAT(cmbs, MAX_CMB); break;
        }
    }
    cat_cache_count[cat] = count;
    cat_cache_tick[cat] = (uint32_t)global_tick;
    pthread_mutex_unlock(&cat_mutexes[cat]);
}

static void disconnect_client(int i) {
    pthread_mutex_lock(&tel_clients_mutex);
    if (tel_clients[i].active) {
        epoll_ctl(tel_epoll_fd, EPOLL_CTL_DEL, tel_clients[i].fd, NULL);
        close(tel_clients[i].fd);
        tel_clients[i].active = false;
        printf("\033[1;31m[TELEMETRY]\033[0m Client %d dropped due to transmission failure.\n", i);
    }
    pthread_mutex_unlock(&tel_clients_mutex);
}

void telemetry_sync_state() {
    if (!tel_running) return;
    
    /* Identify which categories need caching this tick */
    bool needs_update[TEL_CAT_COUNT] = {false};
    pthread_mutex_lock(&tel_clients_mutex);
    for (int i = 0; i < MAX_TEL_CLIENTS; i++) {
        if (tel_clients[i].active && tel_clients[i].category < TEL_CAT_COUNT) {
            needs_update[tel_clients[i].category] = true;
        }
    }
    pthread_mutex_unlock(&tel_clients_mutex);

    /* Update caches for active categories (Must be called while game_mutex is held) */
    for (int c = 0; c < TEL_CAT_COUNT; c++) {
        if (needs_update[c]) {
            update_category_cache(c);
        }
    }
}

void telemetry_broadcast() {
    if (!tel_running) return;

    /* Prepare global stats (Snapshot taken from global variables) */
    TelemetryHeader shdr = {TEL_PKT_STATS, sizeof(TelemetryStats)};
    TelemetryStats stats = {0};
    stats.tick = global_tick;
    /* We read these without game lock, but they are simple ints, safe enough for telemetry */
    for(int j=0; j<MAX_CLIENTS; j++) if(players[j].active) stats.active_players++;
    for(int j=0; j<MAX_NPC; j++) if(npcs[j].active) stats.active_npcs++;

    for (int i = 0; i < MAX_TEL_CLIENTS; i++) {
        pthread_mutex_lock(&tel_clients_mutex);
        if (!tel_clients[i].active) {
            pthread_mutex_unlock(&tel_clients_mutex);
            continue;
        }

        int fd = tel_clients[i].fd;
        uint32_t cat = tel_clients[i].category;
        pthread_mutex_unlock(&tel_clients_mutex);

        if (cat >= TEL_CAT_COUNT) { disconnect_client(i); continue; }

        /* Check if socket is ready to receive data (don't block the server too long) */
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        if (poll(&pfd, 1, 0) <= 0) {
            /* Socket buffer is likely full, skip this tick for this client */
            continue;
        }

        /* Use cached data (Already updated by telemetry_sync_state) */
        pthread_mutex_lock(&cat_mutexes[cat]);
        int count = cat_cache_count[cat];
        bool failed = false;

        /* Send data chunk */
        if (count == 0) {
            TelemetryHeader dhdr = {TEL_PKT_DATA, 0};
            if (send_all(fd, &dhdr, sizeof(dhdr)) != sizeof(dhdr)) failed = true;
        } else {
            int chunk_size = 100;
            for (int offset = 0; offset < count; offset += chunk_size) {
                int current_chunk = (offset + chunk_size > count) ? (count - offset) : chunk_size;
                TelemetryHeader dhdr;
                dhdr.type = TEL_PKT_DATA;
                dhdr.length = (uint32_t)(current_chunk * sizeof(TelemetryObject));
                
                if (send_all(fd, &dhdr, sizeof(dhdr)) == sizeof(dhdr)) {
                    if (send_all(fd, &cat_cache[cat][offset], dhdr.length) != (int)dhdr.length) {
                        failed = true; break;
                    }
                } else {
                    failed = true; break;
                }
            }
        }
        pthread_mutex_unlock(&cat_mutexes[cat]);

        if (!failed) {
            if (send_all(fd, &shdr, sizeof(shdr)) == sizeof(shdr)) {
                if (send_all(fd, &stats, sizeof(stats)) != sizeof(stats)) failed = true;
            } else failed = true;
        }

        if (failed) {
            disconnect_client(i);
        }
    }
}
