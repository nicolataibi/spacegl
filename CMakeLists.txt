cmake_minimum_required(VERSION 3.10)
project(SpaceGL VERSION 2026.04.12 LANGUAGES C)

# --- Standard C e Opzioni di Compilazione ---
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
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
find_package(GLUT REQUIRED)
find_package(GLEW REQUIRED)
find_package(PkgConfig REQUIRED)

# Ncurses via PkgConfig
pkg_check_modules(NCURSES REQUIRED ncurses)

# GLFW e Vulkan per spacegl_vulkan
find_package(Vulkan REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

# --- Include Directories ---
include_directories(include ${OPENSSL_INCLUDE_DIR} ${OPENGL_INCLUDE_DIR} ${GLUT_INCLUDE_DIR} ${GLEW_INCLUDE_DIR} ${NCURSES_INCLUDE_DIRS} ${GLFW_INCLUDE_DIRS})

# --- Helper per Compilazione Shader Vulkan ---
find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc compiler not found. Required for Vulkan shaders.")
endif()

file(GLOB_RECURSE SHADER_SOURCES "assets/shaders/*.vert" "assets/shaders/*.frag" "assets/shaders/*.comp")
set(SPV_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY ${SPV_OUTPUT_DIR})

foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV_FILE "${SPV_OUTPUT_DIR}/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPV_FILE}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER} -o ${SPV_FILE}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${SHADER_NAME}"
    )
    list(APPEND SHADER_SPV_FILES ${SPV_FILE})
endforeach()

add_custom_target(spacegl_shaders ALL DEPENDS ${SHADER_SPV_FILES})

# --- Definizione Target ---

set(COMMON_LIBS Threads::Threads OpenMP::OpenMP_C OpenSSL::Crypto m rt)

# 1. spacegl_server
file(GLOB SERVER_EXTRA_SRCS "src/server/*.c")
add_executable(spacegl_server src/spacegl_server.c ${SERVER_EXTRA_SRCS})
target_link_libraries(spacegl_server ${COMMON_LIBS})

# 2. spacegl_client
add_executable(spacegl_client src/spacegl_client.c)
target_link_libraries(spacegl_client ${COMMON_LIBS})

# 3. spacegl_3dview
add_executable(spacegl_3dview src/spacegl_3dview.c)
target_link_libraries(spacegl_3dview ${COMMON_LIBS} ${GLUT_LIBRARIES} ${GLEW_LIBRARIES} ${OPENGL_LIBRARIES})

# 4. spacegl_viewer
add_executable(spacegl_viewer src/spacegl_viewer.c)
target_link_libraries(spacegl_viewer ${COMMON_LIBS})

# 5. spacegl_hud
add_executable(spacegl_hud src/spacegl_hud.c)
target_link_libraries(spacegl_hud ${COMMON_LIBS} ${NCURSES_LIBRARIES})

# 6. spacegl_vulkan
add_executable(spacegl_vulkan src/spacegl_vulkan.c)
target_link_libraries(spacegl_vulkan ${COMMON_LIBS} Vulkan::Vulkan ${GLFW_LIBRARIES})
add_dependencies(spacegl_vulkan spacegl_shaders)

# 7. spacegl_diag
add_executable(spacegl_diag src/spacegl_diag.c)
target_link_libraries(spacegl_diag ${COMMON_LIBS} ${NCURSES_LIBRARIES})

# --- Installazione ---

include(GNUInstallDirs)

install(TARGETS spacegl_server spacegl_client spacegl_3dview spacegl_viewer spacegl_hud spacegl_vulkan spacegl_diag
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(PROGRAMS spacegl_server.sh spacegl_client.sh spacegl_diag.sh
        DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES ${SHADER_SPV_FILES}
        DESTINATION ${CMAKE_INSTALL_DATADIR}/spacegl/shaders)
