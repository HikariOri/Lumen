/**
 * @file event_pump.hpp
 * @brief 事件轮询：SDL3 实现，转换为平台无关 Event
 *
 * 每帧调用 poll，获取事件列表并更新 Input 状态。
 * 需在 Window 创建后调用（SDL_Init 已执行）。
 */

#pragma once

#include "platform/event.hpp"
#include "platform/input.hpp"

#include <vector>

namespace lumen {
namespace platform {

/**
 * @class EventPump
 * @brief 事件轮询器：从 SDL 拉取事件并转换为引擎事件模型
 */
class EventPump {
public:
    EventPump() = default;

    /**
     * @brief 轮询本帧所有事件
     * @param events 输出事件列表（会清空后填充）
     * @param input 输出输入状态（每帧更新）
     * @return false 表示收到退出请求，主循环应结束
     */
    bool poll(EventList& events, Input& input);

private:
    static constexpr size_t k_max_keys = 512;
};

} // namespace platform
} // namespace lumen
