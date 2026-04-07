/**
 * @file context.cpp
 * @brief RHI Vulkan 上下文：Instance、设备、队列、Surface、Validation、VMA
 */

#include "rhi/context.hpp"

#include "core/log/logger.hpp"
#include "platform/window.hpp"
#include "rhi/vulkan.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <string>

namespace rhi {

namespace {

constexpr const char *kValidationLayerName = "VK_LAYER_KHRONOS_validation";

bool check_validation_layer_support(const std::vector<const char *> &layers) {
    std::uint32_t count = 0;
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
        const auto found =
            std::find_if(available.begin(), available.end(),
                         [name](const vk::LayerProperties &p) {
                             return std::strcmp(name, p.layerName) == 0;
                         });
        if (found == available.end()) {
            return false;
        }
    }
    return true;
}

void merge_unique_extension(std::vector<const char *> &into, const char *ext) {
    if (ext == nullptr) {
        return;
    }
    for (const char *x : into) {
        if (x != nullptr && std::strcmp(x, ext) == 0) {
            return;
        }
    }
    into.push_back(ext);
}

void merge_unique_extensions(std::vector<const char *> &into,
                             const std::vector<const char *> &from) {
    for (const char *e : from) {
        merge_unique_extension(into, e);
    }
}

vk::Bool32 VKAPI_PTR debug_utils_callback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
    vk::DebugUtilsMessageTypeFlagsEXT /*type*/,
    const vk::DebugUtilsMessengerCallbackDataEXT *data, void * /*ud*/) {
    if (data != nullptr && data->pMessage != nullptr) {
        if (severity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
            LUMEN_LOG_WARN("[Vulkan validation] {}",
                           static_cast<const char *>(data->pMessage));
        } else {
            LUMEN_LOG_DEBUG("[Vulkan validation] {}",
                            static_cast<const char *>(data->pMessage));
        }
    }
    return vk::False;
}

} // namespace

Context::Context(Context &&other) noexcept
    : instance_(other.instance_), debug_messenger_(other.debug_messenger_),
      physical_device_(other.physical_device_), device_(other.device_),
      graphics_queue_(other.graphics_queue_),
      compute_queue_(other.compute_queue_),
      transfer_queue_(other.transfer_queue_),
      present_queue_(other.present_queue_),
      graphics_queue_family_(other.graphics_queue_family_),
      compute_queue_family_(other.compute_queue_family_),
      transfer_queue_family_(other.transfer_queue_family_),
      present_queue_family_(other.present_queue_family_),
      surface_(other.surface_), allocator_(other.allocator_),
      desc_(std::move(other.desc_)),
      validation_enabled_(other.validation_enabled_),
      required_device_extensions_(std::move(other.required_device_extensions_)),
      physical_device_properties2_(other.physical_device_properties2_),
      vulkan_11_properties_(other.vulkan_11_properties_),
      vulkan_12_properties_(other.vulkan_12_properties_),
      vulkan_13_properties_(other.vulkan_13_properties_),
      vulkan_14_properties_(other.vulkan_14_properties_),
      physical_device_features2_(other.physical_device_features2_),
      vulkan_11_features_(other.vulkan_11_features_),
      vulkan_12_features_(other.vulkan_12_features_),
      vulkan_13_features_(other.vulkan_13_features_),
      vulkan_14_features_(other.vulkan_14_features_),
      accel_features_(other.accel_features_),
      rt_pipeline_features_(other.rt_pipeline_features_) {
    other.debug_messenger_ = nullptr;
    other.instance_ = nullptr;
    other.physical_device_ = nullptr;
    other.device_ = nullptr;
    other.graphics_queue_ = nullptr;
    other.compute_queue_ = nullptr;
    other.transfer_queue_ = nullptr;
    other.present_queue_ = nullptr;
    other.surface_ = nullptr;
    other.allocator_ = nullptr;
    other.graphics_queue_family_ = UINT32_MAX;
    other.compute_queue_family_ = UINT32_MAX;
    other.transfer_queue_family_ = UINT32_MAX;
    other.present_queue_family_ = UINT32_MAX;
    relink_properties_chain_();
    relink_features_chain_();
}

Context &Context::operator=(Context &&other) noexcept {
    if (this == &other) {
        return *this;
    }
    shutdown();
    instance_ = other.instance_;
    debug_messenger_ = other.debug_messenger_;
    physical_device_ = other.physical_device_;
    device_ = other.device_;
    graphics_queue_ = other.graphics_queue_;
    compute_queue_ = other.compute_queue_;
    transfer_queue_ = other.transfer_queue_;
    present_queue_ = other.present_queue_;
    graphics_queue_family_ = other.graphics_queue_family_;
    compute_queue_family_ = other.compute_queue_family_;
    transfer_queue_family_ = other.transfer_queue_family_;
    present_queue_family_ = other.present_queue_family_;
    surface_ = other.surface_;
    allocator_ = other.allocator_;
    desc_ = std::move(other.desc_);
    validation_enabled_ = other.validation_enabled_;
    required_device_extensions_ = std::move(other.required_device_extensions_);
    physical_device_properties2_ = other.physical_device_properties2_;
    vulkan_11_properties_ = other.vulkan_11_properties_;
    vulkan_12_properties_ = other.vulkan_12_properties_;
    vulkan_13_properties_ = other.vulkan_13_properties_;
    vulkan_14_properties_ = other.vulkan_14_properties_;
    physical_device_features2_ = other.physical_device_features2_;
    vulkan_11_features_ = other.vulkan_11_features_;
    vulkan_12_features_ = other.vulkan_12_features_;
    vulkan_13_features_ = other.vulkan_13_features_;
    vulkan_14_features_ = other.vulkan_14_features_;
    accel_features_ = other.accel_features_;
    rt_pipeline_features_ = other.rt_pipeline_features_;
    other.debug_messenger_ = nullptr;
    other.instance_ = nullptr;
    other.physical_device_ = nullptr;
    other.device_ = nullptr;
    other.graphics_queue_ = nullptr;
    other.compute_queue_ = nullptr;
    other.transfer_queue_ = nullptr;
    other.present_queue_ = nullptr;
    other.surface_ = nullptr;
    other.allocator_ = nullptr;
    other.graphics_queue_family_ = UINT32_MAX;
    other.compute_queue_family_ = UINT32_MAX;
    other.transfer_queue_family_ = UINT32_MAX;
    other.present_queue_family_ = UINT32_MAX;
    relink_properties_chain_();
    relink_features_chain_();
    return *this;
}

Context::~Context() { shutdown(); }

void Context::relink_properties_chain_() {
    physical_device_properties2_.pNext = &vulkan_11_properties_;
    vulkan_11_properties_.pNext = &vulkan_12_properties_;
    vulkan_12_properties_.pNext = &vulkan_13_properties_;
    vulkan_13_properties_.pNext = &vulkan_14_properties_;
    vulkan_14_properties_.pNext = nullptr;
}

void Context::relink_features_chain_() {
    physical_device_features2_.pNext = &vulkan_11_features_;
    vulkan_11_features_.pNext = &vulkan_12_features_;
    vulkan_12_features_.pNext = &vulkan_13_features_;
    vulkan_13_features_.pNext = &vulkan_14_features_;
    if (desc_.enableRayTracing) {
        vulkan_14_features_.pNext = &accel_features_;
        accel_features_.pNext = &rt_pipeline_features_;
        rt_pipeline_features_.pNext = nullptr;
    } else {
        vulkan_14_features_.pNext = nullptr;
    }
}

void Context::build_required_device_extensions_() {
    required_device_extensions_.clear();
    merge_unique_extensions(required_device_extensions_,
                            desc_.deviceExtensions);
    if (surface_) {
        merge_unique_extension(required_device_extensions_,
                               VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    if (desc_.enableRayTracing) {
        merge_unique_extension(required_device_extensions_,
                               VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        merge_unique_extension(required_device_extensions_,
                               VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        merge_unique_extension(required_device_extensions_,
                               VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    }
}

bool Context::check_device_extensions_(vk::PhysicalDevice device) const {
    const auto ext = device.enumerateDeviceExtensionProperties(nullptr);
    if (ext.result != vk::Result::eSuccess) {
        return false;
    }
    const std::vector<vk::ExtensionProperties> &props = ext.value;
    std::vector<std::string> names;
    names.reserve(props.size());
    for (const auto &p : props) {
        names.emplace_back(p.extensionName.data());
    }
    for (const char *req : required_device_extensions_) {
        if (req == nullptr) {
            continue;
        }
        const auto it =
            std::find_if(names.begin(), names.end(),
                         [req](const std::string &n) { return n == req; });
        if (it == names.end()) {
            return false;
        }
    }
    return true;
}

void Context::find_queue_families_(vk::PhysicalDevice device) {
    graphics_queue_family_ = UINT32_MAX;
    compute_queue_family_ = UINT32_MAX;
    transfer_queue_family_ = UINT32_MAX;
    present_queue_family_ = UINT32_MAX;

    const std::vector<vk::QueueFamilyProperties> families =
        device.getQueueFamilyProperties();

    for (std::uint32_t i = 0; i < families.size(); ++i) {
        const vk::QueueFlags flags = families[i].queueFlags;
        if (graphics_queue_family_ == UINT32_MAX &&
            (flags & vk::QueueFlagBits::eGraphics)) {
            graphics_queue_family_ = i;
        }
        if (compute_queue_family_ == UINT32_MAX &&
            (flags & vk::QueueFlagBits::eCompute)) {
            compute_queue_family_ = i;
        }
        if (transfer_queue_family_ == UINT32_MAX &&
            (flags & vk::QueueFlagBits::eTransfer)) {
            transfer_queue_family_ = i;
        }
        if (surface_) {
            vk::Bool32 present = VK_FALSE;
            const vk::Result pr =
                device.getSurfaceSupportKHR(i, surface_, &present);
            if (pr != vk::Result::eSuccess) {
                continue;
            }
            if (present != VK_FALSE && present_queue_family_ == UINT32_MAX) {
                present_queue_family_ = i;
            }
        }
    }

    if (compute_queue_family_ == UINT32_MAX) {
        compute_queue_family_ = graphics_queue_family_;
    }
    if (transfer_queue_family_ == UINT32_MAX) {
        transfer_queue_family_ = graphics_queue_family_;
    }
    if (!surface_) {
        present_queue_family_ = graphics_queue_family_;
    }
}

bool Context::is_device_suitable_(vk::PhysicalDevice device) {
    find_queue_families_(device);
    if (graphics_queue_family_ == UINT32_MAX) {
        return false;
    }
    if (surface_ && present_queue_family_ == UINT32_MAX) {
        return false;
    }
    return check_device_extensions_(device);
}

int Context::score_physical_device_(vk::PhysicalDevice device) const {
    const vk::PhysicalDeviceProperties props = device.getProperties();
    int score = 0;
    switch (props.deviceType) {
    case vk::PhysicalDeviceType::eDiscreteGpu: score += 1000; break;
    case vk::PhysicalDeviceType::eIntegratedGpu: score += 100; break;
    case vk::PhysicalDeviceType::eVirtualGpu: score += 50; break;
    case vk::PhysicalDeviceType::eCpu: score += 10; break;
    default: score += 1; break;
    }

    const vk::PhysicalDeviceMemoryProperties mem = device.getMemoryProperties();
    vk::DeviceSize local_bytes = 0;
    for (std::uint32_t i = 0; i < mem.memoryHeapCount; ++i) {
        if (mem.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal) {
            local_bytes += mem.memoryHeaps[i].size;
        }
    }
    const auto gb =
        static_cast<std::uint64_t>(local_bytes / (1024ULL * 1024ULL * 1024ULL));
    score += static_cast<int>(std::min<std::uint64_t>(gb, 64ULL));
    return score;
}

bool Context::create_instance_() {
    validation_enabled_ = desc_.enableValidation;
    if (validation_enabled_) {
        std::vector<const char *> layers { kValidationLayerName };
        if (!check_validation_layer_support(layers)) {
            LUMEN_LOG_WARN("Validation layers 不可用，已关闭 validation");
            validation_enabled_ = false;
        }
    }

    vk::ApplicationInfo app_info {};
    app_info.pApplicationName = "Lumen";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "Lumen";
    app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.apiVersion = desc_.apiVersion;

    std::vector<const char *> extensions;
    merge_unique_extensions(extensions, desc_.instanceExtensions);
    if (desc_.enableSurface) {
        merge_unique_extension(extensions, VK_KHR_SURFACE_EXTENSION_NAME);
    }
    if (validation_enabled_) {
        merge_unique_extension(extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char *> layers;
    if (validation_enabled_) {
        layers.push_back(kValidationLayerName);
    }

    vk::InstanceCreateInfo ci {};
    ci.pApplicationInfo = &app_info;
    ci.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

    const vk::Result r = vk::createInstance(&ci, nullptr, &instance_);
    if (r != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("vk::createInstance 失败: {}", static_cast<int>(r));
        return false;
    }
    LUMEN_LOG_DEBUG("RHI: Vulkan Instance 创建成功 (validation={})",
                    validation_enabled_);
    return true;
}

bool Context::setup_debug_messenger_() {
    if (!validation_enabled_ || !instance_) {
        return true;
    }

    using Severity = vk::DebugUtilsMessageSeverityFlagBitsEXT;
    using Type = vk::DebugUtilsMessageTypeFlagBitsEXT;
    vk::DebugUtilsMessengerCreateInfoEXT dci {};
    dci.messageSeverity = Severity::eWarning | Severity::eError;
    dci.messageType = Type::eGeneral | Type::eValidation | Type::ePerformance;
    dci.pfnUserCallback = &debug_utils_callback;

    // const vk::Result r = instance_.createDebugUtilsMessengerEXT(
    //     &dci, nullptr, &debug_messenger_);
    // if (r != vk::Result::eSuccess) {
    //     LUMEN_LOG_ERROR("createDebugUtilsMessengerEXT 失败: {}",
    //                     static_cast<int>(r));
    //     return false;
    // }
    return true;
}

bool Context::create_surface_() {
    surface_ = nullptr;
    if (!desc_.enableSurface || desc_.windowHandle == nullptr) {
        return true;
    }
    auto *win = static_cast<lumen::platform::Window *>(desc_.windowHandle);
    if (!win->is_valid()) {
        LUMEN_LOG_ERROR("ContextDesc::windowHandle 指向无效 Window");
        return false;
    }
    surface_ = win->create_vulkan_surface(instance_);
    return static_cast<bool>(surface_);
}

bool Context::pick_physical_device_() {
    std::uint32_t count = 0;
    if (instance_.enumeratePhysicalDevices(&count, nullptr) !=
        vk::Result::eSuccess) {
        return false;
    }
    if (count == 0) {
        LUMEN_LOG_ERROR("未找到任何 Vulkan 物理设备");
        return false;
    }
    std::vector<vk::PhysicalDevice> devices(count);
    if (instance_.enumeratePhysicalDevices(&count, devices.data()) !=
        vk::Result::eSuccess) {
        return false;
    }

    vk::PhysicalDevice best {};
    int best_score = std::numeric_limits<int>::min();
    for (vk::PhysicalDevice dev : devices) {
        if (!is_device_suitable_(dev)) {
            continue;
        }
        const int s = score_physical_device_(dev);
        if (s > best_score) {
            best_score = s;
            best = dev;
        }
    }

    if (!best) {
        LUMEN_LOG_ERROR("没有满足要求的物理设备");
        return false;
    }

    physical_device_ = best;
    find_queue_families_(physical_device_);

    relink_properties_chain_();
    physical_device_.getProperties2(&physical_device_properties2_);

    relink_features_chain_();
    physical_device_.getFeatures2(&physical_device_features2_);

    LUMEN_LOG_DEBUG(
        "RHI: 选用物理设备 \"{}\"",
        std::string(physical_device_properties2_.properties.deviceName.data()));
    return true;
}

bool Context::create_device_() {
    float priority = 1.0F;
    std::set<std::uint32_t> unique_families { graphics_queue_family_,
                                              compute_queue_family_,
                                              transfer_queue_family_,
                                              present_queue_family_ };

    std::vector<vk::DeviceQueueCreateInfo> queue_cis;
    queue_cis.reserve(unique_families.size());
    for (std::uint32_t family : unique_families) {
        if (family == UINT32_MAX) {
            continue;
        }
        vk::DeviceQueueCreateInfo qci {};
        qci.queueFamilyIndex = family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        queue_cis.push_back(qci);
    }

    physical_device_features2_.features.samplerAnisotropy = vk::True;

    if (desc_.enableRayTracing) {
        if (accel_features_.accelerationStructure == VK_FALSE ||
            rt_pipeline_features_.rayTracingPipeline == VK_FALSE) {
            LUMEN_LOG_ERROR("启用光追但当前 GPU 或驱动不支持对应特性");
            return false;
        }
        vulkan_12_features_.bufferDeviceAddress = vk::True;
        accel_features_.accelerationStructure = vk::True;
        rt_pipeline_features_.rayTracingPipeline = vk::True;
    }

    relink_features_chain_();

    vk::DeviceCreateInfo dci {};
    dci.queueCreateInfoCount = static_cast<std::uint32_t>(queue_cis.size());
    dci.pQueueCreateInfos = queue_cis.data();
    dci.pNext = &physical_device_features2_;
    dci.enabledExtensionCount =
        static_cast<std::uint32_t>(required_device_extensions_.size());
    dci.ppEnabledExtensionNames = required_device_extensions_.data();

    const vk::Result r = physical_device_.createDevice(&dci, nullptr, &device_);
    if (r != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("createDevice 失败: {}", static_cast<int>(r));
        return false;
    }

    graphics_queue_ = device_.getQueue(graphics_queue_family_, 0);
    compute_queue_ = device_.getQueue(compute_queue_family_, 0);
    transfer_queue_ = device_.getQueue(transfer_queue_family_, 0);
    present_queue_ = device_.getQueue(present_queue_family_, 0);
    return true;
}

bool Context::create_allocator_() {
    if (allocator_ != nullptr) {
        return true;
    }

    VmaVulkanFunctions vf {};
    vf.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    vf.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo aci {};
    aci.physicalDevice = physical_device_;
    aci.device = device_;
    aci.instance = instance_;
    aci.pVulkanFunctions = &vf;
    aci.vulkanApiVersion = physical_device_properties2_.properties.apiVersion;

    const VkResult vr = vmaCreateAllocator(&aci, &allocator_);
    if (vr != VK_SUCCESS) {
        LUMEN_LOG_ERROR("vmaCreateAllocator 失败: {}", static_cast<int>(vr));
        return false;
    }
    return true;
}

bool Context::init(const ContextDesc &desc) {
    shutdown();
    desc_ = desc;

    if (!create_instance_()) {
        shutdown();
        return false;
    }
    if (!setup_debug_messenger_()) {
        shutdown();
        return false;
    }
    if (!create_surface_()) {
        shutdown();
        return false;
    }

    if (desc_.enableSurface && desc_.windowHandle != nullptr && !surface_) {
        LUMEN_LOG_ERROR("需要 Surface 但创建失败");
        shutdown();
        return false;
    }

    build_required_device_extensions_();

    if (!pick_physical_device_()) {
        shutdown();
        return false;
    }
    if (!create_device_()) {
        shutdown();
        return false;
    }
    if (!create_allocator_()) {
        shutdown();
        return false;
    }

    LUMEN_LOG_INFO("RHI Context 初始化完成 (api=0x{:X}, surface={}, rt={})",
                   desc_.apiVersion, static_cast<bool>(surface_),
                   desc_.enableRayTracing);
    return true;
}

void Context::shutdown() {
    if (allocator_ != nullptr) {
        vmaDestroyAllocator(allocator_);
        allocator_ = nullptr;
    }
    if (device_) {
        device_.destroy(nullptr);
        device_ = nullptr;
        graphics_queue_ = nullptr;
        compute_queue_ = nullptr;
        transfer_queue_ = nullptr;
        present_queue_ = nullptr;
    }
    physical_device_ = nullptr;

    if (instance_) {
        if (debug_messenger_) {
            // instance_.destroyDebugUtilsMessengerEXT(debug_messenger_, nullptr);
            debug_messenger_ = nullptr;
        }
        if (surface_) {
            instance_.destroySurfaceKHR(surface_, nullptr);
            surface_ = nullptr;
        }
        instance_.destroy(nullptr);
        instance_ = nullptr;
    }

    graphics_queue_family_ = UINT32_MAX;
    compute_queue_family_ = UINT32_MAX;
    transfer_queue_family_ = UINT32_MAX;
    present_queue_family_ = UINT32_MAX;
    required_device_extensions_.clear();
}

void Context::wait_idle() const {
    if (device_) {
        const vk::Result r = device_.waitIdle();
        if (r != vk::Result::eSuccess) {
            LUMEN_LOG_WARN("device_.waitIdle 返回 {}", static_cast<int>(r));
        }
    }
}

} // namespace rhi
