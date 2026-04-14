#version 450
/*
 * SpaceGL - GPU Particle Billboard Vertex Shader
 * Generates camera-facing quad vertices from the particle SSBO.
 * No vertex buffer needed: positions come from the storage buffer.
 *
 * View and Projection matrices are uploaded as push constants so the
 * same frame-snapshot matrices used by the scene are reused here.
 */

#define MAX_PARTICLES 65536

struct Particle {
    vec4 pos;   /* xyz=world position, w=remaining life (seconds) */
    vec4 vel;   /* xyz=velocity, w=billboard size */
    vec4 color; /* rgba HDR intensity */
    vec4 meta;  /* x=type, yzw=pad */
};

layout(std430, set = 0, binding = 0) readonly buffer ParticleSSBO {
    Particle particles[];
};

/* 128-byte push constant: view + proj (Row-Major, same convention as UBO) */
layout(push_constant) uniform RPC {
    mat4 view;
    mat4 proj;
} rpc;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUV;

/* Two CCW triangles forming a unit quad in [-1,1] x [-1,1] */
const vec2 QUAD[6] = vec2[](
    vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2(-1.0,  1.0),
    vec2(-1.0,  1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0)
);
const vec2 TEXUV[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0)
);

void main() {
    uint pIdx   = uint(gl_VertexIndex) / 6u;
    uint vLocal = uint(gl_VertexIndex) % 6u;

    if (pIdx >= MAX_PARTICLES) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0); /* beyond far plane */
        fragColor   = vec4(0.0);
        fragUV      = vec2(0.0);
        return;
    }

    Particle p = particles[pIdx];

    /* Dead particle: clip beyond far plane so it produces no fragments */
    if (p.pos.w <= 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        fragColor   = vec4(0.0);
        fragUV      = vec2(0.0);
        return;
    }

    float lifeNorm = clamp(p.pos.w / 2.0, 0.0, 1.0);
    float size     = p.vel.w;

    /*
     * Camera-facing billboard:
     * 1. Transform particle world position to view space.
     * 2. Expand the quad in view-space XY (always faces camera).
     * 3. Project to clip space.
     */
    vec4 viewPos    = rpc.view * vec4(p.pos.xyz, 1.0);
    viewPos.xy     += QUAD[vLocal] * size;
    gl_Position     = rpc.proj * viewPos;

    /* HDR boost: hot core emits above 1.0 so bloom picks it up */
    float hdrBoost = 1.5 + lifeNorm * 4.0;
    fragColor       = vec4(p.color.rgb * hdrBoost, lifeNorm * p.color.a);
    fragUV          = TEXUV[vLocal];
}
