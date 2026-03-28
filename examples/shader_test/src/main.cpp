#include <cstdint>
#include <fstream>
#include <iostream>
#include <print>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spirv_cross/spirv_reflect.hpp>
#include <spirv_glsl.hpp>

std::vector<uint32_t> load_spirv_file(const std::string &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open SPIR-V file: " + path);
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (size % 4 != 0) {
        throw std::runtime_error(
            "Invalid SPIR-V file size (not aligned to 4 bytes)");
    }

    std::vector<uint32_t> buffer(size / 4);

    if (!file.read(reinterpret_cast<char *>(buffer.data()), size)) {
        throw std::runtime_error("Failed to read SPIR-V file: " + path);
    }

    return buffer;
}

// ===================== 递归打印 SPIR-V Type =====================
void dump_type_recursive(spirv_cross::CompilerGLSL &glsl, uint32_t type_id,
                         int indent) {
    const spirv_cross::SPIRType &type = glsl.get_type(type_id);

    for (int i = 0; i < indent; i++) {
        std::cout << " ";
    }

    std::cout << "basetype=" << type.basetype << " vecsize=" << type.vecsize
              << " columns=" << type.columns << "\n";

    // ===== struct =====
    if (type.basetype == spirv_cross::SPIRType::Struct) {
        for (size_t i = 0; i < type.member_types.size(); i++) {
            const uint32_t member_type_id = type.member_types[i];
            std::string member_name = glsl.get_member_name(type_id, i);

            for (int j = 0; j < indent + 2; j++) {
                std::cout << " ";
            }

            std::cout << "member[" << i << "] " << member_name << "\n";

            dump_type_recursive(glsl, member_type_id, indent + 4);
        }
    }

    // ===== array =====
    if (!type.array.empty()) {
        for (int i = 0; i < indent + 2; i++) {
            std::cout << " ";
        }

        std::cout << "array size=" << type.array[0] << "\n";
    }
}

// ===================== 打印 UBO / SSBO 等资源 =====================
void dump_shader_reflection(spirv_cross::CompilerGLSL &glsl) {
    spirv_cross::ShaderResources resources = glsl.get_shader_resources();

    std::cout << "================ SPIR-V REFLECTION ================\n";

    // ================= UBO =================
    std::cout << "\n[Uniform Buffers]\n";
    for (const auto &res : resources.uniform_buffers) {
        uint32_t set =
            glsl.get_decoration(res.id, spv::DecorationDescriptorSet);
        uint32_t binding = glsl.get_decoration(res.id, spv::DecorationBinding);

        std::cout << "UBO: " << res.name << " id=" << res.id << " set=" << set
                  << " binding=" << binding << "\n";

        std::cout << "  STRUCT LAYOUT:\n";
        dump_type_recursive(glsl, res.base_type_id, 4);
    }

    // ================= SSBO =================
    std::cout << "\n[Storage Buffers]\n";
    for (const auto &res : resources.storage_buffers) {
        uint32_t set =
            glsl.get_decoration(res.id, spv::DecorationDescriptorSet);
        uint32_t binding = glsl.get_decoration(res.id, spv::DecorationBinding);

        std::cout << "SSBO: " << res.name << " set=" << set
                  << " binding=" << binding << "\n";

        dump_type_recursive(glsl, res.base_type_id, 4);
    }

    // ================= TEXTURES =================
    std::cout << "\n[Sampled Images]\n";
    for (const auto &res : resources.sampled_images) {
        uint32_t set =
            glsl.get_decoration(res.id, spv::DecorationDescriptorSet);
        uint32_t binding = glsl.get_decoration(res.id, spv::DecorationBinding);

        std::cout << "Texture: " << res.name << " set=" << set
                  << " binding=" << binding << "\n";
    }

    // ================= PUSH CONSTANT =================
    std::cout << "\n[Push Constants]\n";
    for (const auto &res : resources.push_constant_buffers) {
        std::cout << "PushConstant: " << res.name << "\n";
        dump_type_recursive(glsl, res.base_type_id, 4);
    }

    // ================= INPUT =================
    std::cout << "\n[Stage Inputs]\n";
    for (const auto &res : resources.stage_inputs) {
        uint32_t loc = glsl.get_decoration(res.id, spv::DecorationLocation);

        std::cout << res.name << " location=" << loc << "\n";
    }

    // ================= OUTPUT =================
    std::cout << "\n[Stage Outputs]\n";
    for (const auto &res : resources.stage_outputs) {
        uint32_t loc = glsl.get_decoration(res.id, spv::DecorationLocation);

        std::cout << res.name << " location=" << loc << "\n";
    }

    std::cout << "\n================ END REFLECTION ================\n";
}

// 假设 j 是你已经解析好的 nlohmann::json 对象
void save_json_to_file(const nlohmann::json &j, const std::string &filename) {
    std::ofstream file(filename);

    if (file.is_open()) {
        // dump(4) 表示使用 4 个空格缩进，增加可读性
        // 如果不需要缩进（节省空间），直接使用 file << j;
        file << j.dump(-1);

        file.close();
        std::cout << "Successfully saved reflection to " << filename << '\n';
    } else {
        std::cerr << "Could not open file for writing: " << filename << '\n';
    }
}

void show_spirv_json(const nlohmann::json &j) {
    // 遍历 JSON 的一级成员
    for (auto it = j.begin(); it != j.end(); ++it) {
        // it.key() 是键名
        // it.value() 是对应的 JSON 子对象

        // 使用 dump(4) 确保子对象内部也有缩进
        std::println("Key: {:<15} | Value: {}", it.key(), it.value().dump(2));
    }
}

int main() {
    // 1. 读取 SPIR-V 文件到 vector<uint32_t>
    std::vector<uint32_t> spirv_binary =
        load_spirv_file("./shaders/cube.frag.spv");

    try {
        // 2. 创建反射编译器实例
        spirv_cross::CompilerReflection compiler(std::move(spirv_binary));

        // 3. 执行“编译”（在反射模式下即生成 JSON）
        std::string json_data = compiler.compile();

        // 4. 输出结果
        nlohmann::json j = nlohmann::json::parse(json_data);

        save_json_to_file(j, "cube.frag.json");
        show_spirv_json(j);
    } catch (const spirv_cross::CompilerError &e) {
        std::cerr << "SPIRV-Cross Error: " << e.what() << '\n';
    }

    return 0;
}
