# SpaceGL — Development Guide

How to build the tree, how to run the test suite, and the pattern
behind the contract tests. The product overview, feature lore and
runtime usage live in README.md; the threat model and the audit
registry live in SECURITY.md.

This is an integrated source tree (not a git checkout): the 8 product
targets, the shader sources and the `tests/` unit suite are all built
from here.

## 1. Prerequisites

Reference toolchain (Fedora, the project standard): GCC with C23
support (the tree is built warning-clean on GCC 16.2.1), CMake ≥ 3.10
(3.16 for the standalone tests project), pkg-config.

Development packages:

```
dnf install gcc-c++ cmake pkgconf-pkg-config mesa-libGL-devel \
            libGLU-devel glew-devel glfw-devel ncurses-devel \
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
    headers, loader and glslc from the same SDK:
    `/home/nick/dev/c/vulkan-sdk/1.4.363.0/x86_64`); opt out with
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
*   **RPM packaging**: `spacegl.spec` + `local_check.sh` (local mock
    build tree + rpmlint). The spec's `%check` runs the very unit
    suite of §3, so a contract failure also fails the package build.

## 3. Running the test suite

The suite has **9 tests**, built in two equivalent modes:

| # | Test | Pins | Production sources under test |
| :-- | :--- | :--- | :--- |
| 1 | `gdd_contract` | CPU/GPU boundary: struct sizes, **per-field offsets**, vec4 16-byte alignment, flag round-trip, instance builder vs synthetic scenes | `src/spacegl_vulkan_gdd.c` (full TU) |
| 2 | `gdd_mesh` | headless GPU: the real cull/expand/final SPIR-V vs a bit-exact CPU mirror (exit 77 = skip without an ICD/SPV) | `build/shaders/gdd_*.spv` |
| 3 | `nav_heading` | B2: HUD heading stays in [0, 360) across the 0/360 wrap | `src/server/nav_math.c` |
| 4 | `nav_roll` | HUD roll stays in [0, 360) (`pos` normalization + align re-wrap) | `src/server/nav_math.c` |
| 5 | `quad_index` | B3: all 62 static galactic types (quasar & co.) indexed per quadrant | `src/server/quad_index.c` |
| 6 | `pkt_length` | B1: `PacketMessage.length` [0, 65535] wire contract | `include/network.h` |
| 7 | `crypto_table` | B5: shared radio cipher table, slot→cipher contract, round-trip on all slots | `include/radio_crypto.h` |
| 8 | `target_bounds` | `tid`/`target_id` clamps on `players[]` (ASan/UBSan, canary global) | `src/server/commands.c` + `logic.c` + `nav_math.c` + `targets.c` |
| 9 | `vec_thrust` | the `vec` command contract (ASan/UBSan, same scaffolding) | same as 8 |

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
    `commands.c`+`logic.c`, or the whole `spacegl_vulkan_gdd.c` TU with
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
| `src/` | the 8 product TUs (server in `src/server/`, GDD in `src/spacegl_vulkan_gdd.c`) |
| `include/` | shared headers; `spacegl_gdd.h` is the CPU/GPU contract, `radio_crypto.h` the single crypto table, `network.h` the canonical packet checks |
| `assets/shaders/` | GLSL sources (legacy `shader.*` + GDD `gdd_*`, shared `gdd_common.glsl`/`gdd_ops.glsl`) |
| `tests/` | the 9-test suite (dual-mode CMake project) |
| `cmake/SpaceGLVulkanSDK.cmake` | the shared Vulkan SDK resolution chain |
| `man/` | man pages (one per product binary) |
| `spacegl.spec` / `local_check.sh` | RPM spec + local compliance check |
| `changelog` | the release log (versioned per change, `YYYY.MM.DD.NN`) |
| `README.md` | product overview + technical sections |
| `SECURITY.md` | threat model + audit registry (B items) |
| `HOWTO.txt` | run instructions (key + server + clients) |
