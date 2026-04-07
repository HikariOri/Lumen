#include "rhi/resource_tracker.hpp"

namespace rhi {

std::uint64_t ResourceTracker::buffer_key(BufferHandle h) {
    return static_cast<std::uint64_t>(h.id) << 32 | 1u;
}

std::uint64_t ResourceTracker::image_key(ImageHandle h) {
    return static_cast<std::uint64_t>(h.id) << 32 | 2u;
}

void ResourceTracker::set_buffer_state(BufferHandle h, ResourceState s) {
    if (!is_valid(h)) {
        return;
    }
    buffer_states_[buffer_key(h)] = s;
}

void ResourceTracker::set_image_state(ImageHandle h, ResourceState s) {
    if (!is_valid(h)) {
        return;
    }
    image_states_[image_key(h)] = s;
}

std::optional<ResourceState>
ResourceTracker::buffer_state(BufferHandle h) const {
    if (!is_valid(h)) {
        return std::nullopt;
    }
    const auto it = buffer_states_.find(buffer_key(h));
    if (it == buffer_states_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<ResourceState>
ResourceTracker::image_state(ImageHandle h) const {
    if (!is_valid(h)) {
        return std::nullopt;
    }
    const auto it = image_states_.find(image_key(h));
    if (it == image_states_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void ResourceTracker::update_buffer(BufferHandle h, ResourceState s) {
    set_buffer_state(h, s);
}

void ResourceTracker::update_image(ImageHandle h, ResourceState s) {
    set_image_state(h, s);
}

void ResourceTracker::clear() {
    buffer_states_.clear();
    image_states_.clear();
}

void ResourceTracker::erase_buffer(BufferHandle h) {
    if (!is_valid(h)) {
        return;
    }
    buffer_states_.erase(buffer_key(h));
}

void ResourceTracker::erase_image(ImageHandle h) {
    if (!is_valid(h)) {
        return;
    }
    image_states_.erase(image_key(h));
}

} // namespace rhi
