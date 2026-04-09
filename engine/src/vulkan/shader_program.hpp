/**
 * @file shader_program.hpp
 * @brief 多阶段 `ShaderProgram`；顶点输入与设备侧管线/描述符布局一站式查询。
 */

#pragma once

#include "vulkan/descriptor_pool.hpp"
#include "vulkan/shader.hpp"
#include "vulkan/shader_reflection.hpp"

#include <expected>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace vulkan {

class ShaderProgram;

/**
 * @brief `ShaderProgram` 在指定 `VkDevice` 上创建的描述符 set layout + pipeline layout（RAII）。
 */
class ShaderProgramLayoutResources final {
public:
    ShaderProgramLayoutResources() = default;
    ~ShaderProgramLayoutResources();

    ShaderProgramLayoutResources(const ShaderProgramLayoutResources &) = delete;
    ShaderProgramLayoutResources &
    operator=(const ShaderProgramLayoutResources &) = delete;
    ShaderProgramLayoutResources(ShaderProgramLayoutResources &&other) noexcept;
    ShaderProgramLayoutResources &
    operator=(ShaderProgramLayoutResources &&other) noexcept;

    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkPipelineLayout pipeline_layout() const noexcept {
        return pipeline_layout_;
    }
    [[nodiscard]] const std::vector<VkDescriptorSetLayout> &
    descriptor_set_layouts() const noexcept {
        return set_layouts_;
    }
    [[nodiscard]] bool is_valid() const noexcept {
        return pipeline_layout_ != VK_NULL_HANDLE;
    }

private:
    friend class ShaderProgram;

    ShaderProgramLayoutResources(
        VkDevice device, std::vector<VkDescriptorSetLayout> &&set_layouts,
        VkPipelineLayout pipeline_layout) noexcept;

    void destroy() noexcept;

    VkDevice device_ { VK_NULL_HANDLE };
    std::vector<VkDescriptorSetLayout> set_layouts_;
    VkPipelineLayout pipeline_layout_ { VK_NULL_HANDLE };
};

/**
 * @brief 着色器程序：持有若干 `Shader`（各带反射），并按 `stages_` 顺序合并布局描述。
 *
 * @note 常用：`vertex_input_state`、`create_layout_resources`、`merged_push_constants`；
 *       需自定义合并逻辑时再使用 `merged_layout()`。
 */
class ShaderProgram final {
public:
    ShaderProgram() = default;
    ~ShaderProgram() = default;

    ShaderProgram(const ShaderProgram &) = delete;
    ShaderProgram &operator=(const ShaderProgram &) = delete;
    ShaderProgram(ShaderProgram &&) = default;
    ShaderProgram &operator=(ShaderProgram &&) = default;

    [[nodiscard]] static std::expected<ShaderProgram, std::string>
    from_stages(std::vector<Shader> stages);

    [[nodiscard]] static std::expected<ShaderProgram, std::string>
    create_graphics(Shader vertex, Shader fragment);

    [[nodiscard]] bool is_valid() const noexcept { return !stages_.empty(); }

    [[nodiscard]] const std::vector<Shader> &stages() const noexcept {
        return stages_;
    }

    [[nodiscard]] const Shader *vertex_shader() const noexcept;
    [[nodiscard]] const Shader *fragment_shader() const noexcept;

    /**
     * @brief 顶点着色器反射 → `build_vertex_input_state`（无顶点 stage 则失败）。
     */
    [[nodiscard]] std::expected<VertexInputState, std::string>
    vertex_input_state(std::uint32_t vertex_binding_slot = 0) const;

    /**
     * @brief 在设备上创建合并后的描述符 set layout 与 `VkPipelineLayout`。
     */
    [[nodiscard]] std::expected<ShaderProgramLayoutResources, std::string>
    create_layout_resources(VkDevice device) const;

    /**
     * @brief 按合并后的描述符表创建池（`max_sets` 份完整 layout）；无 binding 时失败。
     */
    [[nodiscard]] std::expected<DescriptorPool, std::string>
    create_descriptor_pool(VkDevice device, std::uint32_t max_sets) const;

    [[nodiscard]] const std::vector<PushConstantInfo> &
    merged_push_constants() const noexcept {
        return merged_layout_.merged_push_constants();
    }

    [[nodiscard]] const PipelineLayoutBuilder &merged_layout() const noexcept {
        return merged_layout_;
    }

private:
    ShaderProgram(std::vector<Shader> &&stages,
                  PipelineLayoutBuilder &&merged) noexcept;

    std::vector<Shader> stages_;
    PipelineLayoutBuilder merged_layout_;
};

} // namespace vulkan
