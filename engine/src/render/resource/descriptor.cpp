/**
 * @file descriptor.cpp
 * @brief Descriptor 系统实现（Vulkan-Hpp）
 */

#include "render/resource/descriptor.hpp"
#include "core/log/logger.hpp"
#include "render/context.hpp"

#include <array>

namespace lumen {
namespace render {

bool DescriptorSetLayout::create(
    const Context &ctx, const std::vector<DescriptorBinding> &bindings) {

    device_ = ctx.device();

    std::vector<vk::DescriptorSetLayoutBinding> vkBindings(bindings.size());

    for (size_t i = 0; i < bindings.size(); ++i) {
        vkBindings[i].binding = bindings[i].binding;
        vkBindings[i].descriptorType = bindings[i].type;
        vkBindings[i].descriptorCount = bindings[i].count;
        vkBindings[i].stageFlags = bindings[i].stages;
    }

    vk::DescriptorSetLayoutCreateInfo createInfo {};
    createInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
    createInfo.pBindings = vkBindings.data();

    const vk::Result result =
        device_.createDescriptorSetLayout(&createInfo, nullptr, &layout_);

    if (result == vk::Result::eSuccess) {
        LUMEN_LOG_DEBUG("DescriptorSetLayout 创建成功 bindings={}",
                        bindings.size());
    }

    return result == vk::Result::eSuccess;
}

void DescriptorSetLayout::destroy_() {
    if (layout_) {
        device_.destroyDescriptorSetLayout(layout_, nullptr);
        layout_ = nullptr;
    }
}

DescriptorSetLayout::~DescriptorSetLayout() { destroy_(); }

DescriptorSetLayout::DescriptorSetLayout(DescriptorSetLayout &&other) noexcept
    : device_(other.device_), layout_(other.layout_) {

    other.device_ = nullptr;
    other.layout_ = nullptr;
}

DescriptorSetLayout &
DescriptorSetLayout::operator=(DescriptorSetLayout &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy_();

    device_ = other.device_;
    layout_ = other.layout_;

    other.device_ = nullptr;
    other.layout_ = nullptr;

    return *this;
}

bool DescriptorPool::create(const Context &ctx,
                            const std::vector<DescriptorPoolSize> &poolSizes,
                            uint32_t maxSets) {

    device_ = ctx.device();

    std::vector<vk::DescriptorPoolSize> vkSizes(poolSizes.size());

    for (size_t i = 0; i < poolSizes.size(); ++i) {
        vkSizes[i].type = poolSizes[i].type;
        vkSizes[i].descriptorCount = poolSizes[i].count;
    }

    vk::DescriptorPoolCreateInfo createInfo {};
    createInfo.poolSizeCount = static_cast<uint32_t>(vkSizes.size());
    createInfo.pPoolSizes = vkSizes.data();
    createInfo.maxSets = maxSets;

    const vk::Result result =
        device_.createDescriptorPool(&createInfo, nullptr, &pool_);

    if (result == vk::Result::eSuccess) {
        LUMEN_LOG_DEBUG("DescriptorPool 创建成功 maxSets={}", maxSets);
    }

    return result == vk::Result::eSuccess;
}

bool DescriptorPool::allocate(vk::Device device,
                              vk::DescriptorSetLayout layout,
                              vk::DescriptorSet &outSet) {

    vk::DescriptorSetAllocateInfo allocInfo {};
    allocInfo.descriptorPool = pool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    const vk::Result r =
        device.allocateDescriptorSets(&allocInfo, &outSet);
    return r == vk::Result::eSuccess;
}

void DescriptorPool::reset() {
    if (pool_) {
        device_.resetDescriptorPool(pool_, {});
    }
}

void DescriptorPool::destroy_() {
    if (pool_) {
        device_.destroyDescriptorPool(pool_, nullptr);
        pool_ = nullptr;
    }
}

DescriptorPool::~DescriptorPool() { destroy_(); }

DescriptorPool::DescriptorPool(DescriptorPool &&other) noexcept
    : device_(other.device_), pool_(other.pool_) {

    other.device_ = nullptr;
    other.pool_ = nullptr;
}

DescriptorPool &DescriptorPool::operator=(DescriptorPool &&other) noexcept {
    if (this == &other)
        return *this;

    destroy_();

    device_ = other.device_;
    pool_ = other.pool_;

    other.device_ = nullptr;
    other.pool_ = nullptr;

    return *this;
}

void write_descriptor_set(vk::Device device, vk::DescriptorSet set,
                          std::initializer_list<DescriptorWriteBuffer> buffers,
                          std::initializer_list<DescriptorWriteImage> images) {

    const size_t nb = buffers.size();
    const size_t ni = images.size();
    if (nb + ni == 0) {
        return;
    }

    std::vector<vk::DescriptorBufferInfo> bufferInfos(nb);
    {
        size_t i = 0;
        for (const DescriptorWriteBuffer &b : buffers) {
            bufferInfos[i].buffer = b.buffer;
            bufferInfos[i].offset = b.offset;
            bufferInfos[i].range = b.range;
            ++i;
        }
    }

    std::vector<vk::DescriptorImageInfo> imageInfos(ni);
    {
        size_t i = 0;
        for (const DescriptorWriteImage &im : images) {
            imageInfos[i].imageLayout = im.imageLayout;
            imageInfos[i].imageView = im.imageView;
            imageInfos[i].sampler = im.sampler;
            ++i;
        }
    }

    std::vector<vk::WriteDescriptorSet> writes;
    writes.reserve(nb + ni);

    {
        size_t i = 0;
        for (const DescriptorWriteBuffer &b : buffers) {
            vk::WriteDescriptorSet write {};
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
            vk::WriteDescriptorSet write {};
            write.dstSet = set;
            write.dstBinding = im.binding;
            write.dstArrayElement = 0;
            write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfos[i];
            writes.push_back(write);
            ++i;
        }
    }

    static const std::vector<vk::CopyDescriptorSet> kNoCopies;
    device.updateDescriptorSets(writes, kNoCopies);
}

void write_descriptor_buffer(vk::Device device, vk::DescriptorSet set,
                             uint32_t binding, vk::DescriptorType type,
                             vk::Buffer buffer, size_t offset, size_t range) {

    vk::DescriptorBufferInfo bufferInfo {};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = offset;
    bufferInfo.range = range;

    vk::WriteDescriptorSet write {};
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = type;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    static const std::vector<vk::CopyDescriptorSet> kNoCopies;
    const std::array<vk::WriteDescriptorSet, 1> wa { write };
    device.updateDescriptorSets(wa, kNoCopies);
}

void write_descriptor_image(vk::Device device, vk::DescriptorSet set,
                            uint32_t binding, vk::ImageView imageView,
                            vk::Sampler sampler, vk::ImageLayout imageLayout) {

    vk::DescriptorImageInfo imageInfo {};
    imageInfo.imageLayout = imageLayout;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;

    vk::WriteDescriptorSet write {};
    write.dstSet = set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    static const std::vector<vk::CopyDescriptorSet> kNoCopies;
    const std::array<vk::WriteDescriptorSet, 1> wa { write };
    device.updateDescriptorSets(wa, kNoCopies);
}

} // namespace render
} // namespace lumen
