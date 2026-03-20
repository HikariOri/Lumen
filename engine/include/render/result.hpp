/**
 * @file result.hpp
 * @brief VkResult 封装与调试宏
 *
 * 根据宏定义选择行为：默认使用原生 VkResult，
 * 可选 VK_RESULT_THROW（失败抛异常）、VK_RESULT_NODISCARD（忽略返回值警告）。
 */

#pragma once

#include <vulkan/vulkan.h>

#ifndef NDEBUG
#define ENABLE_DEBUG_MESSENGER true
#else
#define ENABLE_DEBUG_MESSENGER false
#endif

namespace lumen::render {

#ifdef VK_RESULT_THROW
/**
 * @brief VkResult 封装，失败时通过 callback 或抛异常处理
 */
class Result {
    VkResult result_;

public:
    static void (*callback_throw)(VkResult);

    Result(VkResult r) : result_ { r } {}

    Result(Result &&other) noexcept : result_ { other.result_ } {
        other.result_ = VK_SUCCESS;
    }

    ~Result() noexcept(false) {
        if (static_cast<uint32_t>(result_) < VK_RESULT_MAX_ENUM)
            return;
        if (callback_throw)
            callback_throw(result_);
        throw result_;
    }

    operator VkResult() {
        VkResult r = result_;
        result_ = VK_SUCCESS;
        return r;
    }
};

#elif defined(VK_RESULT_NODISCARD)
/** @brief 带 [[nodiscard]] 的 Result，忽略返回值会触发编译器警告 */
struct [[nodiscard]] Result {
    VkResult result_;
    Result(VkResult r) : result_ { r } {}
    operator VkResult() const { return result_; }
};

#else
/** @brief 默认：直接使用 VkResult 类型别名 */
using Result = VkResult;
#endif

} // namespace lumen::render
