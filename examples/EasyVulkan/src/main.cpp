#include "EasyVulkan.hpp"
#include "GlfwGeneral.hpp"
#include "VKBase.h"

using namespace vulkan;

using namespace vulkan; // 上一节中在main.cpp中全局范围内使用了命名空间

pipelineLayout pipelineLayout_triangle; // 管线布局
pipeline pipeline_triangle;             // 管线

// 该函数调用easyVulkan::CreateRpwf_Screen()并存储返回的引用到静态变量
const auto &RenderPassAndFramebuffers() {
    static const auto &rpwf = easyVulkan::CreateRpwf_Screen();
    return rpwf;
}
// 该函数用于创建管线布局
void CreateLayout() {
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo {};
    pipelineLayout_triangle.Create(pipelineLayoutCreateInfo);
}

// 该函数用于创建管线
void CreatePipeline() {
    static shaderModule vert("shader/FirstTriangle.vert.spv");
    static shaderModule frag("shader/FirstTriangle.frag.spv");

    static VkPipelineShaderStageCreateInfo
        shaderStageCreateInfos_triangle[2] = {
            vert.StageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
            frag.StageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT)
        };

    auto Create = [] {
        graphicsPipelineCreateInfoPack pipelineCiPack;
        /*待后续填充*/
        pipeline_triangle.Create(pipelineCiPack);
    };

    auto Destroy = [] { pipeline_triangle.~pipeline(); };

    graphicsBase::Base().AddCallback_CreateSwapchain(Create);
    graphicsBase::Base().AddCallback_DestroySwapchain(Destroy);
    // 调用Create()以创建管线
    Create();
}

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
