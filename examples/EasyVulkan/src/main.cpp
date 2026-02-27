#include "EasyVulkan.hpp"
#include "GlfwGeneral.hpp"
#include "VKBase.h"

using namespace vulkan;

using namespace vulkan; // 上一节中在main.cpp中全局范围内使用了命名空间

pipelineLayout pipelineLayout_triangle; // 管线布局
pipeline pipeline_triangle;             // 管线

// 该函数调用easyVulkan::CreateRpwf_Screen()并存储返回的引用到静态变量
// 获取并缓存屏幕渲染通道和帧缓冲（静态缓存避免重复创建）
const auto &RenderPassAndFramebuffers() {
    static const auto &rpwf = easyVulkan::CreateRpwf_Screen();
    return rpwf;
}
// 该函数用于创建管线布局
// 创建管线布局（此处无描述符集/推送常量）
void CreateLayout() {
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo {};
    pipelineLayout_triangle.Create(pipelineLayoutCreateInfo);
}

// 该函数用于创建管线
// 创建图形管线（依赖交换链尺寸，需在交换链创建后执行）
void CreatePipeline() {
    static shaderModule vert("shaders/glsl/FirstTriangle.vert.spv");
    static shaderModule frag("shaders/glsl/FirstTriangle.frag.spv");

    static VkPipelineShaderStageCreateInfo
        shaderStageCreateInfos_triangle[2] = {
            vert.StageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
            frag.StageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT)
        };

    auto Create = [] {
        graphicsPipelineCreateInfoPack pipelineCiPack;
        pipelineCiPack.createInfo.layout = pipelineLayout_triangle;
        pipelineCiPack.createInfo.renderPass =
            RenderPassAndFramebuffers().renderPass;
        pipelineCiPack.inputAssemblyStateCi.topology =
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        pipelineCiPack.viewports.emplace_back(
            0.0F, 0.0F, float(windowSize.width), float(windowSize.height), 0.0F,
            1.0F);
        pipelineCiPack.scissors.emplace_back(VkOffset2D {}, windowSize);
        pipelineCiPack.multisampleStateCi.rasterizationSamples =
            VK_SAMPLE_COUNT_1_BIT;
        pipelineCiPack.colorBlendAttachmentStates.push_back(
            { .colorWriteMask = 0b1111 });
        pipelineCiPack.UpdateAllArrays();
        pipelineCiPack.createInfo.stageCount = 2;
        pipelineCiPack.createInfo.pStages = shaderStageCreateInfos_triangle;
        pipeline_triangle.Create(pipelineCiPack);
    };

    auto Destroy = [] { pipeline_triangle.~pipeline(); };

    graphicsBase::Base().AddCallback_CreateSwapchain(Create);
    graphicsBase::Base().AddCallback_DestroySwapchain(Destroy);

    Create();
}

int main() {
    if (!InitializeWindow({ 1280, 720 })) {
        return -1;
    }

    const auto &[renderPass, framebuffers] = RenderPassAndFramebuffers();
    CreateLayout();
    CreatePipeline();

    fence fence;
    semaphore semaphore_imageIsAvailable;
    semaphore semaphore_renderingIsOver;

    commandBuffer commandBuffer;
    commandPool commandPool(graphicsBase::Base().QueueFamilyIndex_Graphics(),
                            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandPool.AllocateBuffers(commandBuffer);

    VkClearValue clearColor = { .color = { 1.f, 0.f, 0.f, 1.f } }; // 红色

    while (!glfwWindowShouldClose(pWindow)) {
        while (glfwGetWindowAttrib(pWindow, GLFW_ICONIFIED)) {
            glfwWaitEvents();
        }

        graphicsBase::Base().SwapImage(semaphore_imageIsAvailable);
        auto i = graphicsBase::Base().CurrentImageIndex();

        commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        renderPass.CmdBegin(commandBuffer, framebuffers[i], { {}, windowSize },
                            clearColor);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline_triangle);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);

        renderPass.CmdEnd(commandBuffer);
        commandBuffer.End();

        graphicsBase::Base().SubmitCommandBuffer_Graphics(
            commandBuffer, semaphore_imageIsAvailable,
            semaphore_renderingIsOver, fence);
        graphicsBase::Base().PresentImage(semaphore_renderingIsOver);

        glfwPollEvents();
        TitleFps();

        fence.WaitAndReset();
    }
    TerminateWindow();
    return 0;
}
