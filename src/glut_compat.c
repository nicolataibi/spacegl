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

int glutGet(int state) {
    if (state == GLUT_ELAPSED_TIME) {
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

/* Glyph edge in window pixels (default: the native 8 px font size). */
static int g_bitmap_glyph_px = 8;

void glutBitmapTextSize(int px) {
    if (px < 1) px = 1;
    if (px > 64) px = 64;
    g_bitmap_glyph_px = px;
}

void glutBitmapCharacter(void* font, int character) {
    (void)font;
    if (character < 32 || character > 126) return;
    const unsigned char* bitmap = font8x8[character];

    // Set pixel store mode to avoid noise/corruption due to alignment
    glPushAttrib(GL_PIXEL_MODE_BIT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (g_bitmap_glyph_px == 8) {
        // glBitmap expects the bitmap to be flipped vertically (bottom-to-top)
        unsigned char flipped[8];
        for (int i = 0; i < 8; i++) {
            flipped[i] = bitmap[7 - i];
        }
        // Draw the bitmap and advance the raster position by 8 pixels
        glBitmap(8, 8, 0, 0, 8, 0, flipped);
    } else {
        // glBitmap ignores the current transform and draws in window pixels,
        // so a scaled HUD resamples the 8x8 glyph here (nearest neighbor) to
        // the requested pixel size; the raster advance follows the size.
        unsigned char scaled[64 * 64];
        int px = g_bitmap_glyph_px;
        for (int y = 0; y < px; y++) {
            int src_y = 7 - (y * 8) / px; // destination row 0 = glyph bottom
            for (int x = 0; x < px; x++) {
                scaled[y * px + x] = (bitmap[src_y] >> (7 - (x * 8) / px)) & 1;
            }
        }
        glBitmap(px, px, 0, 0, (GLfloat)px, 0, scaled);
    }

    glPopAttrib();
}
