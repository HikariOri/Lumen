#pragma once

#include "rhi/vulkan.hpp"

#include <vk_mem_alloc.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace rhi {

enum class BufferUsage : std::uint32_t {
    Vertex = 1u << 0,
    Index = 1u << 1,
    Uniform = 1u << 2,
    Storage = 1u << 3,
    TransferSrc = 1u << 4,
    TransferDst = 1u << 5,
};

constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) noexcept {
    using U = std::underlying_type_t<BufferUsage>;
    return static_cast<BufferUsage>(static_cast<U>(a) | static_cast<U>(b));
}

constexpr BufferUsage operator&(BufferUsage a, BufferUsage b) noexcept {
    using U = std::underlying_type_t<BufferUsage>;
    return static_cast<BufferUsage>(static_cast<U>(a) & static_cast<U>(b));
}

constexpr BufferUsage &operator|=(BufferUsage &a, BufferUsage b) noexcept {
    a = a | b;
    return a;
}

enum class MemoryUsage : std::uint8_t { GPU_ONLY, CPU_TO_GPU, GPU_TO_CPU };

struct BufferDesc {
    std::size_t size {};
    BufferUsage usage {};
    MemoryUsage memory { MemoryUsage::GPU_ONLY };
    const void *data = nullptr;
};

struct BufferHandle {
    std::uint32_t id {};
};

struct BufferResource {
    vk::Buffer buffer;
    VmaAllocation allocation {};
    std::size_t size {};
};

[[nodiscard]] constexpr bool is_valid(BufferHandle h) noexcept {
    return h.id != 0;
}

} // namespace rhi
