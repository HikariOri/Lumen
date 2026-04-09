#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::byte> load_file_bytes(const std::string &path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return {};
    }
    const auto sz = static_cast<std::size_t>(f.tellg());
    f.seekg(0);
    std::vector<std::byte> out(sz);
    f.read(reinterpret_cast<char *>(out.data()),
           static_cast<std::streamsize>(sz));
    return out;
}

[[nodiscard]] std::string shader_path(const char *name) {
    const char *base = SDL_GetBasePath();
    if (base != nullptr) {
        std::filesystem::path p(base);
        p /= "shaders";
        p /= name;
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }
    return std::string("shaders/") + name;
}

} // namespace

int main(int argc, char *argv[]) {

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::println(stderr, "SDL_Init: {}", SDL_GetError());
        return 1;
    }

    SDL_GPUDevice *device =
        SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
    if (device == nullptr) {
        std::println(stderr, "SDL_CreateGPUDevice: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Window *window =
        SDL_CreateWindow("SDL3 GPU — 三角形", 800, 800, SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        std::println(stderr, "SDL_CreateWindow: {}", SDL_GetError());
        SDL_DestroyGPUDevice(device);
        SDL_Quit();
        return 1;
    }

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        std::println(stderr, "SDL_ClaimWindowForGPUDevice: {}", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_DestroyGPUDevice(device);
        SDL_Quit();
        return 1;
    }

    const std::string vert_path = shader_path("triangle.vert.spv");
    const std::string frag_path = shader_path("triangle.frag.spv");
    std::vector<std::byte> vert_spv = load_file_bytes(vert_path);
    std::vector<std::byte> frag_spv = load_file_bytes(frag_path);
    if (vert_spv.empty() || frag_spv.empty()) {
        std::println(stderr, "无法加载 SPIR-V: {} / {}", vert_path, frag_path);
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyWindow(window);
        SDL_DestroyGPUDevice(device);
        SDL_Quit();
        return 1;
    }

    SDL_GPUShaderCreateInfo shader_info {};
    shader_info.code = reinterpret_cast<const Uint8 *>(vert_spv.data());
    shader_info.code_size = vert_spv.size();
    shader_info.entrypoint = "main";
    shader_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    shader_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    shader_info.num_samplers = 0;
    shader_info.num_storage_textures = 0;
    shader_info.num_storage_buffers = 0;
    shader_info.num_uniform_buffers = 0;

    SDL_GPUShader *vs = SDL_CreateGPUShader(device, &shader_info);
    if (vs == nullptr) {
        std::println(stderr, "顶点着色器: {}", SDL_GetError());
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyWindow(window);
        SDL_DestroyGPUDevice(device);
        SDL_Quit();
        return 1;
    }

    shader_info.code = reinterpret_cast<const Uint8 *>(frag_spv.data());
    shader_info.code_size = frag_spv.size();
    shader_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    SDL_GPUShader *fs = SDL_CreateGPUShader(device, &shader_info);
    if (fs == nullptr) {
        std::println(stderr, "片段着色器: {}", SDL_GetError());
        SDL_ReleaseGPUShader(device, vs);
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyWindow(window);
        SDL_DestroyGPUDevice(device);
        SDL_Quit();
        return 1;
    }

    /// 必须与交换链 color attachment 格式一致，否则 Vulkan 管线子通道无 color0，与
    /// `layout(location=0) out` 及 `BeginGPURenderPass` 不一致，触发校验层。
    const SDL_GPUTextureFormat swap_fmt =
        SDL_GetGPUSwapchainTextureFormat(device, window);

    SDL_GPUVertexInputState vertex_input {};
    vertex_input.num_vertex_buffers = 0;
    vertex_input.num_vertex_attributes = 0;

    SDL_GPUColorTargetDescription color_target_desc {};
    color_target_desc.format = swap_fmt;
    color_target_desc.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color_target_desc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    color_target_desc.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target_desc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color_target_desc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    color_target_desc.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target_desc.blend_state.color_write_mask =
        SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
        SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    color_target_desc.blend_state.enable_blend = false;

    SDL_GPUGraphicsPipelineTargetInfo target_info {};
    target_info.color_target_descriptions = &color_target_desc;
    target_info.num_color_targets = 1;
    target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    target_info.has_depth_stencil_target = false;

    SDL_GPUMultisampleState multisample {};
    multisample.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_GPUDepthStencilState depth_stencil {};
    depth_stencil.enable_depth_test = false;
    depth_stencil.enable_depth_write = false;
    depth_stencil.enable_stencil_test = false;

    SDL_GPUGraphicsPipelineCreateInfo pipeline_info {};
    pipeline_info.vertex_shader = vs;
    pipeline_info.fragment_shader = fs;
    pipeline_info.vertex_input_state = vertex_input;
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.multisample_state = multisample;
    pipeline_info.depth_stencil_state = depth_stencil;
    pipeline_info.target_info = target_info;

    SDL_GPUGraphicsPipeline *pipeline =
        SDL_CreateGPUGraphicsPipeline(device, &pipeline_info);
    SDL_ReleaseGPUShader(device, vs);
    SDL_ReleaseGPUShader(device, fs);

    if (pipeline == nullptr) {
        std::println(stderr, "SDL_CreateGPUGraphicsPipeline: {}", SDL_GetError());
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyWindow(window);
        SDL_DestroyGPUDevice(device);
        SDL_Quit();
        return 1;
    }

    bool running { true };
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr) {
            std::println(stderr, "SDL_AcquireGPUCommandBuffer: {}",
                         SDL_GetError());
            break;
        }

        SDL_GPUTexture *backbuffer { nullptr };
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &backbuffer,
                                                    nullptr, nullptr)) {
            std::println(stderr, "SDL_WaitAndAcquireGPUSwapchainTexture: {}",
                         SDL_GetError());
            break;
        }

        if (backbuffer != nullptr) {
            SDL_GPUColorTargetInfo color {};
            color.texture = backbuffer;
            color.load_op = SDL_GPU_LOADOP_CLEAR;
            color.store_op = SDL_GPU_STOREOP_STORE;
            color.clear_color = SDL_FColor { 0.1F, 0.1F, 0.12F, 1.F };

            SDL_GPURenderPass *pass =
                SDL_BeginGPURenderPass(cmd, &color, 1, nullptr);
            SDL_BindGPUGraphicsPipeline(pass, pipeline);
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
            SDL_EndGPURenderPass(pass);
        }

        SDL_SubmitGPUCommandBuffer(cmd);
    }

    SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyWindow(window);
    SDL_DestroyGPUDevice(device);
    SDL_Quit();
    return 0;
}
