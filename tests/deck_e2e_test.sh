#!/bin/bash
# SPACE GL - 3D LOGIC ENGINE
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# deck_e2e_test.sh — the command deck's own paths, on the REAL server
# (2026.10.31.39, roadmap: deck coverage). smoke_test.sh and
# radio_e2e_test.sh drive the happy path of a session; this script drives
# what they leave out, each step with its own check:
#   1. no server: --help, --version, a host name that does not resolve, a
#      refused connection;
#   2. a deck with another master key is refused at the handshake;
#   3. names the deck rejects before sending them (a station name, a
#      character outside [A-Za-z0-9_-], more than 32 characters);
#   4. one new captain per faction 1..11 (the faction menu; a non-Alliance
#      captain gets no class menu), each logs in and leaves;
#   5. the input line of a session: backspace, an empty line, ESC, the
#      view toggles (axs, grd), tune with no captain, the usage of rad and
#      enc3, enc3 to a captain with no link, every cipher name of enc (the
#      whole chain), enc off, rad to a faction (KORTHIAN receives it, the
#      other factions do not), rad to an unknown faction, rad #ID;
#   6. the same name while that captain is on board is refused; once he
#      left, a wrong access code is refused and the right one is
#      "RETURNING COMMANDER RECOGNIZED";
#   7. the server goes down and comes back on the same data directory: the
#      deck reconnects (handshake + login again) and keeps working.
# No DISPLAY: the decks run in CLI mode, as in smoke_test.sh.
#
# Usage: deck_e2e_test.sh <spacegl_server> <spacegl_client>
# Exit: 0 PASS, 1 FAIL, 77 SKIP.
set -uo pipefail

SERVER_BIN=${1:-}
CLIENT_BIN=${2:-}
if [ ! -x "$SERVER_BIN" ] || [ ! -x "$CLIENT_BIN" ]; then
    echo "SKIP: deck_e2e needs the spacegl_server and spacegl_client binaries"
    exit 77
fi

PORT=5000
KEY="ci-smoke-shared-master-key-000000"
export SPACEGL_KEY="$KEY"
unset DISPLAY WAYLAND_DISPLAY
DATA_DIR=$(mktemp -d "${TMPDIR:-/tmp}/spacegl_deck.XXXXXX") || exit 1
WORK=$(mktemp -d "${TMPDIR:-/tmp}/spacegl_deck_logs.XXXXXX") || exit 1
BIN_DIR=$(cd "$(dirname "$CLIENT_BIN")" && pwd)
SERVER_PID=""
PIDS=()
cleanup() {
    for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done
    if [ -n "$SERVER_PID" ]; then kill "$SERVER_PID" 2>/dev/null; wait "$SERVER_PID" 2>/dev/null; fi
    [ -n "${KEEP:-}" ] || rm -rf "$DATA_DIR" "$WORK"
}
trap cleanup EXIT

port_open() { (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; }
start_server() {
    "$SERVER_BIN" --data-dir "$DATA_DIR" >>"$WORK/server.log" 2>&1 &
    SERVER_PID=$!
    for _ in $(seq 1 100); do port_open && return 0; sleep 0.2; done
    echo "FAIL: server port $PORT never opened"; tail -n 30 "$WORK/server.log"; exit 1
}
stop_server() {
    kill "$SERVER_PID" 2>/dev/null; wait "$SERVER_PID" 2>/dev/null; SERVER_PID=""
    for _ in $(seq 1 50); do port_open || return 0; sleep 0.2; done
}
# deck <log name> <timeout s> : runs a deck in its own directory, stdin
# from the caller, output (colours stripped) in $WORK/<name>.txt
deck() {
    mkdir -p "$WORK/$1"; [ -e "$WORK/$1/build" ] || ln -s "$BIN_DIR" "$WORK/$1/build"
    (cd "$WORK/$1" && timeout "$2" "$CLIENT_BIN" "${@:3}") >"$WORK/$1.log" 2>&1
    local rc=$?
    sed 's/\x1b\[[0-9;]*[A-Za-z]//g' "$WORK/$1.log" > "$WORK/$1.txt"
    return $rc
}
login() { printf '127.0.0.1\n%s\n%s\n' "$1" "$2"; }   # name, access code

rc=0
check() {   # check <label> <command...>
    if "${@:2}"; then echo "  ok   $1"; else echo "  FAIL $1"; rc=1; fi
}
has() { grep -qF -- "$2" "$WORK/$1.txt"; }
hasnt() { ! grep -qF -- "$2" "$WORK/$1.txt"; }

if port_open; then echo "SKIP: port $PORT is busy (another server is running)"; exit 77; fi

# --- 1. no server ------------------------------------------------------
deck help 10 --help </dev/null
check "--help prints the usage"                 has help "Usage:"
deck version 10 --version </dev/null
check "--version prints the version"            has version "Space GL Client v"
printf 'no-such-host.invalid\n' | deck nohost 20
check "unresolvable host name is reported"      has nohost "Could not resolve hostname 'no-such-host.invalid'"
printf '127.0.0.1\n' | deck refused 10
check "refused connection is reported"          has refused "Connection to '127.0.0.1' failed"

start_server

# --- 2. another master key ---------------------------------------------
printf '127.0.0.1\n' | SPACEGL_KEY="another-master-key-not-the-servers" deck badkey 10
check "another master key: handshake refused"   has badkey "Master Key mismatch or Handshake rejected"

# --- 3. names rejected by the deck -------------------------------------
printf '127.0.0.1\nCOMPUTER\n' | deck name_station 10
check "station name refused"                    has name_station "Invalid commander name"
printf '127.0.0.1\nbad.name\n' | deck name_chars 10
check "name with '.' refused"                   has name_chars "Invalid commander name"
printf '127.0.0.1\nAAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDD\n' | deck name_long 10
check "33-character name refused"               has name_long "Invalid commander name"

# --- 4. one captain per faction (in parallel) --------------------------
# KORTHIAN (faction 1) stays on board through step 5 to listen to the
# radio; the others leave at once.
{ login KORTHIAN k-code; printf '1\n'; sleep 30; printf 'exit\n'; sleep 1; } | deck f1 45 &
PIDS+=($!)
for f in 2 3 4 5 6 7 8 9 10 11; do
    { login "FACTION$f" "f$f-code"; printf '%s\n' "$f"; sleep 4; printf 'exit\n'; sleep 1; } | deck "f$f" 25 &
    PIDS+=($!)
done
sleep 5

# --- 5. the input line of one session ----------------------------------
{
    login CHARLIE c-code; printf '0\n0\n'
    sleep 3
    printf 'srx\177\177rs\n'      # backspace twice: "srs"
    printf '\n'; printf '\033\n'  # empty line, ESC
    sleep 0.5; printf 'axs\n'; sleep 0.3; printf 'grd\n'
    sleep 0.3; printf 'tune \n'
    sleep 0.3; printf 'rad\n'
    sleep 0.3; printf 'enc3 NOBODY\n'
    sleep 0.3; printf 'enc3 NOBODY aes\n'
    sleep 0.3; printf 'enc salsa\n'; sleep 0.5; printf 'rad Salsa on the fleet frequency\n'
    sleep 0.3; printf 'enc off\n'
    sleep 0.3; printf 'rad @Korthian Hail the Korthian fleet\n'
    sleep 0.3; printf 'rad @Nowhere Unknown faction falls back to all\n'
    sleep 0.3; printf 'rad #1 To captain number one\n'
    sleep 0.3; printf 'rad #x Not a number\n'
    sleep 3; printf 'exit\n'; sleep 1
} | deck charlie 40 &
CH_PID=$!; PIDS+=($CH_PID)
sleep 6

# --- 6. the same name on board, wrong / right access code --------------
{ login CHARLIE c-code; sleep 2; } | deck dup 15
check "same name while on board is refused"     has dup "COMMANDER NAME ALREADY ACTIVE"
wait "$CH_PID" 2>/dev/null
for p in "${PIDS[@]}"; do wait "$p" 2>/dev/null; done
PIDS=()
sleep 1
{ login CHARLIE wrong-code; sleep 2; } | deck wrongpass 15
check "wrong access code is refused"            has wrongpass "INVALID ACCESS CODE"
{ login CHARLIE c-code; sleep 3; printf 'exit\n'; sleep 1; } | deck returning 20
check "right access code: returning commander"  has returning "RETURNING COMMANDER RECOGNIZED"

check "faction captains 1..11 all logged in" \
    bash -c 'for f in 1 2 3 4 5 6 7 8 9 10 11; do grep -q "Command Deck ready" "$0/f$f.txt" || exit 1; done' "$WORK"
check "no class menu for a non-Alliance captain" hasnt f5 "SELECT YOUR CLASS"
check "CHARLIE logged in"                       has charlie "Command Deck ready"
check "backspace edits the line (srs ran)"      has charlie "SHORT RANGE"
check "tune with no captain resets the tuner"   has charlie "Tuner reset to self-frequency"
check "rad alone prints its usage"              has charlie "Usage: rad <MSG>"
check "enc3 without a cipher prints its usage"  has charlie "Usage: enc3 <Peer> <algo>"
check "enc3 to a captain with no link refused"  has charlie "No Link with NOBODY"
check "KORTHIAN heard CHARLIE's fleet message" has f1 "Salsa on the fleet frequency"
check "CHARLIE's faction message reached CHARLIE" has charlie "CHARLIE (Alliance Command): Hail the Korthian fleet"
check "...and not KORTHIAN (sender's faction)"  hasnt f1 "Hail the Korthian fleet"
check "no protocol error on CHARLIE"            hasnt charlie "protocol error"

# --- 7. the server goes away and comes back ----------------------------
{ login DELTA d-code; printf '0\n0\n'; sleep 4; printf 'sta\n'; sleep 14; printf 'sta\n'; sleep 2; printf 'exit\n'; sleep 1; } | deck delta 40 &
D_PID=$!; PIDS+=($D_PID)
sleep 7
stop_server
sleep 2
start_server
wait "$D_PID" 2>/dev/null; PIDS=()
check "deck saw the link drop"                  has delta "[RECONNECT] Connection lost"
check "deck reconnected"                        has delta "[RECONNECT] Reconnected successfully"
check "status works after the reconnect" \
    bash -c 'sed -n "/Reconnected successfully/,\$p" "$0" | grep -q "PHYSICAL HULL"' "$WORK/delta.txt"
stop_server

if [ $rc -ne 0 ]; then
    for f in charlie delta f1 dup wrongpass returning; do
        echo "--- $f (tail) ---"; tail -n 15 "$WORK/$f.txt" 2>/dev/null
    done
    echo "--- server (tail) ---"; tail -n 20 "$WORK/server.log"
    echo "FAIL: deck_e2e"
    exit 1
fi
echo "PASS: deck_e2e"
exit 0
