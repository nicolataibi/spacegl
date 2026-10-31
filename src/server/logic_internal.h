/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * logic_internal.h — functions shared by the game-logic translation units
 * (2026.10.31.17): logic.c (tick, player movement and collisions) and
 * logic_npc.c (NPC behaviour). Private to src/server/logic*.c.
 */
#ifndef LOGIC_INTERNAL_H
#define LOGIC_INTERNAL_H

#include "obstacles.h"

/* A comet hit by a ship breaks up: its debris and the event for the
 * viewers. Used by the NPC and the player collision checks. */
void comet_destroyed_by_ship(const ObsHit *hit);

#endif
