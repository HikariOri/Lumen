/**
 * @file engine.hpp
 * @brief 引擎统一入口
 *
 * 引入模块：
 * - platform/window.hpp: 窗口（SDL3）
 * - render/render.hpp: Vulkan 封装、管线、资源等
 * - core/logger.hpp: 日志系统
 *
 * 可选：network/file_downloader.h, core/core.h
 */
#pragma once

#include "core/log/logger.hpp"
#include "core/path.hpp"
#include "platform/event.hpp"
#include "platform/event_pump.hpp"
#include "platform/input.hpp"
#include "platform/window.hpp"
#include "render/render.hpp"
