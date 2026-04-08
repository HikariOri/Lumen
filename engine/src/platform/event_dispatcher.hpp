/**
 * @file event_dispatcher.hpp
 * @brief 对齐 Hazel：`EventCategory`、`DispatchableEvent`（payload +
 * handled）、`EventDispatcher`
 *
 * 平台负载仍为 `event.hpp` 中的 `Event`（`std::variant`）。层链与
 * `EventPump::poll` 在 `event_pump.hpp`。
 */

#pragma once

#include "platform/event.hpp"

namespace lumen {
namespace platform {

enum class EventCategory : uint32_t {
    None = 0,
    Application = 1u << 0,
    Input = 1u << 1,
    Keyboard = 1u << 2,
    Mouse = 1u << 3,
    MouseButton = 1u << 4,
};

[[nodiscard]] constexpr bool event_in_category(uint32_t flags,
                                               EventCategory c) noexcept {
    return (flags & static_cast<uint32_t>(c)) != 0u;
}

[[nodiscard]] uint32_t event_categories(const Event &e);

/**
 * @brief 沿层链传递的负载（对应 Hazel 中带 `Handled` 的 `Event&`）
 */
struct DispatchableEvent {
    Event event;
    bool handled { false };
};

/**
 * @brief 按具体事件类型分派（与 Hazel `EventDispatcher::Dispatch` 相同用法）
 *
 * 若 `fn` 返回 `bool`，`true` 会合并到 `handled`；返回 `void` 则不修改
 * `handled`。
 */
class EventDispatcher {
public:
    explicit EventDispatcher(DispatchableEvent &de) : de_(de) {}

    template <typename T, typename Fn>
    bool dispatch(Fn &&fn) {
        if (de_.handled) {
            return false;
        }
        if (T *const p = std::get_if<T>(&de_.event)) {
            using R = std::invoke_result_t<Fn, T &>;
            if constexpr (std::is_same_v<R, bool>) {
                de_.handled |= std::invoke(fn, *p);
            } else {
                std::invoke(fn, *p);
            }
            return true;
        }
        return false;
    }

private:
    DispatchableEvent &de_;
};

} // namespace platform
} // namespace lumen
