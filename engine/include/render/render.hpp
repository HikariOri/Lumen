/**
 * @file render.hpp
 * @brief 渲染模块统一入口：Vulkan 封装
 *
 * 根据 RENDER_ENGINE_PLAN 的 Vulkan 实现层设计，
 * 包含 Context、Swapchain、资源、管线、Pass 等。
 */
#pragma once

#include "context.hpp"
#include "surface.hpp"
#include "swapchain.hpp"
#include "result.hpp"
#include "shader.hpp"
#include "pipeline.hpp"
#include "command_buffer.hpp"
#include "pass/render_graph.hpp"
#include "pass/render_pass.hpp"
#include "pass/render_target.hpp"
#include "resource/buffer.hpp"
#include "resource/image.hpp"
#include "resource/sampler.hpp"
#include "resource/texture.hpp"
#include "resource/descriptor.hpp"
