/**
 * @file shader.cpp
 * @brief ShaderModule 实现
 *
 * 实现 Vulkan ShaderModule 的创建、加载与销毁逻辑。
 *
 * 核心职责：
 * - 从 SPIR-V 字节码创建 VkShaderModule
 * - 从文件加载 SPIR-V
 * - 提供 Pipeline Shader Stage 描述
 *
 * ⚠️ Vulkan 关键语义：
 * - ShaderModule 只是 SPIR-V 的封装（不是最终 GPU shader）
 * - 真正编译发生在 vkCreateGraphicsPipelines
 * - Pipeline 创建后即可销毁 ShaderModule
 */

#include "render/shader.hpp"
#include "core/logger.hpp"

#include <fstream>

namespace lumen {
namespace render {

/**
 * @brief 从 SPIR-V 字节码创建 ShaderModule
 *
 * @param device Vulkan 逻辑设备
 * @param code SPIR-V 字节码（必须 uint32_t 对齐）
 *
 * @return true 成功
 * @return false 失败
 *
 * ⚠️ 注意：
 * - SPIR-V 数据必须是 4 字节对齐（uint32_t）
 * - codeSize 必须是字节数（不是元素数）
 */
bool ShaderModule::create_from_spirv(VkDevice device,
                                     std::span<const uint32_t> code) {
    if (code.empty()) {
        LUMEN_LOG_ERROR("ShaderModule 创建失败: code 为空");
        return false;
    }

    device_ = device;

    VkShaderModuleCreateInfo createInfo {
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO
    };

    /// SPIR-V 字节大小（单位：byte）
    createInfo.codeSize = code.size_bytes();

    /// 指向 SPIR-V 数据（uint32_t 对齐）
    createInfo.pCode = code.data();

    VkResult result =
        vkCreateShaderModule(device_, &createInfo, nullptr, &module_);

    if (result == VK_SUCCESS) {
        LUMEN_LOG_DEBUG("ShaderModule 创建成功, {} bytes", code.size_bytes());
    } else {
        LUMEN_LOG_ERROR("ShaderModule 创建失败: {}", static_cast<int>(result));
    }

    return result == VK_SUCCESS;
}

bool ShaderModule::create_from_spirv(const Context &context,
                                     std::span<const uint32_t> code) {
    return create_from_spirv(context.device(), code);
}

/**
 * @brief 从文件加载 SPIR-V 并创建 ShaderModule
 *
 * @param device Vulkan 逻辑设备
 * @param filePath SPIR-V 文件路径（.spv）
 *
 * @return true 成功
 * @return false 失败
 *
 * 实现细节：
 * - 使用 std::ios::ate 直接获取文件大小
 * - 一次性读取到内存
 *
 * ⚠️ 注意：
 * - 文件大小必须是 4 的倍数（SPIR-V 对齐要求）
 * - 文件必须为二进制格式
 */
bool ShaderModule::create_from_file(VkDevice device, const char *filePath) {
    /// 以二进制 + 定位到文件末尾打开
    std::ifstream file { filePath, std::ios::ate | std::ios::binary };

    if (!file) {
        LUMEN_LOG_ERROR("Shader 文件打开失败: {}", filePath);
        return false;
    }

    /// 获取文件大小（字节）
    size_t fileSize = file.tellg();

    /// SPIR-V 必须 4 字节对齐
    if (fileSize % 4 != 0) {
        LUMEN_LOG_ERROR("Shader 文件大小不是 4 的倍数: {}", filePath);
        return false;
    }

    /// 分配 uint32_t 缓冲区
    std::vector<uint32_t> code(fileSize / 4);

    /// 回到文件开头
    file.seekg(0);

    /// 读取全部数据
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

/**
 * @brief 构造 Pipeline Shader Stage 信息
 *
 * @param stage Shader 阶段（Vertex / Fragment / Compute 等）
 * @param entryPoint 入口函数名（默认 "main"）
 *
 * @return VkPipelineShaderStageCreateInfo
 *
 * 用途：
 * - 在 Pipeline 创建时指定 shader
 *
 * 示例：
 *   Vertex Shader:
 *     stage = VK_SHADER_STAGE_VERTEX_BIT
 *
 * ⚠️ 注意：
 * - module_ 必须有效
 * - entryPoint 必须存在于 SPIR-V 中
 */
VkPipelineShaderStageCreateInfo
ShaderModule::stage_create_info(VkShaderStageFlagBits stage,
                                const char *entryPoint) const {
    VkPipelineShaderStageCreateInfo info {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
    };

    info.stage = stage;
    info.module = module_;
    info.pName = entryPoint;

    return info;
}

/**
 * @brief 销毁 ShaderModule
 *
 * 调用 vkDestroyShaderModule 释放资源
 *
 * ⚠️ 生命周期：
 * - 必须在 device 销毁前调用
 * - 可在 Pipeline 创建后立即销毁
 */
void ShaderModule::destroy_() {
    if (module_ != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device_, module_, nullptr);
        module_ = VK_NULL_HANDLE;
    }
}

/**
 * @brief 析构函数（RAII）
 */
ShaderModule::~ShaderModule() { destroy_(); }

/**
 * @brief 移动构造函数
 *
 * 转移 ShaderModule 所有权
 */
ShaderModule::ShaderModule(ShaderModule &&other) noexcept
    : device_ { other.device_ }, module_ { other.module_ } {
    other.device_ = VK_NULL_HANDLE;
    other.module_ = VK_NULL_HANDLE;
}

/**
 * @brief 移动赋值运算符
 *
 * - 释放当前资源
 * - 接管 other 的资源
 */
ShaderModule &ShaderModule::operator=(ShaderModule &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    destroy_();

    device_ = other.device_;
    module_ = other.module_;

    other.device_ = VK_NULL_HANDLE;
    other.module_ = VK_NULL_HANDLE;

    return *this;
}

} // namespace render
} // namespace lumen
