/**
 * @file context.hpp
 * @brief Vulkan 1.3 实例 / 设备 / 交换链 / VMA 的轻量封装 `Context`。
 */

#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace vulkan {

/**
 * @brief 管理 `VkInstance`、`VkDevice`、交换链与
 * `VmaAllocator`（表面在实例创建后由回调创建）；含 graphics / present / compute 队列。
 *
 * @note 通过 `create()` 或 `ContextBuilder::build()` 取得 `std::unique_ptr<Context>`；移动 /
 * 复制已禁用。
 * @note 实例扩展（含平台 surface）须由调用方传入，例如 SDL 的
 * `SDL_Vulkan_GetInstanceExtensions`。
 * @note `surfaceFromInstance` 在 `vkCreateInstance` 成功之后调用；返回的
 * surface 由 `Context` 析构时销毁。
 */
class Context final {
public:
    /**
     * @param appName 应用名（`VkApplicationInfo`）。
     * @param appVersion `VK_MAKE_API_VERSION` 风格的应用版本。
     * @param instanceExtensions 须包含 `VK_KHR_surface` 及平台 surface 扩展（如
     * `Window::get_vulkan_instance_extensions()` 的返回值）。
     * @param windowWidth / windowHeight 初始交换链范围（会受 surface
     * capabilities 钳制）。
     * @param surfaceFromInstance 在实例就绪后创建 `VkSurfaceKHR`（如
     * `SDL_Vulkan_CreateSurface`）。
     * @param enableValidation 是否启用校验层与 `VK_EXT_debug_utils`。
     */
    [[nodiscard]] static std::expected<std::unique_ptr<Context>, std::string>
    create(std::string_view appName, std::uint32_t appVersion,
           const std::vector<const char *> &instanceExtensions,
           std::uint32_t windowWidth, std::uint32_t windowHeight,
           const std::function<VkSurfaceKHR(VkInstance)> &surfaceFromInstance,
           bool enableValidation = true);

    ~Context();

    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;
    Context(Context &&) = delete;
    Context &operator=(Context &&) = delete;

    /// 窗口尺寸变化时重建交换链（会 `vkDeviceWaitIdle`）。
    [[nodiscard]] std::expected<void, std::string>
    recreate_swapchain(std::uint32_t width, std::uint32_t height);

    [[nodiscard]] VkInstance instance() const noexcept { return vkInstance_; }
    [[nodiscard]] VkPhysicalDevice physical_device() const noexcept {
        return physicalDevice_;
    }
    [[nodiscard]] VkDevice device() const noexcept { return vkDevice_; }
    [[nodiscard]] VkQueue graphics_queue() const noexcept {
        return graphicsQueue_;
    }
    [[nodiscard]] VkQueue present_queue() const noexcept {
        return presentQueue_;
    }
    [[nodiscard]] std::uint32_t graphics_queue_family() const noexcept {
        return graphicsQueueFamily_;
    }
    [[nodiscard]] std::uint32_t present_queue_family() const noexcept {
        return presentQueueFamily_;
    }
    [[nodiscard]] VkQueue compute_queue() const noexcept {
        return computeQueue_;
    }
    [[nodiscard]] std::uint32_t compute_queue_family() const noexcept {
        return computeQueueFamily_;
    }
    [[nodiscard]] VkSurfaceKHR surface() const noexcept { return vkSurface_; }
    [[nodiscard]] VmaAllocator allocator() const noexcept {
        return vmaAllocator_;
    }
    [[nodiscard]] VkFormat depth_format() const noexcept {
        return depthFormat_;
    }

    [[nodiscard]] VkSwapchainKHR swapchain() const noexcept {
        return vkSwapchain_;
    }
    [[nodiscard]] VkFormat swapchain_format() const noexcept {
        return swapchainFormat_;
    }
    [[nodiscard]] std::uint32_t swapchain_width() const noexcept {
        return swapchainWidth_;
    }
    [[nodiscard]] std::uint32_t swapchain_height() const noexcept {
        return swapchainHeight_;
    }
    [[nodiscard]] const std::vector<VkImageView> &
    swapchain_image_views() const noexcept {
        return swapchainImageViews_;
    }
    /// 与 `swapchain_image_views()` 一一对应；供 `RenderGraph` 等在每帧 `acquire`
    /// 后做 layout 屏障或调试。
    [[nodiscard]] const std::vector<VkImage> &
    swapchain_images() const noexcept {
        return swapchainImages_;
    }

private:
    Context() = default;

    [[nodiscard]] std::expected<void, std::string>
    init(std::string_view appName, std::uint32_t appVersion,
         const std::function<VkSurfaceKHR(VkInstance)> &surfaceFromInstance,
         const std::vector<const char *> &instanceExtensions,
         std::uint32_t windowWidth, std::uint32_t windowHeight,
         bool enableValidation);

    [[nodiscard]] std::expected<void, std::string>
    create_instance(std::string_view appName, std::uint32_t appVersion,
                    const std::vector<const char *> &instanceExtensions);
    void setup_debug_messenger();
    [[nodiscard]] std::expected<void, std::string> pick_physical_device();
    [[nodiscard]] std::expected<void, std::string> create_device();
    [[nodiscard]] std::expected<void, std::string> create_vma_allocator();
    [[nodiscard]] std::expected<void, std::string>
    create_swapchain(std::uint32_t width, std::uint32_t height,
                     VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);
    void cleanup_swapchain() noexcept;

    [[nodiscard]] static std::optional<std::uint32_t>
    find_graphics_queue_family(VkPhysicalDevice physicalDevice);
    [[nodiscard]] static std::optional<std::uint32_t>
    find_present_queue_family(VkPhysicalDevice physicalDevice,
                              VkSurfaceKHR surface);
    /// 优先与 @p graphicsFamily 相同且带 `VK_QUEUE_COMPUTE_BIT`，否则任选一计算族。
    [[nodiscard]] static std::optional<std::uint32_t>
    find_compute_queue_family(VkPhysicalDevice physicalDevice,
                              std::uint32_t graphicsFamily);
    [[nodiscard]] static VkFormat
    find_supported_depth_format(VkPhysicalDevice physicalDevice,
                                const std::vector<VkFormat> &candidates);

    static VKAPI_ATTR VkBool32 VKAPI_CALL
    debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                   VkDebugUtilsMessageTypeFlagsEXT messageType,
                   const VkDebugUtilsMessengerCallbackDataEXT *callbackData,
                   void *userData);

    VkInstance vkInstance_ { VK_NULL_HANDLE };
    VkDebugUtilsMessengerEXT debugMessenger_ { VK_NULL_HANDLE };
    VkPhysicalDevice physicalDevice_ { VK_NULL_HANDLE };
    VkDevice vkDevice_ { VK_NULL_HANDLE };
    VkQueue graphicsQueue_ { VK_NULL_HANDLE };
    VkQueue presentQueue_ { VK_NULL_HANDLE };
    VkQueue computeQueue_ { VK_NULL_HANDLE };
    std::uint32_t graphicsQueueFamily_ { UINT32_MAX };
    std::uint32_t presentQueueFamily_ { UINT32_MAX };
    std::uint32_t computeQueueFamily_ { UINT32_MAX };
    VkSurfaceKHR vkSurface_ { VK_NULL_HANDLE };
    VkFormat depthFormat_ { VK_FORMAT_UNDEFINED };

    VmaAllocator vmaAllocator_ { VK_NULL_HANDLE };

    VkSwapchainKHR vkSwapchain_ { VK_NULL_HANDLE };
    VkFormat swapchainFormat_ { VK_FORMAT_UNDEFINED };
    std::uint32_t swapchainWidth_ { 0 };
    std::uint32_t swapchainHeight_ { 0 };
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;

    bool enableValidation_ { false };
};

/**
 * @brief 流式配置并创建 `Context`；扩展名在内部以 `std::string` 持有，调用 `build()`
 * 前无需保持外部指针有效。
 */
class ContextBuilder final {
public:
    ContextBuilder() = default;

    ContextBuilder &set_application_name(std::string name) {
        appName_ = std::move(name);
        return *this;
    }

    ContextBuilder &set_application_version(std::uint32_t version) {
        appVersion_ = version;
        return *this;
    }

    /// 拷贝扩展名字符串（适合 SDL 等返回的 `const char *` 列表）。
    ContextBuilder &
    set_instance_extensions(const std::vector<const char *> &extensions) {
        extensionNames_.clear();
        extensionNames_.reserve(extensions.size());
        for (const char *const ext : extensions) {
            if (ext != nullptr) {
                extensionNames_.emplace_back(ext);
            }
        }
        return *this;
    }

    ContextBuilder &
    set_instance_extensions(std::vector<std::string> extensions) {
        extensionNames_ = std::move(extensions);
        return *this;
    }

    ContextBuilder &add_instance_extension(std::string extensionName) {
        extensionNames_.push_back(std::move(extensionName));
        return *this;
    }

    ContextBuilder &set_initial_size(std::uint32_t width, std::uint32_t height) {
        initialWidth_ = width;
        initialHeight_ = height;
        return *this;
    }

    /// 必填：`VkInstance` 创建成功后用于创建 `VkSurfaceKHR`（如
    /// `SDL_Vulkan_CreateSurface`）。
    ContextBuilder &set_surface_from_instance(
        std::function<VkSurfaceKHR(VkInstance)> callback) {
        surfaceFromInstance_ = std::move(callback);
        return *this;
    }

    ContextBuilder &set_enable_validation(bool enable) {
        enableValidation_ = enable;
        return *this;
    }

    [[nodiscard]] std::expected<std::unique_ptr<Context>, std::string> build() {
        if (!surfaceFromInstance_.has_value()) {
            return std::unexpected(
                std::string("ContextBuilder: set_surface_from_instance not called"));
        }
        if (initialWidth_ == 0U || initialHeight_ == 0U) {
            return std::unexpected(
                std::string("ContextBuilder: initial width/height must be non-zero"));
        }
        std::vector<const char *> extPtrs;
        extPtrs.reserve(extensionNames_.size());
        for (const std::string &name : extensionNames_) {
            extPtrs.push_back(name.c_str());
        }
        return Context::create(appName_, appVersion_, extPtrs, initialWidth_,
                               initialHeight_, *surfaceFromInstance_,
                               enableValidation_);
    }

private:
    std::string appName_ { "Lumen" };
    std::uint32_t appVersion_ { VK_MAKE_API_VERSION(0, 1, 0, 0) };
    std::vector<std::string> extensionNames_;
    std::uint32_t initialWidth_ { 1280 };
    std::uint32_t initialHeight_ { 720 };
    std::optional<std::function<VkSurfaceKHR(VkInstance)>> surfaceFromInstance_;
    bool enableValidation_ { true };
};

} // namespace vulkan
