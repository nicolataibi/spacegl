/*
 * SPACE GL - OpenGL function loader (GLEW replacement)
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 */

#include "gl_loader.h"
#include <GLFW/glfw3.h>
#include <stdio.h>

#define SG_GL_FN(T, N) T sg_##N = NULL;
#define SG_GL_FN_ALT(T, N, ALT) T sg_##N = NULL;
#include "gl_loader_funcs.inc"
#undef SG_GL_FN
#undef SG_GL_FN_ALT

bool sg_gl_load(void) {
    int missing = 0;

#define SG_GL_FN(T, N) \
    sg_##N = (T)glfwGetProcAddress(#N); \
    if (!sg_##N) { fprintf(stderr, "[GL LOADER] missing: %s\n", #N); missing++; }
#define SG_GL_FN_ALT(T, N, ALT) \
    sg_##N = (T)glfwGetProcAddress(#N); \
    if (!sg_##N) sg_##N = (T)glfwGetProcAddress(#ALT); \
    if (!sg_##N) { fprintf(stderr, "[GL LOADER] missing: %s (or %s)\n", #N, #ALT); missing++; }
#include "gl_loader_funcs.inc"
#undef SG_GL_FN
#undef SG_GL_FN_ALT

    return missing == 0;
}
