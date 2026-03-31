/**
 * @file log_ui_sink.cpp
 */

#include "core/log_ui_sink.hpp"
#include "core/log_view_buffer.hpp"

#include <array>
#include <chrono>
#include <ctime>
#include <format>
#include <mutex>
#include <string>

#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/base_sink.h>

namespace lumen {
namespace core {
namespace {

std::string format_msg_time(spdlog::log_clock::time_point tp) {
    using namespace std::chrono;
    const auto sec_tp = time_point_cast<seconds>(tp);
    const auto frac_ms = duration_cast<milliseconds>(tp - sec_tp);
    const std::time_t t = std::chrono::system_clock::to_time_t(
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            sec_tp));
    std::tm tm_buf {};
#if defined(_WIN32)
    if (localtime_s(&tm_buf, &t) != 0) {
        return {};
    }
#else
    if (localtime_r(&t, &tm_buf) == nullptr) {
        return {};
    }
#endif
    std::array<char, 32> date {};
    if (std::strftime(date.data(), date.size(), "%Y-%m-%d %H:%M:%S", &tm_buf) ==
        0) {
        return {};
    }
    return std::format("{}.{:03}", date.data(), frac_ms.count());
}

class log_view_sink_mt final : public spdlog::sinks::base_sink<std::mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg &msg) override {
        std::string logger(msg.logger_name.data(), msg.logger_name.size());
        std::string text(msg.payload.data(), msg.payload.size());
        std::string ts = format_msg_time(msg.time);
        LogViewBuffer::instance().push_line(msg.level, std::move(ts),
                                            std::move(logger), std::move(text));
    }
    void flush_() override {}
}; 

} // namespace

std::shared_ptr<spdlog::sinks::sink> make_log_view_sink() {
    return std::make_shared<log_view_sink_mt>();
}

} // namespace core
} // namespace lumen
