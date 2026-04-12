#pragma once

#include "vulkan/context.hpp"
#include "vulkan/swapchain.hpp"
#include <vulkan/vulkan_core.h>

namespace renderer {

struct Renderer {
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physDev = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    uint32_t queueFamily = UINT32_MAX;

    vulkan::Swapchain swapchain;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    VkSemaphore timelineSemaphore = VK_NULL_HANDLE;
    std::vector<VkSemaphore> presentSemaphores;

    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t currentFrame = 0;
    uint64_t timelineValue = 0;

    void init(VkDevice device, VkPhysicalDevice physDev, VkQueue graphicsQueue,
              VkQueue presentQueue, uint32_t queueFamilyIndex,
              VmaAllocator allocator, VkSurfaceKHR surface, SDL_Window *window,
              VkFormat depthFormat);

    void cleanup();
    void recreate(const lumen::platform::Window &window, VkSurfaceKHR surface,
                  VmaAllocator allocator,  vulkan::Context &context);

    bool beginFrame(uint32_t &imageIndex);
    void beginRenderPass(uint32_t imageIndex);
    void endRenderPass();
    void endFrame(uint32_t imageIndex);

    void setViewportScissor();
    void bindPipeline(VkPipeline pipeline);
    void bindDescriptorSets(VkPipelineLayout layout,
                            const VkDescriptorSet *sets, uint32_t count);

    [[nodiscard]] VkCommandBuffer currentCmd() const {
        return commandBuffers[currentFrame];
    }

private:
    void createCommandPool();
    void createCommandBuffers();
    void createSemaphores();
};

} // namespace renderer
