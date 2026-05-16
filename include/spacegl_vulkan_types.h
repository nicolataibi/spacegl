#ifndef SPACEGL_VULKAN_TYPES_H
#define SPACEGL_VULKAN_TYPES_H
#include <vulkan/vulkan.h>

#define CIRCLE_SEGMENTS 72
#define ARC_SEGMENTS 37

const uint16_t gridIndices[84]; /* Forward declaration to keep code clean */

const Vertex vectorVertices[] = {
    {{0.18f, 0, 0}, {0, 1, 0}, {1, 0, 0}}, {{1.38f, 0, 0}, {0, 1, 0}, {1, 0, 0}}, /* Line */
    {{1.38f, 0.05f, 0.05f}, {0, 1, 0}, {1, 0, 0}}, {{1.38f, -0.05f, 0.05f}, {0, 1, 0}, {1, 0, 0}},
    {{1.38f, -0.05f, -0.05f}, {0, 1, 0}, {1, 0, 0}}, {{1.38f, 0.05f, -0.05f}, {0, 1, 0}, {1, 0, 0}},
    {{1.68f, 0, 0}, {0, 1, 0}, {1, 0, 0}} /* Tip */
};
const uint32_t vectorIndices[] = {
    0, 1, 
    2, 3, 3, 4, 4, 5, 5, 2, 
    6, 2, 6, 3, 6, 4, 6, 5 
};

typedef struct { mat4 model; float color[4]; float time; int usePushColor; float metallic; float roughness; } PushConstants;
typedef struct { mat4 view; mat4 proj; } UniformBufferObject;

#define MAX_FRAMES_IN_FLIGHT 2
#define MAX_ACTIVE_BEAMS 64
#define MAX_ACTIVE_BOOMS 128
#define MAX_ACTIVE_TORPS 256
#define MAX_ACTIVE_DISMANTLES 64
#define EXPLOSION_PIXELS 256
#define MAX_STARS 2000
#define MAX_ARRIVAL_PARTICLES 500

/* Starfield Mode Selection Macros */
#define STARFIELD_MODE_TWINKLE 1  /* Dynamic intensity variation */
#define STARFIELD_MODE_HALO    2  /* Geometric atmospheric glow */
#define STARFIELD_MODE_BLOOM   3  /* Point Sprite Procedural Bloom (Most Realistic) */

#define STARFIELD_MODE STARFIELD_MODE_TWINKLE

typedef struct { float sx, sy, sz, tx, ty, tz, life; int owner_id; int extra; int emitter_id; } ActiveBeam;
typedef struct { float x, y, z, life; float offsets[EXPLOSION_PIXELS][3]; float colors[EXPLOSION_PIXELS][3]; } ActiveBoom;
typedef struct { float x, y, z, life; float scale; } ActiveDismantle;
typedef struct { float x, y, z; float dx, dy, dz; int active; int id; } ActiveTorp;
typedef struct { float x, y, z; float angle; float radius; float speed; int active; } ArrivalParticle;

typedef struct { float x, y, z; double h, m; int active; int timer; } JumpState;
typedef struct { float x, y, z; double h, m; int active; } WormholeState;

typedef struct VulkanApp {
    GLFWwindow* window; VkInstance instance; VkPhysicalDevice physicalDevice; VkDevice device; VkQueue graphicsQueue;
    VkSurfaceKHR surface; VkSwapchainKHR swapChain; VkImage* swapChainImages; uint32_t swapChainImageCount;
    VkFormat swapChainImageFormat; VkExtent2D swapChainExtent; VkImageView* swapChainImageViews;
    VkRenderPass renderPass; VkDescriptorSetLayout descriptorSetLayout; VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline; VkPipeline wireframePipeline; VkPipeline pointPipeline; VkPipeline glowPipeline; VkPipeline alphaPipeline;
    VkFramebuffer* swapChainFramebuffers;
    VkCommandPool commandPool; VkSampleCountFlagBits msaaSamples;
    VkImage colorImage; VkDeviceMemory colorImageMemory; VkImageView colorImageView;
    VkImage depthImage; VkDeviceMemory depthImageMemory; VkImageView depthImageView;
    VkBuffer vertexBuffer; VkDeviceMemory vertexBufferMemory; VkBuffer indexBuffer; VkDeviceMemory indexBufferMemory;
    VkBuffer shipVertexBuffer; VkDeviceMemory shipVertexBufferMemory; VkBuffer shipIndexBuffer; VkDeviceMemory shipIndexBufferMemory;
    VkBuffer starbaseVertexBuffer; VkDeviceMemory starbaseVertexBufferMemory; VkBuffer starbaseIndexBuffer; VkDeviceMemory starbaseIndexBufferMemory;
    VkBuffer cubeVertexBuffer; VkDeviceMemory cubeVertexBufferMemory; 
    VkBuffer cubeIndexBuffer; VkDeviceMemory cubeIndexBufferMemory;
    VkBuffer cubeSolidIndexBuffer; VkDeviceMemory cubeSolidIndexBufferMemory;
    VkBuffer torpVertexBuffer; VkDeviceMemory torpVertexBufferMemory; VkBuffer torpIndexBuffer; VkDeviceMemory torpIndexBufferMemory;
    VkBuffer axesVertexBuffer; VkDeviceMemory axesVertexBufferMemory; VkBuffer axesIndexBuffer; VkDeviceMemory axesIndexBufferMemory;
    VkBuffer beamVertexBuffer; VkDeviceMemory beamVertexBufferMemory; VkBuffer beamIndexBuffer; VkDeviceMemory beamIndexBufferMemory;
    VkBuffer circleVertexBuffer; VkDeviceMemory circleVertexBufferMemory; VkBuffer circleIndexBuffer; VkDeviceMemory circleIndexBufferMemory;
    VkBuffer arcVertexBuffer; VkDeviceMemory arcVertexBufferMemory; VkBuffer arcIndexBuffer; VkDeviceMemory arcIndexBufferMemory;
    VkBuffer rollCircleVertexBuffer; VkDeviceMemory rollCircleVertexBufferMemory; VkBuffer rollCircleIndexBuffer; VkDeviceMemory rollCircleIndexBufferMemory;
    VkBuffer gridVertexBuffer; VkDeviceMemory gridVertexBufferMemory; VkBuffer gridIndexBuffer; VkDeviceMemory gridIndexBufferMemory;
    VkBuffer vectorVertexBuffer; VkDeviceMemory vectorVertexBufferMemory; VkBuffer vectorIndexBuffer; VkDeviceMemory vectorIndexBufferMemory;
    VkBuffer sphereVertexBuffer; VkDeviceMemory sphereVertexBufferMemory; VkBuffer sphereIndexBuffer; VkDeviceMemory sphereIndexBufferMemory;
    VkBuffer whVertexBuffer; VkDeviceMemory whVertexBufferMemory; VkBuffer whIndexBuffer; VkDeviceMemory whIndexBufferMemory; uint32_t whIndexCount;
    VkBuffer coreVB[13]; VkDeviceMemory coreVBM[13]; VkBuffer coreIB[13]; VkDeviceMemory coreIBM[13]; uint32_t coreICount[13];
    VkBuffer starfieldVertexBuffer; VkDeviceMemory starfieldVertexBufferMemory; VkBuffer starfieldIndexBuffer; VkDeviceMemory starfieldIndexBufferMemory;
    uint64_t gridVertexCount; uint64_t starfieldIndexCount;
    VkBuffer uniformBuffers[MAX_FRAMES_IN_FLIGHT]; VkDeviceMemory uniformBuffersMemory[MAX_FRAMES_IN_FLIGHT];
    VkDescriptorPool descriptorPool; VkDescriptorSet descriptorSets[MAX_FRAMES_IN_FLIGHT];
    VkCommandBuffer commandBuffers[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore imageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT]; VkSemaphore renderFinishedSemaphores[MAX_FRAMES_IN_FLIGHT];
    VkFence inFlightFences[MAX_FRAMES_IN_FLIGHT]; uint32_t currentFrame;
    SharedIPC* shm; int shm_fd; float angleY; float angleX; float cameraDist; bool autoRotate;
    ActiveBeam activeBeams[MAX_ACTIVE_BEAMS]; ActiveBoom activeBooms[MAX_ACTIVE_BOOMS];
    ActiveDismantle activeDismantles[MAX_ACTIVE_DISMANTLES];
    ActiveTorp activeTorps[MAX_ACTIVE_TORPS]; ArrivalParticle arrivalParticles[MAX_ARRIVAL_PARTICLES];
    JumpState jumpArrival; WormholeState departureWormhole;
    /* Smooth State Interpolation */
    long long last_shm_frame_id;
    double last_shm_time;
    double smoothed_shm_time;
    struct {
        float x, y, z, h, m, r;
        float prev_x, prev_y, prev_z, prev_h, prev_m, prev_r;
        float target_x, target_y, target_z, target_h, target_m, target_r;
        float vx, vy, vz;
        float prev_vx, prev_vy, prev_vz;
        bool first;
    } smoothObjs[MAX_NET_OBJECTS];

    float mapAnim;
    int mapFilter;
#endif
