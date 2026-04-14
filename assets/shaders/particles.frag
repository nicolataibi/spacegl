#version 450
/*
 * SpaceGL - GPU Particle Fragment Shader
 * Renders a circular Gaussian glow into the HDR framebuffer.
 * The HDR values above 1.0 are picked up by the Bloom bright-pass
 * extraction stage and contribute to the cinematic glow effect.
 */

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main() {
    /* Map [0,1] UV space to [-1,1] */
    vec2  d     = fragUV * 2.0 - 1.0;
    float dist2 = dot(d, d);

    /* Discard outside unit circle */
    if (dist2 > 1.0) {
        discard;
    }

    /* Gaussian glow: bright core, smooth soft edge */
    float glow = exp(-dist2 * 3.0);

    /* Additive blend: alpha encodes opacity for blending with other passes */
    outColor = vec4(fragColor.rgb * glow, glow * fragColor.a);
}
