#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D diffuseSampler;

layout(push_constant) uniform PushConstants {
    vec4 params;       /* x: Threshold, y: OffsetX, z: OffsetY, w: Mode (0:Extract, 1:Blur) */
} pc;

void main() {
    if (pc.params.w < 0.5) {
        /* MODE 0: Highlight Extraction */
        vec3 color = texture(diffuseSampler, inUV).rgb;
        float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
        if (brightness > pc.params.x) {
            outColor = vec4(color, 1.0);
        } else {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
        }
    } else {
        /* MODE 1: Gaussian Blur (9-tap) */
        float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
        vec2 texOffset = 1.0 / textureSize(diffuseSampler, 0);
        vec3 result = texture(diffuseSampler, inUV).rgb * weight[0];
        
        vec2 dir = vec2(pc.params.y, pc.params.z);
        for(int i = 1; i < 5; ++i) {
            result += texture(diffuseSampler, inUV + dir * i * texOffset).rgb * weight[i];
            result += texture(diffuseSampler, inUV - dir * i * texOffset).rgb * weight[i];
        }
        outColor = vec4(result, 1.0);
    }
}
