/*
 * SPACE GL - VULKAN INTERNAL INTERFACE
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
 * Translation Unit: include/vulkan_device_internal.h
 * Role: Shared internal declarations for the Vulkan rendering engine.
 * Connects vulkan_device.c, vulkan_scene.c, vulkan_render.c, and vulkan_main.c.
 */

#ifndef VULKAN_DEVICE_INTERNAL_H
#define VULKAN_DEVICE_INTERNAL_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "game_config.h"
#include "spacegl_vulkan_types.h"
#include "spacegl_gdd.h"
#include "sglog.h"
#include "nav_math.h"
#include "shared_state.h"

#include "quadrant_geom.h"

typedef struct VulkanApp VulkanContext;

/* Window resolution and viewport constants */
#define VIEW_WINDOW_WIDTH 1920
#define VIEW_WINDOW_HEIGHT 1080
#define VIEW_QUADRANT_HALF QG_HALF
#define VIEW_ROTATION_SPEED 4.5f
#define VIEW_CAMERA_DIST (60.0f * QG_SCALE)

#define SCALE_SHIP 0.45f
#define BRIDGE_CAMERA_OFFSET_Y 0.30f
/* Pilot mode (show_bridge == 20) camera + joystick constants */
#define PILOT_CAMERA_OFFSET_Y  0.12f   /* cockpit: closer to the hull than bridge */
#define PILOT_FOV_DEG          78.0f   /* wider FOV for sense of speed            */
#define PILOT_ANIM_STEP        0.04f   /* transition speed (faster than bridge)   */
#define PILOT_SHOW_BRIDGE_VAL  20      /* reserved show_bridge value for pilot    */
/* Dead zone and rate constants for T.16000M FCS */
#define JS_DEAD_ZONE           0.08f   /* stick drift threshold                   */
#define JS_YAW_AHEAD           90.0f   /* yaw lead at full stick (turn rate deg/s) */
#define JS_PITCH_AHEAD         60.0f   /* pitch lead at full stick (deg/s)         */
#define JS_ROLL_AHEAD          60.0f   /* roll lead at full twist (deg/s)          */
#define JS_STICK_SPEED_MAX     100.0f  /* impulse % at full stick deflection      */
#define JS_IMP_INTERVAL        0.10    /* max rate for "imp" commands (10 Hz)     */
#define JS_POS_INTERVAL        0.20    /* max rate for "pos" commands (5 Hz)      */
#define JS_PHA_LOCKOUT         0.15    /* min interval between "pha" fires        */
#define JS_TOR_LOCKOUT         0.50    /* min interval between "tor" fires        */
#define PILOT_PHA_ENERGY       250     /* energy E of the trigger "pha <E>" shot  */

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

#define CIRCLE_SEGMENTS 72
#define ARC_SEGMENTS 37

extern const uint32_t indices[32];
extern const uint32_t shipIndices[16];
extern const uint32_t torpIndices[24];
extern const uint32_t cubeIndices[24];

static inline int pilot_diag_on(void) {
    static int g_pilot_diag = -1;
    if (g_pilot_diag < 0) g_pilot_diag = (getenv("SPACEGL_PILOT_DIAG") != NULL);
    return g_pilot_diag;
}
#define PILOT_DIAG(...) do { if (pilot_diag_on()) printf(__VA_ARGS__); } while (0)

/* Shared global settings and paths */
extern char vertex_shader_path[512];
extern char fragment_shader_path[512];
extern bool s_no_msaa;
extern char s_shot_path[512];
extern int  s_shot_frame;
extern int  s_rng_seed;
extern bool s_no_rotate;
extern int  s_frame_no;

/* --- vulkan_device.c --- */
void resolve_shader_paths(void);
void createBuffer(VulkanApp* app, VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties, VkBuffer* buffer,
                  VkDeviceMemory* bufferMemory);
void createImage(VulkanApp* app, uint32_t w, uint32_t h, uint32_t mipLevels,
                 VkSampleCountFlagBits numSamples, VkFormat format,
                 VkImageTiling tiling, VkImageUsageFlags usage,
                 VkMemoryPropertyFlags properties, VkImage* image,
                 VkDeviceMemory* imageMemory);
VkImageView createImageView(VkDevice device, VkImage img, VkFormat fmt, VkImageAspectFlags asp, uint32_t mip);
VkShaderModule createShaderModule(VkDevice device, const char* code, size_t codeSize);
void createInstance(VulkanApp* app);
void createSurface(VulkanApp* app);
void pickPhysicalDevice(VulkanApp* app);
void createLogicalDevice(VulkanApp* app);
void createSwapChain(VulkanApp* app);
void createRenderPass(VulkanApp* app);
void createDescriptorSetLayout(VulkanApp* app);
void createGraphicsPipeline(VulkanApp* app);
void createCommandPool(VulkanApp* app);
void createColorResources(VulkanApp* app);
void createDepthResources(VulkanApp* app);
void createSwapChainResources(VulkanApp* app);
void destroySwapChainResources(VulkanApp* app);
void recreateSwapChain(VulkanApp* app);
void createUniformBuffers(VulkanApp* app);
void createDescriptorPool(VulkanApp* app);
void createDescriptorSets(VulkanApp* app);
void createCommandBuffers(VulkanApp* app);
void createSyncObjects(VulkanApp* app);
void framebufferSizeCallback(GLFWwindow* window, int width, int height);
void initVulkan(VulkanApp* app);
void cleanup(VulkanApp* app);

/* --- vulkan_scene.c --- */
uint32_t findMemoryType(VkPhysicalDevice pDevice, uint32_t typeFilter, VkMemoryPropertyFlags props);
void getObjectColor(int type, int faction, float* r, float* g, float* b);
void createSceneMeshes(VulkanApp* app);
void createStarfield(VulkanApp* app);
void drawWormholeCore_v1(VkCommandBuffer cb, VulkanApp* app, mat4 modelBase, float pulse, int type);
void drawWormholeCore_v2(VkCommandBuffer cb, VulkanApp* app, mat4 modelBase, float pulse, int type);
void drawWormholeCore(VkCommandBuffer cb, VulkanApp* app, mat4 modelBase, float pulse, int type, int ver);
void drawSwarmCube(VkCommandBuffer cb, VulkanApp* app, mat4 modelBase, float pulse);
void drawWormhole_v1(VkCommandBuffer cb, VulkanApp* app, float x, float y, float z, double h, double m, int type, float pulse, float tactScale, float closingScale);
void drawWormhole_v2(VkCommandBuffer cb, VulkanApp* app, float x, float y, float z, double h, double m, int type, float pulse, float tactScale, float closingScale);
void drawWormhole(VkCommandBuffer cb, VulkanApp* app, float x, float y, float z, double h, double m, int type, float pulse, float tactScale, float closingScale, int ver);
void drawDiffuseNebula(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float pulse, float tactScale);
void drawDarkNebula(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float pulse, float tactScale);
void drawPlanetaryNebula(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float pulse, float tactScale);
void drawNebula(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, int subType, float pulse, float tactScale);
void drawGalaxyMap(VkCommandBuffer cb, VulkanApp* app, GameState* st, float pulse);
void drawShieldGlow(VkCommandBuffer cb, VulkanApp* app, mat4 modelBase, float radius, float r, float g, float b, float alpha, float pulse);
void drawShieldEffect(VkCommandBuffer cb, VulkanApp* app, float pulse, float tactScale, mat4 R_ship, mat4 T_ship);
void scene_update(VulkanApp* app, float deltaTime);

/* Table draws from spacegl_vulkan_tables.inc */
void drawStarbase(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse, int faction);
void drawPulsar(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse, int type);
void drawComet(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float h, float m, float tactScale, float pulse);
void drawAsteroid(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float size, int resType, float tactScale, float pulse);
void drawMonster(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, int type, float tactScale, float pulse);
void drawTradingHub(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawMine(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawCommBuoy(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawPlatform(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, int faction, float tactScale, float pulse);
void drawRift(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawAlienArtifact(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawNeutronStar(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawMegaStructure(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawVoidCrystal(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawSatellite(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawDysonFragment(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawInterstellarFilament(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawAncientRelic(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawSubspaceRupture(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawDarkMatterCloud(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawSingularity(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawIonStorm(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse, int type);
void drawPlasmaStorm(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawTimeAnomaly(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawSubspaceAnomaly(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);

/* Extra draws from spacegl_vulkan_extras.inl */
void drawBokGlobule_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawInterstellarBubble_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawClumpCore_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawAccretionDisk_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawRelativisticJet_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawShockWave_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawStellarBowShock_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawCosmicVoid_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawCosmicFilament_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawEventHorizon_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawKilonova_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawGravLens_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawGRB_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawGravWave_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawProtoplanetaryDisk_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawDebrisDisk_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawPlanetesimal_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawRoguePlanet_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawBrownDwarf_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawISO_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawMagReconn_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawCurrentSheet_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawHeliosphere_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawTermShock_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawMagnetosphere_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawCosmicString_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawDomainWall_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawDMHalo_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawIGM_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawCGM_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawLymanAlpha_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawCMB_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);

/* --- vulkan_render.c --- */
void recordCommandBuffer(VkCommandBuffer cb, uint32_t idx, VulkanApp* app);
void drawFrame(VulkanApp* app);
void render_frame(VulkanApp* app, float dt);
void vulkan_grab_screenshot(VulkanApp *app, uint32_t slot, const char *path);

/* --- vulkan_main.c / input --- */
void vulkan_send_ipc_command(VulkanApp *app, const char *cmd_str);
void pilot_process_joystick(VulkanApp *app, double now);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void mainLoop(VulkanApp* app);

#endif /* VULKAN_DEVICE_INTERNAL_H */
