/**
 * @file render_graph.cpp
 * @brief RenderGraph 实现：拓扑排序、依赖分析、Layout 转换与 Pipeline Barrier
 *
 * ---
 * @section rg_impl_overview 实现概述
 *
 * 本文件实现 RenderGraph 的核心逻辑：
 *
 * - RGImage：资源封装（Texture / Swapchain）
 * - 拓扑排序：根据读写关系生成执行顺序
 * - 同步系统：
 *   - Image Layout 管理
 *   - Pipeline Barrier 插入
 *
 * ---
 * @section rg_impl_sync 同步模型（重要）
 *
 * 当前实现为“简化同步模型”：
 *
 * - 基于 Image Layout 推导 access / stage
 * - 每次 Layout 变化插入一个 VkImageMemoryBarrier
 *
 * ⚠️ 注意：
 * - 未处理 UAV / storage image 写后读 hazard
 * - 未区分不同 shader stage（统一 fragment）
 * - 未合并 barrier（存在冗余）
 *
 * 👉 属于“可用但非最优”的实现
 */

#include "render/pass/render_graph.hpp"

#include "core/logger.hpp"
#include "render/context.hpp"
#include "render/resource/image.hpp"
#include "render/swapchain.hpp"

namespace lumen::render {

namespace {

/**
 * @brief 根据 RGImage 推导 aspect mask
 *
 * @param img RenderGraph 图像
 * @return VkImageAspectFlags
 *
 * @note
 * - 颜色图：VK_IMAGE_ASPECT_COLOR_BIT
 * - 深度图：DEPTH / DEPTH|STENCIL
 */
[[nodiscard]] VkImageAspectFlags aspect_mask_for_rg_image(const RGImage &img) {
    if (!img.isDepth) {
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
    switch (img.format) {
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default: return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
}

} // namespace

// -----------------------------------------------------------------------------
// RGImage
// -----------------------------------------------------------------------------

/**
 * @brief 从离屏 Image 构造 RGImage
 *
 * @note 初始 Layout = UNDEFINED（由 RenderGraph 管理）
 */
RGImage RGImage::from_texture(const Image &img, bool asDepth) {
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

/**
 * @brief 从 Swapchain 构造 RGImage
 *
 * @note 每个 backbuffer 独立维护 Layout
 */
RGImage RGImage::from_swapchain(const Swapchain &swapchain) {
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

/**
 * @brief 获取 VkImage
 */
VkImage RGImage::image_at(uint32_t index) const {
    if (type == RGImageType::Swapchain && swapchain_) {
        return swapchain_->image(index);
    }
    return image;
}

/**
 * @brief 获取 VkImageView
 */
VkImageView RGImage::view_at(uint32_t index) const {
    if (type == RGImageType::Swapchain && swapchain_) {
        return swapchain_->image_view(index);
    }
    return view;
}

/**
 * @brief 获取当前 Layout
 */
VkImageLayout RGImage::layout_at(uint32_t index) const {
    if (type == RGImageType::Swapchain && index < perIndexLayouts_.size()) {
        return perIndexLayouts_[index];
    }
    return currentLayout;
}

/**
 * @brief 设置 Layout
 */
void RGImage::set_layout(uint32_t index, VkImageLayout layout) {
    if (type == RGImageType::Swapchain && index < perIndexLayouts_.size()) {
        perIndexLayouts_[index] = layout;
    } else {
        currentLayout = layout;
    }
}

/**
 * @brief 获取图像数量
 */
uint32_t RGImage::image_count() const {
    if (type == RGImageType::Swapchain && swapchain_) {
        return swapchain_->image_count();
    }
    return 1;
}

// -----------------------------------------------------------------------------
// RenderGraph
// -----------------------------------------------------------------------------

RenderGraph::RenderGraph(const Context *ctx) : ctx_(ctx) {}

/**
 * @brief 添加 Pass
 *
 * @note 会使 compile 结果失效
 */
void RenderGraph::add_pass(const RGPass &pass) {
    passes_.push_back(pass);
    compile_stale_ = true;
    compile_ok_ = false;
}

/**
 * @brief 清空所有 Pass
 */
void RenderGraph::clear() {
    passes_.clear();
    execution_order_.clear();
    compile_stale_ = true;
    compile_ok_ = false;
}

/**
 * @brief 插入 Image Memory Barrier
 *
 * @param cmd 命令缓冲
 * @param img 图像
 * @param oldLayout 旧 Layout
 * @param newLayout 新 Layout
 * @param index Swapchain index
 *
 * ---
 * @section barrier_logic Barrier 推导规则
 *
 * 当前实现基于 Layout 推导：
 *
 * oldLayout → srcAccess + srcStage
 * newLayout → dstAccess + dstStage
 *
 * ---
 * @warning
 * - 未覆盖所有 Vulkan Layout（仅常用路径）
 * - 未处理 Compute / Transfer pipeline
 */
void RenderGraph::pipeline_barrier_(VkCommandBuffer cmd, RGImage *img,
                                    VkImageLayout oldLayout,
                                    VkImageLayout newLayout, uint32_t index) {
    if (!img || img->image_at(index) == VK_NULL_HANDLE) {
        return;
    }
    if (oldLayout == newLayout) {
        return;
    }

    VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = img->image_at(index);
    barrier.subresourceRange.aspectMask = aspect_mask_for_rg_image(*img);
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = 0;

    // --- src ---
    if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        srcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    // --- dst ---
    if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);

    img->set_layout(index, newLayout);
}

/**
 * @brief Layout 转换封装
 */
void RenderGraph::transition_image_(VkCommandBuffer cmd, RGImage *img,
                                    VkImageLayout newLayout, uint32_t index) {
    if (!img || img->image_at(index) == VK_NULL_HANDLE) {
        return;
    }

    VkImageLayout oldLayout = img->layout_at(index);
    if (oldLayout == newLayout) {
        return;
    }

    pipeline_barrier_(cmd, img, oldLayout, newLayout, index);
}

/**
 * @brief 拓扑排序（Kahn 算法）
 *
 * ---
 * @section topo_rule 依赖规则
 *
 * - A 写 → B 读 ⇒ A → B
 *
 * ---
 * @return 执行顺序
 * @note 若存在环，返回 size < passes_.size()
 */
std::vector<size_t> RenderGraph::topo_sort_() const {
    const size_t n = passes_.size();
    std::vector<std::vector<size_t>> adj(n);
    std::vector<int> inDeg(n, 0);

    for (size_t i = 0; i < n; ++i) {
        for (RGImage *write : passes_[i].writes) {
            if (!write) {
                continue;
            }

            for (size_t j = 0; j < n; ++j) {
                if (i == j) {
                    continue;
                }

                for (RGImage *read : passes_[j].reads) {
                    if (read == write) {
                        adj[i].push_back(j);
                        inDeg[j]++;
                        break;
                    }
                }
            }
        }
    }

    std::vector<size_t> order;
    std::vector<size_t> queue;

    for (size_t i = 0; i < n; ++i) {
        if (inDeg[i] == 0) {
            queue.push_back(i);
        }
    }

    while (!queue.empty()) {
        size_t u = queue.back();
        queue.pop_back();
        order.push_back(u);

        for (size_t v : adj[u]) {
            if (--inDeg[v] == 0) {
                queue.push_back(v);
            }
        }
    }

    return order;
}

/**
 * @brief 编译 RenderGraph
 */
bool RenderGraph::compile() {
    if (passes_.empty()) {
        execution_order_.clear();
        compile_ok_ = true;
        compile_stale_ = false;
        return true;
    }

    std::vector<size_t> order = topo_sort_();

    if (order.size() != passes_.size()) {
        LUMEN_LOG_ERROR("RenderGraph: 检测到循环依赖");
        execution_order_.clear();
        compile_ok_ = false;
        compile_stale_ = false;
        return false;
    }

    execution_order_ = std::move(order);
    compile_ok_ = true;
    compile_stale_ = false;
    return true;
}

/**
 * @brief 执行 RenderGraph
 *
 * ---
 * @section exec_flow 执行流程
 *
 * 对每个 Pass：
 *
 * 1. 处理 reads → 转为 SHADER_READ_ONLY
 * 2. 处理 writes → 转为 attachment
 * 3. 执行 pass
 * 4. 写资源更新最终 layout
 */
void RenderGraph::execute(VkCommandBuffer cmd, uint32_t swapchainImageIndex) {
    if (!ctx_ || passes_.empty()) {
        return;
    }

    if (compile_stale_ || !compile_ok_) {
        if (!compile()) {
            return;
        }
    }

    for (size_t idx : execution_order_) {
        const RGPass &pass = passes_[idx];

        // --- reads ---
        for (RGImage *img : pass.reads) {
            if (img) {
                transition_image_(
                    cmd, img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    img->is_swapchain() ? swapchainImageIndex : 0);
            }
        }

        // --- writes ---
        for (RGImage *img : pass.writes) {
            if (!img) {
                continue;
            }

            uint32_t subIndex = img->is_swapchain() ? swapchainImageIndex : 0;

            VkImageLayout targetLayout =
                img->isDepth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                             : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            transition_image_(cmd, img, targetLayout, subIndex);
        }

        // --- execute ---
        if (pass.execute) {
            pass.execute(cmd, swapchainImageIndex);
        }

        // --- finalize layout ---
        for (RGImage *img : pass.writes) {
            if (!img) {
                continue;
            }

            uint32_t subIndex = img->is_swapchain() ? swapchainImageIndex : 0;

            VkImageLayout finalLayout =
                img->is_swapchain() ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                : img->isDepth
                    ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            img->set_layout(subIndex, finalLayout);
        }
    }
}

} // namespace lumen::render
