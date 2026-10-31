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

#include "gl_loader.h"
#include "glut_compat.h"
#include <math.h>
#include <stdlib.h>

#include "../include/spacegl_extras_glfw.h"

void drawBokGlobule(double x, double y, double z) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_LIGHTING);
    glColor3f(0.1f, 0.1f, 0.15f);
    glutSolidSphere(0.4, 24, 24);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.2f, 0.2f, 0.25, 0.5);
    glutSolidSphere(0.6, 24, 24);
    glDisable(GL_BLEND);
    glDisable(GL_LIGHTING);
    glPopMatrix();
}

void drawInterstellarBubble(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    double alpha = 0.3 + sin(pulse * 2.0) * 0.1;
    glColor4f(0.5, 0.8f, 1.0, (GLfloat)alpha);
    glutSolidSphere(0.5 + sin(pulse * 5.0) * 0.05, 16, 16);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawClumpCore(double x, double y, double z) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_LIGHTING);
    
    /* Core: Dense, rocky appearance */
    glColor3f(0.4f, 0.3f, 0.2f);
    glutSolidSphere(0.3, 16, 16);
    
    /* Shell: Translucent mineral layer */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.5, 0.4f, 0.35f, 0.4f);
    glutSolidSphere(0.5, 16, 16);
    glDisable(GL_BLEND);
    
    /* Highlights: Small sparkling points */
    glDisable(GL_LIGHTING);
    glColor3f(0.8f, 0.7f, 0.6f);
    glBegin(GL_POINTS);
    for(int i=0; i<10; i++) {
        glVertex3f((GLfloat)(((double)rand()/RAND_MAX-0.5)*0.8), 
                   (GLfloat)(((double)rand()/RAND_MAX-0.5)*0.8), 
                   (GLfloat)(((double)rand()/RAND_MAX-0.5)*0.8));
    }
    glEnd();
    glEnable(GL_LIGHTING);
    
    glDisable(GL_LIGHTING);
    glPopMatrix();
}
void drawAccretionDisk(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glRotatef((GLfloat)(pulse * 20.0), 0, 1, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    /* Inner glowing ring */
    glColor4f(1.0, 0.7f, 0.3f, 0.6f);
    glutWireTorus(0.05, 1.2, 8, 64);
    
    /* Outer dust ring */
    glColor4f(0.8f, 0.3f, 0.1f, 0.4f);
    glutWireTorus(0.1, 2.0, 12, 64);
    
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawRelativisticJet(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glRotatef((GLfloat)(pulse * 100.0), 0, 1, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    /* Jet Core */
    glColor4f(0.3f, 0.5, 1.0, 0.6f);
    glPushMatrix();
    glRotatef(90, 1, 0, 0);
    GLUquadric *q = gluNewQuadric();
    gluCylinder(q, 0.2, 0.05, 5.0, 16, 1);
    gluDeleteQuadric(q);
    glPopMatrix();
    
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawShockWave(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    double s = 1.0 + fmod(pulse * 2.0, 5.0);
    glColor4f(1.0, 0.5, 0.2f, (GLfloat)(0.5 * (1.0 - (s-1.0)/5.0)));
    glutSolidSphere(s, 32, 32);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawStellarBowShock(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.4f, 0.6f, 1.0, 0.4f);
    glRotatef(90, 0, 0, 1);
    glScalef(1.5, 1.0, 1.5);
    /* Simplified bow shock as a half-sphere using a clip plane or just a disc */
    glutSolidSphere(2.0, 16, 32); 
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawCosmicVoid(double x, double y, double z) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.05f, 0.0, 0.1f, 0.2f);
    glutSolidSphere(10.0, 32, 32);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawCosmicFilament(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.3f, 0.2f, 0.5, 0.3f);
    glScalef(0.2f, 10.0, 0.2f);
    glutSolidSphere(1.0, 8, 16);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawEventHorizon(double x, double y, double z) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glColor3f(0.0, 0.0, 0.0);
    glutSolidSphere(1.2, 32, 32);
    glPopMatrix();
}

void drawKilonova(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    double s = 1.5 + 0.5 * sin(pulse * 10.0);
    glColor4f(1.0, 0.8f, 0.0, 0.8f);
    glutSolidSphere(s, 32, 32);
    glColor4f(1.0, 0.4f, 0.0, 0.4f);
    glutSolidSphere(s * 2.0, 32, 32);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawGravLens(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(1.0, 1.0, 1.0, 0.2f);
    glutWireTorus(0.1, 2.0, 8, 64);
    glutWireTorus(0.05, 2.5, 8, 64);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawGRB(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.8f, 0.9f, 1.0, 0.8f);
    glPushMatrix();
    glRotatef(90, 1, 0, 0);
    glTranslatef(0, 0, -25.0);
    GLUquadric *q = gluNewQuadric();
    gluCylinder(q, 0.1, 0.1, 50.0, 16, 1);
    gluDeleteQuadric(q);
    glPopMatrix();
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawGravWave(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    double s = 2.0 + sin(pulse * 4.0);
    glColor4f(0.5, 0.5, 1.0, 0.2f);
    glutWireTorus(0.2, s, 4, 32);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawProtoplanetaryDisk(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glRotatef((GLfloat)(pulse * 5.0), 0, 1, 0);
    glScalef(1.0, 0.05f, 1.0);
    glColor3f(0.6f, 0.4f, 0.2f);
    glutSolidTorus(0.5, 3.0, 16, 64);
    glPopMatrix();
}

void drawDebrisDisk(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glRotatef((GLfloat)(pulse * 2.0), 0, 1, 0);
    glScalef(1.0, 0.02f, 1.0);
    glColor3f(0.4f, 0.4f, 0.4f);
    glutWireTorus(0.8, 4.0, 10, 64);
    glPopMatrix();
}

void drawPlanetesimal(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glRotatef((GLfloat)(pulse * 30.0), 1, 1, 0);
    glColor3f(0.5, 0.5, 0.55f);
    glutSolidIcosahedron();
    glPopMatrix();
}

void drawRoguePlanet(double x, double y, double z) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_LIGHTING);
    glColor3f(0.1f, 0.1f, 0.2f);
    glutSolidSphere(1.0, 24, 24);
    glDisable(GL_LIGHTING);
    glPopMatrix();
}

void drawBrownDwarf(double x, double y, double z) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.4f, 0.1f, 0.05f, 1.0);
    glutSolidSphere(1.2, 24, 24);
    glColor4f(0.3f, 0.0, 0.0, 0.5);
    glutSolidSphere(1.5, 24, 24);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawISO(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glRotatef((GLfloat)(pulse * 50.0), 0, 1, 1);
    glScalef(0.2f, 1.0, 0.2f);
    glColor3f(0.3f, 0.3f, 0.3f);
    glutSolidSphere(0.5, 8, 8);
    glPopMatrix();
}

void drawMagReconn(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    double s = 0.5 + 0.5 * sin(pulse * 20.0);
    glColor4f(1.0, 1.0, 1.0, (GLfloat)s);
    glBegin(GL_LINES);
    glVertex3f(-1,0,0); glVertex3f(1,0,0);
    glVertex3f(0,-1,0); glVertex3f(0,1,0);
    glEnd();
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawCurrentSheet(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.0, 0.5, 1.0, 0.3f);
    glScalef(5.0, 0.01f, 5.0);
    glutSolidCube(1.0);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawHeliosphere(double x, double y, double z) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); 
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); /* Additive for better glow */
    glColor4f(0.3f, 0.4f, 1.0, 0.4f); /* Boosted alpha */
    glutWireSphere(15.0, 32, 32); /* Wireframe + Solid for better definition */
    glColor4f(0.2f, 0.3f, 0.8f, 0.15f);
    glutSolidSphere(14.8, 32, 32);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glPopMatrix();
}

void drawTermShock(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.5, 0.7f, 1.0, 0.2f);
    glutWireSphere(10.0, 16, 16);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawMagnetosphere(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.3f, 0.5, 1.0, 0.2f);
    for(int i=0; i<4; i++) {
        glPushMatrix();
        glRotatef((GLfloat)(i * 45), 0, 1, 0);
        glutWireTorus(0.5, 3.0, 8, 32);
        glPopMatrix();
    }
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawCosmicString(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(1.0, 1.0, 1.0, 1.0);
    glBegin(GL_LINES);
    glVertex3f(0,-50,0); glVertex3f(0,50,0);
    glEnd();
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawDomainWall(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.5, 0.0, 0.5, 0.2f);
    glScalef(50.0, 50.0, 0.01f);
    glutSolidCube(1.0);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawDMHalo(double x, double y, double z) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); 
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    /* DM Halo: Deep violet/blue with high visibility alpha */
    glColor4f(0.15f, 0.1f, 0.4f, 0.25); 
    glutSolidSphere(25.0, 48, 48);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glPopMatrix();
}

void drawIGM(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.4f, 0.4f, 0.6f, 0.1f);
    glutSolidSphere(8.0, 16, 16);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawCGM(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.5, 0.3f, 0.7f, 0.15f);
    glutSolidSphere(6.0, 16, 16);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawLymanAlpha(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(1.0, 0.2f, 0.2f, 0.2f);
    glutSolidSphere(10.0, 16, 16);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawCMB(double x, double y, double z) {
    glPushMatrix();
    glTranslatef((GLfloat)x, (GLfloat)y, (GLfloat)z);
    glDisable(GL_CULL_FACE); /* Ensure visibility from inside */
    glEnable(GL_BLEND); 
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    /* CMB: Restoration with Planck-style anisotropies (Warm/Cool layers) */
    /* Inner Layer: Warm (Dark Red/Orange) */
    glColor4f(0.6f, 0.1f, 0.05f, 0.45f);
    glutSolidSphere(15.0, 48, 48);
    
    /* Outer Layer: Cool (Deep Blue) */
    glColor4f(0.05f, 0.2f, 0.7f, 0.35f);
    glutSolidSphere(18.0, 48, 48);
    
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glPopMatrix();
}


