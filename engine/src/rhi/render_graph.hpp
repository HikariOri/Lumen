#pragma once

#include "rhi/buffer.hpp"
#include "rhi/command_buffer.hpp"
#include "rhi/vulkan.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rhi {

class Device;
struct RgGpuCompileContext;

/// 带 GPU 编译的帧执行参数：与 `execute(cmd, ctx, per_pass)` 中 **按 Pass 索引** 的 `optional` 对应。
struct RgGpuExecuteFrame {
    Device *rhi_device {};
    std::uint32_t swap_image_index {};
    vk::Extent2D surface_extent {};
    /// 与合并反射中 **全部** `eUniformBufferDynamic` 绑定在 `merged.bindings` 中的遍历顺序一致
    /// （与 `update_reflected_buffer_descriptors` / `bind_descriptor_sets` 的 dynamic offset 序一致）。
    std::vector<vk::DeviceSize> dynamic_uniform_offsets;
};

/// 逻辑资源句柄（非 VkBuffer / VkImage）。执行期 barrier 仅对已 `bind_buffer` 的 Buffer 下发。
struct RgResourceId {
    static constexpr std::uint32_t k_invalid =
        std::numeric_limits<std::uint32_t>::max();

    std::uint32_t value { k_invalid };

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != k_invalid;
    }
    [[nodiscard]] constexpr bool operator==(const RgResourceId &) const noexcept =
        default;
};

enum class RgResourceType : std::uint8_t { Buffer, Image };

enum class RgAccessKind : std::uint8_t { Read, Write };

/// Pass 对某一逻辑资源的一次使用（用于依赖分析与 barrier 目标阶段/访问掩码）。
struct RgResourceUsage {
    RgResourceId resource {};
    RgAccessKind access { RgAccessKind::Read };
    vk::PipelineStageFlags stages {};
    vk::AccessFlags access_mask {};
};

/// GPU Pass 上按 **着色器反射 resource_name** 绑定的缓冲；`dynamic_stride > 0` 仅用于 UBO，且会 promote 为
/// dynamic uniform。
enum class RgPassGpuNamedBufferKind : std::uint8_t { Uniform, Storage };

struct RgPassGpuNamedBufferSlot {
    std::string resource_name {};
    RgResourceId resource {};
    RgPassGpuNamedBufferKind kind { RgPassGpuNamedBufferKind::Uniform };
    /// 仅 `kind == Uniform`；`> 0` 时对应绑定 promote 为 `eUniformBufferDynamic`，描述符 range 为该 stride。
    vk::DeviceSize dynamic_stride {};
};

/// Pass 级图形管线元数据；由 `compile(..., RgGpuCompileContext*)` 结合 SPIR-V 反射创建
/// PipelineLayout、描述符与 `VkPipeline`（非 `VkBuffer` 的呈现 framebuffer 亦在此阶段绑定 swapchain）。
struct RgPassGpuDesc {
    std::shared_ptr<const std::vector<std::byte>> vert_spv;
    std::shared_ptr<const std::vector<std::byte>> frag_spv;
    std::vector<vk::VertexInputBindingDescription> vertex_bindings;
    std::vector<vk::VertexInputAttributeDescription> vertex_attributes;
    std::vector<RgPassGpuNamedBufferSlot> named_buffer_slots {};
    /// 由 `compile` 根据 SPIR-V 反射为 `named_buffer_slots` 生成，参与 barrier / DAG；勿手动修改。
    std::vector<RgResourceUsage> reflection_reads {};
    /// 呈现附件 clear（单颜色）；`execute` 内置录制时使用。
    std::array<float, 4> color_attachment_clear { 0.05F, 0.06F, 0.09F, 1.0F };
    RgResourceId draw_vertex_buffer {};
    std::uint32_t draw_vertex_count {};
    std::uint32_t draw_first_vertex {};
    std::uint32_t draw_instance_count { 1 };
    /// `draw_index_count > 0` 时走 `drawIndexed`；与 `draw_vertex_count` 互斥（由 API 保证）。
    RgResourceId draw_index_buffer {};
    std::uint32_t draw_index_count {};
    std::uint32_t draw_first_index {};
    std::int32_t draw_index_vertex_offset {};
    vk::IndexType draw_index_type { vk::IndexType::eUint16 };
};

struct RgPass {
    std::string name;
    std::vector<RgResourceUsage> reads;
    std::vector<RgResourceUsage> writes;
    std::function<void(CommandBuffer &)> execute;
    std::optional<RgPassGpuDesc> gpu;
};

/// 最小 Render Graph（三阶段心智模型）：
/// 1. **声明**：`add_pass` + `read`/`write` 描述数据依赖；可选 `declare_buffer_prior_write`
///    声明 **进入本图执行前** 已在图外完成的 buffer 写（如 `upload_buffer` 的 copy）。
/// 2. **编译**：`compile` 拓扑排序，并为 Buffer 调度 `vk::BufferMemoryBarrier`。
/// 3. **执行**：先 barrier，再按 Pass 录制命令：
///    - `set_execute` 的 `std::function`（与 `execute(cmd)` 搭配）；
///    - 或 `execute(cmd, ctx, per_pass)`：无 lambda 且带 `RgPass::gpu` 的 Pass 由引擎按反射结果自动录
///      `render pass / pipeline / descriptor / draw`（见 `RgGpuExecuteFrame`）。
///
/// Buffer 自动 barrier；Image 仅参与排序，layout / `vk::ImageMemoryBarrier` 由 Pass 内自理（V1）。
class RenderGraph {
public:
    RenderGraph() = default;
    RenderGraph(const RenderGraph &) = delete;
    RenderGraph &operator=(const RenderGraph &) = delete;
    RenderGraph(RenderGraph &&) = default;
    RenderGraph &operator=(RenderGraph &&) = default;
    ~RenderGraph() = default;

    [[nodiscard]] RgResourceId create_buffer();
    [[nodiscard]] RgResourceId create_image();

    /// 绑定逻辑 Buffer 与 Vulkan 缓冲；`size == 0` 表示 `vk::WholeSize`。
    /// `rhi_buffer` 供 GPU 编译阶段写描述符（`try_get`）；缺省则不参与反射描述符更新。
    void bind_buffer(RgResourceId id, vk::Buffer buffer, vk::DeviceSize size = 0,
                     BufferHandle rhi_buffer = {});

    /// 声明：在 `execute` 之前、图外已对该 buffer 完成一次写（阶段/访问与真实提交一致）。
    /// 用于在 **首个 Pass 仅 read** 时仍能插入 Transfer→Shader 等 barrier；与 DAG 边无关。
    void declare_buffer_prior_write(RgResourceId id,
                                    vk::PipelineStageFlags producer_stages,
                                    vk::AccessFlags producer_access);

    class PassBuilder;

    /// 追加空 Pass，返回链式构造器。
    [[nodiscard]] PassBuilder add_pass(std::string name);

    /// 构建依赖图、拓扑排序、调度 buffer barrier；若 `gpu_ctx != nullptr` 则继续按 Pass 上的
    /// `RgPass::gpu` + SPIR-V 反射创建/复用管线、描述符与呈现 framebuffer。
    /// 失败时清空编译结果并返回 `false`（例如环或 GPU 阶段失败）。
    [[nodiscard]] bool compile(RgGpuCompileContext *gpu_ctx = nullptr);

    [[nodiscard]] bool is_compiled() const noexcept { return compiled_; }

    /// 按编译顺序执行：先 `pipeline_barrier`（若有），再仅调用已 `set_execute` 的 Pass（无 lambda 的 Pass 跳过）。
    void execute(CommandBuffer &cmd) const;

    /// 与 `compile(&ctx)` 配套：`per_pass.size()` 须等于 `passes().size()`。无 `set_execute` 且含 `gpu` 的 Pass
    /// 使用 `per_pass[i]` 自动录制；有 lambda 时 **只** 调 lambda。失败返回 `false`。
    [[nodiscard]] bool
    execute(CommandBuffer &cmd, RgGpuCompileContext &ctx,
            const std::vector<std::optional<RgGpuExecuteFrame>> &per_pass) const;

    /// 清空 Pass 与编译缓存，并清除各 buffer 上的 `declare_buffer_prior_write`；
    /// 资源 id 与 `bind_buffer` 句柄保留，便于下一帧再声明 + 构图。
    void clear_passes();

    [[nodiscard]] const std::vector<RgPass> &passes() const { return passes_; }
    [[nodiscard]] const std::vector<std::uint32_t> &execution_order() const {
        return execution_order_;
    }

    /// 调试用：上一帧 `compile` 为各 Pass 记录的 buffer barrier 条数（按 `execution_order_` 下标）。
    [[nodiscard]] const std::vector<std::uint32_t> &
    debug_barrier_counts_per_pass() const {
        return debug_barrier_counts_;
    }

    /// 供 `compile(..., RgGpuCompileContext*)` 查询 `bind_buffer` 结果。
    [[nodiscard]] BufferHandle resource_rhi_buffer(RgResourceId id) const;
    [[nodiscard]] vk::Buffer resource_vk_buffer(RgResourceId id) const;
    [[nodiscard]] vk::DeviceSize
    resource_buffer_bound_size(RgResourceId id) const;

private:
    struct PriorWriteOutsideGraph {
        vk::PipelineStageFlags stages {};
        vk::AccessFlags access {};
    };

    struct ResourceDesc {
        RgResourceType type { RgResourceType::Buffer };
        vk::Buffer buffer {};
        vk::DeviceSize buffer_size { 0 }; // 0 → WholeSize
        BufferHandle rhi_buffer {};
        std::optional<PriorWriteOutsideGraph> prior_write_outside_graph {};
    };

    void build_edges_(std::vector<std::vector<std::uint32_t>> &adj) const;
    [[nodiscard]] bool topological_sort_(
        const std::vector<std::vector<std::uint32_t>> &adj,
        std::vector<std::uint32_t> &out_order) const;
    [[nodiscard]] bool validate_buffer_binding_and_first_read_() const;
    void schedule_buffer_barriers_();
    [[nodiscard]] bool rebuild_gpu_reflection_reads_();

    std::vector<ResourceDesc> resources_;
    std::vector<RgPass> passes_;
    std::vector<std::uint32_t> execution_order_;
    std::vector<std::vector<vk::BufferMemoryBarrier>> pass_barriers_;
    std::vector<vk::PipelineStageFlags> pass_barrier_src_stages_;
    std::vector<vk::PipelineStageFlags> pass_barrier_dst_stages_;
    std::vector<std::uint32_t> debug_barrier_counts_;
    bool compiled_ { false };
};

/// `add_pass("x").read(...).write(...).set_execute(...)` → 返回 `RenderGraph&` 以继续链式添加 Pass。
class RenderGraph::PassBuilder {
public:
    PassBuilder &read(RgResourceId id, vk::PipelineStageFlags stages,
                      vk::AccessFlags access);
    PassBuilder &write(RgResourceId id, vk::PipelineStageFlags stages,
                       vk::AccessFlags access);

    /// 等价于 `write(..., eTransfer, eTransferWrite)`。
    PassBuilder &write_transfer(RgResourceId id);

    RenderGraph &set_execute(std::function<void(CommandBuffer &)> fn);

    /// 须先于 `gpu_vertex_input` / `gpu_bind_*` 调用；SPIR-V 模块完整字节（含头）。
    PassBuilder &gpu_shaders(
        std::shared_ptr<const std::vector<std::byte>> vert_spv,
        std::shared_ptr<const std::vector<std::byte>> frag_spv);
    PassBuilder &
    gpu_vertex_input(std::vector<vk::VertexInputBindingDescription> bindings,
                     std::vector<vk::VertexInputAttributeDescription> attrs);
    /// 按反射 `resource_name` 绑定 UBO；`dynamic_stride > 0` 时 promote 为 dynamic UBO。
    PassBuilder &gpu_bind_uniform_buffer(std::string resource_name,
                                         RgResourceId buffer,
                                         vk::DeviceSize dynamic_stride = 0);
    /// 按反射 `resource_name` 绑定 SSBO（`dynamic_stride` 须为 0）。
    PassBuilder &gpu_bind_storage_buffer(std::string resource_name,
                                         RgResourceId buffer);
    /// 内置执行期 `draw`；`vertex_buffer` 须已 `bind_buffer`（可带 `BufferHandle` 以便校验）。
    RenderGraph &gpu_draw(RgResourceId vertex_buffer, std::uint32_t vertex_count,
                          std::uint32_t first_vertex = 0,
                          std::uint32_t instance_count = 1);
    /// 内置执行期 `drawIndexed`；与 `gpu_draw` 互斥（会清空非索引 draw 字段）。
    RenderGraph &
    gpu_draw_indexed(RgResourceId vertex_buffer, RgResourceId index_buffer,
                     std::uint32_t index_count, std::uint32_t first_index = 0,
                     std::int32_t vertex_offset = 0,
                     vk::IndexType index_type = vk::IndexType::eUint16,
                     std::uint32_t instance_count = 1);
    PassBuilder &gpu_clear_color(std::array<float, 4> rgba);

private:
    friend class RenderGraph;
    PassBuilder(RenderGraph *g, std::size_t pass_index) noexcept
        : graph_(g), pass_index_(pass_index) {}

    RenderGraph *graph_ {};
    std::size_t pass_index_ { 0 };
};

} // namespace rhi
