#include "renderer/mesh.hpp"

namespace renderer {

void Mesh::upload(VmaAllocator allocator,
                  const vulkan::UploadContext &uploadCtx,
                  const std::vector<Vertex> &vertices,
                  const std::vector<uint16_t> &indices) {

    indexCount = static_cast<uint32_t>(indices.size());

    // 顶点缓冲区
    vertexBuffer.init(allocator, vertices.size() * sizeof(Vertex),
                      vulkan::BufferUsage::Vertex,
                      vulkan::MemoryMode::GPU_ONLY);

    uploadToGPU(uploadCtx, vertexBuffer, vertices.data(),
                vertices.size() * sizeof(Vertex));

    // 索引缓冲区
    indexBuffer.init(allocator, indices.size() * sizeof(uint16_t),
                     vulkan::BufferUsage::Index, vulkan::MemoryMode::GPU_ONLY);
    uploadToGPU(uploadCtx, indexBuffer, indices.data(),
                indices.size() * sizeof(uint16_t));
}

void Mesh::bind(VkCommandBuffer cmd) const {
    VkBuffer vertexBufferHandle = vertexBuffer.buffer();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBufferHandle, &offset);
    vkCmdBindIndexBuffer(cmd, indexBuffer.buffer(), 0, VK_INDEX_TYPE_UINT16);
}

void Mesh::draw(VkCommandBuffer cmd) const {
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
}

void Mesh::destroy() {
    vertexBuffer.destroy();
    indexBuffer.destroy();
}

// 快速创建带UV的立方体Mesh
Mesh createCubeMesh(VmaAllocator allocator,
                    const vulkan::UploadContext &uploadCtx) {
    std::vector<Vertex> vertices = {
        // 前
        { .pos={ -1, -1, 1 }, .uv={ 0, 1 } },
        { { 1, -1, 1 }, { 1, 1 } },
        { .pos={ 1, 1, 1 }, .uv={ 1, 0 } },
        { { -1, 1, 1 }, { 0, 0 } },
        // 后
        { { -1, -1, -1 }, { 1, 1 } },
        { { 1, -1, -1 }, { 0, 1 } },
        { { 1, 1, -1 }, { 0, 0 } },
        { { -1, 1, -1 }, { 1, 0 } },
        // 左
        { { -1, -1, -1 }, { 0, 1 } },
        { { -1, -1, 1 }, { 1, 1 } },
        { { -1, 1, 1 }, { 1, 0 } },
        { { -1, 1, -1 }, { 0, 0 } },
        // 右
        { { 1, -1, 1 }, { 0, 1 } },
        { { 1, -1, -1 }, { 1, 1 } },
        { { 1, 1, -1 }, { 1, 0 } },
        { { 1, 1, 1 }, { 0, 0 } },
        // 上
        { { -1, 1, 1 }, { 0, 1 } },
        { { 1, 1, 1 }, { 1, 1 } },
        { { 1, 1, -1 }, { 1, 0 } },
        { { -1, 1, -1 }, { 0, 0 } },
        // 下
        { { -1, -1, -1 }, { 0, 1 } },
        { { 1, -1, -1 }, { 1, 1 } },
        { { 1, -1, 1 }, { 1, 0 } },
        { { -1, -1, 1 }, { 0, 0 } },
    };

    std::vector<uint16_t> indices = { 0,  1,  2,  2,  3,  0,  4,  5,  6,
                                      6,  7,  4,  8,  9,  10, 10, 11, 8,
                                      12, 13, 14, 14, 15, 12, 16, 17, 18,
                                      18, 19, 16, 20, 21, 22, 22, 23, 20 };

    Mesh mesh;
    mesh.upload(allocator, uploadCtx, vertices, indices);
    return mesh;
}

} // namespace renderer
