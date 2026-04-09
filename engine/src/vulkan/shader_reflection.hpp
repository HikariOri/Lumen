/**
 * @file shader_reflection.hpp
 * @brief SPIR-V（SPIRV-Reflect）→ 中间层 `ShaderReflection` → 合并描述符与 push
 *        constant → `VkDescriptorSetLayout` / `VkPipelineLayout`
 * 的最小可用内核。
 *
 * @note `vulkan::Shader` 在 `create` / `load_spv` 时会调用 `reflect_spirv` 并缓存
 *       `ShaderReflection`；多阶段合并请使用 `ShaderProgram`。
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

namespace vulkan {

/**
 * @brief 单个 descriptor binding 的引擎侧中间表示（跨 stage 合并前为单
 * stage）。
 */
struct DescriptorBindingInfo {
    std::uint32_t set { 0 };
    std::uint32_t binding { 0 };
    VkDescriptorType type { VK_DESCRIPTOR_TYPE_MAX_ENUM };
    std::uint32_t count { 0 };
    VkShaderStageFlags stageFlags { 0 };
    std::string name;
};

/**
 * @brief Push constant 区间（与 `VkPushConstantRange` 对应）。
 */
struct PushConstantInfo {
    std::uint32_t offset { 0 };
    std::uint32_t size { 0 };
    VkShaderStageFlags stageFlags { 0 };
};

/**
 * @brief 顶点 stage 的用户输入（不含 `gl_` 内置）。
 */
struct VertexInputInfo {
    std::uint32_t location { 0 };
    VkFormat format { VK_FORMAT_UNDEFINED };
    std::string name;
};

/**
 * @brief 单份着色器 SPIR-V 的反射结果。
 */
struct ShaderReflection {
    std::vector<DescriptorBindingInfo> bindings;
    std::vector<PushConstantInfo> pushConstants;
    std::vector<VertexInputInfo> vertexInputs;
};

/**
 * @brief `build_vertex_input_state` 的输出（绑定、属性与紧密打包后的 stride）。
 */
struct VertexInputState {
    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    std::uint32_t stride { 0 };
};

/**
 * @brief 描述符合并键 `(set, binding)`。
 */
struct BindingKey {
    std::uint32_t set { 0 };
    std::uint32_t binding { 0 };

    bool operator==(const BindingKey &other) const noexcept {
        return set == other.set && binding == other.binding;
    }
};

struct BindingKeyHash {
    std::size_t operator()(BindingKey key) const noexcept {
        const std::uint64_t packed =
            (static_cast<std::uint64_t>(key.set) << 32U) |
            static_cast<std::uint64_t>(key.binding);
        return std::hash<std::uint64_t> {}(packed);
    }
};

/**
 * @brief 将 SPIR-V 字码反射为 `ShaderReflection`。
 */
[[nodiscard]] std::expected<ShaderReflection, std::string>
reflect_spirv(const std::vector<std::uint32_t> &spirvWords,
              VkShaderStageFlagBits stage);

/**
 * @brief 由顶点着色器反射结果生成顶点输入绑定 / 属性（紧密打包、`offset`
 * 按分量对齐）。
 */
[[nodiscard]] std::expected<VertexInputState, std::string>
build_vertex_input_state(const ShaderReflection &vertexReflection,
                         std::uint32_t vertexBindingSlot);

/**
 * @brief 多份 `ShaderReflection` 的合并器：按 `(set,binding)` 合并
 *        `stageFlags`、取 `count` 最大值，并要求 `type` 一致。
 */
class PipelineLayoutBuilder {
public:
    PipelineLayoutBuilder() = default;

    void clear() noexcept;

    /**
     * @brief 并入一份反射结果。
     * @return 失败时携带原因（如描述符类型不一致、push 区间重叠）。
     */
    [[nodiscard]] std::expected<void, std::string>
    add(const ShaderReflection &reflection);

    [[nodiscard]] const std::unordered_map<BindingKey, DescriptorBindingInfo,
                                           BindingKeyHash> &
    merged_bindings() const noexcept {
        return mergedBindings_;
    }

    [[nodiscard]] const std::vector<PushConstantInfo> &
    merged_push_constants() const noexcept {
        return mergedPushConstants_;
    }

    /**
     * @brief 按 set 下标连续创建 `VkDescriptorSetLayout`（空缺 set 为空
     * layout）。
     */
    [[nodiscard]] std::expected<std::vector<VkDescriptorSetLayout>, std::string>
    create_descriptor_set_layouts(VkDevice device) const;

    /**
     * @brief 使用已创建的 set layouts 与内部合并后的 push constant 区间创建
     *        `VkPipelineLayout`。
     */
    [[nodiscard]] std::expected<VkPipelineLayout, std::string>
    create_pipeline_layout(
        VkDevice device,
        const std::vector<VkDescriptorSetLayout> &setLayouts) const;

    static void destroy_descriptor_set_layouts(
        VkDevice device, const std::vector<VkDescriptorSetLayout> &layouts);

private:
    std::unordered_map<BindingKey, DescriptorBindingInfo, BindingKeyHash>
        mergedBindings_;
    std::vector<PushConstantInfo> mergedPushConstants_;
};

/**
 * @brief 按合并后的 binding 表估算 descriptor pool（假设每种 layout 各分配
 *        `maxSets` 份相同布局的 descriptor set，且每个 set 使用全部
 * binding）。`merged` 为空时成功返回 `VK_NULL_HANDLE`。
 */
[[nodiscard]] std::expected<VkDescriptorPool, std::string>
create_descriptor_pool_for_merged_bindings(
    VkDevice device,
    const std::unordered_map<BindingKey, DescriptorBindingInfo, BindingKeyHash>
        &merged,
    std::uint32_t maxSets);

/**
 * @brief 填充 `VkWriteDescriptorSet` 的公共字段（`pBufferInfo` /
 *        `pImageInfo` 等由调用方随后设置）。
 */
void init_write_descriptor_set(VkWriteDescriptorSet &write,
                               VkDescriptorSet dstSet,
                               const DescriptorBindingInfo &binding);

} // namespace vulkan
