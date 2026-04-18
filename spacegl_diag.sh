#!/usr/bin/bash
# SpaceGL Diagnostic Launcher
# Launches the scanner in a dedicated high-resolution terminal
# Space GL - Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# Professional Startup Script

# 1. Find Server PID (exclude script, find binary)
# Using -x to match the executable name exactly
SERVER_PID=$(pgrep -x "spacegl_server" | head -n 1)

if [ -z "$SERVER_PID" ]; then
    echo "Error: SpaceGL Server process not found."
    echo "Ensure spacegl_server is running before launching diagnostics."
    exit 1
fi

echo "Targeting Server PID: $SERVER_PID"

# 2. Determine which diagnostic tool to use
# The legacy diag_shm has been removed, using spacegl_diag
if [ -f "./build/spacegl_diag" ]; then
    DIAG_BIN="./build/spacegl_diag"
elif [ -f "./spacegl_diag" ]; then
    DIAG_BIN="./spacegl_diag"
else
    DIAG_BIN=$(which spacegl_diag 2>/dev/null || echo "/usr/bin/spacegl_diag")
fi

if [ ! -x "$DIAG_BIN" ]; then
    echo "Error: Diagnostic binary not found or not executable: $DIAG_BIN"
    exit 1
fi

# 3. Launch XTerm
# Added a trap/read to keep the window open if the binary crashes or exits with error
echo "Launching $DIAG_BIN..."
xterm -geometry 160x50 \
      -fa 'Monospace' -fs 10 \
      -bg black -fg "#00ff00" \
      -T "SPACE GL - REAL-TIME DIAGNOSTIC [PID: $SERVER_PID]" \
      -e "bash -c '$DIAG_BIN $SERVER_PID || { echo; echo \"Process exited with error. Press Enter to close.\"; read; }'" &
