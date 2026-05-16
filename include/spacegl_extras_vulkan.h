#ifndef SPACEGL_EXTRAS_VULKAN_H
#define SPACEGL_EXTRAS_VULKAN_H

void drawBokGlobule_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawInterstellarBubble_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawClumpCore_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);

void drawRelativisticJet_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawRelativisticJet_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
#endif
