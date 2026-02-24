#include "EasyVulkan.hpp"
#include "GlfwGeneral.hpp"
#include "VKBase.h"

using namespace vulkan;

pipelineLayout pipelineLayout_triangle; // 管线布局
pipeline pipeline_triangle;             // 管线

int main() {
    if (!InitializeWindow({ 1280, 720 })) {
        return -1;
    }

    const auto &[renderPass, framebuffers] = easyVulkan::CreateRpwf_Screen();

    fence fence; // 以非置位状态创建栅栏
    semaphore semaphore_imageIsAvailable;
    semaphore semaphore_renderingIsOver;

    commandBuffer commandBuffer;
    commandPool commandPool(graphicsBase::Base().QueueFamilyIndex_Graphics(),
                            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandPool.AllocateBuffers(commandBuffer);

    VkClearValue clearColor;
    clearColor.color = { 1.0F, 0.0F, 0.0F, 1.0F };

    while (!glfwWindowShouldClose(pWindow)) {
        while (glfwGetWindowAttrib(pWindow, GLFW_ICONIFIED)) {
            glfwWaitEvents();
        }

        graphicsBase::Base().SwapImage(semaphore_imageIsAvailable);
        // 因为帧缓冲与所获取的交换链图像一一对应，获取交换链图像索引
        auto i = graphicsBase::Base().CurrentImageIndex();

        commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        renderPass.CmdBegin(commandBuffer, framebuffers[i], { {}, windowSize },
                            clearColor);
        /*渲染命令，待填充*/
        renderPass.CmdEnd(commandBuffer);
        commandBuffer.End();

        vulkan::graphicsBase::Base().SubmitCommandBuffer_Graphics(
            commandBuffer, semaphore_imageIsAvailable,
            semaphore_renderingIsOver, fence);
        vulkan::graphicsBase::Base().PresentImage(semaphore_renderingIsOver);

        glfwPollEvents();
        TitleFps();

        fence.WaitAndReset();
    }
    TerminateWindow();
    return 0;
}
