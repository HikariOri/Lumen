/**
 * @file shader.hpp
 * @brief `VkShaderModule` 的 RAII 封装；创建/加载时同步完成 SPIR-V 反射。
 */

#pragma once

#include "vulkan/shader_reflection.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace vulkan {

/**
 * @brief 单阶段着色器：模块 + 与 SPIR-V 一致的 `ShaderReflection`（`create` /
 * `load_spv` 成功即已填充）。
 */
class Shader final {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;
    Shader(Shader &&other) noexcept;
    Shader &operator=(Shader &&other) noexcept;

    /**
     * @brief 自 SPIR-V 字码创建模块并反射；`spirv` 须 `uint32_t` 对齐。
     */
    [[nodiscard]] static std::expected<Shader, std::string>
    create(VkDevice device, const std::vector<std::uint32_t> &spirv,
           VkShaderStageFlagBits stage);

    /**
     * @brief 自 `.spv` 文件加载并反射（文件大小须为 4 的倍数）。
     */
    [[nodiscard]] static std::expected<Shader, std::string>
    load_spv(VkDevice device, const std::string &path,
             VkShaderStageFlagBits stage);

    [[nodiscard]] VkShaderModule shader_module() const noexcept {
        return vk_module_;
    }
    [[nodiscard]] VkShaderStageFlagBits stage() const noexcept {
        return stage_;
    }
    [[nodiscard]] bool is_valid() const noexcept {
        return vk_module_ != VK_NULL_HANDLE;
    }

    /**
     * @brief 与当前模块对应的反射结果；`is_valid()` 时为加载/创建成功时的快照。
     */
    [[nodiscard]] const ShaderReflection &reflection() const noexcept {
        return reflection_;
    }

private:
    explicit Shader(VkDevice device, VkShaderModule mod,
                    VkShaderStageFlagBits stage,
                    ShaderReflection &&reflection) noexcept;

    void destroy() noexcept;

    VkDevice vk_device_ { VK_NULL_HANDLE };
    VkShaderModule vk_module_ { VK_NULL_HANDLE };
    VkShaderStageFlagBits stage_ { static_cast<VkShaderStageFlagBits>(0) };
    ShaderReflection reflection_ {};
};

} // namespace vulkan
