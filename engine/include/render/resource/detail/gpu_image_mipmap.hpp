/**
 * @file gpu_image_mipmap.hpp
 * @brief 2D 颜色图像：layout barrier 与 GPU mipmap 生成（内部实现细节）
 */
#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

namespace lumen::render::gpu_image_mipmap {

[[nodiscard]] uint32_t calculate_mip_levels(uint32_t width, uint32_t height);

void transition_image_subresource(VkCommandBuffer cmd, VkImage image,
                                  VkImageLayout oldLayout,
                                  VkImageLayout newLayout,
                                  uint32_t baseMipLevel, uint32_t levelCount,
                                  uint32_t baseArrayLayer,
                                  uint32_t layerCount);

void transition_image_layout(VkCommandBuffer cmd, VkImage image,
                             VkImageLayout oldLayout, VkImageLayout newLayout,
                             uint32_t baseMipLevel, uint32_t levelCount);

void generate_mipmaps(VkCommandBuffer cmd, VkImage image, uint32_t width,
                      uint32_t height, uint32_t mipLevels);

} // namespace lumen::render::gpu_image_mipmap
