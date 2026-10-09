# =========================================================================
# SpaceGL - Vulkan SDK resolution
#
# Shared by the top-level CMakeLists.txt and the standalone
# tests/CMakeLists.txt: ONE policy, ONE implementation, so the two trees
# can never drift apart (the pre-chain code duplicated this block in both
# files).
#
# The project builds against ONE Vulkan, resolved by a CHAIN (project
# decision, see changelog 2026.10.09.01). The first usable entry wins:
#
#   1. the VULKAN_SDK environment variable
#      The official Vulkan SDK variable: the explicit per-machine
#      override. Set AND usable, it wins over the pinned default.
#      Set but NOT usable it warns and the chain continues (a stale
#      export must not brick the build).
#   2. the pinned default SDK (project default)
#      DEFAULT_VULKAN_SDK, default /home/nick/dev/c/vulkan-sdk/
#      1.4.363.0/x86_64 - the documented project decision for
#      reproducible builds (headers, loader and glslc from the same
#      SDK). Disable: -DUSE_DEFAULT_VULKAN_SDK=OFF.
#   3. the system Vulkan
#      find_package(Vulkan) (official config, else CMake's FindVulkan
#      module: libvulkan + system headers), then, as last resort, the
#      distro from-source loader config (find_package(VulkanLoader),
#      the Vulkan::Loader target + the distro headers): that is what
#      Fedora & co. ship (there is no VulkanConfig.cmake, so the plain
#      find_package(Vulkan REQUIRED) of the pre-fix code could not
#      succeed on this machine).
#
# "Usable" for the SDK-root entries (1-2) means the root carries both
# the from-source loader CMake config
# (<root>/lib/VulkanLoader/lib/cmake/VulkanLoader) and the SDK headers
# (<root>/include/vulkan/vulkan.h). The from-source loader exports the
# target Vulkan::Loader, whose config carries no include dirs: they are
# attached here and the target is exposed as the official-name alias
# Vulkan::Vulkan that every target and test links.
#
# Usage:
#   list(APPEND CMAKE_MODULE_PATH <directory-containing-this-file>)
#   include(SpaceGLVulkanSDK)
#   spacegl_find_vulkan_sdk()
#
# After the call, in the caller's scope:
#   SpaceGL_VULKAN_SDK_ROOT    - the SDK root used ("" on the system leg)
#   SpaceGL_VULKAN_SDK_SOURCE  - "env" | "default" | "system"
# and a usable Vulkan::Vulkan target exists.
# =========================================================================

function(spacegl_find_vulkan_sdk)
    # Cache knobs (names/values preserved from the pre-chain code so
    # existing build trees keep their settings on re-configure).
    set(USE_DEFAULT_VULKAN_SDK ON CACHE BOOL
        "Use the pinned default Vulkan SDK (chain step 2)")
    set(DEFAULT_VULKAN_SDK "/home/nick/dev/c/vulkan-sdk/1.4.363.0/x86_64"
        CACHE STRING
        "Root of the pinned default Vulkan SDK (bin/include/lib/share layout)")

    set(_sg_root "")
    set(_sg_kind "")

    # --- chain step 1: the VULKAN_SDK environment variable -------------
    if(DEFINED ENV{VULKAN_SDK} AND NOT "$ENV{VULKAN_SDK}" STREQUAL "")
        if(EXISTS "$ENV{VULKAN_SDK}/lib/VulkanLoader/lib/cmake/VulkanLoader"
           AND EXISTS "$ENV{VULKAN_SDK}/include/vulkan/vulkan.h")
            set(_sg_root "$ENV{VULKAN_SDK}")
            set(_sg_kind "env")
        else()
            message(WARNING
                "Vulkan: VULKAN_SDK is set to '$ENV{VULKAN_SDK}' but is not "
                "usable (missing the from-source loader CMake config or the "
                "SDK headers); continuing the chain (pinned default, then system)")
        endif()
    endif()

    # --- chain step 2: the pinned default SDK (project decision) -------
    if(_sg_kind STREQUAL "" AND USE_DEFAULT_VULKAN_SDK)
        if(EXISTS "${DEFAULT_VULKAN_SDK}/lib/VulkanLoader/lib/cmake/VulkanLoader"
           AND EXISTS "${DEFAULT_VULKAN_SDK}/include/vulkan/vulkan.h")
            set(_sg_root "${DEFAULT_VULKAN_SDK}")
            set(_sg_kind "default")
        else()
            message(STATUS
                "Vulkan: pinned default SDK ${DEFAULT_VULKAN_SDK} not found "
                "or disabled (USE_DEFAULT_VULKAN_SDK=OFF); "
                "falling back to the system Vulkan")
        endif()
    endif()

    # --- chain step 3: the system Vulkan --------------------------------
    if(_sg_kind STREQUAL "")
        find_package(Vulkan QUIET)
        if(Vulkan_FOUND AND TARGET Vulkan::Vulkan)
            set(_sg_kind "system")
            if(Vulkan_DIR)
                set(_sg_detail "official config at ${Vulkan_DIR}")
            else()
                set(_sg_detail "system libvulkan + headers (FindVulkan module)")
            endif()
        else()
            find_package(VulkanLoader QUIET)
            if(VulkanLoader_FOUND AND TARGET Vulkan::Loader)
                # The distro loader config carries no include dirs: attach
                # the installed Vulkan headers to the target.
                find_path(VULKAN_HEADERS_INCLUDE_DIR NAMES vulkan/vulkan.h)
                if(NOT VULKAN_HEADERS_INCLUDE_DIR
                   OR VULKAN_HEADERS_INCLUDE_DIR STREQUAL "VULKAN_HEADERS_INCLUDE_DIR-NOTFOUND")
                    message(FATAL_ERROR
                        "Vulkan: system VulkanLoader found (${VulkanLoader_DIR}) "
                        "but vulkan/vulkan.h is missing - install the distro "
                        "Vulkan headers package (e.g. dnf install vulkan-headers)")
                endif()
                target_include_directories(Vulkan::Loader INTERFACE
                    "${VULKAN_HEADERS_INCLUDE_DIR}")
                if(NOT TARGET Vulkan::Vulkan)
                    add_library(Vulkan::Vulkan ALIAS Vulkan::Loader)
                endif()
                set(_sg_kind "system")
                set(_sg_detail "distro VulkanLoader config at ${VulkanLoader_DIR}")
            endif()
        endif()
        if(_sg_kind STREQUAL "")
            message(FATAL_ERROR
                "Vulkan: no usable Vulkan found along the chain:\n"
                "  1. the VULKAN_SDK environment variable (point it at an SDK root)\n"
                "  2. the pinned default SDK ${DEFAULT_VULKAN_SDK}\n"
                "  3. the system Vulkan (find_package(Vulkan), else the distro "
                "VulkanLoader package)\n"
                "Install one of them (e.g. dnf install vulkan-loader-devel "
                "vulkan-headers glslc) or set VULKAN_SDK to an SDK root.")
        endif()
    endif()

    # The SDK-root legs (env / pinned) use the from-source loader config:
    # it carries no include dirs, so point them at the SDK headers.
    if(_sg_kind STREQUAL "env" OR _sg_kind STREQUAL "default")
        find_package(VulkanLoader REQUIRED
            PATHS "${_sg_root}/lib/VulkanLoader" NO_DEFAULT_PATH)
        if(NOT TARGET Vulkan::Loader)
            message(FATAL_ERROR
                "Vulkan: ${_sg_kind} SDK ${_sg_root} carries the loader CMake "
                "config but it defined no Vulkan::Loader target (corrupt SDK?)")
        endif()
        target_include_directories(Vulkan::Loader INTERFACE "${_sg_root}/include")
        if(NOT TARGET Vulkan::Vulkan)
            add_library(Vulkan::Vulkan ALIAS Vulkan::Loader)
        endif()
    endif()

    if(_sg_kind STREQUAL "env")
        message(STATUS "Vulkan: using the SDK from VULKAN_SDK: ${_sg_root}")
    elseif(_sg_kind STREQUAL "default")
        message(STATUS "Vulkan: using the pinned default SDK ${_sg_root}")
    else()
        message(STATUS "Vulkan: using the system Vulkan (${_sg_detail})")
    endif()

    set(SpaceGL_VULKAN_SDK_ROOT "${_sg_root}" PARENT_SCOPE)
    set(SpaceGL_VULKAN_SDK_SOURCE "${_sg_kind}" PARENT_SCOPE)
endfunction()
