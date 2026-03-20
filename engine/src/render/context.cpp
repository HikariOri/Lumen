/**
 * @file context.cpp
 * @brief Context 实现：Vulkan Instance、Device、Queue 创建与销毁
 */

#include "render/context.hpp"
#include "core/logger.hpp"

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>

namespace lumen::render {

namespace {

const char *kValidationLayerName = "VK_LAYER_KHRONOS_validation";

bool check_validation_layer_support(const std::vector<const char *> &layers) {
    uint32_t count { 0 };
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> available(count);
    vkEnumerateInstanceLayerProperties(&count, available.data());

    for (const char *name : layers) {
        bool found { false };
        for (const auto &prop : available) {
            if (strcmp(name, prop.layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

} // namespace

void Context::relink_properties_chain_() {
    physicalDeviceProperties2_.pNext = &vulkan11Properties_;
    vulkan11Properties_.pNext = &vulkan12Properties_;
    vulkan12Properties_.pNext = &vulkan13Properties_;
    vulkan13Properties_.pNext = &vulkan14Properties_;
    vulkan14Properties_.pNext = nullptr;
}

void Context::relink_features_chain_() {
    physicalDeviceFeatures2_.pNext = &vulkan11Features_;
    vulkan11Features_.pNext = &vulkan12Features_;
    vulkan12Features_.pNext = &vulkan13Features_;
    vulkan13Features_.pNext = &vulkan14Features_;
    vulkan14Features_.pNext = nullptr;
}

bool Context::init_instance(const ContextConfig &config) {
    if (instance_ != VK_NULL_HANDLE)
        return true;

    validationEnabled_ = config.enableValidation;

    if (validationEnabled_) {
        std::vector<const char *> layers { config.instanceLayers };
        if (std::find(layers.begin(), layers.end(), kValidationLayerName) ==
            layers.end()) {
            layers.emplace_back(kValidationLayerName);
        }
        if (!check_validation_layer_support(layers)) {
            LUMEN_LOG_WARN("Validation layers requested but not available");
            validationEnabled_ = false;
        }
    }

    VkApplicationInfo appInfo { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = config.appName.c_str();
    appInfo.applicationVersion = config.appVersion;
    appInfo.pEngineName = config.engineName.c_str();
    appInfo.engineVersion = config.engineVersion;
    appInfo.apiVersion = config.apiVersion;

    std::vector<const char *> extensions { config.instanceExtensions };
    if (validationEnabled_) {
        extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char *> layers;
    if (validationEnabled_) {
        layers.emplace_back(kValidationLayerName);
    }
    for (const char *l : config.instanceLayers) {
        if (l != kValidationLayerName)
            layers.emplace_back(l);
    }

    VkInstanceCreateInfo createInfo { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        LUMEN_LOG_ERROR("vkCreateInstance failed: {}",
                        static_cast<int>(result));
        return false;
    }
    LUMEN_LOG_DEBUG("Vulkan instance 创建成功, validation={}",
                    validationEnabled_);
    return true;
}

bool Context::pick_physical_device_(VkSurfaceKHR surface) {
    uint32_t count { 0 };
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0)
        return false;

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    for (VkPhysicalDevice dev : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);

        uint32_t queueFamilyCount { 0 };
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount,
                                                 nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount,
                                                 queueFamilies.data());

        uint32_t gfxIdx { UINT32_MAX };
        uint32_t presentIdx { UINT32_MAX };

        for (uint32_t i { 0 }; i < queueFamilyCount; ++i) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                gfxIdx = i;
            }
            if (surface != VK_NULL_HANDLE) {
                VkBool32 presentSupport { VK_FALSE };
                vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface,
                                                     &presentSupport);
                if (presentSupport)
                    presentIdx = i;
            }
        }

        if (gfxIdx != UINT32_MAX &&
            (surface == VK_NULL_HANDLE || presentIdx != UINT32_MAX)) {
            LUMEN_LOG_DEBUG("选中物理设备: {}, 队列族 gfx={} present={}",
                            props.deviceName, gfxIdx, presentIdx);
            physicalDevice_ = dev;
            graphicsQueueFamily_ = gfxIdx;
            presentQueueFamily_ =
                surface != VK_NULL_HANDLE ? presentIdx : gfxIdx;

            // Properties2 + Vulkan 1.1-1.4 链
            relink_properties_chain_();
            vulkan11Properties_.pNext = &vulkan12Properties_;
            vulkan12Properties_.pNext = &vulkan13Properties_;
            vulkan13Properties_.pNext = &vulkan14Properties_;
            vulkan14Properties_.pNext = nullptr;
            vkGetPhysicalDeviceProperties2(dev, &physicalDeviceProperties2_);

            // Features2 + Vulkan 1.1-1.4 链
            relink_features_chain_();
            vkGetPhysicalDeviceFeatures2(dev, &physicalDeviceFeatures2_);

            vkGetPhysicalDeviceMemoryProperties(
                dev, &physicalDeviceMemoryProperties_);
            return true;
        }
    }
    return false;
}

bool Context::create_logical_device_(VkSurfaceKHR surface) {
    float queuePriority { 1.0f };
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueFamilies { graphicsQueueFamily_,
                                        presentQueueFamily_ };

    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo info {
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO
        };
        info.queueFamilyIndex = family;
        info.queueCount = 1;
        info.pQueuePriorities = &queuePriority;
        queueCreateInfos.emplace_back(info);
    }

    std::vector<const char *> deviceExtensions {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    // Vulkan 1.1+ 使用 pNext 特性链，不再用 pEnabledFeatures
    // 仅启用需要的特性，版本化特性保持查询结果（支持则启用）
    physicalDeviceFeatures2_.features.samplerAnisotropy = VK_TRUE;
    relink_features_chain_();

    VkDeviceCreateInfo createInfo { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    createInfo.queueCreateInfoCount =
        static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pNext = &physicalDeviceFeatures2_;
    createInfo.enabledExtensionCount =
        static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    VkResult result =
        vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_);
    if (result != VK_SUCCESS) {
        LUMEN_LOG_ERROR("vkCreateDevice 失败: {}", static_cast<int>(result));
        return false;
    }

    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentQueueFamily_, 0, &presentQueue_);
    LUMEN_LOG_DEBUG("逻辑设备创建成功");
    return true;
}

const char *device_type_name(PhysicalDeviceType type) {
    switch (type) {
    case PhysicalDeviceType::Discrete: return "Discrete GPU";
    case PhysicalDeviceType::Integrated: return "Integrated GPU";
    case PhysicalDeviceType::Virtual: return "Virtual GPU";
    case PhysicalDeviceType::Cpu: return "CPU";
    default: return "Other";
    }
}

PhysicalDeviceInfo Context::physical_device_info() const {
    PhysicalDeviceInfo info {};
    if (physicalDevice_ == VK_NULL_HANDLE)
        return info;

    const auto &p = physicalDeviceProperties2_.properties;
    info.deviceName = p.deviceName;
    info.vendorId = p.vendorID;
    info.deviceId = p.deviceID;
    info.driverVersion = p.driverVersion;
    info.apiVersion = p.apiVersion;

    switch (p.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        info.deviceType = PhysicalDeviceType::Integrated;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        info.deviceType = PhysicalDeviceType::Discrete;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        info.deviceType = PhysicalDeviceType::Virtual;
        break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        info.deviceType = PhysicalDeviceType::Cpu;
        break;
    default: info.deviceType = PhysicalDeviceType::Other; break;
    }

    for (uint32_t i { 0 }; i < physicalDeviceMemoryProperties_.memoryHeapCount;
         ++i) {
        const auto &heap = physicalDeviceMemoryProperties_.memoryHeaps[i];
        if (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            info.deviceLocalMemoryBytes += heap.size;
    }
    return info;
}

void Context::wait_idle() const {
    if (device_ != VK_NULL_HANDLE)
        vkDeviceWaitIdle(device_);
}

bool Context::init_device(VkSurfaceKHR surface) {
    if (!has_instance())
        return false;
    if (device_ != VK_NULL_HANDLE)
        return true;

    if (!pick_physical_device_(surface)) {
        LUMEN_LOG_ERROR("未找到合适的物理设备");
        return false;
    }
    if (!create_logical_device_(surface))
        return false;
    auto info = physical_device_info();
    LUMEN_LOG_INFO("Vulkan 设备初始化完成: {} ({})", info.deviceName,
                   device_type_name(info.deviceType));
    return true;
}

Context::~Context() { destroy_(); }

Context::Context(Context &&other) noexcept
    : instance_ { other.instance_ }, physicalDevice_ { other.physicalDevice_ },
      device_ { other.device_ }, graphicsQueue_ { other.graphicsQueue_ },
      presentQueue_ { other.presentQueue_ },
      graphicsQueueFamily_ { other.graphicsQueueFamily_ },
      presentQueueFamily_ { other.presentQueueFamily_ },
      validationEnabled_ { other.validationEnabled_ },
      physicalDeviceProperties2_ { other.physicalDeviceProperties2_ },
      vulkan11Properties_ { other.vulkan11Properties_ },
      vulkan12Properties_ { other.vulkan12Properties_ },
      vulkan13Properties_ { other.vulkan13Properties_ },
      vulkan14Properties_ { other.vulkan14Properties_ },
      physicalDeviceFeatures2_ { other.physicalDeviceFeatures2_ },
      vulkan11Features_ { other.vulkan11Features_ },
      vulkan12Features_ { other.vulkan12Features_ },
      vulkan13Features_ { other.vulkan13Features_ },
      vulkan14Features_ { other.vulkan14Features_ },
      physicalDeviceMemoryProperties_ {
          other.physicalDeviceMemoryProperties_
      } {
    relink_properties_chain_();
    relink_features_chain_();
    other.instance_ = VK_NULL_HANDLE;
    other.physicalDevice_ = VK_NULL_HANDLE;
    other.device_ = VK_NULL_HANDLE;
    other.graphicsQueue_ = VK_NULL_HANDLE;
    other.presentQueue_ = VK_NULL_HANDLE;
}

Context &Context::operator=(Context &&other) noexcept {
    if (this == &other)
        return *this;
    destroy_();
    instance_ = other.instance_;
    physicalDevice_ = other.physicalDevice_;
    device_ = other.device_;
    graphicsQueue_ = other.graphicsQueue_;
    presentQueue_ = other.presentQueue_;
    graphicsQueueFamily_ = other.graphicsQueueFamily_;
    presentQueueFamily_ = other.presentQueueFamily_;
    validationEnabled_ = other.validationEnabled_;
    physicalDeviceProperties2_ = other.physicalDeviceProperties2_;
    vulkan11Properties_ = other.vulkan11Properties_;
    vulkan12Properties_ = other.vulkan12Properties_;
    vulkan13Properties_ = other.vulkan13Properties_;
    vulkan14Properties_ = other.vulkan14Properties_;
    physicalDeviceFeatures2_ = other.physicalDeviceFeatures2_;
    vulkan11Features_ = other.vulkan11Features_;
    vulkan12Features_ = other.vulkan12Features_;
    vulkan13Features_ = other.vulkan13Features_;
    vulkan14Features_ = other.vulkan14Features_;
    physicalDeviceMemoryProperties_ = other.physicalDeviceMemoryProperties_;
    relink_properties_chain_();
    relink_features_chain_();
    other.instance_ = VK_NULL_HANDLE;
    other.physicalDevice_ = VK_NULL_HANDLE;
    other.device_ = VK_NULL_HANDLE;
    other.graphicsQueue_ = VK_NULL_HANDLE;
    other.presentQueue_ = VK_NULL_HANDLE;
    return *this;
}

void Context::destroy_() {
    if (device_ != VK_NULL_HANDLE) {
        LUMEN_LOG_DEBUG("销毁逻辑设备");
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
        graphicsQueue_ = VK_NULL_HANDLE;
        presentQueue_ = VK_NULL_HANDLE;
    }
    physicalDevice_ = VK_NULL_HANDLE;
    if (instance_ != VK_NULL_HANDLE) {
        LUMEN_LOG_DEBUG("销毁 Vulkan instance");
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

} // namespace lumen::render
