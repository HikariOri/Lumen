/**
 * @file event_dispatcher.cpp
 */

#include "platform/event_dispatcher.hpp"

#include <type_traits>
#include <variant>

namespace lumen::platform {

uint32_t event_categories(const Event &e) {
    return std::visit(
        [](const auto &arg) -> uint32_t {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, EventQuit>) {
                return static_cast<uint32_t>(EventCategory::Application);
            }
            if constexpr (std::is_same_v<T, EventKeyDown> ||
                          std::is_same_v<T, EventKeyUp>) {
                return static_cast<uint32_t>(EventCategory::Input) |
                       static_cast<uint32_t>(EventCategory::Keyboard);
            }
            if constexpr (std::is_same_v<T, EventMouseMove> ||
                          std::is_same_v<T, EventMouseWheel>) {
                return static_cast<uint32_t>(EventCategory::Input) |
                       static_cast<uint32_t>(EventCategory::Mouse);
            }
            if constexpr (std::is_same_v<T, EventMouseButtonDown> ||
                          std::is_same_v<T, EventMouseButtonUp>) {
                return static_cast<uint32_t>(EventCategory::Input) |
                       static_cast<uint32_t>(EventCategory::Mouse) |
                       static_cast<uint32_t>(EventCategory::MouseButton);
            }
            if constexpr (std::is_same_v<T, EventWindowResize> ||
                          std::is_same_v<T, EventWindowMinimize> ||
                          std::is_same_v<T, EventWindowMaximize> ||
                          std::is_same_v<T, EventWindowRestore>) {
                return static_cast<uint32_t>(EventCategory::Application);
            }
            return 0u;
        },
        e);
}

} // namespace lumen::platform
