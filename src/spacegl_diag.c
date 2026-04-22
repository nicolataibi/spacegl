/*
 * SPACE GL - TACTICAL REAL-TIME SCANNER (PIE-READY)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <ncurses.h>
#include <elf.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include "game_config.h"
#include "shared_state.h"
#include "network.h"
#include "server_internal.h"

#define MAX_PAGES 50
#define sym_type(i) ((i) & 0xf)

typedef enum {
    CAT_MENU = 0, CAT_SHIP, CAT_STAR, CAT_PLANET, CAT_BASE, CAT_BH, CAT_NEBULA, CAT_PULSAR, CAT_QUASAR,
    CAT_COMET, CAT_ASTEROID, CAT_DERELICT, CAT_MINE, CAT_BUOY, CAT_PLATFORM, CAT_RIFT, CAT_MONSTER,
    CAT_DYSON, CAT_HUB, CAT_RELIC, CAT_RUPTURE, CAT_SATELLITE, CAT_STORM, CAT_TORPEDO,
    CAT_ARTIFACT, CAT_WARP_GATE, CAT_NEUTRON_STAR, CAT_MEGA_STRUCT, CAT_DARK_CLOUD,
    CAT_SINGULARITY, CAT_PLASMA_STORM, CAT_ORBITAL_RING, CAT_TIME_ANOMALY, CAT_VOID_CRYSTAL,
    CAT_ANOMALY
} DiagCat;

typedef struct {
    DiagCat cat;
    int faction_filter;
    const char* name;
} DiagPage;

DiagPage pages[MAX_PAGES] = {
    {CAT_MENU, -1, "MAIN NAVIGATION MENU"},
    {CAT_SHIP, -1, "GALAXY SUMMARY (ALL SHIPS)"},
    {CAT_STAR, 0, "STELLAR BODIES (STARS)"},
    {CAT_PLANET, 0, "PLANETARY BODIES"},
    {CAT_BASE, 0, "STARBASES (OUTPOSTS)"},
    {CAT_BH, 0, "SINGULARITIES (BLACK HOLES)"},
    {CAT_NEBULA, 0, "NEBULAR CLOUDS"},
    {CAT_PULSAR, 0, "STREAMS (PULSARS)"},
    {CAT_QUASAR, 0, "QUASARS (RADIO SOURCES)"},
    {CAT_COMET, 0, "COMETARY OBJECTS"},
    {CAT_ASTEROID, 0, "ASTEROID BELTS"},
    {CAT_DERELICT, 0, "SPACE HULKS (DERELICTS)"},
    {CAT_MONSTER, 0, "BIOLOGICAL THREATS"},
    {CAT_DYSON, 0, "DYSON FRAGMENTS"},
    {CAT_HUB, 0, "TRADING HUBS"},
    {CAT_RELIC, 0, "ANCIENT RELICS"},
    {CAT_RUPTURE, 0, "SUBSPACE RUPTURES"},
    {CAT_SATELLITE, 0, "ORBITAL SATELLITES"},
    {CAT_STORM, 0, "IONIC STORMS"},
    {CAT_MINE, 0, "TACTICAL MINES"},
    {CAT_BUOY, 0, "COMMUNICATION BUOYS"},
    {CAT_PLATFORM, 0, "DEFENSE PLATFORMS"},
    {CAT_RIFT, 0, "SPATIAL RIFTS"},
    {CAT_TORPEDO, 0, "ACTIVE TORPEDOES"},
    {CAT_ARTIFACT, 0, "ALIEN ARTIFACTS"},
    {CAT_WARP_GATE, 0, "WARP GATES (STAGATE)"},
    {CAT_NEUTRON_STAR, 0, "NEUTRON STARS"},
    {CAT_MEGA_STRUCT, 0, "MEGA STRUCTURES"},
    {CAT_DARK_CLOUD, 0, "DARK MATTER CLOUDS"},
    {CAT_SINGULARITY, 0, "QUANTUM SINGULARITIES"},
    {CAT_PLASMA_STORM, 0, "PLASMA STORMS"},
    {CAT_ORBITAL_RING, 0, "ORBITAL RINGS"},
    {CAT_TIME_ANOMALY, 0, "TIME ANOMALIES"},
    {CAT_VOID_CRYSTAL, 0, "VOID CRYSTALS"},
    {CAT_ANOMALY, 0, "SUBSPACE ANOMALIES"},
    {CAT_SHIP, FACTION_ALLIANCE, "ALLIANCE (PLAYER)"},
    {CAT_SHIP, FACTION_KORTHIAN, "KORTHIAN EMPIRE"},
    {CAT_SHIP, FACTION_XYLARI,   "XYLARI HEGEMONY"},
    {CAT_SHIP, FACTION_SWARM,    "THE SWARM"},
    {CAT_SHIP, FACTION_VESPERIAN, "VESPERIAN CONCLAVE"},
    {CAT_SHIP, FACTION_JEM_HADAR, "ASCENDANT DOMINION"},
    {CAT_SHIP, FACTION_THOLIAN,   "QUARZITE ASSEMBLY"},
    {CAT_SHIP, FACTION_GORN,      "SAURIAN HEGEMONY"},
    {CAT_SHIP, FACTION_GILDED,    "GILDED FEDERATION"},
    {CAT_SHIP, FACTION_SPECIES_8472, "FLUIDIC SPECIES"},
    {CAT_SHIP, FACTION_BREEN,     "CRYOS CONFEDERACY"},
    {CAT_SHIP, FACTION_HIROGEN,   "APEX HUNTERS"},
};

typedef struct {
    int type; /* 0=Player, 1=NPC */
    int index; /* Original array index */
} RenderItem;

const char* get_species_name(int s) {
    switch(s) {
        case FACTION_ALLIANCE: return "Alliance"; 
        case FACTION_KORTHIAN: return "Korthian"; 
        case FACTION_XYLARI: return "Xylari"; 
        case FACTION_SWARM: return "Swarm";
        case FACTION_VESPERIAN: return "Vesperian"; 
        case FACTION_JEM_HADAR: return "Ascendant"; 
        case FACTION_THOLIAN: return "Quarzite";
        case FACTION_GORN: return "Saurian"; 
        case FACTION_GILDED: return "Gilded"; 
        case FACTION_SPECIES_8472: return "Fluidic Void";
        case FACTION_BREEN: return "Cryos"; 
        case FACTION_HIROGEN: return "Apex";
        default: return "Unknown";
    }
}

ssize_t read_remote(pid_t pid, uintptr_t addr, void *buf, size_t size) {
    if (!addr) return -1;
    struct iovec local = {buf, size}, remote = {(void*)addr, size};
    return process_vm_readv(pid, &local, 1, &remote, 1, 0);
}

uintptr_t get_symbol_offset(const char* exe_path, const char* name, int* is_pie) {
    FILE* f = fopen(exe_path, "rb");
    if (!f) return 0;
    Elf64_Ehdr ehdr;
    if (fread(&ehdr, 1, sizeof(ehdr), f) != sizeof(ehdr)) { fclose(f); return 0; }
    
    if (is_pie) *is_pie = (ehdr.e_type == ET_DYN);

    Elf64_Shdr* shdrs = malloc(ehdr.e_shentsize * ehdr.e_shnum);
    fseek(f, ehdr.e_shoff, SEEK_SET);
    if (fread(shdrs, ehdr.e_shentsize, ehdr.e_shnum, f) != ehdr.e_shnum) {
        free(shdrs); fclose(f); return 0;
    }
    
    uintptr_t offset = 0;
    for (int i = 0; i < ehdr.e_shnum; i++) {
        if (shdrs[i].sh_type == SHT_SYMTAB || shdrs[i].sh_type == SHT_DYNSYM) {
            Elf64_Sym* syms = malloc(shdrs[i].sh_size);
            fseek(f, shdrs[i].sh_offset, SEEK_SET);
            if (fread(syms, 1, shdrs[i].sh_size, f) != shdrs[i].sh_size) {
                free(syms); continue;
            }
            
            int count = shdrs[i].sh_size / sizeof(Elf64_Sym);
            Elf64_Shdr str_shdr = shdrs[shdrs[i].sh_link];
            char* strtab = malloc(str_shdr.sh_size);
            fseek(f, str_shdr.sh_offset, SEEK_SET);
            if (fread(strtab, 1, str_shdr.sh_size, f) != str_shdr.sh_size) {
                free(strtab); free(syms); continue;
            }
            
            for (int j = 0; j < count; j++) {
                if (sym_type(syms[j].st_info) != STT_FUNC && sym_type(syms[j].st_info) != STT_OBJECT) continue;
                if (syms[j].st_name != 0 && strcmp(name, strtab + syms[j].st_name) == 0) {
                    offset = syms[j].st_value;
                    break;
                }
            }
            free(strtab); free(syms);
        }
        if (offset) break;
    }
    free(shdrs); fclose(f);
    return offset;
}

int main(int argc, char** argv) {
    int opt;

    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            printf("Usage: spacegl_diag <PID> [OPTIONS]\n");
            printf("Space GL Process Memory Inspector (PIE-Compatible)\n\n");
            return 0;
        }
    }

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
            case 'h': return 0;
        }
    }

    if (optind >= argc) { fprintf(stderr, "Usage: %s <PID> [OPTIONS]\n", argv[0]); return 1; }
    pid_t pid = atoi(argv[optind]);
    char exe[512], line[1024];
    sprintf(exe, "/proc/%d/exe", pid);
    
    uintptr_t base = 0;
    char map_p[128]; sprintf(map_p, "/proc/%d/maps", pid);
    FILE* mf = fopen(map_p, "r");
    if (mf) {
        while (fgets(line, sizeof(line), mf)) {
            if (strstr(line, "spacegl_server") && strstr(line, "r-xp")) {
                base = strtoull(line, NULL, 16); break;
            }
        }
        fclose(mf);
    }

    int is_pie = 0;
    #define RESOLVE(name) uintptr_t a_##name = (get_symbol_offset(exe, #name, &is_pie) + (is_pie ? base : 0)); if (a_##name == (is_pie?base:0)) unresolved_count++;
    #define RESOLVE_S(name, sym) uintptr_t a_##name = (get_symbol_offset(exe, sym, &is_pie) + (is_pie ? base : 0)); if (a_##name == (is_pie?base:0)) unresolved_count++;

    int unresolved_count = 0;
    uintptr_t a_tick = (get_symbol_offset(exe, "global_tick", &is_pie) + (is_pie ? base : 0));
    if (a_tick == (is_pie?base:0)) unresolved_count++;

    RESOLVE(npcs); RESOLVE(players); RESOLVE_S(stars, "stars_data"); RESOLVE(planets);
    RESOLVE(bases); RESOLVE(black_holes); RESOLVE(nebulas); RESOLVE(pulsars); RESOLVE(quasars);
    RESOLVE(comets); RESOLVE(asteroids); RESOLVE(derelicts); RESOLVE(mines); RESOLVE(buoys);
    RESOLVE(platforms); RESOLVE(rifts); RESOLVE(monsters); RESOLVE(dysons); RESOLVE(hubs);
    RESOLVE(relics); RESOLVE(ruptures); RESOLVE(satellites); RESOLVE(storms); RESOLVE(players_torpedoes);
    RESOLVE(artifacts); RESOLVE(warp_gates); RESOLVE(neutron_stars); RESOLVE(mega_structs); RESOLVE(dark_clouds);
    RESOLVE(singularities); RESOLVE(plasma_storms); RESOLVE(orbital_rings); RESOLVE(time_anomalies); RESOLVE(void_crystals);

    if (unresolved_count > 25) {
        printf("CRITICAL ERROR: Failed to resolve %d symbols. Ensure spacegl_server has symbols.\n", unresolved_count);
        return 1;
    }

    initscr(); start_color(); curs_set(0); noecho(); nodelay(stdscr, TRUE); keypad(stdscr, TRUE);
    init_pair(1, COLOR_GREEN, COLOR_BLACK);
    init_pair(2, COLOR_CYAN, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);
    init_pair(4, COLOR_RED, COLOR_BLACK);
    init_pair(5, COLOR_WHITE, COLOR_BLACK);

    int current_page = 0;
    int scroll_offset = 0;
    int menu_selection = 1;
    int ch;

    ConnectedPlayer* pl_buf = malloc(sizeof(ConnectedPlayer) * MAX_CLIENTS);
    NPCShip* npc_buf = malloc(sizeof(NPCShip) * MAX_NPC);
    NPCStar* star_buf = malloc(sizeof(NPCStar) * MAX_STARS);
    NPCPlanet* planet_buf = malloc(sizeof(NPCPlanet) * MAX_PLANETS);
    NPCBase* base_buf = malloc(sizeof(NPCBase) * MAX_BASES);
    NPCBlackHole* bh_buf = malloc(sizeof(NPCBlackHole) * MAX_BH);
    NPCNebula* neb_buf = malloc(sizeof(NPCNebula) * MAX_NEBULAS);
    NPCPulsar* pul_buf = malloc(sizeof(NPCPulsar) * MAX_PULSARS);
    NPCQuasar* qua_buf = malloc(sizeof(NPCQuasar) * MAX_QUASARS);
    NPCComet* com_buf = malloc(sizeof(NPCComet) * MAX_COMETS);
    NPCAsteroid* ast_buf = malloc(sizeof(NPCAsteroid) * MAX_ASTEROIDS);
    NPCDerelict* der_buf = malloc(sizeof(NPCDerelict) * MAX_DERELICTS);
    NPCMine* mine_buf = malloc(sizeof(NPCMine) * MAX_MINES);
    NPCBuoy* buoy_buf = malloc(sizeof(NPCBuoy) * MAX_BUOYS);
    NPCPlatform* plat_buf = malloc(sizeof(NPCPlatform) * MAX_PLATFORMS);
    NPCRift* rift_buf = malloc(sizeof(NPCRift) * MAX_RIFTS);
    NPCMonster* mon_buf = malloc(sizeof(NPCMonster) * MAX_MONSTERS);
    NPCDyson* dys_buf = malloc(sizeof(NPCDyson) * MAX_DYSON);
    NPCHub* hub_buf = malloc(sizeof(NPCHub) * MAX_HUBS);
    NPCRelic* rel_buf = malloc(sizeof(NPCRelic) * MAX_RELICS);
    NPCRupture* rup_buf = malloc(sizeof(NPCRupture) * MAX_RUPTURES);
    NPCSatellite* sat_buf = malloc(sizeof(NPCSatellite) * MAX_SATELLITES);
    NPCStorm* sto_buf = malloc(sizeof(NPCStorm) * MAX_STORMS);
    NPCArtifact* art_buf = malloc(sizeof(NPCArtifact) * MAX_ARTIFACTS);
    NPCWarpGate* war_buf = malloc(sizeof(NPCWarpGate) * MAX_WARP_GATES);
    NPCNeutronStar* neu_buf = malloc(sizeof(NPCNeutronStar) * MAX_NEUTRON_STARS);
    NPCMegaStructure* meg_buf = malloc(sizeof(NPCMegaStructure) * MAX_MEGA_STRUCTS);
    NPCDarkCloud* dar_buf = malloc(sizeof(NPCDarkCloud) * MAX_DARK_CLOUDS);
    NPCSingularity* sin_buf = malloc(sizeof(NPCSingularity) * MAX_SINGULARITIES);
    NPCPlasmaStorm* pla_buf = malloc(sizeof(NPCPlasmaStorm) * MAX_PLASMA_STORMS);
    NPCOrbitalRing* orb_buf = malloc(sizeof(NPCOrbitalRing) * MAX_ORBITAL_RINGS);
    NPCTimeAnomaly* tim_buf = malloc(sizeof(NPCTimeAnomaly) * MAX_TIME_ANOMALIES);
    NPCVoidCrystal* voi_buf = malloc(sizeof(NPCVoidCrystal) * MAX_VOID_CRYSTALS);
    PlayerTorpedo* torp_buf = malloc(sizeof(PlayerTorpedo) * MAX_GLOBAL_TORPEDOES);
    RenderItem* render_list = malloc(sizeof(RenderItem) * 50000);

    while ((ch = getch()) != 'q') {
        if (ch == 'n') { current_page = (current_page + 1) % MAX_PAGES; scroll_offset = 0; }
        if (ch == 'p') { current_page = (current_page - 1 + MAX_PAGES) % MAX_PAGES; scroll_offset = 0; }
        
        DiagCat cat = pages[current_page].cat;

        if (cat == CAT_MENU) {
            if (ch == KEY_UP && menu_selection > 1) menu_selection--;
            if (ch == KEY_DOWN && menu_selection < MAX_PAGES - 1) menu_selection++;
            if (ch == 10 || ch == 13 || ch == KEY_ENTER) {
                current_page = menu_selection;
                scroll_offset = 0;
            }
        } else {
            if (ch == KEY_UP && scroll_offset > 0) scroll_offset--;
            if (ch == KEY_DOWN) scroll_offset++;
            if (ch == KEY_PPAGE) scroll_offset -= (LINES - 6);
            if (ch == KEY_NPAGE) scroll_offset += (LINES - 6);
            if (scroll_offset < 0) scroll_offset = 0;
            if (ch == 27 || ch == 'm') { current_page = 0; } /* Go back to menu */
        }

        int tick = 0;
        if (read_remote(pid, a_tick, &tick, sizeof(int)) <= 0) break;

        cat = pages[current_page].cat;
        int filter = pages[current_page].faction_filter;
        int total_items = 0;

        if (cat == CAT_MENU) {
            /* No remote data for menu */
        } else if (cat == CAT_SHIP) {
            read_remote(pid, a_players, pl_buf, sizeof(ConnectedPlayer) * MAX_CLIENTS);
            read_remote(pid, a_npcs, npc_buf, sizeof(NPCShip) * MAX_NPC);
            for (int i = 0; i < MAX_CLIENTS; i++) if (pl_buf[i].active && (filter == -1 || pl_buf[i].faction == filter)) { render_list[total_items].type = 0; render_list[total_items].index = i; total_items++; }
            for (int i = 0; i < MAX_NPC; i++) if (npc_buf[i].active && (filter == -1 || npc_buf[i].faction == filter)) { render_list[total_items].type = 1; render_list[total_items].index = i; total_items++; }
        } else {
            #define READ_CAT(buf, addr, max, type_id) read_remote(pid, addr, buf, sizeof(*buf)*max); for(int i=0; i<max; i++) if(buf[i].active) { render_list[total_items].type = type_id; render_list[total_items].index = i; total_items++; }
            switch(cat) {
                case CAT_STAR: READ_CAT(star_buf, a_stars, MAX_STARS, 2); break;
                case CAT_PLANET: READ_CAT(planet_buf, a_planets, MAX_PLANETS, 3); break;
                case CAT_BASE: READ_CAT(base_buf, a_bases, MAX_BASES, 4); break;
                case CAT_BH: READ_CAT(bh_buf, a_black_holes, MAX_BH, 5); break;
                case CAT_NEBULA: READ_CAT(neb_buf, a_nebulas, MAX_NEBULAS, 6); break;
                case CAT_PULSAR: READ_CAT(pul_buf, a_pulsars, MAX_PULSARS, 7); break;
                case CAT_QUASAR: READ_CAT(qua_buf, a_quasars, MAX_QUASARS, 8); break;
                case CAT_COMET: READ_CAT(com_buf, a_comets, MAX_COMETS, 9); break;
                case CAT_ASTEROID: READ_CAT(ast_buf, a_asteroids, MAX_ASTEROIDS, 10); break;
                case CAT_DERELICT: READ_CAT(der_buf, a_derelicts, MAX_DERELICTS, 11); break;
                case CAT_MINE: READ_CAT(mine_buf, a_mines, MAX_MINES, 12); break;
                case CAT_BUOY: READ_CAT(buoy_buf, a_buoys, MAX_BUOYS, 13); break;
                case CAT_PLATFORM: READ_CAT(plat_buf, a_platforms, MAX_PLATFORMS, 14); break;
                case CAT_RIFT: READ_CAT(rift_buf, a_rifts, MAX_RIFTS, 15); break;
                case CAT_MONSTER: READ_CAT(mon_buf, a_monsters, MAX_MONSTERS, 16); break;
                case CAT_DYSON: READ_CAT(dys_buf, a_dysons, MAX_DYSON, 17); break;
                case CAT_HUB: READ_CAT(hub_buf, a_hubs, MAX_HUBS, 18); break;
                case CAT_RELIC: READ_CAT(rel_buf, a_relics, MAX_RELICS, 19); break;
                case CAT_RUPTURE: READ_CAT(rup_buf, a_ruptures, MAX_RUPTURES, 20); break;
                case CAT_SATELLITE: READ_CAT(sat_buf, a_satellites, MAX_SATELLITES, 21); break;
                case CAT_STORM: READ_CAT(sto_buf, a_storms, MAX_STORMS, 22); break;
                case CAT_TORPEDO: READ_CAT(torp_buf, a_players_torpedoes, MAX_GLOBAL_TORPEDOES, 23); break;
                case CAT_ARTIFACT: READ_CAT(art_buf, a_artifacts, MAX_ARTIFACTS, 30); break;
                case CAT_WARP_GATE: READ_CAT(war_buf, a_warp_gates, MAX_WARP_GATES, 31); break;
                case CAT_NEUTRON_STAR: READ_CAT(neu_buf, a_neutron_stars, MAX_NEUTRON_STARS, 32); break;
                case CAT_MEGA_STRUCT: READ_CAT(meg_buf, a_mega_structs, MAX_MEGA_STRUCTS, 33); break;
                case CAT_DARK_CLOUD: READ_CAT(dar_buf, a_dark_clouds, MAX_DARK_CLOUDS, 34); break;
                case CAT_SINGULARITY: READ_CAT(sin_buf, a_singularities, MAX_SINGULARITIES, 35); break;
                case CAT_PLASMA_STORM: READ_CAT(pla_buf, a_plasma_storms, MAX_PLASMA_STORMS, 36); break;
                case CAT_ORBITAL_RING: READ_CAT(orb_buf, a_orbital_rings, MAX_ORBITAL_RINGS, 37); break;
                case CAT_TIME_ANOMALY: READ_CAT(tim_buf, a_time_anomalies, MAX_TIME_ANOMALIES, 38); break;
                case CAT_VOID_CRYSTAL: READ_CAT(voi_buf, a_void_crystals, MAX_VOID_CRYSTALS, 39); break;
                default: break;
            }
        }

        int max_visible_lines = LINES - 6;
        if (scroll_offset > total_items - max_visible_lines) scroll_offset = total_items - max_visible_lines;
        if (scroll_offset < 0) scroll_offset = 0;

        erase();
        attron(COLOR_PAIR(2) | A_BOLD);
        mvprintw(1, 2, "SPACE GL GLOBAL SCANNER | TICK: %d | PAGE %d/%d: %s | ITEMS: %d | SYMS: %s", 
                tick, current_page+1, MAX_PAGES, pages[current_page].name, total_items, 
                (unresolved_count==0)?"OK":"MISSING");
        mvhline(2, 0, ACS_HLINE, COLS); attroff(A_BOLD);

        int y = 4;
        if (cat == CAT_MENU) {
            attron(COLOR_PAIR(3) | A_BOLD);
            mvprintw(y++, 4, "--- NAVIGATION MENU ---");
            attroff(A_BOLD);
            mvprintw(y++, 4, "Use ARROWS to select and ENTER to jump. Press 'm' to return here.");
            y++;
            for (int i = 1; i < MAX_PAGES; i++) {
                if (i == menu_selection) {
                    attron(COLOR_PAIR(1) | A_REVERSE);
                    mvprintw(y++, 6, " > %-40s ", pages[i].name);
                    attroff(COLOR_PAIR(1) | A_REVERSE);
                } else {
                    mvprintw(y++, 6, "   %-40s ", pages[i].name);
                }
                if (y >= LINES - 2) break;
            }
        } else {
            attron(A_BOLD | COLOR_PAIR(5));
            mvprintw(y++, 2, "%-6s | %-20s | %-15s | %-12s | %-12s | %-4s | %-12s", "ID", "NAME", "TYPE/INFO", "QUADRANT", "SECTOR", "INTEG", "ENERGY/EXTRA");
            mvhline(y++, 0, ACS_HLINE, COLS); attroff(A_BOLD);

            for (int i = 0; i < max_visible_lines; i++) {
                int idx = scroll_offset + i; if (idx >= total_items) break;
                RenderItem item = render_list[idx];
                int cp = 5;
                char sid[16], name[64], info[32], qstr[32], sstr[32], integ[16], extra[32];
                strcpy(integ, "100%"); strcpy(extra, "-");

                switch(item.type) {
                    case 0: { ConnectedPlayer* p = &pl_buf[item.index]; cp=1; sprintf(sid, "P%02d", item.index+1); sprintf(name, "%s", p->name); sprintf(info, "%s", get_species_name(p->faction)); sprintf(qstr, "[%2d,%2d,%2d]", p->state.q1, p->state.q2, p->state.q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", p->state.s1, p->state.s2, p->state.s3); sprintf(integ, "%3d%%", (int)p->state.hull_integrity); sprintf(extra, "%" PRIu64, p->state.energy); } break;
                    case 1: { NPCShip* n = &npc_buf[item.index]; cp=(n->faction==FACTION_KORTHIAN)?4:3; sprintf(sid, "N%04d", n->id); sprintf(name, "%s", n->name); sprintf(info, "%s", get_species_name(n->faction)); sprintf(qstr, "[%2d,%2d,%2d]", n->q1, n->q2, n->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", n->x, n->y, n->z); sprintf(integ, "%3d%%", (int)n->health); sprintf(extra, "%" PRIu64, n->energy); } break;
                    case 2: { NPCStar* s = &star_buf[item.index]; cp=3; sprintf(sid, "S%04d", s->id); sprintf(name, "Star"); sprintf(info, "Spectral %d", s->faction); sprintf(qstr, "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 3: { NPCPlanet* p = &planet_buf[item.index]; cp=2; sprintf(sid, "PL%04d", p->id); sprintf(name, "Planet"); sprintf(info, "Res:%d", p->resource_type); sprintf(qstr, "[%2d,%2d,%2d]", p->q1, p->q2, p->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", p->x, p->y, p->z); sprintf(extra, "Amt:%d", p->amount); } break;
                    case 4: { NPCBase* b = &base_buf[item.index]; cp=1; sprintf(sid, "B%04d", b->id); sprintf(name, "Starbase"); sprintf(info, "Faction %d", b->faction); sprintf(qstr, "[%2d,%2d,%2d]", b->q1, b->q2, b->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", b->x, b->y, b->z); sprintf(integ, "%d", b->health); } break;
                    case 5: { NPCBlackHole* b = &bh_buf[item.index]; cp=4; sprintf(sid, "BH%04d", b->id); sprintf(name, "Black Hole"); sprintf(info, "Singularity"); sprintf(qstr, "[%2d,%2d,%2d]", b->q1, b->q2, b->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", b->x, b->y, b->z); } break;
                    case 6: { NPCNebula* n = &neb_buf[item.index]; cp=5; sprintf(sid, "NEB%04d", n->id); sprintf(name, "Nebula"); sprintf(info, "Type:%d", n->type); sprintf(qstr, "[%2d,%2d,%2d]", n->q1, n->q2, n->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", n->x, n->y, n->z); } break;
                    case 7: { NPCPulsar* p = &pul_buf[item.index]; cp=3; sprintf(sid, "PUL%04d", p->id); sprintf(name, "Pulsar"); sprintf(info, "Type:%d", p->type); sprintf(qstr, "[%2d,%2d,%2d]", p->q1, p->q2, p->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", p->x, p->y, p->z); } break;
                    case 8: { NPCQuasar* q = &qua_buf[item.index]; cp=3; sprintf(sid, "QSR%04d", q->id); sprintf(name, "Quasar"); sprintf(info, "Type:%d", q->type); sprintf(qstr, "[%2d,%2d,%2d]", q->q1, q->q2, q->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", q->x, q->y, q->z); } break;
                    case 9: { NPCComet* c = &com_buf[item.index]; cp=2; sprintf(sid, "COM%04d", c->id); sprintf(name, "Comet"); sprintf(info, "Moving"); sprintf(qstr, "[%2d,%2d,%2d]", c->q1, c->q2, c->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", c->x, c->y, c->z); } break;
                    case 10: { NPCAsteroid* a = &ast_buf[item.index]; cp=3; sprintf(sid, "AST%04d", a->id); sprintf(name, "Asteroid"); sprintf(info, "Res:%d", a->resource_type); sprintf(qstr, "[%2d,%2d,%2d]", a->q1, a->q2, a->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", a->x, a->y, a->z); sprintf(extra, "Amt:%d", a->amount); } break;
                    case 11: { NPCDerelict* d = &der_buf[item.index]; cp=5; sprintf(sid, "DE%04d", d->id); sprintf(name, "%s", d->name); sprintf(info, "Class %d", d->ship_class); sprintf(qstr, "[%2d,%2d,%2d]", d->q1, d->q2, d->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", d->x, d->y, d->z); } break;
                    case 12: { NPCMine* m = &mine_buf[item.index]; cp=4; sprintf(sid, "MIN%04d", m->id); sprintf(name, "Mine"); sprintf(info, "Faction %d", m->faction); sprintf(qstr, "[%2d,%2d,%2d]", m->q1, m->q2, m->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", m->x, m->y, m->z); } break;
                    case 13: { NPCBuoy* b = &buoy_buf[item.index]; cp=2; sprintf(sid, "BUY%04d", b->id); sprintf(name, "Comm Buoy"); sprintf(info, "Active"); sprintf(qstr, "[%2d,%2d,%2d]", b->q1, b->q2, b->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", b->x, b->y, b->z); } break;
                    case 14: { NPCPlatform* p = &plat_buf[item.index]; cp=4; sprintf(sid, "PF%04d", p->id); sprintf(name, "Platform"); sprintf(info, "Fac:%d", p->faction); sprintf(qstr, "[%2d,%2d,%2d]", p->q1, p->q2, p->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", p->x, p->y, p->z); sprintf(integ, "%d", p->health); sprintf(extra, "E:%" PRIu64, p->energy); } break;
                    case 15: { NPCRift* r = &rift_buf[item.index]; cp=2; sprintf(sid, "RIF%04d", r->id); sprintf(name, "Spatial Rift"); sprintf(info, "Active"); sprintf(qstr, "[%2d,%2d,%2d]", r->q1, r->q2, r->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", r->x, r->y, r->z); } break;
                    case 16: { NPCMonster* m = &mon_buf[item.index]; cp=4; sprintf(sid, "M%02d", m->id); sprintf(name, (m->type==30)?"Crystalline":"Amoeba"); sprintf(info, "OMEGA"); sprintf(qstr, "[%2d,%2d,%2d]", m->q1, m->q2, m->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", m->x, m->y, m->z); sprintf(integ, "%d", m->health); } break;
                    case 17: { NPCDyson* d = &dys_buf[item.index]; cp=3; sprintf(sid, "DY%04d", d->id); sprintf(name, "Dyson Frag"); sprintf(info, "Ancient"); sprintf(qstr, "[%2d,%2d,%2d]", d->q1, d->q2, d->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", d->x, d->y, d->z); } break;
                    case 18: { NPCHub* h = &hub_buf[item.index]; cp=2; sprintf(sid, "HB%04d", h->id); sprintf(name, "Trading Hub"); sprintf(info, "Neutral"); sprintf(qstr, "[%2d,%2d,%2d]", h->q1, h->q2, h->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", h->x, h->y, h->z); } break;
                    case 19: { NPCRelic* r = &rel_buf[item.index]; cp=2; sprintf(sid, "RE%04d", r->id); sprintf(name, "Ancient Relic"); sprintf(info, "Tech"); sprintf(qstr, "[%2d,%2d,%2d]", r->q1, r->q2, r->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", r->x, r->y, r->z); } break;
                    case 20: { NPCRupture* r = &rup_buf[item.index]; cp=4; sprintf(sid, "RU%04d", r->id); sprintf(name, "Subspace Rup"); sprintf(info, "Anomaly"); sprintf(qstr, "[%2d,%2d,%2d]", r->q1, r->q2, r->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", r->x, r->y, r->z); } break;
                    case 21: { NPCSatellite* s = &sat_buf[item.index]; cp=5; sprintf(sid, "SA%04d", s->id); sprintf(name, "Satellite"); sprintf(info, "Relay"); sprintf(qstr, "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 22: { NPCStorm* s = &sto_buf[item.index]; cp=5; sprintf(sid, "SO%04d", s->id); sprintf(name, "Ion Storm"); sprintf(info, "Meteo"); sprintf(qstr, "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 23: { PlayerTorpedo* t = &torp_buf[item.index]; cp=4; sprintf(sid, "T%04d", t->id); sprintf(name, "Torpedo"); sprintf(info, "Owner %d", t->owner_idx); sprintf(qstr, "[%2d,%2d,%2d]", t->q1, t->q2, t->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", t->x, t->y, t->z); sprintf(extra, "TO:%d", t->timeout); } break;
                    case 30: { NPCArtifact* a = &art_buf[item.index]; cp=3; sprintf(sid, "AA%04d", a->id); sprintf(name, "Alien Artifact"); sprintf(info, "Exotic"); sprintf(qstr, "[%2d,%2d,%2d]", a->q1, a->q2, a->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", a->x, a->y, a->z); } break;
                    case 31: { NPCWarpGate* w = &war_buf[item.index]; cp=2; sprintf(sid, "WG%04d", w->id); sprintf(name, "Warp Gate"); sprintf(info, "Active"); sprintf(qstr, "[%2d,%2d,%2d]", w->q1, w->q2, w->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", w->x, w->y, w->z); } break;
                    case 32: { NPCNeutronStar* n = &neu_buf[item.index]; cp=5; sprintf(sid, "NS%04d", n->id); sprintf(name, "Neutron Star"); sprintf(info, "Degenerate"); sprintf(qstr, "[%2d,%2d,%2d]", n->q1, n->q2, n->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", n->x, n->y, n->z); } break;
                    case 33: { NPCMegaStructure* m = &meg_buf[item.index]; cp=1; sprintf(sid, "MS%04d", m->id); sprintf(name, "Mega Struct"); sprintf(info, "Unknown"); sprintf(qstr, "[%2d,%2d,%2d]", m->q1, m->q2, m->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", m->x, m->y, m->z); } break;
                    case 34: { NPCDarkCloud* d = &dar_buf[item.index]; cp=4; sprintf(sid, "DC%04d", d->id); sprintf(name, "Dark Matter"); sprintf(info, "Obscured"); sprintf(qstr, "[%2d,%2d,%2d]", d->q1, d->q2, d->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", d->x, d->y, d->z); } break;
                    case 35: { NPCSingularity* s = &sin_buf[item.index]; cp=4; sprintf(sid, "QS%04d", s->id); sprintf(name, "Singularity"); sprintf(info, "Quantum"); sprintf(qstr, "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 36: { NPCPlasmaStorm* p = &pla_buf[item.index]; cp=3; sprintf(sid, "PS%04d", p->id); sprintf(name, "Plasma Storm"); sprintf(info, "Unstable"); sprintf(qstr, "[%2d,%2d,%2d]", p->q1, p->q2, p->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", p->x, p->y, p->z); } break;
                    case 37: { NPCOrbitalRing* o = &orb_buf[item.index]; cp=2; sprintf(sid, "OR%04d", o->id); sprintf(name, "Orbital Ring"); sprintf(info, "Planetary"); sprintf(qstr, "[%2d,%2d,%2d]", o->q1, o->q2, o->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", o->x, o->y, o->z); } break;
                    case 38: { NPCTimeAnomaly* t = &tim_buf[item.index]; cp=1; sprintf(sid, "TA%04d", t->id); sprintf(name, "Time Anomaly"); sprintf(info, "Temporal"); sprintf(qstr, "[%2d,%2d,%2d]", t->q1, t->q2, t->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", t->x, t->y, t->z); } break;
                    case 39: { NPCVoidCrystal* v = &voi_buf[item.index]; cp=5; sprintf(sid, "VC%04d", v->id); sprintf(name, "Void Crystal"); sprintf(info, "Crystalline"); sprintf(qstr, "[%2d,%2d,%2d]", v->q1, v->q2, v->q3); sprintf(sstr, "%5.1f,%5.1f,%5.1f", v->x, v->y, v->z); } break;
                    default: break;
                }

                attron(COLOR_PAIR(cp));
                mvprintw(y++, 2, "%-6s | %-20.20s | %-15.15s | %-12s | %-12s | %-4s | %-12s", sid, name, info, qstr, sstr, integ, extra);
                attroff(COLOR_PAIR(cp));
            }
        }
        attron(COLOR_PAIR(2));
        mvprintw(LINES-1, 2, "TOTAL: %d | VISIBLE: %d-%d | [UP/DWN] Scroll | [N/P] Page | [Q] Quit", total_items, scroll_offset+1, (scroll_offset+max_visible_lines > total_items)?total_items:scroll_offset+max_visible_lines);
        refresh(); usleep(100000);
    }
    endwin(); return 0;
}
