#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "rhi/shader_reflection.hpp"

#include <ghc/filesystem.hpp>

#include <cstdint>
#include <fstream>
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
    CHECK(ubo->descriptor_count == 1);
    CHECK((ubo->stages & vk::ShaderStageFlagBits::eVertex) ==
          vk::ShaderStageFlagBits::eVertex);
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
    CHECK(ubo->descriptor_type == vk::DescriptorType::eUniformBuffer);
    CHECK((ubo->stages & vk::ShaderStageFlagBits::eVertex) ==
          vk::ShaderStageFlagBits::eVertex);
    CHECK((ubo->stages & vk::ShaderStageFlagBits::eFragment) ==
          vk::ShaderStageFlags {});
}
