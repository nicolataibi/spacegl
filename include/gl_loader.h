/*
 * SPACE GL - OpenGL function loader
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * Replaces GLEW: GLEW is built against GLX and fails glewInit() on a
 * Wayland/EGL context (GLEW_ERROR_NO_GLX_DISPLAY). Entry points beyond
 * OpenGL 1.1 are resolved with glfwGetProcAddress(), which is
 * windowing-system independent (GLX, EGL/Wayland, ...).
 *
 * Usage: after glfwMakeContextCurrent(window) call sg_gl_load(); it
 * returns false (and lists what is missing on stderr) if a required
 * function cannot be resolved.
 */
#ifndef SG_GL_LOADER_H
#define SG_GL_LOADER_H

#include <stdbool.h>
#include <GL/gl.h>
#include <GL/glext.h>

#ifdef GL_GLEXT_PROTOTYPES
#error "gl_loader.h: do not define GL_GLEXT_PROTOTYPES (it would clash with the loaded entry points)"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SG_GL_FN(T, N) extern T sg_##N;
#define SG_GL_FN_ALT(T, N, ALT) extern T sg_##N;
#include "gl_loader_funcs.inc"
#undef SG_GL_FN
#undef SG_GL_FN_ALT

bool sg_gl_load(void);

#ifdef __cplusplus
}
#endif

/* The code keeps calling the standard GL names. */
#define glActiveTexture sg_glActiveTexture
#define glAttachShader sg_glAttachShader
#define glBindBuffer sg_glBindBuffer
#define glBindFramebuffer sg_glBindFramebuffer
#define glBindRenderbuffer sg_glBindRenderbuffer
#define glBindVertexArray sg_glBindVertexArray
#define glBlitFramebuffer sg_glBlitFramebuffer
#define glBufferData sg_glBufferData
#define glBufferSubData sg_glBufferSubData
#define glCheckFramebufferStatus sg_glCheckFramebufferStatus
#define glCompileShader sg_glCompileShader
#define glCreateProgram sg_glCreateProgram
#define glCreateShader sg_glCreateShader
#define glDeleteFramebuffers sg_glDeleteFramebuffers
#define glDeleteRenderbuffers sg_glDeleteRenderbuffers
#define glDisableVertexAttribArray sg_glDisableVertexAttribArray
#define glEnableVertexAttribArray sg_glEnableVertexAttribArray
#define glFramebufferRenderbuffer sg_glFramebufferRenderbuffer
#define glFramebufferTexture2D sg_glFramebufferTexture2D
#define glGenBuffers sg_glGenBuffers
#define glGenFramebuffers sg_glGenFramebuffers
#define glGenRenderbuffers sg_glGenRenderbuffers
#define glGenVertexArrays sg_glGenVertexArrays
#define glGetAttribLocation sg_glGetAttribLocation
#define glGetProgramInfoLog sg_glGetProgramInfoLog
#define glGetProgramiv sg_glGetProgramiv
#define glGetShaderInfoLog sg_glGetShaderInfoLog
#define glGetShaderiv sg_glGetShaderiv
#define glGetUniformLocation sg_glGetUniformLocation
#define glLinkProgram sg_glLinkProgram
#define glRenderbufferStorageMultisample sg_glRenderbufferStorageMultisample
#define glShaderSource sg_glShaderSource
#define glUniform1f sg_glUniform1f
#define glUniform1i sg_glUniform1i
#define glUniform2f sg_glUniform2f
#define glUniform3f sg_glUniform3f
#define glUseProgram sg_glUseProgram
#define glVertexAttribPointer sg_glVertexAttribPointer
#define glDrawArraysInstancedARB sg_glDrawArraysInstancedARB
#define glVertexAttribDivisorARB sg_glVertexAttribDivisorARB

#endif /* SG_GL_LOADER_H */
