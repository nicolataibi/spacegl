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
 * SPACE GL - VULKAN SHARED TYPES
 *
 * Canonical home of the VulkanApp state and every type shared between
 * the CPU-driven path (src/spacegl_vulkan.c + spacegl_vulkan_extras.inl)
 * and the GPU-driven path (src/spacegl_vulkan_gdd.c, SPACEGL_GPD=1).
 *
 * The per-frame limits and the interpolated/effect state structs are
 * OWNED by include/spacegl_gdd.h (the CPU<->GPU contract); the aliases
 * below keep the pre-existing CPU-driven code compiling unchanged.
 */

#ifndef SPACEGL_VULKAN_TYPES_H
#define SPACEGL_VULKAN_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include "game_state.h"
#include "shared_state.h"
#include "spacegl_gdd.h"

/* ------------------------------------------------------------------ */
/* Limit aliases (canonical values live in spacegl_gdd.h). Cast to int */
/* to preserve the signed loop bounds of the pre-existing code.        */
/* ------------------------------------------------------------------ */
#define MAX_FRAMES_IN_FLIGHT      ((int)GDD_MAX_FRAMES)
#define MAX_ACTIVE_BEAMS          ((int)GDD_MAX_ACTIVE_BEAMS)
#define MAX_ACTIVE_BOOMS          ((int)GDD_MAX_ACTIVE_BOOMS)
#define MAX_ACTIVE_TORPS          ((int)GDD_MAX_ACTIVE_TORPS)
#define MAX_ACTIVE_DISMANTLES     ((int)GDD_MAX_ACTIVE_DISMANTLES)
#define EXPLOSION_PIXELS          ((int)GDD_EXPLOSION_PIXELS)
#define MAX_STARS                 ((int)GDD_STAR_COUNT)
#define MAX_ARRIVAL_PARTICLES     ((int)GDD_MAX_ARRIVAL_PARTICLES)

/* Matrix math types (engine convention: Row-Major matrices, v' = v*M,
 * translation in the bottom row — see the note in spacegl_vulkan.c).
 * The helpers are shared by BOTH architectures (and by tests/): they
 * live here as static inline so every translation unit and the
 * standalone test project get them without link dependencies. */
typedef float vec3[3];
typedef float mat4[4][4];

static inline void mat4_identity(mat4 m) {
    memset(m, 0, sizeof(mat4));
    m[0][0] = 1.0f; m[1][1] = 1.0f; m[2][2] = 1.0f; m[3][3] = 1.0f;
}

static inline void mat4_multiply(mat4 a, mat4 b, mat4 res) {
    mat4 tmp;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    memcpy(res, tmp, sizeof(mat4));
}

static inline void mat4_translate(mat4 m, vec3 v) {
    mat4_identity(m);
    m[3][0] = v[0]; m[3][1] = v[1]; m[3][2] = v[2];
}

static inline void mat4_scale(mat4 m, vec3 v) {
    mat4_identity(m);
    m[0][0] = v[0]; m[1][1] = v[1]; m[2][2] = v[2]; m[3][3] = 1.0f;
}

static inline void mat4_rotate(mat4 m, float angle, vec3 axis) {
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

static inline void mat4_lookat(vec3 eye, vec3 center, vec3 up, mat4 dest) {
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

static inline void mat4_perspective(float fovy, float aspect, float nearZ, float farZ, mat4 dest) {
    float f = 1.0f / tanf(fovy / 2.0f);
    mat4_identity(dest);
    dest[0][0] = f / aspect; dest[1][1] = f;
    dest[2][2] = farZ / (nearZ - farZ); dest[2][3] = -1.0f;
    dest[3][2] = (nearZ * farZ) / (nearZ - farZ); dest[3][3] = 0.0f;
}

/* Legacy CPU-path vertex layout (3 x vec3). */
typedef struct { float pos[3]; float color[3]; float normal[3]; } Vertex;

/* CPU-path push constants / UBO (mirrored by the GDD scene PC). */
typedef struct { mat4 model; float color[4]; float time; int usePushColor; float metallic; float roughness; } PushConstants;
typedef struct { mat4 view; mat4 proj; } UniformBufferObject;

/* Starfield mode (CPU path; the GDD starfield always twinkles). */
#define STARFIELD_MODE_TWINKLE 1  /* Dynamic intensity variation */
#define STARFIELD_MODE_HALO    2  /* Geometric atmospheric glow */
#define STARFIELD_MODE_BLOOM   3  /* Point Sprite Procedural Bloom */
#define STARFIELD_MODE STARFIELD_MODE_TWINKLE

/* ------------------------------------------------------------------ */
/* GPU-driven per-frame slot (the GDD ring element)                    */
/* ------------------------------------------------------------------ */
typedef struct {
    VkCommandBuffer cmd;
    /* instance lists (host visible, persistently mapped) */
    VkBuffer dyn_inst;  VkDeviceMemory dyn_mem;  void *dyn_ptr;
    VkBuffer map_inst;  VkDeviceMemory map_mem;  void *map_ptr;
    /* GPU cull output (device local) */
    VkBuffer dyn_vis;   VkDeviceMemory dvis_mem;
    VkBuffer map_vis;   VkDeviceMemory mvis_mem;
    /* GPU-generated vertices + indirect commands + counters */
    VkBuffer verts;     VkDeviceMemory vert_mem;
    VkBuffer indirect;  VkDeviceMemory ind_mem;
    VkBuffer counters;  VkDeviceMemory cnt_mem;
    /* descriptor sets (written once: the buffers are stable per slot) */
    VkDescriptorSet desc_cull_dyn;
    VkDescriptorSet desc_cull_map;
    VkDescriptorSet desc_exp_dyn;
    VkDescriptorSet desc_exp_map;
    VkDescriptorSet desc_final;
    VkDescriptorSet desc_scene;
    uint32_t dyn_count;
    uint32_t map_count;
} GddFrame;

/* ------------------------------------------------------------------ */
/* The application state (single struct for both architectures)        */
/* ------------------------------------------------------------------ */
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
    VkBuffer coreVB[14]; VkDeviceMemory coreVBM[14]; VkBuffer coreIB[14]; VkDeviceMemory coreIBM[14]; uint32_t coreICount[14];
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
    /* Smooth State Interpolation (cubic Hermite — shared by both paths) */
    long long last_shm_frame_id;
    double last_shm_time;
    double smoothed_shm_time;
    SmoothObj smoothObjs[MAX_NET_OBJECTS];

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

    /* --- GPU-driven path (SPACEGL_GPD=1; NULL gdd => CPU-driven) --- */
    bool gdd_wanted;          /* the env var requested the GDD path */
    uint32_t gdd_queue_family;/* queue family used for the device queue */
    GddState* gdd;            /* GPU-driven state (NULL when inactive) */
} VulkanApp;

#endif /* SPACEGL_VULKAN_TYPES_H */
