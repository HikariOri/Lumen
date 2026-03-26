/**
 * @file time.hpp
 * @brief 单调时间工具：统一的 steady_clock 时间基准 + 帧间隔计时器
 *
 * 封装 std::chrono::steady_clock，提供：
 * - 全局统一的“时间零点”（epoch）
 * - 稳定递增的运行时间（不受系统时间修改影响）
 * - 帧间隔（delta time）计算工具
 *
 * 设计目标：
 * - 避免使用 system_clock（会被用户/系统修改）
 * - 保证动画 / 物理 / 相机等逻辑稳定
 */

#pragma once

namespace lumen {
namespace core {

/**
 * @brief 锚定 steady 时间零点（epoch）
 *
 * 默认情况下：
 * - 第一次调用 steady_seconds() 时会自动建立零点
 *
 * 本函数的作用是：
 * 👉 手动控制“时间起点”
 *
 * 行为：
 * - 若零点尚未建立 → 将当前时刻设为零点
 * - 若零点已存在 → 不做任何操作（幂等）
 *
 * 使用场景：
 * - 在主循环开始前调用，避免把初始化时间计入运行时间
 *
 * 示例：
 * @code
 * init_engine();
 * anchor_steady_epoch();   // 从这里开始计时
 * main_loop();
 * @endcode
 */
void anchor_steady_epoch();

/**
 * @brief 获取从零点开始的累计时间（秒）
 *
 * 特性：
 * - 基于 std::chrono::steady_clock
 * - 单调递增（不会倒退）
 * - 不受系统时间修改影响
 *
 * 零点建立规则：
 * - 优先使用 anchor_steady_epoch()
 * - 若未调用，则首次调用本函数时自动建立
 *
 * @return 从零点开始的秒数（double 精度）
 *
 * 示例：
 * @code
 * double t = steady_seconds();
 * float anim = sin(t);
 * @endcode
 */
double steady_seconds();

/**
 * @brief 帧间隔计时器（Delta Time 计算）
 *
 * 用于计算“每帧时间差（dt）”，常用于：
 * - 动画更新
 * - 相机移动
 * - 物理模拟
 *
 * 内部逻辑：
 * - 构造时记录当前 steady 时间
 * - 每次 tick_seconds():
 *      dt = now - last
 *      last = now
 */
class FrameDeltaClock {
public:
    /**
     * @brief 构造函数
     *
     * 行为：
     * - 记录当前 steady_seconds() 作为初始时间
     *
     * 注意：
     * - 第一次 tick 的 dt =（构造 → 第一次 tick 的时间差）
     */
    FrameDeltaClock();

    /**
     * @brief 获取帧间隔（秒）
     *
     * 行为：
     * - 计算当前时间与上一次 tick 的差值
     * - 更新内部 last 时间
     *
     * @return delta time（单位：秒）
     *
     * 示例：
     * @code
     * FrameDeltaClock clock;
     * while (running) {
     *     double dt = clock.tick_seconds();
     *     update(dt);
     * }
     * @endcode
     */
    double tick_seconds();

    /**
     * @brief 获取上一次 tick 对应的累计时间
     *
     * 即：
     * - 上一帧结束时的 steady_seconds()
     *
     * @return 秒数（自零点起）
     */
    [[nodiscard]] double last_steady_seconds() const { return last_total_; }

private:
    /// 上一次 tick 时刻（单位：秒，自零点起）
    double last_total_ { 0.0 };
};

} // namespace core
} // namespace lumen
