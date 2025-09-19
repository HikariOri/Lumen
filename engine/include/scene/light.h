#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

enum struct LightType : std::uint8_t { Point, Spot, Directional, Area };

struct Light {
    LightType type;

    // 通用参数
    glm::vec3 color = glm::vec3(1.0F); // 光颜色
    float intensity = 1.0F;            // 光强度（总体强度 multiplier）

    // 对于 Point, Spot, Area 类型，需要位置
    glm::vec3 position = glm::vec3(0.0F);

    // 对于 Directional, Spot, Area 类型，需要方向
    glm::vec3 direction = glm::vec3(0.0F, -1.0F, 0.0F); // 单位方向向量

    // 点光 & 聚光的衰减参数
    float range = 10.0F; // 有效半径，超过这个距离贡献为零或非常小

    // 聚光灯特有参数
    float innerConeAngle = glm::radians(15.0F); // 内部锥角（弧度）
    float outerConeAngle =
        glm::radians(30.0F); // 外部锥角（弧度），通常 > innerConeAngle

    // 面光 (Area light) 特定参数（假设矩形区域光）
    glm::vec2 areaSize = glm::vec2(1.0F, 1.0F); // 面光的宽度和高度
    // 面光可能要知道它所处的平面法线方向等，可用 direction 表示哪个朝向发光

    // 可选阴影／光源投射属性等等可 expand

    // 构造函数
    Light(LightType t = LightType::Point) : type(t) {}

    // 判断点是否在光源的影响范围内（对于 Point / Spot / Area）
    bool isInRange(const glm::vec3 &point) const {
        if (type == LightType::Directional) {
            // 方向光无距离衰减或无限远
            return true;
        }
        float dist2 = glm::distance2(point, position);
        return dist2 <= (range * range);
    }

    // 对 Spot 类型，判断点是否在锥内
    bool isInsideSpot(const glm::vec3 &point) const {
        if (type != LightType::Spot) {
            return false;
        }
        // 方向向量应为单位向量
        glm::vec3 L = glm::normalize(point - position);
        float cosAngle = glm::dot(L, glm::normalize(direction));
        // outerConeAngle 是角度（弧度），取余弦比较
        float cosOuter = glm::cos(outerConeAngle);
        return cosAngle >= cosOuter;
    }

    // 获取衰减（基于距离），可以根据你的 shader 用不同公式
    float getAttenuation(const glm::vec3 &point) const {
        if (type == LightType::Directional) {
            return 1.0F;
        }
        float dist = glm::distance(point, position);
        // 简单的反平方衰减 +在近处 clamp
        float att = 1.0F / (dist * dist + 1e-6F);
        // 若希望在 range 外衰减到零，可以乘以一个 smoothstep
        if (dist > range) {
            return 0.0F;
        }
        return att;
    }
};