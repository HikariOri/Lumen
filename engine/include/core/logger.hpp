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

/// 单一路径配置
struct LoggerPathConfig {
    spdlog::level::level_enum level { spdlog::level::info };
    bool enable { true };
    std::string filePath;
    size_t maxFileSize { 5ULL * 1024 * 1024 };
    size_t maxFiles { 3 };
};

/// 日志配置：引擎与外部分别配置
struct LoggerConfig {
    bool enableConsole { true };
    /// 引擎内部日志（render、platform 等）
    LoggerPathConfig engine { spdlog::level::info, true, "logs/engine.log" };
    /// 外部调用日志（应用层）
    LoggerPathConfig app { spdlog::level::info, true, "logs/app.log" };
};

/**
 * @class Logger
 * @brief 双 logger 封装：引擎内 (lumen) 与外部 (app) 分离
 */
class Logger {
public:
    Logger() = default;
    Logger(const Logger &) = delete;
    Logger(Logger &&) = default;
    Logger &operator=(const Logger &) = delete;
    Logger &operator=(Logger &&) = default;
    ~Logger() = default;

    /**
     * @brief 初始化引擎与外部 logger
     */
    static bool init(const LoggerConfig &config = {});

    static void shutdown();

    /// 引擎内部 logger
    static std::shared_ptr<spdlog::logger> engine();

    /// 外部调用 logger
    static std::shared_ptr<spdlog::logger> app();

    /// 引擎 logger 名称（constexpr）
    static constexpr const char *engine_name() { return "lumen"; }
    /// 外部 logger 名称（constexpr）
    static constexpr const char *app_name() { return "app"; }
};

} // namespace core
} // namespace lumen

// ============== 引擎内部日志宏 ==============
// Debug 模式下输出，Release (NDEBUG) 下为空操作，避免开销
// 传入 source_loc 以使 pattern 中的 %s:%# 显示文件和行号
#ifndef NDEBUG
#define LUMEN_LOG_TRACE(...)                                                   \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::engine())                         \
            _l->log(::spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},  \
                    ::spdlog::level::trace, __VA_ARGS__);                      \
    } while (0)
#define LUMEN_LOG_DEBUG(...)                                                   \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::engine())                         \
            _l->log(::spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},  \
                    ::spdlog::level::debug, __VA_ARGS__);                      \
    } while (0)
#define LUMEN_LOG_INFO(...)                                                    \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::engine())                         \
            _l->log(::spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},  \
                    ::spdlog::level::info, __VA_ARGS__);                       \
    } while (0)
#define LUMEN_LOG_WARN(...)                                                    \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::engine())                         \
            _l->log(::spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},  \
                    ::spdlog::level::warn, __VA_ARGS__);                       \
    } while (0)
#define LUMEN_LOG_ERROR(...)                                                   \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::engine())                         \
            _l->log(::spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},  \
                    ::spdlog::level::err, __VA_ARGS__);                        \
    } while (0)
#define LUMEN_LOG_CRITICAL(...)                                                \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::engine())                         \
            _l->log(::spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},  \
                    ::spdlog::level::critical, __VA_ARGS__);                   \
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
            _l->log(::spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},  \
                    ::spdlog::level::trace, __VA_ARGS__);                      \
    } while (0)
#define LUMEN_APP_LOG_DEBUG(...)                                               \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::app())                            \
            _l->log(::spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},  \
                    ::spdlog::level::debug, __VA_ARGS__);                      \
    } while (0)
#define LUMEN_APP_LOG_INFO(...)                                                \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::app())                            \
            _l->log(::spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},  \
                    ::spdlog::level::info, __VA_ARGS__);                       \
    } while (0)
#define LUMEN_APP_LOG_WARN(...)                                                \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::app())                            \
            _l->log(::spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},  \
                    ::spdlog::level::warn, __VA_ARGS__);                       \
    } while (0)
#define LUMEN_APP_LOG_ERROR(...)                                               \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::app())                            \
            _l->log(::spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},  \
                    ::spdlog::level::err, __VA_ARGS__);                        \
    } while (0)
#define LUMEN_APP_LOG_CRITICAL(...)                                            \
    do {                                                                       \
        if (auto _l = ::lumen::core::Logger::app())                            \
            _l->log(::spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION},  \
                    ::spdlog::level::critical, __VA_ARGS__);                   \
    } while (0)
