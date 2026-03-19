/**
 * @file logger.cpp
 * @brief Logger 实现：引擎与外部双 logger
 */

#include "core/logger.hpp"

#include <ghc/filesystem.hpp>
#include <string>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace fs = ghc::filesystem;

namespace lumen {
namespace core {

namespace {

constexpr const char* k_engine_logger_name = "lumen";
constexpr const char* k_app_logger_name = "app";

spdlog::sink_ptr make_console_sink() {
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [%s:%#] %v");
    return console;
}

spdlog::sink_ptr make_file_sink(const LoggerPathConfig& cfg) {
    if (!cfg.enable || cfg.filePath.empty()) {
        return nullptr;
    }
    fs::path path { cfg.filePath };
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path());
    }
    if (cfg.maxFileSize > 0) {
        return std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            cfg.filePath, cfg.maxFileSize, cfg.maxFiles);
    }
    return std::make_shared<spdlog::sinks::basic_file_sink_mt>(cfg.filePath);
}

std::shared_ptr<spdlog::logger> create_logger(const char* name,
                                              const LoggerConfig& config,
                                              const LoggerPathConfig& pathCfg) {
    std::vector<spdlog::sink_ptr> sinks;
    if (config.enableConsole) {
        sinks.emplace_back(make_console_sink());
    }
    if (auto file = make_file_sink(pathCfg)) {
        file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [%s:%#] %v");
        sinks.emplace_back(std::move(file));
    }
    if (sinks.empty()) {
        return nullptr;
    }
    auto logger = std::make_shared<spdlog::logger>(
        name, sinks.begin(), sinks.end());
    logger->set_level(pathCfg.level);
    logger->flush_on(pathCfg.level);
    return logger;
}

} // namespace

bool Logger::init(const LoggerConfig& config) {
    spdlog::shutdown();

    auto engineLogger =
        create_logger(k_engine_logger_name, config, config.engine);
    if (!engineLogger) {
        return false;
    }
    spdlog::register_logger(engineLogger);

    auto appLogger = create_logger(k_app_logger_name, config, config.app);
    if (appLogger) {
        spdlog::register_logger(appLogger);
    }

    spdlog::set_default_logger(engineLogger);
    return true;
}

void Logger::shutdown() { spdlog::shutdown(); }

std::shared_ptr<spdlog::logger> Logger::engine() {
    return spdlog::get(k_engine_logger_name);
}

std::shared_ptr<spdlog::logger> Logger::app() {
    return spdlog::get(k_app_logger_name);
}

} // namespace core
} // namespace lumen
