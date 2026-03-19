/**
 * @file EasyVulkan.hpp
 * @brief EasyVulkan 便捷封装，提供渲染通道与帧缓冲的快速创建
 */

#pragma once

#include "VKBase+.h"
#include "VKBase.h"
#include "VKFormat.h"
#include <vulkan/vulkan_core.h>

using namespace vulkan;

/** @brief 当前窗口/交换链尺寸的引用 */
const VkExtent2D &windowSize =
    graphicsBase::Base().SwapchainCreateInfo().imageExtent;

namespace easyVulkan {
    using namespace vulkan;

    struct renderPassWithFramebuffer {
        renderPass renderPass;
        framebuffer framebuffer;
    };

    colorAttachment ca_canvas;

    /**
     * @brief 创建用于离屏渲染的画布渲染通道与帧缓冲
     *
     * 基于当前交换链图像格式创建一个单色附件的 `renderPass`，\n
     * 并将 `ca_canvas` 作为唯一的颜色附件绑定到 `framebuffer` 上，\n
     * 形成“画布纹理＋对应渲染通道”的一体化封装，方便在纹理上绘制内容。\n
     *
     * 渲染流程特性：
     * - 外部在片段着色器阶段以 `SHADER_READ_ONLY_OPTIMAL` 布局读画布纹理；
     * - 子通道内作为 `COLOR_ATTACHMENT_OPTIMAL` 进行写入；
     * - 写入结束后再次转换回 `SHADER_READ_ONLY_OPTIMAL` 供后续采样。
     *
     * @param canvasSize 画布尺寸（默认使用窗口尺寸 `windowSize`）
     * @return 画布渲染通道与帧缓冲封装的常量引用
     */
    const auto &CreateRpwf_Canvas(VkExtent2D canvasSize = windowSize) {
        static renderPassWithFramebuffer rpwf;

        ca_canvas.Create(graphicsBase::Base().SwapchainCreateInfo().imageFormat,
                         canvasSize, 1, VK_SAMPLE_COUNT_1_BIT,
                         VK_IMAGE_USAGE_SAMPLED_BIT |
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT);

        VkAttachmentDescription attachmentDescription {
            .format = graphicsBase::Base().SwapchainCreateInfo().imageFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        VkSubpassDependency subpassDependencies[2] = {
            { .srcSubpass = VK_SUBPASS_EXTERNAL,
              .dstSubpass = 0,
              .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              .srcAccessMask = 0,
              .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
              .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT },
            { .srcSubpass = 0,
              .dstSubpass = VK_SUBPASS_EXTERNAL,
              .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
              .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
              .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
              .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT }
        };
        VkAttachmentReference attachmentReference = {
            0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
        VkSubpassDescription subpassDescription = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachmentReference
        };
        VkRenderPassCreateInfo renderPassCreateInfo = {
            .attachmentCount = 1,
            .pAttachments = &attachmentDescription,
            .subpassCount = 1,
            .pSubpasses = &subpassDescription,
            .dependencyCount = 2,
            .pDependencies = subpassDependencies,
        };

        VkFramebufferCreateInfo framebufferCreateInfo = {
            .renderPass = rpwf.renderPass,
            .attachmentCount = 1,
            .pAttachments = ca_canvas.AddressOfImageView(),
            .width = canvasSize.width,
            .height = canvasSize.height,
            .layers = 1
        };
        rpwf.framebuffer.Create(framebufferCreateInfo);

        return rpwf;
    }

    /**
     * @struct renderPassWithFramebuffers
     * @brief 渲染通道与对应帧缓冲的绑定组合
     */
    struct renderPassWithFramebuffers {
        renderPass renderPass;
        std::vector<framebuffer> framebuffers;
    };

    /**
     * @brief 创建用于屏幕输出的渲染通道和帧缓冲
     * @return 静态缓存的 renderPassWithFramebuffers 引用
     */
    const auto &CreateRpwf_Screen() {
        static renderPassWithFramebuffers rpwf;

        VkAttachmentDescription attachmentDescription {};
        attachmentDescription.format =
            graphicsBase::Base().SwapchainCreateInfo().imageFormat;
        attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference attachmentReference {};
        attachmentReference.attachment = 0;
        attachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpassDescription {};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = 1;
        subpassDescription.pColorAttachments = &attachmentReference;

        VkSubpassDependency subpassDependency {};
        subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        subpassDependency.dstSubpass = 0;
        subpassDependency.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDependency.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDependency.srcAccessMask = 0;
        subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        subpassDependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo renderPassCreateInfo {};
        renderPassCreateInfo.attachmentCount = 1;
        renderPassCreateInfo.pAttachments = &attachmentDescription;
        renderPassCreateInfo.subpassCount = 1;
        renderPassCreateInfo.pSubpasses = &subpassDescription;
        renderPassCreateInfo.dependencyCount = 1;
        renderPassCreateInfo.pDependencies = &subpassDependency;

        rpwf.renderPass.Create(renderPassCreateInfo);

        auto CreateFramebuffers = [] {
            rpwf.framebuffers.resize(
                graphicsBase::Base().SwapchainImageCount());
            VkFramebufferCreateInfo framebufferCreateInfo = {
                .renderPass = rpwf.renderPass,
                .attachmentCount = 1,
                .width = windowSize.width,
                .height = windowSize.height,
                .layers = 1
            };
            for (size_t i {}; i < graphicsBase::Base().SwapchainImageCount();
                 i++) {
                VkImageView attachment =
                    graphicsBase::Base().SwapchainImageView(i);
                framebufferCreateInfo.pAttachments = &attachment;
                rpwf.framebuffers[i].Create(framebufferCreateInfo);
            }
        };

        auto DestroyFramebuffers = [] { rpwf.framebuffers.clear(); };

        CreateFramebuffers();

        ExecuteOnce(rpwf); // 防止再次调用本函数时，重复添加回调函数
        graphicsBase::Base().AddCallback_CreateSwapchain(CreateFramebuffers);
        graphicsBase::Base().AddCallback_DestroySwapchain(DestroyFramebuffers);
        return rpwf;
    }

    void BootScreen(const char *imagePath, VkFormat imageFormat) {
        VkExtent2D imageExtent;
        std::unique_ptr<uint8_t[]> pImageData = texture2d::LoadFile(
            imagePath, imageExtent, FormatInfo(imageFormat));

        if (!pImageData) {
            return;
        }

        stagingBuffer::BufferData_MainThread(
            pImageData.get(), FormatInfo(imageFormat).sizePerPixel *
                                  imageExtent.width * imageExtent.height);

        semaphore semaphore_imageIsAvailable;
        fence fence;
        commandBuffer commandBuffer;
        graphicsBase::Plus().CommandPool_Graphics().AllocateBuffers(
            commandBuffer);

        graphicsBase::Base().SwapImage(semaphore_imageIsAvailable);

        commandBuffer.Begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        VkExtent2D swapchainImageSize =
            graphicsBase::Base().SwapchainCreateInfo().imageExtent;

        bool blit = imageExtent.width != swapchainImageSize.width ||
                    imageExtent.height != swapchainImageSize.height ||
                    imageFormat !=
                        graphicsBase::Base().SwapchainCreateInfo().imageFormat;

        imageMemory imageMemory;

        if (blit) {
            VkImage image = stagingBuffer::AliasedImage2d_MainThread(
                imageFormat, imageExtent);

            if (image) {
                VkImageMemoryBarrier imageMemoryBarrier {
                    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    nullptr,
                    0,
                    VK_ACCESS_TRANSFER_READ_BIT,
                    VK_IMAGE_LAYOUT_PREINITIALIZED,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_QUEUE_FAMILY_IGNORED,
                    VK_QUEUE_FAMILY_IGNORED,
                    image,
                    { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
                };

                vkCmdPipelineBarrier(
                    commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                    1, &imageMemoryBarrier);

            } else {
                VkImageCreateInfo imageCreateInfo {
                    .imageType = VK_IMAGE_TYPE_2D,
                    .format = imageFormat,
                    .extent = { imageExtent.width, imageExtent.height, 1 },
                    .mipLevels = 1,
                    .arrayLayers = 1,
                    .samples = VK_SAMPLE_COUNT_1_BIT,
                    .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT
                };
                imageMemory.Create(imageCreateInfo,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

                VkBufferImageCopy region_copy {
                    .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                    .imageExtent = imageCreateInfo.extent
                };

                imageOperation::CmdCopyBufferToImage(
                    commandBuffer, stagingBuffer::Buffer_MainThread(),
                    imageMemory.Image(), region_copy,
                    { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                      VK_IMAGE_LAYOUT_UNDEFINED },
                    { VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_TRANSFER_READ_BIT,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL });

                image = imageMemory.Image();
            }

            VkImageBlit region_blit = { { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                                        { {},
                                          { int32_t(imageExtent.width),
                                            int32_t(imageExtent.height), 1 } },
                                        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                                        { {},
                                          { int32_t(swapchainImageSize.width),
                                            int32_t(swapchainImageSize.height),
                                            1 } } };

            imageOperation::CmdBlitImage(
                commandBuffer, image,
                graphicsBase::Base().SwapchainImage(
                    graphicsBase::Base().CurrentImageIndex()),
                region_blit,
                { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                  VK_IMAGE_LAYOUT_UNDEFINED },
                { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR },
                VK_FILTER_LINEAR);
        } else {
            VkBufferImageCopy region_copy {
                .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .imageExtent = { imageExtent.width, imageExtent.height, 1 }
            };

            imageOperation::CmdCopyBufferToImage(
                commandBuffer, stagingBuffer::Buffer_MainThread(),
                graphicsBase::Base().SwapchainImage(
                    graphicsBase::Base().CurrentImageIndex()),
                region_copy,
                { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                  VK_IMAGE_LAYOUT_UNDEFINED },
                { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                  VK_IMAGE_LAYOUT_PRESENT_SRC_KHR });
        }

        commandBuffer.End();

        VkPipelineStageFlags waitDstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        VkSubmitInfo submitInfo = { .waitSemaphoreCount = 1,
                                    .pWaitSemaphores =
                                        semaphore_imageIsAvailable.Address(),
                                    .pWaitDstStageMask = &waitDstStage,
                                    .commandBufferCount = 1,
                                    .pCommandBuffers =
                                        commandBuffer.Address() };
        graphicsBase::Base().SubmitCommandBuffer_Graphics(submitInfo, fence);
        fence.WaitAndReset();
        graphicsBase::Base().PresentImage();

        graphicsBase::Plus().CommandPool_Graphics().FreeBuffers(commandBuffer);
    }

    void CmdClearCanvas(VkCommandBuffer commandBuffer,
                        VkClearColorValue clearColor) {
        // Call this function before rpwf.renderPass begins.
        VkImageSubresourceRange imageSubresourceRange {
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1
        };

        VkImageMemoryBarrier imageMemoryBarrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = ca_canvas.Image(),
            .subresourceRange = imageSubresourceRange
        };

        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &imageMemoryBarrier);

        vkCmdClearColorImage(commandBuffer, ca_canvas.Image(),
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor,
                             1, &imageSubresourceRange);

        imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        imageMemoryBarrier.dstAccessMask = 0;
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
    }
} // namespace easyVulkan
