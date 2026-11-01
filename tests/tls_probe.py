#!/usr/bin/env python3
# SPACE GL - 3D LOGIC ENGINE
# Copyright (C) 2026 Nicola Taibi
# License: GPL-3.0-or-later
"""
tls_probe.py — what a network peer sees of the game port (wire revision 5,
2026.11.01.11). Used by tests/deck_e2e_test.sh step 9 against the real
server:
  1. TLS 1.3 is negotiated, and the certificate's key is the server's
     Ed25519 identity (the key the decks pin);
  2. a client limited to TLS 1.2 is refused;
  3. a hello in clear (a deck of wire revision 4) gets no byte back and the
     connection is closed;
  4. bytes that are not a TLS record are not answered either.
Usage: tls_probe.py <host> <port> <identity public key, 64 hex digits>
Exit 0 when every check holds, 1 otherwise (one line per check).
"""

import socket
import ssl
import struct
import sys

ED25519_SPKI = bytes.fromhex("302a300506032b6570032100")


def client_ctx(max_version=None):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE   # no CA: the deck pins the key itself
    if max_version is not None:
        ctx.maximum_version = max_version
    return ctx


def closed_without_reply(host, port, payload, timeout=8.0):
    """Send payload in clear; True when the server closes without a byte."""
    with socket.create_connection((host, port), timeout=timeout) as s:
        s.sendall(payload)
        try:
            data = s.recv(4096)
        except ConnectionResetError:
            return True
        except socket.timeout:
            return False
        return data == b""


def main():
    host, port, want = sys.argv[1], int(sys.argv[2]), bytes.fromhex(sys.argv[3])
    ok = True

    def report(label, cond):
        nonlocal ok
        print(("  ok   " if cond else "  FAIL ") + label)
        ok = ok and cond

    # 1. TLS 1.3 and the identity key in the certificate
    ctx = client_ctx()
    with socket.create_connection((host, port), timeout=10) as raw:
        with ctx.wrap_socket(raw) as s:
            version = s.version()
            der = s.getpeercert(binary_form=True) or b""
            cipher = s.cipher()[0]
    i = der.find(ED25519_SPKI)
    key = der[i + len(ED25519_SPKI):i + len(ED25519_SPKI) + 32] if i >= 0 else b""
    report(f"TLS 1.3 negotiated ({version}, {cipher})", version == "TLSv1.3")
    report("the certificate's key is the server identity", key == want)

    # 2. TLS 1.2 refused
    refused = False
    try:
        with socket.create_connection((host, port), timeout=10) as raw:
            with client_ctx(ssl.TLSVersion.TLSv1_2).wrap_socket(raw):
                pass
    except (ssl.SSLError, ConnectionResetError, OSError):
        refused = True
    report("a TLS 1.2 client is refused", refused)

    # 3. the hello of a deck in clear (PKT_HANDSHAKE, "HSV4", zeros)
    hello = struct.pack("<ii", 6, 0x34565348) + bytes(256)
    report("a hello in clear gets nothing back and is closed", closed_without_reply(host, port, hello))

    # 4. a stray line of text
    report("bytes that are not TLS get nothing back", closed_without_reply(host, port, b"GET / HTTP/1.0\r\n\r\n"))

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
