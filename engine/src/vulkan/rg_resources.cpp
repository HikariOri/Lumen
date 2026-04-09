/**
 * @file rg_resources.cpp
 */

#include "vulkan/rg_resources.hpp"

#include "vulkan/context.hpp"

namespace vulkan {

namespace {

[[nodiscard]] VkImageAspectFlags depth_aspect_mask_fn(const VkFormat format) {
    switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    default:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    }
}

} // namespace

RgResources::RgResources(Context &ctx) noexcept : ctx_(ctx) {}

RgResourceHandle RgResources::create_texture(const std::uint32_t width,
                                             const std::uint32_t height,
                                             const VkFormat format) {
    Slot slot {};
    slot.type = RgResourceType::Texture;
    ImageCreateInfo info {};
    info.extent = { width, height, 1 };
    info.format = format;
    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.viewDevice = ctx_.device();
    info.viewAspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (auto img = Image::create(ctx_.allocator(), info); img.has_value()) {
        slot.owned_image = std::move(*img);
        slot.target = to_render_target(slot.owned_image);
    }
    if (!slot.target.is_valid()) {
        return {};
    }
    slots_.push_back(std::move(slot));
    states_.push_back(RgResourceState {});
    return RgResourceHandle { .id = static_cast<std::uint32_t>(slots_.size() - 1U) };
}

RgResourceHandle RgResources::create_depth(const std::uint32_t width,
                                           const std::uint32_t height) {
    const VkFormat depth_fmt = ctx_.depth_format();
    if (depth_fmt == VK_FORMAT_UNDEFINED) {
        return {};
    }
    Slot slot {};
    slot.type = RgResourceType::Depth;
    ImageCreateInfo info {};
    info.extent = { width, height, 1 };
    info.format = depth_fmt;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT;
    info.viewDevice = ctx_.device();
    info.viewAspectMask = depth_aspect_mask_fn(depth_fmt);
    if (auto img = Image::create(ctx_.allocator(), info); img.has_value()) {
        slot.owned_image = std::move(*img);
        slot.target = to_render_target(slot.owned_image);
    }
    if (!slot.target.is_valid()) {
        return {};
    }
    slots_.push_back(std::move(slot));
    states_.push_back(RgResourceState {});
    return RgResourceHandle { .id = static_cast<std::uint32_t>(slots_.size() - 1U) };
}

RgResourceHandle RgResources::import_swapchain(const RenderTarget &rt,
                                               const VkImage swapchain_image) {
    if (!rt.is_valid()) {
        return {};
    }
    Slot slot {};
    slot.type = RgResourceType::Swapchain;
    slot.target = rt;
    slot.external_image = swapchain_image;
    slots_.push_back(std::move(slot));
    states_.push_back(RgResourceState {});
    return RgResourceHandle { .id = static_cast<std::uint32_t>(slots_.size() - 1U) };
}

void RgResources::set_resource_target(const RgResourceHandle handle,
                                        const RenderTarget &rt,
                                        const VkImage swapchain_image) {
    if (!valid_handle(handle)) {
        return;
    }
    Slot &slot = slots_[handle.id];
    slot.target = rt;
    if (swapchain_image != VK_NULL_HANDLE) {
        slot.external_image = swapchain_image;
    }
}

bool RgResources::valid_handle(const RgResourceHandle h) const noexcept {
    return h.is_valid() && static_cast<std::size_t>(h.id) < slots_.size();
}

RgResourceType RgResources::resource_type(const RgResourceHandle h) const {
    if (!valid_handle(h)) {
        return RgResourceType::Texture;
    }
    return slots_[h.id].type;
}

RenderTarget RgResources::render_target(const RgResourceHandle h) const {
    if (!valid_handle(h)) {
        return {};
    }
    return slots_[h.id].target;
}

VkImage RgResources::barrier_vk_image(const RgResourceHandle h) const noexcept {
    if (!valid_handle(h)) {
        return VK_NULL_HANDLE;
    }
    const Slot &slot = slots_[h.id];
    if (slot.owned_image.is_valid()) {
        return slot.owned_image.image();
    }
    return slot.external_image;
}

const Image *RgResources::try_owned_image(const RgResourceHandle h) const noexcept {
    if (!valid_handle(h)) {
        return nullptr;
    }
    const Slot &slot = slots_[h.id];
    if (slot.owned_image.is_valid()) {
        return &slot.owned_image;
    }
    return nullptr;
}

VkImageLayout RgResources::layout(const RgResourceHandle h) const {
    if (!valid_handle(h)) {
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }
    return states_[h.id].layout;
}

void RgResources::set_layout(const RgResourceHandle h, const VkImageLayout layout) {
    if (!valid_handle(h)) {
        return;
    }
    states_[h.id].layout = layout;
}

void RgResources::reset_all_layouts(const VkImageLayout layout) {
    for (RgResourceState &s : states_) {
        s.layout = layout;
    }
}

RgResourceState &RgResources::mutable_state(const RgResourceHandle h) noexcept {
    static RgResourceState invalid {};
    if (!valid_handle(h)) {
        return invalid;
    }
    return states_[h.id];
}

const RgResourceState &RgResources::resource_state(
    const RgResourceHandle h) const noexcept {
    static const RgResourceState invalid {};
    if (!valid_handle(h)) {
        return invalid;
    }
    return states_[h.id];
}

RenderTarget &RgResources::render_target_ref(const RgResourceHandle h) noexcept {
    static RenderTarget invalid {};
    if (!valid_handle(h)) {
        return invalid;
    }
    return slots_[h.id].target;
}

const RenderTarget &RgResources::render_target_ref(
    const RgResourceHandle h) const noexcept {
    static const RenderTarget invalid {};
    if (!valid_handle(h)) {
        return invalid;
    }
    return slots_[h.id].target;
}

Image *RgResources::try_owned_image_mut(const RgResourceHandle h) noexcept {
    if (!valid_handle(h)) {
        return nullptr;
    }
    Image &im = slots_[h.id].owned_image;
    if (im.is_valid()) {
        return &im;
    }
    return nullptr;
}

} // namespace vulkan
