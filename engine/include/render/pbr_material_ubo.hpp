/**
 * @file pbr_material_ubo.hpp
 * @brief PBR 材质 Uniform Buffer（Set=1, Binding=0，std140 布局）
 *
 * @details
 * 该结构体用于向 GPU 传递 PBR 材质参数，对应 GLSL：
 *
 * @code
 * layout(set = 1, binding = 0, std140) uniform MaterialUBO {
 *     vec4 base_color_factor;
 *     vec4 mr_ao_factors;
 *     vec4 emissive_factor;
 *     vec4 shader_params;
 * };
 * @endcode
 *
 * ============================================================
 * std140 布局说明（关键）
 * ============================================================
 *
 * - 所有 vec3 / vec4 对齐为 16 字节
 * - struct 对齐到 16 字节倍数
 * - 每个 vec4 占 16 字节
 *
 * 因此本结构：
 *
 *   4 × vec4 = 64 bytes
 *
 * @note
 * std140 规则要求：
 * - vec3 也按 vec4 对齐（16 bytes） :contentReference[oaicite:0]{index=0}
 * - struct size 必须是最大对齐的倍数（通常是 16）
 * :contentReference[oaicite:1]{index=1}
 *
 * ============================================================
 * 设计原则
 * ============================================================
 *
 * - 全部使用 vec4 → 避免 padding 问题
 * - CPU 与 GLSL 完全一致
 * - 易于扩展（每个 vec4 是一个逻辑块）
 *
 * ============================================================
 * 使用场景
 * ============================================================
 *
 * - Vulkan Descriptor Set = 1
 * - Binding = 0
 * - 每个 draw call 绑定一个材质
 *
 * ============================================================
 * 注意事项
 * ============================================================
 *
 * @warning
 * - 必须与 shader 中 std140 完全匹配
 * - 不可随意改成员顺序
 * - 不要使用 vec3（容易错位）
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <glm/vec4.hpp>

namespace lumen {
namespace render {

/**
 * @brief PBR 材质 UBO 数据结构（std140 对齐）
 *
 * @details
 * 每个字段对应 shader 中一个 vec4：
 *
 * ------------------------------------------------------------
 * [0] base_color_factor
 * ------------------------------------------------------------
 * xyz = BaseColor（反照率）
 * w   = Alpha（透明度）
 *
 * ------------------------------------------------------------
 * [1] mr_ao_factors
 * ------------------------------------------------------------
 * x = metallic
 * y = roughness
 * z = ambient occlusion
 * w = 保留
 *
 * @note
 * 与贴图组合方式：
 *   final = texture * factor
 *
 * ------------------------------------------------------------
 * [2] emissive_factor
 * ------------------------------------------------------------
 * xyz = emissive（自发光）
 * w   = 保留
 *
 * ------------------------------------------------------------
 * [3] shader_params
 * ------------------------------------------------------------
 * x = alpha mode
 *     0 → Opaque
 *     1 → Mask
 *     2 → Blend
 *
 * y = alpha cutoff（Mask 模式）
 *
 * z = texture mask（位标记）
 *     使用 uint → float 位拷贝
 *
 * w = 保留
 */
struct alignas(16) PbrMaterialUbo {

    /**
     * @brief Base Color 因子
     *
     * @note
     * - 对应 glTF baseColorFactor
     * - 与 baseColorTexture 相乘
     */
    glm::vec4 base_color_factor { 1.0f, 1.0f, 1.0f, 1.0f };

    /**
     * @brief Metallic / Roughness / AO 因子
     *
     * @note
     * - metallic: 控制金属度
     * - roughness: 控制粗糙度
     * - ao: 环境光遮蔽
     */
    glm::vec4 mr_ao_factors { 1.0f, 1.0f, 1.0f, 0.0f };

    /**
     * @brief Emissive（自发光）
     *
     * @note
     * - 用于发光材质（灯、屏幕）
     */
    glm::vec4 emissive_factor { 0.0f, 0.0f, 0.0f, 0.0f };

    /**
     * @brief Shader 参数（打包）
     *
     * @details
     * 用于减少 UBO 大小（避免新增字段）
     *
     * --------------------------------------------------------
     * x = alpha mode
     * y = alpha cutoff
     * z = texture mask（bitmask）
     * w = reserved
     */
    glm::vec4 shader_params { 0.0f, 0.5f, 0.0f, 0.0f };
};

/**
 * @brief 编译期检查：std140 size 必须为 64 bytes
 */
static_assert(sizeof(PbrMaterialUbo) == 64,
              "PbrMaterialUbo must match std140 layout (64 bytes)");

/**
 * @brief uint → float 位级转换（无数值变化）
 *
 * @param u 输入 uint32
 * @return 按位解释为 float
 *
 * @details
 * 用于将 bitmask 写入 shader_params.z：
 *
 * @code
 * ubo.shader_params.z = uint_bits_to_float(mask);
 * @endcode
 *
 * shader 中：
 * @code
 * uint mask = floatBitsToUint(shader_params.z);
 * @endcode
 *
 * @note
 * - 不进行数值转换，仅 reinterpret
 * - 等价于 GLSL floatBitsToUint
 */
inline float uint_bits_to_float(std::uint32_t u) {
    float f {};
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

} // namespace render
} // namespace lumen
