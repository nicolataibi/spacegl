#!/bin/bash
# SPACE GL - TACTICAL BRIDGE INTERFACE
# Space GL - Copyright (C) 2026 Nicola Taibi
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# Professional Startup Script

# Colori
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
CYAN='\033[1;36m'
NC='\033[0m' # No Color

clear
echo -e "${CYAN}  ____________________________________________________________________________"
echo -e ' /                                                                            \'
echo -e " | ${BLUE}   ███████╗██████╗  █████╗  ██████╗███████╗     ██████╗ ██╗            ${CYAN}  |"
echo -e " | ${BLUE}   ██╔════╝██╔══██╗██╔══██╗██╔════╝██╔════╝    ██╔════╝ ██║            ${CYAN}  |"
echo -e " | ${BLUE}   ███████╗██████╔╝███████║██║     █████╗      ██║  ███╗██║            ${CYAN}  |"
echo -e " | ${BLUE}   ╚════██║██╔═══╝ ██╔══██║██║     ██╔══╝      ██║   ██║██║            ${CYAN}  |"
echo -e " | ${BLUE}   ███████║██║     ██║  ██║╚██████╗███████╗    ╚██████╔╝███████╗       ${CYAN}  |"
echo -e " | ${BLUE}   ╚══════╝╚═╝     ╚═╝  ╚═╝ ╚═════╝╚══════╝     ╚═════╝ ╚══════╝       ${CYAN}  |"
echo -e ' |                                                                            |'
echo -e " | ${YELLOW}          ---  SPACE EXPLORATION & COMBAT INTERFACE  ---              ${CYAN} |"
echo -e " | ${MAGENTA}             \"Per Tenebras, Lumen\" (Attraverso le tenebre, la luce)          ${CYAN} |"
echo -e ' \____________________________________________________________________________/'"${NC}"
echo ""

# Verifica eseguibile
if [ -f "./spacegl_client" ]; then
    SPACEGL_BIN="./spacegl_client"
elif command -v spacegl_client > /dev/null; then
    SPACEGL_BIN="spacegl_client"
else
    echo -e "${RED}[ERROR]${NC} Interfaccia tattica non trovata."
    echo -e "${YELLOW}[HINT]${NC} Esegui 'make' o installa il pacchetto RPM."
    exit 1
fi

# Gestione Sicurezza (Master Key)
if [ -z "$SPACEGL_KEY" ]; then
    echo -e "${BLUE}[SYSTEM]${NC} Richiesta autorizzazione di sistema..."
    echo -e "${CYAN}[AUTH]${NC} Inserisci la Master Key del Server:"
    read -sp "> " key
    echo ""
    export SPACEGL_KEY="$key"
fi

echo -e "${GREEN}[LINK]${NC} Master Key caricata. Inizializzazione sistemi..."
echo ""

# Esecuzione del client
$SPACEGL_BIN "$@"
