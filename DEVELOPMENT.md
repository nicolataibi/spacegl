# SpaceGL — Development Guide

How to build the tree, how to run the test suite, and the pattern
behind the contract tests. The product overview, feature lore and
runtime usage live in README.md; the threat model and the audit
registry live in SECURITY.md.

This is an integrated source tree: the 8 product
targets, the shader sources and the `tests/` unit suite are all built
from here.

## Build System

### Build types

| Type | Flags | Use case |
|:---|:---|:---|
| `Release` (default) | `-O3 -D_FORTIFY_SOURCE=2` | Production, CI, RPM |
| `Debug` | `-O0 -g` (+ ASan/UBSan in tests) | Development, debugging |
| `RelWithDebInfo` | `-O2 -g -D_FORTIFY_SOURCE=2` | Profiling (perf, valgrind) |
| `MinSizeRel` | `-Os -D_FORTIFY_SOURCE=2` | Size-constrained deployment |

### Quick dev loop

```bash
./build.sh          # Release + tests (one command)
./build.sh -d       # Debug + tests (ASan/UBSan)
./build.sh -c       # Clean rebuild + tests
./build.sh -t       # Tests only (skip rebuild)
```

### Standalone test build

```bash
cmake -B build-tests tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tests -j$(nproc)
ctest --test-dir build-tests --output-on-failure
```

### Build directory policy

- Build directories (`build/`, `build-tests/`, `build-tests-clang/`, …) are **never committed**.
- The `.gitignore` covers `build/` and `build-*/` (any variant).
- CI fails if a build directory is found in the git index (`git ls-files | grep build/`).

## 1. Prerequisites

Reference toolchain (Fedora, the project standard): GCC with C23
support (the tree is built warning-clean on GCC 16.2.1), CMake ≥ 3.10
(3.16 for the standalone tests project), pkg-config.

Development packages:

```
dnf install gcc-c++ cmake pkgconf-pkg-config mesa-libGL-devel \
            libGLU-devel glfw-devel ncurses-devel \
            openssl-devel openmp-devel vulkan-loader-devel \
            vulkan-headers glslc
```

**Vulkan SDK resolution is a chain**, implemented in ONE shared module
(`cmake/SpaceGLVulkanSDK.cmake`, included by both the top-level
CMakeLists.txt and the standalone tests/CMakeLists.txt so the two
trees can never drift apart). The first usable entry wins:

1.  the `VULKAN_SDK` environment variable — the explicit per-machine
    override; set and usable it wins over everything, set but unusable
    it warns and the chain continues (a stale export cannot brick the
    build);
2.  the pinned default SDK (project decision for reproducible builds —
    headers, loader and glslc from the same SDK; the default root is the
    maintainer-local `DEFAULT_VULKAN_SDK` cache variable, and when that
    path does not exist the step is skipped silently); opt out with
    `-DUSE_DEFAULT_VULKAN_SDK=OFF`;
3.  the system Vulkan — `find_package(Vulkan)` (official config, else
    CMake's FindVulkan module: libvulkan + system headers), then the
    distro from-source loader package (`VulkanLoader`, exposed as the
    alias `Vulkan::Vulkan`).

`glslc` follows the same chain: the resolved SDK's `bin/` is searched
before `PATH`. It is a hard requirement — there is deliberately no
fallback to glslangValidator.

## 2. Building

```bash
cmake -B build              # Release by default
cmake --build build -j
```

*   **Build types**: `Release` is the default (`-O3 -DNDEBUG`);
    `Debug` (`-DCMAKE_BUILD_TYPE=Debug`) gives `-O0 -g` WITHOUT
    `NDEBUG` — which is also what switches the Vulkan validation layer
    on by default (see §4). An empty cached build type is normalized
    back to Release at configure time.
*   **Global flags**: `-Wall -Wextra -Wpedantic -Wformat
    -Wformat-security -fstack-protector-strong -D_FORTIFY_SOURCE=2`,
    PIE (`-fPIE`/`-pie`) and `relro,now` link flags. The tree builds
    with **zero warnings** — keep it that way (a new warning is a
    regression to fix in the same change that introduced it).
*   **Options**: `-DSPACEGL_BUILD_TESTS=OFF` excludes the unit suite
    from the main build (default ON); `-DUSE_DEFAULT_VULKAN_SDK=OFF`
    opts out of the pinned SDK.
*   **Targets**: 8 product executables (`spacegl_server`,
    `spacegl_client`, `spacegl_3dview`, `spacegl_viewer`,
    `spacegl_hud`, `spacegl_vulkan`, `spacegl_diag`,
    `spacegl_telemetry`) plus `spacegl_shaders`, which compiles the 7
    SPIR-V sources (2 legacy + 5 GDD) with glslc into
    `build/shaders/` (installed to `/usr/share/spacegl/shaders`).
*   **Server sources are an explicit list** (no `file(GLOB)`): a new
    file in `src/server/` must be added to `SERVER_SRCS` in the
    top-level CMakeLists.txt or it is silently not built.
*   **RPM packaging**: `spacegl.spec`. The spec's `%check` runs the very unit
    suite of §3, so a contract failure also fails the package build.

## 3. Running the test suite

The suite has **18 tests**, built in two equivalent modes:

| # | Test | Pins | Production sources under test |
| :-- | :--- | :--- | :--- |
| 1 | `gdd_contract` | CPU/GPU boundary: struct sizes, **per-field offsets**, vec4 16-byte alignment, flag round-trip, instance builder vs synthetic scenes | `src/vulkan_gdd_build.c` (full TU) |
| 2 | `gdd_mesh` | headless GPU: the real cull/expand/final SPIR-V vs a bit-exact CPU mirror (exit 77 = skip without an ICD/SPV) | `build/shaders/gdd_*.spv` (test is self-contained; no production `.c` linked) |
| 3 | `nav_heading` | B2: HUD heading stays in [0, 360) across the 0/360 wrap | `src/server/nav_math.c` |
| 4 | `nav_roll` | HUD roll stays in [0, 360) (`pos` normalization + align re-wrap) | `src/server/nav_math.c` |
| 5 | `quad_index` | B3: all 62 static galactic types (quasar & co.) indexed per quadrant | `src/server/quad_index.c` |
| 6 | `pkt_length` | B1: `PacketMessage.length` [0, 65535] wire contract | `include/network.h` |
| 7 | `crypto_table` | B5: shared radio cipher table, slot→cipher contract, round-trip on all slots | `include/radio_crypto.h` |
| 8 | `target_bounds` | `tid`/`target_id` clamps on `players[]` (ASan/UBSan, canary global) | `src/server/commands.c` + `logic.c` + `nav_math.c` + `targets.c` |
| 9 | `vec_thrust` | the `vec` command contract (ASan/UBSan, same scaffolding) | same as 8 |
| 10 | `fuzz_corpus` | packet dispatcher / session stream fuzzing seeds under ASan/UBSan | `src/server/packets.c` + `net.c` + `conn.c` |
| 11 | `rate_limit` | DoS protection: token-bucket rate limit (200 pkt/s, 10 drops → disconnect, 1 s refill) on the real dispatcher, incl. async staged mode (ASan/UBSan) | `src/server/packets.c` + `net.c` + `conn.c` |
| 12 | `crypto_table_oob` | B2-b: `radio_cipher_for_algo()` index bounds, all 256 algo bytes, canary byte (ASan/UBSan) | `include/radio_crypto.h` |
| 13 | `net_count_oob` | B3-b: object/beam `count` field bounds (0 / MAX / MAX+1 / -1 / INT32_MIN / INT32_MAX), canary array (ASan/UBSan) | `src/server/packets.c` + `net.c` |
| 14 | `golden` | CPU-vs-GDD render parity & GDD golden regression pin, **region-based** (4 regions + global sanity; a localised artifact can no longer hide in the pixel average) (integrated only) | `build/spacegl_vulkan` (product binary, black-box) |
| 15 | `gdd_bench` | GDD cull/expand/final compute pass runtime benchmark against thresholds | none (self-contained `gdd_mesh_test --benchmark`) |
| 16 | `server_tick_bench` | server tick rate regression: 1000 ticks of the real game loop over a worst-case-but-stable scene; avg < `threshold_ms`, p99 < `p99_ms` (tests/server_tick_thresholds.txt, 8.0/12.0 ms) | `src/server/logic.c` + `logic_events.c` + `quad_index.c` + `targets.c` |
| 17 | `stability` | long-running memory-leak check: real server + real client for 60 s, server RSS delta ≤ 10 MB (integrated only; the CI smoke job runs it explicitly too) | `build/spacegl_server` + `build/spacegl_client` (black-box) |
| 18 | `object_types_lint` | every object `type` sent by the server is an `OBJ_*` name from `include/object_types.h` (no bare numbers, no duplicated values); needs only Python 3 | `src/server/logic.c`, `include/object_types.h` |

### 3.1 Integrated mode (default)

`tests/` is part of the main build (option `SPACEGL_BUILD_TESTS`, ON):

```bash
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

In this mode `gdd_mesh` defaults `GDD_SPV_DIR` to THIS build tree's
own `shaders/` directory — the SPIR-V the build just compiled — so a
shader change that breaks the GPU contract fails the local build
instead of surviving until someone runs the suite by hand.

### 3.2 Standalone mode (tests/ as an independent project)

`tests/` is dual-mode: configured directly it recreates the same setup
by itself (own `project()`, own flags, own SDK lookup, own
`enable_testing()`):

```bash
cmake -B build-tests tests
cmake --build build-tests -j
ctest --test-dir build-tests --output-on-failure
```

The code under test is pulled straight from `../src` and `../include`,
so both modes always exercise the real production sources. In
standalone mode `GDD_SPV_DIR` defaults to the main tree's
`build/shaders/`; override with `-DGDD_SPV_DIR=<dir>`.

## 4. Vulkan validation layer (Debug builds)

`spacegl_vulkan` requests the standard `VK_LAYER_KHRONOS_validation`
layer **by default in Debug builds** (CMake `Debug`, i.e. `NDEBUG` not
defined): synchronization/barrier and pipeline errors are then reported
by the driver at the moment they happen, instead of surfacing later as
silent GPU corruption. Release builds request nothing by default.

Control (documented for the auditors):

*   `SPACEGL_VALIDATION=0` — disable, even in a Debug build;
*   `SPACEGL_VALIDATION=1` — enable, even in a Release build;
*   unset — the build default above.

The standard `VK_INSTANCE_LAYERS` variable (colon-separated layer
names) is honored as before and merged with the default; a requested
layer the driver does not provide is warned and skipped.

Per-frame statistics: the GDD path prints a `[GDD perf [frame N]: ...]`
line every 60 frames **only** when `SPACEGL_GDD_PERF=1` is set (it is
silent by default). A frame that dropped vertices (`[OVERFLOW]`) is
always reported.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
SPACEGL_GPD=1 ./build/spacegl_vulkan          # validation on (Debug)
SPACEGL_GPD=1 SPACEGL_VALIDATION=0 \
    ./build/spacegl_vulkan                    # validation off
```

## 5. The contract-test pattern

The suite is built around **contract tests**: a boundary invariant
(CPU/GPU struct layout, wire format, navigation math, spatial index,
crypto table) is expressed as production code and the test verifies the
production code itself — never a copy. The pattern:

1.  **Two layers, same contract.** The layout contracts live as
    compile-time `_Static_assert`s in the shared header (for the GDD
    boundary: `include/spacegl_gdd.h` — sizes, every field offset, and
    the 16-byte alignment of every vec4 start) and are re-checked at
    runtime by the test (`gdd_contract_test`, §1 "Layout
    invariants"), so a mis-compiled TU — or a different compiler with a
    different layout — cannot sneak through.
2.  **Link the real sources.** The test target compiles the production
    `.c` files it pins (`nav_math.c`, `quad_index.c`,
    `commands.c`+`logic.c`, or the whole `vulkan_gdd_build.c` TU with
    small stubs for the device-side references the test never calls —
    `recreateSwapChain`, `glfwGetTime`). When the module indexes
    globals owned by a file that is not linked (e.g. the server object
    arrays live in `galaxy.c`), the test TU owns those globals at
    production size.
3.  **No framework.** Plain C with a `CHECK`/`CHECK_F` macro (file:line
    plus the offending values on failure), a final
    `N checks passed, M failed` line, and the exit contract:
    `0` = pass, `1` = fail. `gdd_mesh` adds `77` = skip (no Vulkan ICD
    or missing SPIR-V) with the matching `SKIP_RETURN_CODE` in CMake,
    so a headless CI host does not fail the suite.
4.  **Fidelity details that matter.** Bit-exact CPU mirrors of GLSL
    geometry are compiled with `-ffp-contract=off` (GLSL does not
    FMA-contract, neither may the mirror). Tests that exercise
    attacker-controlled sink arithmetic (`target_bounds`,
    `vec_thrust`) are built with `-fsanitize=address,undefined` and
    place the real `spacegl_master` canary right after `players[]` —
    the production BSS adjacency — so any residual out-of-bounds access
    aborts the run instead of passing silently.
5.  **Every test names its regression.** The file header states which
    audit item / changelog entry the test pins (B1, B2, B3, B5, … —
    see SECURITY.md), so the registry and the suite stay mutually
    consistent.

**Adding a new contract test**: create `tests/<name>_test.c` with the
project GPL header and the regression story, add the `add_executable` +
`add_test` pair in `tests/CMakeLists.txt` (the file is dual-mode, so
both trees pick it up automatically), run the full suite in both modes,
and reference the new test from the changelog entry and from
SECURITY.md if it pins an audit item.

## 6. Repository map

| Path | What |
| :--- | :--- |
| `src/` | the 8 product TUs (server in `src/server/`, GDD in `src/vulkan_gdd_build.c` and `src/vulkan_gdd_render.c`) |
| `include/` | shared headers; `spacegl_gdd.h` is the CPU/GPU contract, `object_types.h` the single table of object `type` values (X-macro; server, HUD, viewers and the GDD builder all use its `OBJ_*` names and predicates),
  `radio_crypto.h` the single crypto table, `network.h` the canonical packet checks |
| `assets/shaders/` | GLSL sources (legacy `shader.*` + GDD `gdd_*`, shared `gdd_common.glsl`/`gdd_ops.glsl`) |
| `tests/` | the 18-test suite (dual-mode CMake project) |
| `cmake/SpaceGLVulkanSDK.cmake` | the shared Vulkan SDK resolution chain |
| `man/` | man pages (one per product binary) |
| `spacegl.spec` | RPM spec (its `%check` runs the unit suite) |
| `CHANGELOG` | the release log (versioned per change, `YYYY.MM.DD.NN`) |
| `README.md` | product overview + technical sections |
| `SECURITY.md` | threat model + audit registry (B items) |
| `HOWTO.txt` | run instructions (key + server + clients) |

## 7. Threads and locks

Verified under ThreadSanitizer by `tests/tsan_e2e.sh` (CI job `tsan`):
the real server in both I/O modes and two real decks, no report allowed.
Build it locally with
`cmake -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DSPACEGL_TEST_SANITIZER=thread -DCMAKE_C_FLAGS=-fsanitize=thread -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread`
and run with `OMP_NUM_THREADS=1` (libgomp is not instrumented).

### Server threads

| Thread | Runs | Holds `game_mutex` |
| :--- | :--- | :--- |
| main (epoll loop, legacy or async) | accept, `dispatch_packet()`, teardown (`reap_player_fd`) | per packet, briefly |
| game loop (60 Hz) | `update_game_logic()`, telemetry snapshot, requested saves; then, **after** the unlock, `send_pending_updates()` and `telemetry_broadcast()` | for the tick |
| thread pool workers | galaxy sync at login, server messages (ASYNC mode), fleet radio relay | to snapshot, never while writing a socket |
| telemetry uplink | accepts telemetry clients, subscriptions | never |

### Locks and their order

| Lock | Guards |
| :--- | :--- |
| `game_mutex` | the simulation: `players[]` (except the send state below), every NPC/object pool, `spacegl_master`, the packet-side tables of `packets.c` (`g_cred_seq`, the signing keys) |
| `players[i].socket_mutex` | writes on player `i`'s socket and the clearing of `players[i].socket` (`player_socket_detach`) |
| `tel_clients_mutex`, `cat_mutexes[]` | the telemetry client table and the per-category caches |

Order: **`game_mutex` → `socket_mutex`**, never the reverse. A thread that
writes a socket while holding `game_mutex` (legacy `send_server_msg`, the
LRS reply) is allowed; a thread holding a `socket_mutex` never takes
`game_mutex`.

### Ownership rules

- **Send state**: `last_sent_state`, `full_update_timer`, `last_q*`,
  `pending_send` of each player are written only by the game loop
  (`send_pending_updates`, outside the lock). Anything that reads the
  whole `players[]` array must therefore run on the game thread: a save
  requested by another thread (`save_galaxy()`) is carried out by the game
  loop at its next tick, under the lock.
- **Signatures**: `spacegl_master` is signed at boot only; the login sync
  signs its own snapshot under the lock. Nothing rewrites the shared
  struct outside the lock.
- **Routing snapshots**: the radio relay and `send_server_msg()` copy the
  recipient's fd, frequency and key under the lock, then write without
  it; every write re-checks under `socket_mutex` that the slot still holds
  that fd.
- **Teardown**: `player_socket_detach()` shuts the socket down first (a
  writer blocked in `send()` returns at once), then clears it under
  `socket_mutex`; the caller closes the fd afterwards.
- **Shutdown flag**: `g_running` is a lock-free `atomic_int` (stored by the
  signal handler, read by every thread).

### Command deck threads

The main thread (keyboard, commands) and `network_listener` (server
packets). `g_state_mutex` guards the shared-memory write buffer
(`g_shared_state` and its contents), the buffer swap and the peer table
`g_peers`; nothing else is taken while it is held. The input line belongs
to the main thread alone: the listener only requests a prompt redraw.

## 8. The galaxy file (galaxy.dat)

Since 2026.10.31.19 `galaxy.dat` is written in **format 2**. The format
(section table, save image, reader) is `src/server/galaxy_file.c`, shared
with the offline inspector `spacegl_viewer`; the save scheduling and the
load policy (repairs, completion, report) are `src/server/galaxy_io.c`. The
file is a header (`SGGALAX2`, format, `GALAXY_VERSION`)
followed by one section per pool, each with its tag, element size, layout
number and element count.

- **Saving.** Under `game_mutex` only an in-memory image is built (one copy
  per pool); the thread pool writes it to `galaxy.dat.tmp`, `fsync`s it and
  renames it over `galaxy.dat`. A crash during the write leaves the previous
  file intact. At shutdown the pool is gone and the write happens in place.
- **Loading.** Each section is read on its own. A section whose element size
  or layout number differs from the running code is skipped: that pool
  starts empty and the completion spawners refill the families they know
  (evolved stars, moons, protostellar objects, star systems). Only the
  `master` section is required; without it a new galaxy is generated.
- **Format 1** (the raw dump written before 2026.10.31.19) is no longer read
  (2026.10.31.25): like any file without the `SGGALAX2` magic it makes the
  server generate a new galaxy.
- **Rule for a change to a persisted struct.** A change of size is detected
  by itself. A change of the fields that keeps the size (a reorder, a type
  swap of equal width) is not: bump `GALAXY_SECTION_LAYOUT` for it in
  `galaxy_file.c` (per-section numbers if more than one struct moves).
- **Test.** `tests/galaxy_file_test.sh` (ctest `galaxy_file`) runs the real
  server through a new galaxy, a restart, the load repairs, the inspector
  on the same file, a format-1 file (refused, a new galaxy is generated), a
  section with a changed layout and a corrupt file.

## 9. Coverage, fuzzing and static analysis in CI

- **Coverage.** `-DSPACEGL_COVERAGE=ON` instruments every target with gcov.
  The CI `coverage` job runs the suite and measures the server's line
  coverage with `gcovr`; it fails under `tests/coverage_threshold.txt`. That
  file is a ratchet: raise it when tests raise the coverage, never lower it.
  At 2026.10.31.29 the server is at 84.5% (floor 83, target 80% reached);
  the command handlers run on a real galaxy in `tests/server_commands_test.c`
  and the telemetry port in `tests/telemetry_test.sh`. The least covered
  files are logic_events.c and commands.c (about 65%).
- **Fuzzing.** The `fuzz` job of `ci.yml` runs libFuzzer for 3 minutes on
  every push; `nightly-fuzz.yml` runs it for one hour every night and keeps
  any finding as an artifact (replay it with `build-fuzz/tests/fuzz_packets
  <file>`). New corpus units are kept as an artifact for 7 days; the
  committed corpus changes only by a reviewed commit.
- **Static analysis.** `.clang-tidy` is the profile of the `lint` job (server
  sources, zero findings). Run it locally with
  `clang-tidy -p <build dir with compile_commands.json> <files>`.
- **Warnings.** `-DSPACEGL_WERROR=ON` (on in the CI matrix) turns warnings into
  errors. `-Wshadow` is on everywhere. `-Wconversion` is on for the server
  target (`spacegl_server`, 2026.10.31.26): an implicit conversion that can
  change a value or a sign is written as an explicit cast, with a comment
  when the range that makes it safe is not obvious from the line. A random
  coordinate inside a quadrant is `galaxy_rand_coord()`
  (`src/server/galaxy_internal.h`). Since 2026.10.31.32 the deck
  (`spacegl_client`) and the text tools (`spacegl_telemetry`,
  `spacegl_viewer`, `spacegl_hud`, `spacegl_diag`) are under it too. The
  graphical viewers (`spacegl_3dview`, `spacegl_vulkan`, about 2300 sites,
  mostly double to float for the graphics) are not yet; each change is
  checked by comparing the machine code (`objdump -d`) before and after.

