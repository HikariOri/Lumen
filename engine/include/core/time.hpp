/**
 * @file time.hpp
 * @brief 高精度时间工具
 */

#pragma once

namespace lumen {
namespace core {

/**
 * @brief 获取自程序启动以来的秒数（单调递增）
 * 使用 std::chrono::steady_clock，不依赖 SDL
 */
double get_time_seconds();

} // namespace core
} // namespace lumen
