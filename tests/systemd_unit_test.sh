#!/bin/bash
# shellcheck disable=SC2317,SC2329  # the checks are functions called through check()
# SPACE GL - 3D LOGIC ENGINE
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# systemd_unit_test.sh — the optional systemd unit of the server
# (systemd/spacegl-server.service.in, 2026.11.01.12), as the build
# configured it:
#   1. systemd-analyze verify accepts it (syntax, directives, the
#      ExecStart program, here the server of this build; --man=no, the
#      man page of Documentation= is looked for in the source tree);
#   2. it is installed disabled-by-default material only: an [Install]
#      section, no "enable" anywhere in the build;
#   3. the key reaches the server as a credential (LoadCredential, and
#      SPACEGL_KEY_FILE pointing into the credentials directory), the
#      state lives in StateDirectory, the service user is dynamic;
#   4. the sandbox: systemd-analyze security --offline rates its exposure
#      at most 2.0 ("OK"; 1.2 when written).
# Needs systemd-analyze (SKIP 77 without it, e.g. a build chroot).
# Usage: systemd_unit_test.sh <configured spacegl-server.service> <spacegl_server>
set -uo pipefail

UNIT=${1:-}
SERVER_BIN=${2:-}
if [ ! -f "$UNIT" ] || [ ! -x "$SERVER_BIN" ]; then
    echo "SKIP: systemd_unit needs the configured unit and the spacegl_server binary"
    exit 77
fi
if ! command -v systemd-analyze >/dev/null 2>&1; then
    echo "SKIP: systemd_unit: no systemd-analyze"
    exit 77
fi

WORK=$(mktemp -d "${TMPDIR:-/tmp}/spacegl_unit.XXXXXX") || exit 1
trap 'rm -rf "$WORK"' EXIT
rc=0
check() {   # check <label> <command...>
    if "${@:2}"; then echo "  ok   $1"; else echo "  FAIL $1"; rc=1; fi
}

# The installed unit names the installed program (e.g. /usr/bin): point it
# at this build's server so that verify can check the program itself.
BIN_DIR=$(cd "$(dirname "$SERVER_BIN")" && pwd)
sed -E "s#^ExecStart=[^ ]*/spacegl_server #ExecStart=$BIN_DIR/spacegl_server #" "$UNIT" >"$WORK/spacegl-server.service"

verify_ok() {
    # --man=no: verify would run man(1) for Documentation=, which fails
    # (exit 16) wherever the page is not installed yet, i.e. on every
    # build tree; the page itself is checked below in the source tree.
    systemd-analyze verify --man=no "$WORK/spacegl-server.service" >"$WORK/verify.txt" 2>&1 && return 0
    cat "$WORK/verify.txt"
    return 1
}
check "systemd-analyze verify accepts the unit" verify_ok
d() { grep -qxF -- "$1" "$WORK/spacegl-server.service"; }
ROOT=$(cd "$(dirname "$0")/.." && pwd)
doc_pages_exist() {   # every man:page(N) of Documentation= is in man/manN/
    local doc page sec found=0 docs
    read -ra docs < <(sed -n 's/^Documentation=//p' "$WORK/spacegl-server.service" | tr '\n' ' ')
    for doc in "${docs[@]}"; do
        [[ $doc == man:* ]] || continue
        page=${doc#man:}; sec=${page##*(}; sec=${sec%)}; page=${page%(*}
        [ -f "$ROOT/man/man$sec/$page.$sec" ] || { echo "  (missing: man/man$sec/$page.$sec)"; return 1; }
        found=1
    done
    [ "$found" -eq 1 ]
}
check "the Documentation= man page is in the tree" doc_pages_exist
check "ExecStart runs the server on the state directory" \
    grep -qE '^ExecStart=.*/spacegl_server --data-dir %S/spacegl( |$)' "$WORK/spacegl-server.service"
check "the key is a credential"                 d "LoadCredential=master_key:/etc/spacegl/key"
check "...given to the server by SPACEGL_KEY_FILE" d "Environment=SPACEGL_KEY_FILE=%d/master_key"
check "...and required before the start"        d "AssertPathExists=/etc/spacegl/key"
check "state in StateDirectory"                 d "StateDirectory=spacegl"
check "...mode 0700"                            d "StateDirectoryMode=0700"
check "transient unprivileged user"             d "DynamicUser=yes"
check "SIGTERM is given time to save the galaxy" d "TimeoutStopSec=90"
check "an [Install] section (enable is up to the user)" d "WantedBy=multi-user.target"

if systemd-analyze security --offline=true "$WORK/spacegl-server.service" >"$WORK/security.txt" 2>&1; then
    level=$(sed -n 's/.*Overall exposure level for [^:]*: \([0-9.]*\).*/\1/p' "$WORK/security.txt" | tail -n 1)
    if [ -n "$level" ]; then
        check "sandbox exposure $level <= 2.0"  awk -v l="$level" 'BEGIN { exit !(l <= 2.0) }'
    else
        echo "  skip sandbox exposure (no level in the output)"
    fi
else
    echo "  skip sandbox exposure (systemd-analyze security --offline unavailable)"
fi

# The server accepts the unit's way of passing the key: a credential file
# is mode 0400 (no group/other bits), read by the service's own user, and
# the state directory is given with --data-dir.
printf '%s\n' "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" >"$WORK/master_key"
chmod 0400 "$WORK/master_key"
mkdir -m 0700 "$WORK/state"
boots_with_credential() {
    if (exec 3<>/dev/tcp/127.0.0.1/5000) 2>/dev/null; then
        echo "  (port 5000 busy: boot not tried)"
        return 0
    fi
    (cd "$WORK/state" && exec env -u SPACEGL_KEY SPACEGL_KEY_FILE="$WORK/master_key" \
        "$SERVER_BIN" --data-dir "$WORK/state") >"$WORK/boot.log" 2>&1 &
    local pid=$! i
    for ((i = 0; i < 300; i++)); do
        grep -q "listening on port 5000" "$WORK/boot.log" 2>/dev/null && break
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.2
    done
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    if grep -qF "Master key loaded from file '$WORK/master_key'" "$WORK/boot.log" &&
        grep -q "listening on port 5000" "$WORK/boot.log" && [ -s "$WORK/state/server_identity.key" ]; then
        return 0
    fi
    tail -n 20 "$WORK/boot.log"
    return 1
}
check "the server boots on a 0400 credential and the state directory" boots_with_credential

if [ $rc -ne 0 ]; then
    echo "FAIL: systemd_unit"
    exit 1
fi
echo "PASS: systemd_unit"
exit 0
