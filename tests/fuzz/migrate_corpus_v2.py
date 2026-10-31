#!/usr/bin/env python3
# SPACE GL - 3D LOGIC ENGINE
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
"""
Migrate the libFuzzer-grown seeds of tests/fuzz/corpus (the files named by
their SHA-1) from protocol v1 to protocol v2 (2026.10.31.08-.10), so the
corpus keeps reaching the code behind the login:

  - a valid v1 hello (pubkey_len 64, magic XOR the harness's zero key)
    becomes a v2 hello (Session, gen_corpus.py); an invalid v1 hello becomes
    a v2 hello with a forged MAC (still refused, as before);
  - PKT_QUERY / PKT_LOGIN bodies grow from 184 to 216 bytes (ed25519_pubkey)
    and, after a valid hello, their credentials are masked under the session
    key; a PKT_LOGIN of a name not yet queried in the session gets the
    PKT_QUERY the real client sends first (2026.10.31.04);
  - every other packet is copied as is; at the first truncated packet or
    unknown type the rest of the seed is copied verbatim (as the server
    would stop there).

The named seeds are produced by gen_corpus.py and are left alone. Run once:
    python3 tests/fuzz/migrate_corpus_v2.py [corpus_dir]
"""

import hashlib
import importlib.util
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("gen_corpus", os.path.join(HERE, "gen_corpus.py"))
gc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gc)

MAGIC_V1 = b"SPACEGL-KEY-VERIFICATION-SIG-32B"
BODY_HANDSHAKE = 4 + 256
BODY_LOGIN_V1 = 184
BODY_COMMAND = 256
BODY_MESSAGE_FIXED = 219
BODY_QUERY_KEY = 100
MSG_LEN_OFFSET = 76          # int32 length inside the PKT_MESSAGE body


def migrate(data):
    out = bytearray()
    pos = 0
    session = None
    queried = set()
    n_hello = 0
    while pos + 4 <= len(data):
        t = struct.unpack_from("<i", data, pos)[0]
        body_at = pos + 4
        if t == gc.T_HANDSHAKE:
            if body_at + BODY_HANDSHAKE > len(data):
                break
            plen = struct.unpack_from("<i", data, body_at)[0]
            pub = data[body_at + 4:body_at + 4 + 256]
            if plen == 64 and pub[32:64] == MAGIC_V1:
                n_hello += 1
                session = gc.Session(nonce=bytes([0x20 + (n_hello & 0x0f)]) * 32)
                queried = set()
                out += session.hello()
            elif plen == gc.HS_V2_MARKER:
                session = None
                out += data[pos:body_at + BODY_HANDSHAKE]
            else:
                session = None
                out += gc.handshake(mac=b"\x5a" * 32)
            pos = body_at + BODY_HANDSHAKE
        elif t in (gc.T_QUERY, gc.T_LOGIN):
            if body_at + BODY_LOGIN_V1 > len(data):
                break
            body = data[body_at:body_at + BODY_LOGIN_V1] + b"\x5e" * 32
            name = body[:64]
            if session is not None:
                if t == gc.T_LOGIN and name not in queried:
                    out += session._masked(struct.pack("<i", gc.T_QUERY) + body)
                    queried.add(name)
                if t == gc.T_QUERY:
                    queried.add(name)
                out += session._masked(struct.pack("<i", t) + body)
            else:
                out += struct.pack("<i", t) + body
            pos = body_at + BODY_LOGIN_V1
        elif t == gc.T_COMMAND:
            if body_at + BODY_COMMAND > len(data):
                break
            out += data[pos:body_at + BODY_COMMAND]
            pos = body_at + BODY_COMMAND
        elif t == gc.T_QUERY_KEY:
            if body_at + BODY_QUERY_KEY > len(data):
                break
            out += data[pos:body_at + BODY_QUERY_KEY]
            pos = body_at + BODY_QUERY_KEY
        elif t == gc.T_MESSAGE:
            if body_at + BODY_MESSAGE_FIXED > len(data):
                break
            ln = struct.unpack_from("<i", data, body_at + MSG_LEN_OFFSET)[0]
            end = body_at + BODY_MESSAGE_FIXED + (ln if 0 <= ln <= 65535 else 0)
            if not (0 <= ln <= 65535) or end > len(data):
                break
            out += data[pos:end]
            pos = end
        else:
            break
    out += data[pos:]            # the unparsed rest, verbatim
    return bytes(out)


def main():
    corpus = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "corpus")
    hashed = [f for f in sorted(os.listdir(corpus)) if re.fullmatch(r"[0-9a-f]{40}", f)]
    written = set()
    changed = 0
    for f in hashed:
        path = os.path.join(corpus, f)
        with open(path, "rb") as fh:
            data = fh.read()
        new = migrate(data)
        name = hashlib.sha1(new).hexdigest()
        if new != data:
            changed += 1
        os.remove(path)
        if name not in written:
            with open(os.path.join(corpus, name), "wb") as fh:
                fh.write(new)
            written.add(name)
    print(f"{len(hashed)} hashed seeds, {changed} migrated, {len(written)} written")


if __name__ == "__main__":
    main()
