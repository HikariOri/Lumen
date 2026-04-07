#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "rhi/shader_reflection.hpp"

#include <ghc/filesystem.hpp>

#include <cstdint>
#include <fstream>
#include <span>
#include <vector>

namespace fs = ghc::filesystem;

namespace {

[[nodiscard]] std::vector<std::byte> read_spv_file(const fs::path &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return {};
    }
    const auto sz = static_cast<std::size_t>(f.tellg());
    f.seekg(0);
    std::vector<std::byte> out(sz);
    if (sz > 0) {
        f.read(reinterpret_cast<char *>(out.data()),
               static_cast<std::streamsize>(sz));
    }
    if (!f) {
        return {};
    }
    return out;
}

} // namespace

#ifndef LUMEN_TEST_TRIANGLE_SHADERS_DIR
#error "LUMEN_TEST_TRIANGLE_SHADERS_DIR must be set by CMake for test_shader_reflection"
#endif

TEST_CASE("reflect triangle.vert.spv: set0 binding0 UBO vertex stage") {
    const fs::path vert_path =
        fs::path(LUMEN_TEST_TRIANGLE_SHADERS_DIR) / "triangle.vert.spv";
    const std::vector<std::byte> bytes = read_spv_file(vert_path);
    REQUIRE_MESSAGE(!bytes.empty(),
                    "无法读取 triangle.vert.spv，路径: " << vert_path.generic_string());
    REQUIRE((bytes.size() % 4) == 0);

    const std::span<const std::uint32_t> words {
        reinterpret_cast<const std::uint32_t *>(bytes.data()),
        bytes.size() / 4
    };
    const std::optional<rhi::ShaderReflection> refl =
        rhi::reflect_spirv(words, vk::ShaderStageFlagBits::eVertex);
    REQUIRE(refl.has_value());
    REQUIRE_GE(refl->bindings.size(), 1u);

    const rhi::DescriptorBinding *ubo = nullptr;
    for (const rhi::DescriptorBinding &b : refl->bindings) {
        if (b.set == 0 && b.binding == 0 &&
            b.descriptor_type == vk::DescriptorType::eUniformBuffer) {
            ubo = &b;
            break;
        }
    }
    REQUIRE(ubo != nullptr);
    CHECK(ubo->resource_name == "ubo");
    CHECK(ubo->descriptor_count == 1);
    CHECK(ubo->uniform_buffer_mode == rhi::UniformBufferBindingMode::Static);
    CHECK((ubo->stages & vk::ShaderStageFlagBits::eVertex) ==
          vk::ShaderStageFlagBits::eVertex);
}

TEST_CASE("reflect_vertex_input_interleaved: triangle.vert.spv stride 20") {
    const fs::path vert_path =
        fs::path(LUMEN_TEST_TRIANGLE_SHADERS_DIR) / "triangle.vert.spv";
    const std::vector<std::byte> bytes = read_spv_file(vert_path);
    REQUIRE_MESSAGE(!bytes.empty(),
                    "无法读取 triangle.vert.spv，路径: " << vert_path.generic_string());
    REQUIRE((bytes.size() % 4) == 0);

    const std::span<const std::uint32_t> words {
        reinterpret_cast<const std::uint32_t *>(bytes.data()),
        bytes.size() / 4
    };
    const std::optional<rhi::ReflectedVertexInput> vi =
        rhi::reflect_vertex_input_interleaved(words);
    REQUIRE(vi.has_value());
    REQUIRE(vi->bindings.size() == 1u);
    CHECK(vi->bindings[0].binding == 0u);
    CHECK(vi->bindings[0].inputRate == vk::VertexInputRate::eVertex);
    CHECK(vi->bindings[0].stride == 20u);

    REQUIRE(vi->attributes.size() == 2u);
    CHECK(vi->attributes[0].location == 0u);
    CHECK(vi->attributes[0].binding == 0u);
    CHECK(vi->attributes[0].format == vk::Format::eR32G32Sfloat);
    CHECK(vi->attributes[0].offset == 0u);
    CHECK(vi->attributes[1].location == 1u);
    CHECK(vi->attributes[1].binding == 0u);
    CHECK(vi->attributes[1].format == vk::Format::eR32G32B32Sfloat);
    CHECK(vi->attributes[1].offset == 8u);
}

TEST_CASE("merge_vert_frag triangle: UBO stages vertex only") {
    const fs::path shader_dir = fs::path(LUMEN_TEST_TRIANGLE_SHADERS_DIR);
    const std::vector<std::byte> vert_bytes = read_spv_file(shader_dir / "triangle.vert.spv");
    const std::vector<std::byte> frag_bytes = read_spv_file(shader_dir / "triangle.frag.spv");
    REQUIRE(!vert_bytes.empty());
    REQUIRE(!frag_bytes.empty());

    const std::span<const std::uint32_t> vw {
        reinterpret_cast<const std::uint32_t *>(vert_bytes.data()),
        vert_bytes.size() / 4
    };
    const std::span<const std::uint32_t> fw {
        reinterpret_cast<const std::uint32_t *>(frag_bytes.data()),
        frag_bytes.size() / 4
    };
    const auto rv = rhi::reflect_spirv(vw, vk::ShaderStageFlagBits::eVertex);
    const auto rf = rhi::reflect_spirv(fw, vk::ShaderStageFlagBits::eFragment);
    REQUIRE(rv.has_value());
    REQUIRE(rf.has_value());

    rhi::ShaderReflection merged {};
    REQUIRE(rhi::merge_vert_frag_reflection(*rv, *rf, merged));

    const rhi::DescriptorBinding *ubo = nullptr;
    for (const rhi::DescriptorBinding &b : merged.bindings) {
        if (b.set == 0 && b.binding == 0) {
            ubo = &b;
            break;
        }
    }
    REQUIRE(ubo != nullptr);
    CHECK(ubo->resource_name == "ubo");
    CHECK(ubo->descriptor_type == vk::DescriptorType::eUniformBuffer);
    CHECK(ubo->uniform_buffer_mode == rhi::UniformBufferBindingMode::Static);
    CHECK((ubo->stages & vk::ShaderStageFlagBits::eVertex) ==
          vk::ShaderStageFlagBits::eVertex);
    CHECK((ubo->stages & vk::ShaderStageFlagBits::eFragment) ==
          vk::ShaderStageFlags {});
}

TEST_CASE("merge_vert_frag + promote_uniform_binding_to_dynamic_by_name: dynamic") {
    const fs::path shader_dir = fs::path(LUMEN_TEST_TRIANGLE_SHADERS_DIR);
    const std::vector<std::byte> vert_bytes = read_spv_file(shader_dir / "triangle.vert.spv");
    const std::vector<std::byte> frag_bytes = read_spv_file(shader_dir / "triangle.frag.spv");
    REQUIRE(!vert_bytes.empty());
    REQUIRE(!frag_bytes.empty());

    const std::span<const std::uint32_t> vw {
        reinterpret_cast<const std::uint32_t *>(vert_bytes.data()),
        vert_bytes.size() / 4
    };
    const std::span<const std::uint32_t> fw {
        reinterpret_cast<const std::uint32_t *>(frag_bytes.data()),
        frag_bytes.size() / 4
    };
    const auto rv = rhi::reflect_spirv(vw, vk::ShaderStageFlagBits::eVertex);
    const auto rf = rhi::reflect_spirv(fw, vk::ShaderStageFlagBits::eFragment);
    REQUIRE(rv.has_value());
    REQUIRE(rf.has_value());

    rhi::ShaderReflection merged {};
    REQUIRE(rhi::merge_vert_frag_reflection(*rv, *rf, merged));
    REQUIRE(rhi::promote_uniform_binding_to_dynamic_by_name(merged, "ubo"));

    const rhi::DescriptorBinding *ubo = nullptr;
    for (const rhi::DescriptorBinding &b : merged.bindings) {
        if (b.set == 0 && b.binding == 0) {
            ubo = &b;
            break;
        }
    }
    REQUIRE(ubo != nullptr);
    CHECK(ubo->resource_name == "ubo");
    CHECK(ubo->descriptor_type == vk::DescriptorType::eUniformBufferDynamic);
    CHECK(ubo->uniform_buffer_mode == rhi::UniformBufferBindingMode::Dynamic);
}

TEST_CASE("descriptor_pool_plan_from_reflection: triangle merge+promote") {
    const fs::path shader_dir = fs::path(LUMEN_TEST_TRIANGLE_SHADERS_DIR);
    const std::vector<std::byte> vert_bytes = read_spv_file(shader_dir / "triangle.vert.spv");
    const std::vector<std::byte> frag_bytes = read_spv_file(shader_dir / "triangle.frag.spv");
    REQUIRE(!vert_bytes.empty());
    REQUIRE(!frag_bytes.empty());

    const std::span<const std::uint32_t> vw {
        reinterpret_cast<const std::uint32_t *>(vert_bytes.data()),
        vert_bytes.size() / 4
    };
    const std::span<const std::uint32_t> fw {
        reinterpret_cast<const std::uint32_t *>(frag_bytes.data()),
        frag_bytes.size() / 4
    };
    const auto rv = rhi::reflect_spirv(vw, vk::ShaderStageFlagBits::eVertex);
    const auto rf = rhi::reflect_spirv(fw, vk::ShaderStageFlagBits::eFragment);
    REQUIRE(rv.has_value());
    REQUIRE(rf.has_value());

    rhi::ShaderReflection merged {};
    REQUIRE(rhi::merge_vert_frag_reflection(*rv, *rf, merged));
    REQUIRE(rhi::promote_uniform_binding_to_dynamic_by_name(merged, "ubo"));

    const std::optional<rhi::DescriptorPoolPlan> p1 =
        rhi::descriptor_pool_plan_from_reflection(merged, 1u);
    REQUIRE(p1.has_value());
    CHECK(p1->max_sets == 1u);
    REQUIRE(p1->pool_sizes.size() == 1u);
    CHECK(p1->pool_sizes[0].type == vk::DescriptorType::eUniformBufferDynamic);
    CHECK(p1->pool_sizes[0].descriptorCount == 1u);

    const std::optional<rhi::DescriptorPoolPlan> p2 =
        rhi::descriptor_pool_plan_from_reflection(merged, 2u);
    REQUIRE(p2.has_value());
    CHECK(p2->max_sets == 2u);
    REQUIRE(p2->pool_sizes.size() == 1u);
    CHECK(p2->pool_sizes[0].descriptorCount == 2u);
}

TEST_CASE("descriptor_pool_plan_from_reflection: sets_per_layout 0 -> nullopt") {
    rhi::ShaderReflection empty {};
    CHECK_FALSE(rhi::descriptor_pool_plan_from_reflection(empty, 0u).has_value());
}
