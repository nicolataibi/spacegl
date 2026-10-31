#!/usr/bin/env python3
# SPACE GL - 3D LOGIC ENGINE
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
"""
Move the seeds of tests/fuzz/corpus from wire revision 3 to revision 4
(2026.10.31.43, the server identity in the handshake reply). Only the hello
changes for the deck -> server direction: its pubkey_len marker "HSV3"
becomes "HSV4" (the server refuses a revision-3 hello). Every PKT_HANDSHAKE
packet of a seed is walked, packet by packet as the server reads them (the
same parser as migrate_corpus_v2.py); its marker is rewritten when it is
"HSV3"; every other byte is kept. A seed whose stream cannot be parsed to
the end is still rewritten up to the point where the server would stop.

Only the libFuzzer-grown seeds (named by a 40-digit SHA-1) are migrated;
the named seeds come from gen_corpus.py (which writes revision 4 and the
new handshake_rev3_refused.pkt, a revision-3 hello kept on purpose); run
both, once, in any order:
    python3 tests/fuzz/gen_corpus.py tests/fuzz/corpus
    python3 tests/fuzz/migrate_corpus_rev4.py tests/fuzz/corpus
The seeds keep their file names (libFuzzer does not require the name to be
the SHA-1 of the content).
"""

import os
import re
import struct
import sys

T_LOGIN, T_COMMAND, T_MESSAGE, T_QUERY, T_HANDSHAKE, T_QUERY_KEY = 1, 2, 4, 5, 6, 8
MARK_REV3 = struct.pack("<i", 0x33565348)   # "HSV3"
MARK_REV4 = struct.pack("<i", 0x34565348)   # "HSV4"
BODY = {T_HANDSHAKE: 4 + 256, T_LOGIN: 216, T_QUERY: 216, T_COMMAND: 256,
        T_QUERY_KEY: 64 + 32 + 4}
MSG_FIXED = 219   # PacketMessage body before the text (223 - the type)


def migrate(data):
    out = bytearray(data)
    off = 0
    changed = 0
    while off + 4 <= len(data):
        (t,) = struct.unpack_from("<i", data, off)
        body = off + 4
        if t == T_HANDSHAKE:
            if body + BODY[t] > len(data):
                break
            if data[body:body + 4] == MARK_REV3:
                out[body:body + 4] = MARK_REV4
                changed += 1
            off = body + BODY[t]
        elif t in BODY:
            off = body + BODY[t]
        elif t == T_MESSAGE:
            if body + MSG_FIXED > len(data):
                break
            (length,) = struct.unpack_from("<i", data, body + 64 + 4 + 4 + 4)
            if length < 0 or length > 65535:
                break
            off = body + MSG_FIXED + length
        else:
            break
    return bytes(out), changed


def main():
    corpus = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus")
    files = changed_files = hellos = 0
    for name in sorted(os.listdir(corpus)):
        path = os.path.join(corpus, name)
        if not os.path.isfile(path) or not re.fullmatch(r"[0-9a-f]{40}", name):
            continue
        files += 1
        with open(path, "rb") as f:
            data = f.read()
        new, n = migrate(data)
        if n:
            with open(path, "wb") as f:
                f.write(new)
            changed_files += 1
            hellos += n
    print(f"migrate_corpus_rev4: {files} seeds, {changed_files} rewritten ({hellos} hellos HSV3 -> HSV4)")


if __name__ == "__main__":
    main()
