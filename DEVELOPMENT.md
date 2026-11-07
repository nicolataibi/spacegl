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
    opts out of the pinned SDK; `-DSPACEGL_INSTALL_SYSTEMD_UNIT=OFF`
    leaves the optional `spacegl-server.service` out of `make install`
    (default ON on Linux, into `SPACEGL_SYSTEMD_UNITDIR`, by default
    `<prefix>/lib/systemd/system`; the RPM passes `%{_unitdir}`). A
    change of the unit is checked by `tests/systemd_unit_test.sh`; a
    new system call in the server must stay inside the unit's filter
    (`@system-service` without `@privileged` and `@resources`, see
    SECURITY.md §1.5).
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

The suite has **57 tests** (integrated mode; the standalone mode builds the ones that need no product binary), built in two equivalent modes:

| # | Test | Pins | Production sources under test |
| :-- | :--- | :--- | :--- |
| 1 | `gdd_contract` | CPU/GPU boundary: struct sizes, **per-field offsets**, vec4 16-byte alignment, flag round-trip, instance builder vs synthetic scenes | `src/vulkan_gdd_build.c` (full TU) |
| 2 | `object_types_lint` | every object `type` sent by the server is an `OBJ_*` name from `include/object_types.h` (no bare numbers, no duplicated values); Python 3 | `src/server/logic.c`, `include/object_types.h` |
| 3 | `gdd_mesh` | headless GPU: the real cull/expand/final SPIR-V vs a bit-exact CPU mirror (77 = skip without an ICD/SPV) | `build/shaders/gdd_*.spv` |
| 4 | `nav_heading` | B2: HUD heading stays in [0, 360) across the 0/360 wrap | `src/server/nav_math.c` |
| 5 | `nav_roll` | HUD roll stays in [0, 360) (`pos` normalization + align re-wrap) | `src/server/nav_math.c` |
| 6 | `moon_orbit` | natural-moon orbits: one planet each, at most 4 per planet, slow, inside the planet's quadrant, deterministic | `include/moon_orbits.h` |
| 7 | `star_system` | star systems: slot arithmetic, id bands, contact radii, every body inside one quadrant at every time | `include/star_system.h` |
| 8 | `steer_avoid` | segment test against solids (fast ships inside one tick) and obstacle steering | `include/steer_avoid.h` |
| 9 | `obstacles` | the per-quadrant solid-object provider on a hand-made spatial index | `include/obstacles.h` |
| 10 | `quad_index` | B3: all 62 static galactic types indexed per quadrant | `src/server/quad_index.c` |
| 11 | `pkt_length` | B1: `PacketMessage.length` [0, 65535] wire contract | `include/network.h` |
| 12 | `crypto_table` | B5: shared radio cipher table, slot→cipher contract, round-trip on all slots | `include/radio_crypto.h` |
| 13 | `target_bounds` | `tid`/`target_id` clamps on `players[]` (ASan/UBSan, canary global) | `src/server/commands.c` + `logic.c` + `nav_math.c` + `targets.c` |
| 14 | `vec_thrust` | the `vec` command contract (ASan/UBSan) | same as `target_bounds` |
| 15 | `collision` | ships and comets hitting a solid object, on the real `update_game_logic()` | `src/server/logic.c` + `logic_events.c` |
| 16 | `fuzz_corpus` | the dispatcher replayed over the committed fuzz corpus (ASan/UBSan) | `src/server/packets.c` + `net.c` + `conn.c` |
| 17 | `rate_limit` | token-bucket rate limit (200 pkt/s, 10 drops → disconnect, 1 s refill), legacy and async (ASan/UBSan) | `src/server/packets.c` + `net.c` + `conn.c` |
| 18 | `relay_snapshot` | radio relay: NUL-safe encryption, byte-exact relay, captured routes, detach of a blocked writer, one encoding per frequency and key | `src/server/net.c` + `conn.c` |
| 19 | `login_auth` | PKT_LOGIN must prove the identity; takeover; unsalted records refused; credentials masked; stale handshakes closed; concurrent new captains; handshake slot released | `src/server/packets.c` |
| 20 | `sender_stamp` | the sender of a radio message is the authenticated captain (from, COMPUTER lock, faction, station names, signing key) | `src/server/packets.c` + `net.c` |
| 21 | `client_wire` | the deck's clamps of server-supplied counts (ASan/UBSan); the effects block of variable size: round trip, sizes, hostile counts refused | `include/client_wire.h`, `include/effects_wire.h` |
| 22 | `key_file` | `load_master_key()`: creation 0600, sources, permissions, damaged files | `include/network.h` |
| 23 | `handshake_v2` | handshake: RFC vectors, exchange, refusals (revisions 2-5 too), K off the wire, the server identity signature (revision 4), dispatcher | `include/handshake_v2.h`, `src/server/packets.c` |
| 24 | `shm_layout` | the SharedIPC segment: alignment of the shared mutex/semaphore/atomics, layout stamp | `include/shared_state.h` |
| 25 | `sg_str` | bounded `SG_SPRINTF`/`SG_STRCAT`/`SG_APPENDF` | `include/sg_str.h` |
| 26 | `obj_delta` | UPD_OBJDELTA encoder/decoder round trip, full/motion/status records and their sizes, malformed blocks | `include/obj_delta.h` |
| 27 | `update_delta` | the real `send_optimized_update()` decoded over 900 ticks; a one-object quadrant as status records; the effects block read back exactly | `src/server/net.c` |
| 28 | `sg_parse` | `sg_scan_nums()` and `sg_hex_decode()`, the checked replacements for `sscanf()` | `include/sg_parse.h` |
| 29 | `crypto_table_oob` | B2-b: `radio_cipher_for_algo()` bounds, all 256 algo bytes (ASan/UBSan) | `include/radio_crypto.h` |
| 30 | `net_count_oob` | B3-b: object/beam `count` field bounds, canary array (ASan/UBSan) | `src/server/packets.c` + `net.c` |
| 31 | `golden` | CPU-vs-GDD render parity and GDD golden regression pin, region-based (integrated only) | `spacegl_vulkan` (black-box) |
| 32 | `golden_gl` | OpenGL viewer: deterministic frame (two runs bit-identical), the committed `golden_gl.ppm` (region-based), the `SPACEGL_GL_BLOOM` modes (integrated only) | `spacegl_3dview` (black-box) |
| 33 | `gdd_default` | the Vulkan viewer's default path: GDD by default, `SPACEGL_GPD=0` CPU-driven, fallback when the GDD cannot start, strict `SPACEGL_GPD=1` for screenshots (integrated only) | `spacegl_vulkan` (black-box) |
| 34 | `gdd_bench` | GDD cull/expand/final compute pass times against thresholds | `gdd_mesh_test --benchmark` |
| 35 | `server_tick_bench` | server tick: 1000 ticks of the real loop over a worst-case-but-stable scene (with the real LRS grid slices since 2026.11.01.13); avg < `threshold_ms`, p99 < `p99_ms` (`tests/server_tick_thresholds.txt`); reports p99.9, the CPU time per tick next to the wall time, the largest wait for a CPU and the galaxy save image (fresh vs reused buffer) | `src/server/logic.c` + `logic_events.c` + `lrs_grid.c` + `quad_index.c` + `targets.c` + `galaxy_file.c` |
| 36 | `stability` | real server + real deck for 60 s, server RSS delta ≤ 10 MB (integrated only) | `spacegl_server` + `spacegl_client` (black-box) |
| 37 | `server_commands` | the command handlers on a real galaxy; the `nav` course through and at planets, an object moving onto it in flight, random jumps with no collision | `src/server/commands*.c`, galaxy, logic, targets |
| 38 | `radio_e2e` | two real decks on the real server: clear, fleet-encrypted, identity-frequency and peer messages, verified | `spacegl_server` + `spacegl_client` (black-box) |
| 39 | `gl_shape_cache` | OpenGL viewer: display-list shapes match immediate mode; compiled on second use (needs an OpenGL context: skip otherwise) | `src/glut_compat.c` |
| 40 | `gl_text` | OpenGL viewer text as textured quads: the font's pixels and the raster colour at 8 px, the raster position moved on as by `glBitmap`, exact glyphs at 5/12/16 px, depth as when queued, `glutBitmapString`, under the viewer's point-sprite state (needs an OpenGL context: skip otherwise) | `src/glut_compat.c` |
| 41 | `known_servers` | the deck's pins of server identity keys: first contact, mismatch, `SPACEGL_SERVER_KEY`, file format | `include/known_servers.h` |
| 42 | `sg_tls` | TLS 1.3 on the game port on socketpairs: handshake and identity key from the certificate, packets buffered in the session, a deck in clear refused, a silent peer timed out, one reader and two writers on one session (TSan in CI) | `src/sg_tls.c` |
| 43 | `systemd_unit` | the optional `spacegl-server.service` as configured: `systemd-analyze verify`, the key as a credential, the state directory, the dynamic user, the sandbox exposure (at most 2.0), a boot of the real server on a 0400 credential (no `systemd-analyze`: skip) | `systemd/spacegl-server.service.in` |
| 44 | `tick_monitor` | the server's measure of its tick with synthetic ticks: no warning inside the budget, one per 10 s with the worst tick by phase, a tick waiting for a CPU named, the summary, `SPACEGL_TICK_THREADS` | `include/tick_monitor.h` |
| 45 | `frame_bench` | the viewers' frame-time helpers: warm-up, average, percentiles; the busy quadrant of `--bench-scene` | `include/frame_bench.h`, `include/bench_scene.h` |
| 46 | `viewer_bench` | both viewers with `--bench-frames`/`--bench-scene`: one `[BENCH]` line each, the scene loaded (display or xvfb-run and a Vulkan ICD: skip otherwise) | `spacegl_vulkan`, `spacegl_3dview` |
| 47 | `launcher` | the launcher scripts with stub programs: arguments, exit status, key sources and prompt (pseudo-terminal), `--help`, a missing program, the diagnostic's server lookup | `spacegl-server`, `spacegl-client`, `spacegl-diag` |
| 48 | `deck_e2e` | the deck's own paths: no server, wrong key, refused names, eleven concurrent logins, the input line, duplicate/wrong logins, reconnect after a restart, server identity pinning, the TLS 1.3 transport (`tests/tls_probe.py`: TLS 1.3 only, certificate = identity, clear text refused; a silent connection closed), servers the deck refuses at the transport (`tests/tls_fake_server.c`: no TLS, a TLS identity that is not the one signing the handshake, a link reset during the handshake) | `spacegl_server` + `spacegl_client` (black-box) |
| 49 | `telemetry` | the telemetry port (TCP 5001): every category, frames checked against the boot table | `spacegl_server` + `telemetry_client_test` |
| 50 | `galaxy_file` | galaxy.dat format 2: atomic save, mode 0600, reload, damaged files | `spacegl_server` (black-box) |
| 51 | `smoke_e2e` | login round-trip of a real deck on the real server (legacy I/O) | `spacegl_server` + `spacegl_client` (black-box) |
| 52 | `smoke_async` | the same with the server in `--io=async` | same |
| 53 | `orbit` | the circle of `orb`: radius, plane, angular rate, period law (r^1.5), capture without a jump | `include/orbit_math.h` |
| 54 | `sphere_lod` | the sphere levels of the CPU-driven Vulkan path: level 0 identical to the old 64x128 mesh, indices inside their level, silhouette within 0.5 px (filled) or lines at most 1 px apart (wireframe), dense wireframes drawn filled up to 8 px at every size, full mesh near or behind the camera | `include/sphere_lod.h` |
| 56 | `nav_route` | the collision-free course of `nav`: straight when clear, detours that never enter a keep-out sphere (single obstacle, walls, 3000 random and 300 dense fields), goal / start inside a keep-out sphere, galaxy box, walled-in goal stops short, determinism, moving object seen on the course | `include/nav_route.h` |
| 57 | `nav_route_view` | the look of the course shared by the three 3D views: clipped to the quadrant on screen, exit face and next quadrant, rounded corners and waypoint markers, keep-out bubbles, red stop mark, primitive budget, animation | `include/nav_route_view.h` |
| 55 | `gpu_clock` | the GPU clock helpers of `gpu_golden_check.sh` on fake sysfs trees: i915 (connectors skipped), xe, amdgpu, the rendering vendor's GPU among two, `--gpu-card`, pin and restore (also after SIGINT), clock sampling (the samples at 0 MHz, GPU asleep, counted apart), no supported card | `tests/gpu_clock.sh` |

The tests that use port 5000/5001 run serially (`RUN_SERIAL`); an image or GPU test without a display or an ICD returns 77 (SKIP). Every end-to-end script uses a private `known_servers` file (`SPACEGL_KNOWN_SERVERS`), so a new data directory is not taken for a changed server.

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
./build/spacegl_vulkan                        # GDD (the default), validation on (Debug)
SPACEGL_VALIDATION=0 ./build/spacegl_vulkan   # validation off
SPACEGL_GPD=0 ./build/spacegl_vulkan          # the CPU-driven path
```

Since 2026.10.31.42 the GPU-driven path is the default (`SPACEGL_GPD`
unset or `1`) and falls back to the CPU-driven path when it cannot start;
`SPACEGL_GPD=0` selects the CPU-driven path, and with an explicit
`SPACEGL_GPD=1` a `--screenshot` run fails instead of falling back.
`SPACEGL_GDD_FAIL_INIT=1` simulates a GDD that cannot start (test hook,
`tests/gdd_default_test.sh`).

The CPU-driven path draws every sphere at a level of detail (2026.11.07.01,
`include/sphere_lod.h`, `include/vulkan_cpu_lod.h`): the index buffer holds
the 64x128 sphere and three coarser levels (32x64, 16x32, 8x16), and
`vk_draw_sphere()` picks the coarsest one whose silhouette stays within half
a pixel at the sphere's size on screen (with the wireframe pipeline, whose
lines are the triangle edges, only while the lines stay at most one pixel
apart, 2026.11.07.03; a wireframe sphere whose full mesh puts its lines at
most half a pixel apart is drawn filled, 2026.11.07.05).
The size comes from the model matrix last pushed and the previous frame's
view; `vulkan_cpu_lod.h` wraps `vkCmdPushConstants` and `vkCmdBindPipeline`
in the files that include it to know them. New sphere draws call
`vk_draw_sphere(cb)` instead of `vkCmdDrawIndexed` with
`SPHERE_LATS * SPHERE_LONGS * 6`. `SPACEGL_SPHERE_LOD=0` draws the full
mesh always; `--bench-frames` prints the spheres a frame by level
(`[SPHERE-LOD]`). With the validation layer on (Debug), `gdd_default`
also fails on any `VUID-` line in the viewer's logs.

Where the frame of the CPU-driven path goes (2026.11.07.02): the same
wrappers count the draws, indices, pipeline binds (and real changes of
pipeline) and push constants by kind of object (`CpuCmdStats`: ships,
stars, planets, bases, asteroids, comets, nebulae, other, effects), printed
by `--bench-frames` as a `[CPU-CMDS]` line; with `SPACEGL_VK_GPU_TIMES=1`
a timestamp after every object charges the GPU time to its kind in the
`[GPU-TIMES]` line (the timestamps cost time themselves: compare shares);
`SPACEGL_VK_GPU_TIMES_DRAWS=<kind>` times every draw of that kind on its
own (labels `<kind>:<pipeline>/<indices>`, 2026.11.07.04).
On the A770 measure with:

```sh
export SPACEGL_SHADERS_DIR="$PWD/build-rel/shaders"
SPACEGL_GPD=0 SPACEGL_VK_GPU_TIMES=1 build-rel/spacegl_vulkan sg_vk_x --window 1920x1080 \
    --seed 1 --no-rotate --bench-scene 256 --bench-frames 300 | grep -E "GPU-TIMES|BENCH|SPHERE|CPU-CMDS"
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
  `radio_crypto.h` the single crypto table, `network.h` the canonical packet checks, `handshake_v2.h` the handshake (wire revision 4, the server identity signature), `sg_tls.h` the TLS 1.3 transport of the game port (wire revision 5, one seam for the socket I/O of server and deck, with its traffic counters), `effects_wire.h` the effects block on the wire (wire revision 6), `obj_delta.h` the object list delta (full, motion and status records), `tick_monitor.h` the server's measure of its tick, `known_servers.h` the deck's pins of server identities |
| `assets/shaders/` | GLSL sources (legacy `shader.*` + GDD `gdd_*`, shared `gdd_common.glsl`/`gdd_ops.glsl`) |
| `tests/` | the 51-test suite (dual-mode CMake project), `bench_ab.sh` (A/B benchmark gate), `load_test.sh` (the real server under N real decks: traffic and slow ticks, by hand), `format_check.sh`, `fuzz/` (libFuzzer harness seeds and their generator/migration scripts) |
| `systemd/` | `spacegl-server.service.in`, the optional system service of the server (configured by CMake with the install prefix, never enabled by the build or the package) |
| `PROTOCOL.md` | the wire protocol (TLS transport, packets, handshake, identity, limits) |
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

### One server tick

`update_game_logic()` (`src/server/logic.c`, under `game_mutex`) runs, in
this order: autosave, moon and star-system orbits, LRS grid, nebula
refill, death timers; then for each active captain
`player_tick_survival()` (a ship with no hull or no crew is towed away and
skips the rest), `player_tick_life_support()`, `player_tick_shields()`,
`player_tick_weapons()`, `player_tick_probes()`,
`player_tick_navigation()` (one `nav_tick_*()` per navigation state) and
the collision check; then `logic_generate_events()`; then for each
connected captain the update packet (`update_fill_state()`,
`update_fill_objects()`, `update_fill_effects()`); last, the beam queues
and the transient NPC effects are reset. The order is part of the
behaviour: a phase reads what the previous ones wrote.

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
  that fd. Outside the lock a fleet message is encoded once per distinct
  frequency and key (`RelayEncoding`, src/server/net.c): the fleet keys
  are the same for every captain, so the recipients on one frequency get
  the same packet; a cleartext message to a cleartext recipient is written
  from the received packet itself.
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
  files are logic_events.c and commands.c (about 65%). The command deck
  (`src/spacegl_client.c`) has its own floor,
  `tests/coverage_threshold_deck.txt`: 86.8% at 2026.10.31.39 (floor 84),
  with `tests/deck_e2e_test.sh` driving the paths the smoke and radio tests
  leave out (no server, wrong key, refused names, the faction menu, the
  input line, duplicate and wrong logins, the reconnect after a server
  restart). The server's floor is 84 (85.3% measured).
- **Fuzzing.** The `fuzz` job of `ci.yml` runs libFuzzer for 3 minutes on
  every push; `nightly-fuzz.yml` runs it for one hour every night and keeps
  any finding as an artifact (replay it with `build-fuzz/tests/fuzz_packets
  <file>`). New corpus units are kept as an artifact for 7 days; the
  committed corpus changes only by a reviewed commit.
- **Benchmark A/B.** The thresholds of `server_tick_bench` and `gdd_bench`
  are absolute and sized for the slowest gated machine, so they catch a 2x
  regression, not a 10% one. The CI `bench_ab` job (2026.10.31.38) builds
  the base commit and the pushed tree side by side in Release, runs the
  server tick benchmark 7 times on each side, interleaved, and compares the
  medians: a tick average more than 5% above the base fails, but only after
  a second round of 7+7 confirms it. The p99 and the GDD pass times are
  measured too and only reported (job summary). Locally:
  `tests/bench_ab.sh <base ref>` (it measures the working tree against the
  base; `--reps`, `--tol`, `--gdd-tol`, `--no-gdd`). It skips, with the
  reason, when the base does not build or when the benchmark program
  itself changed between the two sides.
- **The tick on a real machine (2026.11.01.13).** The server times every
  tick itself (`include/tick_monitor.h`): logic, wait for `game_mutex`,
  save image, network, telemetry, late wake-up and the game thread's CPU
  time. Ticks over the 16.7 ms budget are reported every 10 s at most as
  one `PERFORMANCE` warning with the worst tick by phase, and the totals
  at shutdown; a tick with less than half its duration in CPU time waited
  for a CPU (the machine), one with as much CPU as wall time is the code.
  `server_tick_bench` shows the same split (CPU time next to the wall
  time). What the monitor found and fixed: the save image (a fresh 30-45
  MB buffer at every save, 28-45 ms of page faults under `game_mutex`;
  now one reused buffer, 1-8 ms), the LRS grid (the whole grid in one
  tick once per second, 10-15 ms; now one plane per tick, 0.3 ms) and the
  OpenMP team of the tick (a second thread gave no gain and doubled the
  CPU time; `SPACEGL_TICK_THREADS`, default 1). A change of the benchmark
  program makes the next `bench_ab` run skip (the two sides would measure
  different programs).
- **Frame times of the viewers (2026.11.01.15).** `spacegl_vulkan` and
  `spacegl_3dview` take `--bench-frames N [--bench-warmup W]
  [--bench-scene N]`: N frames, each waiting for the GPU (the frame's whole
  cost; no vsync, no frame limiter), then one `[BENCH]` line (average,
  p50, p95, p99, max). `--bench-scene` fills the empty segment with a busy
  quadrant (the captain + up to 255 objects, `include/bench_scene.h`).
  `tests/gpu_golden_check.sh` runs them on the GPU after the golden
  checks: Vulkan CPU-driven and GPU-driven, OpenGL with each bloom mode,
  on the empty scene and on 256 objects, at 1920x1080 (`--bench-window`),
  300 frames (`--frames`), against a 16.7 ms budget on p95 (`--budget`;
  reported, or exit 3 with `--enforce-budget`).
- **Traffic (2026.11.01.14).** The server counts every connection's bytes
  (protocol and TLS, `sg_net_counters()`) and the bytes of each update
  block, and logs one `NETWORK` line every `SPACEGL_NET_REPORT_S` seconds
  (default 60) while decks are on board. `tests/load_test.sh <server>
  <client> [--decks N] [--seconds S] [--report S]` runs N real decks that
  fly and fire, and prints those lines, the slow ticks, the server's CPU
  and memory. With 16 decks for 60 s (this machine): 25.0 KB/s to each
  deck at wire revision 5 (objects 37%, effects 24%: a fixed 18 KB effects
  block, and full object records for energy changes), 14.5 KB/s at
  revision 6 (effects of variable size, status records), TLS +10%, the
  server at 17% of one CPU.
- **Golden renders on a real GPU (roadmap 10).** The committed golden
  images come from the software drivers (lavapipe, llvmpipe). On a machine
  with a GPU, in its graphical session, after a Release build:
  `tests/gpu_golden_check.sh [build dir] [--out DIR]` renders the Vulkan
  CPU-driven and GPU-driven frames and the OpenGL frame on the GPU, refuses
  to go on if a software driver rendered (`SPACEGL_VK_DEVICE=<index or name
  part>` picks the Vulkan device), and compares: Vulkan CPU vs GDD (parity),
  GDD vs `golden_gdd.ppm` and OpenGL vs `golden_gl.ppm` (regression). It
  writes the captures and `report.txt` into the output directory. The CI
  `gpu_real` job runs it on a self-hosted runner labelled `gpu` (repo
  variable `GPU_RUNNER_ENABLED=true`; the runner needs a graphical session).
  The viewer prints its device at start, and prefers a real GPU over the
  software driver.
  Frame times and the GPU clock (2026.11.07.07): a GPU lowers its clock
  under a light load and keeps it up for a while after a heavy one, so a
  row's time depends on the run before it (on the A770 the same
  GPU-driven frame measured 1.1 or 1.9 ms, 0.82 ms at a fixed clock). The
  script prints the build type and flags and the OpenGL driver version,
  samples the GPU's actual clock every 0.1 s during each timed run (MHz
  and min-max while the GPU ran, and the share of samples with the GPU
  asleep between frames: i915/xe report 0 MHz then, 2026.11.07.08), runs `--warmup S` seconds (default 3)
  of the heaviest load before each run, and says at the end whether the
  rows ran at clocks within 10 % of each other. `--pin-freq` raises the
  GPU's minimum clock to its top for the whole check (sudo) and restores
  it at exit, also on Ctrl+C: the way to compare two builds or two runs.
  `tests/gpu_clock.sh` holds the clock code (i915, xe, amdgpu: read and
  pin; nvidia: read), `--gpu-card cardN` picks the card when the
  rendering GPU's vendor is not enough; `gpu_clock` tests it.
- **OpenGL viewer: golden render and bloom.** `spacegl_3dview --screenshot
  FILE [--screenshot-frame N] [--seed N] [--no-rotate] [--window WxH]`
  renders frame N with a fixed clock (one frame = 1/60 s; every animation
  reads the time through `glutGet()`), creating an empty SHM segment when
  no deck is running: the same picture on every run. `golden_gl`
  (`tests/run_golden_gl.sh`, ctest and the CI `gpu` job) checks two runs
  bit-identical, the committed `tests/golden/golden_gl.ppm` (region-based,
  as the Vulkan golden), and the bloom modes. `SPACEGL_GL_BLOOM=full`
  (default) | `half` | `off`: under llvmpipe at 1920x1080 the bloom phase
  costs 394 / 86 / 37 ms a frame (the 37 ms are the MSAA resolve).
- **OpenGL viewer: text (2026.11.01.17).** `glutBitmapCharacter()` and
  `glutBitmapString()` (`src/glut_compat.c`) no longer call `glBitmap` per
  character: they read the raster position and colour once a string and
  queue textured quads from a glyph atlas built at the size in use
  (`glutBitmapTextSize()`), drawn in one call by `glutBitmapFlush()`. The
  viewer flushes before the bloom pass, after the object labels, at the
  end of each HUD block (`hudEnd()`) and of the frame; code that draws
  over text must flush first. The pixels are those of `glBitmap` (at
  1920x1080 the busy scene differs only where a label now covers a health
  bar), and the text is drawn at every window height (before, only at
  1080). The flush sets its own state and disables point sprites
  (`GL_COORD_REPLACE` replaced the glyphs' coordinates on Mesa) and
  texturing on the other units. `gl_text` checks it.
- **OpenGL viewer: shared geometry and profile.** `src/glut_compat.c`
  compiles every glut shape (sphere, cube, cone, cylinder, torus,
  octahedron, icosahedron) into a display list by its exact parameters on
  its second use and replays it afterwards; a parameter animated frame by
  frame stays in immediate mode. Exact parameters, not a scaled unit
  shape: the hull shader reads the object-space vertex. `gl_shape_cache`
  (ctest, and the CI `gpu` job under xvfb) checks that the replay matches
  immediate mode. `SPACEGL_GL_PROFILE=1` makes `spacegl_3dview` print every
  300 frames the frame time, the time of each phase (sky, backdrop,
  objects, effects, bloom, HUD; each phase waits for the GL, only while
  profiling) and the shapes per frame; `SPACEGL_GL_PROFILE_FRAMES=N` exits
  after N frames, `SPACEGL_GL_SHAPE_CACHE=0` turns the lists off. The
  viewer inherits the deck's environment:
  `SPACEGL_GL_PROFILE=1 spacegl_client gl`. Under llvmpipe (software GL,
  1920x1080, 4x MSAA) the bloom chain is 86% of a frame (658 of 769 ms),
  the shapes 1%.
- **Static analysis.** `.clang-tidy` is the profile of the `lint` job (server
  sources, zero findings). Run it locally with
  `clang-tidy -p <build dir with compile_commands.json> <files>`.
- **Formatting.** `.clang-format` describes the style of the tree as
  closely as clang-format can (4 spaces, attached braces, no reflow of long
  lines or comments). The CI `format` job checks only the lines a push or a
  pull request changes (`tests/format_check.sh <base>`); fix them with
  `git clang-format <base>`. The tree itself is not reformatted: doing it
  once, when no branch is open, is
  `git ls-files '*.c' '*.h' '*.inc' '*.inl' | xargs clang-format -i` in one
  commit of its own (after it every line complies; the check is unchanged).
- **Warnings.** `-DSPACEGL_WERROR=ON` (on in the CI matrix) turns warnings into
  errors. `-Wshadow` is on everywhere. `-Wconversion` is on for the server
  target (`spacegl_server`, 2026.10.31.26): an implicit conversion that can
  change a value or a sign is written as an explicit cast, with a comment
  when the range that makes it safe is not obvious from the line. A random
  coordinate inside a quadrant is `galaxy_rand_coord()`
  (`src/server/galaxy_internal.h`). Since 2026.10.31.32 the deck
  (`spacegl_client`) and the text tools (`spacegl_telemetry`,
  `spacegl_viewer`, `spacegl_hud`, `spacegl_diag`) are under it too, and
  since 2026.10.31.34 the graphical viewers (`spacegl_3dview`,
  `spacegl_vulkan`): every program of the project. Each change was checked
  by comparing the machine code and the constant data (`objdump -d -s`)
  before and after. In a compound assignment to a float (`x += d`, `d` a
  double) the explicit form is `x = (float)(x + d)`, not
  `x += (float)d`, which would add in float instead of double.

