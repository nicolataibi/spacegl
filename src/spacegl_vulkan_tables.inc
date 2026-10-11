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
