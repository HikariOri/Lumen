/**
 * @file descriptor.cpp
 * @brief Descriptor 实现
 */

#include "render/resource/descriptor.hpp"
#include "render/context.hpp"
#include "core/logger.hpp"

namespace lumen {
namespace render {

bool DescriptorSetLayout::create(const Context& ctx,
                                 const std::vector<DescriptorBinding>& bindings) {
    device_ = ctx.device();

    std::vector<VkDescriptorSetLayoutBinding> vkBindings(bindings.size());
    for (size_t i { 0 }; i < bindings.size(); ++i) {
        vkBindings[i].binding = bindings[i].binding;
        vkBindings[i].descriptorType = bindings[i].type;
        vkBindings[i].descriptorCount = bindings[i].count;
        vkBindings[i].stageFlags = bindings[i].stages;
    }

    VkDescriptorSetLayoutCreateInfo createInfo {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    createInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
    createInfo.pBindings = vkBindings.data();

    VkResult result =
        vkCreateDescriptorSetLayout(device_, &createInfo, nullptr, &layout_);
    if (result == VK_SUCCESS) {
        LUMEN_LOG_DEBUG("DescriptorSetLayout 创建成功 bindings={}",
                        bindings.size());
    }
    return result == VK_SUCCESS;
}

void DescriptorSetLayout::destroy_() {
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
}

DescriptorSetLayout::~DescriptorSetLayout() { destroy_(); }

DescriptorSetLayout::DescriptorSetLayout(DescriptorSetLayout&& other) noexcept
    : device_ { other.device_ }
    , layout_ { other.layout_ } {
    other.device_ = VK_NULL_HANDLE;
    other.layout_ = VK_NULL_HANDLE;
}

DescriptorSetLayout&
DescriptorSetLayout::operator=(DescriptorSetLayout&& other) noexcept {
    if (this == &other) return *this;
    destroy_();
    device_ = other.device_;
    layout_ = other.layout_;
    other.device_ = VK_NULL_HANDLE;
    other.layout_ = VK_NULL_HANDLE;
    return *this;
}

// --- DescriptorPool ---

bool DescriptorPool::create(const Context& ctx,
                            const std::vector<DescriptorPoolSize>& poolSizes,
                            uint32_t maxSets) {
    device_ = ctx.device();

    std::vector<VkDescriptorPoolSize> vkSizes(poolSizes.size());
    for (size_t i { 0 }; i < poolSizes.size(); ++i) {
        vkSizes[i].type = poolSizes[i].type;
        vkSizes[i].descriptorCount = poolSizes[i].count;
    }

    VkDescriptorPoolCreateInfo createInfo {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    createInfo.poolSizeCount = static_cast<uint32_t>(vkSizes.size());
    createInfo.pPoolSizes = vkSizes.data();
    createInfo.maxSets = maxSets;

    VkResult result =
        vkCreateDescriptorPool(device_, &createInfo, nullptr, &pool_);
    if (result == VK_SUCCESS) {
        LUMEN_LOG_DEBUG("DescriptorPool 创建成功 maxSets={}", maxSets);
    }
    return result == VK_SUCCESS;
}

bool DescriptorPool::allocate(VkDevice device, VkDescriptorSetLayout layout,
                              VkDescriptorSet& outSet) {
    VkDescriptorSetAllocateInfo allocInfo {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.descriptorPool = pool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    return vkAllocateDescriptorSets(device, &allocInfo, &outSet) == VK_SUCCESS;
}

void DescriptorPool::reset() {
    if (pool_ != VK_NULL_HANDLE) {
        vkResetDescriptorPool(device_, pool_, 0);
    }
}

void DescriptorPool::destroy_() {
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
}

DescriptorPool::~DescriptorPool() { destroy_(); }

DescriptorPool::DescriptorPool(DescriptorPool&& other) noexcept
    : device_ { other.device_ }
    , pool_ { other.pool_ } {
    other.device_ = VK_NULL_HANDLE;
    other.pool_ = VK_NULL_HANDLE;
}

DescriptorPool& DescriptorPool::operator=(DescriptorPool&& other) noexcept {
    if (this == &other) return *this;
    destroy_();
    device_ = other.device_;
    pool_ = other.pool_;
    other.device_ = VK_NULL_HANDLE;
    other.pool_ = VK_NULL_HANDLE;
    return *this;
}

// --- write helpers ---

void write_descriptor_buffer(VkDevice device, VkDescriptorSet set,
                             uint32_t binding, VkDescriptorType type,
                             VkBuffer buffer, size_t offset, size_t range) {
    VkDescriptorBufferInfo bufferInfo {};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = offset;
    bufferInfo.range = range;

    VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = type;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void write_descriptor_image(VkDevice device, VkDescriptorSet set,
                            uint32_t binding, VkImageView imageView,
                            VkSampler sampler, VkImageLayout imageLayout) {
    VkDescriptorImageInfo imageInfo {};
    imageInfo.imageLayout = imageLayout;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

} // namespace render
} // namespace lumen
