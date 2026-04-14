#!/bin/bash
# SpaceGL Diagnostic Launcher
# Launches the scanner in a dedicated high-resolution terminal

# 1. Find Server PID (exclude script, find binary)
SERVER_PID=$(pgrep -f "spacegl_server" | grep -v "$$" | head -n 1)

# Check if we found a generic pgrep result, try to filter for the binary if multiple exist
if [ -z "$SERVER_PID" ]; then
    echo "Error: SpaceGL Server not running."
    exit 1
fi

# Ensure we have the binary PID, not the shell wrapper if possible
# (The C program also does internal validation, but better safe here)
REAL_PID=""
for pid in $(pgrep -f "spacegl_server"); do
    if ls -l /proc/$pid/exe 2>/dev/null | grep -q "spacegl_server"; then
        if ! ls -l /proc/$pid/exe 2>/dev/null | grep -q ".sh"; then
            REAL_PID=$pid
            break
        fi
    fi
done

if [ -z "$REAL_PID" ]; then
    # Fallback to whatever pgrep found if we can't inspect /proc (e.g. permission issues)
    REAL_PID=$SERVER_PID
fi

echo "Targeting Server PID: $REAL_PID"

# 2. Find Diagnostic Binary and determine if we need offsets
DIAG_BIN=""
OFFSETS=""

if [ -f "./build/spacegl_diag" ]; then
    DIAG_BIN="./build/spacegl_diag"
elif [ -f "./spacegl_diag" ]; then
    DIAG_BIN="./spacegl_diag"
else
    DIAG_BIN="spacegl_diag"
fi

# If it's a system installation (likely stripped), add offsets
if [[ "$DIAG_BIN" == "spacegl_diag" ]]; then
    OFFSETS="-t 0x47000 -n 0xfc2180 -p 0x120d80"
fi

# 3. Launch XTerm
xterm -geometry 160x50 \
      -fa 'Monospace' -fs 10 \
      -bg black -fg "#00ff00" \
      -T "SPACE GL - GALACTIC OVERWATCH [LIVE]" \
      -e "$DIAG_BIN $OFFSETS $REAL_PID" &
