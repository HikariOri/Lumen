#include "rhi/device.hpp"

#include "core/log/logger.hpp"

#include <cstring>
#include <vector>

namespace rhi {

namespace {

[[nodiscard]] vk::BufferUsageFlags to_vk(BufferUsage u) noexcept {
    using U = std::underlying_type_t<BufferUsage>;
    const auto m = static_cast<U>(u);
    vk::BufferUsageFlags f {};
    if (m & static_cast<U>(BufferUsage::Vertex)) {
        f |= vk::BufferUsageFlagBits::eVertexBuffer;
    }
    if (m & static_cast<U>(BufferUsage::Index)) {
        f |= vk::BufferUsageFlagBits::eIndexBuffer;
    }
    if (m & static_cast<U>(BufferUsage::Uniform)) {
        f |= vk::BufferUsageFlagBits::eUniformBuffer;
    }
    if (m & static_cast<U>(BufferUsage::Storage)) {
        f |= vk::BufferUsageFlagBits::eStorageBuffer;
    }
    if (m & static_cast<U>(BufferUsage::TransferSrc)) {
        f |= vk::BufferUsageFlagBits::eTransferSrc;
    }
    if (m & static_cast<U>(BufferUsage::TransferDst)) {
        f |= vk::BufferUsageFlagBits::eTransferDst;
    }
    return f;
}

} // namespace

Device::~Device() { shutdown(); }

void Device::fill_vma_alloc_(MemoryUsage mem,
                             VmaAllocationCreateInfo &aci) const {
    aci = {};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    switch (mem) {
    case MemoryUsage::GPU_ONLY: break;
    case MemoryUsage::CPU_TO_GPU:
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case MemoryUsage::GPU_TO_CPU:
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    }
}

void Device::apply_buffer_tracker_(BufferHandle h, BufferUsage usage) {
    if (!tracker_ || !is_valid(h)) {
        return;
    }
    using U = std::underlying_type_t<BufferUsage>;
    const auto m = static_cast<U>(usage);
    vk::PipelineStageFlags stages {};
    vk::AccessFlags access {};
    if (m & static_cast<U>(BufferUsage::Vertex)) {
        stages |= vk::PipelineStageFlagBits::eVertexInput;
        access |= vk::AccessFlagBits::eVertexAttributeRead;
    }
    if (m & static_cast<U>(BufferUsage::Index)) {
        stages |= vk::PipelineStageFlagBits::eVertexInput;
        access |= vk::AccessFlagBits::eIndexRead;
    }
    if (m & static_cast<U>(BufferUsage::Uniform)) {
        stages |= vk::PipelineStageFlagBits::eVertexShader |
                  vk::PipelineStageFlagBits::eFragmentShader;
        access |= vk::AccessFlagBits::eUniformRead;
    }
    if (m & static_cast<U>(BufferUsage::Storage)) {
        stages |= vk::PipelineStageFlagBits::eFragmentShader |
                  vk::PipelineStageFlagBits::eComputeShader;
        access |=
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
    }
    if (stages != vk::PipelineStageFlags {}) {
        tracker_->set_buffer_state(
            h, ResourceState { stages, access, vk::ImageLayout::eUndefined });
    }
}

void Device::apply_image_tracker_sampled_(ImageHandle h) {
    if (!tracker_ || !is_valid(h)) {
        return;
    }
    tracker_->set_image_state(
        h, ResourceState { vk::PipelineStageFlagBits::eFragmentShader,
                           vk::AccessFlagBits::eShaderRead,
                           vk::ImageLayout::eShaderReadOnlyOptimal });
}

void Device::record_image_upload_frame_(FrameContext &frame, vk::Image image,
                                        vk::Format format, vk::Extent3D extent,
                                        const void *data,
                                        const std::size_t row_pitch) {
    if (!image || data == nullptr || extent.width == 0 || extent.height == 0) {
        return;
    }
    (void)format;
    constexpr std::uint32_t bpp = 4;
    const std::size_t tight_pitch =
        static_cast<std::size_t>(extent.width) * bpp;
    const std::size_t src_pitch = row_pitch != 0 ? row_pitch : tight_pitch;
    const std::size_t image_bytes = tight_pitch * extent.height;

    UploadRingBuffer::Allocation a =
        frame.upload_ring().allocate(image_bytes, 4);
    if (!a) {
        LUMEN_LOG_ERROR("Device: 帧上传环空间不足以容纳图像");
        return;
    }

    auto *const dst_base = static_cast<std::byte *>(a.cpu);
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

    vk::CommandBuffer cmd = frame.command_buffer();

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

    const std::vector<vk::ImageMemoryBarrier> pre { to_transfer };
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eTransfer, {}, nullptr,
                        nullptr, pre);

    vk::BufferImageCopy region {};
    region.bufferOffset = a.offset;
    region.bufferRowLength = extent.width;
    region.bufferImageHeight = extent.height;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D { 0, 0, 0 };
    region.imageExtent = extent;

    cmd.copyBufferToImage(a.buffer, image, vk::ImageLayout::eTransferDstOptimal,
                          region);

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

    const std::vector<vk::ImageMemoryBarrier> post { to_sample };
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr,
                        nullptr, post);
}

bool Device::init(Context &ctx) {
    if (initialized_) {
        return true;
    }
    if (!ctx.device() || ctx.allocator() == nullptr) {
        LUMEN_LOG_ERROR("Device::init: Context 未就绪");
        return false;
    }
    device_ = ctx.device();
    physical_device_ = ctx.physical_device();
    queue_ = ctx.graphics_queue();
    queue_family_ = ctx.graphics_queue_family();
    present_queue_ = ctx.present_queue();
    present_queue_family_ = ctx.present_queue_family();
    allocator_ = ctx.allocator();

    constexpr vk::DeviceSize k_ring_bytes =
        static_cast<vk::DeviceSize>(64ull * 1024ull * 1024ull);
    for (std::uint32_t i = 0; i < k_frames_in_flight; ++i) {
        if (!frames_[i].init(device_, queue_family_, allocator_,
                             k_ring_bytes)) {
            for (std::uint32_t j = 0; j < i; ++j) {
                frames_[j].shutdown(device_, allocator_);
            }
            device_ = nullptr;
            physical_device_ = nullptr;
            queue_ = nullptr;
            present_queue_ = nullptr;
            allocator_ = nullptr;
            return false;
        }
    }

    sync_upload_ = std::make_unique<UploadContext>();
    if (!sync_upload_->init(device_, queue_, allocator_, queue_family_)) {
        for (auto &f : frames_) {
            f.shutdown(device_, allocator_);
        }
        sync_upload_.reset();
        device_ = nullptr;
        physical_device_ = nullptr;
        queue_ = nullptr;
        present_queue_ = nullptr;
        allocator_ = nullptr;
        return false;
    }

    tracker_ = std::make_unique<ResourceTracker>();
    frame_slot_ = 0;
    in_frame_ = false;
    initialized_ = true;
    LUMEN_LOG_DEBUG("rhi::Device 初始化完成 (frames_in_flight={})",
                    k_frames_in_flight);
    return true;
}

void Device::shutdown() {
    if (!initialized_) {
        buffer_pool_.clear(nullptr);
        image_pool_.clear(vk::Device {}, nullptr);
        sync_upload_.reset();
        tracker_.reset();
        device_ = nullptr;
        physical_device_ = nullptr;
        queue_ = nullptr;
        present_queue_ = nullptr;
        allocator_ = nullptr;
        in_frame_ = false;
        return;
    }

    static_cast<void>(device_.waitIdle());

    buffer_pool_.clear(allocator_);
    image_pool_.clear(device_, allocator_);

    for (auto &f : frames_) {
        f.shutdown(device_, allocator_);
    }

    sync_upload_.reset();
    tracker_.reset();

    device_ = nullptr;
    physical_device_ = nullptr;
    queue_ = nullptr;
    present_queue_ = nullptr;
    allocator_ = nullptr;
    frame_slot_ = 0;
    in_frame_ = false;
    initialized_ = false;
}

void Device::begin_frame() {
    if (!initialized_) {
        return;
    }
    if (in_frame_) {
        LUMEN_LOG_WARN("Device::begin_frame: 已处于帧内，忽略重复调用");
        return;
    }
    frames_[frame_slot_].begin_recording(device_);
    in_frame_ = true;
}

void Device::end_frame(const vk::Semaphore wait_image_available,
                       const vk::PipelineStageFlags wait_dst_stage_mask,
                       const vk::Semaphore signal_render_finished) {
    if (!initialized_) {
        return;
    }
    if (!in_frame_) {
        LUMEN_LOG_WARN("Device::end_frame: 未调用 begin_frame，忽略");
        return;
    }
    frames_[frame_slot_].submit(device_, queue_, wait_image_available,
                                wait_dst_stage_mask, signal_render_finished);
    frame_slot_ = (frame_slot_ + 1) % k_frames_in_flight;
    in_frame_ = false;
}

vk::CommandBuffer Device::frame_command_buffer() const {
    if (!initialized_ || !in_frame_) {
        return nullptr;
    }
    return frames_[frame_slot_].command_buffer();
}

const BufferResource *Device::try_get(BufferHandle h) const {
    return buffer_pool_.get(h);
}

const ImageResource *Device::try_get(ImageHandle h) const {
    return image_pool_.get(h);
}

BufferHandle Device::create_buffer(const BufferDesc &desc) {
    if (!initialized_ || desc.size == 0) {
        return {};
    }

    BufferUsage usage = desc.usage;
    if (desc.data != nullptr &&
        (std::to_underlying(usage) &
         std::to_underlying(BufferUsage::TransferDst)) == 0) {
        usage |= BufferUsage::TransferDst;
    }

    vk::BufferCreateInfo bci {};
    bci.size = desc.size;
    bci.usage = to_vk(usage);
    bci.sharingMode = vk::SharingMode::eExclusive;

    VmaAllocationCreateInfo aci {};
    fill_vma_alloc_(desc.memory, aci);

    VmaAllocation alloc {};
    VkBuffer vk_buf {};
    const VkResult vr = vmaCreateBuffer(
        allocator_, reinterpret_cast<const VkBufferCreateInfo *>(&bci), &aci,
        &vk_buf, &alloc, nullptr);
    if (vr != VK_SUCCESS) {
        LUMEN_LOG_ERROR("create_buffer: vmaCreateBuffer 失败 {}",
                        static_cast<int>(vr));
        return {};
    }

    BufferResource res {};
    res.buffer = vk::Buffer { vk_buf };
    res.allocation = alloc;
    res.size = desc.size;

    const BufferHandle handle = buffer_pool_.insert(std::move(res));

    if (desc.data != nullptr) {
        upload_buffer(handle, desc.data, desc.size, 0);
        if (!in_frame_) {
            flush_uploads();
        }
        apply_buffer_tracker_(handle, usage);
    }

    return handle;
}

void Device::destroy_buffer(BufferHandle h) {
    if (!initialized_ || !is_valid(h)) {
        return;
    }
    buffer_pool_.destroy(h, allocator_);
    if (tracker_) {
        tracker_->erase_buffer(h);
    }
}

void Device::upload_buffer(BufferHandle h, const void *data, std::size_t size,
                           vk::DeviceSize dst_offset) {
    if (!initialized_ || data == nullptr || size == 0) {
        return;
    }
    const BufferResource *br = try_get(h);
    if (br == nullptr) {
        return;
    }

    if (in_frame_) {
        FrameContext &frame = frames_[frame_slot_];
        UploadRingBuffer::Allocation a = frame.upload_ring().allocate(size, 16);
        if (!a) {
            LUMEN_LOG_ERROR("Device::upload_buffer: 帧环分配失败");
            return;
        }
        std::memcpy(a.cpu, data, size);
        vk::BufferCopy copy {};
        copy.srcOffset = a.offset;
        copy.dstOffset = dst_offset;
        copy.size = static_cast<vk::DeviceSize>(size);
        frame.command_buffer().copyBuffer(a.buffer, br->buffer, copy);
        return;
    }

    if (sync_upload_) {
        sync_upload_->upload_buffer(br->buffer, dst_offset, data, size);
    }
}

void Device::flush_uploads() {
    if (sync_upload_) {
        sync_upload_->flush();
    }
}

ImageHandle Device::create_image(const ImageDesc &desc) {
    if (!initialized_ || desc.width == 0 || desc.height == 0) {
        return {};
    }

    ImageUsage usage = desc.usage;
    if (desc.data != nullptr) {
        const auto um = static_cast<std::underlying_type_t<ImageUsage>>(usage);
        if ((um & static_cast<std::underlying_type_t<ImageUsage>>(
                      ImageUsage::TransferDst)) == 0) {
            usage |= ImageUsage::TransferDst;
        }
    }

    const vk::Format vkfmt = to_vk(desc.format);
    vk::ImageUsageFlags vkusage = to_vk(usage);

    vk::ImageCreateInfo ici {};
    ici.imageType = vk::ImageType::e2D;
    ici.format = vkfmt;
    ici.extent = vk::Extent3D { desc.width, desc.height, 1 };
    ici.mipLevels = desc.mip_levels;
    ici.arrayLayers = 1;
    ici.samples = vk::SampleCountFlagBits::e1;
    ici.tiling = vk::ImageTiling::eOptimal;
    ici.usage = vkusage;
    ici.sharingMode = vk::SharingMode::eExclusive;
    ici.initialLayout = vk::ImageLayout::eUndefined;

    VmaAllocationCreateInfo aci {};
    fill_vma_alloc_(desc.memory, aci);

    VmaAllocation alloc {};
    VkImage vk_img {};
    const VkResult vr = vmaCreateImage(
        allocator_, reinterpret_cast<const VkImageCreateInfo *>(&ici), &aci,
        &vk_img, &alloc, nullptr);
    if (vr != VK_SUCCESS) {
        LUMEN_LOG_ERROR("create_image: vmaCreateImage 失败 {}",
                        static_cast<int>(vr));
        return {};
    }

    ImageResource res {};
    res.image = vk::Image { vk_img };
    res.allocation = alloc;
    res.vk_format = vkfmt;
    res.extent = ici.extent;
    res.mip_levels = desc.mip_levels;

    vk::ImageViewCreateInfo vci {};
    vci.image = res.image;
    vci.viewType = vk::ImageViewType::e2D;
    vci.format = vkfmt;
    vci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    vci.subresourceRange.baseMipLevel = 0;
    vci.subresourceRange.levelCount = desc.mip_levels;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount = 1;

    const vk::Result vrr = device_.createImageView(&vci, nullptr, &res.view);
    if (vrr != vk::Result::eSuccess) {
        LUMEN_LOG_ERROR("create_image: createImageView 失败 {}",
                        static_cast<int>(vrr));
        vmaDestroyImage(allocator_, vk_img, alloc);
        return {};
    }

    const ImageHandle handle = image_pool_.insert(std::move(res));

    if (desc.data != nullptr) {
        const ImageResource *ir = image_pool_.get(handle);
        if (ir != nullptr) {
            if (in_frame_) {
                record_image_upload_frame_(frames_[frame_slot_], ir->image,
                                           vkfmt, ir->extent, desc.data,
                                           desc.data_row_pitch);
            } else if (sync_upload_) {
                sync_upload_->upload_image(ir->image, vkfmt, ir->extent,
                                           desc.data, desc.data_row_pitch);
                flush_uploads();
            }
            apply_image_tracker_sampled_(handle);
        }
    }

    return handle;
}

void Device::destroy_image(ImageHandle h) {
    if (!initialized_ || !is_valid(h)) {
        return;
    }
    image_pool_.destroy(h, device_, allocator_);
    if (tracker_) {
        tracker_->erase_image(h);
    }
}

DescriptorSetHandle Device::create_descriptor_set() { return {}; }

} // namespace rhi
