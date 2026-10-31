#!/usr/bin/env python3
"""
SPACE GL - seed corpus generator for the packet-dispatch fuzzer.
Copyright (C) 2026 Nicola Taibi
License: GPL-3.0-or-later

Generates tests/fuzz/corpus/*.pkt: binary CLIENT SESSION streams for
tests/fuzz_packets.c (the libFuzzer harness / corpus runner). One file
= one session = a sequence of packets, each an int32 type followed by
its wire body, exactly as spacegl_server.c's legacy loop consumes them.

Wire layout (include/network.h, all little-endian, no padding needed):
  header         int32 type
  PacketHandshake i32 type, i32 pubkey_len, u8[256] pubkey
                 (v2: pubkey_len = HSV2 marker, pubkey[0:32) client
                  ephemeral X25519 key, [32:64) nonce, [64:96) HMAC)
  PacketLogin    i32 type, c[64] name, i32 faction, i32 ship_class,
                 u8[32] pass_hash, u8[16] salt,
                 u8[32] reserved, u8[32] x25519_pubkey,
                 u8[32] ed25519_pubkey                               (220 B)
  PacketCommand  i32 type, c[256] cmd                                  (260 B)
  PacketMessage  i32 type, c[64] from, i32 faction, i32 scope,
                 i32 target_id, i32 length, i64 origin_frame,
                 u8 is_encrypted, u8 crypto_algo, u8[16] iv, u8[16] tag,
                 u8 has_signature, u8[64] signature, u8[32] sender_pubkey
                 (223 B fixed) + length text bytes

The fuzzer's MASTER_SESSION_KEY is all-zero (harness default), so a
VALID v2 hello carries HMAC-SHA256(zero key, "SPACEGL-HS-V2/client" ||
eC || Nc) (include/handshake_v2.h); eC is the RFC 7748 test key.
A new-captain PKT_QUERY (unknown name + non-zero salt) creates the
identity records in the harness's temp sandbox; the PKT_LOGIN that follows
carries the same hash and salt, proves the identity (2026.10.31.04: a
PKT_LOGIN without a matching record is refused) and activates the player,
so the following PKT_COMMAND packets reach process_command().

Usage:  python3 tests/fuzz/gen_corpus.py [corpus_dir]
"""

import hashlib
import hmac
import os
import struct
import sys

HS_V2_MARKER = 0x32565348   # "HSV2", include/handshake_v2.h
HARNESS_KEY = b"\x00" * 32   # fuzz harness MASTER_SESSION_KEY (all-zero)
# RFC 7748 section 6.1 test public key: a valid X25519 point, so the
# server's ECDH succeeds and the session goes on.
X25519_PUB = bytes.fromhex(
    "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a")

T_LOGIN = 1
T_COMMAND = 2
T_MESSAGE = 4
T_QUERY = 5
T_HANDSHAKE = 6
T_QUERY_KEY = 8


def hdr(t):
    return struct.pack("<i", t)


def handshake(key=HARNESS_KEY, nonce=b"\x22" * 32, eph=X25519_PUB,
              marker=HS_V2_MARKER, mac=None):
    """Handshake v2 hello: ephemeral key, nonce, HMAC(K, label||eC||Nc)."""
    if mac is None:
        mac = hmac.new(key, b"SPACEGL-HS-V2/client" + eph + nonce,
                       hashlib.sha256).digest()
    pub = eph + nonce + mac + b"\x00" * (256 - 96)
    return hdr(T_HANDSHAKE) + struct.pack("<i", marker) + pub


# The fuzz build (FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION) fixes the
# server's ephemeral key to RFC 7748 6.1 "Bob" and its nonce to 0x44 * 32;
# with the client key "Alice" (X25519_PUB) the shared secret is the RFC's.
RFC7748_SHARED = bytes.fromhex(
    "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742")
FUZZ_SERVER_NONCE = b"\x44" * 32


def _hkdf32(salt, ikm, info):
    prk = hmac.new(salt, ikm, hashlib.sha256).digest()
    return hmac.new(prk, info + b"\x01", hashlib.sha256).digest()


def _cred_mask(session_key, seq):
    out = b""
    i = 1
    while len(out) < 80:
        out += hmac.new(session_key, b"spacegl v2 credentials"
                        + struct.pack("<I", seq) + bytes([i]),
                        hashlib.sha256).digest()
        i += 1
    return out[:80]


class Session:
    """One connection's v2 handshake, then its masked QUERY/LOGIN packets
    (hs_v2_cred_xor: credentials XOR a mask keyed by the session key and
    the packet's sequence number since the handshake)."""

    def __init__(self, nonce=b"\x22" * 32):
        self.nonce = nonce
        self.key = _hkdf32(nonce + FUZZ_SERVER_NONCE, RFC7748_SHARED,
                           b"spacegl v2 session key")
        self.seq = 0

    def hello(self):
        return handshake(nonce=self.nonce)

    def _masked(self, pkt):
        # pkt = header(4) name(64) faction(4) class(4) | creds(80) | x25519(32) | ed25519(32)
        start = 4 + 64 + 8
        creds = bytes(a ^ b for a, b in zip(pkt[start:start + 80],
                                            _cred_mask(self.key, self.seq)))
        self.seq += 1
        return pkt[:start] + creds + pkt[start + 80:]

    def query(self, name, salt=b"\xaa" * 16):
        return self._masked(query(name, salt=salt))

    def login(self, name, salt=b"\xaa" * 16):
        return self._masked(login(name, salt=salt))

    def enroll(self, name, salt=b"\xaa" * 16):
        return self.query(name, salt) + self.login(name, salt)


def session_enroll(name, nonce=b"\x22" * 32):
    """handshake + masked PKT_QUERY + masked PKT_LOGIN, as the client does."""
    s = Session(nonce)
    return s.hello() + s.enroll(name)


def handshake_v1(key=b"\x11" * 32):
    """The refused v1 hello (session key || magic, pubkey_len 64)."""
    pub = key + b"SPACEGL-KEY-VERIFICATION-SIG-32B" + b"\x00" * (256 - 64)
    return hdr(T_HANDSHAKE) + struct.pack("<i", 64) + pub


def login(name, faction=0, ship_class=0, salt=b"\xaa" * 16):
    if isinstance(name, str):
        name = name.encode()
    return (hdr(T_LOGIN) + name.ljust(64, b"\x00")
            + struct.pack("<ii", faction, ship_class)
            + b"\x42" * 32          # pass_hash (v2, salted)
            + salt
            + b"\x24" * 32          # reserved (ignored by the server)
            + b"\x9c" * 32          # x25519_pubkey
            + b"\x5e" * 32)         # ed25519_pubkey (signing key bound at login)


def query(name, faction=0, ship_class=0, salt=b"\xaa" * 16):
    """PKT_QUERY: same body as PKT_LOGIN; creates a new captain's record."""
    return hdr(T_QUERY) + login(name, faction, ship_class, salt)[4:]


def enroll(name, salt=b"\xaa" * 16):
    """What the real client sends: PKT_QUERY (identity) then PKT_LOGIN."""
    return query(name, salt=salt) + login(name, salt=salt)


def command(text):
    if isinstance(text, str):
        text = text.encode()
    body = text.ljust(256, b"\x00")
    return hdr(T_COMMAND) + body


def message(frm, length, text=b"", is_encrypted=0, algo=0):
    if isinstance(frm, str):
        frm = frm.encode()
    fixed = (hdr(T_MESSAGE) + frm.ljust(64, b"\x00")
             + struct.pack("<iiiiq", 0, 0, 0, length, 1)
             + bytes([is_encrypted, algo])
             + bytes((i for i in range(16)))      # iv
             + bytes((i for i in range(16, 32)))  # tag
             + bytes([0])                          # has_signature
             + b"\x00" * 64                        # signature
             + b"\x00" * 32)                       # sender_pubkey
    assert len(fixed) == 223, len(fixed)
    return fixed + text


def query_key(name):
    if isinstance(name, str):
        name = name.encode()
    return (hdr(T_QUERY_KEY) + name.ljust(64, b"\x00")
            + b"\x7d" * 32 + struct.pack("<i", 0))


def write(corpus, name, data):
    with open(os.path.join(corpus, name), "wb") as f:
        f.write(data)
    print(f"  {name:34s} {len(data):8d} bytes")


def main():
    corpus = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus")
    os.makedirs(corpus, exist_ok=True)
    print(f"corpus dir: {corpus}")

    auth = session_enroll(b"SEEDCAPTAIN")

    # 1. Happy path: handshake -> login -> common commands -> message
    #    -> profile deletion (zztop closes the connection; the close is
    #    itself the behavior under test).
    write(corpus, "handshake_login_full.pkt",
          auth
          + command("imp 45 10 0.5")
          + command("pos 90 0 0")
          + command("vec 10 20 0.3")
          + command("nav 15 -10 3 1.5")
          + command("lock off")
          + command("scan 1")
          + command("pha 1")
          + command("tor 1")
          + command("pow 50 30 20")
          + command("enc 1")
          + command("rad Hello fleet")
          + command("map st")
          + command("srs")
          + command("lrs 5")
          + command("who")
          + command("help")
          + message("SEEDCAPTAIN", 18, b"Fleet test message.")
          + command("zztop"))

    # 2. Every registered command, once, with plausible and hostile
    #    arguments (the full parser surface, including invalid IDs and
    #    out-of-range quadrants).
    cmds = [
        "dbgspn", "dbgnpc",
        "nav 359 90 40 9.9", "nav 999999 999999 999999 99",
        "imp 0 0 0", "imp 999999 999999 999999",
        "pos 360 -91 720", "pos 0 0 0",
        "vec 999 999 9.9", "vec 10 20 0",
        "jum 1 1 1 1", "jum 1 0 0 0", "jum 1 41 41 41",
        "jum 99 99 99 99",
        "apr 16 0.5", "apr 17 0.5", "apr 999 0.5",
        "cha 1", "cha 17",
        "srs", "lrs 999",
        "pha 16", "pha 17", "pha 999", "pha",
        "tor 16", "tor 17", "tor",
        "she 50 50 50 50 50 50",
        "lock 16", "lock 17", "lock 999", "lock 0", "lock off",
        "pow 0 0 0", "pow 200 -100 50",
        "psy 1", "scan 0", "scan 17", "scan 999", "scan",
        "clo", "bor 16", "bor 17", "bor 999", "bor",
        "dis 1", "min 1", "sco", "har", "doc 1", "doc",
        "con 1", "con 9", "load 1", "load 9", "rep", "fix",
        "sta", "inv", "dam", "cal", "ical", "who",
        "rad @Alliance hello", "rad #17 hi", "rad",
        "tune SEEDCAPTAIN", "enc 21", "enc2 2",
        "enc3 SEEDCAPTAIN 3", "enc4 SEEDCAPTAIN 4",
        "link SEEDCAPTAIN", "aux probe", "aux report",
        "xxx", "axs", "grd",
        "bridge top", "bridge off",
        "pilot on", "pilot off",
        "map", "map mo", "red",
        "orb 1", "orb 999", "und", "undock",
        "hull", "supernova",
    ]
    write(corpus, "all_commands.pkt", auth + b"".join(command(c) for c in cmds))

    # 3. The B1 wire-boundary seeds: PacketMessage.length edges.
    write(corpus, "message_len_zero.pkt",
          auth + message("SEEDCAPTAIN", 0))
    write(corpus, "message_len_max.pkt",
          auth + message("SEEDCAPTAIN", 65535, b"A" * 65535)
          + command("imp 0 0 0"))
    write(corpus, "message_len_oob_65536.pkt",
          auth + message("SEEDCAPTAIN", 65536, b"A" * 16))
    write(corpus, "message_len_oob_negative.pkt",
          auth + message("SEEDCAPTAIN", -1, b"A" * 16))
    write(corpus, "message_len_oob_intmax.pkt",
          auth + message("SEEDCAPTAIN", 0x7FFFFFFF, b"A" * 16))

    # 4. Every crypto slot with garbage ciphertext: the server decrypt
    #    paths (EVP error handling, AEAD tag failures, per-recipient
    #    relay re-encryption) must survive unparseable payloads.
    msgs = b"".join(
        message("SEEDCAPTAIN", 64,
                bytes((algo * 7 + k) % 251 for k in range(64)), 1, algo)
        for algo in range(0, 22))
    write(corpus, "message_crypto_slots.pkt", auth + msgs)

    # 5. Hostile handshake / login seeds.
    write(corpus, "bad_handshake_sig.pkt",
          handshake(mac=b"\x5a" * 32) + command("who"))
    write(corpus, "handshake_v1_refused.pkt",
          handshake_v1() + Session().enroll(b"SEEDCAPTAIN") + command("who"))
    write(corpus, "handshake_small_order_key.pkt",   # all-zero point
          handshake(eph=b"\x00" * 32) + command("who"))
    write(corpus, "handshake_only.pkt", handshake())
    write(corpus, "double_handshake.pkt",
          handshake() + session_enroll(b"SEEDCAPTAIN", nonce=b"\x33" * 32)
          + command("who"))
    s = Session()
    write(corpus, "login_bad_name_traversal.pkt",
          s.hello() + s.login(b"../evil"))
    s = Session()
    write(corpus, "login_bad_name_dot.pkt",
          s.hello() + s.login(b"bad.name"))
    s = Session()
    write(corpus, "login_bad_name_long.pkt",
          s.hello() + s.login(b"A" * 33))
    s = Session()
    write(corpus, "login_salt_probe.pkt",  # all-zero salt (client probe)
          s.hello() + s.query(b"SEEDCAPTAIN", salt=b"\x00" * 16)
          + s.enroll(b"SEEDCAPTAIN"))
    # PKT_LOGIN without the matching identity: refused, connection closed.
    s = Session()
    write(corpus, "login_without_identity.pkt",
          s.hello() + s.login(b"NOIDENTITY") + command("who"))
    s = Session()
    t = Session(nonce=b"\x55" * 32)
    write(corpus, "login_wrong_salt.pkt",
          s.hello() + s.enroll(b"SEEDCAPTAIN")
          + t.hello() + t.login(b"SEEDCAPTAIN", salt=b"\xbb" * 16)
          + command("who"))
    # credentials not masked (or masked with another session): refused.
    write(corpus, "login_unmasked_credentials.pkt",
          handshake() + enroll(b"SEEDCAPTAIN") + command("who"))
    # QUERY/LOGIN before any handshake: no session key, dropped.
    write(corpus, "login_before_handshake.pkt",
          enroll(b"SEEDCAPTAIN") + command("who"))

    # 6. Unauthenticated commands (p_idx == -1): the body MUST still be
    #    consumed, or the stream desynchronizes.
    write(corpus, "commands_without_auth.pkt",
          b"".join(command(c) for c in
                   ["imp 1 2 3", "pha 16", "zztop", "who"]))

    # 7. Truncations at every stage (EOF / EAGAIN read-failure paths).
    hs = handshake()
    write(corpus, "truncated_handshake.pkt", hs[:4 + 100])
    write(corpus, "truncated_command.pkt",
          auth + hdr(T_COMMAND) + b"\x00" * 50)
    write(corpus, "truncated_message_len.pkt",
          auth + hdr(T_MESSAGE) + b"\x00" * 100)
    write(corpus, "truncated_login.pkt",
          handshake() + login(b"SEEDCAPTAIN")[:4 + 30])

    # 8. Stream-level adversarial seeds.
    write(corpus, "unknown_type.pkt", struct.pack("<i", 0x12345678))
    write(corpus, "unknown_type_after_auth.pkt",
          auth + struct.pack("<i", 42) + command("who"))
    write(corpus, "header_only.pkt", struct.pack("<i", T_COMMAND))
    write(corpus, "empty.pkt", b"")
    write(corpus, "garbage_after_auth.pkt",
          auth + b"\xde\xad\xbe\xef" * 32)

    # 9. Key query round-trip (the x25519 tactical-link lookup).
    write(corpus, "query_key.pkt",
          auth + query_key(b"SEEDCAPTAIN") + query_key(b"NOBODY")
          + command("link NOBODY"))

    # 10. Regression: huge / non-finite angles. normalize_upright() used to
    #     subtract 360 in a loop, so "imp 0 1e12 .." took billions of
    #     iterations and "imp 0 1e300 .." / "inf" never returned (libFuzzer
    #     timeout in CI). Every command that parses H/M with %lf is covered.
    write(corpus, "huge_angles.pkt",
          auth
          + command("imp 999999 9999999999999999 999999")
          + command("imp 0 1e12 0.5")
          + command("imp 0 1e300 0.5")
          + command("vec 0 inf 0.3")
          + command("vec 1e300 -1e300 0.3")
          + command("nav 0 1e15 3 1.5")
          + command("nav nan nan 3 1.5")
          + command("tor 0 1e18")
          + command("rot 0 1e20 1e20"))

    print("done.")


if __name__ == "__main__":
    main()
