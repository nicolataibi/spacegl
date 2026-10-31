#!/usr/bin/env bash
#
# SPACE GL - 3D LOGIC ENGINE
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# galaxy_file_test.sh — galaxy.dat format 2 (2026.10.31.19) with the REAL
# server binary:
#   1. a new galaxy is saved in format 2 (magic SGGALAX2), atomically (no
#      galaxy.dat.tmp left behind), mode 0600;
#   2. a restart loads it with the same astrometrics counts;
#   3. a format-1 file (the previous raw dump, rebuilt here from the format-2
#      payloads) is no longer read (2026.10.31.25): a new galaxy is
#      generated and saved in format 2;
#   4. a section whose element size changed is skipped, the rest is loaded;
#   5. a corrupt file makes the server generate a new galaxy, no crash.
#
# Usage: galaxy_file_test.sh [server_bin]     Exit: 0 pass, 1 fail, 77 skip.

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
SERVER_BIN="${1:-$REPO_ROOT/build/spacegl_server}"
[ -x "$SERVER_BIN" ] || { echo "SKIP: spacegl_server binary not found"; exit 77; }
command -v python3 >/dev/null 2>&1 || { echo "SKIP: python3 not available"; exit 77; }

WORK=$(mktemp -d "${TMPDIR:-/tmp}/spacegl_galaxy.XXXXXX") || exit 1
DATA="$WORK/data"; mkdir -p "$DATA"
SERVER_PID=""
cleanup() { [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null; wait 2>/dev/null; [ -n "${KEEP:-}" ] || rm -rf "$WORK"; }
trap cleanup EXIT INT TERM
export SPACEGL_KEY="ci-smoke-shared-master-key-000000"
export HOME="$WORK"

fail=0
check() { local what="$1"; shift; if "$@"; then echo "ok   - $what"; else echo "FAIL - $what"; fail=1; fi; }

# Start the server on $DATA, wait for the port (boot = load or generate +
# save), stop it (shutdown save), keep its log in $1 without colours.
run_server() {
    "$SERVER_BIN" --data-dir "$DATA" >"$WORK/raw.log" 2>&1 &
    SERVER_PID=$!
    local ready=0
    for _ in $(seq 1 150); do
        if (exec 3<>/dev/tcp/127.0.0.1/5000) 2>/dev/null; then ready=1; break; fi
        kill -0 "$SERVER_PID" 2>/dev/null || break
        sleep 0.2
    done
    kill "$SERVER_PID" 2>/dev/null; wait "$SERVER_PID" 2>/dev/null; SERVER_PID=""
    sed 's/\x1b\[[0-9;]*[A-Za-z]//g' "$WORK/raw.log" > "$1"
    [ "$ready" -eq 1 ]
}

# The astrometrics counts that do not change while the server runs (label +
# value pairs: the generation and the load reports lay them out differently).
counts() { grep -oE "(Planets|NATURAL MOONS|TOTAL FEATURES): *[0-9]+|SYSTEM (STARS|PLANETS|MOONS) *\| READY *\| *[0-9]+" "$1" | tr -s ' ' | sort -u; }

magic() { head -c 8 "$DATA/galaxy.dat" 2>/dev/null; }
not_grep() { ! grep -q "$1" "$2"; }

# 1. new galaxy, format 2
check "boot on an empty data dir"           run_server "$WORK/1.log"
check "a new galaxy was generated"          grep -q "Generating Master Galaxy" "$WORK/1.log"
check "saved in format 2 (SGGALAX2)"        test "$(magic)" = "SGGALAX2"
check "no galaxy.dat.tmp left behind"       test ! -e "$DATA/galaxy.dat.tmp"
check "galaxy.dat is mode 0600"             test "$(stat -c %a "$DATA/galaxy.dat")" = "600"
counts "$WORK/1.log" > "$WORK/c1.txt"
check "astrometrics counts present"         test -s "$WORK/c1.txt"

# 2. restart: same galaxy
check "restart"                             run_server "$WORK/2.log"
check "loaded, not generated"               grep -q "PERSISTENT GALAXY LOADED SUCCESSFULLY" "$WORK/2.log"
counts "$WORK/2.log" > "$WORK/c2.txt"
check "same counts after the restart"       cmp -s "$WORK/c1.txt" "$WORK/c2.txt"

# 3. format 1 (version int + the payloads in the same order) -> refused
python3 - "$DATA/galaxy.dat" "$WORK/v1.dat" <<'EOF'
import struct, sys
src, dst = sys.argv[1], sys.argv[2]
b = open(src, 'rb').read()
magic, fmt, gver, nsec, _ = struct.unpack_from('<8sIIII', b, 0)
assert magic == b'SGGALAX2' and fmt == 2
off, out = 24, [struct.pack('<i', gver)]
for _ in range(nsec):
    tag, esz, layout, cnt = struct.unpack_from('<24sIIQ', b, off); off += 40
    out.append(b[off:off + esz * cnt]); off += esz * cnt
open(dst, 'wb').write(b''.join(out))
EOF
cp "$WORK/v1.dat" "$DATA/galaxy.dat"
check "boot with a format-1 file"            run_server "$WORK/3.log"
check "format 1 refused"                    grep -q "GALAXY FILE NOT IN FORMAT 2" "$WORK/3.log"
check "format 1 not loaded"                 not_grep "PERSISTENT GALAXY LOADED SUCCESSFULLY" "$WORK/3.log"
check "a new galaxy was generated"          grep -q "Generating Master Galaxy" "$WORK/3.log"
check "saved in format 2 (SGGALAX2)"        test "$(magic)" = "SGGALAX2"
counts "$WORK/3.log" > "$WORK/c3.txt"

# 4. a section whose element size changed is skipped, the galaxy still loads
python3 - "$DATA/galaxy.dat" <<'EOF'
import struct, sys
p = sys.argv[1]
b = bytearray(open(p, 'rb').read())
_, _, _, nsec, _ = struct.unpack_from('<8sIIII', b, 0)
off = 24
for _ in range(nsec):
    tag, esz, layout, cnt = struct.unpack_from('<24sIIQ', b, off)
    if tag.rstrip(b'\0') == b'mines':
        # same bytes, different element size: as if NPCMine had changed
        struct.pack_into('<IQ', b, off + 24, esz * 2, 0)   # elem_size doubled ...
        struct.pack_into('<I', b, off + 28, layout)        # ... layout kept
        struct.pack_into('<Q', b, off + 32, cnt // 2)      # ... count halved: same payload length
        if cnt % 2: raise SystemExit("odd mines count")
        break
    off += 40 + esz * cnt
open(p, 'wb').write(b)
EOF
check "file with a changed section"         run_server "$WORK/4.log"
check "the changed section is reported"     grep -q "section 'mines': layout changed" "$WORK/4.log"
check "the rest is loaded"                  grep -q "PERSISTENT GALAXY LOADED SUCCESSFULLY" "$WORK/4.log"
counts "$WORK/4.log" > "$WORK/c4.txt"
check "planets, moons, systems unchanged"   cmp -s "$WORK/c3.txt" "$WORK/c4.txt"

# 5. corrupt file: a new galaxy, no crash
head -c 4096 /dev/urandom > "$DATA/galaxy.dat"
check "server survives a corrupt file"      run_server "$WORK/5.log"
check "a new galaxy replaces it"            grep -q "Generating Master Galaxy" "$WORK/5.log"
check "and is saved in format 2"            test "$(magic)" = "SGGALAX2"

if [ "$fail" -ne 0 ]; then
    for f in 1 2 3 4 5; do echo "--- run $f ---"; grep -E "GALAXY|section|Generating" "$WORK/$f.log" | head -8; done
    echo "FAIL: galaxy_file_test"; exit 1
fi
echo "PASS: galaxy_file_test"
exit 0
