/**
 * @file arrayRef.hpp
 * @brief 轻量级数组引用类型，模拟对连续数组的只读/读写引用
 */

#pragma once

/**
 * @class arrayRef
 * @tparam T 元素类型
 * @brief 非拥有式数组视图，支持从单个对象、连续范围或指针+数量构造
 */
template <typename T>
class arrayRef {
    T *const pArray = nullptr;
    size_t count = 0;

public:
    /** @brief 默认构造，空引用 count=0 */
    arrayRef() = default;

    /**
     * @brief 从单个对象构造，count=1
     * @param data 单个元素的引用
     */
    arrayRef(T &data) : pArray(&data), count(1) {}
    /**
     * @brief 从满足 contiguous_range 的容器构造
     * @tparam R 连续范围类型
     * @param range 可转发引用范围
     */
    template <typename R>
    arrayRef(R &&range)
        requires requires(R r) {
            requires std::ranges::contiguous_range<R>;
            requires std::ranges::sized_range<R>;
            requires std::ranges::borrowed_range<R>;
            requires std::convertible_to<decltype(std::ranges::data(r)), T *>;
            requires sizeof(std::iter_value_t<R>) == sizeof(T);
        }
        : pArray(std::ranges::data(range)), count(std::ranges::size(range)) {}

    /**
     * @brief 从指针和元素个数构造
     * @param pData 数组首地址
     * @param elementCount 元素个数
     */
    arrayRef(T *pData, size_t elementCount)
        : pArray(pData), count(elementCount) {}

    /**
     * @brief 从无 const 版本构造（当 T 为 const 修饰时）
     * @param other 源 arrayRef
     */
    arrayRef(const arrayRef<std::remove_const_t<T>> &other)
        : pArray(other.Pointer()), count(other.Count()) {}

    /** @brief 获取数组指针 */
    T *Pointer() const { return pArray; }

    /** @brief 获取元素个数 */
    size_t Count() const { return count; }

    /** @brief 下标访问 */
    T &operator[](size_t index) const { return pArray[index]; }
    /** @brief 迭代器起始 */
    T *begin() const { return pArray; }

    /** @brief 迭代器结尾 */
    T *end() const { return pArray + count; }

    /** @brief 禁止赋值，arrayRef 模拟引用语义 */
    arrayRef &operator=(const arrayRef &) = delete;
};
