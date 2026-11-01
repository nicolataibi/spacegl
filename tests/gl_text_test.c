/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * gl_text_test — the bitmap text of the OpenGL viewer (src/glut_compat.c,
 * 2026.11.01.17): glutBitmapCharacter() queues textured quads that
 * glutBitmapFlush() draws.
 *  1. At the native 8 px size every printable character is drawn in the
 *     raster colour, at the raster position, with exactly the pixels of the
 *     font (checked against the font rows of 'A', 'H' and 'I'), and the
 *     raster position moves on by the glyph width, as glBitmap did. (At
 *     1920x1080 the viewer's frame is pixel-identical to the glBitmap one
 *     except where a label now covers a health bar: checked by hand.)
 *  2. At other sizes (5, 12, 16 px) every set pixel of the glyph,
 *     resampled nearest-neighbour, is drawn and nothing else: before, any
 *     size but 8 drew nothing (one byte per pixel where glBitmap reads one
 *     bit), so every text of the viewer vanished at any window height but
 *     1080.
 *  3. Depth: a character queued with the depth test on is hidden behind
 *     nearer geometry; one queued with it off is drawn over it.
 *  4. An invalid raster position draws nothing and does not move.
 *  Run under the viewer's state: GL_POINT_SPRITE enabled with
 *  GL_COORD_REPLACE on unit 0 (Mesa then replaces the texture coordinates
 *  of every primitive, and the glyphs came out blank), texturing left on
 *  another unit.
 *  5. glutBitmapString() gives the pixels and the raster position of the
 *     same characters drawn one by one (non-printable bytes skipped
 *     without moving), also for a string longer than the queue.
 *  6. The time of 6 000 characters (a busy quadrant's HUD) is printed.
 * Needs an OpenGL context (a display, or xvfb-run): exit 77 (SKIP) without.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GLFW/glfw3.h>
#include "glut_compat.h"

#define W 256
#define H 128

#define TEST_CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "TEST FAILED: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

static unsigned char g_px[W * H * 4];

static void grab(void) {
    glFinish();
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, g_px);
}

static bool lit(int x, int y) {
    const unsigned char *p = &g_px[((size_t)y * W + (size_t)x) * 4];
    return p[0] > 128 || p[1] > 128 || p[2] > 128;
}

static void clear(void) {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void ortho(void) {
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, W, 0, H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/* the glyph of c at size px, as the viewer draws it (row 0 = bottom) */
static bool glyph_bit(const unsigned char rows[8], int px, int x, int y) {
    int src_y = 7 - (y * 8) / px;
    return (rows[src_y] >> (7 - (x * 8) / px)) & 1;
}

/* the 8x8 rows of a character, read back from an 8 px glBitmap reference */
static void reference_rows(int c, unsigned char rows[8]) {
    memset(rows, 0, 8);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            if (lit(10 + (c - 32) % 24 * 9 + x, 10 + (c - 32) / 24 * 9 + y)) rows[7 - y] |= (unsigned char)(0x80u >> x);
}

int main(void) {
    if (!glfwInit()) {
        printf("SKIP: gl_text: no display for GLFW\n");
        return 77;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    GLFWwindow *win = glfwCreateWindow(W, H, "gl_text_test", NULL, NULL);
    if (!win) {
        glfwTerminate();
        printf("SKIP: gl_text: no OpenGL context\n");
        return 77;
    }
    glfwMakeContextCurrent(win);
    TEST_CHECK(sg_gl_load());
    printf("OpenGL: %s / %s\n", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    ortho();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    /* the state the viewer leaves (spacegl_3dview.c main and bloom pass) */
    glEnable(GL_POINT_SPRITE);
    glTexEnvi(GL_POINT_SPRITE, GL_COORD_REPLACE, GL_TRUE);
    GLuint other_tex = 0;
    glGenTextures(1, &other_tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, other_tex);
    const unsigned char black[4] = { 0, 0, 0, 0 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glEnable(GL_TEXTURE_2D);   /* would modulate the text to black */
    glActiveTexture(GL_TEXTURE0);

    /* 1. 8 px: every printable character, against the glyph read back as
     * the queue draws it, then the same characters pixel by pixel */
    clear();
    glColor3f(1.0f, 0.5f, 0.0f);
    glutBitmapTextSize(8);
    for (int c = 32; c <= 126; c++) {
        glRasterPos2i(10 + (c - 32) % 24 * 9, 10 + (c - 32) / 24 * 9);
        glutBitmapCharacter(NULL, c);
    }
    glutBitmapFlush();
    grab();
    int set = 0;
    for (size_t k = 0; k < sizeof(g_px); k += 4)
        if (g_px[k] > 128) {
            set++;
            TEST_CHECK(g_px[k] == 255 && g_px[k + 1] >= 126 && g_px[k + 1] <= 129 && g_px[k + 2] == 0);   /* the raster colour */
        }
    TEST_CHECK(set > 95 * 10);   /* the glyphs are drawn */
    unsigned char rows['~' + 1][8];
    for (int c = 32; c <= 126; c++) reference_rows(c, rows[c]);
    static const unsigned char font_A[8] = { 0x18, 0x3c, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x00 };
    static const unsigned char font_H[8] = { 0x66, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x66, 0x00 };
    static const unsigned char font_I[8] = { 0x3c, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00 };
    TEST_CHECK(memcmp(rows['A'], font_A, 8) == 0 && memcmp(rows['H'], font_H, 8) == 0 &&
               memcmp(rows['I'], font_I, 8) == 0);   /* the font's own pixels */
    printf("  [1] 8 px: all 95 characters drawn in the raster colour (%d pixels): OK\n", set);

    /* the raster position moves on by the glyph width */
    clear();
    glRasterPos2i(20, 40);
    glutBitmapCharacter(NULL, 'H');
    glutBitmapCharacter(NULL, 'I');
    glutBitmapFlush();
    GLfloat rp[4];
    glGetFloatv(GL_CURRENT_RASTER_POSITION, rp);
    TEST_CHECK(fabsf(rp[0] - 36.0f) < 0.01f && fabsf(rp[1] - 40.0f) < 0.01f);
    grab();
    for (int c = 0; c < 2; c++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                TEST_CHECK(lit(20 + c * 8 + x, 40 + y) == glyph_bit(rows[c ? 'I' : 'H'], 8, x, y));
    printf("  [1] raster position moves 8 px a character, glyph pixels exact: OK\n");

    /* 2. other sizes */
    const int sizes[] = { 5, 12, 16 };
    for (int s = 0; s < 3; s++) {
        int px = sizes[s];
        clear();
        glutBitmapTextSize(px);
        glRasterPos2i(30, 50);
        const char *word = "SpaceGL";
        for (const char *q = word; *q; q++) glutBitmapCharacter(NULL, *q);
        glutBitmapFlush();
        grab();
        int bad = 0, on = 0;
        for (int k = 0; word[k]; k++)
            for (int y = 0; y < px; y++)
                for (int x = 0; x < px; x++) {
                    bool want = glyph_bit(rows[(unsigned char)word[k]], px, x, y);
                    if (lit(30 + k * px + x, 50 + y) != want) bad++;
                    on += want;
                }
        TEST_CHECK(on > 0 && bad == 0);
        printf("  [2] %d px: \"%s\" drawn exactly (%d pixels): OK\n", px, word, on);
    }
    glutBitmapTextSize(8);

    /* 3. depth: a near quad at z = -0.5 (window depth 0.75... in front of
     * the text's 0.5 with this ortho), text behind it with the test on */
    clear();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glColor3f(0.0f, 0.0f, 1.0f);
    glBegin(GL_QUADS);   /* window depth 0.25: nearer than the text */
    glVertex3f(0, 0, 0.5f);
    glVertex3f(W, 0, 0.5f);
    glVertex3f(W, H / 2, 0.5f);
    glVertex3f(0, H / 2, 0.5f);
    glEnd();
    glColor3f(1.0f, 1.0f, 1.0f);
    glRasterPos3f(40, 20, 0.0f);   /* window depth 0.5, behind the quad */
    glutBitmapCharacter(NULL, 'W');
    glDisable(GL_DEPTH_TEST);   /* queued under another state: a separate draw */
    glRasterPos3f(40, 80, 0.0f);
    glutBitmapCharacter(NULL, 'W');
    glEnable(GL_DEPTH_TEST);
    glRasterPos3f(60, 20, 0.0f);
    glDisable(GL_DEPTH_TEST);
    glutBitmapCharacter(NULL, 'W');   /* depth test off when queued: over the quad */
    glutBitmapFlush();
    grab();
    int hidden = 0, shown = 0, over = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            const unsigned char *p = &g_px[((size_t)(20 + y) * W + (size_t)(40 + x)) * 4];
            if (p[0] > 128) hidden++;
            if (lit(40 + x, 80 + y)) shown++;
            const unsigned char *q = &g_px[((size_t)(20 + y) * W + (size_t)(60 + x)) * 4];
            if (q[0] > 128) over++;
        }
    TEST_CHECK(hidden == 0 && shown > 0 && over == shown);
    printf("  [3] depth test as when queued: hidden behind nearer geometry, or drawn over it: OK\n");

    /* 4. invalid raster position */
    clear();
    glRasterPos3f(40, 40, 5.0f);   /* outside the near/far planes: invalid */
    GLint valid = GL_TRUE;
    glGetIntegerv(GL_CURRENT_RASTER_POSITION_VALID, &valid);
    TEST_CHECK(!valid);
    glutBitmapCharacter(NULL, 'X');
    glutBitmapFlush();
    grab();
    int any = 0;
    for (size_t k = 0; k < sizeof(g_px); k += 4) any += g_px[k] > 128;
    TEST_CHECK(any == 0);
    printf("  [4] an invalid raster position draws nothing: OK\n");

    /* 5. glutBitmapString: the same pixels and raster position as the
     * characters one by one, non-printable bytes skipped without moving;
     * a string longer than the queue */
    static unsigned char ref_px[W * H * 4];
    const char *line = "Hull: 100% [\x01\xc3\xa9] ~{ok}";
    clear();
    glColor3f(0.2f, 1.0f, 0.6f);
    glRasterPos2f(12.6f, 70.3f);
    for (const char *q = line; *q; q++) glutBitmapCharacter(NULL, *q);
    glutBitmapFlush();
    GLfloat rp_char[4];
    glGetFloatv(GL_CURRENT_RASTER_POSITION, rp_char);
    grab();
    memcpy(ref_px, g_px, sizeof(g_px));
    clear();
    glRasterPos2f(12.6f, 70.3f);
    glutBitmapString(NULL, (const unsigned char *)line);
    glutBitmapFlush();
    GLfloat rp_str[4];
    glGetFloatv(GL_CURRENT_RASTER_POSITION, rp_str);
    grab();
    TEST_CHECK(memcmp(ref_px, g_px, sizeof(g_px)) == 0);
    TEST_CHECK(rp_char[0] == rp_str[0] && rp_char[1] == rp_str[1]);
    TEST_CHECK(fabsf(rp_str[0] - (12.6f + 8.0f * 19.0f)) < 0.01f);   /* 19 printable characters */
    static unsigned char longs[5000];
    memset(longs, 'M', sizeof(longs) - 1);
    longs[sizeof(longs) - 1] = 0;
    glRasterPos2i(0, 100);
    glutBitmapString(NULL, longs);   /* 4999 > 4096 queued: flushed on the way */
    glutBitmapFlush();
    glGetFloatv(GL_CURRENT_RASTER_POSITION, rp_str);
    TEST_CHECK(fabsf(rp_str[0] - 8.0f * 4999.0f) < 0.5f);
    printf("  [5] glutBitmapString: the pixels and raster position of the characters one by one: OK\n");

    /* 6. 6 000 characters, as the HUD of a busy quadrant: 240 strings */
    clear();
    char word[26];
    for (int k = 0; k < 25; k++) word[k] = (char)('A' + k);
    word[25] = 0;
    double t0 = glfwGetTime();
    for (int r = 0; r < 4; r++) {
        for (int i = 0; i < 240; i++) {
            glRasterPos2i((i * 37) % 200, (i * 13) % 120);
            glutBitmapString(NULL, (const unsigned char *)word);
        }
        glutBitmapFlush();
        glFinish();
    }
    printf("  [6] 6000 characters in 240 strings: %.2f ms a frame\n", (glfwGetTime() - t0) * 1000.0 / 4);

    glfwDestroyWindow(win);
    glfwTerminate();
    printf("PASS: gl_text\n");
    return 0;
}
