#pragma once

#include <vulkan/vulkan_core.h>

namespace vulkan {

/// 全局唯一上传上下文：fence +（自建或外部 command pool 上分配的）primary command buffer
class UploadContext {
public:
    [[nodiscard]] static UploadContext &instance();

    void init(VkDevice device, VkQueue queue, std::uint32_t graphicsQueueFamily);
    /// 使用外部 command pool（内部 `vkAllocateCommandBuffers` 分配 1 个 primary CB；不拥有 pool）
    void init(VkDevice device, VkQueue queue, VkCommandPool sharedCommandPool);

    /// 释放 fence；若拥有 pool 则 `vkDestroyCommandPool`；否则 `vkFreeCommandBuffers`
    void destroy();

    VkDevice device {};
    VkQueue queue {};
    VkCommandPool commandPool {};
    VkCommandBuffer commandBuffer {};
    VkFence fence {};
    /// 为 true 时本对象创建 command pool；销毁时 `vkDestroyCommandPool`
    bool ownsCommandPool { true };

private:
    UploadContext() = default;
    UploadContext(const UploadContext &) = delete;
    UploadContext &operator=(const UploadContext &) = delete;
};

} // namespace vulkan
