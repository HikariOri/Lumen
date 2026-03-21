/**
 * @file logger.hpp
 * @brief 日志系统：引擎日志与外部调用日志分离，支持控制台与文件输出
 */

#pragma once

#include <memory>
#include <string>

#include <spdlog/spdlog.h>

namespace lumen {
namespace core {

/// 日志级别
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
};

/// 单一路径配置
struct LoggerPathConfig {
    LogLevel level { LogLevel::Debug };
    bool enable { true };
    std::string filePath;
    size_t maxFileSize { 5ULL * 1024 * 1024 };
    size_t maxFiles { 3 };
};

/// 日志配置：引擎与外部分别配置，默认均为 debug 级别
struct LoggerConfig {
    bool enableConsole { true };
    /// 引擎内部日志（render、platform 等）
    LoggerPathConfig engine { LogLevel::Debug, true, "logs/engine.log" };
    /// 外部调用日志（应用层）
    LoggerPathConfig app { LogLevel::Debug, true, "logs/app.log" };
};

/**
 * @class Logger
 * @brief 单例：引擎内 (lumen) 与外部 (app) 双 logger
 */
class Logger {
public:
    static bool init(const LoggerConfig &config = {});
    static void shutdown();

    static std::shared_ptr<spdlog::logger> engine();
    static std::shared_ptr<spdlog::logger> app();

    static constexpr const char *engine_name() { return "lumen"; }
    static constexpr const char *app_name() { return "app"; }

    Logger(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger &operator=(Logger &&) = delete;

private:
    static Logger &instance();

    bool init_(const LoggerConfig &config);
    void shutdown_();
    std::shared_ptr<spdlog::logger> engine_();
    std::shared_ptr<spdlog::logger> app_();

    Logger() = default;
    ~Logger() = default;
};

namespace detail {
inline spdlog::level::level_enum to_spdlog(LogLevel l) {
    switch (l) {
    case LogLevel::Trace: return spdlog::level::trace;
    case LogLevel::Debug: return spdlog::level::debug;
    case LogLevel::Info: return spdlog::level::info;
    case LogLevel::Warn: return spdlog::level::warn;
    case LogLevel::Error: return spdlog::level::err;
    case LogLevel::Critical: return spdlog::level::critical;
    default: return spdlog::level::info;
    }
}
} // namespace detail

} // namespace core
} // namespace lumen

// ============== 引擎内部日志宏 ==============
// Debug 模式下输出，Release (NDEBUG) 下为空操作，避免开销
// 传入 source_loc 以使 pattern 中的 %s:%# 显示文件和行号
#ifndef NDEBUG
#define LUMEN_LOG_TRACE(...)                                                   \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::engine())                         \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::lumen::core::detail::to_spdlog(                              \
                    ::lumen::core::LogLevel::Trace),                           \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_LOG_DEBUG(...)                                                   \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::engine())                         \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::lumen::core::detail::to_spdlog(                              \
                    ::lumen::core::LogLevel::Debug),                           \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_LOG_INFO(...)                                                    \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::engine())                         \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::lumen::core::detail::to_spdlog(                              \
                    ::lumen::core::LogLevel::Info),                            \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_LOG_WARN(...)                                                    \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::engine())                         \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::lumen::core::detail::to_spdlog(                              \
                    ::lumen::core::LogLevel::Warn),                            \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_LOG_ERROR(...)                                                   \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::engine())                         \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::lumen::core::detail::to_spdlog(                              \
                    ::lumen::core::LogLevel::Error),                           \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_LOG_CRITICAL(...)                                                \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::engine())                         \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::lumen::core::detail::to_spdlog(                              \
                    ::lumen::core::LogLevel::Critical),                        \
                __VA_ARGS__);                                                  \
    } while (0)
#else
#define LUMEN_LOG_TRACE(...) ((void)0)
#define LUMEN_LOG_DEBUG(...) ((void)0)
#define LUMEN_LOG_INFO(...) ((void)0)
#define LUMEN_LOG_WARN(...) ((void)0)
#define LUMEN_LOG_ERROR(...) ((void)0)
#define LUMEN_LOG_CRITICAL(...) ((void)0)
#endif

// ============== 外部调用日志宏 ==============
#define LUMEN_APP_LOG_TRACE(...)                                               \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::app())                            \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::lumen::core::detail::to_spdlog(                              \
                    ::lumen::core::LogLevel::Trace),                           \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_APP_LOG_DEBUG(...)                                               \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::app())                            \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::lumen::core::detail::to_spdlog(                              \
                    ::lumen::core::LogLevel::Debug),                           \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_APP_LOG_INFO(...)                                                \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::app())                            \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::lumen::core::detail::to_spdlog(                              \
                    ::lumen::core::LogLevel::Info),                            \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_APP_LOG_WARN(...)                                                \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::app())                            \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::lumen::core::detail::to_spdlog(                              \
                    ::lumen::core::LogLevel::Warn),                            \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_APP_LOG_ERROR(...)                                               \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::app())                            \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::lumen::core::detail::to_spdlog(                              \
                    ::lumen::core::LogLevel::Error),                           \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_APP_LOG_CRITICAL(...)                                            \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::app())                            \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::lumen::core::detail::to_spdlog(                              \
                    ::lumen::core::LogLevel::Critical),                        \
                __VA_ARGS__);                                                  \
    } while (0)
