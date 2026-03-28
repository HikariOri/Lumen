/**
 * @file obj_loader.hpp
 * @brief OBJ 模型加载接口（基于 tinyobjloader）
 *
 * @details
 * 提供从 Wavefront OBJ 文件加载网格数据的功能，并转换为适用于 Vulkan
 * 渲染管线的顶点/索引缓冲格式。
 *
 * 转换过程包括：
 * - 解析 OBJ 顶点属性（position / normal / texcoord）
 * - 三角化面（若输入非三角形）
 * - 构建统一索引（适配 Vulkan）
 * - 自动补全缺失数据（法线 / UV）
 *
 * @note
 * - OBJ 使用“分离索引”（position / normal / uv 各自独立）
 * - Vulkan 使用“统一索引”，因此需要顶点去重/展开
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

namespace lumen {
namespace core {

/**
 * @brief OBJ 顶点结构（Vulkan 顶点输入格式）
 *
 * @details
 * 对应 Vulkan 顶点属性布局：
 * - location 0: position (vec3)
 * - location 1: normal   (vec3)
 * - location 2: uv       (vec2)
 *
 * @note
 * - position 为模型空间坐标
 * - normal 需为单位向量（加载时保证）
 * - uv 范围通常为 [0,1]
 */
struct ObjVertex {

    /** @brief 顶点位置（模型空间） */
    glm::vec3 position;

    /** @brief 法线向量（用于光照计算） */
    glm::vec3 normal;

    /**
     * @brief 纹理坐标
     * @note OBJ 的 V 坐标可能需要翻转（取决于纹理约定）
     */
    glm::vec2 uv;
};

/**
 * @brief OBJ 网格数据（单一 Mesh）
 *
 * @details
 * 表示一个完整的渲染网格，包含：
 * - 顶点数组（去重后）
 * - 索引数组（统一索引）
 *
 * @note
 * 当前实现：
 * - 所有 shape/material 合并为单 mesh
 * - 不区分 submesh
 */
struct ObjMesh {

    /** @brief 顶点缓冲数据 */
    std::vector<ObjVertex> vertices;

    /**
     * @brief 索引缓冲数据
     * @note 使用 uint32_t 以支持大模型
     */
    std::vector<uint32_t> indices;
};

/**
 * @brief 从 OBJ 文件加载网格数据
 *
 * @details
 * 使用 tinyobjloader 解析 OBJ 文件，并转换为 Vulkan 兼容的数据格式。
 *
 * 处理流程：
 * 1. 解析 OBJ（position / normal / uv）
 * 2. 三角化面
 * 3. 构建统一顶点索引
 * 4. 补全缺失属性：
 *    - 无法线 → 使用面法线（flat shading）
 *    - 无 UV   → 使用 (0,0)
 *
 * @param[in]  filePath OBJ 文件路径
 *                      - .mtl 文件需位于同目录（自动加载）
 * @param[out] outMesh  输出网格数据（顶点 + 索引）
 *
 * @return
 * - true  加载成功
 * - false 加载失败（错误信息通过日志输出）
 *
 * @note
 * - OBJ 默认右手坐标系（Y-up）
 * - 若与渲染系统不一致，需要额外转换
 *
 * @warning
 * - 若未进行顶点去重，会导致顶点数量暴增
 * - UV 未翻转可能导致纹理上下颠倒
 * - 法线未归一化会导致光照错误
 */
bool load_obj(std::string_view filePath, ObjMesh &outMesh);

} // namespace core
} // namespace lumen
