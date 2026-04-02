/**
 * @file shader.cpp
 * @brief ShaderModule 实现（Vulkan-Hpp）
 */

#include "render/shader.hpp"
#include "core/logger.hpp"

#include <fstream>

namespace lumen {
namespace render {

bool ShaderModule::create_from_spirv(vk::Device device,
                                     std::span<const uint32_t> code) {
    if (code.empty()) {
        LUMEN_LOG_ERROR("ShaderModule 创建失败: code 为空");
        return false;
    }

    device_ = device;

    vk::ShaderModuleCreateInfo createInfo {};
    createInfo.codeSize = code.size_bytes();
    createInfo.pCode = code.data();

    const vk::Result result =
        device_.createShaderModule(&createInfo, nullptr, &module_);

    if (result == vk::Result::eSuccess) {
        LUMEN_LOG_DEBUG("ShaderModule 创建成功, {} bytes", code.size_bytes());
    } else {
        LUMEN_LOG_ERROR("ShaderModule 创建失败: {}", static_cast<int>(result));
    }

    return result == vk::Result::eSuccess;
}

bool ShaderModule::create_from_spirv(const Context &context,
                                     std::span<const uint32_t> code) {
    return create_from_spirv(context.device(), code);
}

bool ShaderModule::create_from_file(vk::Device device, const char *filePath) {
    std::ifstream file { filePath, std::ios::ate | std::ios::binary };

    if (!file) {
        LUMEN_LOG_ERROR("Shader 文件打开失败: {}", filePath);
        return false;
    }

    size_t fileSize = file.tellg();

    if (fileSize % 4 != 0) {
        LUMEN_LOG_ERROR("Shader 文件大小不是 4 的倍数: {}", filePath);
        return false;
    }

    std::vector<uint32_t> code(fileSize / 4);

    file.seekg(0);

    file.read(reinterpret_cast<char *>(code.data()), fileSize);

    bool ok = create_from_spirv(device, code);

    if (ok) {
        LUMEN_LOG_DEBUG("Shader 加载成功: {}", filePath);
    }

    return ok;
}

bool ShaderModule::create_from_file(const Context &context,
                                    const char *filePath) {
    return create_from_file(context.device(), filePath);
}

vk::PipelineShaderStageCreateInfo
ShaderModule::stage_create_info(vk::ShaderStageFlagBits stage,
                                const char *entryPoint) const {
    vk::PipelineShaderStageCreateInfo info {};
    info.stage = stage;
    info.module = module_;
    info.pName = entryPoint;
    return info;
}

void ShaderModule::destroy_() {
    if (module_) {
        device_.destroyShaderModule(module_, nullptr);
        module_ = nullptr;
    }
}

ShaderModule::~ShaderModule() { destroy_(); }

ShaderModule::ShaderModule(ShaderModule &&other) noexcept
    : device_ { other.device_ }, module_ { other.module_ } {
    other.device_ = nullptr;
    other.module_ = nullptr;
}

ShaderModule &ShaderModule::operator=(ShaderModule &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy_();

    device_ = other.device_;
    module_ = other.module_;

    other.device_ = nullptr;
    other.module_ = nullptr;

    return *this;
}

} // namespace render
} // namespace lumen
