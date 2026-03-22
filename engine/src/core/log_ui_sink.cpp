/**
 * @file log_ui_sink.cpp
 */

#include "core/log_ui_sink.hpp"
#include "core/log_view_buffer.hpp"

#include <mutex>
#include <string>

#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/base_sink.h>

namespace lumen {
namespace core {
namespace {

class log_view_sink_mt final : public spdlog::sinks::base_sink<std::mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg &msg) override {
        std::string logger(msg.logger_name.data(), msg.logger_name.size());
        std::string text(msg.payload.data(), msg.payload.size());
        LogViewBuffer::instance().push_line(msg.level, std::move(logger),
                                             std::move(text));
    }
    void flush_() override {}
};

} // namespace

std::shared_ptr<spdlog::sinks::sink> make_log_view_sink() {
    return std::make_shared<log_view_sink_mt>();
}

} // namespace core
} // namespace lumen
