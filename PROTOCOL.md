# SPACE GL — Network protocol

Reference for the binary protocol between the command deck (`spacegl_client`)
and the server (`spacegl_server`), as implemented in `include/network.h`,
`include/handshake_v2.h`, `src/server/packets.c`, `src/server/net.c` and
`src/spacegl_client.c`. Current protocol: **handshake v2** (2026.10.31.08).
Sizes below are those of this build (x86-64, `#pragma pack(1)`).

## 1. Transport and framing

- TCP, port **5000** (`DEFAULT_PORT`), one connection per captain.
- All structures are packed (`#pragma pack(push, 1)`) and sent in the host
  byte order of both ends (little endian on every supported platform).
- Every packet starts with an `int32 type`. The body that follows has a
  fixed size known from the type, except:
  - `PKT_MESSAGE`: a fixed header (223 bytes including the type) followed by
    `length` bytes of text, `0 <= length <= 65535`
    (`packet_message_length_valid`); any other length closes the connection;
  - the server-to-client update packets, whose payload size depends on the
    blocks they carry (section 6).
- An unknown type closes the connection: the stream cannot be resynchronised.

## 2. Packet types

| Type | Value | Direction | Size (bytes) | Purpose |
| --- | --- | --- | --- | --- |
| `PKT_HANDSHAKE` | 6 | C→S, S→C | 264 / 4 + 128 | Authenticated key exchange (section 3) |
| `PKT_QUERY` | 5 | C→S, S→C | 220 / 4 (+16) | Identity check and enrollment (section 4) |
| `PKT_LOGIN` | 1 | C→S | 220 | Bind the connection to a captain (section 4) |
| `PKT_QUERY_KEY` | 8 | C→S, S→C | 104 | Look up a captain's X25519 key for a peer link |
| `PKT_COMMAND` | 2 | C→S | 260 | One console command, `char cmd[256]`, NUL-forced |
| `PKT_MESSAGE` | 4 | C↔S | 223 + length | Radio message (section 7) |
| `PKT_UPDATE` | 3 | S→C | up to 204 032 | Full state update (legacy, still accepted by the deck) |
| `PKT_UPDATE_DELTA` | 7 | S→C | 20 + blocks | State update by blocks (section 6) |
| `PKT_LRS_UPDATE` | 9 | S→C | 24 + 177·n | Long-range scan of one quadrant, `n <= 1024` objects |

Right after a successful login the server also sends the galaxy master
snapshot (`SpaceGLGame`, 1 031 051 bytes) with its HMAC-SHA256 signature,
which the deck verifies with the galaxy verification key (section 3).

## 3. Handshake v2

Every connection starts with it; nothing else is accepted before
(`PKT_QUERY`/`PKT_LOGIN` before the handshake are dropped). `K` is the
deployment master key (`SPACEGL_KEY_FILE`, `SPACEGL_KEY` or
`~/.spacegl/key`); it never travels on the wire.

```
C -> S  PacketHandshake: type = 6, pubkey_len = 0x32565348 ("HSV2")
        pubkey[ 0..31]  eC   client ephemeral X25519 public key
        pubkey[32..63]  Nc   client nonce (32 random bytes)
        pubkey[64..95]  HMAC-SHA256(K, "SPACEGL-HS-V2/client" || eC || Nc)
        pubkey[96..255] 0
S -> C  int32 6, then 128 bytes:
        eS (32) || Ns (32) ||
        HMAC-SHA256(K, "SPACEGL-HS-V2/server" || eC || Nc || eS || Ns || G) (32) ||
        G = galaxy verification key XOR mask (32)
Z            = X25519(e, peer public key)        (all-zero Z refused)
PRK          = HMAC-SHA256(Nc || Ns, Z)          (HKDF-Extract)
session key  = HMAC-SHA256(PRK, "spacegl v2 session key"  || 0x01)
mask         = HMAC-SHA256(PRK, "spacegl v2 galaxy verify" || 0x01)
```

- A hello whose `pubkey_len` is not the v2 marker (the v1 hello) is refused.
- Both sides prove they know `K`; the session key comes from the ephemeral
  exchange (forward secrecy: a later leak of `K` does not open past
  sessions).
- Limit: `K` is shared by every deck of a deployment, so a holder of `K` can
  run the exchange as either side. Per-server keys are future work
  (SECURITY.md).
- A connection that completed the handshake but sends no successful
  `PKT_LOGIN` within **180 s** (renewed by each `PKT_QUERY`) is closed and its
  slot freed (`HANDSHAKE_PENDING_TIMEOUT_S`).

## 4. Identity: PKT_QUERY and PKT_LOGIN

`PacketLogin` (220 bytes): `name[64]` @4, `faction` @68, `ship_class` @72,
`pass_hash[32]` @76, `salt[16]` @108, `reserved[32]` @124,
`x25519_pubkey[32]` @156, `ed25519_pubkey[32]` @188.

- `pass_hash = HMAC-SHA256(K, "SPACEGL-ID-V2" || name || 0 || salt || 0 || password)`;
  `reserved` held the unsalted `HMAC-SHA256(K, password)` until
  2026.10.31.24: it is now sent as zeros and ignored (the layout is
  unchanged). A record without `identity.salt` (the unsalted scheme) is
  refused with status 3: remove `captains/<name>` to enroll again.
- On the wire `pass_hash`, `salt` and `reserved` (80 contiguous
  bytes) are XORed with a mask from the session key and the packet's
  sequence number since the handshake (`hs_v2_cred_xor`): a captured
  packet cannot be replayed on another session.
- Names: `[A-Za-z0-9_-]`, 1 to 32 characters; station names (COMPUTER,
  SCIENCE, STARBASE, ...) are reserved in any letter case.

`PKT_QUERY` reply: `int32 status`, plus `uint8 salt[16]` when status is 5.

| Status | Meaning |
| --- | --- |
| 1 | Known captain, password correct, present in this galaxy |
| 2 | New captain (record just created), or known identity in a new galaxy |
| 3 | Wrong password or wrong salt |
| 4 | Captain already connected |
| 5 | Salt required: retry with the salt that follows (zeros: generate one) |

`PKT_LOGIN` carries the same fields and is verified again against the
stored record (constant-time comparisons); a failed proof closes the
connection. A proved login for a captain still bound to an older connection
takes it over (the older socket is shut down). The `ed25519_pubkey` of the
login becomes the only key with which this captain's radio signatures are
relayed (section 7).

## 5. Rate limits and bounds

- At most **200 packets per second** per connection; packets over the limit
  are dropped, and **10** consecutive drops close the connection
  (`PKT_RATE_LIMIT`, `PKT_DROP_THRESHOLD`).
- Every count read from the wire is clamped to its array before use, on
  both ends: object counts to 1024, beams to 64, events to 64, visible
  torpedoes to 256; negative counts to 0. On the deck an out-of-range
  trailing count closes the connection instead of desynchronising it.

## 6. State updates (S→C)

`PacketUpdateDelta`: `int32 type = 7`, `int64 frame_id`, `uint64 update_mask`,
then one block per set bit, **in this order**:

| Bit | Block | Content |
| --- | --- | --- |
| 0 `UPD_TRANSFORM` | quadrant, sector, heading, mark, roll, ETA |
| 1 `UPD_VITALS` | energy, torpedoes, cargo, crew, prison, plating, hull |
| 2 `UPD_SHIELDS` | 6 shield values |
| 3 `UPD_SYSTEMS` | system health |
| 4 `UPD_INTERNAL` | inventory, power distribution, life support, antimatter |
| 5 `UPD_COMBAT` | lock, tubes, ion beam charge, then `int32 n` + `n` beams (60 B each) |
| 6 `UPD_FLAGS` | cloak, dock, alert, jam, nav state, view toggles, crypto, radio lock |
| 7 `UPD_EFFECTS` | supernova, torpedoes, wormhole, events, visible torpedoes |
| 8 `UPD_PROBES` | 3 probes |
| 9 `UPD_OBJECTS` | `int32 n` + `n` objects (177 B each): the full list of the quadrant |
| 12 `UPD_KINEMATICS` | `int32 n` + `n` positions/velocities (28 B each), when only motion changed |
| 10 `UPD_MAP` | map cell updates |

The full object list is sent when the membership or the metadata of an
object changes, on a quadrant change and at least every 5 s; otherwise only
the kinematics. A tick with no change sends nothing.

## 7. Radio messages

`PacketMessage`: `from[64]`, `faction`, `scope` (0 global, 1 faction,
2 private), `target_id` (1-based player), `length`, `origin_frame`,
`is_encrypted` flags, `crypto_algo`, `iv[16]`, `tag[16]`, `has_signature`,
`signature[64]`, `sender_pubkey[32]`, then `text`.

- `is_encrypted`: `0x01` encrypted, `0x02` private, `0x04` peer (end-to-end
  X25519), `0x08` exclusive. Private, peer and exclusive messages are
  relayed as they are (the server cannot read them); fleet messages are
  decrypted by the server and re-encrypted for each recipient's frequency,
  or sent in clear to a recipient on no frequency.
- `from` and `faction` are overwritten by the server with the authenticated
  captain of the connection; a client cannot speak as another captain or as
  a ship station.
- A signature is relayed only if `sender_pubkey` is the signing key bound
  by that captain's login; otherwise it is removed and the message is
  relayed unsigned.
- The cipher of each `crypto_algo` slot comes from one table shared by both
  ends (`include/radio_crypto.h`); slots 12 to 19 and 21 are experimental
  aliases of AES-256-GCM, not post-quantum ciphers.

## 8. Changing the protocol

Any change to the size or the meaning of a field of a wire structure is a
protocol change: decks and servers of different versions then misread each
other's packets. Bump the handshake marker (a new `HSVn` value in
`pubkey_len`) so that an old peer is refused at the handshake with a clear
error instead of desynchronising later, and update this document and
`tests/fuzz/` (seed generator and corpus) in the same change.
