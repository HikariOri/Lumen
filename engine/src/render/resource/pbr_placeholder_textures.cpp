/**
 * @file pbr_placeholder_textures.cpp
 */

#include "render/resource/pbr_placeholder_textures.hpp"
#include "render/command_buffer.hpp"
#include "render/context.hpp"
#include "render/resource/sampler.hpp"

namespace lumen::render {

bool PbrPlaceholderTextures::create(const Context &ctx, VkQueue transfer_queue,
                                    CommandPool &cmd_pool) {
    constexpr std::uint8_t white_rgba[] = { 255, 255, 255, 255 };
    constexpr std::uint8_t flat_normal_rgba[] = { 128, 128, 255, 255 };
    /// glTF：G=粗糙 1，B=金属 0
    constexpr std::uint8_t default_mr_rgba[] = { 255, 255, 0, 255 };
    constexpr std::uint8_t white_ao_rgba[] = { 255, 255, 255, 255 };
    constexpr std::uint8_t black_emissive_rgba[] = { 0, 0, 0, 255 };

    SamplerConfig linear_repeat {};
    linear_repeat.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    linear_repeat.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    const bool a =
        albedo_.create_from_memory(ctx, white_rgba, 4, 1, 1, transfer_queue,
                                   cmd_pool, VK_FORMAT_R8G8B8A8_SRGB,
                                   linear_repeat, false);
    const bool n =
        normal_.create_from_memory(ctx, flat_normal_rgba, 4, 1, 1,
                                   transfer_queue, cmd_pool,
                                   VK_FORMAT_R8G8B8A8_UNORM, linear_repeat,
                                   false);
    const bool mr = metallic_roughness_.create_from_memory(
        ctx, default_mr_rgba, 4, 1, 1, transfer_queue, cmd_pool,
        VK_FORMAT_R8G8B8A8_UNORM, linear_repeat, false);
    const bool ao =
        ao_.create_from_memory(ctx, white_ao_rgba, 4, 1, 1, transfer_queue,
                               cmd_pool, VK_FORMAT_R8G8B8A8_UNORM, linear_repeat,
                               false);
    const bool e =
        emissive_.create_from_memory(ctx, black_emissive_rgba, 4, 1, 1,
                                     transfer_queue, cmd_pool,
                                     VK_FORMAT_R8G8B8A8_UNORM, linear_repeat,
                                     false);
    return a && n && mr && ao && e;
}

} // namespace lumen::render
