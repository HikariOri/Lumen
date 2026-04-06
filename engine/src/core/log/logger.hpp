/**
 * @file logger.hpp
 * @brief 日志系统：引擎日志与外部调用日志分离，支持控制台与文件输出
 */

#pragma once

#include <memory>
#include <string>

#include <spdlog/spdlog.h>

namespace core::log {

/**
 * @enum LogLevel
 * @brief 日志级别定义
 *
 * 用于控制日志系统的输出粒度，级别从低到高依次递增。
 * 不同级别可用于过滤日志输出，提高调试或发布时的性能与可读性。
 */
enum class LogLevel : uint8_t {
    Trace,    ///< 最详细日志，用于函数级/逐帧调试
    Debug,    ///< 调试信息，仅开发阶段使用
    Info,     ///< 普通信息，描述系统运行状态
    Warn,     ///< 警告信息，表示潜在问题但不影响运行
    Error,    ///< 错误信息，表示功能异常但程序仍可继续
    Critical, ///< 严重错误，可能导致程序崩溃或不可恢复
};

/**
 * @struct LoggerPathConfig
 * @brief 单一路径日志配置
 *
 * 描述一个日志输出目标（通常对应一个文件或输出通道）的配置参数。
 * 每个路径可以独立控制日志级别、开关状态以及文件滚动策略。
 *
 * @note 通常一个日志系统会维护多个 LoggerPathConfig（例如 console + file）
 */
struct LoggerPathConfig {

    /**
     * @brief 日志级别过滤
     *
     * 仅当日志级别 >= 当前设置时才会被记录。
     * 默认值为 Debug。
     */
    LogLevel level { LogLevel::Debug };

    /**
     * @brief 是否启用该日志路径
     *
     * - true  : 启用日志输出
     * - false : 禁用该路径（不会写入）
     */
    bool enable { true };

    /**
     * @brief 日志文件路径
     *
     * 指定日志输出文件的位置。
     * - 支持绝对路径或相对路径
     * - 若为空，通常表示输出到控制台或默认路径
     */
    std::string filePath;

    /**
     * @brief 单个日志文件最大大小（字节）
     *
     * 当文件大小超过该值时触发日志轮转（rolling）。
     * 默认值：5 MB
     */
    size_t maxFileSize { 5ULL * 1024 * 1024 };

    /**
     * @brief 最大保留日志文件数量
     *
     * 超过该数量后，旧日志文件将被删除（或覆盖）。
     * 默认值：3
     */
    size_t maxFiles { 3 };
};

/**
 * @struct LogViewSinkConfig
 * @brief UI 日志缓冲配置（ImGui）
 *
 * 用于将日志写入内存中的环形缓冲区，
 * 供调试 UI（例如 LogPanel）实时显示。
 */
struct LogViewSinkConfig {

    /**
     * @brief 是否启用 UI 日志缓冲
     */
    bool enable { true };

    /**
     * @brief 最大缓存日志行数
     *
     * 超过该数量后将覆盖旧日志（环形缓冲）
     */
    std::size_t maxLines { 4096 };
};

/**
 * @struct LoggerConfig
 * @brief 日志系统整体配置
 *
 * 用于初始化 Logger 单例，分别控制：
 * - 控制台输出
 * - 引擎日志（engine）
 * - 外部应用日志（app）
 * - UI 日志缓冲（LogView）
 *
 * @note engine 与 app 日志通常写入不同文件，便于区分引擎与业务逻辑
 */
struct LoggerConfig {

    /**
     * @brief 是否启用控制台输出
     *
     * - true  : 输出到 stdout / stderr
     * - false : 仅写入文件或其他 sink
     */
    bool enableConsole { true };

    /**
     * @brief 引擎内部日志配置
     *
     * 用于记录引擎模块日志，例如：
     * - 渲染（render）
     * - 平台层（platform）
     * - 资源系统（asset）
     */
    LoggerPathConfig engine { LogLevel::Debug, true, "logs/lumen.log" };

    /**
     * @brief 应用层日志配置
     *
     * 用于外部调用（用户代码）日志输出
     */
    LoggerPathConfig app { LogLevel::Debug, true, "logs/app.log" };

    /**
     * @brief UI 日志缓冲配置
     *
     * 控制是否将日志写入环形缓冲区（用于 ImGui LogPanel）
     */
    LogViewSinkConfig logView {};
};

/**
 * @class Logger
 * @brief 日志系统单例
 *
 * 提供两个独立 logger：
 * - engine() : 引擎内部使用
 * - app()    : 外部调用使用
 *
 * 基于 spdlog 实现，支持多 sink（console / file / UI buffer）。
 *
 * @note 必须在程序启动时调用 init()，结束时调用 shutdown()
 */
class Logger {
public:
    /**
     * @brief 初始化日志系统
     *
     * @param config 日志配置
     * @return 是否初始化成功
     *
     * @note 重复调用通常无效或返回 false（取决于实现）
     */
    static bool init(const LoggerConfig &config = {});

    /**
     * @brief 关闭日志系统
     *
     * 释放所有 logger 与 sink
     */
    static void shutdown();

    /**
     * @brief 获取引擎 logger
     *
     * @return spdlog logger（可能为空）
     */
    static std::shared_ptr<spdlog::logger> engine();

    /**
     * @brief 获取应用 logger
     *
     * @return spdlog logger（可能为空）
     */
    static std::shared_ptr<spdlog::logger> app();

    /**
     * @brief 引擎 logger 名称
     */
    static constexpr const char *engine_name() { return "lumen"; }

    /**
     * @brief 应用 logger 名称
     */
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

/**
 * @brief 将自定义 LogLevel 转换为 spdlog 级别
 *
 * @param l 自定义日志级别
 * @return spdlog::level::level_enum
 */
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

} // namespace core::log

// ============== 引擎内部日志宏 ==============
// Debug 模式下输出，Release (NDEBUG) 下为空操作，避免开销
// 传入 source_loc 以使 pattern 中的 %s:%# 显示文件和行号
#ifndef NDEBUG
#define LUMEN_LOG_TRACE(...)                                                   \
    do {                                                                       \
        if (auto _l = ::core::log::Logger::engine())                           \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::core::log::detail::to_spdlog(::core::log::LogLevel::Trace),  \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_LOG_DEBUG(...)                                                   \
    do {                                                                       \
        if (auto _l = ::core::log::Logger::engine())                           \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::core::log::detail::to_spdlog(::core::log::LogLevel::Debug),  \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_LOG_INFO(...)                                                    \
    do {                                                                       \
        if (auto _l = ::core::log::Logger::engine())                           \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::core::log::detail::to_spdlog(::core::log::LogLevel::Info),   \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_LOG_WARN(...)                                                    \
    do {                                                                       \
        if (auto _l = ::core::log::Logger::engine())                           \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::core::log::detail::to_spdlog(::core::log::LogLevel::Warn),   \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_LOG_ERROR(...)                                                   \
    do {                                                                       \
        if (auto _l = ::core::log::Logger::engine())                           \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::core::log::detail::to_spdlog(::core::log::LogLevel::Error),  \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_LOG_CRITICAL(...)                                                \
    do {                                                                       \
        if (auto _l = ::core::log::Logger::engine())                           \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::core::log::detail::to_spdlog(                                \
                    ::core::log::LogLevel::Critical),                          \
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
        if (auto _l = ::core::log::Logger::app())                              \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::core::log::detail::to_spdlog(::core::log::LogLevel::Trace),  \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_APP_LOG_DEBUG(...)                                               \
    do {                                                                       \
        if (auto _l = ::core::log::Logger::app())                              \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::core::log::detail::to_spdlog(::core::log::LogLevel::Debug),  \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_APP_LOG_INFO(...)                                                \
    do {                                                                       \
        if (auto _l = ::core::log::Logger::app())                              \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::core::log::detail::to_spdlog(::core::log::LogLevel::Info),   \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_APP_LOG_WARN(...)                                                \
    do {                                                                       \
        if (auto _l = ::core::log::Logger::app())                              \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::core::log::detail::to_spdlog(::core::log::LogLevel::Warn),   \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_APP_LOG_ERROR(...)                                               \
    do {                                                                       \
        if (auto _l = ::core::log::Logger::app())                              \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::core::log::detail::to_spdlog(::core::log::LogLevel::Error),  \
                __VA_ARGS__);                                                  \
    } while (0)
#define LUMEN_APP_LOG_CRITICAL(...)                                            \
    do {                                                                       \
        if (auto _l = ::core::log::Logger::app())                              \
            _l->log(                                                           \
                ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION },  \
                ::core::log::detail::to_spdlog(                                \
                    ::core::log::LogLevel::Critical),                          \
                __VA_ARGS__);                                                  \
    } while (0)
