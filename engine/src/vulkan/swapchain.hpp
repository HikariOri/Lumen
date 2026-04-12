#pragma once

#include "platform/window.hpp"
#include "vulkan/context.hpp"
namespace vulkan {
struct Swapchain {
    VkDevice device = VK_NULL_HANDLE;
    VkSwapchainKHR handle = VK_NULL_HANDLE;

    VkFormat imageFormat;
    VkExtent2D extent;

    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;

    VkImage depthImage = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VmaAllocation depthAlloc = VK_NULL_HANDLE;
};

inline void recreateSwapchain(Swapchain &swapchain,
                              const lumen::platform::Window &window,
                              VkDevice device, VkSurfaceKHR surface,
                              VmaAllocator allocator, VkRenderPass renderPass,
                              VkFormat depthFormat, vulkan::Context &context) {
    vkDeviceWaitIdle(device);

    for (auto fb : swapchain.framebuffers)
        vkDestroyFramebuffer(device, fb, nullptr);
    for (auto v : swapchain.views)
        vkDestroyImageView(device, v, nullptr);

    if (swapchain.depthView)
        vkDestroyImageView(device, swapchain.depthView, nullptr);
    if (swapchain.depthImage)
        vmaDestroyImage(allocator, swapchain.depthImage, swapchain.depthAlloc);
    if (swapchain.handle)
        vkDestroySwapchainKHR(device, swapchain.handle, nullptr);

    swapchain.framebuffers.clear();
    swapchain.views.clear();

    int w, h;
    window.get_framebuffer_size(&w, &h);
    swapchain.extent.width = w > 0 ? (uint32_t)w : 1;
    swapchain.extent.height = h > 0 ? (uint32_t)h : 1;

    // VkSwapchainCreateInfoKHR sci {
    //     .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    //     .surface = surface,
    //     .minImageCount = 3,
    //     .imageFormat = VK_FORMAT_B8G8R8A8_SRGB,
    //     .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    //     .imageExtent = swapchain.extent,
    //     .imageArrayLayers = 1,
    //     .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    //     .presentMode = VK_PRESENT_MODE_FIFO_KHR,
    //     .clipped = VK_TRUE,
    // };

    context.recreate_swapchain(w, h);
    swapchain.handle = context.swapchain();

    // if (vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain.handle) !=
    //     VK_SUCCESS)
    //     throw std::runtime_error("swapchain failed");

    swapchain.imageFormat = context.swapchain_format();
    swapchain.images = context.swapchain_images();
    swapchain.views = context.swapchain_image_views();
    // swapchain.

    // uint32_t count = 0;
    // vkGetSwapchainImagesKHR(device, swapchain.handle, &count, nullptr);
    // swapchain.images.resize(count);
    // vkGetSwapchainImagesKHR(device, swapchain.handle, &count,
    //                         swapchain.images.data());

    // for (VkImage img : swapchain.images) {
    //     VkImageViewCreateInfo ivci {
    //         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    //         .image = img,
    //         .viewType = VK_IMAGE_VIEW_TYPE_2D,
    //         .format = swapchain.imageFormat,
    //         .subresourceRange { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    //                             .levelCount = 1,
    //                             .layerCount = 1 }
    //     };

    //     VkImageView view;
    //     vkCreateImageView(device, &ivci, nullptr, &view);
    //     swapchain.views.push_back(view);
    // }

    // VkImageCreateInfo di {
    //     .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    //     .imageType = VK_IMAGE_TYPE_2D,
    //     .format = depthFormat,
    //     .extent = { .width = swapchain.extent.width,
    //                 .height = swapchain.extent.height,
    //                 .depth = 1 },
    //     .mipLevels = 1,
    //     .arrayLayers = 1,
    //     .samples = VK_SAMPLE_COUNT_1_BIT,
    //     .tiling = VK_IMAGE_TILING_OPTIMAL,
    //     .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    // };

    // VmaAllocationCreateInfo allocInfo {
    //     .usage = VMA_MEMORY_USAGE_GPU_ONLY,
    // };

    // vmaCreateImage(allocator, &di, &allocInfo, &swapchain.depthImage,
    //                &swapchain.depthAlloc, nullptr);

    // VkImageViewCreateInfo dvi {
    //     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    //     .image = swapchain.depthImage,
    //     .viewType = VK_IMAGE_VIEW_TYPE_2D,
    //     .format = depthFormat,
    //     .subresourceRange { .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
    //                         .levelCount = 1,
    //                         .layerCount = 1 }
    // };

    // vkCreateImageView(device, &dvi, nullptr, &swapchain.depthView);

    // for (auto &view : swapchain.views) {
    //     VkImageView attachments[] = { view, swapchain.depthView };

    //     VkFramebufferCreateInfo fbci {
    //         .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    //         .renderPass = renderPass,
    //         .attachmentCount = 2,
    //         .pAttachments = attachments,
    //         .width = swapchain.extent.width,
    //         .height = swapchain.extent.height,
    //         .layers = 1
    //     };

    //     VkFramebuffer fb;
    //     vkCreateFramebuffer(device, &fbci, nullptr, &fb);
    //     swapchain.framebuffers.push_back(fb);
    // }
}

} // namespace vulkan
