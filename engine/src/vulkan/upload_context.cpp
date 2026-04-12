#include "vulkan/upload_context.hpp"

namespace vulkan {

UploadContext &UploadContext::instance() {
    static UploadContext inst;
    return inst;
}

void UploadContext::init(VkDevice dev, VkQueue q,
                         std::uint32_t graphicsQueueFamily) {
    if (device != VK_NULL_HANDLE) {
        destroy();
    }
    device = dev;
    queue = q;
    ownsCommandPool = true;

    VkFenceCreateInfo fenceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    vkCreateFence(device, &fenceCreateInfo, nullptr, &fence);

    VkCommandPoolCreateInfo commandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = graphicsQueueFamily,
    };
    vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPool);

    VkCommandBufferAllocateInfo commandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &commandBuffer);
}

void UploadContext::init(VkDevice dev, VkQueue q, VkCommandPool sharedCommandPool) {
    if (device != VK_NULL_HANDLE) {
        destroy();
    }
    device = dev;
    queue = q;
    commandPool = sharedCommandPool;
    ownsCommandPool = false;

    VkFenceCreateInfo fenceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    vkCreateFence(device, &fenceCreateInfo, nullptr, &fence);

    VkCommandBufferAllocateInfo commandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &commandBuffer);
}

void UploadContext::destroy() {
    if (device == VK_NULL_HANDLE) {
        return;
    }
    if (fence != VK_NULL_HANDLE) {
        vkDestroyFence(device, fence, nullptr);
        fence = VK_NULL_HANDLE;
    }
    if (ownsCommandPool) {
        if (commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandPool, nullptr);
        }
    } else {
        if (commandBuffer != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        }
    }
    commandPool = VK_NULL_HANDLE;
    commandBuffer = VK_NULL_HANDLE;
    device = VK_NULL_HANDLE;
    queue = VK_NULL_HANDLE;
}

} // namespace vulkan
