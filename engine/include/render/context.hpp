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

/// 物理设备类型
enum class PhysicalDeviceType : uint8_t {
    Other,
    Integrated,
    Discrete,
    Virtual,
    Cpu,
};

/// 显卡信息（便于日志、UI 显示）
struct PhysicalDeviceInfo {
    std::string deviceName;
    PhysicalDeviceType deviceType { PhysicalDeviceType::Other };
    uint32_t vendorId { 0 };
    uint32_t deviceId { 0 };
    uint32_t driverVersion { 0 };
    uint32_t apiVersion { 0 };
    /// 设备本地显存（VRAM）字节数，0 表示无法确定
    VkDeviceSize deviceLocalMemoryBytes { 0 };
};

/// 设备类型名称（如 "Discrete GPU"）
const char *device_type_name(PhysicalDeviceType type);

/// 上下文创建配置
struct ContextConfig {
    /// 应用名称，用于 Instance 创建
    std::string appName { "Lumen" };
    /// 应用版本号（Vulkan 格式：major.minor.patch 打包）
    uint32_t appVersion { VK_MAKE_VERSION(0, 1, 0) };
    /// 引擎名称
    std::string engineName { "Lumen" };
    /// 引擎版本
    uint32_t engineVersion { VK_MAKE_VERSION(0, 1, 0) };
    /// API 版本，建议 1.4
    uint32_t apiVersion { VK_API_VERSION_1_4 };
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
    [[nodiscard]] VkQueue graphics_queue() const { return graphicsQueue_; }

    /**
     * @brief 获取呈现队列（可能与 graphics_queue 相同）
     */
    [[nodiscard]] VkQueue present_queue() const { return presentQueue_; }

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

    /// 物理设备属性（init_device 后有效）
    [[nodiscard]] const VkPhysicalDeviceProperties &
    physical_device_properties() const {
        return physicalDeviceProperties2_.properties;
    }

    /// 物理设备属性2 + Vulkan 1.1-1.4 扩展（init_device 后有效）
    [[nodiscard]] const VkPhysicalDeviceProperties2 &
    physical_device_properties2() const {
        return physicalDeviceProperties2_;
    }

    /// 物理设备特性（init_device 后有效）
    [[nodiscard]] const VkPhysicalDeviceFeatures &
    physical_device_features() const {
        return physicalDeviceFeatures2_.features;
    }

    /// 物理设备特性2 + Vulkan 1.1-1.4 扩展（init_device 后有效）
    [[nodiscard]] const VkPhysicalDeviceFeatures2 &
    physical_device_features2() const {
        return physicalDeviceFeatures2_;
    }

    /// Vulkan 1.1 属性
    [[nodiscard]] const VkPhysicalDeviceVulkan11Properties &
    physical_device_vulkan11_properties() const {
        return vulkan11Properties_;
    }

    /// Vulkan 1.2 属性
    [[nodiscard]] const VkPhysicalDeviceVulkan12Properties &
    physical_device_vulkan12_properties() const {
        return vulkan12Properties_;
    }

    /// Vulkan 1.3 属性
    [[nodiscard]] const VkPhysicalDeviceVulkan13Properties &
    physical_device_vulkan13_properties() const {
        return vulkan13Properties_;
    }

    /// Vulkan 1.4 属性
    [[nodiscard]] const VkPhysicalDeviceVulkan14Properties &
    physical_device_vulkan14_properties() const {
        return vulkan14Properties_;
    }

    /// 物理设备内存属性（init_device 后有效）
    [[nodiscard]] const VkPhysicalDeviceMemoryProperties &
    physical_device_memory_properties() const {
        return physicalDeviceMemoryProperties_;
    }

    /// 显卡信息摘要（init_device 后有效）
    [[nodiscard]] PhysicalDeviceInfo physical_device_info() const;

    /// 是否已初始化 Instance
    [[nodiscard]] bool has_instance() const {
        return instance_ != VK_NULL_HANDLE;
    }

    /// 是否已初始化 Device
    [[nodiscard]] bool has_device() const { return device_ != VK_NULL_HANDLE; }

    /**
     * @brief 等待设备所有操作完成（阻塞直到 GPU 空闲）
     * @note 在重建 Swapchain、资源销毁等操作前调用
     */
    void wait_idle() const;

private:
    void destroy_();
    void relink_properties_chain_();
    void relink_features_chain_();
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

    VkPhysicalDeviceProperties2 physicalDeviceProperties2_ {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2
    };
    VkPhysicalDeviceVulkan11Properties vulkan11Properties_ {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES
    };
    VkPhysicalDeviceVulkan12Properties vulkan12Properties_ {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES
    };
    VkPhysicalDeviceVulkan13Properties vulkan13Properties_ {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES
    };
    VkPhysicalDeviceVulkan14Properties vulkan14Properties_ {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES
    };

    VkPhysicalDeviceFeatures2 physicalDeviceFeatures2_ {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2
    };
    VkPhysicalDeviceVulkan11Features vulkan11Features_ {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES
    };
    VkPhysicalDeviceVulkan12Features vulkan12Features_ {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
    };
    VkPhysicalDeviceVulkan13Features vulkan13Features_ {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
    };
    VkPhysicalDeviceVulkan14Features vulkan14Features_ {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES
    };

    VkPhysicalDeviceMemoryProperties physicalDeviceMemoryProperties_ {};
};

} // namespace render
} // namespace lumen
