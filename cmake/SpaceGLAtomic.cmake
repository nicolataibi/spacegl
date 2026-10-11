# =========================================================================
# SpaceGL - libatomic resolution (cmake/SpaceGLAtomic.cmake)
#
# ONE shared module so the root CMakeLists.txt and the standalone
# tests/CMakeLists.txt can never drift apart (same policy as
# SpaceGLVulkanSDK.cmake).
#
# WHY: `#pragma omp atomic` on 64-bit state fields (the energy drain in
# src/server/logic.c) is code-generated as __atomic_load /
# __atomic_compare_exchange calls. gcc resolves them from libgcc (or
# auto-links libatomic); clang does NOT link libatomic automatically,
# so every clang link of a TU with OpenMP atomics (spacegl_server,
# target_bounds_test, vec_thrust_test, the fuzz targets) fails with
# undefined references unless -latomic is added explicitly.
#
# The libatomic dev symlink is usually NOT in the default search path
# (it sits in gcc's private compiler directory, which the drivers -
# gcc AND clang - know about but find_library() does not), so a plain
# find_library(atomic) is unreliable. Instead we ask the driver:
# a tiny link test with -latomic. If the toolchain can resolve it,
# SG_ATOMIC_LIB is set to the bare library name ("atomic") to be added
# to the link line; if not, it stays empty (gcc toolchains link fine
# without it).
# =========================================================================

# spacegl_find_atomic_lib()
# Sets the (directory-)scoped variable SG_ATOMIC_LIB.
function(spacegl_find_atomic_lib)
    include(CheckCSourceCompiles)
    set(CMAKE_REQUIRED_LINK_OPTIONS "-latomic")
    check_c_source_compiles("int main(void){return 0;}" _SG_HAVE_LIBATOMIC)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if(_SG_HAVE_LIBATOMIC)
        set(SG_ATOMIC_LIB "atomic" PARENT_SCOPE)
    else()
        set(SG_ATOMIC_LIB "" PARENT_SCOPE)
    endif()
    unset(_SG_HAVE_LIBATOMIC CACHE)
endfunction()
