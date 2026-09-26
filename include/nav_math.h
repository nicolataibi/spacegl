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

/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * Navigation heading math (pure functions, no project state).
 *
 * The alignment interpolation in logic.c (NAV_STATE_ALIGN /
 * NAV_STATE_ALIGN_IMPULSE and NAV_STATE_ALIGN_ONLY) interpolates the
 * player heading between start_h and target_h. Both inputs are always
 * in [0, 360) (normalize_upright / atan2 at the command handlers),
 * and the result must be re-wrapped into [0, 360) so the clients can
 * display the heading as-is (the HUD navigation field shows whatever
 * the server sends — B2).
 */

#ifndef NAV_MATH_H
#define NAV_MATH_H

#include <math.h>

/* Wrap an absolute heading (degrees) into [0, 360). */
double nav_wrap_heading(double h);

/* Shortest-arc linear interpolation between two absolute headings
 * (both in [0, 360)) at fraction t (0 = start_h, 1 = target_h).
 * The result is re-wrapped into [0, 360): a turn crossing the
 * 0/360 boundary (e.g. 350 deg -> 30 deg) passes through 0 instead of
 * leaving the range (390 deg). The wrap is display-only — it is the
 * same angle, so the ship motion vectors (cos/sin of the heading) are
 * unchanged. */
double nav_heading_at(double start_h, double target_h, double t);

/* Shortest-arc linear interpolation between two absolute roll angles
 * (both in [0, 360)) at fraction t (0 = start_r, 1 = target_r).
 * Same contract as nav_heading_at: the result is re-wrapped into
 * [0, 360), so a roll turn crossing the 0/360 boundary (e.g. 350 deg
 * -> 20 deg) passes through 0 instead of leaving the range (370 deg).
 * The wrap is display-only — it is the same angle, so the consumers
 * that use roll through sin/cos (the shield basis in
 * calculate_shield_index, the 3D views) are unchanged. Out-of-range
 * inputs are handled modulo 360 as well. */
double nav_roll_at(double start_r, double target_r, double t);

#endif /* NAV_MATH_H */
