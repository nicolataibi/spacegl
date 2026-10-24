#!/usr/bin/env python3
# =========================================================================
# SpaceGL - object type lint (2026.10.24.02).
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
#
# The `type` of every object the server sends to the clients must come from
# include/object_types.h (OBJ_* names) and not be a bare number: a type that
# lives only as a literal in logic.c is invisible to the clients' tables and
# ends up drawn as a generic sphere (that is how the derelict, type 22, went
# unnoticed in the GPU-driven renderer).
#
# Checks (exit status 1 on failure):
#   1. src/server/logic.c: no NetObject initialiser with a numeric type
#      (positional 7th field or designated `.type = N`);
#   2. every OBJ_* name used in src/server/logic.c exists in object_types.h;
#   3. the object_types.h list has unique values.
# =========================================================================
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
header = (root / "include" / "object_types.h").read_text()
logic = (root / "src" / "server" / "logic.c").read_text()

entries = re.findall(r"X\((OBJ_\w+),\s+(\d+),", header)
names = {n for n, _ in entries}
values = [int(v) for _, v in entries]
errors = []

if len(values) != len(set(values)):
    errors.append("object_types.h: duplicated type values")


def split_top(body):
    out, depth, cur, quote = [], 0, "", False
    for ch in body:
        if ch == '"':
            quote = not quote
        if not quote:
            if ch in "({[":
                depth += 1
            elif ch in ")}]":
                depth -= 1
            elif ch == "," and depth == 0:
                out.append(cur)
                cur = ""
                continue
        cur += ch
    out.append(cur)
    return out


for m in re.finditer(r'\(NetObject\)\{((?:[^{}"]|"[^"]*")*)\}', logic):
    fields = split_top(m.group(1))
    line = logic.count("\n", 0, m.start()) + 1
    if fields[0].strip().startswith("."):
        t = re.search(r"\.type\s*=\s*([^,}]+)", m.group(1))
        expr = t.group(1).strip() if t else None
    elif len(fields) > 6:
        expr = fields[6].strip()
    else:
        continue
    if expr is not None and re.fullmatch(r"\d+", expr):
        errors.append(f"logic.c:{line}: NetObject type is the bare number {expr}"
                      " (use an OBJ_* name from object_types.h)")

for used in set(re.findall(r"\bOBJ_[A-Z0-9_]+\b", logic)):
    if used in names or used in ("OBJ_EXOTIC_FIRST", "OBJ_EXOTIC_COUNT",
                                 "OBJ_EXOTIC_LAST", "OBJ_NPC_FIRST",
                                 "OBJ_NPC_LAST", "OBJ_TYPE_LAST"):
        continue
    errors.append(f"logic.c uses {used}, which object_types.h does not define")

if errors:
    print("object_types lint FAILED:")
    for e in errors:
        print("  " + e)
    sys.exit(1)
print(f"object_types lint OK ({len(values)} types, logic.c clean)")
