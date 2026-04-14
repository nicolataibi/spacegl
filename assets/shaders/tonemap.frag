#version 450

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneSampler;
layout(binding = 1) uniform sampler2D bloomSampler;

vec3 ACES_Filmic(vec3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main() {
    /* 1. Combine Scene + Bloom */
    vec3 scene = texture(sceneSampler, inUV).rgb;
    vec3 bloom = texture(bloomSampler, inUV).rgb;
    
    vec3 color = scene + bloom;
    
    /* 2. ACES Tone Mapping */
    color = ACES_Filmic(color);
    
    /* 3. Gamma Correction (Standard 2.2) */
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, 1.0);
}
