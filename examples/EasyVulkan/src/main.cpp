/**
 * @file main.cpp
 * @brief EasyVulkan 示例程序入口，渲染一个红色三角形
 */

#include "EasyVulkan.hpp"
#include "GlfwGeneral.hpp"
#include "VKBase+.h"
#include "VKBase.h"

struct vertex {
    glm::vec2 position;
    glm::vec2 texCoord;
};

using namespace vulkan;

descriptorSetLayout descriptorSetLayout_texture;
pipelineLayout pipelineLayout_texture;
pipeline pipeline_texture;

/**
 * @brief 获取屏幕渲染通道和帧缓冲（静态缓存，避免重复创建）
 * @return 渲染通道与帧缓冲组合的常量引用
 */
const auto &RenderPassAndFramebuffers() {
    static const auto &rpwf = easyVulkan::CreateRpwf_Screen();
    return rpwf;
}

/**
 * @brief 创建管线布局（此处无描述符集/推送常量）
 */
void CreateLayout() {
    VkDescriptorSetLayoutBinding descriptorSetLayoutBinding_texture {
        .binding = 0, // 描述符被绑定到 0 号 binding
        .descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // 类型为带采样器的图像
        .descriptorCount = 1,                          // 个数是 1 个
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT // 在片段着色器阶段采样图像
    };

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo_texture {
        .bindingCount = 1, .pBindings = &descriptorSetLayoutBinding_texture
    };

    descriptorSetLayout_texture.Create(descriptorSetLayoutCreateInfo_texture);

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {
        .setLayoutCount = 1,
        .pSetLayouts = descriptorSetLayout_texture.Address()
    };

    pipelineLayout_texture.Create(pipelineLayoutCreateInfo);
}

/**
 * @brief 创建图形管线（依赖交换链尺寸，需在交换链创建后执行）
 */
void CreatePipeline() {
    static shaderModule vert("shaders/glsl/Texture.vert.spv");
    static shaderModule frag("shaders/glsl/Texture.frag.spv");

    static VkPipelineShaderStageCreateInfo
        shaderStageCreateInfos_triangle[2] = {
            vert.StageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT),
            frag.StageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT)
        };

    auto Create = [] {
        graphicsPipelineCreateInfoPack pipelineCiPack;
        pipelineCiPack.createInfo.layout = pipelineLayout_texture;
        pipelineCiPack.createInfo.renderPass =
            RenderPassAndFramebuffers().renderPass;
        // 数据来自 0 号顶点缓冲区，输入频率是逐顶点输入
        pipelineCiPack.vertexInputBindings.emplace_back(
            0, sizeof(vertex), VK_VERTEX_INPUT_RATE_VERTEX);
        // location 为 0，数据来自 0 号顶点缓冲区，vec2 对应
        // VK_FORMAT_R32G32_SFLOAT，用 offsetof 计算 position 在 vertex
        // 中的起始位置
        pipelineCiPack.vertexInputAttributes.emplace_back(
            0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, position));
        pipelineCiPack.vertexInputAttributes.emplace_back(
            1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, texCoord));
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
        pipeline_texture.Create(pipelineCiPack);
    };

    auto Destroy = [] { pipeline_texture.~pipeline(); };

    graphicsBase::Base().AddCallback_CreateSwapchain(Create);
    graphicsBase::Base().AddCallback_DestroySwapchain(Destroy);

    Create();
}

/**
 * @brief 程序入口，初始化窗口与 Vulkan，渲染三角形主循环
 * @return 0 正常退出，-1 初始化失败
 */
int main() {
    if (!InitializeWindow({ 1280, 720 }))
        return -1;

    easyVulkan ::BootScreen("./assets/textures/wallhaven-gjm6q3_1920x1080.png",
                            VK_FORMAT_R8G8B8A8_UNORM);
    std::this_thread::sleep_for(std::chrono::seconds(1));

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

    // Load image
    texture2d texture("assets/textures/ikun2026_happy_new_year.jpg",
                      VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, true);
    // Create sampler
    VkSamplerCreateInfo samplerCreateInfo = texture::SamplerCreateInfo();
    sampler sampler(samplerCreateInfo);
    // Create descriptor
    VkDescriptorPoolSize descriptorPoolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
    };
    descriptorPool descriptorPool(1, descriptorPoolSizes);
    descriptorSet descriptorSet_texture;
    descriptorPool.AllocateSets(descriptorSet_texture,
                                descriptorSetLayout_texture);
    descriptorSet_texture.Write(texture.DescriptorImageInfo(sampler),
                                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    // 矩形需两个三角形（6 个顶点），TRIANGLE_LIST 每 3 顶点一个三角形
    vertex vertices[] = {
        { { -.5f, -.5f }, { 0, 0 } }, { { .5f, -.5f }, { 1, 0 } },
        { { -.5f, .5f }, { 0, 1 } }, // 三角形 1: 左下、右下、左上
        { { -.5f, .5f }, { 0, 1 } },  { { .5f, -.5f }, { 1, 0 } },
        { { .5f, .5f }, { 1, 1 } } // 三角形 2: 左上、右下、右上
    };

    vertexBuffer vertexBuffer(sizeof vertices);
    vertexBuffer.TransferData(vertices);

    VkClearValue clearColor = { .color = { 0.2f, 0.3f, 0.3f, 1.0f } };

    while (!glfwWindowShouldClose(pWindow)) {
        while (glfwGetWindowAttrib(pWindow, GLFW_ICONIFIED))
            glfwWaitEvents();

        graphicsBase::Base().SwapImage(semaphore_imageIsAvailable);
        auto i = graphicsBase::Base().CurrentImageIndex();

        commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        renderPass.CmdBegin(commandBuffer, framebuffers[i], { {}, windowSize },
                            clearColor);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffer.Address(),
                               &offset);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline_texture);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout_texture, 0, 1,
                                descriptorSet_texture.Address(), 0, nullptr);
        vkCmdDraw(commandBuffer, 6, 1, 0, 0);
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
