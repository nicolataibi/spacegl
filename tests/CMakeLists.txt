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
# module src/spacegl_vulkan_gdd.c) and ../include, so the tests always
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

# --- CPU contract + instance-builder test (no device needed) -----------
add_executable(gdd_contract_test
    gdd_contract_test.c
    ${SG_ROOT}/src/spacegl_vulkan_gdd.c
    ${SG_ROOT}/src/sglog.c)
target_link_libraries(gdd_contract_test PRIVATE Vulkan::Vulkan m)
if(GLFW_FOUND)
    target_include_directories(gdd_contract_test PRIVATE ${GLFW_INCLUDE_DIRS})
    target_link_libraries(gdd_contract_test PRIVATE ${GLFW_LIBRARIES})
else()
    target_compile_definitions(gdd_contract_test PRIVATE GDD_TEST_STUB_GLFW)
endif()
add_test(NAME gdd_contract COMMAND gdd_contract_test)

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
    ${SG_ROOT}/src/server/nav_math.c
    ${SG_ROOT}/src/server/targets.c
    ${SG_ROOT}/src/sglog.c)
target_link_libraries(target_bounds_test PRIVATE m Threads::Threads)
if(OpenMP_C_FOUND)
    target_link_libraries(target_bounds_test PRIVATE OpenMP::OpenMP_C)
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
    ${SG_ROOT}/src/server/nav_math.c
    ${SG_ROOT}/src/server/targets.c
    ${SG_ROOT}/src/sglog.c)
target_link_libraries(vec_thrust_test PRIVATE m Threads::Threads)
if(OpenMP_C_FOUND)
    target_link_libraries(vec_thrust_test PRIVATE OpenMP::OpenMP_C)
endif()
if(NOT MSVC)
    target_compile_options(vec_thrust_test PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(vec_thrust_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME vec_thrust COMMAND vec_thrust_test)
