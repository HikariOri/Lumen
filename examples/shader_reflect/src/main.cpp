#include <cstdint>
#include <print>

#include "core/log/logger.hpp"

#include "utils.hpp"

#include <spirv-reflect/spirv_reflect.h>
#include <vector>

int main(int argc, char *argv[]) {

    auto vertexShader = load_spirv("./shaders/shader_reflect.frag.spv");

    SpvReflectShaderModule module {};
    SpvReflectResult result = spvReflectCreateShaderModule(
        vertexShader.size(), vertexShader.data(), &module);

    // 获取描述符集
    {
        std::uint32_t setCount {};
        spvReflectEnumerateDescriptorSets(&module, &setCount, nullptr);
        std::vector<SpvReflectDescriptorSet *> sets(setCount);
        spvReflectEnumerateDescriptorSets(&module, &setCount, sets.data());

        for (auto *const set : sets) {
            std::println("Set: {}", set->set);

            for (std::uint32_t j {}; j < set->binding_count; ++j) {
                auto *binding = set->bindings[j];
                std::println("  binding: {}", binding->binding);
                std::println("  name: {}", binding->name);
                std::cout << "  type: " << binding->descriptor_type << '\n';
                std::println("  count: {}", binding->count);
            }
        }
    }

    // 获取 Pusn Constant
    {
        std::uint32_t pcCount {};
        spvRe
    }

    return 0;
}
