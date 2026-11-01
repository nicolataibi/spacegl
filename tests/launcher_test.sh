#!/usr/bin/env bash
# shellcheck disable=SC2016,SC2317,SC2329  # bash -c checks expand their own "$0"/"$1"; check/has/hasnt/cleanup are called indirectly
# SPACE GL - 3D LOGIC ENGINE
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# launcher_test.sh — the launcher scripts spacegl-server, spacegl-client and
# spacegl-diag (2026.11.01.03), with stub programs in SPACEGL_BIN_DIR:
#   1. every argument reaches the program unchanged (spaces, an empty
#      argument, glob characters), --no-banner is removed, the exit status
#      is the program's (the launcher execs it);
#   2. without a terminal nothing is asked and the program's standard input
#      is untouched (the deck reads the captain's answers from it);
#   3. an existing key source (SPACEGL_KEY_FILE, SPACEGL_KEY,
#      ~/.spacegl/key) is used without asking;
#   4. on a terminal with no key source the key is asked for, not echoed
#      (not even when typed the moment the prompt appears), a
#      key that is too short is refused and asked again, the valid one is
#      passed in SPACEGL_KEY; three wrong answers end with exit 2;
#   5. --help prints the launcher's usage and the program's; --help,
#      --version and --print-identity print no banner and ask nothing;
#   6. a missing program: exit 127 with a message;
#   7. spacegl-diag: a PID that is not a spacegl_server is refused; the only
#      running server is found by itself; with two the PID is required; the
#      diagnostic runs in this terminal with SPACEGL_DIAG_TERMINAL=here.
# Exit: 0 PASS, 1 FAIL, 77 SKIP (no python3 for the pseudo-terminal cases).
set -u
ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
T=$(mktemp -d "${TMPDIR:-/tmp}/sg_launcher.XXXXXX") || exit 1
FAKE_PIDS=()
cleanup() {
    local p
    for p in "${FAKE_PIDS[@]+"${FAKE_PIDS[@]}"}"; do kill "$p" 2>/dev/null; done
    rm -rf "$T"
}
trap cleanup EXIT
mkdir -p "$T/bin" "$T/home" "$T/empty"
for name in spacegl_server spacegl_client spacegl_diag; do
    cat > "$T/bin/$name" <<STUB
#!/usr/bin/env bash
for a in "\$@"; do [[ \$a == --help ]] && echo "STUB HELP $name"; done
printf 'STUB %s' "$name"; printf ' [%s]' "\$@"; printf '\n'
echo "KEYLEN=\${#SPACEGL_KEY}"
[[ -t 0 ]] || echo "STDIN=\$(cat)"
[[ -t 0 ]] && echo "TTYECHO=\$(stty -a | tr ' ' '\\n' | grep -x -e echo -e -echo)"
exit "\${STUB_EXIT:-0}"
STUB
    chmod +x "$T/bin/$name"
done
export SPACEGL_BIN_DIR="$T/bin" HOME="$T/home" NO_COLOR=1
unset SPACEGL_KEY SPACEGL_KEY_FILE DISPLAY WAYLAND_DISPLAY

rc=0
check() { if "${@:2}"; then echo "  ok   $1"; else echo "  FAIL $1"; rc=1; fi; }
has() { grep -qF -- "$2" "$T/$1"; }
hasnt() { ! grep -qF -- "$2" "$T/$1"; }

# --- 1 ------------------------------------------------------------------
for s in spacegl-server spacegl-client; do
    STUB_EXIT=7 "$ROOT/$s" --no-banner "two words" "" '*' -d </dev/null >"$T/$s.args" 2>&1
    st=$?
    check "$s: arguments unchanged"      has "$s.args" "[two words] [] [*] [-d]"
    check "$s: --no-banner removed"      hasnt "$s.args" "[--no-banner]"
    check "$s: exit status of the program" [ "$st" -eq 7 ]
done

# --- 2 ------------------------------------------------------------------
printf '127.0.0.1\nCAPTAIN\n' | "$ROOT/spacegl-client" gl >"$T/pipe.out" 2>&1
check "no terminal: nothing asked"          hasnt pipe.out "[AUTH]"
check "no terminal: the warning says so"    has pipe.out "no terminal to ask for one"
check "no terminal: stdin reaches the deck" has pipe.out "STDIN=127.0.0.1"

# --- 3 ------------------------------------------------------------------
SPACEGL_KEY_FILE=/some/file "$ROOT/spacegl-server" </dev/null >"$T/kf.out" 2>&1
check "SPACEGL_KEY_FILE used"               has kf.out "Master key: file /some/file"
SPACEGL_KEY=0123456789abcdef0123456789abcdef "$ROOT/spacegl-client" </dev/null >"$T/ke.out" 2>&1
check "SPACEGL_KEY used, passed on"         has ke.out "KEYLEN=32"
mkdir -p "$T/home/.spacegl" && echo x > "$T/home/.spacegl/key"
"$ROOT/spacegl-server" </dev/null >"$T/kh.out" 2>&1
check "the home key file used"                 has kh.out "Master key: $T/home/.spacegl/key"
rm -rf "$T/home/.spacegl"

# --- 4 (pseudo-terminal) -------------------------------------------------
if command -v python3 >/dev/null 2>&1; then
    # ptyrun.py <answers...> -- <command...>: runs the command on a pty, types
    # one answer per prompt, prints everything the terminal showed.
    cat > "$T/ptyrun.py" <<'PY'
import os, pty, sys, time, select
sep = sys.argv.index("--")
answers, cmd = sys.argv[1:sep], sys.argv[sep + 1:]
pid, fd = pty.fork()
if pid == 0:
    os.execvp(cmd[0], cmd)
out = b""
deadline = time.time() + 15
while time.time() < deadline:
    r, _, _ = select.select([fd], [], [], 0.2)
    if r:
        try:
            data = os.read(fd, 4096)
        except OSError:
            break
        if not data:
            break
        out += data
        while answers and out.count(b"[AUTH]") > 0 and out.count(b"[AUTH]") > len(sys.argv[1:sep]) - len(answers):
            os.write(fd, (answers.pop(0) + "\n").encode())
_, status = os.waitpid(pid, 0)
sys.stdout.write(out.decode(errors="replace"))
sys.stdout.write("\nEXIT=%d\n" % os.waitstatus_to_exitcode(status))
PY
    GOOD=aaaabbbbccccddddeeeeffff00001111aaaabbbbccccddddeeeeffff00001111
    python3 "$T/ptyrun.py" short-key "$GOOD" -- "$ROOT/spacegl-client" --no-banner gl >"$T/tty.out" 2>&1
    check "terminal: the key is asked for"      has tty.out "[AUTH] Master key"
    check "terminal: a short key is refused"    has tty.out "not a valid key (9 characters)"
    check "terminal: the key is not echoed"     hasnt tty.out "$GOOD"
    check "terminal: the valid key passed on"   has tty.out "KEYLEN=64"
    check "terminal: then the deck runs"        has tty.out "STUB spacegl_client [gl]"
    check "terminal: echo is back on for the deck" has tty.out "TTYECHO=echo"
    # A key typed the moment the prompt appears (2026.11.01.12, seen in the
    # COPR el10 chroot): strace delays the launcher after each write, so the
    # answer arrives before read -s starts; echo must already be off.
    if command -v strace >/dev/null 2>&1 && strace -qq -o /dev/null true 2>/dev/null; then
        python3 "$T/ptyrun.py" "$GOOD" -- strace -qq -f -o /dev/null -e trace=write \
            -e inject=write:delay_exit=300000 "$ROOT/spacegl-client" --no-banner gl >"$T/ttyslow.out" 2>&1
        check "slow terminal: a key typed at once is not echoed" hasnt ttyslow.out "$GOOD"
        check "...and is passed on"                 has ttyslow.out "KEYLEN=64"
        check "...and echo is back on"              has ttyslow.out "TTYECHO=echo"
    else
        echo "  (strace not usable: the slow-terminal case skipped)"
    fi
    python3 "$T/ptyrun.py" a b c -- "$ROOT/spacegl-server" --no-banner >"$T/tty3.out" 2>&1
    check "terminal: three wrong keys, exit 2"  has tty3.out "EXIT=2"
    check "...and the server never starts"      hasnt tty3.out "STUB spacegl_server"
    python3 "$T/ptyrun.py" -- "$ROOT/spacegl-server" --version >"$T/ttyv.out" 2>&1
    check "terminal: --version asks nothing"    hasnt ttyv.out "[AUTH]"
else
    echo "  (python3 not available: pseudo-terminal cases skipped)"
fi

# --- 5 ------------------------------------------------------------------
"$ROOT/spacegl-server" --help </dev/null >"$T/help.out" 2>&1
check "--help: the launcher's usage"        has help.out "Usage: spacegl-server [--no-banner]"
check "--help: the program's help"          has help.out "STUB HELP spacegl_server"
"$ROOT/spacegl-server" --print-identity </dev/null >"$T/pi.out" 2>&1
check "--print-identity: only the program's output" bash -c '[ "$(head -c 20 "$1")" = "STUB spacegl_server " ] && ! grep -q "Master key\|no terminal" "$1"' _ "$T/pi.out"

# --- 6 ------------------------------------------------------------------
SPACEGL_BIN_DIR="$T/empty" "$ROOT/spacegl-client" </dev/null >"$T/miss.out" 2>&1
st=$?
check "missing program: exit 127"           [ "$st" -eq 127 ]
check "missing program: a message"          has miss.out "spacegl_client not found"

# --- 7 ------------------------------------------------------------------
"$ROOT/spacegl-diag" $$ </dev/null >"$T/d1.out" 2>&1
st=$?
check "diag: a PID that is not a server is refused" bash -c '[ "$0" -eq 1 ] && grep -q "is not a running spacegl_server" "$1"' "$st" "$T/d1.out"
real_server=0
for d in /proc/[0-9]*; do
    if [[ -r $d/comm ]] && [[ $(cat "$d/comm" 2>/dev/null) == spacegl_server ]]; then real_server=1; break; fi
done
if (( real_server )); then
    echo "  (a real spacegl_server is running: automatic detection cases skipped)"
else
    cp "$(command -v sleep)" "$T/spacegl_server"
    "$T/spacegl_server" 30 & FAKE_PIDS+=($!)
    sleep 0.2
    SPACEGL_DIAG_TERMINAL=here "$ROOT/spacegl-diag" --no-banner -x </dev/null >"$T/d2.out" 2>&1
    check "diag: the only server found, run here" has d2.out "STUB spacegl_diag [${FAKE_PIDS[0]}] [-x]"
    SPACEGL_DIAG_TERMINAL=here "$ROOT/spacegl-diag" --no-banner "${FAKE_PIDS[0]}" </dev/null >"$T/d3.out" 2>&1
    check "diag: an explicit server PID"     has d3.out "STUB spacegl_diag [${FAKE_PIDS[0]}]"
    "$T/spacegl_server" 30 & FAKE_PIDS+=($!)
    sleep 0.2
    "$ROOT/spacegl-diag" </dev/null >"$T/d4.out" 2>&1
    st=$?
    check "diag: two servers, the PID is required" bash -c '[ "$0" -eq 1 ] && grep -q "2 spacegl_server processes are running" "$1"' "$st" "$T/d4.out"
fi

if [ $rc -ne 0 ]; then
    for f in "$T"/*.out "$T"/*.args; do echo "--- $(basename "$f")"; tail -n 8 "$f"; done
    echo "FAIL: launcher"
    exit 1
fi
echo "PASS: launcher"
exit 0
