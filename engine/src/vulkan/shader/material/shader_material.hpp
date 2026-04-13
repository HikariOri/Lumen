#pragma once

/*
 * @FileName       : shader_material.hpp
 * @Author         : yaojie
 * @Date           : 2026/4/12
 * 
 * @todo 动态 offsets
 */


#include "vulkan/shader/reflection/shader_reflection.hpp"
#include "vulkan/texture.hpp"

namespace vulkan::shader::material {

/// 材质封装：基于已 `create_layouts` + `create_pools` 的 `ShaderReflection`
/// 分配 descriptor sets 并提供批量更新接口。
class ShaderMaterial {
public:
    /// 单次写入项：buffer / image 字段按 `type` 选用，其余可留默认。
    struct Write {
        std::uint32_t set {};
        std::uint32_t binding {};
        VkDescriptorType type { VK_DESCRIPTOR_TYPE_MAX_ENUM };

        VkBuffer buffer { VK_NULL_HANDLE };
        VkDeviceSize offset {};
        VkDeviceSize range {};

        VkImageView image_view { VK_NULL_HANDLE };
        VkSampler sampler { VK_NULL_HANDLE };
        VkImageLayout imageLayout { VK_IMAGE_LAYOUT_UNDEFINED };
    };

    /// 与 `update_uniform` / `update_dynamic_uniform` / `update_storage` 对应的
    /// 批量条目（字段一致，便于一次填多条）。
    struct UniformItem {
        std::uint32_t set {};
        std::uint32_t binding {};
        VkBuffer buffer { VK_NULL_HANDLE };
        VkDeviceSize offset {};
        VkDeviceSize size {};
    };

    /// 与 `update_buffer` 对应的批量条目（每条可不同 `VkDescriptorType`）。
    struct BufferItem {
        std::uint32_t set {};
        std::uint32_t binding {};
        VkDescriptorType descriptor_type { VK_DESCRIPTOR_TYPE_MAX_ENUM };
        VkBuffer buffer { VK_NULL_HANDLE };
        VkDeviceSize offset {};
        VkDeviceSize range {};
    };

    /// 与 `update_image` / `update_combined` 对应的批量条目。
    struct ImageItem {
        std::uint32_t set {};
        std::uint32_t binding {};
        VkImageView view { VK_NULL_HANDLE };
        VkSampler sampler { VK_NULL_HANDLE };
        VkImageLayout layout { VK_IMAGE_LAYOUT_UNDEFINED };
    };

    ShaderMaterial(VkDevice device,
                   const reflection::ShaderReflection &reflection);

    ShaderMaterial(const ShaderMaterial &) = delete;
    ShaderMaterial &operator=(const ShaderMaterial &) = delete;
    ShaderMaterial(ShaderMaterial &&) = delete;
    ShaderMaterial &operator=(ShaderMaterial &&) = delete;

    ~ShaderMaterial();

    [[nodiscard]] VkPipelineLayout pipeline_layout() const noexcept {
        return pipeline_layout_;
    }

    [[nodiscard]] VkDescriptorSet descriptor_set(std::uint32_t setIndex) const;

    /// 与 `descriptor_set` 相同；保留简短命名。
    [[nodiscard]] VkDescriptorSet set(std::uint32_t setIndex) const {
        return descriptor_set(setIndex);
    }

    [[nodiscard]] std::vector<VkDescriptorSet> get_all_sets() const;

    /// 万能批量更新：buffer / image / sampler 等按 `type` 写入同一批
    /// `vkUpdateDescriptorSets`。
    void update(const std::vector<Write> &writes);

    void update_uniform(std::uint32_t set, std::uint32_t binding,
                        VkBuffer buffer, VkDeviceSize offset,
                        VkDeviceSize size);

    void update_dynamic_uniform(std::uint32_t set, std::uint32_t binding,
                                VkBuffer buffer, VkDeviceSize offset,
                                VkDeviceSize size);

    void update_buffer(std::uint32_t set, std::uint32_t binding,
                       VkDescriptorType type, VkBuffer buffer,
                       VkDeviceSize offset, VkDeviceSize range);

    void update_image(std::uint32_t set, std::uint32_t binding,
                      VkImageView view, VkSampler sampler,
                      VkImageLayout layout);

    void update_storage(std::uint32_t set, std::uint32_t binding,
                        VkBuffer buffer, VkDeviceSize offset,
                        VkDeviceSize size);

    void update_combined(std::uint32_t set, std::uint32_t binding,
                         VkImageView view, VkSampler sampler,
                         VkImageLayout layout);

    void update_uniforms(const std::vector<UniformItem> &items);

    void update_dynamic_uniforms(const std::vector<UniformItem> &items);

    void update_buffers(const std::vector<BufferItem> &items);

    void update_images(const std::vector<ImageItem> &items);
    /// 批量写入 input attachment（descriptor type 固定为 INPUT_ATTACHMENT）。
    void update_input_attachments(const std::vector<ImageItem> &items);

    void update_storages(const std::vector<UniformItem> &items);

    void update_combined_images(const std::vector<ImageItem> &items);

    /// 绑定单个 descriptor set（`first_set` = pipeline 中的 set
    /// 序号），并传入该次 bind 所需的 **全部** dynamic offset（须与 layout 中
    /// dynamic 描述符数量一致）。
    void bind_dynamic(VkCommandBuffer cmd, std::uint32_t firstSet,
                      const std::vector<std::uint32_t> &dynamicOffsets) const;

    /// 一次绑定多个 descriptor set：`set_indices[i]` 从本 Material
    /// 取句柄，绑定到 管线槽位 `first_set + i`。`dynamic_offsets`
    /// 为本次调用中**所有**被绑定 set 内 dynamic 描述符的偏移（顺序与 Vulkan
    /// 规范一致）。
    void bind_descriptor_sets(
        VkCommandBuffer cmd, std::uint32_t firstSet,
        const std::vector<std::uint32_t> &sets,
        const std::vector<std::uint32_t> &dynamicOffsets) const;

    void bind_descriptor_sets(
        VkCommandBuffer cmd, std::uint32_t firstSet,
        const std::vector<VkDescriptorSet> &sets,
        const std::vector<std::uint32_t> &dynamicOffsets) const;

    // 在你的 Material 类里
    void set_texture(uint32_t set, uint32_t binding, Texture2D* tex);

private:
    VkDevice device_ { VK_NULL_HANDLE };
    VkPipelineLayout pipeline_layout_ { VK_NULL_HANDLE };
    std::unordered_map<std::uint32_t, VkDescriptorSet> sets_;
    std::unordered_map<std::uint32_t, VkDescriptorPool> pools_;
};

} // namespace vulkan::shader::material
