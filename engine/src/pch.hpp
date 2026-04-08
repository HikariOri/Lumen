#pragma once

#include "config.hpp"

#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <exception>
#include <expected>
#include <forward_list>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
#include <shared_mutex>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <vulkan/vulkan.h>

#include <vk_mem_alloc.h>

#include <tracy/Tracy.hpp>

#include <ghc/filesystem.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3_image/SDL_image.h>

#include <stb_image.h>

// ===== base =====
#include <absl/base/attributes.h>
#include <absl/base/macros.h>
#include <absl/base/optimization.h>

// ===== strings =====
#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>
#include <absl/strings/str_split.h>
#include <absl/strings/string_view.h>

// ===== containers =====
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>

// ===== types =====
#include <absl/types/any.h>
#include <absl/types/optional.h>
#include <absl/types/variant.h>

// ===== sync =====
#include <absl/synchronization/mutex.h>
#include <absl/synchronization/notification.h>

// ===== time =====
#include <absl/time/clock.h>
#include <absl/time/time.h>

// ===== status =====
#include <absl/status/status.h>
#include <absl/status/statusor.h>

#include <spirv_cross.hpp>
#include <spirv_glsl.hpp>

#include <spirv-reflect/spirv_reflect.h>

// shader编译
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <shaderc/shaderc.hpp>
