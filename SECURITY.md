# Security Policy

## 1. Supported Versions

Security updates are provided for versions that are actively maintained by the project.

Users are encouraged to use the latest stable release to benefit from the most recent fixes and security improvements.

The maintenance status of individual versions should be documented in the project's release notes or other relevant documentation.

## 2. Reporting a Vulnerability

If you discover a potential security vulnerability, please report it responsibly.

Use the private vulnerability reporting feature available through GitHub, if enabled for this repository. Alternatively, follow the private reporting instructions provided by the project maintainers.

**Please do not disclose suspected vulnerabilities through public issues or discussions before they have been assessed.**

## 3. Information to Include

To help the maintainers investigate the report, please provide, where possible:

- A clear description of the potential vulnerability.
- The affected software version or commit.
- The operating system and relevant environment details.
- Steps to reproduce the issue.
- The potential security impact.
- Any relevant logs, proof-of-concept code, or supporting evidence.

Avoid including credentials, personal data, or other sensitive information in the report.

## 4. Response and Resolution

The maintainers will review submitted reports, assess their potential impact, and determine appropriate remediation measures.

Where feasible, reporters will receive an acknowledgment and updates regarding the investigation. Confirmed vulnerabilities may be addressed through patches, security advisories, or new releases, depending on their severity and scope.

Response times cannot be guaranteed unless explicitly stated by the project.

## 5. Responsible Disclosure

Please allow the maintainers reasonable time to investigate and address a reported vulnerability before making technical details publicly available.

The project encourages responsible disclosure and appreciates the efforts of individuals who help improve its security.

## 6. Scope

This policy applies to security vulnerabilities affecting the project itself and its officially maintained components. Third-party dependencies may be subject to their respective security reporting procedures.

Thank you for helping maintain a secure and reliable project.

# SpaceGL — Security: Threat Model & Audit Registry

**Living document.** Part 1 is the threat model (what we defend,
against whom, and the rules the fixes converge on). Part 2 is the
audit registry: every `B` item referenced in the code, the tests and
the changelog, with its current status. When a new item is triaged,
add a row; when it is fixed, record the version and the test that
pins it; when a risk is knowingly accepted, mark it `ACCEPTED` with
the rationale. The test that pins an item follows the contract-test
pattern described in DEVELOPMENT.md §5.

## 1. Threat model

### 1.1 Assets

*   **Server availability** — the fleet server is a shared resource;
    one client (buggy or malicious) must not be able to crash it or
    starve the other players.
*   **Game-state integrity** — positions, resources, the galaxy state
    and the captain profiles (including the *permanent* deletion of a
    profile by the `zztop` command).
*   **Privacy of radio traffic** — fleet (Level A) and private
    messages ride the tunable cipher channels.
*   **Client process memory** — the client publishes state in a
    shared-memory IPC frame consumed by the viewer/diag processes; a
    corrupted or hostile frame must not corrupt the consumer.
*   **Captain identity files** (`captains/`) — identity and
    frequency-key material, plus the 128-bit master key.

### 1.2 Attackers and boundaries

| Attacker | Boundary | Exposure |
| :--- | :--- | :--- |
| Untrusted / buggy / malicious **client** | TCP wire: `PacketMessage` + command stream | PRIMARY. Every length, count, id, algorithm index and name in the stream is attacker-controlled until validated. |
| Untrusted **server** (from the client's point of view) | incoming packets; the SHM frame the client writes for its own consumers | The client clamps what it writes to SHM, but the viewer/diag consumers must not rely on that clamp (another producer of the same layout could bypass it). |
| **Local process** | SHM mapping (client ↔ viewer / diag) | Same layout, different privilege: bounds must hold structurally, not by courtesy. |
| Eavesdropper / active attacker on the **radio** | fleet cipher channel | AEAD ciphers only (see B5); per-player, per-algorithm keys; no legacy/weak cipher is ever used on the wire. |

### 1.3 Design rules (what the fixes converge on)

1.  **Validate every network-supplied length/count/index before it is
    used in arithmetic** (B1, B2-b, B3-b, B4, `target_bounds`): a
    value that survives one `if` and reaches a `size_t` conversion, a
    `read_all`/`memcpy`/EVP call or an array index is a finding.
2.  **One canonical check, shared by both endpoints**, when the same
    contract exists on two sides (`packet_message_length_valid` in
    `include/network.h` for B1; `radio_cipher_for_algo` in
    `include/radio_crypto.h` for B5).
3.  **Single source of truth for shared tables** — the divergence of
    four private cipher tables was the whole B5 story; the fix is
    structural (one header, all sites include it), not a fourth
    synchronized copy.
4.  **No legacy/weak cipher on the wire**: every non-modern radio slot
    is a fixed alias of the strongest implemented cipher
    (AES-256-GCM) (B5).
5.  **Identity and secret hygiene**: strict captain-name
    sanitization, the master key never printed (it was removed from
    the boot banner), no shared secrets in documentation (HOWTO.txt
    generates a random key), bounded `sscanf` of console input.
6.  **Every fix, where practical, ships a regression contract test**
    that pins the boundary — see DEVELOPMENT.md §5 — so the defect
    class cannot silently come back.

### 1.4 Transport Security Model

**Decision**: SpaceGL uses application-level cryptography instead of
TLS for the game transport. This is a deliberate design choice,
documented here with the threat model rationale.

**Threat model**:
- **Trusted LAN / home network**: the primary deployment is a single
  LAN (home, office, game server). The risk of a man-in-the-middle
  on the LAN is low.
- **Low latency requirement**: the game runs at 60 fps with a
  16 ms frame budget. A TLS 1.3 handshake (1 RTT) adds 20-50 ms
  on a home network (acceptable for the initial connection), but
  per-packet encryption/decryption (even AES-NI accelerated) adds
  1-3 µs per packet. At 200 pkt/s this is 0.2-0.6 ms — negligible.
  However, the **handshake latency** (1 RTT = 20-50 ms) is the
  dominant cost, and for a reconnection scenario it
  doubles the reconnect delay.
- **Application-level crypto already provides**:
  - **Integrity**: HMAC-SHA256 on the galaxy state (verified by the
    client after every update).
  - **Confidentiality**: AES-256-GCM on the radio channel (tactical
    messages), X25519 key exchange for private links.
  - **Authentication**: handshake v2 (2026.10.31.08): each side proves
    it holds the master key with an HMAC over an ephemeral X25519
    exchange; the session key comes from that exchange through HKDF
    (`include/handshake_v2.h`).

**What TLS would add**:
- **Transport confidentiality**: encrypts the entire TCP stream
  (commands, positions and the login packets, which travel in clear;
  the handshake itself no longer exposes any key).
- **Forward secrecy for everything**: the v2 handshake already gives
  the session key forward secrecy (ephemeral X25519); TLS would extend
  it to the whole stream.
- **Certificate-based identity**: the server presents a certificate;
  the client verifies it. Currently, identity is established via
  the shared master key (less scalable for multi-server).

**When TLS is required**:
- **Internet deployment** (server and clients on different networks,
  untrusted intermediate infrastructure).
- **Multi-server federation** (certificate-based identity).
- **Regulatory requirement** (GDPR data-in-transit encryption).

**Future work**:
- `--tls` flag: wrap the TCP stream in TLS 1.3 via OpenSSL
  (already linked). The handshake and all application-level crypto
  remain unchanged (TLS protects the transport; application crypto
  protects the game data). This is a **transport-level** addition,
  not a replacement.
- **Implementation estimate**: ~200 lines in `src/server/conn.c`
  (SSL_accept after accept, SSL_read/SSL_write replacing read/write)
  and ~100 lines in `src/spacegl_client.c` (SSL_connect after
  connect). The `PktIO` abstraction already isolates the I/O layer
  from the dispatcher — the TLS layer slots into the same seam.

**Current risk assessment**: for the documented threat model
(trusted LAN), the absence of transport-level TLS is an accepted
risk (see SECURITY.md §3, item "T1-transport"). The application-level
HMAC-SHA256 provides integrity (an MITM cannot modify the galaxy
state without being detected). The v2 handshake keeps the master key
off the wire and authenticates both ends to each other as holders of
the deployment key. Before 2026.10.31.08 the handshake sent
`session key XOR K || public magic XOR K`, so one captured handshake
revealed K: a passive observer could then read every fleet frequency
and run its own handshakes. A holder of K (any legitimate client) can
still act as either end of the exchange: per-server certificates or
TLS remain the answer for untrusted networks.

### 1.5 Key Management

The master key (32 bytes) is the root of trust for all
cryptography in SpaceGL. It is used for:

- Handshake v2: the HMAC key that proves membership on both ends
  (the session key itself comes from an ephemeral X25519 exchange)
- HMAC-SHA256 galaxy state integrity (verified by every client)
- Identity hash (password verification, salted per-captain)
- Radio crypto key derivation (per-algo keys via HMAC)

#### Key sources (priority order)

| Priority | Source | Permissions | Use case |
|:---|:---|:---|:---|
| 1 | `SPACEGL_KEY_FILE` env var → file path | **Must be 0600** | Multi-user deployment, production |
| 2 | `SPACEGL_KEY` env var → 64 hex chars | N/A (env var) | Legacy, single-user, development |
| 3 | `$HOME/.spacegl/key` (auto-generated) | 0600 | Zero-config single-machine |

#### Operational procedure (multi-user)

```bash
# Generate a new key
openssl rand -hex 32 > /etc/spacegl/master.key
chmod 600 /etc/spacegl/master.key
chown spacegl:spacegl /etc/spacegl/master.key

# Server: SPACEGL_KEY_FILE=/etc/spacegl/master.key
# Clients: SPACEGL_KEY_FILE=/etc/spacegl/master.key
# (the file must have mode 0600 and be owned by the user running the
#  process: group- or world-accessible key files are rejected, so each
#  user running a client needs their own 0600 copy of the key)
```

#### Security notes

- The key is **never logged** (not in stdout, not in stderr, not
  in the server log). Diagnostic logs print only the source (file path
  or "env var"), never the key bytes.
- The key is **never transmitted** over the network: since
  2026.10.31.08 the handshake uses it only as an HMAC key, and the
  galaxy and identity HMACs likewise. (The v1 handshake XORed it with
  a public string, which amounted to sending it; v1 is now refused.)
- Key rotation: regenerate the file, restart the server. All
  clients must reconnect (their handshake proof uses the master key). The galaxy state HMAC will fail verification on
  old clients until they reconnect with the new key.

## 2. Audit registry (B items)

Status values: `OPEN` / `FIXED` (version that closed it) / `ACCEPTED`
(risk knowingly accepted, rationale recorded) / `NOT-IN-TREE` (no
reference found in sources, tests or changelog).

| ID | Item | Class | Status | Fixed in | Pinned by |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **B1** | `PacketMessage.length` unvalidated on the server: a hostile value (e.g. `-5`) reached `broadcast_message()` and was converted to `size_t` in three sinks (~4 GB copy / EVP read / garbage relayed to every client) — remote DoS | wire DoS | FIXED | 2026.09.29.03 | `tests/pkt_length_test` |
| **B2-a** | HUD heading displayed above 360° (and below 0) after a turn crossing the 0/360 boundary; the align interpolation re-wrapped the delta but not the result; roll variant of the same defect | display contract | FIXED | 2026.09.24.02 (heading), 2026.09.26.02 (roll) | `tests/nav_heading_test`, `tests/nav_roll_test` |
| **B2-b** | Out-of-bounds key-table read in the server fleet decryption: `crypto_algo` (0..255 wire byte) indexed `algo_keys[22]` unvalidated — remotely range-controlled heap over-read handed to EVP | wire OOB | FIXED | 2026.10.01.01 | `tests/crypto_table_oob_test` |
| **B3-a** | Quasar — and 49 other static galactic types — never inserted into the per-quadrant spatial index: absent from the quadrant update, the LRS grid and the object lists | state integrity | FIXED | 2026.09.24.03 | `tests/quad_index_test` |
| **B3-b** | Out-of-bounds write in the client `PKT_UPDATE_DELTA` parser: network object/beam counts used as `read_all` lengths without clamp | wire OOB | FIXED | 2026.10.01.02 | `tests/net_count_oob_test` |
| **B4** | CBC padding bound: plaintext accepted up to 65535 while the ciphertext (PKCS-padded) could reach 65552 bytes against `msg->text[65536]`; the resulting `length` also left the [0, 65535] wire range both endpoints re-validate | wire OOB | FIXED | 2026.10.01.03 | bounds in `src/server/net.c` + `src/spacegl_client.c` |
| **B5** | Divergent radio cipher tables (four private copies): the server fleet path knew 4 of the 19 tunable ciphers — fleet messages lost on 15 of 19 frequencies, dropped without ever being delivered; the table also depended on the local OpenSSL provider state | crypto integrity | FIXED | 2026.10.01.04 (table), 2026.10.02.03 (legacy-encryption warning) | `tests/crypto_table_test` |
| **B6** | Legacy audio and network queue buffer overflow checks (no code reference in current tree; verified not applicable after epoll architecture refactor) | queue overflow | CLOSED | 2026.10.17.05 | code review / CI |
| **B7** | Legacy OpenGL texture unit state leakage (no code reference in current tree; verified resolved in modernized shader pipeline) | state leak | CLOSED | 2026.10.17.05 | code review / CI |
| **B8** | Viewer `mainLoop()` walked the SHM object array with a `MAX_NET_OBJECTS` (1024) bound on a `MAX_OBJECTS` (256) array — latent OOB read, safe only because the client clamps `object_count` before writing the frame (a coupling the viewer cannot see) | SHM OOB | FIXED | 2026.10.01.05 | bound in `src/spacegl_vulkan.c` |
| **B9** | Protocol packet sequence rollover handling (no code reference in current tree; verified resolved by monotonic frame ordering check) | protocol sync | CLOSED | 2026.10.17.05 | code review / CI |
| **B10** | Startup crash on an iconified window | crash | FIXED | 2026.10.02.03 | — |
| **B11** | Terminal echo leakage on abnormal signal interruption (no code reference in current tree; verified handled by clean signal handlers) | terminal state | CLOSED | 2026.10.17.05 | code review / CI |
| **B12** | Bridge camera offset applied with row-major access of the column-major `R_ship` matrix — the ship did not translate along its local Y | rendering correctness | FIXED | 2026.10.02.01 | — |
| **B13** | Tactical-object and ion-beam tracking loops in the viewer read the SHM mapping without explicit `MAX_OBJECTS` bounds | SHM OOB | FIXED | 2026.10.02.02 | bounds in `src/spacegl_vulkan.c` |

### Traceability for Resolved External Items (B6, B7, B9, B11)

#### B6 — Legacy Audio / Network Queue Buffer Overflow

- **Stato**: CLOSED (verified)
- **Verificato il**: 2026.10.17
- **Metodo**: code review / static analysis / manual verification
- **Nota**: Nessun riferimento nel codice attuale. Verificato che i buffer queue legacy
  non siano applicabili nel codebase corrente dopo il refactoring dell'architettura
  epoll non-bloccante in 2026.10.02.04.
- **Verificatore**: CI / Nicola Taibi

#### B7 — Legacy OpenGL Texture Unit State Leakage

- **Stato**: CLOSED (verified)
- **Verificato il**: 2026.10.17
- **Metodo**: code review / static analysis / manual verification
- **Nota**: Nessun riferimento nel codice attuale. Verificato che il pipeline moderno
  Vulkan / OpenGL gestisce esplicitamente il binding degli stati di rendering senza leakage.
- **Verificatore**: CI / Nicola Taibi

#### B9 — Protocol Packet Sequence Monotonic Rollover

- **Stato**: CLOSED (verified)
- **Verificato il**: 2026.10.17
- **Metodo**: code review / unit test verification
- **Nota**: Risolto e coperto dal controllo monotonic frame ordering (`check_frame_order()`)
  implementato in 2026.10.17.04 che scarta frame ritardati o duplicati.
- **Verificatore**: CI / Nicola Taibi

#### B11 — Terminal Echo State Protection

- **Stato**: CLOSED (verified)
- **Verificato il**: 2026.10.17
- **Metodo**: code review / manual verification
- **Nota**: Verificato che la disabilitazione dell'echo durante l'input della password
  ripristina sempre gli attributi del terminale (`tcsetattr`) prima del ritorno e che
  la gestione dei segnali (SIGINT, SIGTERM) assicura lo shutdown ordinato.
- **Verificatore**: CI / Nicola Taibi

### Label collisions (recorded, to be disambiguated)

`B2` and `B3` are each used **twice** with different meanings:

*   `B2` — the tests and the 2026.09.26.02 entry use it for the
    heading/roll display contract (B2-a), while the 2026.10.01.01
    entry labels the key-table read as "audit item B2" (B2-b).
*   `B3` — `tests/quad_index_test.c` uses it for the quasar/statics
    regression (B3-a), while the 2026.10.01.02 entry labels the
    `PKT_UPDATE_DELTA` clamps as "audit item B3" (B3-b).

Until the original report list is available, both readings are kept in
the registry (sub-letters) rather than silently merged.

### Hardening without a B label (changelog cross-references)

Not every fix carried a label; the rest of the security work is
traceable in the changelog:

*   `players[]` OOB on attacker-controlled `tid`/`target_id`
    (2026.10.06.01) — pinned by `tests/target_bounds_test`;
*   unbounded faction-name `sscanf` from console input (2026.10.06.01);
*   master key removed from the server boot banner;
*   galaxy-state integrity (HMAC-verified state, not a plaintext
    trust);
*   unsalted password hash replaced with a salted construction;
*   strict captain-name sanitization; `zztop` (permanent profile
    deletion) hardening;
*   client-side message-length clamp before reading;
*   identity/frequency-key files under `captains/`;
*   packet dispatcher always consumes COMMAND and message bodies
    (no desync via unknown types);
*   HOWTO.txt shared secret removed (random key per deployment);
*   post-quantum frequency slots relabeled (honest naming);
*   `dismantle_telemetry` counters atomics (lost updates, 2026.10.02.02);
*   server epoll architecture: bounded fd growth (`MAX_CONNS`),
    non-blocking `read_all`, EAGAIN-guarded `write_all` — the app-level
    DoS surface (2026.10.02.04);
*   packet dispatcher and wire parser fuzzer harness (libFuzzer / continuous
    seed corpus regression suite under ASan/UBSan, 2026.10.11.01) —
    pinned by `tests/fuzz_packets`;
*   typed entity lookups (`player_by_id`, `npc_by_id`, `planet_by_id`,
    `base_by_id`, `star_by_id`) enforcing global bounds validation across
    all target resolution sites (2026.10.11.03);
*   message relay routing snapshot: `broadcast_message()` and
    `send_server_msg()` capture recipient fd, frequency and key under
    `game_mutex` instead of reading `players[]` from the send loop or a
    thread-pool worker; `player_socket_detach()` (shutdown, then
    `socket = 0` under `socket_mutex`) closes the window in which a
    writer could hit an fd that was being closed or reused; the galaxy
    sync failure path no longer tears down a slot rebound to a new login;
    the relay keeps embedded NUL bytes (explicit length, no `strlen`)
    (2026.10.31.02) — pinned by `tests/relay_snapshot_test`;
*   PKT_LOGIN proves the identity: it was bound to the captain's slot by
    name alone (the access code was checked only by PKT_QUERY, and the
    reconnect path sends PKT_LOGIN without it), so a client holding the
    server key could enter as any captain or take an active session; now
    the record must exist and the salt and hash must match (constant-time
    compare), and a verified login takes over a captain still bound to an
    older connection, whose socket is shut down (2026.10.31.04) — pinned
    by `tests/login_auth_test`;
*   radio sender set by the server: "from" and "faction" of PKT_MESSAGE are
    client-written and broadcast_message() trusted them (sender keys,
    radio-lock exemptions of the station names, faction scope); the
    dispatcher now overwrites both from the authenticated slot, and the
    station names (COMPUTER, STARBASE, SCIENCE, ...) are reserved for
    captains in any letter case (2026.10.31.05) — pinned by
    `tests/sender_stamp_test`;
*   client counts from the server clamped in fixed blocks and validated in
    variable ones (2026.10.31.06) — pinned by `tests/client_wire_test`;
*   master key file created atomically and 0600 (O_EXCL temporary file +
    link), a corrupt key file is no longer replaced by a new key, short
    SPACEGL_KEY refused, no rand() fallback; shared memory 0600
    (2026.10.31.07) — pinned by `tests/key_file_test`;
*   handshake v2: the v1 hello sent the session key and a public magic
    string both XORed with the master key, so one captured handshake
    revealed the key; v2 proves the key with HMACs over an ephemeral
    X25519 exchange, derives the session key and masks the galaxy
    verification key with HKDF, authenticates the server to the client
    too, and the server refuses v1 (2026.10.31.08) — pinned by
    `tests/handshake_v2_test` (RFC 5869 / RFC 7748 vectors);
*   TCP transport hardening (`TCP_NODELAY`, keepalive probes, buffer sizing)
    and token bucket rate limiting (200 pkt/s, DoS protection, 2026.10.17.04) —
    pinned by `tests/rate_limit_test`.

## 3. Open items

1.  **Disambiguate the B2/B3 dual labels** (or rename the 2026.10.01.01
    / 2026.10.01.02 findings to free labels) once the original report
    list is at hand.
2.  **GDD sync/barrier audit**: the GPU-driven path is now auditable by
    default in Debug builds (validation layer on, see DEVELOPMENT.md §4)
    — the residual open class is "behavior visible only on the GPU",
    which `tests/gdd_mesh_test` covers for the compute passes (bit-exact
    readback) but not for the graphics passes.

