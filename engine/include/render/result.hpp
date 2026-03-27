/**
 * @file result.hpp
 * @brief VkResult 的统一封装与调试行为控制
 *
 * 该文件提供对 Vulkan API 返回值 VkResult 的三种使用模式：
 *
 * 1. 默认模式（不定义 VK_RESULT_THROW / VK_RESULT_NODISCARD）
 *    - Result = VkResult
 *    - 直接使用 Vulkan 原始返回值
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

#include <vulkan/vulkan.h>

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
 *
 * 特点：
 * - 自动错误检查（析构或隐式转换时触发）
 * - 可选 callback 统一处理错误
 * - 支持 throw VkResult
 *
 * 注意：
 * - 不建议在高频路径中使用（有异常开销）
 */
class Result {
    VkResult result_;

public:
    /// 用户可自定义的异常回调函数
    static void (*callback_throw)(VkResult);

    /// 构造函数：直接包装 VkResult
    explicit Result(VkResult r) : result_ { r } {}

    /// 移动构造：转移结果所有权
    Result(Result &&other) noexcept : result_ { other.result_ } {
        other.result_ = VK_SUCCESS;
    }

    /**
     * @brief 析构时检查错误
     *
     * 如果 result_ 表示失败：
     * - 优先调用 callback_throw
     * - 然后抛出 VkResult 异常
     */
    ~Result() noexcept(false) {
        if (static_cast<uint32_t>(result_) < VK_RESULT_MAX_ENUM)
            return;

        if (callback_throw)
            callback_throw(result_);

        throw result_;
    }

    /**
     * @brief 隐式转换为 VkResult
     *
     * 转换后会清空内部状态，避免重复触发错误处理
     */
    operator VkResult() {
        VkResult r = result_;
        result_ = VK_SUCCESS;
        return r;
    }
};

#elif defined(VK_RESULT_NODISCARD)

/**
 * @brief Vulkan 返回值封装（nodiscard 模式）
 *
 * 特点：
 * - 不允许忽略返回值（编译器警告）
 * - 不做运行时处理
 * - 仅用于增强代码安全性
 */
struct [[nodiscard]] Result {
    VkResult result_;

    explicit Result(VkResult r) : result_ { r } {}

    /// 隐式转换回 VkResult
    operator VkResult() const { return result_; }
};

#else

/**
 * @brief 默认模式：直接使用 VkResult
 *
 * 最轻量模式，无额外封装开销
 */
using Result = VkResult;

#endif

} // namespace lumen::render
