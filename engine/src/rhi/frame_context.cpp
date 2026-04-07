#include "rhi/frame_context.hpp"

#include "core/log/logger.hpp"

#include <limits>

namespace rhi {

bool FrameContext::init(vk::Device device, const std::uint32_t queue_family,
                        VmaAllocator allocator, const vk::DeviceSize ring_bytes) {
    shutdown(device, allocator);
    if (!device || allocator == nullptr) {
        return false;
    }

    vk::CommandPoolCreateInfo pci {};
    pci.queueFamilyIndex = queue_family;
    pci.flags = vk::CommandPoolCreateFlagBits::eTransient;
    const vk::Result pr =
        device.createCommandPool(&pci, nullptr, &cmd_pool_);
    if (pr != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("FrameContext: createCommandPool 失败 {}",
                        static_cast<int>(pr));
        return false;
    }

    vk::CommandBufferAllocateInfo ai {};
    ai.commandPool = cmd_pool_;
    ai.level = vk::CommandBufferLevel::ePrimary;
    ai.commandBufferCount = 1;
    const vk::Result ar = device.allocateCommandBuffers(&ai, &cmd_);
    if (ar != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("FrameContext: allocateCommandBuffers 失败 {}",
                        static_cast<int>(ar));
        device.destroyCommandPool(cmd_pool_, nullptr);
        cmd_pool_ = nullptr;
        return false;
    }

    vk::FenceCreateInfo fi {};
    fi.flags = vk::FenceCreateFlagBits::eSignaled;
    const vk::Result fr = device.createFence(&fi, nullptr, &fence_);
    if (fr != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("FrameContext: createFence 失败 {}",
                        static_cast<int>(fr));
        device.destroyCommandPool(cmd_pool_, nullptr);
        cmd_pool_ = nullptr;
        cmd_ = nullptr;
        return false;
    }

    vk::DeviceSize ring_cap = ring_bytes;
    if (ring_cap == 0) {
        ring_cap = 64u * 1024u * 1024u;
    }
    if (!upload_ring_.init(allocator, ring_cap)) {
        device.destroyFence(fence_, nullptr);
        fence_ = nullptr;
        device.destroyCommandPool(cmd_pool_, nullptr);
        cmd_pool_ = nullptr;
        cmd_ = nullptr;
        return false;
    }

    return true;
}

void FrameContext::shutdown(vk::Device device, VmaAllocator allocator) {
    upload_ring_.shutdown(allocator);
    if (fence_ && device) {
        device.destroyFence(fence_, nullptr);
        fence_ = nullptr;
    }
    if (cmd_pool_ && device) {
        device.destroyCommandPool(cmd_pool_, nullptr);
        cmd_pool_ = nullptr;
    }
    cmd_ = nullptr;
    recording_ = false;
    serial_ = 0;
}

void FrameContext::begin_recording(vk::Device device) {
    if (!device || !cmd_ || !fence_) {
        return;
    }
    if (recording_) {
        LUMEN_LOG_WARN("FrameContext::begin_recording: 已在录制中");
        return;
    }

    static_cast<void>(device.waitForFences(
        1, &fence_, vk::True, std::numeric_limits<std::uint64_t>::max()));
    static_cast<void>(device.resetFences(1, &fence_));
    device.resetCommandPool(cmd_pool_, {});
    upload_ring_.reset();

    vk::CommandBufferBeginInfo bi {};
    bi.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    const vk::Result br = cmd_.begin(&bi);
    if (br != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("FrameContext: commandBuffer.begin 失败 {}",
                        static_cast<int>(br));
        return;
    }
    recording_ = true;
}

void FrameContext::submit(vk::Device device, vk::Queue queue) {
    submit(device, queue, nullptr, {}, nullptr);
}

void FrameContext::submit(vk::Device device, vk::Queue queue,
                          const vk::Semaphore wait_image_available,
                          const vk::PipelineStageFlags wait_dst_stage_mask,
                          const vk::Semaphore signal_render_finished) {
    if (!recording_ || !cmd_ || !queue) {
        return;
    }
    const vk::Result er = cmd_.end();
    if (er != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("FrameContext: commandBuffer.end 失败 {}",
                        static_cast<int>(er));
        recording_ = false;
        return;
    }
    recording_ = false;

    const std::uint32_t wait_n = wait_image_available ? 1u : 0u;
    const std::uint32_t signal_n = signal_render_finished ? 1u : 0u;
    vk::PipelineStageFlags wait_stage = wait_dst_stage_mask;

    vk::SubmitInfo si {};
    si.waitSemaphoreCount = wait_n;
    si.pWaitSemaphores = wait_n > 0 ? &wait_image_available : nullptr;
    si.pWaitDstStageMask = wait_n > 0 ? &wait_stage : nullptr;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    si.signalSemaphoreCount = signal_n;
    si.pSignalSemaphores = signal_n > 0 ? &signal_render_finished : nullptr;

    const vk::Result sr = queue.submit(1, &si, fence_);
    if (sr != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("FrameContext: queue.submit 失败 {}",
                        static_cast<int>(sr));
        return;
    }
    ++serial_;
}

} // namespace rhi
