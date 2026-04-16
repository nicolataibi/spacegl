#!/usr/bin/bash
# SpaceGL Diagnostic Launcher
# Launches the scanner in a dedicated high-resolution terminal

# 1. Find Server PID (exclude script, find binary)
SERVER_PID=$(pgrep -f "spacegl_server" | grep -v "$$" | head -n 1)

if [ -z "$SERVER_PID" ]; then
    echo "Error: SpaceGL Server not running."
    exit 1
fi

echo "Targeting Server PID: $SERVER_PID"

# 2. Determine which diagnostic tool to use
# Prefer the new SHM tool if available, otherwise fallback to legacy process reader
if [ -f "./build/spacegl_diag_shm" ]; then
    DIAG_BIN="./build/spacegl_diag_shm"
elif [ -f "./spacegl_diag_shm" ]; then
    DIAG_BIN="./spacegl_diag_shm"
elif [ -f "/usr/bin/spacegl_diag_shm" ]; then
    DIAG_BIN="/usr/bin/spacegl_diag_shm"
elif [ -f "./build/spacegl_diag" ]; then
    DIAG_BIN="./build/spacegl_diag"
elif [ -f "./spacegl_diag" ]; then
    DIAG_BIN="./spacegl_diag"
else
    DIAG_BIN="spacegl_diag"
fi

# 3. Launch XTerm
# Note: spacegl_diag_shm does not require PID or offsets.
if [[ "$DIAG_BIN" == *"diag_shm"* ]]; then
    xterm -geometry 100x30 \
          -fa 'Monospace' -fs 10 \
          -bg black -fg "#00ff00" \
          -T "SPACE GL - SHM OVERWATCH [LIVE]" \
          -e "$DIAG_BIN" &
else
    # Fallback legacy launch
    xterm -geometry 160x50 \
          -fa 'Monospace' -fs 10 \
          -bg black -fg "#00ff00" \
          -T "SPACE GL - LEGACY SCANNER [LIVE]" \
          -e "$DIAG_BIN $SERVER_PID" &
fi
