#pragma once

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <random>
#include <sstream>
#include <string>

namespace lumen {
    struct UUID {
        uint64_t high; // 高 64 位
        uint64_t low;  // 低 64 位

        UUID() : high(0), low(0) {}

        // 构造一个随机的 UUID（版本 4，即基于随机数）
        static UUID random() {
            static std::random_device rd;
            static std::mt19937_64 gen(rd());
            std::uniform_int_distribution<uint64_t> dis;

            UUID u;
            u.high = dis(gen);
            u.low = dis(gen);

            // 根据 UUIDv4 的规范设置 version 和 variant bits:
            // version 为 4 → bits 12-15 为 0100
            u.high &= 0xFFFFFFFFFFFF0FFFULL;
            u.high |= 0x0000000000004000ULL;
            // variant 为 RFC 4122 variant → 最两位中的前两位 bit pattern 10xx
            u.low &= 0x3FFFFFFFFFFFFFFFULL;
            u.low |= 0x8000000000000000ULL;

            return u;
        }

        // 从字符串格式创建 UUID，例如 "123e4567-e89b-12d3-a456-426655440000"
        static bool fromString(const std::string &str, UUID &out) {
            // 简单解析，忽略格式验证细节
            std::string s = str;
            // 去掉破折号
            s.erase(std::remove(s.begin(), s.end(), '-'), s.end());
            if (s.size() != 32) {
                return false;
            }
            auto hexCharToInt = [](char c) -> int {
                if (c >= '0' && c <= '9')
                    return c - '0';
                else if (c >= 'a' && c <= 'f')
                    return 10 + (c - 'a');
                else if (c >= 'A' && c <= 'F')
                    return 10 + (c - 'A');
                return -1;
            };
            uint64_t hi = 0, lo = 0;
            for (int i = 0; i < 16; ++i) {
                int hi_nibble = hexCharToInt(s[i * 2]);
                int lo_nibble = hexCharToInt(s[i * 2 + 1]);
                if (hi_nibble < 0 || lo_nibble < 0)
                    return false;
                uint8_t byte = (hi_nibble << 4) | lo_nibble;
                if (i < 8) {
                    hi = (hi << 8) | byte;
                } else {
                    lo = (lo << 8) | byte;
                }
            }
            out.high = hi;
            out.low = lo;
            return true;
        }

        std::string toString() const {
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            // high: 8 字节 = 16 hex chars, low 同样
            oss << std::setw(16) << high;
            oss << std::setw(16) << low;
            // 可插入破折号格式，如果需要
            // 例如，把格式改为 8-4-4-4-12 的标准 UUID 模式
            std::string s = oss.str();
            if (s.size() == 32) {
                // 插 dash: 8-4-4-4-12
                return s.substr(0, 8) + "-" + s.substr(8, 4) + "-" +
                       s.substr(12, 4) + "-" + s.substr(16, 4) + "-" +
                       s.substr(20, 12);
            }
            return s;
        }

        bool operator==(const UUID &other) const {
            return high == other.high && low == other.low;
        }

        bool operator!=(const UUID &other) const { return !(*this == other); }

        // 可用于排序／映射键等
        bool operator<(const UUID &other) const {
            if (high < other.high)
                return true;
            if (high > other.high)
                return false;
            return low < other.low;
        }
    };
} // namespace lumen
