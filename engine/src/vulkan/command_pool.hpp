/**
 * @file command_pool.hpp
 * @brief `VkCommandPool` RAII 与主级 `VkCommandBuffer` 分配。
 */

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace vulkan {

class CommandPool final {
public:
    [[nodiscard]] static std::expected<CommandPool, std::string>
    create(VkDevice device, std::uint32_t queue_family_index,
           VkCommandPoolCreateFlags flags =
               VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    CommandPool() = default;
    ~CommandPool();

    CommandPool(const CommandPool &) = delete;
    CommandPool &operator=(const CommandPool &) = delete;
    CommandPool(CommandPool &&other) noexcept;
    CommandPool &operator=(CommandPool &&other) noexcept;

    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkCommandPool pool() const noexcept { return pool_; }
    [[nodiscard]] bool is_valid() const noexcept {
        return pool_ != VK_NULL_HANDLE;
    }

    [[nodiscard]] std::expected<std::vector<VkCommandBuffer>, std::string>
    allocate_primary(std::uint32_t count) const;

private:
    CommandPool(VkDevice device, VkCommandPool pool) noexcept;

    void destroy() noexcept;

    VkDevice device_ { VK_NULL_HANDLE };
    VkCommandPool pool_ { VK_NULL_HANDLE };
};

} // namespace vulkan
