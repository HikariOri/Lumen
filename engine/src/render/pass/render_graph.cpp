/**
 * @file render_graph.cpp
 * @brief RenderGraph 实现：拓扑排序、自动 Layout 转换、PipelineBarrier
 */

#include "render/pass/render_graph.hpp"

#include "core/logger.hpp"
#include "render/context.hpp"
#include "render/resource/image.hpp"
#include "render/swapchain.hpp"

namespace lumen::render {

// --- RGImage ---

RGImage RGImage::from_texture(const Image& img, bool asDepth) {
    RGImage out {};
    out.type = RGImageType::Texture;
    out.image = img.handle();
    out.view = img.view();
    out.format = img.format();
    out.extent = { img.width(), img.height() };
    out.isDepth = asDepth;
    out.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return out;
}

RGImage RGImage::from_swapchain(const Swapchain& swapchain) {
    RGImage out {};
    out.type = RGImageType::Swapchain;
    out.swapchain_ = &swapchain;
    out.format = swapchain.image_format();
    out.extent = swapchain.extent();
    out.isDepth = false;
    out.perIndexLayouts_.resize(swapchain.image_count(),
                                VK_IMAGE_LAYOUT_UNDEFINED);
    return out;
}

VkImage RGImage::image_at(uint32_t index) const {
    if (type == RGImageType::Swapchain && swapchain_) {
        return swapchain_->image(index);
    }
    return image;
}

VkImageView RGImage::view_at(uint32_t index) const {
    if (type == RGImageType::Swapchain && swapchain_) {
        return swapchain_->image_view(index);
    }
    return view;
}

VkImageLayout RGImage::layout_at(uint32_t index) const {
    if (type == RGImageType::Swapchain && index < perIndexLayouts_.size()) {
        return perIndexLayouts_[index];
    }
    return currentLayout;
}

void RGImage::set_layout(uint32_t index, VkImageLayout layout) {
    if (type == RGImageType::Swapchain && index < perIndexLayouts_.size()) {
        perIndexLayouts_[index] = layout;
    } else {
        currentLayout = layout;
    }
}

uint32_t RGImage::image_count() const {
    if (type == RGImageType::Swapchain && swapchain_) {
        return swapchain_->image_count();
    }
    return 1;
}

// --- RenderGraph ---

RenderGraph::RenderGraph(const Context* ctx) : ctx_(ctx) {}

void RenderGraph::add_pass(const RGPass& pass) {
    passes_.push_back(pass);
}

void RenderGraph::clear() {
    passes_.clear();
}

void RenderGraph::pipeline_barrier_(VkCommandBuffer cmd, RGImage* img,
                                   VkImageLayout oldLayout,
                                   VkImageLayout newLayout, uint32_t index) {
    if (!img || img->image_at(index) == VK_NULL_HANDLE)
        return;
    if (oldLayout == newLayout)
        return;

    VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = img->image_at(index);
    barrier.subresourceRange.aspectMask = img->isDepth
        ? VK_IMAGE_ASPECT_DEPTH_BIT
        : VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = 0;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        barrier.srcAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (newLayout ==
               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.dstAccessMask = 0;
        dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr,
                         1, &barrier);

    img->set_layout(index, newLayout);
}

void RenderGraph::transition_image_(VkCommandBuffer cmd, RGImage* img,
                                   VkImageLayout newLayout, uint32_t index) {
    if (!img || img->image_at(index) == VK_NULL_HANDLE)
        return;
    VkImageLayout oldLayout = img->layout_at(index);
    if (oldLayout == newLayout)
        return;
    pipeline_barrier_(cmd, img, oldLayout, newLayout, index);
}

std::vector<size_t> RenderGraph::topo_sort_() const {
    // 构建依赖图：pass i 依赖 pass j 当且仅当 i 读 j 写的资源
    const size_t n = passes_.size();
    std::vector<std::vector<size_t>> adj(n);
    std::vector<int> inDeg(n, 0);

    for (size_t i = 0; i < n; ++i) {
        for (RGImage* write : passes_[i].writes) {
            if (!write)
                continue;
            for (size_t j = 0; j < n; ++j) {
                if (i == j)
                    continue;
                bool readsWrite = false;
                for (RGImage* read : passes_[j].reads) {
                    if (read == write) {
                        readsWrite = true;
                        break;
                    }
                }
                if (readsWrite) {
                    adj[i].push_back(j);  // i -> j 表示 i 必须先于 j
                    inDeg[j]++;
                }
            }
        }
    }

    // Kahn 拓扑排序
    std::vector<size_t> order;
    order.reserve(n);
    std::vector<size_t> queue;
    for (size_t i = 0; i < n; ++i) {
        if (inDeg[i] == 0)
            queue.push_back(i);
    }

    while (!queue.empty()) {
        size_t u = queue.back();
        queue.pop_back();
        order.push_back(u);
        for (size_t v : adj[u]) {
            if (--inDeg[v] == 0)
                queue.push_back(v);
        }
    }

    if (order.size() != n) {
        LUMEN_LOG_ERROR("RenderGraph: 检测到循环依赖，无法拓扑排序");
        order.clear();
        for (size_t i = 0; i < n; ++i)
            order.push_back(i);
    }
    return order;
}

void RenderGraph::execute(VkCommandBuffer cmd, uint32_t swapchainImageIndex) {
    if (!ctx_ || passes_.empty())
        return;

    std::vector<size_t> order = topo_sort_();

    for (size_t idx : order) {
        const RGPass& pass = passes_[idx];

        // 将 reads 转到 SHADER_READ_ONLY
        for (RGImage* img : pass.reads) {
            if (img)
                transition_image_(cmd, img,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  img->is_swapchain() ? swapchainImageIndex : 0);
        }

        // 将 writes 转到 ATTACHMENT
        for (RGImage* img : pass.writes) {
            if (!img)
                continue;
            uint32_t subIndex = img->is_swapchain() ? swapchainImageIndex : 0;
            VkImageLayout targetLayout = img->isDepth
                ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            transition_image_(cmd, img, targetLayout, subIndex);
        }

        // 执行 Pass
        if (pass.execute)
            pass.execute(cmd, swapchainImageIndex);

        // 更新 layout 跟踪：RenderPass 会设置 finalLayout
        // 离屏 color 通常 finalLayout=SHADER_READ_ONLY，swapchain 为 PRESENT_SRC
        for (RGImage* img : pass.writes) {
            if (!img)
                continue;
            uint32_t subIndex = img->is_swapchain() ? swapchainImageIndex : 0;
            VkImageLayout finalLayout =
                img->is_swapchain() ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                : img->isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                              : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            img->set_layout(subIndex, finalLayout);
        }
    }
}

} // namespace lumen::render
