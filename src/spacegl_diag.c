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

#define MAX_PAGES 13
#define sym_type(i) ((i) & 0xf)

typedef struct {
    int id;
    const char* name;
} FactionPage;

FactionPage pages[MAX_PAGES] = {
    {-1, "ALL FACTIONS"},
    {FACTION_ALLIANCE, "ALLIANCE (PLAYER)"},
    {FACTION_KORTHIAN, "KORTHIAN EMPIRE"},
    {FACTION_XYLARI,   "XYLARI HEGEMONY"},
    {FACTION_SWARM,    "THE SWARM"},
    {FACTION_VESPERIAN, "VESPERIAN CONCLAVE"},
    {FACTION_JEM_HADAR, "ASCENDANT DOMINION"},
    {FACTION_THOLIAN,   "QUARZITE ASSEMBLY"},
    {FACTION_GORN,      "SAURIAN HEGEMONY"},
    {FACTION_GILDED,    "GILDED FEDERATION"},
    {FACTION_SPECIES_8472, "FLUIDIC SPECIES"},
    {FACTION_BREEN,     "CRYOS CONFEDERACY"},
    {FACTION_HIROGEN,   "APEX HUNTERS"}
};

typedef struct {
    int type; /* 0=Player, 1=NPC */
    int index; /* Original array index */
} RenderItem;

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
    uintptr_t arg_tick = 0, arg_npcs = 0, arg_players = 0;
    int opt;

    // Custom check for --help/--version before getopt to support long options
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            printf("Usage: spacegl_diag <PID> [OPTIONS]\n");
            printf("Space GL Process Memory Inspector (PIE-Compatible)\n\n");
            printf("Options:\n");
            printf("  --help, -h     Display this help and exit\n");
            printf("  --version      Display version information and exit\n");
            printf("  -t <addr>      Manual address for global_tick\n");
            printf("  -n <addr>      Manual address for npcs\n");
            printf("  -p <addr>      Manual address for players\n\n");
            printf("Arguments:\n");
            printf("  spacegl_diag -h\n");
            return 0;
        }
        if (strcmp(argv[1], "--version") == 0) {
            printf("spacegl_diag 2026.04.14 (PIE Support)\n");
            return 0;
        }
    }

    while ((opt = getopt(argc, argv, "ht:n:p:")) != -1) {
        switch (opt) {
            case 'h':
                // Handled above, but keeping for switch consistency
                return 0;
            case 't': arg_tick = strtoull(optarg, NULL, 16); break;
            case 'n': arg_npcs = strtoull(optarg, NULL, 16); break;
            case 'p': arg_players = strtoull(optarg, NULL, 16); break;
        }
    }

    if (optind >= argc) { 
        fprintf(stderr, "Usage: %s <PID> [OPTIONS]\n", argv[0]); 
        return 1; 
    }
    
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

    uintptr_t a_tick, a_npcs, a_players;
    if (arg_tick && arg_npcs && arg_players) {
        a_tick = base + arg_tick;
        a_npcs = base + arg_npcs;
        a_players = base + arg_players;
    } else {
        int is_pie = 0;
        uintptr_t sym_tick = get_symbol_offset(exe, "global_tick", &is_pie);
        uintptr_t sym_npcs = get_symbol_offset(exe, "npcs", NULL);
        uintptr_t sym_players = get_symbol_offset(exe, "players", NULL);

        if (sym_tick == 0) {
            printf("ERROR: Could not find symbols in %s and no offsets provided.\n", exe);
            printf("Usage: spacegl_diag <PID> -t <offset> -n <offset> -p <offset>\n");
            return 1;
        }

        a_tick = (is_pie ? base : 0) + sym_tick;
        a_npcs = (is_pie ? base : 0) + sym_npcs;
        a_players = (is_pie ? base : 0) + sym_players;
    }

    initscr(); start_color(); curs_set(0); noecho(); nodelay(stdscr, TRUE); keypad(stdscr, TRUE);
    init_pair(1, COLOR_GREEN, COLOR_BLACK);
    init_pair(2, COLOR_CYAN, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);
    init_pair(4, COLOR_RED, COLOR_BLACK);
    init_pair(5, COLOR_WHITE, COLOR_BLACK);

    int current_page = 0;
    int scroll_offset = 0;
    int ch;

    ConnectedPlayer* pl_buf = malloc(sizeof(ConnectedPlayer) * MAX_CLIENTS);
    NPCShip* npc_buf = malloc(sizeof(NPCShip) * MAX_NPC);
    RenderItem* render_list = malloc(sizeof(RenderItem) * (MAX_NPC + MAX_CLIENTS));

    while ((ch = getch()) != 'q') {
        if (ch == 'n') { current_page = (current_page + 1) % MAX_PAGES; scroll_offset = 0; }
        if (ch == 'p') { current_page = (current_page - 1 + MAX_PAGES) % MAX_PAGES; scroll_offset = 0; }
        if (ch == KEY_UP && scroll_offset > 0) scroll_offset--;
        if (ch == KEY_DOWN) scroll_offset++;
        if (ch == KEY_PPAGE) scroll_offset -= (LINES - 6);
        if (ch == KEY_NPAGE) scroll_offset += (LINES - 6);
        if (scroll_offset < 0) scroll_offset = 0;

        int tick = 0;
        if (read_remote(pid, a_tick, &tick, sizeof(int)) <= 0) break;

        read_remote(pid, a_players, pl_buf, sizeof(ConnectedPlayer) * MAX_CLIENTS);
        read_remote(pid, a_npcs, npc_buf, sizeof(NPCShip) * MAX_NPC);

        int total_items = 0;
        int filter_f = pages[current_page].id;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (pl_buf[i].active && (filter_f == -1 || pl_buf[i].faction == filter_f)) {
                render_list[total_items].type = 0;
                render_list[total_items].index = i;
                total_items++;
            }
        }
        for (int i = 0; i < MAX_NPC; i++) {
            if (npc_buf[i].active) {
                if (filter_f != -1 && npc_buf[i].faction != filter_f) continue;
                render_list[total_items].type = 1;
                render_list[total_items].index = i;
                total_items++;
            }
        }

        int max_visible_lines = LINES - 6;
        if (scroll_offset > total_items - max_visible_lines) scroll_offset = total_items - max_visible_lines;
        if (scroll_offset < 0) scroll_offset = 0;

        erase();
        attron(COLOR_PAIR(2) | A_BOLD);
        mvprintw(1, 2, "SPACE GL GLOBAL SCANNER | TICK: %d | PAGE %d/%d: %s", tick, current_page+1, MAX_PAGES, pages[current_page].name);
        mvhline(2, 0, ACS_HLINE, COLS);
        attroff(A_BOLD);

        int y = 4;
        attron(A_BOLD | COLOR_PAIR(5));
        mvprintw(y++, 2, "%-6s | %-20s | %-15s | %-12s | %-12s | %-4s | %-12s", "ID", "NAME", "FACTION", "QUADRANT", "SECTOR", "HULL", "ENERGY");
        attroff(A_BOLD);
        mvhline(y++, 0, ACS_HLINE, COLS);

        for (int i = 0; i < max_visible_lines; i++) {
            int idx_in_list = scroll_offset + i;
            if (idx_in_list >= total_items) break;
            RenderItem item = render_list[idx_in_list];
            
            if (item.type == 0) {
                ConnectedPlayer* p = &pl_buf[item.index];
                attron(COLOR_PAIR(1));
                mvprintw(y++, 2, "P%02d    | %-20.20s | %-15s | [%2d,%2d,%2d] | %5.1f,%5.1f,%5.1f | %3d%% | %-12" PRIu64,
                         item.index + 1, p->name, "ALLIANCE", 
                         p->state.q1, p->state.q2, p->state.q3,
                         p->state.s1, p->state.s2, p->state.s3, 
                         (int)p->state.hull_integrity, p->state.energy);
                attroff(COLOR_PAIR(1));
            } else {
                NPCShip* n = &npc_buf[item.index];
                int cp = (n->faction == FACTION_KORTHIAN) ? 4 : 3;
                const char* f_name = "UNKNOWN";
                for (int p = 1; p < MAX_PAGES; p++) { if (n->faction == pages[p].id) { f_name = pages[p].name; break; } }
                attron(COLOR_PAIR(cp));
                mvprintw(y++, 2, "%-6d | %-20.20s | %-15.15s | [%2d,%2d,%2d] | %5.1f,%5.1f,%5.1f | %3d%% | %-12" PRIu64,
                         n->id + GALAXY_OBJECT_MIN_NPC, n->name, f_name,
                         n->q1, n->q2, n->q3, n->x, n->y, n->z, (int)n->health, n->energy);
                attroff(COLOR_PAIR(cp));
            }
        }
        attron(COLOR_PAIR(2));
        mvprintw(LINES-1, 2, "TOTAL: %d | VISIBLE: %d-%d | [UP/DWN] Scroll | [N/P] Faction | [Q] Quit", 
                 total_items, scroll_offset+1, (scroll_offset+max_visible_lines > total_items)?total_items:scroll_offset+max_visible_lines);
        attroff(COLOR_PAIR(2));
        refresh(); usleep(100000);
    }
    free(pl_buf); free(npc_buf); free(render_list);
    endwin(); return 0;
}
