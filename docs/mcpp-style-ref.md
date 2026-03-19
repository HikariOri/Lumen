# mcpp-style-ref

> Modern C++ Style Reference | 现代 C++ 编码风格参考

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License](https://img.shields.io/badge/license-Apache_2.0-blue.svg)](LICENSE-CODE)

```cpp
#include <print>

int main() {
    std::println("Hello, C++23!");
}
```

> 现代 C++ 编码风格文档，面向 C++23 项目参考。

## 目录

- 一、标识符命名风格
  - 1.0 [类型名 - 大驼峰](#10-类型名---大驼峰)
  - 1.1 [对象/数据成员 - 小驼峰](#11-对象数据成员---小驼峰)
  - 1.2 [函数 - 下划线 (snake_case)](#12-函数---下划线snake_case)
  - 1.3 [私有表示 - `_` 后缀](#13-私有表示---_后缀)
  - 1.4 [空格 - 增强可读性](#14-空格---增强可读性)
  - 1.5 [其他](#15-其他)
- 二、实践参考
  - 2.0 [auto 的使用](#20-auto-的使用)
  - 2.1 [统一使用 `{}` 初始化](#21-统一使用--初始化)
  - 2.2 [优先使用智能指针](#22-优先使用智能指针)
  - 2.3 [用 std::string_view 替代 char*](#23-用-stdstring_view-替代-char)
  - 2.4 [用 optional/expected 替代 int 错误码](#24-用-optionalexpected-替代-int-错误码)
  - 2.5 [RAII 资源管理](#25-raii-资源管理)
  - 2.6 [emplace_back 就地构造](#26-emplace_back-就地构造)
  - 2.7 [Ranges 与算法](#27-ranges-与算法)
  - 2.8 [Range views 惰性视图](#28-range-views-惰性视图)
  - 2.9 [Concepts 概念约束](#29-concepts-概念约束)
- 三、配置文件
  - 3.0 [Clang-Format 配置](./clang-format.md)
  - 3.1 [Clang-Tidy 配置](./clang-tidy.md)

## 一、标识符命名风格

> 核心思想：通过**标识符**风格设计，能快速识别 — 类型、函数、数据以及封装性

下方示例综合展示各小节要点：

```cpp
#include <string>

namespace mcpplibs {
namespace mylib {

class StyleRef {  // 类型名大驼峰

private:
    int data_;           // 私有数据成员 xxx_
    std::string fileName_;

public:  // 构造函数 / Rule of Five 单独放一个 public 区域

    StyleRef() = default;
    StyleRef(const StyleRef&) = default;
    StyleRef(StyleRef&&) = default;
    StyleRef& operator=(const StyleRef&) = default;
    StyleRef& operator=(StyleRef&&) = default;
    ~StyleRef() = default;

public:  // 公有函数区域 — 函数名 snake_case，参数名小驼峰

    void load_config_file(std::string fileName) {
        parse_(fileName);
    }

private:

    void parse_(std::string config) {}  // 私有成员函数以 _ 结尾

};

}  // namespace mylib
}  // namespace mcpplibs
```

### 1.0 类型名 - 大驼峰

> 单词首字母都大写，单词之间不加下划线

- 例：`StyleRef`, `HttpServer`, `JsonParser`

```cpp
struct StyleRef {
    using FileNameType = std::string;
};
```

### 1.1 对象/数据成员 - 小驼峰

> 首单词首字母小写，后续单词首字母大写，不加下划线

- 例：`fileName`, `configText`

```cpp
struct StyleRef {
    std::string fileName;
};

StyleRef mcppStyle;
```

### 1.2 函数 - 下划线 (snake_case)

> 全小写（通常），单词用下划线连接

- 例：`load_config_file()`, `parse_()`, `max_retry_count()`

```cpp
class StyleRef {
public:
    void load_config_file(const std::string& fileName) {}
};
```

### 1.3 私有表示 - `_` 后缀

> 在标识符后加 `_` 表示对外部不可访问/私有的数据或函数

- 例：`fileName_`, `parse_`

```cpp
class StyleRef {
private:
    std::string fileName_;
    void parse_(const std::string& config) {}
};
```

### 1.4 空格 - 增强可读性

> 在符号两侧使用空格，提升可读性

- **推荐**：`T x { ... }` 符号两侧留空
- **例**：`int n { 42 }`, `std::vector<int> v { 1, 2, 3 }`

```cpp
int n { 42 };
struct Point { int x, y; };
Point p { 10, 20 };
```

### 1.5 其他

- **不可变常量**（替代宏）：全大写 + 下划线，例：`MAX_SIZE`, `DEFAULT_TIMEOUT`
- **全局数据/成员**：前缀 `g`，例：`StyleRef gStyleRef;`
- **模板命名**：可参考类和函数的命名风格

## 二、实践参考

> 现代 C++ 常用编码实践，提升代码健壮性、可读性与维护性

### 2.0 auto 的使用

> 在类型可推断或冗长时使用 `auto`，在需要明确表达意图时保留显式类型

- **推荐**：迭代器、lambda、复杂类型、类型明显可推断时使用 `auto`
- **避免**：过度使用导致可读性下降；需要明确类型表达意图时

```cpp
#include <vector>

void process_items(std::vector<int>& vec) {
    for (auto& item : vec) {
        item *= 2;
    }
    auto result = [](int a, int b) { return a + b; }(1, 2);
}
```

### 2.1 统一使用 `{}` 初始化

> 优先使用花括号 `{}` 进行统一初始化，可避免窄化转换并支持聚合初始化

- **推荐**：尽量使用 `T x { ... }` 或 `T x = { ... }`
- **避免**：窄化转换；与 `std::initializer_list` 歧义时需注意

```cpp
int n { 42 };
std::vector<int> v { 1, 2, 3 };
std::string s { "hello" };

struct Point { int x, y; };
Point p { 10, 20 };
```

### 2.2 优先使用智能指针

> 使用 `std::unique_ptr`、`std::shared_ptr` 管理动态内存，避免裸 `new`/`delete`

- **推荐**：明确所有权语义；使用 `std::make_unique`、`std::make_shared`
- **避免**：裸 `new`/`delete`；混用智能指针与裸指针

```cpp
#include <memory>

class Resource {};

void use_resource() {
    auto p = std::make_unique<Resource>();
}

void share_resource() {
    auto p = std::make_shared<Resource>();
}
```

### 2.3 用 std::string_view 替代 char*

> 对只读字符串参数使用 `std::string_view`，避免拷贝且可接受多种字符串类型

- **适用**：函数参数、临时字符串、不拥有内存的只读视图
- **注意**：`string_view` 不拥有数据，调用方需保证底层数据有效

```cpp
#include <string_view>

void process(std::string_view input) {
    for (auto c : input) { /* ... */ }
}
```

### 2.4 用 optional/expected 替代 int 错误码

> 使用 `std::optional` 表示「可有可无」，使用 `std::expected` 表示「成功或错误」

- **std::optional** (C++17)：表示可能无值
- **std::expected** (C++23)：表示成功返回值或错误信息

```cpp
#include <optional>
#include <expected>
#include <charconv>
#include <string_view>

std::optional<int> parse_int(std::string_view s) {
    int value {};
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc {}) return std::nullopt;
    return value;
}

std::expected<int, std::string> try_parse(std::string_view s) {
    int value {};
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc {}) return std::unexpected { "parse failed" };
    return value;
}
```

### 2.5 RAII 资源管理

> 将资源获取与对象生命周期绑定，构造时获取、析构时释放

- **适用**：文件句柄、锁、自定义句柄等
- **推荐**：优先使用标准库 RAII 类型，如 `std::fstream`、`std::lock_guard`

```cpp
#include <fstream>
#include <mutex>
#include <string_view>

void read_file(std::string_view path) {
    std::ifstream f { std::string { path } };  // 析构时自动关闭
    std::string line;
    while (std::getline(f, line)) { /* ... */ }
}

void with_lock() {
    std::mutex m;
    std::lock_guard lock { m };
}
```

### 2.6 emplace_back 就地构造

> 使用 `emplace_back` 在容器末尾就地构造元素，避免临时对象拷贝或移动

- **推荐**：构造开销较大的类型；需要多参数构造时
- **避免**：简单类型（如 `int`）用 `push_back` 即可

```cpp
#include <vector>
#include <string>

struct Item {
    int id;
    std::string name;
    Item(int i, std::string n) : id { i }, name { std::move(n) } {}
};

void example() {
    std::vector<Item> items;
    items.emplace_back(1, "foo");           // 就地构造，无临时对象
    items.emplace_back(2, std::string { "bar" });
    // 避免: items.push_back(Item { 1, "foo" });  // 产生临时对象
}
```

### 2.7 Ranges 与算法

> 使用 `std::ranges` 命名空间下的算法，支持 range 作为整体操作，更简洁

- **推荐**：替代 `std::` 算法时；需要链式、声明式风格时
- **常用**：`ranges::sort`, `ranges::find_if`, `ranges::transform`, `ranges::copy_if`

```cpp
#include <algorithm>
#include <ranges>
#include <vector>

void example() {
    std::vector<int> v { 5, 2, 8, 1, 9 };
    std::ranges::sort(v);
    auto it = std::ranges::find_if(v, [](int x) { return x > 5; });

    // 投影 (projection)：按成员排序
    struct Person { std::string name; int age; };
    std::vector<Person> people { { "Bob", 30 }, { "Alice", 25 } };
    std::ranges::sort(people, {}, &Person::age);  // 按 age 升序
}
```

### 2.8 Range views 惰性视图

> 使用 `std::views` 创建惰性、非拥有的 range 视图，不复制数据，按需计算

- **特点**：零拷贝、惰性求值、可组合
- **常用**：`views::filter`, `views::transform`, `views::take`, `views::drop`, `views::reverse`

```cpp
#include <ranges>
#include <vector>
#include <print>

namespace v = std::views;

void example() {
    std::vector<int> nums { 1, 2, 3, 4, 5, 6, 7, 8, 9 };

    // 取偶数，平方，取前 3 个
    auto result = nums
        | v::filter([](int x) { return x % 2 == 0; })
        | v::transform([](int x) { return x * x; })
        | v::take(3);

    for (int x : result) {
        std::println("{}", x);  // 4, 16, 36
    }

    // 跳过前 2 个，反转
    for (int x : nums | v::drop(2) | v::reverse) {
        // 7, 6, 5, 4, 3
    }
}
```

### 2.9 Concepts 概念约束

> 使用 `concept` 约束模板参数，提升可读性和编译期错误信息

- **推荐**：约束泛型接口；表达「可调用」「可迭代」等语义
- **常用**：`std::integral`, `std::floating_point`, `std::ranges::range`, `std::invocable`

```cpp
#include <concepts>
#include <ranges>
#include <vector>

// 自定义 concept
template <typename T>
concept Addable = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
};

template <Addable T>
T sum(T a, T b) {
    return a + b;
}

// 约束 range 元素类型
template <std::ranges::range R>
    requires std::integral<std::ranges::range_value_t<R>>
auto sum_range(const R& r) {
    std::ranges::range_value_t<R> total {};
    for (auto x : r) total += x;
    return total;
}

// std::invocable 约束回调
template <typename F>
    requires std::invocable<F, int>
void apply(int x, F&& f) {
    std::forward<F>(f)(x);
}
```

## 三、配置文件

- [Clang-Format 配置](./clang-format.md) — 代码格式化
- [Clang-Tidy 配置](./clang-tidy.md) — 静态检查
