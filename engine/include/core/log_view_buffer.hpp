/**
 * @file log_view_buffer.hpp
 * @brief 线程安全的环形日志缓冲，供 ImGui 日志面板与 spdlog UI sink 使用
 */

#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/common.h>

namespace lumen {
namespace core {

struct LogViewLine {
    spdlog::level::level_enum level { spdlog::level::off };
    /// 本地时间，与控制台 pattern 中 `%Y-%m-%d %H:%M:%S.%e` 对齐（毫秒）
    std::string time;
    std::string logger;
    std::string message;
};

class LogViewBuffer {
public:
    static LogViewBuffer &instance();

    void set_capacity(std::size_t max_lines);
    std::size_t capacity() const;

    void push_line(spdlog::level::level_enum level, std::string time_str,
                   std::string logger_name, std::string message);
    void clear();

    std::vector<LogViewLine> snapshot() const;

    LogViewBuffer(const LogViewBuffer &) = delete;
    LogViewBuffer &operator=(const LogViewBuffer &) = delete;

private:
    LogViewBuffer() = default;

    mutable std::mutex mutex_;
    std::size_t max_lines_ { 4096 };
    std::deque<LogViewLine> lines_;
};

} // namespace core
} // namespace lumen
