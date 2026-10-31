#!/bin/bash
# SPACE GL - 3D LOGIC ENGINE
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# radio_e2e_test.sh — two REAL command decks on the REAL server
# (2026.10.31.33, roadmap phase 6): captain ALPHA talks, captain BRAVO
# listens. Every message goes deck -> server relay -> deck, signed with the
# sender's Ed25519 key bound at login:
#   1. a message in clear reaches BRAVO, [VERIFIED];
#   2. a message on the fleet frequency encrypted with AES-256-GCM ("enc
#      aes") reaches BRAVO decrypted, [VERIFIED];
#   3. a message on another fleet cipher (ChaCha20) reaches BRAVO
#      decrypted;
#   4. a message on ALPHA's identity frequency ("enc2 aes") reaches BRAVO
#      once BRAVO has tuned to ALPHA ("tune ALPHA"), marked [PRIVATE];
#   4b. after "link BRAVO" on ALPHA and "link ALPHA" on BRAVO (each deck
#      fetches the other's X25519 key through the server and derives the
#      shared secret), a peer message ("enc3 BRAVO aes") reaches BRAVO
#      decrypted, marked [P2P];
#   5. both decks run the status, sensor and report commands on the way
#      (srs, lrs, sta, inv, dam, who, help) and decode the update stream
#      without a protocol error; ALPHA leaves with "exit".
# No DISPLAY: the decks run in CLI mode, as in smoke_test.sh.
#
# Usage: radio_e2e_test.sh <spacegl_server> <spacegl_client>
# Exit: 0 PASS, 1 FAIL, 77 SKIP.
set -uo pipefail

SERVER_BIN=${1:-}
CLIENT_BIN=${2:-}
if [ ! -x "$SERVER_BIN" ] || [ ! -x "$CLIENT_BIN" ]; then
    echo "SKIP: radio_e2e needs the spacegl_server and spacegl_client binaries"
    exit 77
fi

PORT=5000
export SPACEGL_KEY="ci-smoke-shared-master-key-000000"
unset DISPLAY WAYLAND_DISPLAY
DATA_DIR=$(mktemp -d "${TMPDIR:-/tmp}/spacegl_radio.XXXXXX") || exit 1
WORK=$(mktemp -d "${TMPDIR:-/tmp}/spacegl_radio_logs.XXXXXX") || exit 1
BIN_DIR=$(cd "$(dirname "$CLIENT_BIN")" && pwd)
mkdir -p "$WORK/a" "$WORK/b"
ln -s "$BIN_DIR" "$WORK/a/build"
ln -s "$BIN_DIR" "$WORK/b/build"
SERVER_PID=""; B_PID=""
cleanup() {
    [ -n "$B_PID" ] && kill "$B_PID" 2>/dev/null
    if [ -n "$SERVER_PID" ]; then kill "$SERVER_PID" 2>/dev/null; wait "$SERVER_PID" 2>/dev/null; fi
    [ -n "${KEEP:-}" ] || rm -rf "$DATA_DIR" "$WORK"
}
trap cleanup EXIT

"$SERVER_BIN" --data-dir "$DATA_DIR" >"$WORK/server.log" 2>&1 &
SERVER_PID=$!
for _ in $(seq 1 100); do
    if (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then break; fi
    sleep 0.2
done
if ! (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then
    echo "FAIL: server port $PORT never opened"; tail -n 30 "$WORK/server.log"; exit 1
fi

# BRAVO: logs in, tunes to ALPHA, runs a few commands, listens.
{
    printf '127.0.0.1\nBRAVO\nbravo-access-code\n0\n0\n'
    sleep 3; printf 'tune ALPHA\n'
    sleep 1; printf 'srs\n'; sleep 0.5; printf 'lrs\n'; sleep 0.5; printf 'who\n'
    sleep 5; printf 'link ALPHA\n'        # the reciprocal half of the peer link
    sleep 15; printf 'exit\n'; sleep 1
} | (cd "$WORK/b" && timeout 40 "$CLIENT_BIN") >"$WORK/b.log" 2>&1 &
B_PID=$!
sleep 5

# ALPHA: logs in and talks.
{
    printf '127.0.0.1\nALPHA\nalpha-access-code\n0\n0\n'
    sleep 3; printf 'rad Clear signal from Alpha\n'
    sleep 1; printf 'enc aes\n'; sleep 1; printf 'rad Encrypted greetings from Alpha\n'
    sleep 1; printf 'enc chacha\n'; sleep 1; printf 'rad Second cipher from Alpha\n'
    sleep 1; printf 'enc2 aes\n'; sleep 1; printf 'rad Identity frequency from Alpha\n'
    sleep 1; printf 'link BRAVO\n'; sleep 1; printf 'enc3 BRAVO aes\n'; sleep 1; printf 'rad Peer link to Bravo\n'
    sleep 1; printf 'sta\n'; sleep 0.5; printf 'inv\n'; sleep 0.5; printf 'dam\n'
    sleep 0.5; printf 'srs\n'; sleep 0.5; printf 'lrs\n'; sleep 0.5; printf 'help\n'
    sleep 2; printf 'exit\n'; sleep 1
} | (cd "$WORK/a" && timeout 40 "$CLIENT_BIN") >"$WORK/a.log" 2>&1
wait "$B_PID" 2>/dev/null; B_PID=""
kill "$SERVER_PID" 2>/dev/null; wait "$SERVER_PID" 2>/dev/null; SERVER_PID=""

for f in a b; do sed 's/\x1b\[[0-9;]*[A-Za-z]//g' "$WORK/$f.log" > "$WORK/$f.txt"; done

rc=0
check() {   # check <label> <command...>
    if "${@:2}"; then echo "  ok   $1"; else echo "  FAIL $1"; rc=1; fi
}
# "[RADIO]<level> [VERIFIED] ALPHA (Alliance Command): <text>", after the prompt
radio() { grep -qF "[RADIO]$2 [VERIFIED] ALPHA (Alliance Command): $1" "$WORK/b.txt"; }
check "ALPHA logged in"                         grep -q "Command Deck ready" "$WORK/a.txt"
check "BRAVO logged in"                         grep -q "Command Deck ready" "$WORK/b.txt"
check "clear message reached BRAVO, verified"   radio "Clear signal from Alpha" ""
check "AES fleet message decrypted, verified"   radio "Encrypted greetings from Alpha" ""
check "ChaCha20 fleet message decrypted"        radio "Second cipher from Alpha" ""
check "identity frequency received [PRIVATE]"   radio "Identity frequency from Alpha" " [PRIVATE]"
check "peer message received [P2P]"            radio "Peer link to Bravo" " [P2P]"
check "no protocol error on ALPHA"              bash -c "! grep -q 'protocol error\|wire desync' '$WORK/a.txt'"
check "no protocol error on BRAVO"              bash -c "! grep -q 'protocol error\|wire desync' '$WORK/b.txt'"
check "LRS scan decoded on BRAVO"               grep -q "LRS TACTICAL SCAN" "$WORK/b.txt"

if [ $rc -ne 0 ]; then
    echo "--- BRAVO (radio lines) ---"; grep -E "RADIO|SIGNAL|MISMATCH|ERROR" "$WORK/b.txt" | tail -n 20
    echo "--- ALPHA (tail) ---"; tail -n 20 "$WORK/a.txt"
    echo "FAIL: radio_e2e"
    exit 1
fi
echo "PASS: radio_e2e"
exit 0
