/**
 * @file log_view_buffer.cpp
 */

#include "core/log_view_buffer.hpp"

namespace lumen {
namespace core {

LogViewBuffer &LogViewBuffer::instance() {
    static LogViewBuffer inst;
    return inst;
}

void LogViewBuffer::set_capacity(std::size_t max_lines) {
    std::lock_guard lock(mutex_);
    max_lines_ = max_lines < 64 ? 64 : max_lines;
    while (lines_.size() > max_lines_) {
        lines_.pop_front();
    }
}

std::size_t LogViewBuffer::capacity() const {
    std::lock_guard lock(mutex_);
    return max_lines_;
}

void LogViewBuffer::push_line(spdlog::level::level_enum level,
                              std::string time_str, std::string logger_name,
                              std::string message) {
    std::lock_guard lock(mutex_);
    lines_.push_back(LogViewLine { level, std::move(time_str),
                                  std::move(logger_name), std::move(message) });
    while (lines_.size() > max_lines_) {
        lines_.pop_front();
    }
}

void LogViewBuffer::clear() {
    std::lock_guard lock(mutex_);
    lines_.clear();
}

std::vector<LogViewLine> LogViewBuffer::snapshot() const {
    std::lock_guard lock(mutex_);
    return std::vector<LogViewLine>(lines_.begin(), lines_.end());
}

} // namespace core
} // namespace lumen
