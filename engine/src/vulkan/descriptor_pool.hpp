/**
 * @file descriptor_pool.hpp
 * @brief `VkDescriptorPool` RAII。
 */

#pragma once

#include <expected>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace vulkan {

class DescriptorPool final {
public:
    [[nodiscard]] static std::expected<DescriptorPool, std::string>
    create(VkDevice device, std::uint32_t max_sets,
           const std::vector<VkDescriptorPoolSize> &pool_sizes,
           VkDescriptorPoolCreateFlags flags = 0);

    /**
     * @brief 接管已由 `vkCreateDescriptorPool` 创建的对象（独占所有权）。
     */
    [[nodiscard]] static DescriptorPool adopt(VkDevice device,
                                              VkDescriptorPool pool) noexcept;

    DescriptorPool() = default;
    ~DescriptorPool();

    DescriptorPool(const DescriptorPool &) = delete;
    DescriptorPool &operator=(const DescriptorPool &) = delete;
    DescriptorPool(DescriptorPool &&other) noexcept;
    DescriptorPool &operator=(DescriptorPool &&other) noexcept;

    [[nodiscard]] VkDevice device() const noexcept { return device_; }
    [[nodiscard]] VkDescriptorPool pool() const noexcept { return pool_; }
    [[nodiscard]] bool is_valid() const noexcept {
        return pool_ != VK_NULL_HANDLE;
    }

private:
    DescriptorPool(VkDevice device, VkDescriptorPool pool) noexcept;

    void destroy() noexcept;

    VkDevice device_ { VK_NULL_HANDLE };
    VkDescriptorPool pool_ { VK_NULL_HANDLE };
};

} // namespace vulkan
