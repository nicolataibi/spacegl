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
#   ./build.sh -h         # help
set -euo pipefail

BUILD_DIR="build"
BUILD_TYPE="Release"
TESTS_ONLY=0
CLEAN=0

while getopts "drstch" opt; do
    case $opt in
        d) BUILD_TYPE="Debug" ;;
        r) BUILD_TYPE="RelWithDebInfo" ;;
        s) BUILD_TYPE="MinSizeRel" ;;
        t) TESTS_ONLY=1 ;;
        c) CLEAN=1 ;;
        h)
            grep '^#' "$0" | sed 's/^# \{0,1\}//' | tail -n +2
            exit 0 ;;
        *) exit 1 ;;
    esac
done

if [ $CLEAN -eq 1 ]; then
    echo "==> Cleaning ${BUILD_DIR}/"
    rm -rf "${BUILD_DIR}"
fi

if [ $TESTS_ONLY -eq 0 ]; then
    echo "==> Configuring (${BUILD_TYPE})"
    cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

    echo "==> Building"
    cmake --build "${BUILD_DIR}" -j"$(nproc)"
fi

echo "==> Running tests"
ctest --test-dir "${BUILD_DIR}" --output-on-failure

echo "==> Done."
