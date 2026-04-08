/**
 * @file shader.cpp
 * @brief `vulkan::Shader` 实现。
 */

#include "vulkan/shader.hpp"

#include <cstring>
#include <fstream>

namespace vulkan {

Shader::Shader(const VkDevice device, const VkShaderModule mod,
               const VkShaderStageFlagBits stage) noexcept
    : vk_device_(device), vk_module_(mod), stage_(stage) {}

void Shader::destroy() noexcept {
    if (vk_device_ != VK_NULL_HANDLE && vk_module_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(vk_device_, vk_module_, nullptr);
    }
    vk_device_ = VK_NULL_HANDLE;
    vk_module_ = VK_NULL_HANDLE;
}

Shader::~Shader() {
    destroy();
}

Shader::Shader(Shader &&other) noexcept
    : vk_device_(other.vk_device_), vk_module_(other.vk_module_),
      stage_(other.stage_) {
    other.vk_device_ = VK_NULL_HANDLE;
    other.vk_module_ = VK_NULL_HANDLE;
}

Shader &Shader::operator=(Shader &&other) noexcept {
    if (this != &other) {
        destroy();
        vk_device_ = other.vk_device_;
        vk_module_ = other.vk_module_;
        stage_ = other.stage_;
        other.vk_device_ = VK_NULL_HANDLE;
        other.vk_module_ = VK_NULL_HANDLE;
    }
    return *this;
}

std::expected<Shader, std::string>
Shader::create(const VkDevice device, const std::vector<std::uint32_t> &spirv,
                const VkShaderStageFlagBits stage) {
    if (device == VK_NULL_HANDLE) {
        return std::unexpected(std::string("Shader::create: null device"));
    }
    if (spirv.empty()) {
        return std::unexpected(std::string("Shader::create: empty SPIR-V"));
    }

    VkShaderModuleCreateInfo info { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    info.codeSize = spirv.size() * sizeof(std::uint32_t);
    info.pCode = spirv.data();

    VkShaderModule mod { VK_NULL_HANDLE };
    const VkResult res =
        vkCreateShaderModule(device, &info, nullptr, &mod);
    if (res != VK_SUCCESS) {
        return std::unexpected(
            std::string("Shader::create: vkCreateShaderModule failed ec=") +
            std::to_string(static_cast<int>(res)));
    }
    return Shader(device, mod, stage);
}

std::expected<Shader, std::string>
Shader::load_spv(const VkDevice device, const std::string &path,
                 const VkShaderStageFlagBits stage) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return std::unexpected(std::string("Shader::load_spv: cannot open ") +
                               path);
    }
    const auto end = f.tellg();
    if (end <= 0) {
        return std::unexpected(
            std::string("Shader::load_spv: empty or invalid file ") + path);
    }
    const auto size = static_cast<std::size_t>(end);
    if ((size % sizeof(std::uint32_t)) != 0U) {
        return std::unexpected(
            std::string("Shader::load_spv: file size not multiple of 4: ") +
            path);
    }
    std::vector<char> bytes(size);
    f.seekg(0);
    f.read(bytes.data(), static_cast<std::streamsize>(size));
    if (!f) {
        return std::unexpected(
            std::string("Shader::load_spv: read failed: ") + path);
    }
    std::vector<std::uint32_t> code(size / sizeof(std::uint32_t));
    std::memcpy(code.data(), bytes.data(), size);
    return create(device, code, stage);
}

} // namespace vulkan
