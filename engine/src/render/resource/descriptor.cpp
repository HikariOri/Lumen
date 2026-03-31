/**
 * @file descriptor.cpp
 * @brief Descriptor 系统实现（Layout / Pool / Write）
 *
 * Vulkan Descriptor 系统的核心实现：
 * - DescriptorSetLayout：定义 shader 资源接口
 * - DescriptorPool：管理 descriptor 内存池
 * - write_*：更新 GPU 可见资源绑定
 */

#include "render/resource/descriptor.hpp"
#include "core/logger.hpp"
#include "render/context.hpp"

namespace lumen {
namespace render {

// =====================================================
// DescriptorSetLayout
// =====================================================

bool DescriptorSetLayout::create(
    const Context &ctx, const std::vector<DescriptorBinding> &bindings) {

    // 保存 device，用于后续 destroy
    device_ = ctx.device();

    /**
     * Vulkan 要求使用 VkDescriptorSetLayoutBinding
     * 而我们上层封装 DescriptorBinding → Vulkan binding
     */
    std::vector<VkDescriptorSetLayoutBinding> vkBindings(bindings.size());

    for (size_t i = 0; i < bindings.size(); ++i) {
        vkBindings[i].binding = bindings[i].binding;

        // descriptor 类型（UBO / sampler / storage buffer）
        vkBindings[i].descriptorType = bindings[i].type;

        // array 数量（支持 texture array / UBO array）
        vkBindings[i].descriptorCount = bindings[i].count;

        // shader 可见阶段（vertex / fragment / compute）
        vkBindings[i].stageFlags = bindings[i].stages;
    }

    /**
     * DescriptorSetLayout = shader interface definition
     * 类似 HLSL root signature / OpenGL uniform layout
     */
    VkDescriptorSetLayoutCreateInfo createInfo {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };

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

DescriptorSetLayout::DescriptorSetLayout(DescriptorSetLayout &&other) noexcept
    : device_(other.device_), layout_(other.layout_) {

    // move 后清空源对象，避免 double free
    other.device_ = VK_NULL_HANDLE;
    other.layout_ = VK_NULL_HANDLE;
}

DescriptorSetLayout &
DescriptorSetLayout::operator=(DescriptorSetLayout &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy_();

    device_ = other.device_;
    layout_ = other.layout_;

    other.device_ = VK_NULL_HANDLE;
    other.layout_ = VK_NULL_HANDLE;

    return *this;
}

// =====================================================
// DescriptorPool
// =====================================================

bool DescriptorPool::create(const Context &ctx,
                            const std::vector<DescriptorPoolSize> &poolSizes,
                            uint32_t maxSets) {

    device_ = ctx.device();

    /**
     * Vulkan DescriptorPool 是“预分配池”
     * 必须提前声明各类 descriptor 数量
     */
    std::vector<VkDescriptorPoolSize> vkSizes(poolSizes.size());

    for (size_t i = 0; i < poolSizes.size(); ++i) {
        vkSizes[i].type = poolSizes[i].type;
        vkSizes[i].descriptorCount = poolSizes[i].count;
    }

    VkDescriptorPoolCreateInfo createInfo {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
    };

    createInfo.poolSizeCount = static_cast<uint32_t>(vkSizes.size());
    createInfo.pPoolSizes = vkSizes.data();

    // 最多能分配多少个 DescriptorSet
    createInfo.maxSets = maxSets;

    VkResult result =
        vkCreateDescriptorPool(device_, &createInfo, nullptr, &pool_);

    if (result == VK_SUCCESS) {
        LUMEN_LOG_DEBUG("DescriptorPool 创建成功 maxSets={}", maxSets);
    }

    return result == VK_SUCCESS;
}

bool DescriptorPool::allocate(VkDevice device, VkDescriptorSetLayout layout,
                              VkDescriptorSet &outSet) {

    /**
     * 从 pool 中分配 descriptor set
     *
     * ⚠️ 注意：
     * - pool 不够会失败
     * - layout 必须匹配 shader
     */
    VkDescriptorSetAllocateInfo allocInfo {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
    };

    allocInfo.descriptorPool = pool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    return vkAllocateDescriptorSets(device, &allocInfo, &outSet) == VK_SUCCESS;
}

void DescriptorPool::reset() {
    if (pool_ != VK_NULL_HANDLE) {

        /**
         * 重置 pool = 一次性释放所有 descriptor set
         * ⚠️ 所有已分配 set 都失效
         */
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

DescriptorPool::DescriptorPool(DescriptorPool &&other) noexcept
    : device_(other.device_), pool_(other.pool_) {

    other.device_ = VK_NULL_HANDLE;
    other.pool_ = VK_NULL_HANDLE;
}

DescriptorPool &DescriptorPool::operator=(DescriptorPool &&other) noexcept {
    if (this == &other)
        return *this;

    destroy_();

    device_ = other.device_;
    pool_ = other.pool_;

    other.device_ = VK_NULL_HANDLE;
    other.pool_ = VK_NULL_HANDLE;

    return *this;
}

// =====================================================
// Descriptor Write Helpers
// =====================================================

void write_descriptor_set(VkDevice device, VkDescriptorSet set,
                          std::initializer_list<DescriptorWriteBuffer> buffers,
                          std::initializer_list<DescriptorWriteImage> images) {

    const size_t nb = buffers.size();
    const size_t ni = images.size();
    if (nb + ni == 0) {
        return;
    }

    std::vector<VkDescriptorBufferInfo> bufferInfos(nb);
    {
        size_t i = 0;
        for (const DescriptorWriteBuffer &b : buffers) {
            bufferInfos[i].buffer = b.buffer;
            bufferInfos[i].offset = b.offset;
            bufferInfos[i].range = b.range;
            ++i;
        }
    }

    std::vector<VkDescriptorImageInfo> imageInfos(ni);
    {
        size_t i = 0;
        for (const DescriptorWriteImage &im : images) {
            imageInfos[i].imageLayout = im.imageLayout;
            imageInfos[i].imageView = im.imageView;
            imageInfos[i].sampler = im.sampler;
            ++i;
        }
    }

    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(nb + ni);

    {
        size_t i = 0;
        for (const DescriptorWriteBuffer &b : buffers) {
            VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = set;
            write.dstBinding = b.binding;
            write.dstArrayElement = 0;
            write.descriptorType = b.type;
            write.descriptorCount = 1;
            write.pBufferInfo = &bufferInfos[i];
            writes.push_back(write);
            ++i;
        }
    }
    {
        size_t i = 0;
        for (const DescriptorWriteImage &im : images) {
            VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = set;
            write.dstBinding = im.binding;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfos[i];
            writes.push_back(write);
            ++i;
        }
    }

    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);
}

void write_descriptor_buffer(VkDevice device, VkDescriptorSet set,
                             uint32_t binding, VkDescriptorType type,
                             VkBuffer buffer, size_t offset, size_t range) {

    /**
     * UBO / SSBO 写入 descriptor set
     *
     * 本质：
     * vkUpdateDescriptorSets + VkDescriptorBufferInfo
     */
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

    /**
     * texture / sampler 绑定
     *
     * ⚠️ 关键点：
     * imageLayout 必须和实际 image layout 一致
     * 否则 shader 读取 undefined
     */
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
