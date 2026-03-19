/**
 * @file shader.cpp
 * @brief ShaderModule 实现
 */

#include "render/shader.hpp"

#include <fstream>

namespace lumen {
namespace render {

bool ShaderModule::create(VkDevice device, std::span<const uint32_t> code) {
    if (code.empty()) return false;

    device_ = device;

    VkShaderModuleCreateInfo createInfo {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    createInfo.codeSize = code.size_bytes();
    createInfo.pCode = code.data();

    VkResult result =
        vkCreateShaderModule(device_, &createInfo, nullptr, &module_);
    return result == VK_SUCCESS;
}

bool ShaderModule::create_from_file(VkDevice device, const char* filePath) {
    std::ifstream file { filePath, std::ios::ate | std::ios::binary };
    if (!file) return false;

    size_t fileSize = file.tellg();
    if (fileSize % 4 != 0) return false;

    std::vector<uint32_t> code(fileSize / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), fileSize);

    return create(device, code);
}

VkPipelineShaderStageCreateInfo ShaderModule::stage_create_info(
    VkShaderStageFlagBits stage, const char* entryPoint) const {
    VkPipelineShaderStageCreateInfo info {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    info.stage = stage;
    info.module = module_;
    info.pName = entryPoint;
    return info;
}

void ShaderModule::destroy_() {
    if (module_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, module_, nullptr);
        module_ = VK_NULL_HANDLE;
    }
}

ShaderModule::~ShaderModule() { destroy_(); }

ShaderModule::ShaderModule(ShaderModule&& other) noexcept
    : device_ { other.device_ }
    , module_ { other.module_ } {
    other.device_ = VK_NULL_HANDLE;
    other.module_ = VK_NULL_HANDLE;
}

ShaderModule& ShaderModule::operator=(ShaderModule&& other) noexcept {
    if (this == &other) return *this;
    destroy_();
    device_ = other.device_;
    module_ = other.module_;
    other.device_ = VK_NULL_HANDLE;
    other.module_ = VK_NULL_HANDLE;
    return *this;
}

} // namespace render
} // namespace lumen
