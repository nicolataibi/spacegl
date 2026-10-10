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
#include <GLFW/glfw3.h>
#include "glut_compat.h"
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
#include <inttypes.h>
#include <omp.h>
#include "spacegl_extras_glfw.h"
#include "shared_state.h"
#include "sglog.h"

#define IS_Q_VALID(q1,q2,q3) ((q1)>=1 && (q1)<=GALAXY_SIZE && (q2)>=1 && (q2)<=GALAXY_SIZE && (q3)>=1 && (q3)<=GALAXY_SIZE)

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#include "network.h"

typedef struct {
    double x, y, z;
    double vx, vy, vz;
    double r, g, b, a;
    double size;
    double life;
    int active;
} FXParticle;

void spawnParticle(double x, double y, double z, double vx, double vy, double vz, double r, double g, double b, double size, double life);
void getFactionColor(int f, float* r, float* g, float* b);

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

/* Live framebuffer size (updated by reshape); the window is resizable, so
 * everything size-dependent (bloom FBOs, projection aspect, HUD text) must
 * follow it instead of the TACTICAL_CUBE_W/H macros. */
int g_fb_w = TACTICAL_CUBE_W;
int g_fb_h = TACTICAL_CUBE_H;

/* Size the bloom FBO chain was actually allocated at (0 = not created). */
int bloom_w = 0, bloom_h = 0;

#define MAX_PARTICLES 16384
typedef struct {
    float x, y, z;
    float r, g, b, a;
    float size;
} ParticleVertex;

FXParticle fx_particles[MAX_PARTICLES];
ParticleVertex particle_vertex_buffer[MAX_PARTICLES];
GLuint vbo_particles = 0;
GLuint particleShaderProgram = 0;

/* Shader Globals */
GLuint skyboxShaderProgram = 0;
GLuint hullShaderProgram = 0;
GLuint starShaderProgram = 0;
GLuint bhShaderProgram = 0;
GLuint whShaderProgram = 0;
GLuint cloakShaderProgram = 0;
GLuint asteroidVAO = 0, asteroidVBO = 0, asteroidInstanceVBO = 0;
GLuint asteroidInstancedShaderProgram = 0;

/* Asteroid Instancing Shader */
const char* asteroidInstancedVert = "#version 120\n"
    "attribute vec3 position;\n"
    "attribute vec3 instancePos;\n"
    "attribute float instanceScale;\n"
    "attribute float instanceRot;\n"
    "varying vec3 vNormal;\n"
    "void main() {\n"
    "    float s = sin(instanceRot); float c = cos(instanceRot);\n"
    "    mat3 rot = mat3(c, 0, s, 0, 1, 0, -s, 0, c);\n"
    "    vec3 pos = (position * instanceScale) * rot + instancePos;\n"
    "    vNormal = rot * position;\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * vec4(pos, 1.0);\n"
    "    gl_FrontColor = vec4(0.5, 0.35, 0.25, 1.0);\n"
    "}";

const char* asteroidInstancedFrag = "#version 120\n"
    "varying vec3 vNormal;\n"
    "void main() {\n"
    "    float diff = max(dot(normalize(vNormal), vec3(0.5, 0.7, 1.0)), 0.2);\n"
    "    gl_FragColor = gl_Color * diff;\n"
    "}";

typedef struct {
    float x, y, z;
    float scale;
    float rot;
} AsteroidInstanceData;

AsteroidInstanceData asteroidData[1000];
int asteroidInstanceCount = 0;

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
        "attribute float pSize;\n"
        "void main() {\n"
        "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
        "    gl_FrontColor = gl_Color;\n"
        "    /* Sharp fragments: dynamic size based on distance */\n"
        "    gl_PointSize = pSize * " STR(QUADRANT_SIZE) " * (1.0 / length(gl_ModelViewMatrix * gl_Vertex));\n"
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
        "    gl_FrontColor = gl_Color;\n"
        "    gl_Position = vScreenPos;\n"
        "}";
    bhShaderProgram = linkProgram(compileShader(bhVert, GL_VERTEX_SHADER), compileShader(bhFrag, GL_FRAGMENT_SHADER));

    /* Wormhole Shader — respects vertex color (gl_Color) set by glColor4f */
    const char* whFrag = "#version 120\n"
        "uniform float time;\n"
        "varying vec3 pos;\n"
        "void main() {\n"
        "  float d = length(pos);\n"
        "  float ripple = sin(d * 30.0 - time * 20.0) * 0.5 + 0.5;\n"
        "  vec3 col = gl_Color.rgb;\n"
        "  /* Specular highlight uses same hue as vertex color, not a fixed blue */\n"
        "  vec3 highlight = col * pow(ripple, 3.0) * 1.5;\n"
        "  gl_FragColor = vec4(col * (0.4 + 0.6 * ripple) + highlight, gl_Color.a);\n"
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
    asteroidInstancedShaderProgram = linkProgram(compileShader(asteroidInstancedVert, GL_VERTEX_SHADER), compileShader(asteroidInstancedFrag, GL_FRAGMENT_SHADER));

    /* Initialize Asteroid VBO (Low-poly dodecahedron style cube) */
    float asteroidVertices[] = {
        -0.1,-0.1,-0.1,  0.1,-0.1,-0.1,  0.1, 0.1,-0.1, -0.1, 0.1,-0.1,
        -0.1,-0.1, 0.1,  0.1,-0.1, 0.1,  0.1, 0.1, 0.1, -0.1, 0.1, 0.1,
        -0.1,-0.1,-0.1, -0.1, 0.1,-0.1, -0.1, 0.1, 0.1, -0.1,-0.1, 0.1,
         0.1,-0.1,-0.1,  0.1, 0.1,-0.1,  0.1, 0.1, 0.1,  0.1,-0.1, 0.1,
        -0.1,-0.1,-0.1,  0.1,-0.1,-0.1,  0.1,-0.1, 0.1, -0.1,-0.1, 0.1,
        -0.1, 0.1,-0.1,  0.1, 0.1,-0.1,  0.1, 0.1, 0.1, -0.1, 0.1, 0.1
    };
    glGenBuffers(1, &asteroidVBO);
    glBindBuffer(GL_ARRAY_BUFFER, asteroidVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(asteroidVertices), asteroidVertices, GL_STATIC_DRAW);

    glGenBuffers(1, &asteroidInstanceVBO);
    glBindBuffer(GL_ARRAY_BUFFER, asteroidInstanceVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(asteroidData), NULL, GL_DYNAMIC_DRAW);
    
    /* BLOOM: Simple Quad Vertex Shader */
    const char* quadVert = "#version 120\n"
        "attribute vec2 position;\n"
        "varying vec2 TexCoords;\n"
        "void main() {\n"
        "    gl_Position = vec4(position.x, position.y, 0.0, 1.0);\n"
        "    TexCoords = (position + 1.0) / 2.0;\n"
        "}";

    /* BLOOM: Blur Fragment Shader (Two-Pass Gaussian)
     * The texel size is a uniform (not baked into the source): the FBOs are
     * recreated at the live window size on resize, so the kernel offset must
     * follow without recompiling the program. */
    const char* blurFrag = "#version 120\n"
        "uniform sampler2D image;\n"
        "uniform bool horizontal;\n"
        "uniform vec2 texel;\n"
        "varying vec2 TexCoords;\n"
        "void main() {\n"
        "    float weight[5] = float[] (0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);\n"
        "    vec2 tex_offset = texel;\n"
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
        "    /* Simple additive mixing as originally intended */\n"
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
            -1.0,  1.0,
            -1.0, -1.0,
             1.0,  1.0,
             1.0, -1.0,
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

static void destroyBloomFBO() {
    /* Free the whole chain (all handles are 0-safe) and reset the size stamp. */
    glDeleteFramebuffers(1, &fbo_msaa);
    glDeleteRenderbuffers(1, &rbo_color_msaa);
    glDeleteRenderbuffers(1, &rbo_depth_msaa);
    glDeleteFramebuffers(1, &fbo_scene);
    glDeleteTextures(1, &tex_scene);
    glDeleteFramebuffers(2, fbo_pingpong);
    glDeleteTextures(2, tex_pingpong);
    fbo_msaa = rbo_color_msaa = rbo_depth_msaa = 0;
    fbo_scene = tex_scene = 0;
    fbo_pingpong[0] = fbo_pingpong[1] = tex_pingpong[0] = tex_pingpong[1] = 0;
    bloom_w = bloom_h = 0;
}

void initBloomFBO(int w, int h) {
    /* The chain is size-dependent: w/h are the LIVE framebuffer size, not the
     * native 1920x1080 (the window is resizable). */
    /* 1. MSAA Framebuffer (Initial Render Target) */
    glGenFramebuffers(1, &fbo_msaa);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_msaa);

    glGenRenderbuffers(1, &rbo_color_msaa);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo_color_msaa);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGB16F, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo_color_msaa);

    glGenRenderbuffers(1, &rbo_depth_msaa);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo_depth_msaa);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo_depth_msaa);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        printf("[BLOOM] ERROR: MSAA Framebuffer not complete!\n");

    /* 2. Scene FBO (Resolve Target for MSAA) */
    glGenFramebuffers(1, &fbo_scene);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_scene);

    glGenTextures(1, &tex_scene);
    glBindTexture(GL_TEXTURE_2D, tex_scene);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, NULL);
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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); 
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_pingpong[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            printf("[BLOOM] ERROR: PingPong Framebuffer %d not complete!\n", i);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    bloom_w = w;
    bloom_h = h;
    printf("[BLOOM] FBOs initialized at %dx%d.\n", w, h);
}

/* Recreate the bloom chain if it does not match the requested size. Called
 * from display() as a backstop (reshape() already does this on the resize
 * callback; this covers the first frame and any coalesced callback). */
static void ensureBloomFBO(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (bloom_w == w && bloom_h == h) return;
    destroyBloomFBO();
    initBloomFBO(w, h);
}

int shm_fd = -1;
SharedIPC *g_shm = NULL;
GameState *g_shared_state = NULL; /* Current read buffer */

volatile int g_data_dirty = 0;

void send_ipc_command(const char* cmd_str) {
    if (!g_shm) return;
    int tail = atomic_load_explicit(&g_shm->cmd_tail, memory_order_relaxed);
    int head = atomic_load_explicit(&g_shm->cmd_head, memory_order_acquire);
    
    int next_tail = (tail + 1) % CMD_QUEUE_SIZE;
    if (next_tail != head) {
        /* Bounded copy with guaranteed NUL termination (the pre-fix
         * strncpy(..., 127) left a source of 127+ chars unterminated,
         * so the client's drain could over-read into the next slot).
         * The bounded memcpy also keeps -Wstringop-truncation quiet. */
        size_t n = strlen(cmd_str);
        if (n >= sizeof(g_shm->cmd_queue[tail].cmd))
            n = sizeof(g_shm->cmd_queue[tail].cmd) - 1;
        memcpy(g_shm->cmd_queue[tail].cmd, cmd_str, n);
        g_shm->cmd_queue[tail].cmd[n] = '\0';
        atomic_store_explicit(&g_shm->cmd_tail, next_tail, memory_order_release);
    }
}

void *shm_listener_thread(void *arg) {
    (void)arg;
    while(1) {
        if (g_shm) {
            sem_wait(&g_shm->data_ready);
            
            /* Update read buffer pointer atomicaly */
            int r_idx = atomic_load_explicit(&g_shm->read_index, memory_order_acquire);
            g_shared_state = &g_shm->buffers[r_idx];
            
            g_data_dirty = 1;
        } else usleep(10000);
    }
    return NULL;
}
volatile int g_is_loading = 0;
int g_is_cloaked_rendering = 0;
int g_is_derelict_rendering = 0;
float angleY = 0.0;
float angleX = 20.0;
float zoom = -65.0;
float autoRotate = 0.075;
int g_aniso_level = 4;
int g_star_count = 2000;
float pulse = 0.0;
float map_anim = 0.0;
float bridge_anim = 0.0;
GLdouble hud_model[16], hud_proj[16];
GLint hud_view[4];
int g_shield_hit_timers[6] = {0,0,0,0,0,0};
int g_hull_hit_timer = 0;
float g_last_hull = (float)YIELD_HARVEST_MAX;
int g_last_shields_val_hit[6] = {SHIELD_MAX_STRENGTH, SHIELD_MAX_STRENGTH, SHIELD_MAX_STRENGTH, SHIELD_MAX_STRENGTH, SHIELD_MAX_STRENGTH, SHIELD_MAX_STRENGTH};

uint64_t g_energy = 0;
int g_crew = 0, g_prison_unit = 0, g_shields = 0, g_Korthians = 0;
int g_composite_plating = 0;
float g_hull_integrity = (float)YIELD_HARVEST_MAX;
int g_shields_val[6] = {0};
uint64_t g_cargo_energy = 0;
int g_cargo_torps = 0, g_torpedoes_launcher = 0;
float g_system_health[10] = {0};
float g_power_dist[3] = {0.33, 0.33, 0.34};
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
int g_cloaked = 0;
int g_tube_state = 0;
float g_ion_charge = 0.0;
int g_map_filter = 0;
int g_my_q[3] = {1,1,1};
int64_t g_galaxy[41][41][41];
int g_show_hud = 1; /* Default HUD ON */
int g_show_nebulas = 1; /* Default Nebulae ON */
char g_quadrant[128] = "Scanning...";
char g_last_quadrant[128] = "";
char g_player_name[64] = "Unknown";
int g_player_class = 0;

#define MAX_TRAIL 40
typedef struct {
    float x, y, z;
    float tx, ty, tz; /* Interpolation targets */
    float vx, vy, vz;
    float h, m, r;
    float th, tm, tr;     /* Target heading, mark and roll */
    int type;
    int ship_class;
    int health_pct;   /* HUD */
    uint64_t energy;       /* HUD */
        int plating;       /* HUD */
        int hull_integrity; /* HUD */
        int faction;       /* HUD */
        int id;           /* HUD */
        int is_cloaked;   /* Cloaking Device status */
        char name[64];    /* HUD Name */
    
    float trail[MAX_TRAIL][3];
    int trail_ptr;
    int trail_count;
    float last_update_time;
    int q1, q2, q3;
} GameObject;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float r, g, b;
    int active;
} Particle;

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

GameObject objects[MAX_OBJECTS];
int objectCount = 0;
ViewProbe g_local_probes[3];
ArrivalEffect g_arrival_fx = {0};
RecoveryFX g_recovery_fx = {0};

/* Removed global variables for single trail for universal trail */
typedef struct { float sx, sy, sz, tx, ty, tz, alpha; } IonBeam;
IonBeam beams[MAX_BEAMS];
int beamCount = 0;

typedef struct { float x, y, z, tx, ty, tz, vx, vy, vz, h, m, x_init, y_init, z_init; int active; int timer; int q1, q2, q3; int id; int jump_type; } ViewPoint;
ViewPoint g_torps[MAX_VISIBLE_TORPEDOES];
int g_torpedo_count = 0;
ViewPoint g_booms[10];
int boom_idx = 0;
ViewPoint g_wormhole = {0};
ViewPoint g_jump_arrival = {0};
ViewPoint g_sn_pos = {0};
int g_sn_q[3] = {0,0,0};

float PlayerX = 0, PlayerY = 0, PlayerZ = 0;
float stars[8000][3];

/* Prototypes */
void drawStarbase(double x, double y, double z, int faction);
void drawPlanet(double x, double y, double z);
void drawGrid();
void drawKorthian(double x, double y, double z);
void drawXylari(double x, double y, double z);
void drawSwarm(double x, double y, double z);
void drawVesperian(double x, double y, double z);
void drawAscendant(double x, double y, double z);
void drawQuarzite(double x, double y, double z);
void drawSaurian(double x, double y, double z);
void drawGilded(double x, double y, double z);
void drawFluidicVoid(double x, double y, double z);
void drawCryos(double x, double y, double z);
void drawApex(double x, double y, double z);
void drawNacelle(double len, double width, double r, double g, double b);
void drawDeflector(double r, double g, double b);
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
void drawGlow(double radius, double r, double g, double b, double alpha);
void drawHullDetail(void (*drawFunc)(void), double r, double g, double b);
void drawNavLights(float x, float y, float z);
void drawCommandModule(double sx, double sy, double sz);
void glutSolidSphere_wrapper_module();
void glutSolidSphere_wrapper_hull();
void glutSolidSphere_wrapper_escort();
void glutSolidCube_wrapper();

void handle_signal(int sig) { if (sig == SIGUSR1) g_data_dirty = 1; }

void initStars() {
    for(int i=0; i<8000; i++) {
        float r = 150.0 + (rand()%100);
        float t = (rand()%360) * 3.14/180;
        float p = (rand()%360) * 3.14/180;
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
    if (!grid_data) { perror("[3D VIEW] Failed to allocate grid VBO"); exit(1); }
    int idx = 0;
    for(int i=0; i<=(int)QUADRANT_SIZE; i+=10) {
        float p = -20.0 + i;
        for(int j=0; j<=(int)QUADRANT_SIZE; j+=10) {
            float q = -20.0 + j;
            grid_data[idx++] = p; grid_data[idx++] = q; grid_data[idx++] = -20.0;
            grid_data[idx++] = p; grid_data[idx++] = q; grid_data[idx++] = 20.0;
            grid_data[idx++] = p; grid_data[idx++] = -20.0; grid_data[idx++] = q;
            grid_data[idx++] = p; grid_data[idx++] = 20.0; grid_data[idx++] = q;
            grid_data[idx++] = -20.0; grid_data[idx++] = p; grid_data[idx++] = q;
            grid_data[idx++] = 20.0; grid_data[idx++] = p; grid_data[idx++] = q;
        }
    }
    grid_vertex_count = idx / 3;
    glGenBuffers(1, &vbo_grid);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_grid);
    glBufferData(GL_ARRAY_BUFFER, idx * sizeof(float), grid_data, GL_STATIC_DRAW);
    free(grid_data);

    /* Initialize Particle VBO */
    glGenBuffers(1, &vbo_particles);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_particles);
    glBufferData(GL_ARRAY_BUFFER, sizeof(particle_vertex_buffer), NULL, GL_DYNAMIC_DRAW);
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

long long last_frame_id = -1;

void loadGameState() {
    if (g_shm && atomic_load(&g_shm->force_shutdown)) {
        printf("[3D VIEW] GLOBAL EMERGENCY SHUTDOWN SIGNAL RECEIVED. CLEAN EXIT.\n");
        exit(0);
    }
    GameState *state = g_shared_state;
    if (!state) return;
    
    if (state->shm_force_shutdown) {
        printf("[3D VIEW] EMERGENCY SHUTDOWN SIGNAL RECEIVED (xxx). CLEAN EXIT.\n");
        exit(0);
    }

    /* Process Persistent Event Queue (Transient Effects) */
    if (g_shm) {
        int head = atomic_load_explicit(&g_shm->event_head, memory_order_acquire);
        int tail = atomic_load_explicit(&g_shm->event_tail, memory_order_acquire);
        
        static int last_telemetry_count = 0;
        if (g_shm->dismantle_telemetry.count > last_telemetry_count) {
             /* Telemetry suggests an event WAS sent, check if we catch it */
        }

        while (head != tail) {
            IPCEvent *ev = &g_shm->event_queue[head];
            
            if (ev->type == IPC_EV_DISMANTLE) {
                if (g_shm) g_shm->dismantle_telemetry.count++;
                printf("[GLFW] Processing IPC_EV_DISMANTLE at Sector(%.2f, %.2f, %.2f)\n", ev->x1, ev->y1, ev->z1);
                
                float dx = ev->x1 - (QUADRANT_SIZE / 2.0);
                float dy = ev->z1 - (QUADRANT_SIZE / 2.0);
                float dz = (QUADRANT_SIZE / 2.0) - ev->y1;
                
                /* VIBRANT MULTI-COLORED PIXEL EXPLOSION */
                for(int i=0; i<800; i++) { 
                    float speed = 0.8 + (rand()%200)/100.0;
                    float theta = (rand()%360) * M_PI / 180.0;
                    float phi = (rand()%180 - 90) * M_PI / 180.0;
                    float vx = speed * cos(phi) * cos(theta);
                    float vy = speed * sin(phi);
                    float vz = speed * cos(phi) * sin(theta);
                    float r = (rand()%100)/100.0; float g = (rand()%100)/100.0; float b = (rand()%100)/100.0;
                    if (r < 0.3 && g < 0.3 && b < 0.3) { r += 0.5; g += 0.5; b += 0.5; }
                    float p_size = 5.0 + (rand()%1500)/100.0;
                    float p_life = 4.0 + (rand()%400)/100.0;
                    spawnParticle(dx, dy, dz, vx, vy, vz, r, g, b, p_size, p_life);
                }
                for(int i=0; i<100; i++) {
                    float vx = ((rand()%100)-50)/5.0; float vy = ((rand()%100)-50)/5.0; float vz = ((rand()%100)-50)/5.0;
                    spawnParticle(dx, dy, dz, vx, vy, vz, 1.0, 1.0, 1.0, 15.0, 1.5);
                }
            } else if (ev->type == IPC_EV_BEAM) {
                SG_TRACE3(SG_CAT_PHA, "view[GL]: consume IPC_EV_BEAM owner=%d target=%d emitter=%d s=(%.2f,%.2f,%.2f) t=(%.2f,%.2f,%.2f)",
                          ev->padding[0], ev->extra, ev->padding[1],
                          ev->x1, ev->y1, ev->z1, ev->x2, ev->y2, ev->z2);
                int slot = -1;
                for(int j=0; j<64; j++) if(beams[j].alpha <= 0) { slot = j; break; }
                if (slot == -1) {
                    if (sglog_rate("gl_beam_full", 2))
                        SG_WARNING(SG_CAT_RENDER, "view[GL]: beams[64] FULL - overwriting slot with random() (beams will flicker)");
                    slot = rand()%64;
                }
                beams[slot].sx = ev->x1 - (QUADRANT_SIZE / 2.0);
                beams[slot].sy = ev->z1 - (QUADRANT_SIZE / 2.0);
                beams[slot].sz = (QUADRANT_SIZE / 2.0) - ev->y1;
                beams[slot].tx = ev->x2 - (QUADRANT_SIZE / 2.0);
                beams[slot].ty = ev->z2 - (QUADRANT_SIZE / 2.0);
                beams[slot].tz = (QUADRANT_SIZE / 2.0) - ev->y2;
                beams[slot].alpha = 1.0;
            } else if (ev->type == IPC_EV_BOOM) {
                /* Explosion matches server physics instantly */
                int current_boom = boom_idx;
                g_booms[current_boom].x = ev->x1 - (QUADRANT_SIZE / 2.0);
                g_booms[current_boom].y = ev->z1 - (QUADRANT_SIZE / 2.0);
                g_booms[current_boom].z = (QUADRANT_SIZE / 2.0) - ev->y1;
                g_booms[current_boom].active = 1;
                g_booms[current_boom].timer = 60; /* 1 second visual lifespan */
                boom_idx = (boom_idx + 1) % 10;
                
                /* Instantly despawn the specific torpedo or any very close ones */
                for(int s=0; s<MAX_VISIBLE_TORPEDOES; s++) {
                    if (g_torps[s].active) {
                        /* Prioritize ID matching from server (using IPC_TORPEDO_ID_OFFSET) */
                        if (ev->extra >= IPC_TORPEDO_ID_OFFSET && g_torps[s].id == ev->extra) {
                            g_torps[s].active = 0;
                            continue;
                        }
                        /* Fallback: Proximity check */
                        float dist = sqrtf(powf(g_torps[s].x - g_booms[current_boom].x, 2) + 
                                           powf(g_torps[s].y - g_booms[current_boom].y, 2) + 
                                           powf(g_torps[s].z - g_booms[current_boom].z, 2));
                        if (dist < 2.5f) {
                            g_torps[s].active = 0;
                            /* Snap to explosion point for visual consistency before deactivating */
                            g_torps[s].x = g_booms[current_boom].x;
                            g_torps[s].y = g_booms[current_boom].y;
                            g_torps[s].z = g_booms[current_boom].z;
                        }
                    }
                }
            } else if (ev->type == IPC_EV_RECOVERY) {
                g_recovery_fx.x = ev->x1 - (QUADRANT_SIZE / 2.0);
                g_recovery_fx.y = ev->z1 - (QUADRANT_SIZE / 2.0);
                g_recovery_fx.z = (QUADRANT_SIZE / 2.0) - ev->y1;
                g_recovery_fx.timer = (int)GAME_TICK_RATE;
            } else if (ev->type == IPC_EV_JUMP) {
                float jx = ev->x1 - 20.0; float jy = ev->z1 - 20.0; float jz = 20.0 - ev->y1;
                g_jump_arrival.x = jx; g_jump_arrival.y = jy; g_jump_arrival.z = jz;
                g_jump_arrival.h = state->shm_h; g_jump_arrival.m = state->shm_m; g_jump_arrival.active = 1;
                g_jump_arrival.jump_type = ev->extra;
                g_jump_arrival.timer = (5 * GAME_TICK_RATE);
                g_arrival_fx.x = jx; g_arrival_fx.y = jy; g_arrival_fx.z = jz;
                g_arrival_fx.timer = (5 * GAME_TICK_RATE);
                for(int i=0; i<150; i++) {
                    float theta = (rand() % 360) * M_PI / 180.0; float phi = (rand() % 180 - 90) * M_PI / 180.0;
                    float dist = 3.0 + (rand() % 200) / 100.0;
                    g_arrival_fx.particles[i].x = g_arrival_fx.x + dist * cos(phi) * cos(theta);
                    g_arrival_fx.particles[i].y = g_arrival_fx.y + dist * sin(phi);
                    g_arrival_fx.particles[i].z = g_arrival_fx.z + dist * cos(phi) * sin(theta);
                    g_arrival_fx.particles[i].vx = (g_arrival_fx.x - g_arrival_fx.particles[i].x) / 100.0;
                    g_arrival_fx.particles[i].vy = (g_arrival_fx.y - g_arrival_fx.particles[i].y) / 100.0;
                    g_arrival_fx.particles[i].vz = (g_arrival_fx.z - g_arrival_fx.particles[i].z) / 100.0;
                    g_arrival_fx.particles[i].r = (rand() % 100) / 100.0; g_arrival_fx.particles[i].g = (rand() % 100) / 100.0; g_arrival_fx.particles[i].b = (rand() % 100) / 100.0;
                    g_arrival_fx.particles[i].active = 1;
                }
            } else if (ev->type == IPC_EV_TORPEDO) {
                int tid = ev->extra;
                float n_tx = ev->x1 - (QUADRANT_SIZE / 2.0); 
                float n_ty = ev->z1 - (QUADRANT_SIZE / 2.0); 
                float n_tz = (QUADRANT_SIZE / 2.0) - ev->y1;

                int found = -1;
                /* Check if this specific torpedo ID is already active */
                for(int s=0; s<MAX_VISIBLE_TORPEDOES; s++) {
                    if (g_torps[s].active && g_torps[s].id == tid) {
                        found = s;
                        break;
                    }
                }

                /* If not found, look for an empty slot to spawn a new one */
                if (found == -1) {
                    for(int s=0; s<MAX_VISIBLE_TORPEDOES; s++) {
                        if (!g_torps[s].active) {
                            found = s;
                            break;
                        }
                    }
                }

                if (found != -1) {
                    /* Position correction: Blend current local position with server-authoritative position 
                       to reduce visual jump while maintaining synchronization */
                    if (!g_torps[found].active) {
                        g_torps[found].x = n_tx;
                        g_torps[found].y = n_ty;
                        g_torps[found].z = n_tz;
                    } else {
                        /* Incremental correction to fix drift */
                        g_torps[found].x = g_torps[found].x * 0.7f + n_tx * 0.3f;
                        g_torps[found].y = g_torps[found].y * 0.7f + n_ty * 0.3f;
                        g_torps[found].z = g_torps[found].z * 0.7f + n_tz * 0.3f;
                    }
                    g_torps[found].active = 1;
                    g_torps[found].id = tid;
                    
                    /* Always update target and velocity */
                    g_torps[found].tx = n_tx;
                    g_torps[found].ty = n_ty;
                    g_torps[found].tz = n_tz;
                    /* True velocity matching server physics */
                    g_torps[found].vx = ev->x2;
                    g_torps[found].vy = ev->z2;
                    g_torps[found].vz = -ev->y2;

                    /* Dynamic Lifecycle: Allow it to fly until boundary impact or timeout */
                    float speed = sqrtf(powf(g_torps[found].vx, 2) + 
                                        powf(g_torps[found].vy, 2) + 
                                        powf(g_torps[found].vz, 2));
                    if (speed > 0.01f) {
                        /* Strict margin (1.2x travel time) to avoid overshooting after server-authoritative death */
                        g_torps[found].timer = (int)((QUADRANT_SIZE * 1.2f / speed)) + 60;
                    } else {
                        g_torps[found].timer = 1200; /* 20 seconds fallback */
                    }

                    g_torps[found].h = 600;
                    g_torps[found].q1 = g_shared_state->shm_q[0];
                    g_torps[found].q2 = g_shared_state->shm_q[1];
                    g_torps[found].q3 = g_shared_state->shm_q[2];
                } else {
                    printf("[DEBUG] No slots available for torpedo ID %d\n", tid);
                }
            }

            head = (head + 1) % IPC_EVENT_QUEUE_SIZE;
            atomic_store_explicit(&g_shm->event_head, head, memory_order_release);
        }
    }

    if (state->frame_id == last_frame_id) { return; }
    last_frame_id = state->frame_id;
    g_is_loading = 1;
    g_energy = state->shm_energy;
    g_composite_plating = state->shm_composite_plating;
    g_hull_integrity = state->shm_hull_integrity;
    g_crew = state->shm_crew;
    g_prison_unit = state->shm_prison_unit;
    g_torpedoes_launcher = state->shm_torpedoes;
    g_cargo_energy = state->shm_cargo_energy;
    g_cargo_torps = state->shm_cargo_torpedoes;
    for(int s=0; s<10; s++) g_system_health[s] = state->shm_system_health[s];
    for(int p=0; p<3; p++) g_power_dist[p] = state->shm_power_dist[p];
    for(int inv=0; inv<10; inv++) g_inventory[inv] = state->inventory[inv];
    g_lock_target = state->shm_lock_target;
    g_is_docked = state->shm_is_docked;
    g_red_alert = state->shm_red_alert;
    g_is_jammed = state->shm_is_jammed;
    g_nav_state = state->shm_nav_state;
    g_cloaked = state->is_cloaked;
    g_tube_state = state->shm_tube_state;
    g_ion_charge = state->shm_ion_beam_charge;
    int total_s = 0;
    /* Shield Hit Timers are now updated in drawShieldEffect to ensure perfect sync with rendering */
    for(int s=0; s<6; s++) total_s += state->shm_shields[s];
    
            /* Detect Direct Hull Hit: if hull decreased */
            if (g_hull_integrity < g_last_hull) {
                g_hull_hit_timer = (GAME_TICK_RATE / 3); /* Red pulse duration */
                /* Spawn fast solid metallic fragments on impact */
                for(int k=0; k<MAX_TRAIL; k++) {
    
            float ox = ((double)rand()/(double)RAND_MAX - 0.5) * 0.3;
            float oy = ((double)rand()/(double)RAND_MAX - 0.5) * 0.3;
            float oz = ((double)rand()/(double)RAND_MAX - 0.5) * 0.3;
            float vx = ((double)rand()/(double)RAND_MAX - 0.5) * 0.18;
            float vy = ((double)rand()/(double)RAND_MAX - 0.5) * 0.18;
            float vz = ((double)rand()/(double)RAND_MAX - 0.5) * 0.18;
            
            /* Varied colors: Steel, Copper, Spark, White */
            float r, g, b;
            int type = rand() % 4;
            if (type == 0) { r=0.7; g=0.7; b=0.8; }      /* Steel */
            else if (type == 1) { r=0.9; g=0.5; b=0.2; } /* Copper/Bronze */
            else if (type == 2) { r=1.0; g=0.8; b=0.1; } /* Hot Spark */
            else { r=1.0; g=1.0; b=1.0; }                /* White hot */
            
            float size = 0.1 + ((double)rand()/(double)RAND_MAX * 0.2);
            spawnParticle(PlayerX + ox, PlayerY + oy, PlayerZ + oz, vx, vy, vz, r, g, b, size, 0.7);
        }
    }
    g_last_hull = g_hull_integrity;

    g_shields = total_s / 6;
    for(int s=0; s<6; s++) g_shields_val[s] = state->shm_shields[s];

    /* Torpedo State Sampling: Disabled in favor of Zero-Loss IPC Events */
    /* All torpedo activation and lifecycle logic is now handled in IPC_EV_TORPEDO handler */

    g_Korthians = state->Korthians;
    size_t nlen = strlen(state->objects[0].shm_name);
    if (nlen > 63) nlen = 63;
    memcpy(g_player_name, state->objects[0].shm_name, nlen);
    g_player_name[nlen] = '\0';
    g_player_class = state->objects[0].ship_class;
    
    int quadrant_changed = 0;
    if (strcmp(g_quadrant, state->quadrant) != 0) {
        quadrant_changed = 1;
        strcpy(g_quadrant, state->quadrant);
        /* Cleanup local visual effects on sector jump,
           but PRESERVE the arrival wormhole if it was just activated this frame.
           A freshly-set timer equals exactly 5 * GAME_TICK_RATE. */
        g_wormhole.active = 0;
        if (g_jump_arrival.timer != (5 * GAME_TICK_RATE)) {
            g_jump_arrival.timer = 0;
        }
    }
    
    g_show_axes = state->shm_show_axes;
    g_show_grid = state->shm_show_grid;
    g_show_map = state->shm_show_map;
    g_show_bridge = state->shm_show_bridge;
    g_map_filter = state->shm_map_filter;
    g_my_q[0] = state->shm_q[0];
    g_my_q[1] = state->shm_q[1];
    g_my_q[2] = state->shm_q[2];
    
    /* Global PlayerX/Y/Z are now synchronized smoothly inside the timer loop via objects[0] */

    memcpy(g_galaxy, g_shm->shm_galaxy, sizeof(g_galaxy));

    for(int p=0; p<3; p++) {
        g_local_probes[p].active = state->probes[p].active;
        g_local_probes[p].q1 = state->probes[p].q1;
        g_local_probes[p].q2 = state->probes[p].q2;
        g_local_probes[p].q3 = state->probes[p].q3;
        g_local_probes[p].eta = state->probes[p].eta;
        g_local_probes[p].status = state->probes[p].status;
        g_local_probes[p].x = state->probes[p].s1 - (QUADRANT_SIZE / 2.0);
        g_local_probes[p].y = state->probes[p].s3 - (QUADRANT_SIZE / 2.0);
        g_local_probes[p].z = (QUADRANT_SIZE / 2.0) - state->probes[p].s2;
    }

    int updated[MAX_OBJECTS] = {0};
    objectCount = state->object_count;
    
    /* 1. FORCE player ship at index 0 for consistent HUD/Camera behavior */
    if (objectCount > 0) {
        int target_id = state->objects[0].id;
        objects[0].id = target_id;
        updated[0] = 1;
        GameObject *obj = &objects[0];
        float next_x = state->objects[0].shm_x - (QUADRANT_SIZE / 2.0);
        float next_y = state->objects[0].shm_z - (QUADRANT_SIZE / 2.0);
        float next_z = (QUADRANT_SIZE / 2.0) - state->objects[0].shm_y;
        
        float dx = next_x - obj->x;
        float dy = next_y - obj->y;
        float dz = next_z - obj->z;
        
        if (quadrant_changed || obj->x < -200.0 || (dx*dx + dy*dy + dz*dz) > 400.0) {
            obj->x = obj->tx = next_x;
            obj->y = obj->ty = next_y;
            obj->z = obj->tz = next_z;
            obj->h = obj->th = state->objects[0].h;
            obj->m = obj->tm = state->objects[0].m;
            obj->r = obj->tr = state->objects[0].r;
            obj->trail_count = 0;
            obj->trail_ptr = 0;
        } else {
            obj->tx = next_x;
            obj->ty = next_y;
            obj->tz = next_z;
        }
        obj->th = state->objects[0].h;
        obj->tm = state->objects[0].m;
        obj->tr = state->objects[0].r;
        obj->last_update_time = glutGet(GLUT_ELAPSED_TIME);
        obj->type = state->objects[0].type;
        obj->ship_class = state->objects[0].ship_class;
        obj->health_pct = state->objects[0].health_pct;
        obj->energy = state->objects[0].energy;
        obj->plating = state->objects[0].plating;
        obj->hull_integrity = state->objects[0].hull_integrity;
        obj->faction = state->objects[0].faction;
        obj->is_cloaked = state->objects[0].is_cloaked;
        obj->vx = (float)state->objects[0].vx;
        obj->vy = (float)state->objects[0].vz; // Mapping: Stellar Z -> Viewer Y
        obj->vz = -(float)state->objects[0].vy; // Mapping: Stellar Y -> -Viewer Z
        strncpy(obj->name, state->objects[0].shm_name, sizeof(obj->name) - 1);
        obj->name[sizeof(obj->name) - 1] = '\0';
    }

    /* 2. Process remaining objects starting from index 1 */
    for(int i=1; i<objectCount; i++) {
        int target_id = state->objects[i].id;
        int local_idx = -1;

        /* Find existing object by ID (skip index 0) */
        for(int k=1; k<MAX_OBJECTS; k++) {
            if (objects[k].id == target_id && target_id != 0) {
                local_idx = k;
                break;
            }
        }

        /* If not found, find an empty slot (skip index 0) */
        if (local_idx == -1) {
            for(int k=1; k<MAX_OBJECTS; k++) {
                if (objects[k].id == 0) {
                    local_idx = k;
                    objects[k].id = target_id;
                    objects[k].x = -100.0; 
                    break;
                }
            }
        }

        if (local_idx != -1) {
            updated[local_idx] = 1;
            GameObject *obj = &objects[local_idx];
                    float next_x = state->objects[i].shm_x - (QUADRANT_SIZE / 2.0);
                    float next_y = state->objects[i].shm_z - (QUADRANT_SIZE / 2.0);
                    float next_z = (QUADRANT_SIZE / 2.0) - state->objects[i].shm_y;            
            if (isnan(next_x) || isnan(next_y) || isnan(next_z)) {
                updated[local_idx] = 0; 
                continue;
            }

            float dx = next_x - obj->x;
            float dy = next_y - obj->y;
            float dz = next_z - obj->z;
            
            if (quadrant_changed || obj->x < -200.0 || (dx*dx + dy*dy + dz*dz) > 400.0) {
                obj->x = obj->tx = next_x;
                obj->y = obj->ty = next_y;
                obj->z = obj->tz = next_z;
                obj->h = obj->th = state->objects[i].h;
                obj->m = obj->tm = state->objects[i].m;
                obj->r = obj->tr = state->objects[i].r;
                obj->trail_count = 0;
                obj->trail_ptr = 0;
            } else {
                obj->tx = next_x;
                obj->ty = next_y;
                obj->tz = next_z;
            }

            obj->th = state->objects[i].h;
            obj->tm = state->objects[i].m;
            obj->tr = state->objects[i].r;
            obj->last_update_time = glutGet(GLUT_ELAPSED_TIME);
            obj->type = state->objects[i].type;
            obj->ship_class = state->objects[i].ship_class;
            obj->health_pct = state->objects[i].health_pct;
            obj->energy = state->objects[i].energy;
            obj->plating = state->objects[i].plating;
            obj->hull_integrity = state->objects[i].hull_integrity;
            obj->faction = state->objects[i].faction;
            obj->is_cloaked = state->objects[i].is_cloaked;
            obj->vx = (float)state->objects[i].vx;
            obj->vy = (float)state->objects[i].vz; // Mapping: Stellar Z -> Viewer Y
            obj->vz = -(float)state->objects[i].vy; // Mapping: Stellar Y -> -Viewer Z
            obj->q1 = state->shm_q[0];
            obj->q2 = state->shm_q[1];
            obj->q3 = state->shm_q[2];
            strncpy(obj->name, state->objects[i].shm_name, sizeof(obj->name) - 1);
            obj->name[sizeof(obj->name) - 1] = '\0';
        }
    }

    /* Clear stale objects not present in the latest update */
    for(int k=0; k<MAX_OBJECTS; k++) {
        if (!updated[k]) {
            objects[k].type = 0;
            objects[k].id = 0;
        }
    }

    /* Torpedo State Sampling (IPC-driven: TTL managed in main loop) */
    for(int s=0; s<MAX_VISIBLE_TORPEDOES; s++) {
        /* IPC handles TTL independently, so we don't mess with it here. */
    }

    /* Update departure wormhole only when it becomes newly active (freeze position once set) */
    if (state->wormhole.active) {
        if (!g_wormhole.active) {
            g_wormhole.x = state->wormhole.shm_x - (QUADRANT_SIZE / 2.0);
            g_wormhole.y = state->wormhole.shm_z - (QUADRANT_SIZE / 2.0);
            g_wormhole.z = (QUADRANT_SIZE / 2.0) - state->wormhole.shm_y;
            g_wormhole.h = state->shm_h;
            g_wormhole.m = state->shm_m;
            g_wormhole.active = 1;
            g_wormhole.jump_type = state->wormhole.extra;
        }
    } else {
        g_wormhole.active = 0;
    }

    /* Only update arrival orientation when arrival is not active (preserve locked orientation) */
    if (!g_jump_arrival.active) {
        g_jump_arrival.h = state->shm_h;
        g_jump_arrival.m = state->shm_m;
    }

    /* Supernova epicenter */
    if (state->supernova_pos.active > 0) {
        g_sn_pos.x = state->supernova_pos.shm_x - (QUADRANT_SIZE / 2.0);
        g_sn_pos.y = state->supernova_pos.shm_z - (QUADRANT_SIZE / 2.0);
        g_sn_pos.z = (QUADRANT_SIZE / 2.0) - state->supernova_pos.shm_y;
        g_sn_pos.active = 1;
        g_sn_pos.timer = state->supernova_pos.active;
        g_sn_q[0] = state->shm_sn_q[0];
        g_sn_q[1] = state->shm_sn_q[1];
        g_sn_q[2] = state->shm_sn_q[2];
    } else {
        g_sn_pos.active = 0;
        g_sn_q[0] = g_sn_q[1] = g_sn_q[2] = 0;
    }

    g_is_loading = 0;
}

#include "spacegl_3dview_tables.inc"
/* Bitmap font height in window pixels that keeps the text in sync with the
 * HUD design space (1000x1000 mapped onto the live window): at the native
 * TACTICAL_CUBE size the glyph is the raw 8 px, elsewhere it scales with the
 * window height so text and geometry keep the same relative size. */
static int hudGlyphPx(int h) {
    int px = (int)(8.0 * (double)h / (double)TACTICAL_CUBE_H + 0.5);
    if (px < 1) px = 1;       /* glBitmap needs a non-zero size */
    if (px > 32) px = 32;     /* sanity cap for pathological sizes */
    return px;
}

/* The HUD UI pass is laid out in a fixed 1000x1000 design space (see the
 * coordinates in display()). With a full-window viewport that space is mapped
 * onto the live framebuffer, so the HUD follows window resizes; the bitmap
 * font (glBitmap, window-pixel units) is scaled to match via glutBitmapTextSize.
 * NOTE: hudEnd() MUST be called (also on early-return paths) to keep the
 * PROJECTION/MODELVIEW stacks balanced. */
static void hudBegin(void) {
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    gluOrtho2D(0.0, 1000.0, 0.0, 1000.0);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
}

static void hudEnd(void) {
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
}

void reshape(int w, int h) {
    if (w <= 0 || h <= 0) return;
    g_fb_w = w; g_fb_h = h;
    glViewport(0, 0, w, h);

    /* Size-dependent resources must follow the window (same resize
     * handling as spacegl_vulkan, 2026.09.30.01): the bloom chain is reallocated at
     * the new size so the MSAA resolve blit covers the whole target, and the
     * bitmap font is rescaled so HUD text stays in sync with the design
     * space. display() re-checks via ensureBloomFBO() as a backstop. */
    if (bloom_w != w || bloom_h != h) {
        destroyBloomFBO();
        initBloomFBO(w, h);
    }
    glutBitmapTextSize(hudGlyphPx(h));
}

void display() {
    /* Backstop: the bloom chain must match the live framebuffer size (the
     * resize callback does the same; this covers a missed/coalesced event). */
    ensureBloomFBO(g_fb_w, g_fb_h);

    /* 1. BLOOM PASS: Render Scene to Multisampled Buffer */
    if (fbo_msaa != 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_msaa);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    
    if (g_data_dirty) { loadGameState(); g_data_dirty = 0; }
    
    /* RESET STACKS */
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    /* Dynamic FOV: 45 (Tactical) -> 65 (Bridge) */
    double current_fov = 45.0 * (1.0 - bridge_anim) + 65.0 * bridge_anim;
    /* Aspect follows the LIVE window size (the FBO chain was reallocated to
     * match in reshape/ensureBloomFBO), not the native 1920x1080. */
    gluPerspective(current_fov, (double)g_fb_w/g_fb_h, 0.1, 500); 
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();

    /* Cinematic Camera Transition */
    double current_zoom = zoom - (21.0 * map_anim);
    
    if (bridge_anim > 0.001 && objects[0].id != 0) {
        if (bridge_anim >= 0.999) {
            double y_off = (g_show_bridge >= 11) ? 0.35 : -0.35;
            int mode = g_show_bridge % 10;

            if (mode == 2) { /* LEFT */
                glRotatef(0.0, 0, 1, 0); 
            } else if (mode == 3) { /* RIGHT */
                glRotatef(180.0, 0, 1, 0);
            } else if (mode == 4) { /* UP */
                glRotatef(-90.0, 1, 0, 0);
                glRotatef(90.0, 0, 1, 0);
            } else if (mode == 5) { /* DOWN */
                glRotatef(90.0, 1, 0, 0);
                glRotatef(90.0, 0, 1, 0);
            } else if (mode == 6) { /* REAR */
                glRotatef(-90.0, 0, 1, 0);
            } else { /* FORWARD (1) */
                glRotatef(90.0, 0, 1, 0);
            }

            glTranslatef(0.48, y_off, 0.0);
            glRotatef(-objects[0].m, 0, 0, 1);
            glRotatef(-(objects[0].h - 90.0), 0, 1, 0);
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
            double y_off = (g_show_bridge >= 11) ? 0.35 : -0.35;
            int mode = g_show_bridge % 10;

            if (mode == 2) { glRotatef(0.0, 0, 1, 0); }
            else if (mode == 3) { glRotatef(180.0, 0, 1, 0); }
            else if (mode == 4) { glRotatef(-90.0, 1, 0, 0); glRotatef(90.0, 0, 1, 0); }
            else if (mode == 5) { glRotatef(90.0, 1, 0, 0); glRotatef(90.0, 0, 1, 0); }
            else if (mode == 6) { glRotatef(-90.0, 0, 1, 0); }
            else { glRotatef(90.0, 0, 1, 0); }

            glTranslatef(0.48, y_off, 0.0);
            glRotatef(-objects[0].m, 0, 0, 1);
            glRotatef(-(objects[0].h - 90.0), 0, 1, 0);
            glTranslatef(-objects[0].x, -objects[0].y, -objects[0].z);
            GLdouble m_brg[16];
            glGetDoublev(GL_MODELVIEW_MATRIX, m_brg);
            glPopMatrix();

            GLdouble m_final[16];
            for(int i=0; i<16; i++) m_final[i] = m_std[i] * (1.0 - bridge_anim) + m_brg[i] * bridge_anim;
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
    double sn_intensity = 0.0;
    int q1 = g_shared_state->shm_q[0], q2 = g_shared_state->shm_q[1], q3 = g_shared_state->shm_q[2];
    if (q1 >= 0 && q1 <= GALAXY_SIZE && q2 >= 0 && q2 <= GALAXY_SIZE && q3 >= 0 && q3 <= GALAXY_SIZE) {
        if (g_shm->shm_galaxy[q1][q2][q3] < 0) {
            int timer = -g_shm->shm_galaxy[q1][q2][q3];
            sn_intensity = 0.3 + sin(pulse*10.0) * 0.2;
            if (timer < 300) sn_intensity += 0.3; 
        }
    }
    
    /* Background fade to darker black in map mode */
    double bg_level = 0.05 * (1.0 - map_anim);
    glClearColor(bg_level + sn_intensity, bg_level, bg_level, 1.0);
    /* glClear is removed here as it is handled at the start of FBO binding */

    /* Render Procedural Nebula Background */
    if (map_anim < 0.9) drawSkybox();

    /* --- RENDER GALAXY MAP (Fades In) --- */
    if (map_anim > 0.01) {
        glPushMatrix();
        /* Holographic appearing effect: Scale up from center */
        double map_scale = map_anim; 
        glScalef(map_scale, map_scale, map_scale);
        drawExplorerMap();
        glPopMatrix();
    }

    /* --- RENDER TACTICAL VIEW (Fades Out) --- */
    if (map_anim < 0.99) {
        glPushMatrix();
        double tact_scale = 1.0 - map_anim;
        glScalef(tact_scale, tact_scale, tact_scale);

        /* 1. BACKGROUND STARS */
        glDisable(GL_LIGHTING);
        if (vbo_stars != 0) { 
            glPointSize(1.0);
            glColor3f(0.8, 0.8, 0.8); 
            glEnableClientState(GL_VERTEX_ARRAY); 
            glBindBuffer(GL_ARRAY_BUFFER, vbo_stars); 
            glVertexPointer(3, GL_FLOAT, 0, 0); 
            glDrawArrays(GL_POINTS, 0, g_star_count); 
            glBindBuffer(GL_ARRAY_BUFFER, 0); 
            glDisableClientState(GL_VERTEX_ARRAY); 
        }

        drawTacticalCube();

        /* Apply Anisotropic Filtering level to global state if supported */
        #ifdef GL_TEXTURE_MAX_ANISOTROPY_EXT
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, (float)g_aniso_level);
        #endif

        /* 2. LOCAL OBJECTS */
        /* Find a star for dynamic lighting */
        double lX = 50.0, lY = 50.0, lZ = 50.0; /* Default distant light */
        for(int i=0; i<MAX_OBJECTS; i++) {
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
            double h_rad = objects[0].h * M_PI / 180.0;
            /* Inverted pitch rotation to match ship physics: nose up = ring up */
            glRotatef(-objects[0].m, cos(h_rad), 0, -sin(h_rad));
            drawHeadingRing(); 
            glPopMatrix();

            /* 3. MARK ARC (Stays Vertical, only follows Heading) */
            glPushMatrix();
            glRotatef(objects[0].h - 90.0f, 0, 1, 0);
            drawMarkArc();
            glPopMatrix();

            /* 4. DIRECTIONAL VECTOR (Green Arrow - Ship Orientation) */
            glPushMatrix();
            glRotatef(objects[0].h - 90.0f, 0, 1, 0);
            glRotatef(objects[0].m, 0, 0, 1);
            glRotatef(objects[0].r, 1, 0, 0);
            
            /* 4.1 NOSE VECTOR (Green) */
            glDisable(GL_LIGHTING);
            glLineWidth(2.0);
            glBegin(GL_LINES);
            glColor3f(0.0, 1.0, 0.0); /* Bright Green */
            glVertex3f(0.2, 0, 0); glVertex3f(1.8, 0, 0); /* Arrow shaft */
            /* Small arrow head */
            glVertex3f(1.8, 0, 0); glVertex3f(1.5, 0.1, 0.1);
            glVertex3f(1.8, 0, 0); glVertex3f(1.5, -0.1, 0.1);
            glVertex3f(1.8, 0, 0); glVertex3f(1.5, -0.1, -0.1);
            glVertex3f(1.8, 0, 0); glVertex3f(1.5, 0.1, -0.1);
            glEnd();

            /* 4.2 TOP VECTOR (Blue - Half length) */
            glBegin(GL_LINES);
            glColor3f(0.0, 0.5, 1.0); /* Bright Blue */
            glVertex3f(0.0, 0.1, 0); glVertex3f(0.0, 0.9, 0); /* Arrow shaft (Local +Y) */
            /* Small arrow head pointing UP */
            glVertex3f(0.0, 0.9, 0); glVertex3f(0.1, 0.7, 0.1);
            glVertex3f(0.0, 0.9, 0); glVertex3f(-0.1, 0.7, 0.1);
            glVertex3f(0.0, 0.9, 0); glVertex3f(-0.1, 0.7, -0.1);
            glVertex3f(0.0, 0.9, 0); glVertex3f(0.1, 0.7, -0.1);
            glEnd();

            /* 4.3 SOLID ROLL RING */
            drawRollCircle();

            glLineWidth(1.0);
            glEnable(GL_LIGHTING);
            glPopMatrix();
            
            glPopMatrix();
        }
        if (g_show_grid) drawGrid();
        
        /* Supernova Flash */
        if (g_sn_pos.active && g_sn_q[0] == g_my_q[0] && g_sn_q[1] == g_my_q[1] && g_sn_q[2] == g_my_q[2] && g_sn_pos.timer < (GAME_TICK_RATE / 2)) {
            glDisable(GL_LIGHTING);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            double flash_s = ((GAME_TICK_RATE / 2) - g_sn_pos.timer) * 0.5;
            glColor4f(1.0, 1.0, 1.0, 0.8);
            glPushMatrix();
            glTranslatef(g_sn_pos.x, g_sn_pos.y, g_sn_pos.z);
            glutSolidSphere(flash_s, 32, 32);
            glPopMatrix();
            glDisable(GL_BLEND);
            glEnable(GL_LIGHTING);
        }

        /* Trails */
        for(int k=0; k<MAX_OBJECTS; k++) {
            if (objects[k].type == 1 || objects[k].type >= 10) drawShipTrail(k);
        }

        /* Objects (Solids) */
        glEnable(GL_LIGHTING);
        asteroidInstanceCount = 0;
        for(int i=0; i<MAX_OBJECTS; i++) {
            if (objects[i].type == 0) continue;

            glColor4f(1.0, 1.0, 1.0, 1.0);
            glPushMatrix(); glTranslatef(objects[i].x, objects[i].y, objects[i].z);

            /* Pass per-object hit status to shader */
            glUseProgram(hullShaderProgram);
            if (i == 0) {
                glUniform1f(glGetUniformLocation(hullShaderProgram, "hitPulse"), g_hull_hit_timer / 20.0);
            } else {
                glUniform1f(glGetUniformLocation(hullShaderProgram, "hitPulse"), 0.0);
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
                glLineWidth(1.0);
            }

            if (objects[i].type == 1) { 
                /* Apply materialization glow if emerging from wormhole (Local player is always at index 0) */
                bool is_emerging = (g_jump_arrival.timer > 180 && i == 0);
                
                if (objects[i].faction == 0) { // FACTION_ALLIANCE
                    if (is_emerging) {
                        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                        double progress = 1.0 - (g_jump_arrival.timer - 180) / 120.0;
                        double glow = 1.0 - progress;
                        glColor4f(0.8 + glow*0.2, 0.8 + glow*0.2, 1.0, 0.5 + glow*0.5);
                    } else {
                        if (!g_is_cloaked_rendering) glUseProgram(hullShaderProgram);
                    }
                    drawAllianceShip(objects[i].ship_class, objects[i].h, objects[i].m, objects[i].r);
                    glUseProgram(0);
                    if (is_emerging) glDisable(GL_BLEND);
                } else {
                    /* Non-Alliance Player: Use Faction model */
                    glRotatef(objects[i].h - 90.0, 0, 1, 0); 
                    glRotatef(objects[i].m, 0, 0, 1);
                    glRotatef(objects[i].r, 1, 0, 0);
                    if (is_emerging) {
                        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                        double progress = 1.0 - (g_jump_arrival.timer - 180) / 120.0;
                        double glow = 1.0 - progress;
                        glColor4f(0.8 + glow*0.2, 0.8 + glow*0.2, 1.0, 0.5 + glow*0.5);
                    } else {
                        if (!g_is_cloaked_rendering) glUseProgram(hullShaderProgram);
                    }
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
                        default: drawAllianceShip(0, 0, 0, 0); break;
                    }
                    glUseProgram(0);
                    if (is_emerging) glDisable(GL_BLEND);
                }
            } else {
                glPushMatrix();
                /* Removed glTranslatef(x, y, z) to avoid double-transformation misalignment */
                glRotatef(objects[i].h - 90.0, 0, 1, 0); 
                glRotatef(objects[i].m, 0, 0, 1);
                glRotatef(objects[i].r, 1, 0, 0);
                
                bool use_hull = false;
                int t = objects[i].type;
                if (t == 3 || t == 10 || t == 21 || t == 22 || t == 23 || t == 24 || t == 25 || (t >= 11 && t <= 20)) use_hull = true;
                
                if (use_hull && !g_is_cloaked_rendering) glUseProgram(hullShaderProgram);
                switch(t) {
                    case 3: drawStarbase(0,0,0, objects[i].faction); break;
                    case 4: drawStar(0,0,0, objects[i].id); break;
                    case 5: drawPlanet(0,0,0); break;
                    case 6: drawAccretionDisk(0,0,0, pulse); drawBlackHole(0,0,0); break;
                    case 100: case 101: case 102: case 103: case 104: case 105: case 106: case 107:
                    case 108: case 109: case 110: case 111: case 112: case 113: case 114: case 115:
                    case 116: case 117: case 118: case 119: case 120: case 121: case 122: case 123:
                    case 124: case 125: case 126: case 127: case 128: case 129: case 130: case 131: {
                        int e_id = t - 100;
                        if (e_id % 3 == 0) drawBlackHole(0,0,0);
                        else if (e_id % 3 == 1) drawSingularity(0,0,0);
                        else drawNeutronStar(0,0,0);
                        
                        if (e_id % 2 == 0) drawAccretionDisk(0,0,0, pulse + e_id);
                        if (e_id % 4 == 0) drawRelativisticJet(0,0,0, pulse + e_id);
                        if (e_id % 5 == 0) drawTimeAnomaly(0,0,0);
                        
                        /* Add a distinct colored glow based on type ID */
                        float r = (e_id % 3) * 0.5f;
                        float g = (e_id % 4) * 0.33f;
                        float b = (e_id % 5) * 0.25f + 0.2f;
                        drawGlow(4.0 + (e_id % 3), r, g, b, 0.6);
                        break;
                    }
                    case 7: drawStellarNebula(0,0,0, objects[i].ship_class); break;
                    case 8: drawPulsar(0,0,0); break;
                    case 9: drawComet(0,0,0); break;
                    case 51: drawStellarNebula(0,0,0, 3); break; // Diffuse
                    case 52: drawStellarNebula(0,0,0, 2); break; // Dark
                    case 53: drawStellarNebula(0,0,0, 4); break; // Planetary
                    case 54: drawStellarNebula(0,0,0, 4); break; // SNR
                    case 55: drawStellarNebula(0,0,0, 5); break; // GMC
                    case 56: drawInterstellarFilament(); break;
                    case 57: drawInterstellarBubble(0,0,0, pulse); break;
                    case 58: drawBokGlobule(0,0,0); break;
                    case 59: drawClumpCore(0,0,0); break;
                    case 60: drawAccretionDisk(0,0,0, pulse); break;
                    case 61: drawRelativisticJet(0,0,0, pulse); break;
                    case 62: drawShockWave(0,0,0, pulse); break;
                    case 63: drawStellarBowShock(0,0,0, pulse); break;
                    case 64: drawCosmicVoid(0,0,0); break;
                    case 65: drawCosmicFilament(0,0,0, pulse); break;
                    case 66: drawEventHorizon(0,0,0); break;
                    case 67: drawKilonova(0,0,0, pulse); break;
                    case 68: drawGravLens(0,0,0, pulse); break;
                    case 69: drawGRB(0,0,0, pulse); break;
                    case 70: drawGravWave(0,0,0, pulse); break;
                    case 71: drawProtoplanetaryDisk(0,0,0, pulse); break;
                    case 72: drawDebrisDisk(0,0,0, pulse); break;
                    case 73: drawPlanetesimal(0,0,0, pulse); break;
                    case 74: drawRoguePlanet(0,0,0); break;
                    case 75: drawBrownDwarf(0,0,0); break;
                    case 76: drawISO(0,0,0, pulse); break;
                    case 77: drawMagReconn(0,0,0, pulse); break;
                    case 78: drawCurrentSheet(0,0,0, pulse); break;
                    case 79: drawHeliosphere(0,0,0); break;
                    case 80: drawTermShock(0,0,0, pulse); break;
                    case 81: drawMagnetosphere(0,0,0, pulse); break;
                    case 82: drawCosmicString(0,0,0, pulse); break;
                    case 83: drawDomainWall(0,0,0, pulse); break;
                    case 84: drawDMHalo(0,0,0); break;
                    case 85: drawIGM(0,0,0, pulse); break;
                    case 86: drawCGM(0,0,0, pulse); break;
                    case 87: drawLymanAlpha(0,0,0, pulse); break;
                    case 88: drawCMB(0,0,0); break;
                    case 10: drawKorthian(0,0,0); break;
                    case 21: 
                        if (asteroidInstanceCount < 1000) {
                            /* World position (quadrant-centered): must match the
                               position used by drawHUD()/gluProject for the label,
                               otherwise the mesh renders at the quadrant center. */
                            asteroidData[asteroidInstanceCount].x = objects[i].x;
                            asteroidData[asteroidInstanceCount].y = objects[i].y;
                            asteroidData[asteroidInstanceCount].z = objects[i].z;
                            asteroidData[asteroidInstanceCount].scale = 0.5 + (objects[i].plating / (float)YIELD_HARVEST_MAX);
                            asteroidData[asteroidInstanceCount].rot = pulse * 2.0 + (objects[i].id * 0.1);
                            asteroidInstanceCount++;
                        } break;
                    case 22: drawDerelict(objects[i].ship_class, objects[i].faction); break;
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
                    case 27: { drawAlienArtifact(0,0,0); break; }
                    case 28: { /* Torpedo: Skip rendering here, handled by drawTorpedo() */ break; }
                    case 29: drawQuasar(0,0,0, objects[i].ship_class); break;
                    case 34: drawDysonFragment(0,0,0); break;
                    case 35: drawTradingHub(0,0,0); break;
                    case 36: drawAncientRelic(0,0,0); break;
                    case 37: drawSubspaceRupture(0,0,0); break;
                    case 38: drawSatellite(0,0,0); break;
                    case 39: drawIonStorm(0,0,0); break;
                    case 40: drawAlienArtifact(0,0,0); break;
                    case 41: drawWarpGate(0,0,0); break;
                    case 42: drawNeutronStar(0,0,0); break;
                    case 43: drawMegaStructure(0,0,0); break;
                    case 44: drawDarkCloud(0,0,0); break;
                    case 45: drawSingularity(0,0,0); break;
                    case 46: drawPlasmaStorm(0,0,0); break;
                    case 47: drawOrbitalRing(0,0,0); break;
                    case 48: drawTimeAnomaly(0,0,0); break;
                    case 49: drawVoidCrystal(0,0,0); break;
                    case 50: drawSubspaceAnomaly(0,0,0); break;
                }
                glPopMatrix();
            }

            if (objects[i].is_cloaked) { 
                glPopAttrib();
                glUseProgram(0);
                g_is_cloaked_rendering = 0;
            }

            glPopMatrix();
        }

        /* --- Draw Instanced Asteroids --- */
        if (asteroidInstanceCount > 0) {
            glUseProgram(asteroidInstancedShaderProgram);
            glBindBuffer(GL_ARRAY_BUFFER, asteroidInstanceVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0, asteroidInstanceCount * sizeof(AsteroidInstanceData), asteroidData);

            glBindBuffer(GL_ARRAY_BUFFER, asteroidVBO);
            GLint posAttrib = glGetAttribLocation(asteroidInstancedShaderProgram, "position");
            glEnableVertexAttribArray(posAttrib);
            glVertexAttribPointer(posAttrib, 3, GL_FLOAT, GL_FALSE, 0, 0);

            glBindBuffer(GL_ARRAY_BUFFER, asteroidInstanceVBO);
            GLint instPosAttrib = glGetAttribLocation(asteroidInstancedShaderProgram, "instancePos");
            glEnableVertexAttribArray(instPosAttrib);
            glVertexAttribPointer(instPosAttrib, 3, GL_FLOAT, GL_FALSE, sizeof(AsteroidInstanceData), (void*)0);
            glVertexAttribDivisorARB(instPosAttrib, 1);

            GLint instScaleAttrib = glGetAttribLocation(asteroidInstancedShaderProgram, "instanceScale");
            glEnableVertexAttribArray(instScaleAttrib);
            glVertexAttribPointer(instScaleAttrib, 1, GL_FLOAT, GL_FALSE, sizeof(AsteroidInstanceData), (void*)(3 * sizeof(float)));
            glVertexAttribDivisorARB(instScaleAttrib, 1);

            GLint instRotAttrib = glGetAttribLocation(asteroidInstancedShaderProgram, "instanceRot");
            glEnableVertexAttribArray(instRotAttrib);
            glVertexAttribPointer(instRotAttrib, 1, GL_FLOAT, GL_FALSE, sizeof(AsteroidInstanceData), (void*)(4 * sizeof(float)));
            glVertexAttribDivisorARB(instRotAttrib, 1);

            glDrawArraysInstancedARB(GL_QUADS, 0, 24, asteroidInstanceCount);

            glDisableVertexAttribArray(posAttrib);
            glDisableVertexAttribArray(instPosAttrib);
            glDisableVertexAttribArray(instScaleAttrib);
            glDisableVertexAttribArray(instRotAttrib);
            glUseProgram(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        /* 3. TRANSPARENT EFFECTS (Drawn last with depth testing usually) */
        drawShieldEffect();
        drawIonBeams();
        drawTorpedo();
        drawExplosion();
        drawParticles();
        drawJumpArrival();
        
        /* Render Active Probes in current quadrant */
        for(int p=0; p<3; p++) {
            if (g_local_probes[p].active) {
                /* Check if probe is in player's current quadrant */
                if (g_local_probes[p].q1 == g_shared_state->shm_q[0] &&
                    g_local_probes[p].q2 == g_shared_state->shm_q[1] &&
                    g_local_probes[p].q3 == g_shared_state->shm_q[2]) {
                    
                    /* Convert sector coordinates [0,10] to local GL space [-5,5] */
                    /* Note: Y and Z axes are flipped in this coordinate system */
                    double px = g_shared_state->probes[p].s1 - 20.0;
                    double py = g_shared_state->probes[p].s3 - 20.0;
                    double pz = 20.0 - g_shared_state->probes[p].s2;
                    drawProbe(p, px, py, pz);
                }
            }
        }

        updateProbeHUD();
        if (g_wormhole.active) drawWormhole(g_wormhole.x, g_wormhole.y, g_wormhole.z, g_wormhole.h, g_wormhole.m, 0, g_wormhole.jump_type);
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
        /* Full-frame blit at the LIVE size (FBOs == viewport after
           ensureBloomFBO), so nothing is cropped or letterboxed. */
        glBlitFramebuffer(0, 0, bloom_w, bloom_h, 0, 0, bloom_w, bloom_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    /* 3. BLOOM PASS: Blur the Bright Texture (Ping-Pong) */
    if (fbo_scene != 0) {
        bool horizontal = true, first_iteration = true;
        unsigned int amount = 6;
        glUseProgram(blurShaderProgram);
        glDisable(GL_DEPTH_TEST);
        /* Gaussian kernel offset in texels of the LIVE-size textures. */
        glUniform2f(glGetUniformLocation(blurShaderProgram, "texel"),
                    1.0f / (float)bloom_w, 1.0f / (float)bloom_h);
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
        if (map_anim < 0.5) {
            for(int i=0; i<MAX_OBJECTS; i++) {
                if (objects[i].type != 0 && !g_is_loading) {
                    drawHUD(i);
                }
            }
        }
    }

    /* Draw HUD Overlay (Map Mode) */
    if (g_show_hud && map_anim > 0.5) {
        /* Show Map specific text */
        hudBegin();
        glDisable(GL_LIGHTING); glColor3f(0, 1, 1);
        if (g_is_jammed) {
            glColor3f(1, 0, 0);
            drawText3D(20, 960, 0, "--- CARTOGRAPHY FAILURE: SENSORS JAMMED ---");
        } else {
            drawText3D(20, 960, 0, "--- STELLAR CARTOGRAPHY: FULL GALAXY VIEW ---");
        }
        
        int fy = 920;
        /* Vertical list: Color Block + Object Name */
        struct { double r,g,b; const char* name; } legends[] = {
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
            {1.0, 1.0, 1.0, "White: Space Monsters (mo)"},
            {1.0, 0.0, 1.0, "Magenta: Quasars (qu)"}
        };

        for(int i=0; i<17; i++) {
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

        hudEnd();
    }

    /* Main HUD block (1000x1000 design space mapped onto the live window).
     * Pushed unconditionally: hudEnd() below (and on the early-return path)
     * keeps the matrix stacks balanced. */
    hudBegin();
    glDisable(GL_LIGHTING);

    char buf[256];    if (g_show_hud && map_anim < 0.5) {
        /* --- TOP LEFT: Comprehensive Ship Status --- */
        int x_off = 20;
        int y_pos = 970;
        
        /* 1. Command & Location */
        glColor3f(1.0, 1.0, 0.0); /* Yellow */
        /* Use captain name and class from local variables copied under mutex */
        if (g_shared_state->objects[0].faction == 0) { // FACTION_ALLIANCE
            sprintf(buf, "Alliance - %s - CAPTAIN: %s", getClassName(g_player_class), g_player_name);
        } else {
            sprintf(buf, "%s - CAPTAIN: %s", getFactionHUDName(g_shared_state->objects[0].faction), g_player_name);
        }
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 20;

        if (g_is_docked) {
            glColor3f(0.0, 1.0, 0.0); /* Green */
            sprintf(buf, "SHIP STATUS: DOCKED (Systems Secured)");
        } else if (g_red_alert) {
            double blink = sin(glutGet(GLUT_ELAPSED_TIME)*0.01) * 0.5 + 0.5;
            glColor3f(1.0, blink * 0.3, 0.0); /* Pulsing Red/Orange */
            sprintf(buf, "SHIP STATUS: RED ALERT (Battle Stations)");
        } else {
            glColor3f(0.0, 1.0, 1.0); /* Bright Cyan */
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
            glColor3f(1.0, 1.0, 1.0);
            drawText3D(x_off, y_pos, 0, "WARNING: ELECTRONIC WARFARE JAMMING ACTIVE"); y_pos -= 20;
        }

        /* --- TACTICAL STATUS OVERLAY --- */
        if (g_cloaked) {
            double blink = sin(glutGet(GLUT_ELAPSED_TIME)*0.015) * 0.5 + 0.5;
            glColor3f(0.3, 0.6, 1.0); /* Soft Blue */
            sprintf(buf, "CLOAKING DEVICE: ACTIVE (Signature Hidden) [%s]", (blink > 0.5) ? "OK" : "  ");
            drawText3D(x_off, y_pos, 0, buf); y_pos -= 20;
        }

        /* Target Lock Status */
        glColor3f(1.0, 1.0, 1.0);
        sprintf(buf, "LOCK: ");
        drawText3D(x_off, y_pos, 0, buf);
        if (g_lock_target > 0) {
            glColor3f(1, 0, 0); sprintf(buf, "[ ID %d ]", g_lock_target);
        } else {
            glColor3f(0.5, 0.5, 0.5); sprintf(buf, "[ NONE ]");
        }
        drawText3D(x_off + 60, y_pos, 0, buf); y_pos -= 18;

        /* Torpedo Tube Status (Multi-tube HUD) */
        glColor3f(1.0, 0.0, 0.0);
        drawText3D(x_off, y_pos, 0, "--- TORPEDO TUBES ---"); y_pos -= 18;
        for(int t=0; t<4; t++) {
            const char* tube_str = "[READY]"; 
            int timer = g_shared_state->tube_load_timers[t];
            int eta = g_shared_state->tube_torpedo_etas[t];
            bool is_current = (g_shared_state->current_tube == t);

            if (g_tube_state == 3) { glColor3f(0.5, 0.5, 0.5); tube_str = "[OFFLINE]"; }
            else if (timer > 180) { glColor3f(1.0, 0.0, 0.0); tube_str = "[FIRING]"; }
            else if (timer > 0) { glColor3f(1.0, 1.0, 0.0); tube_str = "[LOADING]"; }
            else if (is_current && g_tube_state == 2) { glColor3f(1.0, 0.0, 0.0); tube_str = "[FIRING]"; }
            else { glColor3f(0.0, 1.0, 0.0); tube_str = "[READY]"; }

            char timing_buf[32] = " T:--- ";
            if (timer > 0) sprintf(timing_buf, " T:%4.1fs", (double)timer / 60.0);

            char eta_buf[32] = " ETA:---";
            if (eta > 0) sprintf(eta_buf, " ETA:%4.1fs", (double)eta / 60.0);

            sprintf(buf, "%sTUBE %d:[%-7s]%s%s", is_current ? ">" : " ", t+1, tube_str, timing_buf, eta_buf);
            drawText3D(x_off, y_pos, 0, buf);
            y_pos -= 15;
        }
        y_pos -= 10;

        /* Power Distribution Status */
        glColor3f(1.0, 1.0, 0.0);
        drawText3D(x_off, y_pos, 0, "--- POWER & NAVIGATION ---"); y_pos -= 18;
        sprintf(buf, "ENGINES: %.0f%%| SHIELDS: %.0f%%| WEAPONS: %.0f%%",
                g_power_dist[0] * (double)YIELD_HARVEST_MAX, g_power_dist[1] * (double)YIELD_HARVEST_MAX, g_power_dist[2] * (double)YIELD_HARVEST_MAX);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 20;
        /* Ion Beam Charge Bar */
        if (g_ion_charge > 0) {
            glColor3f(0, 0.8, 1.0);
            sprintf(buf, "ION BEAM CHARGE: [%-10.*s] %.0f%%", (int)(g_ion_charge/10.0), "============", g_ion_charge);
            drawText3D(x_off, y_pos, 0, buf); y_pos -= 20;
        }

        glColor3f(0.0, 1.0, 1.0); /* Cyan */
        /* Convert focal point coordinates back to Stellar sector coordinates (0 to 10) */
        double disp_s1 = PlayerX + 20.0;
        double disp_s2 = 20.0 - PlayerZ;
        double disp_s3 = PlayerY + 20.0;
        sprintf(buf, "QUADRANT: %s  |  SECTOR: [%.2f, %.2f, %.2f]", g_quadrant, disp_s1, disp_s2, disp_s3); 
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 20;

        sprintf(buf, "HEADING: %05.1f\302\260  |  MARK: %+05.1f\302\260  |  ROLL: %+05.1f\302\260", g_shared_state->shm_h, g_shared_state->shm_m, g_shared_state->shm_r);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 20;
        
        if (g_shared_state->shm_eta > 1.0) {
            char eta_buf[64];
            sprintf(eta_buf, "TIME TO DESTINATION: %5.1fs (ETA)", g_shared_state->shm_eta);
            /* Pulsing Yellow/Red if time is critical (< 5s) */
            if (g_shared_state->shm_eta < 5.0) {
                if (((int)(pulse * 10) % 2) == 0) glColor3f(1.0, 0.0, 0.0); else glColor3f(1.0, 1.0, 0.0);
            } else {
                glColor3f(1.0, 1.0, 0.0); 
            }
            drawText3D(x_off, y_pos, 0, eta_buf); 
            y_pos -= 20;
        } else {
            y_pos -= 5; /* Keep consistent spacing */
        }

        /* 2. Vital Resources */
        glColor3f(0.0, 1.0, 0.0);
        drawText3D(x_off, y_pos, 0, "--- ENERGY & SHIP ---"); y_pos -= 18;
        glColor3f(1.0, 1.0, 1.0);
        sprintf(buf, "ENERGY: %-12" PRIu64 " (CARGO ANTIMATTER: %-12" PRIu64 ")", g_energy, g_cargo_energy);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 18;
        sprintf(buf, "TORPS:  %-4d (CARGO TORPEDOES: %-4d)", g_torpedoes_launcher, g_cargo_torps);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 18;
        
        /* Hull Integrity Main Display */
        if (g_hull_integrity > 60) glColor3f(0, 1, 0);
        else if (g_hull_integrity > 25) glColor3f(1, 1, 0);
        else glColor3f(1, 0, 0);
        sprintf(buf, "HULL INTEGRITY: %.1f%%", g_hull_integrity);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 18;

        if (g_composite_plating > 0) {
            glColor3f(1.0, 0.8, 0.0);
            sprintf(buf, "HULL PLATING: %-5d [Composite REINFORCED]", g_composite_plating);
            drawText3D(x_off, y_pos, 0, buf); y_pos -= 18;
        }

        /* GFX Status Overlay */
        glColor3f(0.7, 0.7, 0.7);
        sprintf(buf, "GFX: ANI: %dx | STARS: %d", g_aniso_level, g_star_count);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 20;

        glColor3f(1.0, 1.0, 1.0);
        sprintf(buf, "CREW: %-4d | PRISON UNIT: %-4d | SHIELDS AVG: %-3d%%", g_crew, g_prison_unit, (g_shields/100));
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 20;
        
        /* 2.1 Individual Shields */
        glColor3f(0.0, 0.7, 1.0);
        drawText3D(x_off, y_pos, 0, "--- SHIELDS ---"); y_pos -= 18;
        const char* sh_names[] = {"F:", "RE:", "T:", "B:", "L:", "RI:"};
        /* Show F and RE */
        sprintf(buf, "%s %-4d  %s %-4d", sh_names[0], g_shields_val[0], sh_names[1], g_shields_val[1]);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 15;
        /* T and B on one line */
        sprintf(buf, "%s %-4d  %s %-4d", sh_names[2], g_shields_val[2], sh_names[3], g_shields_val[3]);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 15;
        /* L and RI spatially: index 5 is L, index 4 is RI */
        sprintf(buf, "%s %-4d  %s %-4d", sh_names[4], g_shields_val[5], sh_names[5], g_shields_val[4]);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 25;

        /* 3. System Health (1 column) */
        glColor3f(0.0, 0.8, 0.0);
        drawText3D(x_off, y_pos, 0, "--- SYSTEMS HEALTH ---"); y_pos -= 18;
        const char* sys_names[] = {"Hyperdrive", "Impulse", "Sensors", "Transp", "Ion Beams", "Torps", "Computer", "Life", "Shields", "Aux"};
        for(int i=0; i<10; i++) {
            double h = g_system_health[i];
            if (h > 75) glColor3f(0, 1, 0); else if (h > 30) glColor3f(1, 1, 0); else glColor3f(1, 0, 0);
            sprintf(buf, "%-12s: %.0f%%", sys_names[i], h);
            drawText3D(x_off, y_pos, 0, buf);
            y_pos -= 15;
        }
        y_pos -= 10;

        /* 4. Cargo Inventory (1 column) */
        glColor3f(0.8, 0.5, 0.0);
        drawText3D(x_off, y_pos, 0, "--- CARGO INVENTORY ---"); y_pos -= 18;
        const char* res_names[] = {"None", "Aetherium", "Neo-Titanium", "Void-Essence", "Graphene", "Synaptics", "Nebular Gas", "Composite", "Dark-Matter"};
        for(int i=1; i<9; i++) {
            glColor3f(0.7, 0.7, 0.7);
            sprintf(buf, "%-14s: %-4d", res_names[i], g_inventory[i]);
            drawText3D(x_off, y_pos, 0, buf);
            y_pos -= 15;
        }
        y_pos -= 10;

        /* 4.1 Deep Space Probes Status */
        glColor3f(0.0, 0.8, 1.0);
        drawText3D(x_off, y_pos, 0, "--- PROBES STATUS ---"); y_pos -= 18;
        for(int p=0; p<3; p++) {
            if (g_local_probes[p].active) {
                const char* st_name = "EN ROUTE";
                if (g_local_probes[p].status == 1) {
                    st_name = "TRANSMITTING";
                    glColor3f(1.0, 1.0, 0.0);
                } else if (g_local_probes[p].status == 2) {
                    st_name = "DERELICT";
                    glColor3f(0.5, 0.5, 0.5);
                } else {
                    glColor3f(0.0, 1.0, 0.0);
                }
                sprintf(buf, "P%d: %-12s [%d,%d,%d] ETA: %4.1fs", 
                        p+1, st_name, 
                        g_local_probes[p].q1, g_local_probes[p].q2, g_local_probes[p].q3,
                        (g_local_probes[p].eta < 0) ? 0 : g_local_probes[p].eta);
            } else {
                glColor3f(0.3, 0.3, 0.3);
                sprintf(buf, "P%d: IDLE", p+1);
            }
            drawText3D(x_off, y_pos, 0, buf); y_pos -= 15;
        }
        y_pos -= 10;

        /* 5. Reactor Power Distribution */
        glColor3f(1.0, 1.0, 0.0);
        drawText3D(x_off, y_pos, 0, "--- REACTOR POWER ALLOCATION ---"); y_pos -= 18;
        sprintf(buf, "ENGINES: %d%% |  SHIELDS: %d%% |  WEAPONS: %d%%", 
                (int)(g_shared_state->shm_power_dist[0]*100), 
                (int)(g_shared_state->shm_power_dist[1]*100), 
                (int)(g_shared_state->shm_power_dist[2]*100));
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 25;

        /* 6. Tactical Ordnance & Defense */
        glColor3f(1.0, 0.0, 0.0);
        drawText3D(x_off, y_pos, 0, "--- TACTICAL ORDNANCE ---"); y_pos -= 18;
        
        /* Weapons Status */
        glColor3f(1.0, 0.5, 0.5);
        const char* tube_labels[] = {"READY", "FIRING...", "LOADING...", "OFFLINE"};
        int ts = g_shared_state->shm_tube_state; if(ts<0||ts>3) ts=3;
        sprintf(buf, "Ion Beam CAPACITOR: %.0f%%| TUBES: %s", 
                g_shared_state->shm_ion_beam_charge,
                tube_labels[ts]);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 15;
        
        sprintf(buf, "Ion Beam INTEGRITY: %.0f%% | Anti-Matter: %d", 
                g_system_health[4],
                g_shared_state->shm_anti_matter);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 15;
        
        sprintf(buf, "LIFE SUPPORT: %.1f%%", g_shared_state->shm_life_support);
        drawText3D(x_off, y_pos, 0, buf); y_pos -= 25;

        /* Viewer Maneuver Controls */
        glColor3f(0, 1, 1);
        if (g_show_map) drawText3D(x_off, y_pos, 0, "Arrows: Rotate Map | W/S: Zoom Map | map (in CLI): Exit Map Mode");
        else drawText3D(x_off, y_pos, 0, "Arrows: Rotate | W/S: Zoom | bridge [pos] | map | H: Toggle HUD | ESC: Exit");
        y_pos -= 25;

        /* 7. Target Tactical Overlay (Center Screen) */
        if (g_lock_target > 0) {
            int tx_pos = 400;
            int ty_pos = 150;
            glColor3f(1.0, 0.0, 0.0);
            drawText3D(tx_pos, ty_pos, 0, ">>> TARGET LOCKED <<<"); ty_pos -= 20;
            
            /* Find target data */
            for(int i=0; i<MAX_OBJECTS; i++) {
                if (objects[i].id == g_lock_target) {
                    glColor3f(1.0, 1.0, 1.0);
                    
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
                    
                    double dx = objects[i].x - PlayerX;
                    double dy = objects[i].y - PlayerY;
                    double dz = objects[i].z - PlayerZ;
                    double dist = sqrt(dx*dx + dy*dy + dz*dz);
                    
                    glColor3f(1.0, 1.0, 1.0);
                    sprintf(buf, "ANTIMATTER: %" PRIu64 " (%d%%) | DIST: %.2f", objects[i].energy, objects[i].health_pct, dist);
                    drawText3D(tx_pos, ty_pos, 0, buf); ty_pos -= 15;

                    sprintf(buf, "HEADING: %.1f | MARK: %+.1f", objects[i].h, objects[i].m);
                    drawText3D(tx_pos, ty_pos, 0, buf);
                    break;
                }
            }
        }

        if (g_shared_state->is_cloaked) {
            glColor3f(0.5, 0.5, 1.0);
            drawText3D(x_off, y_pos - 20, 0, ">>> CLOAKING DEVICE ACTIVE <<<");
        }

        /* --- TOP RIGHT: Quadrant Object List --- */
        int y_off = 965;
        glColor3f(1.0, 0.5, 0.0);
        drawText3D(850, y_off, 0, "--- QUADRANT SENSORS ---");
        y_off -= 25;
        
        for(int i=0; i<MAX_OBJECTS; i++) {
            if (objects[i].id != 0 && objects[i].type != 0) {
                if (objects[i].type == 1) glColor3f(0, 1, 1);
                else if (objects[i].type >= 10 && objects[i].type <= 20) glColor3f(1, 0, 0);
                else glColor3f(0.8, 0.8, 0.8);

                const char* t_name = "Object";

                switch (objects[i].type) {
                    case 1: t_name = "PLAYER"; break;
                    case 3: t_name = "BASE"; break;
                    case 4: t_name = "STAR"; break;
                    case 5: t_name = "PLANET"; break;
                    case 6: t_name = "BLACKHOLE"; break;
                    case 10: t_name = "Korthian"; break;
                    case 11: t_name = "Xylari"; break;
                    case 12: t_name = "Swarm"; break;
                    case 13: t_name = "Vesperian"; break;
                    case 14: t_name = "Ascendant"; break;
                    case 15: t_name = "Quarzite"; break;
                    case 16: t_name = "Saurian"; break;
                    case 17: t_name = "Gilded"; break;
                    case 18: t_name = "Fluidic Void"; break;
                    case 19: t_name = "Cryos"; break;
                    case 20: t_name = "Apex"; break;
                    case 21: t_name = "ASTEROID"; break;
                    case 22: t_name = "DERELICT"; break;
                    case 24: t_name = "BUOY"; break;
                    case 25: t_name = "PLATFORM"; break;
                    case 29: t_name = "QUASAR"; break;
                    case 30: t_name = "ENTITY"; break;
                    case 31: t_name = "AMOEBA"; break;
                    case 51: t_name = "DIFFUSE NEBULA"; break;
                    case 52: t_name = "DARK NEBULA"; break;
                    case 53: t_name = "PLANETARY NEBULA"; break;
                    case 54: t_name = "SNR"; break;
                    case 55: t_name = "GMC"; break;
                    case 56: t_name = "INT FILAMENT"; break;
                    case 57: t_name = "INT BUBBLE"; break;
                    case 58: t_name = "BOK GLOBULE"; break;
                    case 59: t_name = "CLUMP CORE"; break;
                    case 60: t_name = "ACCRETION DISK"; break;
                    case 61: t_name = "RELATIVISTIC JET"; break;
                    case 62: t_name = "SHOCK WAVE"; break;
                    case 63: t_name = "BOW SHOCK"; break;
                    case 64: t_name = "COSMIC VOID"; break;
                    case 65: t_name = "COSMIC FILAMENT"; break;
                    case 66: t_name = "EVENT HORIZON"; break;
                    case 67: t_name = "KILONOVA"; break;
                    case 68: t_name = "GRAV LENS"; break;
                    case 69: t_name = "GRB"; break;
                    case 70: t_name = "GRAV WAVE"; break;
                    case 71: t_name = "PROTOPLANET DISK"; break;
                    case 72: t_name = "DEBRIS DISK"; break;
                    case 73: t_name = "PLANETESIMAL"; break;
                    case 74: t_name = "ROGUE PLANET"; break;
                    case 75: t_name = "BROWN DWARF"; break;
                    case 76: t_name = "ISO"; break;
                    case 77: t_name = "MAG RECONN"; break;
                    case 78: t_name = "CURRENT SHEET"; break;
                    case 79: t_name = "HELIOSPHERE"; break;
                    case 80: t_name = "TERM SHOCK"; break;
                    case 81: t_name = "MAGNETOSPHERE"; break;
                    case 82: t_name = "COSMIC STRING"; break;
                    case 83: t_name = "DOMAIN WALL"; break;
                    case 84: t_name = "DM HALO"; break;
                    case 85: t_name = "IGM"; break;
                    case 86: t_name = "CGM"; break;
                    case 87: t_name = "LYMAN ALPHA"; break;
                    case 88: t_name = "CMB"; break;
                    case 50: t_name = "ANOMALY"; break;
                    default: t_name = "UNKNOWN"; break;
                }
                
                if (objects[i].type == 1) sprintf(buf, "[%03d] %s", objects[i].id, objects[i].name);
                else if (objects[i].type == 10 || (objects[i].type >= 11 && objects[i].type <= 20) || objects[i].type == 22) {
                    if (objects[i].name[0] != '\0') sprintf(buf, "[%03d] %s: %s", objects[i].id, t_name, objects[i].name);
                    else sprintf(buf, "[%03d] %s Vessel", objects[i].id, t_name);
                }
                else if (objects[i].type == 4) {
                    const char* st_cls[] = {"O", "B", "A", "F", "G", "K", "M"};
                    int c_idx = objects[i].ship_class; if(c_idx<0)c_idx=0; if(c_idx>6)c_idx=6;
                    sprintf(buf, "[%03d] STAR: Class %s", objects[i].id, st_cls[c_idx]);
                } else if (objects[i].type == 5) {
                    const char* p_res[] = {"None", "Dil", "Tri", "Ver", "Per", "Anti", "Xen"};
                    int r_idx = objects[i].ship_class; if(r_idx<0)r_idx=0; if(r_idx>6)r_idx=6;
                    sprintf(buf, "[%03d] PLANET: %s", objects[i].id, p_res[r_idx]);
                } else if (objects[i].type == 29) {
                    const char* q_cls[] = {"Radio-loud", "Radio-quiet", "BAL", "Type 2", "Red", "OVV", "Weak-Em"};
                    int q_idx = objects[i].ship_class; if(q_idx<0) q_idx=0; if(q_idx>6) q_idx=6;
                    sprintf(buf, "[%03d] QUASAR: %s", objects[i].id, q_cls[q_idx]);
                } else if (objects[i].type == 7) {
                    const char* n_cls[] = {"Mutara", "Metreon", "Dark Matter Cloud", "Paulson", "McAllister", "Arachnia"};
                    int n_idx = objects[i].ship_class; if(n_idx<0) n_idx=0; if(n_idx>5) n_idx=5;
                    sprintf(buf, "[%03d] NEBULA: %s", objects[i].id, n_cls[n_idx]);
                } else if (objects[i].type == 21) {
                    const char* as_res[] = {"None", "Aetherium", "Neo-Titanium", "Void-Essence", "Graphene", "Synaptics", "Nebular Gas", "Composite", "Dark-Matter"};
                    int r_idx = objects[i].ship_class; if(r_idx<0) r_idx=0; if(r_idx>8) r_idx=8;
                    sprintf(buf, "[%03d] ASTEROID: %s", objects[i].id, as_res[r_idx]);
                } else if (objects[i].type == 24) {
                    sprintf(buf, "[%03d] COMM BUOY", objects[i].id);
                } else if (objects[i].type == 50) {
                    sprintf(buf, "[%03d] SUBSPACE ANOMALY", objects[i].id);
                }
                else sprintf(buf, "[%03d] %s (%s)", objects[i].id, objects[i].name, t_name);
                
                drawText3D(850, y_off, 0, buf);
                y_off -= 20;
                if (y_off < 500) {
                    drawText3D(850, y_off, 0, "...");
                    break;
                }
            }
        }

        /* HUD: Add Active Probes to Sensor List */
        for (int p = 0; p < 3; p++) {
            if (g_local_probes[p].active && 
                g_local_probes[p].q1 == g_shared_state->shm_q[0] &&
                g_local_probes[p].q2 == g_shared_state->shm_q[1] &&
                g_local_probes[p].q3 == g_shared_state->shm_q[2]) {
                
                glColor3f(0.0, 0.7, 1.0);
                const char* pr_st = (g_local_probes[p].status == 0) ? "EN ROUTE" : "ACTIVE";
                sprintf(buf, "[%05d] PROBE P%d: %s", 19000 + p, p + 1, pr_st);
                drawText3D(850, y_off, 0, buf);
                y_off -= 20;
            }
        }

        /* --- BOTTOM RIGHT: Deep Space Telemetry --- */
        int ty = 480;
        glColor3f(0.0, 0.8, 1.0); /* LCARS Blue */
        drawText3D(850, ty, 0, "--- Deep Space UPLINK DIAGNOSTICS ---"); ty -= 20;
        
        glColor3f(0.0, 0.5, 0.7);
        sprintf(buf, "LINK UPTIME: %02ld:%02ld:%02ld", g_shared_state->net_uptime/3600, (g_shared_state->net_uptime%3600)/60, g_shared_state->net_uptime%60);
        drawText3D(850, ty, 0, buf); ty -= 15;
        
        sprintf(buf, "BANDWIDTH: %.2f KB/s | PPS: %d", g_shared_state->net_kbps, g_shared_state->net_packet_count);
        drawText3D(850, ty, 0, buf); ty -= 15;
        
        sprintf(buf, "PULSE JITTER: %.2f ms", g_shared_state->net_jitter);
        drawText3D(850, ty, 0, buf); ty -= 15;
        
        sprintf(buf, "SIGNAL INTEGRITY: %.1f%%", g_shared_state->net_integrity);
        drawText3D(850, ty, 0, buf); ty -= 15;

        glColor3f(1.0, 1.0, 0.0);
        sprintf(buf, "POWER: E:%d%%S:%d%%W:%d%%", 
                (int)(g_shared_state->shm_power_dist[0]*100), 
                (int)(g_shared_state->shm_power_dist[1]*100), 
                (int)(g_shared_state->shm_power_dist[2]*100));
        drawText3D(850, ty, 0, buf); ty -= 15;

        glColor3f(0.0, 0.5, 0.7);
        sprintf(buf, "AVG FRAME: %d bytes (Opt: %.1f%%)", g_shared_state->net_avg_packet_size, g_shared_state->net_efficiency);
        drawText3D(850, ty, 0, buf); ty -= 15;

        if (g_shared_state->shm_crypto_algo == CRYPTO_AES) {
            glColor3f(0.0, 1.0, 0.0);
            drawText3D(850, ty, 0, "ENCRYPTION: AES-256-GCM ACTIVE");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_CHACHA) {
            glColor3f(0.0, 1.0, 0.5);
            drawText3D(850, ty, 0, "ENCRYPTION: CHACHA20-POLY ACTIVE");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_ARIA) {
            glColor3f(0.0, 0.7, 1.0);
            drawText3D(850, ty, 0, "ENCRYPTION: ARIA-256-GCM ACTIVE");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_CAMELLIA) {
            glColor3f(0.0, 1.0, 0.0);
            drawText3D(850, ty, 0, "ENCRYPTION: CAMELLIA-256 (Xylari)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_SEED) {
            glColor3f(1.0, 0.5, 0.0);
            drawText3D(850, ty, 0, "ENCRYPTION: SEED-CBC (ORION)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_CAST5) {
            glColor3f(1.0, 1.0, 0.0);
            drawText3D(850, ty, 0, "ENCRYPTION: CAST5-CBC (REPUBLIC)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_IDEA) {
            glColor3f(1.0, 0.0, 1.0);
            drawText3D(850, ty, 0, "ENCRYPTION: IDEA-CBC (MAQUIS)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_3DES) {
            glColor3f(0.5, 0.5, 0.5);
            drawText3D(850, ty, 0, "ENCRYPTION: 3DES-CBC (ANCIENT)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_BLOWFISH) {
            glColor3f(0.7, 0.4, 0.0);
            drawText3D(850, ty, 0, "ENCRYPTION: BLOWFISH-CBC (GILDED)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_RC4) {
            glColor3f(0.0, 0.5, 0.7);
            drawText3D(850, ty, 0, "ENCRYPTION: RC4-STREAM (TACTICAL)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_DES) {
            glColor3f(0.4, 0.4, 0.4);
            drawText3D(850, ty, 0, "ENCRYPTION: DES-CBC (PRE-HYPERDRIVE)");
        } else if (g_shared_state->shm_crypto_algo == CRYPTO_PQC) {
            glColor3f(1.0, 1.0, 1.0);
            drawText3D(850, ty, 0, "ENCRYPTION: ML-KEM-1024 SLOT (EXP. ALIAS: AES-256-GCM)");
        } else {
            glColor3f(1.0, 0.0, 0.0);
            drawText3D(850, ty, 0, "ENCRYPTION: DISABLED / RAW");
        }
        ty -= 15;

        if (g_shared_state->shm_encryption_flags & 0x01) {
            glColor3f(0.0, 1.0, 0.0);
            drawText3D(850, ty, 0, "SIGNATURE: VERIFIED (HMAC-SHA256)");
        } else {
            glColor3f(1.0, 0.5, 0.0);
            drawText3D(850, ty, 0, "SIGNATURE: NOT PRESENT");
        }
        ty -= 25;

        /* --- ADVANCED SYSTEM TELEMETRY --- */
        glColor3f(0.0, 0.8, 1.0);
        drawText3D(850, ty, 0, "--- QUANTUM & LOGIC ANALYTICS ---"); ty -= 20;

        /* Logic & CPU (Simulated based on load) */
        float logic_lat = 0.5 + (rand() % 100) / 200.0;
        float cpu_load = 15.0 + (g_shared_state->object_count * 0.5) + (rand() % 50) / 10.0;
        if (cpu_load > 99.0) cpu_load = 99.0;
        glColor3f(0.0, 1.0, 0.0);
        sprintf(buf, "LOGIC TICK LATENCY: %.2f ms", logic_lat);
        drawText3D(850, ty, 0, buf); ty -= 15;
        sprintf(buf, "CPU POOL LOAD: %.1f%%", cpu_load);
        drawText3D(850, ty, 0, buf); ty -= 15;
        
        /* Memory & Entropy */
        float mem_pressure = 12.4 + (rand() % 20) / 10.0;
        float entropy = 0.85 + (rand() % 150) / 1000.0;
        glColor3f(0.0, 0.7, 1.0);
        sprintf(buf, "SHM MEMORY PRESSURE: %.1f%%", mem_pressure);
        drawText3D(850, ty, 0, buf); ty -= 15;
        sprintf(buf, "CRYPTOGRAPHIC ENTROPY: %.4f", entropy);
        drawText3D(850, ty, 0, buf); ty -= 15;

        /* Network Flux & Spectral Efficiency */
        float spec_eff = g_shared_state->net_efficiency * 0.95;
        float pkt_loss = (100.0 - g_shared_state->net_integrity) * 0.1;
        glColor3f(1.0, 0.8, 0.0);
        sprintf(buf, "NET SPECTRAL EFF: %.1f%%", spec_eff);
        drawText3D(850, ty, 0, buf); ty -= 15;
        sprintf(buf, "PACKET LOSS RATIO: %.3f%%", pkt_loss);
        drawText3D(850, ty, 0, buf); ty -= 15;

        /* Command Buffer & PQC */
        int cmd_fill = (rand() % 5); /* Low fill for normal ops */
        glColor3f(0.0, 1.0, 0.5);
        sprintf(buf, "CMD BUFFER FILL: %d/%d", cmd_fill, 32);
        drawText3D(850, ty, 0, buf); ty -= 15;
        if (g_shared_state->shm_crypto_algo == CRYPTO_PQC) {
            float deco = 0.001 + (rand() % 100) / 100000.0;
            glColor3f(1.0, 1.0, 1.0);
            sprintf(buf, "PQC DECOHERENCE RATE: %.6f", deco);
            drawText3D(850, ty, 0, buf); ty -= 15;
        }
        ty -= 15;

        /* Spatial & Environmental Flux */
        glColor3f(0.0, 0.8, 1.0);
        drawText3D(850, ty, 0, "--- SPATIAL AWARENESS ---"); ty -= 20;
        
        float grav = 0.0;
        float chron = 0.0;
        /* Simple proximity heuristics for telemetry feel */
        for(int i=0; i<MAX_OBJECTS; i++) {
            if (objects[i].id != 0 && objects[i].type != 0) {
                double d = sqrt(pow(objects[i].x-PlayerX,2)+pow(objects[i].y-PlayerY,2)+pow(objects[i].z-PlayerZ,2));
                if (d < 5.0) {
                    if (objects[i].type == 4 || objects[i].type == 6) grav += (5.0 - d) * 0.2;
                    if (objects[i].type == 7 || objects[i].type == 25) chron += (5.0 - d) * 0.15;
                }
            }
        }
        glColor3f(0.8, 0.4, 1.0);
        sprintf(buf, "GRAVITY WELL INTENSITY: %.3f G", 1.0 + grav);
        drawText3D(850, ty, 0, buf); ty -= 15;
        sprintf(buf, "CHRONITON FLUX: %.2f MeV", chron * 120.0);
        drawText3D(850, ty, 0, buf); ty -= 15;
        sprintf(buf, "SPATIAL INDEX DENSITY: %d obj/Q", g_shared_state->object_count);
        drawText3D(850, ty, 0, buf); ty -= 20;

        /* Render Engine Performance */
        static int frame_count = 0;
        static float fps = 60.0;
        static int last_time = 0;
        frame_count++;
        int current_time = glutGet(GLUT_ELAPSED_TIME);
        if (current_time - last_time > 1000) {
            fps = frame_count * 1000.0 / (current_time - last_time);
            last_time = current_time;
            frame_count = 0;
        }
        glColor3f(0.5, 0.5, 0.5);
        sprintf(buf, "VISOR REFRESH: %.1f FPS", fps);
        drawText3D(850, ty, 0, buf); ty -= 15;
        sprintf(buf, "SCENE VERTEX COUNT: ~%d", g_star_count * 6 + g_shared_state->object_count * 500);
        drawText3D(850, ty, 0, buf); ty -= 15;
    }

    int mq1 = g_my_q[0], mq2 = g_my_q[1], mq3 = g_my_q[2];
    long long sn_val = 0;
    if (mq1 >= 0 && mq1 <= GALAXY_SIZE && mq2 >= 0 && mq2 <= GALAXY_SIZE && mq3 >= 0 && mq3 <= GALAXY_SIZE) {
        sn_val = g_shm->shm_galaxy[mq1][mq2][mq3];
    }
    if (g_show_hud && (g_sn_pos.active || sn_val < 0)) {
        /* Supernova Overlay - Centered and prominently Red */
        hudBegin();
        glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);
        
        int sec = 0;
        /* Priority 1: Global event timer from server */
        if (g_sn_pos.active && g_sn_pos.timer > 0) {
            sec = g_sn_pos.timer / GAME_TICK_RATE;
        } 
        /* Priority 2: Fallback to grid-encoded timer (only if valid: 1-1800 ticks) */
        else if (sn_val < 0 && sn_val > -5000) {
            sec = (int)(-sn_val / GAME_TICK_RATE);
        }
        
        if (sec > 60) sec = 60; /* Max supernova countdown is 60s */
        if (sec < 1 && (g_sn_pos.active || (sn_val < 0 && sn_val > -5000))) sec = 1;
        
        char sn_buf[128];
        glColor3f(1.0, 0.0, 0.0); /* Bright Red */
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
            hudEnd(); /* supernova block */
            hudEnd(); /* main HUD block (still open: early return) */
            glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
            return;
        }
        drawText3D(200, 500, 0, sn_buf);
        
        glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING);
        hudEnd(); /* supernova block */
    }

    hudEnd(); /* main HUD block (opened unconditionally above) */
    glutSwapBuffers();
}

void timer(int v) { (void)v; 
    static int global_trail_tick = 0;
    static int lastTime = 0;
    int currentTime = glutGet(GLUT_ELAPSED_TIME);
    if (lastTime == 0) lastTime = currentTime;
    float deltaTime = (currentTime - lastTime) / 16.6667f; 
    if (deltaTime > 5.0f) deltaTime = 1.0f; /* Cap extreme spikes */
    lastTime = currentTime;

    updateParticles();
    if (autoRotate > 5.0) autoRotate = 0.5; /* Cap speed */
    angleY += autoRotate * deltaTime; 
    if (angleY >= 360.0) angleY -= 360.0;
    pulse += 0.05 * deltaTime; 
    
    /* Cinematic Transition Logic */
    if (g_show_map) {
        if (map_anim < 1.0) map_anim += 0.04;
        if (map_anim > 1.0) map_anim = 1.0;
    } else {
        if (map_anim > 0.0) map_anim -= 0.04;
        if (map_anim < 0.0) map_anim = 0.0;
    }

    if (g_show_bridge) {
        if (bridge_anim < 1.0) bridge_anim += 0.03;
        if (bridge_anim > 1.0) bridge_anim = 1.0;
    } else {
        if (bridge_anim > 0.0) bridge_anim -= 0.03;
        if (bridge_anim < 0.0) bridge_anim = 0.0;
    }

    /* Fade out beams */
    for (int i = 0; i < 64; i++) if (beams[i].alpha > 0) beams[i].alpha -= 0.05;

    /* Update Boom Timers */
    for(int b=0; b<10; b++) if (g_booms[b].timer > 0) g_booms[b].timer--;
    
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
            }
        }
    }

    /* Update Objects with Interpolation (GLIDE EFFECT) - Adjusted for 60Hz/60Hz Sync */
    for (int i = 0; i < MAX_OBJECTS; i++) {
        /* Allow ID 0 (Player) to be interpolated, but handle snap-jumps (Quadrant changes) */
        if (objects[i].id == 0 && objects[i].type == 0) continue; 
        
        /* Special handling for Player (ID 0) to prevent lag-behind on camera */
        if (i == 0) {
            /* Detect Quadrant Jump / Teleport (Distance > 50 units) */
            double dx = objects[i].tx - objects[i].x;
            double dy = objects[i].ty - objects[i].y;
            double dz = objects[i].tz - objects[i].z;
            if (dx*dx + dy*dy + dz*dz > 2500.0) {
                objects[i].x = objects[i].tx;
                objects[i].y = objects[i].ty;
                objects[i].z = objects[i].tz;
            }
        }
        
        /* Smooth position interpolation (Dead Reckoning speed with DeltaTime) */
        objects[i].x += objects[i].vx * deltaTime;
        objects[i].y += objects[i].vy * deltaTime;
        objects[i].z += objects[i].vz * deltaTime;
        
        /* Camera/Player synchronization to avoid visual jitter */
        if (i == 0) {
            PlayerX = objects[0].x;
            PlayerY = objects[0].y;
            PlayerZ = objects[0].z;
        }
        
        double dx_err = objects[i].tx - objects[i].x;
        double dy_err = objects[i].ty - objects[i].y;
        double dz_err = objects[i].tz - objects[i].z;
        if (dx_err*dx_err + dy_err*dy_err + dz_err*dz_err > 100.0) {
            objects[i].x = objects[i].tx;
            objects[i].y = objects[i].ty;
            objects[i].z = objects[i].tz;
        } else {
            /* Smooth convergence towards the target to eliminate back-and-forth stutter */
            objects[i].x += dx_err * INTERP_SPEED_OBJECT * deltaTime;
            objects[i].y += dy_err * INTERP_SPEED_OBJECT * deltaTime;
            objects[i].z += dz_err * INTERP_SPEED_OBJECT * deltaTime;
        }
        
        /* Smooth orientation interpolation (Heading/Mark/Roll) */
        double dh = objects[i].th - objects[i].h;
        if (dh > 180.0) dh -= 360.0;
        if (dh < -180.0) dh += 360.0;
        objects[i].h += dh * INTERP_SPEED_ROT * deltaTime;
        if (objects[i].h >= 360.0) objects[i].h -= 360.0;
        if (objects[i].h < 0.0) objects[i].h += 360.0;

        objects[i].m += (objects[i].tm - objects[i].m) * INTERP_SPEED_ROT * deltaTime;
        objects[i].r += (objects[i].tr - objects[i].r) * INTERP_SPEED_ROT * deltaTime;
        
        if (objects[i].type == 1 || objects[i].type >= 10) {
            /* Check for jump only if we have a history */
            if (objects[i].trail_count > 0) {
                double lastX = objects[i].trail[(objects[i].trail_ptr - 1 + MAX_TRAIL) % MAX_TRAIL][0];
                double lastY = objects[i].trail[(objects[i].trail_ptr - 1 + MAX_TRAIL) % MAX_TRAIL][1];
                double lastZ = objects[i].trail[(objects[i].trail_ptr - 1 + MAX_TRAIL) % MAX_TRAIL][2];

                /* Jump detection (quadrant change or teleport) */
                double dx = objects[i].x - lastX;
                double dy = objects[i].y - lastY;
                double dz = objects[i].z - lastZ;
                double dist_sq = dx*dx + dy*dy + dz*dz;
                if (dist_sq > 400.0) {
                    objects[i].trail_count = 0;
                    objects[i].trail_ptr = 0;
                }
            }

            if (global_trail_tick % 2 == 0) { /* Reduce trail density for visual fluidity */
                objects[i].trail[objects[i].trail_ptr][0] = objects[i].x;
                objects[i].trail[objects[i].trail_ptr][1] = objects[i].y;
                objects[i].trail[objects[i].trail_ptr][2] = objects[i].z;
                objects[i].trail_ptr = (objects[i].trail_ptr + 1) % MAX_TRAIL;
                if (objects[i].trail_count < MAX_TRAIL) objects[i].trail_count++;
            }
        }
    }

    /* Torpedo Movement - Linear projection, server-authoritative */
    static double torp_time_acc = 0;
    torp_time_acc += deltaTime;
    bool tick_timer = (torp_time_acc >= 0.01666);
    if (tick_timer) torp_time_acc = 0;

    for (int s = 0; s < MAX_VISIBLE_TORPEDOES; s++) {
        if (g_torps[s].active) {
            /* Lifecycle movement */
            g_torps[s].x += g_torps[s].vx * deltaTime;
            g_torps[s].y += g_torps[s].vy * deltaTime;
            g_torps[s].z += g_torps[s].vz * deltaTime;

            /* Strict Boundary Check: Deactivate if it leaves the tactical zone (margin 0.1) */
            double d_limit = (QUADRANT_SIZE / 2.0) + 0.1;
            if (fabs(g_torps[s].x) > d_limit || fabs(g_torps[s].y) > d_limit || fabs(g_torps[s].z) > d_limit) {
                g_torps[s].active = 0;
            }

            if (tick_timer) {
                if (--g_torps[s].timer <= 0) {
                    g_torps[s].active = 0;
                }
            }
        }
    }
    global_trail_tick++;
}
void keyboard(unsigned char k, int x, int y) { (void)k; (void)x; (void)y; 
    if(k==27) exit(0); 
    if(k==' ') autoRotate=(autoRotate==0)?0.15:0; 
    if(k=='w' || k=='W') zoom += 0.5;
    if(k=='s' || k=='S') zoom -= 0.5;
    if(k=='h' || k=='H') g_show_hud = !g_show_hud;
    if(k=='n' || k=='N') g_show_nebulas = !g_show_nebulas;
}
void special(int k, int x, int y) { (void)k; (void)x; (void)y; 
    if(k==GLUT_KEY_F7) {
        g_aniso_level *= 2; if (g_aniso_level > 16) g_aniso_level = 1;
        printf("[GFX] Anisotropic Filtering set to %dx\n", g_aniso_level);
    }
    if(k==GLUT_KEY_F8) {
        g_star_count += 1000; if (g_star_count > 8000) g_star_count = 1000;
        printf("[GFX] Starfield Density: %d stars\n", g_star_count);
    }
    if(k==GLUT_KEY_UP) angleX-=2.5; 
    if(k==GLUT_KEY_DOWN) angleX+=2.5; 
    if(angleX > 85.0) angleX = 85.0;
    if(angleX < -85.0) angleX = -85.0;
    if(k==GLUT_KEY_LEFT) angleY-=5; 
    if(k==GLUT_KEY_RIGHT) angleY+=5; 
}

void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)window; (void)scancode; (void)mods;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    
    if (key == GLFW_KEY_ESCAPE) exit(0);
    
    // Map to GLUT keyboard()
    if (key >= 32 && key <= 95) keyboard((unsigned char)key, 0, 0);
    else if (key == GLFW_KEY_SPACE) keyboard(' ', 0, 0);
    else if (key == GLFW_KEY_W) keyboard('W', 0, 0);
    else if (key == GLFW_KEY_S) keyboard('S', 0, 0);
    else if (key == GLFW_KEY_H) keyboard('H', 0, 0);

    // Map to GLUT special()
    int sk = -1;
    if (key == GLFW_KEY_F1) sk = GLUT_KEY_F1;
    else if (key == GLFW_KEY_F2) sk = GLUT_KEY_F2;
    else if (key == GLFW_KEY_F3) sk = GLUT_KEY_F3;
    else if (key == GLFW_KEY_F4) sk = GLUT_KEY_F4;
    else if (key == GLFW_KEY_F5) sk = GLUT_KEY_F5;
    else if (key == GLFW_KEY_F6) sk = GLUT_KEY_F6;
    else if (key == GLFW_KEY_F7) sk = GLUT_KEY_F7;
    else if (key == GLFW_KEY_F8) sk = GLUT_KEY_F8;
    else if (key == GLFW_KEY_F9) sk = GLUT_KEY_F9;
    else if (key == GLFW_KEY_F10) sk = GLUT_KEY_F10;
    else if (key == GLFW_KEY_F11) sk = GLUT_KEY_F11;
    else if (key == GLFW_KEY_F12) sk = GLUT_KEY_F12;
    else if (key == GLFW_KEY_LEFT) sk = GLUT_KEY_LEFT;
    else if (key == GLFW_KEY_UP) sk = GLUT_KEY_UP;
    else if (key == GLFW_KEY_RIGHT) sk = GLUT_KEY_RIGHT;
    else if (key == GLFW_KEY_DOWN) sk = GLUT_KEY_DOWN;
    
    if (sk != -1) special(sk, 0, 0);
}

void glfw_reshape_callback(GLFWwindow* window, int w, int h) {
    (void)window;
    reshape(w, h);
}

void check_display_protocol() {
    /* No-op: GLFW handles Wayland natively */
}

int main(int argc, char** argv) {
    sglog_init("glv");
    sglog_apply_args(argc, argv);
    check_display_protocol();
    /* Handle --help and --version for help2man */
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            printf("Usage: spacegl_3dview [SHM_NAME]\n");
            printf("Space GL OpenGL Tactical Viewer\n\n");
            printf("Options:\n");
            printf("  --help, -h     Display this help and exit\n");
            printf("  --version      Display version information and exit\n\n");
            printf("Environment Variables:\n");
            printf("  DISPLAY        X11 Display for OpenGL output\n");
            return 0;
        }
        if (strcmp(argv[1], "--version") == 0) {
            printf("spacegl_3dview 2026.04.12\n");
            return 0;
        }
    }

    setlocale(LC_ALL, "C"); signal(SIGUSR1, handle_signal);

    memset(objects, 0, sizeof(objects));
    memset(g_torps, 0, sizeof(g_torps));
    for (int i = 0; i < MAX_VISIBLE_TORPEDOES; i++) g_torps[i].id = -1;

    for(int i=0; i<MAX_OBJECTS; i++) { objects[i].x = objects[i].y = objects[i].z = -100.0; }

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

    g_shm = mmap(NULL, sizeof(SharedIPC), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    if (g_shm == MAP_FAILED) {
        perror("[3D VIEW] mmap failed");
        exit(1);
    }
    
    /* Set initial read buffer */
    g_shared_state = &g_shm->buffers[atomic_load(&g_shm->read_index)];
        
    printf("[3D VIEW] Shared memory mapped successfully.\n");
    pthread_t stid;
    
    if (pthread_create(&stid, NULL, shm_listener_thread, NULL) != 0) {
        perror("[3D VIEW] Failed to create listener thread");
        exit(1);
    }

    printf("[3D VIEW] Initializing GLFW...\n");
    if (!glfwInit()) {
        fprintf(stderr, "[3D VIEW] Failed to initialize GLFW\n");
        return 1;
    }
    
    /* Request OpenGL 3.0 Compatibility Profile for legacy + shader support */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_SAMPLES, 4);
    
    GLFWwindow* window = glfwCreateWindow(TACTICAL_CUBE_W, TACTICAL_CUBE_H, "Space GL 3DView - Multiuser", NULL, NULL);
    if (!window) {
        /* Fallback to 2.1 if 3.0 fails */
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        window = glfwCreateWindow(TACTICAL_CUBE_W, TACTICAL_CUBE_H, "Space GL 3DView - Multiuser", NULL, NULL);
    }

    if (!window) {
        fprintf(stderr, "[3D VIEW] Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); /* Enable VSync: Native 60FPS synchronization */
    glfwSetFramebufferSizeCallback(window, glfw_reshape_callback);
    glfwSetKeyCallback(window, glfw_key_callback);
    
    /* Initialize GLEW */
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (GLEW_OK != err) {
        /* On some drivers, glewInit might return GLEW_ERROR_NO_GL_VERSION (1) 
           even if context is valid when using glewExperimental. 
           We check if the context is actually working. */
        const GLubyte* version = glGetString(GL_VERSION);
        if (version) {
            printf("[3D VIEW] GLEW init reported error but context is valid. Version: %s\n", version);
        } else {
            fprintf(stderr, "[3D VIEW] GLEW Error: %s (Code: %d)\n", glewGetErrorString(err), err);
            return 1;
        }
    } else {
        printf("[3D VIEW] GLEW initialized. OpenGL Version: %s\n", glGetString(GL_VERSION));
    }

    /* Initialize from the LIVE framebuffer size (may differ from the window
     * request, e.g. HiDPI): viewport, bloom FBOs and HUD font in one shot. */
    { int fw, fh; glfwGetFramebufferSize(window, &fw, &fh); reshape(fw, fh); }

    /* Initialize Shader Engine */
    initShaders();

    glEnable(GL_DEPTH_TEST); 
    glEnable(GL_CULL_FACE); /* Optimization: Don't render internal/back faces */
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND); 
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
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
    GLfloat global_amb[] = {0.2, 0.2, 0.25, 1.0};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, global_amb);

    /* Main Light (Headlight) */
    GLfloat lp[] = {0, 0, 10, 1}; /* Light coming from camera direction */
    glLightfv(GL_LIGHT0, GL_POSITION, lp);
    GLfloat white[] = {1.0, 1.0, 1.0, 1.0};
    glLightfv(GL_LIGHT0, GL_DIFFUSE, white);
    glLightfv(GL_LIGHT0, GL_SPECULAR, white);
    
    initStars(); 
    initVBOs(); 
    
    glMatrixMode(GL_PROJECTION); 
    gluPerspective(45, (double)g_fb_w/g_fb_h, 1, 500); 
    glMatrixMode(GL_MODELVIEW);
        
    printf("[3D VIEW] Ready. Sending handshake to parent (PID %d).\n", getppid());
    kill(getppid(), SIGUSR2); 
    
    while (!glfwWindowShouldClose(window)) {
        timer(0);
        display();
        glfwSwapBuffers(window);
        glfwPollEvents();
        /* usleep removed: glfwSwapInterval(1) handles timing natively and smoothly */
    }
    
    glfwTerminate();
    return 0;
}
    
