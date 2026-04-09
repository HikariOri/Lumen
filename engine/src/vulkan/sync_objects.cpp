/**
 * @file sync_objects.cpp
 */

#include "vulkan/sync_objects.hpp"

#include "core/log/logger.hpp"

namespace vulkan {

std::expected<Semaphore, std::string> Semaphore::create(
    const VkDevice device) {
    if (device == VK_NULL_HANDLE) {
        return std::unexpected(
            std::string("Semaphore::create: null device"));
    }
    VkSemaphoreCreateInfo info { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkSemaphore sem { VK_NULL_HANDLE };
    if (vkCreateSemaphore(device, &info, nullptr, &sem) != VK_SUCCESS) {
        LUMEN_LOG_ERROR("Semaphore::create: vkCreateSemaphore failed");
        return std::unexpected(
            std::string("Semaphore::create: vkCreateSemaphore failed"));
    }
    return Semaphore(device, sem);
}

Semaphore::Semaphore(const VkDevice device, const VkSemaphore sem) noexcept
    : device_(device), semaphore_(sem) {}

Semaphore::~Semaphore() { destroy(); }

Semaphore::Semaphore(Semaphore &&other) noexcept
    : device_(other.device_), semaphore_(other.semaphore_) {
    other.device_ = VK_NULL_HANDLE;
    other.semaphore_ = VK_NULL_HANDLE;
}

Semaphore &Semaphore::operator=(Semaphore &&other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        semaphore_ = other.semaphore_;
        other.device_ = VK_NULL_HANDLE;
        other.semaphore_ = VK_NULL_HANDLE;
    }
    return *this;
}

void Semaphore::destroy() noexcept {
    if (device_ != VK_NULL_HANDLE && semaphore_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, semaphore_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    semaphore_ = VK_NULL_HANDLE;
}

std::expected<GpuFence, std::string> GpuFence::create(
    const VkDevice device, const VkFenceCreateFlags flags) {
    if (device == VK_NULL_HANDLE) {
        return std::unexpected(std::string("GpuFence::create: null device"));
    }
    VkFenceCreateInfo info {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = flags,
    };
    VkFence fence { VK_NULL_HANDLE };
    if (vkCreateFence(device, &info, nullptr, &fence) != VK_SUCCESS) {
        LUMEN_LOG_ERROR("GpuFence::create: vkCreateFence failed");
        return std::unexpected(
            std::string("GpuFence::create: vkCreateFence failed"));
    }
    return GpuFence(device, fence);
}

GpuFence::GpuFence(const VkDevice device, const VkFence fence) noexcept
    : device_(device), fence_(fence) {}

GpuFence::~GpuFence() { destroy(); }

GpuFence::GpuFence(GpuFence &&other) noexcept
    : device_(other.device_), fence_(other.fence_) {
    other.device_ = VK_NULL_HANDLE;
    other.fence_ = VK_NULL_HANDLE;
}

GpuFence &GpuFence::operator=(GpuFence &&other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        fence_ = other.fence_;
        other.device_ = VK_NULL_HANDLE;
        other.fence_ = VK_NULL_HANDLE;
    }
    return *this;
}

void GpuFence::destroy() noexcept {
    if (device_ != VK_NULL_HANDLE && fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(device_, fence_, nullptr);
    }
    device_ = VK_NULL_HANDLE;
    fence_ = VK_NULL_HANDLE;
}

std::expected<PresentFrameSync, std::string>
PresentFrameSync::create(const VkDevice device) {
    auto ia = Semaphore::create(device);
    if (!ia) {
        return std::unexpected(ia.error());
    }
    auto rf = Semaphore::create(device);
    if (!rf) {
        return std::unexpected(rf.error());
    }
    auto fence =
        GpuFence::create(device, VK_FENCE_CREATE_SIGNALED_BIT);
    if (!fence) {
        return std::unexpected(fence.error());
    }
    return PresentFrameSync { .image_available = std::move(*ia),
                              .render_finished = std::move(*rf),
                              .inflight = std::move(*fence) };
}

std::expected<std::vector<FrameInFlightSlot>, std::string>
create_frame_in_flight_slots(const VkDevice device,
                             const std::uint32_t frame_count) {
    if (frame_count == 0U) {
        return std::unexpected(
            std::string("create_frame_in_flight_slots: frame_count must be > 0"));
    }
    std::vector<FrameInFlightSlot> out;
    out.reserve(frame_count);
    for (std::uint32_t i { 0 }; i < frame_count; ++i) {
        auto sem = Semaphore::create(device);
        if (!sem) {
            return std::unexpected(sem.error());
        }
        auto fence =
            GpuFence::create(device, VK_FENCE_CREATE_SIGNALED_BIT);
        if (!fence) {
            return std::unexpected(fence.error());
        }
        out.push_back(FrameInFlightSlot { .image_available = std::move(*sem),
                                          .inflight_fence = std::move(*fence) });
    }
    return out;
}

PerImageSemaphores::~PerImageSemaphores() { destroy_all(); }

PerImageSemaphores::PerImageSemaphores(PerImageSemaphores &&other) noexcept
    : device_(other.device_), semaphores_(std::move(other.semaphores_)) {
    other.device_ = VK_NULL_HANDLE;
}

PerImageSemaphores &PerImageSemaphores::operator=(
    PerImageSemaphores &&other) noexcept {
    if (this != &other) {
        destroy_all();
        device_ = other.device_;
        semaphores_ = std::move(other.semaphores_);
        other.device_ = VK_NULL_HANDLE;
    }
    return *this;
}

void PerImageSemaphores::destroy_all() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        for (VkSemaphore s : semaphores_) {
            if (s != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, s, nullptr);
            }
        }
    }
    semaphores_.clear();
    device_ = VK_NULL_HANDLE;
}

std::expected<void, std::string>
PerImageSemaphores::sync_count(const VkDevice device, const std::size_t count) {
    if (device == VK_NULL_HANDLE) {
        return std::unexpected(
            std::string("PerImageSemaphores::sync_count: null device"));
    }
    if (semaphores_.size() == count && device_ == device) {
        return {};
    }
    vkDeviceWaitIdle(device);
    destroy_all();
    device_ = device;
    semaphores_.resize(count);
    VkSemaphoreCreateInfo info { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    for (std::size_t i { 0 }; i < count; ++i) {
        if (vkCreateSemaphore(device, &info, nullptr, &semaphores_[i]) !=
            VK_SUCCESS) {
            for (std::size_t k { 0 }; k < i; ++k) {
                vkDestroySemaphore(device, semaphores_[k], nullptr);
                semaphores_[k] = VK_NULL_HANDLE;
            }
            semaphores_.clear();
            device_ = VK_NULL_HANDLE;
            LUMEN_LOG_ERROR(
                "PerImageSemaphores::sync_count: vkCreateSemaphore failed");
            return std::unexpected(
                std::string("PerImageSemaphores::sync_count: "
                            "vkCreateSemaphore failed"));
        }
    }
    return {};
}

VkSemaphore PerImageSemaphores::get(const std::size_t image_index) const {
    if (image_index >= semaphores_.size()) {
        return VK_NULL_HANDLE;
    }
    return semaphores_[image_index];
}

} // namespace vulkan
