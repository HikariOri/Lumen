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
 * - 每次 Layout 变化插入一个 `vk::ImageMemoryBarrier`
 *
 * ⚠️ 注意：
 * - 未处理 UAV / storage image 写后读 hazard
 * - 未区分不同 shader stage（统一 fragment）
 * - 未合并 barrier（存在冗余）
 *
 * 👉 属于“可用但非最优”的实现
 */

#include "render/pass/render_graph.hpp"

#include "core/log/logger.hpp"
#include "render/context.hpp"
#include "render/resource/image.hpp"
#include "render/swapchain.hpp"

namespace lumen::render {

namespace {

/**
 * @brief 根据 RGImage 推导 aspect mask
 *
 * @param img RenderGraph 图像
 * @return `vk::ImageAspectFlags`
 *
 * @note
 * - 颜色图：`vk::ImageAspectFlagBits::eColor`
 * - 深度图：DEPTH / DEPTH|STENCIL
 */
[[nodiscard]] vk::ImageAspectFlags aspect_mask_for_rg_image(const RGImage &img) {
    if (!img.isDepth) {
        return vk::ImageAspectFlagBits::eColor;
    }
    switch (img.format) {
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint:
        return vk::ImageAspectFlagBits::eDepth |
               vk::ImageAspectFlagBits::eStencil;
    default:
        return vk::ImageAspectFlagBits::eDepth;
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
    out.extent = vk::Extent2D { img.width(), img.height() };
    out.isDepth = asDepth;
    out.currentLayout = vk::ImageLayout::eUndefined;
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
                                vk::ImageLayout::eUndefined);
    return out;
}

/**
 * @brief 获取图像句柄
 */
vk::Image RGImage::image_at(uint32_t index) const {
    if (type == RGImageType::Swapchain && swapchain_) {
        return swapchain_->image(index);
    }
    return image;
}

/**
 * @brief 获取图像视图
 */
vk::ImageView RGImage::view_at(uint32_t index) const {
    if (type == RGImageType::Swapchain && swapchain_) {
        return swapchain_->image_view(index);
    }
    return view;
}

/**
 * @brief 获取当前 Layout
 */
vk::ImageLayout RGImage::layout_at(uint32_t index) const {
    if (type == RGImageType::Swapchain && index < perIndexLayouts_.size()) {
        return perIndexLayouts_[index];
    }
    return currentLayout;
}

/**
 * @brief 设置 Layout
 */
void RGImage::set_layout(uint32_t index, vk::ImageLayout layout) {
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
void RenderGraph::pipeline_barrier_(vk::CommandBuffer cmd, RGImage *img,
                                    vk::ImageLayout oldLayout,
                                    vk::ImageLayout newLayout, uint32_t index) {
    if (!img || !static_cast<bool>(img->image_at(index))) {
        return;
    }
    if (oldLayout == newLayout) {
        return;
    }

    vk::ImageMemoryBarrier barrier {};
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

    vk::PipelineStageFlags srcStage =
        vk::PipelineStageFlagBits::eTopOfPipe;
    vk::PipelineStageFlags dstStage =
        vk::PipelineStageFlagBits::eTopOfPipe;
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = {};

    // --- src ---
    if (oldLayout == vk::ImageLayout::eColorAttachmentOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    } else if (oldLayout ==
               vk::ImageLayout::eDepthStencilAttachmentOptimal) {
        barrier.srcAccessMask =
            vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        srcStage = vk::PipelineStageFlagBits::eLateFragmentTests;
    } else if (oldLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        srcStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else if (oldLayout == vk::ImageLayout::ePresentSrcKHR) {
        srcStage = vk::PipelineStageFlagBits::eBottomOfPipe;
    }

    // --- dst ---
    if (newLayout == vk::ImageLayout::eColorAttachmentOptimal) {
        barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    } else if (newLayout ==
               vk::ImageLayout::eDepthStencilAttachmentOptimal) {
        barrier.dstAccessMask =
            vk::AccessFlagBits::eDepthStencilAttachmentRead |
            vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        dstStage = vk::PipelineStageFlagBits::eEarlyFragmentTests;
    } else if (newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        dstStage = vk::PipelineStageFlagBits::eFragmentShader;
    } else if (newLayout == vk::ImageLayout::ePresentSrcKHR) {
        dstStage = vk::PipelineStageFlagBits::eBottomOfPipe;
    }

    cmd.pipelineBarrier(srcStage, dstStage, {}, 0, nullptr, 0, nullptr, 1,
                        &barrier);

    img->set_layout(index, newLayout);
}

/**
 * @brief Layout 转换封装
 */
void RenderGraph::transition_image_(vk::CommandBuffer cmd, RGImage *img,
                                      vk::ImageLayout newLayout,
                                      uint32_t index) {
    if (!img || !static_cast<bool>(img->image_at(index))) {
        return;
    }

    const vk::ImageLayout oldLayout = img->layout_at(index);
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
void RenderGraph::execute(vk::CommandBuffer cmd,
                          uint32_t swapchainImageIndex) {
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
                transition_image_(cmd, img,
                                  vk::ImageLayout::eShaderReadOnlyOptimal,
                                  img->is_swapchain() ? swapchainImageIndex
                                                      : 0);
            }
        }

        // --- writes ---
        for (RGImage *img : pass.writes) {
            if (!img) {
                continue;
            }

            uint32_t subIndex = img->is_swapchain() ? swapchainImageIndex : 0;

            const vk::ImageLayout targetLayout =
                img->isDepth ? vk::ImageLayout::eDepthStencilAttachmentOptimal
                             : vk::ImageLayout::eColorAttachmentOptimal;

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

            vk::ImageLayout finalLayout {};
            if (img->is_swapchain()) {
                finalLayout = vk::ImageLayout::ePresentSrcKHR;
            } else if (img->isDepth) {
                finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            } else {
                finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            }

            img->set_layout(subIndex, finalLayout);
        }
    }
}

} // namespace lumen::render
