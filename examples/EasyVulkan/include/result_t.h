/**
 * @file result_t.h
 * @brief VkResult 封装，支持异常抛出或 [[nodiscard]] 警告
 *
 * 根据宏定义选择行为：
 * - VK_RESULT_THROW: 非成功时抛异常
 * - VK_RESULT_NODISCARD: 忽略返回值时编译器警告
 * - 默认: 使用原生 VkResult
 */

#include "EasyVKStart.h"

#ifndef NDEBUG
#define ENABLE_DEBUG_MESSENGER true
#else
#define ENABLE_DEBUG_MESSENGER false
#endif

#ifdef VK_RESULT_THROW
/** @brief VkResult 封装，失败时通过 callback 或抛异常处理 */
class result_t {
    VkResult result;

public:
    static void (*callback_throw)(VkResult);

    result_t(VkResult result) : result(result) {}

    result_t(result_t &&other) noexcept : result(other.result) {
        other.result = VK_SUCCESS;
    }

    ~result_t() noexcept(false) {
        if (uint32_t(result) < VK_RESULT_MAX_ENUM) {
            return;
        }
        if (callback_throw) {
            callback_throw(result);
        }
        throw result;
    }

    operator VkResult() {
        VkResult result = this->result;
        this->result = VK_SUCCESS;
        return result;
    }
};

inline void (*result_t::callback_throw)(VkResult);

#elifdef VK_RESULT_NODISCARD
/** @brief 带 [[nodiscard]] 的 result_t，忽略返回值会触发编译器警告 */
struct [[nodiscard]] result_t {
    VkResult result;
    result_t(VkResult result) : result(result) {}
    operator VkResult() const { return result; }
};
// 在本文件中关闭弃值提醒（因为我懒得做处理）
#pragma warning(disable : 4834)
#pragma warning(disable : 6031)

#else
/** @brief 默认：直接使用 VkResult 类型 */
using result_t = VkResult;
#endif
