#pragma once

#include "vulkan/buffer.hpp"
#include "vulkan/upload_context.hpp"

namespace renderer {

struct Vertex {
    glm::vec3 pos;
    glm::vec2 uv;
};

class Mesh {
public:
    vulkan::Buffer vertexBuffer;
    vulkan::Buffer indexBuffer;
    uint32_t indexCount = 0;

    void upload(VmaAllocator allocator, const vulkan::UploadContext &uploadCtx,
                const std::vector<Vertex> &vertices,
                const std::vector<uint16_t> &indices);

    void bind(VkCommandBuffer cmd) const;
    void draw(VkCommandBuffer cmd) const;
    void destroy();
};

// 快速创建带UV的立方体Mesh
Mesh createCubeMesh(VmaAllocator allocator,
                    const vulkan::UploadContext &uploadCtx);

} // namespace renderer
