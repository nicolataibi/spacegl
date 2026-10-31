#!/usr/bin/env bash
#
# SPACE GL - 3D LOGIC ENGINE
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# format_check.sh — clang-format (.clang-format) on the lines changed since
# a base commit (2026.10.31.35, roadmap phase 4). Only the changed lines are
# checked: the tree is not reformatted in one go (that is a one-shot
# command, DEVELOPMENT.md section 9), so a commit is asked to format the
# lines it touches and nothing else.
#
# Usage: format_check.sh [base]     (default HEAD~1; CI: the pushed range)
# Fix:   git clang-format <base>    (formats those lines in place)
# Exit:  0 pass, 1 fail, 77 skip (git-clang-format not installed).
set -u
BASE=${1:-HEAD~1}
if ! command -v git-clang-format >/dev/null 2>&1; then
    echo "SKIP: git-clang-format not installed"
    exit 77
fi
if ! git rev-parse --verify --quiet "$BASE^{commit}" >/dev/null; then
    echo "SKIP: base '$BASE' is not a commit"
    exit 77
fi
out=$(git clang-format --diff --extensions c,h,inc,inl "$BASE" -- src include tests 2>&1)
case "$out" in
    ""|*"no modified files to format"*|*"did not modify any files"*)
        echo "PASS: format_check (lines changed since $(git rev-parse --short "$BASE"))"
        exit 0 ;;
esac
printf '%s\n' "$out"
echo "FAIL: format_check: run 'git clang-format $BASE' and commit the result"
exit 1
