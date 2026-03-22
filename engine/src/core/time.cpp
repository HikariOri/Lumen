/**
 * @file time.cpp
 * @brief 单调时钟实现（主线程使用；首访建立零点）
 */

#include "core/time.hpp"

#include <chrono>

namespace lumen {
namespace core {

namespace {

using Clock = std::chrono::steady_clock;

Clock::time_point g_epoch {};
bool g_epoch_set { false };

void ensure_epoch_locked() {
    if (!g_epoch_set) {
        g_epoch = Clock::now();
        g_epoch_set = true;
    }
}

} // namespace

void anchor_steady_epoch() {
    ensure_epoch_locked();
}

double steady_seconds() {
    ensure_epoch_locked();
    const auto now = Clock::now();
    return std::chrono::duration<double>(now - g_epoch).count();
}

FrameDeltaClock::FrameDeltaClock() : last_total_(steady_seconds()) {}

double FrameDeltaClock::tick_seconds() {
    const double t = steady_seconds();
    const double dt = t - last_total_;
    last_total_ = t;
    return dt;
}

} // namespace core
} // namespace lumen
