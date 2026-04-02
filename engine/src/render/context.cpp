/**
 * @file context.cpp
 * @brief Vulkan 上下文实现：Instance / PhysicalDevice / Device / Queue / VMA
 *
 * 负责 Vulkan 核心对象的创建、查询与生命周期管理，包括：
 * - Instance 创建（含 validation layer）
 * - Physical Device 选择
 * - Logical Device 创建（队列 + 特性启用）
 * - VMA 分配器初始化
 * - Properties / Features 查询（基于 pNext 链）
 *
 * 设计特点：
 * - 使用 Vulkan 1.1+ 的 PhysicalDeviceFeatures2 / Properties2（Vulkan-Hpp）
 * - 统一通过 pNext chain 管理扩展能力
 * - 支持无 Surface（离屏渲染）
 */

#include "render/context.hpp"
#include "core/logger.hpp"
#include "platform/window.hpp"

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>

namespace lumen::render {

namespace {

/**
 * @brief 标准 Vulkan validation layer 名称
 */
const char *kValidationLayerName = "VK_LAYER_KHRONOS_validation";

/**
 * @brief 检查所请求的 validation layers 是否被系统支持
 *
 * @param layers 需要启用的 layer 名称列表
 * @return true  所有 layer 均存在
 * @return false 存在缺失
 */
bool check_validation_layer_support(const std::vector<const char *> &layers) {
    uint32_t count { 0 };
    if (vk::enumerateInstanceLayerProperties(&count, nullptr) !=
        vk::Result::eSuccess) {
        return false;
    }
    std::vector<vk::LayerProperties> available(count);
    if (count > 0 && vk::enumerateInstanceLayerProperties(
                        &count, available.data()) != vk::Result::eSuccess) {
        return false;
    }

    for (const char *name : layers) {
        bool found { false };
        for (const auto &prop : available) {
            if (strcmp(name, prop.layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 将 `from` 中的扩展名并入 `into`，忽略已存在的同名项
 */
void merge_unique_instance_extensions(std::vector<const char *> &into,
                                      const std::vector<const char *> &from) {
    for (const char *e : from) {
        if (e == nullptr) {
            continue;
        }
        bool dup { false };
        for (const char *x : into) {
            if (x != nullptr && std::strcmp(x, e) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            into.push_back(e);
        }
    }
}

} // namespace

/**
 * @brief 重新构建 PhysicalDeviceProperties2 的 pNext 链
 */
void Context::relink_properties_chain_() {
    physicalDeviceProperties2_.pNext = &vulkan11Properties_;
    vulkan11Properties_.pNext = &vulkan12Properties_;
    vulkan12Properties_.pNext = &vulkan13Properties_;
    vulkan13Properties_.pNext = &vulkan14Properties_;
    vulkan14Properties_.pNext = nullptr;
}

/**
 * @brief 重新构建 PhysicalDeviceFeatures2 的 pNext 链
 */
void Context::relink_features_chain_() {
    physicalDeviceFeatures2_.pNext = &vulkan11Features_;
    vulkan11Features_.pNext = &vulkan12Features_;
    vulkan12Features_.pNext = &vulkan13Features_;
    vulkan13Features_.pNext = &vulkan14Features_;
    vulkan14Features_.pNext = nullptr;
}

bool Context::init_instance(const ContextConfig &config) {
    if (instance_) {
        LUMEN_LOG_DEBUG("instance 已经被创建过，直接返回");
        return true;
    }

    validationEnabled_ = config.enableValidation;

    if (validationEnabled_) {
        std::vector<const char *> layers { config.instanceLayers };
        if (std::ranges::find(layers, kValidationLayerName) == layers.end()) {
            layers.emplace_back(kValidationLayerName);
        }
        if (!check_validation_layer_support(layers)) {
            LUMEN_LOG_WARN("Validation layers requested but not available");
            validationEnabled_ = false;
        }
    }

    vk::ApplicationInfo appInfo {};
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
        if (l != kValidationLayerName) {
            layers.emplace_back(l);
        }
    }

    vk::InstanceCreateInfo createInfo {};
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

    const vk::Result result =
        vk::createInstance(&createInfo, nullptr, &instance_);
    if (result != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("vk::createInstance failed: {}",
                        static_cast<int>(result));
        return false;
    }
    LUMEN_LOG_DEBUG("Vulkan instance 创建成功, validation={}",
                    validationEnabled_);
    return true;
}

bool Context::init_instance(const ContextConfig &config,
                            const platform::Window &window) {
    ContextConfig merged { config };
    merge_unique_instance_extensions(
        merged.instanceExtensions, window.get_vulkan_instance_extensions());
    return init_instance(merged);
}

bool Context::pick_physical_device_(vk::SurfaceKHR surface) {
    uint32_t count { 0 };
    if (instance_.enumeratePhysicalDevices(&count, nullptr) !=
        vk::Result::eSuccess) {
        return false;
    }
    if (count == 0) {
        return false;
    }

    std::vector<vk::PhysicalDevice> devices(count);
    if (instance_.enumeratePhysicalDevices(&count, devices.data()) !=
        vk::Result::eSuccess) {
        return false;
    }

    for (vk::PhysicalDevice dev : devices) {
        vk::PhysicalDeviceProperties props = dev.getProperties();

        auto queueFamilies = dev.getQueueFamilyProperties();

        uint32_t gfxIdx { UINT32_MAX };
        uint32_t presentIdx { UINT32_MAX };

        for (uint32_t i { 0 }; i < queueFamilies.size(); ++i) {
            if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                gfxIdx = i;
            }
            if (surface) {
                vk::Bool32 presentSupport { VK_FALSE };
                dev.getSurfaceSupportKHR(i, surface, &presentSupport);
                if (presentSupport) {
                    presentIdx = i;
                }
            }
        }

        if (gfxIdx != UINT32_MAX &&
            (!surface || presentIdx != UINT32_MAX)) {
            LUMEN_LOG_DEBUG("选中物理设备: {}, 队列族 gfx={} present={}",
                            std::string(props.deviceName.data()), gfxIdx,
                            presentIdx);
            physicalDevice_ = dev;
            graphicsQueueFamily_ = gfxIdx;
            presentQueueFamily_ = surface ? presentIdx : gfxIdx;

            relink_properties_chain_();
            dev.getProperties2(&physicalDeviceProperties2_);

            relink_features_chain_();
            dev.getFeatures2(&physicalDeviceFeatures2_);

            physicalDeviceMemoryProperties_ = dev.getMemoryProperties();
            return true;
        }
    }
    return false;
}

bool Context::create_vma_allocator_() {
    if (vmaAllocator_ != nullptr) {
        return true;
    }

    VmaVulkanFunctions vf {};
    vf.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    vf.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo aci {};
    aci.physicalDevice = physicalDevice_;
    aci.device = device_;
    aci.instance = instance_;
    aci.pVulkanFunctions = &vf;
    aci.vulkanApiVersion = physicalDeviceProperties2_.properties.apiVersion;

    const auto r = vmaCreateAllocator(&aci, &vmaAllocator_);
    if (r != VK_SUCCESS) {
        LUMEN_LOG_ERROR("vmaCreateAllocator 失败: {}", static_cast<int>(r));
        return false;
    }
    return true;
}

bool Context::create_logical_device_(vk::SurfaceKHR surface) {
    (void)surface;
    float queuePriority { 1.0F };
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueFamilies { graphicsQueueFamily_,
                                        presentQueueFamily_ };

    for (uint32_t family : uniqueFamilies) {
        vk::DeviceQueueCreateInfo info {};
        info.queueFamilyIndex = family;
        info.queueCount = 1;
        info.pQueuePriorities = &queuePriority;
        queueCreateInfos.emplace_back(info);
    }

    std::vector<const char *> deviceExtensions {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    physicalDeviceFeatures2_.features.samplerAnisotropy = VK_TRUE;
    relink_features_chain_();

    vk::DeviceCreateInfo createInfo {};
    createInfo.queueCreateInfoCount =
        static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pNext = &physicalDeviceFeatures2_;
    createInfo.enabledExtensionCount =
        static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    const vk::Result result =
        physicalDevice_.createDevice(&createInfo, nullptr, &device_);
    if (result != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("createDevice 失败: {}", static_cast<int>(result));
        return false;
    }

    graphicsQueue_ = device_.getQueue(graphicsQueueFamily_, 0);
    presentQueue_ = device_.getQueue(presentQueueFamily_, 0);
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
    if (!physicalDevice_) {
        return info;
    }

    const auto &p = physicalDeviceProperties2_.properties;
    info.deviceName = std::string(p.deviceName.data());
    info.vendorId = p.vendorID;
    info.deviceId = p.deviceID;
    info.driverVersion = p.driverVersion;
    info.apiVersion = p.apiVersion;

    switch (p.deviceType) {
    case vk::PhysicalDeviceType::eIntegratedGpu:
        info.deviceType = PhysicalDeviceType::Integrated;
        break;
    case vk::PhysicalDeviceType::eDiscreteGpu:
        info.deviceType = PhysicalDeviceType::Discrete;
        break;
    case vk::PhysicalDeviceType::eVirtualGpu:
        info.deviceType = PhysicalDeviceType::Virtual;
        break;
    case vk::PhysicalDeviceType::eCpu:
        info.deviceType = PhysicalDeviceType::Cpu;
        break;
    default: info.deviceType = PhysicalDeviceType::Other; break;
    }

    for (uint32_t i { 0 }; i < physicalDeviceMemoryProperties_.memoryHeapCount;
         ++i) {
        const auto &heap = physicalDeviceMemoryProperties_.memoryHeaps[i];
        if (heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal) {
            info.deviceLocalMemoryBytes += heap.size;
        }
    }
    return info;
}

void Context::wait_idle() const {
    if (device_) {
        device_.waitIdle();
    }
}

bool Context::init_device(vk::SurfaceKHR surface) {
    if (!has_instance())
        return false;
    if (device_)
        return true;

    if (!pick_physical_device_(surface)) {
        LUMEN_LOG_ERROR("未找到合适的物理设备");
        return false;
    }
    if (!create_logical_device_(surface))
        return false;
    if (!create_vma_allocator_()) {
        device_.destroy(nullptr);
        device_ = nullptr;
        graphicsQueue_ = nullptr;
        presentQueue_ = nullptr;
        return false;
    }
    auto devInfo = physical_device_info();
    LUMEN_LOG_INFO("Vulkan 设备初始化完成: {} ({})", devInfo.deviceName,
                   device_type_name(devInfo.deviceType));
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
      physicalDeviceMemoryProperties_ { other.physicalDeviceMemoryProperties_ },
      vmaAllocator_ { other.vmaAllocator_ } {
    relink_properties_chain_();
    relink_features_chain_();
    other.vmaAllocator_ = nullptr;
    other.instance_ = nullptr;
    other.physicalDevice_ = nullptr;
    other.device_ = nullptr;
    other.graphicsQueue_ = nullptr;
    other.presentQueue_ = nullptr;
}

Context &Context::operator=(Context &&other) noexcept {
    if (this == &other) {
        return *this;
    }
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
    vmaAllocator_ = other.vmaAllocator_;
    relink_properties_chain_();
    relink_features_chain_();
    other.vmaAllocator_ = nullptr;
    other.instance_ = nullptr;
    other.physicalDevice_ = nullptr;
    other.device_ = nullptr;
    other.graphicsQueue_ = nullptr;
    other.presentQueue_ = nullptr;
    return *this;
}

void Context::destroy_() {
    if (vmaAllocator_ != nullptr) {
        LUMEN_LOG_DEBUG("销毁 vma");
        vmaDestroyAllocator(vmaAllocator_);
        vmaAllocator_ = nullptr;
    }
    if (device_) {
        LUMEN_LOG_DEBUG("销毁逻辑设备");
        device_.destroy(nullptr);
        device_ = nullptr;
        graphicsQueue_ = nullptr;
        presentQueue_ = nullptr;
    }
    physicalDevice_ = nullptr;
    if (instance_) {
        LUMEN_LOG_DEBUG("销毁 Vulkan instance");
        instance_.destroy(nullptr);
        instance_ = nullptr;
    }
}

} // namespace lumen::render
