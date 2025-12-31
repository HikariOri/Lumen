/**
 * @file    core.h
 * @brief   一些核心功能的定义
 *
 * Pipeline クラスは Vulkan パイプラインを扱うためのクラスです。
 * 詳細な説明や制約条件、使い方の例などを書くと親切です。
 */

#pragma once

namespace cake {

    /**
     * @brief 生成单个位掩码 (bit mask)。
     *
     * 将传入的整数 x 作为“位偏移 (bit offset)”，
     * 返回一个无符号整型 (unsigned) 值，其第 x 位 (从 0 开始) 为 1，其余位为
     * 0。
     *
     * @param x 要设置为 1 的位偏移 (bit index)，从 0 开始
     * @return 返回一个无符号整数，其中仅第 x 位为 1，其余为 0
     *
     * @code{.cpp}
     * constexpr auto mask0 = BIT(0);  // mask0 == 0x00000001u
     * constexpr auto mask5 = BIT(5);  // mask5 == 0x00000020u
     * unsigned flags = 0;
     * flags |= BIT(3);                // 将 flags 第 3 位设为 1
     * @endcode
     *
     * @note 如果 x 的值大于等于目标整数类型的位宽 (例如 32 位/64
     * 位)，行为将未定义 (可能导致移位溢出)。
     */
    consteval auto BIT(auto x) { return 1U << x; }

} // namespace cake
