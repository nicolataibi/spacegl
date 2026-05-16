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

#define MAX_PAGES 150
#define sym_type(i) ((i) & 0xf)

typedef enum {
    CAT_MENU = 0, CAT_SHIP, CAT_STAR, CAT_PLANET, CAT_BASE, CAT_BH, CAT_NEBULA, CAT_PULSAR, CAT_QUASAR,
    CAT_COMET, CAT_ASTEROID, CAT_DERELICT, CAT_MINE, CAT_BUOY, CAT_PLATFORM, CAT_RIFT, CAT_MONSTER,
    CAT_DYSON, CAT_HUB, CAT_RELIC, CAT_RUPTURE, CAT_SATELLITE, CAT_STORM, CAT_TORPEDO,
    CAT_ARTIFACT, CAT_WARP_GATE, CAT_NEUTRON_STAR, CAT_MEGA_STRUCT, CAT_DARK_CLOUD,
    CAT_SINGULARITY, CAT_PLASMA_STORM, CAT_ORBITAL_RING, CAT_TIME_ANOMALY, CAT_VOID_CRYSTAL,
    CAT_ANOMALY, CAT_DIFFUSE_NEBULA, CAT_DARK_NEBULA, CAT_PLANETARY_NEBULA, CAT_SNR, CAT_GMC,
    CAT_INTERSTELLAR_FILAMENT, CAT_INTERSTELLAR_BUBBLE, CAT_BOK_GLOBULE, CAT_CLUMP_CORE,
    CAT_ACCRETION_DISK, CAT_RELATIVISTIC_JET, CAT_SHOCK_WAVE, CAT_STELLAR_BOW_SHOCK,
    CAT_COSMIC_VOID, CAT_COSMIC_FILAMENT, CAT_EVENT_HORIZON, CAT_KILONOVA, CAT_GRAV_LENS,
    CAT_GRB, CAT_GRAV_WAVE, CAT_PROTOPLANETARY_DISK, CAT_DEBRIS_DISK, CAT_PLANETESIMAL,
    CAT_ROGUE_PLANET, CAT_BROWN_DWARF, CAT_ISO, CAT_MAG_RECONN, CAT_CURRENT_SHEET,
    CAT_HELIOSPHERE, CAT_TERM_SHOCK, CAT_MAGNETOSPHERE, CAT_COSMIC_STRING, CAT_DOMAIN_WALL,
    CAT_DM_HALO, CAT_IGM, CAT_CGM, CAT_LYMAN_ALPHA, CAT_CMB
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
    {CAT_DIFFUSE_NEBULA, 0, "DIFFUSE NEBULA"},
    {CAT_DARK_NEBULA, 0, "DARK NEBULA"},
    {CAT_PLANETARY_NEBULA, 0, "PLANET NEB"},
    {CAT_SNR, 0, "SNR"},
    {CAT_GMC, 0, "GMC"},
    {CAT_INTERSTELLAR_FILAMENT, 0, "INT FILAMENT"},
    {CAT_INTERSTELLAR_BUBBLE, 0, "INT BUBBLE"},
    {CAT_BOK_GLOBULE, 0, "BOK GLOBULE"},
    {CAT_CLUMP_CORE, 0, "CLUMP/CORE"},
    {CAT_ACCRETION_DISK, 0, "ACCRET DISK"},
    {CAT_RELATIVISTIC_JET, 0, "RELATIV JET"},
    {CAT_SHOCK_WAVE, 0, "SHOCK WAVE"},
    {CAT_STELLAR_BOW_SHOCK, 0, "BOW SHOCK"},
    {CAT_COSMIC_VOID, 0, "COSMIC VOID"},
    {CAT_COSMIC_FILAMENT, 0, "COSMIC FIL"},
    {CAT_EVENT_HORIZON, 0, "EVENT HORIZ"},
    {CAT_KILONOVA, 0, "KILONOVA"},
    {CAT_GRAV_LENS, 0, "GRAV LENS"},
    {CAT_GRB, 0, "GRB"},
    {CAT_GRAV_WAVE, 0, "GRAV WAVE"},
    {CAT_PROTOPLANETARY_DISK, 0, "PROTOPLANET"},
    {CAT_DEBRIS_DISK, 0, "DEBRIS DISK"},
    {CAT_PLANETESIMAL, 0, "PLANETESIMAL"},
    {CAT_ROGUE_PLANET, 0, "ROGUE PLANET"},
    {CAT_BROWN_DWARF, 0, "BROWN DWARF"},
    {CAT_ISO, 0, "INTERST OBJ"},
    {CAT_MAG_RECONN, 0, "MAG RECONN"},
    {CAT_CURRENT_SHEET, 0, "CUR SHEET"},
    {CAT_HELIOSPHERE, 0, "HELIOSPHERE"},
    {CAT_TERM_SHOCK, 0, "TERM SHOCK"},
    {CAT_MAGNETOSPHERE, 0, "MAGNETOSPH"},
    {CAT_COSMIC_STRING, 0, "COSM STRING"},
    {CAT_DOMAIN_WALL, 0, "DOMAIN WALL"},
    {CAT_DM_HALO, 0, "DM HALO"},
    {CAT_IGM, 0, "IGM"},
    {CAT_CGM, 0, "CGM"},
    {CAT_LYMAN_ALPHA, 0, "LYMAN ALPHA"},
    {CAT_CMB, 0, "CMB"},
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
    {0, 0, NULL} /* Sentinel */
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
    #define RESOLVE(name) uintptr_t off_##name = get_symbol_offset(exe, #name, &is_pie); \
                          uintptr_t a_##name = off_##name ? (off_##name + (is_pie ? base : 0)) : 0; \
                          if (!a_##name) unresolved_count++;
    #define RESOLVE_S(name, sym) uintptr_t off_##name = get_symbol_offset(exe, sym, &is_pie); \
                                uintptr_t a_##name = off_##name ? (off_##name + (is_pie ? base : 0)) : 0; \
                                if (!a_##name) unresolved_count++;

    int unresolved_count = 0;
    uintptr_t off_tick = get_symbol_offset(exe, "global_tick", &is_pie);
    uintptr_t a_tick = off_tick ? (off_tick + (is_pie ? base : 0)) : 0;
    if (!a_tick) unresolved_count++;

    RESOLVE(npcs); RESOLVE(players); RESOLVE_S(stars, "stars_data"); RESOLVE(planets);
    RESOLVE(bases); RESOLVE(black_holes); RESOLVE(nebulas); RESOLVE(pulsars); RESOLVE(quasars);
    RESOLVE(comets); RESOLVE(asteroids); RESOLVE(derelicts); RESOLVE(mines); RESOLVE(buoys);
    RESOLVE(platforms); RESOLVE(rifts); RESOLVE(monsters); RESOLVE(dysons); RESOLVE(hubs);
    RESOLVE(relics); RESOLVE(ruptures); RESOLVE(satellites); RESOLVE(storms); RESOLVE(players_torpedoes);
    RESOLVE(artifacts); RESOLVE(warp_gates); RESOLVE(neutron_stars); RESOLVE(mega_structs); RESOLVE(dark_clouds);
    RESOLVE(singularities); RESOLVE(plasma_storms); RESOLVE(orbital_rings); RESOLVE(time_anomalies); RESOLVE(void_crystals); RESOLVE(subspace_anomalies);
    RESOLVE(diffuse_nebulae);
    RESOLVE(dark_nebulae);
    RESOLVE(planetary_nebulae);
    RESOLVE(snrs);
    RESOLVE(gmcs);
    RESOLVE(interstellar_filaments);
    RESOLVE(interstellar_bubbles);
    RESOLVE(bok_globules);
    RESOLVE(clump_cores);
    RESOLVE(accretion_disks);
    RESOLVE(relativistic_jets);
    RESOLVE(shock_waves);
    RESOLVE(stellar_bow_shocks);
    RESOLVE(cosmic_voids);
    RESOLVE(cosmic_filaments);
    RESOLVE(event_horizons);
    RESOLVE(kilonovae);
    RESOLVE(grav_lenses);
    RESOLVE(grbs);
    RESOLVE(grav_waves);
    RESOLVE(protoplanetary_disks);
    RESOLVE(debris_disks);
    RESOLVE(planetesimals);
    RESOLVE(rogue_planets);
    RESOLVE(brown_dwarfs);
    RESOLVE(isos);
    RESOLVE(mag_reconns);
    RESOLVE(current_sheets);
    RESOLVE(heliospheres);
    RESOLVE(term_shocks);
    RESOLVE(magnetospheres);
    RESOLVE(cosmic_strings);
    RESOLVE(domain_walls);
    RESOLVE(dm_halos);
    RESOLVE(igms);
    RESOLVE(cgms);
    RESOLVE(lyman_alphas);
    RESOLVE(cmbs);

    if (unresolved_count > 30) {
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

    ConnectedPlayer* pl_buf = calloc(MAX_CLIENTS, sizeof(ConnectedPlayer));
    NPCShip* npc_buf = calloc(MAX_NPC, sizeof(NPCShip));
    NPCStar* star_buf = calloc(MAX_STARS, sizeof(NPCStar));
    NPCPlanet* planet_buf = calloc(MAX_PLANETS, sizeof(NPCPlanet));
    NPCBase* base_buf = calloc(MAX_BASES, sizeof(NPCBase));
    NPCBlackHole* bh_buf = calloc(MAX_BH, sizeof(NPCBlackHole));
    NPCNebula* neb_buf = calloc(MAX_NEBULAS, sizeof(NPCNebula));
    NPCPulsar* pul_buf = calloc(MAX_PULSARS, sizeof(NPCPulsar));
    NPCQuasar* qua_buf = calloc(MAX_QUASARS, sizeof(NPCQuasar));
    NPCComet* com_buf = calloc(MAX_COMETS, sizeof(NPCComet));
    NPCAsteroid* ast_buf = calloc(MAX_ASTEROIDS, sizeof(NPCAsteroid));
    NPCDerelict* der_buf = calloc(MAX_DERELICTS, sizeof(NPCDerelict));
    NPCMine* mine_buf = calloc(MAX_MINES, sizeof(NPCMine));
    NPCBuoy* buoy_buf = calloc(MAX_BUOYS, sizeof(NPCBuoy));
    NPCPlatform* plat_buf = calloc(MAX_PLATFORMS, sizeof(NPCPlatform));
    NPCRift* rift_buf = calloc(MAX_RIFTS, sizeof(NPCRift));
    NPCMonster* mon_buf = calloc(MAX_MONSTERS, sizeof(NPCMonster));
    NPCDyson* dys_buf = calloc(MAX_DYSON, sizeof(NPCDyson));
    NPCHub* hub_buf = calloc(MAX_HUBS, sizeof(NPCHub));
    NPCRelic* rel_buf = calloc(MAX_RELICS, sizeof(NPCRelic));
    NPCRupture* rup_buf = calloc(MAX_RUPTURES, sizeof(NPCRupture));
    NPCSatellite* sat_buf = calloc(MAX_SATELLITES, sizeof(NPCSatellite));
    NPCStorm* sto_buf = calloc(MAX_STORMS, sizeof(NPCStorm));
    NPCArtifact* art_buf = calloc(MAX_ARTIFACTS, sizeof(NPCArtifact));
    NPCWarpGate* war_buf = calloc(MAX_WARP_GATES, sizeof(NPCWarpGate));
    NPCNeutronStar* neu_buf = calloc(MAX_NEUTRON_STARS, sizeof(NPCNeutronStar));
    NPCMegaStructure* meg_buf = calloc(MAX_MEGA_STRUCTS, sizeof(NPCMegaStructure));
    NPCDarkCloud* dar_buf = calloc(MAX_DARK_CLOUDS, sizeof(NPCDarkCloud));
    NPCSingularity* sin_buf = calloc(MAX_SINGULARITIES, sizeof(NPCSingularity));
    NPCPlasmaStorm* pla_buf = calloc(MAX_PLASMA_STORMS, sizeof(NPCPlasmaStorm));
    NPCOrbitalRing* orb_buf = calloc(MAX_ORBITAL_RINGS, sizeof(NPCOrbitalRing));
    NPCTimeAnomaly* tim_buf = calloc(MAX_TIME_ANOMALIES, sizeof(NPCTimeAnomaly));
    NPCVoidCrystal* voi_buf = calloc(MAX_VOID_CRYSTALS, sizeof(NPCVoidCrystal));
    NPCSubspaceAnomaly* sub_buf = calloc(MAX_SUBSPACE_ANOMALIES, sizeof(NPCSubspaceAnomaly));
    NPCDiffuseNebula* dif_buf = calloc(MAX_DIFFUSE_NEBULAE, sizeof(NPCDiffuseNebula));
    NPCDarkNebula* dar2_buf = calloc(MAX_DARK_NEBULAE, sizeof(NPCDarkNebula));
    NPCPlanetaryNebula* pla2_buf = calloc(MAX_PLANETARY_NEBULAE, sizeof(NPCPlanetaryNebula));
    NPCSNR* snr_buf = calloc(MAX_SNR, sizeof(NPCSNR));
    NPCGMC* gmc_buf = calloc(MAX_GMC, sizeof(NPCGMC));
    NPCInterstellarFilament* inf_buf = calloc(MAX_INTERSTELLAR_FILAMENTS, sizeof(NPCInterstellarFilament));
    NPCInterstellarBubble* inb_buf = calloc(MAX_INTERSTELLAR_BUBBLES, sizeof(NPCInterstellarBubble));
    NPCBokGlobule* bok_buf = calloc(MAX_BOK_GLOBULES, sizeof(NPCBokGlobule));
    NPCClumpCore* clc_buf = calloc(MAX_CLUMP_CORES, sizeof(NPCClumpCore));
    NPCAccretionDisk* acd_buf = calloc(MAX_ACCRETION_DISKS, sizeof(NPCAccretionDisk));
    NPCRelativisticJet* rej_buf = calloc(MAX_RELATIVISTIC_JETS, sizeof(NPCRelativisticJet));
    NPCShockWave* shw_buf = calloc(MAX_SHOCK_WAVES, sizeof(NPCShockWave));
    NPCStellarBowShock* sbs_buf = calloc(MAX_STELLAR_BOW_SHOCKS, sizeof(NPCStellarBowShock));
    NPCCosmicVoid* cov_buf = calloc(MAX_COSMIC_VOIDS, sizeof(NPCCosmicVoid));
    NPCCosmicFilament* cof_buf = calloc(MAX_COSMIC_FILAMENTS, sizeof(NPCCosmicFilament));
    NPCEventHorizon* evh_buf = calloc(MAX_EVENT_HORIZONS, sizeof(NPCEventHorizon));
    NPCKilonova* kil_buf = calloc(MAX_KILONOVAE, sizeof(NPCKilonova));
    NPCGravLens* grl_buf = calloc(MAX_GRAV_LENSES, sizeof(NPCGravLens));
    NPCGRB* grb_buf = calloc(MAX_GRB, sizeof(NPCGRB));
    NPCGravWave* grw_buf = calloc(MAX_GRAV_WAVES, sizeof(NPCGravWave));
    NPCProtoplanetaryDisk* ppd_buf = calloc(MAX_PROTOPLANETARY_DISKS, sizeof(NPCProtoplanetaryDisk));
    NPCDebrisDisk* ded_buf = calloc(MAX_DEBRIS_DISKS, sizeof(NPCDebrisDisk));
    NPCPlanetesimal* ptm_buf = calloc(MAX_PLANETESIMALS, sizeof(NPCPlanetesimal));
    NPCRoguePlanet* rop_buf = calloc(MAX_ROGUE_PLANETS, sizeof(NPCRoguePlanet));
    NPCBrownDwarf* brd_buf = calloc(MAX_BROWN_DWARFS, sizeof(NPCBrownDwarf));
    NPCISO* iso_buf = calloc(MAX_ISO, sizeof(NPCISO));
    NPCMagReconn* mgr_buf = calloc(MAX_MAG_RECONN, sizeof(NPCMagReconn));
    NPCCurrentSheet* cus_buf = calloc(MAX_CURRENT_SHEETS, sizeof(NPCCurrentSheet));
    NPCHeliosphere* hel_buf = calloc(MAX_HELIOSPHERES, sizeof(NPCHeliosphere));
    NPCTermShock* tes_buf = calloc(MAX_TERM_SHOCKS, sizeof(NPCTermShock));
    NPCMagnetosphere* mgs_buf = calloc(MAX_MAGNETOSPHERES, sizeof(NPCMagnetosphere));
    NPCCosmicString* cst_buf = calloc(MAX_COSMIC_STRINGS, sizeof(NPCCosmicString));
    NPCDomainWall* dow_buf = calloc(MAX_DOMAIN_WALLS, sizeof(NPCDomainWall));
    NPCDMHalo* dmh_buf = calloc(MAX_DM_HALO, sizeof(NPCDMHalo));
    NPCIGM* igm_buf = calloc(MAX_IGM, sizeof(NPCIGM));
    NPCCGM* cgm_buf = calloc(MAX_CGM, sizeof(NPCCGM));
    NPCLymanAlpha* lya_buf = calloc(MAX_LYMAN_ALPHA, sizeof(NPCLymanAlpha));
    NPCCMB* cmb_buf = calloc(MAX_CMB, sizeof(NPCCMB));
    PlayerTorpedo* torp_buf = calloc(MAX_GLOBAL_TORPEDOES, sizeof(PlayerTorpedo));
    RenderItem* render_list = malloc(sizeof(RenderItem) * 100000);

    while ((ch = getch()) != 'q') {
        if (ch == 'n') { current_page = (current_page + 1) % MAX_PAGES; scroll_offset = 0; }
        if (ch == 'p') { current_page = (current_page - 1 + MAX_PAGES) % MAX_PAGES; scroll_offset = 0; }
        
        while (current_page < MAX_PAGES && pages[current_page].name == NULL) current_page = (current_page + 1) % MAX_PAGES;
        DiagCat cat = pages[current_page].cat;

        if (cat == CAT_MENU) {
            if (ch == KEY_UP && menu_selection > 1) menu_selection--;
            if (ch == KEY_DOWN && menu_selection < MAX_PAGES - 1) {
                if (pages[menu_selection + 1].name != NULL) menu_selection++;
            }
            if (ch == KEY_RIGHT && menu_selection + 22 < MAX_PAGES) {
                if (pages[menu_selection + 22].name != NULL) menu_selection += 22;
            }
            if (ch == KEY_LEFT && menu_selection - 22 >= 1) menu_selection -= 22;
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
            #define READ_CAT(buf, addr, max, type_id) if (addr) { if (read_remote(pid, addr, buf, sizeof(*buf)*max) > 0) { for(int i=0; i<max; i++) if(buf[i].active) { render_list[total_items].type = type_id; render_list[total_items].index = i; total_items++; } } }
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
                case CAT_ANOMALY: READ_CAT(sub_buf, a_subspace_anomalies, MAX_SUBSPACE_ANOMALIES, 50); break;
                case CAT_DIFFUSE_NEBULA: READ_CAT(dif_buf, a_diffuse_nebulae, MAX_DIFFUSE_NEBULAE, 51); break;
                case CAT_DARK_NEBULA: READ_CAT(dar2_buf, a_dark_nebulae, MAX_DARK_NEBULAE, 52); break;
                case CAT_PLANETARY_NEBULA: READ_CAT(pla2_buf, a_planetary_nebulae, MAX_PLANETARY_NEBULAE, 53); break;
                case CAT_SNR: READ_CAT(snr_buf, a_snrs, MAX_SNR, 54); break;
                case CAT_GMC: READ_CAT(gmc_buf, a_gmcs, MAX_GMC, 55); break;
                case CAT_INTERSTELLAR_FILAMENT: READ_CAT(inf_buf, a_interstellar_filaments, MAX_INTERSTELLAR_FILAMENTS, 56); break;
                case CAT_INTERSTELLAR_BUBBLE: READ_CAT(inb_buf, a_interstellar_bubbles, MAX_INTERSTELLAR_BUBBLES, 57); break;
                case CAT_BOK_GLOBULE: READ_CAT(bok_buf, a_bok_globules, MAX_BOK_GLOBULES, 58); break;
                case CAT_CLUMP_CORE: READ_CAT(clc_buf, a_clump_cores, MAX_CLUMP_CORES, 59); break;
                case CAT_ACCRETION_DISK: READ_CAT(acd_buf, a_accretion_disks, MAX_ACCRETION_DISKS, 60); break;
                case CAT_RELATIVISTIC_JET: READ_CAT(rej_buf, a_relativistic_jets, MAX_RELATIVISTIC_JETS, 61); break;
                case CAT_SHOCK_WAVE: READ_CAT(shw_buf, a_shock_waves, MAX_SHOCK_WAVES, 62); break;
                case CAT_STELLAR_BOW_SHOCK: READ_CAT(sbs_buf, a_stellar_bow_shocks, MAX_STELLAR_BOW_SHOCKS, 63); break;
                case CAT_COSMIC_VOID: READ_CAT(cov_buf, a_cosmic_voids, MAX_COSMIC_VOIDS, 64); break;
                case CAT_COSMIC_FILAMENT: READ_CAT(cof_buf, a_cosmic_filaments, MAX_COSMIC_FILAMENTS, 65); break;
                case CAT_EVENT_HORIZON: READ_CAT(evh_buf, a_event_horizons, MAX_EVENT_HORIZONS, 66); break;
                case CAT_KILONOVA: READ_CAT(kil_buf, a_kilonovae, MAX_KILONOVAE, 67); break;
                case CAT_GRAV_LENS: READ_CAT(grl_buf, a_grav_lenses, MAX_GRAV_LENSES, 68); break;
                case CAT_GRB: READ_CAT(grb_buf, a_grbs, MAX_GRB, 69); break;
                case CAT_GRAV_WAVE: READ_CAT(grw_buf, a_grav_waves, MAX_GRAV_WAVES, 70); break;
                case CAT_PROTOPLANETARY_DISK: READ_CAT(ppd_buf, a_protoplanetary_disks, MAX_PROTOPLANETARY_DISKS, 71); break;
                case CAT_DEBRIS_DISK: READ_CAT(ded_buf, a_debris_disks, MAX_DEBRIS_DISKS, 72); break;
                case CAT_PLANETESIMAL: READ_CAT(ptm_buf, a_planetesimals, MAX_PLANETESIMALS, 73); break;
                case CAT_ROGUE_PLANET: READ_CAT(rop_buf, a_rogue_planets, MAX_ROGUE_PLANETS, 74); break;
                case CAT_BROWN_DWARF: READ_CAT(brd_buf, a_brown_dwarfs, MAX_BROWN_DWARFS, 75); break;
                case CAT_ISO: READ_CAT(iso_buf, a_isos, MAX_ISO, 76); break;
                case CAT_MAG_RECONN: READ_CAT(mgr_buf, a_mag_reconns, MAX_MAG_RECONN, 77); break;
                case CAT_CURRENT_SHEET: READ_CAT(cus_buf, a_current_sheets, MAX_CURRENT_SHEETS, 78); break;
                case CAT_HELIOSPHERE: READ_CAT(hel_buf, a_heliospheres, MAX_HELIOSPHERES, 79); break;
                case CAT_TERM_SHOCK: READ_CAT(tes_buf, a_term_shocks, MAX_TERM_SHOCKS, 80); break;
                case CAT_MAGNETOSPHERE: READ_CAT(mgs_buf, a_magnetospheres, MAX_MAGNETOSPHERES, 81); break;
                case CAT_COSMIC_STRING: READ_CAT(cst_buf, a_cosmic_strings, MAX_COSMIC_STRINGS, 82); break;
                case CAT_DOMAIN_WALL: READ_CAT(dow_buf, a_domain_walls, MAX_DOMAIN_WALLS, 83); break;
                case CAT_DM_HALO: READ_CAT(dmh_buf, a_dm_halos, MAX_DM_HALO, 84); break;
                case CAT_IGM: READ_CAT(igm_buf, a_igms, MAX_IGM, 85); break;
                case CAT_CGM: READ_CAT(cgm_buf, a_cgms, MAX_CGM, 86); break;
                case CAT_LYMAN_ALPHA: READ_CAT(lya_buf, a_lyman_alphas, MAX_LYMAN_ALPHA, 87); break;
                case CAT_CMB: READ_CAT(cmb_buf, a_cmbs, MAX_CMB, 88); break;
                default: break;
            }
        }

        int max_visible_lines = LINES - 6;
        if (total_items > 0 && scroll_offset > total_items - max_visible_lines) scroll_offset = total_items - max_visible_lines;
        if (scroll_offset < 0) scroll_offset = 0;

        erase();
        attron(COLOR_PAIR(2) | A_BOLD);
        mvprintw(1, 2, "SPACE GL GLOBAL SCANNER | TICK: %d | PAGE %d/%d: %s | ITEMS: %d | SYMS: %s", 
                tick, current_page+1, MAX_PAGES, pages[current_page].name ? pages[current_page].name : "UNKNOWN", total_items, 
                (unresolved_count==0)?"OK":"MISSING");
        mvhline(2, 0, ACS_HLINE, COLS); attroff(A_BOLD);

        int y = 4;
        if (cat == CAT_MENU) {
            attron(COLOR_PAIR(3) | A_BOLD);
            mvprintw(y++, 4, "--- NAVIGATION MENU ---");
            attroff(A_BOLD);
            mvprintw(y++, 4, "Use ARROWS to select and ENTER to jump. Press 'm' to return here.");
            y++;
            int start_y = y;
            for (int i = 1; i < MAX_PAGES; i++) {
                if (pages[i].name == NULL) continue;
                int col = (i - 1) / 22;
                int row = (i - 1) % 22;
                int x_pos = 4 + (col * 42);
                if (i == menu_selection) {
                    attron(COLOR_PAIR(1) | A_REVERSE);
                    mvprintw(start_y + row, x_pos, " > %-36.36s ", pages[i].name);
                    attroff(COLOR_PAIR(1) | A_REVERSE);
                } else {
                    mvprintw(start_y + row, x_pos, "   %-36.36s ", pages[i].name);
                }
            }
        } else {
            attron(A_BOLD | COLOR_PAIR(5));
            mvprintw(y++, 2, "%-6s | %-20s | %-15s | %-12s | %-12s | %-4s | %-12s", "ID", "NAME", "TYPE/INFO", "QUADRANT", "SECTOR", "INTEG", "ENERGY/EXTRA");
            mvhline(y++, 0, ACS_HLINE, COLS); attroff(A_BOLD);

            for (int i = 0; i < max_visible_lines; i++) {
                int idx = scroll_offset + i; if (idx >= total_items) break;
                RenderItem item = render_list[idx];
                int cp = 5;
                char sid[32], name[64], info[64], qstr[64], sstr[128], integ[32], extra[64];
                strcpy(integ, "100%"); strcpy(extra, "-");

                switch(item.type) {
                    case 0: { ConnectedPlayer* p = &pl_buf[item.index]; cp=1; snprintf(sid, sizeof(sid), "P%02d", item.index+1); snprintf(name, sizeof(name), "%s", p->name); snprintf(info, sizeof(info), "%s", get_species_name(p->faction)); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", p->state.q1, p->state.q2, p->state.q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", p->state.s1, p->state.s2, p->state.s3); snprintf(integ, sizeof(integ), "%3d%%", (int)p->state.hull_integrity); snprintf(extra, sizeof(extra), "%" PRIu64, p->state.energy); } break;
                    case 1: { NPCShip* n = &npc_buf[item.index]; cp=(n->faction==FACTION_KORTHIAN)?4:3; snprintf(sid, sizeof(sid), "N%04d", n->id); snprintf(name, sizeof(name), "%s", n->name); snprintf(info, sizeof(info), "%s", get_species_name(n->faction)); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", n->q1, n->q2, n->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", n->x, n->y, n->z); snprintf(integ, sizeof(integ), "%3d%%", (int)n->health); snprintf(extra, sizeof(extra), "%" PRIu64, n->energy); } break;
                    case 2: { NPCStar* s = &star_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "S%04d", s->id); snprintf(name, sizeof(name), "Star"); snprintf(info, sizeof(info), "Spectral %d", s->faction); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 3: { NPCPlanet* p = &planet_buf[item.index]; cp=2; snprintf(sid, sizeof(sid), "PL%04d", p->id); snprintf(name, sizeof(name), "Planet"); snprintf(info, sizeof(info), "Res:%d", p->resource_type); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", p->q1, p->q2, p->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", p->x, p->y, p->z); snprintf(extra, sizeof(extra), "Amt:%d", p->amount); } break;
                    case 4: { NPCBase* b = &base_buf[item.index]; cp=1; snprintf(sid, sizeof(sid), "B%04d", b->id); snprintf(name, sizeof(name), "%s Base", get_species_name(b->faction)); snprintf(info, sizeof(info), "Faction %d", b->faction); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", b->q1, b->q2, b->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", b->x, b->y, b->z); snprintf(integ, sizeof(integ), "%d", b->health); } break;
                    case 5: { NPCBlackHole* b = &bh_buf[item.index]; cp=4; snprintf(sid, sizeof(sid), "BH%04d", b->id); snprintf(name, sizeof(name), "Black Hole"); snprintf(info, sizeof(info), "Singularity"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", b->q1, b->q2, b->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", b->x, b->y, b->z); } break;
                    case 6: { NPCNebula* n = &neb_buf[item.index]; cp=5; snprintf(sid, sizeof(sid), "NEB%04d", n->id); snprintf(name, sizeof(name), "Nebula"); snprintf(info, sizeof(info), "Type:%d", n->type); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", n->q1, n->q2, n->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", n->x, n->y, n->z); } break;
                    case 7: { NPCPulsar* p = &pul_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "PUL%04d", p->id); snprintf(name, sizeof(name), "Pulsar"); snprintf(info, sizeof(info), "Type:%d", p->type); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", p->q1, p->q2, p->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", p->x, p->y, p->z); } break;
                    case 8: { NPCQuasar* q = &qua_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "QSR%04d", q->id); snprintf(name, sizeof(name), "Quasar"); snprintf(info, sizeof(info), "Type:%d", q->type); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", q->q1, q->q2, q->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", q->x, q->y, q->z); } break;
                    case 9: { NPCComet* c = &com_buf[item.index]; cp=2; snprintf(sid, sizeof(sid), "COM%04d", c->id); snprintf(name, sizeof(name), "Comet"); snprintf(info, sizeof(info), "Moving"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", c->q1, c->q2, c->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", c->x, c->y, c->z); } break;
                    case 10: { NPCAsteroid* a = &ast_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "AST%04d", a->id); snprintf(name, sizeof(name), "Asteroid"); snprintf(info, sizeof(info), "Res:%d", a->resource_type); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", a->q1, a->q2, a->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", a->x, a->y, a->z); snprintf(extra, sizeof(extra), "Amt:%d", a->amount); } break;
                    case 11: { NPCDerelict* d = &der_buf[item.index]; cp=5; snprintf(sid, sizeof(sid), "DE%04d", d->id); snprintf(name, sizeof(name), "%s", d->name); snprintf(info, sizeof(info), "Class %d", d->ship_class); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", d->q1, d->q2, d->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", d->x, d->y, d->z); } break;
                    case 12: { NPCMine* m = &mine_buf[item.index]; cp=4; snprintf(sid, sizeof(sid), "MIN%04d", m->id); snprintf(name, sizeof(name), "Mine"); snprintf(info, sizeof(info), "Faction %d", m->faction); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", m->q1, m->q2, m->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", m->x, m->y, m->z); } break;
                    case 13: { NPCBuoy* b = &buoy_buf[item.index]; cp=2; snprintf(sid, sizeof(sid), "BUY%04d", b->id); snprintf(name, sizeof(name), "Comm Buoy"); snprintf(info, sizeof(info), "Active"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", b->q1, b->q2, b->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", b->x, b->y, b->z); } break;
                    case 14: { NPCPlatform* p = &plat_buf[item.index]; cp=4; snprintf(sid, sizeof(sid), "PF%04d", p->id); snprintf(name, sizeof(name), "Platform"); snprintf(info, sizeof(info), "Fac:%d", p->faction); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", p->q1, p->q2, p->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", p->x, p->y, p->z); snprintf(integ, sizeof(integ), "%d", p->health); snprintf(extra, sizeof(extra), "E:%" PRIu64, p->energy); } break;
                    case 15: { NPCRift* r = &rift_buf[item.index]; cp=2; snprintf(sid, sizeof(sid), "RIF%04d", r->id); snprintf(name, sizeof(name), "Spatial Rift"); snprintf(info, sizeof(info), "Active"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", r->q1, r->q2, r->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", r->x, r->y, r->z); } break;
                    case 16: { NPCMonster* m = &mon_buf[item.index]; cp=4; snprintf(sid, sizeof(sid), "M%02d", m->id); snprintf(name, sizeof(name), (m->type==30)?"Crystalline":"Amoeba"); snprintf(info, sizeof(info), "OMEGA"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", m->q1, m->q2, m->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", m->x, m->y, m->z); snprintf(integ, sizeof(integ), "%d", m->health); } break;
                    case 17: { NPCDyson* d = &dys_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "DY%04d", d->id); snprintf(name, sizeof(name), "Dyson Frag"); snprintf(info, sizeof(info), "Ancient"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", d->q1, d->q2, d->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", d->x, d->y, d->z); } break;
                    case 18: { NPCHub* h = &hub_buf[item.index]; cp=2; snprintf(sid, sizeof(sid), "HB%04d", h->id); snprintf(name, sizeof(name), "Trading Hub"); snprintf(info, sizeof(info), "Neutral"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", h->q1, h->q2, h->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", h->x, h->y, h->z); } break;
                    case 19: { NPCRelic* r = &rel_buf[item.index]; cp=2; snprintf(sid, sizeof(sid), "RE%04d", r->id); snprintf(name, sizeof(name), "Ancient Relic"); snprintf(info, sizeof(info), "Tech"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", r->q1, r->q2, r->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", r->x, r->y, r->z); } break;
                    case 20: { NPCRupture* r = &rup_buf[item.index]; cp=4; snprintf(sid, sizeof(sid), "RU%04d", r->id); snprintf(name, sizeof(name), "Subspace Rup"); snprintf(info, sizeof(info), "Anomaly"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", r->q1, r->q2, r->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", r->x, r->y, r->z); } break;
                    case 21: { NPCSatellite* s = &sat_buf[item.index]; cp=5; snprintf(sid, sizeof(sid), "SA%04d", s->id); snprintf(name, sizeof(name), "Satellite"); snprintf(info, sizeof(info), "Relay"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 22: { NPCStorm* s = &sto_buf[item.index]; cp=5; snprintf(sid, sizeof(sid), "SO%04d", s->id); snprintf(name, sizeof(name), "Ion Storm"); snprintf(info, sizeof(info), "Meteo"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 23: { PlayerTorpedo* t = &torp_buf[item.index]; cp=4; snprintf(sid, sizeof(sid), "T%04d", t->id); snprintf(name, sizeof(name), "Torpedo"); snprintf(info, sizeof(info), "Owner %d", t->owner_idx); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", t->q1, t->q2, t->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", t->x, t->y, t->z); snprintf(extra, sizeof(extra), "TO:%d", t->timeout); } break;
                    case 30: { NPCArtifact* a = &art_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "AA%04d", a->id); snprintf(name, sizeof(name), "Alien Artifact"); snprintf(info, sizeof(info), "Exotic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", a->q1, a->q2, a->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", a->x, a->y, a->z); } break;
                    case 31: { NPCWarpGate* w = &war_buf[item.index]; cp=2; snprintf(sid, sizeof(sid), "WG%04d", w->id); snprintf(name, sizeof(name), "Warp Gate"); snprintf(info, sizeof(info), "Active"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", w->q1, w->q2, w->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", w->x, w->y, w->z); } break;
                    case 32: { NPCNeutronStar* n = &neu_buf[item.index]; cp=5; snprintf(sid, sizeof(sid), "NS%04d", n->id); snprintf(name, sizeof(name), "Neutron Star"); snprintf(info, sizeof(info), "Degenerate"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", n->q1, n->q2, n->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", n->x, n->y, n->z); } break;
                    case 33: { NPCMegaStructure* m = &meg_buf[item.index]; cp=1; snprintf(sid, sizeof(sid), "MS%04d", m->id); snprintf(name, sizeof(name), "Mega Struct"); snprintf(info, sizeof(info), "Unknown"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", m->q1, m->q2, m->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", m->x, m->y, m->z); } break;
                    case 34: { NPCDarkCloud* d = &dar_buf[item.index]; cp=4; snprintf(sid, sizeof(sid), "DC%04d", d->id); snprintf(name, sizeof(name), "Dark Matter"); snprintf(info, sizeof(info), "Obscured"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", d->q1, d->q2, d->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", d->x, d->y, d->z); } break;
                    case 35: { NPCSingularity* s = &sin_buf[item.index]; cp=4; snprintf(sid, sizeof(sid), "QS%04d", s->id); snprintf(name, sizeof(name), "Singularity"); snprintf(info, sizeof(info), "Quantum"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 36: { NPCPlasmaStorm* p = &pla_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "PS%04d", p->id); snprintf(name, sizeof(name), "Plasma Storm"); snprintf(info, sizeof(info), "Unstable"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", p->q1, p->q2, p->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", p->x, p->y, p->z); } break;
                    case 37: { NPCOrbitalRing* o = &orb_buf[item.index]; cp=2; snprintf(sid, sizeof(sid), "OR%04d", o->id); snprintf(name, sizeof(name), "Orbital Ring"); snprintf(info, sizeof(info), "Planetary"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", o->q1, o->q2, o->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", o->x, o->y, o->z); } break;
                    case 38: { NPCTimeAnomaly* t = &tim_buf[item.index]; cp=1; snprintf(sid, sizeof(sid), "TA%04d", t->id); snprintf(name, sizeof(name), "Time Anomaly"); snprintf(info, sizeof(info), "Temporal"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", t->q1, t->q2, t->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", t->x, t->y, t->z); } break;
                    case 39: { NPCVoidCrystal* v = &voi_buf[item.index]; cp=5; snprintf(sid, sizeof(sid), "VC%04d", v->id); snprintf(name, sizeof(name), "Void Crystal"); snprintf(info, sizeof(info), "Crystalline"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", v->q1, v->q2, v->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", v->x, v->y, v->z); } break;
                    case 50: { NPCSubspaceAnomaly* s = &sub_buf[item.index]; cp=2; snprintf(sid, sizeof(sid), "AN%04d", s->id); snprintf(name, sizeof(name), "Subspace Anom"); snprintf(info, sizeof(info), "Unstable"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 51: { NPCDiffuseNebula* s = &dif_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "DI%04d", s->id); snprintf(name, sizeof(name), "Diffuse Nebula"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 52: { NPCDarkNebula* s = &dar2_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "DA%04d", s->id); snprintf(name, sizeof(name), "Dark Nebula"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 53: { NPCPlanetaryNebula* s = &pla2_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "PL%04d", s->id); snprintf(name, sizeof(name), "Planet Neb"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 54: { NPCSNR* s = &snr_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "SN%04d", s->id); snprintf(name, sizeof(name), "SNR"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 55: { NPCGMC* s = &gmc_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "GM%04d", s->id); snprintf(name, sizeof(name), "GMC"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 56: { NPCInterstellarFilament* s = &inf_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "IN%04d", s->id); snprintf(name, sizeof(name), "Int Filament"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 57: { NPCInterstellarBubble* s = &inb_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "IN%04d", s->id); snprintf(name, sizeof(name), "Int Bubble"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 58: { NPCBokGlobule* s = &bok_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "BO%04d", s->id); snprintf(name, sizeof(name), "Bok Globule"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 59: { NPCClumpCore* s = &clc_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "CL%04d", s->id); snprintf(name, sizeof(name), "Clump/Core"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 60: { NPCAccretionDisk* s = &acd_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "AC%04d", s->id); snprintf(name, sizeof(name), "Accret Disk"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 61: { NPCRelativisticJet* s = &rej_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "RE%04d", s->id); snprintf(name, sizeof(name), "Relativ Jet"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 62: { NPCShockWave* s = &shw_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "SH%04d", s->id); snprintf(name, sizeof(name), "Shock Wave"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 63: { NPCStellarBowShock* s = &sbs_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "ST%04d", s->id); snprintf(name, sizeof(name), "Bow Shock"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 64: { NPCCosmicVoid* s = &cov_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "CO%04d", s->id); snprintf(name, sizeof(name), "Cosmic Void"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 65: { NPCCosmicFilament* s = &cof_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "CO%04d", s->id); snprintf(name, sizeof(name), "Cosmic Fil"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 66: { NPCEventHorizon* s = &evh_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "EV%04d", s->id); snprintf(name, sizeof(name), "Event Horiz"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 67: { NPCKilonova* s = &kil_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "KI%04d", s->id); snprintf(name, sizeof(name), "Kilonova"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 68: { NPCGravLens* s = &grl_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "GR%04d", s->id); snprintf(name, sizeof(name), "Grav Lens"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 69: { NPCGRB* s = &grb_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "GR%04d", s->id); snprintf(name, sizeof(name), "GRB"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 70: { NPCGravWave* s = &grw_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "GR%04d", s->id); snprintf(name, sizeof(name), "Grav Wave"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 71: { NPCProtoplanetaryDisk* s = &ppd_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "PR%04d", s->id); snprintf(name, sizeof(name), "Protoplanet"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 72: { NPCDebrisDisk* s = &ded_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "DE%04d", s->id); snprintf(name, sizeof(name), "Debris Disk"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 73: { NPCPlanetesimal* s = &ptm_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "PL%04d", s->id); snprintf(name, sizeof(name), "Planetesimal"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 74: { NPCRoguePlanet* s = &rop_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "RO%04d", s->id); snprintf(name, sizeof(name), "Rogue Planet"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 75: { NPCBrownDwarf* s = &brd_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "BR%04d", s->id); snprintf(name, sizeof(name), "Brown Dwarf"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 76: { NPCISO* s = &iso_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "IS%04d", s->id); snprintf(name, sizeof(name), "Interst Obj"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 77: { NPCMagReconn* s = &mgr_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "MA%04d", s->id); snprintf(name, sizeof(name), "Mag Reconn"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 78: { NPCCurrentSheet* s = &cus_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "CU%04d", s->id); snprintf(name, sizeof(name), "Cur Sheet"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 79: { NPCHeliosphere* s = &hel_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "HE%04d", s->id); snprintf(name, sizeof(name), "Heliosphere"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 80: { NPCTermShock* s = &tes_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "TE%04d", s->id); snprintf(name, sizeof(name), "Term Shock"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 81: { NPCMagnetosphere* s = &mgs_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "MA%04d", s->id); snprintf(name, sizeof(name), "Magnetosph"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 82: { NPCCosmicString* s = &cst_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "CO%04d", s->id); snprintf(name, sizeof(name), "Cosm String"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 83: { NPCDomainWall* s = &dow_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "DO%04d", s->id); snprintf(name, sizeof(name), "Domain Wall"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 84: { NPCDMHalo* s = &dmh_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "DM%04d", s->id); snprintf(name, sizeof(name), "DM Halo"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 85: { NPCIGM* s = &igm_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "IG%04d", s->id); snprintf(name, sizeof(name), "IGM"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 86: { NPCCGM* s = &cgm_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "CG%04d", s->id); snprintf(name, sizeof(name), "CGM"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 87: { NPCLymanAlpha* s = &lya_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "LY%04d", s->id); snprintf(name, sizeof(name), "Lyman Alpha"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
                    case 88: { NPCCMB* s = &cmb_buf[item.index]; cp=3; snprintf(sid, sizeof(sid), "CM%04d", s->id); snprintf(name, sizeof(name), "CMB"); snprintf(info, sizeof(info), "Cosmic"); snprintf(qstr, sizeof(qstr), "[%2d,%2d,%2d]", s->q1, s->q2, s->q3); snprintf(sstr, sizeof(sstr), "%5.1f,%5.1f,%5.1f", s->x, s->y, s->z); } break;
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
