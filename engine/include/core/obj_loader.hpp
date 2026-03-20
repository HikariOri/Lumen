/**
 * @file obj_loader.hpp
 * @brief OBJ 模型加载：基于 tinyobjloader，输出 Vulkan 可用的顶点数据
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

namespace lumen {
namespace core {

/// OBJ 顶点：position + normal + uv，适用于 Vulkan 渲染
struct ObjVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

/// OBJ 网格：顶点 + 索引
struct ObjMesh {
    std::vector<ObjVertex> vertices;
    std::vector<uint32_t> indices;
};

/**
 * @brief 从文件加载 OBJ 模型
 * @param filePath OBJ 文件路径（.mtl 在同目录下自动加载）
 * @param outMesh 输出网格
 * @return 成功返回 true，失败返回 false（错误信息见 LUMEN_LOG_ERROR）
 *
 * 支持无法线 / 无 UV 的 OBJ：缺法线时用面法线，缺 UV 时用 (0,0)。
 */
bool load_obj(std::string_view filePath, ObjMesh& outMesh);

} // namespace core
} // namespace lumen
