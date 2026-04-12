#include "vulkan/context.hpp"

[[nodiscard]] std::vector<std::byte> load_spirv(const std::string &filename) {
    std::ifstream file { filename, std::ios::ate | std::ios::binary };

    if (!file) {
        LUMEN_APP_LOG_ERROR("Failed to open file: {}", filename);
        return {};
    }

    const auto end = file.tellg();
    if (end <= 0) {
        LUMEN_APP_LOG_ERROR("SPIR-V 文件为空或无效: {}", filename);
        return {};
    }
    const auto size = static_cast<std::size_t>(end);
    if (size % sizeof(std::uint32_t) != 0U) {
        LUMEN_APP_LOG_ERROR("SPIR-V 文件大小须为 4 的倍数: {}", filename);
        return {};
    }

    file.seekg(0);
    std::vector<std::byte> out(size);
    file.read(reinterpret_cast<char *>(out.data()), // NOLINT
              static_cast<std::streamsize>(size));
    if (!file) {
        LUMEN_APP_LOG_ERROR("SPIR-V 读取失败: {}", filename);
        return {};
    }
    return out;
}

template <typename PODType>
VkDeviceSize getMinUniformBufferOffsetAlignment(vulkan::Context *context) {

    constexpr VkDeviceSize size = sizeof(PODType);

    VkPhysicalDeviceProperties physicalDeviceProps {};

    vkGetPhysicalDeviceProperties(context->physical_device(),
                                  &physicalDeviceProps);

    const VkDeviceSize minUniformBufferOffsetAlignment =
        physicalDeviceProps.limits.minUniformBufferOffsetAlignment;

    return (size + minUniformBufferOffsetAlignment - 1) &
           ~(minUniformBufferOffsetAlignment - 1);
}
