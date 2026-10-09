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

## 2. Audit registry (B items)

Status values: `OPEN` / `FIXED` (version that closed it) / `ACCEPTED`
(risk knowingly accepted, rationale recorded) / `NOT-IN-TREE` (no
reference found in sources, tests or changelog).

| ID | Item | Class | Status | Fixed in | Pinned by |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **B1** | `PacketMessage.length` unvalidated on the server: a hostile value (e.g. `-5`) reached `broadcast_message()` and was converted to `size_t` in three sinks (~4 GB copy / EVP read / garbage relayed to every client) — remote DoS | wire DoS | FIXED | 2026.09.29.03 | `tests/pkt_length_test` |
| **B2-a** | HUD heading displayed above 360° (and below 0) after a turn crossing the 0/360 boundary; the align interpolation re-wrapped the delta but not the result; roll variant of the same defect | display contract | FIXED | 2026.09.24.02 (heading), 2026.09.26.02 (roll) | `tests/nav_heading_test`, `tests/nav_roll_test` |
| **B2-b** | Out-of-bounds key-table read in the server fleet decryption: `crypto_algo` (0..255 wire byte) indexed `algo_keys[22]` unvalidated — remotely range-controlled heap over-read handed to EVP | wire OOB | FIXED | 2026.10.01.01 | guard in `src/server/net.c` (no dedicated test yet — see §3) |
| **B3-a** | Quasar — and 49 other static galactic types — never inserted into the per-quadrant spatial index: absent from the quadrant update, the LRS grid and the object lists | state integrity | FIXED | 2026.09.24.03 | `tests/quad_index_test` |
| **B3-b** | Out-of-bounds write in the client `PKT_UPDATE_DELTA` parser: network object/beam counts used as `read_all` lengths without clamp | wire OOB | FIXED | 2026.10.01.02 | clamps in `src/spacegl_client.c` (no dedicated test yet — see §3) |
| **B4** | CBC padding bound: plaintext accepted up to 65535 while the ciphertext (PKCS-padded) could reach 65552 bytes against `msg->text[65536]`; the resulting `length` also left the [0, 65535] wire range both endpoints re-validate | wire OOB | FIXED | 2026.10.01.03 | bounds in `src/server/net.c` + `src/spacegl_client.c` |
| **B5** | Divergent radio cipher tables (four private copies): the server fleet path knew 4 of the 19 tunable ciphers — fleet messages lost on 15 of 19 frequencies, dropped without ever being delivered; the table also depended on the local OpenSSL provider state | crypto integrity | FIXED | 2026.10.01.04 (table), 2026.10.02.03 (legacy-encryption warning) | `tests/crypto_table_test` |
| **B6** | — no reference in the tree — | — | NOT-IN-TREE | — | — |
| **B7** | — no reference in the tree — | — | NOT-IN-TREE | — | — |
| **B8** | Viewer `mainLoop()` walked the SHM object array with a `MAX_NET_OBJECTS` (1024) bound on a `MAX_OBJECTS` (256) array — latent OOB read, safe only because the client clamps `object_count` before writing the frame (a coupling the viewer cannot see) | SHM OOB | FIXED | 2026.10.01.05 | bound in `src/spacegl_vulkan.c` |
| **B9** | — no reference in the tree — | — | NOT-IN-TREE | — | — |
| **B10** | Startup crash on an iconified window | crash | FIXED | 2026.10.02.03 | — |
| **B11** | — no reference in the tree — | — | NOT-IN-TREE | — | — |
| **B12** | Bridge camera offset applied with row-major access of the column-major `R_ship` matrix — the ship did not translate along its local Y | rendering correctness | FIXED | 2026.10.02.01 | — |
| **B13** | Tactical-object and ion-beam tracking loops in the viewer read the SHM mapping without explicit `MAX_OBJECTS` bounds | SHM OOB | FIXED | 2026.10.02.02 | bounds in `src/spacegl_vulkan.c` |

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
    DoS surface (2026.10.02.04).

## 3. Open items

1.  **Disambiguate the B2/B3 dual labels** (or rename the 2026.10.01.01
    / 2026.10.01.02 findings to free labels) once the original report
    list is at hand.
2.  **B2-b and B3-b have no dedicated contract test yet**: the guards
    are in-tree and code-reviewed, but a small test in the
    `pkt_length_test` style (canonical check + proof that the pre-fix
    sink arithmetic was out of bounds) would pin them. Same for B4.
3.  **Confirm B6/B7/B9/B11**: no reference exists in the current tree;
    if they were handled in an external tracker, record them here with
    their status.
4.  **GDD sync/barrier audit**: the GPU-driven path is now auditable by
    default in Debug builds (validation layer on, see DEVELOPMENT.md §4)
    — the residual open class is "behavior visible only on the GPU",
    which `tests/gdd_mesh_test` covers for the compute passes (bit-exact
    readback) but not for the graphics passes.
