#include "vulkan/shader/material/shader_material.hpp"

#include "core/log/logger.hpp"

#include <algorithm>

namespace vulkan::shader::material {

namespace {

[[nodiscard]] bool is_buffer_descriptor_type(VkDescriptorType t) noexcept {
    switch (t) {
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: return true;
    default: return false;
    }
}

[[nodiscard]] bool is_image_descriptor_type(VkDescriptorType t) noexcept {
    switch (t) {
    case VK_DESCRIPTOR_TYPE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: return true;
    default: return false;
    }
}

} // namespace

ShaderMaterial::ShaderMaterial(VkDevice device,
                               const reflection::ShaderReflection &reflection)
    : device_(device), pipeline_layout_(reflection.pipeline_layout()) {
    pools_ = reflection.create_pools(device, 16);
    sets_ = reflection.allocateSets(device, pools_);
}

ShaderMaterial::~ShaderMaterial() {
    for (auto &[_, pool] : pools_) {
        vkDestroyDescriptorPool(device_, pool, nullptr);
    }
}

VkDescriptorSet ShaderMaterial::descriptor_set(std::uint32_t setIndex) const {
    const auto it = sets_.find(setIndex);
    return it != sets_.end() ? it->second : VK_NULL_HANDLE;
}

std::vector<VkDescriptorSet> ShaderMaterial::get_all_sets() const {
    std::vector<std::uint32_t> sorted_sets;
    sorted_sets.reserve(sets_.size());
    for (const auto &kv : sets_) {
        sorted_sets.push_back(kv.first);
    }
    std::sort(sorted_sets.begin(), sorted_sets.end());

    std::vector<VkDescriptorSet> out;
    out.reserve(sorted_sets.size());
    for (const std::uint32_t s : sorted_sets) {
        out.push_back(sets_.at(s));
    }
    return out;
}

void ShaderMaterial::update(const std::vector<Write> &writes) {
    std::vector<VkWriteDescriptorSet> vk_writes;
    std::vector<VkDescriptorBufferInfo> buffers;
    std::vector<VkDescriptorImageInfo> images;

    vk_writes.reserve(writes.size());
    buffers.reserve(writes.size());
    images.reserve(writes.size());

    for (const auto &w : writes) {
        const VkDescriptorSet ds = descriptor_set(w.set);
        if (ds == VK_NULL_HANDLE) {
            continue;
        }

        VkWriteDescriptorSet vk_w {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ds,
            .dstBinding = w.binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = w.type,
        };

        if (is_buffer_descriptor_type(w.type)) {
            buffers.push_back(VkDescriptorBufferInfo {
                .buffer = w.buffer,
                .offset = w.offset,
                .range = w.range,
            });
            vk_w.pBufferInfo = &buffers.back();
        } else if (is_image_descriptor_type(w.type)) {
            images.push_back(VkDescriptorImageInfo {
                .sampler = w.sampler,
                .imageView = w.image_view,
                .imageLayout = w.imageLayout,
            });
            vk_w.pImageInfo = &images.back();
        } else {
            LUMEN_LOG_ERROR(
                "ShaderMaterial::update: unsupported VkDescriptorType for "
                "batch write");
            continue;
        }

        vk_writes.push_back(vk_w);
    }

    if (!vk_writes.empty()) {
        vkUpdateDescriptorSets(device_,
                               static_cast<std::uint32_t>(vk_writes.size()),
                               vk_writes.data(), 0, nullptr);
    }
}

void ShaderMaterial::update_uniform(std::uint32_t set,
                                    std::uint32_t binding, VkBuffer buffer,
                                    VkDeviceSize offset, VkDeviceSize size) {
    update({ Write {
        .set = set,
        .binding = binding,
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .buffer = buffer,
        .offset = offset,
        .range = size,
    } });
}

void ShaderMaterial::update_dynamic_uniform(std::uint32_t set,
                                            std::uint32_t binding,
                                            VkBuffer buffer,
                                            VkDeviceSize offset,
                                            VkDeviceSize size) {
    update({ Write {
        .set = set,
        .binding = binding,
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .buffer = buffer,
        .offset = offset,
        .range = size,
    } });
}

void ShaderMaterial::update_buffer(std::uint32_t set,
                                   std::uint32_t binding,
                                   VkDescriptorType type,
                                   VkBuffer buffer, VkDeviceSize offset,
                                   VkDeviceSize range) {
    update({ Write {
        .set = set,
        .binding = binding,
        .type = type,
        .buffer = buffer,
        .offset = offset,
        .range = range,
    } });
}

void ShaderMaterial::update_image(std::uint32_t set,
                                  std::uint32_t binding, VkImageView view,
                                  VkSampler sampler, VkImageLayout layout) {
    update({ Write {
        .set = set,
        .binding = binding,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .image_view = view,
        .sampler = sampler,
        .imageLayout = layout,
    } });
}

void ShaderMaterial::update_storage(std::uint32_t set,
                                    std::uint32_t binding, VkBuffer buffer,
                                    VkDeviceSize offset, VkDeviceSize size) {
    update({ Write {
        .set = set,
        .binding = binding,
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = buffer,
        .offset = offset,
        .range = size,
    } });
}

void ShaderMaterial::update_combined(std::uint32_t set,
                                     std::uint32_t binding, VkImageView view,
                                     VkSampler sampler, VkImageLayout layout) {
    update({ Write {
        .set = set,
        .binding = binding,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .image_view = view,
        .sampler = sampler,
        .imageLayout = layout,
    } });
}

void ShaderMaterial::update_uniforms(const std::vector<UniformItem> &items) {
    std::vector<Write> writes;
    writes.reserve(items.size());
    for (const auto &u : items) {
        writes.push_back(Write {
            .set = u.set,
            .binding = u.binding,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .buffer = u.buffer,
            .offset = u.offset,
            .range = u.size,
        });
    }
    update(writes);
}

void ShaderMaterial::update_dynamic_uniforms(
    const std::vector<UniformItem> &items) {
    std::vector<Write> writes;
    writes.reserve(items.size());
    for (const auto &u : items) {
        writes.push_back(Write {
            .set = u.set,
            .binding = u.binding,
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .buffer = u.buffer,
            .offset = u.offset,
            .range = u.size,
        });
    }
    update(writes);
}

void ShaderMaterial::update_buffers(const std::vector<BufferItem> &items) {
    std::vector<Write> writes;
    writes.reserve(items.size());
    for (const auto &b : items) {
        writes.push_back(Write {
            .set = b.set,
            .binding = b.binding,
            .type = b.descriptor_type,
            .buffer = b.buffer,
            .offset = b.offset,
            .range = b.range,
        });
    }
    update(writes);
}

void ShaderMaterial::update_images(const std::vector<ImageItem> &items) {
    std::vector<Write> writes;
    writes.reserve(items.size());
    for (const auto &i : items) {
        writes.push_back(Write {
            .set = i.set,
            .binding = i.binding,
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .image_view = i.view,
            .sampler = i.sampler,
            .imageLayout = i.layout,
        });
    }
    update(writes);
}

void ShaderMaterial::update_storages(const std::vector<UniformItem> &items) {
    std::vector<Write> writes;
    writes.reserve(items.size());
    for (const auto &u : items) {
        writes.push_back(Write {
            .set = u.set,
            .binding = u.binding,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .buffer = u.buffer,
            .offset = u.offset,
            .range = u.size,
        });
    }
    update(writes);
}

void ShaderMaterial::update_combined_images(
    const std::vector<ImageItem> &items) {
    update_images(items);
}

void ShaderMaterial::bind_dynamic(
    VkCommandBuffer cmd, const std::uint32_t firstSet,
    const std::vector<std::uint32_t> &dynamicOffsets) const {
    const VkDescriptorSet ds = descriptor_set(firstSet);
    if (ds == VK_NULL_HANDLE) {
        return;
    }
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline_layout_, firstSet, 1, &ds,
                            static_cast<std::uint32_t>(dynamicOffsets.size()),
                            dynamicOffsets.data());
}

void ShaderMaterial::bind_descriptor_sets(
    VkCommandBuffer cmd, const std::uint32_t firstSet,
    const std::vector<std::uint32_t> &sets,
    const std::vector<std::uint32_t> &dynamicOffsets) const {
    if (sets.empty()) {
        return;
    }

    std::vector<VkDescriptorSet> handles;
    handles.reserve(sets.size());
    for (const std::uint32_t s : sets) {
        const VkDescriptorSet ds = descriptor_set(s);
        if (ds == VK_NULL_HANDLE) {
            LUMEN_LOG_ERROR(
                "ShaderMaterial::bind_descriptor_sets: null descriptor set");
            return;
        }
        handles.push_back(ds);
    }

    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, firstSet,
        static_cast<std::uint32_t>(handles.size()), handles.data(),
        static_cast<std::uint32_t>(dynamicOffsets.size()),
        dynamicOffsets.data());
}

void ShaderMaterial::bind_descriptor_sets(
    VkCommandBuffer cmd, const std::uint32_t firstSet,
    const std::vector<VkDescriptorSet> &sets,
    const std::vector<std::uint32_t> &dynamicOffsets) const {
    if (sets.empty()) {
        return;
    }
    for (const VkDescriptorSet ds : sets) {
        if (ds == VK_NULL_HANDLE) {
            LUMEN_LOG_ERROR(
                "ShaderMaterial::bind_descriptor_sets: null VkDescriptorSet in "
                "list");
            return;
        }
    }
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, firstSet,
        static_cast<std::uint32_t>(sets.size()), sets.data(),
        static_cast<std::uint32_t>(dynamicOffsets.size()),
        dynamicOffsets.data());
}

} // namespace vulkan::shader::material
