/**
 * @file render_graph.cpp
 * @brief `vulkan::RenderGraph` 实现。
 */

#include "vulkan/render_graph.hpp"

#include "core/log/logger.hpp"
#include "vulkan/context.hpp"

#include <unordered_set>
#include <vector>

namespace vulkan {

namespace {

[[nodiscard]] VkImageAspectFlags depth_aspect_mask(const VkFormat format) {
    switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    default:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
}

void record_image_barrier(const VkCommandBuffer cmd, const VkImage image,
                          const VkImageAspectFlags aspect,
                          const VkImageLayout old_layout,
                          const VkImageLayout new_layout,
                          const VkPipelineStageFlags src_stage,
                          const VkPipelineStageFlags dst_stage,
                          const VkAccessFlags src_access,
                          const VkAccessFlags dst_access) {
    if (image == VK_NULL_HANDLE || old_layout == new_layout) {
        return;
    }
    VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr,
                         1, &barrier);
}

void barrier_to_undefined_for_attachment(const VkCommandBuffer cmd,
                                         const VkImage image,
                                         const VkImageAspectFlags aspect,
                                         const VkImageLayout old_layout) {
    if (image == VK_NULL_HANDLE || old_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        return;
    }
    VkPipelineStageFlags src_stage { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
    VkAccessFlags src_access { 0 };
    switch (old_layout) {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        src_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        src_access = VK_ACCESS_SHADER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        src_stage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        src_access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        src_access = VK_ACCESS_SHADER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        src_access = 0;
        break;
    default:
        src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        break;
    }
    record_image_barrier(cmd, image, aspect, old_layout,
                         VK_IMAGE_LAYOUT_UNDEFINED, src_stage,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, src_access, 0);
}

void barrier_color_to_shader_read(const VkCommandBuffer cmd,
                                  const Image *owned_image, const VkImage image,
                                  const VkImageLayout old_layout) {
    if (image == VK_NULL_HANDLE) {
        return;
    }
    if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        return;
    }
    if (owned_image != nullptr) {
        owned_image->record_layout_barrier(
            cmd, old_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        return;
    }
    record_image_barrier(
        cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, old_layout,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
}

void barrier_depth_to_shader_read(const VkCommandBuffer cmd,
                                  const Image *owned_image, const VkImage image,
                                  const VkFormat depth_format,
                                  const VkImageLayout old_layout) {
    if (image == VK_NULL_HANDLE) {
        return;
    }
    if (old_layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
        return;
    }
    const VkImageAspectFlags aspect = depth_aspect_mask(depth_format);
    if (owned_image != nullptr) {
        owned_image->record_layout_barrier(
            cmd, old_layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT, aspect);
        return;
    }
    record_image_barrier(
        cmd, image, aspect, old_layout,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT);
}

void barrier_writes_to_undefined(const RgResources &res, const VkCommandBuffer cmd,
                                 const std::uint32_t id,
                                 const RgResourceType type,
                                 const VkFormat depth_format_if_any) {
    const RgResourceHandle h { .id = id };
    const VkImage img = res.barrier_vk_image(h);
    if (img == VK_NULL_HANDLE) {
        return;
    }
    const VkImageLayout old_layout = res.layout(h);
    const Image *owned = res.try_owned_image(h);
    if (owned != nullptr) {
        if (type == RgResourceType::Depth) {
            const VkPipelineStageFlags src_st =
                old_layout == VK_IMAGE_LAYOUT_UNDEFINED
                    ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                    : VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            const VkAccessFlags src_acc =
                old_layout == VK_IMAGE_LAYOUT_UNDEFINED
                    ? 0
                    : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            owned->record_layout_barrier(
                cmd, old_layout, VK_IMAGE_LAYOUT_UNDEFINED, src_st,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, src_acc, 0,
                depth_aspect_mask(depth_format_if_any));
        } else {
            const VkPipelineStageFlags src_st =
                old_layout == VK_IMAGE_LAYOUT_UNDEFINED
                    ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                    : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            const VkAccessFlags src_acc =
                old_layout == VK_IMAGE_LAYOUT_UNDEFINED
                    ? 0
                    : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            owned->record_layout_barrier(cmd, old_layout,
                                         VK_IMAGE_LAYOUT_UNDEFINED, src_st,
                                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                         src_acc, 0);
        }
        return;
    }
    if (type == RgResourceType::Depth) {
        barrier_to_undefined_for_attachment(
            cmd, img, depth_aspect_mask(depth_format_if_any), old_layout);
    } else {
        barrier_to_undefined_for_attachment(cmd, img, VK_IMAGE_ASPECT_COLOR_BIT,
                                            old_layout);
    }
}

} // namespace

RenderGraph::RenderGraph(Context &ctx) noexcept : ctx_(ctx), resources_(ctx) {}

RenderGraph::~RenderGraph() { clear_compiled(); }

bool RenderGraph::valid_handle(const RgResourceHandle h) const noexcept {
    return resources_.valid_handle(h);
}

VkRenderPass RenderGraph::render_pass_for_pass(const std::size_t pass_index) const noexcept {
    if (pass_index >= compiled_.size() ||
        !compiled_[pass_index].render_pass.has_value()) {
        return VK_NULL_HANDLE;
    }
    return compiled_[pass_index].render_pass->vk_render_pass();
}

RgResourceType RenderGraph::resource_type(const RgResourceHandle h) const {
    return resources_.resource_type(h);
}

RenderTarget RenderGraph::resource_render_target(const RgResourceHandle h) const {
    return resources_.render_target(h);
}

void RenderGraph::destroy_compiled_passes() noexcept {
    const VkDevice dev = ctx_.device();
    if (dev == VK_NULL_HANDLE) {
        compiled_.clear();
        return;
    }
    for (CompiledPass &p : compiled_) {
        p.bundle.reset(dev);
        p.render_pass.reset();
    }
    compiled_.clear();
}

void RenderGraph::clear_compiled() { destroy_compiled_passes(); }

void RenderGraph::destroy(const VkDevice device) {
    (void)device;
    clear_compiled();
}

RgResourceHandle RenderGraph::create_texture(const std::uint32_t width,
                                             const std::uint32_t height,
                                             const VkFormat format) {
    return resources_.create_texture(width, height, format);
}

RgResourceHandle RenderGraph::create_depth(const std::uint32_t width,
                                           const std::uint32_t height) {
    return resources_.create_depth(width, height);
}

RgResourceHandle
RenderGraph::import_swapchain(const RenderTarget &rt,
                              const VkImage swapchain_image) {
    return resources_.import_swapchain(rt, swapchain_image);
}

void RenderGraph::set_resource_target(const RgResourceHandle handle,
                                      const RenderTarget &rt,
                                      const VkImage swapchain_image) {
    resources_.set_resource_target(handle, rt, swapchain_image);
}

void RenderGraph::add_pass(RgPassNode pass) {
    pass_nodes_.push_back(std::move(pass));
}

void RenderGraph::add_pass(
    std::string name,
    std::vector<std::pair<RgResourceHandle, RgAccess>> writes,
    std::vector<std::pair<RgResourceHandle, RgAccess>> reads,
    RgExecuteFunc execute) {
    pass_nodes_.push_back(RgPassNode { std::move(name), std::move(writes),
                                       std::move(reads), std::move(execute) });
}

void RenderGraph::dump_graph() const {
    LUMEN_LOG_INFO("=== RenderGraph: registered_passes={} ===",
                   pass_nodes_.size());
    for (const RgPassNode &n : pass_nodes_) {
        LUMEN_LOG_INFO("  Pass \"{}\": writes={} reads={}", n.name,
                       n.writes.size(), n.reads.size());
    }
    LUMEN_LOG_INFO("=== end RenderGraph ===");
}

std::expected<void, std::string> RenderGraph::build_bundle_from_writes_(
    const std::vector<std::pair<RgResourceHandle, RgAccess>> &writes,
    RenderTargetBundle &bundle) const {
    std::vector<RgResourceHandle> colors;
    std::vector<RgResourceHandle> depths;
    for (const auto &[h, access] : writes) {
        if (access != RgAccess::Write) {
            continue;
        }
        if (!resources_.valid_handle(h)) {
            return std::unexpected(
                std::string("RenderGraph: invalid write handle"));
        }
        if (resources_.resource_type(h) == RgResourceType::Depth) {
            depths.push_back(h);
        } else {
            colors.push_back(h);
        }
    }
    if (colors.empty() && depths.empty()) {
        return std::unexpected(
            std::string("RenderGraph: pass has no write attachments"));
    }
    if (depths.size() > 1U) {
        return std::unexpected(
            std::string("RenderGraph: at most one depth write per pass"));
    }
    for (const RgResourceHandle h : colors) {
        const RenderTarget rt = resources_.render_target(h);
        if (!bundle.add_color_target(rt)) {
            return std::unexpected(
                std::string("RenderGraph: add_color_target failed"));
        }
    }
    if (!depths.empty()) {
        const RenderTarget dt = resources_.render_target(depths.front());
        if (!bundle.set_depth_target(dt)) {
            return std::unexpected(
                std::string("RenderGraph: set_depth_target failed"));
        }
    }
    return {};
}

bool RenderGraph::pass_writes_swapchain_(const RgPassNode &node) const {
    for (const auto &[h, acc] : node.writes) {
        if (acc != RgAccess::Write) {
            continue;
        }
        if (resources_.valid_handle(h) &&
            resources_.resource_type(h) == RgResourceType::Swapchain) {
            return true;
        }
    }
    return false;
}

std::expected<void, std::string> RenderGraph::prepare_frame(const VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        return std::unexpected(std::string("RenderGraph::prepare_frame: null device"));
    }
    for (CompiledPass &p : compiled_) {
        if (!p.has_swapchain_write) {
            continue;
        }
        p.bundle.reset(device);
        if (auto e = build_bundle_from_writes_(p.writes, p.bundle); !e.has_value()) {
            return e;
        }
    }
    return {};
}

std::expected<void, std::string> RenderGraph::compile() {
    const VkDevice device = ctx_.device();
    if (device == VK_NULL_HANDLE) {
        return std::unexpected(std::string("RenderGraph::compile: null device"));
    }
    destroy_compiled_passes();

    for (const RgPassNode &node : pass_nodes_) {
        CompiledPass p {};
        p.name = node.name;
        p.writes = node.writes;
        p.reads = node.reads;
        p.execute = node.execute;
        p.has_swapchain_write = pass_writes_swapchain_(node);
        if (auto e = build_bundle_from_writes_(node.writes, p.bundle);
            !e.has_value()) {
            destroy_compiled_passes();
            return e;
        }
        const bool present = p.bundle.is_output_to_swapchain();
        auto rp = RenderPass::create(device, p.bundle, present);
        if (!rp.has_value()) {
            destroy_compiled_passes();
            return std::unexpected(rp.error());
        }
        p.render_pass = std::move(*rp);
        compiled_.push_back(std::move(p));
    }

    resources_.reset_all_layouts(VK_IMAGE_LAYOUT_UNDEFINED);
    return {};
}

std::expected<void, std::string>
RenderGraph::execute(const VkCommandBuffer cmd, const VkDevice device) {
    if (cmd == VK_NULL_HANDLE || device == VK_NULL_HANDLE) {
        return std::unexpected(
            std::string("RenderGraph::execute: null cmd or device"));
    }
    if (compiled_.size() != pass_nodes_.size()) {
        return std::unexpected(
            std::string("RenderGraph::execute: compile() not called or stale"));
    }

    for (CompiledPass &p : compiled_) {
        std::unordered_set<std::uint32_t> write_ids;
        for (const auto &[h, acc] : p.writes) {
            if (acc == RgAccess::Write && resources_.valid_handle(h)) {
                write_ids.insert(h.id);
            }
        }

        for (const std::uint32_t id : write_ids) {
            const RgResourceHandle wh { .id = id };
            const RgResourceType tp = resources_.resource_type(wh);
            const VkFormat depth_fmt =
                tp == RgResourceType::Depth
                    ? resources_.render_target(wh).format
                    : VK_FORMAT_UNDEFINED;
            barrier_writes_to_undefined(resources_, cmd, id, tp, depth_fmt);
            resources_.set_layout(wh, VK_IMAGE_LAYOUT_UNDEFINED);
        }

        for (const auto &[h, acc] : p.reads) {
            if (acc != RgAccess::Read || !resources_.valid_handle(h)) {
                continue;
            }
            if (write_ids.contains(h.id)) {
                continue;
            }
            const VkImage img = resources_.barrier_vk_image(h);
            const Image *owned = resources_.try_owned_image(h);
            const VkImageLayout old_layout = resources_.layout(h);
            if (resources_.resource_type(h) == RgResourceType::Depth) {
                barrier_depth_to_shader_read(cmd, owned, img,
                                             resources_.render_target(h).format,
                                             old_layout);
                resources_.set_layout(
                    h, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            } else {
                barrier_color_to_shader_read(cmd, owned, img, old_layout);
                resources_.set_layout(h, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
        }

        if (!p.render_pass.has_value()) {
            return std::unexpected(
                std::string("RenderGraph::execute: missing RenderPass"));
        }
        const VkRenderPass vk_rp = p.render_pass->vk_render_pass();
        auto fb_result = p.bundle.get_framebuffer(device, vk_rp);
        if (!fb_result.has_value()) {
            return std::unexpected(fb_result.error());
        }
        const VkFramebuffer framebuffer = *fb_result;

        VkRenderPassBeginInfo begin { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        begin.renderPass = vk_rp;
        begin.framebuffer = framebuffer;
        begin.renderArea.offset = { 0, 0 };
        begin.renderArea.extent = { p.bundle.width(), p.bundle.height() };
        const std::uint32_t color_count =
            static_cast<std::uint32_t>(p.bundle.color_targets().size());
        const std::uint32_t depth_clear = p.bundle.has_depth() ? 1U : 0U;
        std::vector<VkClearValue> clear_values(
            static_cast<std::size_t>(color_count + depth_clear), VkClearValue {});
        begin.clearValueCount = color_count + depth_clear;
        begin.pClearValues = clear_values.data();

        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
        if (p.execute) {
            p.execute(cmd, p.bundle);
        }
        vkCmdEndRenderPass(cmd);

        for (const std::uint32_t id : write_ids) {
            const RgResourceHandle wh { .id = id };
            const RgResourceType tp = resources_.resource_type(wh);
            if (tp == RgResourceType::Swapchain) {
                resources_.set_layout(wh, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
            } else if (tp == RgResourceType::Depth) {
                resources_.set_layout(
                    wh, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            } else {
                resources_.set_layout(wh, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            }
        }
    }

    return {};
}

} // namespace vulkan
