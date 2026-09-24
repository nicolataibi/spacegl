cmake_minimum_required(VERSION 3.10)
project(SpaceGL VERSION 2026.09.13 LANGUAGES C)

# --- Standard C e Opzioni di Compilazione ---
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Default to Release build if not specified.
# NOTE: an *empty* cached value (e.g. a build dir configured once with
# -DCMAKE_BUILD_TYPE=) also counts as "not specified": without this
# guard, on an existing cache the test would keep failing while the
# build silently loses -O3 and -D_FORTIFY_SOURCE=2 (which requires at
# least -O1 to be effective). The empty value is normalized to Release
# and written back to the cache so later re-configures keep it.
if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
    message(STATUS "CMAKE_BUILD_TYPE was not set (or empty): defaulting to Release")
endif()

# Fix OpenGL Policy CMP0072
set(OpenGL_GL_PREFERENCE GLVND)

# Definizioni globali
add_definitions(-D_XOPEN_SOURCE=700)

# Flag di sicurezza e avvisi (Fedora Compliance)
if(MSVC)
    add_compile_options(/W4)
else()
    # Enable PIE globally
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_compile_options(-fPIE)
    add_link_options(-pie)

    add_compile_options(-Wall -Wextra -Wpedantic -fstack-protector-strong -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security)
    
    # Add optimization and debug info based on build type
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-O0 -g)
    endif()

    add_link_options(-Wl,-z,relro,-z,now)
endif()

# --- Ricerca Dipendenze ---
find_package(Threads REQUIRED)
find_package(OpenMP REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(PkgConfig REQUIRED)

# Ncurses via PkgConfig
pkg_check_modules(NCURSES REQUIRED ncurses)

# GLFW e Vulkan per spacegl_vulkan
find_package(Vulkan REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

# --- Include Directories ---
include_directories(include ${OPENSSL_INCLUDE_DIR} ${OPENGL_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${NCURSES_INCLUDE_DIRS} ${GLFW_INCLUDE_DIRS})

# --- Vulkan Shader Compiler Helper ---
# glslc is a hard requirement: there is NO automatic fallback to
# glslangValidator on purpose (different CLI and SPIR-V defaults), so a
# missing compiler must fail loudly with an actionable message.
find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR
        "glslc not found in PATH - it is REQUIRED to compile the Vulkan shaders\n"
        "(assets/shaders/shader.vert / shader.frag -> SPIR-V).\n"
        "\n"
        "Install it with your distribution package manager, e.g.:\n"
        "  Fedora/RHEL     : dnf install glslc\n"
        "  Debian/Ubuntu   : apt install glslc\n"
        "\n"
        "or build it from source: https://github.com/google/shaderc\n"
        "(build target: glslc, then put the binary on PATH).\n"
        "\n"
        "Alternative (manual, not done automatically): compile the shaders\n"
        "with glslangValidator -V -o <name>.spv and place the result in\n"
        "the ${SPV_OUTPUT_DIR}/ directory.")
endif()

set(SHADER_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/shader.vert" 
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/shader.frag"
    # GPU-driven path (GDD): compute cull/expand/final passes + the
    # SSBO-based scene vertex/fragment shaders (common code in
    # assets/shaders/gdd_common.glsl). Used when SPACEGL_GPD=1.
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/gdd_cull.comp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/gdd_expand.comp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/gdd_final.comp"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/gdd_scene.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/gdd_scene.frag"
)
set(SPV_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY ${SPV_OUTPUT_DIR})

foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV_FILE "${SPV_OUTPUT_DIR}/${SHADER_NAME}.spv")
    # The GDD compute shaders #include the shared gdd_common.glsl (and
    # gdd_expand.comp also gdd_ops.glsl): recompile them when those
    # change, and give glslc the include dir.
    if(SHADER MATCHES "gdd_(cull|expand|final)\.comp$")
        set(SHADER_EXTRA_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/gdd_common.glsl"
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/gdd_ops.glsl")
        set(SHADER_INCLUDE_FLAG "-I${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders")
    else()
        set(SHADER_EXTRA_DEPENDS "")
        set(SHADER_INCLUDE_FLAG "")
    endif()
    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER_INCLUDE_FLAG} ${SHADER} -o ${SPV_FILE}
        DEPENDS ${SHADER} ${SHADER_EXTRA_DEPENDS}
        COMMENT "Compiling shader ${SHADER_NAME}"
    )
    list(APPEND SHADER_SPV_FILES ${SPV_FILE})
endforeach()

add_custom_target(spacegl_shaders ALL DEPENDS ${SHADER_SPV_FILES})

# --- Definizione Target ---

set(COMMON_LIBS Threads::Threads OpenMP::OpenMP_C OpenSSL::Crypto m rt)

# 1. spacegl_server
# Explicit source list (no file(GLOB): a new file in src/server/ would
# otherwise be silently ignored until CMake is re-run, and this build
# tree does not use CONFIGURE_DEPENDS). Add new server sources here.
set(SERVER_SRCS
    src/spacegl_server.c
    src/telemetry_server.c
    src/server/commands.c
    src/server/galaxy.c
    src/server/globals.c
    src/server/log.c
    src/server/logic.c
    src/server/nav_math.c
    src/server/net.c
    src/server/quad_index.c
    src/server/targets.c
    src/server/threadpool.c
)
add_executable(spacegl_server ${SERVER_SRCS})
target_link_libraries(spacegl_server ${COMMON_LIBS})

# 1b. spacegl_telemetry
add_executable(spacegl_telemetry src/spacegl_telemetry.c)
target_link_libraries(spacegl_telemetry ${COMMON_LIBS} ${NCURSES_LIBRARIES})

# 2. spacegl_client
add_executable(spacegl_client src/spacegl_client.c)
target_link_libraries(spacegl_client ${COMMON_LIBS})

# 3. spacegl_3dview
add_executable(spacegl_3dview src/spacegl_3dview.c src/glut_compat.c src/spacegl_extras_glfw.c)
target_link_libraries(spacegl_3dview ${COMMON_LIBS} ${GLFW_LIBRARIES} ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES} GLU)

# 4. spacegl_viewer
add_executable(spacegl_viewer src/spacegl_viewer.c)
target_link_libraries(spacegl_viewer ${COMMON_LIBS})

# 5. spacegl_hud
add_executable(spacegl_hud src/spacegl_hud.c)
target_link_libraries(spacegl_hud ${COMMON_LIBS} ${NCURSES_LIBRARIES})

# 6. spacegl_vulkan
# The GPU-driven path (SPACEGL_GPD=1) lives in src/spacegl_vulkan_gdd.c
# and shares the executable with the CPU-driven path (SPACEGL_GPD=0):
# one binary, two architectures, selected at startup (with automatic
# fallback to the CPU path when the device cannot support GDD).
add_executable(spacegl_vulkan src/spacegl_vulkan.c src/spacegl_vulkan_gdd.c)
target_link_libraries(spacegl_vulkan ${COMMON_LIBS} Vulkan::Vulkan ${GLFW_LIBRARIES})
add_dependencies(spacegl_vulkan spacegl_shaders)

# 7. spacegl_diag
add_executable(spacegl_diag src/spacegl_diag.c)
target_link_libraries(spacegl_diag ${COMMON_LIBS} ${NCURSES_LIBRARIES})

# --- Installazione ---

include(GNUInstallDirs)

install(TARGETS spacegl_server spacegl_client spacegl_3dview spacegl_viewer spacegl_hud spacegl_vulkan spacegl_diag spacegl_telemetry
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(PROGRAMS spacegl-server spacegl-client spacegl-diag
        DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES ${SHADER_SPV_FILES}
        DESTINATION ${CMAKE_INSTALL_DATADIR}/spacegl/shaders)

install(FILES
    man/man1/spacegl_server.1
    man/man1/spacegl_client.1
    man/man1/spacegl_3dview.1
    man/man1/spacegl_viewer.1
    man/man1/spacegl_vulkan.1
    man/man1/spacegl_hud.1
    man/man1/spacegl_diag.1
    man/man1/spacegl_telemetry.1
    man/man1/spacegl-server.1
    man/man1/spacegl-client.1
    man/man1/spacegl-diag.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
