/*
 * SPACE GL - VULKAN DEVICE & PIPELINE SUBSYSTEM
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
 * Translation Unit: vulkan_device.c
 * Role: Vulkan instance, physical/logical device initialization, swapchain
 * creation, renderpass, graphic pipelines, sync objects, and resource cleanup.
 * Peer modules: vulkan_scene.c, vulkan_render.c, vulkan_main.c.
 */

#include "vulkan_device_internal.h"
#include <sys/mman.h>

bool s_no_msaa = false;
char vertex_shader_path[512] = "/usr/share/spacegl/shaders/shader.vert.spv";
char fragment_shader_path[512] = "/usr/share/spacegl/shaders/shader.frag.spv";

void resolve_shader_paths(void) {
    const char *env_dir = getenv("SPACEGL_SHADERS_DIR");
    if (env_dir && env_dir[0]) {
        char probe[512];
        snprintf(probe, sizeof(probe), "%s/shader.vert.spv", env_dir);
        if (access(probe, R_OK) == 0) {
            snprintf(vertex_shader_path, sizeof(vertex_shader_path), "%s/shader.vert.spv", env_dir);
            snprintf(fragment_shader_path, sizeof(fragment_shader_path), "%s/shader.frag.spv", env_dir);
            return;
        }
    }
    const char *candidates[] = {
        "/usr/share/spacegl/shaders",
        "build/shaders",
        "shaders"
    };
    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        char probe[512];
        snprintf(probe, sizeof(probe), "%s/shader.vert.spv", candidates[i]);
        if (access(probe, R_OK) == 0) {
            snprintf(vertex_shader_path, sizeof(vertex_shader_path), "%s/shader.vert.spv", candidates[i]);
            snprintf(fragment_shader_path, sizeof(fragment_shader_path), "%s/shader.frag.spv", candidates[i]);
            return;
        }
    }
    strcpy(vertex_shader_path, "build/shaders/shader.vert.spv");
    strcpy(fragment_shader_path, "build/shaders/shader.frag.spv");
}

void createBuffer(VulkanApp* app, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer* buf, VkDeviceMemory* mem) {
    VkBufferCreateInfo bInfo = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size, usage, VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
    if (vkCreateBuffer(app->device, &bInfo, NULL, buf) != VK_SUCCESS) exit(1);
    VkMemoryRequirements mReqs; vkGetBufferMemoryRequirements(app->device, *buf, &mReqs);
    VkMemoryAllocateInfo aInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mReqs.size, findMemoryType(app->physicalDevice, mReqs.memoryTypeBits, props)};
    if (vkAllocateMemory(app->device, &aInfo, NULL, mem) != VK_SUCCESS) exit(1);
    vkBindBufferMemory(app->device, *buf, *mem, 0);
}

void createImage(VulkanApp* app, uint32_t w, uint32_t h, uint32_t mip, VkSampleCountFlagBits numSamples, VkFormat fmt, VkImageTiling til, VkImageUsageFlags usg, VkMemoryPropertyFlags props, VkImage* img, VkDeviceMemory* mem) {
    VkImageCreateInfo iInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, fmt, {w, h, 1}, mip, 1, numSamples, til, usg, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
    if (vkCreateImage(app->device, &iInfo, NULL, img) != VK_SUCCESS) exit(1);
    VkMemoryRequirements mReqs; vkGetImageMemoryRequirements(app->device, *img, &mReqs);
    VkMemoryAllocateInfo aInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mReqs.size, findMemoryType(app->physicalDevice, mReqs.memoryTypeBits, props)};
    if (vkAllocateMemory(app->device, &aInfo, NULL, mem) != VK_SUCCESS) exit(1);
    vkBindImageMemory(app->device, *img, *mem, 0);
}

VkImageView createImageView(VkDevice device, VkImage img, VkFormat fmt, VkImageAspectFlags asp, uint32_t mip) {
    VkImageViewCreateInfo vInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, img, VK_IMAGE_VIEW_TYPE_2D, fmt, {0,0,0,0}, {asp, 0, mip, 0, 1}};
    VkImageView v; if (vkCreateImageView(device, &vInfo, NULL, &v) != VK_SUCCESS) exit(1);
    return v;
}

static char* readShaderFile(const char* filename, size_t* pSize) {
    FILE* file = fopen(filename, "rb");
    if (!file) { fprintf(stderr, "Shader not found: %s\n", filename); return NULL; }
    fseek(file, 0, SEEK_END); long size = ftell(file); fseek(file, 0, SEEK_SET);
    char* buf = malloc((size_t)size);
    if (!buf) { fprintf(stderr, "Out of memory reading shader: %s\n", filename); fclose(file); return NULL; }
    if (fread(buf, 1, (size_t)size, file) != (size_t)size) { free(buf); fclose(file); return NULL; }
    fclose(file); *pSize = (size_t)size; return buf;
}

VkShaderModule createShaderModule(VkDevice device, const char* code, size_t codeSize) {
    VkShaderModuleCreateInfo cInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, codeSize, (const uint32_t*)code};
    VkShaderModule sModule; if (vkCreateShaderModule(device, &cInfo, NULL, &sModule) != VK_SUCCESS) exit(1);
    return sModule;
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
    uint32_t apiVer = VK_API_VERSION_1_0;
    if (app->gdd_wanted) {
        uint32_t drvVer = 0;
        #define VK_API_VERSION_1_3_COMPAT VK_MAKE_API_VERSION(0, 1, 3, 0)
        if (vkEnumerateInstanceVersion(&drvVer) == VK_SUCCESS && drvVer >= VK_API_VERSION_1_3_COMPAT) {
            apiVer = VK_API_VERSION_1_3;
            printf("[GDD] creating Vulkan instance at API version 1.3 (driver exposes %u.%u.%u)\n",
                   VK_VERSION_MAJOR(drvVer), VK_VERSION_MINOR(drvVer), VK_VERSION_PATCH(drvVer));
        } else {
            fprintf(stderr, "[GDD] driver Vulkan %u.%u.%u < 1.3: GPU-driven path unavailable, falling back to CPU-driven\n",
                    VK_VERSION_MAJOR(drvVer), VK_VERSION_MINOR(drvVer), VK_VERSION_PATCH(drvVer));
            app->gdd_wanted = false;
        }
    }
    VkApplicationInfo aInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "SpaceGL", 1, "NoEngine", 1, apiVer};
    uint32_t glfwExtCount = 0; const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);

    bool vk_validation_on;
#ifndef NDEBUG
    vk_validation_on = true;
#else
    vk_validation_on = false;
#endif
    const char *vk_env_val = getenv("SPACEGL_VALIDATION");
    if (vk_env_val && strcmp(vk_env_val, "0") == 0) vk_validation_on = false;
    else if (vk_env_val && strcmp(vk_env_val, "1") == 0) vk_validation_on = true;

    char vk_user[512] = "";
    const char *vk_user_layers = getenv("VK_INSTANCE_LAYERS");
    if (vk_user_layers && vk_user_layers[0])
        snprintf(vk_user, sizeof(vk_user), "%s", vk_user_layers);

    char vk_request[sizeof(vk_user) + 32] = "";
    if (vk_validation_on && strstr(vk_user, "VK_LAYER_KHRONOS_validation") == NULL) {
        snprintf(vk_request, sizeof(vk_request), "VK_LAYER_KHRONOS_validation%s%s",
                 vk_user[0] ? ":" : "", vk_user);
        printf("[VK] validation layer %s (override: SPACEGL_VALIDATION=0/1)\n",
               vk_env_val ? "requested via SPACEGL_VALIDATION=1" : "enabled by default (Debug build)");
    } else {
        snprintf(vk_request, sizeof(vk_request), "%s", vk_user);
    }
    const char *vk_layers[16];
    uint32_t vk_layer_count = 0;
    VkLayerProperties *vk_avail = NULL;
    if (vk_request[0]) {
        uint32_t avail_count = 0;
        if (vkEnumerateInstanceLayerProperties(&avail_count, NULL) == VK_SUCCESS && avail_count > 0) {
            vk_avail = malloc(sizeof(VkLayerProperties) * avail_count);
            uint32_t got = avail_count;
            if (vk_avail && vkEnumerateInstanceLayerProperties(&got, vk_avail) == VK_SUCCESS) {
                char *tok_copy = strdup(vk_request);
                for (char *tok = strtok(tok_copy, ":;, "); tok; tok = strtok(NULL, ":;, ")) {
                    bool match = false;
                    for (uint32_t j = 0; j < got; j++) {
                        if (strcmp(vk_avail[j].layerName, tok) == 0) { match = true; break; }
                    }
                    if (match && vk_layer_count < 16) {
                        vk_layers[vk_layer_count++] = tok;
                    } else if (!match) {
                        fprintf(stderr, "[VK] requested layer '%s' not provided by driver, skipping\n", tok);
                    }
                }
            }
        }
    }

    uint32_t totalExtCount = glfwExtCount;
    const char *allExts[64];
    for (uint32_t i = 0; i < glfwExtCount && i < 60; i++) allExts[i] = glfwExts[i];
    if (app->gdd_wanted) {
        uint32_t instExtCount = 0;
        vkEnumerateInstanceExtensionProperties(NULL, &instExtCount, NULL);
        VkExtensionProperties *instExts = malloc(sizeof(VkExtensionProperties) * instExtCount);
        if (instExts) {
            vkEnumerateInstanceExtensionProperties(NULL, &instExtCount, instExts);
            for (uint32_t i = 0; i < instExtCount; i++) {
                if (strcmp(instExts[i].extensionName, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) == 0) {
                    allExts[totalExtCount++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
                    break;
                }
            }
            free(instExts);
        }
    }

    VkInstanceCreateInfo iInfo = {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &aInfo,
        vk_layer_count, vk_layer_count ? vk_layers : NULL,
        totalExtCount, allExts
    };
    if (vkCreateInstance(&iInfo, NULL, &app->instance) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan instance. Aborting.\n");
        exit(1);
    }
    free(vk_avail);
}

void createSurface(VulkanApp* app) {
    if (glfwCreateWindowSurface(app->instance, app->window, NULL, &app->surface) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create window surface.\n");
        exit(1);
    }
}

static VkSampleCountFlagBits getMaxUsableSampleCount(VkPhysicalDevice pDevice) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(pDevice, &props);
    VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
    if (counts & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

void pickPhysicalDevice(VulkanApp* app) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(app->instance, &count, NULL);
    if (count == 0) {
        fprintf(stderr, "No Vulkan-capable GPU found. Aborting.\n");
        exit(1);
    }
    VkPhysicalDevice* devs = malloc(sizeof(VkPhysicalDevice) * count);
    if (!devs) { fprintf(stderr, "[VK] Out of memory: device list\n"); exit(1); }
    vkEnumeratePhysicalDevices(app->instance, &count, devs);

    uint32_t chosen_idx = 0;
    if (app->gdd_wanted && count > 1) {
        for (uint32_t i = 0; i < count; i++) {
            uint32_t qfam = 0;
            if (!gdd_pick_queue(devs[i], &qfam)) continue;
            VkPhysicalDeviceVulkan13Features q13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
            VkPhysicalDeviceFeatures2 q = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &q13 };
            vkGetPhysicalDeviceFeatures2(devs[i], &q);
            if (q13.synchronization2 && q13.dynamicRendering) {
                chosen_idx = i;
                break;
            }
        }
    }

    app->physicalDevice = devs[chosen_idx];
    if (s_no_msaa) {
        app->msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    } else {
        app->msaaSamples = getMaxUsableSampleCount(app->physicalDevice);
    }
    free(devs);
}

void createLogicalDevice(VulkanApp* app) {
    float prio = 1.0f;
    uint32_t qfam = 0;
    const char* ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkPhysicalDeviceFeatures feat = {0};

    VkPhysicalDeviceVulkan13Features f13;
    VkPhysicalDeviceFeatures2 f2;
    bool gdd_dev = false;
    if (app->gdd_wanted) {
        if (!gdd_pick_queue(app->physicalDevice, &qfam)) {
            fprintf(stderr, "[GDD] no graphics+compute queue family: falling back to CPU-driven path\n");
            app->gdd_wanted = false;
        } else {
            app->gdd_queue_family = qfam;
            VkPhysicalDeviceVulkan13Features q13 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
            VkPhysicalDeviceFeatures2 q = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &q13 };
            vkGetPhysicalDeviceFeatures2(app->physicalDevice, &q);
            if (!q13.synchronization2 || !q13.dynamicRendering) {
                fprintf(stderr, "[GDD] device lacks synchronization2/dynamicRendering: falling back to CPU-driven path\n");
                app->gdd_wanted = false;
            } else {
                gdd_dev = true;
            }
        }
    }

    VkDeviceQueueCreateInfo qInfo = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, qfam, 0, 1, &prio};
    VkDeviceCreateInfo cInfo = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qInfo, 0, NULL, 1, &ext, &feat};
    if (gdd_dev) {
        memset(&f13, 0, sizeof(f13));
        f13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        f13.dynamicRendering = VK_TRUE;
        f13.synchronization2 = VK_TRUE;
        memset(&f2, 0, sizeof(f2));
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f2.pNext = &f13;
        cInfo.pNext = &f2;
        cInfo.pEnabledFeatures = NULL;
        printf("[GDD] device queue family %u (graphics+compute): synchronization2 + dynamicRendering enabled\n", qfam);
    }
    if (vkCreateDevice(app->physicalDevice, &cInfo, NULL, &app->device) != VK_SUCCESS) exit(1);
    vkGetDeviceQueue(app->device, qfam, 0, &app->graphicsQueue);
}

static bool pickSwapChainExtent(VulkanApp* app, VkExtent2D* out) {
    int w, h;
    glfwGetFramebufferSize(app->window, &w, &h);
    if (w <= 0 || h <= 0) return false;
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app->physicalDevice, app->surface, &caps);
    VkExtent2D e = { (uint32_t)w, (uint32_t)h };
    if (caps.minImageExtent.width && e.width < caps.minImageExtent.width) e.width = caps.minImageExtent.width;
    if (caps.minImageExtent.height && e.height < caps.minImageExtent.height) e.height = caps.minImageExtent.height;
    if (caps.maxImageExtent.width && e.width > caps.maxImageExtent.width) e.width = caps.maxImageExtent.width;
    if (caps.maxImageExtent.height && e.height > caps.maxImageExtent.height) e.height = caps.maxImageExtent.height;
    *out = e;
    return true;
}

void createSwapChain(VulkanApp* app) {
    app->swapChainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    if (!pickSwapChainExtent(app, &app->swapChainExtent)) {
        fprintf(stderr, "[VK] window iconified: swapchain creation deferred\n");
        return;
    }
    
    uint32_t modeCount; vkGetPhysicalDeviceSurfacePresentModesKHR(app->physicalDevice, app->surface, &modeCount, NULL);
    VkPresentModeKHR* modes = malloc(sizeof(VkPresentModeKHR) * modeCount);
    if (!modes) { fprintf(stderr, "[VK] Out of memory: present modes\n"); exit(1); }
    vkGetPhysicalDeviceSurfacePresentModesKHR(app->physicalDevice, app->surface, &modeCount, modes);
    
    VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < modeCount; i++) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) { bestMode = modes[i]; break; }
        if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) bestMode = modes[i];
    }
    free(modes);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app->physicalDevice, app->surface, &caps);
    uint32_t minImages = (caps.minImageCount > 2) ? caps.minImageCount : 2;

    VkSwapchainCreateInfoKHR cInfo = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, NULL, 0, app->surface, minImages, app->swapChainImageFormat, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, app->swapChainExtent, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, bestMode, VK_TRUE, VK_NULL_HANDLE};
    if (vkCreateSwapchainKHR(app->device, &cInfo, NULL, &app->swapChain) != VK_SUCCESS) exit(1);
    vkGetSwapchainImagesKHR(app->device, app->swapChain, &app->swapChainImageCount, NULL);
    app->swapChainImages = malloc(sizeof(VkImage)*app->swapChainImageCount);
    if (!app->swapChainImages) { fprintf(stderr, "[VK] Out of memory: swapchain images\n"); exit(1); }
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

void createDescriptorSetLayout(VulkanApp* app) {
    VkDescriptorSetLayoutBinding uboBind = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo dl = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 1, &uboBind};
    if (vkCreateDescriptorSetLayout(app->device, &dl, NULL, &app->descriptorSetLayout) != VK_SUCCESS) exit(1);
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
    VkViewport vp = {0,0,(float)app->swapChainExtent.width,(float)app->swapChainExtent.height,0,1};
    VkRect2D sc = {{0,0},app->swapChainExtent};
    VkPipelineViewportStateCreateInfo vpSt = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,NULL,0,1,&vp,1,&sc};
    VkDynamicState dynSt[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynInfo = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,NULL,0,2,dynSt};
    VkPipelineRasterizationStateCreateInfo rast = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,NULL,0,VK_FALSE,VK_FALSE,VK_POLYGON_MODE_FILL,VK_CULL_MODE_BACK_BIT,VK_FRONT_FACE_CLOCKWISE,VK_FALSE,0,0,0,1.0f};
    VkPipelineMultisampleStateCreateInfo mult = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,NULL,0,app->msaaSamples,VK_FALSE,0.25f,NULL,VK_FALSE,VK_FALSE};
    VkPipelineDepthStencilStateCreateInfo depSt = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,NULL,0,VK_TRUE,VK_TRUE,VK_COMPARE_OP_LESS,VK_FALSE,VK_FALSE, {0}, {0}, 0, 0};
    VkPipelineColorBlendAttachmentState cBlAt = {VK_FALSE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cBl = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &cBlAt, {0,0,0,0}};
    
    createDescriptorSetLayout(app);
    VkPushConstantRange pCr = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo lInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &app->descriptorSetLayout, 1, &pCr};
    if (vkCreatePipelineLayout(app->device, &lInfo, NULL, &app->pipelineLayout) != VK_SUCCESS) exit(1);
    
    VkGraphicsPipelineCreateInfo pInfo = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, NULL, 0, 2, stages, &vIn, &iAs, NULL, &vpSt, &rast, &mult, &depSt, &cBl, &dynInfo, app->pipelineLayout, app->renderPass, 0, VK_NULL_HANDLE, 0};
    if (vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &pInfo, NULL, &app->graphicsPipeline) != VK_SUCCESS) exit(1);
    
    /* 2. Wireframe/Line Pipeline with Blending Enabled */
    iAs.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; rast.polygonMode = VK_POLYGON_MODE_FILL; rast.cullMode = 0;
    cBlAt.blendEnable = VK_TRUE; 
    cBlAt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; 
    cBlAt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    if (vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &pInfo, NULL, &app->wireframePipeline) != VK_SUCCESS) exit(1);
    
    /* 3. Point Sprite Pipeline (for Bloom Effect) */
    iAs.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    cBlAt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cBlAt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    if (vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &pInfo, NULL, &app->pointPipeline) != VK_SUCCESS) exit(1);

    /* 4. Glow Pipeline (Additive Triangle List for Volumetric Spheres) */
    iAs.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    cBlAt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cBlAt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    depSt.depthWriteEnable = VK_FALSE;
    if (vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &pInfo, NULL, &app->glowPipeline) != VK_SUCCESS) exit(1);

    /* 5. Alpha Pipeline (Normal Transparency for Dark Clouds/Bok Globules) */
    cBlAt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    if (vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &pInfo, NULL, &app->alphaPipeline) != VK_SUCCESS) exit(1);

    vkDestroyShaderModule(app->device, fM, NULL); vkDestroyShaderModule(app->device, vM, NULL);
}

void createCommandPool(VulkanApp* app) {
    VkCommandPoolCreateInfo cpInf = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 0};
    if (vkCreateCommandPool(app->device, &cpInf, NULL, &app->commandPool) != VK_SUCCESS) exit(1);
}

void destroySwapChainResources(VulkanApp* app) {
    vkDeviceWaitIdle(app->device);
    for (uint32_t i = 0; i < app->swapChainImageCount; i++)
        vkDestroyFramebuffer(app->device, app->swapChainFramebuffers[i], NULL);
    free(app->swapChainFramebuffers); app->swapChainFramebuffers = NULL;
    for (uint32_t i = 0; i < app->swapChainImageCount; i++)
        vkDestroyImageView(app->device, app->swapChainImageViews[i], NULL);
    free(app->swapChainImageViews); app->swapChainImageViews = NULL;
    free(app->swapChainImages); app->swapChainImages = NULL;
    if (app->imageRenderFinished) {
        for (uint32_t i = 0; i < app->swapChainImageCount; i++)
            vkDestroySemaphore(app->device, app->imageRenderFinished[i], NULL);
        free(app->imageRenderFinished); app->imageRenderFinished = NULL;
    }
    vkDestroySwapchainKHR(app->device, app->swapChain, NULL); app->swapChain = VK_NULL_HANDLE;
    vkDestroyImageView(app->device, app->colorImageView, NULL);
    vkDestroyImage(app->device, app->colorImage, NULL);
    vkFreeMemory(app->device, app->colorImageMemory, NULL);
    vkDestroyImageView(app->device, app->depthImageView, NULL);
    vkDestroyImage(app->device, app->depthImage, NULL);
    vkFreeMemory(app->device, app->depthImageMemory, NULL);
}

void createSwapChainResources(VulkanApp* app) {
    app->swapChainImageViews = malloc(sizeof(VkImageView) * app->swapChainImageCount);
    if (!app->swapChainImageViews) { fprintf(stderr, "[VK] Out of memory: swapchain image views\n"); exit(1); }
    for (uint32_t i = 0; i < app->swapChainImageCount; i++)
        app->swapChainImageViews[i] = createImageView(app->device, app->swapChainImages[i], app->swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    /* Present-wait semaphores are per swapchain image (not per frame in
     * flight): the presentation engine may still hold the semaphore of an
     * image until that image is re-acquired. */
    app->imageRenderFinished = malloc(sizeof(VkSemaphore) * app->swapChainImageCount);
    if (!app->imageRenderFinished) { fprintf(stderr, "[VK] Out of memory: render-finished semaphores\n"); exit(1); }
    {
        VkSemaphoreCreateInfo rsi = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0};
        for (uint32_t i = 0; i < app->swapChainImageCount; i++)
            if (vkCreateSemaphore(app->device, &rsi, NULL, &app->imageRenderFinished[i]) != VK_SUCCESS) exit(1);
    }
    createColorResources(app);
    createDepthResources(app);
    app->swapChainFramebuffers = malloc(sizeof(VkFramebuffer) * app->swapChainImageCount);
    if (!app->swapChainFramebuffers) { fprintf(stderr, "[VK] Out of memory: swapchain framebuffers\n"); exit(1); }
    for (uint32_t i = 0; i < app->swapChainImageCount; i++) {
        VkImageView at[] = {app->colorImageView, app->depthImageView, app->swapChainImageViews[i]};
        VkFramebufferCreateInfo fi = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, NULL, 0, app->renderPass, 3, at, app->swapChainExtent.width, app->swapChainExtent.height, 1};
        if (vkCreateFramebuffer(app->device, &fi, NULL, &app->swapChainFramebuffers[i]) != VK_SUCCESS) exit(1);
    }
}

void recreateSwapChain(VulkanApp* app) {
    VkExtent2D extent;
    if (!pickSwapChainExtent(app, &extent)) return;
    if (extent.width == app->swapChainExtent.width && extent.height == app->swapChainExtent.height) return;
    app->swapChainExtent = extent;

    destroySwapChainResources(app);
    if (app->gdd) gdd_recreate_size_dependent(app);
    createSwapChain(app);
    createSwapChainResources(app);
}

void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    (void)width; (void)height;
    VulkanApp* app = (VulkanApp*)glfwGetWindowUserPointer(window);
    if (!app || app->swapChain == VK_NULL_HANDLE) return;
    recreateSwapChain(app);
}

void createUniformBuffers(VulkanApp* app) {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        createBuffer(app, sizeof(UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &app->uniformBuffers[i], &app->uniformBuffersMemory[i]);
    }
}

void createDescriptorPool(VulkanApp* app) {
    VkDescriptorPoolSize sz = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT};
    VkDescriptorPoolCreateInfo dp = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, MAX_FRAMES_IN_FLIGHT, 1, &sz};
    if (vkCreateDescriptorPool(app->device, &dp, NULL, &app->descriptorPool) != VK_SUCCESS) exit(1);
}

void createDescriptorSets(VulkanApp* app) {
    VkDescriptorSetLayout lys[MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) lys[i] = app->descriptorSetLayout;
    VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, app->descriptorPool, MAX_FRAMES_IN_FLIGHT, lys};
    if (vkAllocateDescriptorSets(app->device, &ai, app->descriptorSets) != VK_SUCCESS) exit(1);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bi = {app->uniformBuffers[i], 0, sizeof(UniformBufferObject)};
        VkWriteDescriptorSet w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, app->descriptorSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &bi, NULL};
        vkUpdateDescriptorSets(app->device, 1, &w, 0, NULL);
    }
}

void createCommandBuffers(VulkanApp* app) {
    VkCommandBufferAllocateInfo ca = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, app->commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, MAX_FRAMES_IN_FLIGHT};
    if (vkAllocateCommandBuffers(app->device, &ca, app->commandBuffers) != VK_SUCCESS) exit(1);
}

void createSyncObjects(VulkanApp* app) {
    VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0};
    VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT};
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(app->device, &si, NULL, &app->imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(app->device, &si, NULL, &app->renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(app->device, &fi, NULL, &app->inFlightFences[i]) != VK_SUCCESS) {
            exit(1);
        }
    }
}

void initVulkan(VulkanApp* app) {
    createInstance(app);
    createSurface(app);
    pickPhysicalDevice(app);
    createLogicalDevice(app);
    
    int width = 0, height = 0;
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(app->window, &width, &height);
        if (width == 0 || height == 0) glfwWaitEvents();
    }
    
    createSwapChain(app);
    createRenderPass(app);
    createSwapChainResources(app);
    createGraphicsPipeline(app);
    createCommandPool(app);
    createSceneMeshes(app);
    createStarfield(app);
    createUniformBuffers(app);
    createDescriptorPool(app);
    createDescriptorSets(app);
    createCommandBuffers(app);
    createSyncObjects(app);
}

void cleanup(VulkanApp* app) {
    vkDeviceWaitIdle(app->device);
    if (app->gdd) gdd_cleanup(app);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(app->device, app->renderFinishedSemaphores[i], NULL);
        vkDestroySemaphore(app->device, app->imageAvailableSemaphores[i], NULL);
        vkDestroyFence(app->device, app->inFlightFences[i], NULL);
    }
    vkDestroyCommandPool(app->device, app->commandPool, NULL);
    destroySwapChainResources(app);
    vkDestroyPipeline(app->device, app->graphicsPipeline, NULL); 
    vkDestroyPipeline(app->device, app->wireframePipeline, NULL); 
    vkDestroyPipeline(app->device, app->pointPipeline, NULL);
    vkDestroyPipeline(app->device, app->glowPipeline, NULL);
    vkDestroyPipeline(app->device, app->alphaPipeline, NULL);
    vkDestroyPipelineLayout(app->device, app->pipelineLayout, NULL); 
    vkDestroyRenderPass(app->device, app->renderPass, NULL);
    vkDestroyBuffer(app->device, app->whIndexBuffer, NULL); vkFreeMemory(app->device, app->whIndexBufferMemory, NULL);
    vkDestroyBuffer(app->device, app->whVertexBuffer, NULL); vkFreeMemory(app->device, app->whVertexBufferMemory, NULL);
    for (int cl = 0; cl <= 13; ++cl) {
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
    vkDestroyBuffer(app->device, app->torpRayVertexBuffer, NULL); vkFreeMemory(app->device, app->torpRayVertexBufferMemory, NULL);
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
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyBuffer(app->device, app->uniformBuffers[i], NULL);
        vkFreeMemory(app->device, app->uniformBuffersMemory[i], NULL);
    }
    vkDestroyDescriptorPool(app->device, app->descriptorPool, NULL);
    vkDestroyDescriptorSetLayout(app->device, app->descriptorSetLayout, NULL);
    vkDestroyDevice(app->device, NULL);
    vkDestroySurfaceKHR(app->instance, app->surface, NULL);
    vkDestroyInstance(app->instance, NULL);
    if (app->window) { glfwDestroyWindow(app->window); }
    glfwTerminate();
    if (app->shm) { munmap(app->shm, sizeof(SharedIPC)); }
    if (app->shm_fd != -1) { close(app->shm_fd); }
    free(app->swapChainImages);
    free(app->swapChainImageViews);
    free(app->swapChainFramebuffers);
}
