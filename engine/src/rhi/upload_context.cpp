#include "rhi/upload_context.hpp"

#include "core/log/logger.hpp"

#include <cstring>
#include <vector>
#include <limits>

namespace rhi {

namespace {

[[nodiscard]] vk::DeviceSize align_up(vk::DeviceSize v, vk::DeviceSize a) noexcept {
    if (a == 0) {
        return v;
    }
    return (v + a - 1) / a * a;
}

} // namespace

UploadContext::~UploadContext() { shutdown(); }

bool UploadContext::init(vk::Device device, vk::Queue queue, VmaAllocator allocator,
                         std::uint32_t queue_family_index,
                         vk::DeviceSize staging_capacity) {
    shutdown();
    if (!device || !queue || allocator == nullptr) {
        LUMEN_LOG_ERROR("UploadContext::init 参数无效");
        return false;
    }
    vk_device_ = device;
    queue_ = queue;
    allocator_ = allocator;
    queue_family_ = queue_family_index;

    vk::CommandPoolCreateInfo pci {};
    pci.queueFamilyIndex = queue_family_index;
    pci.flags = vk::CommandPoolCreateFlagBits::eTransient;
    const vk::Result pr =
        vk_device_.createCommandPool(&pci, nullptr, &cmd_pool_);
    if (pr != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("UploadContext: createCommandPool 失败 {}",
                        static_cast<int>(pr));
        shutdown();
        return false;
    }

    vk::CommandBufferAllocateInfo ai {};
    ai.commandPool = cmd_pool_;
    ai.level = vk::CommandBufferLevel::ePrimary;
    ai.commandBufferCount = 1;
    const vk::Result ar =
        vk_device_.allocateCommandBuffers(&ai, &cmd_);
    if (ar != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("UploadContext: allocateCommandBuffers 失败 {}",
                        static_cast<int>(ar));
        shutdown();
        return false;
    }

    vk::FenceCreateInfo fi {};
    fi.flags = vk::FenceCreateFlagBits::eSignaled;
    const vk::Result fr =
        vk_device_.createFence(&fi, nullptr, &fence_);
    if (fr != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("UploadContext: createFence 失败 {}",
                        static_cast<int>(fr));
        shutdown();
        return false;
    }

    vk::DeviceSize cap = staging_capacity;
    if (cap == 0) {
        cap = static_cast<vk::DeviceSize>(16u * 1024u * 1024u);
    }
    staging_capacity_ = cap;
    if (!ensure_staging_(static_cast<std::size_t>(staging_capacity_))) {
        shutdown();
        return false;
    }

    return true;
}

void UploadContext::shutdown() {
    if (recording_ && cmd_) {
        static_cast<void>(cmd_.end());
        recording_ = false;
    }
    if (staging_alloc_ != nullptr && allocator_ != nullptr) {
        vmaDestroyBuffer(allocator_, static_cast<VkBuffer>(staging_buffer_),
                         staging_alloc_);
        staging_alloc_ = nullptr;
        staging_buffer_ = nullptr;
        staging_mapped_ = nullptr;
        staging_capacity_ = 0;
    }
    if (fence_ && vk_device_) {
        vk_device_.destroyFence(fence_, nullptr);
        fence_ = nullptr;
    }
    if (cmd_pool_ && vk_device_) {
        vk_device_.destroyCommandPool(cmd_pool_, nullptr);
        cmd_pool_ = nullptr;
    }
    cmd_ = nullptr;
    queue_ = nullptr;
    vk_device_ = nullptr;
    allocator_ = nullptr;
    ring_offset_ = 0;
    recording_ = false;
}

bool UploadContext::ensure_staging_(std::size_t need) {
    if (need == 0) {
        return true;
    }
    if (staging_buffer_ && staging_capacity_ >= need &&
        staging_mapped_ != nullptr) {
        return true;
    }
    grow_staging_(static_cast<vk::DeviceSize>(need));
    return staging_buffer_ && staging_capacity_ >= need &&
           staging_mapped_ != nullptr;
}

void UploadContext::grow_staging_(vk::DeviceSize min_required) {
    const vk::DeviceSize preserved_cap =
        (staging_alloc_ != nullptr) ? staging_capacity_ : 0;
    if (staging_alloc_ != nullptr && allocator_ != nullptr) {
        vmaDestroyBuffer(allocator_, static_cast<VkBuffer>(staging_buffer_),
                         staging_alloc_);
        staging_alloc_ = nullptr;
        staging_buffer_ = nullptr;
        staging_mapped_ = nullptr;
        staging_capacity_ = 0;
    }

    vk::DeviceSize new_cap =
        preserved_cap > 0
            ? preserved_cap
            : static_cast<vk::DeviceSize>(16u * 1024u * 1024u);
    while (new_cap < min_required) {
        new_cap *= 2;
    }
    staging_capacity_ = new_cap;

    VkBufferCreateInfo bci { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = static_cast<VkDeviceSize>(staging_capacity_);
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo aci {};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer buf {};
    const VkResult vr =
        vmaCreateBuffer(allocator_, &bci, &aci, &buf, &staging_alloc_, nullptr);
    if (vr != VK_SUCCESS) {
        LUMEN_LOG_ERROR("UploadContext: vmaCreateBuffer(staging) 失败 {}",
                        static_cast<int>(vr));
        staging_capacity_ = 0;
        return;
    }
    staging_buffer_ = vk::Buffer { buf };
    VmaAllocationInfo alloc_info {};
    vmaGetAllocationInfo(allocator_, staging_alloc_, &alloc_info);
    staging_mapped_ = alloc_info.pMappedData;
    if (staging_mapped_ == nullptr) {
        LUMEN_LOG_ERROR("UploadContext: staging MAPPED_BIT 但 pMappedData 为空");
        vmaDestroyBuffer(allocator_, buf, staging_alloc_);
        staging_alloc_ = nullptr;
        staging_buffer_ = nullptr;
        staging_capacity_ = 0;
    }
}

void UploadContext::begin_if_needed_() {
    if (recording_ || !cmd_) {
        return;
    }
    vk::CommandBufferBeginInfo bi {};
    bi.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    const vk::Result r = cmd_.begin(&bi);
    if (r != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("UploadContext: begin 命令缓冲失败 {}",
                        static_cast<int>(r));
        return;
    }
    recording_ = true;
}

void UploadContext::upload_buffer(vk::Buffer dst, vk::DeviceSize dst_offset,
                                  const void *data, std::size_t size) {
    if (!data || size == 0 || !dst) {
        return;
    }

    ring_offset_ = align_up(ring_offset_, static_cast<vk::DeviceSize>(4));
    const vk::DeviceSize chunk =
        static_cast<vk::DeviceSize>(size);
    if (ring_offset_ + chunk > staging_capacity_) {
        flush();
        ring_offset_ = 0;
    }
    if (!ensure_staging_(static_cast<std::size_t>(ring_offset_ + chunk))) {
        LUMEN_LOG_ERROR("UploadContext: staging 不足");
        return;
    }

    begin_if_needed_();
    if (!recording_) {
        return;
    }

    std::memcpy(static_cast<std::byte *>(staging_mapped_) + ring_offset_, data,
                size);

    vk::BufferCopy copy {};
    copy.srcOffset = ring_offset_;
    copy.dstOffset = dst_offset;
    copy.size = chunk;
    cmd_.copyBuffer(staging_buffer_, dst, copy);

    ring_offset_ += chunk;
}

void UploadContext::upload_image(vk::Image image, vk::Format format,
                                 vk::Extent3D extent, const void *data,
                                 std::size_t row_pitch_bytes) {
    if (!image || !data || extent.width == 0 || extent.height == 0) {
        return;
    }

    std::uint32_t bpp = 4;
    switch (format) {
    case vk::Format::eR8G8B8A8Unorm:
    case vk::Format::eR8G8B8A8Srgb:
        bpp = 4;
        break;
    default:
        bpp = 4;
        break;
    }
    const std::size_t tight_pitch =
        static_cast<std::size_t>(extent.width) * bpp;
    const std::size_t src_pitch =
        row_pitch_bytes != 0 ? row_pitch_bytes : tight_pitch;
    const std::size_t image_bytes = tight_pitch * extent.height;

    ring_offset_ = align_up(ring_offset_, static_cast<vk::DeviceSize>(4));
    const vk::DeviceSize chunk =
        static_cast<vk::DeviceSize>(image_bytes);
    if (ring_offset_ + chunk > staging_capacity_) {
        flush();
        ring_offset_ = 0;
    }
    if (!ensure_staging_(static_cast<std::size_t>(ring_offset_ + chunk))) {
        LUMEN_LOG_ERROR("UploadContext: staging 不足以容纳图像");
        return;
    }

    begin_if_needed_();
    if (!recording_) {
        return;
    }

    auto *const dst_base =
        static_cast<std::byte *>(staging_mapped_) + ring_offset_;
    if (src_pitch == tight_pitch) {
        std::memcpy(dst_base, data, image_bytes);
    } else {
        const auto *src = static_cast<const std::byte *>(data);
        for (std::uint32_t y = 0; y < extent.height; ++y) {
            std::memcpy(dst_base + static_cast<std::size_t>(y) * tight_pitch,
                        src + static_cast<std::size_t>(y) * src_pitch,
                        tight_pitch);
        }
    }

    vk::ImageMemoryBarrier to_transfer {};
    to_transfer.oldLayout = vk::ImageLayout::eUndefined;
    to_transfer.newLayout = vk::ImageLayout::eTransferDstOptimal;
    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image = image;
    to_transfer.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    to_transfer.subresourceRange.baseMipLevel = 0;
    to_transfer.subresourceRange.levelCount = 1;
    to_transfer.subresourceRange.baseArrayLayer = 0;
    to_transfer.subresourceRange.layerCount = 1;
    to_transfer.srcAccessMask = {};
    to_transfer.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

    const std::vector<vk::ImageMemoryBarrier> pre_barriers { to_transfer };
    cmd_.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                         vk::PipelineStageFlagBits::eTransfer, {}, nullptr,
                         nullptr, pre_barriers);

    vk::BufferImageCopy region {};
    region.bufferOffset = ring_offset_;
    region.bufferRowLength = extent.width;
    region.bufferImageHeight = extent.height;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D { 0, 0, 0 };
    region.imageExtent = extent;

    cmd_.copyBufferToImage(staging_buffer_, image,
                           vk::ImageLayout::eTransferDstOptimal, region);

    vk::ImageMemoryBarrier to_sample {};
    to_sample.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    to_sample.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    to_sample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_sample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_sample.image = image;
    to_sample.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    to_sample.subresourceRange.baseMipLevel = 0;
    to_sample.subresourceRange.levelCount = 1;
    to_sample.subresourceRange.baseArrayLayer = 0;
    to_sample.subresourceRange.layerCount = 1;
    to_sample.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    to_sample.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    const std::vector<vk::ImageMemoryBarrier> post_barriers { to_sample };
    cmd_.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                         vk::PipelineStageFlagBits::eFragmentShader, {},
                         nullptr, nullptr, post_barriers);

    (void)format;

    ring_offset_ += chunk;
}

void UploadContext::flush() {
    if (!recording_ || !cmd_) {
        ring_offset_ = 0;
        return;
    }
    const vk::Result er = cmd_.end();
    if (er != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("UploadContext: end 命令缓冲失败 {}",
                        static_cast<int>(er));
        recording_ = false;
        ring_offset_ = 0;
        return;
    }
    recording_ = false;

    vk::SubmitInfo si {};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;

    if (fence_) {
        static_cast<void>(vk_device_.resetFences(1, &fence_));
    }
    const vk::Result sr = queue_.submit(1, &si, fence_);
    if (sr != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("UploadContext: queue.submit 失败 {}",
                        static_cast<int>(sr));
        ring_offset_ = 0;
        return;
    }
    if (fence_) {
        static_cast<void>(vk_device_.waitForFences(
            1, &fence_, vk::True, std::numeric_limits<std::uint64_t>::max()));
        static_cast<void>(vk_device_.resetFences(1, &fence_));
    }
    if (cmd_pool_ && vk_device_) {
        vk_device_.resetCommandPool(cmd_pool_, {});
    }
    ring_offset_ = 0;
}

} // namespace rhi
