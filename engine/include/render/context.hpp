/**
 * @file context.hpp
 * @brief Vulkan 上下文：Instance、PhysicalDevice、LogicalDevice、Queue、Surface
 *
 * 本模块负责 Vulkan 核心上下文的创建与管理，包括：
 * - Vulkan Instance 初始化
 * - PhysicalDevice 枚举与选择
 * - LogicalDevice 与 Queue 创建
 * - VMA 分配器管理
 *
 * 根据 docs/design/render-engine-roadmap.md 中的 VkContext 模块设计。
 *
 * @note Vulkan 程序启动大致流程：
 *   1. 创建 Vulkan Instance
 *   2. 枚举可用 PhysicalDevice
 *   3. 选择最合适的 PhysicalDevice
 *   4. 创建 LogicalDevice 与 Command Queues
 *   5. 创建 Surface 并查询呈现支持
 *   6. 初始化 Swapchain 和渲染资源
 *   7. 进入渲染循环
 * @see https://docs.vulkan.org/tutorial/html/chapters/overview.html
 * :contentReference[oaicite:1]{index=1}
 */

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace lumen {
namespace render {

/**
 * @enum PhysicalDeviceType
 * @brief Vulkan 物理设备类型
 *
 * 描述设备的大类，便于选择更适合的 GPU。
 * 与 Vulkan
 * 本身定义的物理设备类型语义一致。
 */
enum class PhysicalDeviceType : uint8_t {
    Other,      ///< 未知或其他类型设备
    Integrated, ///< 集成显卡
    Discrete,   ///< 独立显卡
    Virtual,    ///< 虚拟 GPU
    Cpu,        ///< 软件/CPU 回退实现
};

/**
 * @struct PhysicalDeviceInfo
 * @brief 物理设备摘要信息
 *
 * 用于日志输出、UI 展示等用途。收集了设备名称、类型、驱动/API
 * 版本及显存容量等信息。
 */
struct PhysicalDeviceInfo {
    std::string deviceName;                                      ///< 设备名称
    PhysicalDeviceType deviceType { PhysicalDeviceType::Other }; ///< 设备类型
    uint32_t vendorId { 0 };                                     ///< 厂商 ID
    uint32_t deviceId { 0 };                                     ///< 设备 ID
    uint32_t driverVersion { 0 };                                ///< 驱动版本
    uint32_t apiVersion { 0 }; ///< Vulkan API 版本
    /// 设备本地显存（VRAM）字节数，0 表示无法确定
    VkDeviceSize deviceLocalMemoryBytes { 0 };
};

/**
 * @brief 获取物理设备类型的字符串描述
 *
 * @param type 物理设备类别
 * @return 可读的字符串，如 "Discrete GPU"
 */
const char *device_type_name(PhysicalDeviceType type);

/**
 * @struct ContextConfig
 * @brief 创建 Vulkan Context 时的配置参数
 *
 * 包含应用层与引擎层名称/版本、Vulkan API 版本、
 * 以及实例扩展/层设置等。
 */
struct ContextConfig {
    std::string appName { "Lumen" }; ///< 应用程序名称
    /// 应用层版本（Vulkan 格式：major.minor.patch）
    uint32_t appVersion { VK_MAKE_VERSION(0, 1, 0) };
    std::string engineName { "Lumen" };                  ///< 引擎名称
    uint32_t engineVersion { VK_MAKE_VERSION(0, 1, 0) }; ///< 引擎版本
    /// Vulkan API 版本要求
    uint32_t apiVersion { VK_API_VERSION_1_4 };
    /// 是否启用 Validation Layers（调试用）
    bool enableValidation { true };
    /// 需要启用的 Instance 扩展
    std::vector<const char *> instanceExtensions {};
    /// 需要启用的 Instance Layers
    std::vector<const char *> instanceLayers {};
};

/**
 * @class Context
 * @brief Vulkan 核心上下文封装类
 *
 * 封装 Vulkan Instance、PhysicalDevice、LogicalDevice、
 * 图形与呈现队列（Queues）以及 VMA 分配器的生命周期。
 * 使用 RAII 管理资源，析构时按照正确顺序销毁各对象。
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
     * @param config 上下文创建配置
     * @return 成功返回 true，否则 false
     *
     * 会基于 config 中的 app/engine 信息构造 VkInstanceCreateInfo
     * 并创建 Vulkan 实例。
     */
    bool init_instance(const ContextConfig &config);

    /**
     * @brief 初始化 PhysicalDevice、LogicalDevice、Queue
     * @param surface Vulkan Surface，用于呈现支持检查
     * @return 成功返回 true
     *
     * 枚举可用的 PhysicalDevice，选择合适的设备，
     * 检查队列族的呈现支持，并创建 LogicalDevice。
     * 如果 surface 是 VK_NULL_HANDLE，则只创建 LogicalDevice
     * 的图形队列，不检查呈现支持。
     */
    bool init_device(VkSurfaceKHR surface = VK_NULL_HANDLE);

    /// 获取 Vulkan Instance
    [[nodiscard]] VkInstance instance() const { return instance_; }
    /// 获取 PhysicalDevice
    [[nodiscard]] VkPhysicalDevice physical_device() const {
        return physicalDevice_;
    }
    /// 获取 LogicalDevice
    [[nodiscard]] VkDevice device() const { return device_; }
    /// 获取图形队列
    [[nodiscard]] VkQueue graphics_queue() const { return graphicsQueue_; }
    /// 获取呈现队列
    [[nodiscard]] VkQueue present_queue() const { return presentQueue_; }

    /// 获取图形队列族索引
    [[nodiscard]] uint32_t graphics_queue_family() const {
        return graphicsQueueFamily_;
    }
    /// 获取呈现队列族索引
    [[nodiscard]] uint32_t present_queue_family() const {
        return presentQueueFamily_;
    }

    /// 物理设备属性（需在 init_device 后调用）
    [[nodiscard]] const VkPhysicalDeviceProperties &
    physical_device_properties() const {
        return physicalDeviceProperties2_.properties;
    }

    /// 物理设备属性 2（包括扩展属性）
    [[nodiscard]] const VkPhysicalDeviceProperties2 &
    physical_device_properties2() const {
        return physicalDeviceProperties2_;
    }

    /// 获取物理设备支持的特性集
    [[nodiscard]] const VkPhysicalDeviceFeatures &
    physical_device_features() const {
        return physicalDeviceFeatures2_.features;
    }

    /// 获取物理设备扩展特性信息（1.1+）
    [[nodiscard]] const VkPhysicalDeviceFeatures2 &
    physical_device_features2() const {
        return physicalDeviceFeatures2_;
    }

    /// Vulkan 1.1+ 属性访问
    [[nodiscard]] const VkPhysicalDeviceVulkan11Properties &
    physical_device_vulkan11_properties() const {
        return vulkan11Properties_;
    }
    [[nodiscard]] const VkPhysicalDeviceVulkan12Properties &
    physical_device_vulkan12_properties() const {
        return vulkan12Properties_;
    }
    [[nodiscard]] const VkPhysicalDeviceVulkan13Properties &
    physical_device_vulkan13_properties() const {
        return vulkan13Properties_;
    }
    [[nodiscard]] const VkPhysicalDeviceVulkan14Properties &
    physical_device_vulkan14_properties() const {
        return vulkan14Properties_;
    }

    /// 获取物理设备内存属性
    [[nodiscard]] const VkPhysicalDeviceMemoryProperties &
    physical_device_memory_properties() const {
        return physicalDeviceMemoryProperties_;
    }

    /// 物理设备信息摘要（instancetype/device info）
    [[nodiscard]] PhysicalDeviceInfo physical_device_info() const;

    /// Vulkan Instance 是否已经创建
    [[nodiscard]] bool has_instance() const {
        return instance_ != VK_NULL_HANDLE;
    }

    /// LogicalDevice 是否已经创建
    [[nodiscard]] bool has_device() const { return device_ != VK_NULL_HANDLE; }

    /// 获取 VMA 分配器
    [[nodiscard]] VmaAllocator vma_allocator() const { return vmaAllocator_; }

    /**
     * @brief 等待设备空闲
     *
     * 在 Swapchain 重建或资源销毁之前调用，以保证
     * 所有提交的命令执行完毕。
     */
    void wait_idle() const;

private:
    void destroy_();
    void relink_properties_chain_();
    void relink_features_chain_();
    bool pick_physical_device_(VkSurfaceKHR surface);
    bool create_logical_device_(VkSurfaceKHR surface);
    bool create_vma_allocator_();

    VkInstance instance_ { VK_NULL_HANDLE };
    VkPhysicalDevice physicalDevice_ { VK_NULL_HANDLE };
    VkDevice device_ { VK_NULL_HANDLE };
    VkQueue graphicsQueue_ { VK_NULL_HANDLE };
    VkQueue presentQueue_ { VK_NULL_HANDLE };
    uint32_t graphicsQueueFamily_ { 0 };
    uint32_t presentQueueFamily_ { 0 };
    bool validationEnabled_ { false };

    // Property and feature chain heads
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

    VmaAllocator vmaAllocator_ { nullptr };
};

} // namespace render
} // namespace lumen
