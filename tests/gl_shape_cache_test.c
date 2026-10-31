/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * gl_shape_cache_test — the shared geometry of the OpenGL viewer
 * (src/glut_compat.c, 2026.10.31.41): glut shapes compiled into display
 * lists by their exact parameters and replayed.
 *  1. A scene of every shape (solid and wire spheres, cubes, cones,
 *     cylinders, tori, octahedra, icosahedra, lit, under a hull-like
 *     transform) rendered from display lists matches the same scene
 *     tessellated in immediate mode (at most 1 of 255 on 0.1% of the
 *     bytes: the driver may triangulate a list differently).
 *  2. A shape is compiled on its second use and replayed afterwards: from
 *     the third frame on every shape of the scene is a list replay.
 *  3. A parameter animated frame by frame (a pulsing radius) compiles no
 *     list: it stays in immediate mode, as before.
 *  4. The time of 2000 small spheres (32x32) drawn both ways is printed.
 * Needs an OpenGL context (a display, or xvfb-run): exit 77 (SKIP) without.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <GLFW/glfw3.h>
#include "glut_compat.h"

#define W 256
#define H 256

#define TEST_CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "TEST FAILED: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

static void setup_view(void) {
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0, 1.0, 0.5, 100.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_NORMALIZE);
    GLfloat lp[] = { 0, 0, 10, 1 };
    glLightfv(GL_LIGHT0, GL_POSITION, lp);
}

static void scene(void) {
    glClearColor(0.0f, 0.0f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -14.0f);
    glRotatef(23.0f, 1.0f, 0.4f, 0.0f);
    struct {
        float x, y;
    } at[12] = { { -4, 3 }, { -1.3f, 3 }, { 1.3f, 3 }, { 4, 3 }, { -4, 0 }, { -1.3f, 0 }, { 1.3f, 0 }, { 4, 0 }, { -4, -3 }, { -1.3f, -3 }, { 1.3f, -3 }, { 4, -3 } };
    for (int n = 0; n < 12; n++) {
        glPushMatrix();
        glTranslatef(at[n].x, at[n].y, 0.0f);
        glScalef(1.1f, 0.9f, 1.0f);   /* a non-uniform scale, like the ship models */
        glColor3f(0.3f + 0.05f * (float)n, 0.8f - 0.05f * (float)n, 0.6f);
        switch (n) {
            case 0: glutSolidSphere(1.0, 24, 24); break;
            case 1: glutWireSphere(0.9, 16, 16); break;
            case 2: glutSolidCube(1.4); break;
            case 3: glutWireCube(1.2); break;
            case 4: glutSolidCone(0.8, 1.6, 16, 4); break;
            case 5: glutSolidCylinder(0.6, 1.4, 16, 2); break;
            case 6: glutSolidTorus(0.3, 0.9, 12, 32); break;
            case 7: glutWireTorus(0.1, 1.0, 8, 32); break;
            case 8: glutSolidOctahedron(); break;
            case 9: glutSolidIcosahedron(); break;
            case 10: glutSolidDodecahedron(); break;
            default:
                glutSolidSphere(0.5, 8, 8);
                glutSolidSphere(0.25, 12, 12);
                break;
        }
        glPopMatrix();
    }
    glFinish();
}

/* The same vertex stream, replayed: the driver may still split a quad or
 * a strip into triangles another way inside a list (Mesa does), so the
 * interpolation can round one step differently on a few pixels. Allowed:
 * a difference of 1 per channel on at most 0.1% of the bytes. A wrong or
 * missing shape changes thousands of pixels by far more. */
static size_t compare(const unsigned char *a, const unsigned char *b, size_t sz, const char *what) {
    size_t nd = 0;
    int mx = 0;
    for (size_t k = 0; k < sz; k++) {
        int d = abs((int)a[k] - (int)b[k]);
        if (d) {
            nd++;
            if (d > mx) mx = d;
        }
    }
    if (mx > 1 || nd > sz / 1000) {
        fprintf(stderr, "TEST FAILED: %s differs from immediate mode: %zu bytes, max %d\n", what, nd, mx);
        exit(1);
    }
    return nd;
}

static void grab(unsigned char *px) {
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

int main(void) {
    if (!glfwInit()) {
        printf("SKIP: gl_shape_cache: no display for GLFW\n");
        return 77;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    GLFWwindow *win = glfwCreateWindow(W, H, "gl_shape_cache_test", NULL, NULL);
    if (!win) {
        glfwTerminate();
        printf("SKIP: gl_shape_cache: no OpenGL context\n");
        return 77;
    }
    glfwMakeContextCurrent(win);
    printf("OpenGL: %s / %s\n", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    setup_view();

    size_t sz = (size_t)W * H * 4;
    unsigned char *ref = malloc(sz), *got = malloc(sz);
    TEST_CHECK(ref && got);

    /* 1 + 2: immediate reference, then three frames with the cache */
    glut_shape_cache_enable(false);
    scene();
    grab(ref);
    size_t lit = 0;
    for (size_t k = 0; k < sz; k += 4)
        if (ref[k] > 10 || ref[k + 1] > 10) lit++;
    TEST_CHECK(lit > (size_t)W * H / 20);   /* the scene is really drawn */

    glut_shape_cache_enable(true);
    GlutShapeStats st;
    glut_shape_stats(NULL, true);
    scene();                                   /* frame 1: seen once, immediate */
    glut_shape_stats(&st, true);
    TEST_CHECK(st.calls > 0 && st.immediate == st.calls && st.compiled == 0);
    grab(got);
    TEST_CHECK(memcmp(ref, got, sz) == 0);     /* the same immediate path */
    scene();                                   /* frame 2: compiled and replayed */
    glut_shape_stats(&st, true);
    TEST_CHECK(st.compiled == st.calls && st.list_hits == st.calls);
    grab(got);
    compare(ref, got, sz, "frame 2");
    scene();                                   /* frame 3: replayed */
    glut_shape_stats(&st, true);
    TEST_CHECK(st.compiled == 0 && st.list_hits == st.calls && st.immediate == 0);
    grab(got);
    size_t nd = compare(ref, got, sz, "frame 3");
    printf("[1] display-list scene matches immediate mode (%zu lit pixels; %zu of %zu bytes differ by 1, rounding): OK\n",
           lit, nd, sz);
    printf("[2] compiled on the second use (%lu shapes), replayed from the third frame: OK\n", st.calls);

    /* 3: an animated radius never compiles a list */
    glut_shape_stats(NULL, true);
    for (int f = 0; f < 100; f++) glutSolidSphere(0.5 + 0.05 * sin((double)f * 0.37), 16, 16);
    glut_shape_stats(&st, true);
    TEST_CHECK(st.compiled == 0 && st.immediate == 100);
    printf("[3] animated radius: 100 frames, no list compiled: OK\n");

    /* 4: timing of the submission (tiny spheres: almost no pixels, so the
     * time is the vertex work, not the rasterization) */
    double t_imm, t_list;
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -14.0f);
    glScalef(0.01f, 0.01f, 0.01f);
    glut_shape_cache_enable(false);
    double t0 = glfwGetTime();
    for (int n = 0; n < 2000; n++) glutSolidSphere(1.0, 32, 32);
    glFinish();
    t_imm = glfwGetTime() - t0;
    glut_shape_cache_enable(true);
    glutSolidSphere(1.0, 32, 32);
    glutSolidSphere(1.0, 32, 32);   /* compiled */
    glFinish();
    t0 = glfwGetTime();
    for (int n = 0; n < 2000; n++) glutSolidSphere(1.0, 32, 32);
    glFinish();
    t_list = glfwGetTime() - t0;
    printf("[4] 2000 small spheres 32x32: immediate %.1f ms, display list %.1f ms\n", t_imm * 1000.0, t_list * 1000.0);

    free(ref);
    free(got);
    glfwDestroyWindow(win);
    glfwTerminate();
    printf("PASS: gl_shape_cache\n");
    return 0;
}
