#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/pfr.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

#include <spirv_reflect.h>

namespace vulkan::shader::reflection {

/// 反射选项：与宿主对 descriptor set 用法的约定（例如 ring buffer）。
struct ReflectOptions {
    /// 若设置：对所有 `set` 索引 **小于等于** 该值的 uniform buffer（SPIR-V
    /// 中为 `UNIFORM_BUFFER`）映射为
    /// `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC`。
    std::optional<std::uint32_t> ringUniformMaxSet {};
};

struct VertexAttribute {
    uint32_t location = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t offset = 0;
};

// ==============================
// 顶点结构体 ↔ 着色器顶点输入 校验（运行时描述）
// ==============================

struct VertexMemberInfo {
    std::uint32_t location {};
    VkFormat format { VK_FORMAT_UNDEFINED };
    std::size_t offset {};
    std::string name;
};

// ---------- Boost.PFR：聚合体顶点成员 → VkFormat / 偏移（声明顺序 = location 0..N-1）----------

namespace detail {

template <typename Field>
[[nodiscard]] constexpr VkFormat vk_format_for_vertex_field() noexcept {
    using T = std::remove_cv_t<Field>;
    if constexpr (std::is_same_v<T, float>) {
        return VK_FORMAT_R32_SFLOAT;
    } else if constexpr (std::is_same_v<T, double>) {
        return VK_FORMAT_R64_SFLOAT;
    } else if constexpr (std::is_same_v<T, std::int32_t>) {
        return VK_FORMAT_R32_SINT;
    } else if constexpr (std::is_same_v<T, std::uint32_t>) {
        return VK_FORMAT_R32_UINT;
    } else if constexpr (std::is_same_v<T, glm::vec2>) {
        return VK_FORMAT_R32G32_SFLOAT;
    } else if constexpr (std::is_same_v<T, glm::vec3>) {
        return VK_FORMAT_R32G32B32_SFLOAT;
    } else if constexpr (std::is_same_v<T, glm::vec4>) {
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    } else if constexpr (std::is_same_v<T, glm::ivec2>) {
        return VK_FORMAT_R32G32_SINT;
    } else if constexpr (std::is_same_v<T, glm::ivec3>) {
        return VK_FORMAT_R32G32B32_SINT;
    } else if constexpr (std::is_same_v<T, glm::ivec4>) {
        return VK_FORMAT_R32G32B32A32_SINT;
    } else if constexpr (std::is_same_v<T, glm::uvec2>) {
        return VK_FORMAT_R32G32_UINT;
    } else if constexpr (std::is_same_v<T, glm::uvec3>) {
        return VK_FORMAT_R32G32B32_UINT;
    } else if constexpr (std::is_same_v<T, glm::uvec4>) {
        return VK_FORMAT_R32G32B32A32_UINT;
    } else if constexpr (std::is_same_v<T, glm::dvec2>) {
        return VK_FORMAT_R64G64_SFLOAT;
    } else if constexpr (std::is_same_v<T, glm::dvec3>) {
        return VK_FORMAT_R64G64B64_SFLOAT;
    } else if constexpr (std::is_same_v<T, glm::dvec4>) {
        return VK_FORMAT_R64G64B64A64_SFLOAT;
    } else {
        return VK_FORMAT_UNDEFINED;
    }
}

template <typename Vertex, std::size_t I>
[[nodiscard]] inline VertexMemberInfo make_vertex_member_info() {
    Vertex scratch {};
    using Field = std::remove_cvref_t<decltype(boost::pfr::get<I>(scratch))>;

    VertexMemberInfo m {};
    m.location = static_cast<std::uint32_t>(I);
    m.format = vk_format_for_vertex_field<Field>();
    m.offset = static_cast<std::size_t>(
        reinterpret_cast<const std::byte *>(&boost::pfr::get<I>(scratch)) -
        reinterpret_cast<const std::byte *>(&scratch));
    m.name = "field_" + std::to_string(I);
    return m;
}

template <typename Vertex, std::size_t... I>
[[nodiscard]] inline std::vector<VertexMemberInfo>
reflect_vertex_members_seq(std::index_sequence<I...>) {
    return std::vector<VertexMemberInfo> {
        make_vertex_member_info<Vertex, I>()...};
}

} // namespace detail

/// 对聚合体 `Vertex` 按声明顺序生成 `VertexMemberInfo`（location =
/// 0..N-1），用于 `validateVertexLayout` 与 `create_vertex_input_state<T>`。
template <typename Vertex>
[[nodiscard]] inline std::vector<VertexMemberInfo> reflect_vertex_members() {
    constexpr std::size_t n = boost::pfr::tuple_size_v<Vertex>;
    static_assert(n > 0,
                  "Vertex must have at least one field for PFR reflection");
    return detail::reflect_vertex_members_seq<Vertex>(
        std::make_index_sequence<n> {});
}

struct DescriptorBinding {
    std::uint32_t set {};
    std::uint32_t binding {};
    VkDescriptorType type { VK_DESCRIPTOR_TYPE_MAX_ENUM };
    std::uint32_t count { 1 };
    VkShaderStageFlags stages { VK_SHADER_STAGE_ALL };
    std::string name;
};

struct PushConstantRange {
    std::uint32_t size {};
    std::uint32_t offset {};
    VkShaderStageFlags stages { VK_SHADER_STAGE_ALL };
};

/// 单个 descriptor set 的布局、pool 统计与已创建的 `VkDescriptorSetLayout`。
struct DescriptorSetLayoutInfo {
    std::uint32_t set {};
    VkDescriptorSetLayout layout { VK_NULL_HANDLE };
    std::vector<VkDescriptorPoolSize> poolSizes;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
};

// 用于 Layout 服用的 key
struct LayoutKey {
    std::uint32_t set;
    std::vector<std::uint8_t> hashData;

    bool operator<(const LayoutKey &other) const {
        if (set != other.set) {
            return set < other.set;
        }
        return hashData < other.hashData;
    }
};

// 全局 Layout 复用池
class LayoutCache {
public:
    static LayoutCache &instance();

    VkDescriptorSetLayout get_layout(VkDevice device,
                                     const DescriptorSetLayoutInfo &info);

    void clear(VkDevice device);

private:
    std::map<LayoutKey, VkDescriptorSetLayout> cache_;
};

class ShaderReflection {
public:
    bool reflect(VkShaderStageFlags stage, std::vector<std::byte> spirvCode,
                 const ReflectOptions &options = {}) noexcept;

    bool reflect(VkShaderStageFlags stage, std::vector<std::uint32_t> spirvCode,
                 const ReflectOptions &options = {}) noexcept;

    bool reflect(VkShaderStageFlags stage, const std::uint32_t *spirvCode,
                 std::size_t codeSize,
                 const ReflectOptions &options = {}) noexcept;

    void merge(const ShaderReflection &other);

    /// 按当前反射结果创建各 set 的 `VkDescriptorSetLayout` 与
    /// `VkPipelineLayout`（set 编号从 0 连续到当前最大 set，缺失编号用空
    /// layout 补齐，以满足 Vulkan `pSetLayouts[i]` 与 set `i` 对应）。
    void create_layouts(VkDevice device) noexcept;

    void destroy_layouts(VkDevice device) noexcept;

    /// 每个 set 单独一个 pool（`max_sets_per_set` 为每 set 可分配的 descriptor
    /// set 数量上限）。
    [[nodiscard]] auto create_pools(VkDevice device,
                                    std::uint32_t maxSetsPerSet = 32) const
        -> std::unordered_map<std::uint32_t, VkDescriptorPool>;

    [[nodiscard]] auto create_descriptor_pool(VkDevice device,
                                              std::uint32_t maxSets = 32) const
        -> VkDescriptorPool;

    [[nodiscard]] std::unordered_map<uint32_t, VkDescriptorSet> allocateSets(
        VkDevice device,
        const std::unordered_map<uint32_t, VkDescriptorPool> &pools) const;

    [[nodiscard]] auto pipeline_layout() const noexcept -> VkPipelineLayout {
        return pipelineLayout_;
    }

    /// 与 `pipeline_layout` 中 `pSetLayouts` 顺序一致：下标 = set 序号。
    [[nodiscard]] auto set_layouts() const noexcept
        -> const std::vector<VkDescriptorSetLayout> & {
        return setLayouts_;
    }

    [[nodiscard]] const std::vector<VertexAttribute> &vertex_input() const {
        return vertexAttributes_;
    }

    /// 将着色器反射的顶点输入与 C++ 侧 `reflect_vertex_members<T>()`
    /// 对比；可选传入 `sizeof(T)` 以校验尾部边界与成员区间不重叠。
    [[nodiscard]] bool validateVertexLayout(
        const std::vector<VertexMemberInfo> &runtimeMembers,
        std::optional<std::size_t> struct_byte_size = std::nullopt) const;

    // 自动生成完整的顶点输入状态
    [[nodiscard]] auto create_vertex_input_state(
        uint32_t binding = 0,
        VkVertexInputRate rate = VK_VERTEX_INPUT_RATE_VERTEX) const
        -> VkPipelineVertexInputStateCreateInfo;

    // 自动生成完整 VertexInputState
    template <typename VertexT>
    [[nodiscard]] VkPipelineVertexInputStateCreateInfo
    create_vertex_input_state(
        uint32_t binding = 0,
        VkVertexInputRate rate = VK_VERTEX_INPUT_RATE_VERTEX) const {
        auto members = reflect_vertex_members<VertexT>();

        static std::vector<VkVertexInputAttributeDescription> attrs;
        attrs.clear();
        for (const auto &m : members) {
            attrs.push_back(
                { m.location, binding, m.format, (uint32_t)m.offset });
        }

        static VkVertexInputBindingDescription bindDesc {};
        bindDesc.binding = binding;
        bindDesc.stride = sizeof(VertexT);
        bindDesc.inputRate = rate;

        return { .sType =
                     VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                 .vertexBindingDescriptionCount = 1,
                 .pVertexBindingDescriptions = &bindDesc,
                 .vertexAttributeDescriptionCount = (uint32_t)attrs.size(),
                 .pVertexAttributeDescriptions = attrs.data() };
    }

private:
    std::vector<DescriptorBinding> bindings_;
    std::vector<PushConstantRange> pushConstantRanges_;
    std::vector<VertexAttribute> vertexAttributes_;
    std::unordered_map<std::uint32_t, DescriptorSetLayoutInfo>
        setLayoutInfos_;

    VkPipelineLayout pipelineLayout_ { VK_NULL_HANDLE };
    std::vector<VkDescriptorSetLayout> setLayouts_;

    static auto apply_ring_uniform_rule_(VkDescriptorType type,
                                         std::uint32_t set,
                                         const ReflectOptions &options)
        -> VkDescriptorType;

    static void merge_pool_sizes_(std::vector<VkDescriptorPoolSize> &dst,
                                  const std::vector<VkDescriptorPoolSize> &src);

    static void
    consolidate_pool_sizes_(std::vector<VkDescriptorPoolSize> &poolSizes);

    [[nodiscard]] LayoutKey makeLayoutKey(
        uint32_t set,
        const std::vector<VkDescriptorSetLayoutBinding> &bindings) const;
};

} // namespace vulkan::shader::reflection
