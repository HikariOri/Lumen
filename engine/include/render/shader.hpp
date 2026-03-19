/**
 * @file shader.hpp
 * @brief Shader 模块封装：从 SPIR-V 或文件加载
 *
 * 创建 VkShaderModule，用于 Pipeline 的着色器阶段。
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan.h>

namespace lumen {
    namespace render {

        /**
         * @class ShaderModule
         * @brief Vulkan Shader 模块
         */
        class ShaderModule {
        public:
            ShaderModule() = default;
            ShaderModule(const ShaderModule &) = delete;
            ShaderModule(ShaderModule &&other) noexcept;
            ShaderModule &operator=(const ShaderModule &) = delete;
            ShaderModule &operator=(ShaderModule &&other) noexcept;
            ~ShaderModule();

            /**
             * @brief 从 SPIR-V 二进制创建
             * @param device VkDevice
             * @param code SPIR-V 字节码
             * @return 成功返回 true
             */
            bool create(VkDevice device, std::span<const uint32_t> code);

            /**
             * @brief 从文件加载 SPIR-V 并创建
             * @param device VkDevice
             * @param filePath .spv 文件路径
             * @return 成功返回 true
             */
            bool create_from_file(VkDevice device, const char *filePath);

            /**
             * @brief 获取 Pipeline 阶段创建信息
             * @param stage 着色器阶段
             * @param entryPoint 入口函数名
             */
            VkPipelineShaderStageCreateInfo
            stage_create_info(VkShaderStageFlagBits stage,
                              const char *entryPoint = "main") const;

            [[nodiscard]] VkShaderModule handle() const { return module_; }
            [[nodiscard]] bool is_valid() const {
                return module_ != VK_NULL_HANDLE;
            }

        private:
            void destroy_();

            VkDevice device_ { VK_NULL_HANDLE };
            VkShaderModule module_ { VK_NULL_HANDLE };
        };

    } // namespace render
} // namespace lumen
