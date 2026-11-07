/*
 * SPACE GL - VULKAN RENDER LOOP & COMMAND RECORDING
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
 * Translation Unit: vulkan_render.c
 * Role: Primary rendering loop: recordCommandBuffer(), drawFrame(),
 * render_frame(), view/projection uniform computation, and screenshot capture.
 * Peer modules: vulkan_device.c, vulkan_scene.c, vulkan_main.c.
 */

#include "object_types.h"
#include "vulkan_device_internal.h"
#include "vulkan_cpu_lod.h"   /* after the Vulkan headers: wraps vkCmdPushConstants / vkCmdBindPipeline */
#include "frame_bench.h"
#include <time.h>

char s_shot_path[512] = {0};
int  s_shot_frame     = 30;
int  s_rng_seed       = -1;
bool s_no_rotate      = false;
int  s_frame_no       = 0;
int s_bench_frames = 0;
int s_bench_warmup = 30;
int s_bench_objects = 0;
bool s_frame_limit_off = false;

/* ------------------------------------------------------------------ */
/* GPU time per section (2026.11.02.01)                               */
/* ------------------------------------------------------------------ */
#define VKGT_MAX_MARKS  512   /* timestamps a frame: the CPU path marks every object with SPACEGL_VK_GPU_TIMES=1 */
#define VKGT_MAX_LABELS 64
#define VKGT_SLOTS     ((uint32_t)MAX_FRAMES_IN_FLIGHT)
typedef struct {
    int state;                       /* 0 not tried, 1 on, -1 off */
    bool detail;                     /* SPACEGL_VK_GPU_TIMES=1: per-object marks too (not in a plain bench) */
    VkDevice dev;
    VkQueryPool pool;
    double ms_per_tick;
    uint32_t valid_bits;
    int print_every;                 /* frames between lines (0 = only the final report) */
    const char *labels[VKGT_MAX_LABELS];               /* in order of first use */
    int n_labels;
    uint8_t tag[MAX_FRAMES_IN_FLIGHT][VKGT_MAX_MARKS]; /* label of the interval ending at each mark */
    uint32_t used[MAX_FRAMES_IN_FLIGHT];
    double sum[VKGT_MAX_LABELS];
    double sum_total;
    uint64_t frames;
} VkGpuTimes;
static VkGpuTimes g_gt;

static void vkgt_print(const char *label) {
    if (g_gt.frames == 0) return;
    double n = (double)g_gt.frames;
    printf("[GPU-TIMES] %s: %llu frames, GPU %.3f ms:", label, (unsigned long long)g_gt.frames, g_gt.sum_total / n);
    for (int i = 1; i < g_gt.n_labels; i++) printf(" %s %.3f", g_gt.labels[i], g_gt.sum[i] / n);
    printf("\n");
    fflush(stdout);
}

void vkgt_reset_stats(void) {
    memset(g_gt.sum, 0, sizeof(g_gt.sum));
    g_gt.sum_total = 0.0;
    g_gt.frames = 0;
}

static void vkgt_try_init(VulkanApp *app) {
    const char *e = getenv("SPACEGL_VK_GPU_TIMES");
    bool env_on = e && e[0] == '1';
    if (!env_on && s_bench_frames <= 0) {
        g_gt.state = -1;
        return;
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(app->physicalDevice, &props);
    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(app->physicalDevice, &nq, NULL);
    if (nq == 0) {
        g_gt.state = -1;
        return;
    }
    VkQueueFamilyProperties *qp = (VkQueueFamilyProperties *)calloc(nq, sizeof(*qp));
    if (!qp) {
        g_gt.state = -1;
        return;
    }
    vkGetPhysicalDeviceQueueFamilyProperties(app->physicalDevice, &nq, qp);
    g_gt.valid_bits = qp[0].timestampValidBits;   /* the graphics queue is family 0 */
    free(qp);
    if (g_gt.valid_bits == 0 || props.limits.timestampPeriod <= 0.0f) {
        fprintf(stderr, "[GPU-TIMES] timestamps not supported on the graphics queue: off\n");
        g_gt.state = -1;
        return;
    }
    VkQueryPoolCreateInfo qi = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                 .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                 .queryCount = VKGT_SLOTS * VKGT_MAX_MARKS };
    if (vkCreateQueryPool(app->device, &qi, NULL, &g_gt.pool) != VK_SUCCESS) {
        g_gt.state = -1;
        return;
    }
    g_gt.dev = app->device;
    g_gt.ms_per_tick = (double)props.limits.timestampPeriod * 1.0e-6;
    g_gt.print_every = (env_on && s_bench_frames <= 0) ? 120 : 0;
    /* A timestamp after every object of the CPU-driven path: only when
     * asked for, so that a plain --bench-frames (gpu_golden_check.sh)
     * times the frame as it is */
    g_gt.detail = env_on;
    g_gt.labels[0] = "start";
    g_gt.n_labels = 1;
    g_gt.state = 1;
}

static int vkgt_label(const char *name) {
    for (int i = 0; i < g_gt.n_labels; i++)
        if (g_gt.labels[i] == name || strcmp(g_gt.labels[i], name) == 0) return i;
    if (g_gt.n_labels == VKGT_MAX_LABELS) return VKGT_MAX_LABELS - 1;
    g_gt.labels[g_gt.n_labels] = name;
    return g_gt.n_labels++;
}

void vkgt_mark(VkCommandBuffer cb, uint32_t slot, const char *name) {
    if (g_gt.state != 1 || slot >= VKGT_SLOTS) return;
    uint32_t i = g_gt.used[slot];
    if (i >= VKGT_MAX_MARKS) return;
    g_gt.tag[slot][i] = (uint8_t)vkgt_label(name);
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_gt.pool, slot * VKGT_MAX_MARKS + i);
    g_gt.used[slot] = i + 1;
}

/* A mark only with SPACEGL_VK_GPU_TIMES=1 (per object), leaving room for
 * the marks that close the frame. */
void vkgt_mark_detail(VkCommandBuffer cb, uint32_t slot, const char *name) {
    if (g_gt.state != 1 || !g_gt.detail || slot >= VKGT_SLOTS || g_gt.used[slot] + 4 >= VKGT_MAX_MARKS) return;
    vkgt_mark(cb, slot, name);
}

/* SPACEGL_VK_GPU_TIMES_DRAWS=<kind> (with SPACEGL_VK_GPU_TIMES=1, CPU-driven
 * path, 2026.11.07.04): a timestamp after every draw of the objects of that
 * kind, labelled "<kind> <pipeline> <indices>", to find which of an
 * object's draws takes the time. */
static const VulkanApp *s_mark_app;
void cpu_draw_mark(VkCommandBuffer cb, uint64_t index_count) {
    if (g_gt.state != 1 || !g_gt.detail || !s_mark_app) return;
    static char names[VKGT_MAX_LABELS][48];
    static int n_names = 0;
    VkPipeline p = g_sphere_lod.pipe;
    const char *pipe = p == s_mark_app->graphicsPipeline    ? "lit"
                       : p == s_mark_app->wireframePipeline ? "wire"
                       : p == s_mark_app->glowPipeline      ? "glow"
                       : p == s_mark_app->alphaPipeline     ? "alpha"
                       : p == s_mark_app->pointPipeline     ? "point"
                                                            : "other";
    char want[48];
    snprintf(want, sizeof(want), "%s:%s/%llu", g_cpu_kind_names[g_cpu_cmds.kind], pipe, (unsigned long long)index_count);
    for (int i = 0; i < n_names; i++)
        if (strcmp(names[i], want) == 0) {
            vkgt_mark_detail(cb, s_mark_app->currentFrame, names[i]);
            return;
        }
    if (n_names == VKGT_MAX_LABELS) return;   /* too many kinds of draw: the rest goes to the next mark */
    snprintf(names[n_names], sizeof(names[n_names]), "%s", want);
    vkgt_mark_detail(cb, s_mark_app->currentFrame, names[n_names++]);
}

/* Outside any render pass, first command of the frame. */
void vkgt_begin(VkCommandBuffer cb, VulkanApp *app, uint32_t slot) {
    if (g_gt.state == 0) vkgt_try_init(app);
    if (g_gt.state != 1 || slot >= VKGT_SLOTS) return;
    vkCmdResetQueryPool(cb, g_gt.pool, slot * VKGT_MAX_MARKS, VKGT_MAX_MARKS);
    g_gt.used[slot] = 0;
    vkgt_mark(cb, slot, "start");
}

/* Only after the slot's fence (or a queue wait): its results are final. */
void vkgt_collect(VulkanApp *app, uint32_t slot) {
    (void)app;
    if (g_gt.state != 1 || slot >= VKGT_SLOTS) return;
    uint32_t n = g_gt.used[slot];
    if (n < 2) return;
    static uint64_t r[VKGT_MAX_MARKS];
    VkResult rc = vkGetQueryPoolResults(g_gt.dev, g_gt.pool, slot * VKGT_MAX_MARKS, n,
                                        sizeof(uint64_t) * n, r, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (rc != VK_SUCCESS) return;   /* not ready: the next collect of this slot retries */
    g_gt.used[slot] = 0;
    uint64_t mask = g_gt.valid_bits >= 64 ? ~0ULL : ((1ULL << g_gt.valid_bits) - 1ULL);
    for (uint32_t i = 1; i < n; i++)
        g_gt.sum[g_gt.tag[slot][i]] += (double)((r[i] - r[i - 1]) & mask) * g_gt.ms_per_tick;
    g_gt.sum_total += (double)((r[n - 1] - r[0]) & mask) * g_gt.ms_per_tick;
    g_gt.frames++;
    if (g_gt.print_every > 0 && g_gt.frames >= (uint64_t)g_gt.print_every) {
        vkgt_print("last 120 frames");
        vkgt_reset_stats();
    }
}

void vkgt_report(const char *label) {
    if (g_gt.state == 1) vkgt_print(label);
}

/* The query pool goes with the device: left alive it was reported at exit
 * (VUID-vkDestroyDevice-device-05137, every --bench-frames run). */
void vkgt_destroy(VkDevice dev) {
    if (g_gt.state == 1 && g_gt.pool != VK_NULL_HANDLE) vkDestroyQueryPool(dev, g_gt.pool, NULL);
    g_gt.pool = VK_NULL_HANDLE;
    g_gt.state = -1;
}


void recordCommandBuffer(VkCommandBuffer cb, uint32_t idx, VulkanApp* app) {
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,NULL,0,NULL}; vkBeginCommandBuffer(cb, &bi);
    vkgt_begin(cb, app, app->currentFrame);
    sphere_lod_begin_frame(app->wireframePipeline, app->graphicsPipeline);
    g_cpu_cmds.kind = CPUK_SCENE;
    s_mark_app = app;
    {   /* SPACEGL_VK_GPU_TIMES_DRAWS=<kind name>: the draws of that kind one by one */
        static int draws_kind = -2;
        if (draws_kind == -2) {
            const char *e = getenv("SPACEGL_VK_GPU_TIMES_DRAWS");
            draws_kind = -1;
            for (int k = 0; e && k < CPUK_N; k++)
                if (strcmp(e, g_cpu_kind_names[k]) == 0) draws_kind = k;
        }
        g_cpu_cmds.draw_marks_kind = draws_kind;
    }
    VkClearValue cl[2] = {0}; cl[0].color = (VkClearColorValue){{0,0,0,1}}; cl[1].depthStencil = (VkClearDepthStencilValue){1,0};
    VkRenderPassBeginInfo ri = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,NULL,app->renderPass,app->swapChainFramebuffers[idx],{{0,0},app->swapChainExtent}, 2, cl};
    vkCmdBeginRenderPass(cb, &ri, VK_SUBPASS_CONTENTS_INLINE);
    /* Viewport/scissor are dynamic (see createGraphicsPipeline): sync
       them to the live swapchain extent so the image follows window
       resizes. One setting covers every pipeline of the frame (they
       all enable the same dynamic states). */
    VkViewport dvp = {0,0,(float)app->swapChainExtent.width,(float)app->swapChainExtent.height,0,1};
    VkRect2D dsc = {{0,0},app->swapChainExtent};
    vkCmdSetViewport(cb, 0, 1, &dvp);
    vkCmdSetScissor(cb, 0, 1, &dsc);
    
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

            /* 0. Tactical Cube (Bounding Box) */
            PushConstants pc = {0}; mat4_identity(pc.model); pc.usePushColor = 4; /* Unlit Vertex Colors */
            mat4 S_tact; mat4_scale(S_tact, (vec3){tactScale, tactScale, tactScale});
            memcpy(pc.model, S_tact, sizeof(mat4));
            vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
            vkCmdBindVertexBuffers(cb, 0, 1, &app->vertexBuffer, &off);
            vkCmdBindIndexBuffer(cb, app->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, sizeof(indices)/4, 1, 0, 0, 0);

            if (st->shm_show_axes && st->object_count > 0 && !app->smoothObjs[0].first && app->cameraDist < QG_GRID_HIDE_DIST && !app->jumpArrival.active) {
            /* Zero-Lag AR Compass: Anchored to Smoothed Player Coordinates for fluid motion */
            float ox = app->smoothObjs[0].x;
            float oy = app->smoothObjs[0].y;
            float oz = app->smoothObjs[0].z;
            float oh = app->smoothObjs[0].h;
            float om = app->smoothObjs[0].m;
            float oro = app->smoothObjs[0].r;

            /* Correct Mapping: X_v = X_s - 20, Y_v = oz - 20, Z_v = 20 - oy */
            vec3 p = {(ox - QG_HALF) * tactScale, (oz - QG_HALF) * tactScale, (QG_HALF - oy) * tactScale};
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
            mat4_rotate(R_pitch_h, (float)(om * M_PI / 180.0f), (vec3){cosf((float)h_rad), 0, -sinf((float)h_rad)});
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
            mat4_rotate(R_head_m, (float)((90.0f - oh) * M_PI / 180.0f), (vec3){0, 1, 0});
            
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
            mat4_rotate(R_roll_c, (float)((90.0f - oh) * M_PI / 180.0f), (vec3){0, 1, 0});
            /* 2. Pitch around dynamic transverse axis (to stay perpendicular to the vector) */
            double h_rad_r = oh * M_PI / 180.0;
            mat4_rotate(R_roll_c, (float)(om * M_PI / 180.0f), (vec3){cosf((float)h_rad_r), 0, -sinf((float)h_rad_r)});

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
                mat4_rotate(R_v, (float)(90.0f * M_PI / 180.0f), (vec3){0, 1, 0});
                /* 2. Heading (around vertical Y) */
                mat4_rotate(R_v, (float)(-oh * M_PI / 180.0f), (vec3){0, 1, 0});
                /* 3. Pitch (around the dynamic horizontal axis, nose up = positive elevation) */
                double h_rad_v = oh * M_PI / 180.0f;
                mat4_rotate(R_v, (float)(om * M_PI / 180.0f), (vec3){cosf((float)h_rad_v), 0, -sinf((float)h_rad_v)});
                
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
                mat4_rotate(R_u, (float)(-90.0f * M_PI / 180.0f), (vec3){0, 0, 1});

                /* 2. Apply exactly the same rotations as the ship to stay synced */
                /* Base alignment (+90 North) then Heading (-oh) around vertical Y */
                mat4_rotate(R_u, (float)((90.0f - oh) * M_PI / 180.0f), (vec3){0, 1, 0});
                
                /* Pitch around dynamic transverse axis */
                double h_rad_u = oh * M_PI / 180.0;
                mat4_rotate(R_u, (float)(om * M_PI / 180.0f), (vec3){cosf((float)h_rad_u), 0, -sinf((float)h_rad_u)});
                
                /* Roll around dynamic longitudinal axis (Nose direction) */
                float pitch_rad_u = (float)(om * M_PI / 180.0f);
                vec3 nose_u = {sinf((float)h_rad_u)*cosf(pitch_rad_u), sinf(pitch_rad_u), cosf((float)h_rad_u)*cosf(pitch_rad_u)};
                mat4_rotate(R_u, (float)(oro * M_PI / 180.0f), nose_u);

                mat4_multiply(S_u, R_u, upc.model);
                mat4_multiply(upc.model, T_axes, upc.model);
                
                upc.color[0]=0.0f; upc.color[1]=0.5f; upc.color[2]=1.0f; upc.color[3]=1.0f; upc.usePushColor=1;
                vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(upc), &upc);
                vkCmdDrawIndexed(cb, 18, 1, 0, 0, 0);
            }
        }

        /* Tactical Grid (grd) - Hidden during Jump */
        if (st->shm_show_grid && app->cameraDist < QG_GRID_HIDE_DIST && !app->jumpArrival.active) {
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
                         app->departureWormhole.h, app->departureWormhole.m, 0, pulse, tactScale, 1.0f, app->departureWormhole.jump_type);
        }
        /* Arrival wormhole: shown after jump, fades out over timer */
        if (app->jumpArrival.active && app->jumpArrival.timer > 0) {
            /* Keep arrival wormhole centered on ship if ship is active */
            if (st->object_count > 0 && st->objects[0].active) {
                app->jumpArrival.x = app->smoothObjs[0].x - QG_HALF;
                app->jumpArrival.y = app->smoothObjs[0].z - QG_HALF;
                app->jumpArrival.z = QG_HALF - app->smoothObjs[0].y;
            }
            float closingScale = (app->jumpArrival.timer < 540) ? ((float)app->jumpArrival.timer / 540.0f) : 1.0f;
            drawWormhole(cb, app, app->jumpArrival.x * tactScale, app->jumpArrival.y * tactScale, app->jumpArrival.z * tactScale, 
                         app->jumpArrival.h, app->jumpArrival.m, 1, pulse, tactScale, closingScale, app->jumpArrival.jump_type);
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
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(stpc), &stpc);
        vkCmdBindVertexBuffers(cb, 0, 1, &app->starfieldVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->starfieldIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, (uint32_t)app->starfieldIndexCount, 1, 0, 0, 0);
        vkgt_mark(cb, app->currentFrame, "starfield");

        /* Render individual tactical objects */
        for (int o=0; o<st->object_count && o < MAX_OBJECTS; o++) {
            SharedObject* obj = &st->objects[o]; if (!obj->active) continue;
            /* by kind (2026.11.07.02): the previous object's GPU time goes
             * to its kind (SPACEGL_VK_GPU_TIMES=1), the commands of this one
             * are counted to its own */
            if (g_cpu_cmds.kind != CPUK_SCENE) vkgt_mark_detail(cb, app->currentFrame, g_cpu_kind_names[g_cpu_cmds.kind]);
            g_cpu_cmds.kind = cpu_kind_of(obj->type);
            g_cpu_cmds.objects[g_cpu_cmds.kind]++;

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
            vec3 p = {(ox - QG_HALF) * tactScale, (oz - QG_HALF) * tactScale, (QG_HALF - oy) * tactScale};
            mat4 T, R, S; mat4_translate(T, p); mat4_identity(R);

            if (o == 0) memcpy(app->playerT, T, sizeof(mat4));

            float s = (obj->type==3?SCALE_BASE: (obj->type==4?SCALE_STAR: (obj->type==5?SCALE_PLANET: (obj->type==6?SCALE_BLACKHOLE: (obj->type==29?SCALE_QUASAR: (obj->type==21?SCALE_ASTEROID: (obj->type==50?SCALE_BASE: SCALE_SHIP)))))));
            /* Evolved stars: a star-sized sphere scaled by stage (giants big, white dwarfs tiny) */
            if (obj_type_is_evolved_star(obj->type))
                s = SCALE_STAR * obj_evolved_star_scale(obj->type - OBJ_EVOLVED_FIRST);
            /* Protostellar objects: scaled by evolutionary stage and accretion volume */
            if (obj_type_is_protostellar(obj->type))
                s = SCALE_STAR * obj_protostellar_scale(obj->type - OBJ_PROTOSTELLAR_FIRST);
            /* Moons: a planet-sized sphere scaled by kind (submoons tiny, ocean/atmosphere moons large) */
            if (obj_type_is_moon(obj->type))
                s = SCALE_PLANET * obj_moon_any_scale(obj->type);
            /* Scale the object itself by tactScale */
            s *= tactScale;
            mat4_scale(S, (vec3){s,s,s});
            if (obj->type==1 || obj->type>=10) {
                /* Align model (+X) to North (+Z) */
                mat4_rotate(R, (float)(90.0f * M_PI / 180.0f), (vec3){0,1,0});
                /* Apply Smoothed Heading (around vertical Y) */
                mat4_rotate(R, (float)(-oh * M_PI / 180.0f), (vec3){0,1,0});
                /* Apply Smoothed Pitch around dynamic horizontal axis */
                float obj_h_rad = (float)(oh * M_PI / 180.0f);
                mat4_rotate(R, (float)(om * M_PI / 180.0f), (vec3){cosf(obj_h_rad), 0, -sinf(obj_h_rad)});
                /* Apply Smoothed Roll around local longitudinal axis (+X after North alignment) */
                /* Model orientation: X is forward, Y is up, Z is right. 
                   After North alignment (+X to +Z), the forward axis is +Z in world space.
                   But we rotate around local axes. Pitch is local Z (horizontal). Roll is local X (longitudinal). */
                float pitch_rad = (float)(om * M_PI / 180.0f);
                mat4_rotate(R, (float)(or * M_PI / 180.0f), (vec3){sinf(obj_h_rad)*cosf(pitch_rad), sinf(pitch_rad), cosf(obj_h_rad)*cosf(pitch_rad)});
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
            if (obj->type == OBJ_SUPERNOVA_REMNANT) { drawNebula(cb, app, T, 4, pulse, tactScale); continue; }
            if (obj->type == OBJ_GIANT_MOLECULAR_CLOUD) { drawNebula(cb, app, T, 5, pulse, tactScale); continue; }
            if (obj->type == OBJ_INTERSTELLAR_FILAMENT) { drawInterstellarFilament(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_INTERSTELLAR_BUBBLE) { drawInterstellarBubble_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_BOK_GLOBULE) { drawBokGlobule_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_CLUMP_CORE) { drawClumpCore_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_ACCRETION_DISK) {
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
                VkDeviceSize vb_off = 0;
                vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &vb_off);
                vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vk_draw_sphere(cb);

                /* 2. Accretion Disk and Relativistic Jets */
                drawAccretionDisk_Vulkan(cb, app, T, tactScale, pulse);
                drawRelativisticJet_Vulkan(cb, app, T, tactScale, pulse);
                continue;
            }
            if (obj->type == OBJ_RELATIVISTIC_JET) { drawRelativisticJet_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_SHOCK_WAVE) { drawShockWave_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_STELLAR_BOW_SHOCK) { drawStellarBowShock_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_COSMIC_VOID) { drawCosmicVoid_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_COSMIC_FILAMENT) { drawCosmicFilament_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_EVENT_HORIZON) { drawEventHorizon_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_KILONOVA) { drawKilonova_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_GRAV_LENS) { drawGravLens_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_GRB) { drawGRB_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_GRAV_WAVE) { drawGravWave_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_PROTOPLANETARY_DISK) { drawProtoplanetaryDisk_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_DEBRIS_DISK) { drawDebrisDisk_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_PLANETESIMAL) { drawPlanetesimal_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_ROGUE_PLANET) { drawRoguePlanet_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_BROWN_DWARF) { drawBrownDwarf_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_INTERSTELLAR_OBJECT) { drawISO_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_MAG_RECONNECTION) { drawMagReconn_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_CURRENT_SHEET) { drawCurrentSheet_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_HELIOSPHERE) { drawHeliosphere_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_TERMINATION_SHOCK) { drawTermShock_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_MAGNETOSPHERE) { drawMagnetosphere_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_COSMIC_STRING) { drawCosmicString_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_DOMAIN_WALL) { drawDomainWall_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_DM_HALO) { drawDMHalo_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_IGM) { drawIGM_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_CGM) { drawCGM_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_LYMAN_ALPHA) { drawLymanAlpha_Vulkan(cb, app, T, tactScale, pulse); continue; }
            if (obj->type == OBJ_CMB) { drawCMB_Vulkan(cb, app, T, tactScale, pulse); continue; }
            /* Mapping all categories from telemetry */
            
            

            if (obj->type == OBJ_ION_STORM) {
                drawIonStorm(cb, app, T, tactScale, pulse, 0);
                continue;
            }

            if (obj->type == OBJ_TIME_ANOMALY) {
                drawTimeAnomaly(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_PLASMA_STORM) {
                drawPlasmaStorm(cb, app, T, tactScale, pulse);
                continue;
            }


            if (obj->type == OBJ_DARK_CLOUD) {
                drawDarkMatterCloud(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_SUBSPACE_RUPTURE) {
                drawSubspaceRupture(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_ANCIENT_RELIC) {
                drawAncientRelic(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_DYSON_FRAGMENT) {
                drawDysonFragment(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_SATELLITE) {
                drawSatellite(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_VOID_CRYSTAL) {
                drawVoidCrystal(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_MEGA_STRUCTURE) {
                drawMegaStructure(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_ALIEN_ARTIFACT) {
                drawAlienArtifact(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_SINGULARITY) {
                drawSingularity(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_SUBSPACE_ANOMALY) {
                drawSubspaceAnomaly(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_NEUTRON_STAR) {
                drawNeutronStar(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_RIFT) {
                drawRift(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_PLATFORM) {
                drawPlatform(cb, app, T, obj->faction, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_BUOY) {
                drawCommBuoy(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_MINE) {
                drawMine(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_TRADING_HUB) {
                drawTradingHub(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_CRYSTALLINE_ENTITY || obj->type == OBJ_SPACE_AMOEBA) {
                drawMonster(cb, app, T, obj->type, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_ASTEROID) {
                drawAsteroid(cb, app, T, (float)obj->plating / 100.0f, obj->ship_class, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_COMET) {
                drawComet(cb, app, T, oh, om, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_PULSAR) {
                drawPulsar(cb, app, T, tactScale, pulse, obj->ship_class);
                continue;
            }

            if (obj->type == OBJ_BASE) {
                drawStarbase(cb, app, T, tactScale, pulse, obj->faction);
                continue;
            }

            if (obj->type == OBJ_BLACK_HOLE) {
                /* Draw Black Hole Event Horizon (Black Sphere) */
                vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(opc), &opc);
                VkBuffer vb = app->sphereVertexBuffer;
                VkBuffer ib = app->sphereIndexBuffer;
                vkCmdBindVertexBuffers(cb, 0, 1, &vb, &off);
                vkCmdBindIndexBuffer(cb, ib, 0, VK_INDEX_TYPE_UINT32);
                vk_draw_sphere(cb);
                /* Draw Accretion Disk */
                drawAccretionDisk_Vulkan(cb, app, T, tactScale, pulse);
                continue;
            }

            if (obj->type == OBJ_NEBULA) {
                /* Draw Volumetric Nebula (Hidden during the arrival sequence to eliminate green cloud artifacts) */
                if (!app->jumpArrival.active) {
                    drawNebula(cb, app, T, obj->ship_class, pulse, tactScale);
                }
                continue;
            }

            if (obj->type == OBJ_QUASAR) {
                /* QUASAR: Self-Spinning Sphere + Accretion Disk (No Jets) */
                mat4 R_q; mat4_identity(R_q);
                /* Spin like a top around vertical axis */
                mat4_rotate(R_q, pulse * 3.0f, (vec3){0, 1, 0});
                /* Axial tilt for perspective */
                mat4_rotate(R_q, (float)(20.0f * M_PI / 180.0f), (vec3){1, 0, 0}); 
                
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
                vk_draw_sphere(cb);

                /* 2. Accretion Disk (Flattened sphere, spins with core) */
                mat4 S_disk, M_disk;
                mat4_scale(S_disk, (vec3){3.5f, 0.02f, 3.5f});
                mat4_multiply(M_spin, S_disk, M_disk);
                mat4_multiply(M_disk, T, M_disk);
                PushConstants dpc = qpc; memcpy(dpc.model, M_disk, sizeof(mat4)); dpc.color[3]=0.5f;
                vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(dpc), &dpc);
                vk_draw_sphere(cb);
                continue;
            }

            /* PBR Parameters based on object type */
            if (obj_type_uses_cpu_hull_path(obj->type)) { // Ships
                opc.metallic = 0.9f; opc.roughness = 0.25f;
            } else if (obj->type == OBJ_STAR) { // Star
                opc.metallic = 0.0f; opc.roughness = 1.0f; opc.usePushColor = 6; /* Hyper-Warp for surface plasma */
                /* Spectral classes (ship_class): 0:O 1:B 2:A 3:F 4:G 5:K 6:M -- same palette as the OpenGL viewer */
                const float *star_rgb = obj_star_class_rgb(obj->ship_class);
                opc.color[0]=star_rgb[0]; opc.color[1]=star_rgb[1]; opc.color[2]=star_rgb[2];
            } else if (obj_type_is_evolved_star(obj->type)) { // Evolved star (giant / white dwarf)
                opc.metallic = 0.0f; opc.roughness = 1.0f; opc.usePushColor = 6; /* Hyper-Warp plasma, like a star */
                const float *ev_rgb = obj_evolved_star_rgb(obj->type - OBJ_EVOLVED_FIRST);
                float ev_k = 1.0f;
                if (obj_type_is_pulsating_wd(obj->type)) ev_k = 0.75f + 0.25f * sinf(pulse * 6.0f + (float)obj->id); /* non-radial pulsation */
                opc.color[0]=ev_rgb[0]*ev_k; opc.color[1]=ev_rgb[1]*ev_k; opc.color[2]=ev_rgb[2]*ev_k;
            } else if (obj_type_is_protostellar(obj->type)) { // Protostellar object / YSO
                opc.metallic = 0.0f; opc.roughness = 0.8f; opc.usePushColor = 6; /* Hyper-Warp plasma glow */
                const float *proto_rgb = obj_protostellar_rgb(obj->type - OBJ_PROTOSTELLAR_FIRST);
                opc.color[0]=proto_rgb[0]; opc.color[1]=proto_rgb[1]; opc.color[2]=proto_rgb[2];
            } else if (obj_type_is_moon(obj->type)) { // Natural moon
                opc.metallic = 0.0f; opc.roughness = 0.9f;
                const float *moon_rgb = obj_moon_any_rgb(obj->type);
                opc.color[0]=moon_rgb[0]; opc.color[1]=moon_rgb[1]; opc.color[2]=moon_rgb[2];
            } else if (obj->type == OBJ_PLANET) { // Planet
                opc.metallic = 0.0f; opc.roughness = 0.9f;
                /* Planet classes (ship_class = NPCPlanet.planet_class): 0:Terrestrial 1:Gas Giant 2:Ice 3:Molten 4:Desert */
                const float *planet_rgb = obj_planet_class_rgb(obj->ship_class);
                opc.color[0]=planet_rgb[0]; opc.color[1]=planet_rgb[1]; opc.color[2]=planet_rgb[2];
            } else if (obj->type == OBJ_TORPEDO) { // Torpedo
                opc.metallic = 0.8f; opc.roughness = 0.1f;
            } else {
                opc.metallic = 0.5f; opc.roughness = 0.5f;
            }

            if (o == 0 && app->jumpArrival.timer > 360) {
                float progress = 1.0f - (float)(app->jumpArrival.timer - 360) / 180.0f;
                float glow = 1.0f - progress;
                opc.color[0] = 0.8f + glow * 0.2f; opc.color[1] = 0.8f + glow * 0.2f; opc.color[2] = 1.0f;
            }



            if (obj->faction == 12 && obj_type_uses_cpu_hull_path(obj->type)) {
                drawSwarmCube(cb, app, opc.model, pulse);
                continue;
            }

            /* Draw Standard Object (Ship, Base, Planet, Star, Asteroid, etc.) */
            PushConstants ship_opc = opc;
                if (obj_type_uses_cpu_hull_path(obj->type)) {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
                    float shipScale = 0.55f * tactScale;
                    mat4 S_ship;
                    mat4_scale(S_ship, (vec3){shipScale, shipScale, shipScale});
                    mat4_multiply(S_ship, ship_opc.model, ship_opc.model);
                }
                
                /* Specialized Rendering for new types in Vulkan */
                if (obj->type == OBJ_DYSON_FRAGMENT || obj->type == OBJ_SUBSPACE_RUPTURE || obj->type == OBJ_ION_STORM) { // Dyson, Rupture, Storm (GLOW)
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
                    ship_opc.usePushColor = 6; // Pulse mode
                    if (obj->type == OBJ_DYSON_FRAGMENT) { ship_opc.color[0]=1; ship_opc.color[1]=0.8f; ship_opc.color[2]=0.2f; }
                    if (obj->type == OBJ_SUBSPACE_RUPTURE) { ship_opc.color[0]=0.6f; ship_opc.color[1]=0; ship_opc.color[2]=0.8f; }
                    if (obj->type == OBJ_ION_STORM) { ship_opc.color[0]=0.4f; ship_opc.color[1]=0.4f; ship_opc.color[2]=1; }
                } else if (obj->type == OBJ_ANCIENT_RELIC) { // Ancient Relic (WIREFRAME)
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
                    ship_opc.color[0]=0; ship_opc.color[1]=1; ship_opc.color[2]=0.8f;
                } else if (obj->type >= 40 && obj->type <= 49 && obj->type != OBJ_SINGULARITY && obj->type != OBJ_PLASMA_STORM) {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
                    ship_opc.usePushColor = 1;
                    if (obj->type == OBJ_ALIEN_ARTIFACT) { ship_opc.color[0]=1.0f; ship_opc.color[1]=0.5f; ship_opc.color[2]=0.0f; } // Artifact
                    else if (obj->type == OBJ_WARP_GATE) { ship_opc.color[0]=0.0f; ship_opc.color[1]=0.5f; ship_opc.color[2]=1.0f; } // Warp Gate
                    else if (obj->type == OBJ_NEUTRON_STAR) { ship_opc.color[0]=0.8f; ship_opc.color[1]=0.8f; ship_opc.color[2]=1.0f; } // Neutron Star
                    else if (obj->type == OBJ_MEGA_STRUCTURE) { ship_opc.color[0]=0.5f; ship_opc.color[1]=0.5f; ship_opc.color[2]=0.5f; } // Mega Struct
                    else if (obj->type == OBJ_DARK_CLOUD) { ship_opc.color[0]=0.2f; ship_opc.color[1]=0.0f; ship_opc.color[2]=0.4f; } // Dark Cloud
                    else if (obj->type == OBJ_ORBITAL_RING) { ship_opc.color[0]=0.7f; ship_opc.color[1]=0.7f; ship_opc.color[2]=0.0f; } // Orbital Ring
                    else if (obj->type == OBJ_TIME_ANOMALY) { ship_opc.color[0]=0.0f; ship_opc.color[1]=1.0f; ship_opc.color[2]=0.5f; } // Time Anomaly
                    else if (obj->type == OBJ_VOID_CRYSTAL) { ship_opc.color[0]=0.9f; ship_opc.color[1]=0.0f; ship_opc.color[2]=0.9f; } // Void Crystal
                    else if (obj_type_is_exotic(obj->type)) { 
                        int e_id = obj->type - OBJ_EXOTIC_FIRST;
                        ship_opc.color[0]= (float)(e_id % 3) * 0.5f; 
                        ship_opc.color[1]= (float)(e_id % 4) * 0.33f; 
                        ship_opc.color[2]= (float)(e_id % 5) * 0.25f + 0.2f; 
                        ship_opc.usePushColor = 1 + (e_id % 7); // Use various procedural shaders/FX available
                        ship_opc.metallic = 1.0f; 
                        ship_opc.roughness = 0.0f; 
                    }
                }
                vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ship_opc), &ship_opc);
                VkBuffer vb;
                VkBuffer ib;
                uint32_t cnt;
                VkIndexType it = VK_INDEX_TYPE_UINT32;
                if (obj->type == OBJ_TORPEDO) {
                    vb = app->torpVertexBuffer;
                    ib = app->torpIndexBuffer;
                    cnt = sizeof(torpIndices)/4;
                } else if (obj->type == OBJ_STAR || obj->type == OBJ_PLANET || obj_type_is_exotic(obj->type) || obj_type_is_evolved_star(obj->type) || obj_type_is_moon(obj->type) || obj_type_is_protostellar(obj->type)) {
                    vb = app->sphereVertexBuffer;
                    ib = app->sphereIndexBuffer;
                    cnt = 0;   /* drawn by vk_draw_sphere at its level of detail */
                } else if (obj->type == OBJ_WARP_GATE || obj->type == OBJ_ORBITAL_RING) {
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
                if (vb == app->sphereVertexBuffer) vk_draw_sphere(cb);
                else vkCmdDrawIndexed(cb, cnt, 1, 0, 0, 0);
                if (obj_type_uses_cpu_hull_path(obj->type)) {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
                }

                /* --- QUANTUM CORE (Sphere + 3 Orbiting Rings) --- */
                /* Only for Alliance ships (faction == 0 or faction == 1) */
                int is_alliance = 0;
                if (obj->faction == 0 || obj->faction == 1) is_alliance = 1;

                if (obj_type_uses_cpu_hull_path(obj->type) && is_alliance) {
                    /* Our pyramid is scaled by 0.55f * tactScale and its stern is at local X = -0.7288f.
                     * So in opc.model space, the tail is at -0.7288f * 0.55f * tactScale = -0.40084f * tactScale.
                     * We place the quantum core exactly there */
                    /* 1. Procedural Wireframe Polyhedron Core */
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
                    PushConstants qpc = {0};
                    float coreSize = 0.20f * tactScale; 
                    mat4 S_q; mat4_scale(S_q, (vec3){coreSize, coreSize, coreSize});
                    
                    int ship_cl = obj->ship_class;
                    if (ship_cl < 0 || ship_cl > 13) ship_cl = 13;
                    
                    /* Rotation speed based on class: Legacy (0) rotates faster */
                    mat4 R_q; mat4_identity(R_q);
                    float rotSpeed = 1.0f + ((float)(12 - ship_cl)) * 0.2f; 
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
                    
                    vkCmdBindVertexBuffers(cb, 0, 1, &app->coreVB[ship_cl], &off);
                    vkCmdBindIndexBuffer(cb, app->coreIB[ship_cl], 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(cb, app->coreICount[ship_cl], 1, 0, 0, 0);

                    /* 2. Three Orbiting Rings (Wireframe) */
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
                    vkCmdBindVertexBuffers(cb, 0, 1, &app->circleVertexBuffer, &off);
                    vkCmdBindIndexBuffer(cb, app->circleIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    
                    float speeds[3] = {2.8f, -2.1f, 3.5f};
                    float radii[3] = {0.15f * tactScale, 0.18f * tactScale, 0.21f * tactScale};
                    vec3 axes[3] = {{1,1,0.3f}, {0.3f,1,1}, {1,0.3f,1}};
                    
                    for (int ring_idx=0; ring_idx<3; ring_idx++) {
                        PushConstants rpc = {0};
                        mat4 R_r, S_r;
                        mat4_identity(R_r);
                        mat4_rotate(R_r, pulse * speeds[ring_idx], axes[ring_idx]);
                        mat4_scale(S_r, (vec3){radii[ring_idx], radii[ring_idx], radii[ring_idx]});
                        
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


        /* Torpedoes, after the OpenGL drawTorpedo(): orange core, red additive halo (5 shells), 12 star rays
         * turning in the XY / YZ / XZ planes (quadrant_geom.h holds the shared shape and timing). */
        {
            /* the objects are done: the last one's GPU time to its kind,
             * the rest of the pass is effects (torpedoes, beams, booms,
             * shields) */
            if (g_cpu_cmds.kind != CPUK_SCENE)
                vkgt_mark_detail(cb, app->currentFrame, g_cpu_kind_names[g_cpu_cmds.kind]);
            g_cpu_cmds.kind = CPUK_EFFECTS;
            float wave = qg_torp_wave(pulse);
            float spin = pulse * QG_GL_PULSE_PER_SEC * 250.0f * 0.017453293f;
            float ray_len = QG_TORP_RAY_LEN + wave * 0.3f;
            for (int i=0; i<MAX_ACTIVE_TORPS; i++) {
                if (app->activeTorps[i].active <= 0) continue;
                vec3 tp = { app->activeTorps[i].x * tactScale, app->activeTorps[i].y * tactScale, app->activeTorps[i].z * tactScale };
                mat4 T_t; mat4_translate(T_t, tp);

                /* core: lit orange sphere */
                PushConstants cpc = {0}; mat4 S_c;
                mat4_scale(S_c, (vec3){QG_TORP_CORE * tactScale, QG_TORP_CORE * tactScale, QG_TORP_CORE * tactScale});
                mat4_multiply(S_c, T_t, cpc.model);
                cpc.color[0]=QG_TORP_CORE_R; cpc.color[1]=QG_TORP_CORE_G; cpc.color[2]=QG_TORP_CORE_B; cpc.color[3]=1.0f; cpc.usePushColor=5;
                cpc.metallic = 0.3f; cpc.roughness = 0.4f;
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
                vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(cpc), &cpc);
                vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
                vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vk_draw_sphere(cb);

                /* halo: 5 additive shells, alpha 0.7 / (1.5 i) (OpenGL drawGlow) */
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
                for (int k = 1; k <= 5; k++) {
                    float rs = (QG_TORP_HALO + wave * 0.05f) * (1.0f + (float)k * 0.2f) * tactScale;
                    PushConstants hpc = {0}; mat4 S_h;
                    mat4_scale(S_h, (vec3){rs, rs, rs}); mat4_multiply(S_h, T_t, hpc.model);
                    hpc.color[0]=1.0f; hpc.color[1]=0.2f; hpc.color[2]=0.1f; hpc.color[3]=0.7f / ((float)k * 1.5f); hpc.usePushColor=1;
                    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(hpc), &hpc);
                    vk_draw_sphere(cb);
                }

                /* rays: 4 per plane, the XY set rotated by RotY(90) and RotX(90) for the other planes */
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->wireframePipeline);
                vkCmdBindVertexBuffers(cb, 0, 1, &app->torpRayVertexBuffer, &off);
                for (int p = 0; p < 3; p++) {
                    PushConstants rpc = {0}; mat4 S_r, R_s, R_p, M1, M2;
                    mat4_scale(S_r, (vec3){ray_len * tactScale, ray_len * tactScale, ray_len * tactScale});
                    /* mat4_rotate multiplies the matrix in place: start from the identity */
                    mat4_identity(R_s); mat4_rotate(R_s, spin, (vec3){0, 0, 1});
                    mat4_identity(R_p);
                    if (p == 1) mat4_rotate(R_p, 1.5707963f, (vec3){0, 1, 0});
                    else if (p == 2) mat4_rotate(R_p, 1.5707963f, (vec3){1, 0, 0});
                    /* row-major chain (first applied first): scale, spin in the plane, plane, translate */
                    mat4_multiply(S_r, R_s, M1); mat4_multiply(M1, R_p, M2); mat4_multiply(M2, T_t, rpc.model);
                    rpc.usePushColor = 4;   /* vertex colours: red -> yellow -> dark tip */
                    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(rpc), &rpc);
                    vkCmdDraw(cb, 16, 1, 0, 0);
                }
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
            }
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
                    vk_draw_sphere(cb);
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
                for (int o=0; o < st->object_count && o < MAX_OBJECTS; o++) {
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
                float vsx = (bsx - QG_HALF) * tactScale; float vsy = (bsz - QG_HALF) * tactScale; float vsz = (QG_HALF - bsy) * tactScale;
                float vtx = (btx - QG_HALF) * tactScale; float vty = (btz - QG_HALF) * tactScale; float vtz = (QG_HALF - bty) * tactScale;
                
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
                vk_draw_sphere(cb);
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
                vk_draw_sphere(cb);
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
                
                GameState* st_cur = &app->shm->buffers[app->shm->read_index];

                if (app->shm_inspector_page == 1) {
                    /* Page 1: SHIP VITALS */
                    printf(" [SHIP STATUS: %-15s]  FRAME: %-10lld\n", st_cur->shm_captain_name, (long long)st_cur->frame_id);
                    printf("--------------------------------------------------------------------------------\n");
                    printf(" ENERGY:    %-15lu  | CARGO E:  %-15lu\n", (unsigned long)st_cur->shm_energy, (unsigned long)st_cur->shm_cargo_energy);
                    printf(" HULL:      %-15.2f  | CREW:     %-5d  | PRISON: %-5d\n", st_cur->shm_hull_integrity, st_cur->shm_crew, st_cur->shm_prison_unit);
                    printf(" PLATING:   %-5d             | L-SUPP:   %-15.2f\n", st_cur->shm_composite_plating, st_cur->shm_life_support);
                    printf("--------------------------------------------------------------------------------\n");
                    printf(" SHIELDS:   F: %-5d  RE: %-5d  T: %-5d  B: %-5d  L: %-5d  RI: %-5d\n",
                           st_cur->shm_shields[0], st_cur->shm_shields[1], st_cur->shm_shields[2],
                           st_cur->shm_shields[3], st_cur->shm_shields[4], st_cur->shm_shields[5]);
                    printf(" TARGETS:   F: %-5d  RE: %-5d  T: %-5d  B: %-5d  L: %-5d  RI: %-5d\n",
                           st_cur->shm_target_shields[0], st_cur->shm_target_shields[1], st_cur->shm_target_shields[2],
                           st_cur->shm_target_shields[3], st_cur->shm_target_shields[4], st_cur->shm_target_shields[5]);
                } else if (app->shm_inspector_page == 2) {
                    /* Page 2: TACTICAL & NAVIGATION */
                    printf(" [NAVIGATION & TACTICAL]       POS: [%d,%d,%d] / [%.2f, %.2f, %.2f]\n", 
                           st_cur->shm_q[0], st_cur->shm_q[1], st_cur->shm_q[2], st_cur->shm_s[0], st_cur->shm_s[1], st_cur->shm_s[2]);
                    printf("--------------------------------------------------------------------------------\n");
                    printf(" LOCK TARGET:  %-5d          | ION CHARGE: %-15.2f\n", st_cur->shm_lock_target, st_cur->shm_ion_beam_charge);
                    printf(" TUBE STATE:   %-5d          | CLOAKED:    %-5s  | ALERT: %-5s\n", 
                           st_cur->shm_tube_state, st_cur->is_cloaked ? "YES" : "NO", st_cur->shm_red_alert ? "RED" : "NORMAL");
                    printf("--------------------------------------------------------------------------------\n");
                    printf(" TUBE ETAS:    1:%-5d  2:%-5d  3:%-5d  4:%-5d\n",
                           st_cur->tube_torpedo_etas[0], st_cur->tube_torpedo_etas[1], st_cur->tube_torpedo_etas[2], st_cur->tube_torpedo_etas[3]);
                    printf(" POWER DIST:   ENG: %-5.2f  SHL: %-5.2f  WEP: %-5.2f\n", 
                           st_cur->shm_power_dist[0], st_cur->shm_power_dist[1], st_cur->shm_power_dist[2]);
                } else {
                    /* Page 3: NETWORK & PIPELINE */
                    printf(" [IPC & VULKAN PIPELINE]\n");
                    printf("--------------------------------------------------------------------------------\n");
                    printf(" SHM SEGMENT:  %-15s | READ IDX:  %-5d | WRITE IDX: %-5d\n", 
                           SHM_NAME, atomic_load(&app->shm->read_index), atomic_load(&app->shm->write_index));
                    printf(" OBJECT COUNT: %-5d           | BEAM COUNT: %-5d | TORP COUNT: %-5d\n", 
                           st_cur->object_count, st_cur->beam_count, st_cur->torpedo_count);
                    printf(" CRYPTO:       0x%02X          | FLAGS:      0x%08X\n", 
                           st_cur->shm_crypto_algo, st_cur->shm_encryption_flags);
                    printf("--------------------------------------------------------------------------------\n");
                    printf(" APP CAMERA:   DIST: %-10.2f | ROT: [%.2f, %.2f]\n", 
                           app->cameraDist, app->angleX, app->angleY);
                }
                printf("================================================================================\n");
                fflush(stdout);
            }
        }
    }
    /* without SPACEGL_VK_GPU_TIMES=1 the objects and the effects together,
     * as before; with it, the objects by kind above and the rest here */
    vkgt_mark(cb, app->currentFrame, g_gt.detail ? "effects" : "objects");
    g_cpu_cmds.kind = CPUK_SCENE;
    vkCmdEndRenderPass(cb);
    vkgt_mark(cb, app->currentFrame, "resolve");
    vkEndCommandBuffer(cb);
}

void vulkan_grab_screenshot(VulkanApp *app, uint32_t slot, const char *path) {
    uint32_t w = app->swapChainExtent.width, h = app->swapChainExtent.height;
    uint32_t imgIdx = app->lastImgIdx;
    if (w == 0 || h == 0) {
        fprintf(stderr, "[VK] screenshot: bad swapchain extent\n");
        return;
    }
    if (!app->swapChainCopyable) {
        fprintf(stderr, "[VK] screenshot: the surface does not allow copying its images (no TRANSFER_SRC usage)\n");
        return;
    }
    /* This frame's render (and present) are already submitted: the frame
       functions finish with an empty submit bound to inFlightFences[slot]
       that signals only once the PRESENT is done, so waiting that fence
       (old API, no renamed entry points) guarantees the copy sees the
       finished pixels. The slot's next render is 3 frames later (queue
       order); the fence wait below is the same one the frame loop does
       at the top of the cycle, so re-waiting a signaled fence is normal. */
    vkWaitForFences(app->device, 1, &app->inFlightFences[slot], VK_TRUE, UINT64_MAX);

    VkDeviceSize bytes = (VkDeviceSize)w * (VkDeviceSize)h * 4;
    VkBuffer stg = VK_NULL_HANDLE; VkDeviceMemory stg_mem = VK_NULL_HANDLE;
    void *mapped = NULL;
    VkBufferCreateInfo bc = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                              .size = bytes,
                              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT };
    if (vkCreateBuffer(app->device, &bc, NULL, &stg) != VK_SUCCESS) {
        fprintf(stderr, "[VK] screenshot: staging buffer create failed\n");
        return;
    }
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(app->device, stg, &req);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(app->physicalDevice, &mp);
    uint32_t type_idx = 0; bool mem_found = false;
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((req.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {   /* both, not either */
            type_idx = i; mem_found = true; break;
        }
    }
    VkMemoryAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                .allocationSize = req.size,
                                .memoryTypeIndex = type_idx };
    if (!mem_found ||
        vkAllocateMemory(app->device, &ai, NULL, &stg_mem) != VK_SUCCESS ||
        vkBindBufferMemory(app->device, stg, stg_mem, 0) != VK_SUCCESS) {
        fprintf(stderr, "[VK] screenshot: staging memory failed\n");
        vkDestroyBuffer(app->device, stg, NULL);
        return;
    }

    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                        .commandPool = app->commandPool,
                                        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                        .commandBufferCount = 1 };
    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(app->device, &cai, &cb) != VK_SUCCESS) {
        fprintf(stderr, "[VK] screenshot: command buffer failed\n");
        vkDestroyBuffer(app->device, stg, NULL);
        vkFreeMemory(app->device, stg_mem, NULL);
        return;
    }
    VkCommandBufferBeginInfo cbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cb, &cbi);
    VkBufferImageCopy rc = { .bufferOffset = 0,
                             .bufferRowLength = 0,
                             .bufferImageHeight = 0,
                             .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                             .imageOffset = { 0, 0, 0 },
                             .imageExtent = { w, h, 1 } };
    /* The image was left in PRESENT_SRC_KHR, which a copy cannot read
     * from (VUID-vkCmdCopyImageToBuffer-srcImageLayout-01397): to
     * TRANSFER_SRC_OPTIMAL for the copy, then back for the next present
     * (2026.11.07.01). The contents are kept (old layout given, not
     * UNDEFINED). */
    VkImageMemoryBarrier to_src = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                    .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                    .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                                    .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                    .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .image = app->swapChainImages[imgIdx],
                                    .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                         NULL, 1, &to_src);
    vkCmdCopyImageToBuffer(cb, app->swapChainImages[imgIdx],
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stg, 1, &rc);
    VkImageMemoryBarrier to_present = to_src;
    to_present.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_present.dstAccessMask = 0;
    to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1,
                         &to_present);
    vkEndCommandBuffer(cb);

    VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence = VK_NULL_HANDLE;
    vkCreateFence(app->device, &fci, NULL, &fence);
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .commandBufferCount = 1,
                        .pCommandBuffers = &cb };
    vkQueueSubmit(app->graphicsQueue, 1, &si, fence);
    vkWaitForFences(app->device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkMapMemory(app->device, stg_mem, 0, bytes, 0, &mapped);

    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[VK] screenshot: cannot write %s\n", path);
    } else {
        fprintf(f, "P6\n%u %u\n255\n", w, h);
        const unsigned char *px = (const unsigned char *)mapped;
        unsigned char *row = malloc((size_t)w * 3);
        if (row) {
            for (uint32_t y = 0; y < h; y++) {
                for (uint32_t x = 0; x < w; x++) {
                    /* VK_FORMAT_B8G8R8A8_UNORM: byte order B,G,R,A -> PPM RGB */
                    row[x * 3 + 0] = px[(y * w + x) * 4 + 2];
                    row[x * 3 + 1] = px[(y * w + x) * 4 + 1];
                    row[x * 3 + 2] = px[(y * w + x) * 4 + 0];
                }
                fwrite(row, 1, (size_t)w * 3, f);
            }
            free(row);
        }
        fclose(f);
        printf("[VK] screenshot: %s (%ux%u, slot %u, image %u, %s)\n",
               path, w, h, slot, imgIdx,
               app->gdd ? "GPU-driven path" : "CPU-driven path");
    }

    vkUnmapMemory(app->device, stg_mem);
    vkDestroyFence(app->device, fence, NULL);
    vkFreeCommandBuffers(app->device, app->commandPool, 1, &cb);
    vkDestroyBuffer(app->device, stg, NULL);
    vkFreeMemory(app->device, stg_mem, NULL);
}

void drawFrame(VulkanApp* app) {
    vkWaitForFences(app->device, 1, &app->inFlightFences[app->currentFrame], 1, UINT64_MAX);
    vkgt_collect(app, app->currentFrame);
    uint32_t imgIdx;
    VkResult acq_res = vkAcquireNextImageKHR(app->device, app->swapChain,
                                              100000000ULL, /* 100ms timeout */
                                              app->imageAvailableSemaphores[app->currentFrame],
                                              VK_NULL_HANDLE, &imgIdx);
    if (acq_res == VK_ERROR_OUT_OF_DATE_KHR) {
        /* Window resized under us (the framebuffer-size callback may
           not have fired, or a present raced it): rebuild the
           swapchain and skip this frame; rendering resumes on the
           next one. Without this the frame loop would return here
           forever and the display would freeze. */
        recreateSwapChain(app);
    }
    if (acq_res != VK_SUCCESS && acq_res != VK_SUBOPTIMAL_KHR) {
        /* No work is submitted for this slot: re-signal its fence with
           an empty submit, or the next use of the slot would wait on
           it forever and the whole loop would hang. The fence was just
           waited on (signaled); reset it first (vkQueueSubmit requires
           an unsignaled fence). */
        vkResetFences(app->device, 1, &app->inFlightFences[app->currentFrame]);
        VkSubmitInfo empty = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
        vkQueueSubmit(app->graphicsQueue, 1, &empty, app->inFlightFences[app->currentFrame]);
        return;
    }
    app->lastFrameSlot = app->currentFrame; /* --screenshot bookkeeping */
    app->lastImgIdx = imgIdx;
    vkResetFences(app->device, 1, &app->inFlightFences[app->currentFrame]);
    vkResetCommandBuffer(app->commandBuffers[app->currentFrame], 0); recordCommandBuffer(app->commandBuffers[app->currentFrame], imgIdx, app);
    
    UniformBufferObject ubo; 
    mat4 view; mat4_identity(view);
    
    /* 1. Standard Orbital View (Tactical) */
    mat4 m_std; mat4_identity(m_std);
    mat4_rotate(m_std, (float)(-app->angleY * M_PI / 180.0f), (vec3){0, 1, 0});
    mat4_rotate(m_std, (float)(app->angleX * M_PI / 180.0f), (vec3){1, 0, 0});
    mat4 T_cam; mat4_translate(T_cam, (vec3){0, 0, -qg_view_dist(app->cameraDist, app->mapAnim)});
    mat4_multiply(m_std, T_cam, m_std);

    /* 2. Bridge View (First Person) */
    mat4 m_brg; mat4_identity(m_brg);
    if (app->bridgeAnim > 0.001f) {
        float tactScale = 1.0f - app->mapAnim;
        /* Smooth coordinates of the ship (Object 0) in the Vulkan system (Mapping SHIP: X, Z, -Y) */
        float px = (app->smoothObjs[0].x - QG_HALF) * tactScale;
        float py = (app->smoothObjs[0].z - QG_HALF) * tactScale;
        float pz = (QG_HALF - app->smoothObjs[0].y) * tactScale;
        float ph = app->smoothObjs[0].h;
        float pm = app->smoothObjs[0].m;

        /* Ship Rotation Matrix (Synchronized with recordCommandBuffer) */
        mat4 R_ship; mat4_identity(R_ship);
        /* Model alignment: +X -> +Z (South) */
        mat4_rotate(R_ship, (float)(90.0f * M_PI / 180.0f), (vec3){0, 1, 0});
        /* Heading */
        mat4_rotate(R_ship, (float)(-ph * M_PI / 180.0f), (vec3){0, 1, 0});
        /* Pitch */
        float h_rad = (float)(ph * M_PI / 180.0f);
        mat4_rotate(R_ship, (float)(pm * M_PI / 180.0f), (vec3){cosf(h_rad), 0, -sinf(h_rad)});

        /* Camera Position: exactly on the local Y-axis, slightly above the ship */
        float ly = (app->showBridge >= 11) ? (-BRIDGE_CAMERA_OFFSET_Y * SCALE_SHIP * tactScale) : (BRIDGE_CAMERA_OFFSET_Y * SCALE_SHIP * tactScale);
        /* wx, wy, wz = ShipPos + R_ship * (0, ly, 0) */
        float wx = ly * R_ship[0][1] + px;
        float wy = ly * R_ship[1][1] + py;
        float wz = ly * R_ship[2][1] + pz;

        /* Bridge Camera Orientation: Fixed relative to the ship.
           The ship looks along its local +X axis (Nose). The camera looks along -Z by default. */
        /* To align the camera's Forward with the bow (+X), we use a +90 degree rotation. 
           We multiply R_base by R_ship to apply the ship's world rotations to the already oriented view. */
        mat4 R_cam_world;
        mat4 R_base; mat4_identity(R_base);
        mat4_rotate(R_base, (float)(90.0f * M_PI / 180.0f), (vec3){0, 1, 0});

        /* Rotation based on Mode (Look around) - Relative to the bow */
        int mode = app->showBridge % 10;
        if (mode == 2) mat4_rotate(R_base, (float)(M_PI/2.0f), (vec3){0, 1, 0});       /* LEFT: +90 deg from forward */
        else if (mode == 3) mat4_rotate(R_base, (float)(-M_PI/2.0f), (vec3){0, 1, 0});  /* RIGHT: -90 deg from forward */
        else if (mode == 4) mat4_rotate(R_base, (float)(M_PI/4.0f), (vec3){0, 0, 1});   /* UP: +45 deg around local Z */
        else if (mode == 5) mat4_rotate(R_base, (float)(-M_PI/4.0f), (vec3){0, 0, 1});  /* DOWN: -45 deg around local Z */
        else if (mode == 6) mat4_rotate(R_base, (float)M_PI, (vec3){0, 1, 0});        /* REAR: 180 deg from forward */

        mat4_multiply(R_base, R_ship, R_cam_world);

        /* View Matrix = inv(T_world) * inv(R_cam_world)
           In Row-Major: v * T_inv * R_inv = (v - pos) * R_inv */
        mat4 R_inv; mat4_identity(R_inv);
        for(int i=0; i<3; i++) for(int j=0; j<3; j++) R_inv[i][j] = R_cam_world[j][i];

        mat4 T_inv; mat4_translate(T_inv, (vec3){-wx, -wy, -wz});
        mat4_multiply(T_inv, R_inv, m_brg);
        }
    /* 3. Final View: bridge / pilot interpolation                       */
    /* Pilot mode (showBridge == PILOT_SHOW_BRIDGE_VAL) gets its own     */
    /* cockpit camera that includes ship roll (van_r) which bridge       */
    /* ignores.  Convention is identical to the bridge camera above:     */
    /*   ship position from smoothObjs[0] with Vulkan axis remap         */
    /*   (game X→Vk X, game Y→Vk Z, game Z→Vk -Y)                      */
    /*   rotation via mat4_rotate cascade (model-align +90°Y, heading,  */
    /*   pitch axis, roll around forward axis)                           */
    /*   view inverse = transpose(upper 3×3) then T * R_inv             */
    mat4 m_pilot;
    mat4_identity(m_pilot);
    if (app->showPilot && app->pilotAnim > 0.001f) {
        float tactScale = 1.0f - app->mapAnim;
        /* Ship world position — same remapping as bridge camera */
        float px = (app->smoothObjs[0].x - QG_HALF) * tactScale;
        float py = (app->smoothObjs[0].z - QG_HALF) * tactScale;
        float pz = (QG_HALF - app->smoothObjs[0].y) * tactScale;
        float ph = app->smoothObjs[0].h;
        float pm = app->smoothObjs[0].m;

        /* Fetch current roll from SHM */
        int r_idx_p = atomic_load(&app->shm->read_index);
        GameState *st_p = &app->shm->buffers[r_idx_p];
        float pr = (float)st_p->shm_r;

        /* Build ship orientation: same sequence as bridge, plus roll   */
        mat4 R_ship; mat4_identity(R_ship);
        /* Model alignment: +X → +Z (South) */
        mat4_rotate(R_ship, (float)(90.0f * M_PI / 180.0f), (vec3){0, 1, 0});
        /* Heading */
        mat4_rotate(R_ship, (float)(-ph * M_PI / 180.0f), (vec3){0, 1, 0});
        /* Pitch: same lateral axis as bridge */
        float h_rad = (float)(ph * M_PI / 180.0f);
        mat4_rotate(R_ship, (float)(pm * M_PI / 180.0f), (vec3){cosf(h_rad), 0, -sinf(h_rad)});
        /* Roll: rotate around the ship's local forward axis (+X after  */
        /* the alignment rotation).  Forward in world-space after the   */
        /* heading rotation is (sin(ph), 0, cos(ph)) in Vulkan coords.  */
        mat4_rotate(R_ship, (float)(pr * M_PI / 180.0f),
                    (vec3){sinf(h_rad), 0.0f, cosf(h_rad)});

        /* Camera sits at PILOT_CAMERA_OFFSET_Y above the ship on       */
        /* the ship's local Y-axis: wx = ly*R[0][1]+px, same as bridge  */
        float ly = PILOT_CAMERA_OFFSET_Y * SCALE_SHIP * tactScale;
        float wx = ly * R_ship[0][1] + px;
        float wy = ly * R_ship[1][1] + py;
        float wz = ly * R_ship[2][1] + pz;

        /* Camera orientation: align camera forward with ship's bow     */
        /* (same +90°Y base as bridge, mode == 1 = forward view)        */
        mat4 R_cam_world;
        mat4 R_base; mat4_identity(R_base);
        mat4_rotate(R_base, (float)(90.0f * M_PI / 180.0f), (vec3){0, 1, 0});
        mat4_multiply(R_base, R_ship, R_cam_world);

        /* View = T_inv * R_inv  (bridge exact convention)              */
        mat4 R_inv; mat4_identity(R_inv);
        for(int ii=0; ii<3; ii++) for(int jj=0; jj<3; jj++)
            R_inv[ii][jj] = R_cam_world[jj][ii];
        mat4 T_inv; mat4_translate(T_inv, (vec3){-wx, -wy, -wz});
        mat4_multiply(T_inv, R_inv, m_pilot);
    }


    if (app->showPilot && app->pilotAnim >= 0.999f) {
        memcpy(view, m_pilot, sizeof(mat4));
    } else if (app->showPilot && app->pilotAnim > 0.001f) {
        /* Interpolate: start from current bridge/tactical view */
        mat4 base = {0}; memcpy(base, (app->bridgeAnim >= 0.999f) ? m_brg : m_std, sizeof(mat4));
        for (int i=0; i<4; i++)
            for (int j=0; j<4; j++)
                view[i][j] = base[i][j] * (1.0f - app->pilotAnim) + m_pilot[i][j] * app->pilotAnim;
    } else if (app->bridgeAnim <= 0.001f) {
        memcpy(view, m_std, sizeof(mat4));
    } else if (app->bridgeAnim >= 0.999f) {
        memcpy(view, m_brg, sizeof(mat4));
    } else {
        /* Linear interpolation between matrix components */
        for (int i=0; i<4; i++) {
            for (int j=0; j<4; j++) {
                view[i][j] = m_std[i][j] * (1.0f - app->bridgeAnim) + m_brg[i][j] * app->bridgeAnim;
            }
        }
    }

    memcpy(ubo.view, view, sizeof(mat4));

    /* 4. Dynamic FOV: 45.0 (Tactical) → 65.0 (Bridge) → 78.0 (Pilot) */
    float current_fov = 45.0f * (1.0f - app->bridgeAnim) + 65.0f * app->bridgeAnim;
    if (app->showPilot)
        current_fov = current_fov * (1.0f - app->pilotAnim) + PILOT_FOV_DEG * app->pilotAnim;

    mat4_perspective((float)(current_fov * M_PI / 180.0f), (float)app->swapChainExtent.width / (float)app->swapChainExtent.height, 0.1f, QG_FAR_PLANE, ubo.proj); ubo.proj[1][1] *= -1;
    sphere_lod_set_view(view, ubo.proj[1][1], app->swapChainExtent.height);   /* the next frame's sphere levels */

    void* d; vkMapMemory(app->device, app->uniformBuffersMemory[app->currentFrame], 0, sizeof(ubo), 0, &d); memcpy(d, &ubo, sizeof(ubo)); vkUnmapMemory(app->device, app->uniformBuffersMemory[app->currentFrame]);
    VkPipelineStageFlags w = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 1, &app->imageAvailableSemaphores[app->currentFrame], &w, 1, &app->commandBuffers[app->currentFrame], 1, &app->imageRenderFinished[imgIdx]};
    /* No fence on the render submit: vkQueuePresentKHR takes no fence,
       and the render's fence would signal while the present operation
       (which consumes renderFinishedSemaphores and the swapchain image)
       is still pending, so reusing the slot 3 frames later would
       re-signal the semaphore while the old present still uses it. */
    vkQueueSubmit(app->graphicsQueue, 1, &si, VK_NULL_HANDLE);
    VkSwapchainKHR sw = app->swapChain; VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, NULL, 1, &app->imageRenderFinished[imgIdx], 1, &sw, &imgIdx, NULL};
    VkResult pres_res = vkQueuePresentKHR(app->graphicsQueue, &pi);
    /* Queue ops run in order: this empty submit (and its fence)
       completes only once the PRESENT is done, so the slot can be
       reused 3 frames later without racing the present. */
    {
        VkSubmitInfo empty = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO };
        vkQueueSubmit(app->graphicsQueue, 1, &empty, app->inFlightFences[app->currentFrame]);
    }
    if (pres_res == VK_ERROR_OUT_OF_DATE_KHR) {
        /* Present raced a resize: rebuild now; the next frame acquires
           from the new swapchain. */
        recreateSwapChain(app);
    }
    app->currentFrame = (app->currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    /* The frame cap (SPACEGL_FPS_CAP, default 144) is applied by render_frame(). */

    /* Decrement Shield Hit Timers */
    for (int s = 0; s < 6; s++) if (app->shieldHitTimers[s] > 0) app->shieldHitTimers[s]--;
}


static double bench_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1.0e6;
}

/* Frame cap (2026.11.02.01). It used to be a fixed 6.94 ms sleep AFTER every frame (plus 1 ms in
 * mainLoop), so the frame took work + ~8 ms whatever the work was: a 8 ms frame ran at ~65 fps, not
 * 144. Now the sleep is the time left up to the next deadline, on an absolute clock (no drift), and
 * nothing is slept when the frame already took longer than the period.
 * SPACEGL_FPS_CAP=<n> sets the cap (default 144, 0 = no cap). --bench-frames runs uncapped unless
 * SPACEGL_BENCH_PACE=1. */
static void frame_pace(void) {
    static double cap = -1.0;
    static struct timespec next;
    static bool have_next = false;
    if (cap < 0.0) {
        const char *e = getenv("SPACEGL_FPS_CAP");
        cap = e ? atof(e) : 144.0;
        if (cap < 0.0) cap = 144.0;
    }
    if (cap <= 0.0) return;
    if (s_frame_limit_off && !getenv("SPACEGL_BENCH_PACE")) return;
    long period_ns = (long)(1.0e9 / cap);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!have_next) {
        next = now;
        have_next = true;
    }
    next.tv_nsec += period_ns;
    while (next.tv_nsec >= 1000000000L) {
        next.tv_nsec -= 1000000000L;
        next.tv_sec++;
    }
    double late_ns = (double)(now.tv_sec - next.tv_sec) * 1.0e9 + (double)(now.tv_nsec - next.tv_nsec);
    if (late_ns > 0.0) {
        /* behind schedule: no sleep; after a long stall start again from now (no catch-up burst) */
        if (late_ns > 2.0 * (double)period_ns) next = now;
        return;
    }
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
}

void render_frame(VulkanApp* app, float dt) {
    (void)dt;
    static FrameBench bench;
    static bool bench_on = false;
    if (s_bench_frames > 0 && !bench_on) bench_on = frame_bench_init(&bench, s_bench_frames, s_bench_warmup);
    if (bench_on && bench.seen == bench.warmup) {   /* GPU times and sphere levels: timed frames only */
        vkgt_reset_stats();
        memset(g_sphere_lod.drawn, 0, sizeof(g_sphere_lod.drawn));
        g_sphere_lod.filled = 0;
        int kind = g_cpu_cmds.kind, draws_kind = g_cpu_cmds.draw_marks_kind;
        memset(&g_cpu_cmds, 0, sizeof(g_cpu_cmds));
        g_cpu_cmds.kind = kind;
        g_cpu_cmds.draw_marks_kind = draws_kind;
    }
    double t0 = bench_on ? bench_now_ms() : 0.0;
    app->lastImgIdx = UINT32_MAX;
    if (app->gdd) gdd_draw_frame(app);
    else drawFrame(app);
    if (bench_on) {
        /* the frame's whole cost: wait until the GPU has finished it */
        vkQueueWaitIdle(app->graphicsQueue);
        if (app->lastImgIdx != UINT32_MAX) vkgt_collect(app, app->lastFrameSlot);
        if (frame_bench_add(&bench, bench_now_ms() - t0)) {
            frame_bench_report(&bench, app->gdd ? "vulkan-gdd" : "vulkan-cpu", (int)app->swapChainExtent.width,
                               (int)app->swapChainExtent.height, s_bench_objects);
            vkgt_report(app->gdd ? "vulkan-gdd" : "vulkan-cpu");
            if (!app->gdd) {   /* spheres drawn a frame, by level of detail (vulkan_cpu_lod.h) */
                double nf = (double)(bench.frames > 0 ? bench.frames : 1);
                printf("[SPHERE-LOD] vulkan-cpu: spheres a frame %s: 64x128 %.1f, 32x64 %.1f, 16x32 %.1f, 8x16 %.1f; "
                       "dense wireframes drawn filled %.1f\n",
                       g_sphere_lod.enabled ? "by level" : "(SPACEGL_SPHERE_LOD=0, all full)",
                       (double)g_sphere_lod.drawn[0] / nf, (double)g_sphere_lod.drawn[1] / nf,
                       (double)g_sphere_lod.drawn[2] / nf, (double)g_sphere_lod.drawn[3] / nf,
                       (double)g_sphere_lod.filled / nf);
                /* commands a frame by kind (vulkan_cpu_lod.h): objects, draws,
                 * thousands of indices, pipeline binds (changes), pushes */
                uint64_t td = 0, tb = 0, tc = 0;
                for (int k = 0; k < CPUK_N; k++) {
                    td += g_cpu_cmds.draws[k];
                    tb += g_cpu_cmds.binds[k];
                    tc += g_cpu_cmds.changes[k];
                }
                printf("[CPU-CMDS] vulkan-cpu: a frame: draws %.0f, pipeline binds %.0f (changes %.0f); by kind "
                       "(objects/draws/k-indices/binds/changes):",
                       (double)td / nf, (double)tb / nf, (double)tc / nf);
                for (int k = 0; k < CPUK_N; k++) {
                    if (g_cpu_cmds.draws[k] == 0 && g_cpu_cmds.objects[k] == 0) continue;
                    printf(" %s %.0f/%.0f/%.1f/%.0f/%.0f", g_cpu_kind_names[k], (double)g_cpu_cmds.objects[k] / nf,
                           (double)g_cpu_cmds.draws[k] / nf, (double)g_cpu_cmds.indices[k] / nf / 1000.0,
                           (double)g_cpu_cmds.binds[k] / nf, (double)g_cpu_cmds.changes[k] / nf);
                }
                printf("\n");
                fflush(stdout);
            }
            frame_bench_free(&bench);
            bench_on = false;
            s_bench_frames = 0;
            glfwSetWindowShouldClose(app->window, GLFW_TRUE);
        }
    }

    frame_pace();

    s_frame_no++;
    if (s_shot_path[0] && s_frame_no == s_shot_frame &&
        app->lastImgIdx != UINT32_MAX) {
        vulkan_grab_screenshot(app, app->lastFrameSlot, s_shot_path);
        s_shot_path[0] = '\0';
        glfwSetWindowShouldClose(app->window, GLFW_TRUE);
    }
}
