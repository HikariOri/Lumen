/**
 * @file log_view_buffer.hpp
 * @brief 线程安全的环形日志缓冲，供 ImGui 日志面板与 spdlog UI sink 使用
 *
 * LogViewBuffer 提供一个容量限制的环形缓存，用于存储来自日志 UI
 * sink 的日志条目（LogViewLine）。该缓冲区是线程安全的，可以从任意
 * 线程推入日志，同时可以安全地创建快照用于 UI 显示。
 */

#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/common.h>

#include "core/log/logger.hpp"

namespace core::log {

/**
 * @struct LogViewLine
 * @brief 单条日志缓存行
 *
 * 每个 LogViewLine 表示一条被写入 LogViewBuffer 的日志，
 * 包含：
 * - 日志级别
 * - 格式化时间（毫秒精度）
 * - logger 名称
 * - 日志正文消息
 */
struct LogViewLine {
    spdlog::level::level_enum level { spdlog::level::off }; ///< 日志级别
    std::string
        time; ///< 本地时间字符串，与控制台 pattern `%Y-%m-%d %H:%M:%S.%e` 对齐
    std::string logger;  ///< 该日志属于哪个 logger
    std::string message; ///< 实际日志文本
};

/**
 * @class LogViewBuffer
 * @brief 环形缓存日志存储
 *
 * 该类用于存储最近的日志行，可由 UI 线程获取快照以显示日志。
 * 内部使用 std::deque 作为环形队列，并由互斥量保护线程安全访问。
 */
class LogViewBuffer {
public:
    /**
     * @brief 获取单例实例
     *
     * LogViewBuffer 通过单例模式提供全局访问点，
     * 避免多个 UI 缓冲实例导致数据不一致。
     *
     * @return 全局 LogViewBuffer 实例引用
     */
    static LogViewBuffer &instance();

    /**
     * @brief 设置缓冲区最大容量
     *
     * @param max_lines 最大可容纳行数（超过时旧行将被弹出）
     */
    void set_capacity(std::size_t max_lines);

    /**
     * @brief 获取当前缓冲区容量
     *
     * @return 最大行数
     */
    std::size_t capacity() const;

    /**
     * @brief 向缓冲区推入一条日志
     *
     * 如果当前行数超过 capacity()，会自动弹出最旧的日志。
     *
     * @param level 日志级别
     * @param time_str 格式化时间字符串
     * @param logger_name logger 名称
     * @param message 日志内容
     */
    void push_line(spdlog::level::level_enum level, std::string time_str,
                   std::string logger_name, std::string message);

    /**
     * @brief 清空缓冲区
     *
     * 删除所有已缓存的日志行。
     */
    void clear();

    /**
     * @brief 获取当前缓冲区快照
     *
     * 返回当前所有缓存的日志行的副本。该操作不会改变内部状态，
     * 因此适合 UI 线程调用来渲染日志历史。
     *
     * @return vector<LogViewLine> 当前日志缓存列表副本
     */
    std::vector<LogViewLine> snapshot() const;

    // 禁止拷贝与赋值，以保持单例语义
    LogViewBuffer(const LogViewBuffer &) = delete;
    LogViewBuffer &operator=(const LogViewBuffer &) = delete;

private:
    /// 私有构造函数，仅能通过 instance() 获取单例
    LogViewBuffer() = default;

    mutable std::mutex mutex_;       ///< 互斥量，保护多线程访问
    std::size_t max_lines_ { 4096 }; ///< 最大行数
    std::deque<LogViewLine> lines_;  ///< 内部日志行存储
};

} // namespace core::log
