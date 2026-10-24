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
#include <time.h>

char s_shot_path[512] = {0};
int  s_shot_frame     = 30;
int  s_rng_seed       = -1;
bool s_no_rotate      = false;
int  s_frame_no       = 0;

void recordCommandBuffer(VkCommandBuffer cb, uint32_t idx, VulkanApp* app) {
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,NULL,0,NULL}; vkBeginCommandBuffer(cb, &bi);
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
                         app->departureWormhole.h, app->departureWormhole.m, 0, pulse, tactScale, 1.0f, app->departureWormhole.jump_type);
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
        vkCmdDrawIndexed(cb, app->starfieldIndexCount, 1, 0, 0, 0);

        /* Render individual tactical objects */
        for (int o=0; o<st->object_count && o < MAX_OBJECTS; o++) {
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
            /* Evolved stars: a star-sized sphere scaled by stage (giants big, white dwarfs tiny) */
            if (obj_type_is_evolved_star(obj->type))
                s = SCALE_STAR * obj_evolved_star_scale(obj->type - OBJ_EVOLVED_FIRST);
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
                VkDeviceSize off = 0;
                vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
                vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

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
                VkBuffer vb = app->sphereVertexBuffer; VkBuffer ib = app->sphereIndexBuffer; uint32_t cnt = SPHERE_LATS * SPHERE_LONGS * 6;
                vkCmdBindVertexBuffers(cb, 0, 1, &vb, &off); vkCmdBindIndexBuffer(cb, ib, 0, VK_INDEX_TYPE_UINT32); vkCmdDrawIndexed(cb, cnt, 1, 0, 0, 0);
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
                        ship_opc.color[0]= (e_id % 3) * 0.5f; 
                        ship_opc.color[1]= (e_id % 4) * 0.33f; 
                        ship_opc.color[2]= (e_id % 5) * 0.25f + 0.2f; 
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
                } else if (obj->type == OBJ_STAR || obj->type == OBJ_PLANET || obj_type_is_exotic(obj->type) || obj_type_is_evolved_star(obj->type)) {
                    vb = app->sphereVertexBuffer;
                    ib = app->sphereIndexBuffer;
                    cnt = SPHERE_LATS * SPHERE_LONGS * 6;
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
                vkCmdDrawIndexed(cb, cnt, 1, 0, 0, 0);
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
                    
                    int cl = obj->ship_class;
                    if (cl < 0 || cl > 13) cl = 13;
                    
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

void vulkan_grab_screenshot(VulkanApp *app, uint32_t slot, const char *path) {
    uint32_t w = app->swapChainExtent.width, h = app->swapChainExtent.height;
    uint32_t imgIdx = app->lastImgIdx;
    if (w == 0 || h == 0) {
        fprintf(stderr, "[VK] screenshot: bad swapchain extent\n");
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
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
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
    vkCmdCopyImageToBuffer(cb, app->swapChainImages[imgIdx],
                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, stg, 1, &rc);
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
    mat4_rotate(m_std, -app->angleY * M_PI / 180.0f, (vec3){0, 1, 0});
    mat4_rotate(m_std, app->angleX * M_PI / 180.0f, (vec3){1, 0, 0});
    mat4 T_cam; mat4_translate(T_cam, (vec3){0, 0, -app->cameraDist});
    mat4_multiply(m_std, T_cam, m_std);

    /* 2. Bridge View (First Person) */
    mat4 m_brg; mat4_identity(m_brg);
    if (app->bridgeAnim > 0.001f) {
        float tactScale = 1.0f - app->mapAnim;
        /* Smooth coordinates of the ship (Object 0) in the Vulkan system (Mapping SHIP: X, Z, -Y) */
        float px = (app->smoothObjs[0].x - 20.0f) * tactScale;
        float py = (app->smoothObjs[0].z - 20.0f) * tactScale;
        float pz = (20.0f - app->smoothObjs[0].y) * tactScale;
        float ph = app->smoothObjs[0].h;
        float pm = app->smoothObjs[0].m;

        /* Ship Rotation Matrix (Synchronized with recordCommandBuffer) */
        mat4 R_ship; mat4_identity(R_ship);
        /* Model alignment: +X -> +Z (South) */
        mat4_rotate(R_ship, 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});
        /* Heading */
        mat4_rotate(R_ship, -ph * M_PI / 180.0f, (vec3){0, 1, 0});
        /* Pitch */
        float h_rad = ph * M_PI / 180.0f;
        mat4_rotate(R_ship, pm * M_PI / 180.0f, (vec3){cosf(h_rad), 0, -sinf(h_rad)});

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
        mat4_rotate(R_base, 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});

        /* Rotation based on Mode (Look around) - Relative to the bow */
        int mode = app->showBridge % 10;
        if (mode == 2) mat4_rotate(R_base, M_PI/2.0f, (vec3){0, 1, 0});       /* LEFT: +90 deg from forward */
        else if (mode == 3) mat4_rotate(R_base, -M_PI/2.0f, (vec3){0, 1, 0});  /* RIGHT: -90 deg from forward */
        else if (mode == 4) mat4_rotate(R_base, M_PI/4.0f, (vec3){0, 0, 1});   /* UP: +45 deg around local Z */
        else if (mode == 5) mat4_rotate(R_base, -M_PI/4.0f, (vec3){0, 0, 1});  /* DOWN: -45 deg around local Z */
        else if (mode == 6) mat4_rotate(R_base, M_PI, (vec3){0, 1, 0});        /* REAR: 180 deg from forward */

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
        float px = (app->smoothObjs[0].x - 20.0f) * tactScale;
        float py = (app->smoothObjs[0].z - 20.0f) * tactScale;
        float pz = (20.0f - app->smoothObjs[0].y) * tactScale;
        float ph = app->smoothObjs[0].h;
        float pm = app->smoothObjs[0].m;

        /* Fetch current roll from SHM */
        int r_idx_p = atomic_load(&app->shm->read_index);
        GameState *st_p = &app->shm->buffers[r_idx_p];
        float pr = (float)st_p->shm_r;

        /* Build ship orientation: same sequence as bridge, plus roll   */
        mat4 R_ship; mat4_identity(R_ship);
        /* Model alignment: +X → +Z (South) */
        mat4_rotate(R_ship, 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});
        /* Heading */
        mat4_rotate(R_ship, -ph * M_PI / 180.0f, (vec3){0, 1, 0});
        /* Pitch: same lateral axis as bridge */
        float h_rad = ph * M_PI / 180.0f;
        mat4_rotate(R_ship, pm * M_PI / 180.0f, (vec3){cosf(h_rad), 0, -sinf(h_rad)});
        /* Roll: rotate around the ship's local forward axis (+X after  */
        /* the alignment rotation).  Forward in world-space after the   */
        /* heading rotation is (sin(ph), 0, cos(ph)) in Vulkan coords.  */
        mat4_rotate(R_ship, pr * M_PI / 180.0f,
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
        mat4_rotate(R_base, 90.0f * M_PI / 180.0f, (vec3){0, 1, 0});
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

    mat4_perspective(current_fov * M_PI / 180.0f, (float)app->swapChainExtent.width / (float)app->swapChainExtent.height, 0.1f, 1000.0f, ubo.proj); ubo.proj[1][1] *= -1;
    
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

    /* Frame limiter: cap at ~144fps to avoid saturating the CPU on MAILBOX/IMMEDIATE present */
    struct timespec frame_limit = {0, 6944444L}; /* ~144fps = 1s/144 ≈ 6.94ms */
    nanosleep(&frame_limit, NULL);

    /* Decrement Shield Hit Timers */
    for (int s = 0; s < 6; s++) if (app->shieldHitTimers[s] > 0) app->shieldHitTimers[s]--;
}


void render_frame(VulkanApp* app, float dt) {
    (void)dt;
    app->lastImgIdx = UINT32_MAX;
    if (app->gdd) gdd_draw_frame(app);
    else drawFrame(app);

    s_frame_no++;
    if (s_shot_path[0] && s_frame_no == s_shot_frame &&
        app->lastImgIdx != UINT32_MAX) {
        vulkan_grab_screenshot(app, app->lastFrameSlot, s_shot_path);
        s_shot_path[0] = '\0';
        glfwSetWindowShouldClose(app->window, GLFW_TRUE);
    }
}
