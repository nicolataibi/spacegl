#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec4 pushColor;
layout(location = 4) in flat int usePushColor;
layout(location = 5) in float metallic;
layout(location = 6) in float roughness;
layout(location = 7) in float time;

layout(location = 0) out vec4 outColor;

void main() {
    /* MODE 1: HUD/Wireframe Unlit (Constant Push Color) */
    if (usePushColor == 1) {
        outColor = pushColor;
        return;
    }

    /* MODE 2: Starfield Twinkle (Vertex Color + Dynamic Sine) */
    if (usePushColor == 2) {
        float seed = fragPos.x * 12.9898 + fragPos.y * 78.233 + fragPos.z * 45.164;
        float twinkle = 0.6 + 0.4 * sin(time * 3.0 + seed);
        outColor = vec4(fragColor * twinkle, 1.0);
        return;
    }

    /* MODE 3: Point Sprite Bloom (Procedural intensity) */
    if (usePushColor == 3) {
        float dist = length(gl_PointCoord - vec2(0.5));
        if (dist > 0.5) discard;
        float intensity = exp(-dist * 6.0) * (1.0 - dist * 2.0);
        outColor = vec4(pushColor.rgb, intensity * pushColor.a);
        return;
    }

    /* MODE 4: Unlit Vertex Color (Used for AR Compass Axes) */
    if (usePushColor == 4) {
        outColor = vec4(fragColor, 1.0);
        return;
    }

    /* MODE 6: Hyper-Warp Glow (Chromatic Overload) */
    if (usePushColor == 6) {
        float glow = 0.5 + 0.5 * sin(time * 15.0);
        vec3 finalGlow = pushColor.rgb * (1.5 + glow);
        outColor = vec4(finalGlow, pushColor.a);
        return;
    }

    /* MODE 7: Shockwave Pulse (Expanding Wave Color) */
    if (usePushColor == 7) {
        /* Use 'metallic' as a local pulse reference to stabilize the effect */
        float pulse = metallic; 
        float wave = sin(length(fragPos * 0.1) - pulse);
        vec3 waveColor = mix(pushColor.rgb, vec3(1.0, 1.0, 1.0), max(0.0, wave * 0.5));
        outColor = vec4(waveColor, pushColor.a);
        return;
    }

    /* MODE 8: Black Hole Accretion (White -> Yellow -> Red Gradient) */
    if (usePushColor == 8) {
        /* Re-calculate distance in local space would be better, but we can use pushColor.a as a scale hint */
        /* For now, we use a procedural noise/flicker based on fragPos */
        float dist = length(fragPos - pushColor.gba); /* Approximation: use some components for center */
        /* Actually, let's use a simpler distance-based mix if we pass the center in push constants */
        /* But since we want professional, let's use the 'metallic' field to pass local radius if needed */
        /* For this implementation, we'll use procedural rings */
        float r = mod(length(fragPos.xz) * 5.0 - time * 5.0, 1.0);
        
        vec3 color;
        float t = clamp(length(fragNormal.xy), 0.0, 1.0); /* Reuse normal for variation */
        
        /* Gradient: White (inner) -> Yellow (mid) -> Red (outer) */
        if (t < 0.3) color = mix(vec3(1.0, 1.0, 1.0), vec3(1.0, 1.0, 0.0), t / 0.3);
        else if (t < 0.7) color = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.2, 0.0), (t - 0.3) / 0.4);
        else color = mix(vec3(1.0, 0.2, 0.0), vec3(0.3, 0.0, 0.0), (t - 0.7) / 0.3);

        float alpha = 0.6 + 0.4 * sin(time * 10.0 + fragPos.x * 5.0);
        outColor = vec4(color, alpha * pushColor.a);
        return;
    }

    /* DEFAULT & MODE 5: PBR Lighting for Solid Objects */
    vec3 baseColor;
    float alpha = 1.0;

    if (usePushColor == 5 || usePushColor == 1) {
        baseColor = pushColor.rgb;
        alpha = pushColor.a;
    } else {
        baseColor = fragColor;
    }

    vec3 N = normalize(fragNormal);
    /* Simple directional light from above-front */
    vec3 L = normalize(vec3(50.0, 100.0, 50.0) - fragPos);
    vec3 V = normalize(vec3(0.0, 0.0, 60.0) - fragPos);
    vec3 H = normalize(L + V);

    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * baseColor;

    float spec = pow(max(dot(N, H), 0.0), 32.0 * (1.0 - roughness));
    vec3 specular = vec3(spec) * metallic;

    vec3 ambient = 0.25 * baseColor;
    outColor = vec4(ambient + diffuse + specular, alpha);
}
