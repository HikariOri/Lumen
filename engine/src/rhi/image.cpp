#include "rhi/image.hpp"

namespace rhi {

vk::Format to_vk(Format f) noexcept {
    switch (f) {
    case Format::R8G8B8A8_UNORM: return vk::Format::eR8G8B8A8Unorm;
    case Format::R8G8B8A8_SRGB: return vk::Format::eR8G8B8A8Srgb;
    }
    return vk::Format::eR8G8B8A8Unorm;
}

std::uint32_t format_texel_block_bytes(Format f) noexcept {
    switch (f) {
    case Format::R8G8B8A8_UNORM:
    case Format::R8G8B8A8_SRGB: return 4;
    }
    return 4;
}

vk::ImageUsageFlags to_vk(ImageUsage u) noexcept {
    vk::ImageUsageFlags out {};
    using U = std::underlying_type_t<ImageUsage>;
    const auto m = static_cast<U>(u);
    if (m & static_cast<U>(ImageUsage::Sampled)) {
        out |= vk::ImageUsageFlagBits::eSampled;
    }
    if (m & static_cast<U>(ImageUsage::ColorAttachment)) {
        out |= vk::ImageUsageFlagBits::eColorAttachment;
    }
    if (m & static_cast<U>(ImageUsage::DepthStencil)) {
        out |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    }
    if (m & static_cast<U>(ImageUsage::Storage)) {
        out |= vk::ImageUsageFlagBits::eStorage;
    }
    if (m & static_cast<U>(ImageUsage::TransferSrc)) {
        out |= vk::ImageUsageFlagBits::eTransferSrc;
    }
    if (m & static_cast<U>(ImageUsage::TransferDst)) {
        out |= vk::ImageUsageFlagBits::eTransferDst;
    }
    return out;
}

} // namespace rhi
