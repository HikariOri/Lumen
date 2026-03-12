/**
 * @file EasyVKStart.h
 * @brief EasyVulkan 公共头文件，标准库、GLM、stb_image、Vulkan 及工具函数
 */

#pragma once
// 可能会用上的C++标准库
#include <algorithm>
#include <chrono>
#include <concepts>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <numeric>
#include <print>
#include <span>
#include <sstream>
#include <stack>
#include <unordered_map>
#include <vector>

// GLM
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
// 如果你惯用左手坐标系，在此定义 GLM_FORCE_LEFT_HANDED
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// stb_image.h
#include <stb_image.h>

#include "vk_enum_string_helper.h" //用于将枚举项转为对应的字符串，方便输出错误信息等
#include <vulkan/vulkan.h>

/**
 * @brief 垂直翻转投影矩阵（用于 Vulkan NDC Y 轴向下与 GLM 的适配）
 *
 * @param projection 原始投影矩阵
 * @return 翻转后的投影矩阵
 */
inline glm::mat4 FlipVertical(const glm::mat4 &projection) {
    glm::mat4 _projection = projection;
    for (uint32_t i = 0; i < 4; i++) {
        _projection[i][1] *= -1;
    }
    return _projection;
}
