/**
 * @file time.hpp
 * @brief 单调时钟与帧间隔：基于 std::chrono::steady_clock，不依赖 SDL
 */

#pragma once

namespace lumen {
namespace core {

/**
 * @brief 将「时间零点」锚定到当前时刻（建议在进入主循环前调用一次）
 *
 * 若此前尚未建立零点，则零点即为本次调用时刻；若已通过本函数或其它时间查询
 * 建立过零点，则本调用为**空操作**（零点不变）。
 *
 * 用途：让 `steady_seconds()` 在循环入口附近接近 0，避免把冗长初始化算进计时。
 */
void anchor_steady_epoch();

/**
 * @brief 自零点起的单调时间（秒）
 *
 * 零点在首次调用 `anchor_steady_epoch()` 或首次调用 `steady_seconds()` 时建立；
 * 之后全程使用同一零点。
 */
double steady_seconds();

/**
 * @brief 帧间隔计时：在构造时采样当前 steady 时间，每次 tick_seconds() 返回与上一次的间隔
 *
 * 典型用法：在进入主循环前构造，每帧调用 `tick_seconds()` 得到 dt。第一次调用的 dt
 * 为「从构造到首次 tick」的间隔（与原先手动 lastTime/now 写法一致）。
 */
class FrameDeltaClock {
public:
    FrameDeltaClock();

    /** 距离上次 tick（或构造）的秒数 */
    double tick_seconds();

    /** 上一次 tick 边界的 steady_seconds()（首次为构造时刻的值） */
    [[nodiscard]] double last_steady_seconds() const { return last_total_; }

private:
    double last_total_ { 0.0 };
};

} // namespace core
} // namespace lumen
