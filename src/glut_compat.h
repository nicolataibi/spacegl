#ifndef GLUT_COMPAT_H
#define GLUT_COMPAT_H

#include <GL/glew.h>
#include <GL/glu.h>

/* Mock constants for GLUT compatibility */
#define GLUT_BITMAP_HELVETICA_10 (void*)10
#define GLUT_BITMAP_HELVETICA_12 (void*)12

/* Geometry functions */
void glutSolidSphere(double radius, int slices, int stacks);
void glutWireSphere(double radius, int slices, int stacks);
void glutSolidCube(double size);
void glutWireCube(double size);
void glutSolidTorus(double innerRadius, double outerRadius, int nsides, int rings);
void glutWireTorus(double innerRadius, double outerRadius, int nsides, int rings);
void glutSolidCone(double base, double height, int slices, int stacks);
void glutWireCone(double base, double height, int slices, int stacks);
void glutSolidCylinder(double radius, double height, int slices, int stacks);
void glutWireCylinder(double radius, double height, int slices, int stacks);
void glutSolidOctahedron(void);
void glutWireOctahedron(void);
void glutSolidIcosahedron(void);
void glutWireIcosahedron(void);
void glutSolidDodecahedron(void);
void glutWireDodecahedron(void);
void glutSolidTeapot(double size);
void glutWireTeapot(double size);

/* Text functions */
void glutBitmapCharacter(void* font, int character);

/* Utility functions */
int glutGet(int state);
void glutSwapBuffers(void);

#define GLUT_ELAPSED_TIME 700
#define GLUT_ELAPSED_TIME_MS 700

/* Key constants */
#define GLUT_KEY_F1 0x0001
#define GLUT_KEY_F2 0x0002
#define GLUT_KEY_F3 0x0003
#define GLUT_KEY_F4 0x0004
#define GLUT_KEY_F5 0x0005
#define GLUT_KEY_F6 0x0006
#define GLUT_KEY_F7 0x0007
#define GLUT_KEY_F8 0x0008
#define GLUT_KEY_F9 0x0009
#define GLUT_KEY_F10 0x000A
#define GLUT_KEY_F11 0x000B
#define GLUT_KEY_F12 0x000C
#define GLUT_KEY_LEFT 0x0064
#define GLUT_KEY_UP 0x0065
#define GLUT_KEY_RIGHT 0x0066
#define GLUT_KEY_DOWN 0x0067
#define GLUT_KEY_PAGE_UP 0x0068
#define GLUT_KEY_PAGE_DOWN 0x0069
#define GLUT_KEY_HOME 0x006A
#define GLUT_KEY_END 0x006B
#define GLUT_KEY_INSERT 0x006C

#endif
