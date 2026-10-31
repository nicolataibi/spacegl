/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * galaxy_names.c — species names and random ship names per faction.
 * Split from galaxy.c (2026.10.31.16) without code change.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <stdatomic.h>
#include <pthread.h>
#include "server_internal.h"
#include "object_types.h"
#include "quad_index.h"
#include "moon_orbits.h"
#include "ui.h"

const char* get_species_name(int s) {
    switch(s) {
        case FACTION_ALLIANCE: return "Alliance"; 
        case FACTION_KORTHIAN:    return "Korthian"; 
        case FACTION_XYLARI:    return "Xylari"; 
        case FACTION_SWARM:       return "Swarm";
        case FACTION_VESPERIAN: return "Vesperian"; 
        case FACTION_JEM_HADAR:  return "Ascendant"; 
        case FACTION_THOLIAN:    return "Quarzite";
        case FACTION_GORN:       return "Saurian"; 
        case FACTION_GILDED:    return "Gilded"; 
        case FACTION_SPECIES_8472: return "Fluidic Void";
        case FACTION_BREEN:      return "Cryos"; 
        case FACTION_HIROGEN:    return "Apex";
        default: {
            /* Non-faction values echo the object type (object_types.h):
             * the old hand-written table here used a numbering that no
             * longer matched the real types (28 = Dyson Fragment, ...). */
            const char *n = obj_type_name(s);
            return n ? n : "Unknown";
        }
    }
}

static const char* ALLIANCE_NAMES[] = {"Enterprise", "Defiant", "Voyager", "Discovery", "Stargazer", "Reliant", "Saratoga", "Yorktown", "Intrepid", "Constellation", "Excelsior", "Hood", "Potemkin", "Victory", "Valiant", "Thunderchild", "Yeager", "Appalachia", "Bellerophon", "Budapest", "Curry", "Grissom", "Hathaway", "Kyushu", "Melbourne", "New Orleans", "Niagara", "Oberth", "Prometheus", "Rutledge", "Ticonderoga", "Zodiac", "Endeavour", "Ahwahnee", "Chekov", "Clement", "Drake", "Equinox", "Farragut", "Gettysburg", "Horatio", "Kearsarge", "Lantree", "Maryland", "Nautilus", "Pasteur", "Quark", "Renegade", "Solstice", "Trieste", "Universal", "Venture", "Whorfin", "Xerxes", "Yamato", "Zapata", "Aries", "Bismarck", "Cairo", "Dante", "Excalibur", "Fearless", "Gallant", "Helsinki", "Iwo Jima", "Justin", "Kongo", "Lexington", "Musashi", "Nova", "Olympic", "Phoenix", "Repulse", "Sutherland", "Tian An Men", "Ulysses", "Valley Forge", "Washington", "Ajax", "Berlin", "Centaur", "Dreadnought", "Enterprise-A", "Freedom", "Grafton", "Honshu", "Independence", "Janus", "Korolev", "Lakota", "Majestic", "Nebula", "Orion", "Peregrine", "Rigel", "Surak", "Thunderbolt", "Unity", "Vesta", "Wyoming"};
static const char* KORTHIAN_NAMES[] = {"Gorkon", "Negh'Var", "Vor'cha", "K'mpec", "Kronos One", "B'Moth", "Ch'Tang", "D'Kyr", "Gr'oth", "Hegh'ta", "IKS Bortas", "IKS Pagh", "IKS Rotarran", "IKS Wo'r'iv", "K'Ehleyr", "M'Char", "Ni'Var", "Pagh'tem-far", "Qu'Vat", "Somraw", "T'Ong", "Vorn", "Ya'Vang", "Zapola", "Amar", "Buruk", "D'k Tahg", "Fek'lhr", "Gron", "H'chak", "I'dan", "J'Dan", "K'lar", "L'Kor", "Maht-H'a", "N'Garen", "O'Tang", "P'Trell", "Q'Mok", "R'Ur", "S'Vak", "T'Kumbra", "U'Gak", "V'Rek", "W'Tang", "X'Kor", "Y'Dan", "Z'Kor", "K'Vort", "B'rel"};
static const char* XYLARI_NAMES[] = {"Gal Gath'thong", "Haakona", "Khazara", "Decius", "Belak", "Makar", "T'Vran", "Vahladore", "Apnex", "Bortas", "Centurion", "D'deridex", "Elite", "Firehawk", "Goroth", "H'ros", "Icarus", "Jae'rod", "K'Vort", "L'Vak", "Mogai", "Norexan", "O'Ran", "P'Vak", "Qu'Vat", "R'Vak", "S'Vak", "T'Vak", "U'Vak", "V'Vak", "W'Vak", "X'Vak", "Y'Vak", "Z'Vak", "Shadow One", "Nightshade", "Black Talon", "Void Stalker"};
static const char* SWARM_NAMES[] = {"Cube 01", "Sphere 22", "Diamond 4", "Tactical Cube 13", "Probe 9", "Queen's Vessel", "Unimatrice 0", "Unimatrice 1", "Node Alpha", "Node Beta", "Interceptor 5", "Scout 8", "Assimilation Unit", "Regeneration Hub", "Hive Ship", "Matrix One"};
static const char* VESPERIAN_NAMES[] = {"Trager", "Vetar", "Groumall", "Cuirass", "Aldara", "Bok'Nor", "Ch'Targh", "Dreadnought", "Ekar", "Fek'lhr", "Galor", "Heka", "Indra", "Janissary", "Keldon", "Lektop", "Makar", "Naprem", "Obsidian", "Praetor", "Quark", "Rakal", "Selin", "Tain", "Ulave", "Vrax", "Warlock", "Xenon", "Yarin", "Zarth"};
static const char* ASCENDANT_NAMES[] = {"Vorta's Command", "Jem'Hadar Scout", "Dominion Heavy", "Founder's Guard", "Gamma Attack", "Alpha Strike", "Beta Patrol", "Ketracel's Hope", "White Guard", "Shapeshifter"};
static const char* QUARZITE_NAMES[] = {"Crystal Web", "Prism", "Refraction", "Net Weaver", "Grid Guardian", "Spectrum", "Geometry", "Lattice", "Crystalline", "Array"};
static const char* SAURIAN_NAMES[] = {"Hunter", "Stalker", "Lizard", "Cold Blood", "Desert Fang", "Sand King", "Apex Predator", "Tail Strike", "Green Claw", "Scale"};
static const char* GILDED_NAMES[] = {"Gold Pressed", "Latinum Dream", "Profit Margin", "Rule of Acquisition", "Grand Nagus", "Marauder", "Trade Wind", "Market Force", "Credit", "Greed"};
static const char* FLUIDIC_NAMES[] = {"Bio-Ship 1", "Organism", "Cellular", "Enzyme", "Protector", "Void Swimmer", "Fluidic Alpha", "Genetic", "Mutant", "Anomaly"};
static const char* CRYOS_NAMES[] = {"Freezer", "Cold Death", "Zero Kelvin", "Ice Stalker", "Snow Blind", "Blizzard", "Glacier", "Frost Bite", "Hibernate", "Sub-Zero"};
static const char* APEX_NAMES[] = {"Hirogen Alpha", "Great Hunter", "Trophy Room", "Predator", "Prey Finder", "Tracking", "Kill Stroke", "Honor Bound", "Spirit", "Ancient"};

const char* get_random_ship_name(int faction) {
    const char** list = NULL;
    int size = 0;
    switch(faction) {
        case FACTION_ALLIANCE: list = ALLIANCE_NAMES; size = sizeof(ALLIANCE_NAMES)/sizeof(char*); break;
        case FACTION_KORTHIAN: list = KORTHIAN_NAMES; size = sizeof(KORTHIAN_NAMES)/sizeof(char*); break;
        case FACTION_XYLARI: list = XYLARI_NAMES; size = sizeof(XYLARI_NAMES)/sizeof(char*); break;
        case FACTION_SWARM: list = SWARM_NAMES; size = sizeof(SWARM_NAMES)/sizeof(char*); break;
        case FACTION_VESPERIAN: list = VESPERIAN_NAMES; size = sizeof(VESPERIAN_NAMES)/sizeof(char*); break;
        case FACTION_JEM_HADAR: list = ASCENDANT_NAMES; size = sizeof(ASCENDANT_NAMES)/sizeof(char*); break;
        case FACTION_THOLIAN: list = QUARZITE_NAMES; size = sizeof(QUARZITE_NAMES)/sizeof(char*); break;
        case FACTION_GORN: list = SAURIAN_NAMES; size = sizeof(SAURIAN_NAMES)/sizeof(char*); break;
        case FACTION_GILDED: list = GILDED_NAMES; size = sizeof(GILDED_NAMES)/sizeof(char*); break;
        case FACTION_SPECIES_8472: list = FLUIDIC_NAMES; size = sizeof(FLUIDIC_NAMES)/sizeof(char*); break;
        case FACTION_BREEN: list = CRYOS_NAMES; size = sizeof(CRYOS_NAMES)/sizeof(char*); break;
        case FACTION_HIROGEN: list = APEX_NAMES; size = sizeof(APEX_NAMES)/sizeof(char*); break;
        default: return "Unknown Vessel";
    }
    return list[rand() % size];
}
