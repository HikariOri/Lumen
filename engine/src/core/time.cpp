/**
 * @file time.cpp
 * @brief 单调时钟实现（基于 std::chrono::steady_clock）
 *
 * 设计目标：
 * - 提供“不会倒退”的时间（monotonic）
 * - 用于帧时间（delta time）计算
 * - 避免 system_clock 被系统时间调整影响
 *
 * std::chrono::steady_clock 的特点：
 * - 单调递增（不会回退）
 * - 不受系统时间/NTP/时区影响
 * - 适合测量时间间隔（而不是现实时间） :contentReference[oaicite:0]{index=0}
 */

#include "core/time.hpp"

#include <chrono>

namespace lumen {
namespace core {

namespace {

/** 使用单调时钟类型（不会被系统时间修改） */
using Clock = std::chrono::steady_clock;

/**
 * @brief 全局时间零点（epoch）
 *
 * 表示“时间起点”，所有 steady_seconds() 的返回值
 * 都是相对于该时间点的偏移。
 *
 * ⚠ 注意：
 * - 这个 epoch 不是 UNIX epoch
 * - 只是程序内部参考点
 */
Clock::time_point g_epoch {};

/**
 * @brief 是否已经初始化过时间零点
 *
 * 用于保证 epoch 只被设置一次（懒初始化）
 */
bool g_epoch_set { false };

/**
 * @brief 确保时间零点已初始化（lazy init）
 *
 * 行为：
 * - 第一次调用：记录当前时间为 epoch
 * - 后续调用：不做任何事情
 *
 * ⚠ 线程安全：
 * - 当前实现是“非线程安全”的（假设主线程使用）
 * - 多线程场景需要加锁或使用原子
 */
void ensure_epoch_locked() {
    if (!g_epoch_set) {
        g_epoch = Clock::now();
        g_epoch_set = true;
    }
}

} // namespace

/**
 * @brief 手动锚定时间零点
 *
 * 用途：
 * - 通常在主循环开始前调用
 * - 避免初始化阶段（加载资源等）影响计时
 *
 * 行为：
 * - 若未初始化 → 设置 epoch
 * - 若已初始化 → 无操作
 */
void anchor_steady_epoch() { ensure_epoch_locked(); }

/**
 * @brief 获取自 epoch 起的时间（单位：秒）
 *
 * @return 从时间零点到当前的时间间隔（秒）
 *
 * 实现细节：
 * - 使用 steady_clock，保证单调递增
 * - 返回 double，方便做动画/物理计算
 *
 * ⚠ 注意：
 * - 不是“真实时间”（不能用于显示时间）
 * - 仅用于时间差计算
 */
double steady_seconds() {
    ensure_epoch_locked();

    const auto now = Clock::now();

    // duration → double（秒）
    return std::chrono::duration<double>(now - g_epoch).count();
}

/**
 * @brief 构造函数
 *
 * 初始化 last_total_ 为当前时间，
 * 用于后续计算帧间隔（delta time）。
 */
FrameDeltaClock::FrameDeltaClock() : last_total_(steady_seconds()) {}

/**
 * @brief 获取帧间隔（delta time）
 *
 * @return 距离上一次 tick 的时间（秒）
 *
 * 行为：
 * - 计算当前时间与上一次记录时间的差值
 * - 更新 last_total_
 *
 * 典型用法：
 * @code
 * FrameDeltaClock clock;
 * while (running) {
 *     double dt = clock.tick_seconds();
 *     update(dt);
 * }
 * @endcode
 *
 * ⚠ 第一帧：
 * - dt = 构造到第一次 tick 的时间
 */
double FrameDeltaClock::tick_seconds() {
    const double t = steady_seconds(); ///< 当前时间
    const double dt = t - last_total_; ///< 帧间隔
    last_total_ = t;                   ///< 更新基准
    return dt;
}

} // namespace core
} // namespace lumen
