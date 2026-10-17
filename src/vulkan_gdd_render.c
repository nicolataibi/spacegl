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
 * SPACE GL - GPU-DRIVEN RENDERING (GDD) - VULKAN DEVICE-SIDE PIPELINE
 *
 * Translation Unit: vulkan_gdd_render.c
 * Role: Vulkan 1.4 device-side state, pipelines, and frame execution
 * (Part 2 of the GDD subsystem). Manages compute passes (cull, expand, final),
 * indirect draw passes, dynamic rendering, and per-frame ring buffers.
 * Peer modules: vulkan_gdd_build.c, vulkan_device.c, vulkan_render.c.
 */

#include "spacegl_gdd.h"
#include "spacegl_vulkan_types.h"
#include "sglog.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Forward: VulkanApp fields used by the device-side functions (the
 * full definition lives in include/spacegl_vulkan_types.h; here only
 * the GDD-relevant subset is accessed through a small accessor struct
 * filled by gdd_init from the owning VulkanApp). */

struct GddState {
    /* --- persistent device-side state --- */
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    uint32_t queue_family; /* graphics+compute family */

    /* compute side */
    VkDescriptorSetLayout ds_cull;  /* {0 inst, 1 vis, 2 cnt} */
    VkDescriptorSetLayout ds_exp;   /* {0 inst, 1 vis, 2 cnt, 3 verts} */
    VkDescriptorSetLayout ds_final; /* {0 indirect, 1 cnt} */
    VkPipelineLayout pl_cull;
    VkPipelineLayout pl_expand;
    VkPipelineLayout pl_final;
    VkPipeline pipe_cull;
    VkPipeline pipe_expand;
    VkPipeline pipe_final;

    /* scene side (dynamic rendering: no size dependence) */
    VkDescriptorSetLayout ds_scene; /* {0 verts} */
    VkPipelineLayout pl_scene;
    VkPipeline pipe_scene_opaque;
    VkPipeline pipe_scene_add;

    VkDescriptorPool desc_pool;

    /* MSAA state (CPU-path parity: the whole scene — quadrant cube
     * wireframe included — is drawn with app->msaaSamples, capped at
     * 4x, then resolved to the swapchain image).
     * In this API the dynamic-rendering sample count is defined by the
     * attachments, not by VkRenderingInfo: the color attachment below
     * is multisampled (transient) and carries a per-attachment resolve
     * onto the 1-sample swapchain image; the depth image and the scene
     * pipelines' rasterizationSamples carry the same count (the depth
     * attachment's samples must equal the pipeline's —
     * VUID-VkRenderingInfo-pDepthAttachment-06467). The CPU-driven
     * path keeps its own colorImage/depthImage. */
    VkSampleCountFlagBits msaa;
    VkImage colorImage;
    VkDeviceMemory colorMemory;
    VkImageView colorView;
    VkImage depthImage;
    VkDeviceMemory depthMemory;
    VkImageView depthView;

    /* per-frame ring (same triple-buffer invariant as the CPU path) */
    GddFrame frames[GDD_MAX_FRAMES];
    uint32_t current_frame;

    /* FX funnel diagnostics: set by gdd_build_frame when the dynamic list
     * carries FX (beams/booms/...), used by gdd_record to gate the
     * per-frame push-constant trace (no flood while idle). */
    uint32_t fx_active;

    /* persistent starfield (same distribution as the CPU path) */
    GddStar stars[GDD_STAR_COUNT];

    /* resolved shader directory (build/shaders or /usr/share/spacegl/shaders) */
    char spv_dir[256];

    /* owner back-pointer (VulkanApp) */
    void *app;
};

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */
/* Read an entire file into a malloc'd buffer (NULL on error). */
static uint8_t *gdd_read_file(const char *path, size_t *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *size_out = (size_t)sz;
    return buf;
}

/* Resolve the directory holding the SPIR-V binaries, in the same order
 * the CPU-driven path uses (build/shaders in the dev tree, then the
 * installed /usr/share/spacegl/shaders). The dev path is kept as last
 * resort so the load-error message stays actionable. */
static void gdd_resolve_spv_dir(GddState *g) {
    const char *env_dir = getenv("SPACEGL_SHADERS_DIR");
    if (env_dir && env_dir[0]) {
        char probe[320];
        snprintf(probe, sizeof(probe), "%s/gdd_cull.comp.spv", env_dir);
        FILE *f = fopen(probe, "rb");
        if (f) {
            fclose(f);
            snprintf(g->spv_dir, sizeof(g->spv_dir), "%s", env_dir);
            return;
        }
    }
    const char *candidates[] = {
        "/usr/share/spacegl/shaders",
        "build/shaders",
        "shaders"
    };
    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        char probe[320];
        snprintf(probe, sizeof(probe), "%s/gdd_cull.comp.spv", candidates[i]);
        FILE *f = fopen(probe, "rb");
        if (f) {
            fclose(f);
            snprintf(g->spv_dir, sizeof(g->spv_dir), "%s", candidates[i]);
            return;
        }
    }
    snprintf(g->spv_dir, sizeof(g->spv_dir), "%s", "build/shaders");
}

/* Load one SPIR-V module from g->spv_dir (prints the path on failure). */
static VkShaderModule gdd_load_module(VkDevice device, const GddState *g,
                                      const char *name, bool *ok) {
    *ok = true;
    char path[320];
    snprintf(path, sizeof(path), "%s/%s", g->spv_dir, name);
    size_t sz = 0;
    uint8_t *code = gdd_read_file(path, &sz);
    if (!code) {
        fprintf(stderr, "[GDD] cannot load shader %s\n", path);
        *ok = false;
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo mi = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sz,
        .pCode = (const uint32_t *)code,
    };
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &mi, NULL, &m) != VK_SUCCESS) {
        fprintf(stderr, "[GDD] vkCreateShaderModule failed for %s\n", path);
        *ok = false;
    }
    free(code);
    return m;
}

/* Buffer creation with the physical device (kept in GddState). */
static bool gdd_create_buffer(VkDevice device, VkPhysicalDevice pd,
                              VkDeviceSize size, VkBufferUsageFlags use,
                              VkMemoryPropertyFlags props,
                              VkBuffer *buf, VkDeviceMemory *mem,
                              void **mapped /* may be NULL */) {
    VkBufferCreateInfo bi = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = use,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(device, &bi, NULL, buf) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, *buf, &mr);
    VkPhysicalDeviceMemoryProperties mprops;
    vkGetPhysicalDeviceMemoryProperties(pd, &mprops);
    uint32_t idx = VK_MAX_MEMORY_TYPES;
    for (uint32_t i = 0; i < mprops.memoryTypeCount; i++) {
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mprops.memoryTypes[i].propertyFlags & props) == props) {
            idx = i;
            break;
        }
    }
    if (idx == VK_MAX_MEMORY_TYPES) {
        vkDestroyBuffer(device, *buf, NULL);
        *buf = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = idx,
    };
    if (vkAllocateMemory(device, &ai, NULL, mem) != VK_SUCCESS) {
        vkDestroyBuffer(device, *buf, NULL);
        *buf = VK_NULL_HANDLE;
        return false;
    }
    if (vkBindBufferMemory(device, *buf, *mem, 0) != VK_SUCCESS) {
        vkFreeMemory(device, *mem, NULL);
        vkDestroyBuffer(device, *buf, NULL);
        *buf = VK_NULL_HANDLE;
        return false;
    }
    if (mapped) {
        if (vkMapMemory(device, *mem, 0, size, 0, mapped) != VK_SUCCESS) {
            vkFreeMemory(device, *mem, NULL);
            vkDestroyBuffer(device, *buf, NULL);
            *buf = VK_NULL_HANDLE;
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* depth attachment (MSAA — see GddState.msaa note)                    */
/* ------------------------------------------------------------------ */
static bool gdd_create_depth_image(GddState *g, VulkanApp *app) {
    VkDevice d = g->device;
    VkImageCreateInfo ii = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .extent = { app->swapChainExtent.width, app->swapChainExtent.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = g->msaa,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(d, &ii, NULL, &g->depthImage) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(d, g->depthImage, &mr);
    VkPhysicalDeviceMemoryProperties mprops;
    vkGetPhysicalDeviceMemoryProperties(g->physicalDevice, &mprops);
    uint32_t idx = VK_MAX_MEMORY_TYPES;
    for (uint32_t i = 0; i < mprops.memoryTypeCount; i++)
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mprops.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            idx = i;
            break;
        }
    if (idx == VK_MAX_MEMORY_TYPES) return false;
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = idx,
    };
    if (vkAllocateMemory(d, &ai, NULL, &g->depthMemory) != VK_SUCCESS) return false;
    if (vkBindImageMemory(d, g->depthImage, g->depthMemory, 0) != VK_SUCCESS) return false;
    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = g->depthImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_D32_SFLOAT,
        .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
    };
    if (vkCreateImageView(d, &vi, NULL, &g->depthView) != VK_SUCCESS) return false;
    return true;
}

/* Transient MSAA color attachment: rendered by the scene passes, then
 * resolved onto the swapchain image through the color attachment's
 * per-attachment resolve fields (see gdd_record). Same shape as the
 * CPU-driven path's createColorResources. */
static bool gdd_create_msaa_color_image(GddState *g, VulkanApp *app) {
    VkDevice d = g->device;
    VkImageCreateInfo ii = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = app->swapChainImageFormat,
        .extent = { app->swapChainExtent.width, app->swapChainExtent.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = g->msaa,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(d, &ii, NULL, &g->colorImage) != VK_SUCCESS) return false;
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(d, g->colorImage, &mr);
    VkPhysicalDeviceMemoryProperties mprops;
    vkGetPhysicalDeviceMemoryProperties(g->physicalDevice, &mprops);
    uint32_t idx = VK_MAX_MEMORY_TYPES;
    for (uint32_t i = 0; i < mprops.memoryTypeCount; i++)
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mprops.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            idx = i;
            break;
        }
    if (idx == VK_MAX_MEMORY_TYPES) return false;
    VkMemoryAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mr.size,
        .memoryTypeIndex = idx,
    };
    if (vkAllocateMemory(d, &ai, NULL, &g->colorMemory) != VK_SUCCESS) return false;
    if (vkBindImageMemory(d, g->colorImage, g->colorMemory, 0) != VK_SUCCESS) return false;
    VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = g->colorImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = app->swapChainImageFormat,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    if (vkCreateImageView(d, &vi, NULL, &g->colorView) != VK_SUCCESS) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* queue selection                                                     */
/* ------------------------------------------------------------------ */
bool gdd_pick_queue(VkPhysicalDevice pd, uint32_t *family_out) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, NULL);
    if (count == 0) return false;
    VkQueueFamilyProperties *qf = malloc(sizeof(VkQueueFamilyProperties) * count);
    if (!qf) return false;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, qf);
    bool found = false;
    for (uint32_t i = 0; i < count; i++) {
        if ((qf[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
            (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
            *family_out = i;
            found = true;
            break;
        }
    }
    free(qf);
    return found;
}

/* ------------------------------------------------------------------ */
/* per-frame ring creation                                             */
/* ------------------------------------------------------------------ */
static bool gdd_create_frame_buffers(GddState *g, VulkanApp *app, uint32_t i) {
    (void)app;
    GddFrame *f = &g->frames[i];
    VkDevice d = g->device;
    VkPhysicalDevice pd = g->physicalDevice;
    bool ok = true;
    ok &= gdd_create_buffer(d, pd, GDD_DYN_MAX * GDD_INSTANCE_STRIDE,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &f->dyn_inst, &f->dyn_mem, &f->dyn_ptr);
    ok &= gdd_create_buffer(d, pd, GDD_MAP_MAX * GDD_INSTANCE_STRIDE,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &f->map_inst, &f->map_mem, &f->map_ptr);
    ok &= gdd_create_buffer(d, pd, GDD_DYN_MAX * sizeof(uint32_t),
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            &f->dyn_vis, &f->dvis_mem, NULL);
    ok &= gdd_create_buffer(d, pd, GDD_MAP_MAX * sizeof(uint32_t),
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            &f->map_vis, &f->mvis_mem, NULL);
    ok &= gdd_create_buffer(d, pd, GDD_VERTEX_CAPACITY * GDD_VERTEX_STRIDE,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            &f->verts, &f->vert_mem, NULL);
    ok &= gdd_create_buffer(d, pd, 2 * sizeof(VkDrawIndirectCommand),
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &f->indirect, &f->ind_mem, &f->indirect_ptr);
    ok &= gdd_create_buffer(d, pd, GDD_COUNTS_STRIDE,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            &f->counters, &f->cnt_mem, &f->counters_ptr);
    if (!ok) return false;
    /* one command buffer per slot (same triple-buffer invariant as CPU) */
    VkCommandBufferAllocateInfo cai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = app->commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(d, &cai, &f->cmd) != VK_SUCCESS) return false;
    return true;
}

/* Descriptor set allocation for one frame slot (6 sets, buffers are
 * stable per slot so the sets are written once at creation). */
static bool gdd_create_frame_descs(GddState *g, uint32_t i) {
    GddFrame *f = &g->frames[i];
    VkDevice d = g->device;
    VkDescriptorSet sets[6];
    VkDescriptorSetLayout layouts[6] = {
        g->ds_cull, g->ds_cull, g->ds_exp, g->ds_exp, g->ds_final, g->ds_scene,
    };
    VkDescriptorSetAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = g->desc_pool,
        .descriptorSetCount = 6,
        .pSetLayouts = layouts,
    };
    if (vkAllocateDescriptorSets(d, &ai, sets) != VK_SUCCESS) return false;
    f->desc_cull_dyn = sets[0];
    f->desc_cull_map = sets[1];
    f->desc_exp_dyn  = sets[2];
    f->desc_exp_map  = sets[3];
    f->desc_final    = sets[4];
    f->desc_scene    = sets[5];

    VkDescriptorBufferInfo bi[4];
    bi[0].buffer = f->dyn_inst;  bi[0].offset = 0; bi[0].range = GDD_DYN_MAX * GDD_INSTANCE_STRIDE;
    bi[1].buffer = f->dyn_vis;   bi[1].offset = 0; bi[1].range = GDD_DYN_MAX * sizeof(uint32_t);
    bi[2].buffer = f->counters;  bi[2].offset = 0; bi[2].range = GDD_COUNTS_STRIDE;
    bi[3].buffer = f->verts;     bi[3].offset = 0; bi[3].range = GDD_VERTEX_CAPACITY * GDD_VERTEX_STRIDE;
    VkDescriptorBufferInfo bmap[4];
    bmap[0].buffer = f->map_inst; bmap[0].offset = 0; bmap[0].range = GDD_MAP_MAX * GDD_INSTANCE_STRIDE;
    bmap[1].buffer = f->map_vis;  bmap[1].offset = 0; bmap[1].range = GDD_MAP_MAX * sizeof(uint32_t);
    bmap[2] = bi[2];
    bmap[3] = bi[3];

    VkWriteDescriptorSet w[24];
    int n = 0;
    /* cull dyn: 0 inst, 1 vis, 2 cnt */
    for (int k = 0; k < 3; k++) {
        w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[n].dstSet = f->desc_cull_dyn; w[n].dstBinding = k; w[n].dstArrayElement = 0;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[n].descriptorCount = 1; w[n].pBufferInfo = &bi[k];
        n++;
    }
    /* cull map */
    for (int k = 0; k < 3; k++) {
        w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[n].dstSet = f->desc_cull_map; w[n].dstBinding = k; w[n].dstArrayElement = 0;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[n].descriptorCount = 1; w[n].pBufferInfo = &bmap[k];
        n++;
    }
    /* expand dyn: 0 inst, 1 vis, 2 cnt, 3 verts */
    for (int k = 0; k < 4; k++) {
        w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[n].dstSet = f->desc_exp_dyn; w[n].dstBinding = k; w[n].dstArrayElement = 0;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[n].descriptorCount = 1; w[n].pBufferInfo = &bi[k];
        n++;
    }
    /* expand map */
    for (int k = 0; k < 4; k++) {
        w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[n].dstSet = f->desc_exp_map; w[n].dstBinding = k; w[n].dstArrayElement = 0;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[n].descriptorCount = 1; w[n].pBufferInfo = &bmap[k];
        n++;
    }
    /* final: 0 indirect, 1 cnt */
    VkDescriptorBufferInfo bfin[2];
    bfin[0].buffer = f->indirect; bfin[0].offset = 0; bfin[0].range = 2 * sizeof(VkDrawIndirectCommand);
    bfin[1] = bi[2];
    for (int k = 0; k < 2; k++) {
        w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[n].dstSet = f->desc_final; w[n].dstBinding = k; w[n].dstArrayElement = 0;
        w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        w[n].descriptorCount = 1; w[n].pBufferInfo = &bfin[k];
        n++;
    }
    /* scene: 0 verts */
    w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[n].dstSet = f->desc_scene; w[n].dstBinding = 0; w[n].dstArrayElement = 0;
    w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[n].descriptorCount = 1; w[n].pBufferInfo = &bi[3];
    n++;

    vkUpdateDescriptorSets(d, n, w, 0, NULL);
    return true;
}

/* ------------------------------------------------------------------ */
/* pipelines                                                           */
/* ------------------------------------------------------------------ */
static bool gdd_create_compute_pipelines(GddState *g) {
    VkDevice d = g->device;
    bool ok = true;
    VkShaderModule mods[3] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
    const char *names[3] = { "gdd_cull.comp.spv", "gdd_expand.comp.spv", "gdd_final.comp.spv" };
    VkPipelineLayout *pls[3] = { &g->pl_cull, &g->pl_expand, &g->pl_final };
    VkPipeline *pipes[3] = { &g->pipe_cull, &g->pipe_expand, &g->pipe_final };
    VkDescriptorSetLayout *dsls[3] = { &g->ds_cull, &g->ds_exp, &g->ds_final };
    uint32_t bindings[3] = { 3, 4, 2 };

    for (int i = 0; i < 3; i++) {
        bool mok = true;
        mods[i] = gdd_load_module(d, g, names[i], &mok);
        if (!mok) { ok = false; break; }
        /* descriptor layout: N storage bindings (compute) */
        VkDescriptorSetLayoutBinding lb[4];
        for (uint32_t b = 0; b < bindings[i]; b++) {
            lb[b] = (VkDescriptorSetLayoutBinding){
                .binding = b,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            };
        }
        VkDescriptorSetLayoutCreateInfo dl = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = bindings[i],
            .pBindings = lb,
        };
        if (vkCreateDescriptorSetLayout(d, &dl, NULL, dsls[i]) != VK_SUCCESS) { ok = false; break; }
        /* pipeline layout: set 0 + 32 B compute push constants */
        VkPushConstantRange pcr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, GDD_PC_STRIDE };
        VkPipelineLayoutCreateInfo plc = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = dsls[i],
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pcr,
        };
        if (vkCreatePipelineLayout(d, &plc, NULL, pls[i]) != VK_SUCCESS) { ok = false; break; }
        /* compute pipeline */
        VkPipelineShaderStageCreateInfo stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = mods[i],
            .pName = "main",
        };
        VkComputePipelineCreateInfo cpc = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = stage,
            .layout = *pls[i],
        };
        if (vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &cpc, NULL, pipes[i]) != VK_SUCCESS) { ok = false; break; }
    }
    for (int i = 0; i < 3; i++) if (mods[i]) vkDestroyShaderModule(d, mods[i], NULL);
    return ok;
}

static bool gdd_create_scene_pipelines(GddState *g) {
    VkDevice d = g->device;
    bool ok = true;
    VkShaderModule vmod = gdd_load_module(d, g, "gdd_scene.vert.spv", &ok);
    if (!ok) return false;
    bool fok = true;
    VkShaderModule fmod = gdd_load_module(d, g, "gdd_scene.frag.spv", &fok);
    if (!fok) { vkDestroyShaderModule(d, vmod, NULL); return false; }

    /* scene descriptor layout: 1 storage binding (vertex stage) */
    VkDescriptorSetLayoutBinding lb = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    };
    VkDescriptorSetLayoutCreateInfo dl = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &lb,
    };
    if (vkCreateDescriptorSetLayout(d, &dl, NULL, &g->ds_scene) != VK_SUCCESS) return false;

    VkPushConstantRange pcr = {
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, GDD_SCENE_PC_STRIDE,
    };
    VkPipelineLayoutCreateInfo plc = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &g->ds_scene,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pcr,
    };
    if (vkCreatePipelineLayout(d, &plc, NULL, &g->pl_scene) != VK_SUCCESS) return false;

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vmod, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fmod, .pName = "main" },
    };

    VkPipelineVertexInputStateCreateInfo vin = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .vertexAttributeDescriptionCount = 0,
    };
    VkPipelineInputAssemblyStateCreateInfo ias = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkDynamicState dyn_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dyn_states,
    };
    VkPipelineViewportStateCreateInfo vpst = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rast = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE, /* GDD: double-sided by design */
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    /* MSAA (CPU-path parity): the sample count must equal the
     * multisampled color/depth attachments of the dynamic rendering —
     * in this API the rendering's sample count comes from the
     * attachments themselves. */
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = g->msaa,
    };

    VkPipeline pipelines[2];
    for (int p = 0; p < 2; p++) {
        VkPipelineDepthStencilStateCreateInfo depst = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = p == 0 ? VK_TRUE : VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
        };
        VkPipelineColorBlendAttachmentState cb = {
            .blendEnable = p == 0 ? VK_FALSE : VK_TRUE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };
        VkPipelineColorBlendStateCreateInfo cbs = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &cb,
        };
        VkFormat color_fmt = VK_FORMAT_B8G8R8A8_UNORM;
        VkFormat depth_fmt = VK_FORMAT_D32_SFLOAT;
        VkPipelineRenderingCreateInfo rendering = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &color_fmt,
            .depthAttachmentFormat = depth_fmt,
        };
        VkGraphicsPipelineCreateInfo gi = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2,
            .pStages = stages,
            .pVertexInputState = &vin,
            .pInputAssemblyState = &ias,
            .pViewportState = &vpst,
            .pDynamicState = &dyn,
            .pRasterizationState = &rast,
            .pMultisampleState = &ms,
            .pDepthStencilState = &depst,
            .pColorBlendState = &cbs,
            .layout = g->pl_scene,
            .pNext = &rendering,
        };
        if (vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &gi, NULL, &pipelines[p]) != VK_SUCCESS) return false;
    }
    g->pipe_scene_opaque = pipelines[0];
    g->pipe_scene_add = pipelines[1];
    vkDestroyShaderModule(d, vmod, NULL);
    vkDestroyShaderModule(d, fmod, NULL);
    return true;
}

/* ------------------------------------------------------------------ */
/* starfield (same distribution as the CPU path createStarfield)       */
/* ------------------------------------------------------------------ */
static void gdd_build_starfield(GddState *g) {
    for (uint32_t s = 0; s < GDD_STAR_COUNT; s++) {
        GddStar *st = &g->stars[s];
        float theta = (float)(rand() % 3600) * 0.1f * M_PI / 180.0f;
        float phi = (float)(rand() % 1800) * 0.1f * M_PI / 180.0f;
        float r = 120.0f + (float)(rand() % 1000) * 0.1f;
        st->pos[0] = r * sinf(phi) * cosf(theta);
        st->pos[1] = r * cosf(phi);
        st->pos[2] = r * sinf(phi) * sinf(theta);
        st->color[0] = 0.4f + (float)(rand() % 60) / 100.0f;
        st->color[1] = 0.4f + (float)(rand() % 60) / 100.0f;
        st->color[2] = 0.4f + (float)(rand() % 60) / 100.0f;
        st->scale = 0.1f + (float)(rand() % 50) * 0.01f;
    }
}

/* ------------------------------------------------------------------ */
/* gdd_init / gdd_cleanup                                              */
/* ------------------------------------------------------------------ */
bool gdd_init(VulkanApp *app) {
    GddState *g = calloc(1, sizeof(GddState));
    if (!g) return false;
    g->device = app->device;
    g->physicalDevice = app->physicalDevice;
    g->app = app;
    /* MSAA count shared by both architectures (capped at 4x in
     * pickPhysicalDevice) — must be set before the images/pipelines. */
    g->msaa = app->msaaSamples ? app->msaaSamples : VK_SAMPLE_COUNT_1_BIT;
    if (!gdd_pick_queue(app->physicalDevice, &g->queue_family)) {
        fprintf(stderr, "[GDD] no graphics+compute queue family: falling back to CPU-driven path\n");
        free(g);
        return false;
    }
    gdd_resolve_spv_dir(g);
    gdd_build_starfield(g);

    bool ok = gdd_create_compute_pipelines(g);
    ok &= gdd_create_scene_pipelines(g);
    ok &= gdd_create_depth_image(g, app);
    ok &= gdd_create_msaa_color_image(g, app);
    if (!ok) {
        fprintf(stderr, "[GDD] pipeline creation failed: falling back to CPU-driven path\n");
        gdd_cleanup(app);
        return false;
    }

    /* descriptor pool: 3 frames x 6 sets x 4 bindings (storage) */
    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GDD_MAX_FRAMES * 6 * 4 + 8 };
    VkDescriptorPoolCreateInfo dpc = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = GDD_MAX_FRAMES * 6,
        .poolSizeCount = 1,
        .pPoolSizes = &ps,
    };
    if (vkCreateDescriptorPool(g->device, &dpc, NULL, &g->desc_pool) != VK_SUCCESS) {
        fprintf(stderr, "[GDD] descriptor pool creation failed\n");
        gdd_cleanup(app);
        return false;
    }

    for (uint32_t i = 0; i < GDD_MAX_FRAMES; i++) {
        if (!gdd_create_frame_buffers(g, app, i)) {
            fprintf(stderr, "[GDD] per-frame buffer creation failed (slot %u)\n", i);
            gdd_cleanup(app);
            return false;
        }
        if (!gdd_create_frame_descs(g, i)) {
            fprintf(stderr, "[GDD] per-frame descriptor creation failed (slot %u)\n", i);
            gdd_cleanup(app);
            return false;
        }
    }

    app->gdd = g;
    printf("[GDD] GPU-driven path initialized (queue family %u, %u instance slots, %u vertex capacity, %u sample MSAA)\n",
           g->queue_family, GDD_DYN_MAX, GDD_VERTEX_CAPACITY, (unsigned)g->msaa);
    return true;
}

void gdd_cleanup(VulkanApp *app) {
    GddState *g = (GddState *)app->gdd;
    if (!g) return;
    VkDevice d = g->device;
    if (g->colorView) vkDestroyImageView(d, g->colorView, NULL);
    if (g->colorImage) vkDestroyImage(d, g->colorImage, NULL);
    if (g->colorMemory) vkFreeMemory(d, g->colorMemory, NULL);
    if (g->depthView) vkDestroyImageView(d, g->depthView, NULL);
    if (g->depthImage) vkDestroyImage(d, g->depthImage, NULL);
    if (g->depthMemory) vkFreeMemory(d, g->depthMemory, NULL);
    for (uint32_t i = 0; i < GDD_MAX_FRAMES; i++) {
        GddFrame *f = &g->frames[i];
        if (f->cmd) vkFreeCommandBuffers(d, app->commandPool, 1, &f->cmd);
        if (f->dyn_inst) { if (f->dyn_ptr) vkUnmapMemory(d, f->dyn_mem); vkDestroyBuffer(d, f->dyn_inst, NULL); vkFreeMemory(d, f->dyn_mem, NULL); }
        if (f->map_inst) { if (f->map_ptr) vkUnmapMemory(d, f->map_mem); vkDestroyBuffer(d, f->map_inst, NULL); vkFreeMemory(d, f->map_mem, NULL); }
        if (f->dyn_vis)  { vkDestroyBuffer(d, f->dyn_vis, NULL); vkFreeMemory(d, f->dvis_mem, NULL); }
        if (f->map_vis)  { vkDestroyBuffer(d, f->map_vis, NULL); vkFreeMemory(d, f->mvis_mem, NULL); }
        if (f->verts)    { vkDestroyBuffer(d, f->verts, NULL); vkFreeMemory(d, f->vert_mem, NULL); }
        if (f->indirect) { vkDestroyBuffer(d, f->indirect, NULL); vkFreeMemory(d, f->ind_mem, NULL); }
        if (f->counters) { vkDestroyBuffer(d, f->counters, NULL); vkFreeMemory(d, f->cnt_mem, NULL); }
    }
    vkDestroyDescriptorPool(d, g->desc_pool, NULL);
    if (g->pipe_scene_opaque) vkDestroyPipeline(d, g->pipe_scene_opaque, NULL);
    if (g->pipe_scene_add) vkDestroyPipeline(d, g->pipe_scene_add, NULL);
    if (g->pl_scene) vkDestroyPipelineLayout(d, g->pl_scene, NULL);
    if (g->ds_scene) vkDestroyDescriptorSetLayout(d, g->ds_scene, NULL);
    if (g->pipe_cull) vkDestroyPipeline(d, g->pipe_cull, NULL);
    if (g->pipe_expand) vkDestroyPipeline(d, g->pipe_expand, NULL);
    if (g->pipe_final) vkDestroyPipeline(d, g->pipe_final, NULL);
    if (g->pl_cull) vkDestroyPipelineLayout(d, g->pl_cull, NULL);
    if (g->pl_expand) vkDestroyPipelineLayout(d, g->pl_expand, NULL);
    if (g->pl_final) vkDestroyPipelineLayout(d, g->pl_final, NULL);
    if (g->ds_cull) vkDestroyDescriptorSetLayout(d, g->ds_cull, NULL);
    if (g->ds_exp) vkDestroyDescriptorSetLayout(d, g->ds_exp, NULL);
    if (g->ds_final) vkDestroyDescriptorSetLayout(d, g->ds_final, NULL);
    app->gdd = NULL;
    free(g);
}

/* Re-create the extent-dependent GDD resources (the MSAA color/depth
 * attachments, sized to app->swapChainExtent in gdd_init) after a
 * window resize. Everything else — compute and scene pipelines
 * (dynamic viewport/scissor), descriptor pool, per-frame buffer ring
 * (capacity-sized, see gdd_create_frame_buffers) and the starfield /
 * planet geometry — is extent-independent and is kept as-is, so no
 * fallback to the CPU path and no re-roll of the stars. No-op if
 * app->gdd is NULL. */
void gdd_recreate_size_dependent(VulkanApp *app) {
    GddState *g = (GddState *)app->gdd;
    if (!g) return;
    VkDevice d = g->device;
    if (g->colorView != VK_NULL_HANDLE) vkDestroyImageView(d, g->colorView, NULL);
    if (g->colorImage != VK_NULL_HANDLE) vkDestroyImage(d, g->colorImage, NULL);
    if (g->colorMemory != VK_NULL_HANDLE) vkFreeMemory(d, g->colorMemory, NULL);
    g->colorImage = VK_NULL_HANDLE; g->colorView = VK_NULL_HANDLE; g->colorMemory = VK_NULL_HANDLE;
    if (g->depthView != VK_NULL_HANDLE) vkDestroyImageView(d, g->depthView, NULL);
    if (g->depthImage != VK_NULL_HANDLE) vkDestroyImage(d, g->depthImage, NULL);
    if (g->depthMemory != VK_NULL_HANDLE) vkFreeMemory(d, g->depthMemory, NULL);
    g->depthImage = VK_NULL_HANDLE; g->depthView = VK_NULL_HANDLE; g->depthMemory = VK_NULL_HANDLE;
    if (!gdd_create_msaa_color_image(g, app) || !gdd_create_depth_image(g, app)) {
        /* Should not happen: the extent comes from the surface caps of
         * the same device. If it does, the caller keeps going and the
         * next frame fails validation instead of crashing here. */
        fprintf(stderr, "[GDD] failed to resize MSAA attachments to %ux%u\n",
                (unsigned)app->swapChainExtent.width, (unsigned)app->swapChainExtent.height);
    }
}

/* ================================================================== */
/* ================================================================== */
/* Part 3: the per-frame pipeline (build -> record -> draw)           */
/* ================================================================== */
/* ================================================================== */

#include <stdatomic.h>
#include <time.h>

/* BRIDGE_CAMERA_OFFSET_Y * SCALE_SHIP of the CPU path (kept here so the
 * GDD translation stays free of project-header dependencies). */
#define GDD_BRIDGE_CAMERA_OFFSET (0.30f * 0.45f)
/* Pilot cockpit camera: same formula as PILOT_CAMERA_OFFSET_Y * SCALE_SHIP */
#define GDD_PILOT_CAMERA_OFFSET  (0.12f * 0.45f)


/* Vision cull margin: the quadrant spans [-20,20]^3 (|p| <= 34.6), so
 * cameraDist + 60 always contains the whole scene for any camera
 * distance (min 10), keeping the GDD cull a pure optimization that can
 * never hide what the CPU path would draw. */
#define GDD_CULL_MARGIN 60.0f

/* Minimum screen-space width (pixels, total) enforced on the GDD_MESH_LINE
 * tube cross-section (pushed to the expand pass as GddPC.line_min_wu, see
 * gdd_record). The GDD scene pipeline is triangle-based (no line
 * rasterizer): without this floor the sub-pixel quadrant-cube edges
 * rasterize with stochastic sample coverage — the dashed, flickering
 * wireframe lines (the pipeline now MSAA's like the CPU path, but each
 * sample still covers the thin triangle independently). 1.0 px keeps
 * the exact visual weight of the CPU-driven 1-px MSAA lines (a
 * 1-px-wide quad has a smooth 0.5..1.0 sample coverage under MSAA —
 * no dashes — while anything below sub-pixel would flicker). */
#define GDD_LINE_MIN_PX 1.0f

/* ------------------------------------------------------------------ */
/* View matrix + camera position — mirrors drawFrame() of the         */
/* CPU-driven path (orbital view, bridge view and the blend) so the   */
/* two architectures produce the exact same camera.                    */
/* ------------------------------------------------------------------ */
static void gdd_compute_view(VulkanApp *app, mat4 view, float cam_world[3]) {
    /* 1. Standard orbital view (tactical) */
    mat4 m_std; mat4_identity(m_std);
    mat4_rotate(m_std, -app->angleY * M_PI / 180.0f, (vec3){0, 1, 0});
    mat4_rotate(m_std, app->angleX * M_PI / 180.0f, (vec3){1, 0, 0});
    mat4 T_cam; mat4_translate(T_cam, (vec3){0, 0, -app->cameraDist});
    mat4_multiply(m_std, T_cam, m_std);

    /* Camera world position: C * (R*T) = 0  =>  C * R = (0,0,dist)
     *  =>  C[i] = dist * R[i][2] (R = top-left 3x3, unchanged by T). */
    float cam_orb[3] = {
        app->cameraDist * m_std[0][2],
        app->cameraDist * m_std[1][2],
        app->cameraDist * m_std[2][2],
    };

    /* 2. Bridge view (first person) */
    mat4 m_brg; mat4_identity(m_brg);
    float cam_brg[3] = {0.0f, 0.0f, 0.0f};
    if (app->bridgeAnim > 0.001f) {
        float tactScale = 1.0f - app->mapAnim;
        float px = (app->smoothObjs[0].x - 20.0f) * tactScale;
        float py = (app->smoothObjs[0].z - 20.0f) * tactScale;
        float pz = (20.0f - app->smoothObjs[0].y) * tactScale;
        float ph = app->smoothObjs[0].h;
        float pm = app->smoothObjs[0].m;

        mat4 R_ship; mat4_identity(R_ship);
        mat4_rotate(R_ship, 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});
        mat4_rotate(R_ship, -ph * M_PI / 180.0f, (vec3){0, 1, 0});
        float h_rad = ph * M_PI / 180.0f;
        mat4_rotate(R_ship, pm * M_PI / 180.0f, (vec3){cosf(h_rad), 0, -sinf(h_rad)});

        float ly = (app->showBridge >= 11)
                 ? (-GDD_BRIDGE_CAMERA_OFFSET * tactScale)
                 : ( GDD_BRIDGE_CAMERA_OFFSET * tactScale);
        float wx = ly * R_ship[0][1] + px;
        float wy = ly * R_ship[1][1] + py;
        float wz = ly * R_ship[2][1] + pz;
        cam_brg[0] = wx; cam_brg[1] = wy; cam_brg[2] = wz;

        mat4 R_base; mat4_identity(R_base);
        mat4_rotate(R_base, 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});
        int mode = app->showBridge % 10;
        if (mode == 2) mat4_rotate(R_base, M_PI / 2.0f, (vec3){0, 1, 0});
        else if (mode == 3) mat4_rotate(R_base, -M_PI / 2.0f, (vec3){0, 1, 0});
        else if (mode == 4) mat4_rotate(R_base, M_PI / 4.0f, (vec3){0, 0, 1});
        else if (mode == 5) mat4_rotate(R_base, -M_PI / 4.0f, (vec3){0, 0, 1});
        else if (mode == 6) mat4_rotate(R_base, M_PI, (vec3){0, 1, 0});

        mat4 R_cam_world;
        mat4_multiply(R_base, R_ship, R_cam_world);

        mat4 R_inv; mat4_identity(R_inv);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                R_inv[i][j] = R_cam_world[j][i];
        mat4 T_inv; mat4_translate(T_inv, (vec3){-wx, -wy, -wz});
        mat4_multiply(T_inv, R_inv, m_brg);
    }


    /* 3. Pilot view (show_bridge == 20, cockpit with roll)            */
    mat4 m_pilot; mat4_identity(m_pilot);
    float cam_pilot[3] = {0.0f, 0.0f, 0.0f};
    if (app->showPilot && app->pilotAnim > 0.001f) {
        float tactScale = 1.0f - app->mapAnim;
        float px = (app->smoothObjs[0].x - 20.0f) * tactScale;
        float py = (app->smoothObjs[0].z - 20.0f) * tactScale;
        float pz = (20.0f - app->smoothObjs[0].y) * tactScale;
        float ph = app->smoothObjs[0].h;
        float pm = app->smoothObjs[0].m;

        /* Fetch roll from SHM */
        int r_idx_p = atomic_load(&app->shm->read_index);
        float pr = (float)app->shm->buffers[r_idx_p].shm_r;

        mat4 R_ship; mat4_identity(R_ship);
        mat4_rotate(R_ship, 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});
        mat4_rotate(R_ship, -ph * M_PI / 180.0f, (vec3){0, 1, 0});
        float h_rad = ph * M_PI / 180.0f;
        mat4_rotate(R_ship, pm * M_PI / 180.0f, (vec3){cosf(h_rad), 0, -sinf(h_rad)});
        /* Roll: rotate around the ship's forward axis */
        mat4_rotate(R_ship, pr * M_PI / 180.0f,
                    (vec3){sinf(h_rad), 0.0f, cosf(h_rad)});

        float ly = GDD_PILOT_CAMERA_OFFSET * tactScale;

        float wx = ly * R_ship[0][1] + px;
        float wy = ly * R_ship[1][1] + py;
        float wz = ly * R_ship[2][1] + pz;
        cam_pilot[0] = wx; cam_pilot[1] = wy; cam_pilot[2] = wz;

        mat4 R_base; mat4_identity(R_base);
        mat4_rotate(R_base, 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});
        mat4 R_cam_world;
        mat4_multiply(R_base, R_ship, R_cam_world);

        mat4 R_inv; mat4_identity(R_inv);
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                R_inv[i][j] = R_cam_world[j][i];
        mat4 T_inv; mat4_translate(T_inv, (vec3){-wx, -wy, -wz});
        mat4_multiply(T_inv, R_inv, m_pilot);
    }

    /* 4. Final view interpolation (+ camera position follows it)      */
    if (app->showPilot && app->pilotAnim >= 0.999f) {
        memcpy(view, m_pilot, sizeof(mat4));
        memcpy(cam_world, cam_pilot, sizeof(cam_pilot));
    } else if (app->showPilot && app->pilotAnim > 0.001f) {
        /* Blend from current bridge/tactical toward pilot */
        float *base_v = (app->bridgeAnim >= 0.999f) ? (float *)m_brg : (float *)m_std;
        float *base_c = (app->bridgeAnim >= 0.999f) ? cam_brg : cam_orb;
        float a = app->pilotAnim;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                view[i][j] = ((float(*)[4])base_v)[i][j] * (1.0f - a)
                            + m_pilot[i][j] * a;
        for (int i = 0; i < 3; i++)
            cam_world[i] = base_c[i] * (1.0f - a) + cam_pilot[i] * a;
    } else if (app->bridgeAnim <= 0.001f) {
        memcpy(view, m_std, sizeof(mat4));
        memcpy(cam_world, cam_orb, sizeof(cam_orb));
    } else if (app->bridgeAnim >= 0.999f) {
        memcpy(view, m_brg, sizeof(mat4));
        memcpy(cam_world, cam_brg, sizeof(cam_brg));
    } else {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                view[i][j] = m_std[i][j] * (1.0f - app->bridgeAnim)
                            + m_brg[i][j] * app->bridgeAnim;
        for (int i = 0; i < 3; i++)
            cam_world[i] = cam_orb[i] * (1.0f - app->bridgeAnim)
                         + cam_brg[i] * app->bridgeAnim;
    }
}


/* ------------------------------------------------------------------ */
/* gdd_build_frame — the ONE small per-frame upload of the GDD path.  */
/* Fills the current slot's host-visible instance lists from the shm  */
/* state (classification + effects + statics + map mode).             */
/* ------------------------------------------------------------------ */
void gdd_build_frame(VulkanApp *app, float pulse) {
    GddState *g = (GddState *)app->gdd;
    GddFrame *f = &g->frames[g->current_frame];

    GddBuildCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.objs = app->smoothObjs;
    ctx.beams = app->activeBeams;
    ctx.booms = app->activeBooms;
    ctx.dismantles = app->activeDismantles;
    ctx.torps = app->activeTorps;
    ctx.jump_arrival = &app->jumpArrival;
    ctx.wormhole = &app->departureWormhole;
    ctx.shield_timers = app->shieldHitTimers;
    ctx.map_anim = app->mapAnim;
    ctx.map_filter = app->mapFilter;
    ctx.pulse = pulse;
    ctx.camera_dist = app->cameraDist;
    ctx.stars = g->stars;
    ctx.star_count = (int)GDD_STAR_COUNT;
    ctx.dyn_out = (GddInstance *)f->dyn_ptr;
    ctx.dyn_cap = GDD_DYN_MAX;
    ctx.dyn_count = &f->dyn_count;
    ctx.map_out = (GddInstance *)f->map_ptr;
    ctx.map_cap = GDD_MAP_MAX;
    ctx.map_count = &f->map_count;

    if (app->shm) {
        int r_idx = atomic_load_explicit(&app->shm->read_index, memory_order_acquire);
        GameState *st = &app->shm->buffers[r_idx];

        /* Shield hit detection (CPU parity: the CPU path runs this same
         * logic inside drawShieldEffect). A sector flashes for GAME_TICK_RATE
         * (60, game_config.h) frames whenever its shield value drops. */
        static int g_shield_last[6] = {0};
        static bool g_shield_init = false;
        if (!g_shield_init && st->object_count > 0 && st->objects[0].active) {
            for (int s = 0; s < 6; s++) g_shield_last[s] = st->shm_shields[s];
            g_shield_init = true;
        }
        if (g_shield_init) {
            for (int s = 0; s < 6; s++) {
                if (st->shm_shields[s] < g_shield_last[s]) app->shieldHitTimers[s] = 60;
                g_shield_last[s] = st->shm_shields[s];
            }
        }

        int types[MAX_OBJECTS], factions[MAX_OBJECTS], ship_classes[MAX_OBJECTS];
        int cloaked[MAX_OBJECTS], active[MAX_OBJECTS], platings[MAX_OBJECTS], ids[MAX_OBJECTS];
        int n = st->object_count;
        if (n > MAX_OBJECTS) n = MAX_OBJECTS;
        for (int o = 0; o < n; o++) {
            SharedObject *obj = &st->objects[o];
            types[o] = obj->type;
            factions[o] = obj->faction;
            ship_classes[o] = obj->ship_class;
            cloaked[o] = obj->is_cloaked;
            active[o] = obj->active;
            platings[o] = obj->plating;
            ids[o] = obj->id;
        }
        ctx.types = types;
        ctx.factions = factions;
        ctx.ship_classes = ship_classes;
        ctx.cloaked = cloaked;
        ctx.active = active;
        ctx.platings = platings;
        ctx.ids = ids;
        ctx.object_count = n;
        ctx.raw_objs = st->objects;   /* fallback quando SmoothObj.first=true */
        ctx.player_q[0] = st->shm_q[0];
        ctx.player_q[1] = st->shm_q[1];
        ctx.player_q[2] = st->shm_q[2];
        ctx.show_axes = st->shm_show_axes;
        ctx.show_grid = st->shm_show_grid;
        ctx.show_pilot    = (app->showPilot && app->pilotAnim >= 0.999f) ? 1 : 0;
        ctx.pilot_throttle = (app->pilotLastSpeed / 100.0f); /* normalise 0..1 */

        ctx.galaxy = &app->shm->shm_galaxy[0][0][0];
        ctx.galaxy_size = 40; /* shm_galaxy[41][41][41], coords 1..40 */
    }

    gdd_build_instances(&ctx);

    /* ---------------------------------------------------------------- */
    /* FX FUNNEL DIAGNOSTICS (beam visibility hunt)                     */
    /*                                                                  */
    /* CPU side of THIS frame: f->dyn_ptr / f->dyn_count were just      */
    /* uploaded. GPU side below: f->counters_ptr / f->indirect_ptr hold */
    /* the results of this slot's PREVIOUS use (~3 frames ago) — safe   */
    /* to read: the slot fence was already waited on at the top of      */
    /* gdd_draw_frame. The funnel localizes an FX drop without any      */
    /* GPU-side instrumentation:                                        */
    /*   dyn inst -> dyn_vis      : cull pass                           */
    /*   dyn_vis  -> *_verts      : expand pass (pad/capacity/mesh)     */
    /*   *_verts  -> draw cmds    : final pass                          */
    /*   draw cmds -> pixels      : scene pipeline / frag / blend       */
    /* ---------------------------------------------------------------- */
    {
        int n_beams = 0;
        for (int i = 0; i < (int)GDD_MAX_ACTIVE_BEAMS; i++)
            if (app->activeBeams[i].life > 0.0f) n_beams++;

        int add_inst = 0;
        const GddInstance *insts = (const GddInstance *)f->dyn_ptr;
        for (uint32_t k = 0; k < f->dyn_count; k++)
            if (gdd_flag_bit(insts[k].flags, 1)) add_inst++;

        const GddCounts *gc = (const GddCounts *)f->counters_ptr;
        const VkDrawIndirectCommand *dic = (const VkDrawIndirectCommand *)f->indirect_ptr;

        /* 3-frame ring: correlate CPU additive counts with the GPU
         * counters of the SAME (3-frames-ago) frame. */
        static uint32_t ring_add[3] = {0, 0, 0};
        static uint32_t ring_idx = 0;
        uint32_t add_3f_ago = ring_add[ring_idx]; /* read before overwrite */

        if (n_beams != 0 || add_inst != 0 || gc->additive_verts != 0) {
            SG_TRACE2(SG_CAT_VULKAN,
                      "gdd funnel: cpu(now) dyn_inst=%u add_inst=%d beams_alive=%d | gpu(-3f) dyn_vis=%u map_vis=%u opaque_v=%u add_v=%u | draw(-3f) opaque_vcnt=%u add_vcnt=%u first_vertex=%u",
                      f->dyn_count, add_inst, n_beams,
                      gc->dyn_vis, gc->map_vis, gc->opaque_verts, gc->additive_verts,
                      dic[0].vertexCount, dic[1].vertexCount, dic[1].firstVertex);
        }

        /* Beam-population transition (0<->N only, no per-frame flood):
         * dump the exact GDD_MESH_LINE instance the CPU uploaded for the
         * first tube — position / direction*length / alpha / flags — so we
         * can verify what the GPU actually receives without a GPU debugger.
         * (The beam section of gdd_build_instances already skips a tube when
         * dist<0.1 or the dyn list is full; a WARNING flags that case.) */
        static int last_n_beams = -1;
        if (n_beams != last_n_beams) {
            const GddInstance *li = NULL;
            for (uint32_t k = 0; k < f->dyn_count; k++)
                if ((int)(insts[k].mesh + 0.5f) == GDD_MESH_LINE &&
                    gdd_flag_bit(insts[k].flags, 1)) { li = &insts[k]; break; }
            if (n_beams > 0 && li) {
                SG_TRACE3(SG_CAT_PHA,
                          "gdd: beam population %d -> %d; first additive GDD_MESH_LINE: pos=(%.3f,%.3f,%.3f) dir*len=(%.3f,%.3f,%.3f) alpha=%.3f flags=0x%x (never_cull=%u additive=%u frag_mode=%u)",
                          last_n_beams, n_beams,
                          li->pos[0], li->pos[1], li->pos[2],
                          li->scale[0], li->scale[1], li->scale[2], li->alpha,
                          gdd_flags_to_uint(li->flags),
                          gdd_flag_bit(li->flags, 0), gdd_flag_bit(li->flags, 1), gdd_frag_mode(li->flags));
            } else if (n_beams > 0) {
                SG_WARNING(SG_CAT_PHA,
                           "gdd: %d beam(s) alive but NO additive GDD_MESH_LINE in dyn list (build skipped: dist<0.1, dyn list full, or map_anim>=0.99)",
                           n_beams);
            }
            last_n_beams = n_beams;
        }

        /* Anomaly (visible at default INFO level, rate-limited): additive
         * instances were built 3 frames ago but the GPU published ZERO
         * additive vertices for that frame -> a cull/expand/final drop.
         * (add_v > 0 with no pixels would instead point at the scene
         * pipeline / frag / blend / depth stage.) */
        if (add_3f_ago > 0 && gc->additive_verts == 0) {
            if (sglog_rate("gdd_additive_drop", 2))
                SG_WARNING(SG_CAT_VULKAN,
                           "gdd FUNNEL DROP: %u additive instance(s) built 3 frames ago but GPU published 0 additive vertices (dyn_vis=%u, opaque_v=%u) - cull/expand/final dropping FX",
                           add_3f_ago, gc->dyn_vis, gc->opaque_verts);
        }
        /* Healthy-funnel marker (audit conclusion): the GPU published
         * additive vertices AND the additive draw command is non-zero,
         * 3 frames ago. If the beam is STILL not on screen, the funnel
         * is intact end-to-end and the suspect moves to the pixel side:
         * the MSAA AVERAGE resolve of the transient color, or the depth
         * interaction (the blend state SRC_ALPHA/ONE and the UNLIT frag
         * mode are audited: exact parity with the CPU-driven GL/Vulkan
         * paths, which draw the same beam with GL_SRC_ALPHA/GL_ONE). */
        if (add_3f_ago > 0 && gc->additive_verts > 0 && dic[1].vertexCount > 0) {
            if (sglog_rate("gdd_funnel_ok", 2))
                SG_TRACE3(SG_CAT_VULKAN,
                        "gdd funnel OK: %u add inst -> gpu add_v=%u, draw add_vcnt=%u first_vertex=%u (same frame, -3f); funnel intact - if the beam is still invisible suspect MSAA AVERAGE resolve / depth",
                        add_3f_ago, gc->additive_verts,
                        dic[1].vertexCount, dic[1].firstVertex);
        }
        ring_add[ring_idx] = (uint32_t)add_inst;
        ring_idx = (ring_idx + 1) % 3;

        g->fx_active = (n_beams != 0) || (add_inst != 0);
    }
}

/* ------------------------------------------------------------------ */
/* Small synchronization2 helper (one memory barrier + dep info).     */
/* ------------------------------------------------------------------ */
static void gdd_mem_barrier(VkCommandBuffer cb,
                            VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                            VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
    VkMemoryBarrier2 bar = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    bar.srcStageMask = src_stage;
    bar.srcAccessMask = src_access;
    bar.dstStageMask = dst_stage;
    bar.dstAccessMask = dst_access;
    VkDependencyInfo dep = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                             .memoryBarrierCount = 1, .pMemoryBarriers = &bar };
    vkCmdPipelineBarrier2(cb, &dep);
}

/* ------------------------------------------------------------------ */
/* gdd_record — the GPU side of the architecture. One command buffer: */
/*   fill counters -> cull (2 groups) -> expand (2 groups) -> final   */
/*   -> barriers -> dynamic rendering: 1 DrawIndirect per pass.       */
/* ------------------------------------------------------------------ */
void gdd_record(VkCommandBuffer cb, VulkanApp *app, uint32_t image_idx) {
    GddState *g = (GddState *)app->gdd;
    GddFrame *f = &g->frames[g->current_frame];

    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                    .flags = 0 };
    vkBeginCommandBuffer(cb, &bi);

    /* --- 1. zero the per-frame GPU counters (GPU-side fill: the CPU   */
    /*        never touches the counters after creation) -------------- */
    vkCmdFillBuffer(cb, f->counters, 0, GDD_COUNTS_STRIDE, 0);
    gdd_mem_barrier(cb,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

    /* --- 2. cull: one dispatch per group (same pipeline, the group    */
    /*        is selected through push constants) ---------------------- */
    GddPC pc;
    memset(&pc, 0, sizeof(pc));
    pc.cull_center[0] = 0.0f; pc.cull_center[1] = 0.0f; pc.cull_center[2] = 0.0f;
    pc.cull_radius = app->cameraDist + GDD_CULL_MARGIN;
    pc.capacity = GDD_VERTEX_CAPACITY;

    /* Screen-space floor for the GDD_MESH_LINE tubes (see GDD_LINE_MIN_PX):
     * world units per pixel at the current camera, so a line is never thinner
     * than GDD_LINE_MIN_PX pixels. The GDD rasterizer is triangle-based (no
     * 1-px LINE_LIST primitive) and a sub-pixel tube cross-section rasterizes
     * with stochastic coverage — the dashed, flickering quadrant cube lines.
     * The CPU-driven path gets the same guarantee for free from the
     * fixed-function line rasterizer + MSAA. Uses the ACTUAL camera position
     * (orbital / bridge blend), not cameraDist alone. */
    float fov = 45.0f * (1.0f - app->bridgeAnim) + 65.0f * app->bridgeAnim;
    mat4 view = {0}; float cam_world[3] = {0};
    gdd_compute_view(app, view, cam_world);
    float cam_dist = sqrtf(cam_world[0] * cam_world[0] +
                           cam_world[1] * cam_world[1] +
                           cam_world[2] * cam_world[2]);
    if (cam_dist < 1.0f) cam_dist = 1.0f;
    float wu_per_px = 2.0f * cam_dist * tanf(fov * M_PI / 360.0f)
                    / (float)app->swapChainExtent.height;
    pc.line_min_wu = 0.5f * GDD_LINE_MIN_PX * wu_per_px;

    uint32_t map_wg = (f->map_count + GDD_WORKGROUP - 1u) / GDD_WORKGROUP;
    uint32_t dyn_wg = (f->dyn_count + GDD_WORKGROUP - 1u) / GDD_WORKGROUP;

    if (g->fx_active) {
        SG_TRACE2(SG_CAT_VULKAN,
                  "gdd record: cull r=%.1f cam=(%.1f,%.1f,%.1f) cam_dist=%.1f line_min_wu=%.6f dyn_wg=%u map_wg=%u",
                  pc.cull_radius, cam_world[0], cam_world[1], cam_world[2], cam_dist,
                  pc.line_min_wu, dyn_wg, map_wg);
    }

    if (map_wg > 0) {
        pc.list_count = f->map_count;
        pc.group = 1;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe_cull);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl_cull, 0, 1,
                                &f->desc_cull_map, 0, NULL);
        vkCmdPushConstants(cb, g->pl_cull, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           GDD_PC_STRIDE, &pc);
        vkCmdDispatch(cb, map_wg, 1, 1);
    }
    if (dyn_wg > 0) {
        pc.list_count = f->dyn_count;
        pc.group = 0;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe_cull);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl_cull, 0, 1,
                                &f->desc_cull_dyn, 0, NULL);
        vkCmdPushConstants(cb, g->pl_cull, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           GDD_PC_STRIDE, &pc);
        vkCmdDispatch(cb, dyn_wg, 1, 1);
    }

    gdd_mem_barrier(cb,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

    /* --- 3. expand: same dispatch shape (the entry guards on the      */
    /*        visible count, only known through the counters) ---------- */
    /* --- Passaggio 3A: Espansione Opaque (pc.pad = 0) --- */
    /* --- Passaggio 3A: Espansione Opaque (pc.pad = 0) --- */
    pc.pad = 0u;
    if (map_wg > 0) {
        pc.list_count = f->map_count; pc.group = 1;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe_expand);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl_expand, 0, 1, &f->desc_exp_map, 0, NULL);
        vkCmdPushConstants(cb, g->pl_expand, VK_SHADER_STAGE_COMPUTE_BIT, 0, GDD_PC_STRIDE, &pc);
        vkCmdDispatch(cb, map_wg, 1, 1);
    }
    if (dyn_wg > 0) {
        pc.list_count = f->dyn_count; pc.group = 0;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe_expand);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl_expand, 0, 1, &f->desc_exp_dyn, 0, NULL);
        vkCmdPushConstants(cb, g->pl_expand, VK_SHADER_STAGE_COMPUTE_BIT, 0, GDD_PC_STRIDE, &pc);
        vkCmdDispatch(cb, dyn_wg, 1, 1);
    }

    /* Barriera di sincronizzazione tra opaque e additive */
    gdd_mem_barrier(cb,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

    /* --- Passaggio 3B: Espansione Additive (pc.pad = 1) --- */
    pc.pad = 1u;
    if (map_wg > 0) {
        pc.list_count = f->map_count; pc.group = 1;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe_expand);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl_expand, 0, 1, &f->desc_exp_map, 0, NULL);
        vkCmdPushConstants(cb, g->pl_expand, VK_SHADER_STAGE_COMPUTE_BIT, 0, GDD_PC_STRIDE, &pc);
        vkCmdDispatch(cb, map_wg, 1, 1);
    }
    if (dyn_wg > 0) {
        pc.list_count = f->dyn_count; pc.group = 0;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe_expand);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl_expand, 0, 1, &f->desc_exp_dyn, 0, NULL);
        vkCmdPushConstants(cb, g->pl_expand, VK_SHADER_STAGE_COMPUTE_BIT, 0, GDD_PC_STRIDE, &pc);
        vkCmdDispatch(cb, dyn_wg, 1, 1);
    }

    /* --- 3C. barriera expand_additive → final ------------------------ */
    /* CRITICAL: the additive expand dispatch writes additive_verts;
     * gdd_final.comp reads it to publish VkDrawIndirectCommand[1].
     * Without this barrier, final races expand_additive and reads
     * additive_verts == 0, producing add_vcnt=0 every frame even
     * when add_v > 0 (the symptom reported in log_clt_gpd2.txt). */
    gdd_mem_barrier(cb,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT);

    /* --- 4. final: publish the two VkDrawIndirectCommands ------------ */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pipe_final);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->pl_final, 0, 1,
                            &f->desc_final, 0, NULL);
    vkCmdDispatch(cb, 1, 1, 1);

    /* --- 5. compute -> graphics (buffer barrier + attachment setups) - */
    {
        VkMemoryBarrier2 mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        mb.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        mb.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        mb.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
        mb.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;

        VkImageMemoryBarrier2 ib_color = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        ib_color.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        ib_color.srcAccessMask = 0;
        ib_color.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        ib_color.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        ib_color.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ib_color.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ib_color.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib_color.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib_color.image = app->swapChainImages[image_idx];
        ib_color.subresourceRange = (VkImageSubresourceRange){
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 };

        VkImageMemoryBarrier2 ib_msaa = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        ib_msaa.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        ib_msaa.srcAccessMask = 0;
        ib_msaa.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        ib_msaa.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        ib_msaa.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ib_msaa.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ib_msaa.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib_msaa.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib_msaa.image = g->colorImage;
        ib_msaa.subresourceRange = (VkImageSubresourceRange){
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 };

        VkImageMemoryBarrier2 ib_depth = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        ib_depth.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        ib_depth.srcAccessMask = 0;
        ib_depth.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        ib_depth.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        ib_depth.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ib_depth.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        ib_depth.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib_depth.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib_depth.image = g->depthImage;
        ib_depth.subresourceRange = (VkImageSubresourceRange){
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 };

        VkImageMemoryBarrier2 barriers[3] = { ib_color, ib_msaa, ib_depth };
        VkDependencyInfo dep = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                 .memoryBarrierCount = 1, .pMemoryBarriers = &mb,
                                 .imageMemoryBarrierCount = 3,
                                 .pImageMemoryBarriers = barriers };
        vkCmdPipelineBarrier2(cb, &dep);
    }

    /* --- 6. dynamic rendering with MSAA (Vulkan 1.4): one render     */
    /*        pass, one DrawIndirect per pass, hardware resolve -------
    *        The scene passes draw into a TRANSIENT multisampled color
    *        attachment; the per-attachment resolve (VkRenderingAttach-
    *        mentInfo resolve fields) averages the samples onto the
    *        1-sample swapchain image — the same MSAA + resolve the
    *        CPU-driven render pass does. In this API the rendering's
    *        sample count is defined by the attachments (VkRendering-
    *        Info has no samples member); the depth attachment and the
    *        pipeline rasterizationSamples carry the same count. */
    VkRenderingAttachmentInfo color_att = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    color_att.imageView = g->colorView; /* MSAA transient color */
    color_att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    /* AVERAGE (this SDK's name for the 0x2 SAMPLE_AVERAGE bit): average
     * the MSAA samples onto the 1-sample swapchain image. */
    color_att.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    color_att.resolveImageView = app->swapChainImageViews[image_idx];
    color_att.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; /* transient: only the resolve consumes it */
    color_att.clearValue.color = (VkClearColorValue){{0,0,0,1}}; /* nero opaco, come la path CPU */
    VkRenderingAttachmentInfo depth_att = { .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depth_att.imageView = g->depthView; /* GDD MSAA depth (matches rasterizationSamples) */
    depth_att.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_att.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_att.clearValue.depthStencil.depth = 1.0f;

    VkRenderingInfo ri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO };
    ri.renderArea.extent = app->swapChainExtent;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color_att;
    ri.pDepthAttachment = &depth_att;
    vkCmdBeginRendering(cb, &ri);

    VkViewport vp = { 0.0f, 0.0f, (float)app->swapChainExtent.width,
                      (float)app->swapChainExtent.height, 0.0f, 1.0f };
    VkRect2D sc = { {0, 0}, app->swapChainExtent };
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);

    /* Scene push constants: mvp = view * proj (C row-major product,
     * consumed verbatim by gdd_scene.vert), time, camera position.
     * view/cam_world/fov are computed once at the top (line floor). */
    GddScenePC spc;
    memset(&spc, 0, sizeof(spc));
    memcpy(spc.cam, cam_world, sizeof(spc.cam));
    mat4 proj; mat4 mvp;
    mat4_perspective(fov * M_PI / 180.0f,
                     (float)app->swapChainExtent.width / (float)app->swapChainExtent.height,
                     0.1f, 1000.0f, proj);
    proj[1][1] *= -1.0f; /* CPU-path Y flip, kept for exact parity */
    mat4_multiply(view, proj, mvp);
    memcpy(spc.mvp, mvp, sizeof(mvp));
    spc.time = (float)glfwGetTime();

    /* Pass A: opaque (depth write on) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g->pipe_scene_opaque);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g->pl_scene, 0, 1,
                            &f->desc_scene, 0, NULL);
    vkCmdPushConstants(cb, g->pl_scene,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       GDD_SCENE_PC_STRIDE, &spc);
    vkCmdDrawIndirect(cb, f->indirect, 0, 1, sizeof(VkDrawIndirectCommand));

    /* Pass B: additive glow (depth write off, src-alpha + one) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g->pipe_scene_add);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g->pl_scene, 0, 1,
                            &f->desc_scene, 0, NULL);
    vkCmdPushConstants(cb, g->pl_scene,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       GDD_SCENE_PC_STRIDE, &spc);
    vkCmdDrawIndirect(cb, f->indirect, sizeof(VkDrawIndirectCommand), 1,
                      sizeof(VkDrawIndirectCommand));

    vkCmdEndRendering(cb);

    /* --- 7. present barrier (color -> PRESENT_SRC_KHR) --------------- */
    {
        VkImageMemoryBarrier2 ib = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        ib.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        ib.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        ib.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        ib.dstAccessMask = 0;
        ib.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        ib.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        ib.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib.image = app->swapChainImages[image_idx];
        ib.subresourceRange = (VkImageSubresourceRange){
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 };
        VkDependencyInfo dep = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                 .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &ib };
        vkCmdPipelineBarrier2(cb, &dep);
    }

    vkEndCommandBuffer(cb);
}

/* ------------------------------------------------------------------ */
/* gdd_draw_frame — full GDD frame with the SAME triple-buffer        */
/* invariant as the CPU-driven path (one fence / one acquire semaphore */
/* / one render semaphore per slot; the wait happens at the TOP, so   */
/* the CPU and the GPU overlap).                                      */
/* ------------------------------------------------------------------ */
void gdd_draw_frame(VulkanApp *app) {
    GddState *g = (GddState *)app->gdd;
    uint32_t slot = g->current_frame;
    GddFrame *f = &g->frames[slot];

    /* The GPU must be done with this slot's buffers (last use was
     * GDD_MAX_FRAMES frames ago). From here on the fence is UNSIGNALED,
     * so every early-exit path below re-signals it with an empty
     * submit or the next use of the slot would wait forever. */
    vkWaitForFences(app->device, 1, &app->inFlightFences[slot], VK_TRUE, UINT64_MAX);

    uint32_t imgIdx = 0;
    VkResult acq = vkAcquireNextImageKHR(app->device, app->swapChain, 100000000ULL,
                                         app->imageAvailableSemaphores[slot],
                                         VK_NULL_HANDLE, &imgIdx);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        /* Window resized under us: rebuild the swapchain (backstop for
         * the framebuffer-size callback) and skip this frame. */
        recreateSwapChain(app);
    }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
        /* The fence was just waited on (signaled); reset it first
         * (vkQueueSubmit requires an unsignaled fence). */
        vkResetFences(app->device, 1, &app->inFlightFences[slot]);
        VkSubmitInfo empty = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
        vkQueueSubmit(app->graphicsQueue, 1, &empty, app->inFlightFences[slot]);
        g->current_frame = (slot + 1) % GDD_MAX_FRAMES;
        return;
    }

    app->lastFrameSlot = slot; /* --screenshot bookkeeping */
    app->lastImgIdx = imgIdx;
    vkResetFences(app->device, 1, &app->inFlightFences[slot]);
    vkResetCommandBuffer(f->cmd, 0);

    /* The ONE per-frame CPU upload, then the GPU takes over. */
    gdd_build_frame(app, (float)glfwGetTime());
    gdd_record(f->cmd, app, imgIdx);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .waitSemaphoreCount = 1,
                        .pWaitSemaphores = &app->imageAvailableSemaphores[slot],
                        .pWaitDstStageMask = &wait_stage,
                        .commandBufferCount = 1,
                        .pCommandBuffers = &f->cmd,
                        .signalSemaphoreCount = 1,
                        .pSignalSemaphores = &app->renderFinishedSemaphores[slot] };
    /* No fence on the render submit: vkQueuePresentKHR takes no fence,
     * and the render's fence would signal while the present op is
     * still pending, racing the next use of this slot. */
    vkQueueSubmit(app->graphicsQueue, 1, &si, VK_NULL_HANDLE);

    VkPresentInfoKHR pi = { .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                            .waitSemaphoreCount = 1,
                            .pWaitSemaphores = &app->renderFinishedSemaphores[slot],
                            .swapchainCount = 1,
                            .pSwapchains = &app->swapChain,
                            .pImageIndices = &imgIdx };
    VkResult pres = vkQueuePresentKHR(app->graphicsQueue, &pi);
    /* Queue ops run in order: this empty submit (and its fence)
     * completes only once the PRESENT is done. */
    {
        VkSubmitInfo empty = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
        vkQueueSubmit(app->graphicsQueue, 1, &empty, app->inFlightFences[slot]);
    }
    if (pres == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain(app);
    } else if (pres != VK_SUCCESS && pres != VK_SUBOPTIMAL_KHR) {
        fprintf(stderr, "[GDD] vkQueuePresentKHR failed: %d\n", (int)pres);
    }

    g->current_frame = (slot + 1) % GDD_MAX_FRAMES;

    /* Same frame limiter as the CPU path (~144 fps cap). */
    struct timespec frame_limit = { 0, 6944444L };
    nanosleep(&frame_limit, NULL);

    /* Decrement shield hit timers (CPU-path parity, done per frame). */
    for (int s = 0; s < 6; s++)
        if (app->shieldHitTimers[s] > 0) app->shieldHitTimers[s]--;
}
