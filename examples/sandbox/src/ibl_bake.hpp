/**
 * @file ibl_bake.hpp
 * @brief 从 HDR 等距图生成环境立方体 + IBL 贴图（Irradiance / Prefilter / BRDF
 * LUT）
 */

#pragma once

#include "render/resource/texture.hpp"

#include <string>
#include <vulkan/vulkan.h>

namespace lumen::render {
class Context;
class CommandPool;
} // namespace lumen::render

namespace pbr {

struct IblTextures {
    lumen::render::Texture environment;
    lumen::render::Texture irradiance;
    lumen::render::Texture prefilter;
    lumen::render::Texture brdf_lut;
};

/**
 * @brief 加载 HDR 并烘焙 IBL 资源（耗时操作，建议启动时一次）
 * @return 失败时 err 含原因；成功则 out 内四份纹理均为有效 RGBA32F（立方体或 2D
 * LUT）
 */
bool bake_ibl(lumen::render::Context &ctx, lumen::render::CommandPool &cmd_pool,
              VkQueue queue, const char *hdr_path, IblTextures &out,
              std::string &err);

} // namespace pbr
