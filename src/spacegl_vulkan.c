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

/*
 * SPACE GL - VULKAN VISUALIZER
 * 
 * CRITICAL MATRIX MATH NOTE:
 * This engine uses Row-Major matrices where Translation is stored in the bottom row (m[3]).
 * To produce the correct GLSL Column-Major transformation of T * R * S (Translate * Rotate * Scale),
 * the C-side multiplication order MUST be Reversed: S * R * T (Scale * Rotate * Translate).
 * 
 * Failure to follow this order will result in Translations being Scaled or Rotated, causing
 * objects to orbit the origin instead of rotating in place.
 */

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <inttypes.h>
#include "game_config.h"
#include "shared_state.h"

#define VIEW_WINDOW_WIDTH 1440
#define VIEW_WINDOW_HEIGHT 900
#define VIEW_QUADRANT_HALF 20.0f
#define VIEW_ROTATION_SPEED 0.0056687f
#define VIEW_CAMERA_DIST 60.0f

#define SCALE_SHIP 0.45f
#define BRIDGE_CAMERA_OFFSET_Y 0.30f
#define SCALE_BASE 0.6f
#define SCALE_STAR 2.5f
#define SCALE_PLANET 1.8f
#define SCALE_BLACKHOLE 2.0f
#define SCALE_QUASAR 2.2f
#define SCALE_ASTEROID 0.08f
#define DISTANZA_SIMMETRIA_FUNNELS_WORMHOLES 3.0f

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SPHERE_LATS 64
#define SPHERE_LONGS 128
#define WH_NR 32
#define WH_NT 64

typedef float vec3[3];
typedef float mat4[4][4];

void mat4_identity(mat4 m) {
    memset(m, 0, sizeof(mat4));
    m[0][0] = 1.0f; m[1][1] = 1.0f; m[2][2] = 1.0f; m[3][3] = 1.0f;
}

void mat4_multiply(mat4 a, mat4 b, mat4 res) {
    mat4 tmp;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    memcpy(res, tmp, sizeof(mat4));
}

void mat4_translate(mat4 m, vec3 v) {
    mat4_identity(m);
    m[3][0] = v[0]; m[3][1] = v[1]; m[3][2] = v[2];
}

void mat4_scale(mat4 m, vec3 v) {
    mat4_identity(m);
    m[0][0] = v[0]; m[1][1] = v[1]; m[2][2] = v[2]; m[3][3] = 1.0f;
}

void mat4_rotate(mat4 m, float angle, vec3 axis) {
    float c = cosf(angle); float s = sinf(angle); float t = 1.0f - c;
    float x = axis[0], y = axis[1], z = axis[2];
    float len = sqrtf(x*x + y*y + z*z);
    if (len > 0) { x /= len; y /= len; z /= len; }
    mat4 rot; mat4_identity(rot);
    rot[0][0] = t*x*x + c;   rot[0][1] = t*x*y - s*z; rot[0][2] = t*x*z + s*y;
    rot[1][0] = t*x*y + s*z; rot[1][1] = t*y*y + c;   rot[1][2] = t*y*z - s*x;
    rot[2][0] = t*x*z - s*y; rot[2][1] = t*y*z + s*x; rot[2][2] = t*z*z + c;
    mat4_multiply(m, rot, m);
}

void mat4_lookat(vec3 eye, vec3 center, vec3 up, mat4 dest) {
    vec3 f = { center[0] - eye[0], center[1] - eye[1], center[2] - eye[2] };
    float flen = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
    f[0] /= flen; f[1] /= flen; f[2] /= flen;
    vec3 s = { f[1]*up[2] - f[2]*up[1], f[2]*up[0] - f[0]*up[2], f[0]*up[1] - f[1]*up[0] };
    float slen = sqrtf(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
    s[0] /= slen; s[1] /= slen; s[2] /= slen;
    vec3 u = { s[1]*f[2] - s[2]*f[1], s[2]*f[0] - s[0]*f[2], s[0]*f[1] - s[1]*f[0] };
    mat4_identity(dest);
    dest[0][0] = s[0]; dest[1][0] = u[0]; dest[2][0] = -f[0];
    dest[0][1] = s[1]; dest[1][1] = u[1]; dest[2][1] = -f[1];
    dest[0][2] = s[2]; dest[1][2] = u[2]; dest[2][2] = -f[2];
    dest[3][0] = -(s[0]*eye[0] + s[1]*eye[1] + s[2]*eye[2]);
    dest[3][1] = -(u[0]*eye[0] + u[1]*eye[1] + u[2]*eye[2]);
    dest[3][2] = f[0]*eye[0] + f[1]*eye[1] + f[2]*eye[2];
}

void mat4_perspective(float fovy, float aspect, float nearZ, float farZ, mat4 dest) {
    float f = 1.0f / tanf(fovy / 2.0f);
    mat4_identity(dest);
    dest[0][0] = f / aspect; dest[1][1] = f;
    dest[2][2] = farZ / (nearZ - farZ); dest[2][3] = -1.0f;
    dest[3][2] = (nearZ * farZ) / (nearZ - farZ); dest[3][3] = 0.0f;
}

void getObjectColor(int type, int faction, float* r, float* g, float* b);

const int WIDTH = VIEW_WINDOW_WIDTH;
const int HEIGHT = VIEW_WINDOW_HEIGHT;

char vertex_shader_path[512] = "/usr/share/spacegl/shaders/shader.vert.spv";
char fragment_shader_path[512] = "/usr/share/spacegl/shaders/shader.frag.spv";

void resolve_shader_paths() {
    if (access(vertex_shader_path, F_OK) != 0) {
        strcpy(vertex_shader_path, "build/shaders/shader.vert.spv");
        strcpy(fragment_shader_path, "build/shaders/shader.frag.spv");
    }
}

const char* GLTF_MODEL_PATH = "spaceships/uss_shenzhou44/scene.gltf";

typedef struct { float pos[3]; float color[3]; float normal[3]; } Vertex;

const Vertex vertices[] = {
    /* Top Loop (Green) - Y=20 */
    {{-20, 20,-20}, {0,1,0}, {0,1,0}}, {{ 20, 20,-20}, {0,1,0}, {0,1,0}},
    {{ 20, 20, 20}, {0,1,0}, {0,1,0}}, {{-20, 20, 20}, {0,1,0}, {0,1,0}},
    /* Bottom Loop (Red) - Y=-20 */
    {{-20,-20,-20}, {1,0,0}, {0,-1,0}}, {{ 20,-20,-20}, {1,0,0}, {0,-1,0}},
    {{ 20,-20, 20}, {1,0,0}, {0,-1,0}}, {{-20,-20, 20}, {1,0,0}, {0,-1,0}},
    /* Mid Verticals (Transitions) */
    {{-20,  0,-20}, {1,1,0}, {0,0,0}}, {{ 20,  0,-20}, {1,1,0}, {0,0,0}},
    {{ 20,  0, 20}, {1,1,0}, {0,0,0}}, {{-20,  0, 20}, {1,1,0}, {0,0,0}}
};
const uint32_t indices[] = {
    /* Top */    0,1, 1,2, 2,3, 3,0,
    /* Bottom */ 4,5, 5,6, 6,7, 7,4,
    /* Verticals: Top to Mid */ 0,8, 1,9, 2,10, 3,11,
    /* Verticals: Mid to Bottom */ 8,4, 9,5, 10,6, 11,7
};

const Vertex shipVertices[] = {
    /* Base (X = -0.7288, YZ plane). Side length b = 1 */
    {{ -0.7288f,  0.5f,  0.5f}, {1,1,1}, {1, 0, 0}},
    {{ -0.7288f, -0.5f,  0.5f}, {1,1,1}, {1, 0, 0}},
    {{ -0.7288f, -0.5f, -0.5f}, {1,1,1}, {1, 0, 0}},
    {{ -0.7288f,  0.5f, -0.5f}, {1,1,1}, {1, 0, 0}},
    /* Apex (Nose, X > 0). L = 3. Height = 2.91547f. Apex X = 2.91547 - 0.7288 = 2.1866f */
    {{ 2.1866f, 0.0f, 0.0f}, {1,1,1}, {1, 0, 0}}
};
const uint32_t shipIndices[] = {
    0, 1, 1, 2, 2, 3, 3, 0, /* Base */
    0, 4, 1, 4, 2, 4, 3, 4  /* Sides to Apex */
};

/* Starbase Geometry: Optimized Wireframe (Octahedron Core + Radial Arms + Ring) */
const Vertex starbaseVertices[] = {
    /* Core Octahedron (Indices 0-5) */
    {{ 1.0f,  0.0f,  0.0f}, {0,1,1}, { 1, 0, 0}}, {{-1.0f,  0.0f,  0.0f}, {0,1,1}, {-1, 0, 0}},
    {{ 0.0f,  1.0f,  0.0f}, {0,1,1}, { 0, 1, 0}}, {{ 0.0f, -1.0f,  0.0f}, {0,1,1}, { 0,-1, 0}},
    {{ 0.0f,  0.0f,  1.0f}, {0,1,1}, { 0, 0, 1}}, {{ 0.0f,  0.0f, -1.0f}, {0,1,1}, { 0, 0,-1}},
    /* Radial Arms (Indices 6-11) */
    {{ 3.0f,  0.0f,  0.0f}, {0,1,1}, { 1, 0, 0}}, {{-3.0f,  0.0f,  0.0f}, {0,1,1}, {-1, 0, 0}},
    {{ 0.0f,  3.0f,  0.0f}, {0,1,1}, { 0, 1, 0}}, {{ 0.0f, -3.0f,  0.0f}, {0,1,1}, { 0,-1, 0}},
    {{ 0.0f,  0.0f,  3.0f}, {0,1,1}, { 0, 0, 1}}, {{ 0.0f,  0.0f, -3.0f}, {0,1,1}, { 0, 0,-1}},
    /* Outer Ring (Indices 12-15) */
    {{ 2.0f,  0.0f,  2.0f}, {0,1,1}, { 0, 1, 0}}, {{-2.0f,  0.0f,  2.0f}, {0,1,1}, { 0, 1, 0}},
    {{-2.0f,  0.0f, -2.0f}, {0,1,1}, { 0, 1, 0}}, {{ 2.0f,  0.0f, -2.0f}, {0,1,1}, { 0, 1, 0}}
};
const uint32_t starbaseIndices[] = {
    /* Core Octahedron Edges */
    0,2, 0,3, 0,4, 0,5, 1,2, 1,3, 1,4, 1,5, 2,4, 2,5, 3,4, 3,5,
    /* Radial Arms */
    0,6, 1,7, 2,8, 3,9, 4,10, 5,11,
    /* Outer Ring */
    12,13, 13,14, 14,15, 15,12,
    /* Connections Arms to Ring */
    6,12, 6,15, 7,13, 7,14, 10,12, 10,13, 11,14, 11,15
};


const Vertex torpVertices[] = {
    {{ 0.45, 0.0, 0.0}, {1.0, 1.0, 0.0}, { 1.0, 0.0, 0.0}}, /* Nose */
    {{-0.45, 0.0, 0.0}, {1.0, 0.4, 0.0}, {-1.0, 0.0, 0.0}}, /* Tail */
    {{ 0.0,  0.1, 0.1}, {1.0, 0.8, 0.0}, { 0.0, 1.0, 1.0}},
    {{ 0.0,  0.1,-0.1}, {1.0, 0.8, 0.0}, { 0.0, 1.0,-1.0}},
    {{ 0.0, -0.1, 0.1}, {1.0, 0.8, 0.0}, { 0.0,-1.0, 1.0}},
    {{ 0.0, -0.1,-0.1}, {1.0, 0.8, 0.0}, { 0.0,-1.0,-1.0}}
};
const uint32_t torpIndices[] = {
    0,2,3, 0,3,5, 0,5,4, 0,4,2,
    1,3,2, 1,5,3, 1,4,5, 1,2,4
};

const Vertex cubeVertices[] = {
    {{-0.5,-0.5, 0.5}, {1,1,1}, {0,0,1}}, {{ 0.5,-0.5, 0.5}, {1,1,1}, {0,0,1}}, {{ 0.5, 0.5, 0.5}, {1,1,1}, {0,0,1}}, {{-0.5, 0.5, 0.5}, {1,1,1}, {0,0,1}},
    {{-0.5,-0.5,-0.5}, {1,1,1}, {0,0,-1}}, {{ 0.5,-0.5,-0.5}, {1,1,1}, {0,0,-1}}, {{ 0.5, 0.5,-0.5}, {1,1,1}, {0,0,-1}}, {{-0.5, 0.5,-0.5}, {1,1,1}, {0,0,-1}}
};
const uint32_t cubeIndices[] = { 0,1, 1,2, 2,3, 3,0, 4,5, 5,6, 6,7, 7,4, 0,4, 1,5, 2,6, 3,7 };
const uint32_t cubeSolidIndices[] = {
    0,3,2, 0,2,1, /* Front (Z+) CW */
    4,5,6, 4,6,7, /* Back (Z-) CW */
    0,4,7, 0,7,3, /* Left (X-) CW */
    1,2,6, 1,6,5, /* Right (X+) CW */
    3,7,6, 3,6,2, /* Top (Y+) CW */
    0,1,5, 0,5,4  /* Bottom (Y-) CW */
};

const Vertex axesVertices[] = {
    {{-5.5, 0, 0}, {1.0, 0, 0}, {0, 1, 0}}, {{5.5, 0, 0}, {1.0, 0, 0}, {0, 1, 0}},
    {{0, -5.5, 0}, {0, 1.0, 0}, {0, 1, 0}}, {{0, 5.5, 0}, {0, 1.0, 0}, {0, 1, 0}},
    {{0, 0, -5.5}, {0, 0, 1.0}, {0, 1, 0}}, {{0, 0, 5.5}, {0, 0, 1.0}, {0, 1, 0}}
};
const uint32_t axesIndices[] = {0, 1, 2, 3, 4, 5};

#define CIRCLE_SEGMENTS 72
#define ARC_SEGMENTS 37

const uint16_t gridIndices[84]; /* Forward declaration to keep code clean */

const Vertex vectorVertices[] = {
    {{0.18f, 0, 0}, {0, 1, 0}, {1, 0, 0}}, {{1.38f, 0, 0}, {0, 1, 0}, {1, 0, 0}}, /* Line */
    {{1.38f, 0.05f, 0.05f}, {0, 1, 0}, {1, 0, 0}}, {{1.38f, -0.05f, 0.05f}, {0, 1, 0}, {1, 0, 0}},
    {{1.38f, -0.05f, -0.05f}, {0, 1, 0}, {1, 0, 0}}, {{1.38f, 0.05f, -0.05f}, {0, 1, 0}, {1, 0, 0}},
    {{1.68f, 0, 0}, {0, 1, 0}, {1, 0, 0}} /* Tip */
};
const uint32_t vectorIndices[] = {
    0, 1, 
    2, 3, 3, 4, 4, 5, 5, 2, 
    6, 2, 6, 3, 6, 4, 6, 5 
};

typedef struct { mat4 model; float color[4]; float time; int usePushColor; float metallic; float roughness; } PushConstants;
typedef struct { mat4 view; mat4 proj; } UniformBufferObject;

#define MAX_FRAMES_IN_FLIGHT 2
#define MAX_ACTIVE_BEAMS 64
#define MAX_ACTIVE_BOOMS 128
#define MAX_ACTIVE_TORPS 256
#define MAX_ACTIVE_DISMANTLES 64
#define EXPLOSION_PIXELS 256
#define MAX_STARS 2000
#define MAX_ARRIVAL_PARTICLES 500

/* Starfield Mode Selection Macros */
#define STARFIELD_MODE_TWINKLE 1  /* Dynamic intensity variation */
#define STARFIELD_MODE_HALO    2  /* Geometric atmospheric glow */
#define STARFIELD_MODE_BLOOM   3  /* Point Sprite Procedural Bloom (Most Realistic) */

#define STARFIELD_MODE STARFIELD_MODE_TWINKLE

typedef struct { float sx, sy, sz, tx, ty, tz, life; int owner_id; int extra; int emitter_id; } ActiveBeam;
typedef struct { float x, y, z, life; float offsets[EXPLOSION_PIXELS][3]; float colors[EXPLOSION_PIXELS][3]; } ActiveBoom;
typedef struct { float x, y, z, life; float scale; } ActiveDismantle;
typedef struct { float x, y, z; float dx, dy, dz; int active; int id; } ActiveTorp;
typedef struct { float x, y, z; float angle; float radius; float speed; int active; } ArrivalParticle;

typedef struct { float x, y, z; double h, m; int active; int timer; } JumpState;
typedef struct { float x, y, z; double h, m; int active; } WormholeState;

typedef struct VulkanApp {
    GLFWwindow* window; VkInstance instance; VkPhysicalDevice physicalDevice; VkDevice device; VkQueue graphicsQueue;
    VkSurfaceKHR surface; VkSwapchainKHR swapChain; VkImage* swapChainImages; uint32_t swapChainImageCount;
    VkFormat swapChainImageFormat; VkExtent2D swapChainExtent; VkImageView* swapChainImageViews;
    VkRenderPass renderPass; VkDescriptorSetLayout descriptorSetLayout; VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline; VkPipeline wireframePipeline; VkPipeline pointPipeline; VkPipeline glowPipeline; VkPipeline alphaPipeline;
    VkFramebuffer* swapChainFramebuffers;
    VkCommandPool commandPool; VkSampleCountFlagBits msaaSamples;
    VkImage colorImage; VkDeviceMemory colorImageMemory; VkImageView colorImageView;
    VkImage depthImage; VkDeviceMemory depthImageMemory; VkImageView depthImageView;
    VkBuffer vertexBuffer; VkDeviceMemory vertexBufferMemory; VkBuffer indexBuffer; VkDeviceMemory indexBufferMemory;
    VkBuffer shipVertexBuffer; VkDeviceMemory shipVertexBufferMemory; VkBuffer shipIndexBuffer; VkDeviceMemory shipIndexBufferMemory;
    VkBuffer starbaseVertexBuffer; VkDeviceMemory starbaseVertexBufferMemory; VkBuffer starbaseIndexBuffer; VkDeviceMemory starbaseIndexBufferMemory;
    VkBuffer cubeVertexBuffer; VkDeviceMemory cubeVertexBufferMemory; 
    VkBuffer cubeIndexBuffer; VkDeviceMemory cubeIndexBufferMemory;
    VkBuffer cubeSolidIndexBuffer; VkDeviceMemory cubeSolidIndexBufferMemory;
    VkBuffer torpVertexBuffer; VkDeviceMemory torpVertexBufferMemory; VkBuffer torpIndexBuffer; VkDeviceMemory torpIndexBufferMemory;
    VkBuffer axesVertexBuffer; VkDeviceMemory axesVertexBufferMemory; VkBuffer axesIndexBuffer; VkDeviceMemory axesIndexBufferMemory;
    VkBuffer beamVertexBuffer; VkDeviceMemory beamVertexBufferMemory; VkBuffer beamIndexBuffer; VkDeviceMemory beamIndexBufferMemory;
    VkBuffer circleVertexBuffer; VkDeviceMemory circleVertexBufferMemory; VkBuffer circleIndexBuffer; VkDeviceMemory circleIndexBufferMemory;
    VkBuffer arcVertexBuffer; VkDeviceMemory arcVertexBufferMemory; VkBuffer arcIndexBuffer; VkDeviceMemory arcIndexBufferMemory;
    VkBuffer rollCircleVertexBuffer; VkDeviceMemory rollCircleVertexBufferMemory; VkBuffer rollCircleIndexBuffer; VkDeviceMemory rollCircleIndexBufferMemory;
    VkBuffer gridVertexBuffer; VkDeviceMemory gridVertexBufferMemory; VkBuffer gridIndexBuffer; VkDeviceMemory gridIndexBufferMemory;
    VkBuffer vectorVertexBuffer; VkDeviceMemory vectorVertexBufferMemory; VkBuffer vectorIndexBuffer; VkDeviceMemory vectorIndexBufferMemory;
    VkBuffer sphereVertexBuffer; VkDeviceMemory sphereVertexBufferMemory; VkBuffer sphereIndexBuffer; VkDeviceMemory sphereIndexBufferMemory;
    VkBuffer whVertexBuffer; VkDeviceMemory whVertexBufferMemory; VkBuffer whIndexBuffer; VkDeviceMemory whIndexBufferMemory; uint32_t whIndexCount;
    VkBuffer coreVB[13]; VkDeviceMemory coreVBM[13]; VkBuffer coreIB[13]; VkDeviceMemory coreIBM[13]; uint32_t coreICount[13];
    VkBuffer starfieldVertexBuffer; VkDeviceMemory starfieldVertexBufferMemory; VkBuffer starfieldIndexBuffer; VkDeviceMemory starfieldIndexBufferMemory;
    uint64_t gridVertexCount; uint64_t starfieldIndexCount;
    VkBuffer uniformBuffers[MAX_FRAMES_IN_FLIGHT]; VkDeviceMemory uniformBuffersMemory[MAX_FRAMES_IN_FLIGHT];
    VkDescriptorPool descriptorPool; VkDescriptorSet descriptorSets[MAX_FRAMES_IN_FLIGHT];
    VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT]; VkSemaphore renderFinishedSemaphores[MAX_FRAMES_IN_FLIGHT];
    VkFence inFlightFences[MAX_FRAMES_IN_FLIGHT]; uint32_t currentFrame;
    SharedIPC* shm; int shm_fd; float angleY; float angleX; float cameraDist; bool autoRotate;
    ActiveBeam activeBeams[MAX_ACTIVE_BEAMS]; ActiveBoom activeBooms[MAX_ACTIVE_BOOMS];
    ActiveDismantle activeDismantles[MAX_ACTIVE_DISMANTLES];
    ActiveTorp activeTorps[MAX_ACTIVE_TORPS]; ArrivalParticle arrivalParticles[MAX_ARRIVAL_PARTICLES];
    JumpState jumpArrival; WormholeState departureWormhole;
    /* Smooth State Interpolation */
    long long last_shm_frame_id;
    double last_shm_time;
    double smoothed_shm_time;
    struct {
        float x, y, z, h, m, r;
        float prev_x, prev_y, prev_z, prev_h, prev_m, prev_r;
        float target_x, target_y, target_z, target_h, target_m, target_r;
        float vx, vy, vz;
        float prev_vx, prev_vy, prev_vz;
        bool first;
    } smoothObjs[MAX_NET_OBJECTS];

    float mapAnim;
    int mapFilter;
    float bridgeAnim;
    int showBridge;
    /* Shield Hit Visuals */
    int shieldHitTimers[6];
    int lastShieldsValHit[6];
    bool shieldsInitialized;
    mat4 playerR;
    mat4 playerT;
    int shm_inspector_page; /* 0=Off, 1=Energy/Status, 2=Galaxy/HMAC, 3=Networking */
} VulkanApp;

#include "spacegl_vulkan_extras.inl"

void drawStarbase(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused)), int faction) {
    /* Faction-Specific Proprietary Starbases */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    if (faction == 10) { /* Korthian: Aggressive, jagged design. Red glow. Large spikes. */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
        mat4 S_core; mat4_scale(S_core, (vec3){1.2f*tactScale, 1.2f*tactScale, 1.2f*tactScale});
        mat4_multiply(S_core, baseT, pc.model);
        pc.color[0]=0.15f; pc.color[1]=0.15f; pc.color[2]=0.15f; pc.color[3]=1.0f; pc.usePushColor=5;
        pc.metallic=0.8f; pc.roughness=0.3f;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
        
        pc.color[0]=1.0f; pc.color[1]=0.1f; pc.color[2]=0.0f; pc.usePushColor=6; /* Red Glow */
        for (int i=0; i<4; i++) {
            mat4 S_spike, R_spike, T_spike;
            mat4_identity(R_spike); mat4_rotate(R_spike, i*M_PI/2.0f + pulse*0.5f, (vec3){0,1,0});
            mat4_scale(S_spike, (vec3){0.2f*tactScale, 0.2f*tactScale, 3.5f*tactScale});
            mat4_translate(T_spike, (vec3){0, 0, 1.2f*tactScale});
            mat4_multiply(S_spike, T_spike, pc.model); mat4_multiply(pc.model, R_spike, pc.model); mat4_multiply(pc.model, baseT, pc.model);
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
        }
    } else if (faction == 11) { /* Xylari: Organic/Plant-like. Green glow. */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
        pc.color[0]=0.0f; pc.color[1]=1.0f; pc.color[2]=0.2f; pc.color[3]=0.8f; pc.usePushColor=1;
        vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        for(int i=0; i<3; i++) {
            mat4 S_sph, T_sph; float s = (1.2f + 0.4f*sinf(pulse*2.0f + i))*tactScale;
            mat4_scale(S_sph, (vec3){s, s, s}); mat4_translate(T_sph, (vec3){0, (i-1)*tactScale*1.0f, 0});
            mat4_multiply(S_sph, T_sph, pc.model); mat4_multiply(pc.model, baseT, pc.model);
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDrawIndexed(cb, SPHERE_LATS*SPHERE_LONGS*6, 1, 0, 0, 0);
        }
    } else if (faction == 12) { /* Swarm Dark: Hive-like structure */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        pc.color[0]=0.1f; pc.color[1]=0.1f; pc.color[2]=0.1f; pc.color[3]=1.0f; pc.usePushColor=5;
        for(int i=0; i<8; i++) {
            mat4 S_c, R_c, T_c;
            mat4_identity(R_c); mat4_rotate(R_c, pulse*0.5f + i, (vec3){1,1,1});
            mat4_scale(S_c, (vec3){0.8f*tactScale, 0.8f*tactScale, 0.8f*tactScale});
            mat4_translate(T_c, (vec3){sinf(i)*1.5f*tactScale, cosf(i)*1.5f*tactScale, sinf(i*2.0f)*1.5f*tactScale});
            mat4_multiply(S_c, R_c, pc.model); mat4_multiply(pc.model, T_c, pc.model); mat4_multiply(pc.model, baseT, pc.model);
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
        }
    } else if (faction == 13) { /* Vesperian: Elegant tall pylons, magenta rings */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        pc.color[0]=0.8f; pc.color[1]=0.8f; pc.color[2]=0.9f; pc.color[3]=1.0f; pc.usePushColor=5;
        for(int i=-1; i<=1; i+=2) {
            mat4 S_p, T_p; mat4_scale(S_p, (vec3){0.4f*tactScale, 4.0f*tactScale, 0.4f*tactScale});
            mat4_translate(T_p, (vec3){i*1.2f*tactScale, 0, 0});
            mat4_multiply(S_p, T_p, pc.model); mat4_multiply(pc.model, baseT, pc.model);
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
        }
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
        pc.color[0]=1.0f; pc.color[1]=0.0f; pc.color[2]=1.0f; pc.color[3]=0.8f; pc.usePushColor=1;
        for(int i=-1; i<=1; i++) {
            mat4 S_r, T_r, R_r; mat4_identity(R_r); mat4_rotate(R_r, M_PI/2.0f, (vec3){1,0,0}); mat4_rotate(R_r, pulse*2.0f, (vec3){0,0,1});
            mat4_scale(S_r, (vec3){2.0f*tactScale, 2.0f*tactScale, 2.0f*tactScale});
            mat4_translate(T_r, (vec3){0, i*2.0f*tactScale, 0});
            mat4_multiply(S_r, R_r, pc.model); mat4_multiply(pc.model, T_r, pc.model); mat4_multiply(pc.model, baseT, pc.model);
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDraw(cb, 64, 1, 0, 0);
        }
    } else if (faction == 14) { /* Ascendant: Angelic rings */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        mat4 S_c; mat4_scale(S_c, (vec3){1.0f*tactScale, 1.0f*tactScale, 1.0f*tactScale}); mat4_multiply(S_c, baseT, pc.model);
        pc.color[0]=0.8f; pc.color[1]=0.8f; pc.color[2]=1.0f; pc.color[3]=1.0f; pc.usePushColor=6;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, SPHERE_LATS*SPHERE_LONGS*6, 1, 0, 0, 0);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
        pc.color[0]=0.5f; pc.color[1]=0.0f; pc.color[2]=0.8f; pc.color[3]=0.8f; pc.usePushColor=1;
        for(int i=0; i<3; i++) {
            mat4 S_r, R_r; mat4_identity(R_r); mat4_rotate(R_r, pulse*(1.0f+i), (vec3){1,1,0});
            mat4_scale(S_r, (vec3){(1.8f+i*0.6f)*tactScale, (1.8f+i*0.6f)*tactScale, (1.8f+i*0.6f)*tactScale});
            mat4_multiply(S_r, R_r, pc.model); mat4_multiply(pc.model, baseT, pc.model);
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDraw(cb, 64, 1, 0, 0);
        }
    } else if (faction == 15) { /* Quarzite: Crystals */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        pc.color[0]=1.0f; pc.color[1]=0.5f; pc.color[2]=0.0f; pc.color[3]=1.0f; pc.usePushColor=6;
        for(int i=0; i<4; i++) {
            mat4 S_c, R_c; mat4_identity(R_c); mat4_rotate(R_c, pulse*0.2f + i*M_PI/4.0f, (vec3){0,1,1});
            mat4_scale(S_c, (vec3){1.5f*tactScale, 0.4f*tactScale, 1.5f*tactScale});
            mat4_multiply(S_c, R_c, pc.model); mat4_multiply(pc.model, baseT, pc.model);
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
        }
    } else if (faction == 16) { /* Saurian: Brutalist blocks */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        pc.color[0]=0.4f; pc.color[1]=0.4f; pc.color[2]=0.3f; pc.color[3]=1.0f; pc.usePushColor=5;
        mat4 S_b; mat4_scale(S_b, (vec3){2.0f*tactScale, 2.0f*tactScale, 2.0f*tactScale}); mat4_multiply(S_b, baseT, pc.model);
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
        pc.color[0]=0.4f; pc.color[1]=0.6f; pc.color[2]=0.1f; pc.usePushColor=6;
        for(int i=-1; i<=1; i+=2) {
            mat4 S_g, T_g; mat4_scale(S_g, (vec3){2.1f*tactScale, 0.3f*tactScale, 2.1f*tactScale});
            mat4_translate(T_g, (vec3){0, i*1.0f*tactScale, 0});
            mat4_multiply(S_g, T_g, pc.model); mat4_multiply(pc.model, baseT, pc.model);
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
        }
    } else if (faction == 17) { /* Gilded: Gold sphere + rings */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        pc.color[0]=1.0f; pc.color[1]=0.8f; pc.color[2]=0.1f; pc.color[3]=1.0f; pc.usePushColor=5; pc.metallic=1.0f; pc.roughness=0.1f;
        mat4 S_s; mat4_scale(S_s, (vec3){1.5f*tactScale, 1.5f*tactScale, 1.5f*tactScale}); mat4_multiply(S_s, baseT, pc.model);
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, SPHERE_LATS*SPHERE_LONGS*6, 1, 0, 0, 0);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
        pc.usePushColor=1;
        for(int i=0; i<3; i++) {
            mat4 S_r, R_r; mat4_identity(R_r); mat4_rotate(R_r, M_PI/2.0f, (vec3){1,0,0}); mat4_rotate(R_r, pulse*0.5f*(i+1), (vec3){0,0,1});
            mat4_scale(S_r, (vec3){(1.8f+i*0.5f)*tactScale, (1.8f+i*0.5f)*tactScale, (1.8f+i*0.5f)*tactScale});
            mat4_multiply(S_r, R_r, pc.model); mat4_multiply(pc.model, baseT, pc.model);
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDraw(cb, 64, 1, 0, 0);
        }
    } else if (faction == 18) { /* Fluidic Void: Shifting wireframes */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        pc.color[0]=0.7f; pc.color[1]=1.0f; pc.color[2]=0.0f; pc.color[3]=0.6f; pc.usePushColor=1;
        for(int i=0; i<3; i++) {
            mat4 S_w, R_w; mat4_identity(R_w); mat4_rotate(R_w, pulse*(1.0f+i), (vec3){0.5f, 1, 0});
            float w_s = (1.5f + 0.5f*sinf(pulse*3.0f + i))*tactScale;
            mat4_scale(S_w, (vec3){w_s, w_s*0.8f, w_s});
            mat4_multiply(S_w, R_w, pc.model); mat4_multiply(pc.model, baseT, pc.model);
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDrawIndexed(cb, SPHERE_LATS*SPHERE_LONGS*6, 1, 0, 0, 0);
        }
    } else if (faction == 19) { /* Cryos: Ice shards */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        pc.color[0]=0.0f; pc.color[1]=0.8f; pc.color[2]=1.0f; pc.color[3]=0.8f; pc.usePushColor=6;
        for(int i=0; i<5; i++) {
            mat4 S_i, R_i, T_i; mat4_identity(R_i); mat4_rotate(R_i, i*M_PI*2.0f/5.0f + pulse*0.2f, (vec3){0,1,0});
            mat4_scale(S_i, (vec3){0.3f*tactScale, (2.5f+sinf(i))*tactScale, 0.3f*tactScale});
            mat4_translate(T_i, (vec3){0,0,1.2f*tactScale});
            mat4_multiply(S_i, T_i, pc.model); mat4_multiply(pc.model, R_i, pc.model); mat4_multiply(pc.model, baseT, pc.model);
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
        }
    } else if (faction == 20) { /* Apex: Dark red monoliths */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        pc.color[0]=0.2f; pc.color[1]=0.0f; pc.color[2]=0.0f; pc.color[3]=1.0f; pc.usePushColor=5;
        mat4 S_m; mat4_scale(S_m, (vec3){1.2f*tactScale, 3.5f*tactScale, 1.2f*tactScale}); mat4_multiply(S_m, baseT, pc.model);
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
        pc.color[0]=1.0f; pc.color[1]=0.1f; pc.color[2]=0.1f; pc.usePushColor=6;
        mat4 S_g; mat4_scale(S_g, (vec3){1.3f*tactScale, 0.15f*tactScale, 1.3f*tactScale}); mat4_multiply(S_g, baseT, pc.model);
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    } else {
        /* Default / Alliance: Spire + Disk + Pulsing Core */
        /* 1. CENTRAL ENERGY CORE (Wireframe Sphere, Pulsing) */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
        mat4 S_core, R_core;
        mat4_identity(R_core);
        mat4_rotate(R_core, pulse * 0.5f, (vec3){0,1,0});
        float corePulse = 0.8f + 0.2f * sinf(pulse * 3.0f);
        mat4_scale(S_core, (vec3){corePulse * tactScale, corePulse * tactScale, corePulse * tactScale});
        mat4_multiply(S_core, R_core, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        pc.color[0]=1.0f; pc.color[1]=0.8f; pc.color[2]=0.0f; pc.color[3]=1.0f;
        pc.usePushColor=1;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

        /* 2. CENTRAL SPIRE (Solid, Dark Gray) */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
        mat4 S_spire, T_spire;
        mat4_scale(S_spire, (vec3){0.2f * tactScale, 4.0f * tactScale, 0.2f * tactScale});
        mat4_identity(T_spire); /* Centered on baseT */
        mat4_multiply(S_spire, T_spire, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        pc.color[0]=0.3f; pc.color[1]=0.3f; pc.color[2]=0.3f; pc.color[3]=1.0f;
        pc.usePushColor=5; /* PBR */
        pc.metallic = 0.8f; pc.roughness = 0.2f;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);

        /* 3. MAIN HABITATION DISK (Solid, Gray) */
        mat4 S_disk, R_disk;
        mat4_identity(R_disk);
        mat4_rotate(R_disk, pulse * 0.2f, (vec3){0,1,0});
        mat4_scale(S_disk, (vec3){2.2f * tactScale, 0.15f * tactScale, 2.2f * tactScale});
        mat4_multiply(S_disk, R_disk, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        pc.color[0]=0.5f; pc.color[1]=0.5f; pc.color[2]=0.5f; pc.color[3]=1.0f;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);

        /* 4. AUXILIARY SENSOR DISKS (Solid, Light Gray) */
        float diskOffsets[] = {1.2f, -1.2f};
        for(int i=0; i<2; i++) {
            mat4 S_aux, T_aux, R_aux;
            mat4_identity(R_aux);
            mat4_rotate(R_aux, -pulse * 0.4f * (i+1), (vec3){0,1,0});
            mat4_scale(S_aux, (vec3){1.0f * tactScale, 0.08f * tactScale, 1.0f * tactScale});
            mat4_translate(T_aux, (vec3){0, diskOffsets[i] * tactScale, 0});
            mat4_multiply(S_aux, R_aux, pc.model);
            mat4_multiply(pc.model, T_aux, pc.model);
            mat4_multiply(pc.model, baseT, pc.model);
            pc.color[0]=0.6f; pc.color[1]=0.6f; pc.color[2]=0.6f; pc.color[3]=1.0f;
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
        }
    }
    
    /* Restore pipeline for next objects in the loop */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawPulsar(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused)), int type) {
    /* Pulsar: Fast Spinning Core + Dual Relativistic Jets */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. DEFINE COLORS BY TYPE */
    float coreCol[3], jetCol[3];
    float spinSpeed = 1.0f;
    if (type == 0) { /* High Energy Blue Pulsar */
        coreCol[0]=0.1f; coreCol[1]=0.4f; coreCol[2]=1.0f;
        jetCol[0]=0.4f; jetCol[1]=0.8f; jetCol[2]=1.0f;
        spinSpeed = 5.0f;
    } else if (type == 1) { /* Standard White/Gold Pulsar */
        coreCol[0]=1.0f; coreCol[1]=1.0f; coreCol[2]=1.0f;
        jetCol[0]=1.0f; jetCol[1]=0.9f; jetCol[2]=0.2f;
        spinSpeed = 3.0f;
    } else { /* Red Magnetar / Old Pulsar */
        coreCol[0]=1.0f; coreCol[1]=0.2f; coreCol[2]=0.1f;
        jetCol[0]=1.0f; jetCol[1]=0.4f; jetCol[2]=0.0f;
        spinSpeed = 1.5f;
    }

    /* 2. THE CORE (Solid Sphere) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    mat4 S_core, R_core;
    mat4_identity(R_core);
    mat4_rotate(R_core, pulse * spinSpeed * 2.0f, (vec3){0,1,0});
    mat4_scale(S_core, (vec3){0.4f * tactScale, 0.4f * tactScale, 0.4f * tactScale});
    mat4_multiply(S_core, R_core, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    pc.color[0]=coreCol[0]; pc.color[1]=coreCol[1]; pc.color[2]=coreCol[2]; pc.color[3]=1.0f;
    pc.usePushColor=5; /* PBR */
    pc.metallic = 0.9f; pc.roughness = 0.1f;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 3. ATMOSPHERE / SHELL (Wireframe Sphere) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    float shellPulse = 1.1f + 0.1f * sinf(pulse * spinSpeed * 1.5f);
    mat4_scale(S_core, (vec3){0.4f * shellPulse * tactScale, 0.4f * shellPulse * tactScale, 0.4f * shellPulse * tactScale});
    mat4_multiply(S_core, R_core, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    pc.color[0]=jetCol[0]; pc.color[1]=jetCol[1]; pc.color[2]=jetCol[2]; pc.color[3]=0.6f;
    pc.usePushColor=1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 4. RELATIVISTIC JETS (Long Vertical Spikes) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    for (int i = -1; i <= 1; i += 2) {
        if (i == 0) continue;
        mat4 S_jet, T_jet, R_jet;
        mat4_identity(R_jet);
        /* Jets rotate with the core axis */
        mat4_rotate(R_jet, pulse * spinSpeed * 2.0f, (vec3){0,1,0});
        
        /* Very long and thin spike */
        mat4_scale(S_jet, (vec3){0.08f * tactScale, 8.0f * tactScale, 0.08f * tactScale});
        /* Offset vertically from core */
        mat4_translate(T_jet, (vec3){0, 4.0f * i * tactScale, 0});
        
        mat4_multiply(S_jet, R_jet, pc.model);
        mat4_multiply(pc.model, T_jet, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        pc.color[0]=jetCol[0]; pc.color[1]=jetCol[1]; pc.color[2]=jetCol[2]; pc.color[3]=0.8f;
        pc.usePushColor=6; /* Hyper-Glow Effect */
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }
}

void drawComet(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float h, float m, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Comet: Nucleus + Pulsing Coma + Multi-Layer Trail */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* Orientation for the tail: Align with heading/pitch */
    mat4 R_orient;
    mat4_identity(R_orient);
    mat4_rotate(R_orient, (h - 90.0f) * M_PI / 180.0f, (vec3){0, 1, 0});
    mat4_rotate(R_orient, m * M_PI / 180.0f, (vec3){0, 0, 1});
    mat4_rotate(R_orient, 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});

    /* 1. THE NUCLEUS (Irregular Crystalline Sphere) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    mat4 S_nuc, R_nuc;
    mat4_identity(R_nuc);
    mat4_rotate(R_nuc, pulse * 0.5f, (vec3){1, 0, 1});
    mat4_scale(S_nuc, (vec3){0.18f * tactScale, 0.14f * tactScale, 0.22f * tactScale});
    mat4_multiply(S_nuc, R_nuc, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    pc.color[0]=0.9f; pc.color[1]=0.9f; pc.color[2]=1.0f; pc.color[3]=1.0f;
    pc.usePushColor=5; /* PBR */
    pc.metallic = 0.3f; pc.roughness = 0.7f;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 2. THE COMA (Pulsing Ion Glow) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    float comaPulse = 1.0f + 0.15f * sinf(pulse * 2.0f);
    mat4_scale(S_nuc, (vec3){0.35f * comaPulse * tactScale, 0.35f * comaPulse * tactScale, 0.35f * comaPulse * tactScale});
    mat4_multiply(S_nuc, baseT, pc.model);
    pc.color[0]=0.4f; pc.color[1]=0.7f; pc.color[2]=1.0f; pc.color[3]=0.6f;
    pc.usePushColor=1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 3. THE TAIL (Volumetric Plasma Trail) */
    mat4 R_tail;
    mat4_identity(R_tail);
    mat4_rotate(R_tail, M_PI, (vec3){0, 1, 0}); /* Tail trails behind */
    
    for (int i=0; i<3; i++) {
        mat4 S_tail, R_wobble;
        float t_len = 3.0f + i * 2.0f;
        float t_wid = 0.25f + i * 0.2f;
        mat4_scale(S_tail, (vec3){t_wid * tactScale, t_wid * tactScale, t_len * tactScale});
        mat4_identity(R_wobble);
        mat4_rotate(R_wobble, sinf(pulse * 3.5f + i) * 0.08f, (vec3){1,0,0});
        
        mat4_multiply(S_tail, R_wobble, pc.model);
        mat4_multiply(pc.model, R_tail, pc.model);
        mat4_multiply(pc.model, R_orient, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        pc.color[0]=0.6f; pc.color[1]=0.9f; pc.color[2]=1.0f; pc.color[3]=0.5f - i*0.15f;
        pc.usePushColor=1;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->whVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->whIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, app->whIndexCount, 1, 0, 0, 0);
    }
}

void drawAsteroid(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float size, int resType, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Asteroid: Irregular Rocky Body (Main Core + 4 Surface Nodes) */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. DEFINE COLORS BY RESOURCE TYPE */
    if (resType == 0) { /* Standard Rock */
        pc.color[0]=0.55f; pc.color[1]=0.45f; pc.color[2]=0.35f;
    } else if (resType == 1) { /* Metal-Rich (Iron/Nickel) */
        pc.color[0]=0.3f; pc.color[1]=0.3f; pc.color[2]=0.32f;
    } else if (resType == 2) { /* Radioactive/Exotic (Greenish) */
        pc.color[0]=0.4f; pc.color[1]=0.7f; pc.color[2]=0.4f;
    } else { /* Ice / Frozen Volatiles */
        pc.color[0]=0.85f; pc.color[1]=0.95f; pc.color[2]=1.0f;
    }
    pc.color[3]=1.0f;
    pc.usePushColor=5; /* PBR */
    pc.metallic = (resType == 1) ? 0.8f : 0.05f;
    pc.roughness = (resType == 1) ? 0.3f : 0.85f;

    float finalScale = (0.5f + size) * tactScale;
    
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    /* 2. MAIN CORE (Irregularly scaled cube) */
    mat4 S_core, R_core;
    mat4_identity(R_core);
    mat4_rotate(R_core, pulse * 0.2f, (vec3){1.0f, 0.8f, 0.3f});
    mat4_scale(S_core, (vec3){1.2f * finalScale, 0.9f * finalScale, 1.1f * finalScale});
    mat4_multiply(S_core, R_core, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);

    /* 3. SURFACE NODES (Randomized bumps) */
    vec3 nodeOffsets[] = {{0.12f,0,0}, {-0.1f,0.08f,0}, {0,-0.1f,0.05f}, {0.05f,0.05f,-0.12f}};
    vec3 nodeScales[] = {{0.6f,0.5f,0.7f}, {0.5f,0.6f,0.5f}, {0.7f,0.4f,0.6f}, {0.4f,0.7f,0.5f}};
    
    for (int i=0; i<4; i++) {
        mat4 S_node, T_node, R_node;
        mat4_identity(R_node);
        mat4_rotate(R_node, (float)i * 90.0f * M_PI / 180.0f, (vec3){0, 1, 1});
        mat4_scale(S_node, (vec3){nodeScales[i][0] * finalScale, nodeScales[i][1] * finalScale, nodeScales[i][2] * finalScale});
        mat4_translate(T_node, (vec3){nodeOffsets[i][0] * finalScale, nodeOffsets[i][1] * finalScale, nodeOffsets[i][2] * finalScale});
        
        /* Node rotation follows the core rotation but adds its own local transform */
        mat4_multiply(S_node, R_node, pc.model);
        mat4_multiply(pc.model, T_node, pc.model);
        mat4_multiply(pc.model, R_core, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }
}

void drawMonster(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, int type, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Omega Class Entities: Crystalline Entity (30) and Space Amoeba (31) */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    if (type == 30) {
        /* 1. CRYSTALLINE ENTITY: Geometric Fractal Facets */
        pc.usePushColor = 5; /* PBR */
        pc.metallic = 0.95f; pc.roughness = 0.05f;
        
        /* Central Core */
        mat4 S_core, R_core;
        mat4_identity(R_core);
        mat4_rotate(R_core, pulse * 0.5f, (vec3){0, 1, 0});
        mat4_scale(S_core, (vec3){0.8f * tactScale, 0.8f * tactScale, 0.8f * tactScale});
        mat4_multiply(S_core, R_core, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        pc.color[0]=1.0f; pc.color[1]=1.0f; pc.color[2]=1.0f; pc.color[3]=1.0f;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

        /* Facets (6 tilted cubes) */
        vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        for (int i=0; i<6; i++) {
            mat4 S_f, R_f;
            mat4_identity(R_f);
            mat4_rotate(R_f, (float)i * 60.0f * M_PI / 180.0f + pulse, (vec3){1, 1, 0});
            mat4_scale(S_f, (vec3){0.3f * tactScale, 1.8f * tactScale, 0.3f * tactScale});
            mat4_multiply(S_f, R_f, pc.model);
            mat4_multiply(pc.model, baseT, pc.model);
            
            /* Rainbow shimmer based on facet index */
            pc.color[0]=0.4f + 0.3f * sinf(pulse + i);
            pc.color[1]=0.7f + 0.2f * cosf(pulse * 0.8f + i);
            pc.color[2]=1.0f;
            pc.usePushColor = 6; /* Hyper-Glow */
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
        }
    } else {
        /* 2. SPACE AMOEBA: Undulating Organic Organism */
        /* Central Nucleus */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
        mat4 S_nuc, R_nuc;
        mat4_identity(R_nuc);
        mat4_scale(S_nuc, (vec3){1.2f * tactScale, 1.2f * tactScale, 1.2f * tactScale});
        mat4_multiply(S_nuc, R_nuc, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        pc.color[0]=1.0f; pc.color[1]=0.1f; pc.color[2]=0.8f; pc.color[3]=0.8f;
        pc.usePushColor=5; pc.metallic=0.1f; pc.roughness=0.9f;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

        /* Undulating Nodes (8 spheres) */
        for (int i=0; i<8; i++) {
            mat4 S_n, T_n;
            float phase = pulse * 1.2f + (float)i * 0.785f;
            float d = 0.8f + 0.4f * sinf(phase);
            float s = 0.5f + 0.2f * cosf(phase * 0.5f);
            
            float tx = d * cosf((float)i * 45.0f * M_PI / 180.0f);
            float ty = 0.3f * sinf(phase * 2.0f);
            float tz = d * sinf((float)i * 45.0f * M_PI / 180.0f);
            
            mat4_scale(S_n, (vec3){s * tactScale, s * tactScale, s * tactScale});
            mat4_translate(T_n, (vec3){tx * tactScale, ty * tactScale, tz * tactScale});
            mat4_multiply(S_n, T_n, pc.model);
            mat4_multiply(pc.model, baseT, pc.model);
            
            pc.color[0]=0.6f + 0.4f * sinf(phase);
            pc.color[1]=0.2f;
            pc.color[2]=0.8f;
            pc.color[3]=0.6f;
            pc.usePushColor = 1; /* Wireframe for internal membrane look */
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
        }
    }
}

void drawTradingHub(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Trading Hub: Vertical Spire + 3 Rotating Habitation Rings */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. CENTRAL SPIRE (Main Structure) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    mat4 S_spire, R_spire;
    mat4_identity(R_spire);
    mat4_rotate(R_spire, pulse * 0.1f, (vec3){0, 1, 0});
    mat4_scale(S_spire, (vec3){0.4f * tactScale, 4.0f * tactScale, 0.4f * tactScale});
    mat4_multiply(S_spire, R_spire, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    pc.color[0]=0.5f; pc.color[1]=0.5f; pc.color[2]=0.55f; pc.color[3]=1.0f;
    pc.usePushColor=5; pc.metallic=0.8f; pc.roughness=0.3f;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);

    /* 2. ROTATING RINGS & DOCKING PADS */
    float ringHeights[] = {1.0f, 0.0f, -1.0f};
    float ringSpeeds[] = {0.5f, -0.3f, 0.8f};
    float ringScales[] = {1.8f, 2.2f, 1.6f};
    
    for (int i=0; i<3; i++) {
        mat4 S_ring, T_ring, R_ring;
        mat4_identity(R_ring);
        mat4_rotate(R_ring, pulse * ringSpeeds[i], (vec3){0, 1, 0});
        mat4_translate(T_ring, (vec3){0, ringHeights[i] * tactScale, 0});
        
        /* The Ring Geometry (Flattened Wireframe Sphere) */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
        mat4_scale(S_ring, (vec3){ringScales[i] * tactScale, 0.15f * tactScale, ringScales[i] * tactScale});
        mat4_multiply(S_ring, R_ring, pc.model);
        mat4_multiply(pc.model, T_ring, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        pc.color[0]=0.3f; pc.color[1]=0.6f; pc.color[2]=1.0f; pc.color[3]=0.7f;
        pc.usePushColor=1;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
        
        /* The Docking Pads (4 per ring) */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        for (int j=0; j<4; j++) {
            mat4 S_pad, T_pad, R_pad;
            mat4_identity(R_pad);
            mat4_rotate(R_pad, (float)j * 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});
            mat4_scale(S_pad, (vec3){0.3f * tactScale, 0.2f * tactScale, 0.3f * tactScale});
            mat4_translate(T_pad, (vec3){ringScales[i] * tactScale, 0, 0});
            
            /* Order: S -> R_local -> T_local -> R_ring -> T_height -> T_world */
            mat4_multiply(S_pad, R_pad, pc.model);
            mat4_multiply(pc.model, T_pad, pc.model);
            mat4_multiply(pc.model, R_ring, pc.model);
            mat4_multiply(pc.model, T_ring, pc.model);
            mat4_multiply(pc.model, baseT, pc.model);
            
            pc.color[0]=0.6f; pc.color[1]=0.6f; pc.color[2]=0.6f; pc.color[3]=1.0f;
            pc.usePushColor=5;
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
        }
    }

    /* 3. NAVIGATIONAL BEACON (Top Light) */
    mat4 S_light, T_light;
    mat4_scale(S_light, (vec3){0.15f * tactScale, 0.15f * tactScale, 0.15f * tactScale});
    mat4_translate(T_light, (vec3){0, 2.2f * tactScale, 0});
    mat4_multiply(S_light, T_light, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    pc.color[0]=0.0f; pc.color[1]=1.0f; pc.color[2]=0.2f; pc.color[3]=1.0f;
    pc.usePushColor=6; /* Glow */
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawMine(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Space Mine: Explosive Core + 6 Kinetic Spikes + Proximity Beacon */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. KINETIC SPIKES (6 orthogonal bars) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    pc.color[0]=0.25f; pc.color[1]=0.25f; pc.color[2]=0.27f; pc.color[3]=1.0f;
    pc.usePushColor=5; pc.metallic=0.9f; pc.roughness=0.2f;

    /* Unique high-speed rotation for mines */
    mat4 R_mine;
    mat4_identity(R_mine);
    mat4_rotate(R_mine, pulse * 4.0f, (vec3){1, 0, 1});

    for (int i=0; i<3; i++) {
        mat4 S_spike, R_spike;
        mat4_identity(R_spike);
        if (i == 0) mat4_rotate(R_spike, 0, (vec3){1,0,0});
        else if (i == 1) mat4_rotate(R_spike, M_PI/2.0, (vec3){0,1,0});
        else mat4_rotate(R_spike, M_PI/2.0, (vec3){0,0,1});

        mat4_scale(S_spike, (vec3){1.2f * tactScale, 0.12f * tactScale, 0.12f * tactScale});
        
        /* Apply Spike Rotation -> Mine Rotation -> Translation */
        mat4_multiply(S_spike, R_spike, pc.model);
        mat4_multiply(pc.model, R_mine, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }

    /* 2. EXPLOSIVE CORE (Central Sphere) */
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    mat4 S_core;
    mat4_scale(S_core, (vec3){0.3f * tactScale, 0.3f * tactScale, 0.3f * tactScale});
    mat4_multiply(S_core, R_mine, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    pc.color[0]=0.4f; pc.color[1]=0.4f; pc.color[2]=0.45f;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 3. PROXIMITY BEACON (Pulsing Red Light) */
    float blink = 0.5f + 0.5f * sinf(pulse * 15.0f); /* Rapid blinking */
    mat4 S_light;
    mat4_scale(S_light, (vec3){0.18f * tactScale, 0.18f * tactScale, 0.18f * tactScale});
    mat4_multiply(S_light, R_mine, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    
    pc.color[0]=1.0f; pc.color[1]=0.0f; pc.color[2]=0.0f; pc.color[3]=blink;
    pc.usePushColor=6; /* Glow */
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawCommBuoy(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Comm Buoy: Satellite Body + Solar Panels + Signal Waves */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. MAIN BODY (White Structural Cube) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    mat4 S_body, R_body;
    mat4_identity(R_body);
    mat4_rotate(R_body, pulse * 0.5f, (vec3){0, 1, 0});
    mat4_scale(S_body, (vec3){0.25f * tactScale, 0.25f * tactScale, 0.25f * tactScale});
    mat4_multiply(S_body, R_body, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    pc.color[0]=0.9f; pc.color[1]=0.9f; pc.color[2]=1.0f; pc.color[3]=1.0f;
    pc.usePushColor=5; pc.metallic=0.4f; pc.roughness=0.6f;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);

    /* 2. SOLAR PANELS (Dark Blue Lateral Wings) */
    for (int i=-1; i<=1; i+=2) {
        if (i==0) continue;
        mat4 S_wing, T_wing;
        mat4_scale(S_wing, (vec3){0.8f * tactScale, 0.05f * tactScale, 0.4f * tactScale});
        mat4_translate(T_wing, (vec3){i * 0.5f * tactScale, 0, 0});
        mat4_multiply(S_wing, T_wing, pc.model);
        mat4_multiply(pc.model, R_body, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        pc.color[0]=0.1f; pc.color[1]=0.1f; pc.color[2]=0.4f;
        pc.metallic=1.0f; pc.roughness=0.1f;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }

    /* 3. ANTENNA SPIRE */
    mat4 S_ant, T_ant;
    mat4_scale(S_ant, (vec3){0.05f * tactScale, 1.2f * tactScale, 0.05f * tactScale});
    mat4_translate(T_ant, (vec3){0, 0.6f * tactScale, 0});
    mat4_multiply(S_ant, T_ant, pc.model);
    mat4_multiply(pc.model, R_body, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    pc.color[0]=0.8f; pc.color[1]=0.8f; pc.color[2]=0.3f;
    pc.metallic=0.8f; pc.roughness=0.3f;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);

    /* 4. SIGNAL WAVES (Expanding wireframe rings) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->circleIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    for (int i=0; i<3; i++) {
        float waveScale = fmodf(pulse * 0.8f + i * 0.5f, 1.5f);
        mat4 S_wave, T_wave;
        mat4_scale(S_wave, (vec3){waveScale * tactScale, waveScale * tactScale, waveScale * tactScale});
        mat4_translate(T_wave, (vec3){0, 1.2f * tactScale, 0});
        mat4_multiply(S_wave, T_wave, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        pc.color[0]=0.0f; pc.color[1]=0.5f; pc.color[2]=1.0f; pc.color[3]=1.0f - (waveScale / 1.5f);
        pc.usePushColor=1;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, CIRCLE_SEGMENTS * 2, 1, 0, 0, 0);
    }

    /* 5. TELEMETRY LIGHT (Pulsing Cyan Beacon) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    mat4 S_light, T_light;
    mat4_scale(S_light, (vec3){0.12f * tactScale, 0.12f * tactScale, 0.12f * tactScale});
    mat4_translate(T_light, (vec3){0, 1.2f * tactScale, 0});
    mat4_multiply(S_light, T_light, pc.model);
    mat4_multiply(pc.model, R_body, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    pc.color[0]=0.0f; pc.color[1]=1.0f; pc.color[2]=1.0f; pc.color[3]=1.0f;
    pc.usePushColor=6; /* Glow */
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawPlatform(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, int faction, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Defense Platform: Hexagonal Armored Core + 4 Weapon Turrets */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 0. DEFINE FACTION COLORS */
    float fCol[3];
    getObjectColor(25, faction, &fCol[0], &fCol[1], &fCol[2]);

    /* 1. HEXAGONAL CORE (3 rotated armored bars) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    pc.color[0]=0.35f; pc.color[1]=0.35f; pc.color[2]=0.38f; pc.color[3]=1.0f;
    pc.usePushColor=5; pc.metallic=0.7f; pc.roughness=0.4f;

    mat4 R_plat;
    mat4_identity(R_plat);
    mat4_rotate(R_plat, pulse * 0.15f, (vec3){0, 1, 0});

    for (int i=0; i<3; i++) {
        mat4 S_bar, R_bar;
        mat4_identity(R_bar);
        mat4_rotate(R_bar, (float)i * 60.0f * M_PI / 180.0f, (vec3){0, 1, 0});
        mat4_scale(S_bar, (vec3){1.5f * tactScale, 0.4f * tactScale, 0.5f * tactScale});
        
        mat4_multiply(S_bar, R_bar, pc.model);
        mat4_multiply(pc.model, R_plat, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }

    /* 2. WEAPON TURRETS (4 corner pods) */
    for (int i=0; i<4; i++) {
        mat4 S_pod, T_pod, R_pod;
        mat4_identity(R_pod);
        mat4_rotate(R_pod, (float)i * 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});
        mat4_scale(S_pod, (vec3){0.3f * tactScale, 0.3f * tactScale, 0.3f * tactScale});
        mat4_translate(T_pod, (vec3){0.8f * tactScale, 0, 0});
        
        mat4_multiply(S_pod, T_pod, pc.model);
        mat4_multiply(pc.model, R_pod, pc.model);
        mat4_multiply(pc.model, R_plat, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        pc.color[0]=0.2f; pc.color[1]=0.2f; pc.color[2]=0.22f;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);

        /* Barrels */
        mat4 S_bar, T_bar;
        mat4_scale(S_bar, (vec3){0.6f * tactScale, 0.08f * tactScale, 0.08f * tactScale});
        mat4_translate(T_bar, (vec3){0.4f * tactScale, 0, 0});
        mat4_multiply(S_bar, T_bar, pc.model);
        mat4_multiply(pc.model, T_pod, pc.model);
        mat4_multiply(pc.model, R_pod, pc.model);
        mat4_multiply(pc.model, R_plat, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }

    /* 3. CENTRAL REACTOR CORE (Faction-Colored Glow) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    mat4 S_core;
    mat4_scale(S_core, (vec3){0.45f * tactScale, 0.45f * tactScale, 0.45f * tactScale});
    mat4_multiply(S_core, R_plat, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    
    pc.color[0]=fCol[0]; pc.color[1]=fCol[1]; pc.color[2]=fCol[2]; pc.color[3]=1.0f;
    pc.usePushColor=6; /* Glow */
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawRift(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Spatial Rift: Event Horizon + 6 Rotating Energy Rings + Subspace Discharges */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. EVENT HORIZON (Dark Core) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    mat4 S_core;
    mat4_scale(S_core, (vec3){0.25f * tactScale, 0.25f * tactScale, 0.25f * tactScale});
    mat4_multiply(S_core, baseT, pc.model);
    pc.color[0]=0.0f; pc.color[1]=0.0f; pc.color[2]=0.0f; pc.color[3]=1.0f;
    pc.usePushColor=5; pc.metallic=0.1f; pc.roughness=1.0f;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 2. ENERGY RINGS (Multi-planar wireframe arcs) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->circleIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    for (int i=0; i<6; i++) {
        mat4 S_ring, R_ring;
        mat4_identity(R_ring);
        mat4_rotate(R_ring, pulse * (1.5f + i * 0.5f), (vec3){sinf(i), cosf(i), 0.5f});
        mat4_scale(S_ring, (vec3){(0.4f + i * 0.15f) * tactScale, (0.4f + i * 0.15f) * tactScale, (0.4f + i * 0.15f) * tactScale});
        mat4_multiply(S_ring, R_ring, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        pc.color[0]=0.0f; pc.color[1]=0.8f; pc.color[2]=1.0f; pc.color[3]=0.8f - i*0.12f;
        pc.usePushColor=6; /* Glow for additive feel */
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, CIRCLE_SEGMENTS * 2, 1, 0, 0, 0);
    }

    /* 3. SUBSPACE DISCHARGES (Lightning beams) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->beamVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->beamIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    for (int i=0; i<4; i++) {
        mat4 S_beam, R_beam;
        float b_len = 1.2f + 0.5f * sinf(pulse * 10.0f + i);
        mat4_identity(R_beam);
        mat4_rotate(R_beam, (float)i * 90.0f * M_PI / 180.0f + pulse * 5.0f, (vec3){1, 1, 1});
        mat4_scale(S_beam, (vec3){b_len * tactScale, 0, 0}); /* Linear beam */
        
        mat4_multiply(S_beam, R_beam, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        pc.color[0]=1.0f; pc.color[1]=0.0f; pc.color[2]=1.0f; pc.color[3]=0.7f;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(cb, 2, 1, 0, 0);
    }
}

void drawAlienArtifact(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Alien Artifact: Crystalline Core + Orbiting Shards + Resonance Field */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. CRYSTALLINE CORE (4 tilted interlocked cubes) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    pc.color[0]=0.0f; pc.color[1]=1.0f; pc.color[2]=0.6f; pc.color[3]=1.0f;
    pc.usePushColor=6; /* Hyper-Glow */

    mat4 R_artifact;
    mat4_identity(R_artifact);
    mat4_rotate(R_artifact, pulse * 0.8f, (vec3){0, 1, 0});

    for (int i=0; i<4; i++) {
        mat4 S_facet, R_facet;
        mat4_identity(R_facet);
        mat4_rotate(R_facet, (float)i * 45.0f * M_PI / 180.0f + pulse, (vec3){1, 1, 1});
        mat4_scale(S_facet, (vec3){0.4f * tactScale, 0.4f * tactScale, 0.4f * tactScale});
        
        mat4_multiply(S_facet, R_facet, pc.model);
        mat4_multiply(pc.model, R_artifact, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }

    /* 2. ORBITING SHARDS (4 kinetic fragments) */
    pc.color[0]=0.85f; pc.color[1]=0.7f; pc.color[2]=0.1f; /* Ancient Gold */
    pc.usePushColor=5; pc.metallic=1.0f; pc.roughness=0.2f;
    for (int i=0; i<4; i++) {
        mat4 S_shard, T_shard, R_shard;
        float ang = pulse * 2.0f + (float)i * M_PI / 2.0f;
        float r = 0.8f * tactScale;
        
        mat4_identity(R_shard);
        mat4_rotate(R_shard, pulse * 3.0f, (vec3){0, 1, 0});
        mat4_scale(S_shard, (vec3){0.12f * tactScale, 0.12f * tactScale, 0.12f * tactScale});
        mat4_translate(T_shard, (vec3){r * cosf(ang), 0.3f * sinf(pulse + i) * tactScale, r * sinf(ang)});
        
        mat4_multiply(S_shard, R_shard, pc.model);
        mat4_multiply(pc.model, T_shard, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }

    /* 3. RESONANCE RINGS (Expanding Ancient energy) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->circleIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    for (int i=0; i<3; i++) {
        float waveScale = fmodf(pulse * 0.5f + i * 0.66f, 2.0f);
        mat4 S_ring;
        mat4_scale(S_ring, (vec3){waveScale * tactScale, waveScale * tactScale, waveScale * tactScale});
        mat4_multiply(S_ring, baseT, pc.model);
        
        pc.color[0]=0.0f; pc.color[1]=1.0f; pc.color[2]=0.8f; pc.color[3]=1.0f - (waveScale / 2.0f);
        pc.usePushColor=1;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, CIRCLE_SEGMENTS * 2, 1, 0, 0, 0);
    }
}

void drawNeutronStar(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    /* Neutron Star: Ultra-Dense Spinning Core + High-Intensity Magnetic Shell */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. ULTRA-DENSE CORE (Solid Sphere, Very fast spin) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    mat4 S_core, R_core;
    mat4_identity(R_core);
    mat4_rotate(R_core, pulse * 15.0f, (vec3){0, 1, 0});
    mat4_scale(S_core, (vec3){0.35f * tactScale, 0.35f * tactScale, 0.35f * tactScale});
    mat4_multiply(S_core, R_core, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    
    pc.color[0]=0.8f; pc.color[1]=0.8f; pc.color[2]=1.0f; pc.color[3]=1.0f;
    pc.usePushColor=5; /* PBR */
    pc.metallic = 1.0f; pc.roughness = 0.05f;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 2. MAGNETIC SHELL (Wireframe Sphere, Pulsing) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    float shellPulse = 1.2f + 0.1f * sinf(pulse * 8.0f);
    mat4 S_shell;
    mat4_scale(S_shell, (vec3){0.35f * shellPulse * tactScale, 0.35f * shellPulse * tactScale, 0.35f * shellPulse * tactScale});
    mat4_multiply(S_shell, R_core, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    
    pc.color[0]=0.5f; pc.color[1]=0.5f; pc.color[2]=1.0f; pc.color[3]=0.6f;
    pc.usePushColor=1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 3. GAMMA RAYS (Expanding faint rings) */
    vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->circleIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    for (int i=0; i<2; i++) {
        float r_pulse = fmodf(pulse * 1.5f + i * 0.75f, 1.0f);
        float r_scale = (0.5f + r_pulse * 1.5f) * tactScale;
        mat4 S_ring, R_ring;
        mat4_identity(R_ring);
        mat4_rotate(R_ring, M_PI/2.0f, (vec3){1, 0, 0});
        mat4_scale(S_ring, (vec3){r_scale, r_scale, r_scale});
        mat4_multiply(S_ring, R_ring, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        pc.color[0]=0.8f; pc.color[1]=0.9f; pc.color[2]=1.0f; pc.color[3]=0.4f * (1.0f - r_pulse);
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, CIRCLE_SEGMENTS * 2, 1, 0, 0, 0);
    }
}

void drawMegaStructure(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Mega Structure: Colossal Pylon + 4 Transversal Arms + Scaffolding */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. COLOSSAL SPIRE (Massive Central Pillar) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    mat4 S_spire;
    mat4_scale(S_spire, (vec3){1.2f * tactScale, 8.0f * tactScale, 1.2f * tactScale});
    mat4_multiply(S_spire, baseT, pc.model);
    pc.color[0]=0.2f; pc.color[1]=0.2f; pc.color[2]=0.22f; pc.color[3]=1.0f;
    pc.usePushColor=5; pc.metallic=0.6f; pc.roughness=0.6f;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);

    /* 2. TRANSVERSAL ARMS (4 massive bars) */
    for (int i=0; i<4; i++) {
        mat4 S_arm, R_arm;
        mat4_identity(R_arm);
        mat4_rotate(R_arm, (float)i * 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});
        mat4_scale(S_arm, (vec3){4.0f * tactScale, 0.8f * tactScale, 0.8f * tactScale});
        
        mat4_multiply(S_arm, R_arm, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        pc.color[0]=0.25f; pc.color[1]=0.22f; pc.color[2]=0.2f; /* Copper/Steel mix */
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);

        /* 3. END HABITATS & SCAFFOLDING */
        mat4 S_hab, T_hab;
        mat4_scale(S_hab, (vec3){1.0f * tactScale, 1.0f * tactScale, 1.0f * tactScale});
        mat4_translate(T_hab, (vec3){4.0f * tactScale, 0, 0});
        
        mat4_multiply(S_hab, T_hab, pc.model);
        mat4_multiply(pc.model, R_arm, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        /* Solid core of habitat */
        pc.color[0]=0.3f; pc.color[1]=0.3f; pc.color[2]=0.3f;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);

        /* Wireframe Scaffolding around habitat */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
        mat4 S_cage;
        mat4_scale(S_cage, (vec3){1.3f * tactScale, 1.3f * tactScale, 1.3f * tactScale});
        mat4_multiply(S_cage, T_hab, pc.model);
        mat4_multiply(pc.model, R_arm, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        pc.color[0]=1.0f; pc.color[1]=0.5f; pc.color[2]=0.0f; pc.color[3]=0.4f;
        pc.usePushColor=1;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    }

    /* 4. AVIATION BEACONS (Pulsing Orange Lights) */
    float blink = 0.5f + 0.5f * sinf(pulse * 3.0f);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    float heights[] = {3.8f, 0.0f, -3.8f};
    for (int h=0; h<3; h++) {
        mat4 S_light, T_light;
        mat4_scale(S_light, (vec3){0.3f * tactScale, 0.3f * tactScale, 0.3f * tactScale});
        mat4_translate(T_light, (vec3){0, heights[h] * tactScale, 0});
        mat4_multiply(S_light, T_light, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        pc.color[0]=1.0f; pc.color[1]=0.4f; pc.color[2]=0.0f; pc.color[3]=blink;
        pc.usePushColor=6;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }
}

void drawVoidCrystal(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Void Crystal: Crystalline Cluster + Subspace Shell + Void Pulsar */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. CRYSTALLINE CLUSTER (Jagged facets) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    pc.color[0]=0.4f; pc.color[1]=0.0f; pc.color[2]=0.8f; pc.color[3]=0.9f;
    pc.usePushColor=5; pc.metallic=0.2f; pc.roughness=0.1f;

    mat4 R_void;
    mat4_identity(R_void);
    mat4_rotate(R_void, pulse * 0.4f, (vec3){0, 1, 0});
    for (int i=0; i<5; i++) {
        mat4 S_fac, R_fac;
        mat4_identity(R_fac);
        mat4_rotate(R_fac, (float)i * 72.0f * M_PI / 180.0f + pulse * 0.2f, (vec3){1, 0.5f, 0});
        mat4_scale(S_fac, (vec3){(0.3f + i*0.1f) * tactScale, (0.8f + i*0.2f) * tactScale, (0.3f + i*0.1f) * tactScale});
        
        mat4_multiply(S_fac, R_fac, pc.model);
        mat4_multiply(pc.model, R_void, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }
    
    /* 2. SUBSPACE SHELL (Wireframe Sphere) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    mat4 S_shell;
    mat4_scale(S_shell, (vec3){1.5f * tactScale, 1.5f * tactScale, 1.5f * tactScale});
    mat4_multiply(S_shell, R_void, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    pc.color[0]=0.6f; pc.color[1]=0.2f; pc.color[2]=1.0f; pc.color[3]=0.5f;
    pc.usePushColor=1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 3. VOID PULSAR (Glowing Indigo Core) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    mat4 S_core;
    mat4_scale(S_core, (vec3){0.5f * tactScale, 0.5f * tactScale, 0.5f * tactScale});
    mat4_multiply(S_core, baseT, pc.model);
    pc.color[0]=0.2f; pc.color[1]=0.0f; pc.color[2]=0.4f; pc.color[3]=1.0f;
    pc.usePushColor=6; /* Glow */
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawSatellite(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Satellite: Core Body + Solar Wings + Scanning Dish */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. MAIN CHASSIS (Silver Structural Cube) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    mat4 S_body, R_body;
    mat4_identity(R_body);
    mat4_rotate(R_body, pulse * 0.3f, (vec3){0, 1, 0});
    mat4_scale(S_body, (vec3){0.3f * tactScale, 0.3f * tactScale, 0.3f * tactScale});
    mat4_multiply(S_body, R_body, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    pc.color[0]=0.8f; pc.color[1]=0.8f; pc.color[2]=0.85f; pc.color[3]=1.0f;
    pc.usePushColor=5; pc.metallic=0.9f; pc.roughness=0.2f;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);

    /* 2. SOLAR ARRAYS (2 Gold Wings) */
    for (int i=-1; i<=1; i+=2) {
        if (i==0) continue;
        mat4 S_wing, T_wing;
        mat4_scale(S_wing, (vec3){1.0f * tactScale, 0.04f * tactScale, 0.5f * tactScale});
        mat4_translate(T_wing, (vec3){i * 0.7f * tactScale, 0, 0});
        mat4_multiply(S_wing, T_wing, pc.model);
        mat4_multiply(pc.model, R_body, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        pc.color[0]=0.9f; pc.color[1]=0.75f; pc.color[2]=0.1f; /* Gold foil */
        pc.metallic=1.0f; pc.roughness=0.1f;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }

    /* 3. SCANNING DISH (Wireframe parabolic form) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    mat4 S_dish, T_dish, R_dish;
    mat4_identity(R_dish);
    mat4_rotate(R_dish, M_PI/2.0f, (vec3){1, 0, 0}); /* Point forward/up */
    mat4_scale(S_dish, (vec3){0.45f * tactScale, 0.15f * tactScale, 0.45f * tactScale});
    mat4_translate(T_dish, (vec3){0, 0.45f * tactScale, 0});
    mat4_multiply(S_dish, T_dish, pc.model);
    mat4_multiply(pc.model, R_dish, pc.model);
    mat4_multiply(pc.model, R_body, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    pc.color[0]=0.4f; pc.color[1]=0.7f; pc.color[2]=1.0f; pc.color[3]=0.8f;
    pc.usePushColor=1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 4. STATUS LIGHTS (Blinking beacons) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    float blink = 0.5f + 0.5f * sinf(pulse * 6.0f);
    mat4 S_light, T_light;
    mat4_scale(S_light, (vec3){0.1f * tactScale, 0.1f * tactScale, 0.1f * tactScale});
    mat4_translate(T_light, (vec3){0, -0.4f * tactScale, 0});
    mat4_multiply(S_light, T_light, pc.model);
    mat4_multiply(pc.model, R_body, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    pc.color[0]=1.0f; pc.color[1]=0.1f; pc.color[2]=0.1f; pc.color[3]=blink;
    pc.usePushColor=6; /* Glow */
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawDysonFragment(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Dyson Fragment: Exposed Structural Lattice + Tech Backbone + Power Nodes */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    mat4 R_dyson;
    mat4_identity(R_dyson);
    mat4_rotate(R_dyson, pulse * 0.2f, (vec3){0, 1, 0.3f});

    /* 1. STRUCTURAL LATTICE (The outer shell, now wireframe for visibility) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    mat4 S_lat;
    mat4_scale(S_lat, (vec3){2.5f * tactScale, 0.6f * tactScale, 2.5f * tactScale});
    mat4_multiply(S_lat, R_dyson, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    
    pc.color[0]=0.0f; pc.color[1]=0.6f; pc.color[2]=1.0f; pc.color[3]=0.5f; /* Cyan Lattice */
    pc.usePushColor=1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 2. TECHNOLOGICAL BACKBONE (Solid central structural element) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    mat4 S_back, R_back;
    mat4_identity(R_back);
    mat4_rotate(R_back, M_PI/2.0f, (vec3){0, 0, 1});
    mat4_scale(S_back, (vec3){3.0f * tactScale, 0.15f * tactScale, 0.15f * tactScale});
    mat4_multiply(S_back, R_back, pc.model);
    mat4_multiply(pc.model, R_dyson, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    
    pc.color[0]=0.2f; pc.color[1]=0.2f; pc.color[2]=0.25f; pc.color[3]=1.0f;
    pc.usePushColor=5; pc.metallic=0.8f; pc.roughness=0.4f;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);

    /* 3. EXPOSED POWER NODES (Visible through the lattice) */
    float p_glow = 0.7f + 0.3f * sinf(pulse * 3.0f);
    pc.color[0]=1.0f; pc.color[1]=0.4f; pc.color[2]=0.0f; pc.color[3]=p_glow;
    pc.usePushColor=6; /* Glow */
    
    for (int i=-2; i<=2; i++) {
        if (i == 0) continue;
        mat4 S_node, T_node;
        mat4_scale(S_node, (vec3){0.25f * tactScale, 0.25f * tactScale, 0.25f * tactScale});
        mat4_translate(T_node, (vec3){0, (float)i * 0.6f * tactScale, 0});
        
        mat4_multiply(S_node, T_node, pc.model);
        mat4_multiply(pc.model, R_back, pc.model);
        mat4_multiply(pc.model, R_dyson, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }
}

void drawInterstellarFilament(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    
    /* Draw 3 overlapping, twisting elongated shapes to form a filament bundle */
    for (int i=0; i<3; i++) {
        PushConstants pc = {0};
        pc.time = pulse + (float)i * 15.0f;
        
        mat4 S, R, T_offset;
        mat4_identity(R);
        /* Twist each strand slightly differently based on time and index */
        vec3 axis = {sinf(i * 1.5f), cosf(i * 2.1f), sinf(i * 0.8f + 1.0f)};
        float alen = sqrtf(axis[0]*axis[0] + axis[1]*axis[1] + axis[2]*axis[2]);
        if (alen > 0.001f) { axis[0] /= alen; axis[1] /= alen; axis[2] /= alen; }
        mat4_rotate(R, pulse * 0.05f + (float)i * 1.2f, axis);
        
        /* Make them very long and thin (like a plasma strand) */
        mat4_scale(S, (vec3){0.4f * tactScale, 6.0f * tactScale, 0.4f * tactScale});
        
        /* Offset them slightly from the center */
        mat4_translate(T_offset, (vec3){sinf(pulse*0.3f + i)*0.6f * tactScale, 0, cosf(pulse*0.4f + i)*0.6f * tactScale});
        
        mat4_multiply(S, R, pc.model);
        mat4_multiply(pc.model, T_offset, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        /* Base hue for the filament */
        pc.color[0] = 0.2f; pc.color[1] = 0.4f; pc.color[2] = 0.9f; pc.color[3] = 0.9f;
        pc.usePushColor = 10; /* Electrical Discharges Mode */
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }
    
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawAncientRelic(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Ancient Relic: Prismatic Silver Core + Emerald Glyphs + Energy Lattice */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    mat4 R_relic;
    mat4_identity(R_relic);
    mat4_rotate(R_relic, pulse * 0.5f, (vec3){1, 1, 0});

    /* 1. PRISMATIC CORE (Interlocked Silver Diamonds) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    pc.color[0]=0.85f; pc.color[1]=0.85f; pc.color[2]=0.9f; pc.color[3]=1.0f; /* High-Chrome Silver */
    pc.usePushColor=5; pc.metallic=1.0f; pc.roughness=0.1f;

    for (int i=0; i<3; i++) {
        mat4 S_dia, R_dia;
        mat4_identity(R_dia);
        mat4_rotate(R_dia, (float)i * 60.0f * M_PI / 180.0f, (vec3){0, 1, 0});
        mat4_scale(S_dia, (vec3){0.4f * tactScale, 0.8f * tactScale, 0.4f * tactScale});
        
        mat4_multiply(S_dia, R_dia, pc.model);
        mat4_multiply(pc.model, R_relic, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }

    /* 2. ENERGY LATTICE (Wireframe Sphere protection) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    mat4 S_lat;
    mat4_scale(S_lat, (vec3){1.2f * tactScale, 1.2f * tactScale, 1.2f * tactScale});
    mat4_multiply(S_lat, R_relic, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    pc.color[0]=0.0f; pc.color[1]=1.0f; pc.color[2]=0.8f; pc.color[3]=0.4f;
    pc.usePushColor=1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 3. ORBITING EMERALD GLYPHS */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    pc.color[0]=0.0f; pc.color[1]=1.0f; pc.color[2]=0.4f; pc.color[3]=1.0f;
    pc.usePushColor=6; /* Emerald Glow */

    for (int i=0; i<4; i++) {
        mat4 S_gly, T_gly, R_gly;
        float ang = pulse * 1.5f + (float)i * M_PI / 2.0f;
        float r = 0.9f * tactScale;
        
        mat4_identity(R_gly);
        mat4_rotate(R_gly, pulse * 4.0f, (vec3){1, 1, 1});
        mat4_scale(S_gly, (vec3){0.15f * tactScale, 0.15f * tactScale, 0.15f * tactScale});
        mat4_translate(T_gly, (vec3){r * cosf(ang), r * 0.5f * sinf(pulse + i) * tactScale, r * sinf(ang)});
        
        mat4_multiply(S_gly, R_gly, pc.model);
        mat4_multiply(pc.model, T_gly, pc.model);
        mat4_multiply(pc.model, R_relic, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }
}

void drawSubspaceRupture(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Subspace Rupture: Entropy Core + Reality Cracks + Expanding Rings */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. ENTROPY CORE (Dark Pulsing Nucleus) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    float core_scale = (0.6f + 0.2f * sinf(pulse * 12.0f)) * tactScale;
    mat4 S_core;
    mat4_scale(S_core, (vec3){core_scale, core_scale, core_scale});
    mat4_multiply(S_core, baseT, pc.model);
    
    pc.color[0]=0.2f; pc.color[1]=0.0f; pc.color[2]=0.3f; pc.color[3]=1.0f;
    pc.usePushColor=6; /* Deep Glow */
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 2. REALITY CRACKS (Magenta jagged spikes) */
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    pc.color[0]=1.0f; pc.color[1]=0.0f; pc.color[2]=0.6f; pc.color[3]=0.8f;
    for (int i=0; i<6; i++) {
        mat4 S_crack, R_crack;
        float flick = 0.5f + 0.5f * sinf(pulse * 20.0f + i);
        mat4_identity(R_crack);
        mat4_rotate(R_crack, (float)i * 60.0f * M_PI / 180.0f + pulse, (vec3){0, 1, 0.5f});
        mat4_scale(S_crack, (vec3){0.05f * tactScale, 2.0f * flick * tactScale, 0.05f * tactScale});
        
        mat4_multiply(S_crack, R_crack, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }

    /* 3. ENTROPY RINGS (Expanding wavecircles) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
    
    for (int i=0; i<3; i++) {
        float r_pulse = fmodf(pulse * 0.8f + (float)i * 0.33f, 1.0f);
        float r_scale = r_pulse * 3.0f * tactScale;
        mat4 S_ring, R_ring;
        mat4_identity(R_ring);
        mat4_rotate(R_ring, M_PI/2.0f, (vec3){1, 0, 0});
        mat4_scale(S_ring, (vec3){r_scale, r_scale, r_scale});
        
        mat4_multiply(S_ring, R_ring, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        pc.color[0]=0.7f; pc.color[1]=0.2f; pc.color[2]=1.0f; pc.color[3]=1.0f - r_pulse;
        pc.usePushColor=1;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(cb, 64, 1, 0, 0);
    }
}

void drawDarkMatterCloud(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Dark Matter Cloud: Nebulous Mass + Gravitational Veins + Distortion Rings */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. NEBULOUS MASS (Overlapping dark spheres) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    pc.color[0]=0.05f; pc.color[1]=0.05f; pc.color[2]=0.1f; pc.color[3]=0.7f;
    pc.usePushColor=5; pc.metallic=0.1f; pc.roughness=0.9f;

    for (int i=0; i<3; i++) {
        mat4 S_mass, R_mass, T_mass;
        float ang = pulse * 0.2f + (float)i * 120.0f * M_PI / 180.0f;
        mat4_identity(R_mass);
        mat4_rotate(R_mass, ang, (vec3){0, 1, 0.5f});
        mat4_scale(S_mass, (vec3){(1.5f + i*0.2f) * tactScale, (1.2f + i*0.1f) * tactScale, (1.5f + i*0.2f) * tactScale});
        mat4_translate(T_mass, (vec3){0.2f * sinf(pulse + i) * tactScale, 0, 0});
        
        mat4_multiply(S_mass, R_mass, pc.model);
        mat4_multiply(pc.model, T_mass, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }

    /* 2. GRAVITATIONAL VEINS (Internal blue filaments) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    mat4 S_vein;
    mat4_scale(S_vein, (vec3){1.6f * tactScale, 1.6f * tactScale, 1.6f * tactScale});
    mat4_multiply(S_vein, baseT, pc.model);
    pc.color[0]=0.0f; pc.color[1]=0.4f; pc.color[2]=1.0f; pc.color[3]=0.4f;
    pc.usePushColor=1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 3. DISTORTION RINGS (Subtle gravity waves) */
    vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
    for (int i=0; i<4; i++) {
        float r_pulse = fmodf(pulse * 0.4f + (float)i * 0.25f, 1.0f);
        float r_scale = r_pulse * 4.0f * tactScale;
        mat4 S_ring, R_ring;
        mat4_identity(R_ring);
        mat4_rotate(R_ring, M_PI/2.0f, (vec3){1, 0, 0.2f});
        mat4_scale(S_ring, (vec3){r_scale, r_scale, r_scale});
        
        mat4_multiply(S_ring, R_ring, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        pc.color[0]=0.2f; pc.color[1]=0.2f; pc.color[2]=0.4f; pc.color[3]=0.5f * (1.0f - r_pulse);
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(cb, 64, 1, 0, 0);
    }
}

void drawSingularity(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Singularity: Event Horizon + Accretion Disk + Polar Jets + Distortion Shells */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. EVENT HORIZON (Absolute Black Void) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    mat4 S_void;
    mat4_scale(S_void, (vec3){0.8f * tactScale, 0.8f * tactScale, 0.8f * tactScale});
    mat4_multiply(S_void, baseT, pc.model);
    
    pc.color[0]=0.0f; pc.color[1]=0.0f; pc.color[2]=0.0f; pc.color[3]=1.0f;
    pc.usePushColor=5; pc.metallic=0.0f; pc.roughness=1.0f; /* Matte Black */
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 2. ACCRETION DISK (Rotating energy rings) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
    
    for (int i=0; i<3; i++) {
        mat4 S_ring, R_ring;
        float r_scale = (1.5f + i*0.5f) * tactScale;
        mat4_identity(R_ring);
        mat4_rotate(R_ring, M_PI/2.0f, (vec3){1, 0.1f, 0}); /* Tilted disk */
        mat4_rotate(R_ring, pulse * (3.0f - i), (vec3){0, 0, 1}); /* Spinning */
        mat4_scale(S_ring, (vec3){r_scale, r_scale, r_scale});
        
        mat4_multiply(S_ring, R_ring, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        if (i==0) { pc.color[0]=1.0f; pc.color[1]=0.8f; pc.color[2]=0.0f; }
        else if (i==1) { pc.color[0]=1.0f; pc.color[1]=1.0f; pc.color[2]=1.0f; }
        else { pc.color[0]=0.6f; pc.color[1]=0.0f; pc.color[2]=1.0f; }
        
        pc.usePushColor=1;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(cb, 64, 1, 0, 0);
    }

    /* 3. POLAR JETS (High-speed energy beams) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    pc.color[0]=1.0f; pc.color[1]=1.0f; pc.color[2]=1.0f; pc.color[3]=1.0f;
    pc.usePushColor=6; /* High-Intensity Glow */
    
    for (int i=-1; i<=1; i+=2) {
        if (i==0) continue;
        mat4 S_jet, T_jet;
        mat4_scale(S_jet, (vec3){0.02f * tactScale, 5.0f * tactScale, 0.02f * tactScale});
        mat4_translate(T_jet, (vec3){0, i * 2.5f * tactScale, 0});
        
        mat4_multiply(S_jet, T_jet, pc.model);
        /* Apply the same tilt as the disk to be orthogonal (roughly) */
        mat4 R_tilt;
        mat4_identity(R_tilt);
        mat4_rotate(R_tilt, 0.1f, (vec3){0, 0, 1}); 
        mat4_multiply(pc.model, R_tilt, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }

    /* 4. DISTORTION SHELLS (Expanding gravity waves) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    for (int i=0; i<2; i++) {
        float r_pulse = fmodf(pulse * 0.5f + (float)i * 0.5f, 1.0f);
        float r_scale = r_pulse * 5.0f * tactScale;
        mat4 S_shell;
        mat4_scale(S_shell, (vec3){r_scale, r_scale, r_scale});
        mat4_multiply(S_shell, baseT, pc.model);
        
        pc.color[0]=0.4f; pc.color[1]=0.0f; pc.color[2]=0.8f; pc.color[3]=0.4f * (1.0f - r_pulse);
        pc.usePushColor=1;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }
}

void drawIonStorm(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused)), int type) {
    /* Ion Storm: Pulsing Core + Randomized Lightning Arcs + Spatial Distortion Rings */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;

    float mainCol[3], secCol[3];
    if (type == 0) { /* Standard Electric Blue */
        mainCol[0]=0.0f; mainCol[1]=0.5f; mainCol[2]=1.0f;
        secCol[0]=0.4f; secCol[1]=0.8f; secCol[2]=1.0f;
    } else { /* High Instability Violet */
        mainCol[0]=0.7f; mainCol[1]=0.0f; mainCol[2]=1.0f;
        secCol[0]=0.9f; secCol[1]=0.4f; secCol[2]=1.0f;
    }

    /* 1. CORE PULSE (Wireframe sphere) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    float corePulse = 0.8f + 0.2f * sinf(pulse * 5.0f);
    mat4 S_core;
    mat4_scale(S_core, (vec3){corePulse * tactScale, corePulse * tactScale, corePulse * tactScale});
    mat4_multiply(S_core, baseT, pc.model);
    pc.color[0]=mainCol[0]; pc.color[1]=mainCol[1]; pc.color[2]=mainCol[2]; pc.color[3]=0.9f;
    pc.usePushColor=1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 2. JAGGED LIGHTNING ARCS (Randomized spikes) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    pc.color[0]=secCol[0]; pc.color[1]=secCol[1]; pc.color[2]=secCol[2]; pc.color[3]=1.0f;
    pc.usePushColor=6;
    for (int i=0; i<8; i++) {
        float flick = (sinf(pulse * 30.0f + i*2.0f) > 0.6f) ? 1.0f : 0.0f;
        if (flick < 0.5f) continue;
        mat4 S_spike, R_spike, T_spike;
        mat4_identity(R_spike);
        mat4_rotate(R_spike, (float)i * 45.0f * M_PI / 180.0f + pulse, (vec3){sinf(i), 1, cosf(i)});
        mat4_scale(S_spike, (vec3){0.05f * tactScale, 2.0f * tactScale, 0.05f * tactScale});
        mat4_translate(T_spike, (vec3){0, 0.5f * tactScale, 0});
        mat4_multiply(S_spike, R_spike, pc.model);
        mat4_multiply(pc.model, T_spike, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }

    /* 3. SPATIAL DISTORTION RINGS */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
    for (int i=0; i<3; i++) {
        float r_pulse = fmodf(pulse * 0.6f + (float)i * 0.5f, 1.2f);
        float r_scale = r_pulse * 3.5f * tactScale;
        mat4 S_ring, R_ring;
        mat4_identity(R_ring);
        mat4_rotate(R_ring, M_PI/2.0f, (vec3){1, 0.2f*i, 0});
        mat4_scale(S_ring, (vec3){r_scale, r_scale, r_scale});
        mat4_multiply(S_ring, R_ring, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        pc.color[0]=mainCol[0]; pc.color[1]=mainCol[1]; pc.color[2]=mainCol[2]; pc.color[3]=0.5f * (1.0f - r_pulse/1.2f);
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(cb, 64, 1, 0, 0);
    }
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawPlasmaStorm(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Plasma Storm: Fire Nucleus + Plasma Arcs + Turbulence Shells */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. FIRE NUCLEUS (Overlapping turbulent spheres) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    for (int i=0; i<3; i++) {
        mat4 S_fire, R_fire;
        float s_mod = (1.0f + 0.3f * sinf(pulse * 8.0f + i)) * tactScale;
        mat4_identity(R_fire);
        mat4_rotate(R_fire, pulse * (1.0f + i), (vec3){0, 1, 0.2f});
        mat4_scale(S_fire, (vec3){s_mod, s_mod, s_mod});
        mat4_multiply(S_fire, R_fire, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        pc.color[0]=1.0f; pc.color[1]=0.3f + i*0.2f; pc.color[2]=0.0f; pc.color[3]=0.8f;
        pc.usePushColor=6; /* Fire Glow */
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }

    /* 2. PLASMA ARCS (Cyan flickering lightning) */
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    pc.color[0]=0.0f; pc.color[1]=0.8f; pc.color[2]=1.0f;
    for (int i=0; i<6; i++) {
        mat4 S_arc, R_arc, T_arc;
        float flick = (sinf(pulse * 25.0f + i) > 0.5f) ? 1.0f : 0.0f;
        if (flick < 0.1f) continue;
        
        mat4_identity(R_arc);
        mat4_rotate(R_arc, (float)i * 60.0f * M_PI / 180.0f + pulse * 2.0f, (vec3){1, 0.5f, 0});
        mat4_scale(S_arc, (vec3){0.02f * tactScale, 2.5f * tactScale, 0.02f * tactScale});
        mat4_translate(T_arc, (vec3){0.5f * sinf(pulse*10.0f) * tactScale, 0, 0});
        
        mat4_multiply(S_arc, R_arc, pc.model);
        mat4_multiply(pc.model, T_arc, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }

    /* 3. TURBULENCE SHELLS (Rotating wireframes) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    for (int i=0; i<2; i++) {
        mat4 S_turb, R_turb;
        mat4_identity(R_turb);
        mat4_rotate(R_turb, -pulse * (2.0f + i), (vec3){0.3f, 1, 0});
        mat4_scale(S_turb, (vec3){(2.0f + i*0.5f) * tactScale, (2.0f + i*0.5f) * tactScale, (2.0f + i*0.5f) * tactScale});
        mat4_multiply(S_turb, R_turb, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        pc.color[0]=1.0f; pc.color[1]=0.2f; pc.color[2]=0.0f; pc.color[3]=0.4f;
        pc.usePushColor=1;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }
}

void drawTimeAnomaly(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Time Anomaly: Chronos Core + Clockwork Rings + Temporal Ghosting + Chrono-Ripples */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. CHRONOS CORE (Golden Shimmering Sphere) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    mat4 S_core;
    mat4_scale(S_core, (vec3){0.7f * tactScale, 0.7f * tactScale, 0.7f * tactScale});
    mat4_multiply(S_core, baseT, pc.model);
    
    pc.color[0]=1.0f; pc.color[1]=0.85f; pc.color[2]=0.2f; pc.color[3]=1.0f;
    pc.usePushColor=5; pc.metallic=1.0f; pc.roughness=0.1f; /* Gold */
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 2. CLOCKWORK RINGS (Rotating like hands) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
    
    pc.color[0]=1.0f; pc.color[1]=0.9f; pc.color[2]=0.5f; pc.color[3]=0.8f;
    pc.usePushColor=1;

    for (int i=0; i<4; i++) {
        mat4 S_ring, R_ring;
        float r_scale = (1.2f + i*0.3f) * tactScale;
        mat4_identity(R_ring);
        /* Orient rings at different angles */
        if (i < 2) mat4_rotate(R_ring, M_PI/2.0f, (vec3){1, 0, 0});
        else mat4_rotate(R_ring, M_PI/2.0f, (vec3){0, 1, 0});
        
        /* Rotate like clock hands: one fast, one slow */
        mat4_rotate(R_ring, pulse * (float)(i + 1) * 0.5f, (vec3){0, 0, 1});
        mat4_scale(S_ring, (vec3){r_scale, r_scale, r_scale});
        
        mat4_multiply(S_ring, R_ring, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(cb, 64, 1, 0, 0);
    }

    /* 3. TEMPORAL GHOSTING (Past/Future shells) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    for (int i=0; i<3; i++) {
        mat4 S_ghost, R_ghost;
        float g_scale = (1.0f + 0.5f * sinf(pulse * 0.5f + i)) * tactScale;
        mat4_identity(R_ghost);
        mat4_rotate(R_ghost, pulse * 0.2f, (vec3){0, 1, 0});
        mat4_scale(S_ghost, (vec3){g_scale, g_scale, g_scale});
        
        mat4_multiply(S_ghost, R_ghost, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        pc.color[0]=1.0f; pc.color[1]=1.0f; pc.color[2]=0.8f; pc.color[3]=0.2f;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }

    /* 4. CHRONO-RIPPLES (Expanding and Contracting) */
    vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
    for (int i=0; i<3; i++) {
        float r_pulse = 0.5f + 0.5f * sinf(pulse * 0.7f + i);
        float r_scale = (1.0f + r_pulse * 3.0f) * tactScale;
        mat4 S_rip, R_rip;
        mat4_identity(R_rip);
        mat4_rotate(R_rip, M_PI/2.0f, (vec3){1, 0, 0});
        mat4_scale(S_rip, (vec3){r_scale, r_scale, r_scale});
        
        mat4_multiply(S_rip, R_rip, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        pc.color[0]=1.0f; pc.color[1]=0.5f; pc.color[2]=0.0f; pc.color[3]=0.4f * (1.0f - r_pulse);
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(cb, 64, 1, 0, 0);
    }
}

void drawSubspaceAnomaly(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale __attribute__((unused)), float pulse __attribute__((unused))) {
    /* Subspace Anomaly: Dimensional Core + Subspace Lattice + Quantum Pixels + Warp Shell */
    VkDeviceSize off = 0;
    PushConstants pc = {0};
    pc.time = pulse;
    
    /* 1. DIMENSIONAL CORE (Rotating Neon Green Cube) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    mat4 S_core, R_core;
    mat4_identity(R_core);
    mat4_rotate(R_core, pulse * 2.0f, (vec3){1, 1, 1});
    mat4_scale(S_core, (vec3){0.5f * tactScale, 0.5f * tactScale, 0.5f * tactScale});
    mat4_multiply(S_core, R_core, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    
    pc.color[0]=0.0f; pc.color[1]=1.0f; pc.color[2]=0.2f; pc.color[3]=1.0f;
    pc.usePushColor=6; /* High-Intensity Glow */
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);

    /* 2. SUBSPACE LATTICE (Pulsing Blue Wireframe Cube) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    mat4 S_lat;
    float lat_pulse = 1.2f + 0.2f * sinf(pulse * 3.0f);
    mat4_scale(S_lat, (vec3){lat_pulse * tactScale, lat_pulse * tactScale, lat_pulse * tactScale});
    mat4_multiply(S_lat, R_core, pc.model);
    mat4_multiply(pc.model, baseT, pc.model);
    
    pc.color[0]=0.0f; pc.color[1]=0.4f; pc.color[2]=1.0f; pc.color[3]=0.6f;
    pc.usePushColor=1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);

    /* 3. QUANTUM PIXELS (Flickering White Bits) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    pc.color[0]=1.0f; pc.color[1]=1.0f; pc.color[2]=1.0f; pc.color[3]=1.0f;
    pc.usePushColor=6;
    
    for (int i=0; i<8; i++) {
        float flick = (sinf(pulse * 15.0f + i) > 0.6f) ? 1.0f : 0.0f;
        if (flick < 0.1f) continue;
        
        mat4 S_pix, T_pix;
        float x = ((i & 1) ? 1 : -1) * tactScale;
        float y = ((i & 2) ? 1 : -1) * tactScale;
        float z = ((i & 4) ? 1 : -1) * tactScale;
        
        mat4_scale(S_pix, (vec3){0.1f * tactScale, 0.1f * tactScale, 0.1f * tactScale});
        mat4_translate(T_pix, (vec3){x, y, z});
        mat4_multiply(S_pix, T_pix, pc.model);
        mat4_multiply(pc.model, R_core, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
    }

    /* 4. WARP SHELL (Outer Subspace Horizon) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    mat4 S_shell;
    mat4_scale(S_shell, (vec3){2.0f * tactScale, 2.0f * tactScale, 2.0f * tactScale});
    mat4_multiply(S_shell, baseT, pc.model);
    
    pc.color[0]=0.2f; pc.color[1]=1.0f; pc.color[2]=0.6f; pc.color[3]=0.3f;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

uint32_t findMemoryType(VkPhysicalDevice pDevice, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mProps; vkGetPhysicalDeviceMemoryProperties(pDevice, &mProps);
    for (uint32_t i = 0; i < mProps.memoryTypeCount; i++) { if ((typeFilter & (1 << i)) && (mProps.memoryTypes[i].propertyFlags & props) == props) return i; }
    return 0;
}

void createBuffer(VulkanApp* app, VkDeviceSize size, VkBufferUsageFlags use, VkMemoryPropertyFlags props, VkBuffer* buf, VkDeviceMemory* mem) {
    VkBufferCreateInfo bInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size, use, VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
    if (vkCreateBuffer(app->device, &bInfo, NULL, buf) != VK_SUCCESS) exit(1);
    VkMemoryRequirements mReqs; vkGetBufferMemoryRequirements(app->device, *buf, &mReqs);
    VkMemoryAllocateInfo aInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mReqs.size, findMemoryType(app->physicalDevice, mReqs.memoryTypeBits, props)};
    if (vkAllocateMemory(app->device, &aInfo, NULL, mem) != VK_SUCCESS) exit(1);
    vkBindBufferMemory(app->device, *buf, *mem, 0);
}

void createImage(VulkanApp* app, uint32_t w, uint32_t h, uint32_t mip, VkSampleCountFlagBits smp, VkFormat fmt, VkImageTiling til, VkImageUsageFlags use, VkMemoryPropertyFlags props, VkImage* img, VkDeviceMemory* mem) {
    VkImageCreateInfo iInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, fmt, {w, h, 1}, mip, 1, smp, til, use, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
    if (vkCreateImage(app->device, &iInfo, NULL, img) != VK_SUCCESS) exit(1);
    VkMemoryRequirements mReqs; vkGetImageMemoryRequirements(app->device, *img, &mReqs);
    VkMemoryAllocateInfo aInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mReqs.size, findMemoryType(app->physicalDevice, mReqs.memoryTypeBits, props)};
    if (vkAllocateMemory(app->device, &aInfo, NULL, mem) != VK_SUCCESS) exit(1);
    vkBindImageMemory(app->device, *img, *mem, 0);
}

VkImageView createImageView(VkDevice device, VkImage img, VkFormat fmt, VkImageAspectFlags asp, uint32_t mip) {
    VkImageViewCreateInfo vInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, img, VK_IMAGE_VIEW_TYPE_2D, fmt, {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY}, {asp, 0, mip, 0, 1}};
    VkImageView view; if (vkCreateImageView(device, &vInfo, NULL, &view) != VK_SUCCESS) exit(1);
    return view;
}

static char* readShaderFile(const char* filename, size_t* pSize) { FILE* file = fopen(filename, "rb");
    if (!file) { printf("ERROR: Cannot open shader %s\n", filename); return NULL; }
    fseek(file, 0, SEEK_END); *pSize = ftell(file); fseek(file, 0, SEEK_SET);
    char* buffer = (char*)malloc(*pSize); if(fread(buffer, 1, *pSize, file) != *pSize) { free(buffer); fclose(file); return NULL; } fclose(file);
    return buffer;
}

void getObjectColor(int type, int faction, float* r, float* g, float* b) {
    *r = 1.0f; *g = 1.0f; *b = 1.0f;
    
    if ((type == 1 || type >= 10) && (faction >= 10 && faction <= 20)) {
        switch(faction) {
            case 10: *r = 1.0f; *g = 0.1f; *b = 0.0f; break; /* Korthian */
            case 11: *r = 0.0f; *g = 1.0f; *b = 0.2f; break; /* Xylari */
            case 12: *r = 0.1f; *g = 0.1f; *b = 0.1f; break; /* Swarm Dark */
            case 13: *r = 1.0f; *g = 0.0f; *b = 1.0f; break; /* Vesperian */
            case 14: *r = 0.5f; *g = 0.0f; *b = 0.8f; break; /* Ascendant */
            case 15: *r = 1.0f; *g = 0.5f; *b = 0.0f; break; /* Quarzite */
            case 16: *r = 0.4f; *g = 0.6f; *b = 0.1f; break; /* Saurian */
            case 17: *r = 0.8f; *g = 0.7f; *b = 0.1f; break; /* Gilded */
            case 18: *r = 0.7f; *g = 1.0f; *b = 0.0f; break; /* Fluidic Void */
            case 19: *r = 0.0f; *g = 0.5f; *b = 1.0f; break; /* Cryos */
            case 20: *r = 0.6f; *g = 0.2f; *b = 0.1f; break; /* Apex */
            default: *r = 1.0f; *g = 0.5f; *b = 0.5f; break;
        }
        return;
    }
    
    if (type == 1) { *r = 0.0f; *g = 1.0f; *b = 1.0f; }
    else if (type == 4) { *r = 1.0f; *g = 1.0f; *b = 0.0f; }
    else if (type == 5) { *r = 0.0f; *g = 1.0f; *b = 0.5f; }
    else if (type == 3) { *r = 0.0f; *g = 1.0f; *b = 0.0f; }
    else if (type == 6) { *r = 0.5f; *g = 0.0f; *b = 1.0f; }
    else if (type == 29) { *r = 1.0f; *g = 0.0f; *b = 1.0f; }
    else if (type == 21) { *r = 0.5f; *g = 0.35f; *b = 0.25f; }
    else if (type == 35) { *r = 0.6f; *g = 0.6f; *b = 0.7f; } /* Hub */
    else if (type == 23) { *r = 0.3f; *g = 0.3f; *b = 0.35f; } /* Mine */
    else if (type == 24) { *r = 0.8f; *g = 0.8f; *b = 0.9f; } /* Buoy */
    else if (type == 25) { *r = 0.4f; *g = 0.4f; *b = 0.45f; } /* Platform */
    else if (type == 26) { *r = 0.5f; *g = 0.0f; *b = 0.8f; } /* Rift */
    else if (type == 40) { *r = 0.0f; *g = 0.8f; *b = 0.6f; } /* Artifact */
    else if (type == 50) { *r = 0.0f; *g = 1.0f; *b = 0.5f; }
}

void drawWormholeCore(VkCommandBuffer cb, VulkanApp* app, mat4 modelBase, float pulse, int type) {
    VkDeviceSize off = 0;
    mat4 R_rot; mat4_identity(R_rot);
    mat4_rotate(R_rot, pulse * 5.0f * M_PI / 180.0f, (vec3){1, 1, 1});
    mat4 M_rot; mat4_multiply(R_rot, modelBase, M_rot);

    /* 1. Outer Shell (Dark Grey) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    PushConstants pc1 = {0}; mat4 S_out; mat4_scale(S_out, (vec3){0.85f, 0.85f, 0.85f});
    mat4_multiply(S_out, M_rot, pc1.model);
    pc1.color[0]=0.15f; pc1.color[1]=0.15f; pc1.color[2]=0.15f; pc1.color[3]=1.0f; pc1.usePushColor=1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc1), &pc1);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, sizeof(cubeIndices)/4, 1, 0, 0, 0);

    /* 2. Central Singularity (Solid Black for Departure, White for Arrival) - Now using Sphere Geometry */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    PushConstants pc2 = {0}; mat4 S_core; mat4_scale(S_core, (vec3){0.75f, 0.75f, 0.75f});
    mat4_multiply(S_core, M_rot, pc2.model);
    if (type == 1) { pc2.color[0]=1.0f; pc2.color[1]=1.0f; pc2.color[2]=1.0f; }
    else { pc2.color[0]=0.0f; pc2.color[1]=0.0f; pc2.color[2]=0.0f; }
    pc2.color[3]=1.0f; pc2.usePushColor=1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc2), &pc2);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawSwarmCube(VkCommandBuffer cb, VulkanApp* app, mat4 modelBase, float pulse) {
    VkDeviceSize off = 0;
    mat4 R_rot; mat4_identity(R_rot);
    mat4_rotate(R_rot, pulse * 5.0f * M_PI / 180.0f, (vec3){1, 1, 1});
    
    mat4 M_rot; 
    /* Order: Rotate * modelBase (Translation) */
    mat4_multiply(R_rot, modelBase, M_rot);

    /* 1. Outer Wireframe (Dark Grey) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    PushConstants pc1 = {0}; 
    mat4 S_out; mat4_scale(S_out, (vec3){0.85f, 0.85f, 0.85f}); 
    /* Order: Scale * M_rot */
    mat4_multiply(S_out, M_rot, pc1.model);
    pc1.color[0]=0.15f; pc1.color[1]=0.15f; pc1.color[2]=0.15f; pc1.color[3]=1.0f; pc1.usePushColor=1;
    pc1.metallic = 0.9f; pc1.roughness = 0.1f;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc1), &pc1);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, sizeof(cubeIndices)/4, 1, 0, 0, 0);

    /* 2. Solid Core (Almost Black) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    PushConstants pc2 = {0}; 
    mat4 S_core; mat4_scale(S_core, (vec3){0.75f, 0.75f, 0.75f}); 
    mat4_multiply(S_core, M_rot, pc2.model);
    pc2.color[0]=0.05f; pc2.color[1]=0.05f; pc2.color[2]=0.05f; pc2.color[3]=1.0f; pc2.usePushColor=1;
    pc2.metallic = 1.0f; pc2.roughness = 0.05f;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc2), &pc2);
    vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, sizeof(cubeSolidIndices)/4, 1, 0, 0, 0);
}

void drawWormhole(VkCommandBuffer cb, VulkanApp* app, float x, float y, float z, double h, double m, int type, float pulse, float tactScale, float closingScale) {
    mat4 T, R, S_base;
    mat4_translate(T, (vec3){x, y, z});
    mat4_identity(R);
    
    /* Align funnel (Z) to World Orientation (Heading/Mark) */
    mat4_rotate(R, -h * M_PI / 180.0f, (vec3){0, 1, 0});
    float h_rad = h * M_PI / 180.0f;
    mat4_rotate(R, m * M_PI / 180.0f, (vec3){cosf(h_rad), 0, -sinf(h_rad)});

    /* 1. SPECTACULAR CHROMATIC GLOW (For Arrival Type 1 when closing) */
    if (type == 1 && closingScale < 0.99f) {
        float alpha = (1.0f - closingScale) * 0.9f;
        float base_r = (1.0f - closingScale) * 5.0f * tactScale; /* Expanded size */
        
        struct { float r, g, b, a, scale; int mode; } layers[] = {
            {1.0f, 1.0f, 1.0f, 1.0f, 0.4f, 6},  /* Core: White */
            {0.0f, 0.2f, 0.8f, 0.5f, 0.9f, 6},  /* Inner: Deep Blue (Sharp) */
            {0.0f, 0.1f, 0.6f, 0.4f, 1.4f, 6},  /* Middle: Dark Blue */
            {0.2f, 0.0f, 0.5f, 0.3f, 1.9f, 6},  /* Outer: Purple */
            {0.0f, 0.0f, 0.3f, 0.1f, 2.5f, 6}   /* Aura: Thin Blue */
        };

        /* Use NEW Glow Pipeline (Triangle + Additive Blending) */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
        for(int i=0; i<5; i++) {
            PushConstants gpc = {0};
            mat4 S_glow;
            float s = base_r * layers[i].scale;
            mat4_scale(S_glow, (vec3){s, s, s});
            mat4_multiply(S_glow, T, gpc.model); /* Anchored to T */
            
            gpc.color[0] = layers[i].r; gpc.color[1] = layers[i].g; gpc.color[2] = layers[i].b;
            gpc.color[3] = alpha * layers[i].a;
            gpc.usePushColor = layers[i].mode; 
            gpc.time = pulse;
            
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(gpc), &gpc);
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
            vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
        }
    }

    /* 2. Central Singularity (Clean Core) - Scaled by closing animation (Size doubled to 2.4) */
    float finalScale = 2.4f * tactScale * closingScale;
    mat4 M_base;
    mat4_scale(S_base, (vec3){finalScale, finalScale, finalScale}); 
    mat4_multiply(S_base, R, M_base);
    mat4_multiply(M_base, T, M_base);
    
    drawWormholeCore(cb, app, M_base, pulse, type);

    /* 3. Energy Funnels (Imbuti energetici del Wormhole) - Disegnati simmetricamente ai poli */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    mat4 RR; mat4_identity(RR);
    mat4_rotate(RR, pulse * 2.5f, (vec3){0, 0, 1}); 
    
    mat4 S_funnel;
    float fs = tactScale * closingScale;
    mat4_scale(S_funnel, (vec3){fs, fs, fs});

    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &app->whVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->whIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    for (int side = -1; side <= 1; side += 2) {
        if (side == 0) continue;
        PushConstants fpc = {0};
        if (type == 0) { fpc.color[0]=0.4f; fpc.color[1]=0.1f; fpc.color[2]=1.0f; }
        else { fpc.color[0]=1.0f; fpc.color[1]=0.85f; fpc.color[2]=0.3f; }
        fpc.color[3] = 1.0f; 
        fpc.usePushColor = (type == 1 && closingScale < 0.99f) ? 6 : 1; /* Hyper-Warp funnels too */
        fpc.time = pulse;

        mat4 R_side, T_offset;
        mat4_identity(R_side);
        if (side == -1) mat4_rotate(R_side, M_PI, (vec3){0, 1, 0});
        
        /* Offset funnels along local Z so they emerge from the sphere (Using the symmetry macro) */
        /* la seguente riga non deve essere modificata perche' interviene sul posizionamente della sfera all'interno del wormhole */
        mat4_translate(T_offset, (vec3){0, 0, 0});
        
        mat4_multiply(S_funnel, RR, fpc.model);
        mat4_multiply(fpc.model, R_side, fpc.model);
        mat4_multiply(fpc.model, T_offset, fpc.model);
        mat4_multiply(fpc.model, R, fpc.model);
        mat4_multiply(fpc.model, T, fpc.model);
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(fpc), &fpc);
        vkCmdDrawIndexed(cb, app->whIndexCount, 1, 0, 0, 0);
    }
}

/* Accretion Disk drawing moved to spacegl_vulkan_extras.inl as drawAccretionDisk_Vulkan */

void drawDiffuseNebula(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float pulse, float tactScale);
void drawDarkNebula(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float pulse, float tactScale);
void drawPlanetaryNebula(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float pulse, float tactScale);
void drawNebula(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, int subType, float pulse, float tactScale) {
    /* Volumetric Nebula Cloud (Size of ~4 Planets) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    
    /* Nebula Colors based on Type */
    float r=0.5f, g=0.5f, b=0.5f;
    if (subType == 0) { r=0.55f; g=0.72f; b=1.0f; }  /* Diffuse (cool blue-white) */
    else if (subType == 1) { r=1.0f; g=0.4f; b=0.2f; } /* High-Energy (Orange) */
    else if (subType == 2) { r=0.2f; g=0.0f; b=0.4f; } /* Dark Matter (Dark Purple) */
    else if (subType == 3) { r=0.0f; g=1.0f; b=0.8f; } /* Ionic (Cyan) */
    else if (subType == 4) { r=0.2f; g=1.0f; b=0.2f; } /* Gravimetric (Green) */
    else { r=0.9f; g=0.2f; b=0.5f; } /* Temporal (Pink) */

    if (subType == 0) {
        /* === DIFFUSE NEBULA: Volumetric FBM multi-layer rendering === */
        /* 8 overlapping spheres with varied sizes, offsets and hue tweaks */
        /* to create lumpy, deep, realistic cloud appearance              */
        static const float offsets[8][3] = {
            { 0.0f,  0.0f,  0.0f},  /* Core: large and bright */
            { 2.8f,  1.2f, -1.5f},
            {-2.2f,  2.5f,  1.0f},
            { 1.5f, -2.8f,  2.2f},
            {-1.8f, -1.5f, -2.6f},
            { 3.5f,  3.0f,  0.5f},
            {-3.0f,  0.8f,  3.2f},
            { 0.5f, -3.5f, -3.0f},
        };
        static const float scales[8]  = { 8.5f, 6.2f, 5.8f, 5.0f, 4.5f, 4.0f, 3.5f, 3.0f };
        static const float alphas[8]  = { 0.55f, 0.45f, 0.42f, 0.38f, 0.35f, 0.30f, 0.28f, 0.25f };
        /* Hue variation per layer: shift r/g/b slightly for subtle colour depth */
        static const float hue_r[8] = {0.55f, 0.62f, 0.48f, 0.70f, 0.50f, 0.80f, 0.45f, 0.65f};
        static const float hue_g[8] = {0.72f, 0.68f, 0.75f, 0.60f, 0.78f, 0.55f, 0.80f, 0.65f};
        static const float hue_b[8] = {1.00f, 0.95f, 1.00f, 0.85f, 0.90f, 0.75f, 0.95f, 0.80f};

        for (int i = 0; i < 8; i++) {
            PushConstants npc = {0};
            mat4 S_cloud, R_cloud, T_offset;

            /* Slow organic drift: each layer drifts at slightly different rate */
            float drift = pulse * 0.012f * (1.0f + i * 0.15f);
            float ox = offsets[i][0] * tactScale + sinf(drift + i * 1.3f) * 0.4f * tactScale;
            float oy = offsets[i][1] * tactScale + cosf(drift + i * 0.9f) * 0.3f * tactScale;
            float oz = offsets[i][2] * tactScale + sinf(drift + i * 1.7f) * 0.35f * tactScale;

            mat4_translate(T_offset, (vec3){ox, oy, oz});

            /* Each layer rotates on a different axis for irregular tumbling */
            mat4_identity(R_cloud);
            vec3 axis = {sinf(i * 1.1f), cosf(i * 0.7f), sinf(i * 1.9f + 1.0f)};
            float alen = sqrtf(axis[0]*axis[0] + axis[1]*axis[1] + axis[2]*axis[2]);
            if (alen > 0.001f) { axis[0] /= alen; axis[1] /= alen; axis[2] /= alen; }
            mat4_rotate(R_cloud, pulse * 0.004f * (i % 3 + 1), axis);

            /* Slow breathing: size pulses at different frequencies per layer */
            float breathe = 1.0f + 0.06f * sinf(pulse * 0.08f + i * 0.8f);
            float scale = scales[i] * breathe * tactScale;
            mat4_scale(S_cloud, (vec3){scale, scale * 0.88f, scale * 0.95f});

            mat4_multiply(S_cloud, R_cloud, npc.model);
            mat4_multiply(npc.model, T_offset, npc.model);
            mat4_multiply(npc.model, baseT, npc.model);

            npc.color[0] = hue_r[i];
            npc.color[1] = hue_g[i];
            npc.color[2] = hue_b[i];
            npc.color[3] = alphas[i];
            npc.usePushColor = 9; /* Volumetric Diffuse Nebula mode */
            npc.time = pulse;

            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(npc), &npc);
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
            vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
        }
    } else {
        /* === OTHER NEBULA TYPES: original 5-sphere Hyper-Warp glow === */
        for (int i=0; i<5; i++) {
            PushConstants npc = {0};
            mat4 S_cloud, R_cloud, T_offset;

            float offset_scale = 3.0f * tactScale;
            float ox = sinf(i * 2.0f + pulse * 0.1f) * offset_scale;
            float oy = cosf(i * 3.0f + pulse * 0.1f) * offset_scale;
            float oz = sinf(i * 4.0f + pulse * 0.1f) * offset_scale;

            mat4_translate(T_offset, (vec3){ox, oy, oz});

            mat4_identity(R_cloud);
            mat4_rotate(R_cloud, pulse * 0.05f * (i+1), (vec3){0, 1, 0});

            float scale = (7.0f + sinf(pulse * 0.5f + i)) * tactScale;
            mat4_scale(S_cloud, (vec3){scale, scale, scale});

            mat4_multiply(S_cloud, R_cloud, npc.model);
            mat4_multiply(npc.model, T_offset, npc.model);
            mat4_multiply(npc.model, baseT, npc.model);

            npc.color[0] = r; npc.color[1] = g; npc.color[2] = b;
            npc.color[3] = 0.6f;
            npc.usePushColor = 6; /* Hyper-Warp for cloudy texture */
            npc.time = pulse + i * 10.0f;

            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(npc), &npc);
            VkDeviceSize off = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
            vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
        }
    }

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawGalaxyMap(VkCommandBuffer cb, VulkanApp* app, GameState* st, float pulse) {
    /* Set up scaling for the map view */
    /* Map layout: 40x40x40. Each sector is drawn at gap 1.2 relative distance */
    float gap = 1.2f;
    float mapScale = app->mapAnim;
    float offset = -((float)GALAXY_SIZE * gap) / 2.0f;

    VkDeviceSize off = 0;

    /* 1. Global Galaxy Grid Frame */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    PushConstants pcGrid = {0}; mat4_identity(pcGrid.model);
    mat4 S_grid; mat4_scale(S_grid, (vec3){(float)GALAXY_SIZE * gap * mapScale, (float)GALAXY_SIZE * gap * mapScale, (float)GALAXY_SIZE * gap * mapScale});
    /* Assuming wireframe cube is unit size 1.0 at origin */
    memcpy(pcGrid.model, S_grid, sizeof(mat4));
    pcGrid.color[0]=0.4f; pcGrid.color[1]=0.4f; pcGrid.color[2]=1.0f; pcGrid.color[3]=0.8f; pcGrid.usePushColor=1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pcGrid), &pcGrid);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, sizeof(cubeIndices)/4, 1, 0, 0, 0);

    /* 2. Sectors Loop */
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    if (app->mapFilter == 0) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
        vkCmdBindIndexBuffer(cb, app->cubeIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    } else {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
        vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    }

    for (int z=1; z<=GALAXY_SIZE; z++) {
        for (int y=1; y<=GALAXY_SIZE; y++) {
            for (int x=1; x<=GALAXY_SIZE; x++) {
                int64_t val = app->shm->shm_galaxy[x][y][z];
                bool is_my_q = (x == st->shm_q[0] && y == st->shm_q[1] && z == st->shm_q[2]);
                if (val == 0 && !is_my_q) continue;

                /* Calculate position in map space */
                float px = (offset + (x - 0.5f) * gap) * mapScale;
                float py = (offset + (z - 0.5f) * gap) * mapScale;
                float pz = (offset + ((float)GALAXY_SIZE + 0.5f - y) * gap) * mapScale;

                /* Decode object types */
                int64_t uval = (val < 0) ? -val : val;
                int quasar   = (uval / 100000000000000000LL) % 10;
                int monster  = (uval / 10000000000000000LL) % 10;
                int player   = (uval / 1000000000000000LL) % 10;
                int rift     = (uval / 100000000000000LL) % 10;
                int platform = (uval / 10000000000000LL) % 10;
                int buoy     = (uval / 1000000000000LL) % 10;
                int mine     = (uval / 100000000000LL) % 10;
                int derelict = (uval / 10000000000LL) % 10;
                int asteroid = (uval / 1000000000LL) % 10;
                int comet = (uval / 100000000LL) % 10;
                int storm = (uval / 10000000LL) % 10;
                int pul   = (uval / 1000000LL) % 10;
                int neb   = (uval / 100000LL) % 10;
                int bh    = (uval / 10000LL) % 10;
                int pl    = (uval / 1000LL) % 10;
                int en    = (uval / 100LL) % 10;
                int bs    = (uval / 10LL) % 10;
                int sst   = (uval) % 10;

                /* Filter Logic */
                if (app->mapFilter > 0) {
                    bool match = false;
                    switch(app->mapFilter) {
                        case 1: if(sst>0) match=true; break;
                        case 2: if(pl>0) match=true; break;
                        case 3: if(bs>0) match=true; break;
                        case 4: if(en>0 || player>0) match=true; break;
                        case 5: if(bh>0) match=true; break;
                        case 6: if(neb>0) match=true; break;
                        case 7: if(pul>0) match=true; break;
                        case 8: if(storm>0) match=true; break;
                        case 9: if(comet>0) match=true; break;
                        case 10: if(asteroid>0) match=true; break;
                        case 11: if(derelict>0) match=true; break;
                        case 12: if(mine>0) match=true; break;
                        case 13: if(buoy>0) match=true; break;
                        case 14: if(platform>0) match=true; break;
                        case 15: if(rift>0) match=true; break;
                        case 16: if(monster>0) match=true; break;
                        case 17: if(quasar>0) match=true; break;
                    }
                    if (!match && !is_my_q) continue;
                }

                PushConstants pcSec = {0}; mat4_identity(pcSec.model);
                mat4 T_sec, S_sec;
                mat4_translate(T_sec, (vec3){px, py, pz});
                
                /* Determine color */
                int ef = app->mapFilter;
                if (ef == 17 || (ef == 0 && quasar > 0)) { pcSec.color[0]=1.0f; pcSec.color[1]=0.0f; pcSec.color[2]=1.0f; }
                else if (ef == 16 || (ef == 0 && monster > 0)) { pcSec.color[0]=1.0f; pcSec.color[1]=1.0f; pcSec.color[2]=1.0f; }
                else if (ef == 15 || (ef == 0 && rift > 0)) { pcSec.color[0]=0.0f; pcSec.color[1]=1.0f; pcSec.color[2]=1.0f; }
                else if (ef == 14 || (ef == 0 && platform > 0)) { pcSec.color[0]=0.8f; pcSec.color[1]=0.4f; pcSec.color[2]=0.0f; }
                else if (ef == 13 || (ef == 0 && buoy > 0)) { pcSec.color[0]=0.0f; pcSec.color[1]=0.5f; pcSec.color[2]=1.0f; }
                else if (ef == 12 || (ef == 0 && mine > 0)) { pcSec.color[0]=1.0f; pcSec.color[1]=0.0f; pcSec.color[2]=0.0f; }
                else if (ef == 11 || (ef == 0 && derelict > 0)) { pcSec.color[0]=0.3f; pcSec.color[1]=0.3f; pcSec.color[2]=0.3f; }
                else if (ef == 10 || (ef == 0 && asteroid > 0)) { pcSec.color[0]=0.5f; pcSec.color[1]=0.3f; pcSec.color[2]=0.1f; }
                else if (ef == 9 || (ef == 0 && comet > 0)) { pcSec.color[0]=0.5f; pcSec.color[1]=0.8f; pcSec.color[2]=1.0f; }
                else if (ef == 8 || (ef == 0 && storm > 0)) { pcSec.color[0]=1.0f; pcSec.color[1]=1.0f; pcSec.color[2]=1.0f; }
                else if (ef == 7 || (ef == 0 && pul > 0)) { pcSec.color[0]=1.0f; pcSec.color[1]=0.5f; pcSec.color[2]=0.0f; }
                else if (ef == 6 || (ef == 0 && neb > 0)) { pcSec.color[0]=0.7f; pcSec.color[1]=0.7f; pcSec.color[2]=0.7f; }
                else if (ef == 5 || (ef == 0 && bh > 0)) { pcSec.color[0]=0.6f; pcSec.color[1]=0.0f; pcSec.color[2]=1.0f; }
                else if (ef == 4 || (ef == 0 && (en > 0 || player > 0))) { pcSec.color[0]=1.0f; pcSec.color[1]=0.0f; pcSec.color[2]=0.0f; }
                else if (ef == 3 || (ef == 0 && bs > 0)) { pcSec.color[0]=0.0f; pcSec.color[1]=1.0f; pcSec.color[2]=0.0f; }
                else if (ef == 2 || (ef == 0 && pl > 0)) { pcSec.color[0]=0.0f; pcSec.color[1]=0.8f; pcSec.color[2]=1.0f; }
                else if (ef == 1 || (ef == 0 && sst > 0)) { pcSec.color[0]=1.0f; pcSec.color[1]=1.0f; pcSec.color[2]=0.0f; }
                else { pcSec.color[0]=0.4f; pcSec.color[1]=0.4f; pcSec.color[2]=0.4f; }
                pcSec.color[3] = 1.0f;

                float scale = 0.15f;
                if (quasar > 0) scale = 0.2f + sinf(pulse*15.0f)*0.08f;
                else if (pul > 0) scale += sinf(pulse*8.0f)*0.05f;
                else if (monster > 0) scale = 0.25f + sinf(pulse*5.0f)*0.05f;
                
                if (is_my_q) {
                    /* Special highlight for player quadrant */
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
                    vkCmdBindIndexBuffer(cb, app->cubeIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    PushConstants pcMe = {0}; mat4_identity(pcMe.model);
                    float meScale = 0.4f + sinf(pulse*6.0f)*0.15f;
                    mat4 S_me; mat4_scale(S_me, (vec3){meScale*mapScale, meScale*mapScale, meScale*mapScale});
                    mat4_multiply(S_me, T_sec, pcMe.model);
                    pcMe.color[0]=1.0f; pcMe.color[1]=1.0f; pcMe.color[2]=1.0f; pcMe.color[3]=0.8f; pcMe.usePushColor=1;
                    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pcMe), &pcMe);
                    vkCmdDrawIndexed(cb, 24, 1, 0, 0, 0);
                    
                    /* Restore pipeline for sector marker */
                    if (app->mapFilter == 0) {
                        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
                        vkCmdBindIndexBuffer(cb, app->cubeIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    } else {
                        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
                        vkCmdBindIndexBuffer(cb, app->cubeSolidIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    }

                    /* Also draw the marker if there's an object */
                    if (val != 0) {
                        mat4_scale(S_sec, (vec3){scale*mapScale, scale*mapScale, scale*mapScale});
                        mat4_multiply(S_sec, T_sec, pcSec.model);
                        pcSec.usePushColor = (app->mapFilter == 0 ? 1 : 5);
                        pcSec.metallic = 0.5f; pcSec.roughness = 0.5f;
                        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pcSec), &pcSec);
                        vkCmdDrawIndexed(cb, (app->mapFilter == 0 ? 24 : 36), 1, 0, 0, 0);
                    }
                } else if (val != 0) {
                    mat4_scale(S_sec, (vec3){scale*mapScale, scale*mapScale, scale*mapScale});
                    mat4_multiply(S_sec, T_sec, pcSec.model);
                    pcSec.usePushColor = (app->mapFilter == 0 ? 1 : 5);
                    pcSec.metallic = 0.5f; pcSec.roughness = 0.5f;
                    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pcSec), &pcSec);
                    vkCmdDrawIndexed(cb, (app->mapFilter == 0 ? 24 : 36), 1, 0, 0, 0);
                }
            }
        }
    }
}

void createColorResources(VulkanApp* app) {
    createImage(app, app->swapChainExtent.width, app->swapChainExtent.height, 1, app->msaaSamples, app->swapChainImageFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &app->colorImage, &app->colorImageMemory);
    app->colorImageView = createImageView(app->device, app->colorImage, app->swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

void createDepthResources(VulkanApp* app) {
    createImage(app, app->swapChainExtent.width, app->swapChainExtent.height, 1, app->msaaSamples, VK_FORMAT_D32_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &app->depthImage, &app->depthImageMemory);
    app->depthImageView = createImageView(app->device, app->depthImage, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
}

void createInstance(VulkanApp* app) {
    VkApplicationInfo aInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "SpaceGL", 1, "NoEngine", 1, VK_API_VERSION_1_0};
    uint32_t glfwExtCount = 0; const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    VkInstanceCreateInfo cInf = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &aInfo, 0, NULL, glfwExtCount, glfwExts};
    if (vkCreateInstance(&cInf, NULL, &app->instance) != VK_SUCCESS) exit(1);
}

VkSampleCountFlagBits getMaxUsableSampleCount(VkPhysicalDevice pDevice) {
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(pDevice, &props);
    VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT) return VK_SAMPLE_COUNT_64_BIT;
    if (counts & VK_SAMPLE_COUNT_32_BIT) return VK_SAMPLE_COUNT_32_BIT;
    if (counts & VK_SAMPLE_COUNT_16_BIT) return VK_SAMPLE_COUNT_16_BIT;
    if (counts & VK_SAMPLE_COUNT_8_BIT) return VK_SAMPLE_COUNT_8_BIT;
    if (counts & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
    if (counts & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

void pickPhysicalDevice(VulkanApp* app) {
    uint32_t count = 0; vkEnumeratePhysicalDevices(app->instance, &count, NULL);
    VkPhysicalDevice* devs = malloc(sizeof(VkPhysicalDevice)*count); vkEnumeratePhysicalDevices(app->instance, &count, devs);
    app->physicalDevice = devs[0]; app->msaaSamples = getMaxUsableSampleCount(app->physicalDevice); free(devs);
}

void createLogicalDevice(VulkanApp* app) {
    float prio = 1.0f; VkDeviceQueueCreateInfo qInfo = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, 0, 1, &prio};
    const char* ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkPhysicalDeviceFeatures feat = {0}; VkDeviceCreateInfo cInfo = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qInfo, 0, NULL, 1, &ext, &feat};
    if (vkCreateDevice(app->physicalDevice, &cInfo, NULL, &app->device) != VK_SUCCESS) exit(1);
    vkGetDeviceQueue(app->device, 0, 0, &app->graphicsQueue);
}

void createSwapChain(VulkanApp* app) {
    app->swapChainImageFormat = VK_FORMAT_B8G8R8A8_UNORM; app->swapChainExtent = (VkExtent2D){WIDTH, HEIGHT};
    
    uint32_t modeCount; vkGetPhysicalDeviceSurfacePresentModesKHR(app->physicalDevice, app->surface, &modeCount, NULL);
    VkPresentModeKHR* modes = malloc(sizeof(VkPresentModeKHR) * modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(app->physicalDevice, app->surface, &modeCount, modes);
    
    VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < modeCount; i++) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) { bestMode = modes[i]; break; }
        if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) bestMode = modes[i];
    }
    free(modes);

    VkSwapchainCreateInfoKHR cInfo = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, NULL, 0, app->surface, 2, app->swapChainImageFormat, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, app->swapChainExtent, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, bestMode, VK_TRUE, NULL};
    if (vkCreateSwapchainKHR(app->device, &cInfo, NULL, &app->swapChain) != VK_SUCCESS) exit(1);
    vkGetSwapchainImagesKHR(app->device, app->swapChain, &app->swapChainImageCount, NULL);
    app->swapChainImages = malloc(sizeof(VkImage)*app->swapChainImageCount);
    vkGetSwapchainImagesKHR(app->device, app->swapChain, &app->swapChainImageCount, app->swapChainImages);
}

void createRenderPass(VulkanApp* app) {
    VkAttachmentDescription cAtt = {0, app->swapChainImageFormat, app->msaaSamples, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentDescription dAtt = {0, VK_FORMAT_D32_SFLOAT, app->msaaSamples, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentDescription rAtt = {0, app->swapChainImageFormat, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    VkAttachmentReference cRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, dRef = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}, rRef = {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, NULL, 1, &cRef, &rRef, &dRef, 0, NULL};
    VkSubpassDependency dep = {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0};
    VkAttachmentDescription atts[] = {cAtt, dAtt, rAtt};
    VkRenderPassCreateInfo rInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, NULL, 0, 3, atts, 1, &sub, 1, &dep};
    if (vkCreateRenderPass(app->device, &rInfo, NULL, &app->renderPass) != VK_SUCCESS) exit(1);
}

VkShaderModule createShaderModule(VkDevice device, const char* code, size_t codeSize) {
    VkShaderModuleCreateInfo cInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, codeSize, (const uint32_t*)code};
    VkShaderModule sModule; if (vkCreateShaderModule(device, &cInfo, NULL, &sModule) != VK_SUCCESS) exit(1);
    return sModule;
}

void createGraphicsPipeline(VulkanApp* app) {
    resolve_shader_paths();
    size_t vSz, fSize; 
    char* vC = readShaderFile(vertex_shader_path, &vSz); 
    char* fC = readShaderFile(fragment_shader_path, &fSize);
    
    if (!vC || !fC) {
        exit(1);
    }
    VkShaderModule vM = createShaderModule(app->device, vC, vSz), fM = createShaderModule(app->device, fC, fSize); 
    free(vC); free(fC);
    VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vM, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fM, "main", NULL}
    };
    VkVertexInputBindingDescription bnd = {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription atr[] = {{0,0,VK_FORMAT_R32G32B32_SFLOAT,0}, {1,0,VK_FORMAT_R32G32B32_SFLOAT,12}, {2,0,VK_FORMAT_R32G32B32_SFLOAT,24}};
    VkPipelineVertexInputStateCreateInfo vIn = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,NULL,0,1,&bnd,3,atr};
    VkPipelineInputAssemblyStateCreateInfo iAs = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,NULL,0,VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,VK_FALSE};
    VkViewport vp = {0,0,(float)WIDTH,(float)HEIGHT,0,1}; VkRect2D sc = {{0,0},app->swapChainExtent};
    VkPipelineViewportStateCreateInfo vpSt = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,NULL,0,1,&vp,1,&sc};
    VkPipelineRasterizationStateCreateInfo rast = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,NULL,0,VK_FALSE,VK_FALSE,VK_POLYGON_MODE_FILL,VK_CULL_MODE_BACK_BIT,VK_FRONT_FACE_CLOCKWISE,VK_FALSE,0,0,0,1.0f};
    VkPipelineMultisampleStateCreateInfo mult = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,NULL,0,app->msaaSamples,VK_TRUE,0.25f,NULL,VK_FALSE,VK_FALSE};
    VkPipelineDepthStencilStateCreateInfo depSt = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,NULL,0,VK_TRUE,VK_TRUE,VK_COMPARE_OP_LESS,VK_FALSE,VK_FALSE, {0}, {0}, 0, 0};
    VkPipelineColorBlendAttachmentState cBlAt = {VK_FALSE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cBl = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &cBlAt, {0,0,0,0}};
    VkDescriptorSetLayoutBinding uboBind = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo dl = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,NULL,0,1,&uboBind};
    if (vkCreateDescriptorSetLayout(app->device, &dl, NULL, &app->descriptorSetLayout) != VK_SUCCESS) exit(1);
    /* Push Constants must now be visible to both Vertex AND Fragment shaders for the point bloom effect */
    VkPushConstantRange pCr = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo lInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &app->descriptorSetLayout, 1, &pCr};
    if (vkCreatePipelineLayout(app->device, &lInfo, NULL, &app->pipelineLayout) != VK_SUCCESS) exit(1);
    
    VkGraphicsPipelineCreateInfo pInfo = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, NULL, 0, 2, stages, &vIn, &iAs, NULL, &vpSt, &rast, &mult, &depSt, &cBl, NULL, app->pipelineLayout, app->renderPass, 0, VK_NULL_HANDLE, 0};
    if (vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &pInfo, NULL, &app->graphicsPipeline) != VK_SUCCESS) exit(1);
    
    /* 2. Wireframe/Line Pipeline with Blending Enabled (for Halo Effect) */
    iAs.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; rast.polygonMode = VK_POLYGON_MODE_FILL; rast.cullMode = 0;
    cBlAt.blendEnable = VK_TRUE; 
    cBlAt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; 
    cBlAt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    if (vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &pInfo, NULL, &app->wireframePipeline) != VK_SUCCESS) exit(1);
    
    /* 3. Point Sprite Pipeline (for Bloom Effect) */
    iAs.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    cBlAt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cBlAt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE; /* Additive for glow */
    if (vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &pInfo, NULL, &app->pointPipeline) != VK_SUCCESS) exit(1);

    /* 4. Glow Pipeline (Additive Triangle List for Volumetric Spheres) */
    iAs.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    cBlAt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cBlAt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE; /* Additive */
    depSt.depthWriteEnable = VK_FALSE; /* Disable depth write for transparent glows */
    if (vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &pInfo, NULL, &app->glowPipeline) != VK_SUCCESS) exit(1);

    /* 5. Alpha Pipeline (Normal Transparency for Dark Clouds/Bok Globules) */
    cBlAt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; /* Alpha Blend */
    if (vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &pInfo, NULL, &app->alphaPipeline) != VK_SUCCESS) exit(1);

    vkDestroyShaderModule(app->device, fM, NULL); vkDestroyShaderModule(app->device, vM, NULL);
}

void drawShieldGlow(VkCommandBuffer cb, VulkanApp* app, mat4 modelBase, float radius, float r, float g, float b, float alpha, float pulse) {
    VkDeviceSize off = 0;
    /* Increased layers and radius for far-distance visibility */
    for (int i = 1; i <= 4; i++) {
        float s = radius * (1.2f + i * 0.4f);
        PushConstants pc = {0};
        mat4 S; mat4_scale(S, (vec3){s, s, s});
        mat4_multiply(S, modelBase, pc.model);
        pc.color[0] = r; pc.color[1] = g; pc.color[2] = b; pc.color[3] = alpha / (i * 1.2f);
        pc.usePushColor = 7; pc.time = pulse;
        pc.metallic = pulse * 10.0f; /* Stabilize wave for volumetric glow too */
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }
}

void drawShieldEffect(VkCommandBuffer cb, VulkanApp* app, float pulse, float tactScale, mat4 R_ship, mat4 T_ship) {
    /* Shield Hit Detection & Synchronization (Standardized across engines) */
    static int lastShieldsValHit[6] = {0};
    static bool shieldsInit = false;
    GameState* st = &app->shm->buffers[app->shm->read_index];

    if (!shieldsInit && st->object_count > 0 && st->objects[0].active) {
        for (int s = 0; s < 6; s++) lastShieldsValHit[s] = st->shm_shields[s];
        shieldsInit = true;
    }

    if (shieldsInit) {
        for (int s = 0; s < 6; s++) {
            if (st->shm_shields[s] < lastShieldsValHit[s]) {
                app->shieldHitTimers[s] = (int)GAME_TICK_RATE; 
            }
            lastShieldsValHit[s] = st->shm_shields[s];
        }
    }

    bool any_hit = false;
    for (int s = 0; s < 6; s++) if (app->shieldHitTimers[s] > 0) any_hit = true;
    if (!any_hit) return;

    struct { float rx, ry; } shield_rot[] = {
        {0, 0},           /* 0: Front (+X) */
        {0, M_PI},        /* 1: Rear (-X) */
        {-M_PI/2.0f, 0},  /* 2: Top (+Y) */
        {M_PI/2.0f, 0},   /* 3: Bottom (-Y) */
        {0, M_PI/2.0f},   /* 4: Left (+Z) */
        {0, -M_PI/2.0f}   /* 5: Right (-Z) */
    };

    for (int s = 0; s < 6; s++) {
        if (app->shieldHitTimers[s] <= 0) continue;

        float t = app->shieldHitTimers[s] / 80.0f;
        float alpha = (t > 0.5f) ? 1.0f : t * 2.0f;
        /* Larger scale for visibility */
        float scale = (1.2f + (1.0f - t) * 0.3f) * tactScale;

        mat4 R_sec, T_sec, S_sec, M_sec;
        mat4_identity(R_sec);
        mat4_rotate(R_sec, shield_rot[s].ry, (vec3){0, 1, 0});
        mat4_rotate(R_sec, shield_rot[s].rx, (vec3){0, 0, 1}); 

        /* Standardized thickness for all sectors (0.5f) to match 3DView feel */
        float thk = 0.5f;
        mat4_scale(S_sec, (vec3){thk * scale, 1.8f * scale, 1.8f * scale});
        mat4_translate(T_sec, (vec3){1.45f * scale, 0, 0});

        mat4_multiply(S_sec, T_sec, M_sec);
        mat4_multiply(M_sec, R_sec, M_sec);
        mat4_multiply(M_sec, R_ship, M_sec);
        mat4_multiply(M_sec, T_ship, M_sec);

        /* 1. Shield Energy Grid (Solid layer for visibility in Vulkan) */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
        PushConstants wpc = {0};
        memcpy(wpc.model, M_sec, sizeof(mat4));
        wpc.color[0] = 0.0f; wpc.color[1] = 0.8f; wpc.color[2] = 1.0f; wpc.color[3] = alpha * 0.8f;
        wpc.usePushColor = 5; /* Standard color mode */
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(wpc), &wpc);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

        /* 2. Shield Surface Glow (Solid but transparent) */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
        PushConstants pc = {0};
        memcpy(pc.model, M_sec, sizeof(mat4));
        pc.color[0] = 0.0f; pc.color[1] = 0.6f; pc.color[2] = 1.0f; pc.color[3] = alpha * 0.6f;
        pc.usePushColor = 7; pc.time = pulse;
        pc.metallic = pulse * 15.0f; 
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

        /* 3. Outer Volumetric Glow Effect */
        drawShieldGlow(cb, app, M_sec, 0.7f * scale, 0.0f, 0.5f, 1.0f, alpha * 0.9f, pulse);
    }
}

void recordCommandBuffer(VkCommandBuffer cb, uint32_t idx, VulkanApp* app) {
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,NULL,0,NULL}; vkBeginCommandBuffer(cb, &bi);
    VkClearValue cl[2] = {0}; cl[0].color = (VkClearColorValue){{0,0,0,1}}; cl[1].depthStencil = (VkClearDepthStencilValue){1,0};
    VkRenderPassBeginInfo ri = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,NULL,app->renderPass,app->swapChainFramebuffers[idx],{{0,0},app->swapChainExtent}, 2, cl};
    vkCmdBeginRenderPass(cb, &ri, VK_SUBPASS_CONTENTS_INLINE);
    
    VkDeviceSize off = 0;
    
    /* --- WIREFRAME PASS --- */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->pipelineLayout, 0, 1, &app->descriptorSets[app->currentFrame], 0, NULL);
    
    if (app->shm) {
        int r = atomic_load(&app->shm->read_index); GameState* st = &app->shm->buffers[r];
        float pulse = (float)glfwGetTime();

        /* --- MAP MODE RENDER --- */
        if (app->mapAnim > 0.01f) {
            drawGalaxyMap(cb, app, st, pulse);
        }

        /* --- TACTICAL VIEW RENDER --- */
        if (app->mapAnim < 0.99f) {
            float tactScale = 1.0f - app->mapAnim;
            /* Ensure we are back in wireframe mode for tactical frame */
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
            mat4_identity(app->playerR); mat4_identity(app->playerT);

            /* 0. Cubo Tattico (Bounding Box) */
            PushConstants pc = {0}; mat4_identity(pc.model); pc.usePushColor = 4; /* Unlit Vertex Colors */
            mat4 S_tact; mat4_scale(S_tact, (vec3){tactScale, tactScale, tactScale});
            memcpy(pc.model, S_tact, sizeof(mat4));
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdBindVertexBuffers(cb, 0, 1, &app->vertexBuffer, &off);
            vkCmdBindIndexBuffer(cb, app->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, sizeof(indices)/4, 1, 0, 0, 0);

            if (st->shm_show_axes && st->object_count > 0 && !app->smoothObjs[0].first && app->cameraDist < 150.0f && !app->jumpArrival.active) {
            /* Zero-Lag AR Compass: Anchored to Smoothed Player Coordinates for fluid motion */
            float ox = app->smoothObjs[0].x;
            float oy = app->smoothObjs[0].y;
            float oz = app->smoothObjs[0].z;
            float oh = app->smoothObjs[0].h;
            float om = app->smoothObjs[0].m;
            float oro = app->smoothObjs[0].r;

            /* Correct Mapping: X_v = X_s - 20, Y_v = oz - 20, Z_v = 20 - oy */
            vec3 p = {(ox - 20.0f) * tactScale, (oz - 20.0f) * tactScale, (20.0f - oy) * tactScale};
            mat4 T_axes; mat4_translate(T_axes, p);
            
            /* 1. Global Axes (Fixed to World Orientation) */
            PushConstants apc = {0}; mat4_identity(apc.model); 
            mat4 S_axes_t; mat4_scale(S_axes_t, (vec3){tactScale, tactScale, tactScale});
            mat4_multiply(S_axes_t, T_axes, apc.model);
            apc.usePushColor = 4; /* Mode 4: Unlit Vertex Colors */
            apc.color[0]=1; apc.color[1]=1; apc.color[2]=1; apc.color[3]=1; 
            
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(apc), &apc);
            vkCmdBindVertexBuffers(cb, 0, 1, &app->axesVertexBuffer, &off);
            vkCmdBindIndexBuffer(cb, app->axesIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, 6, 1, 0, 0, 0);

            /* 2. Fixed Compass (White Horizontal Ring - World Orientation) */
            PushConstants cpc = {0}; mat4_identity(cpc.model);
            mat4 S_fixed; mat4_scale(S_fixed, (vec3){3.0f * tactScale, 3.0f * tactScale, 3.0f * tactScale}); 
            mat4_multiply(S_fixed, T_axes, cpc.model);
            cpc.color[0]=1.0f; cpc.color[1]=1.0f; cpc.color[2]=1.0f; cpc.color[3]=1.0f; cpc.usePushColor=1;
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(cpc), &cpc);
            vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
            vkCmdBindIndexBuffer(cb, app->circleIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, CIRCLE_SEGMENTS * 2, 1, 0, 0, 0);

            /* 3. Heading Ring (Cyan Ring - Level with Pitch) */
            PushConstants hpc = {0}; mat4_identity(hpc.model);
            mat4 R_pitch_h, S_heading; mat4_identity(R_pitch_h); mat4_scale(S_heading, (vec3){2.5f * tactScale, 2.5f * tactScale, 2.5f * tactScale});
            /* Dynamic pitch rotation around the heading-dependent transverse axis */
            double h_rad = oh * M_PI / 180.0;
            mat4_rotate(R_pitch_h, om * M_PI / 180.0f, (vec3){cosf(h_rad), 0, -sinf(h_rad)});
            /* Order: Scale * Rotate * Translate (in Row-Major C logic) -> T * R * S (in Shader Col-Major) */
            mat4_multiply(S_heading, R_pitch_h, hpc.model); 
            mat4_multiply(hpc.model, T_axes, hpc.model);
            hpc.color[0]=0.0f; hpc.color[1]=1.0f; hpc.color[2]=1.0f; hpc.color[3]=1.0f; hpc.usePushColor=1;
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(hpc), &hpc);
            vkCmdDrawIndexed(cb, CIRCLE_SEGMENTS * 2, 1, 0, 0, 0);

            /* 4. Mark Arc (Yellow Vertical Arc - Aligned with Ship Heading) */
            PushConstants mpc = {0}; mat4_identity(mpc.model);
            mat4 R_head_m, S_mark; 
            mat4_identity(R_head_m);
            mat4_scale(S_mark, (vec3){2.8f * tactScale, 2.8f * tactScale, 2.8f * tactScale});
            
            /* 1. Base alignment (+90) then Heading (-oh) around vertical Y */
            mat4_rotate(R_head_m, (90.0f - oh) * M_PI / 180.0f, (vec3){0, 1, 0});
            
            /* Combine: Scale * Rotation * Translation */
            mat4_multiply(S_mark, R_head_m, mpc.model);
            mat4_multiply(mpc.model, T_axes, mpc.model);
            
            mpc.color[0]=1.0f; mpc.color[1]=1.0f; mpc.color[2]=0.0f; mpc.color[3]=1.0f; mpc.usePushColor=1;
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(mpc), &mpc);
            vkCmdBindVertexBuffers(cb, 0, 1, &app->arcVertexBuffer, &off);
            vkCmdBindIndexBuffer(cb, app->arcIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, (ARC_SEGMENTS-1) * 2, 1, 0, 0, 0);

            /* 4.1 Roll Circle (Yellow Transversal Circle - Perpendicular to nose) */
            PushConstants rpc = {0}; mat4_identity(rpc.model);
            mat4 R_roll_c, S_roll; mat4_identity(R_roll_c);
            mat4_scale(S_roll, (vec3){2.8f * tactScale, 2.8f * tactScale, 2.8f * tactScale});

            /* 1. Base alignment (+90) then Heading (-oh) around vertical Y */
            mat4_rotate(R_roll_c, (90.0f - oh) * M_PI / 180.0f, (vec3){0, 1, 0});
            /* 2. Pitch around dynamic transverse axis (to stay perpendicular to the vector) */
            double h_rad_r = oh * M_PI / 180.0;
            mat4_rotate(R_roll_c, om * M_PI / 180.0f, (vec3){cosf(h_rad_r), 0, -sinf(h_rad_r)});

            mat4_multiply(S_roll, R_roll_c, rpc.model);
            mat4_multiply(rpc.model, T_axes, rpc.model);

            rpc.color[0]=1.0f; rpc.color[1]=1.0f; rpc.color[2]=0.0f; rpc.color[3]=1.0f; rpc.usePushColor=1;
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(rpc), &rpc);
            vkCmdBindVertexBuffers(cb, 0, 1, &app->rollCircleVertexBuffer, &off);
            vkCmdBindIndexBuffer(cb, app->rollCircleIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, CIRCLE_SEGMENTS * 2, 1, 0, 0, 0);

            /* 5. Directional Vector (Green Arrow - Ship Orientation) - Hidden during Jump */
            if (!app->jumpArrival.active) {
                PushConstants vpc = {0}; mat4_identity(vpc.model);
                mat4 R_v, S_v; mat4_identity(R_v); mat4_scale(S_v, (vec3){tactScale, tactScale, tactScale});
                /* 1. Base alignment: model is +X, we want it to point at +Z (North) */
                mat4_rotate(R_v, 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});
                /* 2. Heading (around vertical Y) */
                mat4_rotate(R_v, -oh * M_PI / 180.0f, (vec3){0, 1, 0});
                /* 3. Pitch (around the dynamic horizontal axis, nose up = positive elevation) */
                double h_rad_v = oh * M_PI / 180.0f;
                mat4_rotate(R_v, om * M_PI / 180.0f, (vec3){cosf(h_rad_v), 0, -sinf(h_rad_v)});
                
                mat4_multiply(S_v, R_v, vpc.model);
                mat4_multiply(vpc.model, T_axes, vpc.model);
                
                vpc.usePushColor = 4; /* Mode 4: Unlit Vertex Colors (Green) */
                vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(vpc), &vpc);
                vkCmdBindVertexBuffers(cb, 0, 1, &app->vectorVertexBuffer, &off);
                vkCmdBindIndexBuffer(cb, app->vectorIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cb, 18, 1, 0, 0, 0);

                /* 5.1 Top Vector (Blue Arrow - Ship Top orientation) */
                PushConstants upc = {0}; mat4_identity(upc.model);
                mat4 R_u, S_u; mat4_identity(R_u); 
                /* Scale is half of the Heading vector (0.5 * tactScale) */
                mat4_scale(S_u, (vec3){0.5f * tactScale, 0.5f * tactScale, 0.5f * tactScale});
                
                /* 1. FIRST: Rotate model (+X) to point Up (+Y local) */
                /* Using -90 to point towards the roof in the Vulkan coordinate mapping */
                mat4_rotate(R_u, -90.0f * M_PI / 180.0f, (vec3){0, 0, 1});

                /* 2. Apply exactly the same rotations as the ship to stay synced */
                /* Base alignment (+90 North) then Heading (-oh) around vertical Y */
                mat4_rotate(R_u, (90.0f - oh) * M_PI / 180.0f, (vec3){0, 1, 0});
                
                /* Pitch around dynamic transverse axis */
                double h_rad_u = oh * M_PI / 180.0;
                mat4_rotate(R_u, om * M_PI / 180.0f, (vec3){cosf(h_rad_u), 0, -sinf(h_rad_u)});
                
                /* Roll around dynamic longitudinal axis (Nose direction) */
                float pitch_rad_u = om * M_PI / 180.0f;
                vec3 nose_u = {sinf(h_rad_u)*cosf(pitch_rad_u), sinf(pitch_rad_u), cosf(h_rad_u)*cosf(pitch_rad_u)};
                mat4_rotate(R_u, oro * M_PI / 180.0f, nose_u);

                mat4_multiply(S_u, R_u, upc.model);
                mat4_multiply(upc.model, T_axes, upc.model);
                
                upc.color[0]=0.0f; upc.color[1]=0.5f; upc.color[2]=1.0f; upc.color[3]=1.0f; upc.usePushColor=1;
                vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(upc), &upc);
                vkCmdDrawIndexed(cb, 18, 1, 0, 0, 0);
            }
        }

        /* Tactical Grid (grd) - Hidden during Jump */
        if (st->shm_show_grid && app->cameraDist < 150.0f && !app->jumpArrival.active) {
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
            PushConstants gpc = {0}; mat4_identity(gpc.model);
            mat4 S_grid_t; mat4_scale(S_grid_t, (vec3){tactScale, tactScale, tactScale});
            memcpy(gpc.model, S_grid_t, sizeof(mat4));
            gpc.color[0] = 0.0f; gpc.color[1] = 1.0f; gpc.color[2] = 0.0f; gpc.color[3] = 1.0f;
            gpc.usePushColor = 1; 
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(gpc), &gpc);
            vkCmdBindVertexBuffers(cb, 0, 1, &app->gridVertexBuffer, &off);
            vkCmdBindIndexBuffer(cb, app->gridIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, (uint32_t)app->gridVertexCount, 1, 0, 0, 0);
        }

        /* Departure/Arrival Wormholes (Internal logic handles pipeline switching) */
        /* Departure wormhole: shown during entry phase, independent of arrival timer */
        if (app->departureWormhole.active) {
            drawWormhole(cb, app, app->departureWormhole.x * tactScale, app->departureWormhole.y * tactScale, app->departureWormhole.z * tactScale, 
                         app->departureWormhole.h, app->departureWormhole.m, 0, pulse, tactScale, 1.0f);
        }
        /* Arrival wormhole: shown after jump, fades out over timer */
        if (app->jumpArrival.active && app->jumpArrival.timer > 0) {
            /* Keep arrival wormhole centered on ship if ship is active */
            if (st->object_count > 0 && st->objects[0].active) {
                app->jumpArrival.x = app->smoothObjs[0].x - 20.0f;
                app->jumpArrival.y = app->smoothObjs[0].z - 20.0f;
                app->jumpArrival.z = 20.0f - app->smoothObjs[0].y;
            }
            float closingScale = (app->jumpArrival.timer < 540) ? ((float)app->jumpArrival.timer / 540.0f) : 1.0f;
            drawWormhole(cb, app, app->jumpArrival.x * tactScale, app->jumpArrival.y * tactScale, app->jumpArrival.z * tactScale, 
                         app->jumpArrival.h, app->jumpArrival.m, 1, pulse, tactScale, closingScale);
        }

        /* --- SOLID PASS --- */
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->pipelineLayout, 0, 1, &app->descriptorSets[app->currentFrame], 0, NULL);

        /* Starfield (Static Background Mesh) */
        PushConstants stpc = {0}; mat4_identity(stpc.model);
        stpc.time = pulse;
#if STARFIELD_MODE == STARFIELD_MODE_BLOOM
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->pointPipeline);
        stpc.usePushColor = 3; 
#elif STARFIELD_MODE == STARFIELD_MODE_HALO
        stpc.usePushColor = 4; /* New Mode for Halo (Unlit Vertex Color + Alpha) */
#elif STARFIELD_MODE == STARFIELD_MODE_TWINKLE
        stpc.usePushColor = 2; 
#else
        stpc.usePushColor = 0; 
#endif
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(stpc), &stpc);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->starfieldVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->starfieldIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, app->starfieldIndexCount, 1, 0, 0, 0);

        /* Render individual tactical objects */
        for (int o=0; o<st->object_count; o++) {
            SharedObject* obj = &st->objects[o]; if (!obj->active) continue;
            
            /* Re-bind main graphics pipeline for each object to prevent state leakage from specialized renderers */
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);

            /* HIDE ALL OTHER OBJECTS DURING JUMP ARRIVAL to avoid lateral artifacts */
            if (app->jumpArrival.active && o != 0) continue;

            PushConstants opc = {0}; mat4_identity(opc.model);
            /* Use Smoothed Coordinates for fluid motion */
            float ox = app->smoothObjs[o].x;
            float oy = app->smoothObjs[o].y;
            float oz = app->smoothObjs[o].z;
            float oh = app->smoothObjs[o].h;
            float om = app->smoothObjs[o].m;
            float or = app->smoothObjs[o].r;

            /* Centered mapping: 0..40 maps to -20..20, using (X, Z, -Y) system (Consistent with Axes) */
            vec3 p = {(ox - 20.0f) * tactScale, (oz - 20.0f) * tactScale, (20.0f - oy) * tactScale};
            mat4 T, R, S; mat4_translate(T, p); mat4_identity(R);

            if (o == 0) memcpy(app->playerT, T, sizeof(mat4));

            float s = (obj->type==3?SCALE_BASE: (obj->type==4?SCALE_STAR: (obj->type==5?SCALE_PLANET: (obj->type==6?SCALE_BLACKHOLE: (obj->type==29?SCALE_QUASAR: (obj->type==21?SCALE_ASTEROID: (obj->type==50?SCALE_BASE: SCALE_SHIP)))))));
            /* Scale the object itself by tactScale */
            s *= tactScale;
            mat4_scale(S, (vec3){s,s,s});
            if (obj->type==1 || obj->type>=10) {
                /* Align model (+X) to North (+Z) */
                mat4_rotate(R, 90.0f * M_PI / 180.0f, (vec3){0,1,0});
                /* Apply Smoothed Heading (around vertical Y) */
                mat4_rotate(R, -oh * M_PI / 180.0f, (vec3){0,1,0});
                /* Apply Smoothed Pitch around dynamic horizontal axis */
                float obj_h_rad = oh * M_PI / 180.0f;
                mat4_rotate(R, om * M_PI / 180.0f, (vec3){cosf(obj_h_rad), 0, -sinf(obj_h_rad)});
                /* Apply Smoothed Roll around local longitudinal axis (+X after North alignment) */
                /* Model orientation: X is forward, Y is up, Z is right. 
                   After North alignment (+X to +Z), the forward axis is +Z in world space.
                   But we rotate around local axes. Pitch is local Z (horizontal). Roll is local X (longitudinal). */
                float pitch_rad = om * M_PI / 180.0f;
                mat4_rotate(R, or * M_PI / 180.0f, (vec3){sinf(obj_h_rad)*cosf(pitch_rad), sinf(pitch_rad), cosf(obj_h_rad)*cosf(pitch_rad)});
                if (o == 0) memcpy(app->playerR, R, sizeof(mat4));
            }
            /* Correct Order: Scale -> Rotate -> Translate (in C Row-Major: S * R * T) */
            mat4_multiply(S, R, opc.model); 
            mat4_multiply(opc.model, T, opc.model);
            
            getObjectColor(obj->type, obj->faction, &opc.color[0], &opc.color[1], &opc.color[2]); opc.color[3]=1.0f; opc.usePushColor=5;
            
            /* CLOAKING EFFECT: Blue Glowing Wireframe for cloaked ships */
            if (obj->is_cloaked) {
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
                opc.color[0]=0.0f; opc.color[1]=0.8f; opc.color[2]=1.0f; opc.color[3]=0.8f + 0.2f * sinf(pulse * 5.0f);
                opc.usePushColor = 1;
            }
            
            /* Cosmic Objects Dispatcher (51-88) */
            if (obj->type >= 51 && obj->type <= 53) { drawNebula(cb, app, T, obj->type - 51, pulse, tactScale); continue; }
            if (obj->type == 54) { drawNebula(cb, app, T, 4, pulse, tactScale); continue; }
            if (obj->type == 55) { drawNebula(cb, app, T, 5, pulse, tactScale); continue; }
            if (obj->type == 56) { drawInterstellarFilament(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 57) { drawInterstellarBubble_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 58) { drawBokGlobule_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 59) { drawClumpCore_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 60) {
                /* ACCRETION DISK (Type 60): Singularity Core + Volumetric Accretion Disk + Jets */
                PushConstants apc = opc; 
                mat4 M_core; mat4_scale(S, (vec3){1.5f * tactScale, 1.5f * tactScale, 1.5f * tactScale});
                mat4_multiply(S, T, M_core);
                memcpy(apc.model, M_core, sizeof(mat4));
                /* Pure black core for the singularity */
                apc.color[0] = 0.0f; apc.color[1] = 0.0f; apc.color[2] = 0.0f; apc.color[3] = 1.0f;
                apc.usePushColor = 1;

                /* 1. Core Sphere (Black Hole) */
                vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(apc), &apc);
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
                vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

                /* 2. Accretion Disk and Relativistic Jets */
                drawAccretionDisk_Vulkan(cb, app, T, tactScale, pulse);
                drawRelativisticJet_Vulkan(cb, app, T, tactScale, pulse);
                continue;
            }
            if (obj->type == 61) { drawRelativisticJet_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 62) { drawShockWave_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 63) { drawStellarBowShock_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 64) { drawCosmicVoid_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 65) { drawCosmicFilament_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 66) { drawEventHorizon_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 67) { drawKilonova_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 68) { drawGravLens_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 69) { drawGRB_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 70) { drawGravWave_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 71) { drawProtoplanetaryDisk_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 72) { drawDebrisDisk_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 73) { drawPlanetesimal_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 74) { drawRoguePlanet_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 75) { drawBrownDwarf_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 76) { drawISO_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 77) { drawMagReconn_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 78) { drawCurrentSheet_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 79) { drawHeliosphere_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 80) { drawTermShock_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 81) { drawMagnetosphere_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 82) { drawCosmicString_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 83) { drawDomainWall_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 84) { drawDMHalo_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 85) { drawIGM_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 86) { drawCGM_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 87) { drawLymanAlpha_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == 88) { drawCMB_Vulkan(cb, app, T, tactScale, pulse); continue; }
            /* Mapping all categories from telemetry */
            
            

            if (obj->type == 39) {
                drawIonStorm(cb, app, T, tactScale, pulse, 0);
                continue;
            }

            if (obj->type == 48) {
                drawTimeAnomaly(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 46) {
                drawPlasmaStorm(cb, app, T, tactScale, pulse);
                continue;
            }


            if (obj->type == 44) {
                drawDarkMatterCloud(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 37) {
                drawSubspaceRupture(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 36) {
                drawAncientRelic(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 34) {
                drawDysonFragment(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 38) {
                drawSatellite(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 49) {
                drawVoidCrystal(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 43) {
                drawMegaStructure(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 40) {
                drawAlienArtifact(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 45) {
                drawSingularity(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 50) {
                drawSubspaceAnomaly(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 42) {
                drawNeutronStar(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 26) {
                drawRift(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 25) {
                drawPlatform(cb, app, T, obj->faction, tactScale, pulse);
                continue;
            }

            if (obj->type == 24) {
                drawCommBuoy(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 23) {
                drawMine(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 35) {
                drawTradingHub(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 30 || obj->type == 31) {
                drawMonster(cb, app, T, obj->type, tactScale, pulse);
                continue;
            }

            if (obj->type == 21) {
                drawAsteroid(cb, app, T, (float)obj->plating / 100.0f, obj->ship_class, tactScale, pulse);
                continue;
            }

            if (obj->type == 9) {
                drawComet(cb, app, T, oh, om, tactScale, pulse);
                continue;
            }

            if (obj->type == 8) {
                drawPulsar(cb, app, T, tactScale, pulse, obj->ship_class);
                continue;
            }

            if (obj->type == 3) {
                drawStarbase(cb, app, T, tactScale, pulse, obj->faction);
                continue;
            }

            if (obj->type == 6) {
                /* Draw Black Hole Event Horizon (Black Sphere) */
                vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(opc), &opc);
                VkBuffer vb = app->sphereVertexBuffer; VkBuffer ib = app->sphereIndexBuffer; uint32_t cnt = SPHERE_LATS * SPHERE_LONGS * 6;
                vkCmdBindVertexBuffers(cb, 0, 1, &vb, &off); vkCmdBindIndexBuffer(cb, ib, 0, VK_INDEX_TYPE_UINT32); vkCmdDrawIndexed(cb, cnt, 1, 0, 0, 0);
                /* Draw Accretion Disk */
                drawAccretionDisk_Vulkan(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == 7) {
                /* Draw Volumetric Nebula (Hidden during the arrival sequence to eliminate green cloud artifacts) */
                if (!app->jumpArrival.active) {
                    drawNebula(cb, app, T, obj->ship_class, pulse, tactScale);
                }
                continue;
            }

            if (obj->type == 29) {
                /* QUASAR: Self-Spinning Sphere + Accretion Disk (No Jets) */
                mat4 R_q; mat4_identity(R_q);
                /* Spin like a top around vertical axis */
                mat4_rotate(R_q, pulse * 3.0f, (vec3){0, 1, 0});
                /* Axial tilt for perspective */
                mat4_rotate(R_q, 20.0f * M_PI / 180.0f, (vec3){1, 0, 0}); 
                
                /* SPIN IN PLACE: Apply R_q before Translation T */
                mat4 M_spin; mat4_multiply(S, R_q, M_spin);
                mat4 M_core; mat4_multiply(M_spin, T, M_core);
                
                PushConstants qpc = opc; memcpy(qpc.model, M_core, sizeof(mat4));
                
                /* Class-specific vibrant colors */
                if (obj->ship_class == 0)      { qpc.color[0]=0.1f; qpc.color[1]=0.4f; qpc.color[2]=1.0f; } /* Blue */
                else if (obj->ship_class == 1) { qpc.color[0]=1.0f; qpc.color[1]=0.8f; qpc.color[2]=0.2f; } /* Gold */
                else if (obj->ship_class == 2) { qpc.color[0]=0.0f; qpc.color[1]=1.0f; qpc.color[2]=0.5f; } /* Green */
                else if (obj->ship_class == 3) { qpc.color[0]=1.0f; qpc.color[1]=0.4f; qpc.color[2]=0.0f; } /* Orange */
                else if (obj->ship_class == 4) { qpc.color[0]=1.0f; qpc.color[1]=0.1f; qpc.color[2]=0.1f; } /* Red */
                else if (obj->ship_class == 5) { qpc.color[0]=0.7f; qpc.color[1]=0.0f; qpc.color[2]=1.0f; } /* Purple */
                else                           { qpc.color[0]=0.0f; qpc.color[1]=0.9f; qpc.color[2]=1.0f; } /* Cyan */
                qpc.color[3]=1.0f; qpc.usePushColor=6;
                
                /* 1. Core Sphere */
                vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(qpc), &qpc);
                vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
                vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

                /* 2. Accretion Disk (Flattened sphere, spins with core) */
                mat4 S_disk, M_disk;
                mat4_scale(S_disk, (vec3){3.5f, 0.02f, 3.5f});
                mat4_multiply(M_spin, S_disk, M_disk);
                mat4_multiply(M_disk, T, M_disk);
                PushConstants dpc = qpc; memcpy(dpc.model, M_disk, sizeof(mat4)); dpc.color[3]=0.5f;
                vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(dpc), &dpc);
                vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
                continue;
            }

            /* PBR Parameters based on object type */
            if (obj->type == 1 || obj->type >= 10) { // Ships
                opc.metallic = 0.9f; opc.roughness = 0.25f;
            } else if (obj->type == 4) { // Star
                opc.metallic = 0.0f; opc.roughness = 1.0f; opc.usePushColor = 6; /* Hyper-Warp for surface plasma */
                /* Spectral Types: 0:Blue, 1:White, 2:Yellow, 3:Orange, 4:Red */
                if (obj->ship_class == 0) { opc.color[0]=0.4f; opc.color[1]=0.6f; opc.color[2]=1.0f; }
                else if (obj->ship_class == 1) { opc.color[0]=0.9f; opc.color[1]=0.9f; opc.color[2]=1.0f; }
                else if (obj->ship_class == 2) { opc.color[0]=1.0f; opc.color[1]=1.0f; opc.color[2]=0.4f; }
                else if (obj->ship_class == 3) { opc.color[0]=1.0f; opc.color[1]=0.7f; opc.color[2]=0.2f; }
                else { opc.color[0]=1.0f; opc.color[1]=0.3f; opc.color[2]=0.2f; }
            } else if (obj->type == 5) { // Planet
                opc.metallic = 0.0f; opc.roughness = 0.9f;
                /* Planet Classes: 0:Terrestrial, 1:Gas Giant, 2:Ice, 3:Molten, 4:Desert */
                if (obj->ship_class == 0) { opc.color[0]=0.2f; opc.color[1]=0.5f; opc.color[2]=1.0f; } /* Earth-like */
                else if (obj->ship_class == 1) { opc.color[0]=0.8f; opc.color[1]=0.6f; opc.color[2]=0.4f; } /* Jupiter-like */
                else if (obj->ship_class == 2) { opc.color[0]=0.7f; opc.color[1]=0.9f; opc.color[2]=1.0f; } /* Ice */
                else if (obj->ship_class == 3) { opc.color[0]=0.8f; opc.color[1]=0.2f; opc.color[2]=0.1f; } /* Molten */
                else { opc.color[0]=0.8f; opc.color[1]=0.7f; opc.color[2]=0.5f; } /* Desert */
            } else if (obj->type == 27) { // Torpedo
                opc.metallic = 0.8f; opc.roughness = 0.1f;
            } else {
                opc.metallic = 0.5f; opc.roughness = 0.5f;
            }

            if (o == 0 && app->jumpArrival.timer > 360) {
                float progress = 1.0f - (float)(app->jumpArrival.timer - 360) / 180.0f;
                float glow = 1.0f - progress;
                opc.color[0] = 0.8f + glow * 0.2f; opc.color[1] = 0.8f + glow * 0.2f; opc.color[2] = 1.0f;
            }



            if (obj->faction == 12 && (obj->type == 1 || obj->type >= 10)) {
                drawSwarmCube(cb, app, opc.model, pulse);
                continue;
            }

            /* Draw Standard Object (Ship, Base, Planet, Star, Asteroid, etc.) */
            PushConstants ship_opc = opc;
                if (obj->type == 1 || obj->type >= 10) {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
                    float shipScale = 0.55f * tactScale;
                    mat4 S_ship;
                    mat4_scale(S_ship, (vec3){shipScale, shipScale, shipScale});
                    mat4_multiply(S_ship, ship_opc.model, ship_opc.model);
                }
                
                /* Specialized Rendering for new types in Vulkan */
                if (obj->type == 34 || obj->type == 37 || obj->type == 39) { // Dyson, Rupture, Storm (GLOW)
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
                    ship_opc.usePushColor = 6; // Pulse mode
                    if (obj->type == 34) { ship_opc.color[0]=1; ship_opc.color[1]=0.8f; ship_opc.color[2]=0.2f; }
                    if (obj->type == 37) { ship_opc.color[0]=0.6f; ship_opc.color[1]=0; ship_opc.color[2]=0.8f; }
                    if (obj->type == 39) { ship_opc.color[0]=0.4f; ship_opc.color[1]=0.4f; ship_opc.color[2]=1; }
                } else if (obj->type == 36) { // Ancient Relic (WIREFRAME)
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
                    ship_opc.color[0]=0; ship_opc.color[1]=1; ship_opc.color[2]=0.8f;
                } else if (obj->type >= 40 && obj->type <= 49 && obj->type != 45 && obj->type != 46) {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
                    ship_opc.usePushColor = 1;
                    if (obj->type == 40) { ship_opc.color[0]=1.0f; ship_opc.color[1]=0.5f; ship_opc.color[2]=0.0f; } // Artifact
                    else if (obj->type == 41) { ship_opc.color[0]=0.0f; ship_opc.color[1]=0.5f; ship_opc.color[2]=1.0f; } // Warp Gate
                    else if (obj->type == 42) { ship_opc.color[0]=0.8f; ship_opc.color[1]=0.8f; ship_opc.color[2]=1.0f; } // Neutron Star
                    else if (obj->type == 43) { ship_opc.color[0]=0.5f; ship_opc.color[1]=0.5f; ship_opc.color[2]=0.5f; } // Mega Struct
                    else if (obj->type == 44) { ship_opc.color[0]=0.2f; ship_opc.color[1]=0.0f; ship_opc.color[2]=0.4f; } // Dark Cloud
                    else if (obj->type == 47) { ship_opc.color[0]=0.7f; ship_opc.color[1]=0.7f; ship_opc.color[2]=0.0f; } // Orbital Ring
                    else if (obj->type == 48) { ship_opc.color[0]=0.0f; ship_opc.color[1]=1.0f; ship_opc.color[2]=0.5f; } // Time Anomaly
                    else if (obj->type == 49) { ship_opc.color[0]=0.9f; ship_opc.color[1]=0.0f; ship_opc.color[2]=0.9f; } // Void Crystal
                }
                vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ship_opc), &ship_opc);
                VkBuffer vb;
                VkBuffer ib;
                uint32_t cnt;
                VkIndexType it = VK_INDEX_TYPE_UINT32;
                if (obj->type == 27) {
                    vb = app->torpVertexBuffer;
                    ib = app->torpIndexBuffer;
                    cnt = sizeof(torpIndices)/4;
                } else if (obj->type == 4 || obj->type == 5) {
                    vb = app->sphereVertexBuffer;
                    ib = app->sphereIndexBuffer;
                    cnt = SPHERE_LATS * SPHERE_LONGS * 6;
                } else if (obj->type == 41 || obj->type == 47) {
                    vb = app->circleVertexBuffer;
                    ib = app->circleIndexBuffer;
                    cnt = CIRCLE_SEGMENTS * 2;
                } else { 
                    vb = app->shipVertexBuffer;
                    ib = app->shipIndexBuffer;
                    cnt = sizeof(shipIndices)/4;
                }
                vkCmdBindVertexBuffers(cb, 0, 1, &vb, &off);
                vkCmdBindIndexBuffer(cb, ib, 0, it);
                vkCmdDrawIndexed(cb, cnt, 1, 0, 0, 0);
                if (obj->type == 1 || obj->type >= 10) {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
                }

                /* --- QUANTUM CORE (Sphere + 3 Orbiting Rings) --- */
                /* Solo per le navi dell'Alleanza (faction == 0 o faction == 1) */
                int is_alliance = 0;
                if (obj->faction == 0 || obj->faction == 1) is_alliance = 1;

                if ((obj->type == 1 || obj->type >= 10) && is_alliance) {
                    /* Our pyramid is scaled by 0.55f * tactScale and its stern is at local X = -0.7288f.
                     * So in opc.model space, the tail is at -0.7288f * 0.55f * tactScale = -0.40084f * tactScale.
                     * We place the quantum core exactly there */
                    /* 1. Procedural Wireframe Polyhedron Core */
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
                    PushConstants qpc = {0};
                    float coreSize = 0.20f * tactScale; 
                    mat4 S_q; mat4_scale(S_q, (vec3){coreSize, coreSize, coreSize});
                    
                    int cl = obj->ship_class;
                    if (cl < 0 || cl > 12) cl = 12;
                    
                    /* Rotation speed based on class: Legacy (0) rotates faster */
                    mat4 R_q; mat4_identity(R_q);
                    float rotSpeed = 1.0f + (12 - cl) * 0.2f; 
                    mat4_rotate(R_q, pulse * rotSpeed, (vec3){0.0f, 1.0f, 0.0f});
                    mat4_multiply(S_q, R_q, S_q);
                    
                    /* Position the core slightly further back (-0.46f) so it doesn't sink into the tail */
                    mat4 T_core_loc, M_core;
                    mat4_translate(T_core_loc, (vec3){-0.46f * tactScale, 0.0f, 0.0f}); 
                    mat4_multiply(T_core_loc, opc.model, M_core);
                    mat4_multiply(S_q, M_core, qpc.model);
                    
                    qpc.color[0] = 0.8f + 0.2f * sinf(pulse * 2.0f);
                    qpc.color[1] = 0.6f + 0.3f * cosf(pulse * 1.5f);
                    qpc.color[2] = 0.2f + 0.2f * sinf(pulse * 3.0f);
                    qpc.color[3] = 1.0f;
                    
                    qpc.usePushColor = 1; 
                    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(qpc), &qpc);
                    
                    vkCmdBindVertexBuffers(cb, 0, 1, &app->coreVB[cl], &off);
                    vkCmdBindIndexBuffer(cb, app->coreIB[cl], 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(cb, app->coreICount[cl], 1, 0, 0, 0);

                    /* 2. Three Orbiting Rings (Wireframe) */
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
                    vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
                    vkCmdBindIndexBuffer(cb, app->circleIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    
                    float speeds[3] = {2.8f, -2.1f, 3.5f};
                    float radii[3] = {0.15f * tactScale, 0.18f * tactScale, 0.21f * tactScale};
                    vec3 axes[3] = {{1,1,0.3}, {0.3,1,1}, {1,0.3,1}};
                    
                    for (int r=0; r<3; r++) {
                        PushConstants rpc = {0};
                        mat4 R_r, S_r;
                        mat4_identity(R_r);
                        mat4_rotate(R_r, pulse * speeds[r], axes[r]);
                        mat4_scale(S_r, (vec3){radii[r], radii[r], radii[r]});
                        
                        mat4_multiply(S_r, R_r, rpc.model);
                        mat4_multiply(rpc.model, M_core, rpc.model);
                        
                        rpc.color[0]=0.0f; rpc.color[1]=0.95f; rpc.color[2]=1.0f; rpc.color[3]=1.0f; 
                        rpc.usePushColor = 1;
                        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(rpc), &rpc);
                        vkCmdDrawIndexed(cb, CIRCLE_SEGMENTS * 2, 1, 0, 0, 0);
                    }
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
                }
            }


        /* Particles/Torps/Booms (Solid) */
        for (int i=0; i<MAX_ACTIVE_TORPS; i++) {
            if (app->activeTorps[i].active <= 0) continue;
            PushConstants tpc = {0}; mat4_identity(tpc.model);
            mat4_translate(tpc.model, (vec3){app->activeTorps[i].x * tactScale, app->activeTorps[i].y * tactScale, app->activeTorps[i].z * tactScale});
            mat4 S_torp; mat4_scale(S_torp, (vec3){tactScale, tactScale, tactScale});
            mat4_multiply(S_torp, tpc.model, tpc.model);
            tpc.color[0]=1; tpc.color[1]=1; tpc.color[2]=0; tpc.color[3]=1.0f; tpc.usePushColor=1;
            tpc.metallic = 0.9f; tpc.roughness = 0.1f;
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(tpc), &tpc);
            vkCmdBindVertexBuffers(cb, 0, 1, &app->torpVertexBuffer, &off); vkCmdBindIndexBuffer(cb, app->torpIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, sizeof(torpIndices)/4, 1, 0, 0, 0);
            }

            /* Dismantle / Dematerialization Effects (Cyan/White Pulse) */
            bool glow_bound = false;
            for (int i=0; i<MAX_ACTIVE_DISMANTLES; i++) {
                if (app->activeDismantles[i].life <= 0) continue;
                if (!glow_bound) {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
                    glow_bound = true;
                }
                float life = app->activeDismantles[i].life;
                
                /* Three nested spheres: Initial Flash + Core White + Expanding Cyan Aura */
                for (int j=0; j<3; j++) {
                    PushConstants dpc = {0};
                    mat4 T, S, R;
                    mat4_translate(T, (vec3){app->activeDismantles[i].x * tactScale, app->activeDismantles[i].y * tactScale, app->activeDismantles[i].z * tactScale});
                    
                    float s;
                    if (j == 0) { /* Initial Flash (Mode 7) - Very bright and fast */
                        if (life < 0.7f) continue;
                        s = (1.0f - life) * 3.0f * tactScale;
                        dpc.color[0]=1; dpc.color[1]=1; dpc.color[2]=1; dpc.color[3]= (life-0.7f)/0.3f;
                        dpc.usePushColor = 7; 
                    } else if (j == 1) { /* Core: Shrinking intense white (Mode 6) */
                        s = life * 1.5f * tactScale;
                        dpc.color[0]=1; dpc.color[1]=1; dpc.color[2]=1; dpc.color[3]=life;
                        dpc.usePushColor = 6; 
                    } else { /* Aura: Expanding cyan shockwave (Mode 7) */
                        s = (1.0f + (1.0f - life) * 2.0f) * 2.5f * tactScale;
                        dpc.color[0]=0; dpc.color[1]=1; dpc.color[2]=1; dpc.color[3]=life * 0.7f;
                        dpc.usePushColor = 7; 
                    }
                    mat4_scale(S, (vec3){s, s, s});
                    mat4_identity(R); mat4_rotate(R, pulse * 8.0f, (vec3){0,1,0});
                    
                    mat4_multiply(S, R, dpc.model);
                    mat4_multiply(dpc.model, T, dpc.model);
                    dpc.time = pulse;
                    
                    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(dpc), &dpc);
                    VkDeviceSize off_glow = 0;
                    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off_glow);
                    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
                }
            }
            /* Restore standard graphics pipeline for explosions */
            if (glow_bound) {
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
            }

            /* --- IONIC BEAMS (Definitive Precision Connection) --- */
            bool beam_bound = false;
            for (int i=0; i<MAX_ACTIVE_BEAMS; i++) {
                if (app->activeBeams[i].life <= 0) continue;
                if (!beam_bound) {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
                    beam_bound = true;
                }
                
                int target_id = app->activeBeams[i].extra; 
                float bsx = app->activeBeams[i].sx, bsy = app->activeBeams[i].sy, bsz = app->activeBeams[i].sz;
                float btx = app->activeBeams[i].tx, bty = app->activeBeams[i].ty, btz = app->activeBeams[i].tz;

                /* Real-time Tracking: Snap beam endpoints to current smoothed model positions */
                int owner_id = app->activeBeams[i].owner_id;
                int emitter_id = app->activeBeams[i].emitter_id;
                for (int o=0; o < st->object_count; o++) {
                    if (st->objects[o].active) {
                        if (st->objects[o].id == owner_id) {
                            float off_v = (emitter_id == 2) ? -0.12f : 0.12f;
                            bsx = app->smoothObjs[o].x; 
                            bsy = app->smoothObjs[o].y; 
                            bsz = app->smoothObjs[o].z + off_v;
                        }
                        if (st->objects[o].id == target_id) {
                            btx = app->smoothObjs[o].x; bty = app->smoothObjs[o].y; btz = app->smoothObjs[o].z;
                        }
                    }
                }
                
                /* Mapping: vX=X-20, vY=Z-20 (Height), vZ=20-Y (Depth) */
                float vsx = (bsx - 20.0f) * tactScale; float vsy = (bsz - 20.0f) * tactScale; float vsz = (20.0f - bsy) * tactScale;
                float vtx = (btx - 20.0f) * tactScale; float vty = (btz - 20.0f) * tactScale; float vtz = (20.0f - bty) * tactScale;
                
                vec3 V = { vtx - vsx, vty - vsy, vtz - vsz };
                float dist = sqrtf(V[0]*V[0] + V[1]*V[1] + V[2]*V[2]);
                if (dist < 0.1f) continue; /* Increased threshold for stability */
                V[0] /= dist; V[1] /= dist; V[2] /= dist;

                PushConstants bpc = {0}; mat4 T, S, R, M;
                
                /* DIRECT BASIS CONSTRUCTION: Align model +X axis with normalized direction vector V */
                mat4_identity(R);
                vec3 right, up_ref = {0, 1, 0};
                if (fabsf(V[1]) > 0.95f) { up_ref[0] = 1; up_ref[1] = 0; } /* Stricter vertical check */
                
                /* right = cross(up_ref, V) */
                right[0] = up_ref[1]*V[2] - up_ref[2]*V[1];
                right[1] = up_ref[2]*V[0] - up_ref[0]*V[2];
                right[2] = up_ref[0]*V[1] - up_ref[1]*V[0];
                float rlen = sqrtf(right[0]*right[0] + right[1]*right[1] + right[2]*right[2]);
                
                if (rlen < 0.001f) { /* Absolute safety against NaN */
                    right[0] = 0; right[1] = 0; right[2] = 1;
                } else {
                    right[0] /= rlen; right[1] /= rlen; right[2] /= rlen;
                }
                
                /* actual_up = cross(V, right) */
                vec3 a_up;
                a_up[0] = V[1]*right[2] - V[2]*right[1];
                a_up[1] = V[2]*right[0] - V[0]*right[2];
                a_up[2] = V[0]*right[1] - V[1]*right[0];

                /* Set matrix basis: X=V (direction), Y=actual_up, Z=right */
                R[0][0] = V[0]; R[0][1] = V[1]; R[0][2] = V[2];
                R[1][0] = a_up[0]; R[1][1] = a_up[1]; R[1][2] = a_up[2];
                R[2][0] = right[0]; R[2][1] = right[1]; R[2][2] = right[2];

                /* Translation to Midpoint */
                mat4_translate(T, (vec3){(vsx + vtx)*0.5f, (vsy + vty)*0.5f, (vsz + vtz)*0.5f});
                
                /* Scaling: Torp model is 0.9 units long. */
                float thick = 0.85f * tactScale * app->activeBeams[i].life;
                mat4_scale(S, (vec3){dist / 0.9f, thick, thick});
                
                /* Order: S * R * T (Row-Major concatenation) */
                mat4_multiply(S, R, M); mat4_multiply(M, T, bpc.model);
                
                bpc.color[0]=0.0f; bpc.color[1]=0.8f; bpc.color[2]=1.0f; bpc.color[3]=app->activeBeams[i].life;
                bpc.usePushColor = 6; bpc.time = pulse;
                
                vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(bpc), &bpc);
                vkCmdBindVertexBuffers(cb, 0, 1, &app->torpVertexBuffer, &off);
                vkCmdBindIndexBuffer(cb, app->torpIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cb, sizeof(torpIndices)/4, 1, 0, 0, 0);

                /* Core and Impact FX */
                PushConstants cpc = bpc; mat4 S_core; mat4_scale(S_core, (vec3){1.0f, 0.35f, 0.35f}); mat4_multiply(S_core, bpc.model, cpc.model);
                cpc.color[0]=1.0f; cpc.color[1]=1.0f; cpc.color[2]=1.0f;
                vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(cpc), &cpc);
                vkCmdDrawIndexed(cb, sizeof(torpIndices)/4, 1, 0, 0, 0);

                PushConstants ipc = bpc; mat4 T_imp, S_imp; mat4_translate(T_imp, (vec3){vtx, vty, vtz});
                float is = 1.3f * tactScale * app->activeBeams[i].life; mat4_scale(S_imp, (vec3){is, is, is}); mat4_multiply(S_imp, T_imp, ipc.model);
                ipc.color[0]=1.0f; ipc.color[1]=1.0f; ipc.color[2]=1.0f; ipc.usePushColor = 7;
                vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ipc), &ipc);
                vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
                vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
            }
            if (beam_bound) {
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
            }

            for (int i=0; i<MAX_ACTIVE_BOOMS; i++) {
            if (app->activeBooms[i].life <= 0) continue;
            /* Expansion factor: 12.0 units at end of life */
            float exp = (1.0f - app->activeBooms[i].life) * 12.0f * tactScale;
            for (int p=0; p<EXPLOSION_PIXELS; p++) {
                PushConstants bpc = {0}; mat4_identity(bpc.model);
                vec3 pos = {(app->activeBooms[i].x + app->activeBooms[i].offsets[p][0]*exp) * tactScale, 
                            (app->activeBooms[i].y + app->activeBooms[i].offsets[p][1]*exp) * tactScale, 
                            (app->activeBooms[i].z + app->activeBooms[i].offsets[p][2]*exp) * tactScale};
                mat4 T, S, R; 
                mat4_translate(T, pos); 
                mat4_scale(S, (vec3){0.25f * tactScale, 0.25f * tactScale, 0.25f * tactScale}); 
                
                /* Dynamic spinning for each particle based on life */
                mat4_identity(R);
                float angle = (1.0f - app->activeBooms[i].life) * 5.0f;
                vec3 rot_axis = { (float)sin(p), (float)cos(p), (float)sin(p*0.5) };
                mat4_rotate(R, angle, rot_axis);

                /* Order: Scale * Rotate * Translate */
                mat4_multiply(S, R, bpc.model);
                mat4_multiply(bpc.model, T, bpc.model);

                bpc.color[0]=app->activeBooms[i].colors[p][0]; bpc.color[1]=app->activeBooms[i].colors[p][1]; bpc.color[2]=app->activeBooms[i].colors[p][2]; bpc.color[3]=1.0f; bpc.usePushColor=1;
                bpc.metallic = 0.0f; bpc.roughness = 1.0f;
                vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(bpc), &bpc);
                vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off); 
                vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32); 
                vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
            }
        }
        
        /* Draw Shield Hits */
        drawShieldEffect(cb, app, pulse, tactScale, app->playerR, app->playerT);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);

        } /* End of Tactical View Render (mapAnim < 0.99) */
        
        /* --- SHARED MEMORY INSPECTOR (Console Telemetry Fallback) --- */
        if (app->shm_inspector_page > 0) {
            static float last_dbg = 0;
            if (pulse - last_dbg > 0.5f) {
                last_dbg = pulse;
                /* Clear Screen and print telemetry */
                printf("\033[2J\033[H");
                printf("================================================================================\n");
                printf(" SPACE GL - VULKAN SHARED MEMORY INSPECTOR (Page %d/3) - [n/p: Nav, m: Toggle]\n", app->shm_inspector_page);
                printf("================================================================================\n");
                
                GameState* st = &app->shm->buffers[app->shm->read_index];

                if (app->shm_inspector_page == 1) {
                    /* Page 1: SHIP VITALS */
                    printf(" [SHIP STATUS: %-15s]  FRAME: %-10lld\n", st->shm_captain_name, (long long)st->frame_id);
                    printf("--------------------------------------------------------------------------------\n");
                    printf(" ENERGY:    %-15lu  | CARGO E:  %-15lu\n", (unsigned long)st->shm_energy, (unsigned long)st->shm_cargo_energy);
                    printf(" HULL:      %-15.2f  | CREW:     %-5d  | PRISON: %-5d\n", st->shm_hull_integrity, st->shm_crew, st->shm_prison_unit);
                    printf(" PLATING:   %-5d             | L-SUPP:   %-15.2f\n", st->shm_composite_plating, st->shm_life_support);
                    printf("--------------------------------------------------------------------------------\n");
                    printf(" SHIELDS:   F: %-5d  RE: %-5d  T: %-5d  B: %-5d  L: %-5d  RI: %-5d\n",
                           st->shm_shields[0], st->shm_shields[1], st->shm_shields[2],
                           st->shm_shields[3], st->shm_shields[4], st->shm_shields[5]);
                    printf(" TARGETS:   F: %-5d  RE: %-5d  T: %-5d  B: %-5d  L: %-5d  RI: %-5d\n",
                           st->shm_target_shields[0], st->shm_target_shields[1], st->shm_target_shields[2],
                           st->shm_target_shields[3], st->shm_target_shields[4], st->shm_target_shields[5]);
                } else if (app->shm_inspector_page == 2) {
                    /* Page 2: TACTICAL & NAVIGATION */
                    printf(" [NAVIGATION & TACTICAL]       POS: [%d,%d,%d] / [%.2f, %.2f, %.2f]\n", 
                           st->shm_q[0], st->shm_q[1], st->shm_q[2], st->shm_s[0], st->shm_s[1], st->shm_s[2]);
                    printf("--------------------------------------------------------------------------------\n");
                    printf(" LOCK TARGET:  %-5d          | ION CHARGE: %-15.2f\n", st->shm_lock_target, st->shm_ion_beam_charge);
                    printf(" TUBE STATE:   %-5d          | CLOAKED:    %-5s  | ALERT: %-5s\n", 
                           st->shm_tube_state, st->is_cloaked ? "YES" : "NO", st->shm_red_alert ? "RED" : "NORMAL");
                    printf("--------------------------------------------------------------------------------\n");
                    printf(" TUBE ETAS:    1:%-5d  2:%-5d  3:%-5d  4:%-5d\n",
                           st->tube_torpedo_etas[0], st->tube_torpedo_etas[1], st->tube_torpedo_etas[2], st->tube_torpedo_etas[3]);
                    printf(" POWER DIST:   ENG: %-5.2f  SHL: %-5.2f  WEP: %-5.2f\n", 
                           st->shm_power_dist[0], st->shm_power_dist[1], st->shm_power_dist[2]);
                } else {
                    /* Page 3: NETWORK & PIPELINE */
                    printf(" [IPC & VULKAN PIPELINE]\n");
                    printf("--------------------------------------------------------------------------------\n");
                    printf(" SHM SEGMENT:  %-15s | READ IDX:  %-5d | WRITE IDX: %-5d\n", 
                           SHM_NAME, atomic_load(&app->shm->read_index), atomic_load(&app->shm->write_index));
                    printf(" OBJECT COUNT: %-5d           | BEAM COUNT: %-5d | TORP COUNT: %-5d\n", 
                           st->object_count, st->beam_count, st->torpedo_count);
                    printf(" CRYPTO:       0x%02X          | FLAGS:      0x%08X\n", 
                           st->shm_crypto_algo, st->shm_encryption_flags);
                    printf("--------------------------------------------------------------------------------\n");
                    printf(" APP CAMERA:   DIST: %-10.2f | ROT: [%.2f, %.2f]\n", 
                           app->cameraDist, app->angleX, app->angleY);
                }
                printf("================================================================================\n");
                fflush(stdout);
            }
        }
    }
    vkCmdEndRenderPass(cb); vkEndCommandBuffer(cb);
}

void drawFrame(VulkanApp* app) {
    vkWaitForFences(app->device, 1, &app->inFlightFences[app->currentFrame], 1, UINT64_MAX);
    uint32_t imgIdx; if (vkAcquireNextImageKHR(app->device, app->swapChain, UINT64_MAX, app->imageAvailableSemaphores[app->currentFrame], NULL, &imgIdx) != VK_SUCCESS) return; 
    vkResetFences(app->device, 1, &app->inFlightFences[app->currentFrame]);
    vkResetCommandBuffer(app->commandBuffers[app->currentFrame], 0); recordCommandBuffer(app->commandBuffers[app->currentFrame], imgIdx, app);
    
    UniformBufferObject ubo; 
    mat4 view; mat4_identity(view);
    
    /* 1. Vista Orbitale Standard (Tattica) */
    mat4 m_std; mat4_identity(m_std);
    mat4_rotate(m_std, app->angleY * M_PI / 180.0f, (vec3){0, 1, 0});
    mat4_rotate(m_std, app->angleX * M_PI / 180.0f, (vec3){1, 0, 0});
    mat4 T_cam; mat4_translate(T_cam, (vec3){0, 0, -app->cameraDist});
    mat4_multiply(m_std, T_cam, m_std);

    /* 2. Vista Bridge (Prima Persona) */
    mat4 m_brg; mat4_identity(m_brg);
    if (app->bridgeAnim > 0.001f) {
        float tactScale = 1.0f - app->mapAnim;
        /* Coordinate smooth della nave (Oggetto 0) nel sistema Vulkan (Mapping SHIP: X, Z, -Y) */
        float px = (app->smoothObjs[0].x - 20.0f) * tactScale;
        float py = (app->smoothObjs[0].z - 20.0f) * tactScale;
        float pz = (20.0f - app->smoothObjs[0].y) * tactScale;
        float ph = app->smoothObjs[0].h;
        float pm = app->smoothObjs[0].m;

        /* Matrice di Rotazione della Nave (Sincronizzata con recordCommandBuffer) */
        mat4 R_ship; mat4_identity(R_ship);
        /* Allineamento modello: +X -> +Z (South) */
        mat4_rotate(R_ship, 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});
        /* Heading */
        mat4_rotate(R_ship, -ph * M_PI / 180.0f, (vec3){0, 1, 0});
        /* Pitch */
        float h_rad = ph * M_PI / 180.0f;
        mat4_rotate(R_ship, pm * M_PI / 180.0f, (vec3){cosf(h_rad), 0, -sinf(h_rad)});

        /* Camera Position: exactly on the local Y-axis, slightly above the ship */
        float ly = (app->showBridge >= 11) ? (-BRIDGE_CAMERA_OFFSET_Y * SCALE_SHIP * tactScale) : (BRIDGE_CAMERA_OFFSET_Y * SCALE_SHIP * tactScale);
        /* wx, wy, wz = ShipPos + R_ship * (0, ly, 0) */
        float wx = ly * R_ship[1][0] + px;
        float wy = ly * R_ship[1][1] + py;
        float wz = ly * R_ship[1][2] + pz;

        /* Bridge Camera Orientation: Fixed relative to the ship.
           The ship looks along its local +X axis (Nose). The camera looks along -Z by default. */
        /* To align the camera's Forward with the bow (+X), we use a +90 degree rotation. 
           We multiply R_base by R_ship to apply the ship's world rotations to the already oriented view. */
        mat4 R_cam_world;
        mat4 R_base; mat4_identity(R_base);
        mat4_rotate(R_base, 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});

        /* Rotation based on Mode (Look around) - Relative to the bow */
        int mode = app->showBridge % 10;
        if (mode == 2) mat4_rotate(R_base, M_PI/2.0f, (vec3){0, 1, 0});       /* LEFT: +90 deg from forward */
        else if (mode == 3) mat4_rotate(R_base, -M_PI/2.0f, (vec3){0, 1, 0});  /* RIGHT: -90 deg from forward */
        else if (mode == 4) mat4_rotate(R_base, M_PI/4.0f, (vec3){0, 0, 1});   /* UP: +45 deg around local Z */
        else if (mode == 5) mat4_rotate(R_base, -M_PI/4.0f, (vec3){0, 0, 1});  /* DOWN: -45 deg around local Z */
        else if (mode == 6) mat4_rotate(R_base, M_PI, (vec3){0, 1, 0});        /* REAR: 180 deg from forward */

        mat4_multiply(R_base, R_ship, R_cam_world);

        /* Matrice di Vista = inv(T_world) * inv(R_cam_world) 
           In Row-Major: v * T_inv * R_inv = (v - pos) * R_inv */
        mat4 R_inv; mat4_identity(R_inv);
        for(int i=0; i<3; i++) for(int j=0; j<3; j++) R_inv[i][j] = R_cam_world[j][i];

        mat4 T_inv; mat4_translate(T_inv, (vec3){-wx, -wy, -wz});
        mat4_multiply(T_inv, R_inv, m_brg);
        }
    /* 3. Interpolazione Finale della Vista */
    if (app->bridgeAnim <= 0.001f) {
        memcpy(view, m_std, sizeof(mat4));
    } else if (app->bridgeAnim >= 0.999f) {
        memcpy(view, m_brg, sizeof(mat4));
    } else {
        /* Interpolazione lineare tra le componenti delle matrici */
        for (int i=0; i<4; i++) {
            for (int j=0; j<4; j++) {
                view[i][j] = m_std[i][j] * (1.0f - app->bridgeAnim) + m_brg[i][j] * app->bridgeAnim;
            }
        }
    }
    
    memcpy(ubo.view, view, sizeof(mat4));
    
    /* 4. FOV Dinamico: 45.0 (Tattico) -> 65.0 (Bridge) */
    float current_fov = 45.0f * (1.0f - app->bridgeAnim) + 65.0f * app->bridgeAnim;
    mat4_perspective(current_fov * M_PI / 180.0f, WIDTH/(float)HEIGHT, 0.1f, 1000.0f, ubo.proj); ubo.proj[1][1] *= -1;
    
    void* d; vkMapMemory(app->device, app->uniformBuffersMemory[app->currentFrame], 0, sizeof(ubo), 0, &d); memcpy(d, &ubo, sizeof(ubo)); vkUnmapMemory(app->device, app->uniformBuffersMemory[app->currentFrame]);
    VkPipelineStageFlags w = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 1, &app->imageAvailableSemaphores[app->currentFrame], &w, 1, &app->commandBuffers[app->currentFrame], 1, &app->renderFinishedSemaphores[app->currentFrame]};
    vkQueueSubmit(app->graphicsQueue, 1, &si, app->inFlightFences[app->currentFrame]);
    VkSwapchainKHR sw = app->swapChain; VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, NULL, 1, &app->renderFinishedSemaphores[app->currentFrame], 1, &sw, &imgIdx, NULL};
    vkQueuePresentKHR(app->graphicsQueue, &pi); app->currentFrame = (app->currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    /* Decrement Shield Hit Timers */
    for (int s = 0; s < 6; s++) if (app->shieldHitTimers[s] > 0) app->shieldHitTimers[s]--;
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    VulkanApp* app = (VulkanApp*)glfwGetWindowUserPointer(window);
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
        return;
    }
    
    if (key == GLFW_KEY_LEFT) {
        app->angleY -= 5.0f;
    }
    if (key == GLFW_KEY_RIGHT) {
        app->angleY += 5.0f;
    }
    if (key == GLFW_KEY_UP) {
        app->angleX -= 2.5f; /* Decreasing angleX to tilt camera UP */
    }
    if (key == GLFW_KEY_DOWN) {
        app->angleX += 2.5f; /* Increasing angleX to tilt camera DOWN */
    }
    if (key == GLFW_KEY_PAGE_UP) {
        app->cameraDist -= 5.0f;
    }
    if (key == GLFW_KEY_PAGE_DOWN) {
        app->cameraDist += 5.0f;
    }
    if (key == GLFW_KEY_M) {
        app->shm_inspector_page = (app->shm_inspector_page == 0) ? 1 : 0;
    }
    if (app->shm_inspector_page > 0) {
        if (key == GLFW_KEY_N) { app->shm_inspector_page = (app->shm_inspector_page % 3) + 1; }
        if (key == GLFW_KEY_P) { app->shm_inspector_page = (app->shm_inspector_page == 1) ? 3 : app->shm_inspector_page - 1; }
    }
    if (key == GLFW_KEY_PAGE_DOWN) {
        app->cameraDist += 5.0f;
    }
    if (key == GLFW_KEY_SPACE) {
        app->autoRotate = !app->autoRotate;
    }
    
    if (app->cameraDist < 10.0f) app->cameraDist = 10.0f;
    if (app->angleX > 85.0f) app->angleX = 85.0f;
    if (app->angleX < -85.0f) app->angleX = -85.0f;
}

void mainLoop(VulkanApp* app) {
    srand(time(NULL));
    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(app->window)) {
        if (app->shm) {
            if (atomic_load(&app->shm->force_shutdown)) {
                break;
            }
            int r_idx = atomic_load(&app->shm->read_index);
            if (app->shm->buffers[r_idx].shm_force_shutdown) {
                break;
            }
        }
        glfwPollEvents();
        
        double currentTime = glfwGetTime();
        float deltaTime = (float)(currentTime - lastTime);
        lastTime = currentTime;

        if (app->autoRotate) {
            app->angleY -= (VIEW_ROTATION_SPEED * 60.0f * deltaTime);
            if (app->angleY <= 0.0f) app->angleY += 360.0f;
        }
        
        /* --- VIRTUAL SERVER CLOCK (Anti-Jitter) --- */
        /* Slowly adapt smoothed clock to match actual server packet arrival */
        double clock_err = currentTime - (app->last_shm_time + app->smoothed_shm_time);
        /* If clock drifted too much or first run, reset */
        if (fabs(clock_err) > 0.1 || app->smoothed_shm_time == 0) {
            app->smoothed_shm_time = 0;
        } else {
            /* Slowly close the gap to filter out network jitter */
            app->smoothed_shm_time += clock_err * 0.05; 
        }
        
        float rawAlpha = (float)((currentTime - app->last_shm_time - app->smoothed_shm_time) / 0.01666);
        float interpAlpha = rawAlpha;
        if (interpAlpha > 1.0f) interpAlpha = 1.0f;
        if (interpAlpha < 0.0f) interpAlpha = 0.0f;
        
        /* Cubic Hermite Spline weights */
        float t = interpAlpha;
        float t2 = t * t;
        float t3 = t2 * t;
        float h00 = 2*t3 - 3*t2 + 1;
        float h10 = t3 - 2*t2 + t;
        float h01 = -2*t3 + 3*t2;
        float h11 = t3 - t2;

        /* Apply interpolation to smooth state */
        for (int o=0; o < MAX_NET_OBJECTS; o++) {
            if (!app->smoothObjs[o].first) {
                float dx = app->smoothObjs[o].target_x - app->smoothObjs[o].prev_x;
                float dy = app->smoothObjs[o].target_y - app->smoothObjs[o].prev_y;
                float dz = app->smoothObjs[o].target_z - app->smoothObjs[o].prev_z;
                float distSq = dx*dx + dy*dy + dz*dz;

                /* Snap threshold: if moved > 10 units (teleport/jump), don't interpolate */
                if (distSq > 100.0f) {
                    app->smoothObjs[o].x = app->smoothObjs[o].target_x;
                    app->smoothObjs[o].y = app->smoothObjs[o].target_y;
                    app->smoothObjs[o].z = app->smoothObjs[o].target_z;
                } else {
                    /* PREMIUN CUBIC HERMITE INTERPOLATION: Uses position AND velocity for perfect fluid curves */
                    /* Note: Velocity must be scaled by the time interval (1 tick) */
                    app->smoothObjs[o].x = h00 * app->smoothObjs[o].prev_x + h10 * app->smoothObjs[o].prev_vx + h01 * app->smoothObjs[o].target_x + h11 * app->smoothObjs[o].vx;
                    app->smoothObjs[o].y = h00 * app->smoothObjs[o].prev_y + h10 * app->smoothObjs[o].prev_vy + h01 * app->smoothObjs[o].target_y + h11 * app->smoothObjs[o].vy;
                    app->smoothObjs[o].z = h00 * app->smoothObjs[o].prev_z + h10 * app->smoothObjs[o].prev_vz + h01 * app->smoothObjs[o].target_z + h11 * app->smoothObjs[o].vz;
                }
                
                /* Angle interpolation (Shortest Path) */
                float dh = app->smoothObjs[o].target_h - app->smoothObjs[o].prev_h;
                while (dh > 180.0f) { dh -= 360.0f; }
                while (dh < -180.0f) { dh += 360.0f; }
                app->smoothObjs[o].h = app->smoothObjs[o].prev_h + dh * interpAlpha;

                app->smoothObjs[o].m = app->smoothObjs[o].prev_m + (app->smoothObjs[o].target_m - app->smoothObjs[o].prev_m) * interpAlpha;

                float dr = app->smoothObjs[o].target_r - app->smoothObjs[o].prev_r;
                while (dr > 180.0f) { dr -= 360.0f; }
                while (dr < -180.0f) { dr += 360.0f; }
                app->smoothObjs[o].r = app->smoothObjs[o].prev_r + dr * interpAlpha;
                }

        }

        if (app->shm) {
            int r_idx = atomic_load_explicit(&app->shm->read_index, memory_order_acquire);
            GameState* st = &app->shm->buffers[r_idx];
            
            /* Detect New Server Frame */
            if (st->frame_id != app->last_shm_frame_id) {
                app->last_shm_frame_id = st->frame_id;
                app->last_shm_time = currentTime;

                for (int o=0; o < st->object_count && o < MAX_NET_OBJECTS; o++) {
                    SharedObject* obj = &st->objects[o];
                    if (!obj->active) { app->smoothObjs[o].first = true; continue; }
                    
                    if (app->smoothObjs[o].first) {
                        app->smoothObjs[o].prev_x = app->smoothObjs[o].target_x = app->smoothObjs[o].x = (float)obj->shm_x;
                        app->smoothObjs[o].prev_y = app->smoothObjs[o].target_y = app->smoothObjs[o].y = (float)obj->shm_y;
                        app->smoothObjs[o].prev_z = app->smoothObjs[o].target_z = app->smoothObjs[o].z = (float)obj->shm_z;
                        app->smoothObjs[o].prev_h = app->smoothObjs[o].target_h = app->smoothObjs[o].h = (float)obj->h;
                        app->smoothObjs[o].prev_m = app->smoothObjs[o].target_m = app->smoothObjs[o].m = (float)obj->m;
                        app->smoothObjs[o].prev_r = app->smoothObjs[o].target_r = app->smoothObjs[o].r = (float)obj->r;
                        app->smoothObjs[o].vx = (float)obj->vx;
                        app->smoothObjs[o].vy = (float)obj->vy;
                        app->smoothObjs[o].vz = (float)obj->vz;
                        app->smoothObjs[o].first = false;
                    } else {
                        /* Shift targets: Previous target becomes starting point for new interpolation interval */
                        app->smoothObjs[o].prev_x = app->smoothObjs[o].target_x;
                        app->smoothObjs[o].prev_y = app->smoothObjs[o].target_y;
                        app->smoothObjs[o].prev_z = app->smoothObjs[o].target_z;
                        app->smoothObjs[o].prev_h = app->smoothObjs[o].target_h;
                        app->smoothObjs[o].prev_m = app->smoothObjs[o].target_m;
                        app->smoothObjs[o].prev_r = app->smoothObjs[o].target_r;
                        app->smoothObjs[o].prev_vx = app->smoothObjs[o].vx;
                        app->smoothObjs[o].prev_vy = app->smoothObjs[o].vy;
                        app->smoothObjs[o].prev_vz = app->smoothObjs[o].vz;

                        app->smoothObjs[o].target_x = (float)obj->shm_x;
                        app->smoothObjs[o].target_y = (float)obj->shm_y;
                        app->smoothObjs[o].target_z = (float)obj->shm_z;
                        app->smoothObjs[o].target_h = (float)obj->h;
                        app->smoothObjs[o].target_m = (float)obj->m;
                        app->smoothObjs[o].target_r = (float)obj->r;
                        app->smoothObjs[o].vx = (float)obj->vx;
                        app->smoothObjs[o].vy = (float)obj->vy;
                        app->smoothObjs[o].vz = (float)obj->vz;
                    }
                }
                /* Torpedo State Sampling: Legacy path removed in favor of Zero-Loss event queue */
            }

            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 1000000;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000L;
            }
            /* Wait at most 1ms, but then process EVERYTHING in the queue */
            sem_timedwait(&app->shm->data_ready, &ts);
            
            int h = atomic_load_explicit(&app->shm->event_head, memory_order_acquire); 
            int t = atomic_load_explicit(&app->shm->event_tail, memory_order_acquire);
            
            /* Process all events currently in the queue */
            while (h != t) {
                IPCEvent *ev = &app->shm->event_queue[h];
                if (ev->type == IPC_EV_BOOM) {
                    int oldest = 0; float min_life = 999.0f;
                    for(int i=0; i<MAX_ACTIVE_BOOMS; i++) if(app->activeBooms[i].life < min_life){ min_life = app->activeBooms[i].life; oldest = i; }
                    int i = oldest;
                    /* Mapping: X_v = X_s - 20, Y_v = Z_s - 20, Z_v = 20 - Y_s */
                    app->activeBooms[i].x = (float)ev->x1 - 20.0f; 
                    app->activeBooms[i].y = (float)ev->z1 - 20.0f; 
                    app->activeBooms[i].z = 20.0f - (float)ev->y1; 
                    app->activeBooms[i].life = 1.0f;
                    for(int k=0; k<MAX_ACTIVE_TORPS; k++) {
                        if (app->activeTorps[k].active > 0) {
                            /* Prioritize ID matching for precise de-allocation (using IPC_TORPEDO_ID_OFFSET) */
                            if (ev->extra >= IPC_TORPEDO_ID_OFFSET && app->activeTorps[k].id == ev->extra) {
                                app->activeTorps[k].active = 0;
                                continue;
                            }
                            /* Fallback: Proximity check */
                            float dx = app->activeTorps[k].x - app->activeBooms[i].x;
                            float dy = app->activeTorps[k].y - app->activeBooms[i].y;
                            float dz = app->activeTorps[k].z - app->activeBooms[i].z;
                            if (sqrtf(dx*dx + dy*dy + dz*dz) < 2.5f) app->activeTorps[k].active = 0;
                        }
                    }
                    for(int p=0; p<EXPLOSION_PIXELS; p++){ 
                        app->activeBooms[i].offsets[p][0] = (float)(rand()%800-400)/100.0f; 
                        app->activeBooms[i].offsets[p][1] = (float)(rand()%800-400)/100.0f; 
                        app->activeBooms[i].offsets[p][2] = (float)(rand()%800-400)/100.0f;
                        float r=1, g=1, b=1; switch(p % 6){ case 0: r=0; g=1; b=1; break; case 1: r=1; g=0; b=1; break; case 2: r=1; g=1; b=0; break; case 3: r=1; g=0; b=0; break; case 4: r=0; g=1; b=0; break; case 5: r=1; g=0.5; b=0; break; }
                        app->activeBooms[i].colors[p][0]=r; app->activeBooms[i].colors[p][1]=g; app->activeBooms[i].colors[p][2]=b;
                    }
                } else if (ev->type == IPC_EV_BEAM) {
                    int slot = -1;
                    int owner_id = ev->padding[0];
                    int target_id = ev->extra;
                    int emitter_id = ev->padding[1];

                    /* Deduplication: Refresh existing beam if found (matching owner, target, AND emitter) */
                    for (int i=0; i<MAX_ACTIVE_BEAMS; i++) {
                        if (app->activeBeams[i].life > 0 && 
                            app->activeBeams[i].owner_id == owner_id && 
                            app->activeBeams[i].extra == target_id &&
                            app->activeBeams[i].emitter_id == emitter_id) {
                            slot = i; break;
                        }
                    }
                    if (slot == -1) {
                        int oldest = 0; float min_life = 999.0f;
                        for(int i=0; i<MAX_ACTIVE_BEAMS; i++) if(app->activeBeams[i].life < min_life){ min_life = app->activeBeams[i].life; oldest = i; }
                        slot = oldest;
                    }
                    app->activeBeams[slot].sx = (float)ev->x1 - 20.0f;
                    app->activeBeams[slot].sy = (float)ev->z1 - 20.0f;
                    app->activeBeams[slot].sz = 20.0f - (float)ev->y1;
                    app->activeBeams[slot].tx = (float)ev->x2 - 20.0f;
                    app->activeBeams[slot].ty = (float)ev->z2 - 20.0f;
                    app->activeBeams[slot].tz = 20.0f - (float)ev->y2;
                    app->activeBeams[slot].life = 1.0f;
                    app->activeBeams[slot].owner_id = owner_id;
                    app->activeBeams[slot].extra = target_id;
                    app->activeBeams[slot].emitter_id = emitter_id;
                } else if (ev->type == IPC_EV_TORPEDO) {
                    int tid = ev->extra; int f = -1;
                    for(int i=0; i<MAX_ACTIVE_TORPS; i++) { if (app->activeTorps[i].active>0 && app->activeTorps[i].id==tid) { f=i; break; } }
                    if (f == -1) {
                        int oldest = 0; float min_life = 999.0f;
                        for(int i=0; i<MAX_ACTIVE_TORPS; i++) if((float)app->activeTorps[i].active < min_life){ min_life = (float)app->activeTorps[i].active; oldest = i; }
                        f = oldest;
                    }
                    float nx = (float)ev->x1 - 20.0f; float ny = (float)ev->z1 - 20.0f; float nz = 20.0f - (float)ev->y1;
                    float vx = (float)ev->x2; float vy = (float)ev->z2; float vz = -(float)ev->y2;
                    if (app->activeTorps[f].active > 0) {
                        /* Converge local position towards server position to eliminate drift */
                        /* Incremental correction to fix drift while avoiding jumps */
                        app->activeTorps[f].x = app->activeTorps[f].x * 0.85f + nx * 0.15f;
                        app->activeTorps[f].y = app->activeTorps[f].y * 0.85f + ny * 0.15f;
                        app->activeTorps[f].z = app->activeTorps[f].z * 0.85f + nz * 0.15f;
                        /* Use server velocity directly */
                        app->activeTorps[f].dx = vx;
                        app->activeTorps[f].dy = vy;
                        app->activeTorps[f].dz = vz;
                    } else {
                        app->activeTorps[f].x = nx; app->activeTorps[f].y = ny; app->activeTorps[f].z = nz;
                        app->activeTorps[f].dx = vx; app->activeTorps[f].dy = vy; app->activeTorps[f].dz = vz;
                    }
                    app->activeTorps[f].id = tid; 
                    app->activeTorps[f].active = 2000;
                } else if (ev->type == IPC_EV_DISMANTLE) {
                    if (app->shm) app->shm->dismantle_telemetry.vk_rcv_count++;
                    int oldest = 0; float min_life = 999.0f;
                    for(int i=0; i<MAX_ACTIVE_DISMANTLES; i++) if(app->activeDismantles[i].life < min_life){ min_life = app->activeDismantles[i].life; oldest = i; }
                    app->activeDismantles[oldest].x = (float)ev->x1 - 20.0f;
                    app->activeDismantles[oldest].y = (float)ev->z1 - 20.0f;
                    app->activeDismantles[oldest].z = 20.0f - (float)ev->y1;
                    app->activeDismantles[oldest].life = 1.0f;
                    app->activeDismantles[oldest].scale = 1.0f;
                    int oldest_boom = 0; min_life = 999.0f;
                    for(int i=0; i<MAX_ACTIVE_BOOMS; i++) if(app->activeBooms[i].life < min_life){ min_life = app->activeBooms[i].life; oldest_boom = i; }
                    app->activeBooms[oldest_boom].x = app->activeDismantles[oldest].x;
                    app->activeBooms[oldest_boom].y = app->activeDismantles[oldest].y;
                    app->activeBooms[oldest_boom].z = app->activeDismantles[oldest].z;
                    app->activeBooms[oldest_boom].life = 1.2f;
                    for(int p=0; p<EXPLOSION_PIXELS; p++){
                        float speed = (p < 32) ? (3.5f + (rand()%200)/100.0f) : (1.0f + (rand()%250)/100.0f);
                        float theta = (float)(rand()%3600) * 0.1f * M_PI / 180.0f;
                        float phi = (float)(rand()%1800 - 900) * 0.1f * M_PI / 180.0f;
                        app->activeBooms[oldest_boom].offsets[p][0] = speed * cosf(phi) * cosf(theta);
                        app->activeBooms[oldest_boom].offsets[p][1] = speed * sinf(phi);
                        app->activeBooms[oldest_boom].offsets[p][2] = speed * cosf(phi) * sinf(theta);
                        float r=0.2f, g=0.8f, b=1.0f; 
                        if (p % 3 == 0) { r=1.0f; g=1.0f; b=1.0f; }
                        else if (p % 5 == 0) { r=0.0f; g=0.5f; b=1.0f; }
                        app->activeBooms[oldest_boom].colors[p][0]=r; app->activeBooms[oldest_boom].colors[p][1]=g; app->activeBooms[oldest_boom].colors[p][2]=b;
                    }
                } else if (ev->type == IPC_EV_RECOVERY) {
                    int oldest = 0; float min_life = 999.0f;
                    for(int i=0; i<MAX_ACTIVE_DISMANTLES; i++) if(app->activeDismantles[i].life < min_life){ min_life = app->activeDismantles[i].life; oldest = i; }
                    app->activeDismantles[oldest].x = (float)ev->x1 - 20.0f;
                    app->activeDismantles[oldest].y = (float)ev->z1 - 20.0f;
                    app->activeDismantles[oldest].z = 20.0f - (float)ev->y1;
                    app->activeDismantles[oldest].life = 1.0f;
                    app->activeDismantles[oldest].scale = 0.5f;
                } else if (ev->type == IPC_EV_JUMP) {
                    app->jumpArrival.x = app->smoothObjs[0].first ? ((float)ev->x1 - 20.0f) : (app->smoothObjs[0].x - 20.0f);
                    app->jumpArrival.y = app->smoothObjs[0].first ? ((float)ev->z1 - 20.0f) : (app->smoothObjs[0].z - 20.0f);
                    app->jumpArrival.z = app->smoothObjs[0].first ? (20.0f - (float)ev->y1) : (20.0f - app->smoothObjs[0].y);
                    app->jumpArrival.active = 1;
                    app->jumpArrival.timer = 540;
                    for (int i = 0; i < MAX_ARRIVAL_PARTICLES; i++) {
                        app->arrivalParticles[i].active = 1;
                        app->arrivalParticles[i].angle = (float)(rand() % 3600) * 0.1f * M_PI / 180.0f;
                        app->arrivalParticles[i].radius = 30.0f + (float)(rand() % 600) * 0.1f;
                        app->arrivalParticles[i].speed = 3.0f + (float)(rand() % 40) * 0.1f;
                        app->arrivalParticles[i].x = 0; app->arrivalParticles[i].y = 0; app->arrivalParticles[i].z = 0;
                    }
                }
                h = (h + 1) % IPC_EVENT_QUEUE_SIZE;
            }
            atomic_store_explicit(&app->shm->event_head, h, memory_order_release);

            /* Sync Map State */
            app->mapFilter = st->shm_map_filter;
            
            /* Shield hit detection moved to drawShieldEffect() for engine consistency */

            if (st->shm_show_map) {
                if (app->mapAnim < 1.0f) app->mapAnim += 0.04f;
                if (app->mapAnim > 1.0f) app->mapAnim = 1.0f;
            } else {
                if (app->mapAnim > 0.0f) app->mapAnim -= 0.04f;
                if (app->mapAnim < 0.0f) app->mapAnim = 0.0f;
            }

        /* Arrival Wormhole Persistence Logic */
        if (app->jumpArrival.active) {
            /* Only decrement timer if ship has stopped moving (IDLE, DOCKING or DRIFT) */
            if (st->shm_nav_state == 0 || st->shm_nav_state == 9 || st->shm_nav_state == 10) {
                if (app->jumpArrival.timer > 0) app->jumpArrival.timer--;
                else app->jumpArrival.active = 0;
            } else {
                /* Keep timer alive while ship is still navigating/approaching */
                if (app->jumpArrival.timer < 180) app->jumpArrival.timer = 180;
            }        }

        /* Update Active Booms (Explosions) and Torpedoes */
        for (int i = 0; i < MAX_ACTIVE_BEAMS; i++) {
            if (app->activeBeams[i].life > 0) {
                app->activeBeams[i].life -= deltaTime / 0.4f;
            }
        }
        for (int i = 0; i < MAX_ACTIVE_BOOMS; i++) {
            if (app->activeBooms[i].life > 0) {
                app->activeBooms[i].life -= deltaTime; /* Frame-rate independent (approx 1s total) */
            }
        }
        for (int i = 0; i < MAX_ACTIVE_DISMANTLES; i++) {
            if (app->activeDismantles[i].life > 0) {
                app->activeDismantles[i].life -= deltaTime * 0.5f; /* Slower fade (2s total) */
            }
        }

        /* Update Arrival Particles */
        if (app->jumpArrival.active && app->jumpArrival.timer > 0) {
            for (int i = 0; i < MAX_ARRIVAL_PARTICLES; i++) {
                if (!app->arrivalParticles[i].active) continue;
                /* Spiral inwards */
                app->arrivalParticles[i].radius -= app->arrivalParticles[i].speed * deltaTime;
                app->arrivalParticles[i].angle += 2.0f * deltaTime;
                
                if (app->arrivalParticles[i].radius < 0.5f) {
                    /* Reset to outer edge for continuous flow */
                    app->arrivalParticles[i].radius = 40.0f + (float)(rand() % 200) * 0.1f;
                }
                
                /* Calculate local position (Z-aligned plane) */
                app->arrivalParticles[i].x = app->arrivalParticles[i].radius * cosf(app->arrivalParticles[i].angle);
                app->arrivalParticles[i].y = app->arrivalParticles[i].radius * sinf(app->arrivalParticles[i].angle);
                /* Random Z variation */
                app->arrivalParticles[i].z = (float)(rand() % 200 - 100) * 0.01f * (app->arrivalParticles[i].radius * 0.1f);
            }
        }

        /* Decrement torpedo timers based on time (60 units = approx 1s) */
        static float torpAcc = 0;
        torpAcc += deltaTime;
        if (torpAcc >= 0.01666f) {
            for(int j=0; j<MAX_ACTIVE_TORPS; j++) if(app->activeTorps[j].active > 0) app->activeTorps[j].active--;
            torpAcc = 0;
        }

        for (int i = 0; i < MAX_ACTIVE_TORPS; i++) {
            if (app->activeTorps[i].active > 0) {
                /* Move torpedoes based on their velocity, scaled by deltaTime */
                /* Displacement dx is distance per server frame (1/60s) */
                float speedScale = deltaTime / 0.01666f;
                app->activeTorps[i].x += app->activeTorps[i].dx * speedScale;
                app->activeTorps[i].y += app->activeTorps[i].dy * speedScale;
                app->activeTorps[i].z += app->activeTorps[i].dz * speedScale;

                /* Boundary check: Deactivate torpedo if it leaves the quadrant ([-20, 20] range) 
                   Matching server's 0.1 margin: 20.0 - 0.1 = 19.9. But we allow 20.1 for visual tolerance */
                if (fabsf(app->activeTorps[i].x) > 20.1f || 
                    fabsf(app->activeTorps[i].y) > 20.1f || 
                    fabsf(app->activeTorps[i].z) > 20.1f) {
                    app->activeTorps[i].active = 0;
                }
            }
        }

        /* Update departure wormhole only when it becomes newly active (freeze position once set) */
        if (st->wormhole.active) {
            if (!app->departureWormhole.active) {
                /* First activation: latch position and orientation */
                app->departureWormhole.x = (float)st->wormhole.shm_x - 20.0f;
                app->departureWormhole.y = (float)st->wormhole.shm_z - 20.0f;
                app->departureWormhole.z = 20.0f - (float)st->wormhole.shm_y;
                app->departureWormhole.h = st->shm_h;
                app->departureWormhole.m = st->shm_m;
            }
            app->departureWormhole.active = 1;
        } else {
            app->departureWormhole.active = 0;
        }
        /* Only update arrival orientation when arrival is not active (preserve locked orientation) */
        if (!app->jumpArrival.active) {
            app->jumpArrival.h = st->shm_h;
            app->jumpArrival.m = st->shm_m;
        }

            /* Sincronizzazione dello stato della Vista Bridge */
            app->showBridge = st->shm_show_bridge;
            if (app->showBridge) {
                /* Transizione verso la vista bridge (0.0 -> 1.0) */
                if (app->bridgeAnim < 1.0f) app->bridgeAnim += 0.03f;
                if (app->bridgeAnim > 1.0f) app->bridgeAnim = 1.0f;
            } else {
                /* Ritorno alla vista orbitale (1.0 -> 0.0) */
                if (app->bridgeAnim > 0.0f) app->bridgeAnim -= 0.03f;
                if (app->bridgeAnim < 0.0f) app->bridgeAnim = 0.0f;
            }
        }
        drawFrame(app);
    }
}

void createStarfield(VulkanApp* app) {
#if STARFIELD_MODE == STARFIELD_MODE_BLOOM
    /* Single vertex per star for point rendering */
    uint64_t total_verts = MAX_STARS;
    uint64_t total_inds = MAX_STARS;
    Vertex* v = malloc(total_verts * sizeof(Vertex));
    uint32_t* inds = malloc(total_inds * sizeof(uint32_t));
    for (uint64_t s = 0; s < MAX_STARS; s++) {
        float theta = (float)(rand() % 3600) * 0.1f * M_PI / 180.0f;
        float phi = (float)(rand() % 1800) * 0.1f * M_PI / 180.0f;
        float r = 150.0f + (float)(rand() % 1500) * 0.1f;
        float sx = r * sinf(phi) * cosf(theta);
        float sy = r * cosf(phi);
        float sz = r * sinf(phi) * sinf(theta);
        float cr = 0.5f + (float)(rand() % 50) / 100.0f;
        float cg = 0.5f + (float)(rand() % 50) / 100.0f;
        float cb = 0.5f + (float)(rand() % 50) / 100.0f;
        v[s] = (Vertex){{sx, sy, sz}, {cr, cg, cb}, {0, 0, 0}};
        inds[s] = (uint32_t)s;
    }
#else
    int s_lats = 4, s_longs = 8;
    int multiplier = (STARFIELD_MODE == STARFIELD_MODE_HALO) ? 2 : 1;
    uint64_t verts_per_star = (uint64_t)(s_lats + 1) * (s_longs + 1);
    uint64_t inds_per_star = (uint64_t)s_lats * s_longs * 6;
    uint64_t total_verts = (uint64_t)MAX_STARS * verts_per_star * multiplier;
    uint64_t total_inds = (uint64_t)MAX_STARS * inds_per_star * multiplier;
    Vertex* v = malloc(total_verts * sizeof(Vertex));
    uint32_t* inds = malloc(total_inds * sizeof(uint32_t));
    uint64_t vc = 0, ic = 0;
    for (uint64_t s = 0; s < MAX_STARS; s++) {
        float theta = (float)(rand() % 3600) * 0.1f * M_PI / 180.0f;
        float phi = (float)(rand() % 1800) * 0.1f * M_PI / 180.0f;
        float r = 120.0f + (float)(rand() % 1000) * 0.1f;
        float sx = r * sinf(phi) * cosf(theta); float sy = r * cosf(phi); float sz = r * sinf(phi) * sinf(theta);
        float cr = 0.4f + (float)(rand() % 60) / 100.0f;
        float cg = 0.4f + (float)(rand() % 60) / 100.0f;
        float cb = 0.4f + (float)(rand() % 60) / 100.0f;
        float scale = 0.1f + (float)(rand() % 50) * 0.01f;
        for (int m = 0; m < multiplier; m++) {
            float s_scale = (m == 0) ? scale : scale * 2.5f;
            float s_alpha = (m == 0) ? 1.0f : 0.3f;
            uint64_t base_v = vc;
            for (int i = 0; i <= s_lats; i++) {
                float lat = M_PI * i / s_lats;
                for (int j = 0; j <= s_longs; j++) {
                    float lon = 2.0f * M_PI * j / s_longs;
                    float px = sx + s_scale * sinf(lat) * cosf(lon);
                    float py = sy + s_scale * cosf(lat);
                    float pz = sz + s_scale * sinf(lat) * sinf(lon);
                    /* We encode alpha in the normal for the Halo mode if needed, 
                       but here we just use the color. Shaders handle it. */
                    v[vc++] = (Vertex){{px, py, pz}, {cr * s_alpha, cg * s_alpha, cb * s_alpha}, {0, 1, 0}};
                }
            }
            for (int i = 0; i < s_lats; i++) {
                for (int j = 0; j < s_longs; j++) {
                    uint32_t first = (uint32_t)(base_v + i * (s_longs + 1) + j);
                    uint32_t second = first + (uint32_t)s_longs + 1;
                    inds[ic++] = first; inds[ic++] = second; inds[ic++] = first + 1;
                    inds[ic++] = second; inds[ic++] = second + 1; inds[ic++] = first + 1;
                }
            }
        }
    }
#endif
    uint64_t final_inds = (STARFIELD_MODE == STARFIELD_MODE_BLOOM) ? MAX_STARS : total_inds;
    app->starfieldIndexCount = final_inds;
    void* d;
    createBuffer(app, total_verts * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->starfieldVertexBuffer, &app->starfieldVertexBufferMemory);
    vkMapMemory(app->device, app->starfieldVertexBufferMemory, 0, total_verts * sizeof(Vertex), 0, &d); memcpy(d, v, total_verts * sizeof(Vertex)); vkUnmapMemory(app->device, app->starfieldVertexBufferMemory);
    createBuffer(app, final_inds * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->starfieldIndexBuffer, &app->starfieldIndexBufferMemory);
    vkMapMemory(app->device, app->starfieldIndexBufferMemory, 0, final_inds * sizeof(uint32_t), 0, &d); memcpy(d, inds, final_inds * sizeof(uint32_t)); vkUnmapMemory(app->device, app->starfieldIndexBufferMemory);
    free(v); free(inds);
}

void initVulkan(VulkanApp* app) {
    createInstance(app); if (glfwCreateWindowSurface(app->instance, app->window, NULL, &app->surface) != VK_SUCCESS) exit(1);
    pickPhysicalDevice(app); createLogicalDevice(app); createSwapChain(app);
    app->swapChainImageViews = malloc(sizeof(VkImageView)*app->swapChainImageCount); for(uint32_t i=0; i<app->swapChainImageCount; i++) app->swapChainImageViews[i] = createImageView(app->device, app->swapChainImages[i], app->swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    createRenderPass(app); createGraphicsPipeline(app);
    VkCommandPoolCreateInfo cpInf = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0, 0}; vkCreateCommandPool(app->device, &cpInf, NULL, &app->commandPool);
    createColorResources(app); createDepthResources(app);
    app->swapChainFramebuffers = malloc(sizeof(VkFramebuffer)*app->swapChainImageCount); for(size_t i=0; i<app->swapChainImageCount; i++) { VkImageView at[] = {app->colorImageView, app->depthImageView, app->swapChainImageViews[i]}; VkFramebufferCreateInfo fi = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,NULL,0,app->renderPass,3,at,WIDTH,HEIGHT,1}; vkCreateFramebuffer(app->device, &fi, NULL, &app->swapChainFramebuffers[i]); }
    void* d; createBuffer(app, sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->vertexBuffer, &app->vertexBufferMemory); vkMapMemory(app->device, app->vertexBufferMemory, 0, sizeof(vertices), 0, &d); memcpy(d, vertices, sizeof(vertices)); vkUnmapMemory(app->device, app->vertexBufferMemory);
    createBuffer(app, sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->indexBuffer, &app->indexBufferMemory); vkMapMemory(app->device, app->indexBufferMemory, 0, sizeof(indices), 0, &d); memcpy(d, indices, sizeof(indices)); vkUnmapMemory(app->device, app->indexBufferMemory);
    createBuffer(app, sizeof(shipVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->shipVertexBuffer, &app->shipVertexBufferMemory);
    vkMapMemory(app->device, app->shipVertexBufferMemory, 0, sizeof(shipVertices), 0, &d);
    memcpy(d, shipVertices, sizeof(shipVertices));
    vkUnmapMemory(app->device, app->shipVertexBufferMemory);
    
    createBuffer(app, sizeof(shipIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->shipIndexBuffer, &app->shipIndexBufferMemory);
    vkMapMemory(app->device, app->shipIndexBufferMemory, 0, sizeof(shipIndices), 0, &d);
    memcpy(d, shipIndices, sizeof(shipIndices));
    vkUnmapMemory(app->device, app->shipIndexBufferMemory);

    createBuffer(app, sizeof(starbaseVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->starbaseVertexBuffer, &app->starbaseVertexBufferMemory);
    vkMapMemory(app->device, app->starbaseVertexBufferMemory, 0, sizeof(starbaseVertices), 0, &d);
    memcpy(d, starbaseVertices, sizeof(starbaseVertices));
    vkUnmapMemory(app->device, app->starbaseVertexBufferMemory);

    createBuffer(app, sizeof(starbaseIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->starbaseIndexBuffer, &app->starbaseIndexBufferMemory);
    vkMapMemory(app->device, app->starbaseIndexBufferMemory, 0, sizeof(starbaseIndices), 0, &d);
    memcpy(d, starbaseIndices, sizeof(starbaseIndices));
    vkUnmapMemory(app->device, app->starbaseIndexBufferMemory);
    createBuffer(app, sizeof(torpVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->torpVertexBuffer, &app->torpVertexBufferMemory); vkMapMemory(app->device, app->torpVertexBufferMemory, 0, sizeof(torpVertices), 0, &d); memcpy(d, torpVertices, sizeof(torpVertices)); vkUnmapMemory(app->device, app->torpVertexBufferMemory);
    createBuffer(app, sizeof(torpIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->torpIndexBuffer, &app->torpIndexBufferMemory); vkMapMemory(app->device, app->torpIndexBufferMemory, 0, sizeof(torpIndices), 0, &d); memcpy(d, torpIndices, sizeof(torpIndices)); vkUnmapMemory(app->device, app->torpIndexBufferMemory);
    createBuffer(app, sizeof(cubeVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->cubeVertexBuffer, &app->cubeVertexBufferMemory); vkMapMemory(app->device, app->cubeVertexBufferMemory, 0, sizeof(cubeVertices), 0, &d); memcpy(d, cubeVertices, sizeof(cubeVertices)); vkUnmapMemory(app->device, app->cubeVertexBufferMemory);
    createBuffer(app, sizeof(cubeIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->cubeIndexBuffer, &app->cubeIndexBufferMemory); vkMapMemory(app->device, app->cubeIndexBufferMemory, 0, sizeof(cubeIndices), 0, &d); memcpy(d, cubeIndices, sizeof(cubeIndices)); vkUnmapMemory(app->device, app->cubeIndexBufferMemory);
    createBuffer(app, sizeof(cubeSolidIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->cubeSolidIndexBuffer, &app->cubeSolidIndexBufferMemory); vkMapMemory(app->device, app->cubeSolidIndexBufferMemory, 0, sizeof(cubeSolidIndices), 0, &d); memcpy(d, cubeSolidIndices, sizeof(cubeSolidIndices)); vkUnmapMemory(app->device, app->cubeSolidIndexBufferMemory);
    
    /* Tactical Compass Buffers */
    createBuffer(app, sizeof(axesVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->axesVertexBuffer, &app->axesVertexBufferMemory); vkMapMemory(app->device, app->axesVertexBufferMemory, 0, sizeof(axesVertices), 0, &d); memcpy(d, axesVertices, sizeof(axesVertices)); vkUnmapMemory(app->device, app->axesVertexBufferMemory);
    createBuffer(app, sizeof(axesIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->axesIndexBuffer, &app->axesIndexBufferMemory); vkMapMemory(app->device, app->axesIndexBufferMemory, 0, sizeof(axesIndices), 0, &d); memcpy(d, axesIndices, sizeof(axesIndices)); vkUnmapMemory(app->device, app->axesIndexBufferMemory);

    /* Dynamic Beam Buffers (2 Vertices, 2 Indices) */
    createBuffer(app, 2 * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->beamVertexBuffer, &app->beamVertexBufferMemory);
    uint32_t beamInds[] = {0, 1};
    createBuffer(app, sizeof(beamInds), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->beamIndexBuffer, &app->beamIndexBufferMemory); vkMapMemory(app->device, app->beamIndexBufferMemory, 0, sizeof(beamInds), 0, &d); memcpy(d, beamInds, sizeof(beamInds)); vkUnmapMemory(app->device, app->beamIndexBufferMemory);

    Vertex cVerts[CIRCLE_SEGMENTS]; uint32_t cInds[CIRCLE_SEGMENTS * 2];
    for(int i=0; i<CIRCLE_SEGMENTS; i++) {
        float rad = (i * 5.0f) * M_PI / 180.0f;
        cVerts[i] = (Vertex){{sinf(rad), 0, cosf(rad)}, {1,1,1}, {0,1,0}};
        cInds[i*2] = i; cInds[i*2+1] = (i+1)%CIRCLE_SEGMENTS;
    }
    createBuffer(app, sizeof(cVerts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->circleVertexBuffer, &app->circleVertexBufferMemory); vkMapMemory(app->device, app->circleVertexBufferMemory, 0, sizeof(cVerts), 0, &d); memcpy(d, cVerts, sizeof(cVerts)); vkUnmapMemory(app->device, app->circleVertexBufferMemory);
    createBuffer(app, sizeof(cInds), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->circleIndexBuffer, &app->circleIndexBufferMemory); vkMapMemory(app->device, app->circleIndexBufferMemory, 0, sizeof(cInds), 0, &d); memcpy(d, cInds, sizeof(cInds)); vkUnmapMemory(app->device, app->circleIndexBufferMemory);

    Vertex aVerts[ARC_SEGMENTS]; uint32_t aInds[(ARC_SEGMENTS-1) * 2];
    for(int i=0; i<ARC_SEGMENTS; i++) {
        float rad = ((i * 5.0f) - 90.0f) * M_PI / 180.0f;
        /* Longitudinal Plane (XY) */
        aVerts[i] = (Vertex){{cosf(rad), sinf(rad), 0}, {1,1,0}, {0,0,1}};
        if (i < ARC_SEGMENTS - 1) { aInds[i*2] = i; aInds[i*2+1] = i+1; }
    }
    createBuffer(app, sizeof(aVerts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->arcVertexBuffer, &app->arcVertexBufferMemory); vkMapMemory(app->device, app->arcVertexBufferMemory, 0, sizeof(aVerts), 0, &d); memcpy(d, aVerts, sizeof(aVerts)); vkUnmapMemory(app->device, app->arcVertexBufferMemory);
    createBuffer(app, sizeof(aInds), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->arcIndexBuffer, &app->arcIndexBufferMemory); vkMapMemory(app->device, app->arcIndexBufferMemory, 0, sizeof(aInds), 0, &d); memcpy(d, aInds, sizeof(aInds)); vkUnmapMemory(app->device, app->arcIndexBufferMemory);

    Vertex rVerts[CIRCLE_SEGMENTS]; uint32_t rInds[CIRCLE_SEGMENTS * 2];
    for(int i=0; i<CIRCLE_SEGMENTS; i++) {
        float rad = (i * 5.0f) * M_PI / 180.0f;
        /* Transversal Plane (YZ) */
        rVerts[i] = (Vertex){{0, sinf(rad), cosf(rad)}, {1,1,0}, {1,0,0}};
        rInds[i*2] = i; rInds[i*2+1] = (i+1)%CIRCLE_SEGMENTS;
    }
    createBuffer(app, sizeof(rVerts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->rollCircleVertexBuffer, &app->rollCircleVertexBufferMemory); vkMapMemory(app->device, app->rollCircleVertexBufferMemory, 0, sizeof(rVerts), 0, &d); memcpy(d, rVerts, sizeof(rVerts)); vkUnmapMemory(app->device, app->rollCircleVertexBufferMemory);
    createBuffer(app, sizeof(rInds), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->rollCircleIndexBuffer, &app->rollCircleIndexBufferMemory); vkMapMemory(app->device, app->rollCircleIndexBufferMemory, 0, sizeof(rInds), 0, &d); memcpy(d, rInds, sizeof(rInds)); vkUnmapMemory(app->device, app->rollCircleIndexBufferMemory);

    /* Tactical Grid (Full 3D Quadrant Cube) - Increased density */
    int steps = (int)QUADRANT_SIZE / 2;
    uint64_t grid_v_count = (uint64_t)(steps + 1) * (steps + 1) * 3 * 2;
    Vertex* gVerts_dyn = malloc(grid_v_count * sizeof(Vertex));
    uint32_t* gInds_dyn = malloc(grid_v_count * sizeof(uint32_t));
    uint64_t gv = 0;
    float hq = QUADRANT_SIZE / 2.0f;
    for (int i = 0; i <= steps; i++) {
        float p = -hq + i * 2.0f;
        for (int j = 0; j <= steps; j++) {
            float q = -hq + j * 2.0f;
            /* Vivid Green, Zero Normal (no shading) */
            vec3 gcol = {0.0f, 1.0f, 0.2f}, gnorm = {0,0,0};
            /* Lines parallel to Z */
            gVerts_dyn[gv] = (Vertex){{p, q, -hq}, {gcol[0], gcol[1], gcol[2]}, {gnorm[0], gnorm[1], gnorm[2]}}; gInds_dyn[gv] = (uint32_t)gv; gv++;
            gVerts_dyn[gv] = (Vertex){{p, q,  hq}, {gcol[0], gcol[1], gcol[2]}, {gnorm[0], gnorm[1], gnorm[2]}}; gInds_dyn[gv] = (uint32_t)gv; gv++;
            /* Lines parallel to Y */
            gVerts_dyn[gv] = (Vertex){{p, -hq, q}, {gcol[0], gcol[1], gcol[2]}, {gnorm[0], gnorm[1], gnorm[2]}}; gInds_dyn[gv] = (uint32_t)gv; gv++;
            gVerts_dyn[gv] = (Vertex){{p,  hq, q}, {gcol[0], gcol[1], gcol[2]}, {gnorm[0], gnorm[1], gnorm[2]}}; gInds_dyn[gv] = (uint32_t)gv; gv++;
            /* Lines parallel to X */
            gVerts_dyn[gv] = (Vertex){{-hq, p, q}, {gcol[0], gcol[1], gcol[2]}, {gnorm[0], gnorm[1], gnorm[2]}}; gInds_dyn[gv] = (uint32_t)gv; gv++;
            gVerts_dyn[gv] = (Vertex){{ hq, p, q}, {gcol[0], gcol[1], gcol[2]}, {gnorm[0], gnorm[1], gnorm[2]}}; gInds_dyn[gv] = (uint32_t)gv; gv++;
        }
    }
    createBuffer(app, gv * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->gridVertexBuffer, &app->gridVertexBufferMemory); vkMapMemory(app->device, app->gridVertexBufferMemory, 0, gv * sizeof(Vertex), 0, &d); memcpy(d, gVerts_dyn, gv * sizeof(Vertex)); vkUnmapMemory(app->device, app->gridVertexBufferMemory);
    createBuffer(app, gv * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->gridIndexBuffer, &app->gridIndexBufferMemory); vkMapMemory(app->device, app->gridIndexBufferMemory, 0, gv * sizeof(uint32_t), 0, &d); memcpy(d, gInds_dyn, gv * sizeof(uint32_t)); vkUnmapMemory(app->device, app->gridIndexBufferMemory);
    app->gridVertexCount = gv;
    free(gVerts_dyn); free(gInds_dyn);

    createBuffer(app, sizeof(vectorVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->vectorVertexBuffer, &app->vectorVertexBufferMemory); vkMapMemory(app->device, app->vectorVertexBufferMemory, 0, sizeof(vectorVertices), 0, &d); memcpy(d, vectorVertices, sizeof(vectorVertices)); vkUnmapMemory(app->device, app->vectorVertexBufferMemory);
    createBuffer(app, sizeof(vectorIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->vectorIndexBuffer, &app->vectorIndexBufferMemory); vkMapMemory(app->device, app->vectorIndexBufferMemory, 0, sizeof(vectorIndices), 0, &d); memcpy(d, vectorIndices, sizeof(vectorIndices)); vkUnmapMemory(app->device, app->vectorIndexBufferMemory);

    /* Sphere Geometry (for planets, stars, singularities) */
    Vertex sVerts[(SPHERE_LATS + 1) * (SPHERE_LONGS + 1)];
    uint32_t sInds[SPHERE_LATS * SPHERE_LONGS * 6];
    int sv = 0;
    for (int i = 0; i <= SPHERE_LATS; i++) {
        float lat = M_PI * i / SPHERE_LATS;
        for (int j = 0; j <= SPHERE_LONGS; j++) {
            float lon = 2.0f * M_PI * j / SPHERE_LONGS;
            float x = sinf(lat) * cosf(lon);
            float y = cosf(lat);
            float z = sinf(lat) * sinf(lon);
            sVerts[sv++] = (Vertex){{x, y, z}, {1,1,1}, {x,y,z}};
        }
    }
    int sphere_idx = 0;
    for (int i = 0; i < SPHERE_LATS; i++) {
        for (int j = 0; j < SPHERE_LONGS; j++) {
            int first = i * (SPHERE_LONGS + 1) + j;
            int second = first + SPHERE_LONGS + 1;
            sInds[sphere_idx++] = first; sInds[sphere_idx++] = second; sInds[sphere_idx++] = first + 1;
            sInds[sphere_idx++] = second; sInds[sphere_idx++] = second + 1; sInds[sphere_idx++] = first + 1;
        }
    }
    createBuffer(app, sizeof(sVerts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->sphereVertexBuffer, &app->sphereVertexBufferMemory); vkMapMemory(app->device, app->sphereVertexBufferMemory, 0, sizeof(sVerts), 0, &d); memcpy(d, sVerts, sizeof(sVerts)); vkUnmapMemory(app->device, app->sphereVertexBufferMemory);
    createBuffer(app, sizeof(sInds), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->sphereIndexBuffer, &app->sphereIndexBufferMemory); vkMapMemory(app->device, app->sphereIndexBufferMemory, 0, sizeof(sInds), 0, &d); memcpy(d, sInds, sizeof(sInds)); vkUnmapMemory(app->device, app->sphereIndexBufferMemory);

    createStarfield(app);

    /* Wormhole Funnel Geometry (One side only, positive Z) */
    Vertex whVerts[WH_NR * WH_NT];
    uint32_t whInds[(WH_NR * WH_NT + WH_NT * (WH_NR - 1)) * 2];
    int whv = 0, whi = 0;
    float wh_rs = 0.45f, wh_rmax = 1.6f;
    for (int i = 0; i < WH_NR; i++) {
        float r = wh_rs + i * (wh_rmax - wh_rs) / (WH_NR - 1);
        /* base_z: Distanza di simmetria tra i due funnels (imbuti) */
        float base_z = DISTANZA_SIMMETRIA_FUNNELS_WORMHOLES + 2.0f * sqrtf(wh_rs * (r - wh_rs)); 
        for (int j = 0; j < WH_NT; j++) {
            float th = 2.0f * M_PI * j / WH_NT;
            whVerts[whv++] = (Vertex){{r * cosf(th), r * sinf(th), base_z}, {1,1,1}, {0,0, 1.0f}};
        }
    }
    /* Ring indices */
    for (int i = 0; i < WH_NR; i++) {
        for (int j = 0; j < WH_NT; j++) {
            whInds[whi++] = i * WH_NT + j;
            whInds[whi++] = i * WH_NT + (j + 1) % WH_NT;
        }
    }
    /* Spoke indices */
    for (int j = 0; j < WH_NT; j++) {
        for (int i = 0; i < WH_NR - 1; i++) {
            whInds[whi++] = i * WH_NT + j;
            whInds[whi++] = (i + 1) * WH_NT + j;
        }
    }
    uint32_t totalWhInds = whi;
    createBuffer(app, sizeof(whVerts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->whVertexBuffer, &app->whVertexBufferMemory); vkMapMemory(app->device, app->whVertexBufferMemory, 0, sizeof(whVerts), 0, &d); memcpy(d, whVerts, sizeof(whVerts)); vkUnmapMemory(app->device, app->whVertexBufferMemory);
    createBuffer(app, totalWhInds * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->whIndexBuffer, &app->whIndexBufferMemory); vkMapMemory(app->device, app->whIndexBufferMemory, 0, totalWhInds * sizeof(uint32_t), 0, &d); memcpy(d, whInds, totalWhInds * sizeof(uint32_t)); vkUnmapMemory(app->device, app->whIndexBufferMemory);
    app->whIndexCount = totalWhInds;
    
    /* Procedural Quantum Cores for Alliance Classes (0 to 12) */
    for (int cl = 0; cl <= 12; cl++) {
        /* Formula aggressiva per differenziare: 0=75 lati (Legacy), 12=3 lati (Frigate) */
        int eq_sides = 3 + (12 - cl) * 6;
        int num_vertices = eq_sides + 2;
        int num_indices = eq_sides * 6;
        
        Vertex cVerts[200]; /* Safe max size given max eq_sides is 27 */
        uint32_t cInds[600];
        
        cVerts[0] = (Vertex){{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}};
        cVerts[1] = (Vertex){{0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}};
        for (int e = 0; e < eq_sides; e++) {
            float th = 2.0f * M_PI * e / eq_sides;
            cVerts[2 + e] = (Vertex){{cosf(th), 0.0f, sinf(th)}, {1.0f, 1.0f, 1.0f}, {cosf(th), 0.0f, sinf(th)}};
        }
        int idx = 0;
        for (int e = 0; e < eq_sides; e++) {
            int next_e = (e + 1) % eq_sides;
            /* Top edge */
            cInds[idx++] = 0; cInds[idx++] = 2 + e;
            /* Bottom edge */
            cInds[idx++] = 1; cInds[idx++] = 2 + e;
            /* Equator edge */
            cInds[idx++] = 2 + e; cInds[idx++] = 2 + next_e;
        }
        app->coreICount[cl] = num_indices;
        createBuffer(app, num_vertices * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->coreVB[cl], &app->coreVBM[cl]);
        vkMapMemory(app->device, app->coreVBM[cl], 0, num_vertices * sizeof(Vertex), 0, &d); memcpy(d, cVerts, num_vertices * sizeof(Vertex)); vkUnmapMemory(app->device, app->coreVBM[cl]);
        createBuffer(app, num_indices * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->coreIB[cl], &app->coreIBM[cl]);
        vkMapMemory(app->device, app->coreIBM[cl], 0, num_indices * sizeof(uint32_t), 0, &d); memcpy(d, cInds, num_indices * sizeof(uint32_t)); vkUnmapMemory(app->device, app->coreIBM[cl]);
    }

    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) createBuffer(app, sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->uniformBuffers[i], &app->uniformBuffersMemory[i]);
    VkDescriptorPoolSize sz = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT}; VkDescriptorPoolCreateInfo dp = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,NULL,0,MAX_FRAMES_IN_FLIGHT,1,&sz}; vkCreateDescriptorPool(app->device, &dp, NULL, &app->descriptorPool);
    VkDescriptorSetLayout lys[MAX_FRAMES_IN_FLIGHT]; for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) lys[i] = app->descriptorSetLayout;
    VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,NULL,app->descriptorPool,MAX_FRAMES_IN_FLIGHT,lys}; vkAllocateDescriptorSets(app->device, &ai, app->descriptorSets);
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++){ VkDescriptorBufferInfo bi = {app->uniformBuffers[i],0,sizeof(UniformBufferObject)}; VkWriteDescriptorSet w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,NULL,app->descriptorSets[i],0,0,1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,NULL,&bi,NULL}; vkUpdateDescriptorSets(app->device, 1, &w, 0, NULL); }
    VkCommandBufferAllocateInfo ca = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,NULL,app->commandPool,VK_COMMAND_BUFFER_LEVEL_PRIMARY,MAX_FRAMES_IN_FLIGHT}; vkAllocateCommandBuffers(app->device, &ca, app->commandBuffers);
    VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0}; VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,NULL,VK_FENCE_CREATE_SIGNALED_BIT};
    for(int i=0; i<MAX_FRAMES_IN_FLIGHT; i++){ vkCreateSemaphore(app->device, &si, NULL, &app->imageAvailableSemaphores[i]); vkCreateSemaphore(app->device, &si, NULL, &app->renderFinishedSemaphores[i]); vkCreateFence(app->device, &fi, NULL, &app->inFlightFences[i]); }
}

void cleanup(VulkanApp* app) {
    for (int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) { vkDestroySemaphore(app->device, app->renderFinishedSemaphores[i], NULL); vkDestroySemaphore(app->device, app->imageAvailableSemaphores[i], NULL); vkDestroyFence(app->device, app->inFlightFences[i], NULL); }
    vkDestroyCommandPool(app->device, app->commandPool, NULL);
    vkDestroyImageView(app->device, app->colorImageView, NULL); vkDestroyImage(app->device, app->colorImage, NULL); vkFreeMemory(app->device, app->colorImageMemory, NULL);
    vkDestroyImageView(app->device, app->depthImageView, NULL); vkDestroyImage(app->device, app->depthImage, NULL); vkFreeMemory(app->device, app->depthImageMemory, NULL);
    for (uint32_t i=0; i<app->swapChainImageCount; i++) vkDestroyFramebuffer(app->device, app->swapChainFramebuffers[i], NULL);
    vkDestroyPipeline(app->device, app->graphicsPipeline, NULL); 
    vkDestroyPipeline(app->device, app->wireframePipeline, NULL); 
    vkDestroyPipeline(app->device, app->pointPipeline, NULL);
    vkDestroyPipeline(app->device, app->glowPipeline, NULL);
    vkDestroyPipeline(app->device, app->alphaPipeline, NULL);
    vkDestroyPipelineLayout(app->device, app->pipelineLayout, NULL); 
    vkDestroyRenderPass(app->device, app->renderPass, NULL);
    for (uint32_t i=0; i<app->swapChainImageCount; i++) vkDestroyImageView(app->device, app->swapChainImageViews[i], NULL);
    vkDestroySwapchainKHR(app->device, app->swapChain, NULL);
    vkDestroyBuffer(app->device, app->whIndexBuffer, NULL); vkFreeMemory(app->device, app->whIndexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->whVertexBuffer, NULL); vkFreeMemory(app->device, app->whVertexBufferMemory, NULL);
    for (int cl = 0; cl <= 12; ++cl) {
        vkDestroyBuffer(app->device, app->coreIB[cl], NULL); vkFreeMemory(app->device, app->coreIBM[cl], NULL);
        vkDestroyBuffer(app->device, app->coreVB[cl], NULL); vkFreeMemory(app->device, app->coreVBM[cl], NULL);
    }
    vkDestroyBuffer(app->device, app->starfieldIndexBuffer, NULL); vkFreeMemory(app->device, app->starfieldIndexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->starfieldVertexBuffer, NULL); vkFreeMemory(app->device, app->starfieldVertexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->sphereIndexBuffer, NULL); vkFreeMemory(app->device, app->sphereIndexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->sphereVertexBuffer, NULL); vkFreeMemory(app->device, app->sphereVertexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->vectorIndexBuffer, NULL); vkFreeMemory(app->device, app->vectorIndexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->vectorVertexBuffer, NULL); vkFreeMemory(app->device, app->vectorVertexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->gridIndexBuffer, NULL); vkFreeMemory(app->device, app->gridIndexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->gridVertexBuffer, NULL); vkFreeMemory(app->device, app->gridVertexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->arcIndexBuffer, NULL); vkFreeMemory(app->device, app->arcIndexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->arcVertexBuffer, NULL); vkFreeMemory(app->device, app->arcVertexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->rollCircleIndexBuffer, NULL); vkFreeMemory(app->device, app->rollCircleIndexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->rollCircleVertexBuffer, NULL); vkFreeMemory(app->device, app->rollCircleVertexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->circleIndexBuffer, NULL); vkFreeMemory(app->device, app->circleIndexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->circleVertexBuffer, NULL); vkFreeMemory(app->device, app->circleVertexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->axesIndexBuffer, NULL); vkFreeMemory(app->device, app->axesIndexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->beamIndexBuffer, NULL); vkFreeMemory(app->device, app->beamIndexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->axesVertexBuffer, NULL); vkFreeMemory(app->device, app->axesVertexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->beamVertexBuffer, NULL); vkFreeMemory(app->device, app->beamVertexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->cubeIndexBuffer, NULL); vkFreeMemory(app->device, app->cubeIndexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->cubeSolidIndexBuffer, NULL); vkFreeMemory(app->device, app->cubeSolidIndexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->cubeVertexBuffer, NULL); vkFreeMemory(app->device, app->cubeVertexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->torpIndexBuffer, NULL); vkFreeMemory(app->device, app->torpIndexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->torpVertexBuffer, NULL); vkFreeMemory(app->device, app->torpVertexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->shipIndexBuffer, NULL);
    vkFreeMemory(app->device, app->shipIndexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->shipVertexBuffer, NULL);
    vkFreeMemory(app->device, app->shipVertexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->starbaseIndexBuffer, NULL);
    vkFreeMemory(app->device, app->starbaseIndexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->starbaseVertexBuffer, NULL);
    vkFreeMemory(app->device, app->starbaseVertexBufferMemory, NULL);
    
    vkDestroyBuffer(app->device, app->indexBuffer, NULL); vkFreeMemory(app->device, app->indexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->vertexBuffer, NULL); vkFreeMemory(app->device, app->vertexBufferMemory, NULL);
    for (int i=0; i<MAX_FRAMES_IN_FLIGHT; i++) { vkDestroyBuffer(app->device, app->uniformBuffers[i], NULL); vkFreeMemory(app->device, app->uniformBuffersMemory[i], NULL); }
    vkDestroyDescriptorPool(app->device, app->descriptorPool, NULL); vkDestroyDescriptorSetLayout(app->device, app->descriptorSetLayout, NULL);
    vkDestroyDevice(app->device, NULL); vkDestroySurfaceKHR(app->instance, app->surface, NULL); vkDestroyInstance(app->instance, NULL);
    if (app->window) { glfwDestroyWindow(app->window); } glfwTerminate();
    if (app->shm) { munmap(app->shm, sizeof(SharedIPC)); } if (app->shm_fd != -1) { close(app->shm_fd); }
    free(app->swapChainImages); free(app->swapChainImageViews); free(app->swapChainFramebuffers);
}

int main(int argc, char** argv) {
    if (!glfwInit()) {
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    
    VulkanApp* app = malloc(sizeof(VulkanApp));
    if (!app) {
        return 1;
    }
    memset(app, 0, sizeof(VulkanApp));
    app->shm_fd = -1;
    
    if (argc > 1) {
        app->shm_fd = shm_open(argv[1], O_RDWR, 0666);
        if (app->shm_fd != -1) {
            app->shm = mmap(NULL, sizeof(SharedIPC), PROT_READ | PROT_WRITE, MAP_SHARED, app->shm_fd, 0);
            if (app->shm == MAP_FAILED) {
                app->shm = NULL;
            }
        }
    }
    
    app->window = glfwCreateWindow(WIDTH, HEIGHT, "Space GL", NULL, NULL);
    if (!app->window) {
        free(app);
        return 1;
    }
    
    app->angleX = 0.0f;
    app->angleY = 0.0f;
    app->cameraDist = 80.0f;
    app->autoRotate = true;
    app->bridgeAnim = 0.0f;
    app->showBridge = 0;
    
    glfwSetWindowUserPointer(app->window, app);
    glfwSetKeyCallback(app->window, key_callback);
    
    initVulkan(app); 
    
    if (app->shm) {
        pid_t tp = getppid();
        if (argc > 1) {
            char *p = strrchr(argv[1], '_');
            if (p) {
                tp = atoi(p+1);
            }
        }
        if (tp > 1) {
            kill(tp, SIGUSR2);
        }
    }
    
    mainLoop(app);
    cleanup(app);
    free(app);
    return 0;
}
