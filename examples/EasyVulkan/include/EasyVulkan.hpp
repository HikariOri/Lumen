#pragma once

#include "VKBase.h"

using namespace vulkan;

const VkExtent2D &windowSize =
    graphicsBase::Base().SwapchainCreateInfo().imageExtent;

namespace easyVulkan {
    using namespace vulkan;

    struct renderPassWithFramebuffers {
        renderPass renderPass;
        std::vector<framebuffer> framebuffers;
    };

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
} // namespace easyVulkan
