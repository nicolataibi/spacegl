#!/bin/bash
# SPACE GL - TACTICAL BRIDGE INTERFACE
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
echo -e " | ${RED}         ---  G A L A C T I C   C L I E N T   C O R E  ---                ${RED} |"
echo -e " | ${YELLOW}          \"Per Tenebras, Lumen\" (Through darkness, light)                 ${RED} |"
echo -e ' \____________________________________________________________________________/'"${NC}"
echo ""

GREEN='\033[0;32m'
NC='\033[0m'
# Binary verification
if [ -f "./build/spacegl_client" ]; then
    SPACEGL_BIN="./build/spacegl_client"
elif [ -f "./spacegl_client" ]; then
    SPACEGL_BIN="./spacegl_client"
elif command -v spacegl_client > /dev/null; then
    SPACEGL_BIN="spacegl_client"
else
    echo -e "${RED}[ERROR]${NC} Executable 'spacegl_client' not found."
    exit 1
fi

if [ -z "$SPACEGL_KEY" ]; then
    echo -n "Enter Server Master Key: "
    read -sp "> " key
    echo ""
    export SPACEGL_KEY="$key"
fi

# Pass all arguments (like gl or vk) directly to the client
$SPACEGL_BIN "$@"
