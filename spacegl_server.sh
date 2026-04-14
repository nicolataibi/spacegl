#!/bin/bash
# SPACE GL - GALAXY SERVER BOOTLOADER
# Space GL - Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# Professional Startup Script

# Colors
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
CYAN='\033[1;36m'
NC='\033[0m' # No Color

clear
echo -e "${RED}  ____________________________________________________________________________"
echo -e ' /                                                                            \'
echo -e " | ${CYAN}   ███████╗██████╗  █████╗  ██████╗███████╗     ██████╗ ██╗              ${RED}  |"
echo -e " | ${CYAN}   ██╔════╝██╔══██╗██╔══██╗██╔════╝██╔════╝    ██╔════╝ ██║              ${RED}  |"
echo -e " | ${CYAN}   ███████╗██████╔╝███████║██║     █████╗      ██║  ███╗██║              ${RED}  |"
echo -e " | ${CYAN}   ╚════██║██╔═══╝ ██╔══██║██║     ██╔══╝      ██║   ██║██║              ${RED}  |"
echo -e " | ${CYAN}   ███████║██║     ██║  ██║╚██████╗███████╗    ╚██████╔╝███████╗         ${RED}  |"
echo -e " | ${CYAN}   ╚══════╝╚═╝     ╚═╝  ╚═╝ ╚═════╝╚══════╝     ╚═════╝ ╚══════╝         ${RED}  |"
echo -e ' |                                                                            |'
echo -e " | ${RED}         ---  G A L A C T I C   S E R V E R   C O R E  ---                ${RED} |"
echo -e " | ${YELLOW}        \"Per Tenebras, Lumen\" (Through darkness, light)                 ${RED} |"
echo -e ' \____________________________________________________________________________/'"${NC}"
echo ""

# Binary verification
if [ -f "./build/spacegl_server" ]; then
    SPACEGL_BIN="./build/spacegl_server"
elif [ -f "./spacegl_server" ]; then
    SPACEGL_BIN="./spacegl_server"
elif command -v spacegl_server > /dev/null; then
    SPACEGL_BIN="spacegl_server"
else
    echo -e "${RED}[ERROR]${NC} Executable 'spacegl_server' not found."
    exit 1
fi

# Security Management (Master Key)
if [ -z "$SPACEGL_KEY" ]; then
    echo -e "${YELLOW}[SECURITY]${NC} Initializing security protocol..."
    echo -e "${CYAN}[AUTH]${NC} Enter the Master Key for this sector:"
    read -sp "> " key
    echo ""
    export SPACEGL_KEY="$key"
fi

echo -e "${GREEN}[SYSTEM]${NC} Master Key validated. Starting boot sequence..."
echo -e "${BLUE}[INFO]${NC} Server listening on port 5000 (TCP/Binary)"
echo ""

# Server execution
$SPACEGL_BIN "$@"
