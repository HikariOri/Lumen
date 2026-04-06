/**
 * @file result.hpp
 * @brief `vk::Result` 的统一封装与调试行为控制
 *
 * 该文件提供对 Vulkan API 返回值 `vk::Result` 的三种使用模式：
 *
 * 1. 默认模式（不定义 VK_RESULT_THROW / VK_RESULT_NODISCARD）
 *    - Result = vk::Result
 *    - 直接使用 Vulkan-Hpp 返回值
 *
 * 2. VK_RESULT_NODISCARD 模式
 *    - Result 是一个带 [[nodiscard]] 的轻量封装
 *    - 用于防止错误忽略返回值
 *
 * 3. VK_RESULT_THROW 模式
 *    - Result 是 RAII 风格封装
 *    - 在析构或转换时触发错误处理或抛异常
 *
 * 适用于需要严格错误处理或调试 Vulkan 调用链的场景。
 */

#pragma once

#include "render/vulkan.hpp"

#ifndef NDEBUG
/// 是否启用 Vulkan Debug Messenger（仅调试模式开启）
#define ENABLE_DEBUG_MESSENGER true
#else
#define ENABLE_DEBUG_MESSENGER false
#endif

namespace lumen::render {

#ifdef VK_RESULT_THROW

/**
 * @brief Vulkan 返回值封装（异常模式）
 */
class Result {
    vk::Result result_;

public:
    /// 用户可自定义的异常回调函数
    static void (*callback_throw)(vk::Result);

    explicit Result(vk::Result r) : result_ { r } {}

    Result(Result &&other) noexcept : result_ { other.result_ } {
        other.result_ = vk::Result::eSuccess;
    }

    ~Result() noexcept(false) {
        if (result_ == vk::Result::eSuccess)
            return;

        if (callback_throw)
            callback_throw(result_);

        throw result_;
    }

    operator vk::Result() {
        vk::Result r = result_;
        result_ = vk::Result::eSuccess;
        return r;
    }
};

#elif defined(VK_RESULT_NODISCARD)

struct [[nodiscard]] Result {
    vk::Result result_;

    explicit Result(vk::Result r) : result_ { r } {}

    operator vk::Result() const { return result_; }
};

#else

using Result = vk::Result;

#endif

} // namespace lumen::render
