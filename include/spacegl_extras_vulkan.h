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

#ifndef SPACEGL_EXTRAS_VULKAN_H
#define SPACEGL_EXTRAS_VULKAN_H

void drawBokGlobule_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawInterstellarBubble_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawClumpCore_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);

void drawRelativisticJet_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
void drawRelativisticJet_Vulkan(VkCommandBuffer cb, VulkanApp* app, mat4 baseT, float tactScale, float pulse);
#endif
