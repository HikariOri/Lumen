/**
 * @file obj_loader.cpp
 * @brief OBJ 加载实现（基于 tinyobjloader）
 *
 * @details
 * 实现 OBJ 文件解析与转换逻辑，将 Wavefront OBJ 数据转换为
 * Vulkan 可直接使用的顶点/索引缓冲格式。
 *
 * 主要步骤：
 * - 使用 tinyobjloader 解析 OBJ
 * - 遍历 shapes / faces
 * - 提取 position / normal / uv
 * - 处理缺失属性
 * - 构建顶点与索引缓冲
 *
 * @note
 * 当前实现未做顶点去重，每个三角形独立展开顶点（non-indexed reuse）
 */

#include "core/obj_loader.hpp"
#include "core/logger.hpp"

#include <array>
#include <cstddef>
#include <string>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

namespace lumen {
namespace core {

namespace {

/**
 * @brief 获取 .mtl 搜索目录
 *
 * @param[in] filePath OBJ 文件路径
 * @return 材质文件搜索目录
 *
 * @details
 * 从 OBJ 路径中提取目录部分，用于 tinyobjloader 查找 .mtl 文件。
 * 若路径不包含目录，则返回当前目录 "."。
 */
std::string get_mtl_base_dir(std::string_view filePath) {
    std::string path { filePath };
    auto pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return ".";
    }
    return path.substr(0, pos);
}

/**
 * @brief 从 attrib 中读取顶点位置
 *
 * @param[in] attrib tinyobj 属性数据
 * @param[in] idx    顶点索引
 * @return 顶点位置（vec3）
 *
 * @note
 * - OBJ position 是 float 数组，按 xyz 连续存储
 * - 若索引非法，返回 (0,0,0)
 */
glm::vec3 get_vertex(const tinyobj::attrib_t &attrib, int idx) {
    const size_t base = static_cast<size_t>(idx) * 3;
    if (idx < 0 || (base + 2) >= attrib.vertices.size()) {
        return {};
    }

    return { static_cast<float>(attrib.vertices[base + 0]),
             static_cast<float>(attrib.vertices[base + 1]),
             static_cast<float>(attrib.vertices[base + 2]) };
}

/**
 * @brief 从 attrib 中读取法线
 *
 * @param[in] attrib tinyobj 属性数据
 * @param[in] idx    法线索引
 * @return 法线向量
 *
 * @note
 * - 若 OBJ 不包含法线或索引非法，返回默认 (0,1,0)
 * - 实际使用中可能被面法线覆盖
 */
glm::vec3 get_normal(const tinyobj::attrib_t &attrib, int idx) {
    const size_t base = static_cast<size_t>(idx) * 3;
    if (idx < 0 || (base + 2) >= attrib.normals.size()) {
        return { 0.F, 1.F, 0.F };
    }
    return { static_cast<float>(attrib.normals[base + 0]),
             static_cast<float>(attrib.normals[base + 1]),
             static_cast<float>(attrib.normals[base + 2]) };
}

/**
 * @brief 从 attrib 中读取纹理坐标
 *
 * @param[in] attrib tinyobj 属性数据
 * @param[in] idx    UV 索引
 * @return UV 坐标
 *
 * @note
 * - OBJ UV 为 (u,v)
 * - 若缺失或索引非法，返回 (0,0)
 */
glm::vec2 get_texcoord(const tinyobj::attrib_t &attrib, int idx) {
    const size_t base = static_cast<size_t>(idx) * 2;
    if (idx < 0 || (base + 1) >= attrib.texcoords.size()) {
        return {};
    }
    return { static_cast<float>(attrib.texcoords[base + 0]),
             static_cast<float>(attrib.texcoords[base + 1]) };
}

/**
 * @brief 计算三角形面法线
 *
 * @param[in] v0 顶点0
 * @param[in] v1 顶点1
 * @param[in] v2 顶点2
 * @return 单位法线
 *
 * @details
 * 使用叉乘：
 *   n = normalize((v1 - v0) × (v2 - v0))
 *
 * @note
 * - 若三角形退化（面积≈0），返回默认 (0,1,0)
 * - 用于 OBJ 无法线时的 fallback（flat shading）
 */
glm::vec3 compute_face_normal(const glm::vec3 &v0, const glm::vec3 &v1,
                              const glm::vec3 &v2) {
    const glm::vec3 e1 = v1 - v0;
    const glm::vec3 e2 = v2 - v0;
    const glm::vec3 n = glm::cross(e1, e2);
    const float len = glm::length(n);
    if (len < 1e-8F) {
        return { 0.F, 1.F, 0.F };
    }
    return n / len;
}

} // namespace

/**
 * @brief 加载 OBJ 文件
 *
 * @param[in]  filePath OBJ 文件路径
 * @param[out] outMesh  输出网格数据
 *
 * @return
 * - true  成功
 * - false 失败
 *
 * @details
 * 加载流程：
 *
 * 1. 初始化 tinyobjloader 配置
 * 2. 解析 OBJ 文件
 * 3. 遍历 shapes / faces
 * 4. 提取顶点属性：
 *    - position
 *    - normal（若存在）
 *    - uv（若存在）
 * 5. 处理缺失数据：
 *    - 无法线 → 使用面法线
 *    - 无 UV   → (0,0)
 * 6. 构建顶点与索引数组
 *
 * @note
 * - 当前实现未进行顶点去重（每个三角形展开为3个顶点）
 * - 输出适用于直接上传 Vulkan Vertex Buffer / Index Buffer
 *
 * @warning
 * - 未去重会导致顶点数量增加
 * - OBJ 坐标系与渲染系统不一致时需额外转换
 */
bool load_obj(std::string_view filePath, ObjMesh &outMesh) {

    // 清空输出
    outMesh.vertices.clear();
    outMesh.indices.clear();

    std::string path { filePath };
    std::string mtlDir = get_mtl_base_dir(path);

    /// tinyobj 配置
    tinyobj::ObjReaderConfig config;
    config.triangulate = true;       ///< 强制三角化
    config.mtl_search_path = mtlDir; ///< 材质路径

    tinyobj::ObjReader reader;

    /// 解析 OBJ 文件
    if (!reader.ParseFromFile(path, config)) {
        if (!reader.Error().empty()) {
            LUMEN_LOG_ERROR("OBJ 加载失败 {}: {}", path, reader.Error());
        }
        if (!reader.Warning().empty()) {
            LUMEN_LOG_WARN("OBJ 警告 {}: {}", path, reader.Warning());
        }
        return false;
    }

    if (!reader.Warning().empty()) {
        LUMEN_LOG_DEBUG("OBJ 警告 {}: {}", path, reader.Warning());
    }

    const auto &attrib = reader.GetAttrib();
    const auto &shapes = reader.GetShapes();

    const bool hasNormals = !attrib.normals.empty();
    const bool hasTexcoords = !attrib.texcoords.empty();

    /// 遍历所有 shape
    for (const auto &shape : shapes) {

        const auto &mesh = shape.mesh;
        const size_t numFaces = mesh.num_face_vertices.size();

        /// 遍历所有面
        for (size_t f = 0; f < numFaces; ++f) {

            /// 确保为三角形
            if (mesh.num_face_vertices[f] != 3) {
                continue;
            }

            std::array<glm::vec3, 3> v {};
            std::array<glm::vec3, 3> n {};
            std::array<glm::vec2, 3> uv {};

            /// 读取三角形三个顶点
            for (int i = 0; i < 3; ++i) {
                const size_t idxOffset = f * 3 + static_cast<size_t>(i);
                const auto &idx = mesh.indices[idxOffset];

                v[i] = get_vertex(attrib, idx.vertex_index);

                n[i] = hasNormals ? get_normal(attrib, idx.normal_index)
                                  : glm::vec3(0.F);

                uv[i] = hasTexcoords ? get_texcoord(attrib, idx.texcoord_index)
                                     : glm::vec2(0.F);
            }

            /// 若无 normal → 使用面法线
            if (!hasNormals) {
                const glm::vec3 faceN = compute_face_normal(v[0], v[1], v[2]);
                n[0] = n[1] = n[2] = faceN;
            }

            /// 写入顶点与索引（无去重）
            const auto base = static_cast<uint32_t>(outMesh.vertices.size());

            for (int i = 0; i < 3; ++i) {
                outMesh.vertices.push_back(
                    { .position = v[i], .normal = n[i], .uv = uv[i] });
                outMesh.indices.push_back(base + static_cast<uint32_t>(i));
            }
        }
    }

    LUMEN_LOG_DEBUG("OBJ 加载成功 {}: {} 顶点, {} 三角形", path,
                    outMesh.vertices.size(), outMesh.indices.size() / 3);

    return true;
}

} // namespace core
} // namespace lumen
