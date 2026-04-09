/**
 * @file material.cpp
 */

#include "vulkan/material.hpp"

#include "core/log/logger.hpp"
#include "vulkan/buffer.hpp"

namespace vulkan {

Material::Material(const VkDevice device, const VkDescriptorSet set) noexcept
    : device_(device), set_(set) {}

std::expected<Material, std::string>
Material::create(const VkDevice device, const VkDescriptorPool pool,
                 const VkDescriptorSetLayout layout) {
    if (device == VK_NULL_HANDLE || pool == VK_NULL_HANDLE ||
        layout == VK_NULL_HANDLE) {
        return std::unexpected(
            std::string("Material::create: null device, pool, or layout"));
    }
    VkDescriptorSetAllocateInfo alloc {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout,
    };
    VkDescriptorSet set { VK_NULL_HANDLE };
    if (vkAllocateDescriptorSets(device, &alloc, &set) != VK_SUCCESS) {
        LUMEN_LOG_ERROR("Material::create: vkAllocateDescriptorSets failed");
        return std::unexpected(
            std::string("Material::create: vkAllocateDescriptorSets failed"));
    }
    return Material(device, set);
}

void Material::write_uniform_buffer(const std::uint32_t binding,
                                    const Buffer &buffer, const VkDeviceSize offset,
                                    const VkDeviceSize range) {
    if (device_ == VK_NULL_HANDLE || set_ == VK_NULL_HANDLE) {
        return;
    }
    VkDescriptorBufferInfo buf_info {
        .buffer = buffer.buffer(),
        .offset = offset,
        .range = range,
    };
    VkWriteDescriptorSet write {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set_,
        .dstBinding = binding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &buf_info,
    };
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void Material::write_combined_image_sampler(
    const std::uint32_t binding, const VkImageView image_view,
    const VkSampler sampler, const VkImageLayout layout) {
    if (device_ == VK_NULL_HANDLE || set_ == VK_NULL_HANDLE) {
        return;
    }
    VkDescriptorImageInfo img_info {
        .sampler = sampler,
        .imageView = image_view,
        .imageLayout = layout,
    };
    VkWriteDescriptorSet write {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set_,
        .dstBinding = binding,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &img_info,
    };
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

} // namespace vulkan
