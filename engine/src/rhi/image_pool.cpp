#include "rhi/image_pool.hpp"

namespace rhi {

ImageHandle ImagePool::insert(ImageResource resource) {
    for (std::uint32_t i = 0; i < slots_.size(); ++i) {
        if (!slots_[i].live) {
            slots_[i].res = std::move(resource);
            slots_[i].live = true;
            return ImageHandle { i + 1 };
        }
    }
    slots_.push_back(Slot { std::move(resource), true });
    return ImageHandle {
        static_cast<std::uint32_t>(slots_.size())
    };
}

void ImagePool::destroy(ImageHandle h, vk::Device device,
                        VmaAllocator allocator) {
    if (!is_valid(h)) {
        return;
    }
    const std::uint32_t i = h.id - 1;
    if (i >= slots_.size() || !slots_[i].live) {
        return;
    }
    Slot &slot = slots_[i];
    if (slot.res.view && device) {
        device.destroyImageView(slot.res.view, nullptr);
    }
    if (slot.res.image && allocator != nullptr) {
        vmaDestroyImage(allocator, static_cast<VkImage>(slot.res.image),
                        slot.res.allocation);
    }
    slot = {};
}

const ImageResource *ImagePool::get(ImageHandle h) const {
    if (!is_valid(h)) {
        return nullptr;
    }
    const std::uint32_t i = h.id - 1;
    if (i >= slots_.size() || !slots_[i].live) {
        return nullptr;
    }
    return &slots_[i].res;
}

ImageResource *ImagePool::get(ImageHandle h) {
    return const_cast<ImageResource *>(
        static_cast<const ImagePool *>(this)->get(h));
}

void ImagePool::clear(vk::Device device, VmaAllocator allocator) {
    for (auto &slot : slots_) {
        if (!slot.live) {
            continue;
        }
        if (slot.res.view && device) {
            device.destroyImageView(slot.res.view, nullptr);
        }
        if (slot.res.image && allocator != nullptr) {
            vmaDestroyImage(allocator, static_cast<VkImage>(slot.res.image),
                            slot.res.allocation);
        }
        slot = {};
    }
    slots_.clear();
}

} // namespace rhi
