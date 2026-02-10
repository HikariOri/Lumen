#pragma once
// 可能会用上的C++标准库
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
// 如果你惯用左手坐标系，在此定义GLM_FORCE_LEFT_HANDED
// #include <glm.hpp>
// #include <gtc/matrix_transform.hpp>

// stb_image.h
#include <stb_image.h>

#include "vk_enum_string_helper.h" //用于将枚举项转为对应的字符串，方便输出错误信息等
#include <vulkan/vulkan.h>
