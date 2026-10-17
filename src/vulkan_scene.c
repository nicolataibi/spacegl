/*
 * SPACE GL - VULKAN SCENE & PROCEDURAL GEOMETRY
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
 * Translation Unit: vulkan_scene.c
 * Role: Procedural mesh generation, starfields, wormholes, nebulae, galaxy map,
 * and per-tick interpolation and event simulation (scene_update).
 * Peer modules: vulkan_device.c, vulkan_render.c, vulkan_main.c.
 */

#include "vulkan_device_internal.h"
#include <time.h>

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

#include "spacegl_vulkan_extras.inl"
#include "spacegl_vulkan_tables.inc"

void getObjectColor(int type, int faction, float* r, float* g, float* b) {
    *r = 1.0f; *g = 1.0f; *b = 1.0f;
    
    if ((type == 1 || (type >= 10 && type < 200)) && (faction >= 10 && faction <= 20)) {
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

void drawWormholeCore_v1(VkCommandBuffer cb, VulkanApp* app, mat4 modelBase, float pulse, int type) {
    VkDeviceSize off = 0;
    mat4 R_rot; mat4_identity(R_rot);
    mat4_rotate(R_rot, pulse * 10.0f * M_PI / 180.0f, (vec3){0, 0, 1});
    mat4 M_rot; mat4_multiply(R_rot, modelBase, M_rot);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    PushConstants pc = {0}; mat4 S_core; mat4_scale(S_core, (vec3){0.35f, 0.35f, 0.35f});
    mat4_multiply(S_core, M_rot, pc.model);
    if (type == 1) { pc.color[0]=1.0f; pc.color[1]=1.0f; pc.color[2]=1.0f; }
    else { pc.color[0]=0.05f; pc.color[1]=0.05f; pc.color[2]=0.05f; }
    pc.color[3]=1.0f; pc.usePushColor=1;
    pc.metallic = 1.0f; pc.roughness = 0.1f;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawWormholeCore_v2(VkCommandBuffer cb, VulkanApp* app, mat4 modelBase, float pulse, int type) {
    VkDeviceSize off = 0;
    mat4 R_rot; mat4_identity(R_rot);
    mat4_rotate(R_rot, pulse * 5.0f * M_PI / 180.0f, (vec3){1, 1, 1});
    mat4 M_rot; mat4_multiply(R_rot, modelBase, M_rot);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    PushConstants pc1 = {0}; mat4 S_out; mat4_scale(S_out, (vec3){0.85f, 0.85f, 0.85f});
    mat4_multiply(S_out, M_rot, pc1.model);
    pc1.color[0]=0.15f; pc1.color[1]=0.15f; pc1.color[2]=0.15f; pc1.color[3]=1.0f; pc1.usePushColor=1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc1), &pc1);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, sizeof(cubeIndices)/4, 1, 0, 0, 0);

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

void drawWormholeCore(VkCommandBuffer cb, VulkanApp* app, mat4 modelBase, float pulse, int type, int ver) {
    if (ver == 1) drawWormholeCore_v1(cb, app, modelBase, pulse, type);
    else drawWormholeCore_v2(cb, app, modelBase, pulse, type);
}

void drawSwarmCube(VkCommandBuffer cb, VulkanApp* app, mat4 modelBase, float pulse) {
    VkDeviceSize off = 0;
    mat4 R_rot; mat4_identity(R_rot);
    mat4_rotate(R_rot, pulse * 5.0f * M_PI / 180.0f, (vec3){1, 1, 1});
    mat4 M_rot; mat4_multiply(R_rot, modelBase, M_rot);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    PushConstants pc1 = {0}; 
    mat4 S_out; mat4_scale(S_out, (vec3){0.85f, 0.85f, 0.85f}); 
    mat4_multiply(S_out, M_rot, pc1.model);
    pc1.color[0]=0.15f; pc1.color[1]=0.15f; pc1.color[2]=0.15f; pc1.color[3]=1.0f; pc1.usePushColor=1;
    pc1.metallic = 0.9f; pc1.roughness = 0.1f;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc1), &pc1);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, sizeof(cubeIndices)/4, 1, 0, 0, 0);

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

void drawWormhole_v1(VkCommandBuffer cb, VulkanApp* app, float x, float y, float z, double h, double m, int type, float pulse, float tactScale, float closingScale) {
    mat4 T, R, S_base;
    mat4_translate(T, (vec3){x, y, z});
    mat4_identity(R);
    mat4_rotate(R, -h * M_PI / 180.0f, (vec3){0, 1, 0});
    float h_rad = h * M_PI / 180.0f;
    mat4_rotate(R, m * M_PI / 180.0f, (vec3){cosf(h_rad), 0, -sinf(h_rad)});

    float finalScale = 1.0f * tactScale * closingScale;
    mat4 M_base;
    mat4_scale(S_base, (vec3){finalScale, finalScale, finalScale}); 
    mat4_multiply(S_base, R, M_base);
    mat4_multiply(M_base, T, M_base);
    drawWormholeCore(cb, app, M_base, pulse, type, 1);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    mat4 RR; mat4_identity(RR);
    mat4_rotate(RR, M_PI / 2.0f, (vec3){0, 1, 0});
    mat4 M_funnel_base;
    mat4_multiply(RR, M_base, M_funnel_base);

    PushConstants pc = {0};
    pc.time = pulse; pc.usePushColor = 4;
    if (type == 1) { pc.color[0]=0.2f; pc.color[1]=0.8f; pc.color[2]=1.0f; }
    else { pc.color[0]=1.0f; pc.color[1]=0.2f; pc.color[2]=0.8f; }

    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &app->whVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->whIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    mat4 R_spin1; mat4_identity(R_spin1);
    mat4_rotate(R_spin1, pulse * 50.0f * M_PI / 180.0f, (vec3){0, 0, 1});
    mat4_multiply(R_spin1, M_funnel_base, pc.model);
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, app->whIndexCount, 1, 0, 0, 0);

    mat4 R_flip, R_spin2; mat4_identity(R_flip);
    mat4_rotate(R_flip, M_PI, (vec3){0, 1, 0});
    mat4_identity(R_spin2);
    mat4_rotate(R_spin2, -pulse * 50.0f * M_PI / 180.0f, (vec3){0, 0, 1});
    mat4_multiply(R_spin2, R_flip, pc.model);
    mat4_multiply(pc.model, M_funnel_base, pc.model);
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, app->whIndexCount, 1, 0, 0, 0);

    /* 4. Swirling Horizon Rings */
    vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->circleIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    for (int r = 0; r < 3; r++) {
        mat4 S_ring, R_ring, M_ring;
        float r_scale = (0.5f + r * 0.4f);
        mat4_scale(S_ring, (vec3){r_scale, r_scale, r_scale});
        mat4_identity(R_ring);
        mat4_rotate(R_ring, (pulse * (40.0f + r * 20.0f) + r * 45.0f) * M_PI / 180.0f, (vec3){0, 0, 1});
        mat4_multiply(S_ring, R_ring, M_ring);
        mat4_multiply(M_ring, M_funnel_base, pc.model);
        pc.color[0] = (r == 0) ? 1.0f : 0.4f;
        pc.color[1] = (r == 1) ? 1.0f : 0.8f;
        pc.color[2] = 1.0f;
        pc.usePushColor = 1;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, CIRCLE_SEGMENTS * 2, 1, 0, 0, 0);
    }
}

void drawWormhole_v2(VkCommandBuffer cb, VulkanApp* app, float x, float y, float z, double h, double m, int type, float pulse, float tactScale, float closingScale) {
    mat4 T, R, S_base;
    mat4_translate(T, (vec3){x, y, z});
    mat4_identity(R);
    mat4_rotate(R, -h * M_PI / 180.0f, (vec3){0, 1, 0});
    float h_rad = h * M_PI / 180.0f;
    mat4_rotate(R, m * M_PI / 180.0f, (vec3){cosf(h_rad), 0, -sinf(h_rad)});

    float finalScale = 1.35f * tactScale * closingScale;
    mat4 M_base;
    mat4_scale(S_base, (vec3){finalScale, finalScale, finalScale}); 
    mat4_multiply(S_base, R, M_base);
    mat4_multiply(M_base, T, M_base);
    drawWormholeCore(cb, app, M_base, pulse, type, 2);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    mat4 RR; mat4_identity(RR);
    mat4_rotate(RR, M_PI / 2.0f, (vec3){0, 1, 0});
    mat4 M_funnel_base;
    mat4_multiply(RR, M_base, M_funnel_base);

    PushConstants pc = {0};
    pc.time = pulse; pc.usePushColor = 4;
    if (type == 1) { pc.color[0]=0.1f; pc.color[1]=0.6f; pc.color[2]=1.0f; }
    else { pc.color[0]=1.0f; pc.color[1]=0.1f; pc.color[2]=0.4f; }

    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &app->whVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->whIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    mat4 R_spin1; mat4_identity(R_spin1);
    mat4_rotate(R_spin1, pulse * 35.0f * M_PI / 180.0f, (vec3){0, 0, 1});
    mat4_multiply(R_spin1, M_funnel_base, pc.model);
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, app->whIndexCount, 1, 0, 0, 0);

    mat4 R_flip, R_spin2; mat4_identity(R_flip);
    mat4_rotate(R_flip, M_PI, (vec3){0, 1, 0});
    mat4_identity(R_spin2);
    mat4_rotate(R_spin2, -pulse * 35.0f * M_PI / 180.0f, (vec3){0, 0, 1});
    mat4_multiply(R_spin2, R_flip, pc.model);
    mat4_multiply(pc.model, M_funnel_base, pc.model);
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, app->whIndexCount, 1, 0, 0, 0);

    /* Concentric horizon rings */
    vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->circleIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    for (int r = 0; r < 4; r++) {
        mat4 S_ring, R_ring, M_ring;
        float r_scale = (0.4f + r * 0.35f);
        mat4_scale(S_ring, (vec3){r_scale, r_scale, r_scale});
        mat4_identity(R_ring);
        mat4_rotate(R_ring, (pulse * (25.0f + r * 15.0f) + r * 30.0f) * M_PI / 180.0f, (vec3){0, 0, 1});
        mat4_multiply(S_ring, R_ring, M_ring);
        mat4_multiply(M_ring, M_funnel_base, pc.model);
        pc.color[0] = (r % 2 == 0) ? 0.9f : 0.2f;
        pc.color[1] = (r % 2 == 0) ? 0.3f : 0.7f;
        pc.color[2] = 1.0f;
        pc.usePushColor = 1;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, CIRCLE_SEGMENTS * 2, 1, 0, 0, 0);
    }
}

void drawWormhole(VkCommandBuffer cb, VulkanApp* app, float x, float y, float z, double h, double m, int type, float pulse, float tactScale, float closingScale, int ver) {
    if (ver == 1) drawWormhole_v1(cb, app, x, y, z, h, m, type, pulse, tactScale, closingScale);
    else drawWormhole_v2(cb, app, x, y, z, h, m, type, pulse, tactScale, closingScale);
}

void drawDiffuseNebula(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float pulse, float tactScale) {
    drawNebula(cb, app, baseT, 0, pulse, tactScale);
}

void drawDarkNebula(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float pulse, float tactScale) {
    drawNebula(cb, app, baseT, 2, pulse, tactScale);
}

void drawPlanetaryNebula(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float pulse, float tactScale) {
    drawNebula(cb, app, baseT, 1, pulse, tactScale);
}

void drawNebula(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, int subType, float pulse, float tactScale) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    
    float r=0.5f, g=0.5f, b=0.5f;
    if (subType == 0) { r=0.55f; g=0.72f; b=1.0f; }
    else if (subType == 1) { r=1.0f; g=0.4f; b=0.2f; }
    else if (subType == 2) { r=0.2f; g=0.0f; b=0.4f; }
    
    for (int cl=0; cl<=13; cl+=2) {
        float f = (float)cl / 13.0f;
        float core_scale = (3.5f + sinf(pulse * 0.2f + f * 3.14f) * 0.5f) * tactScale;
        
        PushConstants pc = {0};
        mat4 S_core, R_core;
        mat4_scale(S_core, (vec3){core_scale, core_scale * 0.8f, core_scale * 1.2f});
        mat4_identity(R_core);
        mat4_rotate(R_core, pulse * 0.05f * (cl + 1), (vec3){0, 1, 0});
        
        mat4_multiply(S_core, R_core, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        pc.color[0] = r; pc.color[1] = g; pc.color[2] = b;
        pc.color[3] = 0.25f / (1.0f + f * 2.0f);
        pc.usePushColor = 7;
        pc.time = pulse;
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &app->coreVB[cl], &off);
        vkCmdBindIndexBuffer(cb, app->coreIB[cl], 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, app->coreICount[cl], 1, 0, 0, 0);
    }
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawGalaxyMap(VkCommandBuffer cb, VulkanApp* app, GameState* st, float pulse) {
    float gap = 1.2f;
    float mapScale = app->mapAnim;
    VkDeviceSize off = 0;

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
    PushConstants pcGrid = {0}; mat4_identity(pcGrid.model);
    mat4 S_grid; mat4_scale(S_grid, (vec3){(float)GALAXY_SIZE * gap * mapScale, (float)GALAXY_SIZE * gap * mapScale, (float)GALAXY_SIZE * gap * mapScale});
    memcpy(pcGrid.model, S_grid, sizeof(mat4));
    pcGrid.color[0]=0.4f; pcGrid.color[1]=0.4f; pcGrid.color[2]=1.0f; pcGrid.color[3]=0.8f; pcGrid.usePushColor=1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pcGrid), &pcGrid);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->cubeVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->cubeIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, sizeof(cubeIndices)/4, 1, 0, 0, 0);

    /* Render Player Position Indicator */
    if (st->object_count > 0 && st->objects[0].active) {
        float px = ((float)st->shm_q[0] - (float)GALAXY_SIZE/2.0f) * gap * mapScale;
        float py = ((float)st->shm_q[2] - (float)GALAXY_SIZE/2.0f) * gap * mapScale;
        float pz = ((float)GALAXY_SIZE/2.0f - (float)st->shm_q[1]) * gap * mapScale;
        PushConstants pcP = {0};
        mat4 T_p, S_p;
        mat4_translate(T_p, (vec3){px, py, pz});
        float pulse_scale = (0.3f + 0.1f * sinf(pulse * 6.0f)) * mapScale;
        mat4_scale(S_p, (vec3){pulse_scale, pulse_scale, pulse_scale});
        mat4_multiply(S_p, T_p, pcP.model);
        pcP.color[0]=0.0f; pcP.color[1]=1.0f; pcP.color[2]=0.2f; pcP.color[3]=1.0f; pcP.usePushColor=1;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pcP), &pcP);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }
}

void drawShieldGlow(VkCommandBuffer cb, VulkanApp* app, mat4 modelBase, float radius, float r, float g, float b, float alpha, float pulse) {
    VkDeviceSize off = 0;
    for (int i = 1; i <= 4; i++) {
        float s = radius * (1.2f + i * 0.4f);
        PushConstants pc = {0};
        mat4 S; mat4_scale(S, (vec3){s, s, s});
        mat4_multiply(S, modelBase, pc.model);
        pc.color[0] = r; pc.color[1] = g; pc.color[2] = b; pc.color[3] = alpha / (i * 1.2f);
        pc.usePushColor = 7; pc.time = pulse;
        pc.metallic = pulse * 10.0f;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }
}

void drawShieldEffect(VkCommandBuffer cb, VulkanApp* app, float pulse, float tactScale, mat4 R_ship, mat4 T_ship) {
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
        float scale = (1.2f + (1.0f - t) * 0.3f) * tactScale;

        mat4 R_sec, T_sec, S_sec, M_sec;
        mat4_identity(R_sec);
        mat4_rotate(R_sec, shield_rot[s].ry, (vec3){0, 1, 0});
        mat4_rotate(R_sec, shield_rot[s].rx, (vec3){0, 0, 1}); 

        float thk = 0.5f;
        mat4_scale(S_sec, (vec3){thk * scale, 1.8f * scale, 1.8f * scale});
        mat4_translate(T_sec, (vec3){1.45f * scale, 0, 0});

        mat4_multiply(S_sec, T_sec, M_sec);
        mat4_multiply(M_sec, R_sec, M_sec);
        mat4_multiply(M_sec, R_ship, M_sec);
        mat4_multiply(M_sec, T_ship, M_sec);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
        PushConstants wpc = {0};
        memcpy(wpc.model, M_sec, sizeof(mat4));
        wpc.color[0] = 0.0f; wpc.color[1] = 0.8f; wpc.color[2] = 1.0f; wpc.color[3] = alpha * 0.8f;
        wpc.usePushColor = 5;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(wpc), &wpc);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
        PushConstants pc = {0};
        memcpy(pc.model, M_sec, sizeof(mat4));
        pc.color[0] = 0.0f; pc.color[1] = 0.6f; pc.color[2] = 1.0f; pc.color[3] = alpha * 0.6f;
        pc.usePushColor = 7; pc.time = pulse;
        pc.metallic = pulse * 15.0f; 
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

        drawShieldGlow(cb, app, M_sec, 0.7f * scale, 0.0f, 0.5f, 1.0f, alpha * 0.9f, pulse);
    }
}

void createSceneMeshes(VulkanApp* app) {
    void* d;
    createBuffer(app, sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->vertexBuffer, &app->vertexBufferMemory);
    vkMapMemory(app->device, app->vertexBufferMemory, 0, sizeof(vertices), 0, &d); memcpy(d, vertices, sizeof(vertices)); vkUnmapMemory(app->device, app->vertexBufferMemory);

    createBuffer(app, sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->indexBuffer, &app->indexBufferMemory);
    vkMapMemory(app->device, app->indexBufferMemory, 0, sizeof(indices), 0, &d); memcpy(d, indices, sizeof(indices)); vkUnmapMemory(app->device, app->indexBufferMemory);

    createBuffer(app, sizeof(shipVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->shipVertexBuffer, &app->shipVertexBufferMemory);
    vkMapMemory(app->device, app->shipVertexBufferMemory, 0, sizeof(shipVertices), 0, &d); memcpy(d, shipVertices, sizeof(shipVertices)); vkUnmapMemory(app->device, app->shipVertexBufferMemory);
    
    createBuffer(app, sizeof(shipIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->shipIndexBuffer, &app->shipIndexBufferMemory);
    vkMapMemory(app->device, app->shipIndexBufferMemory, 0, sizeof(shipIndices), 0, &d); memcpy(d, shipIndices, sizeof(shipIndices)); vkUnmapMemory(app->device, app->shipIndexBufferMemory);

    createBuffer(app, sizeof(starbaseVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->starbaseVertexBuffer, &app->starbaseVertexBufferMemory);
    vkMapMemory(app->device, app->starbaseVertexBufferMemory, 0, sizeof(starbaseVertices), 0, &d); memcpy(d, starbaseVertices, sizeof(starbaseVertices)); vkUnmapMemory(app->device, app->starbaseVertexBufferMemory);

    createBuffer(app, sizeof(starbaseIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->starbaseIndexBuffer, &app->starbaseIndexBufferMemory);
    vkMapMemory(app->device, app->starbaseIndexBufferMemory, 0, sizeof(starbaseIndices), 0, &d); memcpy(d, starbaseIndices, sizeof(starbaseIndices)); vkUnmapMemory(app->device, app->starbaseIndexBufferMemory);

    createBuffer(app, sizeof(torpVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->torpVertexBuffer, &app->torpVertexBufferMemory);
    vkMapMemory(app->device, app->torpVertexBufferMemory, 0, sizeof(torpVertices), 0, &d); memcpy(d, torpVertices, sizeof(torpVertices)); vkUnmapMemory(app->device, app->torpVertexBufferMemory);

    createBuffer(app, sizeof(torpIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->torpIndexBuffer, &app->torpIndexBufferMemory);
    vkMapMemory(app->device, app->torpIndexBufferMemory, 0, sizeof(torpIndices), 0, &d); memcpy(d, torpIndices, sizeof(torpIndices)); vkUnmapMemory(app->device, app->torpIndexBufferMemory);

    createBuffer(app, sizeof(cubeVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->cubeVertexBuffer, &app->cubeVertexBufferMemory);
    vkMapMemory(app->device, app->cubeVertexBufferMemory, 0, sizeof(cubeVertices), 0, &d); memcpy(d, cubeVertices, sizeof(cubeVertices)); vkUnmapMemory(app->device, app->cubeVertexBufferMemory);

    createBuffer(app, sizeof(cubeIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->cubeIndexBuffer, &app->cubeIndexBufferMemory);
    vkMapMemory(app->device, app->cubeIndexBufferMemory, 0, sizeof(cubeIndices), 0, &d); memcpy(d, cubeIndices, sizeof(cubeIndices)); vkUnmapMemory(app->device, app->cubeIndexBufferMemory);

    createBuffer(app, sizeof(cubeSolidIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->cubeSolidIndexBuffer, &app->cubeSolidIndexBufferMemory);
    vkMapMemory(app->device, app->cubeSolidIndexBufferMemory, 0, sizeof(cubeSolidIndices), 0, &d); memcpy(d, cubeSolidIndices, sizeof(cubeSolidIndices)); vkUnmapMemory(app->device, app->cubeSolidIndexBufferMemory);
    
    createBuffer(app, sizeof(axesVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->axesVertexBuffer, &app->axesVertexBufferMemory);
    vkMapMemory(app->device, app->axesVertexBufferMemory, 0, sizeof(axesVertices), 0, &d); memcpy(d, axesVertices, sizeof(axesVertices)); vkUnmapMemory(app->device, app->axesVertexBufferMemory);

    createBuffer(app, sizeof(axesIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->axesIndexBuffer, &app->axesIndexBufferMemory);
    vkMapMemory(app->device, app->axesIndexBufferMemory, 0, sizeof(axesIndices), 0, &d); memcpy(d, axesIndices, sizeof(axesIndices)); vkUnmapMemory(app->device, app->axesIndexBufferMemory);

    createBuffer(app, 2 * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->beamVertexBuffer, &app->beamVertexBufferMemory);
    uint32_t beamInds[] = {0, 1};
    createBuffer(app, sizeof(beamInds), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->beamIndexBuffer, &app->beamIndexBufferMemory);
    vkMapMemory(app->device, app->beamIndexBufferMemory, 0, sizeof(beamInds), 0, &d); memcpy(d, beamInds, sizeof(beamInds)); vkUnmapMemory(app->device, app->beamIndexBufferMemory);

    Vertex cVerts[CIRCLE_SEGMENTS]; uint32_t cInds[CIRCLE_SEGMENTS * 2];
    for (int i=0; i<CIRCLE_SEGMENTS; i++) {
        float rad = (i * 5.0f) * M_PI / 180.0f;
        cVerts[i] = (Vertex){{sinf(rad), 0, cosf(rad)}, {1,1,1}, {0,1,0}};
        cInds[i*2] = i; cInds[i*2+1] = (i+1)%CIRCLE_SEGMENTS;
    }
    createBuffer(app, sizeof(cVerts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->circleVertexBuffer, &app->circleVertexBufferMemory);
    vkMapMemory(app->device, app->circleVertexBufferMemory, 0, sizeof(cVerts), 0, &d); memcpy(d, cVerts, sizeof(cVerts)); vkUnmapMemory(app->device, app->circleVertexBufferMemory);
    createBuffer(app, sizeof(cInds), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->circleIndexBuffer, &app->circleIndexBufferMemory);
    vkMapMemory(app->device, app->circleIndexBufferMemory, 0, sizeof(cInds), 0, &d); memcpy(d, cInds, sizeof(cInds)); vkUnmapMemory(app->device, app->circleIndexBufferMemory);

    Vertex aVerts[ARC_SEGMENTS]; uint32_t aInds[(ARC_SEGMENTS-1) * 2] = {0};
    for (int i=0; i<ARC_SEGMENTS; i++) {
        float rad = ((i * 5.0f) - 90.0f) * M_PI / 180.0f;
        aVerts[i] = (Vertex){{cosf(rad), sinf(rad), 0}, {1,1,0}, {0,0,1}};
        if (i < ARC_SEGMENTS - 1) { aInds[i*2] = i; aInds[i*2+1] = i+1; }
    }
    createBuffer(app, sizeof(aVerts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->arcVertexBuffer, &app->arcVertexBufferMemory);
    vkMapMemory(app->device, app->arcVertexBufferMemory, 0, sizeof(aVerts), 0, &d); memcpy(d, aVerts, sizeof(aVerts)); vkUnmapMemory(app->device, app->arcVertexBufferMemory);
    createBuffer(app, sizeof(aInds), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->arcIndexBuffer, &app->arcIndexBufferMemory);
    vkMapMemory(app->device, app->arcIndexBufferMemory, 0, sizeof(aInds), 0, &d); memcpy(d, aInds, sizeof(aInds)); vkUnmapMemory(app->device, app->arcIndexBufferMemory);

    Vertex rVerts[CIRCLE_SEGMENTS]; uint32_t rInds[CIRCLE_SEGMENTS * 2];
    for (int i=0; i<CIRCLE_SEGMENTS; i++) {
        float rad = (i * 5.0f) * M_PI / 180.0f;
        rVerts[i] = (Vertex){{0, sinf(rad), cosf(rad)}, {1,1,0}, {1,0,0}};
        rInds[i*2] = i; rInds[i*2+1] = (i+1)%CIRCLE_SEGMENTS;
    }
    createBuffer(app, sizeof(rVerts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->rollCircleVertexBuffer, &app->rollCircleVertexBufferMemory);
    vkMapMemory(app->device, app->rollCircleVertexBufferMemory, 0, sizeof(rVerts), 0, &d); memcpy(d, rVerts, sizeof(rVerts)); vkUnmapMemory(app->device, app->rollCircleVertexBufferMemory);
    createBuffer(app, sizeof(rInds), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->rollCircleIndexBuffer, &app->rollCircleIndexBufferMemory);
    vkMapMemory(app->device, app->rollCircleIndexBufferMemory, 0, sizeof(rInds), 0, &d); memcpy(d, rInds, sizeof(rInds)); vkUnmapMemory(app->device, app->rollCircleIndexBufferMemory);

    int steps = (int)QUADRANT_SIZE / 2;
    uint64_t grid_v_count = (uint64_t)(steps + 1) * (steps + 1) * 3 * 2;
    Vertex* gVerts_dyn = malloc(grid_v_count * sizeof(Vertex));
    uint32_t* gInds_dyn = malloc(grid_v_count * sizeof(uint32_t));
    if (!gVerts_dyn || !gInds_dyn) { free(gVerts_dyn); free(gInds_dyn); fprintf(stderr, "[VK] Out of memory: tactical grid buffers\n"); exit(1); }
    uint64_t gv = 0;
    float hq = QUADRANT_SIZE / 2.0f;
    for (int i = 0; i <= steps; i++) {
        float p = -hq + i * 2.0f;
        for (int j = 0; j <= steps; j++) {
            float q = -hq + j * 2.0f;
            vec3 gcol = {0.0f, 1.0f, 0.2f}, gnorm = {0,0,0};
            gVerts_dyn[gv] = (Vertex){{p, q, -hq}, {gcol[0], gcol[1], gcol[2]}, {gnorm[0], gnorm[1], gnorm[2]}}; gInds_dyn[gv] = (uint32_t)gv; gv++;
            gVerts_dyn[gv] = (Vertex){{p, q,  hq}, {gcol[0], gcol[1], gcol[2]}, {gnorm[0], gnorm[1], gnorm[2]}}; gInds_dyn[gv] = (uint32_t)gv; gv++;
            gVerts_dyn[gv] = (Vertex){{p, -hq, q}, {gcol[0], gcol[1], gcol[2]}, {gnorm[0], gnorm[1], gnorm[2]}}; gInds_dyn[gv] = (uint32_t)gv; gv++;
            gVerts_dyn[gv] = (Vertex){{p,  hq, q}, {gcol[0], gcol[1], gcol[2]}, {gnorm[0], gnorm[1], gnorm[2]}}; gInds_dyn[gv] = (uint32_t)gv; gv++;
            gVerts_dyn[gv] = (Vertex){{-hq, p, q}, {gcol[0], gcol[1], gcol[2]}, {gnorm[0], gnorm[1], gnorm[2]}}; gInds_dyn[gv] = (uint32_t)gv; gv++;
            gVerts_dyn[gv] = (Vertex){{ hq, p, q}, {gcol[0], gcol[1], gcol[2]}, {gnorm[0], gnorm[1], gnorm[2]}}; gInds_dyn[gv] = (uint32_t)gv; gv++;
        }
    }
    app->gridVertexCount = gv;
    createBuffer(app, gv * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->gridVertexBuffer, &app->gridVertexBufferMemory);
    vkMapMemory(app->device, app->gridVertexBufferMemory, 0, gv * sizeof(Vertex), 0, &d); memcpy(d, gVerts_dyn, gv * sizeof(Vertex)); vkUnmapMemory(app->device, app->gridVertexBufferMemory);
    createBuffer(app, gv * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->gridIndexBuffer, &app->gridIndexBufferMemory);
    vkMapMemory(app->device, app->gridIndexBufferMemory, 0, gv * sizeof(uint32_t), 0, &d); memcpy(d, gInds_dyn, gv * sizeof(uint32_t)); vkUnmapMemory(app->device, app->gridIndexBufferMemory);
    free(gVerts_dyn); free(gInds_dyn);

    createBuffer(app, sizeof(vectorVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->vectorVertexBuffer, &app->vectorVertexBufferMemory);
    vkMapMemory(app->device, app->vectorVertexBufferMemory, 0, sizeof(vectorVertices), 0, &d); memcpy(d, vectorVertices, sizeof(vectorVertices)); vkUnmapMemory(app->device, app->vectorVertexBufferMemory);
    createBuffer(app, sizeof(vectorIndices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->vectorIndexBuffer, &app->vectorIndexBufferMemory);
    vkMapMemory(app->device, app->vectorIndexBufferMemory, 0, sizeof(vectorIndices), 0, &d); memcpy(d, vectorIndices, sizeof(vectorIndices)); vkUnmapMemory(app->device, app->vectorIndexBufferMemory);

    Vertex sVerts[(SPHERE_LATS + 1) * (SPHERE_LONGS + 1)];
    uint32_t sInds[SPHERE_LATS * SPHERE_LONGS * 6];
    int sv = 0;
    for (int i = 0; i <= SPHERE_LATS; i++) {
        float lat = M_PI * i / SPHERE_LATS;
        for (int j = 0; j <= SPHERE_LONGS; j++) {
            float lon = 2.0f * M_PI * j / SPHERE_LONGS;
            float px = sinf(lat) * cosf(lon), py = cosf(lat), pz = sinf(lat) * sinf(lon);
            sVerts[sv++] = (Vertex){{px, py, pz}, {1, 1, 1}, {px, py, pz}};
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
    createBuffer(app, sizeof(sVerts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->sphereVertexBuffer, &app->sphereVertexBufferMemory);
    vkMapMemory(app->device, app->sphereVertexBufferMemory, 0, sizeof(sVerts), 0, &d); memcpy(d, sVerts, sizeof(sVerts)); vkUnmapMemory(app->device, app->sphereVertexBufferMemory);
    createBuffer(app, sizeof(sInds), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->sphereIndexBuffer, &app->sphereIndexBufferMemory);
    vkMapMemory(app->device, app->sphereIndexBufferMemory, 0, sizeof(sInds), 0, &d); memcpy(d, sInds, sizeof(sInds)); vkUnmapMemory(app->device, app->sphereIndexBufferMemory);

    #define WH_NR 12
    #define WH_NT 24
    Vertex whVerts[WH_NR * WH_NT];
    uint32_t whInds[(WH_NR - 1) * WH_NT * 2 + WH_NR * WH_NT * 2];
    int whv = 0, whi = 0;
    float wh_rs = 0.45f, wh_rmax = 1.6f;
    for (int i = 0; i < WH_NR; i++) {
        float u = (float)i / (float)(WH_NR - 1);
        float r = wh_rs + u * (wh_rmax - wh_rs);
        float z = -0.5f * logf(r / wh_rs + 0.01f);
        for (int j = 0; j < WH_NT; j++) {
            float th = 2.0f * M_PI * j / WH_NT;
            whVerts[whv++] = (Vertex){{r * cosf(th), r * sinf(th), z}, {1, 1, 1}, {0, 0, 1}};
        }
    }
    for (int i = 0; i < WH_NR - 1; i++) {
        for (int j = 0; j < WH_NT; j++) {
            whInds[whi++] = i * WH_NT + j;
            whInds[whi++] = (i + 1) * WH_NT + j;
        }
    }
    for (int i = 0; i < WH_NR; i++) {
        for (int j = 0; j < WH_NT; j++) {
            whInds[whi++] = i * WH_NT + j;
            whInds[whi++] = i * WH_NT + (j + 1) % WH_NT;
        }
    }
    app->whIndexCount = whi;
    createBuffer(app, sizeof(whVerts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->whVertexBuffer, &app->whVertexBufferMemory);
    vkMapMemory(app->device, app->whVertexBufferMemory, 0, sizeof(whVerts), 0, &d); memcpy(d, whVerts, sizeof(whVerts)); vkUnmapMemory(app->device, app->whVertexBufferMemory);
    createBuffer(app, whi * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->whIndexBuffer, &app->whIndexBufferMemory);
    vkMapMemory(app->device, app->whIndexBufferMemory, 0, whi * sizeof(uint32_t), 0, &d); memcpy(d, whInds, whi * sizeof(uint32_t)); vkUnmapMemory(app->device, app->whIndexBufferMemory);

    for (int cl = 0; cl <= 13; ++cl) {
        int eq_sides = 3 + cl;
        int num_vertices = eq_sides + 2;
        int num_indices = eq_sides * 6;
        Vertex cVerts[100];
        memset(cVerts, 0, sizeof(cVerts));
        uint32_t cInds[600] = {0};
        cVerts[0] = (Vertex){{0.0f, 1.0f, 0.0f}, {1, 1, 1}, {0, 1, 0}};
        cVerts[1] = (Vertex){{0.0f, -1.0f, 0.0f}, {1, 1, 1}, {0, -1, 0}};
        for (int e = 0; e < eq_sides; ++e) {
            float th = 2.0f * M_PI * e / eq_sides;
            cVerts[2 + e] = (Vertex){{cosf(th), 0.0f, sinf(th)}, {1, 1, 1}, {cosf(th), 0, sinf(th)}};
        }
        int idx = 0;
        for (int e = 0; e < eq_sides; ++e) {
            int next_e = (e + 1) % eq_sides;
            cInds[idx++] = 0; cInds[idx++] = 2 + e; cInds[idx++] = 2 + next_e;
            cInds[idx++] = 1; cInds[idx++] = 2 + next_e; cInds[idx++] = 2 + e;
        }
        app->coreICount[cl] = num_indices;
        createBuffer(app, num_vertices * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->coreVB[cl], &app->coreVBM[cl]);
        vkMapMemory(app->device, app->coreVBM[cl], 0, num_vertices * sizeof(Vertex), 0, &d); memcpy(d, cVerts, num_vertices * sizeof(Vertex)); vkUnmapMemory(app->device, app->coreVBM[cl]);
        createBuffer(app, num_indices * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &app->coreIB[cl], &app->coreIBM[cl]);
        vkMapMemory(app->device, app->coreIBM[cl], 0, num_indices * sizeof(uint32_t), 0, &d); memcpy(d, cInds, num_indices * sizeof(uint32_t)); vkUnmapMemory(app->device, app->coreIBM[cl]);
    }
}

void createStarfield(VulkanApp* app) {
#if STARFIELD_MODE == STARFIELD_MODE_BLOOM
    uint64_t total_verts = MAX_STARS;
    uint64_t total_inds = MAX_STARS;
    Vertex* v = malloc(total_verts * sizeof(Vertex));
    uint32_t* inds = malloc(total_inds * sizeof(uint32_t));
    if (!v || !inds) { free(v); free(inds); fprintf(stderr, "[VK] Out of memory: starfield buffers\n"); exit(1); }
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
    if (!v || !inds) { free(v); free(inds); fprintf(stderr, "[VK] Out of memory: starfield buffers\n"); exit(1); }
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

void scene_update(VulkanApp* app, float deltaTime) {
    double currentTime = glfwGetTime();

    /* Anti-jitter smoothed virtual server clock */
    double clock_err = currentTime - (app->last_shm_time + app->smoothed_shm_time);
    if (fabs(clock_err) > 0.1 || app->smoothed_shm_time == 0) {
        app->smoothed_shm_time = 0;
    } else {
        app->smoothed_shm_time += clock_err * 0.05; 
    }
    
    float rawAlpha = (float)((currentTime - app->last_shm_time - app->smoothed_shm_time) / 0.01666);
    float interpAlpha = rawAlpha;
    if (interpAlpha > 1.0f) interpAlpha = 1.0f;
    if (interpAlpha < 0.0f) interpAlpha = 0.0f;
    
    float t = interpAlpha;
    float t2 = t * t;
    float t3 = t2 * t;
    float h00 = 2*t3 - 3*t2 + 1;
    float h10 = t3 - 2*t2 + t;
    float h01 = -2*t3 + 3*t2;
    float h11 = t3 - t2;

    for (int o=0; o < MAX_NET_OBJECTS; o++) {
        if (!app->smoothObjs[o].first) {
            float dx = app->smoothObjs[o].target_x - app->smoothObjs[o].prev_x;
            float dy = app->smoothObjs[o].target_y - app->smoothObjs[o].prev_y;
            float dz = app->smoothObjs[o].target_z - app->smoothObjs[o].prev_z;
            float distSq = dx*dx + dy*dy + dz*dz;

            if (distSq > 100.0f) {
                app->smoothObjs[o].x = app->smoothObjs[o].target_x;
                app->smoothObjs[o].y = app->smoothObjs[o].target_y;
                app->smoothObjs[o].z = app->smoothObjs[o].target_z;
            } else {
                app->smoothObjs[o].x = h00 * app->smoothObjs[o].prev_x + h10 * app->smoothObjs[o].prev_vx + h01 * app->smoothObjs[o].target_x + h11 * app->smoothObjs[o].vx;
                app->smoothObjs[o].y = h00 * app->smoothObjs[o].prev_y + h10 * app->smoothObjs[o].prev_vy + h01 * app->smoothObjs[o].target_y + h11 * app->smoothObjs[o].vy;
                app->smoothObjs[o].z = h00 * app->smoothObjs[o].prev_z + h10 * app->smoothObjs[o].prev_vz + h01 * app->smoothObjs[o].target_z + h11 * app->smoothObjs[o].vz;
            }
            
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
        
        if (st->frame_id != app->last_shm_frame_id) {
            app->last_shm_frame_id = st->frame_id;
            app->last_shm_time = currentTime;

            SmoothObj nextObjs[MAX_NET_OBJECTS];
            for (int i=0; i<MAX_NET_OBJECTS; i++) {
                nextObjs[i].first = true;
            }
            
            for (int o=0; o < st->object_count && o < MAX_OBJECTS; o++) {
                SharedObject* obj = &st->objects[o];
                if (!obj->active) continue;
                
                int old_idx = -1;
                for (int i=0; i < MAX_NET_OBJECTS; i++) {
                    if (!app->smoothObjs[i].first && app->smoothObjs[i].id == obj->id) {
                        old_idx = i;
                        break;
                    }
                }
                
                if (old_idx == -1) {
                    nextObjs[o].first = false;
                    nextObjs[o].id = obj->id;
                    nextObjs[o].prev_x = (float)obj->shm_x;
                    nextObjs[o].target_x = (float)obj->shm_x;
                    nextObjs[o].x = (float)obj->shm_x;
                    nextObjs[o].prev_y = (float)obj->shm_y;
                    nextObjs[o].target_y = (float)obj->shm_y;
                    nextObjs[o].y = (float)obj->shm_y;
                    nextObjs[o].prev_z = (float)obj->shm_z;
                    nextObjs[o].target_z = (float)obj->shm_z;
                    nextObjs[o].z = (float)obj->shm_z;
                    nextObjs[o].prev_h = (float)obj->h;
                    nextObjs[o].target_h = (float)obj->h;
                    nextObjs[o].h = (float)obj->h;
                    nextObjs[o].prev_m = (float)obj->m;
                    nextObjs[o].target_m = (float)obj->m;
                    nextObjs[o].m = (float)obj->m;
                    nextObjs[o].prev_r = (float)obj->r;
                    nextObjs[o].target_r = (float)obj->r;
                    nextObjs[o].r = (float)obj->r;
                    nextObjs[o].vx = (float)obj->vx;
                    nextObjs[o].vy = (float)obj->vy;
                    nextObjs[o].vz = (float)obj->vz;
                } else {
                    nextObjs[o] = app->smoothObjs[old_idx];
                    nextObjs[o].id = obj->id;
                    nextObjs[o].prev_x = nextObjs[o].target_x;
                    nextObjs[o].prev_y = nextObjs[o].target_y;
                    nextObjs[o].prev_z = nextObjs[o].target_z;
                    nextObjs[o].prev_h = nextObjs[o].target_h;
                    nextObjs[o].prev_m = nextObjs[o].target_m;
                    nextObjs[o].prev_r = nextObjs[o].target_r;
                    nextObjs[o].prev_vx = nextObjs[o].vx;
                    nextObjs[o].prev_vy = nextObjs[o].vy;
                    nextObjs[o].prev_vz = nextObjs[o].vz;
                    nextObjs[o].target_x = (float)obj->shm_x;
                    nextObjs[o].target_y = (float)obj->shm_y;
                    nextObjs[o].target_z = (float)obj->shm_z;
                    nextObjs[o].target_h = (float)obj->h;
                    nextObjs[o].target_m = (float)obj->m;
                    nextObjs[o].target_r = (float)obj->r;
                    nextObjs[o].vx = (float)obj->vx;
                    nextObjs[o].vy = (float)obj->vy;
                    nextObjs[o].vz = (float)obj->vz;
                }
            }
            for (int o=0; o < MAX_NET_OBJECTS; o++) {
                app->smoothObjs[o] = nextObjs[o];
            }
        }

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 1000000;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }
        sem_timedwait(&app->shm->data_ready, &ts);
        
        int h = atomic_load_explicit(&app->shm->event_head, memory_order_acquire); 
        int t_tail = atomic_load_explicit(&app->shm->event_tail, memory_order_acquire);
        
        while (h != t_tail) {
            IPCEvent *ev = &app->shm->event_queue[h];
            if (ev->type == IPC_EV_BOOM) {
                int oldest = 0; float min_life = 999.0f;
                for (int i=0; i<MAX_ACTIVE_BOOMS; i++) if(app->activeBooms[i].life < min_life){ min_life = app->activeBooms[i].life; oldest = i; }
                int i = oldest;
                app->activeBooms[i].x = (float)ev->x1 - 20.0f; 
                app->activeBooms[i].y = (float)ev->z1 - 20.0f; 
                app->activeBooms[i].z = 20.0f - (float)ev->y1; 
                app->activeBooms[i].life = 1.0f;
                app->activeBooms[i].seed = (float)(rand() % 1001);
                app->activeBooms[i].style = 0;
                for (int k=0; k<MAX_ACTIVE_TORPS; k++) {
                    if (app->activeTorps[k].active > 0) {
                        if (ev->extra >= IPC_TORPEDO_ID_OFFSET && app->activeTorps[k].id == ev->extra) {
                            app->activeTorps[k].active = 0;
                            continue;
                        }
                        float tdx = app->activeTorps[k].x - app->activeBooms[i].x;
                        float tdy = app->activeTorps[k].y - app->activeBooms[i].y;
                        float tdz = app->activeTorps[k].z - app->activeBooms[i].z;
                        if (sqrtf(tdx*tdx + tdy*tdy + tdz*tdz) < 2.5f) app->activeTorps[k].active = 0;
                    }
                }
                for (int p=0; p<EXPLOSION_PIXELS; p++){ 
                    app->activeBooms[i].offsets[p][0] = (float)(rand()%800-400)/100.0f; 
                    app->activeBooms[i].offsets[p][1] = (float)(rand()%800-400)/100.0f; 
                    app->activeBooms[i].offsets[p][2] = (float)(rand()%800-400)/100.0f;
                    float r=1, g=1, b=1; switch(p % 6){ case 0: r=0; g=1; b=1; break; case 1: r=1; g=0; b=1; break; case 2: r=1; g=1; b=0; break; case 3: r=1; g=0; b=0; break; case 4: r=0; g=1; b=0; break; case 5: r=1; g=0.5; b=0; break; }
                    app->activeBooms[i].colors[p][0]=r; app->activeBooms[i].colors[p][1]=g; app->activeBooms[i].colors[p][2]=b;
                }
            } else if (ev->type == IPC_EV_BEAM) {
                SG_TRACE3(SG_CAT_PHA, "view[VK]: consume IPC_EV_BEAM owner=%d target=%d emitter=%d s=(%.2f,%.2f,%.2f) t=(%.2f,%.2f,%.2f)",
                          ev->padding[0], ev->extra, ev->padding[1],
                          ev->x1, ev->y1, ev->z1, ev->x2, ev->y2, ev->z2);
                int slot = -1;
                int owner_id = ev->padding[0];
                int target_id = ev->extra;
                int emitter_id = ev->padding[1];

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
                    for (int i=0; i<MAX_ACTIVE_BEAMS; i++) if(app->activeBeams[i].life < min_life){ min_life = app->activeBeams[i].life; oldest = i; }
                    slot = oldest;
                }
                app->activeBeams[slot].sx = (float)ev->x1;
                app->activeBeams[slot].sy = (float)ev->y1;
                app->activeBeams[slot].sz = (float)ev->z1;
                app->activeBeams[slot].tx = (float)ev->x2;
                app->activeBeams[slot].ty = (float)ev->y2;
                app->activeBeams[slot].tz = (float)ev->z2;
                app->activeBeams[slot].life = 1.0f;
                app->activeBeams[slot].owner_id = owner_id;
                app->activeBeams[slot].extra = target_id;
                app->activeBeams[slot].emitter_id = emitter_id;
            } else if (ev->type == IPC_EV_TORPEDO) {
                int tid = ev->extra; int f = -1;
                for (int i=0; i<MAX_ACTIVE_TORPS; i++) { if (app->activeTorps[i].active>0 && app->activeTorps[i].id==tid) { f=i; break; } }
                if (f == -1) {
                    int oldest = 0; float min_life = 999.0f;
                    for (int i=0; i<MAX_ACTIVE_TORPS; i++) if((float)app->activeTorps[i].active < min_life){ min_life = (float)app->activeTorps[i].active; oldest = i; }
                    f = oldest;
                }
                float nx = (float)ev->x1 - 20.0f; float ny = (float)ev->z1 - 20.0f; float nz = 20.0f - (float)ev->y1;
                float vx = (float)ev->x2; float vy = (float)ev->z2; float vz = -(float)ev->y2;
                if (app->activeTorps[f].active > 0) {
                    app->activeTorps[f].x = app->activeTorps[f].x * 0.85f + nx * 0.15f;
                    app->activeTorps[f].y = app->activeTorps[f].y * 0.85f + ny * 0.15f;
                    app->activeTorps[f].z = app->activeTorps[f].z * 0.85f + nz * 0.15f;
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
                for (int i=0; i<MAX_ACTIVE_DISMANTLES; i++) if(app->activeDismantles[i].life < min_life){ min_life = app->activeDismantles[i].life; oldest = i; }
                app->activeDismantles[oldest].x = (float)ev->x1 - 20.0f;
                app->activeDismantles[oldest].y = (float)ev->z1 - 20.0f;
                app->activeDismantles[oldest].z = 20.0f - (float)ev->y1;
                app->activeDismantles[oldest].life = 1.0f;
                app->activeDismantles[oldest].scale = 1.0f;
                int oldest_boom = 0; min_life = 999.0f;
                for (int i=0; i<MAX_ACTIVE_BOOMS; i++) if(app->activeBooms[i].life < min_life){ min_life = app->activeBooms[i].life; oldest_boom = i; }
                app->activeBooms[oldest_boom].x = app->activeDismantles[oldest].x;
                app->activeBooms[oldest_boom].y = app->activeDismantles[oldest].y;
                app->activeBooms[oldest_boom].z = app->activeDismantles[oldest].z;
                app->activeBooms[oldest_boom].life = 1.2f;
                app->activeBooms[oldest_boom].seed = (float)(rand() % 1001);
                app->activeBooms[oldest_boom].style = 1;
                for (int p=0; p<EXPLOSION_PIXELS; p++){
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
                for (int i=0; i<MAX_ACTIVE_DISMANTLES; i++) if(app->activeDismantles[i].life < min_life){ min_life = app->activeDismantles[i].life; oldest = i; }
                app->activeDismantles[oldest].x = (float)ev->x1 - 20.0f;
                app->activeDismantles[oldest].y = (float)ev->z1 - 20.0f;
                app->activeDismantles[oldest].z = 20.0f - (float)ev->y1;
                app->activeDismantles[oldest].life = 1.0f;
                app->activeDismantles[oldest].scale = 0.5f;
            } else if (ev->type == IPC_EV_JUMP) {
                app->jumpArrival.x = (float)ev->x1 - 20.0f;
                app->jumpArrival.y = (float)ev->z1 - 20.0f;
                app->jumpArrival.z = 20.0f - (float)ev->y1;
                app->jumpArrival.h = ev->x2;
                app->jumpArrival.m = ev->y2;
                app->jumpArrival.active = 1;
                app->jumpArrival.jump_type = ev->extra;
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

        app->mapFilter = st->shm_map_filter;
        
        if (st->shm_show_map) {
            if (app->mapAnim < 1.0f) app->mapAnim += 0.04f;
            if (app->mapAnim > 1.0f) app->mapAnim = 1.0f;
        } else {
            if (app->mapAnim > 0.0f) app->mapAnim -= 0.04f;
            if (app->mapAnim < 0.0f) app->mapAnim = 0.0f;
        }

        if (app->jumpArrival.active) {
            if (st->shm_nav_state == 0 || st->shm_nav_state == 9 || st->shm_nav_state == 10) {
                if (app->jumpArrival.timer > 0) app->jumpArrival.timer--;
                else app->jumpArrival.active = 0;
            } else {
                if (app->jumpArrival.timer < 180) app->jumpArrival.timer = 180;
            }
        }

        for (int i = 0; i < MAX_ACTIVE_BEAMS; i++) {
            if (app->activeBeams[i].life > 0) {
                app->activeBeams[i].life -= deltaTime / 0.4f;
            }
        }
        {
            int n_active = 0;
            for (int i = 0; i < MAX_ACTIVE_BEAMS; i++) if (app->activeBeams[i].life > 0) n_active++;
            static int last_n_active = -1;
            if (n_active != last_n_active) {
                SG_TRACE2(SG_CAT_PHA, "view[VK]: active beams %d -> %d (path=%s, mapAnim=%.2f)",
                          last_n_active, n_active, (app->gdd) ? "GPD" : "CPU", app->mapAnim);
                last_n_active = n_active;
            }
        }
        for (int i = 0; i < MAX_ACTIVE_BOOMS; i++) {
            if (app->activeBooms[i].life > 0) {
                app->activeBooms[i].life -= deltaTime;
            }
        }
        for (int i = 0; i < MAX_ACTIVE_DISMANTLES; i++) {
            if (app->activeDismantles[i].life > 0) {
                app->activeDismantles[i].life -= deltaTime * 0.5f;
            }
        }

        if (app->jumpArrival.active && app->jumpArrival.timer > 0) {
            for (int i = 0; i < MAX_ARRIVAL_PARTICLES; i++) {
                if (!app->arrivalParticles[i].active) continue;
                app->arrivalParticles[i].radius -= app->arrivalParticles[i].speed * deltaTime;
                app->arrivalParticles[i].angle += 2.0f * deltaTime;
                
                if (app->arrivalParticles[i].radius < 0.5f) {
                    app->arrivalParticles[i].radius = 40.0f + (float)(rand() % 200) * 0.1f;
                }
                app->arrivalParticles[i].x = app->arrivalParticles[i].radius * cosf(app->arrivalParticles[i].angle);
                app->arrivalParticles[i].y = app->arrivalParticles[i].radius * sinf(app->arrivalParticles[i].angle);
                app->arrivalParticles[i].z = (float)(rand() % 200 - 100) * 0.01f * (app->arrivalParticles[i].radius * 0.1f);
            }
        }

        static float torpAcc = 0;
        torpAcc += deltaTime;
        if (torpAcc >= 0.01666f) {
            for (int j=0; j<MAX_ACTIVE_TORPS; j++) if(app->activeTorps[j].active > 0) app->activeTorps[j].active--;
            torpAcc = 0;
        }

        for (int i = 0; i < MAX_ACTIVE_TORPS; i++) {
            if (app->activeTorps[i].active > 0) {
                float speedScale = deltaTime / 0.01666f;
                app->activeTorps[i].x += app->activeTorps[i].dx * speedScale;
                app->activeTorps[i].y += app->activeTorps[i].dy * speedScale;
                app->activeTorps[i].z += app->activeTorps[i].dz * speedScale;

                if (fabsf(app->activeTorps[i].x) > 20.1f || 
                    fabsf(app->activeTorps[i].y) > 20.1f || 
                    fabsf(app->activeTorps[i].z) > 20.1f) {
                    app->activeTorps[i].active = 0;
                }
            }
        }

        if (st->wormhole.active) {
            if (!app->departureWormhole.active) {
                app->departureWormhole.x = (float)st->wormhole.shm_x - 20.0f;
                app->departureWormhole.y = (float)st->wormhole.shm_z - 20.0f;
                app->departureWormhole.z = 20.0f - (float)st->wormhole.shm_y;
                app->departureWormhole.h = st->shm_h;
                app->departureWormhole.m = st->shm_m;
                app->departureWormhole.jump_type = st->wormhole.extra;
            }
            app->departureWormhole.active = 1;
        } else {
            app->departureWormhole.active = 0;
        }
        if (!app->jumpArrival.active) {
            app->jumpArrival.h = st->shm_h;
            app->jumpArrival.m = st->shm_m;
        }

        app->showBridge = st->shm_show_bridge;
        {
            static int _last_sb = -1;
            if (app->showBridge != _last_sb) {
                PILOT_DIAG("[PILOT DIAG] shm_show_bridge changed: %d → %d  "
                           "(PILOT_SHOW_BRIDGE_VAL=%d, jsIndex=%d)\n",
                           _last_sb, app->showBridge,
                           PILOT_SHOW_BRIDGE_VAL, app->jsIndex);
                _last_sb = app->showBridge;
            }
        }
        {
            int prev_pilot = app->showPilot;
            app->showPilot = (app->showBridge == PILOT_SHOW_BRIDGE_VAL) ? 1 : 0;
            if (app->showPilot && !prev_pilot) {
                app->pilotHeading   = (float)st->shm_h;
                app->pilotMark      = (float)st->shm_m;
                app->pilotRoll      = (float)st->shm_r;
                app->pilotLastSpeed = 0.0f;
                app->jsPrevHatX     = 0.0f;
                app->jsPrevHatY     = 0.0f;
                app->jsLastImpH = -1.0; app->jsLastImpM = -1.0;
                app->jsLastImpS = -1.0; app->jsLastImpTime = 0.0;
                app->jsLastThrS = -1.0; app->jsLastThrTime = 0.0;
                app->jsLastVecH = -1.0; app->jsLastVecM = -1.0;
                app->jsLastVecS = -1.0; app->jsLastVecTime = 0.0;
                app->jsLastPosH = -1.0; app->jsLastPosM = -1.0;
                app->jsLastPosR = -1.0; app->jsLastPosTime = 0.0;
                app->jsRetroActive = 0;
                memset(app->jsPrevBtns, 0, sizeof(app->jsPrevBtns));
            }
        }
        if (app->showPilot) {
            if (app->pilotAnim < 1.0f) app->pilotAnim += PILOT_ANIM_STEP;
            if (app->pilotAnim > 1.0f) app->pilotAnim = 1.0f;
            app->bridgeAnim = 0.0f;
        } else {
            if (app->pilotAnim > 0.0f) app->pilotAnim -= PILOT_ANIM_STEP;
            if (app->pilotAnim < 0.0f) app->pilotAnim = 0.0f;
            if (app->showBridge) {
                if (app->bridgeAnim < 1.0f) app->bridgeAnim += 0.03f;
                if (app->bridgeAnim > 1.0f) app->bridgeAnim = 1.0f;
            } else {
                if (app->bridgeAnim > 0.0f) app->bridgeAnim -= 0.03f;
                if (app->bridgeAnim < 0.0f) app->bridgeAnim = 0.0f;
            }
        }
    }
}
