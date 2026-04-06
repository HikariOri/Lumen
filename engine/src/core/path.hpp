/**
 * @file path.hpp
 * @brief 路径工具：可执行文件所在目录、资源路径拼接
 *
 * 使用 SDL_GetBasePath，需在 SDL_Init 之后调用。
 */

#pragma once

#include <string>
#include <string_view>

namespace lumen {
namespace core {

/**
 * @brief 获取可执行文件所在目录（以路径分隔符结尾）
 * 基于 SDL_GetBasePath，返回如 "C:/app/bin/"
 * @return 目录路径；失败时返回空字符串
 */
std::string get_base_path();

/**
 * @brief 拼接资源路径：base_path + subpath
 * @param subpath 相对子路径，如 "shaders/triangle.vert.spv"
 * @return 完整路径
 */
std::string get_resource_path(std::string_view subpath);

} // namespace core
} // namespace lumen
