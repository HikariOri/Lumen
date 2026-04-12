#include "renderer/renderer.hpp"
#include "core/log/logger.hpp"

namespace renderer {

void Renderer::init(VkDevice device, VkPhysicalDevice physDev,
                    VkQueue graphicsQueue, VkQueue presentQueue,
                    uint32_t queueFamilyIndex, VmaAllocator allocator,
                    VkSurfaceKHR surface, SDL_Window *window,
                    VkFormat depthFormat) {

    this->device = device;
    this->physDev = physDev;
    this->graphicsQueue = graphicsQueue;
    this->presentQueue = presentQueue;
    this->queueFamily = queueFamilyIndex;
    this->depthFormat = depthFormat;

    createCommandPool();
    createCommandBuffers();
    createSemaphores();
}

void Renderer::cleanup() {}

void Renderer::recreate(const lumen::platform::Window &window,
                        VkSurfaceKHR surface, VmaAllocator allocator,  vulkan::Context &context) {
    vulkan::recreateSwapchain(this->swapchain, window, this->device, surface,
                              allocator, this->renderPass, depthFormat,context);

    for (auto &s : presentSemaphores)
        vkDestroySemaphore(device, s, nullptr);

    presentSemaphores.clear();
    presentSemaphores.resize(swapchain.images.size());

    for (auto &s : presentSemaphores)
        vkCreateSemaphore(device, nullptr, nullptr, &s);

    currentFrame = 0;
}

bool Renderer::beginFrame(uint32_t &imageIndex) {
    // 等上一帧 GPU 完成
    {
        uint64_t waitValue = timelineValue;

        VkSemaphoreWaitInfo waitInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO
        };
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &timelineSemaphore;
        waitInfo.pValues = &waitValue;

        vkWaitSemaphores(device, &waitInfo, UINT64_MAX);
    }

    {
        // VkDevice                                    device,
        // const VkAcquireNextImageInfoKHR*            pAcquireInfo,
        // uint32_t*                                   pImageIndex;

        VkAcquireNextImageInfoKHR acquireNextImageInfo {
            .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR
        };

        acquireNextImageInfo.deviceMask = 1;
        acquireNextImageInfo.swapchain = swapchain.handle;
        acquireNextImageInfo.timeout = UINT64_MAX;
        acquireNextImageInfo.semaphore = imageAvailableSemaphores[currentFrame];
        acquireNextImageInfo.fence = VK_NULL_HANDLE;

        vkAcquireNextImage2KHR(device, &acquireNextImageInfo, &imageIndex);
    }

    // 重置并录制 command buffer
    vkResetCommandBuffer(commandBuffers[imageIndex], 0);

    VkCommandBufferBeginInfo beginInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };
    vkBeginCommandBuffer(commandBuffers[imageIndex], &beginInfo);

    return true;
}

void Renderer::beginRenderPass(uint32_t imageIndex) {
    // 开始 Render Pass
    VkRenderPassBeginInfo renderPassBeginInfo {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
    };

    renderPassBeginInfo.renderPass = renderPass;
    renderPassBeginInfo.framebuffer = swapchain.framebuffers[imageIndex];

    renderPassBeginInfo.renderArea.offset = { .x = 0, .y = 0 };
    renderPassBeginInfo.renderArea.extent = { .width = swapchain.extent.width,
                                              .height =
                                                  swapchain.extent.height };

    VkClearValue clearValues[2];
    clearValues[0].color = { { 0.1, 0.1, 0.1, 1.0 } };
    clearValues[1].depthStencil = { .depth = 1.0, .stencil = 0 };

    renderPassBeginInfo.clearValueCount = 2;
    renderPassBeginInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(commandBuffers[imageIndex], &renderPassBeginInfo,
                         VK_SUBPASS_CONTENTS_INLINE);

    VkViewport dynamicViewport {};
    dynamicViewport.x = 0.0F;
    dynamicViewport.y = 0.0F;
    dynamicViewport.width = static_cast<float>(swapchain.extent.width);
    dynamicViewport.height = static_cast<float>(swapchain.extent.height);
    dynamicViewport.minDepth = 0.0F;
    dynamicViewport.maxDepth = 1.0F;
    vkCmdSetViewport(commandBuffers[imageIndex], 0, 1, &dynamicViewport);

    VkRect2D dynamicScissor {};
    dynamicScissor.offset = { .x = 0, .y = 0 };
    dynamicScissor.extent = {
        .width = static_cast<std::uint32_t>(swapchain.extent.width),
        .height = static_cast<std::uint32_t>(swapchain.extent.height)
    };
    vkCmdSetScissor(commandBuffers[imageIndex], 0, 1, &dynamicScissor);
}

void Renderer::endRenderPass() { vkCmdEndRenderPass(currentCmd()); }

void Renderer::endFrame(uint32_t imageIndex) {
    vkEndCommandBuffer(commandBuffers[imageIndex]);

    // 提交 GPU（混用 binary + timeline 时，Timeline 扩展里 value
    // 数组长度须与 wait/signal 数量一致；binary 对应项的值会被忽略）
    {
        const std::uint64_t signalValue = timelineValue + 1;

        // wait
        std::array<VkSemaphoreSubmitInfo, 2> waitInfos {};

        // imageAvailableSemaphores[currentFrame] (binary)
        waitInfos[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitInfos[0].semaphore = imageAvailableSemaphores[currentFrame];
        waitInfos[0].value = 0; // 忽略
        waitInfos[0].stageMask =
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

        waitInfos[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitInfos[1].semaphore = timelineSemaphore;
        waitInfos[1].value = timelineValue;
        waitInfos[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        // signal
        std::array<VkSemaphoreSubmitInfo, 2> signalInfos {};

        // timeline
        signalInfos[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfos[0].semaphore = timelineSemaphore;
        signalInfos[0].value = signalValue;
        signalInfos[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        // presentSemaphores[imageIndex] (binary)
        signalInfos[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfos[1].semaphore = presentSemaphores[imageIndex];
        signalInfos[1].value = 0; // 忽略
        signalInfos[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

        // command buffer
        VkCommandBufferSubmitInfo commandBufferInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO
        };

        commandBufferInfo.commandBuffer = commandBuffers[imageIndex];
        // uint32_t           deviceMask;

        VkSubmitInfo2 submitInfo { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };

        submitInfo.waitSemaphoreInfoCount = waitInfos.size();
        submitInfo.pWaitSemaphoreInfos = waitInfos.data();

        submitInfo.signalSemaphoreInfoCount = signalInfos.size();
        submitInfo.pSignalSemaphoreInfos = signalInfos.data();

        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandBufferInfo;

        vkQueueSubmit2(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    }

    // present
    {
        VkPresentInfoKHR presentInfo { .sType =
                                           VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };

        VkSwapchainKHR swapchain = this->swapchain.handle;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &presentSemaphores[imageIndex];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;
        // presentInfo.pResults;

        vkQueuePresentKHR(presentQueue, &presentInfo);
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    timelineValue++;
}

void Renderer::setViewportScissor() {
    auto cmd = currentCmd();

    VkViewport vp { .width = (float)swapchain.extent.width,
                    .height = (float)swapchain.extent.height,
                    .maxDepth = 1.0f };

    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor { { 0, 0 }, swapchain.extent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void Renderer::bindPipeline(VkPipeline pipeline) {
    vkCmdBindPipeline(currentCmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}

void Renderer::bindDescriptorSets(VkPipelineLayout layout,
                                  const VkDescriptorSet *sets, uint32_t count) {
    vkCmdBindDescriptorSets(currentCmd(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                            layout, 0, count, sets, 0, nullptr);
}

void Renderer::createCommandPool() {
    VkCommandPoolCreateInfo commandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
    };
    commandPoolCreateInfo.flags =
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolCreateInfo.queueFamilyIndex = queueFamily;
    if (vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr,
                            &commandPool)) {
        LUMEN_APP_LOG_ERROR("Failed to create command pool");
    }
}

void Renderer::createCommandBuffers() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
    };

    if (vkAllocateCommandBuffers(device, &info, commandBuffers.data()) !=
        VK_SUCCESS) {
        LUMEN_APP_LOG_ERROR("Failed to allocate command buffers");
    }
}

void Renderer::createSemaphores() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    presentSemaphores.resize(swapchain.views.size());

    VkSemaphoreCreateInfo semaphoreCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr,
                              &imageAvailableSemaphores[i])) {
            LUMEN_APP_LOG_ERROR("Failed to create image available semaphore");
            return;
        }
    }

    for (size_t i = 0; i < swapchain.views.size(); i++) {
        if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr,
                              &presentSemaphores[i])) {
            LUMEN_APP_LOG_ERROR("Failed to create present semaphore");
            return;
        }
    }

    VkSemaphoreTypeCreateInfo timelineInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO
    };

    timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineInfo.initialValue = 0;

    VkSemaphoreCreateInfo createInfo {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };
    createInfo.pNext = &timelineInfo;

    vkCreateSemaphore(device, &createInfo, nullptr, &timelineSemaphore);
}

} // namespace renderer
