下面是整理好的**工程级 Vulkan 光源系统设计文档**，已经帮你按“引擎开发文档”的风格组织好了，可以直接放进你的项目（比如 `docs/rendering/lighting.md`）。

---

# Vulkan 引擎光源系统设计文档

---

# 1. 概述

光照系统是实时渲染引擎的核心模块之一，负责描述光源类型、光照模型以及光与材质之间的交互关系。

在现代图形 API（如 Vulkan）中，常见光源主要包括：

* Directional Light（方向光）
* Point Light（点光源）
* Spot Light（聚光灯）

这些光源构成了实时渲染的基础体系 ([Vulkan Documentation][1])。

本设计目标：

* 支持现代 PBR 渲染流程
* 支持扩展（Area Light / IBL）
* 支持高性能（Forward+ / Clustered）

---

# 2. 光源类型设计

---

## 2.1 Directional Light（方向光）

### 定义

方向光模拟来自无限远的光源（如太阳），光线方向在整个场景中保持一致 ([P.A. Minerva][2])。

### 特点

* 仅有方向，无位置
* 无距离衰减
* 所有像素共享同一光照方向

### 数据结构

```cpp
struct DirectionalLight {
    glm::vec3 direction;
    float pad0;

    glm::vec3 color;
    float intensity;
};
```

### 使用场景

* 主光源（Sun Light）
* 阴影系统（CSM）

---

## 2.2 Point Light（点光源）

### 定义

点光源从一个位置向所有方向发光，类似灯泡 ([P.A. Minerva][2])。

### 特点

* 有位置
* 向四周均匀发光
* 随距离衰减

### 衰减模型

```glsl
float attenuation = 1.0 / (constant + linear * d + quadratic * d * d);
```

### 数据结构

```cpp
struct PointLight {
    glm::vec3 position;
    float range;

    glm::vec3 color;
    float intensity;
};
```

### 工程注意

* 阴影成本高（需要 cube shadow map，6 次渲染） ([rebelfork.io][3])
* 数量多时性能压力极大

---

## 2.3 Spot Light（聚光灯）

### 定义

聚光灯从一个点沿特定方向发射，形成锥形光照区域 ([P.A. Minerva][2])。

### 特点

* 有位置 + 方向
* 有角度范围
* 有距离衰减 + 角度衰减

### 数据结构

```cpp
struct SpotLight {
    glm::vec3 position;
    float range;

    glm::vec3 direction;
    float innerCutoff;

    float outerCutoff;
    float intensity;

    glm::vec3 color;
};
```

### 优势

* 阴影成本低（单 shadow map） ([rebelfork.io][3])
* 可控性强

---

# 3. 扩展光源（现代引擎）

---

## 3.1 Area Light（面积光）

### 特点

* 光源具有面积（矩形 / 球）
* 更接近真实物理光照

### 问题

* 需要积分计算（昂贵）
* 实时渲染中通常使用近似（LTC）

---

## 3.2 Image Based Lighting（IBL）

### 定义

基于环境贴图的全局光照，用于提供环境光与反射。

### 组成

* Irradiance Map（漫反射）
* Prefilter Map（镜面反射）
* BRDF LUT

### 数据结构

```cpp
struct IBL {
    TextureCube irradiance;
    TextureCube prefiltered;
    Texture2D brdfLUT;
};
```

### 作用

* 提供全局光照
* PBR 必备组件

---

## 3.3 Emissive（自发光）

### 特点

* 材质属性，而非独立光源
* 不一定影响其他物体（除非 GI）

---

# 4. 光照模型

---

## 4.1 传统模型（不推荐）

* Phong
* Blinn-Phong

问题：

* 不符合物理规律
* 能量不守恒

---

## 4.2 PBR（推荐）

PBR（Physically Based Rendering）是现代引擎标准，通过模拟真实光学行为提升真实感 ([CSDN博客][4])。

### 核心特性

* 能量守恒
* Fresnel
* Roughness / Metallic
* BRDF（Cook-Torrance）

### 优势

* 统一材质表现
* 更真实
* 可扩展（IBL / GI）

---

# 5. 引擎架构设计

---

## 5.1 ECS 组件设计

```cpp
enum class LightType {
    Directional,
    Point,
    Spot
};

struct LightComponent {
    LightType type;

    glm::vec3 color;
    float intensity;

    union {
        DirectionalLight directional;
        PointLight point;
        SpotLight spot;
    };
};
```

---

## 5.2 GPU 数据结构

⚠️ 不直接上传 ECS 数据

```cpp
struct GPULight {
    glm::vec4 position;   // w: type
    glm::vec4 direction;
    glm::vec4 color;
    glm::vec4 params;     // range / cutoff
};
```

---

## 5.3 光源管理（关键）

必须实现：

### Light Culling

否则复杂场景会崩：

* Forward+
* Clustered Lighting
* Deferred Rendering

---

## 5.4 阴影系统

不同光源对应不同策略：

| 光源          | 阴影方案                |
| ----------- | ------------------- |
| Directional | Cascaded Shadow Map |
| Point       | Cube Shadow Map     |
| Spot        | Single Shadow Map   |

---

# 6. 渲染流程整合

典型流程：

```text
Scene
 → 收集 LightComponent
 → 构建 GPU Light Buffer
 → Light Culling（Forward+）
 → Shader Lighting（PBR）
 → IBL 叠加
 → 输出最终颜色
```

---

# 7. 开发路线（强烈建议）

---

## 阶段 1（最小可用）

* Directional Light
* Point Light（无阴影）
* Blinn-Phong

---

## 阶段 2（现代化）

* PBR
* IBL
* Spot Light

---

## 阶段 3（引擎级）

* Forward+
* Clustered Lighting
* Area Light（LTC）

---

# 8. 常见问题与建议

---

## ❌ 常见错误

* 一开始就做 PBR + IBL + 阴影
* 无光源裁剪（性能爆炸）
* 光源数据直接绑定 ECS

---

## ✅ 推荐策略

1. 先跑通基础光照
2. 再升级 PBR
3. 最后优化性能

---

# 9. 总结

Vulkan 光源系统的本质是：

> **光源类型 + 光照模型 + 性能架构**

三者缺一不可。

---

如果你下一步要继续做引擎，我建议我们直接进入：

👉 **Light System + Descriptor + Shader 完整实现（含 Forward+）**

我可以帮你把这一块做到“接近商业引擎级别”。

[1]: https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Lighting_Materials/02_lighting_models.html "https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Lighting_Materials/02_lighting_models.html"
[2]: https://paminerva.github.io/docs/LearnVulkan/01.H-Hello-Lighting.html "https://paminerva.github.io/docs/LearnVulkan/01.H-Hello-Lighting.html"
[3]: https://rebelfork.io/docs/rendering-lights.html "https://rebelfork.io/docs/rendering-lights.html"
[4]: https://blog.csdn.net/weixin_42502040/article/details/151691027 "https://blog.csdn.net/weixin_42502040/article/details/151691027"
