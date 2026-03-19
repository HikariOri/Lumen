/**
 * @file engine.hpp
 * @brief 引擎统一入口
 *
 * 按需引入各模块：
 * - render/render.hpp: Vulkan 封装、管线、资源等
 * - scene/*: 场景图、GameObject、Transform 等
 * - platform/*: 窗口与输入
 * - core/core.h: 核心工具
 */
#pragma once

#include "render/render.hpp"
