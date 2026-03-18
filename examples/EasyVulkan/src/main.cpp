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
    glm::vec4 color;
};

using namespace vulkan;

descriptorSetLayout descriptorSetLayout_triangle;
pipelineLayout pipelineLayout_triangle;
pipeline pipeline_triangle;

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
    VkDescriptorSetLayoutBinding descriptorSetLayoutBinding_trianglePosition = {
        .binding = 0, // 描述符被绑定到0号binding
        .descriptorType =
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // 类型为uniform缓冲区
        .descriptorCount = 1,                  // 个数是1个
        .stageFlags =
            VK_SHADER_STAGE_VERTEX_BIT // 在顶点着色器阶段读取uniform缓冲区
    };

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo_triangle = {
        .bindingCount = 1,
        .pBindings = &descriptorSetLayoutBinding_trianglePosition
    };

    descriptorSetLayout_triangle.Create(descriptorSetLayoutCreateInfo_triangle);

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo {
        .setLayoutCount = 1,
        .pSetLayouts = descriptorSetLayout_triangle.Address()
    };

    pipelineLayout_triangle.Create(pipelineLayoutCreateInfo);
}

/**
 * @brief 创建图形管线（依赖交换链尺寸，需在交换链创建后执行）
 */
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
        // pipelineCiPack.createInfo.renderPass =
        // RenderPassAndFramebuffers().renderPass;
        pipelineCiPack.inputAssemblyStateCi.topology =
            VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        pipelineCiPack.viewports.emplace_back(0.F, 0.F, float(windowSize.width),
                                              float(windowSize.height), 0.f,
                                              1.f);
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

/**
 * @brief 程序入口，初始化窗口与 Vulkan，渲染三角形主循环
 * @return 0 正常退出，-1 初始化失败
 */
int main() {
    PFN_vkCmdBeginRenderingKHR vkCmdBeginRendering = ::vkCmdBeginRendering;
    PFN_vkCmdEndRenderingKHR vkCmdEndRendering = ::vkCmdEndRendering;

    graphicsBase::Base().UseLatestApiVersion();
    if (graphicsBase::Base().ApiVersion() < VK_API_VERSION_1_2) {
        return -1;
    }

    if (graphicsBase::Base().ApiVersion() < VK_API_VERSION_1_3) {
        graphicsBase::Base().AddDeviceExtension(
            VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        VkPhysicalDeviceDynamicRenderingFeatures
            physicalDeviceDynamicRenderingFeatures = {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
            };
        graphicsBase::Base().AddNextStructure_PhysicalDeviceFeatures(
            physicalDeviceDynamicRenderingFeatures);
        if (!InitializeWindow({ 1280, 720 }) ||
            !physicalDeviceDynamicRenderingFeatures.dynamicRendering) {
            return -1;
        }
        vkCmdBeginRendering =
            reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(vkGetDeviceProcAddr(
                graphicsBase::Base().Device(), "vkCmdBeginRenderingKHR"));
        vkCmdEndRendering =
            reinterpret_cast<PFN_vkCmdEndRenderingKHR>(vkGetDeviceProcAddr(
                graphicsBase::Base().Device(), "vkCmdEndRenderingKHR"));
    } else if (!InitializeWindow({ 1280, 720 }) ||
               !graphicsBase::Base()
                    .PhysicalDeviceVulkan13Features()
                    .dynamicRendering) {
        return -1;
    }

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
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }
    };

    descriptorPool descriptorPool(1, descriptorPoolSizes);

    descriptorSet descriptorSet_trianglePosition;
    descriptorPool.AllocateSets(descriptorSet_trianglePosition,
                                descriptorSetLayout_triangle);

    // descriptorSet_texture.Write(texture.DescriptorImageInfo(sampler),
    //                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    // 矩形需两个三角形（6 个顶点），TRIANGLE_LIST 每 3 顶点一个三角形
    vertex vertices[] { { { .0f, -.5f }, { 1, 0, 0, 1 } },
                        { { -.5f, .5f }, { 0, 1, 0, 1 } },
                        { { .5f, .5f }, { 0, 0, 1, 1 } } };
    vertexBuffer vertexBuffer(sizeof vertices);
    vertexBuffer.TransferData(vertices);

    glm::vec2 uniform_positions[] { { .0f, .0f }, {},           { -.5f, .0f },
                                    {},           { .5f, .0f }, {} };
    uniformBuffer uniformBuffer(sizeof uniform_positions);
    uniformBuffer.TransferData(uniform_positions);

    VkDescriptorBufferInfo bufferInfo {
        .buffer = uniformBuffer,
        .offset = 0,
        .range = sizeof uniform_positions // 或 VK_WHOLE_SIZE
    };

    descriptorSet_trianglePosition.Write(bufferInfo,
                                         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

    VkClearValue clearColor = { .color = { 0.2f, 0.3f, 0.3f, 1.0f } };

    while (!glfwWindowShouldClose(pWindow)) {
        while (glfwGetWindowAttrib(pWindow, GLFW_ICONIFIED)) {
            glfwWaitEvents();
        }

        graphicsBase::Base().SwapImage(semaphore_imageIsAvailable);
        auto i = graphicsBase::Base().CurrentImageIndex();

        commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        // 渲染开始前的内存屏障
        VkImageMemoryBarrier imageMemoryBarrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = graphicsBase::Base().SwapchainImage(i),
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_DEPENDENCY_BY_REGION_BIT, 0, nullptr, 0,
                             nullptr, 1, &imageMemoryBarrier);

        VkRenderingAttachmentInfo colorAttachmentInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = graphicsBase::Base().SwapchainImageView(i),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { .color = { 1.F, 0.F, 0.F, 1.F } }
        };

        VkRenderingInfo renderingInfo { .sType =
                                            VK_STRUCTURE_TYPE_RENDERING_INFO,
                                        .renderArea = { {}, windowSize },
                                        .layerCount = 1,
                                        .colorAttachmentCount = 1,
                                        .pColorAttachments =
                                            &colorAttachmentInfo };

        vkCmdBeginRendering(commandBuffer, &renderingInfo);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline_triangle);

        vkCmdDraw(commandBuffer, 3, 1, 0, 0);

        vkCmdEndRendering(commandBuffer);

        // 渲染结束后的内存屏障
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        imageMemoryBarrier.dstAccessMask = 0;
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        vkCmdPipelineBarrier(
            commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_DEPENDENCY_BY_REGION_BIT,
            0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
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
