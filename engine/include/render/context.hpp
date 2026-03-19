/**
 * @file context.hpp
 * @brief Vulkan 上下文：Instance、PhysicalDevice、LogicalDevice、Queue、Surface
 *
 * 负责 Vulkan 的初始化与核心对象的生命周期管理。
 * 根据 RENDER_ENGINE_PLAN 的 VkContext 模块设计。
 */

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace lumen {
    namespace render {

        /// 上下文创建配置
        struct ContextConfig {
            /// 应用名称，用于 Instance 创建
            std::string appName { "LearnVulkan" };
            /// 应用版本号（Vulkan 格式：major.minor.patch 打包）
            uint32_t appVersion { VK_MAKE_VERSION(0, 1, 0) };
            /// 引擎名称
            std::string engineName { "Lumen" };
            /// 引擎版本
            uint32_t engineVersion { VK_MAKE_VERSION(0, 1, 0) };
            /// API 版本，建议 1.2+
            uint32_t apiVersion { VK_API_VERSION_1_2 };
            /// Debug 构建是否启用 Validation Layer
            bool enableValidation { true };
            /// 需要的 Instance 扩展（如 VK_KHR_surface、平台相关 Surface 扩展）
            std::vector<const char *> instanceExtensions {};
            /// 需要的 Instance Layers（如 VK_LAYER_KHRONOS_validation）
            std::vector<const char *> instanceLayers {};
        };

        /**
         * @class Context
         * @brief Vulkan 核心上下文，封装
         * Instance、PhysicalDevice、LogicalDevice、Queue、Surface
         *
         * 使用 RAII 管理资源，析构时按正确顺序销毁。
         * Surface 需由外部（如 Window）提供创建回调或句柄。
         */
        class Context {
        public:
            Context() = default;
            Context(const Context &) = delete;
            Context(Context &&other) noexcept;
            Context &operator=(const Context &) = delete;
            Context &operator=(Context &&other) noexcept;
            ~Context();

            /**
             * @brief 初始化 Vulkan Instance
             * @param config 上下文配置
             * @return 成功返回 true，失败返回 false
             */
            bool init_instance(const ContextConfig &config);

            /**
             * @brief 选择 PhysicalDevice 并创建 LogicalDevice、Queue
             * @param surface 用于检查呈现支持的 Surface，可为
             * VK_NULL_HANDLE（无窗口模式）
             * @return 成功返回 true
             */
            bool init_device(VkSurfaceKHR surface = VK_NULL_HANDLE);

            /**
             * @brief 获取 Vulkan Instance
             */
            [[nodiscard]] VkInstance instance() const { return instance_; }

            /**
             * @brief 获取 PhysicalDevice
             */
            [[nodiscard]] VkPhysicalDevice physical_device() const {
                return physicalDevice_;
            }

            /**
             * @brief 获取 LogicalDevice
             */
            [[nodiscard]] VkDevice device() const { return device_; }

            /**
             * @brief 获取图形队列
             */
            [[nodiscard]] VkQueue graphics_queue() const {
                return graphicsQueue_;
            }

            /**
             * @brief 获取呈现队列（可能与 graphics_queue 相同）
             */
            [[nodiscard]] VkQueue present_queue() const {
                return presentQueue_;
            }

            /**
             * @brief 图形队列族索引
             */
            [[nodiscard]] uint32_t graphics_queue_family() const {
                return graphicsQueueFamily_;
            }

            /**
             * @brief 呈现队列族索引
             */
            [[nodiscard]] uint32_t present_queue_family() const {
                return presentQueueFamily_;
            }

            /// 是否已初始化 Instance
            [[nodiscard]] bool has_instance() const {
                return instance_ != VK_NULL_HANDLE;
            }

            /// 是否已初始化 Device
            [[nodiscard]] bool has_device() const {
                return device_ != VK_NULL_HANDLE;
            }

        private:
            void destroy_();
            bool pick_physical_device_(VkSurfaceKHR surface);
            bool create_logical_device_(VkSurfaceKHR surface);

            VkInstance instance_ { VK_NULL_HANDLE };
            VkPhysicalDevice physicalDevice_ { VK_NULL_HANDLE };
            VkDevice device_ { VK_NULL_HANDLE };
            VkQueue graphicsQueue_ { VK_NULL_HANDLE };
            VkQueue presentQueue_ { VK_NULL_HANDLE };
            uint32_t graphicsQueueFamily_ { 0 };
            uint32_t presentQueueFamily_ { 0 };
            bool validationEnabled_ { false };
        };

    } // namespace render
} // namespace lumen
