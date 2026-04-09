/**
 * @file shader_program.cpp
 * @brief `ShaderProgram` / `ShaderProgramLayoutResources` 实现。
 */

#include "vulkan/shader_program.hpp"

namespace vulkan {

ShaderProgramLayoutResources::ShaderProgramLayoutResources(
    const VkDevice device, std::vector<VkDescriptorSetLayout> &&set_layouts,
    const VkPipelineLayout pipeline_layout) noexcept
    : device_(device), set_layouts_(std::move(set_layouts)),
      pipeline_layout_(pipeline_layout) {}

void ShaderProgramLayoutResources::destroy() noexcept {
    if (device_ != VK_NULL_HANDLE && pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    }
    pipeline_layout_ = VK_NULL_HANDLE;
    if (device_ != VK_NULL_HANDLE) {
        for (VkDescriptorSetLayout layout : set_layouts_) {
            if (layout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(device_, layout, nullptr);
            }
        }
    }
    set_layouts_.clear();
    device_ = VK_NULL_HANDLE;
}

ShaderProgramLayoutResources::~ShaderProgramLayoutResources() {
    destroy();
}

ShaderProgramLayoutResources::ShaderProgramLayoutResources(
    ShaderProgramLayoutResources &&other) noexcept
    : device_(other.device_), set_layouts_(std::move(other.set_layouts_)),
      pipeline_layout_(other.pipeline_layout_) {
    other.device_ = VK_NULL_HANDLE;
    other.pipeline_layout_ = VK_NULL_HANDLE;
}

ShaderProgramLayoutResources &ShaderProgramLayoutResources::operator=(
    ShaderProgramLayoutResources &&other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        set_layouts_ = std::move(other.set_layouts_);
        pipeline_layout_ = other.pipeline_layout_;
        other.device_ = VK_NULL_HANDLE;
        other.pipeline_layout_ = VK_NULL_HANDLE;
    }
    return *this;
}

ShaderProgram::ShaderProgram(std::vector<Shader> &&stages,
                             PipelineLayoutBuilder &&merged) noexcept
    : stages_(std::move(stages)), merged_layout_(std::move(merged)) {}

std::expected<ShaderProgram, std::string>
ShaderProgram::from_stages(std::vector<Shader> stages) {
    if (stages.empty()) {
        return std::unexpected(
            std::string("ShaderProgram::from_stages: empty stages"));
    }
    PipelineLayoutBuilder plb {};
    for (const Shader &s : stages) {
        if (!s.is_valid()) {
            return std::unexpected(
                std::string("ShaderProgram::from_stages: invalid shader"));
        }
        if (auto e = plb.add(s.reflection()); !e.has_value()) {
            return std::unexpected(std::move(e.error()));
        }
    }
    return ShaderProgram(std::move(stages), std::move(plb));
}

std::expected<ShaderProgram, std::string>
ShaderProgram::create_graphics(Shader vertex, Shader fragment) {
    std::vector<Shader> stages {};
    stages.reserve(2);
    stages.push_back(std::move(vertex));
    stages.push_back(std::move(fragment));
    return from_stages(std::move(stages));
}

const Shader *ShaderProgram::vertex_shader() const noexcept {
    for (const Shader &s : stages_) {
        if (s.stage() == VK_SHADER_STAGE_VERTEX_BIT) {
            return &s;
        }
    }
    return nullptr;
}

const Shader *ShaderProgram::fragment_shader() const noexcept {
    for (const Shader &s : stages_) {
        if (s.stage() == VK_SHADER_STAGE_FRAGMENT_BIT) {
            return &s;
        }
    }
    return nullptr;
}

std::expected<VertexInputState, std::string>
ShaderProgram::vertex_input_state(
    const std::uint32_t vertex_binding_slot) const {
    const Shader *const v = vertex_shader();
    if (v == nullptr) {
        return std::unexpected(std::string(
            "ShaderProgram::vertex_input_state: no vertex shader"));
    }
    return build_vertex_input_state(v->reflection(), vertex_binding_slot);
}

std::expected<ShaderProgramLayoutResources, std::string>
ShaderProgram::create_layout_resources(const VkDevice device) const {
    if (device == VK_NULL_HANDLE) {
        return std::unexpected(std::string(
            "ShaderProgram::create_layout_resources: null device"));
    }
    auto dsl = merged_layout_.create_descriptor_set_layouts(device);
    if (!dsl.has_value()) {
        return std::unexpected(dsl.error());
    }
    std::vector<VkDescriptorSetLayout> sets = std::move(*dsl);
    auto pl = merged_layout_.create_pipeline_layout(device, sets);
    if (!pl.has_value()) {
        PipelineLayoutBuilder::destroy_descriptor_set_layouts(device, sets);
        return std::unexpected(pl.error());
    }
    return ShaderProgramLayoutResources(device, std::move(sets), *pl);
}

std::expected<DescriptorPool, std::string>
ShaderProgram::create_descriptor_pool(const VkDevice device,
                                      const std::uint32_t max_sets) const {
    if (merged_layout_.merged_bindings().empty()) {
        return std::unexpected(std::string(
            "ShaderProgram::create_descriptor_pool: no descriptor bindings"));
    }
    auto pool_result = create_descriptor_pool_for_merged_bindings(
        device, merged_layout_.merged_bindings(), max_sets);
    if (!pool_result.has_value()) {
        return std::unexpected(pool_result.error());
    }
    if (*pool_result == VK_NULL_HANDLE) {
        return std::unexpected(std::string(
            "ShaderProgram::create_descriptor_pool: pool handle null"));
    }
    return DescriptorPool::adopt(device, *pool_result);
}

} // namespace vulkan
