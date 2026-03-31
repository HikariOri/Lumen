很好，这一步我们把你前面所有设计**收敛成一套完整、统一、可落地的 Forward PBR Shader 接口规范**。这基本就是一个小型引擎的核心渲染接口了 👍

我按**工程文档级别**给你整理👇

---

# 🎯 一、总体设计原则（最重要）

### ✅ 核心思想

> **按“更新频率”划分 Descriptor，而不是按功能**

---

### ✅ 分层结构

```text
Set 0 → Frame（每帧）
Set 1 → Material（每材质）
Set 2 → Object（每 draw）
Set 3 → Lighting（灯光）
```

---

# 🧩 二、Descriptor Layout 总览

```text
Set 0 (Frame)
  binding 0 → FrameUBO
  binding 1 → irradianceMap
  binding 2 → prefilteredMap
  binding 3 → brdfLUT

Set 1 (Material)
  binding 0 → MaterialUBO
  binding 1 → baseColorTex
  binding 2 → metallicRoughnessTex
  binding 3 → normalTex
  binding 4 → occlusionTex
  binding 5 → emissiveTex

Set 2 (Object)
  binding 0 → ObjectUBO

Set 3 (Lighting)
  binding 0 → LightUBO
  binding 1 → shadowMap（预留）
```

---

# 🧠 三、Frame（Set 0）

## GLSL

```glsl
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;

    vec3 cameraPos;

    float exposure;     // 曝光
    float iblStrength;  // IBL强度
} frame;

layout(set = 0, binding = 1) uniform samplerCube irradianceMap;
layout(set = 0, binding = 2) uniform samplerCube prefilteredMap;
layout(set = 0, binding = 3) uniform sampler2D brdfLUT;
```

---

## 🎯 作用

* 相机
* HDR 控制（曝光）
* IBL 控制

---

# 🧩 四、Material（Set 1）

---

## GLSL（最终推荐）

```glsl
layout(set = 1, binding = 0) uniform MaterialUBO {
    vec4 baseColorFactor;

    vec4 emissive; // rgb + intensity

    float metallicFactor;
    float roughnessFactor;

    float occlusionStrength;

    uint flags;
    uint alphaMode;

    float alphaCutoff;
} material;
```

---

## Texture

```glsl
layout(set = 1, binding = 1) uniform sampler2D baseColorTex;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessTex;
layout(set = 1, binding = 3) uniform sampler2D normalTex;
layout(set = 1, binding = 4) uniform sampler2D occlusionTex;
layout(set = 1, binding = 5) uniform sampler2D emissiveTex;
```

---

## 🎯 设计要点

* emissive = `rgb * intensity`
* metallicRoughness：

  * G → roughness
  * B → metallic
* flags → 控制是否有贴图（可选优化）

---

# 🧩 五、Object（Set 2）

```glsl
layout(set = 2, binding = 0) uniform ObjectUBO {
    mat4 model;
    mat4 normalMatrix;
} object;
```

---

## 🎯 作用

* 世界变换
* 法线变换

---

# 💡 六、Lighting（Set 3，重点）

---

## ✅ Light Struct（std140安全）

```glsl
#define MAX_LIGHTS 64

struct Light {
    vec4 position;   // xyz + type
    vec4 direction;  // xyz
    vec4 color;      // rgb + intensity
    vec4 params;     // x=range, y=innerCone, z=outerCone
};
```

---

## Light UBO

```glsl
layout(set = 3, binding = 0) uniform LightUBO {
    int lightCount;
    vec3 padding;

    Light lights[MAX_LIGHTS];
} lighting;
```

---

## 🎯 Light Type

```glsl
#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT       1
#define LIGHT_SPOT        2
```

```glsl
int type = int(light.position.w);
```

---

# 🧠 七、灯光计算（统一入口）

---

## ⭐ 核心函数

```glsl
vec3 EvaluateLight(Light light, vec3 N, vec3 V, vec3 fragPos)
{
    int type = int(light.position.w);

    vec3 L;
    float attenuation = 1.0;

    if (type == LIGHT_DIRECTIONAL) {
        L = normalize(-light.direction.xyz);
    }
    else {
        L = light.position.xyz - fragPos;
        float dist = length(L);
        L = normalize(L);

        attenuation = 1.0 / (dist * dist);

        float range = light.params.x;
        attenuation *= clamp(1.0 - dist / range, 0.0, 1.0);

        if (type == LIGHT_SPOT) {
            float inner = light.params.y;
            float outer = light.params.z;

            float theta = dot(L, normalize(-light.direction.xyz));
            float epsilon = inner - outer;
            float intensity = clamp((theta - outer) / epsilon, 0.0, 1.0);

            attenuation *= intensity;
        }
    }

    vec3 radiance = light.color.rgb * light.color.a * attenuation;

    float NdotL = max(dot(N, L), 0.0);

    return radiance * NdotL;
}
```

---

## 主循环

```glsl
vec3 directLighting = vec3(0.0);

for (int i = 0; i < lighting.lightCount; i++) {
    directLighting += EvaluateLight(lighting.lights[i], N, V, fragPos);
}
```

---

# 🌍 八、IBL（环境光）

```glsl
vec3 diffuseIBL = texture(irradianceMap, N).rgb * albedo;

vec3 specularIBL = ... // prefilteredMap + BRDF LUT

vec3 ibl = (diffuseIBL + specularIBL) * frame.iblStrength;
```

---

# ✨ 九、Emissive

```glsl
vec3 emissive = material.emissive.rgb * material.emissive.a;
```

---

# 🌈 十、HDR → LDR（曝光 + Tone Mapping）

---

## ⭐ 曝光（核心）

```glsl
vec3 colorHDR = directLighting + ibl + emissive;

vec3 mapped = vec3(1.0) - exp(-colorHDR * frame.exposure);
```

---

## Gamma

```glsl
vec3 color = pow(mapped, vec3(1.0 / 2.2));
```

---

# 🎯 十一、完整渲染流程

```glsl
// 1. Direct Light
vec3 directLighting = ...

// 2. IBL
vec3 ibl = ...

// 3. Emissive
vec3 emissive = ...

// 4. HDR 合成
vec3 colorHDR = directLighting + ibl + emissive;

// 5. 曝光
vec3 mapped = vec3(1.0) - exp(-colorHDR * frame.exposure);

// 6. Gamma
vec3 finalColor = pow(mapped, vec3(1.0 / 2.2));
```

---

# ⚠️ 十二、关键注意点（必须记住）

---

## ❗ std140 对齐

* 不用 `vec3` 单独存
* 全部 `vec4` 最安全

---

## ❗ emissive 不单独曝光

```glsl
// ❌ 错误
emissive *= exposure;
```

---

## ❗ roughness 最小值

```glsl
roughness = max(roughness, 0.04);
```

---

## ❗ cone 用 cos

```cpp
innerCone = cos(angle)
```

---

## ❗ 灯光数量限制

```cpp
lightCount = min(sceneLights.size(), MAX_LIGHTS);
```

---

# 🚀 十三、未来升级路径（你已经铺好了）

你的设计可以无缝升级到：

* ✅ SSBO（只换 buffer）
* ✅ Forward+
* ✅ Clustered Lighting
* ✅ Bindless textures
* ✅ 阴影系统

---

# 🎯 最终总结（一句话）

👉 你的 Forward PBR Shader 架构已经是：

> **Frame（相机+曝光） + Material（PBR） + Object（变换） + Lighting（多光源） + IBL + HDR流程**

---

# 💡 如果你继续下一步

我强烈建议：

👉 下一步做：

**完整 Cook-Torrance BRDF（GGX + Fresnel + Geometry）**

或者：

👉 **把 IBL specular 完整接上（prefilter + LUT）**

---

如果你说一声，我可以直接给你：

👉 **工业级完整 PBR Fragment Shader（可直接粘进 Vulkan 用）**

这是你 renderer 的“质变点” 👍
