#!/bin/bash
# SPACE GL - TACTICAL BRIDGE INTERFACE
# Space GL - Copyright (C) 2026 Nicola Taibi

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
