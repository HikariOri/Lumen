#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "rhi/render_graph.hpp"
#include "rhi/vulkan.hpp"

#include <cstdint>
#include <unordered_map>

namespace {

[[nodiscard]] std::unordered_map<std::string, std::uint32_t>
pass_index_by_name(const rhi::RenderGraph &g) {
    std::unordered_map<std::string, std::uint32_t> m;
    const auto &ps = g.passes();
    for (std::uint32_t i = 0; i < ps.size(); ++i) {
        m[ps[i].name] = i;
    }
    return m;
}

[[nodiscard]] std::uint32_t
position_in_order(const std::vector<std::uint32_t> &order,
                  std::uint32_t pass_index) {
    for (std::uint32_t i = 0; i < order.size(); ++i) {
        if (order[i] == pass_index) {
            return i;
        }
    }
    return static_cast<std::uint32_t>(-1);
}

} // namespace

TEST_CASE("RenderGraph: out-of-order add_pass still orders gbuffer before lighting") {
    rhi::RenderGraph g;
    const rhi::RgResourceId color = g.create_buffer();
    const rhi::RgResourceId depth = g.create_buffer();
    const rhi::RgResourceId output = g.create_buffer();

    g.bind_buffer(color,
                  vk::Buffer { reinterpret_cast<VkBuffer>(std::uintptr_t { 0x1000 }) },
                  vk::DeviceSize { 0 });
    g.bind_buffer(depth,
                  vk::Buffer { reinterpret_cast<VkBuffer>(std::uintptr_t { 0x2000 }) },
                  vk::DeviceSize { 0 });
    g.bind_buffer(output,
                  vk::Buffer { reinterpret_cast<VkBuffer>(std::uintptr_t { 0x3000 }) },
                  vk::DeviceSize { 0 });

    const vk::PipelineStageFlags frag = vk::PipelineStageFlagBits::eFragmentShader;
    const vk::AccessFlags col_write = vk::AccessFlagBits::eColorAttachmentWrite;
    const vk::AccessFlags depth_write =
        vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    const vk::AccessFlags shader_read = vk::AccessFlagBits::eShaderRead;

    // 故意先 lighting / present，再 gbuffer
    g.add_pass("lighting")
        .read(color, frag, shader_read)
        .read(depth, frag, shader_read)
        .write(output, frag, col_write)
        .set_execute([](rhi::CommandBuffer &) {});

    g.add_pass("present")
        .read(output, frag, shader_read)
        .set_execute([](rhi::CommandBuffer &) {});

    g.add_pass("gbuffer")
        .write(color, frag, col_write)
        .write(depth, frag, depth_write)
        .set_execute([](rhi::CommandBuffer &) {});

    REQUIRE(g.compile());
    const auto names = pass_index_by_name(g);
    const std::uint32_t ig = names.at("gbuffer");
    const std::uint32_t il = names.at("lighting");
    const std::uint32_t ip = names.at("present");
    const auto &ord = g.execution_order();
    const std::uint32_t pg = position_in_order(ord, ig);
    const std::uint32_t pl = position_in_order(ord, il);
    const std::uint32_t pp = position_in_order(ord, ip);
    CHECK(pg < pl);
    CHECK(pl < pp);
}

TEST_CASE("RenderGraph: compile fails on dependency cycle") {
    rhi::RenderGraph g;
    const rhi::RgResourceId r1 = g.create_buffer();
    const rhi::RgResourceId r2 = g.create_buffer();
    const vk::PipelineStageFlags cs = vk::PipelineStageFlagBits::eComputeShader;
    const vk::AccessFlags rw = vk::AccessFlagBits::eShaderRead |
                               vk::AccessFlagBits::eShaderWrite;

    g.add_pass("A")
        .read(r2, cs, rw)
        .write(r1, cs, rw)
        .set_execute([](rhi::CommandBuffer &) {});

    g.add_pass("B")
        .read(r1, cs, rw)
        .write(r2, cs, rw)
        .set_execute([](rhi::CommandBuffer &) {});

    CHECK_FALSE(g.compile());
    CHECK(g.execution_order().empty());
}

TEST_CASE("RenderGraph: buffer bind schedules at least one barrier between writer and reader") {
    rhi::RenderGraph g;
    const rhi::RgResourceId buf = g.create_buffer();
    const VkBuffer fake_handle = reinterpret_cast<VkBuffer>(std::uintptr_t { 4096 });
    g.bind_buffer(buf, vk::Buffer { fake_handle }, vk::DeviceSize { 256 });

    const vk::PipelineStageFlags f = vk::PipelineStageFlagBits::eFragmentShader;
    const vk::AccessFlags sr = vk::AccessFlagBits::eShaderRead;

    g.add_pass("upload").write_transfer(buf).set_execute(
        [](rhi::CommandBuffer &) {});

    g.add_pass("draw")
        .read(buf, f, sr)
        .set_execute([](rhi::CommandBuffer &) {});

    REQUIRE(g.compile());
    std::uint32_t total = 0;
    for (const std::uint32_t c : g.debug_barrier_counts_per_pass()) {
        total += c;
    }
    CHECK_GE(total, 1u);
}

TEST_CASE("RenderGraph: declare_buffer_prior_write enables first-pass read barrier") {
    rhi::RenderGraph g;
    const rhi::RgResourceId buf = g.create_buffer();
    const VkBuffer fake_handle =
        reinterpret_cast<VkBuffer>(std::uintptr_t { 0x4000 });
    g.bind_buffer(buf, vk::Buffer { fake_handle }, vk::DeviceSize { 256 });
    g.declare_buffer_prior_write(buf, vk::PipelineStageFlagBits::eTransfer,
                                 vk::AccessFlagBits::eTransferWrite);

    g.add_pass("draw")
        .read(buf, vk::PipelineStageFlagBits::eVertexShader,
              vk::AccessFlagBits::eUniformRead)
        .set_execute([](rhi::CommandBuffer &) {});

    REQUIRE(g.compile());
    std::uint32_t total = 0;
    for (const std::uint32_t c : g.debug_barrier_counts_per_pass()) {
        total += c;
    }
    CHECK_GE(total, 1u);
}

TEST_CASE("RenderGraph: compile fails when first buffer use is read without producer") {
    rhi::RenderGraph g;
    const rhi::RgResourceId buf = g.create_buffer();
    const VkBuffer fake_handle =
        reinterpret_cast<VkBuffer>(std::uintptr_t { 0x5000 });
    g.bind_buffer(buf, vk::Buffer { fake_handle }, vk::DeviceSize { 256 });

    g.add_pass("draw")
        .read(buf, vk::PipelineStageFlagBits::eVertexShader,
              vk::AccessFlagBits::eUniformRead)
        .set_execute([](rhi::CommandBuffer &) {});

    CHECK_FALSE(g.compile());
}
