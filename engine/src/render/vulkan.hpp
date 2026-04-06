/**
 * @file vulkan.hpp
 * @brief 引擎统一 Vulkan C++ 绑定入口（Vulkan-Hpp）
 *
 * 业务代码应包含本头，而非直接包含 `<vulkan/vulkan.h>`。
 * `VULKAN_HPP_NO_EXCEPTIONS` 与项目现有 `vk::Result` 检查风格一致。
 */

#pragma once

#ifndef VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_NO_EXCEPTIONS 1
#endif

#include <vulkan/vulkan.hpp>
