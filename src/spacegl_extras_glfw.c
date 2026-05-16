#include <GL/glew.h>
#include "glut_compat.h"
#include <math.h>
#include <stdlib.h>

#include "../include/spacegl_extras_glfw.h"

void drawBokGlobule(double x, double y, double z) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_LIGHTING);
    glColor3f(0.1, 0.1, 0.15);
    glutSolidSphere(0.4, 24, 24);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.2, 0.2, 0.25, 0.5);
    glutSolidSphere(0.6, 24, 24);
    glDisable(GL_BLEND);
    glDisable(GL_LIGHTING);
    glPopMatrix();
}

void drawInterstellarBubble(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    double alpha = 0.3 + sin(pulse * 2.0) * 0.1;
    glColor4f(0.5, 0.8, 1.0, alpha);
    glutSolidSphere(0.5 + sin(pulse * 5.0) * 0.05, 16, 16);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawClumpCore(double x, double y, double z) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_LIGHTING);
    
    /* Core: Dense, rocky appearance */
    glColor3f(0.4, 0.3, 0.2);
    glutSolidSphere(0.3, 16, 16);
    
    /* Shell: Translucent mineral layer */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.5, 0.4, 0.35, 0.4);
    glutSolidSphere(0.5, 16, 16);
    glDisable(GL_BLEND);
    
    /* Highlights: Small sparkling points */
    glDisable(GL_LIGHTING);
    glColor3f(0.8, 0.7, 0.6);
    glBegin(GL_POINTS);
    for(int i=0; i<10; i++) {
        glVertex3f(((double)rand()/RAND_MAX-0.5)*0.8, 
                   ((double)rand()/RAND_MAX-0.5)*0.8, 
                   ((double)rand()/RAND_MAX-0.5)*0.8);
    }
    glEnd();
    glEnable(GL_LIGHTING);
    
    glDisable(GL_LIGHTING);
    glPopMatrix();
}
void drawAccretionDisk(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(pulse * 20.0, 0, 1, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    /* Inner glowing ring */
    glColor4f(1.0, 0.7, 0.3, 0.6);
    glutWireTorus(0.05, 1.2, 8, 64);
    
    /* Outer dust ring */
    glColor4f(0.8, 0.3, 0.1, 0.4);
    glutWireTorus(0.1, 2.0, 12, 64);
    
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawRelativisticJet(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(pulse * 100.0, 0, 1, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    /* Jet Core */
    glColor4f(0.3, 0.5, 1.0, 0.6);
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
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    double s = 1.0 + fmod(pulse * 2.0, 5.0);
    glColor4f(1.0, 0.5, 0.2, 0.5 * (1.0 - (s-1.0)/5.0));
    glutSolidSphere(s, 32, 32);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawStellarBowShock(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.4, 0.6, 1.0, 0.4);
    glRotatef(90, 0, 0, 1);
    glScalef(1.5, 1.0, 1.5);
    /* Simplified bow shock as a half-sphere using a clip plane or just a disc */
    glutSolidSphere(2.0, 16, 32); 
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawCosmicVoid(double x, double y, double z) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.05, 0.0, 0.1, 0.2);
    glutSolidSphere(10.0, 32, 32);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawCosmicFilament(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.3, 0.2, 0.5, 0.3);
    glScalef(0.2, 10.0, 0.2);
    glutSolidSphere(1.0, 8, 16);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawEventHorizon(double x, double y, double z) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glColor3f(0.0, 0.0, 0.0);
    glutSolidSphere(1.2, 32, 32);
    glPopMatrix();
}

void drawKilonova(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    double s = 1.5 + 0.5 * sin(pulse * 10.0);
    glColor4f(1.0, 0.8, 0.0, 0.8);
    glutSolidSphere(s, 32, 32);
    glColor4f(1.0, 0.4, 0.0, 0.4);
    glutSolidSphere(s * 2.0, 32, 32);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawGravLens(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(1.0, 1.0, 1.0, 0.2);
    glutWireTorus(0.1, 2.0, 8, 64);
    glutWireTorus(0.05, 2.5, 8, 64);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawGRB(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.8, 0.9, 1.0, 0.8);
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
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    double s = 2.0 + sin(pulse * 4.0);
    glColor4f(0.5, 0.5, 1.0, 0.2);
    glutWireTorus(0.2, s, 4, 32);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawProtoplanetaryDisk(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(pulse * 5.0, 0, 1, 0);
    glScalef(1.0, 0.05, 1.0);
    glColor3f(0.6, 0.4, 0.2);
    glutSolidTorus(0.5, 3.0, 16, 64);
    glPopMatrix();
}

void drawDebrisDisk(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(pulse * 2.0, 0, 1, 0);
    glScalef(1.0, 0.02, 1.0);
    glColor3f(0.4, 0.4, 0.4);
    glutWireTorus(0.8, 4.0, 10, 64);
    glPopMatrix();
}

void drawPlanetesimal(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(pulse * 30.0, 1, 1, 0);
    glColor3f(0.5, 0.5, 0.55);
    glutSolidIcosahedron();
    glPopMatrix();
}

void drawRoguePlanet(double x, double y, double z) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_LIGHTING);
    glColor3f(0.1, 0.1, 0.2);
    glutSolidSphere(1.0, 24, 24);
    glDisable(GL_LIGHTING);
    glPopMatrix();
}

void drawBrownDwarf(double x, double y, double z) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.4, 0.1, 0.05, 1.0);
    glutSolidSphere(1.2, 24, 24);
    glColor4f(0.3, 0.0, 0.0, 0.5);
    glutSolidSphere(1.5, 24, 24);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawISO(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(pulse * 50.0, 0, 1, 1);
    glScalef(0.2, 1.0, 0.2);
    glColor3f(0.3, 0.3, 0.3);
    glutSolidSphere(0.5, 8, 8);
    glPopMatrix();
}

void drawMagReconn(double x, double y, double z, double pulse) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    double s = 0.5 + 0.5 * sin(pulse * 20.0);
    glColor4f(1.0, 1.0, 1.0, s);
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
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.0, 0.5, 1.0, 0.3);
    glScalef(5.0, 0.01, 5.0);
    glutSolidCube(1.0);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawHeliosphere(double x, double y, double z) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); 
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); /* Additive for better glow */
    glColor4f(0.3, 0.4, 1.0, 0.4); /* Boosted alpha */
    glutWireSphere(15.0, 32, 32); /* Wireframe + Solid for better definition */
    glColor4f(0.2, 0.3, 0.8, 0.15);
    glutSolidSphere(14.8, 32, 32);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glPopMatrix();
}

void drawTermShock(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.5, 0.7, 1.0, 0.2);
    glutWireSphere(10.0, 16, 16);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawMagnetosphere(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.3, 0.5, 1.0, 0.2);
    for(int i=0; i<4; i++) {
        glPushMatrix();
        glRotatef(i * 45, 0, 1, 0);
        glutWireTorus(0.5, 3.0, 8, 32);
        glPopMatrix();
    }
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawCosmicString(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef(x, y, z);
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
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.5, 0.0, 0.5, 0.2);
    glScalef(50.0, 50.0, 0.01);
    glutSolidCube(1.0);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawDMHalo(double x, double y, double z) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND); 
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    /* DM Halo: Deep violet/blue with high visibility alpha */
    glColor4f(0.15, 0.1, 0.4, 0.25); 
    glutSolidSphere(25.0, 48, 48);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glPopMatrix();
}

void drawIGM(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.4, 0.4, 0.6, 0.1);
    glutSolidSphere(8.0, 16, 16);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawCGM(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.5, 0.3, 0.7, 0.15);
    glutSolidSphere(6.0, 16, 16);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawLymanAlpha(double x, double y, double z, double pulse) {
    (void)pulse;
    glPushMatrix();
    glTranslatef(x, y, z);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(1.0, 0.2, 0.2, 0.2);
    glutSolidSphere(10.0, 16, 16);
    glDisable(GL_BLEND);
    glPopMatrix();
}

void drawCMB(double x, double y, double z) {
    glPushMatrix();
    glTranslatef(x, y, z);
    glDisable(GL_CULL_FACE); /* Ensure visibility from inside */
    glEnable(GL_BLEND); 
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    /* CMB: Restoration with Planck-style anisotropies (Warm/Cool layers) */
    /* Inner Layer: Warm (Dark Red/Orange) */
    glColor4f(0.6, 0.1, 0.05, 0.45);
    glutSolidSphere(15.0, 48, 48);
    
    /* Outer Layer: Cool (Deep Blue) */
    glColor4f(0.05, 0.2, 0.7, 0.35);
    glutSolidSphere(18.0, 48, 48);
    
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glPopMatrix();
}


