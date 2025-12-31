#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

// 假设你有某种 GPU 资源句柄类型
struct BufferHandle {
    uint32_t id =
        0; // 具体类型看你的图形 API，例如 GLuint 或 Vulkan 的 buffer handle
};

// 一个顶点结构体例子
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 tangent; // 如果需要切线
    // 你可以加入颜色，骨骼权重／骨骼索引等
};

struct Mesh {
    // CPU 端的数据
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices; // 三角形索引

    // GPU 资源句柄
    BufferHandle vertexBuffer;
    BufferHandle indexBuffer;

    // 用于渲染的几何体元数据
    glm::vec3 boundingBoxMin = glm::vec3(0.0f);
    glm::vec3 boundingBoxMax = glm::vec3(0.0f);

    // 是否已经上传到 GPU
    bool isUploaded = false;

    Mesh() = default;

    // 构造时给数据
    Mesh(std::vector<Vertex> v, std::vector<uint32_t> idx)
        : vertices(std::move(v)), indices(std::move(idx)) {}

    // 上传到 GPU 的方法
    void uploadToGPU() {
        if (isUploaded)
            return;
        // 在这里创建 GPU Buffer，用 vertices 和 indices 填充 vertexBuffer /
        // indexBuffer 更新包围盒
        computeBoundingBox();
        // ...
        isUploaded = true;
    }

    // 计算包围盒
    void computeBoundingBox() {
        if (vertices.empty()) {
            boundingBoxMin = boundingBoxMax = glm::vec3(0.0f);
            return;
        }
        boundingBoxMin = vertices[0].position;
        boundingBoxMax = vertices[0].position;
        for (auto &v : vertices) {
            boundingBoxMin = glm::min(boundingBoxMin, v.position);
            boundingBoxMax = glm::max(boundingBoxMax, v.position);
        }
    }

    // 释放 GPU 资源
    void destroy() {
        if (!isUploaded)
            return;
        // 删除 GPU 缓冲 etc
        // vertexBuffer = {}; indexBuffer = {};
        isUploaded = false;
    }

    bool hasIndices() const { return !indices.empty(); }
};
