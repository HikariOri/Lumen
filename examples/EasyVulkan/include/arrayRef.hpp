#pragma once

template <typename T>
class arrayRef {
    T *const pArray = nullptr;
    size_t count = 0;

public:
    // 从空参数构造，count为0
    arrayRef() = default;
    // 从单个对象构造，count为1
    arrayRef(T &data) : pArray(&data), count(1) {}
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
    // 从指针和元素个数构造
    arrayRef(T *pData, size_t elementCount)
        : pArray(pData), count(elementCount) {}
    // 若T带const修饰，兼容从对应的无const修饰版本的arrayRef构造
    arrayRef(const arrayRef<std::remove_const_t<T>> &other)
        : pArray(other.Pointer()), count(other.Count()) {}
    // Getter
    T *Pointer() const { return pArray; }
    size_t Count() const { return count; }
    // Const Function
    T &operator[](size_t index) const { return pArray[index]; }
    T *begin() const { return pArray; }
    T *end() const { return pArray + count; }
    // Non-const Function
    // 禁止复制/移动赋值（arrayRef旨在模拟“对数组的引用”，用处归根结底只是传参，故使其同C++引用的底层地址一样，防止初始化后被修改）
    arrayRef &operator=(const arrayRef &) = delete;
};
