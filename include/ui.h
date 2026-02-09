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

#ifndef UI_H
#define UI_H

#include <stdio.h>

/* ANSI Color Codes */
#define RESET       "\033[0m"
#define BOLD        "\033[1m"
#define DIM         "\033[2m"
#define ITALIC      "\033[3m"
#define UNDERLINE   "\033[4m"

#define BLACK       "\033[30m"
#define RED         "\033[31m"
#define GREEN       "\033[32m"
#define YELLOW      "\033[33m"
#define BLUE        "\033[34m"
#define MAGENTA     "\033[35m"
#define CYAN        "\033[36m"
#define WHITE       "\033[37m"

#define B_RED       "\033[1;31m"
#define B_GREEN     "\033[1;32m"
#define B_YELLOW    "\033[1;33m"
#define B_BLUE      "\033[1;34m"
#define B_MAGENTA   "\033[1;35m"
#define B_CYAN      "\033[1;36m"
#define B_WHITE     "\033[1;37m"

/* Backgrounds */
#define BG_RED      "\033[41m"
#define BG_BLUE     "\033[44m"
#define BG_BLACK    "\033[40m"

/* Icons */
#define ICON_ENT    "{-E-}"
#define ICON_KLING  "+K+"
#define ICON_BASE   ">B<"
#define ICON_STAR   " * "
#define ICON_EMPTY  "   "

/* Utility Macros */
#define CLS         printf("\033[H\033[J")
#define COLOR_PRINT(c, s) printf("%s%s%s", c, s, RESET)

#endif
