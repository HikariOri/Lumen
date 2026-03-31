/**
 * @file id.hpp
 * @brief 引擎内 64 位 ID 别名与随机生成（`std::uint64_t`）
 */

#pragma once

#include <cstdint>
#include <random>

namespace lumen {
namespace core {

using ID = std::uint64_t;

/// 预留无效值；`generate_random_id()` 不会返回该值
inline constexpr ID INVALID_ID = 0;

/**
 * @brief 线程局部 `std::mt19937_64` 生成随机 ID（非 0）
 */
[[nodiscard]] inline ID generate_random_id() {
    thread_local std::mt19937_64 rng{ std::random_device{}() };
    ID out;
    do {
        out = static_cast<ID>(rng());
    } while (out == INVALID_ID);
    return out;
}

} // namespace core
} // namespace lumen
