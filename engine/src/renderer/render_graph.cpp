#include "renderer/render_graph.hpp"

#include "core/log/logger.hpp"

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vulkan/vulkan_core.h>

namespace renderer {

namespace {

VkImageAspectFlags aspect_mask_for_texture(const TextureResource &tex) {
    if (tex.type == TextureType::Depth) {
        switch (tex.desc.format) {
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        default:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        }
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

void sync_layout_after_color_output(TextureResource &tex, VkImageLayout fin) {
    tex.currentLayout = fin;
    if (fin == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        tex.lastStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        tex.lastAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    } else if (fin == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        tex.lastStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        tex.lastAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    } else {
        tex.lastStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        tex.lastAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    }
}

} // namespace

bool RenderGraph::texture_read_later(TextureHandle h,
                                   std::size_t after_user_pass_index) const {
    for (std::size_t j = after_user_pass_index + 1; j < user_passes_.size();
         ++j) {
        for (auto r : user_passes_[j].inputs) {
            if (r == h) {
                return true;
            }
        }
    }
    return false;
}

VkImageLayout RenderGraph::final_layout_for_color(const TextureResource &tex,
                                                  TextureHandle h,
                                                  std::size_t user_pass_index) const {
    if (tex.type == TextureType::Swapchain) {
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    if (texture_read_later(h, user_pass_index)) {
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}

RenderGraph::~RenderGraph() {
    destroy_compiled();
    for (auto &tex : textures) {
        if (tex.allocation == nullptr) {
            continue;
        }
        if (tex.view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, tex.view, nullptr);
        }
        vmaDestroyImage(allocator, tex.image, tex.allocation);
    }
}

void RenderGraph::set_subpass_merging(bool enable) {
    subpass_merging_enabled_ = enable;
}

TextureHandle RenderGraph::createTexture(const TextureDesc &desc) {
    TextureResource r;
    r.desc = desc;
    r.type = TextureType::RenderTarget;
    textures.push_back(r);
    return static_cast<TextureHandle>(textures.size() - 1);
}

TextureHandle RenderGraph::importSwapchain(const TextureDesc &desc,
                                           std::vector<VkImage> images,
                                           std::vector<VkImageView> views) {
    TextureResource r;
    r.desc = desc;
    r.type = TextureType::Swapchain;
    swapchain_images_ = std::move(images);
    swapchain_views_ = std::move(views);
    if (!swapchain_images_.empty()) {
        r.image = swapchain_images_[0];
    }
    if (!swapchain_views_.empty()) {
        r.view = swapchain_views_[0];
    }
    textures.push_back(r);
    return static_cast<TextureHandle>(textures.size() - 1);
}

TextureHandle RenderGraph::createDepth(const TextureDesc &desc) {
    TextureResource r;
    r.desc = desc;
    r.type = TextureType::Depth;
    if ((r.desc.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0) {
        r.desc.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    textures.push_back(r);
    return static_cast<TextureHandle>(textures.size() - 1);
}

void RenderGraph::add_pass(const std::string &name, const PassInfo &info,
                           std::function<void(VkCommandBuffer)> record_draws) {
    UserPass up;
    up.name = name;
    up.inputs = info.reads;
    up.colors = info.writes;
    up.depth = info.depth;
    up.extent = info.extent;
    up.clear_color = info.clearColor;
    up.clear_depth = info.clearDepth;
    up.clear_stencil = info.clearStencil;
    up.enable_clear_depth = info.enableClearDepth;
    up.exec = std::move(record_draws);
    user_passes_.push_back(std::move(up));
}

bool RenderGraph::set_pass_clear_color(const std::string &name,
                                       VkClearColorValue color) {
    auto it = std::find_if(user_passes_.begin(), user_passes_.end(),
                           [&](const UserPass &p) { return p.name == name; });
    if (it == user_passes_.end()) {
        return false;
    }

    it->clear_color = color;
    const auto &target_colors = it->colors;
    if (target_colors.empty()) {
        return true;
    }

    for (auto &batch : compiled_batches_) {
        const std::size_t n =
            std::min(batch.clear_values.size(), batch.clear_value_handles.size());
        for (std::size_t i = 0; i < n; ++i) {
            const TextureHandle h = batch.clear_value_handles[i];
            if (h == UINT32_MAX || h >= textures.size() ||
                textures[h].type == TextureType::Depth) {
                continue;
            }
            if (std::find(target_colors.begin(), target_colors.end(), h) !=
                target_colors.end()) {
                batch.clear_values[i].color = color;
            }
        }
    }

    return true;
}

TextureResource &RenderGraph::getTexture(TextureHandle h) {
    return textures[h];
}

void RenderGraph::resize_swapchain(TextureHandle handle,
                                   const TextureDesc &desc,
                                   std::vector<VkImage> images,
                                   std::vector<VkImageView> views) {
    if (handle >= textures.size()) {
        return;
    }
    auto &t = textures[handle];
    if (t.type != TextureType::Swapchain) {
        return;
    }

    swapchain_images_ = std::move(images);
    swapchain_views_ = std::move(views);
    if (!swapchain_images_.empty()) {
        t.image = swapchain_images_[0];
    }
    if (!swapchain_views_.empty()) {
        t.view = swapchain_views_[0];
    }
    t.desc = desc;
    t.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    t.lastStage = 0;
    t.lastAccess = 0;
    if (!swapchain_views_.empty()) {
        const std::uint32_t count =
            static_cast<std::uint32_t>(swapchain_views_.size());
        for (auto &batch : compiled_batches_) {
            if (!batch.framebuffers.empty() &&
                batch.framebuffers.size() == count) {
                // noop, rebuild below
            }
        }
    }
    rebuild_framebuffers_referencing(handle);
}

void RenderGraph::resize_renderpass(TextureHandle handle, std::uint32_t width,
                                    std::uint32_t height) {
    if (handle >= textures.size()) {
        return;
    }
    auto &t = textures[handle];
    if (t.type == TextureType::Swapchain) {
        return;
    }
    if (width == 0 || height == 0) {
        return;
    }

    t.desc.width = width;
    t.desc.height = height;
    if (t.image != VK_NULL_HANDLE && t.allocation != nullptr) {
        if (t.view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, t.view, nullptr);
            t.view = VK_NULL_HANDLE;
        }
        vmaDestroyImage(allocator, t.image, t.allocation);
        t.image = VK_NULL_HANDLE;
        t.allocation = nullptr;
    }
    create_all_resources();
    t.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    t.lastStage = 0;
    t.lastAccess = 0;
    rebuild_framebuffers_referencing(handle);
}

void RenderGraph::resetLayouts() {
    for (auto &tex : textures) {
        tex.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        tex.lastStage = 0;
        tex.lastAccess = 0;
    }
}

VkRenderPass RenderGraph::render_pass_named(const std::string &name) const {
    auto it = name_to_render_pass_.find(name);
    if (it != name_to_render_pass_.end()) {
        return it->second;
    }
    return VK_NULL_HANDLE;
}

uint32_t RenderGraph::subpass_index_for(const std::string &name) const {
    auto it = name_to_subpass_.find(name);
    if (it != name_to_subpass_.end()) {
        return it->second;
    }
    return 0;
}

VkExtent2D RenderGraph::extent_for_user_pass(const UserPass &up) const {
    if (up.extent.width > 0 && up.extent.height > 0) {
        return up.extent;
    }
    if (!up.colors.empty() && up.colors[0] < textures.size()) {
        const auto &t = textures[up.colors[0]];
        return { t.desc.width, t.desc.height };
    }
    if (!up.inputs.empty() && up.inputs[0] < textures.size()) {
        const auto &t = textures[up.inputs[0]];
        return { t.desc.width, t.desc.height };
    }
    return { 1, 1 };
}

void RenderGraph::create_all_resources() {
    for (auto &tex : textures) {
        if (tex.image != VK_NULL_HANDLE) {
            continue;
        }
        if (tex.type == TextureType::Swapchain) {
            continue;
        }

        VkImageCreateInfo ii { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                               .imageType = VK_IMAGE_TYPE_2D,
                               .format = tex.desc.format,
                               .extent = { tex.desc.width, tex.desc.height, 1 },
                               .mipLevels = 1,
                               .arrayLayers = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .tiling = VK_IMAGE_TILING_OPTIMAL,
                               .usage = tex.desc.usage };

        VmaAllocationCreateInfo ai { .usage = VMA_MEMORY_USAGE_GPU_ONLY };
        vmaCreateImage(allocator, &ii, &ai, &tex.image, &tex.allocation,
                       nullptr);

        VkImageViewCreateInfo vi {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = tex.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = tex.desc.format,
            .subresourceRange = { aspect_mask_for_texture(tex), 0, 1, 0, 1 }
        };
        vkCreateImageView(device, &vi, nullptr, &tex.view);
    }
}

void RenderGraph::destroy_compiled() {
    for (auto &b : compiled_batches_) {
        for (VkFramebuffer fb : b.framebuffers) {
            if (fb != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(device, fb, nullptr);
            }
        }
        b.framebuffers.clear();
        if (b.render_pass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, b.render_pass, nullptr);
            b.render_pass = VK_NULL_HANDLE;
        }
    }
    compiled_batches_.clear();
}

void RenderGraph::rebuild_batch_framebuffers(std::size_t batch_index) {
    auto &batch = compiled_batches_[batch_index];
    for (VkFramebuffer fb : batch.framebuffers) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, fb, nullptr);
        }
    }
    batch.framebuffers.clear();
    if (batch.render_pass == VK_NULL_HANDLE || batch.attachment_handles.empty()) {
        return;
    }

    std::uint32_t rebuiltWidth = UINT32_MAX;
    std::uint32_t rebuiltHeight = UINT32_MAX;
    for (const TextureHandle h : batch.attachment_handles) {
        if (h >= textures.size()) {
            continue;
        }
        const auto &t = textures[h];
        rebuiltWidth = std::min(rebuiltWidth, t.desc.width);
        rebuiltHeight = std::min(rebuiltHeight, t.desc.height);
    }
    if (rebuiltWidth != UINT32_MAX && rebuiltHeight != UINT32_MAX) {
        batch.extent.width = rebuiltWidth;
        batch.extent.height = rebuiltHeight;
    }

    bool uses_swapchain = false;
    for (const TextureHandle h : batch.attachment_handles) {
        if (h < textures.size() && textures[h].type == TextureType::Swapchain) {
            uses_swapchain = true;
            break;
        }
    }
    const uint32_t fbCount =
        uses_swapchain
            ? static_cast<uint32_t>(std::max(std::size_t { 1 }, swapchain_views_.size()))
            : 1u;
    batch.framebuffers.assign(fbCount, VK_NULL_HANDLE);
    for (uint32_t fi = 0; fi < fbCount; ++fi) {
        std::vector<VkImageView> views;
        views.reserve(batch.attachment_handles.size());
        for (const TextureHandle h : batch.attachment_handles) {
            if (h >= textures.size()) {
                continue;
            }
            const auto &tex = textures[h];
            if (tex.type == TextureType::Swapchain) {
                if (fi < swapchain_views_.size()) {
                    views.push_back(swapchain_views_[fi]);
                } else {
                    views.push_back(tex.view);
                }
            } else {
                views.push_back(tex.view);
            }
        }
        VkFramebufferCreateInfo fbci {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = batch.render_pass,
            .attachmentCount = static_cast<uint32_t>(views.size()),
            .pAttachments = views.data(),
            .width = batch.extent.width,
            .height = batch.extent.height,
            .layers = 1
        };
        vkCreateFramebuffer(device, &fbci, nullptr, &batch.framebuffers[fi]);
    }
}

void RenderGraph::rebuild_framebuffers_referencing(TextureHandle handle) {
    for (std::size_t i = 0; i < compiled_batches_.size(); ++i) {
        const auto &attachments = compiled_batches_[i].attachment_handles;
        if (std::find(attachments.begin(), attachments.end(), handle) !=
            attachments.end()) {
            rebuild_batch_framebuffers(i);
        }
    }
}

void RenderGraph::build_batch_single_subpass(CompiledBatch &batch,
                                             const UserPass &up,
                                             std::size_t user_pass_index) {
    batch.color_outputs = up.colors;
    batch.color_final_layouts.clear();
    batch.has_depth =
        (up.depth != UINT32_MAX && up.depth < textures.size());
    batch.depth_final_layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    batch.depth_handles.clear();
    if (batch.has_depth) {
        batch.depth_handles.push_back(up.depth);
    }

    for (TextureHandle h : up.colors) {
        if (h >= textures.size()) {
            continue;
        }
        batch.color_final_layouts.push_back(
            final_layout_for_color(textures[h], h, user_pass_index));
    }

    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> color_refs;

    for (TextureHandle h : up.colors) {
        if (h >= textures.size()) {
            continue;
        }
        auto &tex = textures[h];
        VkImageLayout fin = final_layout_for_color(tex, h, user_pass_index);

        VkAttachmentDescription ad {};
        ad.format = tex.desc.format;
        ad.samples = VK_SAMPLE_COUNT_1_BIT;
        ad.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ad.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ad.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        ad.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        ad.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ad.finalLayout = fin;
        attachments.push_back(ad);

        VkAttachmentReference cref {};
        cref.attachment = static_cast<uint32_t>(attachments.size() - 1);
        cref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_refs.push_back(cref);
    }

    VkAttachmentReference depth_ref {};
    if (batch.has_depth) {
        auto &dtex = textures[up.depth];
        VkAttachmentDescription ad {};
        ad.format = dtex.desc.format;
        ad.samples = VK_SAMPLE_COUNT_1_BIT;
        ad.loadOp = up.enable_clear_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                          : VK_ATTACHMENT_LOAD_OP_LOAD;
        ad.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        ad.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        ad.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        ad.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ad.finalLayout = batch.depth_final_layout;
        attachments.push_back(ad);

        depth_ref.attachment = static_cast<uint32_t>(attachments.size() - 1);
        depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    VkSubpassDescription sub {};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = static_cast<uint32_t>(color_refs.size());
    sub.pColorAttachments =
        color_refs.empty() ? nullptr : color_refs.data();
    sub.pDepthStencilAttachment =
        batch.has_depth ? &depth_ref : nullptr;

    VkRenderPassCreateInfo rpc { .sType =
                                     VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                 .attachmentCount =
                                     static_cast<uint32_t>(attachments.size()),
                                 .pAttachments = attachments.data(),
                                 .subpassCount = 1,
                                 .pSubpasses = &sub };

    vkCreateRenderPass(device, &rpc, nullptr, &batch.render_pass);

    batch.clear_values.clear();
    batch.clear_value_handles.clear();
    for (std::size_t i = 0; i < up.colors.size(); ++i) {
        VkClearValue cv {};
        cv.color = up.clear_color;
        batch.clear_values.push_back(cv);
        batch.clear_value_handles.push_back(up.colors[i]);
    }
    if (batch.has_depth) {
        VkClearValue cv {};
        cv.depthStencil.depth = up.clear_depth;
        cv.depthStencil.stencil = up.clear_stencil;
        batch.clear_values.push_back(cv);
        batch.clear_value_handles.push_back(up.depth);
    }
    batch.attachment_handles = up.colors;
    if (batch.has_depth) {
        batch.attachment_handles.push_back(up.depth);
    }

    bool uses_swapchain = false;
    for (auto h : up.colors) {
        if (h < textures.size() &&
            textures[h].type == TextureType::Swapchain) {
            uses_swapchain = true;
            break;
        }
    }
    uint32_t fb_count = 1u;
    if (uses_swapchain) {
        for (auto h : up.colors) {
            if (h < textures.size() &&
                textures[h].type == TextureType::Swapchain) {
                fb_count = std::max(
                    fb_count,
                    static_cast<uint32_t>(
                        std::max(std::size_t{1}, swapchain_views_.size())));
            }
        }
    }

    batch.framebuffers.assign(fb_count, VK_NULL_HANDLE);
    for (uint32_t fi = 0; fi < fb_count; ++fi) {
        std::vector<VkImageView> views;
        for (auto h : up.colors) {
            if (h >= textures.size()) {
                continue;
            }
            auto &tex = textures[h];
            if (tex.type == TextureType::Swapchain) {
                if (fi < swapchain_views_.size()) {
                    views.push_back(swapchain_views_[fi]);
                } else {
                    views.push_back(tex.view);
                }
            } else {
                views.push_back(tex.view);
            }
        }
        if (batch.has_depth) {
            views.push_back(textures[up.depth].view);
        }

        VkFramebufferCreateInfo fbci {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = batch.render_pass,
            .attachmentCount = static_cast<uint32_t>(views.size()),
            .pAttachments = views.data(),
            .width = batch.extent.width,
            .height = batch.extent.height,
            .layers = 1
        };
        vkCreateFramebuffer(device, &fbci, nullptr, &batch.framebuffers[fi]);
    }
}

void RenderGraph::build_batch_multi_subpass(CompiledBatch &batch,
                                            const std::vector<UserPass> &subs,
                                            std::size_t first_user_pass_index) {
    std::unordered_map<TextureHandle, uint32_t> tex_to_att;
    std::vector<TextureHandle> attachment_order;

    auto ensure_att = [&](TextureHandle h) -> uint32_t {
        if (h == UINT32_MAX || h >= textures.size()) {
            return UINT32_MAX;
        }
        auto it = tex_to_att.find(h);
        if (it != tex_to_att.end()) {
            return it->second;
        }
        const uint32_t idx = static_cast<uint32_t>(attachment_order.size());
        tex_to_att[h] = idx;
        attachment_order.push_back(h);
        return idx;
    };

    for (const auto &sp : subs) {
        for (auto h : sp.colors) {
            ensure_att(h);
        }
        if (sp.depth != UINT32_MAX) {
            ensure_att(sp.depth);
        }
        for (auto h : sp.inputs) {
            ensure_att(h);
        }
    }

    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkImageLayout> attachment_final_layouts;
    std::vector<VkClearColorValue> attachment_clear_colors;
    attachments.reserve(attachment_order.size());
    attachment_final_layouts.reserve(attachment_order.size());
    attachment_clear_colors.reserve(attachment_order.size());
    for (TextureHandle h : attachment_order) {
        auto &tex = textures[h];
        const UserPass *producer = nullptr;
        std::size_t prod_idx = first_user_pass_index;
        for (std::size_t s = 0; s < subs.size(); ++s) {
            for (auto c : subs[s].colors) {
                if (c == h) {
                    producer = &subs[s];
                    prod_idx = first_user_pass_index + s;
                    break;
                }
            }
            if (producer) {
                break;
            }
        }

        VkImageLayout fin = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        if (tex.type == TextureType::Swapchain) {
            fin = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        } else if (tex.type == TextureType::Depth) {
            fin = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        } else if (producer) {
            fin = final_layout_for_color(tex, h, prod_idx);
        } else {
            fin = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        attachment_final_layouts.push_back(fin);
        attachment_clear_colors.push_back(
            producer ? producer->clear_color : VkClearColorValue {});

        VkAttachmentDescription ad {};
        ad.format = tex.desc.format;
        ad.samples = VK_SAMPLE_COUNT_1_BIT;
        ad.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        ad.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ad.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        ad.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        ad.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ad.finalLayout = fin;
        attachments.push_back(ad);
    }

    std::vector<VkSubpassDescription> subpass_descs;
    std::vector<std::vector<VkAttachmentReference>> color_refs_storage;
    std::vector<std::vector<VkAttachmentReference>> input_refs_storage;
    std::vector<VkAttachmentReference> depth_refs;
    color_refs_storage.resize(subs.size());
    input_refs_storage.resize(subs.size());
    depth_refs.resize(subs.size());

    for (std::size_t si = 0; si < subs.size(); ++si) {
        const auto &sp = subs[si];
        auto &colors = color_refs_storage[si];
        auto &inputs = input_refs_storage[si];
        for (auto h : sp.colors) {
            colors.push_back({ ensure_att(h),
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
        }
        for (auto h : sp.inputs) {
            inputs.push_back({ ensure_att(h),
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
        }
        if (sp.depth != UINT32_MAX) {
            depth_refs[si] = { ensure_att(sp.depth),
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        }

        VkSubpassDescription sd {};
        sd.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sd.colorAttachmentCount =
            static_cast<uint32_t>(color_refs_storage[si].size());
        sd.pColorAttachments =
            color_refs_storage[si].empty() ? nullptr
                                           : color_refs_storage[si].data();
        sd.inputAttachmentCount =
            static_cast<uint32_t>(input_refs_storage[si].size());
        sd.pInputAttachments =
            input_refs_storage[si].empty() ? nullptr
                                           : input_refs_storage[si].data();
        sd.pDepthStencilAttachment =
            (sp.depth != UINT32_MAX) ? &depth_refs[si] : nullptr;
        subpass_descs.push_back(sd);
    }

    std::vector<VkSubpassDependency> deps;
    {
        VkSubpassDependency d0 {};
        d0.srcSubpass = VK_SUBPASS_EXTERNAL;
        d0.dstSubpass = 0;
        d0.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        d0.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        d0.srcAccessMask = 0;
        d0.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps.push_back(d0);
    }
    for (uint32_t i = 0; i + 1 < subpass_descs.size(); ++i) {
        VkSubpassDependency d {};
        d.srcSubpass = i;
        d.dstSubpass = i + 1;
        d.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        d.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        d.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        d.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps.push_back(d);
    }

    VkRenderPassCreateInfo rpc { .sType =
                                     VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                 .attachmentCount =
                                     static_cast<uint32_t>(attachments.size()),
                                 .pAttachments = attachments.data(),
                                 .subpassCount =
                                     static_cast<uint32_t>(subpass_descs.size()),
                                 .pSubpasses = subpass_descs.data(),
                                 .dependencyCount =
                                     static_cast<uint32_t>(deps.size()),
                                 .pDependencies = deps.data() };

    vkCreateRenderPass(device, &rpc, nullptr, &batch.render_pass);

    batch.clear_values.clear();
    batch.clear_value_handles = attachment_order;
    for (std::size_t ai = 0; ai < attachment_order.size(); ++ai) {
        const TextureHandle h = attachment_order[ai];
        VkClearValue cv {};
        if (textures[h].type == TextureType::Depth) {
            cv.depthStencil.depth = subs.back().clear_depth;
            cv.depthStencil.stencil = subs.back().clear_stencil;
        } else {
            cv.color = attachment_clear_colors[ai];
        }
        batch.clear_values.push_back(cv);
    }

    bool uses_swapchain = false;
    for (auto h : attachment_order) {
        if (textures[h].type == TextureType::Swapchain) {
            uses_swapchain = true;
            break;
        }
    }
    uint32_t fb_count = 1u;
    if (uses_swapchain) {
        for (auto h : attachment_order) {
            if (textures[h].type == TextureType::Swapchain) {
                fb_count = std::max(
                    fb_count,
                    static_cast<uint32_t>(
                        std::max(std::size_t{1}, swapchain_views_.size())));
            }
        }
    }

    batch.framebuffers.assign(fb_count, VK_NULL_HANDLE);
    for (uint32_t fi = 0; fi < fb_count; ++fi) {
        std::vector<VkImageView> views;
        views.reserve(attachment_order.size());
        for (auto h : attachment_order) {
            auto &tex = textures[h];
            if (tex.type == TextureType::Swapchain) {
                if (fi < swapchain_views_.size()) {
                    views.push_back(swapchain_views_[fi]);
                } else {
                    views.push_back(tex.view);
                }
            } else {
                views.push_back(tex.view);
            }
        }
        VkFramebufferCreateInfo fbci {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = batch.render_pass,
            .attachmentCount = static_cast<uint32_t>(views.size()),
            .pAttachments = views.data(),
            .width = batch.extent.width,
            .height = batch.extent.height,
            .layers = 1
        };
        vkCreateFramebuffer(device, &fbci, nullptr, &batch.framebuffers[fi]);
    }

    batch.color_outputs.clear();
    batch.color_final_layouts.clear();
    batch.has_depth = false;
    batch.depth_handles.clear();
    for (std::size_t ai = 0; ai < attachment_order.size(); ++ai) {
        const TextureHandle h = attachment_order[ai];
        if (textures[h].type == TextureType::Depth) {
            batch.has_depth = true;
            batch.depth_handles.push_back(h);
            batch.depth_final_layout = attachment_final_layouts[ai];
        } else {
            batch.color_outputs.push_back(h);
            batch.color_final_layouts.push_back(attachment_final_layouts[ai]);
        }
    }
    batch.attachment_handles = attachment_order;
}

void RenderGraph::merge_into_batches() {
    compiled_batches_.clear();
    name_to_render_pass_.clear();
    name_to_subpass_.clear();

    if (user_passes_.empty()) {
        return;
    }

    if (!subpass_merging_enabled_) {
        for (std::size_t i = 0; i < user_passes_.size(); ++i) {
            const auto &up = user_passes_[i];
            CompiledBatch batch;
            batch.first_user_pass_index = i;
            batch.extent = extent_for_user_pass(up);
            CompiledSubpass cs;
            cs.name = up.name;
            cs.exec = up.exec;
            cs.subpass_index = 0;
            batch.subpasses.push_back(cs);
            name_to_subpass_[up.name] = 0;
            build_batch_single_subpass(batch, up, i);
            compiled_batches_.push_back(std::move(batch));
            name_to_render_pass_[up.name] =
                compiled_batches_.back().render_pass;
        }
        return;
    }

    std::unordered_set<TextureHandle> written_global;
    std::vector<UserPass> subs_in_batch;
    std::size_t batch_first_idx = 0;
    VkExtent2D cur_extent {};

    auto flush = [&]() {
        if (subs_in_batch.empty()) {
            return;
        }
        const std::size_t first_idx = batch_first_idx;
        if (subs_in_batch.size() == 1) {
            CompiledBatch batch;
            batch.first_user_pass_index = first_idx;
            batch.extent = extent_for_user_pass(subs_in_batch[0]);
            CompiledSubpass cs;
            cs.name = subs_in_batch[0].name;
            cs.exec = subs_in_batch[0].exec;
            cs.subpass_index = 0;
            batch.subpasses.push_back(cs);
            name_to_subpass_[subs_in_batch[0].name] = 0;
            build_batch_single_subpass(batch, subs_in_batch[0], first_idx);
            name_to_render_pass_[subs_in_batch[0].name] = batch.render_pass;
            compiled_batches_.push_back(std::move(batch));
        } else {
            CompiledBatch batch;
            batch.first_user_pass_index = first_idx;
            batch.extent = extent_for_user_pass(subs_in_batch[0]);
            for (std::size_t si = 0; si < subs_in_batch.size(); ++si) {
                CompiledSubpass cs;
                cs.name = subs_in_batch[si].name;
                cs.exec = subs_in_batch[si].exec;
                cs.subpass_index = static_cast<uint32_t>(si);
                batch.subpasses.push_back(cs);
                name_to_subpass_[subs_in_batch[si].name] = cs.subpass_index;
            }
            build_batch_multi_subpass(batch, subs_in_batch, first_idx);
            for (const auto &s : subs_in_batch) {
                name_to_render_pass_[s.name] = batch.render_pass;
            }
            compiled_batches_.push_back(std::move(batch));
        }
        subs_in_batch.clear();
    };

    for (std::size_t pi = 0; pi < user_passes_.size(); ++pi) {
        const auto &up = user_passes_[pi];
        bool inputs_ok = true;
        for (auto h : up.inputs) {
            if (h != UINT32_MAX && !written_global.count(h)) {
                inputs_ok = false;
                break;
            }
        }
        VkExtent2D ext = extent_for_user_pass(up);
        bool extent_ok =
            subs_in_batch.empty() ||
            (ext.width == cur_extent.width && ext.height == cur_extent.height);

        if (!subs_in_batch.empty() && (!inputs_ok || !extent_ok)) {
            flush();
            inputs_ok = true;
            for (auto h : up.inputs) {
                if (h != UINT32_MAX && !written_global.count(h)) {
                    inputs_ok = false;
                    break;
                }
            }
            ext = extent_for_user_pass(up);
        }

        if (!inputs_ok) {
            flush();
        }

        if (subs_in_batch.empty()) {
            batch_first_idx = pi;
            cur_extent = ext;
        }

        subs_in_batch.push_back(up);
        for (auto h : up.colors) {
            written_global.insert(h);
        }
        if (up.depth != UINT32_MAX) {
            written_global.insert(up.depth);
        }
    }
    flush();
}

void RenderGraph::rebuild_barriers_for_batch(std::size_t batch_index) {
    auto &b = compiled_batches_[batch_index];
    b.barriers_before_batch.clear();

    auto emit = [&](TextureHandle h, VkImageLayout new_layout,
                    VkPipelineStageFlags2 dst_st, VkAccessFlags2 dst_acc) {
        if (h == UINT32_MAX || h >= textures.size()) {
            return;
        }
        auto &tex = textures[h];
        if (tex.image == VK_NULL_HANDLE) {
            return;
        }
        if (tex.currentLayout == new_layout && tex.lastAccess == dst_acc &&
            tex.lastStage == dst_st) {
            return;
        }
        VkImageMemoryBarrier2 m {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = tex.lastStage,
            .srcAccessMask = tex.lastAccess,
            .dstStageMask = dst_st,
            .dstAccessMask = dst_acc,
            .oldLayout = tex.currentLayout,
            .newLayout = new_layout,
            .image = tex.image,
            .subresourceRange = { aspect_mask_for_texture(tex), 0, 1, 0, 1 }
        };
        b.barriers_before_batch.push_back(m);
        tex.currentLayout = new_layout;
        tex.lastStage = dst_st;
        tex.lastAccess = dst_acc;
    };

    const UserPass &first = user_passes_[b.first_user_pass_index];
    for (auto h : first.inputs) {
        emit(h, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
             VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    for (auto h : first.colors) {
        emit(h, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
             VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
             VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }
    if (first.depth != UINT32_MAX) {
        emit(first.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
             VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                 VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
             VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    }
}

void RenderGraph::sync_batch_output_layouts(std::size_t batch_index) {
    auto &b = compiled_batches_[batch_index];
    for (std::size_t i = 0; i < b.color_outputs.size() &&
                            i < b.color_final_layouts.size();
         ++i) {
        const auto h = b.color_outputs[i];
        if (h < textures.size()) {
            sync_layout_after_color_output(textures[h], b.color_final_layouts[i]);
        }
    }
    if (b.has_depth && !b.depth_handles.empty()) {
        const auto dh = b.depth_handles.back();
        if (dh < textures.size()) {
            auto &dt = textures[dh];
            dt.currentLayout = b.depth_final_layout;
            dt.lastStage = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            dt.lastAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
    }
}

void RenderGraph::compile() {
    destroy_compiled();
    create_all_resources();
    merge_into_batches();

    std::fprintf(stdout, "[RenderGraph] compile: logical_passes=%u, batches=%u\n",
                 static_cast<unsigned>(user_passes_.size()),
                 static_cast<unsigned>(compiled_batches_.size()));
    for (std::size_t bi = 0; bi < compiled_batches_.size(); ++bi) {
        const auto &batch = compiled_batches_[bi];
        std::fprintf(stdout, "[RenderGraph]   batch[%u]: subpasses=%u\n",
                     static_cast<unsigned>(bi),
                     static_cast<unsigned>(batch.subpasses.size()));
        for (std::size_t si = 0; si < batch.subpasses.size(); ++si) {
            const auto &sp = batch.subpasses[si];
            std::fprintf(stdout, "[RenderGraph]     subpass[%u]: %s\n",
                         static_cast<unsigned>(si), sp.name.c_str());
        }
    }
}

void RenderGraph::execute(VkCommandBuffer cmd, uint32_t swapchain_image_index) {
    // 关键：在每帧执行前，把 Swapchain 纹理句柄对齐到当前 acquire 的 image/view。
    // 否则 barrier/layout 跟踪可能仍落在旧 image 上，触发验证层布局错配报错。
    if (!swapchain_images_.empty() && !swapchain_views_.empty()) {
        const std::size_t idx = std::min<std::size_t>(
            swapchain_image_index, swapchain_images_.size() - 1);
        const VkImage currentSwapImage = swapchain_images_[idx];
        const VkImageView currentSwapView = swapchain_views_[std::min<std::size_t>(
            idx, swapchain_views_.size() - 1)];

        for (auto &tex : textures) {
            if (tex.type != TextureType::Swapchain) {
                continue;
            }
            if (tex.image != currentSwapImage) {
                // 不同 swapchain image 间布局状态互不共享；切图时重置跟踪状态。
                tex.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                tex.lastStage = 0;
                tex.lastAccess = 0;
            }
            tex.image = currentSwapImage;
            tex.view = currentSwapView;
        }
    }

    for (std::size_t bi = 0; bi < compiled_batches_.size(); ++bi) {
        auto &b = compiled_batches_[bi];
        rebuild_barriers_for_batch(bi);
        if (!b.barriers_before_batch.empty()) {
            VkDependencyInfo di {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .imageMemoryBarrierCount =
                    static_cast<uint32_t>(b.barriers_before_batch.size()),
                .pImageMemoryBarriers = b.barriers_before_batch.data()
            };
            vkCmdPipelineBarrier2(cmd, &di);
        }

        VkFramebuffer fb = VK_NULL_HANDLE;
        if (!b.framebuffers.empty()) {
            if (swapchain_image_index < b.framebuffers.size()) {
                fb = b.framebuffers[swapchain_image_index];
            } else {
                fb = b.framebuffers[0];
            }
        }

        VkRenderPassBeginInfo rp_begin {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = b.render_pass,
            .framebuffer = fb,
            .renderArea = { .offset = { 0, 0 }, .extent = b.extent },
            .clearValueCount = static_cast<uint32_t>(b.clear_values.size()),
            .pClearValues = b.clear_values.data()
        };

        vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);
        for (std::size_t si = 0; si < b.subpasses.size(); ++si) {
            b.subpasses[si].exec(cmd);
            if (si + 1 < b.subpasses.size()) {
                vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
            }
        }
        vkCmdEndRenderPass(cmd);

        sync_batch_output_layouts(bi);
    }
}

} // namespace renderer
