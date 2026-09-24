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
 * Navigation heading math.
 *
 * Pure functions extracted from the alignment interpolation in
 * logic.c (NAV_STATE_ALIGN / NAV_STATE_ALIGN_IMPULSE and
 * NAV_STATE_ALIGN_ONLY).  Historically that interpolation was
 *
 *     van_h = start_h + diff_h * t        (diff_h wrapped to ±180°)
 *
 * without re-wrapping the result, so a turn crossing the 0/360
 * boundary (e.g. 350° -> 30°) drove van_h outside [0, 360) — 390° at
 * t = 1, or negative values turning the other way — and the clients
 * displayed it as-is in the HUD heading field (B2).  The
 * interpolation now normalizes the result back into [0, 360); since
 * that is the same angle, the ship motion vectors (cos/sin of the
 * heading) are unchanged.
 *
 * Dependency-free (only <math.h>) on purpose: the independent test
 * project (tests/) compiles this TU directly, so the regression test
 * exercises the real production formula.
 */

#include "nav_math.h"

double nav_wrap_heading(double h) {
    h = fmod(h, 360.0);
    if (h < 0.0) h += 360.0;
    return h;
}

double nav_heading_at(double start_h, double target_h, double t) {
    /* Shortest-arc delta: exactly the while-loop the server used
     * before the extraction (d ends up in [-180, 180] when both
     * headings are in [0, 360)). */
    double d = target_h - start_h;
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    return nav_wrap_heading(start_h + d * t);
}
