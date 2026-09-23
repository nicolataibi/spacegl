#version 450
/*
 * SPACE GL - GDD scene fragment shader (GPU-driven path).
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * Re-implements the procedural color modes of the CPU-driven path
 * (assets/shaders/shader.frag) on top of the GPU-generated vertices:
 * the per-vertex mode (GDD_FRAG_*) plays the role of usePushColor,
 * vColor plays the role of pushColor, and the push constant carries
 * time/camera. The mode numbers are aligned with the CPU path so the
 * two architectures render the same semantics.
 */

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vPos;
layout(location = 3) in vec3 vLocal;
layout(location = 4) in flat float vMode;
layout(location = 5) in float vMetal;
layout(location = 6) in float vRough;
/* Raw params: (metallic, roughness, ..) — or the per-vertex barycentric
 * coords (x,y,z) for the GDD_FRAG_WIREFRAME(_PBR) modes. */
layout(location = 7) in vec4 vParams;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform GddScenePC {
    mat4 mvp;
    float time;
    vec3 cam;
    float pad;
} sc;

/* --- Procedural Noise Utilities (ported from shader.frag) --- */

float hash3(vec3 p) {
    p = fract(p * vec3(127.1, 311.7, 74.7));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

float smoothNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    float n000 = hash3(i + vec3(0,0,0));
    float n100 = hash3(i + vec3(1,0,0));
    float n010 = hash3(i + vec3(0,1,0));
    float n110 = hash3(i + vec3(1,1,0));
    float n001 = hash3(i + vec3(0,0,1));
    float n101 = hash3(i + vec3(1,0,1));
    float n011 = hash3(i + vec3(0,1,1));
    float n111 = hash3(i + vec3(1,1,1));
    return mix(
        mix(mix(n000,n100,u.x), mix(n010,n110,u.x), u.y),
        mix(mix(n001,n101,u.x), mix(n011,n111,u.x), u.y),
        u.z
    );
}

float fbm(vec3 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 4; i++) {
        v += a * smoothNoise(p);
        p  = p * 2.1 + vec3(1.7, 9.2, 2.8);
        a *= 0.5;
    }
    return v;
}

/* Barycentric wireframe edge mask (vParams.xyz = per-vertex barycentric
 * coords for the GDD_FRAG_WIREFRAME(_PBR) modes): 1 on a triangle edge,
 * 0 inside the face. Per-sample evaluation + the caller's discard gives
 * MSAA-antialiased lines, the triangle counterpart of the CPU path's
 * fixed-function 1-px LINE_LIST wireframe. */
float gdd_wire_mask(vec3 b) {
    vec3 d = fwidth(b) + 1e-6;
    vec3 a = smoothstep(vec3(0.0), d * 1.25, b);
    return 1.0 - min(min(a.x, a.y), a.z);
}

void main() {
    int mode = int(vMode + 0.5);

    /* MODE 1: Unlit constant color (wireframe/HUD look) */
    if (mode == 1) {
        outColor = vColor;
        return;
    }

    /* MODE 2: Starfield Twinkle (color * dynamic sine) */
    if (mode == 2) {
        float seed = vPos.x * 12.9898 + vPos.y * 78.233 + vPos.z * 45.164;
        float twinkle = 0.6 + 0.4 * sin(sc.time * 3.0 + seed);
        outColor = vec4(vColor.rgb * twinkle, 1.0);
        return;
    }

    /* MODE 4: Unlit vertex color (compass axes) */
    if (mode == 4) {
        outColor = vec4(vColor.rgb, 1.0);
        return;
    }

    /* MODE 6: Hyper-Warp Glow (Pulsing) */
    if (mode == 6) {
        float glow = 0.5 + 0.5 * sin(sc.time * 15.0);
        vec3 finalGlow = vColor.rgb * (1.5 + glow);
        outColor = vec4(finalGlow, vColor.a);
        return;
    }

    /* MODE 7: Shockwave Pulse (Expanding Wave Color) */
    if (mode == 7) {
        float wave = sin(length(vPos * 0.1) - vMetal);
        vec3 waveColor = mix(vColor.rgb, vec3(1.0), max(0.0, wave * 0.5));
        outColor = vec4(waveColor, vColor.a);
        return;
    }

    /* MODE 8: Black Hole Accretion (White -> Yellow -> Red Gradient) */
    if (mode == 8) {
        float t = (length(vLocal) > 1e-6)
              ? clamp(length(normalize(vLocal).xz), 0.0, 1.0) : 0.0;
        vec3 color;
        if (t < 0.3) color = mix(vec3(1.0, 1.0, 1.0), vec3(1.0, 1.0, 0.0), t / 0.3);
        else if (t < 0.7) color = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.2, 0.0), (t - 0.3) / 0.4);
        else color = mix(vec3(1.0, 0.2, 0.0), vec3(0.3, 0.0, 0.0), (t - 0.7) / 0.3);
        float alpha = 0.6 + 0.4 * sin(sc.time * 10.0 + vLocal.x * 5.0);
        outColor = vec4(color, alpha * vColor.a);
        return;
    }

    /* MODE 9: Volumetric Diffuse Nebula (FBM cloud) */
    if (mode == 9) {
        float t_slow = sc.time * 0.003;
        float cos_t  = cos(t_slow);
        float sin_t  = sin(t_slow);
        vec3 np = vPos * 0.18;
        np = vec3(cos_t * np.x - sin_t * np.z, np.y, sin_t * np.x + cos_t * np.z);
        float density = fbm(np + vec3(0.0, sc.time * 0.002, 0.0));
        float detail  = fbm(np * 2.3 + vec3(5.1, 1.7, 3.4) + vec3(0.0, sc.time * 0.004, 0.0));
        float cloud = density * 0.65 + detail * 0.35;
        cloud = pow(cloud, 1.4);
        float radial = clamp(1.0 - length(vNormal) * 0.5, 0.0, 1.0);
        radial = pow(radial, 0.6);
        float alpha = cloud * radial * vColor.a;
        alpha = clamp(alpha, 0.0, 0.92);
        vec3 baseHue    = vColor.rgb;
        vec3 emissionHue = vec3(
            min(baseHue.r + 0.35, 1.0),
            max(baseHue.g - 0.15, 0.0),
            max(baseHue.b - 0.25, 0.0)
        );
        vec3 finalColor = mix(baseHue, emissionHue, cloud * 0.55);
        float rimFactor = 1.0 - abs(dot(length(vNormal) > 1e-6 ? normalize(vNormal) : vec3(0,1,0), vec3(0.0, 0.0, 1.0)));
        rimFactor = pow(rimFactor, 2.0) * 0.4;
        finalColor += baseHue * rimFactor;
        finalColor = mix(finalColor, vec3(1.0), clamp(cloud - 0.7, 0.0, 1.0) * 0.5);
        outColor = vec4(finalColor, alpha);
        return;
    }

    /* MODE 10: Interstellar Filament (Electrical Discharges) */
    if (mode == 10) {
        vec3 p = vPos * 0.5;
        float n = fbm(p + vec3(sc.time * 2.0, 0.0, sc.time * 1.5));
        float sparkX = sin(p.y * 5.0 + n * 12.0 + sc.time * 18.0);
        float sparkY = cos(p.x * 4.0 + n * 15.0 - sc.time * 25.0);
        float sparkZ = sin(p.z * 6.0 + n * 10.0 + sc.time * 20.0);
        float spark = abs(sparkX * sparkY * sparkZ);
        spark = pow(spark, 16.0) * 15.0;
        float plasma = pow(n, 2.5) * 1.2;
        vec3 baseHue  = vColor.rgb;
        vec3 sparkHue = vec3(0.9, 0.3, 1.0);
        vec3 coreHue  = vec3(0.4, 0.8, 1.0);
        vec3 finalColor = baseHue * plasma + sparkHue * spark + coreHue * (plasma * 0.5);
        float alpha = (plasma * 0.7 + spark) * vColor.a;
        alpha = clamp(alpha, 0.0, 1.0);
        outColor = vec4(finalColor, alpha);
        return;
    }

    /* MODE 11: Barycentric wireframe, unlit (CPU mode 1 on the
     * wireframe pipeline: constant push color, only the lines rasterize). */
    if (mode == 11) {
        float line = gdd_wire_mask(vParams.xyz);
        if (line < 0.5) discard; /* inside the face: not rasterized, like LINE_LIST */
        outColor = vec4(vColor.rgb, vColor.a);
        return;
    }

    /* MODE 12: Barycentric wireframe, PBR (CPU ships: wireframe pipeline
     * + usePushColor=5 with the ship's metal 0.9 / roughness 0.25). */
    if (mode == 12) {
        float line = gdd_wire_mask(vParams.xyz);
        if (line < 0.5) discard;
        vec3 N = (length(vNormal) > 1e-6) ? normalize(vNormal) : vec3(0, 1, 0);
        vec3 L = normalize(vec3(50.0, 100.0, 50.0) - vPos);
        vec3 V = normalize(sc.cam - vPos);
        vec3 H = normalize(L + V);
        float diff = max(dot(N, L), 0.0);
        vec3 specular = vec3(pow(max(dot(N, H), 0.0), 32.0 * (1.0 - 0.25))) * 0.9;
        outColor = vec4(0.25 * vColor.rgb + diff * vColor.rgb + specular, vColor.a);
        return;
    }

    /* DEFAULT & MODE 5: PBR Lighting (same model as the CPU path) */
    vec3 baseColor = vColor.rgb;
    vec3 N = (length(vNormal) > 1e-6) ? normalize(vNormal) : vec3(0, 1, 0);
    vec3 L = normalize(vec3(50.0, 100.0, 50.0) - vPos);
    vec3 V = normalize(sc.cam - vPos);
    vec3 H = normalize(L + V);
    float diff = max(dot(N, L), 0.0);
    vec3 specular = vec3(pow(max(dot(N, H), 0.0), 32.0 * (1.0 - vRough))) * vMetal;
    outColor = vec4(0.25 * baseColor + diff * baseColor + specular, vColor.a);
}
