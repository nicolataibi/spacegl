#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 3) in mat4 instanceModel;
layout(location = 7) in vec4 instanceColor;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;
    float time;
    int usePushColor;
    float metallic;
    float roughness;
    int useInstancing;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragPos;
layout(location = 3) out vec4 pushColor;
layout(location = 4) out flat int usePushColor;
layout(location = 5) out float metallic;
layout(location = 6) out float roughness;
layout(location = 7) out float time;

void main() {
    vec3 pos = inPosition;

    /* MODE 6: Hyper-Warp Displacement (Twisting / Pulsing) */
    if (pc.usePushColor == 6) {
        float angle = pc.time * 5.0 + pos.z * 10.0;
        float s = sin(angle);
        float c = cos(angle);
        float x = pos.x * c - pos.y * s;
        float y = pos.x * s + pos.y * c;
        pos.x = x * (1.0 + 0.2 * sin(pc.time * 8.0));
        pos.y = y * (1.0 + 0.2 * cos(pc.time * 8.0));
        pos.z += 0.1 * sin(pc.time * 12.0 + pos.x * 5.0);
    }

    /* MODE 7: Shockwave Ripple (Expansion + Sine Wave) */
    if (pc.usePushColor == 7) {
        float dist = length(pos);
        pos += normalize(pos) * 0.3 * sin(dist * 15.0 - pc.time * 20.0);
    }

    /* MODE 8: Gravitational Vortex (Swirling / Lensing simulation) */
    if (pc.usePushColor == 8) {
        float r = length(pos.xz);
        float swirl = 5.0 / (r + 0.1); /* Stronger twist near the center */
        float angle = pc.time * 10.0 + swirl;
        float s = sin(angle);
        float c = cos(angle);
        float x = pos.x * c - pos.z * s;
        float z = pos.x * s + pos.z * c;
        pos.x = x;
        pos.z = z;
        /* Slight vertical oscillation */
        pos.y += 0.05 * sin(pc.time * 15.0 + r * 20.0);
    }

    mat4 modelMatrix = (pc.useInstancing != 0) ? instanceModel : pc.model;
    vec4 finalPushColor = (pc.useInstancing != 0) ? instanceColor : pc.color;

    vec4 worldPos = modelMatrix * vec4(pos, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;
    
    if (pc.usePushColor == 3) {
        gl_PointSize = 4.0 * (1.0 + sin(pc.time * 2.0 + fragPos.x));
    } else {
        gl_PointSize = 1.0;
    }

    fragPos = worldPos.xyz;
    fragNormal = mat3(transpose(inverse(modelMatrix))) * inNormal;
    fragColor = inColor;
    pushColor = finalPushColor;
    usePushColor = pc.usePushColor;
    metallic = pc.metallic;
    roughness = pc.roughness;
    time = pc.time;
}
