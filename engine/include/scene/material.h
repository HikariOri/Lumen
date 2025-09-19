#pragma once

#include <glm/glm.hpp>
#include <optional>
#include <string>

struct TextureHandle {
    // 你实际项目中可能是个 ID、指针、shared_ptr、resource handle 等
    // 用 string path 或整型 ID 或你资源管理系统的句柄
    std::string path;

    bool operator==(const TextureHandle &other) const {
        return path == other.path;
    }
    bool operator!=(const TextureHandle &other) const {
        return !(*this == other);
    }
};

struct Material {
    // 材质名称／标签（可选）
    std::string name = "DefaultMaterial";

    // PBR 基本属性
    glm::vec4 baseColor = glm::vec4(1.0F);     // RGBA（albedo + alpha）
    float metallic = 0.0F;                     // 0 = dielectric, 1 = metal
    float roughness = 1.0F;                    // 0 = 平滑, 1 = 粗糙
    float ambientOcclusion = 1.0F;             // ambient occlusion 强度
    glm::vec3 emissiveColor = glm::vec3(0.0F); // 发光颜色
    float emissiveIntensity = 0.0F;            // 发光强度

    // 贴图（可选）
    std::optional<TextureHandle> baseColorTexture;
    std::optional<TextureHandle> metallicTexture;
    std::optional<TextureHandle> roughnessTexture;
    std::optional<TextureHandle> normalTexture;
    std::optional<TextureHandle> aoTexture; // ambient occlusion map
    std::optional<TextureHandle> emissiveTexture;

    // 法线贴图 Y 轴翻转（有的引擎／贴图约定法线贴图 Y 方向可能需要翻转）
    bool flipNormalY = false;

    // 构造函数
    Material() = default;

    Material(std::string name_) : name(std::move(name_)) {}

    // 比较（用于检测材质是否相同、缓存 key 等）
    bool operator==(const Material &other) const {
        return name == other.name && baseColor == other.baseColor &&
               metallic == other.metallic && roughness == other.roughness &&
               ambientOcclusion == other.ambientOcclusion &&
               emissiveColor == other.emissiveColor &&
               emissiveIntensity == other.emissiveIntensity &&
               flipNormalY == other.flipNormalY &&
               baseColorTexture == other.baseColorTexture &&
               metallicTexture == other.metallicTexture &&
               roughnessTexture == other.roughnessTexture &&
               normalTexture == other.normalTexture &&
               aoTexture == other.aoTexture &&
               emissiveTexture == other.emissiveTexture;
    }

    bool operator!=(const Material &other) const { return !(*this == other); }

    // 计算 shader 用的 F0（在金属 vs 非金属情况不同）
    glm::vec3 getF0() const {
        // 通常非金属的 F0 是一个常数，比如 0.04（≈ 4% 反射率）
        const glm::vec3 dielectricF0 = glm::vec3(0.04f);
        if (metallic >= 1.0f) {
            // 如果完全是金属，那么 baseColor 就代表反射色
            return glm::vec3(baseColor);
        } else {
            // 混合金属与非金属 F0
            return glm::mix(dielectricF0, glm::vec3(baseColor), metallic);
        }
    }

    // 是否有完整的 PBR 贴图集可用
    bool hasFullPBRTextures() const {
        return baseColorTexture.has_value() && metallicTexture.has_value() &&
               roughnessTexture.has_value() && normalTexture.has_value() &&
               aoTexture.has_value();
    }
};