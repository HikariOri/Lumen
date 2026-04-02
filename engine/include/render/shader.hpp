/**
 * @file shader.hpp
 * @brief Shader 模块封装：从 SPIR-V 或文件加载
 *
 * Vulkan 中的 Shader 必须使用 SPIR-V 字节码，而不是 GLSL/HLSL 源码。
 * ShaderModule 是对 SPIR-V 的轻量封装，用于 Pipeline 创建阶段。
 *
 * ⚠️ 重要特性：
 * - ShaderModule 本质是“字节码容器”，不包含编译后的 GPU 代码
 * - 真正的编译/优化发生在 Pipeline 创建时
 * - Pipeline 创建后，可以销毁 ShaderModule
 * :contentReference[oaicite:0]{index=0}
 */

#pragma once

#include "context.hpp"
#include <cstdint>
#include <span>
#include <vector>

#include "render/vulkan.hpp"

namespace lumen {
namespace render {

/**
 * @class ShaderModule
 * @brief Vulkan Shader 模块封装
 *
 * 对 VkShaderModule 的 RAII 封装，负责：
 * - SPIR-V 加载
 * - VkShaderModule 创建 / 销毁
 * - 构造 Pipeline Shader Stage 信息
 *
 * 📌 Vulkan 语义：
 * ShaderModule 表示一个“着色器字节码集合”，
 * 可以包含多个 entry point（函数入口） :contentReference[oaicite:1]{index=1}
 *
 * 在 Pipeline 中，通过：
 * - 指定 stage（Vertex / Fragment / Compute）
 * - 指定 entry point（默认 "main"）
 * 来选择实际执行的 shader
 */
class ShaderModule {
public:
    /**
     * @brief 默认构造（无效对象）
     */
    ShaderModule() = default;

    /// 禁止拷贝（Vulkan 资源唯一所有权）
    ShaderModule(const ShaderModule &) = delete;

    /**
     * @brief 移动构造（转移 VkShaderModule 所有权）
     */
    ShaderModule(ShaderModule &&other) noexcept;

    /// 禁止拷贝赋值
    ShaderModule &operator=(const ShaderModule &) = delete;

    /**
     * @brief 移动赋值
     */
    ShaderModule &operator=(ShaderModule &&other) noexcept;

    /**
     * @brief 析构函数
     *
     * 自动调用 vkDestroyShaderModule 释放资源
     */
    ~ShaderModule();

    /**
     * @brief 从 SPIR-V 字节码创建 ShaderModule
     *
     * @param device Vulkan 逻辑设备
     * @param code SPIR-V 二进制（uint32_t 对齐）
     *
     * @return true  创建成功
     * @return false 创建失败
     *
     * ⚠️ 注意：
     * - code 必须是合法 SPIR-V（通常由 glslc / DXC 编译生成）
     * - 数据需满足 uint32_t 对齐要求
     */
    bool create_from_spirv(vk::Device device, std::span<const uint32_t> code);

    /**
     * @brief 从 SPIR-V 字节码创建 ShaderModule（使用 Context）
     *
     * @param context 渲染上下文（提供 VkDevice）
     * @param code SPIR-V 字节码（uint32_t 对齐）
     *
     * @return true  创建成功
     * @return false 创建失败
     *
     * 📌 等价于：
     *   create(context.device(), code)
     *
     * 设计目的：
     * - 避免上层直接接触 VkDevice（降低 Vulkan API 泄漏）
     * - 与引擎其他模块（Pipeline / Texture）接口风格统一
     *
     * ⚠️ 注意：
     * - Context 必须已初始化（device 有效）
     * - code 必须为合法 SPIR-V 数据
     */
    bool create_from_spirv(const Context &context, std::span<const uint32_t> code);

    /**
     * @brief 从文件加载 SPIR-V 并创建 ShaderModule
     *
     * @param device Vulkan 逻辑设备
     * @param filePath SPIR-V 文件路径（.spv）
     *
     * @return true  成功
     * @return false 失败
     *
     * 📌 常见流程：
     * GLSL/HLSL → (glslc/dxc) → SPIR-V → ShaderModule
     */
    bool create_from_file(vk::Device device, const char *filePath);

    /**
     * @brief 从文件加载 SPIR-V 并创建 ShaderModule（Context 封装）
     *
     * @param context 渲染上下文（提供 VkDevice）
     * @param filePath SPIR-V 文件路径（.spv）
     *
     * @return true  成功
     * @return false 失败
     *
     * 📌 等价于：
     *   create_from_file(context.device(), filePath)
     *
     * 设计目的：
     * - 提供更高层接口，减少 Vulkan 句柄暴露
     * - 与引擎资源加载接口统一（Texture / Buffer 等）
     *
     * 推荐使用：
     * - 引擎代码 → 使用该接口
     * - 底层工具 → 使用 VkDevice 版本
     */
    bool create_from_file(const Context &context, const char *filePath);

    /**
     * @brief 构造 Pipeline Shader Stage 创建信息
     *
     * @param stage Shader 阶段（Vertex / Fragment / Compute 等）
     * @param entryPoint Shader 入口函数名（默认 "main"）
     *
     * @return VkPipelineShaderStageCreateInfo
     *
     * 📌 用于 Pipeline 创建：
     * - 指定 shader 属于哪个 stage
     * - 指定 entry point（函数入口）
     *
     * 示例：
     *   Vertex Shader → VK_SHADER_STAGE_VERTEX_BIT
     *
     * ⚠️ 注意：
     * - 一个 ShaderModule 可以有多个 entry point
     * - 不同 stage 可以来自不同 module :contentReference[oaicite:2]{index=2}
     */
    vk::PipelineShaderStageCreateInfo
    stage_create_info(vk::ShaderStageFlagBits stage,
                      const char *entryPoint = "main") const;

    [[nodiscard]] vk::ShaderModule handle() const { return module_; }

    [[nodiscard]] bool is_valid() const { return static_cast<bool>(module_); }

private:
    /**
     * @brief 销毁 ShaderModule（内部使用）
     *
     * 调用 vkDestroyShaderModule
     *
     * ⚠️ 注意：
     * ShaderModule 可以在 Pipeline 创建后销毁，
     * 不影响 Pipeline 使用
     */
    void destroy_();

    /// 创建该 ShaderModule 的逻辑设备
    vk::Device device_ {};

    vk::ShaderModule module_ {};
};

} // namespace render
} // namespace lumen
