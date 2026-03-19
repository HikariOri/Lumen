/**
 * @file time.cpp
 * @brief 时间工具实现
 */

#include "core/time.hpp"

#include <chrono>

namespace lumen {
namespace core {

double get_time_seconds() {
    using Clock = std::chrono::steady_clock;
    static const Clock::time_point start = Clock::now();
    auto now = Clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::duration<double>>(
        now - start);
    return dur.count();
}

} // namespace core
} // namespace lumen
