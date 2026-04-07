#include "rhi/render_graph.hpp"
#include "rhi/render_graph_gpu.hpp"
#include "rhi/shader_reflection.hpp"

#include "core/log/logger.hpp"

#include <algorithm>
#include <array>
#include <queue>
#include <span>
#include <string_view>

namespace rhi {

namespace {

[[nodiscard]] bool pass_writes(const RgPass &p, RgResourceId id) {
    for (const RgResourceUsage &u : p.writes) {
        if (u.resource == id) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool pass_reads(const RgPass &p, RgResourceId id) {
    for (const RgResourceUsage &u : p.reads) {
        if (u.resource == id) {
            return true;
        }
    }
    if (p.gpu.has_value()) {
        for (const RgResourceUsage &u : p.gpu->reflection_reads) {
            if (u.resource == id) {
                return true;
            }
        }
    }
    return false;
}

void add_edge_unique(std::vector<std::vector<std::uint32_t>> &adj,
                     std::uint32_t u, std::uint32_t v) {
    if (u == v) {
        return;
    }
    for (const std::uint32_t x : adj[u]) {
        if (x == v) {
            return;
        }
    }
    adj[u].push_back(v);
}

struct Agg {
    bool active { false };
    bool has_read { false };
    bool has_write { false };
    vk::PipelineStageFlags stages {};
    vk::AccessFlags acc {};
};

[[nodiscard]] bool needs_memory_barrier(bool last_init, bool last_was_write,
                                        const Agg &agg) {
    if (!last_init) {
        return false;
    }
    return last_was_write || agg.has_write;
}

[[nodiscard]] vk::PipelineStageFlags
shader_stages_to_pipeline_stages(const vk::ShaderStageFlags shader_stages) {
    vk::PipelineStageFlags out {};
    if ((shader_stages & vk::ShaderStageFlagBits::eVertex) !=
        vk::ShaderStageFlags {}) {
        out |= vk::PipelineStageFlagBits::eVertexShader;
    }
    if ((shader_stages & vk::ShaderStageFlagBits::eTessellationControl) !=
        vk::ShaderStageFlags {}) {
        out |= vk::PipelineStageFlagBits::eTessellationControlShader;
    }
    if ((shader_stages & vk::ShaderStageFlagBits::eTessellationEvaluation) !=
        vk::ShaderStageFlags {}) {
        out |= vk::PipelineStageFlagBits::eTessellationEvaluationShader;
    }
    if ((shader_stages & vk::ShaderStageFlagBits::eGeometry) !=
        vk::ShaderStageFlags {}) {
        out |= vk::PipelineStageFlagBits::eGeometryShader;
    }
    if ((shader_stages & vk::ShaderStageFlagBits::eFragment) !=
        vk::ShaderStageFlags {}) {
        out |= vk::PipelineStageFlagBits::eFragmentShader;
    }
    if ((shader_stages & vk::ShaderStageFlagBits::eCompute) !=
        vk::ShaderStageFlags {}) {
        out |= vk::PipelineStageFlagBits::eComputeShader;
    }
    if ((shader_stages & vk::ShaderStageFlagBits::eRaygenKHR) !=
        vk::ShaderStageFlags {}) {
        out |= vk::PipelineStageFlagBits::eRayTracingShaderKHR;
    }
    if ((shader_stages & vk::ShaderStageFlagBits::eAnyHitKHR) !=
        vk::ShaderStageFlags {}) {
        out |= vk::PipelineStageFlagBits::eRayTracingShaderKHR;
    }
    if ((shader_stages & vk::ShaderStageFlagBits::eClosestHitKHR) !=
        vk::ShaderStageFlags {}) {
        out |= vk::PipelineStageFlagBits::eRayTracingShaderKHR;
    }
    if ((shader_stages & vk::ShaderStageFlagBits::eMissKHR) !=
        vk::ShaderStageFlags {}) {
        out |= vk::PipelineStageFlagBits::eRayTracingShaderKHR;
    }
    if ((shader_stages & vk::ShaderStageFlagBits::eIntersectionKHR) !=
        vk::ShaderStageFlags {}) {
        out |= vk::PipelineStageFlagBits::eRayTracingShaderKHR;
    }
    if ((shader_stages & vk::ShaderStageFlagBits::eCallableKHR) !=
        vk::ShaderStageFlags {}) {
        out |= vk::PipelineStageFlagBits::eRayTracingShaderKHR;
    }
    if ((shader_stages & vk::ShaderStageFlagBits::eTaskEXT) !=
        vk::ShaderStageFlags {}) {
        out |= vk::PipelineStageFlagBits::eTaskShaderEXT;
    }
    if ((shader_stages & vk::ShaderStageFlagBits::eMeshEXT) !=
        vk::ShaderStageFlags {}) {
        out |= vk::PipelineStageFlagBits::eMeshShaderEXT;
    }
    return out;
}

[[nodiscard]] bool binding_matches_named_slot(const DescriptorBinding &b,
                                              const RgPassGpuNamedBufferSlot &s) {
    if (b.resource_name != s.resource_name) {
        return false;
    }
    if (s.kind == RgPassGpuNamedBufferKind::Uniform) {
        return b.descriptor_type == vk::DescriptorType::eUniformBuffer;
    }
    return b.descriptor_type == vk::DescriptorType::eStorageBuffer;
}

void accumulate_pass_usage(const RgPass &p, std::vector<Agg> &agg) {
    for (const RgResourceUsage &u : p.reads) {
        if (!u.resource.valid() || u.resource.value >= agg.size()) {
            continue;
        }
        Agg &a = agg[u.resource.value];
        a.active = true;
        a.has_read = true;
        a.stages |= u.stages;
        a.acc |= u.access_mask;
    }
    if (p.gpu.has_value()) {
        for (const RgResourceUsage &u : p.gpu->reflection_reads) {
            if (!u.resource.valid() || u.resource.value >= agg.size()) {
                continue;
            }
            Agg &a = agg[u.resource.value];
            a.active = true;
            a.has_read = true;
            a.stages |= u.stages;
            a.acc |= u.access_mask;
        }
    }
    for (const RgResourceUsage &u : p.writes) {
        if (!u.resource.valid() || u.resource.value >= agg.size()) {
            continue;
        }
        Agg &a = agg[u.resource.value];
        a.active = true;
        a.has_write = true;
        a.stages |= u.stages;
        a.acc |= u.access_mask;
    }
}

} // namespace

RgResourceId RenderGraph::create_buffer() {
    const std::uint32_t id = static_cast<std::uint32_t>(resources_.size());
    resources_.push_back(ResourceDesc { RgResourceType::Buffer, nullptr, 0,
                                         BufferHandle {}, std::nullopt });
    return RgResourceId { id };
}

RgResourceId RenderGraph::create_image() {
    const std::uint32_t id = static_cast<std::uint32_t>(resources_.size());
    resources_.push_back(ResourceDesc { RgResourceType::Image, nullptr, 0,
                                        BufferHandle {}, std::nullopt });
    return RgResourceId { id };
}

void RenderGraph::bind_buffer(const RgResourceId id, const vk::Buffer buffer,
                              const vk::DeviceSize size,
                              const BufferHandle rhi_buffer) {
    if (!id.valid() || id.value >= resources_.size()) {
        LUMEN_LOG_WARN("RenderGraph::bind_buffer: 无效资源 id");
        return;
    }
    ResourceDesc &d = resources_[id.value];
    if (d.type != RgResourceType::Buffer) {
        LUMEN_LOG_WARN("RenderGraph::bind_buffer: 资源非 Buffer");
        return;
    }
    d.buffer = buffer;
    d.buffer_size = size;
    d.rhi_buffer = rhi_buffer;
}

BufferHandle RenderGraph::resource_rhi_buffer(const RgResourceId id) const {
    if (!id.valid() || id.value >= resources_.size()) {
        return {};
    }
    return resources_[id.value].rhi_buffer;
}

vk::Buffer RenderGraph::resource_vk_buffer(const RgResourceId id) const {
    if (!id.valid() || id.value >= resources_.size()) {
        return nullptr;
    }
    return resources_[id.value].buffer;
}

vk::DeviceSize
RenderGraph::resource_buffer_bound_size(const RgResourceId id) const {
    if (!id.valid() || id.value >= resources_.size()) {
        return 0;
    }
    return resources_[id.value].buffer_size;
}

void RenderGraph::declare_buffer_prior_write(
    const RgResourceId id, const vk::PipelineStageFlags producer_stages,
    const vk::AccessFlags producer_access) {
    if (!id.valid() || id.value >= resources_.size()) {
        LUMEN_LOG_WARN("RenderGraph::declare_buffer_prior_write: 无效资源 id");
        return;
    }
    ResourceDesc &d = resources_[id.value];
    if (d.type != RgResourceType::Buffer) {
        LUMEN_LOG_WARN(
            "RenderGraph::declare_buffer_prior_write: 资源非 Buffer");
        return;
    }
    d.prior_write_outside_graph =
        PriorWriteOutsideGraph { producer_stages, producer_access };
}

RenderGraph::PassBuilder RenderGraph::add_pass(std::string name) {
    RgPass p {};
    p.name = std::move(name);
    passes_.push_back(std::move(p));
    return PassBuilder { this, passes_.size() - 1 };
}

RenderGraph::PassBuilder &
RenderGraph::PassBuilder::read(const RgResourceId id,
                               const vk::PipelineStageFlags stages,
                               const vk::AccessFlags access) {
    graph_->passes_[pass_index_].reads.push_back(
        RgResourceUsage { id, RgAccessKind::Read, stages, access });
    return *this;
}

RenderGraph::PassBuilder &
RenderGraph::PassBuilder::write(const RgResourceId id,
                                const vk::PipelineStageFlags stages,
                                const vk::AccessFlags access) {
    graph_->passes_[pass_index_].writes.push_back(
        RgResourceUsage { id, RgAccessKind::Write, stages, access });
    return *this;
}

RenderGraph &
RenderGraph::PassBuilder::set_execute(std::function<void(CommandBuffer &)> fn) {
    graph_->passes_[pass_index_].execute = std::move(fn);
    return *graph_;
}

RenderGraph::PassBuilder &
RenderGraph::PassBuilder::write_transfer(const RgResourceId id) {
    return write(id, vk::PipelineStageFlagBits::eTransfer,
                 vk::AccessFlagBits::eTransferWrite);
}

RenderGraph::PassBuilder &RenderGraph::PassBuilder::gpu_shaders(
    std::shared_ptr<const std::vector<std::byte>> vert_spv,
    std::shared_ptr<const std::vector<std::byte>> frag_spv) {
    graph_->passes_[pass_index_].gpu.emplace();
    graph_->passes_[pass_index_].gpu->vert_spv = std::move(vert_spv);
    graph_->passes_[pass_index_].gpu->frag_spv = std::move(frag_spv);
    return *this;
}

RenderGraph::PassBuilder &RenderGraph::PassBuilder::gpu_vertex_input(
    std::vector<vk::VertexInputBindingDescription> bindings,
    std::vector<vk::VertexInputAttributeDescription> attrs) {
    std::optional<RgPassGpuDesc> &g = graph_->passes_[pass_index_].gpu;
    if (!g.has_value()) {
        LUMEN_LOG_ERROR(
            "RenderGraph::gpu_vertex_input: 须先调用 gpu_shaders (Pass \"{}\")",
            graph_->passes_[pass_index_].name);
        return *this;
    }
    g->vertex_bindings = std::move(bindings);
    g->vertex_attributes = std::move(attrs);
    return *this;
}

RenderGraph::PassBuilder &
RenderGraph::PassBuilder::gpu_bind_uniform_buffer(std::string resource_name,
                                                  const RgResourceId buffer,
                                                  const vk::DeviceSize dynamic_stride) {
    std::optional<RgPassGpuDesc> &g = graph_->passes_[pass_index_].gpu;
    if (!g.has_value()) {
        LUMEN_LOG_ERROR(
            "RenderGraph::gpu_bind_uniform_buffer: 须先调用 gpu_shaders (Pass \"{}\")",
            graph_->passes_[pass_index_].name);
        return *this;
    }
    RgPassGpuNamedBufferSlot slot {};
    slot.resource_name = std::move(resource_name);
    slot.resource = buffer;
    slot.kind = RgPassGpuNamedBufferKind::Uniform;
    slot.dynamic_stride = dynamic_stride;
    g->named_buffer_slots.push_back(std::move(slot));
    return *this;
}

RenderGraph::PassBuilder &
RenderGraph::PassBuilder::gpu_bind_storage_buffer(std::string resource_name,
                                                  const RgResourceId buffer) {
    std::optional<RgPassGpuDesc> &g = graph_->passes_[pass_index_].gpu;
    if (!g.has_value()) {
        LUMEN_LOG_ERROR(
            "RenderGraph::gpu_bind_storage_buffer: 须先调用 gpu_shaders (Pass \"{}\")",
            graph_->passes_[pass_index_].name);
        return *this;
    }
    RgPassGpuNamedBufferSlot slot {};
    slot.resource_name = std::move(resource_name);
    slot.resource = buffer;
    slot.kind = RgPassGpuNamedBufferKind::Storage;
    slot.dynamic_stride = 0;
    g->named_buffer_slots.push_back(std::move(slot));
    return *this;
}

RenderGraph &
RenderGraph::PassBuilder::gpu_draw(const RgResourceId vertex_buffer,
                                   const std::uint32_t vertex_count,
                                   const std::uint32_t first_vertex,
                                   const std::uint32_t instance_count) {
    std::optional<RgPassGpuDesc> &g = graph_->passes_[pass_index_].gpu;
    if (!g.has_value()) {
        LUMEN_LOG_ERROR(
            "RenderGraph::gpu_draw: 须先调用 gpu_shaders (Pass \"{}\")",
            graph_->passes_[pass_index_].name);
        return *graph_;
    }
    g->draw_vertex_buffer = vertex_buffer;
    g->draw_vertex_count = vertex_count;
    g->draw_first_vertex = first_vertex;
    g->draw_instance_count = instance_count;
    g->draw_index_buffer = RgResourceId {};
    g->draw_index_count = 0;
    g->draw_first_index = 0;
    g->draw_index_vertex_offset = 0;
    g->draw_index_type = vk::IndexType::eUint16;
    return *graph_;
}

RenderGraph &
RenderGraph::PassBuilder::gpu_draw_indexed(const RgResourceId vertex_buffer,
                                           const RgResourceId index_buffer,
                                           const std::uint32_t index_count,
                                           const std::uint32_t first_index,
                                           const std::int32_t vertex_offset,
                                           const vk::IndexType index_type,
                                           const std::uint32_t instance_count) {
    std::optional<RgPassGpuDesc> &g = graph_->passes_[pass_index_].gpu;
    if (!g.has_value()) {
        LUMEN_LOG_ERROR(
            "RenderGraph::gpu_draw_indexed: 须先调用 gpu_shaders (Pass \"{}\")",
            graph_->passes_[pass_index_].name);
        return *graph_;
    }
    g->draw_vertex_buffer = vertex_buffer;
    g->draw_vertex_count = 0;
    g->draw_first_vertex = 0;
    g->draw_instance_count = instance_count;
    g->draw_index_buffer = index_buffer;
    g->draw_index_count = index_count;
    g->draw_first_index = first_index;
    g->draw_index_vertex_offset = vertex_offset;
    g->draw_index_type = index_type;
    return *graph_;
}

RenderGraph::PassBuilder &
RenderGraph::PassBuilder::gpu_clear_color(const std::array<float, 4> rgba) {
    std::optional<RgPassGpuDesc> &g = graph_->passes_[pass_index_].gpu;
    if (!g.has_value()) {
        LUMEN_LOG_ERROR(
            "RenderGraph::gpu_clear_color: 须先调用 gpu_shaders (Pass \"{}\")",
            graph_->passes_[pass_index_].name);
        return *this;
    }
    g->color_attachment_clear = rgba;
    return *this;
}

void RenderGraph::clear_passes() {
    passes_.clear();
    compiled_ = false;
    execution_order_.clear();
    pass_barriers_.clear();
    pass_barrier_src_stages_.clear();
    pass_barrier_dst_stages_.clear();
    debug_barrier_counts_.clear();
    for (ResourceDesc &d : resources_) {
        if (d.type == RgResourceType::Buffer) {
            d.prior_write_outside_graph.reset();
        }
    }
}

void RenderGraph::build_edges_(
    std::vector<std::vector<std::uint32_t>> &adj) const {
    const std::uint32_t n = static_cast<std::uint32_t>(passes_.size());
    adj.assign(n, {});

    const auto rid_max = static_cast<std::uint32_t>(resources_.size());
    for (std::uint32_t rid = 0; rid < rid_max; ++rid) {
        const RgResourceId id { rid };
        std::vector<std::uint32_t> writers;
        writers.reserve(n);
        std::vector<std::uint32_t> readers;
        readers.reserve(n);
        for (std::uint32_t p = 0; p < n; ++p) {
            if (pass_writes(passes_[p], id)) {
                writers.push_back(p);
            }
            if (pass_reads(passes_[p], id)) {
                readers.push_back(p);
            }
        }
        for (const std::uint32_t w : writers) {
            for (const std::uint32_t l : readers) {
                add_edge_unique(adj, w, l);
            }
        }
        std::sort(writers.begin(), writers.end());
        for (std::size_t i = 1; i < writers.size(); ++i) {
            add_edge_unique(adj, writers[i - 1], writers[i]);
        }
    }
}

bool RenderGraph::topological_sort_(
    const std::vector<std::vector<std::uint32_t>> &adj,
    std::vector<std::uint32_t> &out_order) const {
    const std::uint32_t n = static_cast<std::uint32_t>(passes_.size());
    std::vector<std::uint32_t> indegree(n, 0);
    for (std::uint32_t u = 0; u < n; ++u) {
        for (const std::uint32_t v : adj[u]) {
            ++indegree[v];
        }
    }
    std::queue<std::uint32_t> q;
    for (std::uint32_t i = 0; i < n; ++i) {
        if (indegree[i] == 0) {
            q.push(i);
        }
    }
    out_order.clear();
    out_order.reserve(n);
    while (!q.empty()) {
        const std::uint32_t u = q.front();
        q.pop();
        out_order.push_back(u);
        for (const std::uint32_t v : adj[u]) {
            if (--indegree[v] == 0) {
                q.push(v);
            }
        }
    }
    if (out_order.size() != n) {
        out_order.clear();
        return false;
    }
    return true;
}

bool RenderGraph::validate_buffer_binding_and_first_read_() const {
    for (const RgPass &p : passes_) {
        const auto check_usage = [&](const RgResourceUsage &u) {
            if (!u.resource.valid() || u.resource.value >= resources_.size()) {
                return true;
            }
            const ResourceDesc &d = resources_[u.resource.value];
            if (d.type != RgResourceType::Buffer) {
                return true;
            }
            if (!d.buffer) {
                LUMEN_LOG_ERROR(
                    "RenderGraph::compile: Pass \"{}\" 引用 buffer 资源但未 "
                    "`bind_buffer`",
                    p.name);
                return false;
            }
            return true;
        };
        for (const RgResourceUsage &u : p.reads) {
            if (!check_usage(u)) {
                return false;
            }
        }
        if (p.gpu.has_value()) {
            for (const RgResourceUsage &u : p.gpu->reflection_reads) {
                if (!check_usage(u)) {
                    return false;
                }
            }
        }
        for (const RgResourceUsage &u : p.writes) {
            if (!check_usage(u)) {
                return false;
            }
        }
    }

    std::vector<bool> written(resources_.size(), false);
    for (const std::uint32_t pi : execution_order_) {
        const RgPass &p = passes_[pi];
        std::vector<Agg> agg(resources_.size());
        accumulate_pass_usage(p, agg);
        for (std::uint32_t rid = 0; rid < resources_.size(); ++rid) {
            const Agg &a = agg[rid];
            if (!a.active) {
                continue;
            }
            const ResourceDesc &d = resources_[rid];
            if (d.type != RgResourceType::Buffer) {
                if (a.has_write) {
                    written[rid] = true;
                }
                continue;
            }
            const bool read_only_in_pass = a.has_read && !a.has_write;
            if (read_only_in_pass && !written[rid] &&
                !d.prior_write_outside_graph.has_value()) {
                LUMEN_LOG_ERROR(
                    "RenderGraph::compile: Pass \"{}\" 对 buffer 资源首次使用为 "
                    "read，且图中无先行 write、亦未 `declare_buffer_prior_write`",
                    p.name);
                return false;
            }
            if (a.has_write) {
                written[rid] = true;
            }
        }
    }
    return true;
}

void RenderGraph::schedule_buffer_barriers_() {
    const std::uint32_t n = static_cast<std::uint32_t>(passes_.size());
    pass_barriers_.assign(n, {});
    pass_barrier_src_stages_.assign(n, {});
    pass_barrier_dst_stages_.assign(n, {});
    debug_barrier_counts_.assign(n, 0);

    struct LastState {
        bool initialized { false };
        bool last_was_write { false };
        vk::PipelineStageFlags stages {};
        vk::AccessFlags access {};
    };
    std::vector<LastState> last(resources_.size());
    for (std::uint32_t rid = 0; rid < resources_.size(); ++rid) {
        const ResourceDesc &d = resources_[rid];
        if (d.type != RgResourceType::Buffer || !d.prior_write_outside_graph) {
            continue;
        }
        LastState &st = last[rid];
        st.initialized = true;
        st.last_was_write = true;
        st.stages = d.prior_write_outside_graph->stages;
        st.access = d.prior_write_outside_graph->access;
    }

    for (const std::uint32_t pi : execution_order_) {
        const RgPass &p = passes_[pi];
        std::vector<Agg> agg(resources_.size());
        accumulate_pass_usage(p, agg);

        vk::PipelineStageFlags batch_src {};
        vk::PipelineStageFlags batch_dst {};
        std::vector<vk::BufferMemoryBarrier> &bars = pass_barriers_[pi];

        for (std::uint32_t rid = 0; rid < resources_.size(); ++rid) {
            const Agg &a = agg[rid];
            if (!a.active) {
                continue;
            }
            LastState &st = last[rid];
            if (needs_memory_barrier(st.initialized, st.last_was_write, a)) {
                const ResourceDesc &desc = resources_[rid];
                if (desc.type == RgResourceType::Buffer && desc.buffer) {
                    vk::BufferMemoryBarrier b {};
                    b.srcAccessMask = st.access;
                    b.dstAccessMask = a.acc;
                    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    b.buffer = desc.buffer;
                    b.offset = 0;
                    b.size = desc.buffer_size == 0 ? vk::WholeSize : desc.buffer_size;
                    bars.push_back(b);
                    batch_src |= st.stages;
                    batch_dst |= a.stages;
                }
            }
            st.initialized = true;
            st.last_was_write = a.has_write;
            st.stages = a.stages;
            st.access = a.acc;
        }

        pass_barrier_src_stages_[pi] = batch_src;
        pass_barrier_dst_stages_[pi] = batch_dst;
        debug_barrier_counts_[pi] = static_cast<std::uint32_t>(bars.size());
    }
}

bool RenderGraph::rebuild_gpu_reflection_reads_() {
    for (RgPass &p : passes_) {
        if (!p.gpu.has_value()) {
            continue;
        }
        RgPassGpuDesc &gpu = *p.gpu;
        gpu.reflection_reads.clear();

        if (gpu.vert_spv == nullptr || gpu.frag_spv == nullptr ||
            gpu.vert_spv->empty() || gpu.frag_spv->empty() ||
            (gpu.vert_spv->size() % 4) != 0 ||
            (gpu.frag_spv->size() % 4) != 0) {
            LUMEN_LOG_ERROR(
                "RenderGraph::compile: Pass \"{}\" GPU SPIR-V 无效（无法构建 reflection_reads）",
                p.name);
            return false;
        }

        const std::span<const std::uint32_t> vert_words {
            reinterpret_cast<const std::uint32_t *>(gpu.vert_spv->data()),
            gpu.vert_spv->size() / 4
        };
        const std::span<const std::uint32_t> frag_words {
            reinterpret_cast<const std::uint32_t *>(gpu.frag_spv->data()),
            gpu.frag_spv->size() / 4
        };

        const std::optional<ShaderReflection> refl_v =
            reflect_spirv(vert_words, vk::ShaderStageFlagBits::eVertex);
        const std::optional<ShaderReflection> refl_f =
            reflect_spirv(frag_words, vk::ShaderStageFlagBits::eFragment);
        if (!refl_v || !refl_f) {
            LUMEN_LOG_ERROR(
                "RenderGraph::compile: Pass \"{}\" 着色器反射失败（reflection_reads）",
                p.name);
            return false;
        }
        ShaderReflection merged {};
        if (!merge_vert_frag_reflection(*refl_v, *refl_f, merged)) {
            LUMEN_LOG_ERROR(
                "RenderGraph::compile: Pass \"{}\" 合并 VS/FS 反射失败（reflection_reads）",
                p.name);
            return false;
        }

        const std::size_t n_slots = gpu.named_buffer_slots.size();
        for (std::size_t i = 0; i < n_slots; ++i) {
            for (std::size_t j = i + 1; j < n_slots; ++j) {
                if (gpu.named_buffer_slots[i].resource_name ==
                    gpu.named_buffer_slots[j].resource_name) {
                    LUMEN_LOG_ERROR(
                        "RenderGraph::compile: Pass \"{}\" 重复的 resource_name \"{}\"",
                        p.name, gpu.named_buffer_slots[i].resource_name);
                    return false;
                }
            }
        }

        for (const RgPassGpuNamedBufferSlot &slot : gpu.named_buffer_slots) {
            if (slot.resource_name.empty()) {
                LUMEN_LOG_ERROR(
                    "RenderGraph::compile: Pass \"{}\" gpu_bind_* 的 resource_name 为空",
                    p.name);
                return false;
            }
            if (slot.kind == RgPassGpuNamedBufferKind::Storage &&
                slot.dynamic_stride != 0) {
                LUMEN_LOG_ERROR(
                    "RenderGraph::compile: Pass \"{}\" SSBO \"{}\" 不应设置 dynamic_stride",
                    p.name, slot.resource_name);
                return false;
            }
            const DescriptorBinding *match = nullptr;
            for (const DescriptorBinding &b : merged.bindings) {
                if (!binding_matches_named_slot(b, slot)) {
                    continue;
                }
                if (match != nullptr) {
                    LUMEN_LOG_ERROR(
                        "RenderGraph::compile: Pass \"{}\" resource_name \"{}\" 在反射中匹配多条绑定",
                        p.name, slot.resource_name);
                    return false;
                }
                match = &b;
            }
            if (match == nullptr) {
                LUMEN_LOG_ERROR(
                    "RenderGraph::compile: Pass \"{}\" 未找到与 gpu_bind 匹配的着色器资源 \"{}\"",
                    p.name, slot.resource_name);
                return false;
            }
            if (match->resource_name.empty()) {
                LUMEN_LOG_ERROR(
                    "RenderGraph::compile: Pass \"{}\" set={} binding={} 缺少 resource_name，无法按名绑定",
                    p.name, match->set, match->binding);
                return false;
            }
            const vk::AccessFlags acc =
                slot.kind == RgPassGpuNamedBufferKind::Uniform
                    ? vk::AccessFlagBits::eUniformRead
                    : vk::AccessFlagBits::eShaderRead;
            gpu.reflection_reads.push_back(RgResourceUsage {
                slot.resource, RgAccessKind::Read,
                shader_stages_to_pipeline_stages(match->stages), acc });
        }

        for (const DescriptorBinding &b : merged.bindings) {
            if (b.descriptor_type != vk::DescriptorType::eUniformBuffer &&
                b.descriptor_type != vk::DescriptorType::eStorageBuffer) {
                continue;
            }
            bool covered = false;
            for (const RgPassGpuNamedBufferSlot &slot : gpu.named_buffer_slots) {
                if (binding_matches_named_slot(b, slot)) {
                    covered = true;
                    break;
                }
            }
            if (!covered) {
                LUMEN_LOG_ERROR(
                    "RenderGraph::compile: Pass \"{}\" 着色器缓冲 \"{}\" (set={} binding={}) 缺少 "
                    "gpu_bind_uniform_buffer / gpu_bind_storage_buffer",
                    p.name, b.resource_name, b.set, b.binding);
                return false;
            }
        }
    }
    return true;
}

bool RenderGraph::compile(RgGpuCompileContext *gpu_ctx) {
    compiled_ = false;
    execution_order_.clear();
    pass_barriers_.clear();
    pass_barrier_src_stages_.clear();
    pass_barrier_dst_stages_.clear();
    debug_barrier_counts_.clear();

    if (passes_.empty()) {
        LUMEN_LOG_WARN("RenderGraph::compile: 无 Pass");
        compiled_ = true;
        if (gpu_ctx != nullptr &&
            !compile_render_graph_gpu_phase(*this, *gpu_ctx)) {
            compiled_ = false;
            return false;
        }
        return true;
    }

    for (const RgPass &p : passes_) {
        for (const RgResourceUsage &u : p.reads) {
            if (!u.resource.valid() || u.resource.value >= resources_.size()) {
                LUMEN_LOG_ERROR("RenderGraph::compile: Pass \"{}\" read 无效资源",
                                p.name);
                return false;
            }
        }
        for (const RgResourceUsage &u : p.writes) {
            if (!u.resource.valid() || u.resource.value >= resources_.size()) {
                LUMEN_LOG_ERROR("RenderGraph::compile: Pass \"{}\" write 无效资源",
                                p.name);
                return false;
            }
        }
    }

    if (!rebuild_gpu_reflection_reads_()) {
        return false;
    }

    std::vector<std::vector<std::uint32_t>> adj;
    build_edges_(adj);

    if (!topological_sort_(adj, execution_order_)) {
        LUMEN_LOG_ERROR("RenderGraph::compile: 检测到环或拓扑失败");
        execution_order_.clear();
        return false;
    }

    if (!validate_buffer_binding_and_first_read_()) {
        execution_order_.clear();
        return false;
    }

    schedule_buffer_barriers_();
    compiled_ = true;
    if (gpu_ctx != nullptr &&
        !compile_render_graph_gpu_phase(*this, *gpu_ctx)) {
        compiled_ = false;
        execution_order_.clear();
        pass_barriers_.clear();
        pass_barrier_src_stages_.clear();
        pass_barrier_dst_stages_.clear();
        debug_barrier_counts_.clear();
        return false;
    }
    return true;
}

void RenderGraph::execute(CommandBuffer &cmd) const {
    if (!compiled_) {
        LUMEN_LOG_WARN("RenderGraph::execute: 未 compile");
        return;
    }
    for (const std::uint32_t pi : execution_order_) {
        const std::vector<vk::BufferMemoryBarrier> &bars = pass_barriers_[pi];
        if (!bars.empty()) {
            cmd.pipeline_barrier(pass_barrier_src_stages_[pi],
                                 pass_barrier_dst_stages_[pi], {}, nullptr,
                                 bars, nullptr);
        }
        const RgPass &p = passes_[pi];
        if (p.execute) {
            p.execute(cmd);
        }
    }
}

bool RenderGraph::execute(
    CommandBuffer &cmd, RgGpuCompileContext &ctx,
    const std::vector<std::optional<RgGpuExecuteFrame>> &per_pass) const {
    if (!compiled_) {
        LUMEN_LOG_WARN("RenderGraph::execute: 未 compile");
        return false;
    }
    if (per_pass.size() != passes_.size()) {
        LUMEN_LOG_ERROR(
            "RenderGraph::execute: per_pass.size() ({}) != passes().size() ({})",
            per_pass.size(), passes_.size());
        return false;
    }
    for (const std::uint32_t pi : execution_order_) {
        const std::vector<vk::BufferMemoryBarrier> &bars = pass_barriers_[pi];
        if (!bars.empty()) {
            cmd.pipeline_barrier(pass_barrier_src_stages_[pi],
                                 pass_barrier_dst_stages_[pi], {}, nullptr,
                                 bars, nullptr);
        }
        const RgPass &p = passes_[pi];
        if (p.execute) {
            p.execute(cmd);
            continue;
        }
        if (p.gpu.has_value()) {
            if (pi >= ctx.compiled.size() || !ctx.compiled[pi].has_value()) {
                LUMEN_LOG_ERROR(
                    "RenderGraph::execute: Pass \"{}\" GPU 未编译 (index {})",
                    p.name, pi);
                return false;
            }
            if (!per_pass[pi].has_value()) {
                LUMEN_LOG_ERROR(
                    "RenderGraph::execute: Pass \"{}\" 缺少 RgGpuExecuteFrame",
                    p.name);
                return false;
            }
            if (!rg_record_builtin_present_graphics_pass(
                    *this, pi, cmd, *ctx.compiled[pi], *per_pass[pi])) {
                return false;
            }
            continue;
        }
        LUMEN_LOG_ERROR(
            "RenderGraph::execute: Pass \"{}\" 无 set_execute 且无 gpu 描述",
            p.name);
        return false;
    }
    return true;
}

} // namespace rhi
