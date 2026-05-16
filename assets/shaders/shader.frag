#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;
layout(location = 3) in vec4 pushColor;
layout(location = 4) in flat int usePushColor;
layout(location = 5) in float metallic;
layout(location = 6) in float roughness;
layout(location = 7) in float time;
layout(location = 8) in vec3 localPos;

layout(location = 0) out vec4 outColor;

/* --- Procedural Noise Utilities --- */

/* Value hash: returns pseudo-random float in [0,1] from a vec3 seed */
float hash3(vec3 p) {
    p = fract(p * vec3(127.1, 311.7, 74.7));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

/* Smooth value noise: trilinear interpolation over a 3D grid */
float smoothNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f); /* Hermite smoothstep */
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

/* Fractal Brownian Motion: 4-octave layered noise */
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
        float r = mod(length(localPos.xz) * 5.0 - time * 5.0, 1.0);
        
        vec3 color;
        float t = clamp(length(normalize(localPos).xz), 0.0, 1.0); /* Use normalized local pos for radial gradient */
        
        /* Gradient: White (inner) -> Yellow (mid) -> Red (outer) */
        if (t < 0.3) color = mix(vec3(1.0, 1.0, 1.0), vec3(1.0, 1.0, 0.0), t / 0.3);
        else if (t < 0.7) color = mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.2, 0.0), (t - 0.3) / 0.4);
        else color = mix(vec3(1.0, 0.2, 0.0), vec3(0.3, 0.0, 0.0), (t - 0.7) / 0.3);

        float alpha = 0.6 + 0.4 * sin(time * 10.0 + localPos.x * 5.0);
        outColor = vec4(color, alpha * pushColor.a);
        return;
    }

    /* ------------------------------------------------------------------ */
    /* MODE 9: Volumetric Diffuse Nebula                                   */
    /*   - FBM noise gives lumpy, irregular cloud structure                 */
    /*   - Radial gradient from center: bright core, fading translucent rim */
    /*   - Dual-hue shift: base color bleeds towards warm emission tones    */
    /*   - Slow rotation encoded in the noise coordinates via time          */
    /* ------------------------------------------------------------------ */
    if (usePushColor == 9) {
        /* Slow drift: rotate the noise sample space over time */
        float t_slow = time * 0.003;
        float cos_t  = cos(t_slow);
        float sin_t  = sin(t_slow);
        vec3 np = fragPos * 0.18;
        np = vec3(
            cos_t * np.x - sin_t * np.z,
            np.y,
            sin_t * np.x + cos_t * np.z
        );

        /* Primary FBM cloud density */
        float density = fbm(np + vec3(0.0, time * 0.002, 0.0));

        /* Secondary FBM layer for filament detail, at higher frequency */
        float detail  = fbm(np * 2.3 + vec3(5.1, 1.7, 3.4) + vec3(0.0, time * 0.004, 0.0));

        /* Combine: base density + sharpened filaments */
        float cloud = density * 0.65 + detail * 0.35;
        cloud = pow(cloud, 1.4); /* Darken low-density areas */

        /* Radial fade: sphere surface is ~at |fragPos| = objectRadius;
           fragNormal approximates the outward direction on the sphere */
        float radial = clamp(1.0 - length(fragNormal) * 0.5, 0.0, 1.0);
        radial = pow(radial, 0.6); /* Softer falloff */

        /* Combined alpha: high where cloud is dense AND near center */
        float alpha = cloud * radial * pushColor.a;
        alpha = clamp(alpha, 0.0, 0.92);

        /* Colour: base hue from pushColor, shift toward a warmer emission hue
           in dense areas (simulates ionized hydrogen pink/orange emission) */
        vec3 baseHue    = pushColor.rgb;
        vec3 emissionHue = vec3(
            min(baseHue.r + 0.35, 1.0),
            max(baseHue.g - 0.15, 0.0),
            max(baseHue.b - 0.25, 0.0)
        );
        vec3 finalColor = mix(baseHue, emissionHue, cloud * 0.55);

        /* Rim brightness: edges of the cloud glow more strongly */
        float rimFactor = 1.0 - abs(dot(normalize(fragNormal), vec3(0.0, 0.0, 1.0)));
        rimFactor = pow(rimFactor, 2.0) * 0.4;
        finalColor += pushColor.rgb * rimFactor;

        /* Bright core: very dense regions bleach towards white */
        finalColor = mix(finalColor, vec3(1.0), clamp(cloud - 0.7, 0.0, 1.0) * 0.5);

        outColor = vec4(finalColor, alpha);
        return;
    }

    /* ------------------------------------------------------------------ */
    /* MODE 10: Interstellar Filament (Electrical Discharges)             */
    /*   - Twisting plasma core with high-frequency electric sparks       */
    /*   - Sharp branching noise for lightning effect                     */
    /* ------------------------------------------------------------------ */
    if (usePushColor == 10) {
        vec3 p = fragPos * 0.5;
        float n = fbm(p + vec3(time * 2.0, 0.0, time * 1.5));
        
        /* Create lightning-like branching using intersecting sine waves perturbed by noise */
        float sparkX = sin(p.y * 5.0 + n * 12.0 + time * 18.0);
        float sparkY = cos(p.x * 4.0 + n * 15.0 - time * 25.0);
        float sparkZ = sin(p.z * 6.0 + n * 10.0 + time * 20.0);
        
        float spark = abs(sparkX * sparkY * sparkZ);
        spark = pow(spark, 16.0) * 15.0; /* Make it sharp and bright */
        
        /* Base plasma filament */
        float plasma = pow(n, 2.5) * 1.2;
        
        vec3 baseHue = pushColor.rgb;
        vec3 sparkHue = vec3(0.9, 0.3, 1.0); /* Electric purple/pink sparks */
        vec3 coreHue = vec3(0.4, 0.8, 1.0); /* Cyan plasma */
        
        vec3 finalColor = baseHue * plasma + sparkHue * spark + coreHue * (plasma * 0.5);
        
        /* Alpha based on intensity */
        float alpha = (plasma * 0.7 + spark) * pushColor.a;
        alpha = clamp(alpha, 0.0, 1.0);
        
        outColor = vec4(finalColor, alpha);
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
