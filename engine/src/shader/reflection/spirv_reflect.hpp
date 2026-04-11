/**
 * @file shader/reflection/spirv_reflect.hpp
 * @brief 着色器反射数据与 Vulkan 布局（描述符 / 管线 / 顶点输入）的对外接口。
 * @details 单阶段用 `ShaderReflection::from_spirv`，多阶段合并用 `MergedShaderReflection::merge(..., VkDevice)`；
 *          `merge(..., VkDevice, allocator)` 绑定后续 Vulkan 调用使用的设备；`create_descriptor_set_layouts`、
 *          `create_pipeline_layout` 使用该设备创建并持有各 set 的 `VkDescriptorSetLayout` 及管线布局所需的
 *          layout 序列；JSON 等路径可在反序列化后调用 `set_vulkan_device` 再创建 Vulkan 对象。
 *          `packed_vertex_input_state` 打包顶点输入；描述符绑定表在 `merge` 成功时预计算并缓存，
 *          若经 JSON 反序列化构造则于首次创建布局时惰性填充。
 *          可失败接口返回 `std::expected<T, ReflectionError>`，不抛出 C++ 标准库异常。
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include <spirv-reflect/spirv_reflect.h>
#include <vulkan/vulkan.h>

namespace shader::reflection {

#if defined(VK_SHADER_STAGE_TASK_BIT)
constexpr VkShaderStageFlags k_task_shader_stage_bit = VK_SHADER_STAGE_TASK_BIT;
#elif defined(VK_SHADER_STAGE_TASK_BIT_EXT)
constexpr VkShaderStageFlags k_task_shader_stage_bit =
    VK_SHADER_STAGE_TASK_BIT_EXT;
#elif defined(VK_SHADER_STAGE_TASK_BIT_NV)
constexpr VkShaderStageFlags k_task_shader_stage_bit =
    VK_SHADER_STAGE_TASK_BIT_NV;
#else
constexpr VkShaderStageFlags k_task_shader_stage_bit = VkShaderStageFlags { 0 };
#endif

#if defined(VK_SHADER_STAGE_MESH_BIT)
constexpr VkShaderStageFlags k_mesh_shader_stage_bit = VK_SHADER_STAGE_MESH_BIT;
#elif defined(VK_SHADER_STAGE_MESH_BIT_EXT)
constexpr VkShaderStageFlags k_mesh_shader_stage_bit =
    VK_SHADER_STAGE_MESH_BIT_EXT;
#elif defined(VK_SHADER_STAGE_MESH_BIT_NV)
constexpr VkShaderStageFlags k_mesh_shader_stage_bit =
    VK_SHADER_STAGE_MESH_BIT_NV;
#else
constexpr VkShaderStageFlags k_mesh_shader_stage_bit = VkShaderStageFlags { 0 };
#endif

/**
 * @brief 着色器资源在反射中的逻辑类型。
 */
enum class ResourceType {
    UniformBuffer,
    StorageBuffer,
    SampledImage,
    StorageImage,
    Sampler,
    CombinedImageSampler,
    UniformTexelBuffer,
    StorageTexelBuffer,
    UniformBufferDynamic,
    StorageBufferDynamic,
    InputAttachment,
    AccelerationStructureKHR,
    Unknown
};

/**
 * @brief 单个描述符绑定在合并前的视图。
 */
struct ShaderResource {
    uint32_t set {};
    uint32_t binding {};
    ResourceType type { ResourceType::Unknown };

    uint32_t count {};
    uint32_t size {};
    uint32_t offset {};

    std::string name;

    VkShaderStageFlags stages {};
};

/**
 * @brief 顶点着色器输入（location / format / 名称）。
 */
struct ShaderInput {
    uint32_t location {};
    VkFormat format { VK_FORMAT_UNDEFINED };
    std::string name;
};

/**
 * @brief 片段着色器输出（location / format / 名称）。
 */
struct ShaderOutput {
    uint32_t location {};
    VkFormat format { VK_FORMAT_UNDEFINED };
    std::string name;
};

/**
 * @brief 顶点属性：location / format 来自反射；binding 约定 0 = per-vertex、1 = per-instance。
 */
struct VertexAttribute {
    uint32_t location {};
    VkFormat format { VK_FORMAT_UNDEFINED };
    /** @brief Vulkan vertex buffer binding；约定 0 = per-vertex，1 = per-instance。 */
    uint32_t binding {};
};

/**
 * @brief 顶点缓冲绑定（实现细节类型；`MergedShaderReflection::packed_vertex_input_state` 会用到）。
 */
struct VertexBinding {
    uint32_t binding {};
    uint32_t stride {};
    VkVertexInputRate rate { VK_VERTEX_INPUT_RATE_VERTEX };
};

/**
 * @brief Push constant 范围在反射中的描述。
 */
struct PushConstant {
    uint32_t size {};
    uint32_t offset {};
    VkShaderStageFlags stages {};
};

/**
 * @brief 打包后的顶点输入状态及 `VkPipelineVertexInputStateCreateInfo`。
 */
struct PackedVertexInputState {
    std::vector<VkVertexInputBindingDescription> binds;
    std::vector<VkVertexInputAttributeDescription> attributes;
    VkPipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo {};

    PackedVertexInputState();

    /**
     * @brief 在拷贝 / 移动后，将 `pipelineVertexInputStateCreateInfo` 内的指针重新指向本对象的
     *        `binds` 与 `attributes`。
     */
    void rebind_pipeline_vertex_input_state_create_info() noexcept;
};

/** @brief 反射 / 布局 API 错误说明（`std::expected` 的 error 类型）。 */
using ReflectionError = std::string;

/**
 * @brief 单阶段 SPIR-V 反射结果（由 `from_spirv` 构造）。
 */
class ShaderReflection {
    friend void from_json(const nlohmann::json &j, ShaderReflection &r);

public:
    [[nodiscard]] static ShaderReflection from_spirv(std::span<const std::byte> spirv,
                                                     VkShaderStageFlagBits stage);

    [[nodiscard]] static ShaderReflection from_spirv(const std::vector<std::byte> &spirv,
                                                     VkShaderStageFlagBits stage) {
        return from_spirv(std::span<const std::byte> { spirv.data(), spirv.size() },
                          stage);
    }

    [[nodiscard]] VkShaderStageFlagBits stage() const noexcept { return stage_; }

    [[nodiscard]] const std::vector<ShaderResource> &resources() const noexcept {
        return resources_;
    }

    [[nodiscard]] const std::vector<ShaderInput> &inputs() const noexcept {
        return inputs_;
    }

    [[nodiscard]] const std::vector<ShaderOutput> &outputs() const noexcept {
        return outputs_;
    }

    [[nodiscard]] const std::vector<PushConstant> &pushConstants() const noexcept {
        return pushConstants_;
    }

private:
    VkShaderStageFlagBits stage_ {};

    std::vector<ShaderResource> resources_;
    std::vector<ShaderInput> inputs_;
    std::vector<ShaderOutput> outputs_;

    std::vector<PushConstant> pushConstants_;
};

/**
 * @brief 多阶段着色器反射合并后的视图（由 `merge` 构造）。
 */
class MergedShaderReflection {
    friend void from_json(const nlohmann::json &j, MergedShaderReflection &m);

public:
    /**
     * @brief 合并多阶段反射；`device` / `allocator` 供后续 `create_descriptor_set_layouts` 等使用（可传
     *        `VK_NULL_HANDLE`，稍后用 `set_vulkan_device` 绑定）。
     */
    [[nodiscard]] static std::expected<MergedShaderReflection, ReflectionError>
    merge(const std::vector<ShaderReflection>& shaders, VkDevice device,
          const VkAllocationCallbacks *allocator = nullptr);

    /**
     * @brief 在未经过带 `VkDevice` 的 `merge` 时（例如 JSON `from_json` 之后）绑定 Vulkan 设备。
     * @details 若已有内部 `VkDescriptorSetLayout`，会先调用 `destroy` 再写入新设备。
     */
    void set_vulkan_device(VkDevice device,
                           const VkAllocationCallbacks *allocator = nullptr);

    /**
     * @brief 参与合并的着色器阶段位掩码（各 `ShaderReflection::stage()` 的按位或）。
     * @details 类型为 `VkShaderStageFlags`，语义上由各 `VkShaderStageFlagBits` 组合而成。
     */
    [[nodiscard]] VkShaderStageFlags stages() const noexcept { return stages_; }

    [[nodiscard]] const std::vector<ShaderResource> &resources() const noexcept {
        return resources_;
    }

    [[nodiscard]] const std::vector<ShaderInput> &vertexInput() const noexcept {
        return vertexInput_;
    }

    [[nodiscard]] const std::vector<ShaderOutput> &fragmentOutput() const noexcept {
        return fragmentOutput_;
    }

    [[nodiscard]] const std::vector<PushConstant> &pushConstants() const noexcept {
        return pushConstants_;
    }

    /**
     * @brief 为连续 set `0…S` 创建 `VkDescriptorSetLayout` 并保存在本对象内；成功时返回指向它们的
     *        `span`（`[i]` = `set=i`），指针指向本对象内部存储；调用 `destroy` 或再次调用本函数后失效。
     * @details 使用 `merge` / `set_vulkan_device` 绑定的 `VkDevice`；`allocator` 非空时覆盖绑定时的
     *          allocator。要求 `resources()` 中的 `set` **恰好**为 `{0,1,…,S}`；无描述符资源时内部清空
     *          并返回空 `span`。再次调用会先销毁此前已创建的 layout。
     */
    [[nodiscard]] std::expected<std::span<const VkDescriptorSetLayout>, ReflectionError>
    create_descriptor_set_layouts(
        const VkAllocationCallbacks *allocator = nullptr) const;

    /**
     * @brief 释放本对象持有的 Vulkan 资源（当前为各 `VkDescriptorSetLayout`）。
     * @note 须在对应 `VkDevice` 仍有效时调用；`from_json` 会在反序列化前调用。不修改 CPU 侧反射数据。
     */
    void destroy() const noexcept;

    /**
     * @brief 使用内部的 descriptor set layout 序列与合并结果中的 push constant 创建
     *        `VkPipelineLayout`。
     * @note 使用 `merge` / `set_vulkan_device` 绑定的设备；`allocator` 非空时用于 `vkCreatePipelineLayout`。
     *       若存在描述符资源而内部尚无 layout，或设备 / allocator 与已缓存 layout 不一致，则在本调用内
     *       先执行与 `create_descriptor_set_layouts` 等价的创建。`VkPushConstantRange` 预计算并复用；
     *       不缓存 `VkPipelineLayout` 句柄。
     */
    [[nodiscard]] std::expected<VkPipelineLayout, ReflectionError>
    create_pipeline_layout(
        const VkAllocationCallbacks *allocator = nullptr) const;

    /**
     * @brief 由合并结果中的顶点输入生成紧密打包的 `VkPipelineVertexInputStateCreateInfo`。
     * @details 顶点属性均在 binding 0；binding 1 预留为 per-instance，stride 由实现按格式推导。
     * @note 首次成功或失败后结果会缓存；经 JSON `from_json` 会重置缓存。
     */
    [[nodiscard]] std::expected<PackedVertexInputState, ReflectionError>
    packed_vertex_input_state() const;

private:
    VkShaderStageFlags stages_ {};

    VkDevice vkDevice_ { VK_NULL_HANDLE };
    const VkAllocationCallbacks *vkAllocator_ { nullptr };

    std::vector<ShaderResource> resources_;
    std::vector<ShaderInput> vertexInput_;
    std::vector<ShaderOutput> fragmentOutput_;

    std::vector<PushConstant> pushConstants_;

    [[nodiscard]] std::expected<std::vector<VkDescriptorSetLayoutBinding>, ReflectionError>
    descriptor_bindings_for_set(std::uint32_t set_index) const;

    [[nodiscard]] std::expected<VkDescriptorSetLayout, ReflectionError>
    create_descriptor_set_layout_for_set_internal(
        VkDevice device, std::uint32_t set_index,
        const VkAllocationCallbacks *allocator) const;

    void rebuild_push_constant_ranges_for_pipeline();

    [[nodiscard]] std::expected<void, ReflectionError>
    rebuild_descriptor_set_layouts_storage(
        VkDevice device, const VkAllocationCallbacks *allocator) const;

    mutable std::vector<VkDescriptorSetLayout> descriptorSetLayouts_;
    mutable VkDevice descriptorSetLayoutsDevice_ { VK_NULL_HANDLE };
    mutable const VkAllocationCallbacks *descriptorSetLayoutsAllocator_ { nullptr };

    mutable std::optional<std::expected<
        std::map<std::uint32_t, std::vector<VkDescriptorSetLayoutBinding>>,
        ReflectionError>>
        descriptorBindingsBySetCache_;

    std::vector<VkPushConstantRange> pushConstantRangesForPipeline_;

    mutable std::optional<std::expected<PackedVertexInputState, ReflectionError>>
        packedVertexInputStateCache_;
};

/**
 * @brief 将 VkShaderStageFlags 序列化为 JSON 等使用的阶段名字符串（供引擎内部与 JSON 模块使用）。
 * @note 非稳定 ABI；外部业务代码请优先使用本头文件中的反射与布局 API。
 */
namespace detail {
[[nodiscard]] std::string shader_stage_flags_to_string(VkShaderStageFlags v);
}

} // namespace shader::reflection
