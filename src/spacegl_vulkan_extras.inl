void drawBokGlobule_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->alphaPipeline);
    
    /* Bok Globules are dense, dark clouds of dust and gas. 
       We render them as large volumetric clouds that obscure background objects. */
    for (int i=0; i<3; i++) {
        PushConstants pc = {0};
        mat4 S, R, T_offset;
        
        /* Large scale (about the size of a small nebula) */
        float scale = (5.0f + sinf(pulse * 0.2f + i)) * tactScale;
        mat4_scale(S, (vec3){scale, scale * 0.9f, scale * 1.1f});
        
        mat4_identity(R);
        mat4_rotate(R, pulse * 0.02f * (i+1), (vec3){0, 1, 0});
        
        /* Slight random offsets to create an irregular dark mass */
        mat4_translate(T_offset, (vec3){
            sinf(i * 2.0f) * tactScale * 1.5f, 
            cosf(i * 3.0f) * tactScale * 1.5f, 
            sinf(i * 4.0f) * tactScale * 1.5f
        });
        
        mat4_multiply(S, R, pc.model);
        mat4_multiply(pc.model, T_offset, pc.model);
        mat4_multiply(pc.model, baseT, pc.model);
        
        /* Very dark, almost black, with slight dark blue/brown tint, very opaque */
        pc.color[0] = 0.02f; pc.color[1] = 0.02f; pc.color[2] = 0.04f; pc.color[3] = 0.95f;
        pc.usePushColor = 9; /* Volumetric Diffuse Nebula shader works well for dense clouds too */
        pc.time = pulse + i * 5.0f;
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }
    
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawInterstellarBubble_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    mat4 S, M;
    float s = 0.5f + sinf(pulse * 5.0f) * 0.05f;
    mat4_scale(S, (vec3){s * tactScale, s * tactScale, s * tactScale});
    mat4_multiply(S, baseT, M);
    
    PushConstants pc = {0};
    memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.5f; pc.color[1]=0.8f; pc.color[2]=1.0f; pc.color[3]=0.3f + sinf(pulse * 2.0f) * 0.1f;
    pc.usePushColor = 5;
    
    VkDeviceSize off = 0;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawClumpCore_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->alphaPipeline);
    
    /* 1. Outer Envelope (The Clump): Dense, darkish volumetric cloud of gas and dust */
    for (int i=0; i<2; i++) {
        PushConstants pc_env = {0};
        mat4 S_env, R_env, T_env;
        
        float env_scale = (3.5f + sinf(pulse * 0.3f + i)) * tactScale;
        mat4_scale(S_env, (vec3){env_scale, env_scale * 0.9f, env_scale * 1.1f});
        
        mat4_identity(R_env);
        mat4_rotate(R_env, pulse * 0.03f * (i+1), (vec3){0, 1, 0});
        
        mat4_translate(T_env, (vec3){sinf(i)*tactScale, 0, cosf(i)*tactScale});
        
        mat4_multiply(S_env, R_env, pc_env.model);
        mat4_multiply(pc_env.model, T_env, pc_env.model);
        mat4_multiply(pc_env.model, baseT, pc_env.model);
        
        /* Dark brownish-red envelope */
        pc_env.color[0]=0.2f; pc_env.color[1]=0.1f; pc_env.color[2]=0.05f; pc_env.color[3]=0.6f;
        pc_env.usePushColor = 9; /* Volumetric Diffuse Nebula shader */
        pc_env.time = pulse + i * 10.0f;
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc_env), &pc_env);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
        vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }
    
    /* 2. Inner Core: Dense, hot region where a star is forming (Protostar) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    
    PushConstants pc_core = {0};
    mat4 S_core, M_core;
    
    /* Pulsing inner core */
    float core_scale = (0.8f + 0.1f * sinf(pulse * 2.0f)) * tactScale;
    mat4_scale(S_core, (vec3){core_scale, core_scale, core_scale});
    mat4_multiply(S_core, baseT, M_core);
    
    memcpy(pc_core.model, M_core, sizeof(mat4));
    
    /* Intense bright yellow/orange heat glow */
    pc_core.color[0]=1.0f; pc_core.color[1]=0.6f; pc_core.color[2]=0.2f; pc_core.color[3]=1.0f;
    pc_core.usePushColor = 6; /* Hyper-Warp Glow to make it radiant */
    pc_core.time = pulse * 2.0f;
    
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc_core), &pc_core);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}
void drawAccretionDisk_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    /* Use Glow Pipeline (additive) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    
    // Define per-layer properties for cinematic cycling
    float layerSpeeds[3] = {1.2f, 2.0f, 3.5f};
    vec3 layerAxes[3] = {{0.05f, 1.0f, 0.05f}, {-0.05f, 1.0f, 0.0f}, {0.0f, 1.0f, -0.05f}};
    float layerScales[3] = {1.5f, 2.5f, 3.5f};
    vec3 layerColors[3] = {{1.0f, 0.5f, 0.0f}, {1.0f, 0.8f, 0.2f}, {0.6f, 0.2f, 1.0f}};
    float layerAlphas[3] = {0.8f, 0.6f, 0.4f};

    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    
    // Ensure depth testing is disabled via the bound glow pipeline (already configured for additive blending)
    // Apply small upward offset to avoid Z-fighting with the central singularity core
    const float zOffset = 0.01f * tactScale;
    
    for (int i = 0; i < 3; i++) {
        PushConstants dpc = {0};
        mat4 S_disk, R_disk, T_offset;
        mat4_identity(R_disk);
        mat4_rotate(R_disk, pulse * layerSpeeds[i], layerAxes[i]);
        
        float s = layerScales[i] * tactScale;
        // Slightly thicker disk for visibility
        mat4_scale(S_disk, (vec3){s, 0.05f * tactScale, s});
        // Translate upward to avoid Z-fight
        mat4_translate(T_offset, (vec3){0.0f, zOffset, 0.0f});
        // Combine scale, rotation, and offset
        mat4 M_temp;
        mat4_multiply(S_disk, R_disk, M_temp);
        mat4_multiply(M_temp, T_offset, dpc.model);
        mat4_multiply(dpc.model, baseT, dpc.model);
        
        // Compute dynamic color per layer (cinematic cycling)
        float hueMod = 0.5f * (sinf(pulse * 1.3f + i) + 1.0f);
        dpc.color[0] = layerColors[i][0] * hueMod;
        dpc.color[1] = layerColors[i][1] * hueMod;
        dpc.color[2] = layerColors[i][2] * hueMod;
        dpc.color[3] = layerAlphas[i];
        dpc.usePushColor = 8; // Gradient vortex mode for richer visual
        dpc.time = pulse;
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(dpc), &dpc);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }
    // Restore default graphics pipeline after drawing disks
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawRelativisticJet_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse __attribute__((unused))) {
    mat4 S, M;
    mat4_scale(S, (vec3){0.2f * tactScale, 5.0f * tactScale, 0.2f * tactScale});
    mat4_multiply(S, baseT, M);
    
    PushConstants pc = {0};
    memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.3f; pc.color[1]=0.5f; pc.color[2]=1.0f; pc.color[3]=0.6f;
    pc.usePushColor = 5;
    
    VkDeviceSize off = 0;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawShockWave_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    mat4 S, M;
    float s = (2.0f + fmodf(pulse * 2.0f, 5.0f)) * tactScale;
    mat4_scale(S, (vec3){s, s, s});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=1.0f; pc.color[1]=0.5f; pc.color[2]=0.2f; pc.color[3]=0.4f * (1.0f - fmodf(pulse * 2.0f, 5.0f)/5.0f);
    pc.usePushColor = 7; pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawStellarBowShock_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    mat4 S, M;
    float s = (2.0f + fmodf(pulse * 2.0f, 5.0f)) * tactScale;
    mat4_scale(S, (vec3){s, s, s});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.4f; pc.color[1]=0.6f; pc.color[2]=1.0f; pc.color[3]=0.4f * (1.0f - fmodf(pulse * 2.0f, 5.0f)/5.0f);
    pc.usePushColor = 7; pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawCosmicVoid_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse __attribute__((unused))) {
    mat4 S, M;
    mat4_scale(S, (vec3){15.0f * tactScale, 15.0f * tactScale, 15.0f * tactScale});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.05f; pc.color[1]=0.0f; pc.color[2]=0.1f; pc.color[3]=0.2f;
    pc.usePushColor = 1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawCosmicFilament_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    mat4 S, M;
    mat4_scale(S, (vec3){0.5f * tactScale, 20.0f * tactScale, 0.5f * tactScale});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.3f; pc.color[1]=0.2f; pc.color[2]=0.5f; pc.color[3]=0.3f;
    pc.usePushColor = 6; pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawEventHorizon_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    mat4 S, M;
    /* Event Horizon: Pure black core with a subtle violet gravitational lensing rim-glow */
    mat4_scale(S, (vec3){1.5f * tactScale, 1.5f * tactScale, 1.5f * tactScale});
    mat4_multiply(S, baseT, M);
    
    PushConstants pc = {0}; 
    memcpy(pc.model, M, sizeof(mat4));
    
    /* 1. SOLID CORE (Pure Black Singularity) */
    pc.color[0]=0.0f; pc.color[1]=0.0f; pc.color[2]=0.0f; pc.color[3]=1.0f;
    pc.usePushColor = 1; /* Flat mode */
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* 2. GRAVITATIONAL LENSING RIM-GLOW (Violet/Deep Blue) */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    mat4 S_glow;
    /* Slightly larger than core for visibility */
    mat4_scale(S_glow, (vec3){1.55f * tactScale, 1.55f * tactScale, 1.55f * tactScale});
    mat4_multiply(S_glow, baseT, pc.model);
    
    pc.color[0]=0.5f; pc.color[1]=0.0f; pc.color[2]=1.0f; pc.color[3]=0.6f;
    pc.usePushColor = 7; /* Rim glow mode */
    pc.time = pulse;
    
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* Restore graphics pipeline */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawKilonova_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    mat4 S, M;
    float s = (1.5f + 0.5f * sinf(pulse * 10.0f)) * tactScale;
    mat4_scale(S, (vec3){s, s, s});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=1.0f; pc.color[1]=0.8f; pc.color[2]=0.0f; pc.color[3]=1.0f;
    pc.usePushColor = 6; pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawGravLens_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse __attribute__((unused))) {
    mat4 S, M;
    mat4_scale(S, (vec3){2.5f * tactScale, 2.5f * tactScale, 2.5f * tactScale});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=1.0f; pc.color[1]=1.0f; pc.color[2]=1.0f; pc.color[3]=0.3f;
    pc.usePushColor = 1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawGRB_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    mat4 S, M;
    mat4_scale(S, (vec3){0.1f * tactScale, 50.0f * tactScale, 0.1f * tactScale});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.8f; pc.color[1]=0.9f; pc.color[2]=1.0f; pc.color[3]=0.9f;
    pc.usePushColor = 6; pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawGravWave_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    mat4 S, M;
    float s = (3.0f + sinf(pulse * 4.0f)) * tactScale;
    mat4_scale(S, (vec3){s, s, s});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.5f; pc.color[1]=0.5f; pc.color[2]=1.0f; pc.color[3]=0.2f;
    pc.usePushColor = 7; pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
}

void drawProtoplanetaryDisk_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    /* PROTOPLANETARY DISK: Multi-layered dusty disk with additive glow to prevent Z-fighting */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    for (int i = 0; i < 2; i++) {
        mat4 S, M;
        float s = (3.5f + i * 1.2f) * tactScale;
        mat4_scale(S, (vec3){s, 0.06f * tactScale, s});
        mat4_rotate(S, pulse * (0.15f + i * 0.05f), (vec3){0.05f, 1.0f, 0.0f});
        mat4_multiply(S, baseT, M);
        
        PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
        /* Dusty warm colors: Orange/Brown gradient */
        if (i == 0) { pc.color[0]=0.85f; pc.color[1]=0.45f; pc.color[2]=0.1f; pc.color[3]=0.8f; }
        else { pc.color[0]=0.6f; pc.color[1]=0.3f; pc.color[2]=0.05f; pc.color[3]=0.5f; }
        
        pc.usePushColor = 7; /* Rim glow for volumetric feel */
        pc.time = pulse;
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawDebrisDisk_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    /* DEBRIS DISK: Cool, grey rocky disk with additive glow */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    mat4 S, M;
    float s = 5.0f * tactScale;
    mat4_scale(S, (vec3){s, 0.04f * tactScale, s});
    mat4_rotate(S, pulse * 0.08f, (vec3){0, 1, 0});
    mat4_multiply(S, baseT, M);
    
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.5f; pc.color[1]=0.5f; pc.color[2]=0.55f; pc.color[3]=0.6f;
    pc.usePushColor = 7;
    pc.time = pulse;
    
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawPlanetesimal_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    /* PLANETESIMAL: Small rocky core with a subtle atmospheric/dusty glow */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    mat4 S, M;
    /* Core: Small sphere with scale boost */
    mat4_scale(S, (vec3){1.2f * tactScale, 1.2f * tactScale, 1.2f * tactScale});
    mat4_rotate(S, pulse * 0.5f, (vec3){1, 1, 0});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.55f; pc.color[1]=0.5f; pc.color[2]=0.45f; pc.color[3]=1.0f;
    pc.usePushColor = 5; /* PBR */
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* Subtle Glow for visibility */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    mat4_scale(S, (vec3){1.25f * tactScale, 1.25f * tactScale, 1.25f * tactScale});
    mat4_multiply(S, baseT, pc.model);
    pc.color[0]=0.6f; pc.color[1]=0.55f; pc.color[2]=0.4f; pc.color[3]=0.4f;
    pc.usePushColor = 7; pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawRoguePlanet_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    mat4 S, M;
    mat4_scale(S, (vec3){1.5f * tactScale, 1.5f * tactScale, 1.5f * tactScale});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.1f; pc.color[1]=0.2f; pc.color[2]=0.4f; pc.color[3]=1.0f;
    pc.usePushColor = 5;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* Cool blue rim glow for the Rogue Planet */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    mat4_scale(S, (vec3){1.55f * tactScale, 1.55f * tactScale, 1.55f * tactScale});
    mat4_multiply(S, baseT, pc.model);
    pc.color[0]=0.2f; pc.color[1]=0.4f; pc.color[2]=0.8f; pc.color[3]=0.3f;
    pc.usePushColor = 7; pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawBrownDwarf_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    mat4 S, M;
    mat4_scale(S, (vec3){1.8f * tactScale, 1.8f * tactScale, 1.8f * tactScale});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.5f; pc.color[1]=0.2f; pc.color[2]=0.1f; pc.color[3]=1.0f;
    pc.usePushColor = 6; /* Pulsing for a failed star */
    pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* Smoldering red glow */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    mat4_scale(S, (vec3){1.85f * tactScale, 1.85f * tactScale, 1.85f * tactScale});
    mat4_multiply(S, baseT, pc.model);
    pc.color[0]=0.6f; pc.color[1]=0.1f; pc.color[2]=0.0f; pc.color[3]=0.4f;
    pc.usePushColor = 7;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawISO_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    /* INTERSTELLAR OBJECT (ISO): Elongated self-illuminated body */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
    mat4 S, M;
    /* Scale and rotate using a unified matrix S to ensure proper initialization */
    mat4_scale(S, (vec3){1.2f * tactScale, 3.6f * tactScale, 1.2f * tactScale});
    mat4_rotate(S, pulse * 1.5f, (vec3){0, 1, 1});
    mat4_multiply(S, baseT, M);
    
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.7f; pc.color[1]=0.7f; pc.color[2]=0.8f; pc.color[3]=1.0f;
    pc.usePushColor = 6; /* Self-illuminated pulsing core */
    pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);

    /* Trail/Glow */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    mat4_scale(S, (vec3){1.3f * tactScale, 3.8f * tactScale, 1.3f * tactScale});
    mat4_rotate(S, pulse * 1.5f, (vec3){0, 1, 1});
    mat4_multiply(S, baseT, pc.model);
    pc.color[0]=0.5f; pc.color[1]=0.5f; pc.color[2]=0.8f; pc.color[3]=0.4f;
    pc.usePushColor = 7; pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawMagReconn_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    mat4 S, M;
    float s = (0.5f + 0.5f * sinf(pulse * 20.0f)) * tactScale;
    mat4_scale(S, (vec3){s, s, s});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=1.0f; pc.color[1]=1.0f; pc.color[2]=1.0f; pc.color[3]=1.0f;
    pc.usePushColor = 6; pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawCurrentSheet_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    mat4 S, M;
    mat4_scale(S, (vec3){10.0f * tactScale, 0.01f * tactScale, 10.0f * tactScale});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.0f; pc.color[1]=0.5f; pc.color[2]=1.0f; pc.color[3]=0.4f;
    pc.usePushColor = 6; pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawHeliosphere_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    /* HELIOSPHERE (Type 79): Large protective bubble around a star system */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    /* Double-layered volumetric effect */
    for (int i = 0; i < 2; i++) {
        mat4 S, M;
        float s = (20.0f + i * 2.0f) * tactScale;
        mat4_scale(S, (vec3){s, s, s});
        mat4_multiply(S, baseT, M);
        
        PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
        /* Deep blue with cyan highlights */
        pc.color[0]=0.1f; pc.color[1]=0.4f; pc.color[2]=0.8f; 
        pc.color[3]=(i == 0) ? 0.25f : 0.15f;
        
        pc.usePushColor = 7; /* Rim glow for boundary effect */
        pc.time = pulse;
        /* Use metallic as a slight offset for the pulse effect in shader 7 */
        pc.metallic = (float)i * 0.5f; 
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }
    
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawTermShock_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    mat4 S, M;
    mat4_scale(S, (vec3){15.0f * tactScale, 15.0f * tactScale, 15.0f * tactScale});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.5f; pc.color[1]=0.7f; pc.color[2]=1.0f; pc.color[3]=0.2f;
    pc.usePushColor = 7; pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawMagnetosphere_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse __attribute__((unused))) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    mat4 S, M;
    mat4_scale(S, (vec3){4.0f * tactScale, 6.0f * tactScale, 4.0f * tactScale});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.3f; pc.color[1]=0.5f; pc.color[2]=1.0f; pc.color[3]=0.15f;
    pc.usePushColor = 1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawCosmicString_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    mat4 S, M;
    mat4_scale(S, (vec3){0.01f * tactScale, 100.0f * tactScale, 0.01f * tactScale});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=1.0f; pc.color[1]=1.0f; pc.color[2]=1.0f; pc.color[3]=1.0f;
    pc.usePushColor = 6; pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawDomainWall_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse __attribute__((unused))) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    mat4 S, M;
    mat4_scale(S, (vec3){100.0f * tactScale, 100.0f * tactScale, 0.01f * tactScale});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.5f; pc.color[1]=0.0f; pc.color[2]=0.5f; pc.color[3]=0.2f;
    pc.usePushColor = 1;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawDMHalo_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    /* DARK MATTER HALO (Type 84): Enormous, subtle gravitational well */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    /* Use volumetric shader 9 for a "ghostly" lumpy appearance */
    for (int i = 0; i < 2; i++) {
        mat4 S, M;
        float s = (30.0f + i * 5.0f) * tactScale;
        mat4_scale(S, (vec3){s, s, s});
        mat4_multiply(S, baseT, M);
        
        PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
        /* Deep violet/dark indigo */
        pc.color[0]=0.15f; pc.color[1]=0.05f; pc.color[2]=0.3f; 
        pc.color[3]=(i == 0) ? 0.3f : 0.2f;
        
        pc.usePushColor = 9; /* Volumetric Nebula effect */
        pc.time = pulse;
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }
    
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawIGM_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    mat4 S, M;
    mat4_scale(S, (vec3){10.0f * tactScale, 10.0f * tactScale, 10.0f * tactScale});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.4f; pc.color[1]=0.4f; pc.color[2]=0.6f; pc.color[3]=0.1f;
    pc.usePushColor = 6; pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawCGM_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    mat4 S, M;
    mat4_scale(S, (vec3){8.0f * tactScale, 8.0f * tactScale, 8.0f * tactScale});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=0.5f; pc.color[1]=0.3f; pc.color[2]=0.7f; pc.color[3]=0.15f;
    pc.usePushColor = 6; pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawLymanAlpha_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    mat4 S, M;
    mat4_scale(S, (vec3){12.0f * tactScale, 12.0f * tactScale, 12.0f * tactScale});
    mat4_multiply(S, baseT, M);
    PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
    pc.color[0]=1.0f; pc.color[1]=0.2f; pc.color[2]=0.2f; pc.color[3]=0.2f;
    pc.usePushColor = 6; pc.time = pulse;
    vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    VkDeviceSize off = 0; vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}

void drawCMB_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse) {
    /* CMB (Type 88): Restoration with Planck-style anisotropies and safe visibility */
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->glowPipeline);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &app->sphereVertexBuffer, &off);
    vkCmdBindIndexBuffer(cb, app->sphereIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    for (int i = 0; i < 2; i++) {
        mat4 S, M;
        /* Safe scale to stay within Far Plane (1000.0f) */
        float s = (15.0f + i * 3.0f) * tactScale;
        mat4_scale(S, (vec3){s, s, s});
        mat4_multiply(S, baseT, M);
        
        PushConstants pc = {0}; memcpy(pc.model, M, sizeof(mat4));
        /* Planck map colors: Dark Red (warm) and Deep Blue (cool) */
        if (i == 0) { pc.color[0]=0.6f; pc.color[1]=0.1f; pc.color[2]=0.05f; }
        else        { pc.color[0]=0.05f; pc.color[1]=0.2f; pc.color[2]=0.7f; }
        
        pc.color[3]=0.45f; /* High visibility alpha */
        pc.usePushColor = 9; 
        pc.time = pulse * 0.4f;
        
        vkCmdPushConstants(cb, app->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDrawIndexed(cb, SPHERE_LATS * SPHERE_LONGS * 6, 1, 0, 0, 0);
    }
    
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, app->graphicsPipeline);
}
