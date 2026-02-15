/*
 * SPACE GL - 3D LOGIC ENGINE
 * Copyright (C) 2026 Nicola Taibi
 * License: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#define _DEFAULT_SOURCE
#define GL_GLEXT_PROTOTYPES
#include <GL/glew.h>
#include <GL/freeglut.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <signal.h>
#include <locale.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include "shared_state.h"

#define IS_Q_VALID(q1,q2,q3) ((q1)>=1 && (q1)<=10 && (q2)>=1 && (q2)<=10 && (q3)>=1 && (q3)<=10)

#include "network.h"

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float r, g, b, a;
    float size;
    float life;
    int active;
} FXParticle;

void spawnParticle(float x, float y, float z, float vx, float vy, float vz, float r, float g, float b, float size, float life);

/* VBO Globals */
GLuint vbo_stars = 0;
GLuint vbo_grid = 0;
int grid_vertex_count = 0;

/* Bloom FBO Globals */
GLuint fbo_scene = 0, tex_scene = 0, rbo_depth = 0;
GLuint fbo_msaa = 0, rbo_color_msaa = 0, rbo_depth_msaa = 0;
GLuint fbo_pingpong[2] = {0, 0}, tex_pingpong[2] = {0, 0};
GLuint blurShaderProgram = 0, finalShaderProgram = 0;
GLuint quadVAO = 0, quadVBO = 0;

#define MAX_PARTICLES 2000
FXParticle fx_particles[MAX_PARTICLES];
GLuint particleShaderProgram = 0;

/* Shader Globals */
GLuint skyboxShaderProgram = 0;
GLuint hullShaderProgram = 0;
GLuint starShaderProgram = 0;
GLuint bhShaderProgram = 0;
GLuint whShaderProgram = 0;
GLuint cloakShaderProgram = 0;

GLuint compileShader(const char* source, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        fprintf(stderr, "Shader Compilation Error: %s\n", infoLog);
    }
    return shader;
}

GLuint linkProgram(GLuint vert, GLuint frag) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);
    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        fprintf(stderr, "Shader Linking Error: %s\n", infoLog);
    }
    return program;
}

void initShaders() {
    /* SKYBOX: Procedural Nebula Shader */
    const char* skyboxVert = "#version 120\n"
        "varying vec3 vPos;\n"
        "void main() {\n"
        "    vPos = gl_Vertex.xyz;\n"
        "    gl_Position = gl_ModelViewProjectionMatrix * vec4(gl_Vertex.xyz, 1.0);\n"
        "}";

    const char* skyboxFrag = "#version 120\n"
        "varying vec3 vPos;\n"
        "uniform float time;\n"
        "/* Pseudo-random noise function */\n"
        "float hash(float n) { return fract(sin(n) * 43758.5453123); }\n"
        "float noise(vec3 x) {\n"
        "    vec3 p = floor(x);\n"
        "    vec3 f = fract(x);\n"
        "    f = f*f*(3.0-2.0*f);\n"
        "    float n = p.x + p.y*57.0 + 113.0*p.z;\n"
        "    return mix(mix(mix(hash(n+0.0), hash(n+1.0),f.x),\n"
        "                   mix(hash(n+57.0), hash(n+58.0),f.x),f.y),\n"
        "               mix(mix(hash(n+113.0), hash(n+114.0),f.x),\n"
        "                   mix(hash(n+170.0), hash(n+171.0),f.x),f.y),f.z);\n"
        "}\n"
        "void main() {\n"
        "    vec3 dir = normalize(vPos);\n"
        "    float n = noise(dir * 2.0 + time * 0.01);\n"
        "    n += 0.5 * noise(dir * 4.0 - time * 0.02);\n"
        "    /* Color palette: Deep purple and blue */\n"
        "    vec3 col1 = vec3(0.05, 0.0, 0.15);\n"
        "    vec3 col2 = vec3(0.0, 0.05, 0.1);\n"
        "    vec3 finalCol = mix(col1, col2, n);\n"
        "    /* Add some brighter gas patches */\n"
        "    finalCol += vec3(0.1, 0.0, 0.2) * pow(n, 4.0);\n"
        "    gl_FragColor = vec4(finalCol, 1.0);\n"
        "}";

    skyboxShaderProgram = linkProgram(compileShader(skyboxVert, GL_VERTEX_SHADER), compileShader(skyboxFrag, GL_FRAGMENT_SHADER));

    /* HULL: Procedural Plating with Triplanar Mapping */
    const char* hullVert = "#version 120\n"
        "varying vec3 vPos;\n"
        "varying vec3 vNorm;\n"
        "void main() {\n"
        "    vPos = gl_Vertex.xyz;\n"
        "    vNorm = gl_NormalMatrix * gl_Normal;\n"
        "    gl_Position = gl_ModelViewProjectionMatrix * vec4(gl_Vertex.xyz, 1.0);\n"
        "    gl_FrontColor = gl_Color;\n"
        "}";

    const char* hullFrag = "#version 120\n"
        "varying vec3 vPos;\n"
        "varying vec3 vNorm;\n"
        "uniform vec3 lightPos;\n"
        "uniform float hitPulse;\n"
        "float hash(float n) { return fract(sin(n) * 43758.5453123); }\n"
        "float noise(vec3 x) {\n"
        "    vec3 p = floor(x);\n"
        "    vec3 f = fract(x);\n"
        "    f = f*f*(3.0-2.0*f);\n"
        "    float n = p.x + p.y*57.0 + 113.0*p.z;\n"
        "    return mix(mix(mix(hash(n+0.0), hash(n+1.0),f.x),\n"
        "                   mix(hash(n+57.0), hash(n+58.0),f.x),f.y),\n"
        "               mix(mix(hash(n+113.0), hash(n+114.0),f.x),\n"
        "                   mix(hash(n+170.0), hash(n+171.0),f.x),f.y),f.z);\n"
        "}\n"
        "void main() {\n"
        "    vec3 normal = normalize(vNorm);\n"
        "    vec3 lightDir = normalize(lightPos - vPos);\n"
        "    /* Triplanar blending weights */\n"
        "    vec3 blending = abs(normal);\n"
        "    blending /= (blending.x + blending.y + blending.z);\n"
        "    \n"
        "    /* Generate hull panels */\n"
        "    float scale = 15.0;\n"
        "    float hx = noise(vec3(vPos.yz * scale, 0.0));\n"
        "    float hy = noise(vec3(vPos.xz * scale, 1.0));\n"
        "    float hz = noise(vec3(vPos.xy * scale, 2.0));\n"
        "    float hull = hx * blending.x + hy * blending.y + hz * blending.z;\n"
        "    hull = step(0.4, hull) * 0.2 + 0.8;\n"
        "    \n"
        "    /* Dynamic Lighting */\n"
        "    float diff = max(dot(normal, lightDir), 0.1);\n"
        "    vec3 baseCol = gl_Color.rgb * hull * diff;\n"
        "    \n"
        "    /* Apply Red Hit Pulse */\n"
        "    baseCol = mix(baseCol, vec3(1.0, 0.0, 0.0), hitPulse * 0.85);\n"
        "    \n"
        "    /* Specular highlight */\n"
        "    vec3 viewDir = normalize(-vPos);\n"
        "    vec3 reflectDir = reflect(-lightDir, normal);\n"
        "    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0);\n"
        "    \n"
        "    gl_FragColor = vec4(baseCol + vec3(0.4) * spec, 1.0);\n"
        "}";
    hullShaderProgram = linkProgram(compileShader(hullVert, GL_VERTEX_SHADER), compileShader(hullFrag, GL_FRAGMENT_SHADER));

    /* PARTICLES: Glowing Sprites */
    const char* partVert = "#version 120\n"
        "void main() {\n"
        "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
        "    gl_FrontColor = gl_Color;\n"
        "    /* Sharp fragments: significantly smaller base size */\n"
        "    gl_PointSize = 40.0 * (1.0 / length(gl_ModelViewMatrix * gl_Vertex));\n"
        "    if (gl_PointSize < 0.5) gl_PointSize = 0.5;\n"
        "}";
    const char* partFrag = "#version 120\n"
        "void main() {\n"
        "    float d = length(gl_PointCoord - vec2(0.5));\n"
        "    if (d > 0.5) discard;\n"
        "    /* Solid particle with no glow for debris look */\n"
        "    gl_FragColor = gl_Color;\n"
        "}";
    particleShaderProgram = linkProgram(compileShader(partVert, GL_VERTEX_SHADER), compileShader(partFrag, GL_FRAGMENT_SHADER));

    const char* starVert = "#version 120\n"
        "void main() { gl_Position = ftransform(); gl_FrontColor = gl_Color; }";
    const char* starFrag = "#version 120\n"
        "uniform float time;\n"
        "void main() { float p = (sin(time * 3.0) + 1.0) * 0.5;\n"
        "gl_FragColor = vec4(gl_Color.rgb, gl_Color.a * (0.6 + p * 0.4)); }";

    GLuint sv = compileShader(starVert, GL_VERTEX_SHADER);
    GLuint sf = compileShader(starFrag, GL_FRAGMENT_SHADER);
    starShaderProgram = linkProgram(sv, sf);

    const char* bhFrag = "#version 120\n"
        "uniform float time;\n"
        "uniform sampler2D sceneTex;\n"
        "varying vec2 vTexCoord;\n"
        "varying vec4 vScreenPos;\n"
        "void main() {\n"
        "    vec2 rel = vTexCoord - vec2(0.5);\n"
        "    float d = length(rel) * 2.0;\n"
        "    \n"
        "    /* DISCARD outside the effect area to eliminate the square edges */\n"
        "    if (d > 1.0) discard;\n"
        "    \n"
        "    vec2 uv = (vScreenPos.xy / vScreenPos.w) * 0.5 + 0.5;\n"
        "    \n"
        "    /* 1. GRAVITATIONAL LENSING */\n"
        "    float lens_strength = 0.08;\n"
        "    float dist_inv = 1.0 / (d + 0.01);\n"
        "    vec2 distortedUV = uv + normalize(rel) * dist_inv * lens_strength * 0.02;\n"
        "    \n"
        "    /* 2. EVENT HORIZON */\n"
        "    if (d < 0.25) {\n"
        "        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
        "        return;\n"
        "    }\n"
        "    \n"
        "    /* 3. ACCRETION DISK Logic */\n"
        "    vec3 sceneCol = texture2D(sceneTex, (d > 0.8) ? uv : distortedUV).rgb;\n"
        "    \n"
        "    float ripple = sin(d * 30.0 - time * 10.0) * 0.5 + 0.5;\n"
        "    float disk_mask = smoothstep(0.8, 0.3, d);\n"
        "    vec3 diskCol = vec3(1.0, 0.4, 0.0) * ripple + vec3(1.0, 0.8, 0.2) * pow(ripple, 4.0);\n"
        "    \n"
        "    /* Final composition */\n"
        "    vec3 finalCol = mix(sceneCol, diskCol, disk_mask * 0.8);\n"
        "    \n"
        "    /* Bright Plasma Ring */\n"
        "    if (d < 0.28) finalCol += vec3(1.0, 0.9, 0.6) * (1.0 - (d-0.25)*33.0);\n"
        "    \n"
        "    gl_FragColor = vec4(finalCol, 1.0);\n"
        "}";
    
    const char* bhVert = "#version 120\n"
        "varying vec2 vTexCoord;\n"
        "varying vec4 vScreenPos;\n"
        "varying vec3 pos;\n"
        "void main() {\n"
        "    vTexCoord = gl_MultiTexCoord0.xy;\n"
        "    vScreenPos = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
        "    pos = gl_Vertex.xyz;\n"
        "    gl_Position = vScreenPos;\n"
        "}";
    bhShaderProgram = linkProgram(compileShader(bhVert, GL_VERTEX_SHADER), compileShader(bhFrag, GL_FRAGMENT_SHADER));

    /* Wormhole Shader (Cyan/Blue Variant) */
    const char* whFrag = "#version 120\n"
        "uniform float time;\n"
        "varying vec3 pos;\n"
        "void main() {\n"
        "  float d = length(pos);\n"
        "  float ripple = sin(d * 30.0 - time * 20.0) * 0.5 + 0.5;\n"
        "  vec3 col = vec3(0.0, 0.8, 1.0) * (1.0 - d) + vec3(0.0, 0.1, 0.3);\n"
        "  gl_FragColor = vec4(col * ripple + vec3(0.8, 0.9, 1.0) * pow(ripple, 4.0), 0.8);\n"
        "}";
    whShaderProgram = linkProgram(compileShader(bhVert, GL_VERTEX_SHADER), compileShader(whFrag, GL_FRAGMENT_SHADER));
    
    /* Cloak Shader (Blue Pulsing) */
    const char* cloakVert = "#version 120\n"
        "varying vec3 pos;\n"
        "varying vec3 norm;\n"
        "void main() {\n"
        "  pos = gl_Vertex.xyz;\n"
        "  norm = gl_NormalMatrix * gl_Normal;\n"
        "  gl_Position = ftransform();\n"
        "}";
    const char* cloakFrag = "#version 120\n"
        "uniform float time;\n"
        "varying vec3 pos;\n"
        "varying vec3 norm;\n"
        "void main() {\n"
        "  float pulse = (sin(time * 2.0) + 1.0) * 0.5;\n"
        "  float edge = 1.0 - max(dot(normalize(norm), vec3(0,0,1)), 0.0);\n"
        "  vec3 col = vec3(0.1, 0.4, 1.0) * (0.5 + pulse * 0.5) + vec3(0.8, 0.9, 1.0) * pow(edge, 3.0);\n"
        "  gl_FragColor = vec4(col, 0.4 + pulse * 0.2);\n"
        "}";
    cloakShaderProgram = linkProgram(compileShader(cloakVert, GL_VERTEX_SHADER), compileShader(cloakFrag, GL_FRAGMENT_SHADER));
    
    /* BLOOM: Simple Quad Vertex Shader */
    const char* quadVert = "#version 120\n"
        "attribute vec2 position;\n"
        "varying vec2 TexCoords;\n"
        "void main() {\n"
        "    gl_Position = vec4(position.x, position.y, 0.0, 1.0);\n"
        "    TexCoords = (position + 1.0) / 2.0;\n"
        "}";

    /* BLOOM: Blur Fragment Shader (Two-Pass Gaussian) */
    const char* blurFrag = "#version 120\n"
        "uniform sampler2D image;\n"
        "uniform bool horizontal;\n"
        "varying vec2 TexCoords;\n"
        "void main() {\n"
        "    float weight[5] = float[] (0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);\n"
        "    vec2 tex_offset = 1.0 / vec2(1280.0, 720.0);\n" // Hardcoded resolution for simplicity
        "    vec3 result = texture2D(image, TexCoords).rgb * weight[0];\n"
        "    if(horizontal) {\n"
        "        for(int i = 1; i < 5; ++i) {\n"
        "            result += texture2D(image, TexCoords + vec2(tex_offset.x * i, 0.0)).rgb * weight[i];\n"
        "            result += texture2D(image, TexCoords - vec2(tex_offset.x * i, 0.0)).rgb * weight[i];\n"
        "        }\n"
        "    } else {\n"
        "        for(int i = 1; i < 5; ++i) {\n"
        "            result += texture2D(image, TexCoords + vec2(0.0, tex_offset.y * i)).rgb * weight[i];\n"
        "            result += texture2D(image, TexCoords - vec2(0.0, tex_offset.y * i)).rgb * weight[i];\n"
        "        }\n"
        "    }\n"
        "    gl_FragColor = vec4(result, 1.0);\n"
        "}";

    /* BLOOM: Final Combination Shader */
    const char* finalFrag = "#version 120\n"
        "uniform sampler2D scene;\n"
        "uniform sampler2D bloomBlur;\n"
        "varying vec2 TexCoords;\n"
        "void main() {\n"
        "    vec3 hdrColor = texture2D(scene, TexCoords).rgb;\n"
        "    vec3 bloomColor = texture2D(bloomBlur, TexCoords).rgb;\n"
        "    /* Additive mixing with slight tone mapping */\n"
        "    vec3 result = hdrColor + bloomColor;\n" 
        "    gl_FragColor = vec4(result, 1.0);\n"
        "}";

    blurShaderProgram = linkProgram(compileShader(quadVert, GL_VERTEX_SHADER), compileShader(blurFrag, GL_FRAGMENT_SHADER));
    finalShaderProgram = linkProgram(compileShader(quadVert, GL_VERTEX_SHADER), compileShader(finalFrag, GL_FRAGMENT_SHADER));
}

void renderQuad() {
    if (quadVAO == 0) {
        float quadVertices[] = {
            /* pos (2) */
            -1.0f,  1.0f,
            -1.0f, -1.0f,
             1.0f,  1.0f,
             1.0f, -1.0f,
        };
        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    }
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void initBloomFBO() {
    /* 1. MSAA Framebuffer (Initial Render Target) */
    glGenFramebuffers(1, &fbo_msaa);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_msaa);

    glGenRenderbuffers(1, &rbo_color_msaa);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo_color_msaa);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGB16F, 1280, 720);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo_color_msaa);

    glGenRenderbuffers(1, &rbo_depth_msaa);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo_depth_msaa);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT, 1280, 720);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo_depth_msaa);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        printf("[BLOOM] ERROR: MSAA Framebuffer not complete!\n");

    /* 2. Scene FBO (Resolve Target for MSAA) */
    glGenFramebuffers(1, &fbo_scene);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_scene);

    glGenTextures(1, &tex_scene);
    glBindTexture(GL_TEXTURE_2D, tex_scene);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 1280, 720, 0, GL_RGB, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_scene, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        printf("[BLOOM] ERROR: Scene Framebuffer not complete!\n");

    /* 3. Ping-Pong FBOs for Blur */
    glGenFramebuffers(2, fbo_pingpong);
    glGenTextures(2, tex_pingpong);
    for (unsigned int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_pingpong[i]);
        glBindTexture(GL_TEXTURE_2D, tex_pingpong[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 1280, 720, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); 
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_pingpong[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            printf("[BLOOM] ERROR: PingPong Framebuffer %d not complete!\n", i);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    printf("[BLOOM] FBOs initialized successfully.\n");
}

int shm_fd = -1;
GameState *g_shared_state = NULL;

volatile int g_data_dirty = 0;

void *shm_listener_thread(void *arg) {
    while(1) {
        if (g_shared_state) {
            sem_wait(&g_shared_state->data_ready);
            g_data_dirty = 1;
        } else usleep(10000);
    }
    return NULL;
}
volatile int g_is_loading = 0;
int g_is_cloaked_rendering = 0;
float angleY = 0.0f;
float angleX = 20.0f;
float zoom = -14.0f;
float autoRotate = 0.075f;
float pulse = 0.0f;
float map_anim = 0.0f;
float bridge_anim = 0.0f;
GLdouble hud_model[16], hud_proj[16];
GLint hud_view[4];
int g_shield_hit_timers[6] = {0,0,0,0,0,0};
int g_hull_hit_timer = 0;
float g_last_hull = 100.0f;
int g_last_shields_val_hit[6] = {0,0,0,0,0,0};

int g_energy = 0, g_crew = 0, g_prison_unit = 0, g_shields = 0, g_Korthians = 0;
int g_composite_plating = 0;
float g_hull_integrity = 100.0f;
int g_shields_val[6] = {0};
int g_cargo_energy = 0, g_cargo_torps = 0, g_torpedoes_launcher = 0;
float g_system_health[10] = {0};
int g_inventory[10] = {0};
int g_lock_target = 0;
int g_show_axes = 0;
int g_show_grid = 0;
int g_show_map = 0;
int g_show_bridge = 0;
int g_is_docked = 0;
int g_red_alert = 0;
int g_is_jammed = 0;
int g_nav_state = 0;
int g_map_filter = 0;
int g_my_q[3] = {1,1,1};
int64_t g_galaxy[11][11][11];
int g_show_hud = 1; /* Default HUD ON */
char g_quadrant[128] = "Scanning...";
char g_last_quadrant[128] = "";
char g_player_name[64] = "Unknown";
int g_player_class = 0;

#define MAX_TRAIL 40
typedef struct {
    float x, y, z;
    float tx, ty, tz; /* Interpolation targets */
    float h, m;
    float th, tm;     /* Target heading and mark */
    int type;
    int ship_class;
    int health_pct;   /* HUD */
    int energy;       /* HUD */
        int plating;       /* HUD */
        int hull_integrity; /* HUD */
        int faction;       /* HUD */
        int id;           /* HUD */
        int is_cloaked;   /* Cloaking Device status */
        char name[64];    /* HUD Name */
    
    float trail[MAX_TRAIL][3];
    int trail_ptr;
    int trail_count;
    double last_update_time;
} GameObject;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float r, g, b;
    int active;
} Particle;

typedef struct {
    float x, y, z;
    int species;
    int timer;
    Particle particles[100];
} Dismantle;

typedef struct {
    float x, y, z;
    int timer;
    Particle particles[150];
} ArrivalEffect;

typedef struct {
    float x, y, z;
    int active;
    int status;
    float eta;
    int q1, q2, q3;
} ViewProbe;

typedef struct {
    float x, y, z;
    int timer;
} RecoveryFX;

GameObject objects[256];
int objectCount = 0;
ViewProbe g_local_probes[3];
Dismantle g_dismantle = {-100, -100, -100, 0, 0};
ArrivalEffect g_arrival_fx = {0, 0, 0, 0};
RecoveryFX g_recovery_fx = {0, 0, 0, 0};

/* Rimosse variabili globali scia singola per scia universale */
typedef struct { float sx, sy, sz, tx, ty, tz, alpha; } IonBeam;
IonBeam beams[10];
int beamCount = 0;

typedef struct { float x, y, z, h, m; int active; int timer; } ViewPoint;
ViewPoint g_torp = {0,0,0,0,0};
ViewPoint g_boom = {0,0,0,0,0};
ViewPoint g_wormhole = {0,0,0,0,0};
ViewPoint g_jump_arrival = {0,0,0,0,0};
ViewPoint g_sn_pos = {0,0,0,0,0};
int g_sn_q[3] = {0,0,0};

float PlayerX = 0, PlayerY = 0, PlayerZ = 0;
float stars[1000][3];

/* Prototypes */
void drawStarbase(float x, float y, float z);
void drawPlanet(float x, float y, float z);
void drawGrid();
void drawKorthian(float x, float y, float z);
void drawXylari(float x, float y, float z);
void drawSwarm(float x, float y, float z);
void drawVesperian(float x, float y, float z);
void drawAscendant(float x, float y, float z);
void drawQuarzite(float x, float y, float z);
void drawSaurian(float x, float y, float z);
void drawGilded(float x, float y, float z);
void drawFluidicVoid(float x, float y, float z);
void drawCryos(float x, float y, float z);
void drawApex(float x, float y, float z);
void drawNacelle(float len, float width, float r, float g, float b);
void drawDeflector(float r, float g, float b);
void drawLegacy();
void drawScout();
void drawHeavyCruiser();
void drawMultiEngine();
void drawEscort();
void drawExplorer();
void drawFlagship();
void drawScience();
void drawCarrier();
void drawTactical();
void drawDiplomatic();
void drawResearch();
void drawFrigate();
void drawGlow(float radius, float r, float g, float b, float alpha);
void drawHullDetail(void (*drawFunc)(void), float r, float g, float b);
void drawNavLights(float x, float y, float z);
void drawCommandModule(float sx, float sy, float sz);
void glutSolidSphere_wrapper_module();
void glutSolidSphere_wrapper_hull();
void glutSolidSphere_wrapper_escort();
void glutSolidCube_wrapper();

void handle_signal(int sig) { if (sig == SIGUSR1) g_data_dirty = 1; }

void initStars() {
    for(int i=0; i<1000; i++) {
        float r = 150.0f + (float)(rand()%100);
        float t = (float)(rand()%360) * 3.14/180;
        float p = (float)(rand()%360) * 3.14/180;
        stars[i][0] = r * sin(p) * cos(t);
        stars[i][1] = r * sin(p) * sin(t);
        stars[i][2] = r * cos(p);
    }
}

void initVBOs() {
    glGenBuffers(1, &vbo_stars);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_stars);
    glBufferData(GL_ARRAY_BUFFER, sizeof(stars), stars, GL_STATIC_DRAW);
    int max_verts = 11 * 11 * 3 * 2; 
    float *grid_data = malloc(max_verts * 3 * sizeof(float));
    int idx = 0;
    for(int i=0; i<=10; i++) {
        float p = -5.0f + (float)i;
        for(int j=0; j<=10; j++) {
            float q = -5.0f + (float)j;
            grid_data[idx++] = p; grid_data[idx++] = q; grid_data[idx++] = -5.0f;
            grid_data[idx++] = p; grid_data[idx++] = q; grid_data[idx++] = 5.0f;
            grid_data[idx++] = p; grid_data[idx++] = -5.0f; grid_data[idx++] = q;
            grid_data[idx++] = p; grid_data[idx++] = 5.0f; grid_data[idx++] = q;
            grid_data[idx++] = -5.0f; grid_data[idx++] = p; grid_data[idx++] = q;
            grid_data[idx++] = 5.0f; grid_data[idx++] = p; grid_data[idx++] = q;
        }
    }
    grid_vertex_count = idx / 3;
    glGenBuffers(1, &vbo_grid);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_grid);
    glBufferData(GL_ARRAY_BUFFER, idx * sizeof(float), grid_data, GL_STATIC_DRAW);
    free(grid_data);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

long long last_frame_id = -1;

void loadGameState() {
    if (!g_shared_state) return;
    pthread_mutex_lock(&g_shared_state->mutex);
    if (g_shared_state->frame_id == last_frame_id) { pthread_mutex_unlock(&g_shared_state->mutex); return; }
    last_frame_id = g_shared_state->frame_id;
    g_is_loading = 1;
    g_energy = g_shared_state->shm_energy;
    g_composite_plating = g_shared_state->shm_composite_plating;
    g_hull_integrity = g_shared_state->shm_hull_integrity;
    g_crew = g_shared_state->shm_crew;
    g_prison_unit = g_shared_state->shm_prison_unit;
    g_torpedoes_launcher = g_shared_state->shm_torpedoes;
    g_cargo_energy = g_shared_state->shm_cargo_energy;
    g_cargo_torps = g_shared_state->shm_cargo_torpedoes;
    for(int s=0; s<10; s++) g_system_health[s] = g_shared_state->shm_system_health[s];
    for(int inv=0; inv<10; inv++) g_inventory[inv] = g_shared_state->inventory[inv];
    g_lock_target = g_shared_state->shm_lock_target;
    g_is_docked = g_shared_state->shm_is_docked;
    g_red_alert = g_shared_state->shm_red_alert;
    g_is_jammed = g_shared_state->shm_is_jammed;
    g_nav_state = g_shared_state->shm_nav_state;
    int total_s = 0;
    for(int s=0; s<6; s++) {
        /* Always detect a hit if the shared state value is less than our last tracked value,
           OR if the timer is triggered by the server. 
           Wait, the client only sees the CURRENT value. If the value is 0 and stays 0, we need another way.
           However, Ion Beams usually do a pulse. 
        */
        if (g_shared_state->shm_shields[s] < g_last_shields_val_hit[s]) {
            g_shield_hit_timers[s] = 40; 
            /* Spawn blue shield particles on hit */
            for(int k=0; k<15; k++) {
                float vx = ((float)rand()/RAND_MAX - 0.5f) * 0.15f;
                float vy = ((float)rand()/RAND_MAX - 0.5f) * 0.15f;
                float vz = ((float)rand()/RAND_MAX - 0.5f) * 0.15f;
                spawnParticle(PlayerX, PlayerY, PlayerZ, vx, vy, vz, 0.0f, 0.8f, 1.0f, 0.2f, 0.4f);
            }
        } else if (g_hull_integrity < g_last_hull && g_shared_state->shm_shields[s] == 0) {
             /* If hull is taking damage and this shield is 0, it means it's passing through. 
                Spawn some 'shield failure' sparks too? No, hull sparks are enough. */
        }
        g_last_shields_val_hit[s] = g_shared_state->shm_shields[s];
        total_s += g_shared_state->shm_shields[s];
    }
    
    /* Detect Direct Hull Hit: if hull decreased */
    if (g_hull_integrity < g_last_hull) {
        g_hull_hit_timer = 20; /* Red pulse duration */
        /* Spawn fast solid metallic fragments on impact */
        for(int k=0; k<40; k++) {
            float ox = ((float)rand()/RAND_MAX - 0.5f) * 0.3f;
            float oy = ((float)rand()/RAND_MAX - 0.5f) * 0.3f;
            float oz = ((float)rand()/RAND_MAX - 0.5f) * 0.3f;
            float vx = ((float)rand()/RAND_MAX - 0.5f) * 0.18f;
            float vy = ((float)rand()/RAND_MAX - 0.5f) * 0.18f;
            float vz = ((float)rand()/RAND_MAX - 0.5f) * 0.18f;
            
            /* Varied colors: Steel, Copper, Spark, White */
            float r, g, b;
            int type = rand() % 4;
            if (type == 0) { r=0.7f; g=0.7f; b=0.8f; }      /* Steel */
            else if (type == 1) { r=0.9f; g=0.5f; b=0.2f; } /* Copper/Bronze */
            else if (type == 2) { r=1.0f; g=0.8f; b=0.1f; } /* Hot Spark */
            else { r=1.0f; g=1.0f; b=1.0f; }                /* White hot */
            
            float size = 0.1f + ((float)rand()/RAND_MAX * 0.2f);
            spawnParticle(PlayerX + ox, PlayerY + oy, PlayerZ + oz, vx, vy, vz, r, g, b, size, 0.7f);
        }
    }
    g_last_hull = g_hull_integrity;

    g_shields = total_s / 6;
    for(int s=0; s<6; s++) g_shields_val[s] = g_shared_state->shm_shields[s];
    g_Korthians = g_shared_state->Korthians;
    size_t nlen = strlen(g_shared_state->objects[0].shm_name);
    if (nlen > 63) nlen = 63;
    memcpy(g_player_name, g_shared_state->objects[0].shm_name, nlen);
    g_player_name[nlen] = '\0';
    g_player_class = g_shared_state->objects[0].ship_class;
    
    int quadrant_changed = 0;
    if (strcmp(g_quadrant, g_shared_state->quadrant) != 0) {
        quadrant_changed = 1;
        strcpy(g_quadrant, g_shared_state->quadrant);
        /* Cleanup local visual effects on sector jump */
        g_wormhole.active = 0;
        g_jump_arrival.timer = 0;
    }
    
    g_show_axes = g_shared_state->shm_show_axes;
    g_show_grid = g_shared_state->shm_show_grid;
    g_show_map = g_shared_state->shm_show_map;
    g_show_bridge = g_shared_state->shm_show_bridge;
    g_map_filter = g_shared_state->shm_map_filter;
    g_my_q[0] = g_shared_state->shm_q[0];
    g_my_q[1] = g_shared_state->shm_q[1];
    g_my_q[2] = g_shared_state->shm_q[2];
    
    /* Center the camera/world on the player's explicit sector coordinates */
    float s0 = g_shared_state->shm_s[0];
    float s1 = g_shared_state->shm_s[1];
    float s2 = g_shared_state->shm_s[2];
    
    if (!isnan(s0) && !isnan(s1) && !isnan(s2)) {
        PlayerX = s0 - 5.0f;
        PlayerY = s2 - 5.0f; /* Y in Stellar is Z in Viewer */
        PlayerZ = 5.0f - s1; /* Z in Stellar is -Y in Viewer */
    }

    memcpy(g_galaxy, g_shared_state->shm_galaxy, sizeof(g_galaxy));

    for(int p=0; p<3; p++) {
        g_local_probes[p].active = g_shared_state->probes[p].active;
        g_local_probes[p].q1 = g_shared_state->probes[p].q1;
        g_local_probes[p].q2 = g_shared_state->probes[p].q2;
        g_local_probes[p].q3 = g_shared_state->probes[p].q3;
        g_local_probes[p].eta = g_shared_state->probes[p].eta;
        g_local_probes[p].status = g_shared_state->probes[p].status;
        g_local_probes[p].x = g_shared_state->probes[p].s1 - 5.0f;
        g_local_probes[p].y = g_shared_state->probes[p].s3 - 5.0f;
        g_local_probes[p].z = 5.0f - g_shared_state->probes[p].s2;
    }

    int updated[256] = {0};
    objectCount = g_shared_state->object_count;
    for(int i=0; i<objectCount; i++) {
        int target_id = g_shared_state->objects[i].id;
        int local_idx = -1;

        /* Find existing object by ID */
        for(int k=0; k<256; k++) {
            if (objects[k].id == target_id && target_id != 0) {
                local_idx = k;
                break;
            }
        }

        /* If not found, find an empty slot */
        if (local_idx == -1) {
            for(int k=0; k<256; k++) {
                if (objects[k].id == 0) {
                    local_idx = k;
                    objects[k].id = target_id;
                    /* First time initialization */
                    objects[k].x = -100.0f; 
                    break;
                }
            }
        }

        if (local_idx != -1) {
            updated[local_idx] = 1;
            GameObject *obj = &objects[local_idx];
            float next_x = g_shared_state->objects[i].shm_x - 5.0f;
            float next_y = g_shared_state->objects[i].shm_z - 5.0f;
            float next_z = 5.0f - g_shared_state->objects[i].shm_y;
            
            if (isnan(next_x) || isnan(next_y) || isnan(next_z)) {
                updated[local_idx] = 0; /* Ignore this object */
                continue;
            }

            float dx = next_x - obj->x;
            float dy = next_y - obj->y;
            float dz = next_z - obj->z;
            
            if (quadrant_changed || obj->x < -50.0f || (dx*dx + dy*dy + dz*dz) > 25.0f) {
                obj->x = obj->tx = next_x;
                obj->y = obj->ty = next_y;
                obj->z = obj->tz = next_z;
                obj->h = obj->th = g_shared_state->objects[i].h;
                obj->m = obj->tm = g_shared_state->objects[i].m;
                obj->trail_count = 0;
                obj->trail_ptr = 0;
            } else {
                obj->tx = next_x;
                obj->ty = next_y;
                obj->tz = next_z;
            }

            obj->th = g_shared_state->objects[i].h;
            obj->tm = g_shared_state->objects[i].m;
            obj->last_update_time = glutGet(GLUT_ELAPSED_TIME);
            obj->type = g_shared_state->objects[i].type;
            obj->ship_class = g_shared_state->objects[i].ship_class;
            obj->health_pct = g_shared_state->objects[i].health_pct;
            obj->energy = g_shared_state->objects[i].energy;
            obj->plating = g_shared_state->objects[i].plating;
            obj->hull_integrity = g_shared_state->objects[i].hull_integrity;
            obj->faction = g_shared_state->objects[i].faction;
            obj->is_cloaked = g_shared_state->objects[i].is_cloaked;
            strncpy(obj->name, g_shared_state->objects[i].shm_name, 63);
        }
    }

    /* Clear stale objects not present in the latest update */
    for(int k=0; k<256; k++) {
        if (!updated[k]) {
            objects[k].type = 0;
            objects[k].id = 0;
        }
    }

    if (g_shared_state->beam_count > 0) {
        for(int i=0; i<g_shared_state->beam_count; i++) {
            int slot = -1;
            for(int j=0; j<10; j++) if(beams[j].alpha <= 0) { slot = j; break; }
            if (slot == -1) slot = rand()%10;
            /* Source */
            beams[slot].sx = g_shared_state->beams[i].shm_sx - 5.0f;
            beams[slot].sy = g_shared_state->beams[i].shm_sz - 5.0f;
            beams[slot].sz = 5.0f - g_shared_state->beams[i].shm_sy;
            /* Target */
            beams[slot].tx = g_shared_state->beams[i].shm_tx - 5.0f;
            beams[slot].ty = g_shared_state->beams[i].shm_tz - 5.0f;
            beams[slot].tz = 5.0f - g_shared_state->beams[i].shm_ty;
            beams[slot].alpha = 1.5f;
        }
        /* Consume events */
        g_shared_state->beam_count = 0;
    }
    if (g_shared_state->torp.active) {
        g_torp.x = g_shared_state->torp.shm_x - 5.0f;
        g_torp.y = g_shared_state->torp.shm_z - 5.0f;
        g_torp.z = 5.0f - g_shared_state->torp.shm_y;
        g_torp.active = 1;
    } else g_torp.active = 0;
    if (g_shared_state->boom.active) {
        g_boom.x = g_shared_state->boom.shm_x - 5.0f;
        g_boom.y = g_shared_state->boom.shm_z - 5.0f;
        g_boom.z = 5.0f - g_shared_state->boom.shm_y;
        g_boom.active = 1;
        g_boom.timer = 40; /* 1.3 seconds approx */
        /* Consume event */
        g_shared_state->boom.active = 0;
    }
    if (g_shared_state->wormhole.active) {
        g_wormhole.x = g_shared_state->wormhole.shm_x - 5.0f;
        g_wormhole.y = g_shared_state->wormhole.shm_z - 5.0f;
        g_wormhole.z = 5.0f - g_shared_state->wormhole.shm_y;
        g_wormhole.h = g_shared_state->shm_h; 
        g_wormhole.m = g_shared_state->shm_m;
        g_wormhole.active = 1;
    } else {
        g_wormhole.active = 0;
    }

    if (g_shared_state->jump_arrival.active) {
        float jx = g_shared_state->jump_arrival.shm_x - 5.0f;
        float jy = g_shared_state->jump_arrival.shm_z - 5.0f;
        float jz = 5.0f - g_shared_state->jump_arrival.shm_y;
        
        /* Sanity check: prevent invalid arrival triggers (large values indicate uninitialized state) */
        if (fabs(jx) > 50.0f || fabs(jy) > 50.0f) {
             g_shared_state->jump_arrival.active = 0;
        } else {
                    g_jump_arrival.x = jx; g_jump_arrival.y = jy; g_jump_arrival.z = jz;
                    g_jump_arrival.h = g_shared_state->shm_h; 
                    g_jump_arrival.m = g_shared_state->shm_m;
                    g_jump_arrival.active = 1;            g_jump_arrival.timer = 300;
            g_arrival_fx.x = jx; g_arrival_fx.y = jy; g_arrival_fx.z = jz;
            g_arrival_fx.timer = 300;
            
            for(int i=0; i<150; i++) {
                float theta = (rand() % 360) * M_PI / 180.0f;
                float phi = (rand() % 180 - 90) * M_PI / 180.0f;
                float dist = 3.0f + (rand() % 200) / 100.0f;
                g_arrival_fx.particles[i].x = g_arrival_fx.x + dist * cos(phi) * cos(theta);
                g_arrival_fx.particles[i].y = g_arrival_fx.y + dist * sin(phi);
                g_arrival_fx.particles[i].z = g_arrival_fx.z + dist * cos(phi) * sin(theta);
                g_arrival_fx.particles[i].vx = (g_arrival_fx.x - g_arrival_fx.particles[i].x) / 100.0f;
                g_arrival_fx.particles[i].vy = (g_arrival_fx.y - g_arrival_fx.particles[i].y) / 100.0f;
                g_arrival_fx.particles[i].vz = (g_arrival_fx.z - g_arrival_fx.particles[i].z) / 100.0f;
                g_arrival_fx.particles[i].r = (rand() % 100) / 100.0f;
                g_arrival_fx.particles[i].g = (rand() % 100) / 100.0f;
                g_arrival_fx.particles[i].b = (rand() % 100) / 100.0f;
                g_arrival_fx.particles[i].active = 1;
            }
            g_shared_state->jump_arrival.active = 0;
        }
    }

    /* Supernova epicenter */
    if (g_shared_state->supernova_pos.active > 0) {
        g_sn_pos.x = g_shared_state->supernova_pos.shm_x - 5.0f;
        g_sn_pos.y = g_shared_state->supernova_pos.shm_z - 5.0f;
        g_sn_pos.z = 5.0f - g_shared_state->supernova_pos.shm_y;
        g_sn_pos.active = 1;
        g_sn_pos.timer = g_shared_state->supernova_pos.active;
        g_sn_q[0] = g_shared_state->shm_sn_q[0];
        g_sn_q[1] = g_shared_state->shm_sn_q[1];
        g_sn_q[2] = g_shared_state->shm_sn_q[2];
    } else {
        g_sn_pos.active = 0;
        g_sn_q[0] = g_sn_q[1] = g_sn_q[2] = 0;
    }

    /* Dismantle */
    if (g_shared_state->dismantle.active) {
        g_dismantle.x = g_shared_state->dismantle.shm_x - 5.0f;
        g_dismantle.y = g_shared_state->dismantle.shm_z - 5.0f;
        g_dismantle.z = 5.0f - g_shared_state->dismantle.shm_y;
        g_dismantle.species = g_shared_state->dismantle.species;
        g_dismantle.timer = 60;
        for(int i=0; i<100; i++) {
            g_dismantle.particles[i].x = g_dismantle.x;
            g_dismantle.particles[i].y = g_dismantle.y;
            g_dismantle.particles[i].z = g_dismantle.z;
            g_dismantle.particles[i].vx = ((rand()%100)-50)/150.0f;
            g_dismantle.particles[i].vy = ((rand()%100)-50)/150.0f;
            g_dismantle.particles[i].vz = ((rand()%100)-50)/150.0f;
            g_dismantle.particles[i].r = (rand()%100)/100.0f;
            g_dismantle.particles[i].g = (rand()%100)/100.0f;
            g_dismantle.particles[i].b = (rand()%100)/100.0f;
            g_dismantle.particles[i].active = 1;
        }
        /* Reset event to prevent re-triggering */
        g_shared_state->dismantle.active = 0;
    }

    if (g_shared_state->recovery_fx.active) {
        g_recovery_fx.x = g_shared_state->recovery_fx.shm_x - 5.0f;
        g_recovery_fx.y = g_shared_state->recovery_fx.shm_z - 5.0f;
        g_recovery_fx.z = 5.0f - g_shared_state->recovery_fx.shm_y;
        g_recovery_fx.timer = 60;
        g_shared_state->recovery_fx.active = 0;
    }
    pthread_mutex_unlock(&g_shared_state->mutex);
    kill(getppid(), SIGUSR2);
    g_is_loading = 0;
}

void drawText3D(float x, float y, float z, const char* text) {
    glRasterPos3f(x, y, z);
    for(int i=0; i<strlen(text); i++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, text[i]);
}

const char* getSpeciesName(int s) {
    switch(s) {
        case 1: return "Player"; case 3: return "Starbase"; case 4: return "Star"; case 5: return "Planet"; case 6: return "Black Hole";
        case 7: return "Tactical Cruiser"; case 8: return "Pulsar";
        case 10: return "Korthian"; case 11: return "Xylari"; case 12: return "Swarm";
        case 13: return "Vesperian"; case 14: return "Ascendant"; case 15: return "Quarzite";
        case 16: return "Saurian"; case 17: return "Gilded"; case 18: return "Fluidic Void";
        case 19: return "Cryos"; case 20: return "Apex";
        default: return "Unknown";
    }
}

const char* getFactionHUDName(int f) {
    switch(f) {
        case 0:  return "Alliance";
        case 10: return "Korthian Empire";
        case 11: return "Xylari Star Empire";
        case 12: return "Swarm Collective";
        case 13: return "Vesperian Union";
        case 14: return "The Ascendancy";
        case 15: return "Quarzite Matrix";
        case 16: return "Saurian Legion";
        case 17: return "Gilded Cartel";
        case 18: return "Fluidic Void";
        case 19: return "Cryos Enclave";
        case 20: return "Apex Stalkers";
        default: return "Unknown Faction";
    }
}

const char* getClassName(int c) {
    switch(c) {
        case SHIP_CLASS_LEGACY: return "Legacy Class";
        case SHIP_CLASS_SCOUT:      return "Scout Class";
        case SHIP_CLASS_HEAVY_CRUISER:    return "Heavy Cruiser";
        case SHIP_CLASS_MULTI_ENGINE: return "Multi-Engine Cruiser";
        case SHIP_CLASS_ESCORT:      return "Escort Class";
        case SHIP_CLASS_EXPLORER:       return "Explorer Class";
        case SHIP_CLASS_FLAGSHIP:    return "Flagship Class";
        case SHIP_CLASS_SCIENCE:     return "Science Vessel";
        case SHIP_CLASS_CARRIER:        return "Carrier Class";
        case SHIP_CLASS_TACTICAL:       return "Tactical Cruiser";
        case SHIP_CLASS_DIPLOMATIC:   return "Diplomatic Cruiser";
        case SHIP_CLASS_RESEARCH:       return "Research Vessel";
        case SHIP_CLASS_FRIGATE:  return "Frigate Class";
        case SHIP_CLASS_GENERIC_ALIEN: return "Vessel";
        default:                      return "Vessel";
    }
}

void drawHUD(int obj_idx) {
    if (obj_idx < 0 || obj_idx >= objectCount) return;
    
    GameObject *obj = &objects[obj_idx];
    float x = obj->x;
    float y = obj->y;
    float z = obj->z;
    int type = obj->type;
    int id = obj->id;
    int hp = obj->health_pct;

    GLdouble winX, winY, winZ;

    /* Project slightly above the ship (Y is height in viewer) */
    float v_off = (type == 21) ? 0.4f : 0.8f;
    if (gluProject(x, y + v_off, z, hud_model, hud_proj, hud_view, &winX, &winY, &winZ) == GL_TRUE) {
        /* Check if behind camera or too close to clip plane */
        if (winZ < 0.0 || winZ > 1.0) return;

        glMatrixMode(GL_PROJECTION);
        glPushMatrix(); glLoadIdentity();
        gluOrtho2D(0, hud_view[2], 0, hud_view[3]);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);

        /* Draw Name/ID - Use a fixed offset from winY to keep it above health bar */
        char buf[128];
        bool handled_custom = false;
        if (type == 1) {
            /* Player: Faction - Class (Captain) */
            if (obj->faction == 0) { // FACTION_ALLIANCE
                sprintf(buf, "Alliance - %s (%s)", getClassName(obj->ship_class), obj->name);
            } else {
                sprintf(buf, "%s (%s)", getFactionHUDName(obj->faction), obj->name);
            }
            glColor3f(0.0f, 1.0f, 1.0f); /* Cyan for player */
        } else if (type >= 10 && type < 21) {
            /* NPC: Species [ID] */
            if (type == 12) { /* Swarm Special */
                sprintf(buf, "Swarm CUBE [%d]", id);
                glColor3f(0.0f, 1.0f, 0.0f); /* Green for Swarm */
            } else {
                sprintf(buf, "%s [%d]", (obj->name[0] != '\0') ? obj->name : getSpeciesName(type), id);
                glColor3f(1.0f, 0.3f, 0.3f); /* Redish for NPCs */
            }
        } else {
            /* Other: Name based on type */
            const char* type_name = "Object";
            switch(type) {
                case 3: type_name = "STARBASE"; glColor3f(0.0f, 1.0f, 0.0f); break;
                case 4: {
                    type_name = "STAR"; glColor3f(1.0f, 1.0f, 0.0f);
                    const char* classes[] = {"O (Blue)", "B (Light Blue)", "A (White)", "F (Yellow-White)", "G (Yellow)", "K (Orange)", "M (Red)"};
                    int c_idx = obj->ship_class; if(c_idx<0) c_idx=0; if(c_idx>6) c_idx=6;
                    sprintf(buf, "STAR: %s [%d]", classes[c_idx], id);
                    handled_custom = true;
                } break;
                case 5: {
                    type_name = "PLANET"; glColor3f(0.0f, 1.0f, 0.5f);
                    const char* res[] = {"None", "Aetherium", "Neo-Titanium", "Void-Essence", "Graphene", "Synaptics", "Nebular Gas", "Composite", "Dark-Matter"};
                    int r_idx = obj->ship_class; if(r_idx<0) r_idx=0; if(r_idx>8) r_idx=8;
                    sprintf(buf, "PLANET: %s [%d]", res[r_idx], id);
                    handled_custom = true;
                } break;
                case 6: type_name = "BLACK HOLE"; glColor3f(0.5f, 0.0f, 1.0f); break;
                case 7: {
                    type_name = "NEBULA"; glColor3f(0.7f, 0.7f, 0.7f);
                    const char* neb_classes[] = {"Standard Class", "High-Energy Class", "Dark Matter Class", "Ionic Class", "Gravimetric Class", "Temporal Class"};
                    int n_idx = obj->ship_class; if(n_idx<0) n_idx=0; if(n_idx>5) n_idx=5;
                    sprintf(buf, "NEBULA: %s [%d]", neb_classes[n_idx], id);
                    handled_custom = true;
                } break;
                case 8: type_name = "PULSAR"; glColor3f(1.0f, 0.5f, 0.0f); break;
                case 9: type_name = "COMET"; glColor3f(0.5, 0.8, 1.0); break;
                case 21: {
                    type_name = "ASTEROID"; glColor3f(0.6f, 0.4f, 0.2f); 
                    const char* res_names[] = {"None", "Aetherium", "Neo-Titanium", "Void-Essence", "Graphene", "Synaptics", "Nebular Gas", "Composite", "Dark-Matter"};
                    int r_idx = obj->ship_class; if(r_idx<0) r_idx=0; if(r_idx>8) r_idx=8;
                    sprintf(buf, "ASTEROID [%d]: %s (%d units)", id, res_names[r_idx], obj->energy);
                    handled_custom = true;
                } break;
                case 22: type_name = "DERELICT"; glColor3f(0.4f, 0.4f, 0.4f); break;
                case 23: type_name = "MINE"; glColor3f(1.0f, 0.0f, 0.0f); break;
                case 24: type_name = "BUOY"; glColor3f(0.0f, 0.5f, 1.0f); break;
                case 25: type_name = "PLATFORM"; glColor3f(1.0f, 0.6f, 0.0f); break;
                case 26: type_name = "RIFT"; glColor3f(0.0f, 1.0f, 1.0f); break;
                case 30: 
                case 31: {
                    type_name = (type == 30) ? "CRYSTALLINE ENTITY" : "SPACE AMOEBA";
                    glColor3f(1.0f, 1.0f, 1.0f);
                    sprintf(buf, "%s [%d]", type_name, id);
                } break;
            }
            if (handled_custom) {
                /* Skip generic formatting */
            } else if (obj->name[0] != '\0' && strcmp(obj->name, "Unknown") != 0) {
                sprintf(buf, "%s: %s [%d]", type_name, obj->name, id);
            } else {
                sprintf(buf, "%s [%d]", type_name, id);
            }
        }
        
        glRasterPos2f(winX - (strlen(buf)*4), winY + 25);
        for(int i=0; i<strlen(buf); i++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, buf[i]);

        /* Draw Health Bar (Ships, Bases, Platforms, Monsters) */
        if (type == 1 || type == 3 || (type >= 10 && type <= 20) || type == 22 || type == 25 || type >= 30) {
            float w = 40.0f;
            float h = 4.0f;
            float bar = (hp / 100.0f) * w;
            if (bar < 0) bar = 0; 
            if (bar > w) bar = w;

            /* Border */
            glColor3f(0.5f, 0.5f, 0.5f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(winX - w/2, winY);
            glVertex2f(winX + w/2, winY);
            glVertex2f(winX + w/2, winY + h);
            glVertex2f(winX - w/2, winY + h);
            glEnd();

            /* Fill */
            if (hp > 50) glColor3f(0.0f, 1.0f, 0.0f);
            else if (hp > 25) glColor3f(1.0f, 1.0f, 0.0f);
            else glColor3f(1.0f, 0.0f, 0.0f);

            glBegin(GL_QUADS);
            glVertex2f(winX - w/2, winY);
            glVertex2f(winX - w/2 + bar, winY);
            glVertex2f(winX - w/2 + bar, winY + h);
            glVertex2f(winX - w/2, winY + h);
            glEnd();

            /* Hull Integrity Text */
            char hbuf[32];
            sprintf(hbuf, "HULL: %d%%", obj->hull_integrity);
            if (obj->hull_integrity > 60) glColor3f(0, 1, 0);
            else if (obj->hull_integrity > 25) glColor3f(1, 1, 0);
            else glColor3f(1, 0, 0);
            glRasterPos2f(winX + w/2 + 5, winY);
            for(int i=0; i<strlen(hbuf); i++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, hbuf[i]);

            /* Plating indicator if present */
            if (obj->plating > 0) {
                char pbuf[32];
                sprintf(pbuf, "HULL: +%d", obj->plating);
                glColor3f(1.0f, 0.8f, 0.0f); /* Yellow for Composite */
                glRasterPos2f(winX - (strlen(pbuf)*4), winY + 35);
                for(int i=0; i<strlen(pbuf); i++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, pbuf[i]);
            }
        }

        glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
        glMatrixMode(GL_PROJECTION); glPopMatrix();
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
    }
}

void drawHeadingRing() {
    /* 1. Heading Ring (Horizontal plane XZ in OpenGL) */
    glColor4f(0.0f, 1.0f, 1.0f, 0.3f);
    glBegin(GL_LINE_LOOP);
    for(int i=0; i<360; i+=5) {
        float rad = i * M_PI / 180.0f;
        glVertex3f(sin(rad)*2.5f, 0, cos(rad)*2.5f);
    }
    glEnd();

    /* 2. Heading Labels (every 45 degrees) */
    glColor3f(0.0f, 0.8f, 0.8f);
    for(int i=0; i<360; i+=45) {
        float rad = i * M_PI / 180.0f;
        float lx = sin(rad)*2.7f;
        float lz = cos(rad)*2.7f;
        char buf[8]; sprintf(buf, "%d", i);
        drawText3D(lx, 0.1f, lz, buf);
    }
}

void drawFixedCompass() {
    /* Fixed Horizontal Compass - White */
    glDisable(GL_LIGHTING);
    glColor4f(1.0f, 1.0f, 1.0f, 0.4f);
    
    /* Outer Ring */
    glBegin(GL_LINE_LOOP);
    for(int i=0; i<360; i+=5) {
        float rad = i * M_PI / 180.0f;
        glVertex3f(sin(rad)*3.0f, 0, cos(rad)*3.0f);
    }
    glEnd();

    /* Inner Ticks and Labels */
    glColor3f(0.9f, 0.9f, 0.9f);
    for(int i=0; i<360; i+=30) {
        float rad = i * M_PI / 180.0f;
        glBegin(GL_LINES);
        glVertex3f(sin(rad)*2.9f, 0, cos(rad)*2.9f);
        glVertex3f(sin(rad)*3.1f, 0, cos(rad)*3.1f);
        glEnd();
        
        if (i % 90 == 0) {
            float lx = sin(rad)*3.3f;
            float lz = cos(rad)*3.3f;
            const char* nsew[] = {"N", "E", "S", "W"}; /* Simplified N=0, E=90, S=180, W=270 */
            drawText3D(lx, 0.1f, lz, nsew[i/90]);
        }
    }
}

void drawMarkArc() {
    /* 3. Mark Arc (Frontal Vertical plane -90 to +90) */
    glColor4f(1.0f, 1.0f, 0.0f, 0.2f);
    glBegin(GL_LINE_STRIP);
    for(int i=-90; i<=90; i+=5) {
        float rad = i * M_PI / 180.0f;
        float vy = sin(rad) * 2.5f;
        float vz = cos(rad) * 2.5f;
        glVertex3f(0, vy, vz);
    }
    glEnd();

    /* 4. Mark Labels (Only valid tactical range) */
    glColor3f(0.8f, 0.8f, 0.0f);
    int marks[] = {-90, -45, 0, 45, 90};
    for(int i=0; i<5; i++) {
        float rad = marks[i] * M_PI / 180.0f;
        float ly = sin(rad) * 2.8f;
        float lz = cos(rad) * 2.8f;
        char buf[16]; sprintf(buf, "M:%+d", marks[i]);
        drawText3D(0, ly, lz, buf);
    }
}

void drawCompass() {
    glDisable(GL_LIGHTING);
    drawHeadingRing();
    glPushMatrix();
    glRotatef(objects[0].h, 0, 1, 0);
    drawMarkArc();
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawGlow(float radius, float r, float g, float b, float alpha) {
    if (g_is_cloaked_rendering) return;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);
    for (int i = 1; i <= 5; i++) {
        float s = radius * (1.0f + i * 0.2f);
        glColor4f(r, g, b, alpha / (i * 1.5f));
        glutSolidSphere(s, 16, 16);
    }
    glPopAttrib();
}

void glutSolidSphere_wrapper_module() { glutSolidSphere(0.5, 64, 64); }
void glutSolidSphere_wrapper_hull() { glutSolidSphere(0.15, 48, 48); }
void glutSolidSphere_wrapper_escort() { glutSolidSphere(0.35, 48, 48); }
void glutSolidCube_wrapper() { glutSolidCube(0.5); }

void drawHullDetail(void (*drawFunc)(void), float r, float g, float b) {
    glShadeModel(GL_SMOOTH);
    glColor3f(r, g, b); 
    drawFunc();
    /* Wireframe overlay removed to ensure homogeneous surface */
}

void drawNacelle(float len, float width, float r, float g, float b) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    /* Nacelle Body (Shaded - High tessellation for smooth look) */
    glPushMatrix(); 
    glScalef(len, width, width); 
    glColor3f(0.45f, 0.45f, 0.5f); 
    glutSolidSphere(0.1, 48, 32); 
    glPopMatrix();

    if (g_is_cloaked_rendering) {
        glPopAttrib();
        return;
    }

    /* Blue Hyperdrive Field Grille (Emissive) */
    GLfloat nac_emit[] = {r*0.7f, g*0.7f, b*0.7f, 1.0f};
    GLfloat no_emit[] = {0.0f, 0.0f, 0.0f, 1.0f};
    glPushMatrix(); 
    glTranslatef(-0.05f * len, 0, 0); 
    glScalef(len*0.6f, width*1.1f, width*1.1f);
    glMaterialfv(GL_FRONT, GL_EMISSION, nac_emit);
    glColor3f(r, g, b);
    glutSolidSphere(0.08, 12, 12);
    glMaterialfv(GL_FRONT, GL_EMISSION, no_emit);
    glPopMatrix();

    /* Bussard Collector (Front Red Tip) - UNLIT for maximum brightness */
    glDisable(GL_LIGHTING);
    glPushMatrix(); 
    glTranslatef(0.1f * len, 0, 0); 
    glColor3f(1.0f, 0.0f, 0.0f); /* Solid Bright Red */
    glutSolidSphere(0.05, 16, 16); 
    /* Extra Glow Layer */
    glEnable(GL_BLEND);
    glColor4f(1.0f, 0.2f, 0.0f, 0.4f);
    glutSolidSphere(0.07, 12, 12);
    glPopMatrix();
    glPopAttrib();
}

void drawDeflector(float r, float g, float b) {
    if (g_is_cloaked_rendering) return;
    glDisable(GL_LIGHTING); glColor3f(r, g, b); glutSolidSphere(0.12, 16, 16); glEnable(GL_LIGHTING);
}

void drawCommandModule(float sx, float sy, float sz) {
    glPushMatrix(); glScalef(sx, sy, sz); drawHullDetail(glutSolidSphere_wrapper_module, 0.88f, 0.88f, 0.92f); glPopMatrix();
    
    if (g_is_cloaked_rendering) return;

    /* Luci di posizione superiori */
    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 0.0f, 0.0f); /* Rosso (Port) */
    glPushMatrix(); glTranslatef(0, 0.12f, 0.2f); glutSolidSphere(0.02, 8, 8); glPopMatrix();
    glColor3f(0.0f, 1.0f, 0.0f); /* Verde (Starboard) */
    glPushMatrix(); glTranslatef(0, 0.12f, -0.2f); glutSolidSphere(0.02, 8, 8); glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawLegacy() {
    glShadeModel(GL_SMOOTH);
    glEnable(GL_LIGHTING);

    /* --- [ NOMENCLATURE: LEGACY CLASS ] --- */

    /* 1. Primary Hull (Command Saucer) */
    drawCommandModule(1.0f, 0.15f, 1.0f);
    
    /* 2. Bridge Module (Tactical Hub) */
    glDisable(GL_LIGHTING); glColor3f(0.0f, 0.5f, 1.0f); glPushMatrix(); glTranslatef(0, 0.1f, 0); glutSolidSphere(0.08, 12, 12); drawGlow(0.06, 0, 0.5, 1, 0.4); glPopMatrix(); glEnable(GL_LIGHTING);
    
    /* 3. Secondary Hull (Engineering Section) - Extended & Widened & Lengthened */
    glPushMatrix(); 
    glTranslatef(-0.6f, 0.0f, 0); 
    glRotatef(180, 0, 1, 0);
    glScalef(2.16f, 1.2f, 1.2f); /* Lengthened by 20% (1.8 -> 2.16) */
    drawHullDetail(glutSolidSphere_wrapper_hull, 0.8f, 0.8f, 0.85f); 
    glPopMatrix();

    /* 3.5 Stabilizing Rudder (Aetherium Fin) - Vertical & Rear-Mounted */
    glPushMatrix();
    /* Lowered position by 20% (0.15f -> 0.12f) */
    glTranslatef(-0.9f, 0.12f, 0); 
    glScalef(0.65f, 0.45f, 0.03f); 
    glColor3f(0.8f, 0.8f, 0.85f);
    glutSolidSphere(0.5, 32, 16);
    glPopMatrix();

    /* 4. Main Deflector Array (Tail End) - Moved 10% closer */
    glPushMatrix(); 
    glTranslatef(-1.44f, 0.0f, 0); 
    glScalef(0.25f, 0.25f, 0.25f); /* Applied 50% reduction to the previous 0.5f scale */
    drawDeflector(1.0f, 0.4f, 0.0f); 
    drawGlow(0.1f, 1.0f, 0.3f, 0.0f, 0.5f); 
    
    /* Futuristic Propulsion Rings (3 Rotating Rings - Removed 4th) */
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for(int r=0; r<3; r++) {
        glPushMatrix();
        /* Each ring rotates at a different speed */
        glRotatef(pulse * (40.0f + r * 15.0f), 1, 0, 0); 
        glRotatef(pulse * 10.0f, 0, 1, 0);
        glColor4f(0.0f, 0.6f, 1.0f, 0.6f - (r * 0.1f));
        /* Thin wire torus for the ring effect */
        glutWireTorus(0.01, 0.35 + (r * 0.12), 8, 32);
        glPopMatrix();
    }
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    glPopMatrix();

    for(int side=-1; side<=1; side+=2) {
        /* 5. Structural Pylons (Support Arms) - Position Unchanged */
        glPushMatrix(); 
        /* Shifted backward to -0.918f, nacelles remain at -1.275f */
        glTranslatef(-0.918f, 0.0f, side * 0.30f); 
        glRotatef(side*90, 1, 0, 0); 
        glScalef(0.38f, 0.76f, 0.076f); 
        glColor3f(0.8f, 0.8f, 0.85f); 
        glutSolidSphere(0.5, 24, 16); 
        glPopMatrix();

        /* 6. Hyperdrive Nacelles (FTL Units) - Moved 20% forward */
        glPushMatrix(); 
        /* Shifted forward to -1.02f, pylons remain at -0.918f */
        glTranslatef(-1.02f, 0.0f, side * 0.65f); 
        drawNacelle(4.8, 0.28, 0.2, 0.5, 1.0); 
        glPopMatrix();
    }
}

void drawScout() { 
    drawCommandModule(1.2f, 0.18f, 1.1f);
    glPushMatrix(); glTranslatef(-0.1f, 0.0f, 0); glScalef(0.3f, 0.5f, 0.9f); glColor3f(0.75f, 0.75f, 0.8f); glutSolidCube(0.5); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.1f, 0.0f, side * 0.3f); glScalef(0.1f, 0.4f, 0.1f); glutSolidCube(1.0); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.2f, 0.0f, side * 0.45f); drawNacelle(3.8, 0.4, 0.3, 0.4, 0.8); glPopMatrix();
    }
}

void drawHeavyCruiser() { 
    drawCommandModule(1.4f, 0.12f, 1.3f);
    
    /* Secondary Hull - Integrated */
    glPushMatrix(); 
    glTranslatef(-0.5f, 0.0f, 0); 
    glRotatef(180, 0, 1, 0);
    glScalef(2.8f, 0.7f, 0.7f); 
    drawHullDetail(glutSolidSphere_wrapper_hull, 0.8f, 0.8f, 0.85f); 
    glPopMatrix();

    /* Deflector Dish */
    glPushMatrix(); 
    glTranslatef(-1.0f, 0.0f, 0); 
    drawDeflector(0.0f, 0.6f, 1.0f); 
    drawGlow(0.1f, 0.2f, 0.7f, 1.0f, 0.4f); 
    glPopMatrix();

    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.5f, 0.0f, side * 0.35f); drawNacelle(5.5, 0.25, 0.2, 0.6, 1.0); glPopMatrix();
    }
}

void drawMultiEngine() {
    glPushMatrix(); glScalef(1.2f, 0.4f, 0.9f); drawHullDetail(glutSolidCube_wrapper, 0.75f, 0.75f, 0.8f); glPopMatrix();
    for(int updown=-1; updown<=1; updown+=2) {
        for(int side=-1; side<=1; side+=2) {
            glPushMatrix(); glTranslatef(-0.1f, updown * 0.1f, side * 0.35f); drawNacelle(3.5, 0.3, 0.5, 0.5, 0.6); glPopMatrix();
        }
    }
}

void drawEscort() {
    glPushMatrix(); glTranslatef(0.1f, 0, 0); glScalef(0.5f, 0.4f, 0.4f); drawDeflector(1.0f, 0.3f, 0.0f); drawGlow(0.15f, 1.0f, 0.2f, 0.0f, 0.4f); glPopMatrix();
    glPushMatrix(); glScalef(1.5f, 0.5f, 1.8f); drawHullDetail(glutSolidSphere_wrapper_escort, 0.6f, 0.6f, 0.65f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.1f, 0.0f, side * 0.4f); drawNacelle(2.5, 0.6, 0.2, 0.3, 0.7); glPopMatrix();
    }
}

void drawExplorer() {
    /* --- [ NOMENCLATURE: EXPLORER CLASS ] --- */

    /* 1. Primary Hull (Command Saucer) - Main Command Deck */
    glPushMatrix();
    drawCommandModule(1.6f, 0.15f, 2.4f);
    
    if (!g_is_cloaked_rendering) {
        /* 2. Bridge Module (Command Hub) */
        glDisable(GL_LIGHTING);
        glColor3f(1.0f, 1.0f, 1.0f);
        glPushMatrix(); glTranslatef(0, 0.15f, 0); glScalef(0.3f, 0.1f, 0.2f); glutSolidSphere(0.5, 12, 12); glPopMatrix();
        
        /* 3. External Window Grid */
        glColor3f(1.0f, 1.0f, 0.8f);
        for(int i=0; i<360; i+=30) {
            glPushMatrix();
            glRotatef(i, 0, 1, 0);
            glTranslatef(1.5f, 0, 0);
            glutSolidSphere(0.015, 4, 4);
            glPopMatrix();
        }

        /* 4. Multi-Spectral Sensor Probes */
        for(int i=0; i<3; i++) {
            glPushMatrix();
            glRotatef(pulse * 30.0f + (i * 120), 0, 1, 0); 
            glRotatef(30.0f, 1, 0, 1);
            glTranslatef(2.2f, 0, 0);
            glColor3f(0.0f, 0.7f, 1.0f); 
            glutSolidSphere(0.03, 8, 8);
            glPopMatrix();
        }
        glEnable(GL_LIGHTING);
    }
    glPopMatrix();

    /* 5. Structural Neck (REMOVED for integrated design) */

    /* 6. Secondary Hull (Engineering) - Integrated & Inverted & Widened & Lengthened */
    glPushMatrix();
    glTranslatef(-0.3f, 0.0f, 0); 
    glRotatef(180, 0, 1, 0); 
    glScalef(2.64f, 1.2f, 1.35f); /* Lengthened by 20% (2.2 -> 2.64) */
    drawHullDetail(glutSolidSphere_wrapper_hull, 0.85f, 0.85f, 0.9f);
    glPopMatrix();

    /* 7. Advanced Main Deflector Dish */
    if (!g_is_cloaked_rendering) {
        glPushMatrix();
        glTranslatef(-0.7f, 0.0f, 0); 
        glRotatef(-90, 0, 1, 0);
        glRotatef(90, 0, 0, 1); 
        
        /* Deflector Housing */
        glColor3f(0.6f, 0.4f, 0.2f);
        glutSolidTorus(0.05, 0.25, 8, 24);
        
        /* Resonance Dish */
        glPushMatrix();
        glScalef(0.1f, 1.0f, 1.0f);
        glColor3f(0.0f, 0.2f, 0.5f);
        glutSolidSphere(0.22, 16, 16);
        glPopMatrix();

        /* Deflector Core (GDIS Data Stream) */
        glDisable(GL_LIGHTING);
        float pulse_glow = 0.8f + sin(pulse*3.0f)*0.2f;
        glColor3f(0.0f, 0.6f * pulse_glow, 1.0f * pulse_glow);
        glutSolidSphere(0.08, 12, 12);
        glEnable(GL_LIGHTING);
        glPopMatrix();
    }

    /* 8. Nacelle Pylons (Structural Support) */
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix();
        glTranslatef(-0.3f, 0.0f, side * 0.6f);
        glRotatef(side * 90, 1, 0, 0); 
        glScalef(0.1f, 0.6f, 0.02f); 
        glColor3f(0.8f, 0.8f, 0.82f);
        glutSolidCube(1.0);
        glPopMatrix();

        /* 9. Hyperdrive Nacelles (FTL Propulsion) */
        glPushMatrix();
        glTranslatef(-0.3f, 0.0f, side * 1.2f);
        drawNacelle(4.5, 0.35, 0.2, 0.6, 1.0);
        glPopMatrix();
    }
}

void drawFlagship() {
    drawCommandModule(2.2f, 0.12f, 1.3f);
    glPushMatrix(); glTranslatef(-0.5f, 0.0f, 0); glRotatef(180, 0, 1, 0); glScalef(2.5f, 0.5f, 0.6f); drawHullDetail(glutSolidSphere_wrapper_hull, 0.9f, 0.9f, 0.95f); glPopMatrix();
    glPushMatrix(); glTranslatef(-1.0f, 0.0f, 0); drawDeflector(0.0f, 0.4f, 0.8f); drawGlow(0.1f, 0.2f, 0.6f, 1.0f, 0.4f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.5f, 0.0f, side * 0.45f); drawNacelle(6.0, 0.2, 0.2, 0.5, 1.0); glPopMatrix();
    }
}

void drawScience() {
    drawCommandModule(2.0f, 0.15f, 1.0f);
    glPushMatrix(); glTranslatef(-0.4f, 0.0f, 0); glRotatef(180, 0, 1, 0); glScalef(1.8f, 0.4f, 0.5f); drawHullDetail(glutSolidSphere_wrapper_hull, 0.85f, 0.85f, 0.95f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.8f, 0.0f, 0); drawDeflector(0.0f, 0.5f, 0.9f); drawGlow(0.1f, 0.3f, 0.7f, 1.0f, 0.4f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.4f, 0.0f, side * 0.4f); glRotatef(side*90, 1, 0, 0);
        drawNacelle(3.5, 0.25, 0.3, 0.6, 1.0); glPopMatrix();
    }
}

void drawCarrier() {
    glColor3f(0.6f, 0.6f, 0.7f); glPushMatrix(); glScalef(1.4f, 0.2f, 1.8f); drawHullDetail(glutSolidSphere_wrapper_module, 0.6f, 0.6f, 0.7f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.6f, -0.2f, side * 0.7f); drawNacelle(4.5, 0.4, 0.4, 0.4, 0.8); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.2f, -0.1f, side * 0.5f); glRotatef(side*30, 1,0,0); glScalef(0.2f, 0.6f, 0.2f); glutSolidCube(1.0); glPopMatrix();
    }
}

void drawTactical() {
    drawExplorer();
    glPushMatrix(); glTranslatef(-0.6f, 0.4f, 0); glColor3f(0.7f, 0.7f, 0.75f);
    glPushMatrix(); glScalef(0.8f, 0.15f, 0.8f); glutSolidSphere(0.5, 24, 24); glPopMatrix();
    glPushMatrix(); glTranslatef(0, -0.2f, 0); glScalef(0.1f, 0.4f, 0.4f); glutSolidCube(1.0); glPopMatrix();
    glPopMatrix();
}

void drawDiplomatic() {
    drawCommandModule(1.4f, 0.2f, 1.4f);
    glPushMatrix(); glTranslatef(-0.45f, -0.25f, 0); glScalef(1.6f, 0.8f, 0.8f); drawHullDetail(glutSolidSphere_wrapper_hull, 0.8f, 0.8f, 0.85f); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.15f, -0.25f, 0); drawDeflector(0.0f, 0.3f, 0.7f); drawGlow(0.1f, 0.2f, 0.5f, 1.0f, 0.4f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.7f, 0.05f, side * 0.45f); drawNacelle(4.5, 0.35, 0.3, 0.4, 0.9); glPopMatrix();
    }
}

void drawResearch() {
    glColor3f(0.9f, 0.9f, 0.9f); glPushMatrix(); glScalef(1.1f, 0.15f, 0.9f); drawHullDetail(glutSolidSphere_wrapper_module, 0.9f, 0.9f, 0.9f); glPopMatrix();
    glPushMatrix(); glTranslatef(0, -0.5f, 0); glScalef(1.2f, 0.3f, 0.6f); glutSolidSphere(0.2, 12, 12); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(0, -0.25f, side * 0.35f); glScalef(0.15f, 0.6f, 0.1f); glutSolidCube(1.0); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.35f, -0.25f, side * 0.4f); drawNacelle(2.5, 0.2, 0.4, 0.4, 0.7); glPopMatrix();
    }
}

void drawFrigate() {
    glColor3f(0.6f, 0.6f, 0.65f); glPushMatrix(); glScalef(1.6f, 0.25f, 1.5f); drawHullDetail(glutSolidSphere_wrapper_module, 0.6f, 0.6f, 0.65f); glPopMatrix();
    for(int side=-1; side<=1; side+=2) {
        glPushMatrix(); glTranslatef(-0.7f, -0.05f, side * 0.55f); drawNacelle(3.5, 0.3, 0.2, 0.3, 0.6); glPopMatrix();
        glPushMatrix(); glTranslatef(-0.8f, 0, 0); glScalef(0.1f, 0.1f, 1.0f); glutSolidCube(1.0); glPopMatrix();
    }
}

void drawAllianceShip(int class, float h, float m) {
    if (!g_is_cloaked_rendering) glUseProgram(hullShaderProgram);
    glRotatef(h - 90.0f, 0, 1, 0);
    glRotatef(m, 0, 0, 1);
    switch(class) {
        case SHIP_CLASS_LEGACY: drawLegacy(); break;
        case SHIP_CLASS_SCOUT:      drawScout(); break;
        case SHIP_CLASS_HEAVY_CRUISER:    drawHeavyCruiser(); break;
        case SHIP_CLASS_MULTI_ENGINE: drawMultiEngine(); break;
        case SHIP_CLASS_ESCORT:      drawEscort(); break;
        case SHIP_CLASS_EXPLORER:       drawExplorer(); break;
        case SHIP_CLASS_FLAGSHIP:    drawFlagship(); break;
        case SHIP_CLASS_SCIENCE:     drawScience(); break;
        case SHIP_CLASS_CARRIER:        drawCarrier(); break;
        case SHIP_CLASS_TACTICAL:       drawTactical(); break;
        case SHIP_CLASS_DIPLOMATIC:   drawDiplomatic(); break;
        case SHIP_CLASS_RESEARCH:       drawResearch(); break;
        case SHIP_CLASS_FRIGATE:  drawFrigate(); break;
        default:                      drawLegacy(); break;
    }
    glUseProgram(0);
}

void drawKorthian(float x, float y, float z) {
    glPushMatrix(); glColor3f(0.6f, 0.1f, 0.0f); glScalef(1.0f, 0.3f, 1.5f); glutSolidSphere(0.3, 16, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(0.4f, 0, 0); glScalef(2.0f, 0.2f, 0.2f); glutSolidSphere(0.15, 8, 8); glPopMatrix();
    glColor3f(0.8f, 0.0f, 0.0f); glPushMatrix(); glTranslatef(0.7f, 0, 0); glScalef(1.0f, 0.5f, 1.2f); glutSolidSphere(0.15, 12, 12); glPopMatrix();
}

void drawXylari(float x, float y, float z) {
    glColor3f(0.0f, 0.5f, 0.0f); glPushMatrix(); glScalef(1.5f, 0.2f, 1.0f); glutSolidSphere(0.4, 16, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(0, 0.25f, 0); glScalef(1.5f, 0.2f, 0.8f); glutSolidSphere(0.35, 16, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(0.4f, 0.1f, 0); glScalef(1.0f, 0.5f, 0.2f); glutSolidCube(0.3); glPopMatrix();
    glColor3f(0.0f, 0.7f, 0.2f); glPushMatrix(); glTranslatef(0.7f, 0.1f, 0); glutSolidCone(0.1, 0.3, 8, 8); glPopMatrix();
}

void drawSwarm(float x, float y, float z) {
    glRotatef(pulse*5, 1, 1, 1); glColor3f(0.15f, 0.15f, 0.15f); glutWireCube(0.85);
    glColor3f(0.05f, 0.05f, 0.05f); glutSolidCube(0.75); glDisable(GL_LIGHTING);
    float p = (sin(pulse)+1.0f)*0.5f; glColor4f(0.0f, 0.8f * p, 0.0f, 0.6f); glutWireCube(0.8);
    for(int i=0; i<6; i++) {
        glPushMatrix(); if(i==0) glTranslatef(0.38,0,0); else if(i==1) glTranslatef(-0.38,0,0); else if(i==2) glTranslatef(0,0.38,0); else if(i==3) glTranslatef(0,-0.38,0); else if(i==4) glTranslatef(0,0,0.38); else if(i==5) glTranslatef(0,0,-0.38);
        glColor3f(0, 1, 0); glutSolidSphere(0.04, 8, 8); drawGlow(0.03, 0, 1, 0, 0.4); glPopMatrix();
    }
    glEnable(GL_LIGHTING);
}

void drawVesperian(float x, float y, float z) {
    glColor3f(0.6f, 0.5f, 0.3f); glPushMatrix(); glScalef(2.0f, 0.2f, 1.2f); glutSolidSphere(0.4, 16, 16); glPopMatrix();
    glColor3f(0.8f, 0.7f, 0.2f); glPushMatrix(); glTranslatef(0.5f, 0, 0); glScalef(1.0f, 0.4f, 0.4f); glutSolidSphere(0.2, 12, 12); glPopMatrix();
}

void drawAscendant(float x, float y, float z) {
    glColor3f(0.4f, 0.4f, 0.6f); glPushMatrix(); glScalef(1.2f, 0.5f, 1.0f); glutSolidSphere(0.35, 12, 12); glPopMatrix();
    glPushMatrix(); glTranslatef(0.4f, 0, 0.15f); glutSolidCone(0.05, 0.3, 8, 8); glPopMatrix();
    glPushMatrix(); glTranslatef(0.4f, 0, -0.15f); glutSolidCone(0.05, 0.3, 8, 8); glPopMatrix();
}

void drawQuarzite(float x, float y, float z) {
    glRotatef(pulse*15, 0, 1, 0); glColor4f(1.0f, 0.5f, 0.0f, 0.6f); glDisable(GL_LIGHTING); glutWireOctahedron();
    glColor4f(1.0f, 0.2f, 0.0f, 0.4f); glutSolidOctahedron(); glEnable(GL_LIGHTING);
}

void drawSaurian(float x, float y, float z) {
    glColor3f(0.3f, 0.4f, 0.1f); glPushMatrix(); glScalef(1.5f, 0.6f, 0.6f); glutSolidCube(0.4); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.3f, 0, 0); glScalef(0.5f, 1.2f, 1.5f); glutSolidCube(0.3); glPopMatrix();
}

void drawGilded(float x, float y, float z) {
    glColor3f(0.7f, 0.3f, 0.1f); glPushMatrix(); glScalef(1.0f, 0.2f, 2.0f); glutSolidSphere(0.4, 16, 16); glPopMatrix();
    glPushMatrix(); glTranslatef(0.3f, 0, 0); glScalef(1.2f, 0.4f, 0.6f); glutSolidSphere(0.3, 12, 12); glPopMatrix();
}

void drawFluidicVoid(float x, float y, float z) {
    glRotatef(pulse*10, 1, 0, 1); glColor3f(0.8f, 0.8f, 0.2f); for(int i=0; i<3; i++) { glPushMatrix(); glRotatef(i*120, 0, 1, 0); glTranslatef(0.3f, 0, 0); glScalef(2.0f, 0.3f, 0.3f); glutSolidSphere(0.15, 12, 12); glPopMatrix(); }
    glutSolidSphere(0.2, 12, 12);
}

void drawCryos(float x, float y, float z) {
    glColor3f(0.4f, 0.5f, 0.4f); glPushMatrix(); glScalef(1.8f, 0.2f, 0.8f); glutSolidCube(0.4); glPopMatrix();
    glPushMatrix(); glTranslatef(0.2f, 0.1f, 0.2f); glScalef(0.5f, 0.5f, 1.2f); glutSolidSphere(0.2, 8, 8); glPopMatrix();
}

void drawApex(float x, float y, float z) {
    glColor3f(0.5f, 0.5f, 0.5f); glPushMatrix(); glScalef(2.5f, 0.15f, 0.4f); glutSolidSphere(0.35, 12, 12); glPopMatrix();
    glPushMatrix(); glTranslatef(-0.4f, 0, 0); glScalef(0.5f, 0.8f, 1.5f); glutSolidCube(0.2); glPopMatrix();
}

void drawStarbase(float x, float y, float z) {
    glRotatef(pulse*10, 0, 1, 0); glColor3f(0.9f, 0.9f, 0.1f); glutWireSphere(0.4, 12, 12);
    glColor3f(0.5f, 0.5f, 0.5f); glPushMatrix(); glScalef(1.5f, 0.1f, 1.5f); glutSolidCube(0.6); glPopMatrix();
}

void drawStar(float x, float y, float z, int id) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);

    /* Spectral Class Color based on ID */
    float r = 1.0f, g = 1.0f, b = 1.0f;
    int type = id % 7;
    if (type == 0) { r=0.4f; g=0.6f; b=1.0f; }      /* Class O - Blue */
    else if (type == 1) { r=0.7f; g=0.8f; b=1.0f; } /* Class B - Light Blue */
    else if (type == 2) { r=1.0f; g=1.0f; b=1.0f; } /* Class A - White */
    else if (type == 3) { r=1.0f; g=1.0f; b=0.7f; } /* Class F - Yellow-White */
    else if (type == 4) { r=1.0f; g=0.9f; b=0.1f; } /* Class G - Yellow (Sol) */
    else if (type == 5) { r=1.0f; g=0.6f; b=0.2f; } /* Class K - Orange */
                        else { r=1.0f; g=0.2f; b=0.1f; }                /* Class H - Red */
    /* 3. Core */
    float core_size = 0.38f;
    
    /* Violent pulsation if this is the supernova star */
    /* Check using the world coordinates passed to the function AND global quadrant */
    if (g_sn_pos.active && g_sn_q[0] == g_my_q[0] && g_sn_q[1] == g_my_q[1] && g_sn_q[2] == g_my_q[2] &&
        fabs(x - g_sn_pos.x) < 0.1f && fabs(y - g_sn_pos.y) < 0.1f && fabs(z - g_sn_pos.z) < 0.1f) {
        float sn_factor = 1.0f - (g_sn_pos.timer / 1800.0f); /* 0 to 1 as it nears explosion */
        core_size += sn_factor * 0.4f * (sin(pulse*30.0f)*0.5f + 0.5f);
        glColor3f(1.0f, 1.0f, 1.0f); /* Turning white hot */
    } else {
        glColor3f(r, g, b);
    }
    glutSolidSphere(core_size, 32, 32);

    /* 2. ATMOSPHERIC LAYERS (Corona): Semi-transparent and glowing */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); /* Additive glow */
    glDepthMask(GL_FALSE); /* Aura doesn't block depth */

    for(int i=0; i<3; i++) {
        glPushMatrix();
        glRotatef(pulse * (10.0f + i*5.0f), 0, 1, 0);
        glRotatef(pulse * (5.0f + i*2.0f), 1, 0, 0);
        float s = 0.4f + i*0.12f + sin(pulse*3.0f + i)*0.03f;
        glColor4f(r, g, b, 0.3f / (i+1));
        glutSolidSphere(s, 16, 16);
        glPopMatrix();
    }
    
    /* 3. Solar Flares (Prominences) */
    glLineWidth(1.0f);
    for(int i=0; i<8; i++) {
        glPushMatrix();
        glRotatef(i*45 + pulse*20, 0, 1, 0);
        glRotatef(sin(pulse + i)*30, 0, 0, 1);
        float flare_len = 0.45f + sin(pulse*5.0f + i)*0.2f;
        glBegin(GL_LINES);
        glColor4f(r, g, b, 0.7f);
        glVertex3f(0.3f, 0, 0);
        glColor4f(r, g, b, 0.0f);
        glVertex3f(flare_len, 0, 0);
        glEnd();
        glPopMatrix();
    }

    glPopAttrib();
}

void drawPlanet(float x, float y, float z) {
    GLfloat spec[] = {0.3f, 0.3f, 0.3f, 1.0f};
    glMaterialfv(GL_FRONT, GL_SPECULAR, spec);
    glMateriali(GL_FRONT, GL_SHININESS, 50);
    
    glColor3f(0.2f, 0.6f, 1.0f);
    glutSolidSphere(0.3, 24, 24);
    
    /* Atmosphere */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, 0.2f);
    glutSolidSphere(0.32, 24, 24);
    glDisable(GL_BLEND);
}

void drawWormhole(float x, float y, float z, float h, float m, int type) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(h - 90.0f, 0, 1, 0);
    glRotatef(m, 0, 0, 1);
    glRotatef(90, 0, 1, 0); /* Align longitudinal axis with ship forward */

    /* 1. Draw the central 3D sphere with material properties */
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    
    if (type == 0) {
        /* DEPARTURE: Dark charcoal with metallic specularity */
        float mat_amb[] = {0.0f, 0.0f, 0.0f, 1.0f};
        float mat_diff[] = {0.02f, 0.02f, 0.02f, 1.0f};
        float mat_spec[] = {0.6f, 0.6f, 0.6f, 1.0f};
        glMaterialfv(GL_FRONT, GL_AMBIENT, mat_amb);
        glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_diff);
        glMaterialfv(GL_FRONT, GL_SPECULAR, mat_spec);
        glMaterialf(GL_FRONT, GL_SHININESS, 100.0f);
        glColor3f(0.05f, 0.05f, 0.05f);
    } else {
        /* ARRIVAL: Brilliant White/Blue Singularity */
        float mat_amb[] = {0.8f, 0.8f, 1.0f, 1.0f};
        float mat_diff[] = {1.0f, 1.0f, 1.0f, 1.0f};
        float mat_spec[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glMaterialfv(GL_FRONT, GL_AMBIENT, mat_amb);
        glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_diff);
        glMaterialfv(GL_FRONT, GL_SPECULAR, mat_spec);
        glMaterialf(GL_FRONT, GL_SHININESS, 128.0f);
        glColor3f(1.0f, 1.0f, 1.0f);
    }
    
    /* Using glutSolidSphere instead of GLUquadric to avoid memory issues */
    glutSolidSphere(0.35f, 32, 32);
    glDisable(GL_LIGHTING);

    /* 2. Activate shader for the external energy cones */
    if (whShaderProgram) {
        glUseProgram(whShaderProgram);
        glUniform1f(glGetUniformLocation(whShaderProgram, "time"), pulse);
    }

    glRotatef(pulse * 25.0f, 0, 0, 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); 
    glDisable(GL_DEPTH_TEST); 
    
    float rs = 0.45f;
    float rmax = 1.6f;
    int Nr = 15;
    int Nt = 30;
    float dr = (rmax - rs) / (float)Nr;
    float dth = 2.0f * M_PI / (float)Nt;

    for (int side = -1; side <= 1; side += 2) {
        for(int i=0; i<Nr; i++){
            float r = rs + (float)i * dr;
            /* zz represents the distance from the center sphere along the axis */
            /* Start at sphere surface (0.35f) and curve outwards */
            float zz = 0.35f + 2.0f * sqrt(rs * (r - rs));
            
            float fade = 1.0f - (float)i/(float)Nr;
            if (type == 0) glColor4f(0.3f*fade, 0.0f, 1.0f*fade, 0.8f*fade); 
            else glColor4f(1.0f*fade, 0.8f*fade, 0.2f*fade, 0.8f*fade);      
            
            glBegin(GL_LINE_LOOP);
            for(int j=0; j<=Nt; j++){
                float th = (float)j * dth;
                glVertex3f(r * cos(th), r * sin(th), (float)side * zz);
            }
            glEnd();
        }
        for(int j=0; j<Nt; j++){
            float th = (float)j * dth;
            glBegin(GL_LINE_STRIP);
            for(int i=0; i<Nr; i++){
                float r = rs + (float)i * dr;
                float zz = 0.35f + 2.0f * sqrt(rs * (r - rs));
                
                float fade = 1.0f - (float)i/(float)Nr;
                if (type == 0) glColor4f(0.2f*fade, 0.0f, 0.6f*fade, 0.6f*fade);
                else glColor4f(0.8f*fade, 0.6f*fade, 0.2f*fade, 0.6f*fade);
                glVertex3f(r * cos(th), r * sin(th), (float)side * zz);
            }
            glEnd();
        }
    }
    glPopAttrib();
    glPopMatrix();
    glUseProgram(0);
}

void drawBlackHole(float x, float y, float z) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE); /* Don't write to depth buffer to avoid square artifacts */
    
    glPushMatrix();
    glTranslatef(x, y, z);
    
    /* Billboard logic: face the camera */
    float m[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, m);
    for(int i=0; i<3; i++) for(int j=0; j<3; j++) {
        if(i==j) m[i*4+j] = 1.0; else m[i*4+j] = 0.0;
    }
    glLoadMatrixf(m);

    glUseProgram(bhShaderProgram);
    glUniform1f(glGetUniformLocation(bhShaderProgram, "time"), pulse);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_scene);
    glUniform1i(glGetUniformLocation(bhShaderProgram, "sceneTex"), 0);

    /* Draw full-size quad for the shader to work on */
    float s = 1.2f;
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex3f(-s, -s, 0);
    glTexCoord2f(1,0); glVertex3f( s, -s, 0);
    glTexCoord2f(1,1); glVertex3f( s,  s, 0);
    glTexCoord2f(0,1); glVertex3f(-s,  s, 0);
    glEnd();

    glUseProgram(0);
    glPopMatrix();
    glPopAttrib();
}

void drawStellarNebula(float x, float y, float z, int type) {
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    /* Multilayered gas cloud */
    for (int i = 0; i < 5; i++) {
        glPushMatrix();
        glRotatef(pulse * (3.0f + i * 1.5f), 0, 1, 0);
        glRotatef(pulse * (2.0f + i), 1, 0, 0);
        
        float scale = 1.0f + i * 0.4f;
        float alpha = 0.25f - (i * 0.04f);
        if (alpha < 0.05f) alpha = 0.05f;
        
        if (type == 1) { /* High-Energy: Yellowish-Orange */
            glColor4f(0.8f + i*0.05f, 0.5f + i*0.1f, 0.1f, alpha);
        } else if (type == 2) { /* Dark Matter: Dark Grey/Black with faint Purple */
            glColor4f(0.1f + i*0.02f, 0.05f, 0.2f + i*0.02f, alpha);
        } else if (type == 3) { /* Ionic: Green-Cyan */
            glColor4f(0.1f, 0.6f + i*0.05f, 0.4f + i*0.1f, alpha);
        } else if (type == 4) { /* Gravimetric: Lime/Yellow-Green */
            glColor4f(0.5f + i*0.1f, 0.7f, 0.1f, alpha);
        } else if (type == 5) { /* Temporal: Magenta/Electric Purple */
            glColor4f(0.8f, 0.1f, 0.8f + i*0.04f, alpha);
        } else { /* Standard (Default): Purple and Blue */
            glColor4f(0.4f + i*0.1f, 0.2f, 0.6f + i*0.05f, alpha);
        }
        
        glScalef(scale, scale * 0.8f, scale * 1.2f);
        glutSolidSphere(1.0f, 16, 16);
        glPopMatrix();
    }
    
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

void drawPulsar(float x, float y, float z) {
    glDisable(GL_LIGHTING);
    /* Core */
    glColor3f(1.0f, 1.0f, 1.0f);
    glutSolidSphere(0.2, 16, 16);
    
    /* Beams */
    glPushMatrix();
    glRotatef(pulse * 100.0f, 0, 1, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_LINES);
    for (int i = -1; i <= 1; i+=2) {
        glColor4f(1.0f, 0.5f, 0.0f, 0.8f);
        glVertex3f(0, 0, 0);
        glColor4f(1.0f, 0.2f, 0.0f, 0.0f);
        glVertex3f(0, 4.0f * i, 0);
    }
    glEnd();
    glDisable(GL_BLEND);
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawComet(float x, float y, float z) {
    glDisable(GL_LIGHTING);
    glPushMatrix();
    
    /* Irregular Nucleus */
    glPushMatrix();
    glRotatef(pulse*5, 1, 0, 1);
    glColor3f(0.8f, 0.8f, 1.0f);
    glScalef(1.2f, 0.8f, 0.9f);
    glutSolidSphere(0.12f, 10, 10);
    glPopMatrix();
    
    /* Coma (Glow) */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(0.4f, 0.6f, 1.0f, 0.5f);
    glutSolidSphere(0.25f, 16, 16);
    
    /* Volumetric Tail (Multiple cones/layers) */
    /* Point tail in negative X direction (opposite to movement) */
    glPushMatrix();
    glRotatef(90, 0, 1, 0); 
    for(int i=0; i<3; i++) {
        glPushMatrix();
        float length = 1.5f + i*0.5f;
        float width = 0.2f + i*0.1f;
        float alpha = 0.4f - i*0.1f;
        glColor4f(0.5f, 0.7f, 1.0f, alpha);
        glRotatef(sin(pulse*2 + i)*5, 1, 0, 0);
        glutSolidCone(width, length, 12, 4);
        glPopMatrix();
    }
    glPopMatrix();
    
    glDisable(GL_BLEND);
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawAsteroid(float x, float y, float z) {
    glUseProgram(hullShaderProgram);
    glPushMatrix();
    glColor3f(0.5f, 0.35f, 0.25f); /* Brownish */
    glRotatef(pulse * 10.0f, 1, 1, 0);
    
    /* Main rocky body (Irregular composition) */
    glPushMatrix();
    glScalef(1.2f, 0.9f, 1.1f);
    glutSolidCube(0.2);
    glPopMatrix();
    
    /* Random bumps for a more 3D asteroid look */
    for(int i=0; i<4; i++) {
        glPushMatrix();
        glRotatef(i*90, 0, 1, 1);
        glTranslatef(0.1f, 0, 0);
        glScalef(0.6f, 0.5f, 0.7f);
        glutSolidCube(0.15);
        glPopMatrix();
    }
    glPopMatrix();
    glUseProgram(0);
}

void drawDerelict(int ship_class) {
    glPushMatrix();
    /* Slow drifting rotation */
    glRotatef(pulse * 2.0f, 0.3, 1.0, 0.2);
    /* Dark hull, no emissive lights */
    glColor3f(0.3f, 0.3f, 0.32f);
    drawAllianceShip(ship_class, 0, 0);
    glPopMatrix();
}

void drawMine(float x, float y, float z) {
    glPushMatrix();
    glRotatef(pulse * 50.0f, 1, 0, 1);
    
    /* Core */
    glColor3f(0.4f, 0.4f, 0.45f);
    glutSolidSphere(0.1f, 8, 8);
    
    /* Spikes */
    glColor3f(0.3f, 0.3f, 0.3f);
    for(int i=0; i<6; i++) {
        glPushMatrix();
        if(i==0) glRotatef(90, 1,0,0);
        else if(i==1) glRotatef(-90, 1,0,0);
        else if(i==2) glRotatef(90, 0,1,0);
        else if(i==3) glRotatef(-90, 0,1,0);
        else if(i==4) glRotatef(90, 0,0,1);
        else if(i==5) glRotatef(-90, 0,0,1);
        glTranslatef(0, 0, 0.15f);
        glutSolidCone(0.02, 0.1, 4, 4);
        glPopMatrix();
    }
    
    /* Pulsing Light */
    float p = 0.5f + sin(pulse*10.0f)*0.5f;
    glDisable(GL_LIGHTING);
    glColor3f(p, 0, 0);
    glutSolidSphere(0.04f, 8, 8);
    glEnable(GL_LIGHTING);
    
    glPopMatrix();
}

void drawBuoy(float x, float y, float z) {
    glPushMatrix();
    /* Main body */
    glColor3f(0.6f, 0.6f, 0.7f);
    glutSolidCube(0.15);
    
    /* Lattice structure */
    glColor3f(0.4f, 0.4f, 0.5f);
    glBegin(GL_LINES);
    glVertex3f(0, 0, 0); glVertex3f(0, 0.5, 0);
    glVertex3f(0, 0.5, 0); glVertex3f(0.2, 0.7, 0);
    glVertex3f(0, 0.5, 0); glVertex3f(-0.2, 0.7, 0);
    glEnd();
    
    /* Rotating Antenna */
    glPushMatrix();
    glTranslatef(0, 0.5, 0);
    glRotatef(pulse * 40.0f, 0, 1, 0);
    glColor3f(0.8f, 0.8f, 0.0f);
    glBegin(GL_TRIANGLES);
    glVertex3f(0, 0, 0); glVertex3f(0.1, 0.2, 0.05); glVertex3f(0.1, 0.2, -0.05);
    glEnd();
    glPopMatrix();
    
    /* Blue Signal Rings (Pulse) */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    float ring_s = fmod(pulse * 0.5f, 1.0f);
    glColor4f(0.0f, 0.5f, 1.0f, 1.0f - ring_s);
    glPushMatrix();
    glRotatef(90, 1, 0, 0);
    glutWireTorus(0.01, ring_s * 0.8f, 8, 20);
    glPopMatrix();
    glDisable(GL_BLEND);
    
    glPopMatrix();
}

void drawPlatform(float x, float y, float z) {
    glPushMatrix();
    glRotatef(pulse * 5.0f, 0, 1, 0);
    
    /* Main Hexagonal Body */
    glColor3f(0.4f, 0.4f, 0.4f);
    for(int i=0; i<3; i++) {
        glPushMatrix();
        glRotatef(i*120, 0, 1, 0);
        glScalef(1.0f, 0.4f, 0.3f);
        glutSolidCube(1.0);
        glPopMatrix();
    }
    
    /* Ion Beam Banks (Top/Bottom) */
    glColor3f(0.2f, 0.2f, 0.2f);
    glPushMatrix(); glTranslatef(0, 0.25, 0); glutSolidCylinder(0.1, 0.1, 12, 2); glPopMatrix();
    glPushMatrix(); glTranslatef(0, -0.35, 0); glutSolidCylinder(0.1, 0.1, 12, 2); glPopMatrix();
    
    /* Energy Core */
    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 0.2f, 0.0f);
    glutSolidSphere(0.15, 12, 12);
    glEnable(GL_LIGHTING);
    
    glPopMatrix();
}

void drawRift(float x, float y, float z) {
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); /* Additive blending */
    glPushMatrix();
    
    /* Rotating energy rings */
    for (int i = 0; i < 5; i++) {
        glPushMatrix();
        glRotatef(pulse * (20.0f + i * 10.0f), 0.2, 1.0, 0.5);
        float r = 0.3f + i * 0.1f;
        glColor4f(0.0f, 0.8f, 1.0f, 0.6f - i * 0.1f);
        glutWireTorus(0.02, r, 8, 24);
        glPopMatrix();
    }
    
    /* Inner crackle */
    glColor4f(1.0f, 1.0f, 1.0f, 0.8f);
    glutSolidSphere(0.1f + sin(pulse*20.0f)*0.02f, 8, 8);
    
    glPopMatrix();
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

void drawMonster(int type, float x, float y, float z) {
    if (type == 30) { /* Crystalline Entity */
        glDisable(GL_LIGHTING);
        glPushMatrix();
        glRotatef(pulse * 20.0f, 1, 1, 1);
        glColor3f(1.0f, 1.0f, 1.0f);
        glutWireIcosahedron();
        for(int i=0; i<4; i++) {
            glPushMatrix();
            glRotatef(i*90, 0, 1, 0);
            glScalef(0.2f, 2.0f, 0.2f);
            glColor4f(0.8f, 0.5f, 1.0f, 0.8f);
            glutSolidCube(1.0);
            glPopMatrix();
        }
        glPopMatrix();
        glEnable(GL_LIGHTING);
    } else if (type == 31) { /* Space Amoeba */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_LIGHTING);
        glPushMatrix();
        float s = 1.0f + sin(pulse*2.0f)*0.1f;
        glScalef(s, s*0.8f, s*1.1f);
        glColor4f(0.0f, 0.6f, 0.2f, 0.6f);
        glutSolidSphere(1.5, 16, 16);
        /* Nucleus */
        glColor4f(0.8f, 0.2f, 0.0f, 0.8f);
        glutSolidSphere(0.4, 8, 8);
        glPopMatrix();
        glDisable(GL_BLEND);
        glEnable(GL_LIGHTING);
    }
}

void drawGalacticCompass() {
    glDisable(GL_LIGHTING);
    glPushMatrix();
    /* Position the compass at the top center, aligned with the vertical axis */
    glTranslatef(0.0f, 9.0f, 0.0f);
    
    float len = 1.5f;
    glLineWidth(2.0f);
    
    /* Z-Axis (0/180 Degrees) - Blue */
    glColor3f(0.3f, 0.3f, 1.0f);
    glBegin(GL_LINES); glVertex3f(0,0,-len); glVertex3f(0,0,len); glEnd();
    drawText3D(0, 0, len + 0.2f, "0");
    drawText3D(0, 0, -len - 0.5f, "180");

    /* X-Axis (90/270 Degrees) - Red */
    glColor3f(1.0f, 0.3f, 0.3f);
    glBegin(GL_LINES); glVertex3f(-len,0,0); glVertex3f(len,0,0); glEnd();
    drawText3D(len + 0.2f, 0, 0, "90");
    drawText3D(-len - 0.5f, 0, 0, "270");

    /* Y-Axis (Mark) - Green */
    glColor3f(0.3f, 1.0f, 0.3f);
    glBegin(GL_LINES); glVertex3f(0,-len,0); glVertex3f(0,len,0); glEnd();
    drawText3D(0, len + 0.2f, 0, "M:+90");
    drawText3D(0, -len - 0.5f, 0, "M:-90");
    
    glColor3f(1, 1, 1);
    glutWireSphere(0.1f, 8, 8);
    
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

void drawExplorerMap() {
    drawGalacticCompass();
    glDisable(GL_LIGHTING);
    float gap = 1.2f;
    float offset = - (10.0f * gap) / 2.0f;

    /* Draw full grid frame */
    glColor4f(0.2f, 0.2f, 0.5f, 0.3f);
    glPushMatrix();
    glTranslatef(0, 0, 0);
    glScalef(10.0f * gap, 10.0f * gap, 10.0f * gap);
    glutWireCube(1.0);
    glPopMatrix();

    /* Draw Vertex Coordinates for orientation */
    glColor3f(0.5f, 0.5f, 0.5f);
    char vbuf[32];
    int v_coords[] = {1, 10};
    for(int vz=0; vz<2; vz++) {
        for(int vy=0; vy<2; vy++) {
            for(int vx=0; vx<2; vx++) {
                int cx = v_coords[vx], cy = v_coords[vy], cz = v_coords[vz];
                float px = offset + cx * gap;
                float py = offset + cz * gap;
                float pz = offset + (11 - cy) * gap; /* Inverted Y for map display consistency */
                sprintf(vbuf, "[%d,%d,%d]", cx, cy, cz);
                drawText3D(px, py + 0.3f, pz, vbuf);
            }
        }
    }

    for(int z=1; z<=10; z++) {
        for(int y=1; y<=10; y++) {
            for(int x=1; x<=10; x++) {
                int64_t val = g_galaxy[x][y][z];
                bool is_my_q = (x == g_my_q[0] && y == g_my_q[1] && z == g_my_q[2]);
                if (val == 0 && !is_my_q) continue;

                float px = offset + x * gap;
                float py = offset + z * gap;
                float pz = offset + (11 - y) * gap;

                /* Use absolute value for digit extraction to handle supernova (negative) quadrants */
                int64_t uval = (val < 0) ? -val : val;

                /* Color coding based on M|U|R|T|B|M|D|A|C|S|Pu|N|BH|P|K|B|S (17 digits) */
                int monster  = (uval / 10000000000000000LL) % 10;
                int player   = (uval / 1000000000000000LL) % 10;
                int rift     = (uval / 100000000000000LL) % 10;
                int platform = (uval / 10000000000000LL) % 10;
                int buoy     = (uval / 1000000000000LL) % 10;
                int mine     = (uval / 100000000000LL) % 10;
                int derelict = (uval / 10000000000LL) % 10;
                int asteroid = (uval / 1000000000LL) % 10;
                int comet = (uval / 100000000LL) % 10;
                int storm = (uval / 10000000LL) % 10;
                int pul   = (uval / 1000000LL) % 10;
                int neb   = (uval / 100000LL) % 10;
                int bh    = (uval / 10000LL) % 10;
                int pl    = (uval / 1000LL) % 10;
                int en    = (uval / 100LL) % 10;
                int bs    = (uval / 10LL) % 10;
                int st    = (uval) % 10;

                /* Apply Filter */
                if (g_map_filter > 0) {
                    bool match = false;
                    switch(g_map_filter) {
                        case 1: if(st>0) match=true; break;
                        case 2: if(pl>0) match=true; break;
                        case 3: if(bs>0) match=true; break;
                        case 4: if(en>0 || player>0) match=true; break;
                        case 5: if(bh>0) match=true; break;
                        case 6: if(neb>0) match=true; break;
                        case 7: if(pul>0) match=true; break;
                        case 8: if(storm>0) match=true; break;
                        case 9: if(comet>0) match=true; break;
                        case 10: if(asteroid>0) match=true; break;
                        case 11: if(derelict>0) match=true; break;
                        case 12: if(mine>0) match=true; break;
                        case 13: if(buoy>0) match=true; break;
                        case 14: if(platform>0) match=true; break;
                        case 15: if(rift>0) match=true; break;
                        case 16: if(monster>0) match=true; break;
                    }
                    if (!match && !is_my_q) continue;
                }

                glPushMatrix();
                glTranslatef(px, py, pz);
                
                if (is_my_q) {
                    /* Highlight current quadrant: Pulsing White + Label */
                    float s_glow = 0.4f + sin(pulse*6.0f)*0.15f;
                    glColor4f(1, 1, 1, 0.8);
                    glutWireCube(s_glow);
                    
                    /* Directional Vector (Heading Needle) - Tensor Style */
                    glDisable(GL_LIGHTING);
                    double r_h = g_shared_state->shm_h * M_PI / 180.0;
                    double r_m = g_shared_state->shm_m * M_PI / 180.0;
                    
                    /* Map axes to screen coordinates */
                    float v_x = sin(r_h) * cos(r_m);
                    float v_y = sin(r_m);
                    float v_z = cos(r_h) * cos(r_m);
                    
                    float line_len = gap * 0.75f;
                    glLineWidth(1.0f);
                    glBegin(GL_LINES);
                    glColor3f(1.0f, 1.0f, 0.0f); /* Bright Yellow Line */
                    glVertex3f(0, 0, 0);
                    glVertex3f(v_x * line_len, v_y * line_len, v_z * line_len);
                    glEnd();
                    
                    /* Pointer head - Red Cone Tip */
                    glPushMatrix();
                    glTranslatef(v_x * line_len, v_y * line_len, v_z * line_len);
                    /* Rotate cone to point in the same direction as the vector */
                    glRotatef(g_shared_state->shm_h, 0, 1, 0);
                    glRotatef(-g_shared_state->shm_m, 1, 0, 0);
                    glColor3f(1.0f, 0.0f, 0.0f); /* Red Pointer */
                    glutSolidCone(0.1, 0.3, 8, 8);
                    glPopMatrix();
                    
                    glLineWidth(1.0f);
                    
                    glColor3f(1, 1, 1);
                    char you_msg[64];
                    sprintf(you_msg, "YOU (H:%03.0f M:%+03.0f)", g_shared_state->shm_h, g_shared_state->shm_m);
                    drawText3D(-0.3f, 0.4f, 0, you_msg);
                    glEnable(GL_LIGHTING);
                }

                if (g_sn_pos.active && x == g_sn_q[0] && y == g_sn_q[1] && z == g_sn_q[2]) {
                    /* Supernova Warning - Red Pulsing (Global Broadcast) */
                    float blink = (sin(pulse * 10.0f) + 1.0f) * 0.5f;
                    glColor4f(1.0f, 0.0f, 0.0f, 0.3f + blink * 0.5f);
                    glutSolidCube(gap * 0.8f);
                }

                if (storm > 0) {
                    /* Ion Storm: Large Transparent White Wireframe Shell - Enhanced visibility */
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glLineWidth(1.0f);
                    glColor4f(1.0f, 1.0f, 1.0f, 0.4f + sin(pulse*4.0f)*0.2f);
                    glutWireCube(0.8f);
                    glLineWidth(1.0f);
                    glDisable(GL_BLEND);
                }

                if (val != 0) {
                    /* If a filter is active, force the color of the filtered object */
                    int effective_filter = g_map_filter;
                    
                    /* If no filter, we use the default priority chain, 
                       otherwise we jump straight to the filtered color */
                    if (effective_filter == 16 || (effective_filter == 0 && monster > 0)) glColor3f(1.0, 1.0, 1.0); /* White - Monster */
                    else if (effective_filter == 15 || (effective_filter == 0 && rift > 0)) glColor3f(0.0, 1.0, 1.0); /* Cyan - Rift */
                    else if (effective_filter == 14 || (effective_filter == 0 && platform > 0)) glColor3f(0.8, 0.4, 0.0); /* Dark Orange - Platform */
                    else if (effective_filter == 13 || (effective_filter == 0 && buoy > 0)) glColor3f(0.0, 0.5, 1.0); /* Blue - Buoy */
                    else if (effective_filter == 12 || (effective_filter == 0 && mine > 0)) glColor3f(1.0, 0.0, 0.0); /* Bright Red - Mine */
                    else if (effective_filter == 11 || (effective_filter == 0 && derelict > 0)) glColor3f(0.3, 0.3, 0.3); /* Dark Grey - Derelict */
                    else if (effective_filter == 10 || (effective_filter == 0 && asteroid > 0)) glColor3f(0.5, 0.3, 0.1); /* Brown - Asteroid */
                    else if (effective_filter == 9 || (effective_filter == 0 && comet > 0)) glColor3f(0.5, 0.8, 1.0); /* Light Blue - Comet */
                    else if (effective_filter == 8 || (effective_filter == 0 && storm > 0)) glColor3f(1.0, 1.0, 1.0); /* White - Ion Storm */
                    else if (effective_filter == 7 || (effective_filter == 0 && pul > 0)) glColor3f(1.0, 0.5, 0); /* Orange - Pulsar */
                    else if (effective_filter == 6 || (effective_filter == 0 && neb > 0)) glColor3f(0.7, 0.7, 0.7); /* Grey - Nebula */
                    else if (effective_filter == 5 || (effective_filter == 0 && bh > 0)) glColor3f(0.6, 0, 1.0); /* Purple - Black Hole */
                    else if (effective_filter == 4 || (effective_filter == 0 && (en > 0 || player > 0))) glColor3f(1, 0, 0); /* Red - Hostile/Vessel */
                    else if (effective_filter == 3 || (effective_filter == 0 && bs > 0)) glColor3f(0, 1, 0); /* Green - Base */
                    else if (effective_filter == 2 || (effective_filter == 0 && pl > 0)) glColor3f(0, 0.8, 1); /* Cyan - Planet */
                    else if (effective_filter == 1 || (effective_filter == 0 && st > 0)) glColor3f(1, 1, 0); /* Yellow - Star */
                    else glColor3f(0.4, 0.4, 0.4); /* Dark gray fallback */
                    
                    float base_s = 0.15f;
                    if (pul > 0) base_s += sin(pulse*8.0f)*0.05f; /* Pulsar heartbeat */
                    if (monster > 0) base_s = 0.25f + sin(pulse*5.0f)*0.05f;
                    if (mine > 0) base_s = 0.1f;
                    if (buoy > 0) base_s = 0.12f;
                    if (platform > 0) base_s = 0.18f;
                    if (rift > 0) base_s = 0.2f;
                    glutSolidCube(base_s);
                }
                glPopMatrix();
            }
        }
    }
    glEnable(GL_LIGHTING);
}

void drawGrid() {
    glDisable(GL_LIGHTING); glColor4f(0.5f, 0.5f, 0.5f, 0.2f);
    if (vbo_grid != 0) { glEnableClientState(GL_VERTEX_ARRAY); glBindBuffer(GL_ARRAY_BUFFER, vbo_grid); glVertexPointer(3, GL_FLOAT, 0, 0); glDrawArrays(GL_LINES, 0, grid_vertex_count); glBindBuffer(GL_ARRAY_BUFFER, 0); glDisableClientState(GL_VERTEX_ARRAY); }
    glEnable(GL_LIGHTING);
}

void drawShipTrail(int obj_idx) {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);
    glLineWidth(1.0f);
    glBegin(GL_LINE_STRIP);
    
    float r=0.4, g=0.7, b=1.0; /* Default Alliance Command Blue */
    if (objects[obj_idx].type == 10) { r=1.0; g=0.1; b=0.0; } /* Korthian Red */
    if (objects[obj_idx].type == 11) { r=0.0; g=1.0; b=0.2; } /* Xylari Green */
    if (objects[obj_idx].type == 12) { r=0.0; g=0.8; b=0.8; } /* Swarm Cyan */

    for (int i = 0; i < objects[obj_idx].trail_count; i++) {
        int idx = (objects[obj_idx].trail_ptr - 1 - i + MAX_TRAIL) % MAX_TRAIL;
        float alpha = (1.0f - (float)i / objects[obj_idx].trail_count) * 0.5f;
        glColor4f(r, g, b, alpha);
        glVertex3f(objects[obj_idx].trail[idx][0], objects[obj_idx].trail[idx][1], objects[obj_idx].trail[idx][2]);
        
        /* Spawn a subtle engine particle occasionally at the latest trail point */
        if (i == 0 && (rand() % 5 == 0)) {
            spawnParticle(objects[obj_idx].x, objects[obj_idx].y, objects[obj_idx].z, 0, 0, 0, r, g, b, 0.5f, 0.3f);
        }
    }
    glEnd();
    glPopAttrib();
}

void drawIonBeams() {
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);
    for (int i = 0; i < 10; i++) {
        if (beams[i].alpha > 0) {
            glLineWidth(3.0f); glColor4f(1.0f, 0.8f, 0.0f, (beams[i].alpha > 1.0f) ? 1.0f : beams[i].alpha);
            glBegin(GL_LINES); 
            glVertex3f(beams[i].sx, beams[i].sy, beams[i].sz); 
            glVertex3f(beams[i].tx, beams[i].ty, beams[i].tz); 
            glEnd();
        }
    }
    glPopAttrib();
}

void drawExplosion() {
    if (g_boom.timer <= 0) return;
    
    /* Spawn particles on first frame of explosion */
    static int last_boom_id = -1;
    if (g_boom.timer >= 38 && last_boom_id != g_shared_state->frame_id) {
        for (int i = 0; i < 100; i++) {
            float vx = ((float)rand() / RAND_MAX - 0.5f) * 0.3f;
            float vy = ((float)rand() / RAND_MAX - 0.5f) * 0.3f;
            float vz = ((float)rand() / RAND_MAX - 0.5f) * 0.3f;
            spawnParticle(g_boom.x, g_boom.y, g_boom.z, vx, vy, vz, 1.0f, 0.7f, 0.2f, 1.5f, 1.0f);
        }
        last_boom_id = g_shared_state->frame_id;
    }
    
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);
    glPushMatrix(); glTranslatef(g_boom.x, g_boom.y, g_boom.z);
    /* Pulsing expansion */
    float s = 1.0f + (40 - g_boom.timer) * 0.15f; /* Doubled size */
    float alpha = g_boom.timer / 40.0f;
    glColor4f(1.0f, 0.6f, 0.1f, alpha); 
    glutSolidSphere(s, 24, 24);
    glPopMatrix(); 
    glPopAttrib();
}

void drawJumpArrival() {
    if (g_jump_arrival.timer <= 0) return;
    
    glPushMatrix();
    glTranslatef(g_jump_arrival.x, g_jump_arrival.y, g_jump_arrival.z);
    
    /* Phase 1: Brilliant White Wormhole */
    float wh_scale = (g_jump_arrival.timer < 60) ? (g_jump_arrival.timer / 60.0f) : 1.0f;
    
    glPushMatrix();
    glScalef(wh_scale, wh_scale, wh_scale);
    drawWormhole(0, 0, 0, g_jump_arrival.h, g_jump_arrival.m, 1); /* Type 1: Arrival (White) */
    glPopMatrix();
    
    /* Phase 2: Ship Emerging (Timer 300 -> 180) */
    if (g_jump_arrival.timer > 180) {
        float progress = 1.0f - (g_jump_arrival.timer - 180) / 120.0f; /* 0.0 at 300, 1.0 at 180 */
        
        glPushMatrix();
        /* Align with ship longitudinal axis (same as wormhole cones) */
        glRotatef(g_jump_arrival.h - 90.0f, 0, 1, 0);
        glRotatef(g_jump_arrival.m, 0, 0, 1);
        glRotatef(90, 0, 1, 0); 
        
        /* Move ship from 0 to 4 units along the axis */
        glTranslatef(0, 0, progress * 4.0f);
        
        /* Render ship with materialization glow */
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        float glow = 1.0f - progress;
        glColor4f(0.8f + glow*0.2f, 0.8f + glow*0.2f, 1.0f, 0.5f + glow*0.5f);
        drawAllianceShip(g_player_class, 0, 0); /* Angles are 0 because we already rotated the matrix */
        glDisable(GL_BLEND);
        glPopMatrix();
    }

    /* Phase 3: Final Flash */
    if (g_jump_arrival.timer < 120) {
        float flash_t = 1.0f - (g_jump_arrival.timer / 120.0f);
        drawGlow(0.5f + flash_t * 5.0f, 1.0f, 1.0f, 1.0f, (1.0f - flash_t) * 0.9f);
    }

    glPopMatrix();
}

/* Personal Probe HUD Data Sync Only (Rendering is now universal via drawObject Type 27) */
void updateProbeHUD() {
    /* No 3D rendering here anymore */
}

void drawTorpedo() {
    if (!g_torp.active) return;
    
    /* Radiant Multicolored Logic */
    static float t_color = 0;
    t_color += 0.2f;
    float r = sin(t_color) * 0.5f + 0.5f;
    float g = sin(t_color + 2.0f) * 0.5f + 0.5f;
    float b = sin(t_color + 4.0f) * 0.5f + 0.5f;
    
    if (rand() % 1 == 0) {
        spawnParticle(g_torp.x, g_torp.y, g_torp.z, 
                      ((float)rand()/RAND_MAX-0.5f)*0.01f, 
                      ((float)rand()/RAND_MAX-0.5f)*0.01f, 
                      ((float)rand()/RAND_MAX-0.5f)*0.01f, 
                      r, g, b, 0.05f, 0.3f);
    }

    glDisable(GL_LIGHTING);
    glPushMatrix(); glTranslatef(g_torp.x, g_torp.y, g_torp.z);
    
    float wave = (sin(pulse * 30.0f) + 1.0f) * 0.5f;
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    /* 1. Central Glow sparkle - Background aura */
    drawGlow(0.08f + wave * 0.04f, r, g, b, 0.5f);

    /* 2. Luminous Star Rays (Flares) - LENGTHENED and THICKER */
    glLineWidth(2.5f);
    for(int axis=0; axis<3; axis++) {
        glPushMatrix();
        if(axis==1) glRotatef(90, 0, 1, 0);
        if(axis==2) glRotatef(90, 1, 0, 0);
        glRotatef(pulse * 250.0f, 0, 0, 1);
        glBegin(GL_LINES);
        for(int j=0; j<4; j++) {
            float ang = j * (M_PI / 2.0f);
            float len = 0.35f + wave * 0.15f; /* Significantly longer rays */
            glColor4f(r, g, b, 1.0f); glVertex3f(0, 0, 0);
            glColor4f(r, g, b, 0.0f); glVertex3f(cos(ang)*len, sin(ang)*len, 0);
        }
        glEnd();
        glPopMatrix();
    }

    /* 3. Solid Geometric Core - WITH LIGHTING */
    glEnable(GL_LIGHTING);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glPushMatrix();
    glScalef(0.04f, 0.04f, 0.04f); /* Increased size for visibility */
    glRotatef(pulse * 400.0f, 1, 1, 1);
    glutSolidIcosahedron(); 
    glPopMatrix();
    glDisable(GL_LIGHTING);

    /* 4. Tiny Black Singularity - Contrast point */
    glColor3f(0.0f, 0.0f, 0.0f);
    glPushMatrix();
    glScalef(0.015f, 0.015f, 0.015f);
    glutSolidSphere(1.0, 8, 8);
    glPopMatrix();

    glDisable(GL_BLEND);
    glPopMatrix(); glEnable(GL_LIGHTING);
}
void drawRecoveryEffect() {
    if (g_recovery_fx.timer <= 0) return;
    
    float x = g_recovery_fx.x;
    float y = g_recovery_fx.y;
    float z = g_recovery_fx.z;
    float t = (float)g_recovery_fx.timer / 60.0f; 
    
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    /* 1. Golden Transporter Beam (Vertical) - Radius reduced to 0.12 */
    float beam_alpha = (t > 0.5f) ? (1.0f - t) * 2.0f : (t * 2.0f);
    glColor4f(1.0f, 0.9f, 0.3f, beam_alpha * 0.7f); 
    
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(90, 1, 0, 0); 
    glTranslatef(0, 0, -2.0f); 
    GLUquadric *q = gluNewQuadric();
    gluCylinder(q, 0.12, 0.12, 4.0, 16, 1);
    gluDeleteQuadric(q);
    glPopMatrix();
    
    /* 2. Swirling Sparkles - Increased visibility and count */
    for(int i=0; i<30; i++) {
        float angle = (pulse * 25.0f) + (i * 12.0f);
        float r = 0.15f; /* Slightly wider than the beam */
        float px = x + cos(angle * M_PI / 180.0f) * r;
        float pz = z + sin(angle * M_PI / 180.0f) * r;
        /* Vertical distribution centered around the probe */
        float py = y - 1.5f + (float)(i % 15) * 0.2f;
        
        glColor4f(1.0f, 1.0f, 1.0f, beam_alpha);
        glPushMatrix();
        glTranslatef(px, py, pz);
        glutSolidSphere(0.025, 4, 4); /* Made them larger */
        glPopMatrix();
    }
    
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    g_recovery_fx.timer--;
}

void drawDismantle() {
    if (g_dismantle.timer <= 0) return;
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDisable(GL_LIGHTING);
    
    float alpha = g_dismantle.timer / 60.0f;
    
    for(int i=0; i<100; i++) {
        if (g_dismantle.particles[i].active) {
            glPushMatrix();
            glTranslatef(g_dismantle.particles[i].x, g_dismantle.particles[i].y, g_dismantle.particles[i].z);
            glColor4f(g_dismantle.particles[i].r, g_dismantle.particles[i].g, g_dismantle.particles[i].b, alpha);
            glutSolidSphere(0.05f * alpha, 8, 8);
            glPopMatrix();
        }
    }
    
    glEnable(GL_LIGHTING);
    glDisable(GL_BLEND);
}

void drawFaceLabels() {
    if (!g_show_hud || g_show_map) return;
    
    int q1 = g_my_q[0];
    int q2 = g_my_q[1];
    int q3 = g_my_q[2];

    struct {
        float x, y, z;
        int nq[3];
    } faces[6] = {
        { 5.5f, 0, 0, {q1+1, q2, q3} },   /* Right (X+) */
        {-5.5f, 0, 0, {q1-1, q2, q3} },   /* Left (X-) */
        { 0, 5.5f, 0, {q1, q2, q3+1} },   /* Top (Z+) -> Y+ in viewer */
        { 0,-5.5f, 0, {q1, q2, q3-1} },   /* Down (Z-) -> Y- in viewer */
        { 0, 0,-5.5f, {q1, q2+1, q3} },   /* Fore (Y+) -> Z- in viewer */
        { 0, 0, 5.5f, {q1, q2-1, q3} }    /* Aft (Y-) -> Z+ in viewer */
    };

    GLdouble model[16], proj[16];
    GLint view[4];
    glGetDoublev(GL_MODELVIEW_MATRIX, model);
    glGetDoublev(GL_PROJECTION_MATRIX, proj);
    glGetIntegerv(GL_VIEWPORT, view);

    for(int i=0; i<6; i++) {
        if (!IS_Q_VALID(faces[i].nq[0], faces[i].nq[1], faces[i].nq[2])) continue;
        
        GLdouble winX, winY, winZ;
        if (gluProject(faces[i].x, faces[i].y, faces[i].z, model, proj, view, &winX, &winY, &winZ) == GL_TRUE) {
            if (winZ < 0.0 || winZ > 1.0) continue;
            
            char buf[32];
            sprintf(buf, "[%d,%d,%d]", faces[i].nq[0], faces[i].nq[1], faces[i].nq[2]);
            
            glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
            gluOrtho2D(0, view[2], 0, view[3]);
            glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
            glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);
            
            glColor3f(0.0f, 0.8f, 0.8f); /* Cyan-ish LCARS */
            glRasterPos2f(winX - 25, winY);
            for(int k=0; k<strlen(buf); k++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, buf[k]);
            
            glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
            glMatrixMode(GL_PROJECTION); glPopMatrix();
            glMatrixMode(GL_MODELVIEW); glPopMatrix();
        }
    }
}

void drawTacticalCube() {
    glDisable(GL_LIGHTING);
    glLineWidth(1.5f);
    
    float min = -5.0f; /* Bottom (Depth -1) */
    float mid = 0.0f;  /* Local (Depth 0) */
    float max = 5.0f;  /* Top (Depth +1) */

    /* 1. TOP PLANE (Depth +1) - GREEN */
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_LINE_LOOP);
    glVertex3f(min, max, min); glVertex3f(max, max, min);
    glVertex3f(max, max, max); glVertex3f(min, max, max);
    glEnd();

    /* 2. BOTTOM PLANE (Depth -1) - RED */
    glColor3f(1.0f, 0.0f, 0.0f);
    glBegin(GL_LINE_LOOP);
    glVertex3f(min, min, min); glVertex3f(max, min, min);
    glVertex3f(max, min, max); glVertex3f(min, min, max);
    glEnd();

    /* 3. VERTICAL CONNECTORS (Gradients Green -> Yellow -> Red) */
    glBegin(GL_LINES);
    for(int i=0; i<4; i++) {
        float x = (i==0 || i==3) ? min : max;
        float z = (i<2) ? min : max;
        
        /* Top -> Mid (Green -> Yellow) */
        glColor3f(0.0f, 1.0f, 0.0f); glVertex3f(x, max, z);
        glColor3f(1.0f, 1.0f, 0.0f); glVertex3f(x, mid, z);
        
        /* Mid -> Bottom (Yellow -> Red) */
        glColor3f(1.0f, 1.0f, 0.0f); glVertex3f(x, mid, z);
        glColor3f(1.0f, 0.0f, 0.0f); glVertex3f(x, min, z);
    }
    glEnd();

    glEnable(GL_LIGHTING);
}

void spawnParticle(float x, float y, float z, float vx, float vy, float vz, float r, float g, float b, float size, float life) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!fx_particles[i].active) {
            fx_particles[i].x = x; fx_particles[i].y = y; fx_particles[i].z = z;
            fx_particles[i].vx = vx; fx_particles[i].vy = vy; fx_particles[i].vz = vz;
            fx_particles[i].r = r; fx_particles[i].g = g; fx_particles[i].b = b;
            fx_particles[i].a = 1.0f;
            fx_particles[i].size = size;
            fx_particles[i].life = life;
            fx_particles[i].active = 1;
            break;
        }
    }
}

void updateParticles() {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (fx_particles[i].active) {
            fx_particles[i].x += fx_particles[i].vx;
            fx_particles[i].y += fx_particles[i].vy;
            fx_particles[i].z += fx_particles[i].vz;
            
            /* Spark-like physics: slight downward drift and less drag than before */
            fx_particles[i].vy -= 0.001f; 
            fx_particles[i].vx *= 0.99f;
            fx_particles[i].vy *= 0.99f;
            fx_particles[i].vz *= 0.99f;

            fx_particles[i].life -= 0.035f; /* Much faster decay */
            fx_particles[i].a = fx_particles[i].life;
            if (fx_particles[i].life <= 0) fx_particles[i].active = 0;
        }
    }
}

void drawParticles() {
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glUseProgram(particleShaderProgram);
    
    glPointSize(5.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (fx_particles[i].active) {
            glColor4f(fx_particles[i].r, fx_particles[i].g, fx_particles[i].b, fx_particles[i].a);
            glVertex3f(fx_particles[i].x, fx_particles[i].y, fx_particles[i].z);
        }
    }
    glEnd();
    
    glUseProgram(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
}

void drawSkybox() {
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glUseProgram(skyboxShaderProgram);
    glUniform1f(glGetUniformLocation(skyboxShaderProgram, "time"), pulse);
    
    glPushMatrix();
    /* Rotate skybox slowly */
    glRotatef(pulse * 0.5f, 0, 1, 0);
    glutSolidSphere(400.0, 32, 32);
    glPopMatrix();
    
    glUseProgram(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
}

void drawShieldEffect() {
    bool any_hit = false;
    for(int s=0; s<6; s++) if (g_shield_hit_timers[s] > 0) any_hit = true;
    if (!any_hit) return;
    
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
    glPushMatrix();
    glTranslatef(PlayerX, PlayerY, PlayerZ);
    /* Rotate shield system to match ship heading/mark */
    glRotatef(objects[0].h - 90.0f, 0, 1, 0);
    glRotatef(objects[0].m, 0, 0, 1);

    /* Shield sectors mapping: 0:F, 1:R, 2:T, 3:B, 4:L, 5:RI */
    struct { float rx, ry, rz; } shield_rot[] = {
        {0, 90, 0},   /* Front: point to X+ */
        {0, -90, 0},  /* Rear: point to X- */
        {-90, 0, 0},  /* Top: point to Y+ */
        {90, 0, 0},   /* Bottom: point to Y- */
        {0, 0, 0},    /* Left: point to Z- (default for torus if aligned) */
        {0, 180, 0}   /* Right: point to Z+ */
    };

    for(int s=0; s<6; s++) {
        if (g_shield_hit_timers[s] <= 0) continue;
        
        float t = (float)g_shield_hit_timers[s] / 40.0f;
        /* Stay bright longer, then fade quickly at the end */
        float alpha = (t > 0.5f) ? 1.0f : t * 2.0f;
        float scale = 1.0f + (1.0f - t) * 0.15f; 
        
        glPushMatrix();
        glRotatef(shield_rot[s].ry, 0, 1, 0);
        glRotatef(shield_rot[s].rx, 1, 0, 0);
        
        /* Position arc slightly outside hull */
        glTranslatef(0, 0, 1.2f);
        glScalef(scale * 1.5f, scale * 1.2f, scale * 0.5f);
        
        glColor4f(0.0f, 0.7f, 1.0f, alpha * 0.7f);
        
        /* Use a wireframe torus or sphere segment for the "grid" look */
        glutWireTorus(0.05, 0.8, 8, 12);
        
        /* Local glow at impact site */
        drawGlow(0.6f, 0.0f, 0.5f, 1.0f, alpha * 0.4f);
        
        glPopMatrix();
    }
    
    glPopMatrix();
    glPopAttrib();
}

void reshape(int w, int h) {
    if (w != 1280 || h != 720) {
        glutReshapeWindow(1280, 720);
    }
}

void display() {
    /* 1. BLOOM PASS: Render Scene to Multisampled Buffer */
    if (fbo_msaa != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_msaa);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    
    if (g_data_dirty) { loadGameState(); g_data_dirty = 0; }
    
    /* RESET STACKS */
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    /* Dynamic FOV: 45 (Tactical) -> 65 (Bridge) */
    float current_fov = 45.0f * (1.0f - bridge_anim) + 65.0f * bridge_anim;
    gluPerspective(current_fov, 1.77, 0.1, 500); 
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    /* Cinematic Camera Transition */
    float current_zoom = zoom - (21.0f * map_anim);
    
    if (bridge_anim > 0.001f && objects[0].id != 0) {
        if (bridge_anim >= 0.999f) {
            float y_off = (g_show_bridge >= 11) ? 0.35f : -0.35f;
            int mode = g_show_bridge % 10;

            if (mode == 2) { /* LEFT */
                glRotatef(0.0f, 0, 1, 0); 
            } else if (mode == 3) { /* RIGHT */
                glRotatef(180.0f, 0, 1, 0);
            } else if (mode == 4) { /* UP */
                glRotatef(-90.0f, 1, 0, 0);
                glRotatef(90.0f, 0, 1, 0);
            } else if (mode == 5) { /* DOWN */
                glRotatef(90.0f, 1, 0, 0);
                glRotatef(90.0f, 0, 1, 0);
            } else if (mode == 6) { /* REAR */
                glRotatef(-90.0f, 0, 1, 0);
            } else { /* FORWARD (1) */
                glRotatef(90.0f, 0, 1, 0);
            }

            glTranslatef(0.48f, y_off, 0.0f);
            glRotatef(-objects[0].m, 0, 0, 1);
            glRotatef(-(objects[0].h - 90.0f), 0, 1, 0);
            glTranslatef(-objects[0].x, -objects[0].y, -objects[0].z);
        } else {
            glPushMatrix();
            glLoadIdentity();
            glTranslatef(0, 0, current_zoom);
            glRotatef(angleX, 1, 0, 0);
            glRotatef(angleY, 0, 1, 0);
            GLdouble m_std[16];
            glGetDoublev(GL_MODELVIEW_MATRIX, m_std);
            
            glLoadIdentity();
            float y_off = (g_show_bridge >= 11) ? 0.35f : -0.35f;
            int mode = g_show_bridge % 10;

            if (mode == 2) { glRotatef(0.0f, 0, 1, 0); }
            else if (mode == 3) { glRotatef(180.0f, 0, 1, 0); }
            else if (mode == 4) { glRotatef(-90.0f, 1, 0, 0); glRotatef(90.0f, 0, 1, 0); }
            else if (mode == 5) { glRotatef(90.0f, 1, 0, 0); glRotatef(90.0f, 0, 1, 0); }
            else if (mode == 6) { glRotatef(-90.0f, 0, 1, 0); }
            else { glRotatef(90.0f, 0, 1, 0); }

            glTranslatef(0.48f, y_off, 0.0f);
            glRotatef(-objects[0].m, 0, 0, 1);
            glRotatef(-(objects[0].h - 90.0f), 0, 1, 0);
            glTranslatef(-objects[0].x, -objects[0].y, -objects[0].z);
            GLdouble m_brg[16];
            glGetDoublev(GL_MODELVIEW_MATRIX, m_brg);
            glPopMatrix();

            GLdouble m_final[16];
            for(int i=0; i<16; i++) m_final[i] = m_std[i] * (1.0f - bridge_anim) + m_brg[i] * bridge_anim;
            glLoadMatrixd(m_final);
        }
    } else {
        glTranslatef(0, 0, current_zoom);
        glRotatef(angleX, 1, 0, 0); 
        glRotatef(angleY, 0, 1, 0);
    }

    /* Capture matrices for HUD projection later */
    glGetDoublev(GL_MODELVIEW_MATRIX, hud_model);
    glGetDoublev(GL_PROJECTION_MATRIX, hud_proj);
    glGetIntegerv(GL_VIEWPORT, hud_view);

    /* Sky color pulse */
    float sn_intensity = 0.0f;
    if (g_shared_state->shm_galaxy[g_shared_state->shm_q[0]][g_shared_state->shm_q[1]][g_shared_state->shm_q[2]] < 0) {
        int timer = -g_shared_state->shm_galaxy[g_shared_state->shm_q[0]][g_shared_state->shm_q[1]][g_shared_state->shm_q[2]];
        sn_intensity = 0.3f + sin(pulse*10.0f) * 0.2f;
        if (timer < 300) sn_intensity += 0.3f; 
    }
    
    /* Background fade to darker black in map mode */
    float bg_level = 0.05f * (1.0f - map_anim);
    glClearColor(bg_level + sn_intensity, bg_level, bg_level, 1.0f);
    /* glClear is removed here as it is handled at the start of FBO binding */

    /* Render Procedural Nebula Background */
    if (map_anim < 0.9f) drawSkybox();

    /* --- RENDER GALAXY MAP (Fades In) --- */
    if (map_anim > 0.01f) {
        glPushMatrix();
        /* Holographic appearing effect: Scale up from center */
        float map_scale = map_anim; 
        glScalef(map_scale, map_scale, map_scale);
        drawExplorerMap();
        glPopMatrix();
    }

    /* --- RENDER TACTICAL VIEW (Fades Out) --- */
    if (map_anim < 0.99f) {
        glPushMatrix();
        float tact_scale = 1.0f - map_anim;
        glScalef(tact_scale, tact_scale, tact_scale);

        /* 1. BACKGROUND STARS */
        glDisable(GL_LIGHTING);
        if (vbo_stars != 0) { 
            glPointSize(1.0f);
            glColor3f(0.8f, 0.8f, 0.8f); 
            glEnableClientState(GL_VERTEX_ARRAY); 
            glBindBuffer(GL_ARRAY_BUFFER, vbo_stars); 
            glVertexPointer(3, GL_FLOAT, 0, 0); 
            glDrawArrays(GL_POINTS, 0, 1000); 
            glBindBuffer(GL_ARRAY_BUFFER, 0); 
            glDisableClientState(GL_VERTEX_ARRAY); 
        }

        drawTacticalCube();

        /* 2. LOCAL OBJECTS */
        /* Find a star for dynamic lighting */
        float lX = 50.0f, lY = 50.0f, lZ = 50.0f; /* Default distant light */
        for(int i=0; i<256; i++) {
            if (objects[i].type == 4) {
                lX = objects[i].x; lY = objects[i].y; lZ = objects[i].z;
                break;
            }
        }
        glUseProgram(hullShaderProgram);
        glUniform3f(glGetUniformLocation(hullShaderProgram, "lightPos"), lX, lY, lZ);
        glUseProgram(0);

        if (g_show_axes && objects[0].id != 0) {
            glPushMatrix();
            glTranslatef(objects[0].x, objects[0].y, objects[0].z);
            
            /* 1. FIXED GLOBAL AXES */
            glDisable(GL_LIGHTING);
            glBegin(GL_LINES);
            glColor3f(0.5, 0, 0); glVertex3f(-5.5,0,0); glVertex3f(5.5,0,0);
            glColor3f(0, 0.5, 0); glVertex3f(0,-5.5,0); glVertex3f(0,5.5,0);
            glColor3f(0, 0, 0.5); glVertex3f(0,0,-5.5); glVertex3f(0,0,5.5);
            glEnd();

            /* 1.5 FIXED HORIZONTAL COMPASS (White) */
            drawFixedCompass();

            /* 2. HEADING RING (Tilts with Pitch) */
            glPushMatrix();
            float h_rad = objects[0].h * M_PI / 180.0f;
            /* Inverted pitch rotation to match ship physics: nose up = ring up */
            glRotatef(-objects[0].m, cos(h_rad), 0, -sin(h_rad));
            drawHeadingRing(); 
            glPopMatrix();

            /* 3. MARK ARC (Stays Vertical, only follows Heading) */
            glPushMatrix();
            glRotatef(objects[0].h, 0, 1, 0);
            drawMarkArc();
            glPopMatrix();
            
            glPopMatrix();
        }
        if (g_show_grid) drawGrid();
        
        /* Supernova Flash */
        if (g_sn_pos.active && g_sn_q[0] == g_my_q[0] && g_sn_q[1] == g_my_q[1] && g_sn_q[2] == g_my_q[2] && g_sn_pos.timer < 30) {
            glDisable(GL_LIGHTING);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            float flash_s = (30 - g_sn_pos.timer) * 0.5f;
            glColor4f(1.0f, 1.0f, 1.0f, 0.8f);
            glPushMatrix();
            glTranslatef(g_sn_pos.x, g_sn_pos.y, g_sn_pos.z);
            glutSolidSphere(flash_s, 32, 32);
            glPopMatrix();
            glDisable(GL_BLEND);
            glEnable(GL_LIGHTING);
        }

        /* Trails */
        for(int k=0; k<256; k++) {
            if (objects[k].type == 1 || objects[k].type >= 10) drawShipTrail(k);
        }

        /* Objects (Solids) */
        glEnable(GL_LIGHTING);
        for(int i=0; i<256; i++) {
            if (objects[i].type == 0) continue;

            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
            glPushMatrix(); glTranslatef(objects[i].x, objects[i].y, objects[i].z);

            /* Pass per-object hit status to shader */
            glUseProgram(hullShaderProgram);
            if (i == 0) {
                glUniform1f(glGetUniformLocation(hullShaderProgram, "hitPulse"), (float)g_hull_hit_timer / 20.0f);
            } else {
                glUniform1f(glGetUniformLocation(hullShaderProgram, "hitPulse"), 0.0f);
            }
            glUseProgram(0);

            if (objects[i].is_cloaked) { 
                g_is_cloaked_rendering = 1;
                glPushAttrib(GL_ALL_ATTRIB_BITS);
                glEnable(GL_BLEND); 
                glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                glUseProgram(cloakShaderProgram);
                glUniform1f(glGetUniformLocation(cloakShaderProgram, "time"), pulse);
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                glLineWidth(1.0f);
            }

            if (objects[i].type == 1) { 
                /* Hide ship if it's currently emerging from a wormhole */
                bool is_emerging = (g_jump_arrival.timer > 180);
                
                if (!is_emerging && objects[i].faction == 0) { // FACTION_ALLIANCE
                    if (!g_is_cloaked_rendering) glUseProgram(hullShaderProgram);
                    drawAllianceShip(objects[i].ship_class, objects[i].h, objects[i].m);
                    glUseProgram(0);
                } else if (!is_emerging) {
                    /* Non-Alliance Player: Use Faction model */
                    glRotatef(objects[i].h - 90.0f, 0, 1, 0); glRotatef(objects[i].m, 0, 0, 1);
                    if (!g_is_cloaked_rendering) glUseProgram(hullShaderProgram);
                    switch(objects[i].faction) {
                        case 10: drawKorthian(0,0,0); break;
                        case 11: drawXylari(0,0,0); break;
                        case 12: drawSwarm(0,0,0); break;
                        case 13: drawVesperian(0,0,0); break;
                        case 14: drawAscendant(0,0,0); break;
                        case 15: drawQuarzite(0,0,0); break;
                        case 16: drawSaurian(0,0,0); break;
                        case 17: drawGilded(0,0,0); break;
                        case 18: drawFluidicVoid(0,0,0); break;
                        case 19: drawCryos(0,0,0); break;
                        case 20: drawApex(0,0,0); break;
                        default: drawAllianceShip(0, 0, 0); break;
                    }
                    glUseProgram(0);
                }
            } else {
                glRotatef(objects[i].h - 90.0f, 0, 1, 0); glRotatef(objects[i].m, 0, 0, 1);
                bool use_hull = false;
                int t = objects[i].type;
                if (t == 3 || t == 10 || t == 21 || t == 22 || t == 23 || t == 24 || t == 25 || (t >= 11 && t <= 20)) use_hull = true;
                
                if (use_hull && !g_is_cloaked_rendering) glUseProgram(hullShaderProgram);
                switch(t) {
                    case 3: drawStarbase(0,0,0); break;
                    case 4: drawStar(objects[i].x, objects[i].y, objects[i].z, objects[i].id); break;
                    case 5: drawPlanet(0,0,0); break;
                    case 6: drawBlackHole(0,0,0); break;
                    case 7: drawStellarNebula(0,0,0, objects[i].ship_class); break;
                    case 8: drawPulsar(0,0,0); break;
                    case 9: drawComet(0,0,0); break;
                    case 10: drawKorthian(0,0,0); break;
                    case 21: drawAsteroid(0,0,0); break;
                    case 22: drawDerelict(objects[i].ship_class); break;
                    case 23: drawMine(0,0,0); break;
                    case 24: drawBuoy(0,0,0); break;
                    case 25: drawPlatform(0,0,0); break;
                    case 26: drawRift(0,0,0); break;
                    case 30: drawMonster(30,0,0,0); break;
                    case 31: drawMonster(31,0,0,0); break;
                    case 11: drawXylari(0,0,0); break;
                    case 12: drawSwarm(0,0,0); break;
                    case 13: drawVesperian(0,0,0); break;
                    case 14: drawAscendant(0,0,0); break;
                    case 15: drawQuarzite(0,0,0); break;
                    case 16: drawSaurian(0,0,0); break;
                    case 17: drawGilded(0,0,0); break;
                    case 18: drawFluidicVoid(0,0,0); break;
                    case 19: drawCryos(0,0,0); break;
                    case 20: drawApex(0,0,0); break;
                    case 27: {
                        /* Universal Probe Rendering */
                        int status = objects[i].ship_class; /* Passed from server */
                        if (status == 2) {
                            /* Derelict Red */
                            glColor3f(0.4f, 0.4f, 0.45f); glutSolidSphere(0.05, 8, 8);
                            glColor3f(1.0f, 0.0f, 0.0f);
                            glPushMatrix(); glRotatef(pulse * 20.0f, 0, 1, 0); glutWireTorus(0.01, 0.1, 4, 12); glPopMatrix();
                            glPushMatrix(); glRotatef(90, 1, 0, 0); glRotatef(-pulse * 15.0f, 0, 1, 0); glPushAttrib(GL_LINE_BIT); glLineWidth(1.0f); glutWireTorus(0.005, 0.12, 4, 16); glPopAttrib(); glPopMatrix();
                        } else {
                            /* Active Cyan with Glow */
                            glColor3f(0.0f, 0.7f, 1.0f); glutSolidSphere(0.05, 8, 8);
                            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                            float p_glow = 0.08f + sin(pulse*5.0f)*0.03f;
                            glColor4f(0.0f, 0.4f, 1.0f, 0.5f); glutSolidSphere(p_glow, 12, 12);
                            glDisable(GL_BLEND);
                            glColor3f(0.0f, 1.0f, 1.0f);
                            glPushMatrix(); glRotatef(pulse * 100.0f, 0, 1, 0); glutWireTorus(0.01, 0.1, 4, 12); glPopMatrix();
                            glPushMatrix(); glRotatef(90, 1, 0, 0); glRotatef(-pulse * 80.0f, 0, 1, 0); glPushAttrib(GL_LINE_BIT); glLineWidth(1.0f); glutWireTorus(0.005, 0.12, 4, 16); glPopAttrib(); glPopMatrix();
                        }
                    } break;
                }
                glUseProgram(0);
            }

            if (objects[i].is_cloaked) { 
                glPopAttrib();
                glUseProgram(0);
                g_is_cloaked_rendering = 0;
            }

            glPopMatrix();
        }

        /* 3. TRANSPARENT EFFECTS (Drawn last with depth testing usually) */
        drawShieldEffect();
        drawIonBeams();
        drawExplosion();
        drawParticles();
        drawJumpArrival();
        drawTorpedo();
        updateProbeHUD();
        if (g_wormhole.active && g_jump_arrival.timer <= 0) drawWormhole(g_wormhole.x, g_wormhole.y, g_wormhole.z, g_wormhole.h, g_wormhole.m, 0);
        drawDismantle();
        drawRecoveryEffect();


        drawFaceLabels();
        glPopMatrix(); /* End of Tactical Mode scaling */
    }

    glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glEnable(GL_LIGHTING);

    /* 2. BLOOM PASS: Resolve MSAA to standard texture */
    if (fbo_msaa != 0 && fbo_scene != 0) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_msaa);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_scene);
        glBlitFramebuffer(0, 0, 1280, 720, 0, 0, 1280, 720, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    /* 3. BLOOM PASS: Blur the Bright Texture (Ping-Pong) */
    if (fbo_scene != 0) {
        bool horizontal = true, first_iteration = true;
        unsigned int amount = 10;
        glUseProgram(blurShaderProgram);
        glDisable(GL_DEPTH_TEST);
        for (unsigned int i = 0; i < amount; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, fbo_pingpong[horizontal]); 
            glUniform1i(glGetUniformLocation(blurShaderProgram, "horizontal"), horizontal);
            glBindTexture(GL_TEXTURE_2D, first_iteration ? tex_scene : tex_pingpong[!horizontal]); 
            renderQuad();
            horizontal = !horizontal;
            first_iteration = false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        /* 3. BLOOM PASS: Final Combine (HDR + Bloom) */
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(finalShaderProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_scene);
        glUniform1i(glGetUniformLocation(finalShaderProgram, "scene"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, tex_pingpong[!horizontal]);
        glUniform1i(glGetUniformLocation(finalShaderProgram, "bloomBlur"), 1);
        renderQuad();
        
        glUseProgram(0);
        glEnable(GL_DEPTH_TEST);
    }

    /* --- FINAL UI PASS (Drawn on top of everything, bypassed by Bloom for sharpness) --- */
    /* HUD Overlay (Tactical Mode) - Fade Alpha */
    if (g_show_hud) {
        if (map_anim < 0.5f) {
            for(int i=0; i<256; i++) {
                if (objects[i].type != 0 && !g_is_loading) {
                    drawHUD(i);
                }
            }
        }
    }

    /* Draw HUD Overlay (Map Mode) */
    if (g_show_hud && map_anim > 0.5f) {
        /* Show Map specific text */
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); gluOrtho2D(0, 1000, 0, 1000); glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glDisable(GL_LIGHTING); glColor3f(0, 1, 1);
        if (g_is_jammed) {
            glColor3f(1, 0, 0);
            drawText3D(20, 960, 0, "--- CARTOGRAPHY FAILURE: SENSORS JAMMED ---");
        } else {
            drawText3D(20, 960, 0, "--- STELLAR CARTOGRAPHY: FULL GALAXY VIEW ---");
        }
        
        int fy = 920;
        /* Vertical list: Color Block + Object Name */
        struct { float r,g,b; const char* name; } legends[] = {
            {1.0, 1.0, 0.0, "Yellow: Stars (st)"},
            {0.0, 0.8, 1.0, "Cyan: Planets (pl)"},
            {0.0, 1.0, 0.0, "Green: Starbases (bs)"},
            {1.0, 0.0, 0.0, "Red: Hostiles (en)"},
            {0.6, 0.0, 1.0, "Purple: Black Holes (bh)"},
            {0.7, 0.7, 0.7, "Grey: Nebulas (ne)"},
            {1.0, 0.5, 0.0, "Orange: Pulsars (pu)"},
            {1.0, 1.0, 1.0, "White Shell: Ion Storms (is)"},
            {0.5, 0.8, 1.0, "Light Blue: Comets (co)"},
            {0.5, 0.3, 0.1, "Brown: Asteroids (as)"},
            {0.3, 0.3, 0.3, "Dark Grey: Derelicts (de)"},
            {1.0, 0.0, 0.0, "Bright Red: Minefields (mi)"},
            {0.0, 0.5, 1.0, "Blue: Comm Buoys (bu)"},
            {0.8, 0.4, 0.0, "Dark Orange: Defense Platforms (pf)"},
            {0.0, 1.0, 1.0, "Cyan: Spatial Rifts (ri)"},
            {1.0, 1.0, 1.0, "White: Space Monsters (mo)"}
        };

        for(int i=0; i<16; i++) {
            /* Draw a small color square */
            glColor3f(legends[i].r, legends[i].g, legends[i].b);
            glBegin(GL_QUADS);
            glVertex2i(25, fy-2); glVertex2i(35, fy-2); glVertex2i(35, fy+10); glVertex2i(25, fy+10);
            glEnd();
            /* Draw the text */
            glColor3f(0.8, 0.8, 1.0);
            drawText3D(45, fy, 0, legends[i].name);
            fy -= 22;
        }

        glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
    }

    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); gluOrtho2D(0, 1000, 0, 1000); glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glDisable(GL_LIGHTING); glColor3f(0, 1, 1); 
    if (g_show_map) drawText3D(20, 50, 0, "Arrows: Rotate Map | W/S: Zoom Map | map (in CLI): Exit Map Mode");
    else drawText3D(20, 50, 0, "Arrows: Rotate | W/S: Zoom | bridge [top/bottom/up/down/left/right/rear/off] | map | H: Toggle HUD | ESC: Exit");

    char buf[256]; 
    if (g_show_hud && map_anim < 0.5f) {
        /* --- TOP LEFT: Comprehensive Ship Status --- */
        int x_off = 20;
        int y_pos = 970;
        
        /* 1. Command & Location */
        glColor3f(1.0f, 1.0f, 0.0f); /* Yellow */
        /* Use captain name and class from local variables copied under mutex */
        if (g_shared_state->objects[0].faction == 0) { // FACTION_ALLIANCE
            sprintf(buf, "Alliance - %s - CAPTAIN: %s", getClassName(g_player_class), g_player_name);
        } else {
            sprintf(buf, "%s - CAPTAIN: %s", getFactionHUDName(g_shared_state->objects[0].faction), g_player_name);
        }
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 20;

        if (g_is_docked) {
            glColor3f(0.0f, 1.0f, 0.0f); /* Green */
            sprintf(buf, "SHIP STATUS: DOCKED (Systems Secured)");
        } else if (g_red_alert) {
            float blink = (float)sin(glutGet(GLUT_ELAPSED_TIME)*0.01) * 0.5f + 0.5f;
            glColor3f(1.0f, blink * 0.3f, 0.0f); /* Pulsing Red/Orange */
            sprintf(buf, "SHIP STATUS: RED ALERT (Battle Stations)");
        } else {
            glColor3f(0.0f, 1.0f, 1.0f); /* Bright Cyan */
            const char* st_name = "ACTIVE";
            switch(g_nav_state) {
                case 1:  st_name = "ALIGNING"; break;
                case 2:  st_name = "HYPERDRIVE"; break;
                case 3:  st_name = "REALIGNING"; break;
                case 4:  st_name = "IMPULSE"; break;
                case 5:  st_name = "CHASING"; break;
                case 6:  st_name = "ALIGNING (IMP)"; break;
                case 7:  st_name = "WORMHOLE JUMP"; break;
                case 8:  st_name = "RE-ORIENTING"; break;
                case 9:  st_name = "DOCKING SEQUENCE"; break;
                case 10: st_name = "DRIFTING (EMERGENCY)"; break;
                case 11: st_name = "ORBITING"; break;
                case 12: st_name = "GRAV-SLINGSHOT"; break;
                default: st_name = "ACTIVE"; break;
            }
            sprintf(buf, "SHIP STATUS: %s", st_name);
        }
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 20;

        if (g_is_jammed) {
            glColor3f(1.0f, 1.0f, 1.0f);
            drawText3D(x_off, y_pos, 0, "WARNING: ELECTRONIC WARFARE JAMMING ACTIVE"); y_pos -= 20;
        }

        glColor3f(0.0f, 1.0f, 1.0f); /* Cyan */
        /* Convert focal point coordinates back to Stellar sector coordinates (0 to 10) */
        float disp_s1 = PlayerX + 5.0f;
        float disp_s2 = 5.0f - PlayerZ;
        float disp_s3 = PlayerY + 5.0f;
        sprintf(buf, "QUADRANT: %s  |  SECTOR: [%.2f, %.2f, %.2f]", g_quadrant, disp_s1, disp_s2, disp_s3); 
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 20;

        sprintf(buf, "HEADING: %03.0f\302\260  |  MARK: %+03.0f\302\260", g_shared_state->shm_h, g_shared_state->shm_m);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 25;

        /* 2. Vital Resources */
        glColor3f(1.0f, 1.0f, 1.0f);
        sprintf(buf, "ENERGY: %-7d (CARGO ANTIMATTER: %-7d) | TORPS: %-4d (CARGO TORPEDOES: %-4d)", g_energy, g_cargo_energy, g_torpedoes_launcher, g_cargo_torps);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 18;
        
        /* Hull Integrity Main Display */
        if (g_hull_integrity > 60) glColor3f(0, 1, 0);
        else if (g_hull_integrity > 25) glColor3f(1, 1, 0);
        else glColor3f(1, 0, 0);
        sprintf(buf, "HULL INTEGRITY: %.1f%%", g_hull_integrity);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 18;

        if (g_composite_plating > 0) {
            glColor3f(1.0f, 0.8f, 0.0f);
            sprintf(buf, "HULL PLATING: %-5d [Composite REINFORCED]", g_composite_plating);
            drawText3D(x_off, y_pos, 0, buf); y_pos -= 18;
        }

        glColor3f(1.0f, 1.0f, 1.0f);
        sprintf(buf, "CREW: %-4d | PRISON UNIT: %-4d | SHIELDS AVG: %-3d%% | LOCK: ", g_crew, g_prison_unit, g_shields);
        drawText3D(x_off, y_pos, 0, buf);
        if (g_lock_target > 0) {
            glColor3f(1, 0, 0); sprintf(buf, "[ ID %d ]", g_lock_target);
        } else {
            glColor3f(0.5, 0.5, 0.5); sprintf(buf, "[ NONE ]");
        }
        drawText3D(x_off + 450, y_pos, 0, buf); y_pos -= 20;

        /* 2.1 Individual Shields */
        glColor3f(0.0f, 0.7f, 1.0f);
        const char* sh_names[] = {"F:", "R:", "T:", "B:", "L:", "RI:"};
        for(int i=0; i<6; i++) {
            sprintf(buf, "%s %-4d", sh_names[i], g_shields_val[i]);
            drawText3D(x_off + i*60, y_pos, 0, buf);
        }
        y_pos -= 25;

        /* 3. System Health (2 columns) */
        glColor3f(0.0f, 0.8f, 0.0f);
        drawText3D(x_off, y_pos, 0, "--- SYSTEMS HEALTH ---"); y_pos -= 18;
        const char* sys_names[] = {"Hyperdrive", "Impulse", "Sensors", "Transp", "Ion Beams", "Torps", "Computer", "Life", "Shields", "Aux"};
        for(int i=0; i<5; i++) {
            for(int col=0; col<2; col++) {
                int idx = i + col*5;
                float h = g_system_health[idx];
                if (h > 75) glColor3f(0, 1, 0); else if (h > 30) glColor3f(1, 1, 0); else glColor3f(1, 0, 0);
                sprintf(buf, "%-8s: %3.0f%%", sys_names[idx], h);
                drawText3D(x_off + col*150, y_pos, 0, buf);
            }
            y_pos -= 15;
        }
        y_pos -= 10;

        /* 4. Cargo Inventory (2 columns) */
        glColor3f(0.8f, 0.5f, 0.0f);
        drawText3D(x_off, y_pos, 0, "--- CARGO INVENTORY ---"); y_pos -= 18;
        const char* res_names[] = {"None", "Aetherium", "Neo-Titanium", "Void-Essence", "Graphene", "Synaptics", "Nebular Gas", "Composite", "Dark-Matter"};
        for(int i=1; i<5; i++) {
            for(int col=0; col<2; col++) {
                int idx = i + col*4;
                if (idx > 8) continue;
                glColor3f(0.7, 0.7, 0.7);
                sprintf(buf, "%-10s: %-4d", res_names[idx], g_inventory[idx]);
                drawText3D(x_off + col*150, y_pos, 0, buf);
            }
            y_pos -= 15;
        }
        y_pos -= 10;

        /* 4.1 Deep Space Probes Status */
        glColor3f(0.0f, 0.8f, 1.0f);
        drawText3D(x_off, y_pos, 0, "--- PROBES STATUS ---"); y_pos -= 18;
        for(int p=0; p<3; p++) {
            if (g_local_probes[p].active) {
                const char* st_name = "EN ROUTE";
                if (g_local_probes[p].status == 1) {
                    st_name = "TRANSMITTING";
                    glColor3f(1.0f, 1.0f, 0.0f);
                } else if (g_local_probes[p].status == 2) {
                    st_name = "DERELICT";
                    glColor3f(0.5f, 0.5f, 0.5f);
                } else {
                    glColor3f(0.0f, 1.0f, 0.0f);
                }
                sprintf(buf, "P%d: %-12s [%d,%d,%d] ETA: %4.1fs", 
                        p+1, st_name, 
                        g_local_probes[p].q1, g_local_probes[p].q2, g_local_probes[p].q3,
                        (g_local_probes[p].eta < 0) ? 0 : g_local_probes[p].eta);
            } else {
                glColor3f(0.3f, 0.3f, 0.3f);
                sprintf(buf, "P%d: IDLE", p+1);
            }
            drawText3D(x_off, y_pos, 0, buf); y_pos -= 15;
        }
        y_pos -= 10;

        /* 5. Reactor Power Distribution */
        glColor3f(1.0f, 1.0f, 0.0f);
        drawText3D(x_off, y_pos, 0, "--- REACTOR POWER ALLOCATION ---"); y_pos -= 18;
        sprintf(buf, "ENGINES: %d%%  |  SHIELDS: %d%%  |  WEAPONS: %d%%", 
                (int)(g_shared_state->shm_power_dist[0]*100), 
                (int)(g_shared_state->shm_power_dist[1]*100), 
                (int)(g_shared_state->shm_power_dist[2]*100));
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 25;

        /* 6. Tactical Ordnance & Defense */
        glColor3f(1.0f, 0.0f, 0.0f);
        drawText3D(x_off, y_pos, 0, "--- TACTICAL ORDNANCE ---"); y_pos -= 18;
        
        /* Weapons Status */
        glColor3f(1.0f, 0.5f, 0.5f);
        const char* tube_labels[] = {"READY", "FIRING...", "LOADING...", "OFFLINE"};
        int ts = g_shared_state->shm_tube_state; if(ts<0||ts>3) ts=3;
        sprintf(buf, "Ion Beam CAPACITOR: %-3.0f%% | TUBES: %s", 
                g_shared_state->shm_ion_beam_charge,
                tube_labels[ts]);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 15;
        
        sprintf(buf, "Ion Beam INTEGRITY: %-3.0f%%  | Anti-Matter: %d", 
                g_system_health[4],
                g_shared_state->shm_anti_matter);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 15;
        
        sprintf(buf, "LIFE SUPPORT: %.1f%%", g_shared_state->shm_life_support);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 25;

        /* 7. Target Tactical Overlay (Center Screen) */
        if (g_lock_target > 0) {
            int tx_pos = 400;
            int ty_pos = 150;
            glColor3f(1.0f, 0.0f, 0.0f);
            drawText3D(tx_pos, ty_pos, 0, ">>> TARGET LOCKED <<<"); ty_pos -= 20;
            
            /* Find target data */
            for(int i=0; i<256; i++) {
                if (objects[i].id == g_lock_target) {
                    glColor3f(1.0f, 1.0f, 1.0f);
                    
                    const char* f_name = "Neutral/Unknown";
                    switch(objects[i].faction) {
                        case 0:  f_name = "Alliance"; glColor3f(0, 1, 1); break;
                        case 10: f_name = "Korthian"; glColor3f(1, 0, 0); break;
                        case 11: f_name = "Xylari"; glColor3f(0, 1, 0); break;
                        case 12: f_name = "Swarm"; glColor3f(1, 0, 1); break;
                        case 13: f_name = "VESPERIAN"; glColor3f(1, 0.5, 0); break;
                        case 14: f_name = "ASCENDANT"; glColor3f(0.5, 0, 1); break;
                        default: f_name = "INDEPENDENT"; glColor3f(0.8, 0.8, 0.8); break;
                    }

                    sprintf(buf, "NAME: %s (%s)", objects[i].name, f_name);
                    drawText3D(tx_pos, ty_pos, 0, buf); ty_pos -= 15;
                    
                    float dx = objects[i].x - PlayerX;
                    float dy = objects[i].y - PlayerY;
                    float dz = objects[i].z - PlayerZ;
                    float dist = sqrt(dx*dx + dy*dy + dz*dz);
                    
                    glColor3f(1.0f, 1.0f, 1.0f);
                    sprintf(buf, "ANTIMATTER: %d (%d%%) | DIST: %.2f", objects[i].energy, objects[i].health_pct, dist);
                    drawText3D(tx_pos, ty_pos, 0, buf); ty_pos -= 15;

                    sprintf(buf, "HEADING: %.0f | MARK: %+.0f", objects[i].h, objects[i].m);
                    drawText3D(tx_pos, ty_pos, 0, buf);
                    break;
                }
            }
        }

        if (g_shared_state->is_cloaked) {
            glColor3f(0.5f, 0.5f, 1.0f);
            drawText3D(x_off, y_pos - 20, 0, ">>> CLOAKING DEVICE ACTIVE <<<");
        }

        /* --- TOP RIGHT: Quadrant Object List --- */
        int y_off = 965;
        glColor3f(1.0f, 0.5f, 0.0f);
        drawText3D(750, y_off, 0, "--- QUADRANT SENSORS ---");
        y_off -= 25;
        
        for(int i=0; i<256; i++) {
            if (objects[i].id != 0 && objects[i].type != 0) {
                if (objects[i].type == 1) glColor3f(0, 1, 1);
                else if (objects[i].type >= 10 && objects[i].type <= 20) glColor3f(1, 0, 0);
                else glColor3f(0.8, 0.8, 0.8);

                const char* t_name = "Object";
                switch(objects[i].type) {
                    case 1: t_name = "PLAYER"; break;
                    case 3: t_name = "BASE"; break;
                    case 4: t_name = "STAR"; break;
                    case 5: t_name = "PLANET"; break;
                    case 6: t_name = "BLACKHOLE"; break;
                    case 10: t_name = "Korthian"; break;
                    case 11: t_name = "Xylari"; break;
                    case 12: t_name = "Swarm"; break;
                    default: t_name = "OTHER"; break;
                }
                
                if (objects[i].type == 1) sprintf(buf, "[%03d] %s", objects[i].id, objects[i].name);
                else if (objects[i].type == 4) {
                    const char* st_cls[] = {"O", "B", "A", "F", "G", "K", "M"};
                    int c_idx = objects[i].ship_class; if(c_idx<0)c_idx=0; if(c_idx>6)c_idx=6;
                    sprintf(buf, "[%03d] STAR: Class %s", objects[i].id, st_cls[c_idx]);
                } else if (objects[i].type == 5) {
                    const char* p_res[] = {"None", "Dil", "Tri", "Ver", "Per", "Anti", "Xen"};
                    int r_idx = objects[i].ship_class; if(r_idx<0)r_idx=0; if(r_idx>6)r_idx=6;
                    sprintf(buf, "[%03d] PLANET: %s", objects[i].id, p_res[r_idx]);
                } else if (objects[i].type == 7) {
                    const char* n_cls[] = {"Mutara", "Metreon", "Dark Matter Cloud", "Paulson", "McAllister", "Arachnia"};
                    int n_idx = objects[i].ship_class; if(n_idx<0) n_idx=0; if(n_idx>5) n_idx=5;
                    sprintf(buf, "[%03d] NEBULA: %s", objects[i].id, n_cls[n_idx]);
                }
                else sprintf(buf, "[%03d] %s (%s)", objects[i].id, objects[i].name, t_name);
                
                drawText3D(750, y_off, 0, buf);
                y_off -= 20;
                if (y_off < 500) {
                    drawText3D(750, y_off, 0, "...");
                    break;
                }
            }
        }

        /* --- BOTTOM RIGHT: Deep Space Telemetry --- */
        int ty = 150;
        glColor3f(0.0f, 0.8f, 1.0f); /* LCARS Blue */
        drawText3D(750, ty, 0, "--- Deep Space UPLINK DIAGNOSTICS ---"); ty -= 20;
        
        glColor3f(0.0f, 0.5f, 0.7f);
        sprintf(buf, "LINK UPTIME: %02ld:%02ld:%02ld", g_shared_state->net_uptime/3600, (g_shared_state->net_uptime%3600)/60, g_shared_state->net_uptime%60);
        drawText3D(750, ty, 0, buf); ty -= 15;
        
        sprintf(buf, "BANDWIDTH: %.2f KB/s | PPS: %d", g_shared_state->net_kbps, g_shared_state->net_packet_count);
        drawText3D(750, ty, 0, buf); ty -= 15;
        
        sprintf(buf, "PULSE JITTER: %.2f ms", g_shared_state->net_jitter);
        drawText3D(750, ty, 0, buf); ty -= 15;
        
        sprintf(buf, "SIGNAL INTEGRITY: %.1f%%", g_shared_state->net_integrity);
        drawText3D(750, ty, 0, buf); ty -= 15;

        glColor3f(1.0f, 1.0f, 0.0f);
        sprintf(buf, "POWER: E:%d%% S:%d%% W:%d%%", 
                (int)(g_shared_state->shm_power_dist[0]*100), 
                (int)(g_shared_state->shm_power_dist[1]*100), 
                (int)(g_shared_state->shm_power_dist[2]*100));
        drawText3D(750, ty, 0, buf); ty -= 15;

        glColor3f(0.0f, 0.5f, 0.7f);
        sprintf(buf, "AVG FRAME: %d bytes (Opt: %.1f%%)", g_shared_state->net_avg_packet_size, g_shared_state->net_efficiency);
        drawText3D(750, ty, 0, buf); ty -= 15;

        if (g_shared_state->shm_crypto_algo == CRYPTO_AES) {
            glColor3f(0.0f, 1.0f, 0.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: AES-256-GCM ACTIVE");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_CHACHA) {
            glColor3f(0.0f, 1.0f, 0.5f);
            drawText3D(750, ty, 0, "ENCRYPTION: CHACHA20-POLY ACTIVE");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_ARIA) {
            glColor3f(0.0f, 0.7f, 1.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: ARIA-256-GCM ACTIVE");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_CAMELLIA) {
            glColor3f(0.0f, 1.0f, 0.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: CAMELLIA-256 (Xylari)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_SEED) {
            glColor3f(1.0f, 0.5f, 0.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: SEED-CBC (ORION)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_CAST5) {
            glColor3f(1.0f, 1.0f, 0.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: CAST5-CBC (REPUBLIC)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_IDEA) {
            glColor3f(1.0f, 0.0f, 1.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: IDEA-CBC (MAQUIS)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_3DES) {
            glColor3f(0.5f, 0.5f, 0.5f);
            drawText3D(750, ty, 0, "ENCRYPTION: 3DES-CBC (ANCIENT)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_BLOWFISH) {
            glColor3f(0.7f, 0.4f, 0.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: BLOWFISH-CBC (GILDED)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_RC4) {
            glColor3f(0.0f, 0.5f, 0.7f);
            drawText3D(750, ty, 0, "ENCRYPTION: RC4-STREAM (TACTICAL)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_DES) {
            glColor3f(0.4f, 0.4f, 0.4f);
            drawText3D(750, ty, 0, "ENCRYPTION: DES-CBC (PRE-HYPERDRIVE)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_PQC) {
            glColor3f(1.0f, 1.0f, 1.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: ML-KEM-1024 (QUANTUM-SECURE)");
        } else {
            glColor3f(1.0f, 0.0f, 0.0f);
            drawText3D(750, ty, 0, "ENCRYPTION: DISABLED / RAW");
        }
        ty -= 15;

        if (g_shared_state->shm_encryption_flags & 0x01) {
            glColor3f(0.0f, 1.0f, 0.0f);
            drawText3D(750, ty, 0, "SIGNATURE: VERIFIED (HMAC-SHA256)");
        } else {
            glColor3f(1.0f, 0.5f, 0.0f);
            drawText3D(750, ty, 0, "SIGNATURE: NOT PRESENT");
        }
        ty -= 15;
    }

    long long sn_val = g_shared_state->shm_galaxy[g_my_q[0]][g_my_q[1]][g_my_q[2]];
    if (g_show_hud && (g_sn_pos.active || sn_val < 0)) {
        /* Supernova Overlay - Centered and prominently Red */
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); gluOrtho2D(0, 1000, 0, 1000); 
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
        glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);
        
        int sec = 0;
        /* Priority 1: Global event timer from server */
        if (g_sn_pos.active && g_sn_pos.timer > 0) {
            sec = g_sn_pos.timer / 30;
        } 
        /* Priority 2: Fallback to grid-encoded timer (only if valid: 1-1800 ticks) */
        else if (sn_val < 0 && sn_val > -5000) {
            sec = (int)(-sn_val / 30);
        }
        
        if (sec > 60) sec = 60; /* Max supernova countdown is 60s */
        if (sec < 1 && (g_sn_pos.active || (sn_val < 0 && sn_val > -5000))) sec = 1;
        
        char sn_buf[128];
        glColor3f(1.0f, 0.0f, 0.0f); /* Bright Red */
        /* Determine if the supernova is IN the current quadrant */
        bool in_this_q = false;
        if (sn_val < 0 && sn_val > -5000) in_this_q = true;
        if (g_sn_pos.active && g_my_q[0] == g_sn_q[0] && g_my_q[1] == g_sn_q[1] && g_my_q[2] == g_sn_q[2]) in_this_q = true;

        if (in_this_q) {
            sprintf(sn_buf, "!!! CRITICAL: SUPERNOVA IMMINENT IN THIS SECTOR: %d SEC !!!", sec);
        } else if (g_sn_pos.active) {
            sprintf(sn_buf, "!!! WARNING: SUPERNOVA DETECTED IN Q-%d-%d-%d: %d SEC !!!", g_sn_q[0], g_sn_q[1], g_sn_q[2], sec);
        } else {
            /* Event likely cleared but grid not yet synced */
            glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW); glPopMatrix();
            glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
            return;
        }
        drawText3D(200, 500, 0, sn_buf);
        
        glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
        glMatrixMode(GL_PROJECTION); glPopMatrix(); 
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
    }

    glutSwapBuffers();
}

void timer(int v) { 
    updateParticles();
    if (autoRotate > 5.0f) autoRotate = 0.5f; /* Cap speed */
    angleY += autoRotate; 
    if (angleY >= 360.0f) angleY -= 360.0f;
    pulse += 0.05; 
    
    /* Cinematic Transition Logic */
    if (g_show_map) {
        if (map_anim < 1.0f) map_anim += 0.04f;
        if (map_anim > 1.0f) map_anim = 1.0f;
    } else {
        if (map_anim > 0.0f) map_anim -= 0.04f;
        if (map_anim < 0.0f) map_anim = 0.0f;
    }

    if (g_show_bridge) {
        if (bridge_anim < 1.0f) bridge_anim += 0.03f;
        if (bridge_anim > 1.0f) bridge_anim = 1.0f;
    } else {
        if (bridge_anim > 0.0f) bridge_anim -= 0.03f;
        if (bridge_anim < 0.0f) bridge_anim = 0.0f;
    }

    /* Fade out beams */
    for (int i = 0; i < 10; i++) if (beams[i].alpha > 0) beams[i].alpha -= 0.05f;

    /* Update Boom Timer */
    if (g_boom.timer > 0) g_boom.timer--;
    
    for(int s=0; s<6; s++) if (g_shield_hit_timers[s] > 0) g_shield_hit_timers[s]--;
    if (g_hull_hit_timer > 0) g_hull_hit_timer--;

    /* Update Supernova timer */
    if (g_sn_pos.active && g_sn_pos.timer > 0) {
        g_sn_pos.timer--;
        if (g_sn_pos.timer <= 0) g_sn_pos.active = 0;
    }

    /* Update Jump Arrival Timer */
    if (g_jump_arrival.timer > 0) {
        g_jump_arrival.timer--;
        for(int i=0; i<150; i++) {
            if (g_arrival_fx.particles[i].active) {
                g_arrival_fx.particles[i].x += g_arrival_fx.particles[i].vx;
                g_arrival_fx.particles[i].y += g_arrival_fx.particles[i].vy;
                g_arrival_fx.particles[i].z += g_arrival_fx.particles[i].vz;
                
                /* Slowly fade or change behavior if they reach center? 
                   For now just simple convergence */
            }
        }
    }

    /* Update Dismantle */
    if (g_dismantle.timer > 0) {
        g_dismantle.timer--;
        for(int i=0; i<100; i++) {
            if (g_dismantle.particles[i].active) {
                g_dismantle.particles[i].x += g_dismantle.particles[i].vx;
                g_dismantle.particles[i].y += g_dismantle.particles[i].vy;
                g_dismantle.particles[i].z += g_dismantle.particles[i].vz;
            }
        }
    }

    /* Update Objects with Interpolation (GLIDE EFFECT) */
    for (int i = 0; i < 200; i++) {
        if (objects[i].id == 0) continue;
        /* Interpolazione fluida per la posizione (LERP) - Sped up for better glide */
        float interp_speed = 0.35f;
        objects[i].x += (objects[i].tx - objects[i].x) * interp_speed;
        objects[i].y += (objects[i].ty - objects[i].y) * interp_speed;
        objects[i].z += (objects[i].tz - objects[i].z) * interp_speed;
        
        /* Interpolazione fluida per l'orientamento (Heading/Mark) - Significant speed up */
        float dh = objects[i].th - objects[i].h;
        if (dh > 180.0f) dh -= 360.0f;
        if (dh < -180.0f) dh += 360.0f;
        objects[i].h += dh * 0.15f;
        if (objects[i].h >= 360.0f) objects[i].h -= 360.0f;
        if (objects[i].h < 0.0f) objects[i].h += 360.0f;

        objects[i].m += (objects[i].tm - objects[i].m) * 0.15f;
        
        if (objects[i].type == 1 || objects[i].type >= 10) {
            /* Check for jump only if we have a history */
            if (objects[i].trail_count > 0) {
                float lastX = objects[i].trail[(objects[i].trail_ptr - 1 + MAX_TRAIL) % MAX_TRAIL][0];
                float lastY = objects[i].trail[(objects[i].trail_ptr - 1 + MAX_TRAIL) % MAX_TRAIL][1];
                float lastZ = objects[i].trail[(objects[i].trail_ptr - 1 + MAX_TRAIL) % MAX_TRAIL][2];

                /* Rilevamento salto (cambio quadrante o teletrasporto) */
                float dx = objects[i].x - lastX;
                float dy = objects[i].y - lastY;
                float dz = objects[i].z - lastZ;
                float dist_sq = dx*dx + dy*dy + dz*dz;
                if (dist_sq > 25.0f) {
                    objects[i].trail_count = 0;
                    objects[i].trail_ptr = 0;
                }
            }

            static int trail_tick = 0;
            if (trail_tick % 2 == 0) { /* Riduciamo la densità della scia per fluidità visiva */
                objects[i].trail[objects[i].trail_ptr][0] = objects[i].x;
                objects[i].trail[objects[i].trail_ptr][1] = objects[i].y;
                objects[i].trail[objects[i].trail_ptr][2] = objects[i].z;
                objects[i].trail_ptr = (objects[i].trail_ptr + 1) % MAX_TRAIL;
                if (objects[i].trail_count < MAX_TRAIL) objects[i].trail_count++;
            }
        }
    }
    static int global_trail_tick = 0;
    global_trail_tick++;

    glutPostRedisplay(); glutTimerFunc(16, timer, 0); 
}
void keyboard(unsigned char k, int x, int y) { 
    if(k==27) exit(0); 
    if(k==' ') autoRotate=(autoRotate==0)?0.15:0; 
    if(k=='w' || k=='W') zoom += 0.5f;
    if(k=='s' || k=='S') zoom -= 0.5f;
    if(k=='h' || k=='H') g_show_hud = !g_show_hud;
}
void special(int k, int x, int y) { 
    if(k==GLUT_KEY_UP) angleX-=2.5f; 
    if(k==GLUT_KEY_DOWN) angleX+=2.5f; 
    if(angleX > 85.0f) angleX = 85.0f;
    if(angleX < -85.0f) angleX = -85.0f;
    if(k==GLUT_KEY_LEFT) angleY-=5; 
    if(k==GLUT_KEY_RIGHT) angleY+=5; 
}

int main(int argc, char** argv) {

    setlocale(LC_ALL, "C"); signal(SIGUSR1, handle_signal);

    memset(objects, 0, sizeof(objects));

    for(int i=0; i<256; i++) { objects[i].x = objects[i].y = objects[i].z = -100.0f; }

    printf("[3D VIEW] Starting...\n");

    char *shm_name = SHM_NAME; if (argc > 1) shm_name = argv[1];

    printf("[3D VIEW] Connecting to SHM: %s\n", shm_name);
    

    int retries = 0; 

    while(shm_fd == -1 && retries < 10) { 
        shm_fd = shm_open(shm_name, O_RDWR, 0666); 
        if (shm_fd == -1) { 
            printf("[3D VIEW] SHM not ready, retry %d/10...\n", retries+1);
            usleep(100000); retries++; 
        } 
    }

    
    if (shm_fd == -1) {
        fprintf(stderr, "[3D VIEW] FATAL: Could not access shared memory %s after retries.\n", shm_name);
        exit(1);
    }

    g_shared_state = mmap(NULL, sizeof(GameState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (g_shared_state == MAP_FAILED) {
        perror("[3D VIEW] mmap failed");
        exit(1);
    }
        
    printf("[3D VIEW] Shared memory mapped successfully.\n");
    pthread_t stid;
    
    if (pthread_create(&stid, NULL, shm_listener_thread, NULL) != 0) {
        perror("[3D VIEW] Failed to create listener thread");
        exit(1);
    }

    printf("[3D VIEW] Initializing GLUT (check DISPLAY: %s)...\n", getenv("DISPLAY"));
    glutInit(&argc, argv); 
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH | GLUT_MULTISAMPLE); glutInitWindowSize(1280, 720); glutCreateWindow("Space GL 3DView - Multiuser");
    
    /* Initialize GLEW */
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (GLEW_OK != err) {
        fprintf(stderr, "[3D VIEW] GLEW Error: %s\n", glewGetErrorString(err));
        return 1;
    }
    printf("[3D VIEW] GLEW initialized. OpenGL Version: %s\n", glGetString(GL_VERSION));

    /* Initialize Bloom FBOs */
    initBloomFBO();

    /* Initialize Shader Engine */
    initShaders();

    glEnable(GL_DEPTH_TEST); 
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND); 
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    /* Enable Point Size control from Vertex Shader and Point Sprites */
    glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
    glEnable(GL_POINT_SPRITE);
    glTexEnvi(GL_POINT_SPRITE, GL_COORD_REPLACE, GL_TRUE);

    glShadeModel(GL_SMOOTH);
    glEnable(GL_LIGHTING); 
    glEnable(GL_LIGHT0); 
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_NORMALIZE);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    /* Ambient Light (Shadow fill) */
    GLfloat global_amb[] = {0.2f, 0.2f, 0.25f, 1.0f};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, global_amb);

    /* Main Light (Headlight) */
    GLfloat lp[] = {0, 0, 10, 1}; /* Light coming from camera direction */
    glLightfv(GL_LIGHT0, GL_POSITION, lp);
    GLfloat white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glLightfv(GL_LIGHT0, GL_DIFFUSE, white);
    glLightfv(GL_LIGHT0, GL_SPECULAR, white);
        initStars(); initVBOs(); glMatrixMode(GL_PROJECTION); gluPerspective(45, 1.77, 1, 500); glMatrixMode(GL_MODELVIEW);
        glutDisplayFunc(display); glutReshapeFunc(reshape); glutKeyboardFunc(keyboard); glutSpecialFunc(special); glutTimerFunc(16, timer, 0);
        
        printf("[3D VIEW] Ready. Sending handshake to parent (PID %d).\n", getppid());
        kill(getppid(), SIGUSR2); 
        glutMainLoop(); 
        return 0;
}
    
