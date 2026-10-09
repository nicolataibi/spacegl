#version 450
/*
 * SPACE GL - GDD scene vertex shader (GPU-driven path).
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * Reads the compute-generated vertices from the storage buffer (there
 * is NO fixed-function vertex input on this pipeline). The MVP is the
 * C row-major product view*proj pushed verbatim (see the convention
 * note in include/spacegl_gdd.h). GddVertex must stay in sync with
 * include/spacegl_gdd.h and assets/shaders/gdd_common.glsl.
 */

struct GddVertex {
    vec4 pos;
    vec4 color;
    vec4 normal;
    vec4 local;
    vec4 params;
};

layout(binding = 0) buffer GddSceneVtx { GddVertex v[]; } gdd_scene_verts;

layout(push_constant) uniform GddScenePC {
    mat4 mvp;
    float time;
    vec3 cam;
    float pad;
} sc;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vPos;
layout(location = 3) out vec3 vLocal;
layout(location = 4) out flat float vMode;
layout(location = 5) out float vMetal;
layout(location = 6) out float vRough;
/* Raw params: (metallic, roughness, ..) — or the per-vertex barycentric
 * coords (x,y,z) for the GDD_FRAG_WIREFRAME(_PBR) modes. */
layout(location = 7) out vec4 vParams;

void main() {
    // Vulkan automatically applies the 'firstVertex' of the indirect
    // command to the global vertex index (gl_VertexIndex). Consequently,
    // by reading directly from gdd_scene_verts.v[gl_VertexIndex], the
    // additive pass will correctly fetch from the offset calculated in
    // gdd_final.comp.

    GddVertex g = gdd_scene_verts.v[gl_VertexIndex];

    gl_Position = sc.mvp * vec4(g.pos.xyz, 1.0);
    vColor = g.color;
    vNormal = g.normal.xyz;
    vPos = g.pos.xyz;
    vLocal = g.local.xyz;
    vMode = g.normal.w;
    vMetal = g.params.x;
    vRough = g.params.y;
    vParams = g.params;
}
