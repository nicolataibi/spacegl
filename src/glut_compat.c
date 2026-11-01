/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "glut_compat.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GLFW/glfw3.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* A fixed clock for deterministic renders (2026.11.01.09, the viewer's
 * --screenshot): every animation of the viewer reads the time through
 * glutGet(GLUT_ELAPSED_TIME); with the clock fixed it advances only by
 * glut_clock_step(), one frame at a time, so frame N is the same picture on
 * every run whatever the machine's speed. */
static double g_fixed_clock_ms = -1.0;

void glut_clock_fix(bool on) { g_fixed_clock_ms = on ? 0.0 : -1.0; }
void glut_clock_step(double ms) {
    if (g_fixed_clock_ms >= 0.0) g_fixed_clock_ms += ms;
}

int glutGet(int state) {
    if (state == GLUT_ELAPSED_TIME) {
        if (g_fixed_clock_ms >= 0.0) return (int)g_fixed_clock_ms;
        return (int)(glfwGetTime() * 1000.0);
    }
    return 0;
}

void glutSwapBuffers(void) {
    // No-op: we swap buffers in the main GLFW loop
}

static void solid_sphere_imm(double radius, int slices, int stacks) {
    GLUquadric* q = gluNewQuadric();
    gluQuadricNormals(q, GLU_SMOOTH);
    gluSphere(q, radius, slices, stacks);
    gluDeleteQuadric(q);
}

void glutWireSphere(double radius, int slices, int stacks) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glutSolidSphere(radius, slices, stacks);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

static void solid_cube_imm(double size) {
    double s = size / 2.0;
    glBegin(GL_QUADS);
    glNormal3f(0, 0, 1);
    glVertex3f((GLfloat)(-s), (GLfloat)(-s), (GLfloat)s); glVertex3f((GLfloat)s, (GLfloat)(-s), (GLfloat)s); glVertex3f((GLfloat)s, (GLfloat)s, (GLfloat)s); glVertex3f((GLfloat)(-s), (GLfloat)s, (GLfloat)s);
    glNormal3f(0, 0, -1);
    glVertex3f((GLfloat)(-s), (GLfloat)(-s), (GLfloat)(-s)); glVertex3f((GLfloat)(-s), (GLfloat)s, (GLfloat)(-s)); glVertex3f((GLfloat)s, (GLfloat)s, (GLfloat)(-s)); glVertex3f((GLfloat)s, (GLfloat)(-s), (GLfloat)(-s));
    glNormal3f(0, 1, 0);
    glVertex3f((GLfloat)(-s), (GLfloat)s, (GLfloat)(-s)); glVertex3f((GLfloat)(-s), (GLfloat)s, (GLfloat)s); glVertex3f((GLfloat)s, (GLfloat)s, (GLfloat)s); glVertex3f((GLfloat)s, (GLfloat)s, (GLfloat)(-s));
    glNormal3f(0, -1, 0);
    glVertex3f((GLfloat)(-s), (GLfloat)(-s), (GLfloat)(-s)); glVertex3f((GLfloat)s, (GLfloat)(-s), (GLfloat)(-s)); glVertex3f((GLfloat)s, (GLfloat)(-s), (GLfloat)s); glVertex3f((GLfloat)(-s), (GLfloat)(-s), (GLfloat)s);
    glNormal3f(1, 0, 0);
    glVertex3f((GLfloat)s, (GLfloat)(-s), (GLfloat)(-s)); glVertex3f((GLfloat)s, (GLfloat)s, (GLfloat)(-s)); glVertex3f((GLfloat)s, (GLfloat)s, (GLfloat)s); glVertex3f((GLfloat)s, (GLfloat)(-s), (GLfloat)s);
    glNormal3f(-1, 0, 0);
    glVertex3f((GLfloat)(-s), (GLfloat)(-s), (GLfloat)(-s)); glVertex3f((GLfloat)(-s), (GLfloat)(-s), (GLfloat)s); glVertex3f((GLfloat)(-s), (GLfloat)s, (GLfloat)s); glVertex3f((GLfloat)(-s), (GLfloat)s, (GLfloat)(-s));
    glEnd();
}

void glutWireCube(double size) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glutSolidCube(size);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

static void solid_cone_imm(double base, double height, int slices, int stacks) {
    GLUquadric* q = gluNewQuadric();
    gluQuadricNormals(q, GLU_SMOOTH);
    gluCylinder(q, base, 0.0, height, slices, stacks);
    glPushMatrix();
    glRotatef(180, 1, 0, 0);
    gluDisk(q, 0.0, base, slices, 1);
    glPopMatrix();
    gluDeleteQuadric(q);
}

void glutWireCone(double base, double height, int slices, int stacks) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glutSolidCone(base, height, slices, stacks);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

static void solid_cylinder_imm(double radius, double height, int slices, int stacks) {
    GLUquadric* q = gluNewQuadric();
    gluQuadricNormals(q, GLU_SMOOTH);
    gluCylinder(q, radius, radius, height, slices, stacks);
    glPushMatrix();
    glRotatef(180, 1, 0, 0);
    gluDisk(q, 0.0, radius, slices, 1);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(0, 0, (GLfloat)height);
    gluDisk(q, 0.0, radius, slices, 1);
    glPopMatrix();
    gluDeleteQuadric(q);
}

void glutWireCylinder(double radius, double height, int slices, int stacks) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glutSolidCylinder(radius, height, slices, stacks);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

static void solid_torus_imm(double innerRadius, double outerRadius, int nsides, int rings) {
    for (int i = 0; i < rings; i++) {
        double theta1 = (double)i * 2.0 * M_PI / rings;
        double theta2 = (double)(i + 1) * 2.0 * M_PI / rings;
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= nsides; j++) {
            double phi = (double)j * 2.0 * M_PI / nsides;
            double cos_phi = cos(phi);
            double sin_phi = sin(phi);
            
            double x1 = (outerRadius + innerRadius * cos_phi) * cos(theta1);
            double y1 = (outerRadius + innerRadius * cos_phi) * sin(theta1);
            double z1 = innerRadius * sin_phi;
            glNormal3f((GLfloat)(cos_phi * cos(theta1)), (GLfloat)(cos_phi * sin(theta1)), (GLfloat)sin_phi);
            glVertex3f((GLfloat)x1, (GLfloat)y1, (GLfloat)z1);
            
            double x2 = (outerRadius + innerRadius * cos_phi) * cos(theta2);
            double y2 = (outerRadius + innerRadius * cos_phi) * sin(theta2);
            double z2 = innerRadius * sin_phi;
            glNormal3f((GLfloat)(cos_phi * cos(theta2)), (GLfloat)(cos_phi * sin(theta2)), (GLfloat)sin_phi);
            glVertex3f((GLfloat)x2, (GLfloat)y2, (GLfloat)z2);
        }
        glEnd();
    }
}

void glutWireTorus(double innerRadius, double outerRadius, int nsides, int rings) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glutSolidTorus(innerRadius, outerRadius, nsides, rings);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

static void solid_octahedron_imm(void) {
    glBegin(GL_TRIANGLES);
    glNormal3f(1, 1, 1); glVertex3f(0, 1, 0); glVertex3f(1, 0, 0); glVertex3f(0, 0, 1);
    glNormal3f(-1, 1, 1); glVertex3f(0, 1, 0); glVertex3f(0, 0, 1); glVertex3f(-1, 0, 0);
    glNormal3f(-1, 1, -1); glVertex3f(0, 1, 0); glVertex3f(-1, 0, 0); glVertex3f(0, 0, -1);
    glNormal3f(1, 1, -1); glVertex3f(0, 1, 0); glVertex3f(0, 0, -1); glVertex3f(1, 0, 0);
    glNormal3f(1, -1, 1); glVertex3f(0, -1, 0); glVertex3f(0, 0, 1); glVertex3f(1, 0, 0);
    glNormal3f(-1, -1, 1); glVertex3f(0, -1, 0); glVertex3f(-1, 0, 0); glVertex3f(0, 0, 1);
    glNormal3f(-1, -1, -1); glVertex3f(0, -1, 0); glVertex3f(0, 0, -1); glVertex3f(-1, 0, 0);
    glNormal3f(1, -1, -1); glVertex3f(0, -1, 0); glVertex3f(1, 0, 0); glVertex3f(0, 0, -1);
    glEnd();
}

void glutWireOctahedron(void) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glutSolidOctahedron();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

static void solid_icosahedron_imm(void) {
    double t = (1.0 + sqrt(5.0)) / 2.0;
    GLfloat v[12][3] = {
        {-1, (GLfloat)t, 0}, {1, (GLfloat)t, 0}, {-1, -(GLfloat)t, 0}, {1, -(GLfloat)t, 0},
        {0, -1, (GLfloat)t}, {0, 1, (GLfloat)t}, {0, -1, -(GLfloat)t}, {0, 1, -(GLfloat)t},
        {(GLfloat)t, 0, -1}, {(GLfloat)t, 0, 1}, {-(GLfloat)t, 0, -1}, {-(GLfloat)t, 0, 1}
    };
    static const GLint f[20][3] = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}
    };
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < 20; i++) {
        float nx = (float)((v[f[i][0]][0] + v[f[i][1]][0] + v[f[i][2]][0]) / 3.0);
        float ny = (float)((v[f[i][0]][1] + v[f[i][1]][1] + v[f[i][2]][1]) / 3.0);
        float nz = (float)((v[f[i][0]][2] + v[f[i][1]][2] + v[f[i][2]][2]) / 3.0);
        glNormal3f(nx, ny, nz);
        glVertex3fv(v[f[i][0]]);
        glVertex3fv(v[f[i][1]]);
        glVertex3fv(v[f[i][2]]);
    }
    glEnd();
}

void glutWireIcosahedron(void) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glutSolidIcosahedron();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void glutSolidDodecahedron(void) { glutSolidCube(1.0); }
void glutWireDodecahedron(void) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glutSolidDodecahedron();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

/* --- Shared geometry (2026.10.31.41) ------------------------------------
 * The viewer draws hundreds of glut shapes a frame, each tessellated again
 * in immediate mode (and a GLU quadric allocated for every sphere, cone and
 * cylinder). A shape with the same parameters is the same vertex stream
 * every time, so it is compiled once into a display list and replayed.
 *
 * The key is the exact parameters (shape, two doubles, two ints): a list
 * replays exactly the calls the immediate path makes, so the picture is
 * the same. Shapes are not drawn as a scaled unit shape on purpose: the
 * hull shader reads the object-space vertex (gl_Vertex) for its panels and
 * lighting, which a scale in the modelview would change.
 *
 * A key is compiled on its SECOND use: a parameter animated frame by frame
 * (a pulsing radius) never repeats exactly and stays in immediate mode, as
 * before, instead of compiling a list per frame. The candidates seen once
 * live in a small ring; compiled lists in a fixed table (when it is full,
 * further shapes are drawn immediately). SPACEGL_GL_SHAPE_CACHE=0 turns the
 * cache off (immediate mode for every shape, for comparisons). */

enum { SHAPE_SPHERE = 1,
       SHAPE_CUBE,
       SHAPE_CONE,
       SHAPE_CYLINDER,
       SHAPE_TORUS,
       SHAPE_OCTA,
       SHAPE_ICOSA };

typedef struct {
    int shape;
    int i, j;
    double a, b;
} ShapeKey;

typedef struct {
    ShapeKey key;
    GLuint list;   /* 0: empty slot */
} ShapeList;

#define SHAPE_LISTS_MAX 1024   /* power of two; at most 3/4 used */
#define SHAPE_SEEN_MAX  256     /* ring of the keys seen once */

static ShapeList g_shape_lists[SHAPE_LISTS_MAX];
static int g_shape_lists_used = 0;
static ShapeKey g_shape_seen[SHAPE_SEEN_MAX];
static int g_shape_seen_next = 0;
static int g_shape_cache_mode = -1;   /* -1: not read yet, 0 off, 1 on */
static GlutShapeStats g_shape_stats;

static bool shape_key_eq(const ShapeKey *x, const ShapeKey *y) {
    /* bitwise on the doubles: the exact parameters, as the key promises */
    return x->shape == y->shape && x->i == y->i && x->j == y->j &&
           memcmp(&x->a, &y->a, sizeof(double)) == 0 && memcmp(&x->b, &y->b, sizeof(double)) == 0;
}

static uint32_t shape_key_hash(const ShapeKey *k) {
    uint32_t h = 2166136261u;
    const unsigned char *p;
    p = (const unsigned char *)&k->shape;
    for (size_t n = 0; n < sizeof(k->shape); n++) h = (h ^ p[n]) * 16777619u;
    p = (const unsigned char *)&k->i;
    for (size_t n = 0; n < sizeof(k->i); n++) h = (h ^ p[n]) * 16777619u;
    p = (const unsigned char *)&k->j;
    for (size_t n = 0; n < sizeof(k->j); n++) h = (h ^ p[n]) * 16777619u;
    p = (const unsigned char *)&k->a;
    for (size_t n = 0; n < sizeof(k->a); n++) h = (h ^ p[n]) * 16777619u;
    p = (const unsigned char *)&k->b;
    for (size_t n = 0; n < sizeof(k->b); n++) h = (h ^ p[n]) * 16777619u;
    return h;
}

static void shape_draw_imm(const ShapeKey *k) {
    switch (k->shape) {
        case SHAPE_SPHERE: solid_sphere_imm(k->a, k->i, k->j); break;
        case SHAPE_CUBE: solid_cube_imm(k->a); break;
        case SHAPE_CONE: solid_cone_imm(k->a, k->b, k->i, k->j); break;
        case SHAPE_CYLINDER: solid_cylinder_imm(k->a, k->b, k->i, k->j); break;
        case SHAPE_TORUS: solid_torus_imm(k->a, k->b, k->i, k->j); break;
        case SHAPE_OCTA: solid_octahedron_imm(); break;
        case SHAPE_ICOSA: solid_icosahedron_imm(); break;
        default: break;
    }
}

static void shape_draw(int shape, double a, double b, int i, int j) {
    ShapeKey k;
    memset(&k, 0, sizeof(k));   /* padding too: the key is compared as values, hashed by field */
    k.shape = shape;
    k.a = a;
    k.b = b;
    k.i = i;
    k.j = j;
    g_shape_stats.calls++;

    if (g_shape_cache_mode < 0) {
        const char *env = getenv("SPACEGL_GL_SHAPE_CACHE");
        g_shape_cache_mode = (env && env[0] == '0') ? 0 : 1;
    }
    if (g_shape_cache_mode == 0) {
        g_shape_stats.immediate++;
        shape_draw_imm(&k);
        return;
    }

    uint32_t mask = SHAPE_LISTS_MAX - 1;
    uint32_t slot = shape_key_hash(&k) & mask;
    while (g_shape_lists[slot].list != 0) {
        if (shape_key_eq(&g_shape_lists[slot].key, &k)) {
            g_shape_stats.list_hits++;
            glCallList(g_shape_lists[slot].list);
            return;
        }
        slot = (slot + 1) & mask;
    }

    /* Not compiled. Seen once before? Then compile it now. */
    bool seen = false;
    for (int n = 0; n < SHAPE_SEEN_MAX; n++) {
        if (g_shape_seen[n].shape != 0 && shape_key_eq(&g_shape_seen[n], &k)) {
            seen = true;
            g_shape_seen[n].shape = 0;   /* promoted: free the candidate */
            break;
        }
    }
    if (seen && g_shape_lists_used < SHAPE_LISTS_MAX / 4 * 3) {
        GLuint list = glGenLists(1);
        if (list != 0) {
            glNewList(list, GL_COMPILE);
            shape_draw_imm(&k);
            glEndList();
            g_shape_lists[slot].key = k;
            g_shape_lists[slot].list = list;
            g_shape_lists_used++;
            g_shape_stats.compiled++;
            g_shape_stats.list_hits++;
            glCallList(list);
            return;
        }
    }
    if (!seen) {
        g_shape_seen[g_shape_seen_next] = k;
        g_shape_seen_next = (g_shape_seen_next + 1) % SHAPE_SEEN_MAX;
    }
    g_shape_stats.immediate++;
    shape_draw_imm(&k);
}

void glut_shape_cache_enable(bool on) { g_shape_cache_mode = on ? 1 : 0; }

void glut_shape_stats(GlutShapeStats *out, bool reset) {
    if (out) {
        *out = g_shape_stats;
        out->lists = g_shape_lists_used;
    }
    if (reset) {
        g_shape_stats.calls = 0;
        g_shape_stats.list_hits = 0;
        g_shape_stats.immediate = 0;
        g_shape_stats.compiled = 0;
    }
}

void glutSolidSphere(double radius, int slices, int stacks) { shape_draw(SHAPE_SPHERE, radius, 0.0, slices, stacks); }
void glutSolidCube(double size) { shape_draw(SHAPE_CUBE, size, 0.0, 0, 0); }
void glutSolidCone(double base, double height, int slices, int stacks) {
    shape_draw(SHAPE_CONE, base, height, slices, stacks);
}
void glutSolidCylinder(double radius, double height, int slices, int stacks) {
    shape_draw(SHAPE_CYLINDER, radius, height, slices, stacks);
}
void glutSolidTorus(double innerRadius, double outerRadius, int nsides, int rings) {
    shape_draw(SHAPE_TORUS, innerRadius, outerRadius, nsides, rings);
}
void glutSolidOctahedron(void) { shape_draw(SHAPE_OCTA, 0.0, 0.0, 0, 0); }
void glutSolidIcosahedron(void) { shape_draw(SHAPE_ICOSA, 0.0, 0.0, 0, 0); }

void glutSolidTeapot(double size) { (void)size; }
void glutWireTeapot(double size) { (void)size; }

/* 8x8 Font Data (ASCII 32-126) */
static const unsigned char font8x8[128][8] = {
    [32] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // Space
    [33] = {0x18,0x3c,0x3c,0x18,0x18,0x00,0x18,0x00}, // !
    [34] = {0x6c,0x6c,0x6c,0x00,0x00,0x00,0x00,0x00}, // "
    [35] = {0x6c,0x6c,0xfe,0x6c,0xfe,0x6c,0x6c,0x00}, // #
    [36] = {0x18,0x3e,0x60,0x3c,0x06,0x7c,0x18,0x00}, // $
    [37] = {0x00,0xc6,0xcc,0x18,0x30,0x66,0xc6,0x00}, // %
    [38] = {0x38,0x6c,0x38,0x76,0xdc,0xcc,0x76,0x00}, // &
    [39] = {0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00}, // '
    [40] = {0x0c,0x18,0x30,0x30,0x30,0x18,0x0c,0x00}, // (
    [41] = {0x30,0x18,0x0c,0x0c,0x0c,0x18,0x30,0x00}, // )
    [42] = {0x00,0x66,0x3c,0xff,0x3c,0x66,0x00,0x00}, // *
    [43] = {0x00,0x18,0x18,0x7e,0x18,0x18,0x00,0x00}, // +
    [44] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // ,
    [45] = {0x00,0x00,0x00,0x7e,0x00,0x00,0x00,0x00}, // -
    [46] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // .
    [47] = {0x02,0x06,0x0c,0x18,0x30,0x60,0x40,0x00}, // /
    [48] = {0x3c,0x66,0x6e,0x76,0x66,0x66,0x3c,0x00}, // 0
    [49] = {0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0x00}, // 1
    [50] = {0x3c,0x66,0x06,0x0c,0x18,0x30,0x7e,0x00}, // 2
    [51] = {0x3c,0x66,0x06,0x1c,0x06,0x66,0x3c,0x00}, // 3
    [52] = {0x1c,0x3c,0x6c,0xcc,0xfe,0x0c,0x1e,0x00}, // 4
    [53] = {0x7e,0x60,0x7c,0x06,0x06,0x66,0x3c,0x00}, // 5
    [54] = {0x3c,0x66,0x60,0x7c,0x66,0x66,0x3c,0x00}, // 6
    [55] = {0x7e,0x66,0x06,0x0c,0x18,0x18,0x18,0x00}, // 7
    [56] = {0x3c,0x66,0x66,0x3c,0x66,0x66,0x3c,0x00}, // 8
    [57] = {0x3c,0x66,0x66,0x3e,0x06,0x66,0x3c,0x00}, // 9
    [58] = {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, // :
    [59] = {0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00}, // ;
    [60] = {0x00,0x0c,0x18,0x30,0x18,0x0c,0x00,0x00}, // <
    [61] = {0x00,0x00,0x7e,0x00,0x7e,0x00,0x00,0x00}, // =
    [62] = {0x00,0x30,0x18,0x0c,0x18,0x30,0x00,0x00}, // >
    [63] = {0x3c,0x66,0x06,0x0c,0x18,0x00,0x18,0x00}, // ?
    [64] = {0x3c,0x66,0x6e,0x6e,0x60,0x62,0x3c,0x00}, // @
    [65] = {0x18,0x3c,0x66,0x66,0x7e,0x66,0x66,0x00}, // A
    [66] = {0x7c,0x66,0x66,0x7c,0x66,0x66,0x7c,0x00}, // B
    [67] = {0x3c,0x66,0x60,0x60,0x60,0x66,0x3c,0x00}, // C
    [68] = {0x78,0x6c,0x66,0x66,0x66,0x6c,0x78,0x00}, // D
    [69] = {0x7e,0x60,0x60,0x78,0x60,0x60,0x7e,0x00}, // E
    [70] = {0x7e,0x60,0x60,0x78,0x60,0x60,0x60,0x00}, // F
    [71] = {0x3c,0x66,0x60,0x6e,0x66,0x66,0x3c,0x00}, // G
    [72] = {0x66,0x66,0x66,0x7e,0x66,0x66,0x66,0x00}, // H
    [73] = {0x3c,0x18,0x18,0x18,0x18,0x18,0x3c,0x00}, // I
    [74] = {0x1e,0x0c,0x0c,0x0c,0x0c,0x6c,0x38,0x00}, // J
    [75] = {0x66,0x6c,0x78,0x70,0x78,0x6c,0x66,0x00}, // K
    [76] = {0x60,0x60,0x60,0x60,0x60,0x60,0x7e,0x00}, // L
    [77] = {0x63,0x77,0x7f,0x6b,0x63,0x63,0x63,0x00}, // M
    [78] = {0x66,0x66,0x76,0x7e,0x6e,0x66,0x66,0x00}, // N
    [79] = {0x3c,0x66,0x66,0x66,0x66,0x66,0x3c,0x00}, // O
    [80] = {0x7c,0x66,0x66,0x7c,0x60,0x60,0x60,0x00}, // P
    [81] = {0x3c,0x66,0x66,0x66,0x66,0x6c,0x36,0x00}, // Q
    [82] = {0x7c,0x66,0x66,0x7c,0x6c,0x66,0x66,0x00}, // R
    [83] = {0x3c,0x66,0x60,0x3c,0x06,0x66,0x3c,0x00}, // S
    [84] = {0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // T
    [85] = {0x66,0x66,0x66,0x66,0x66,0x66,0x3c,0x00}, // U
    [86] = {0x66,0x66,0x66,0x66,0x66,0x3c,0x18,0x00}, // V
    [87] = {0x63,0x63,0x63,0x6b,0x7f,0x77,0x63,0x00}, // W
    [88] = {0x66,0x66,0x3c,0x18,0x3c,0x66,0x66,0x00}, // X
    [89] = {0x66,0x66,0x66,0x3c,0x18,0x18,0x18,0x00}, // Y
    [90] = {0x7e,0x06,0x0c,0x18,0x30,0x60,0x7e,0x00}, // Z
    [91] = {0x1c,0x10,0x10,0x10,0x10,0x10,0x1c,0x00}, // [
    [92] = {0x40,0x60,0x30,0x18,0x0c,0x06,0x02,0x00}, // Backslash
    [93] = {0x1c,0x04,0x04,0x04,0x04,0x04,0x1c,0x00}, // ]
    [94] = {0x10,0x38,0x6c,0x00,0x00,0x00,0x00,0x00}, // ^
    [95] = {0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x00}, // _
    [96] = {0x30,0x30,0x18,0x00,0x00,0x00,0x00,0x00}, // `
    [97] = {0x00,0x00,0x3c,0x06,0x3e,0x66,0x3e,0x00}, // a
    [98] = {0x60,0x60,0x7c,0x66,0x66,0x66,0x7c,0x00}, // b
    [99] = {0x00,0x00,0x3c,0x60,0x60,0x66,0x3c,0x00}, // c
    [100] = {0x06,0x06,0x3e,0x66,0x66,0x66,0x3e,0x00}, // d
    [101] = {0x00,0x00,0x3c,0x66,0x7e,0x60,0x3c,0x00}, // e
    [102] = {0x1c,0x30,0x78,0x30,0x30,0x30,0x30,0x00}, // f
    [103] = {0x00,0x00,0x3e,0x66,0x66,0x3e,0x06,0x3c}, // g
    [104] = {0x60,0x60,0x7c,0x66,0x66,0x66,0x66,0x00}, // h
    [105] = {0x18,0x00,0x38,0x18,0x18,0x18,0x3c,0x00}, // i
    [106] = {0x06,0x00,0x06,0x06,0x06,0x66,0x3c,0x00}, // j
    [107] = {0x60,0x66,0x6c,0x78,0x70,0x6c,0x66,0x00}, // k
    [108] = {0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0x00}, // l
    [109] = {0x00,0x00,0x66,0x7f,0x7f,0x6b,0x63,0x00}, // m
    [110] = {0x00,0x00,0x7c,0x66,0x66,0x66,0x66,0x00}, // n
    [111] = {0x00,0x00,0x3c,0x66,0x66,0x66,0x3c,0x00}, // o
    [112] = {0x00,0x00,0x7c,0x66,0x66,0x7c,0x60,0x60}, // p
    [113] = {0x00,0x00,0x3e,0x66,0x66,0x3e,0x06,0x06}, // q
    [114] = {0x00,0x00,0x7c,0x66,0x60,0x60,0x60,0x00}, // r
    [115] = {0x00,0x00,0x3e,0x60,0x3c,0x06,0x7c,0x00}, // s
    [116] = {0x30,0x30,0x78,0x30,0x30,0x30,0x1c,0x00}, // t
    [117] = {0x00,0x00,0x66,0x66,0x66,0x66,0x3e,0x00}, // u
    [118] = {0x00,0x00,0x66,0x66,0x66,0x3c,0x18,0x00}, // v
    [119] = {0x00,0x00,0x63,0x6b,0x7f,0x7f,0x36,0x00}, // w
    [120] = {0x00,0x00,0x66,0x3c,0x18,0x3c,0x66,0x00}, // x
    [121] = {0x00,0x00,0x66,0x66,0x66,0x3e,0x06,0x3c}, // y
    [122] = {0x00,0x00,0x7e,0x0c,0x18,0x30,0x7e,0x00}, // z
    [123] = {0x0c,0x18,0x18,0x30,0x18,0x18,0x0c,0x00}, // {
    [124] = {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // |
    [125] = {0x30,0x18,0x18,0x0c,0x18,0x18,0x30,0x00}, // }
    [126] = {0x00,0x00,0x3b,0x6e,0x00,0x00,0x00,0x00}, // ~
};

#define GLYPH_MAX_PX_LIMIT 64
/* Glyph edge in window pixels (default: the native 8 px font size). */
static int g_bitmap_glyph_px = 8;

void glutBitmapTextSize(int px) {
    if (px < 1) px = 1;
    if (px > GLYPH_MAX_PX_LIMIT) px = GLYPH_MAX_PX_LIMIT;
    if (px != g_bitmap_glyph_px) glutBitmapFlush();   /* the queued glyphs keep their size */
    g_bitmap_glyph_px = px;
}

/* Bitmap text as textured quads (2026.11.01.17). glutBitmapCharacter()
 * drew each character with glBitmap: the HUD of a busy quadrant (6 000
 * characters a frame) cost 12.4 of the 18.4 ms of a frame on an Arc A770
 * at 1920x1080, 45 ms on llvmpipe, and at any window height other than
 * 1080 every text was blank (the resampled glyph was one BYTE per pixel
 * where glBitmap reads one BIT). Now a string (glutBitmapString, or one
 * character) reads the raster position and colour glBitmap would use,
 * queues a quad a glyph at those pixels and that depth, and moves the
 * raster position on as glBitmap did; the queue is drawn in one call
 * from a glyph atlas texture (one texel per pixel,
 * nearest filtering: the same pixels as glBitmap) when glutBitmapFlush()
 * is called, when the glyph size or the depth test or blending state (as
 * they were when the character was queued) changes, or when it is full;
 * the queue is drawn under that depth and blend state. The caller
 * flushes before it changes framebuffer and before the frame is shown. */
#define GLYPH_ATLAS_COLS  16
#define GLYPH_ATLAS_ROWS  6   /* 95 glyphs, 32..126 */
#define TEXT_BATCH_GLYPHS 4096

static GLuint g_glyph_tex = 0;
static int g_glyph_tex_px = 0;
static float g_tb_xyz[TEXT_BATCH_GLYPHS * 4 * 3];
static float g_tb_uv[TEXT_BATCH_GLYPHS * 4 * 2];
static float g_tb_rgba[TEXT_BATCH_GLYPHS * 4 * 4];
static int g_tb_n = 0;      /* glyphs queued */
static int g_tb_px = 0;     /* their size */
static GLboolean g_tb_depth = GL_FALSE, g_tb_blend = GL_FALSE;   /* the state they were queued under */

static void glyph_atlas_build(int px) {
    int tw = GLYPH_ATLAS_COLS * px, th = GLYPH_ATLAS_ROWS * px;
    unsigned char *img = calloc((size_t)tw * (size_t)th, 1);
    if (!img) return;
    for (int c = 32; c <= 126; c++) {
        int i = c - 32, cx = (i % GLYPH_ATLAS_COLS) * px, cy = (i / GLYPH_ATLAS_COLS) * px;
        const unsigned char *src = font8x8[c];
        for (int y = 0; y < px; y++) {   /* texture row 0 = the glyph's bottom */
            int src_y = 7 - (y * 8) / px;
            for (int x = 0; x < px; x++)
                if ((src[src_y] >> (7 - (x * 8) / px)) & 1) img[(size_t)(cy + y) * (size_t)tw + (size_t)(cx + x)] = 255;
        }
    }
    if (!g_glyph_tex) glGenTextures(1, &g_glyph_tex);
    GLint prev = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    glBindTexture(GL_TEXTURE_2D, g_glyph_tex);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, tw, th, 0, GL_ALPHA, GL_UNSIGNED_BYTE, img);
    glPopClientAttrib();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
    free(img);
    g_glyph_tex_px = px;
}

void glutBitmapFlush(void) {
    if (g_tb_n == 0) return;
    int px = g_tb_px;
    GLint prog = 0, active = GL_TEXTURE0, abuf = 0, vp[4] = { 0, 0, 1, 1 };
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &abuf);
    glGetIntegerv(GL_VIEWPORT, vp);
    if (prog) glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    if (abuf) glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (px != g_glyph_tex_px || !g_glyph_tex) glyph_atlas_build(px);

    glPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_TRANSFORM_BIT |
                 GL_POLYGON_BIT | GL_LIGHTING_BIT | GL_POINT_BIT);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    /* window coordinates of the current viewport, window depth as z */
    glOrtho(vp[0], vp[0] + vp[2], vp[1], vp[1] + vp[3], 0.0, -1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_POINT_SPRITE);   /* with GL_COORD_REPLACE Mesa replaced the glyphs' coordinates: blank text */
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_1D);
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    if (g_tb_depth) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    if (g_tb_blend) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);
    /* only unit 0 textures the glyphs: the viewer leaves texturing state
     * on other units (the bloom combine leaves GL_TEXTURE1 active), which
     * would modulate the text by whatever they hold. glPopAttrib
     * (GL_ENABLE_BIT) restores the enables of every unit. */
    static GLint units = 0;
    if (units == 0) {
        glGetIntegerv(GL_MAX_TEXTURE_UNITS, &units);
        if (units < 1) units = 1;
        if (units > 32) units = 32;
    }
    for (GLint u = 1; u < units; u++) {
        glActiveTexture((GLenum)(GL_TEXTURE0 + u));
        glDisable(GL_TEXTURE_1D);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_TEXTURE_3D);
        glDisable(GL_TEXTURE_CUBE_MAP);
    }
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_TEXTURE_3D);
    glDisable(GL_TEXTURE_CUBE_MAP);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_glyph_tex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    /* the glyph's set pixels only, as glBitmap draws them */
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.0f);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, g_tb_xyz);
    glTexCoordPointer(2, GL_FLOAT, 0, g_tb_uv);
    glColorPointer(4, GL_FLOAT, 0, g_tb_rgba);
    glDrawArrays(GL_QUADS, 0, g_tb_n * 4);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();   /* the matrix mode too (GL_TRANSFORM_BIT) */

    if (abuf) glBindBuffer(GL_ARRAY_BUFFER, (GLuint)abuf);
    glActiveTexture((GLenum)active);
    if (prog) glUseProgram((GLuint)prog);
    g_tb_n = 0;
}

/* Queue the printable characters of text[0..n) at the current raster
 * position: the GL state is read once for the whole run (glGet can stall a
 * threaded driver, such as Mesa's glthread), and the raster position moves
 * on once by the width of the characters drawn, as one glBitmap per
 * character did. Characters outside 32..126 draw nothing and do not move,
 * as before. */
static void text_queue(const unsigned char *text, size_t n) {
    size_t drawn = 0;
    for (size_t k = 0; k < n; k++) drawn += text[k] >= 32 && text[k] <= 126;
    if (drawn == 0) return;
    GLint valid = GL_FALSE;
    glGetIntegerv(GL_CURRENT_RASTER_POSITION_VALID, &valid);
    if (!valid) return;   /* glBitmap draws nothing and does not move */
    int px = g_bitmap_glyph_px;
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
    if (g_tb_n > 0 && (px != g_tb_px || depth != g_tb_depth || blend != g_tb_blend)) glutBitmapFlush();
    g_tb_px = px;
    g_tb_depth = depth;
    g_tb_blend = blend;

    GLfloat pos[4], col[4];
    glGetFloatv(GL_CURRENT_RASTER_POSITION, pos);
    glGetFloatv(GL_CURRENT_RASTER_COLOR, col);
    /* glBitmap puts the lower-left corner at floor(raster position); the
     * advance is a whole number of pixels, so each next character starts
     * px pixels further on */
    float x0 = floorf(pos[0]), y0 = floorf(pos[1]), z = pos[2];
    float y1 = y0 + (float)px;
    for (size_t k = 0; k < n; k++) {
        int ch = text[k];
        if (ch < 32 || ch > 126) continue;
        if (g_tb_n == TEXT_BATCH_GLYPHS) glutBitmapFlush();   /* keeps px, depth, blend */
        float x1 = x0 + (float)px;
        int i = ch - 32;
        float u0 = (float)(i % GLYPH_ATLAS_COLS) / GLYPH_ATLAS_COLS, u1 = u0 + 1.0f / GLYPH_ATLAS_COLS;
        float v0 = (float)(i / GLYPH_ATLAS_COLS) / GLYPH_ATLAS_ROWS, v1 = v0 + 1.0f / GLYPH_ATLAS_ROWS;
        float *p = &g_tb_xyz[g_tb_n * 12], *t = &g_tb_uv[g_tb_n * 8], *c = &g_tb_rgba[g_tb_n * 16];
        const float xs[4] = { x0, x1, x1, x0 }, ys[4] = { y0, y0, y1, y1 };
        const float us[4] = { u0, u1, u1, u0 }, vs[4] = { v0, v0, v1, v1 };
        for (int q = 0; q < 4; q++) {
            p[q * 3] = xs[q];
            p[q * 3 + 1] = ys[q];
            p[q * 3 + 2] = z;
            t[q * 2] = us[q];
            t[q * 2 + 1] = vs[q];
            memcpy(&c[q * 4], col, sizeof(GLfloat) * 4);
        }
        g_tb_n++;
        x0 = x1;
    }
    /* the raster position moves on, as before */
    glBitmap(0, 0, 0, 0, (GLfloat)((size_t)px * drawn), 0, NULL);
}

void glutBitmapCharacter(void *font, int character) {
    (void)font;
    unsigned char ch = (unsigned char)character;
    if (character != (int)ch) return;
    text_queue(&ch, 1);
}

void glutBitmapString(void *font, const unsigned char *string) {
    (void)font;
    if (string) text_queue(string, strlen((const char *)string));
}
