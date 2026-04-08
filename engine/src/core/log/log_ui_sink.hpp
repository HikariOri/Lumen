/**
 * @file log_ui_sink.hpp
 * @brief 将 spdlog 输出推入 LogViewBuffer 的 sink 工厂
 */

#pragma once

namespace spdlog {
namespace sinks {
class sink;
}
} // namespace spdlog

namespace core::log {

std::shared_ptr<spdlog::sinks::sink> make_log_view_sink();

} // namespace core::log
