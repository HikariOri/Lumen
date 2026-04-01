好，我们就**只收敛 Debug / Visualization 这一块**，整理成一套**可以直接落地的设计 + shader实现 + 可扩展方向** 👍

---

# 🎯 一、Debug 系统设计目标

你需要解决的是：

```text
在 Forward PBR 中，动态查看：
- 几何信息（normal / depth）
- 材质（albedo / roughness / metallic）
- 光照分量（diffuse / specular / IBL）
- 数值分布（heatmap）
```

---

## ✅ 核心原则

### ⭐ 1. 不破坏主渲染流程

```text
始终完整计算 PBR → 最后选择输出
```

---

### ⭐ 2. Debug 是“输出层”，不是“计算层”

```text
EvaluatePBR() ❌ 不改
GetDebugColor() ✅ 控制输出
```

---

### ⭐ 3. 支持扩展（以后加 shadow / AO / GI）

---

# 🧩 二、Debug Mode 设计（分组非常重要）

---

## ⭐ 推荐枚举

```glsl
#define DEBUG_NONE                0

// Geometry
#define DEBUG_NORMAL_WS           1
#define DEBUG_NORMAL_TS           2
#define DEBUG_DEPTH               3

// Material
#define DEBUG_ALBEDO              10
#define DEBUG_METALLIC            11
#define DEBUG_ROUGHNESS           12
#define DEBUG_AO                  13

// Lighting
#define DEBUG_DIRECT_DIFFUSE      20
#define DEBUG_DIRECT_SPECULAR     21
#define DEBUG_IBL_DIFFUSE         22
#define DEBUG_IBL_SPECULAR        23
#define DEBUG_EMISSIVE            24

// Combined
#define DEBUG_FINAL_NO_IBL        30
#define DEBUG_FINAL_NO_DIRECT     31

// Heatmap
#define DEBUG_HEAT_LIGHT_INTENSITY 50
#define DEBUG_HEAT_NDOTL           51
#define DEBUG_HEAT_LIGHT_COUNT     52
```

---

# 🧠 三、Shader 结构（必须这样组织）

---

## ⭐ Step 1：PBR结果拆分

```glsl
struct PBRResult {
    vec3 directDiffuse;
    vec3 directSpecular;

    vec3 iblDiffuse;
    vec3 iblSpecular;

    vec3 emissive;
};
```

---

## ⭐ Step 2：主流程（核心骨架）

```glsl
PBRResult r = EvaluatePBR(...);
```

---

# 🎯 四、Debug 输出核心函数（重点）

---

## ⭐ 标准实现

```glsl
vec3 GetDebugColor(
    int mode,
    PBRResult r,
    vec3 N,
    vec3 albedo,
    float metallic,
    float roughness,
    float ao,
    float depth,
    float NdotL,
    int lightCount)
{
    switch (mode)
    {
        // ===== Geometry =====
        case DEBUG_NORMAL_WS:
            return N * 0.5 + 0.5;

        case DEBUG_DEPTH:
            return vec3(depth);

        // ===== Material =====
        case DEBUG_ALBEDO:
            return albedo;

        case DEBUG_METALLIC:
            return vec3(metallic);

        case DEBUG_ROUGHNESS:
            return vec3(roughness);

        case DEBUG_AO:
            return vec3(ao);

        // ===== Lighting =====
        case DEBUG_DIRECT_DIFFUSE:
            return r.directDiffuse;

        case DEBUG_DIRECT_SPECULAR:
            return r.directSpecular;

        case DEBUG_IBL_DIFFUSE:
            return r.iblDiffuse;

        case DEBUG_IBL_SPECULAR:
            return r.iblSpecular;

        case DEBUG_EMISSIVE:
            return r.emissive;

        // ===== Combined =====
        case DEBUG_FINAL_NO_IBL:
            return r.directDiffuse + r.directSpecular;

        case DEBUG_FINAL_NO_DIRECT:
            return r.iblDiffuse + r.iblSpecular + r.emissive;

        // ===== Heatmap =====
        case DEBUG_HEAT_LIGHT_INTENSITY:
        {
            float v = length(r.directDiffuse + r.directSpecular);
            return Heatmap(v / 10.0);
        }

        case DEBUG_HEAT_NDOTL:
            return Heatmap(NdotL);

        case DEBUG_HEAT_LIGHT_COUNT:
            return Heatmap(float(lightCount) / 64.0);

        default:
            return
                r.directDiffuse +
                r.directSpecular +
                r.iblDiffuse +
                r.iblSpecular +
                r.emissive;
    }
}
```

---

# 🌈 五、Heatmap 实现（通用工具）

---

## ⭐ 推荐版本（通用）

```glsl
vec3 Heatmap(float v)
{
    v = clamp(v, 0.0, 1.0);

    return vec3(
        smoothstep(0.5, 1.0, v),
        smoothstep(0.0, 1.0, v),
        1.0 - smoothstep(0.0, 0.5, v)
    );
}
```

---

## 👉 颜色分布

```text
蓝 → 青 → 绿 → 黄 → 红
```

---

# ⚠️ 六、Tone Mapping 处理（非常关键）

---

## ❗ Debug 时不要做 Tone Mapping

```glsl
vec3 color = GetDebugColor(...);

bool isFinal = (mode == DEBUG_NONE);

if (isFinal)
{
    color = vec3(1.0) - exp(-color * frame.exposure);
    color = pow(color, vec3(1.0 / 2.2));
}
```

---

👉 否则：

* normal 会变
* roughness 会变
* heatmap 会失真

---

# 🧩 七、主函数完整写法（标准模板）

---

```glsl
// 1. 计算 PBR
PBRResult r = EvaluatePBR(...);

// 2. Debug 输出
vec3 color = GetDebugColor(
    frame.debugMode,
    r,
    N,
    albedo,
    metallic,
    roughness,
    ao,
    depth,
    NdotL,
    lighting.lightCount
);

// 3. Tone Mapping（仅最终）
if (frame.debugMode == DEBUG_NONE)
{
    color = vec3(1.0) - exp(-color * frame.exposure);
    color = pow(color, vec3(1.0 / 2.2));
}

// 4. 输出
outColor = vec4(color, 1.0);
```

---

# 🎯 八、CPU / 引擎侧设计

---

## FrameUBO

```cpp
struct FrameUBO {
    ...
    int debugMode;
};
```

---

## UI（示例）

```cpp
ImGui::Combo("Debug Mode", &debugMode, {
    "Final",
    "Normal",
    "Albedo",
    "Roughness",
    "Direct Diffuse",
    "Specular",
    "IBL",
    "Heat Light"
});
```

---

# 🚀 九、扩展方向（Debug 专属）

---

## 🔥 1. 更多 Heatmap

```text
- shadow intensity
- AO
- specular energy
- exposure
```

---

## 🔥 2. Histogram（进阶）

```text
统计亮度分布 → 自动曝光
```

---

## 🔥 3. MRT Debug（Forward进阶）

```glsl
outColor
outNormal
outMaterial
```

---

## 🔥 4. Debug Pass（专业级）

```text
Forward Pass → Buffer → Debug Pass
```

👉 不污染主 shader

---

## 🔥 5. Overlay Debug

```text
最终画面 + 边缘高亮 / light count overlay
```

---

# 🎯 十、一句话总结

👉 Debug 系统本质是：

> **“完整计算 → 最后选择你想看的数据（而不是改计算）”**

---

# 💡 最关键建议

你现在应该做到：

```text
1. PBRResult 拆分 ✔
2. GetDebugColor ✔
3. Heatmap ✔
4. Tone Mapping 分离 ✔
```

---

做到这一步，你的渲染器调试能力已经接近
Unreal Engine / Unity 的基础级别 👍

---

如果你下一步继续，我可以帮你：

👉 设计 **“RenderGraph Debug Viewer（多窗口 + 多buffer）”**

这个会是你引擎从“能用”到“专业工具”的关键一步 🚀
