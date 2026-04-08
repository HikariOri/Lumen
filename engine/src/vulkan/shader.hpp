/**
 * @file shader.hpp
 * @brief `VkShaderModule` 的 RAII 封装（SPIR-V 字码或文件）。
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace vulkan {

/**
 * @brief 持有 `VkShaderModule` 及对应阶段；可默认构造后由 `create` / `load_spv` 填充。
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
     * @brief 自 SPIR-V 字码创建；`spirv` 大小须为 4 的倍数（按 `uint32_t` 计）。
     */
    [[nodiscard]] static std::expected<Shader, std::string>
    create(VkDevice device, const std::vector<std::uint32_t> &spirv,
           VkShaderStageFlagBits stage);

    /**
     * @brief 自 `.spv` 文件加载（二进制，长度须为 4 的倍数）。
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

private:
    explicit Shader(VkDevice device, VkShaderModule mod,
                    VkShaderStageFlagBits stage) noexcept;

    void destroy() noexcept;

    VkDevice vk_device_ { VK_NULL_HANDLE };
    VkShaderModule vk_module_ { VK_NULL_HANDLE };
    /// 仅 `is_valid()` 时有效；默认构造下无意义。
    VkShaderStageFlagBits stage_ { static_cast<VkShaderStageFlagBits>(0) };
};

} // namespace vulkan
