#!/bin/bash
# SPACE GL - TACTICAL BRIDGE INTERFACE
# Space GL - Copyright (C) 2026 Nicola Taibi

GREEN='\033[0;32m'
NC='\033[0m'
SPACEGL_BIN="./spacegl_client"

if [ -z "$SPACEGL_KEY" ]; then
    echo -n "Inserire Master Key del Server: "
    read -sp "> " key
    echo ""
    export SPACEGL_KEY="$key"
fi

# Pass all arguments (like gl or vk) directly to the client
$SPACEGL_BIN "$@"
