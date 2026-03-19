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

        bool check_validation_layer_support(
            const std::vector<const char *> &layers) {
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

    bool Context::init_instance(const ContextConfig &config) {
        if (instance_ != VK_NULL_HANDLE)
            return true;

        validationEnabled_ = config.enableValidation;

        if (validationEnabled_) {
            std::vector<const char *> layers { config.instanceLayers };
            if (std::find(layers.begin(), layers.end(), kValidationLayerName) ==
                layers.end()) {
                layers.push_back(kValidationLayerName);
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
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        std::vector<const char *> layers;
        if (validationEnabled_) {
            layers.push_back(kValidationLayerName);
        }
        for (const char *l : config.instanceLayers) {
            if (l != kValidationLayerName)
                layers.push_back(l);
        }

        VkInstanceCreateInfo createInfo {
            VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
        };
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount =
            static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
        createInfo.ppEnabledLayerNames =
            layers.empty() ? nullptr : layers.data();

        VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
        if (result != VK_SUCCESS) {
            LUMEN_LOG_ERROR("vkCreateInstance failed: {}",
                            static_cast<int>(result));
            return false;
        }
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
            std::vector<VkQueueFamilyProperties> queueFamilies(
                queueFamilyCount);
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
                physicalDevice_ = dev;
                graphicsQueueFamily_ = gfxIdx;
                presentQueueFamily_ =
                    surface != VK_NULL_HANDLE ? presentIdx : gfxIdx;
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
            queueCreateInfos.push_back(info);
        }

        std::vector<const char *> deviceExtensions {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        VkPhysicalDeviceFeatures features {};
        features.samplerAnisotropy = VK_TRUE;

        VkDeviceCreateInfo createInfo { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        createInfo.queueCreateInfoCount =
            static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();
        createInfo.pEnabledFeatures = &features;
        createInfo.enabledExtensionCount =
            static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        VkResult result =
            vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_);
        if (result != VK_SUCCESS)
            return false;

        vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, presentQueueFamily_, 0, &presentQueue_);
        return true;
    }

    bool Context::init_device(VkSurfaceKHR surface) {
        if (!has_instance())
            return false;
        if (device_ != VK_NULL_HANDLE)
            return true;

        if (!pick_physical_device_(surface))
            return false;
        if (!create_logical_device_(surface))
            return false;
        return true;
    }

    Context::~Context() { destroy_(); }

    Context::Context(Context &&other) noexcept
        : instance_ { other.instance_ },
          physicalDevice_ { other.physicalDevice_ }, device_ { other.device_ },
          graphicsQueue_ { other.graphicsQueue_ },
          presentQueue_ { other.presentQueue_ },
          graphicsQueueFamily_ { other.graphicsQueueFamily_ },
          presentQueueFamily_ { other.presentQueueFamily_ },
          validationEnabled_ { other.validationEnabled_ } {
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
        other.instance_ = VK_NULL_HANDLE;
        other.physicalDevice_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
        other.graphicsQueue_ = VK_NULL_HANDLE;
        other.presentQueue_ = VK_NULL_HANDLE;
        return *this;
    }

    void Context::destroy_() {
        if (device_ != VK_NULL_HANDLE) {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
            graphicsQueue_ = VK_NULL_HANDLE;
            presentQueue_ = VK_NULL_HANDLE;
        }
        physicalDevice_ = VK_NULL_HANDLE;
        if (instance_ != VK_NULL_HANDLE) {
            vkDestroyInstance(instance_, nullptr);
            instance_ = VK_NULL_HANDLE;
        }
    }

} // namespace lumen::render
