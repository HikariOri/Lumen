#pragma once

#include "upload_context.hpp"

namespace vulkan {

enum class BufferUsage : std::int8_t {
    Vertex,
    Index,
    Uniform,
    Storage,
    UniformDynamic,
    StorageDynamic,
    UploadStaging
};

enum class MemoryMode : std::int8_t {
    CPU_TO_GPU,    // 普通上传，短生命周期
    GPU_ONLY,      // 设备本地，用 staging 写入
    PERSISTENT_MAP // 持久映射，适合高频更新（UBO/动态UBO）
};

class Buffer {
public:
    // 创建缓冲
    void init(VmaAllocator allocator, VkDeviceSize size, BufferUsage usage,
              MemoryMode memoryMode);

    // 上传数据
    void upload(const void *data, VkDeviceSize size, VkDeviceSize offset = 0);

    // 模板版本：直接传结构体/数组
    template <typename T>
    void upload(const T &obj) {
        upload(&obj, sizeof(T));
    }

    template <typename T>
    void upload(const std::vector<T> &vec) {
        upload(vec.data(), vec.size() * sizeof(T));
    }

    // 绑定（Vertex / Index 专用）
    void bind_vertex(VkCommandBuffer cmd, uint32_t binding = 0,
                     VkDeviceSize offset = 0) const;

    void bind_index(VkCommandBuffer cmd,
                    VkIndexType indexType = VK_INDEX_TYPE_UINT16,
                    VkDeviceSize offset = 0) const;

    // 直接写（如果已映射）
    void copy_to_mapped(const void *data, VkDeviceSize size = 0,
                        VkDeviceSize offset = 0);

    // 销毁
    void destroy();

    [[nodiscard]] VkBuffer buffer() const { return buffer_; }
    [[nodiscard]] VmaAllocation allocation() const { return allaction_; }
    [[nodiscard]] VmaAllocator allocator() const { return allocator_; }
    [[nodiscard]] VkDeviceSize size() const { return size_; }
    [[nodiscard]] void *mapped() const { return mapped_; }

private:
    VkBuffer buffer_ { VK_NULL_HANDLE };
    VmaAllocation allaction_ { VK_NULL_HANDLE };
    VmaAllocator allocator_ { VK_NULL_HANDLE };
    VkDeviceSize size_ {};
    void *mapped_ = nullptr;
    MemoryMode memoryMode_ = MemoryMode::CPU_TO_GPU;
};

// 自动 staging 上传
void uploadToGPU(
    const UploadContext& ctx,
    Buffer& dst,
    const void* data,
    VkDeviceSize size
);


// 环形动态 UBO 管理器
struct DynamicRingBuffer {
    Buffer buffer;
    VkDeviceSize totalSize = 0;
    VkDeviceSize frameSize = 0;
    VkDeviceSize alignment = 0;
    uint32_t frameCount = 0;

    void init(VmaAllocator allocator, VkDeviceSize perFrameSize,
              uint32_t frameCount, uint32_t minAlignment);

    void destroy();

    // 获取当前帧可写指针 & 设备偏移
    void *getMappedFrame(uint32_t frameIndex, VkDeviceSize &outBufferOffset);
};

// 简单 Buffer 池（按大小&用途复用）
struct BufferPool {
    VmaAllocator allocator;
    std::unordered_map<size_t, std::vector<Buffer>> pool;

    Buffer getBuffer(size_t size, BufferUsage usage, MemoryMode mode);
    void returnBuffer(Buffer &&buf);
    void clear();
};

} // namespace vulkan
