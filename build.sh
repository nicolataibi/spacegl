#!/usr/bin/env bash
# build.sh — quick dev loop for SpaceGL
#
# Usage:
#   ./build.sh            # Release build + full test suite (default)
#   ./build.sh -d         # Debug build + full test suite (ASan/UBSan)
#   ./build.sh -r         # RelWithDebInfo (profiling)
#   ./build.sh -s         # MinSizeRel
#   ./build.sh -t         # tests only (no rebuild)
#   ./build.sh -c         # clean build (rm -rf build/) + rebuild
#   ./build.sh -n         # build only, skip the test suite
#   ./build.sh -h         # help
#
# Options can be combined, e.g. ./build.sh -c -n  (or -cn).

# This script needs bash (pipefail, arrays). If it was started with
# "sh build.sh", re-run it under bash instead of failing in dash.
if [ -z "${BASH_VERSION:-}" ]; then
    if command -v bash >/dev/null 2>&1; then
        exec bash "$0" "$@"
    fi
    echo "build.sh: bash is required but was not found in PATH" >&2
    exit 1
fi

set -euo pipefail

# Always work from the repository root, wherever the script is called from.
cd "$(dirname "${BASH_SOURCE[0]}")"

usage() {
    grep '^#' "$0" | sed 's/^# \{0,1\}//' | tail -n +2 | sed '/^This script needs bash/,$d'
}

BUILD_DIR="build"
BUILD_TYPE="Release"
TESTS_ONLY=0
CLEAN=0
RUN_TESTS=1

while getopts "drstcnh" opt; do
    case $opt in
        d) BUILD_TYPE="Debug" ;;
        r) BUILD_TYPE="RelWithDebInfo" ;;
        s) BUILD_TYPE="MinSizeRel" ;;
        t) TESTS_ONLY=1 ;;
        c) CLEAN=1 ;;
        n) RUN_TESTS=0 ;;
        h) usage; exit 0 ;;
        *) usage >&2; exit 1 ;;
    esac
done

if [ "$TESTS_ONLY" -eq 1 ] && [ "$RUN_TESTS" -eq 0 ]; then
    echo "build.sh: -t (tests only) and -n (no tests) cannot be combined" >&2
    exit 1
fi

if [ "$CLEAN" -eq 1 ]; then
    echo "==> Cleaning ${BUILD_DIR}/"
    rm -rf "${BUILD_DIR}"
fi

if [ "$TESTS_ONLY" -eq 0 ]; then
    echo "==> Configuring (${BUILD_TYPE})"
    cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

    echo "==> Building"
    cmake --build "${BUILD_DIR}" -j"$(nproc)"
fi

if [ "$RUN_TESTS" -eq 1 ]; then
    echo "==> Running tests"
    ctest --test-dir "${BUILD_DIR}" --output-on-failure
fi

echo "==> Done."
