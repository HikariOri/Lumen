#pragma once

#include "rhi/vulkan.hpp"

#include <vk_mem_alloc.h>

#include <cstdint>
#include <string>
#include <vector>

/*
职责：

*   instance / device
*   队列管理（graphics / compute / transfer / present）
*   feature 启用（Vulkan 1.4 链；光追扩展预留）
*   allocator（VMA）

👉 `ContextDesc::windowHandle` 指向 `lumen::platform::Window`（启用 Surface 时）。
*/

namespace rhi {

struct ContextDesc {
    bool enableValidation = true;
    bool enableSurface = true;
    bool enableRayTracing = false;

    std::uint32_t apiVersion = VK_API_VERSION_1_4;

    std::vector<const char *> instanceExtensions;
    std::vector<const char *> deviceExtensions;

    void *windowHandle = nullptr; ///< `lumen::platform::Window*`，可选

    /// 非空时：`Device` 会创建 `vk::PipelineCache` 时读入、析构时写出驱动管线缓存。
    std::string pipeline_cache_file_path;
};

class Context {
public:
    Context() = default;
    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;
    Context(Context &&other) noexcept;
    Context &operator=(Context &&other) noexcept;
    ~Context();

    [[nodiscard]] bool init(const ContextDesc &desc);
    void shutdown();

    [[nodiscard]] vk::Instance instance() const { return instance_; }
    [[nodiscard]] vk::PhysicalDevice physical_device() const {
        return physical_device_;
    }
    [[nodiscard]] vk::Device device() const { return device_; }

    [[nodiscard]] std::uint32_t graphics_queue_family() const {
        return graphics_queue_family_;
    }
    [[nodiscard]] std::uint32_t compute_queue_family() const {
        return compute_queue_family_;
    }
    [[nodiscard]] std::uint32_t transfer_queue_family() const {
        return transfer_queue_family_;
    }
    [[nodiscard]] std::uint32_t present_queue_family() const {
        return present_queue_family_;
    }

    [[nodiscard]] vk::Queue graphics_queue() const { return graphics_queue_; }
    [[nodiscard]] vk::Queue compute_queue() const { return compute_queue_; }
    [[nodiscard]] vk::Queue transfer_queue() const { return transfer_queue_; }
    [[nodiscard]] vk::Queue present_queue() const { return present_queue_; }

    [[nodiscard]] vk::SurfaceKHR surface() const { return surface_; }

    [[nodiscard]] const vk::PhysicalDeviceProperties2 &
    physical_device_properties2() const {
        return physical_device_properties2_;
    }
    [[nodiscard]] const vk::PhysicalDeviceFeatures2 &
    physical_device_features2() const {
        return physical_device_features2_;
    }

    [[nodiscard]] VmaAllocator allocator() const { return allocator_; }
    [[nodiscard]] bool validation_enabled() const {
        return validation_enabled_;
    }

    [[nodiscard]] const std::string &pipeline_cache_file_path() const {
        return desc_.pipeline_cache_file_path;
    }

    void wait_idle() const;

private:
    void relink_properties_chain_();
    void relink_features_chain_();
    void build_required_device_extensions_();

    [[nodiscard]] bool create_instance_();
    [[nodiscard]] bool setup_debug_messenger_();
    [[nodiscard]] bool create_surface_();
    [[nodiscard]] bool pick_physical_device_();
    [[nodiscard]] bool create_device_();
    [[nodiscard]] bool create_allocator_();

    [[nodiscard]] bool check_device_extensions_(
        vk::PhysicalDevice device) const;
    [[nodiscard]] bool is_device_suitable_(vk::PhysicalDevice device);
    void find_queue_families_(vk::PhysicalDevice device);
    [[nodiscard]] int score_physical_device_(vk::PhysicalDevice device) const;

    vk::Instance instance_;
    vk::DebugUtilsMessengerEXT debug_messenger_;
    vk::PhysicalDevice physical_device_;
    vk::Device device_;

    vk::Queue graphics_queue_;
    vk::Queue compute_queue_;
    vk::Queue transfer_queue_;
    vk::Queue present_queue_;

    std::uint32_t graphics_queue_family_ { UINT32_MAX };
    std::uint32_t compute_queue_family_ { UINT32_MAX };
    std::uint32_t transfer_queue_family_ { UINT32_MAX };
    std::uint32_t present_queue_family_ { UINT32_MAX };

    vk::SurfaceKHR surface_;

    VmaAllocator allocator_ { VK_NULL_HANDLE };

    ContextDesc desc_;
    bool validation_enabled_ { false };

    std::vector<const char *> required_device_extensions_;

    vk::PhysicalDeviceProperties2 physical_device_properties2_;
    vk::PhysicalDeviceVulkan11Properties vulkan_11_properties_;
    vk::PhysicalDeviceVulkan12Properties vulkan_12_properties_;
    vk::PhysicalDeviceVulkan13Properties vulkan_13_properties_;
    vk::PhysicalDeviceVulkan14Properties vulkan_14_properties_;

    vk::PhysicalDeviceFeatures2 physical_device_features2_;
    vk::PhysicalDeviceVulkan11Features vulkan_11_features_;
    vk::PhysicalDeviceVulkan12Features vulkan_12_features_;
    vk::PhysicalDeviceVulkan13Features vulkan_13_features_;
    vk::PhysicalDeviceVulkan14Features vulkan_14_features_;

    vk::PhysicalDeviceAccelerationStructureFeaturesKHR accel_features_;
    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rt_pipeline_features_;
};

} // namespace rhi

