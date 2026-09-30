# =========================================================================
# SpaceGL - standalone unit-test project (tests/)
#
# This directory is SELF-CONTAINED: the main project's CMakeLists.txt has
# no reference to it (same layout as the DRAGONGL tests). Build and run
# the suite independently:
#
#   cmake -B build-tests tests
#   cmake --build build-tests
#   ctest --test-dir build-tests --output-on-failure
#
# The code under test is pulled straight from ../src (the GPU-driven
# module src/spacegl_vulkan_gdd.c) and ../include, so the tests always
# exercise the real production sources. Compile settings mirror the
# main project (C23, same warning flags).
# =========================================================================

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

# Project roots (this file is tests/CMakeLists.txt)
set(SG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

include_directories(${SG_ROOT}/include)

# The GPU module links against Vulkan (device-side functions) and, in the
# contract test, pulls the whole gdd.c TU which also references
# glfwGetTime (frame path): link GLFW when available, stub it otherwise.
#
# Vulkan SDK di default (stessa decisione del progetto principale, vedi
# ../CMakeLists.txt): SDK locale 1.4.363.0 per build riproducibili. Il
# loader built-from-source esporta Vulkan::Loader: alias Vulkan::Vulkan.
# Disable: -DUSE_DEFAULT_VULKAN_SDK=OFF
set(USE_DEFAULT_VULKAN_SDK ON CACHE BOOL "Use the default local Vulkan SDK")
set(DEFAULT_VULKAN_SDK "/home/nick/dev/c/vulkan-sdk/1.4.363.0/x86_64"
    CACHE STRING "Root of the default Vulkan SDK (bin/include/lib/share layout)")
if(USE_DEFAULT_VULKAN_SDK AND EXISTS "${DEFAULT_VULKAN_SDK}/lib/VulkanLoader/lib/cmake/VulkanLoader")
    find_package(VulkanLoader REQUIRED
        PATHS "${DEFAULT_VULKAN_SDK}/lib/VulkanLoader" NO_DEFAULT_PATH)
    if(TARGET Vulkan::Loader)
        target_include_directories(Vulkan::Loader INTERFACE "${DEFAULT_VULKAN_SDK}/include")
        if(NOT TARGET Vulkan::Vulkan)
            add_library(Vulkan::Vulkan ALIAS Vulkan::Loader)
        endif()
    endif()
else()
    find_package(Vulkan REQUIRED)
endif()
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GLFW glfw3)
endif()

enable_testing()

# --- CPU contract + instance-builder test (no device needed) -----------
# The bit-exact geometry mirrors in the builder tests must not be
# FMA-contracted: -ffp-contract=off matches the non-FMA GLSL shaders.
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
add_compile_options(-ffp-contract=off)
# Default SPIR-V location: the main project's build tree.
set(GDD_SPV_DIR "${SG_ROOT}/build/shaders" CACHE PATH "Directory holding the gdd_*.spv binaries")
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
