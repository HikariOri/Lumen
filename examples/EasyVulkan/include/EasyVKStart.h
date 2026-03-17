/**
 * @file EasyVKStart.h
 * @brief EasyVulkan 公共头文件，标准库、GLM、stb_image、Vulkan 及工具函数
 */

#pragma once
// 可能会用上的C++标准库
#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
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
#include <thread>
#include <unordered_map>
#include <vector>

// GLM
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
// 如果你惯用左手坐标系，在此定义 GLM_FORCE_LEFT_HANDED
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// stb_image.h
#define STB_IMAGE_IMPLEMENTATION
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

/**
 * @defgroup MathUtils 数学工具
 * 符号、区间判断等数学相关工具函数
 */

//----------Math Related-------------------------------------------------------

/**
 * @brief 获取带符号整数的符号
 * @ingroup MathUtils
 * @tparam T 带符号整型
 * @param num 待判断的数值
 * @return 1 为正数，-1 为负数，0 为零
 */
template <std::signed_integral T>
constexpr int GetSign(T num) {
    return (num > 0) - (num < 0);
}

/**
 * @brief 判断两个带符号整数是否同号（严格：0 按正负分别处理）
 * @ingroup MathUtils
 * @tparam T 带符号整型
 * @param num0 第一个数值
 * @param num1 第二个数值
 * @return 同号返回 true，否则返回 false
 */
template <std::signed_integral T>
constexpr bool SameSign(T num0, T num1) {
    return num0 == num1 || !(num0 >= 0 && num1 <= 0 || num0 <= 0 && num1 >= 0);
}

/**
 * @brief 判断两个带符号整数是否同号（弱版本：0 视为正数）
 * @ingroup MathUtils
 * @tparam T 带符号整型
 * @param num0 第一个数值
 * @param num1 第二个数值
 * @return 同号返回 true，否则返回 false
 */
template <std::signed_integral T>
constexpr bool SameSign_Weak(T num0, T num1) {
    return (num0 ^ num1) >= 0;
}

/**
 * @brief 判断数值是否在开区间 (min, max) 内
 * @ingroup MathUtils
 * @tparam T 带符号整型
 * @param min 区间下界（不包含）
 * @param num 待判断的数值
 * @param max 区间上界（不包含）
 * @return 在开区间内返回 true，否则返回 false
 */
template <std::signed_integral T>
constexpr bool Between_Open(T min, T num, T max) {
    return ((min - num) & (num - max)) < 0;
}

/**
 * @brief 判断数值是否在闭区间 [min, max] 内
 * @ingroup MathUtils
 * @tparam T 带符号整型
 * @param min 区间下界（包含）
 * @param num 待判断的数值
 * @param max 区间上界（包含）
 * @return 在闭区间内返回 true，否则返回 false
 */
template <std::signed_integral T>
constexpr bool Between_Closed(T min, T num, T max) {
    return ((num - min) | (max - num)) >= 0;
}
