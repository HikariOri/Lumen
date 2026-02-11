#include "GlfwGeneral.hpp"
#include "VKBase.h"

int main() {
    if (!InitializeWindow({ 1280, 720 })) {
        return -1;
    }

    vulkan::fence fence; // 以非置位状态创建栅栏
    vulkan::semaphore semaphore_imageIsAvailable;
    vulkan::semaphore semaphore_renderingIsOver;

    vulkan::commandBuffer commandBuffer;
    vulkan::commandPool commandPool(
        vulkan::graphicsBase::Base().QueueFamilyIndex_Graphics(),
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandPool.AllocateBuffers(commandBuffer);

    while (!glfwWindowShouldClose(pWindow)) {
        while (glfwGetWindowAttrib(pWindow, GLFW_ICONIFIED)) {
            glfwWaitEvents();
        }

        vulkan::graphicsBase::Base().SwapImage(semaphore_imageIsAvailable);

        commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        /*渲染命令，待填充*/
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
