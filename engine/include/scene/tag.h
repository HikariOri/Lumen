#pragma once

#include <string>
#include <utility>

struct Tag {
    std::string name;

    Tag() = default;
    explicit Tag(std::string n) : name(std::move(n)) {}

    // 拷贝／移动构造、赋值默认即可

    bool operator==(const Tag &other) const { return name == other.name; }
    bool operator!=(const Tag &other) const { return !(*this == other); }
};