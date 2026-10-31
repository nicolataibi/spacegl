# =========================================================================
# SpaceGL - unit-test suite (tests/)
#
# DUAL-MODE CMake project:
#
#   1. INTEGRATED (default): the top-level CMakeLists.txt pulls this
#      directory in with add_subdirectory(tests), gated by the
#      SPACEGL_BUILD_TESTS option (ON by default). In that mode this file
#      only defines the test targets: the C23 standard, the warning/
#      security flags, the dependency packages (Threads/OpenMP/OpenSSL),
#      the Vulkan::Vulkan alias, the GLFW_* pkg-config variables and the
#      include/ directory are all inherited from the parent project, and
#      enable_testing() is called by the parent. GDD_SPV_DIR then points
#      at THIS build tree's own shaders/ directory (the output of the
#      parent's spacegl_shaders target), so gdd_mesh always checks the
#      SPIR-V the build just compiled.
#
#   2. STANDALONE: configure this directory directly and the file
#      recreates the same setup by itself (own project(), own flags,
#      own Vulkan-SDK lookup, own enable_testing()):
#
#         cmake -B build-tests tests
#         cmake --build build-tests
#         ctest --test-dir build-tests --output-on-failure
#
# The code under test is pulled straight from ../src (the GPU-driven
# module src/vulkan_gdd_build.c) and ../include, so the tests always
# exercise the real production sources. Compile settings mirror the
# main project (C23, same warning flags).
# =========================================================================

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # ==================== STANDALONE MODE ====================
    cmake_minimum_required(VERSION 3.16)
    project(SpaceGLTests VERSION 2026.09.18 LANGUAGES C)

    set(CMAKE_C_STANDARD 23)
    set(CMAKE_C_STANDARD_REQUIRED ON)

    # Default to Release build if not specified.
    if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    endif()

    # Global definitions (same as the main project)
    add_compile_definitions(_XOPEN_SOURCE=700)

    # The target_bounds test links src/server/logic.c, whose tick loops are
    # OpenMP-parallel exactly like in the main server binary (see
    # ../CMakeLists.txt): link OpenMP when available so the pragmas behave
    # as in production (without it they are ignored and the loops run
    # sequentially, which is still a valid exercise of the code paths).
    find_package(Threads REQUIRED)
    find_package(OpenMP QUIET)

    if(MSVC)
        add_compile_options(/W4)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic -Wformat -Wformat-security)
        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            add_compile_options(-O3)
        elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
            add_compile_options(-O0 -g)
        endif()
    endif()

    # The crypto-table contract test exercises the shared radio cipher
    # lookup (include/radio_crypto.h), which is OpenSSL-based like the
    # production code that includes it.
    find_package(OpenSSL REQUIRED)

    # The GPU module links against Vulkan (device-side functions) and, in the
    # contract test, pulls the whole gdd.c TU which also references
    # glfwGetTime (frame path): link GLFW when available, stub it otherwise.
    #
    # Vulkan SDK resolution: the SAME chain as the main project, from the
    # same shared module (../cmake/SpaceGLVulkanSDK.cmake): VULKAN_SDK env
    # variable -> pinned local 1.4.363.0 default (the documented project
    # decision for reproducible builds; disable with
    # -DUSE_DEFAULT_VULKAN_SDK=OFF) -> system (find_package(Vulkan), else
    # the distro VulkanLoader package). The from-source loader exports
    # Vulkan::Loader: alias Vulkan::Vulkan.
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")
    include(SpaceGLVulkanSDK)
    spacegl_find_vulkan_sdk()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GLFW glfw3)
    endif()

    # Project root (this file is tests/CMakeLists.txt)
    set(SG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
    include_directories(${SG_ROOT}/include)

    enable_testing()
else()
    # ==================== INTEGRATED MODE ====================
    # add_subdirectory(tests) from the top-level CMakeLists.txt (option
    # SPACEGL_BUILD_TESTS, ON by default): the C23 standard, the Fedora
    # warning/security flags, Threads/OpenMP/OpenSSL, the Vulkan::Vulkan
    # alias, the GLFW_* pkg-config variables and the include/ directory all
    # come from the parent, and enable_testing() is called by the parent.
    set(SG_ROOT "${CMAKE_SOURCE_DIR}")
endif()

# The builder tests (gdd_contract / gdd_mesh) carry bit-exact mirrors of
# the geometry the GLSL shaders produce: they must not be FMA-contracted,
# -ffp-contract=off matches the non-FMA GLSL.
#
# NOTE on placement: add_compile_options only seeds targets CREATED AFTER
# IT in this directory. The old file declared it right after
# gdd_mesh_test, so the two gdd tests (the ones that need it) never got
# the flag while the later server tests did — it now precedes every test
# target below, in both modes (in integrated mode it only affects this
# directory, never the product targets).
add_compile_options(-ffp-contract=off)

# libatomic: needed by the targets that link logic.c (its `#pragma omp
# atomic` on 64-bit state emits __atomic_* calls; clang, unlike gcc,
# does not link libatomic automatically — shared resolution in
# cmake/SpaceGLAtomic.cmake, same module as the root project).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")
include(SpaceGLAtomic)
spacegl_find_atomic_lib()

# --- CPU contract + instance-builder test (no device needed) -----------
# --- CPU contract + instance-builder test (no device needed) -----------
add_executable(gdd_contract_test
    gdd_contract_test.c
    ${SG_ROOT}/src/vulkan_gdd_build.c
    ${SG_ROOT}/src/sglog.c)
target_link_libraries(gdd_contract_test PRIVATE Vulkan::Vulkan m)
if(GLFW_FOUND)
    target_include_directories(gdd_contract_test PRIVATE ${GLFW_INCLUDE_DIRS})
    target_link_libraries(gdd_contract_test PRIVATE ${GLFW_LIBRARIES})
else()
    target_compile_definitions(gdd_contract_test PRIVATE GDD_TEST_STUB_GLFW)
endif()
# ADD THIS:
if(SG_ATOMIC_LIB)
    target_link_libraries(gdd_contract_test PRIVATE ${SG_ATOMIC_LIB})
endif()
add_test(NAME gdd_contract COMMAND gdd_contract_test)

# object types: the server must send OBJ_* names from include/object_types.h
# (no bare numbers), see tests/object_types_lint.py.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME object_types_lint
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/object_types_lint.py)
endif()

# --- headless GPU compute test (cull/expand/final vs CPU reference) ----
# Self-contained: no production sources linked (its CPU mirrors of the
# GLSL ops live in the test file, like the DRAGONGL gpu_mesh_test).
# Exits 0 = pass, 1 = fail, 77 = skip (no Vulkan ICD / missing .spv).
add_executable(gdd_mesh_test gdd_mesh_test.c)
target_link_libraries(gdd_mesh_test PRIVATE Vulkan::Vulkan m)
# Default SPIR-V location: in integrated mode this build tree's own
# shaders/ directory (the parent's spacegl_shaders output, i.e. the
# SPIR-V this very build just compiled); standalone mode defaults to the
# main project's build tree.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(GDD_SPV_DIR_DEFAULT "${SG_ROOT}/build/shaders")
else()
    set(GDD_SPV_DIR_DEFAULT "${CMAKE_BINARY_DIR}/shaders")
endif()
set(GDD_SPV_DIR "${GDD_SPV_DIR_DEFAULT}" CACHE PATH "Directory holding the gdd_*.spv binaries")
add_test(NAME gdd_mesh COMMAND gdd_mesh_test "${GDD_SPV_DIR}")
set_tests_properties(gdd_mesh PROPERTIES SKIP_RETURN_CODE 77)

# --- Server navigation heading contract (B2: HUD heading must stay
# in [0, 360) even when a turn crosses the 0/360 boundary) -----------
# Pulls the real production source, same pattern as gdd_contract_test.
add_executable(nav_heading_test nav_heading_test.c ${SG_ROOT}/src/server/nav_math.c)
target_link_libraries(nav_heading_test PRIVATE m)
add_test(NAME nav_heading COMMAND nav_heading_test)

# --- Server navigation roll contract (the HUD roll field must stay
# in [0, 360): "pos" normalizes the requested roll into [0, 360) and
# the NAV_STATE_ALIGN_ONLY interpolation re-wraps the trajectory) ---
# Pulls the real production source, same pattern as nav_heading_test.
add_executable(nav_roll_test nav_roll_test.c ${SG_ROOT}/src/server/nav_math.c)
target_link_libraries(nav_roll_test PRIVATE m)
add_test(NAME nav_roll COMMAND nav_roll_test)

# --- Moon orbits (types 89..98): one planet and one orbit per moon, at most
# 4 moons per planet, slow, always inside the planet's quadrant, same parents
# after a restart (header-only, include/moon_orbits.h) ------------------------
add_executable(moon_orbit_test moon_orbit_test.c)
target_link_libraries(moon_orbit_test PRIVATE m)
add_test(NAME moon_orbit COMMAND moon_orbit_test)

# --- Star systems: one star + up to 4 planets + up to 4 moons per quadrant,
# slot arithmetic, id bands, contact radii and containment (header-only,
# include/star_system.h) -----------------------------------------------------
add_executable(star_system_test star_system_test.c)
target_link_libraries(star_system_test PRIVATE m)
add_test(NAME star_system COMMAND star_system_test)

# --- Sphere obstacles: segment collision test and steering (header-only,
# include/steer_avoid.h). Ships and comets that hit a solid object, the
# "safe" navigation and the NPC avoidance all use it ------------------------
add_executable(steer_avoid_test steer_avoid_test.c)
target_link_libraries(steer_avoid_test PRIVATE m)
add_test(NAME steer_avoid COMMAND steer_avoid_test)

# --- Obstacle provider on the per-quadrant index (include/obstacles.h) ---
add_executable(obstacles_test obstacles_test.c)
target_link_libraries(obstacles_test PRIVATE m)
add_test(NAME obstacles COMMAND obstacles_test)

# --- Server spatial index contract (B3: the quasar and the other
# static galactic objects must be inserted into the per-quadrant
# index, otherwise the per-frame update / LRS grid never send them)
# ------------------------------------------------------------------
# Pulls the real production source, same pattern as nav_heading_test.
# The test TU defines the global object arrays the module indexes
# (in the server they live in galaxy.c, which is not linked).
add_executable(quad_index_test quad_index_test.c ${SG_ROOT}/src/server/quad_index.c)
add_test(NAME quad_index COMMAND quad_index_test)

# --- Protocol contract: PacketMessage.length bounds (server-side remote
# DoS guard) ---------------------------------------------------------
# The attacker-controlled PacketMessage.length must be validated by BOTH
# endpoints before it is used in size_t arithmetic (server/net.c:
# broadcast c_len, EVP_DecryptUpdate, relay pkt_size). The canonical
# check lives in include/network.h (packet_message_length_valid); this
# test pins the [0, 65535] contract against the 65536-byte text buffer
# and proves the pre-fix sink arithmetic was out of bounds for the
# attack values.
add_executable(pkt_length_test pkt_length_test.c)
add_test(NAME pkt_length COMMAND pkt_length_test)

# --- Protocol contract: radio cipher table (audit item B5: fleet
# messages lost on 15 of 19 frequencies) ---------------------------
# The client encrypts fleet (0x01) messages with every tunable cipher
# while the server fleet-decrypt path knew only 4 of them, so a
# message on any other frequency died with "Frequency parity failure"
# without ever being delivered. The table is now ONE shared
# client/server lookup (include/radio_crypto.h, included by both
# endpoints); this test pins its contract: every slot resolves,
# each slot keeps the cipher/AEAD flag the client always used, the
# client-encrypt -> server-decrypt round-trip succeeds on ALL slots,
# and a wrong key never passes.
add_executable(crypto_table_test crypto_table_test.c)
target_link_libraries(crypto_table_test PRIVATE OpenSSL::Crypto)
add_test(NAME crypto_table COMMAND crypto_table_test)

# --- Server target-ID boundary regression (2026.10.06.01 clamp fix) --
# The attacker-controlled tid (pha/bor/lock/scan/apr command parameters)
# and PlayerTorpedo.target_id may index players[] only within
# [0, MAX_CLIENTS): this test pins the boundary cases tid = 16 / 17 /
# 999 and the target_id edges (16/17/999/0/-1, NPC edges 1000/3999)
# against the REAL commands.c and logic.c. The test TU owns the global
# object arrays at production size, with spacegl_master as the canary
# right after players[] (the server BSS adjacency the pre-fix OOB
# writes corrupted). Built with ASan/UBSan so any residual
# out-of-bounds access aborts the run instead of passing silently.
add_executable(target_bounds_test
    target_bounds_test.c
    ${SG_ROOT}/src/server/commands.c
    ${SG_ROOT}/src/server/logic.c
    ${SG_ROOT}/src/server/logic_events.c
    ${SG_ROOT}/src/server/nav_math.c
    ${SG_ROOT}/src/server/targets.c
    ${SG_ROOT}/src/sglog.c)
target_link_libraries(target_bounds_test PRIVATE m Threads::Threads)
if(OpenMP_C_FOUND)
    target_link_libraries(target_bounds_test PRIVATE OpenMP::OpenMP_C)
endif()
if(SG_ATOMIC_LIB)
    target_link_libraries(target_bounds_test PRIVATE ${SG_ATOMIC_LIB})
endif()
if(NOT MSVC)
    target_compile_options(target_bounds_test PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(target_bounds_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME target_bounds COMMAND target_bounds_test)

# --- Server vector-thrust ("vec") contract (2026.10.07.02 pilot      ---
# --- hat-switch retro-thrusters) -------------------------------------
# The pilot hat needs a thrust command that translates the ship
# WITHOUT touching its attitude (the 3-arg impulse always aligns the
# nose first — the pre-fix hat read as a 180-deg view spin and no
# thruster ever fired). The new "vec <H> <M> <S>" command sets the
# flight vector from the requested course and cruises IMPULSE along
# it with van_h/van_m/van_r untouched; "vec H M 0" releases the
# thrust. This test pins that contract against the REAL commands.c
# (same scaffolding as target_bounds: production-size globals, the
# send_server_msg recorder, ASan/UBSan build).
add_executable(vec_thrust_test
    vec_thrust_test.c
    ${SG_ROOT}/src/server/commands.c
    ${SG_ROOT}/src/server/logic.c
    ${SG_ROOT}/src/server/logic_events.c
    ${SG_ROOT}/src/server/nav_math.c
    ${SG_ROOT}/src/server/targets.c
    ${SG_ROOT}/src/sglog.c)
target_link_libraries(vec_thrust_test PRIVATE m Threads::Threads)
if(OpenMP_C_FOUND)
    target_link_libraries(vec_thrust_test PRIVATE OpenMP::OpenMP_C)
endif()
if(SG_ATOMIC_LIB)
    target_link_libraries(vec_thrust_test PRIVATE ${SG_ATOMIC_LIB})
endif()
if(NOT MSVC)
    target_compile_options(vec_thrust_test PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(vec_thrust_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME vec_thrust COMMAND vec_thrust_test)

# --- Collisions: a ship (warp / impulse) or a comet that hits a solid object
# (real update_game_logic() on a hand-made spatial index; same scaffolding
# as vec_thrust_test) -------------------------------------------------------
add_executable(collision_test
    collision_test.c
    ${SG_ROOT}/src/server/commands.c
    ${SG_ROOT}/src/server/logic.c
    ${SG_ROOT}/src/server/logic_events.c
    ${SG_ROOT}/src/server/nav_math.c
    ${SG_ROOT}/src/server/targets.c
    ${SG_ROOT}/src/sglog.c)
target_link_libraries(collision_test PRIVATE m Threads::Threads)
if(OpenMP_C_FOUND)
    target_link_libraries(collision_test PRIVATE OpenMP::OpenMP_C)
endif()
if(SG_ATOMIC_LIB)
    target_link_libraries(collision_test PRIVATE ${SG_ATOMIC_LIB})
endif()
if(NOT MSVC)
    target_compile_options(collision_test PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(collision_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME collision COMMAND collision_test)

# --- Network fuzzing: the REAL libFuzzer harness for the packet ---
# --- dispatcher (2026.10.11.01: replaces the no-op stub) ------------
# One input = one full client session (a sequence of type+body
# packets); the harness replays it through the production LEGACY loop
# (spacegl_server.c's read-type + dispatch_packet with io->rx == NULL)
# over a non-blocking socketpair, with the login path's file I/O
# sandboxed into a mkdtemp() directory. The code under test is the
# WHOLE server ingress: packets.c (handshake signature, login identity
# checks, the B1 message-length guard, unknown-type teardown), net.c
# (read_all/write_all, the broadcast/relay crypto paths) and
# commands.c (the attacker-controlled command parser, full registry).
set(FUZZ_DISPATCH_SRCS
    fuzz_packets.c
    ${SG_ROOT}/src/server/packets.c
    ${SG_ROOT}/src/server/net.c
    ${SG_ROOT}/src/server/conn.c
    ${SG_ROOT}/src/server/globals.c
    ${SG_ROOT}/src/server/log.c
    ${SG_ROOT}/src/server/threadpool.c
    ${SG_ROOT}/src/server/commands.c
    ${SG_ROOT}/src/server/logic.c
    ${SG_ROOT}/src/server/logic_events.c
    ${SG_ROOT}/src/server/nav_math.c
    ${SG_ROOT}/src/server/targets.c
    ${SG_ROOT}/src/sglog.c)

# 1) fuzz_corpus_test: plain main() over the committed seed corpus
#    (tests/fuzz/corpus, generated by tests/fuzz/gen_corpus.py),
#    ASan/UBSan, on EVERY compiler — the corpus is a regression suite
#    that ctest runs on both the gcc and the clang matrix jobs.
add_executable(fuzz_corpus_test ${FUZZ_DISPATCH_SRCS})
# Fixed server handshake key in the harness only (handshake_v2.h), so the
# seeds can precompute the session key that masks the login credentials.
target_compile_definitions(fuzz_corpus_test PRIVATE FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION)
target_link_libraries(fuzz_corpus_test PRIVATE m Threads::Threads OpenSSL::Crypto)
if(OpenMP_C_FOUND)
    target_link_libraries(fuzz_corpus_test PRIVATE OpenMP::OpenMP_C)
endif()
if(SG_ATOMIC_LIB)
    target_link_libraries(fuzz_corpus_test PRIVATE ${SG_ATOMIC_LIB})
endif()
if(NOT MSVC)
    target_compile_options(fuzz_corpus_test PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(fuzz_corpus_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME fuzz_corpus
         COMMAND fuzz_corpus_test "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus")

# --- Rate limiting & DoS protection test (2026.10.17.04 / Prompt 3) ---
# Verifies token bucket rate limiting (PKT_RATE_LIMIT = 200 pkt/sec,
# PKT_DROP_THRESHOLD = 10 drops before disconnect, 1s token refill).
set(RATE_LIMIT_SRCS
    rate_limit_test.c
    ${SG_ROOT}/src/server/packets.c
    ${SG_ROOT}/src/server/net.c
    ${SG_ROOT}/src/server/conn.c
    ${SG_ROOT}/src/server/globals.c
    ${SG_ROOT}/src/server/log.c
    ${SG_ROOT}/src/server/threadpool.c
    ${SG_ROOT}/src/server/commands.c
    ${SG_ROOT}/src/server/logic.c
    ${SG_ROOT}/src/server/logic_events.c
    ${SG_ROOT}/src/server/nav_math.c
    ${SG_ROOT}/src/server/targets.c
    ${SG_ROOT}/src/sglog.c)

add_executable(rate_limit_test ${RATE_LIMIT_SRCS})
target_link_libraries(rate_limit_test PRIVATE m Threads::Threads OpenSSL::Crypto)
if(OpenMP_C_FOUND)
    target_link_libraries(rate_limit_test PRIVATE OpenMP::OpenMP_C)
endif()
if(SG_ATOMIC_LIB)
    target_link_libraries(rate_limit_test PRIVATE ${SG_ATOMIC_LIB})
endif()
if(NOT MSVC)
    target_compile_options(rate_limit_test PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(rate_limit_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME rate_limit COMMAND rate_limit_test)

# --- Relay routing snapshot (2026.10.31.02): encrypt_payload explicit ---
# length (NULs kept), byte-exact fleet relay to cleartext and encrypted
# recipients, captured route of send_server_msg, player_socket_detach()
# waking a writer blocked in send(). Same sources as rate_limit_test.
string(REPLACE "rate_limit_test.c" "relay_snapshot_test.c" RELAY_SNAPSHOT_SRCS "${RATE_LIMIT_SRCS}")
add_executable(relay_snapshot_test ${RELAY_SNAPSHOT_SRCS})
target_link_libraries(relay_snapshot_test PRIVATE m Threads::Threads OpenSSL::Crypto)
if(OpenMP_C_FOUND)
    target_link_libraries(relay_snapshot_test PRIVATE OpenMP::OpenMP_C)
endif()
if(SG_ATOMIC_LIB)
    target_link_libraries(relay_snapshot_test PRIVATE ${SG_ATOMIC_LIB})
endif()
if(NOT MSVC)
    target_compile_options(relay_snapshot_test PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(relay_snapshot_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME relay_snapshot COMMAND relay_snapshot_test)
set_tests_properties(relay_snapshot PROPERTIES TIMEOUT 60)

# --- PKT_LOGIN identity proof (2026.10.31.04, roadmap phase 0) ---
# The real dispatcher refuses PKT_LOGIN without an identity record, with a
# wrong hash or a wrong salt; a verified login binds the slot and takes over
# a captain still bound to another connection. Same sources as rate_limit.
string(REPLACE "rate_limit_test.c" "login_auth_test.c" LOGIN_AUTH_SRCS "${RATE_LIMIT_SRCS}")
add_executable(login_auth_test ${LOGIN_AUTH_SRCS})
target_link_libraries(login_auth_test PRIVATE m Threads::Threads OpenSSL::Crypto)
if(OpenMP_C_FOUND)
    target_link_libraries(login_auth_test PRIVATE OpenMP::OpenMP_C)
endif()
if(SG_ATOMIC_LIB)
    target_link_libraries(login_auth_test PRIVATE ${SG_ATOMIC_LIB})
endif()
if(NOT MSVC)
    target_compile_options(login_auth_test PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(login_auth_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME login_auth COMMAND login_auth_test)
set_tests_properties(login_auth PROPERTIES TIMEOUT 60)

# --- Radio sender set by the server (2026.10.31.05, roadmap phase 0) ---
# Forged "from" and "faction" are replaced by the authenticated captain's
# before the relay; station names (COMPUTER, STARBASE, ...) are reserved.
string(REPLACE "rate_limit_test.c" "sender_stamp_test.c" SENDER_STAMP_SRCS "${RATE_LIMIT_SRCS}")
add_executable(sender_stamp_test ${SENDER_STAMP_SRCS})
target_link_libraries(sender_stamp_test PRIVATE m Threads::Threads OpenSSL::Crypto)
if(OpenMP_C_FOUND)
    target_link_libraries(sender_stamp_test PRIVATE OpenMP::OpenMP_C)
endif()
if(SG_ATOMIC_LIB)
    target_link_libraries(sender_stamp_test PRIVATE ${SG_ATOMIC_LIB})
endif()
if(NOT MSVC)
    target_compile_options(sender_stamp_test PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(sender_stamp_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME sender_stamp COMMAND sender_stamp_test)
set_tests_properties(sender_stamp PROPERTIES TIMEOUT 60)

# --- Client wire counts (2026.10.31.06, roadmap phase 0) ---
# include/client_wire.h with hostile event/torpedo/beam/object counts,
# negative included, under ASan/UBSan.
add_executable(client_wire_test client_wire_test.c)
if(NOT MSVC)
    target_compile_options(client_wire_test PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(client_wire_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME client_wire COMMAND client_wire_test)

# --- Master key file (2026.10.31.07, roadmap phase 0) ---
# load_master_key(): created 0600 atomically (O_EXCL temp + link), corrupt
# file refused and untouched, loose mode tightened, short SPACEGL_KEY
# refused, concurrent first start yields one key.
add_executable(key_file_test key_file_test.c)
target_link_libraries(key_file_test PRIVATE OpenSSL::Crypto)
if(NOT MSVC)
    target_compile_options(key_file_test PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(key_file_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME key_file COMMAND key_file_test)

# --- Handshake v2 (2026.10.31.08, roadmap phase 1) ---
# RFC 5869 / RFC 7748 vectors, the exchange, refusals, the master key kept
# off the wire, and the real dispatcher (v1 refused, v2 answered).
string(REPLACE "rate_limit_test.c" "handshake_v2_test.c" HANDSHAKE_V2_SRCS "${RATE_LIMIT_SRCS}")
add_executable(handshake_v2_test ${HANDSHAKE_V2_SRCS})
target_link_libraries(handshake_v2_test PRIVATE m Threads::Threads OpenSSL::Crypto)
if(OpenMP_C_FOUND)
    target_link_libraries(handshake_v2_test PRIVATE OpenMP::OpenMP_C)
endif()
if(SG_ATOMIC_LIB)
    target_link_libraries(handshake_v2_test PRIVATE ${SG_ATOMIC_LIB})
endif()
if(NOT MSVC)
    target_compile_options(handshake_v2_test PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(handshake_v2_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME handshake_v2 COMMAND handshake_v2_test)
set_tests_properties(handshake_v2 PROPERTIES TIMEOUT 60)

# --- B2-b: radio cipher table index bounds (ASan/UBSan) ---
add_executable(crypto_table_oob_test crypto_table_oob_test.c)
target_link_libraries(crypto_table_oob_test PRIVATE OpenSSL::Crypto)
if(NOT MSVC)
    target_compile_options(crypto_table_oob_test PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(crypto_table_oob_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME crypto_table_oob COMMAND crypto_table_oob_test)

# --- B3-b: network object/beam count bounds (ASan/UBSan) ---
add_executable(net_count_oob_test net_count_oob_test.c)
if(NOT MSVC)
    target_compile_options(net_count_oob_test PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(net_count_oob_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME net_count_oob COMMAND net_count_oob_test)

# 2) fuzz_packets: the real libFuzzer (clang only — libFuzzer is a
#    clang/LLVM feature; -fsanitize=fuzzer supplies main()). CI runs it
#    in the dedicated fuzz job: ./build/tests/fuzz_packets
#    tests/fuzz/corpus -max_total_time=240
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    add_executable(fuzz_packets ${FUZZ_DISPATCH_SRCS})
    target_compile_definitions(fuzz_packets PRIVATE FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION)
    target_link_libraries(fuzz_packets PRIVATE m Threads::Threads OpenSSL::Crypto)
    if(OpenMP_C_FOUND)
        target_link_libraries(fuzz_packets PRIVATE OpenMP::OpenMP_C)
    endif()
    if(SG_ATOMIC_LIB)
        target_link_libraries(fuzz_packets PRIVATE ${SG_ATOMIC_LIB})
    endif()
    target_compile_options(fuzz_packets PRIVATE
        -DFUZZ_LIBFUZZER
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(fuzz_packets PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

# --- Golden render test (2026.10.11.01) ------------------------------
# Renders frame 30 of BOTH render paths headlessly with the
# deterministic flags (--seed 1 --no-rotate --window 640x480; the
# viewer auto-creates the empty SHM segment) and compares with
# tests/golden_test.py: CPU-vs-GDD parity (tol 25.0 — the starfield
# is identical geometry/data but rasterized with different primitives
# per path, so the cross-path RMSE floor is ~18) and GDD vs the
# committed golden image tests/golden/golden_gdd.ppm (tol 5.0 — the
# real regression pin, same path to same path). Integrated mode only
# (it needs the spacegl_vulkan product binary); SKIP_RETURN_CODE 77
# covers no-ICD / no-display / no-PIL environments (e.g. RPM chroots).
if(TARGET spacegl_vulkan)
    add_test(NAME golden
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_golden.sh
                     $<TARGET_FILE:spacegl_vulkan>)
    # The viewer resolves its SPIR-V shaders relative to the cwd
    # ("build/shaders"), but ctest runs from <build>/tests: point it at
    # the freshly compiled ones, otherwise it exits rc=1 and the runner
    # reports a bogus "no Vulkan ICD" SKIP.
    set_tests_properties(golden PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 RUN_SERIAL TRUE
                         ENVIRONMENT "SPACEGL_SHADERS_DIR=${CMAKE_BINARY_DIR}/shaders")
endif()

# --- GDD per-pass benchmark (2026.10.11.01) --------------------------
# Median ms of the cull/expand/final passes (100 runs, warmup done,
# one fence-wait per pass — the client's per-pass rhythm) against the
# committed regression thresholds tests/gdd_bench_thresholds.txt:
# a median over its ceiling exits 2 = test failure = CI regression.
# SKIP_RETURN_CODE 77 = no Vulkan ICD (the same skip the contract
# test uses, so the suite stays green in bare build chroots).
add_test(NAME gdd_bench
         COMMAND gdd_mesh_test "${GDD_SPV_DIR}" --benchmark 100
                 "${CMAKE_CURRENT_SOURCE_DIR}/gdd_bench_thresholds.txt")
set_tests_properties(gdd_bench PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 RUN_SERIAL TRUE)

# --- Server tick rate benchmark (2026.10.18.04 / Prompt 6) -----------
# 1000 ticks of the REAL game loop (update_game_logic + global_tick++
# + spacegl_master.frame_id++ under game_mutex, the exact sequence of
# spacegl_server.c's game_loop_thread) over a worst-case-but-stable
# scene: 16/16 players active with full vitals, all dynamic pools at
# production capacity (4000 NPCs, 1000 monsters, ...), all 63 static
# arrays at capacity, 64/64 torpedoes. The average tick must stay under
# threshold_ms and the p99 under p99_ms from
# tests/server_tick_thresholds.txt (8.0 / 12.0 ms: half and three
# quarters of the 16.7 ms frame budget, the headroom covering
# sign_galaxy_data + send_pending_updates + epoll). A refactoring that
# doubles the tick time is visible in the CI log; one that quadruples
# it fails. The test TU mirrors galaxy.c's rebuild_spatial_index() (the
# tick's two per-tick calls, see the file header) because galaxy.c owns
# the production globals and cannot be linked; the static insertion is
# the REAL production code (quad_index.c). OpenMP is linked so the
# logic_events.c parallel loops behave as in the server binary.
add_executable(server_tick_bench
    server_tick_bench.c
    ${SG_ROOT}/src/server/log.c
    ${SG_ROOT}/src/server/logic.c
    ${SG_ROOT}/src/server/logic_events.c
    ${SG_ROOT}/src/server/nav_math.c
    ${SG_ROOT}/src/server/quad_index.c
    ${SG_ROOT}/src/server/targets.c
    ${SG_ROOT}/src/server/globals.c
    ${SG_ROOT}/src/sglog.c)
target_link_libraries(server_tick_bench PRIVATE m Threads::Threads)
if(OpenMP_C_FOUND)
    target_link_libraries(server_tick_bench PRIVATE OpenMP::OpenMP_C)
endif()
if(SG_ATOMIC_LIB)
    target_link_libraries(server_tick_bench PRIVATE ${SG_ATOMIC_LIB})
endif()
# The threshold gate applies ONLY in optimized, non-instrumented builds:
# the CI matrix's Debug jobs carry ASan/UBSan (-fsanitize in
# CMAKE_C_FLAGS, a 2-3x slowdown that distorts tick times), and a plain
# -O0 Debug build is just as unrepresentative. In those the benchmark
# runs with --report-only (the numbers are still printed in the CI log;
# the gate stays on for Release/RelWithDebInfo/MinSizeRel without
# sanitizers).
#
# SPACEGL_TICK_BENCH_GATE (default ON): set it to OFF on shared/virtualized
# build farms (e.g. the RPM %check on COPR/Koji) to keep the numbers in
# the log but never fail the build on a wall-clock measurement: a timing
# gate is only meaningful on a quiet machine.
option(SPACEGL_TICK_BENCH_GATE
       "Fail server_tick_bench when the tick time exceeds the thresholds" ON)
set(SG_TICK_BENCH_ARGS
    --threshold-file "${CMAKE_CURRENT_SOURCE_DIR}/server_tick_thresholds.txt")
if(NOT SPACEGL_TICK_BENCH_GATE)
    list(APPEND SG_TICK_BENCH_ARGS --report-only)
elseif(CMAKE_C_FLAGS MATCHES "sanitize" OR CMAKE_CXX_FLAGS MATCHES "sanitize")
    list(APPEND SG_TICK_BENCH_ARGS --report-only)
elseif(NOT CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    list(APPEND SG_TICK_BENCH_ARGS --report-only)
endif()
add_test(NAME server_tick_bench COMMAND server_tick_bench ${SG_TICK_BENCH_ARGS})
# RUN_SERIAL: the benchmark measures wall-clock time (and its OpenMP loops
# use every core), so it must never share the machine with other tests.
# RPM's %ctest (and `ctest -j`) runs tests in parallel: without this the
# tick is slowed 3-10x by the concurrent tests (avg and especially p99).
set_tests_properties(server_tick_bench PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

# --- Long-running stability test / memory-leak check (2026.10.18.04 / Prompt 6)
# The REAL server + the REAL client (fresh recruit, "pos" every 2 s) run
# for 60 s; the server's RSS may not grow by more than 10 MB over the
# window (~170 B/tick = ~36 MB/h leak would fail). Runs in the default
# ctest suite and in the CI smoke job; exclude it from a fast local run
# with: ctest --output-on-failure -E "stability". Integrated mode only
# (it needs the spacegl_server/spacegl_client product binaries); SKIP
# (77) covers chroot/bare environments where the binaries are absent.
if(TARGET spacegl_server AND TARGET spacegl_client)
    add_test(NAME stability
             COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/stability_test.sh" 60
                     $<TARGET_FILE:spacegl_server> $<TARGET_FILE:spacegl_client>)
    set_tests_properties(stability PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 120
        SKIP_RETURN_CODE 77
        PASS_REGULAR_EXPRESSION "PASS: stability_test"
        FAIL_REGULAR_EXPRESSION "FAIL: RSS grew")
    # Sanitized builds (the CI Debug jobs pass -fsanitize=address,undefined
    # in CMAKE_C_FLAGS, which also instruments the product binaries this
    # test runs): ASan keeps every freed block in a quarantine (256 MB by
    # default) before reusing it, so the server's RSS climbs by tens of MB
    # during the 60 s window even though nothing leaks (measured: +56 MB
    # with ASan, +1 MB in Release, +4.5 MB with the quarantine capped).
    # Cap the quarantine so the RSS delta measures real growth again. With
    # clang the ASan + libomp combination also adds per-thread allocator
    # state that grows with the OpenMP thread count (measured with 4
    # threads: +11.5 MB under clang+ASan, +1.0 MB in the Release build, so
    # not a leak): the sanitized jobs use a wider 30 MB threshold, the
    # strict 10 MB one stays on the non-instrumented builds.
    if(CMAKE_C_FLAGS MATCHES "sanitize" OR CMAKE_CXX_FLAGS MATCHES "sanitize")
        set_tests_properties(stability PROPERTIES
            ENVIRONMENT "ASAN_OPTIONS=quarantine_size_mb=1;STABILITY_LEAK_THRESHOLD_MB=30")
    endif()
endif()

# --- End-to-end smoke (2026.10.31.03): the CI smoke step inside ctest ---
# Same commands and assertions as the "End-to-end smoke" step of
# .github/workflows/ci.yml, so every matrix job and rpm %ctest catches a
# broken handshake/login/sync round-trip or a client that mangles the
# server's cleartext messages (the 2026.10.31.01 regression). RUN_SERIAL:
# the server always listens on port 5000, like the stability test.
if(TARGET spacegl_server AND TARGET spacegl_client)
    add_test(NAME smoke_e2e
             COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/smoke_test.sh"
                     $<TARGET_FILE:spacegl_server> $<TARGET_FILE:spacegl_client>)
    set_tests_properties(smoke_e2e PROPERTIES
        RUN_SERIAL TRUE
        TIMEOUT 90
        SKIP_RETURN_CODE 77
        PASS_REGULAR_EXPRESSION "PASS: smoke_test"
        FAIL_REGULAR_EXPRESSION "FAIL:")
endif()

# --- Standalone and developer convenience targets ---
set(ALL_TEST_TARGETS
    gdd_contract_test
    gdd_mesh_test
    nav_heading_test
    nav_roll_test
    quad_index_test
    pkt_length_test
    crypto_table_test
    target_bounds_test
    vec_thrust_test
    fuzz_corpus_test
    rate_limit_test
    crypto_table_oob_test
    net_count_oob_test
    server_tick_bench
)
if(TARGET fuzz_packets)
    list(APPEND ALL_TEST_TARGETS fuzz_packets)
endif()

# `cmake --build build-tests --target run-all` — standalone mode:
# build and run every test, print a summary table.
add_custom_target(run-all
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS ${ALL_TEST_TARGETS}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    USES_TERMINAL
    COMMENT "Running all ${CMAKE_PROJECT_NAME} tests (standalone mode)"
)
set(ALL_TEST_TARGETS ${ALL_TEST_TARGETS} PARENT_SCOPE)
