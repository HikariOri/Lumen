很好，这一步就是把 **“这些模式到底在看什么”彻底讲清楚** 👍
我按你分的类别，一个个说清楚它们的**物理意义 + 调试用途 + 常见问题表现**。

---

# 🎯 一、DEBUG_NONE

```glsl
#define DEBUG_NONE 0
```

👉 **正常最终输出**

### 看什么？

* 完整 PBR（direct + IBL + emissive）
* HDR + tone mapping + gamma

### 用途

* 最终效果验证

---

# 🧩 二、Geometry（几何信息）

---

## ⭐ DEBUG_NORMAL_WS

```glsl
#define DEBUG_NORMAL_WS 1
```

👉 **世界空间法线**

```glsl
color = N * 0.5 + 0.5;
```

---

### 看什么？

* 模型表面朝向

### 正常表现

* 红绿蓝渐变平滑

### 常见问题

| 表现    | 问题        |
| ----- | --------- |
| 一片黑/白 | 法线没传      |
| 花屏    | TBN错误     |
| 接缝明显  | tangent问题 |

---

## ⭐ DEBUG_NORMAL_TS

```glsl
#define DEBUG_NORMAL_TS 2
```

👉 **切线空间法线（normal map）**

---

### 看什么？

* normal map 是否正确

### 常见问题

| 表现   | 问题                      |
| ---- | ----------------------- |
| 全蓝   | 没采样                     |
| 反向光照 | Y轴没翻（OpenGL vs DirectX） |

---

## ⭐ DEBUG_DEPTH

```glsl
#define DEBUG_DEPTH 3
```

👉 **深度值**

---

### 看什么？

* 深度 buffer

### 注意

```glsl
// 非线性
depth = gl_FragCoord.z;
```

👉 更推荐线性化：

```glsl
float linearDepth = ...
```

---

### 常见问题

| 表现     | 问题     |
| ------ | ------ |
| 全白     | 深度范围错误 |
| 很近才有变化 | 非线性    |

---

# 🎨 三、Material（材质）

---

## ⭐ DEBUG_ALBEDO

```glsl
#define DEBUG_ALBEDO 10
```

👉 **基础颜色**

---

### 看什么？

* baseColorFactor × texture

### 常见问题

| 表现 | 问题      |
| -- | ------- |
| 太灰 | sRGB没开  |
| 偏色 | gamma错误 |

---

## ⭐ DEBUG_METALLIC

```glsl
#define DEBUG_METALLIC 11
```

👉 **金属度**

---

### 看什么？

* 0 → 非金属
* 1 → 金属

---

### 常见问题

| 表现 | 问题  |
| -- | --- |
| 全0 | 没采样 |
| 全1 | 贴图错 |

---

## ⭐ DEBUG_ROUGHNESS

```glsl
#define DEBUG_ROUGHNESS 12
```

👉 **粗糙度**

---

### 看什么？

* 影响高光模糊

---

### 常见问题

| 表现   | 问题          |
| ---- | ----------- |
| 全亮   | roughness=1 |
| 高光异常 | roughness错误 |

---

## ⭐ DEBUG_AO

```glsl
#define DEBUG_AO 13
```

👉 **环境遮蔽**

---

### 看什么？

* 阴影细节

---

# 💡 四、Lighting（光照分量）

---

## ⭐ DEBUG_DIRECT_DIFFUSE

```glsl
#define DEBUG_DIRECT_DIFFUSE 20
```

👉 **直接光漫反射**

---

### 看什么？

```text
Lambert / Disney Diffuse
```

---

### 用途

* 验证光源方向
* 验证 shadow

---

## ⭐ DEBUG_DIRECT_SPECULAR

```glsl
#define DEBUG_DIRECT_SPECULAR 21
```

👉 **直接光高光**

---

### 看什么？

```text
GGX + Fresnel + Geometry
```

---

### 常见问题

| 表现  | 问题          |
| --- | ----------- |
| 没高光 | roughness太大 |
| 太亮  | F0错误        |
| 错位  | N/V/L错误     |

---

## ⭐ DEBUG_IBL_DIFFUSE

```glsl
#define DEBUG_IBL_DIFFUSE 22
```

👉 **环境漫反射**

---

### 看什么？

```glsl
irradianceMap(N)
```

---

### 常见问题

| 表现 | 问题            |
| -- | ------------- |
| 黑  | irradiance没生成 |
| 不对 | cubemap方向错    |

---

## ⭐ DEBUG_IBL_SPECULAR

```glsl
#define DEBUG_IBL_SPECULAR 23
```

👉 **环境高光**

---

### 看什么？

```text
prefiltered map + BRDF LUT
```

---

### 常见问题

| 表现   | 问题           |
| ---- | ------------ |
| 没反射  | mip错误        |
| 模糊不对 | roughness映射错 |

---

## ⭐ DEBUG_EMISSIVE

```glsl
#define DEBUG_EMISSIVE 24
```

👉 **自发光**

---

### 看什么？

* emissive texture × factor

---

# 🔀 五、Combined（组合）

---

## ⭐ DEBUG_FINAL_NO_IBL

```glsl
#define DEBUG_FINAL_NO_IBL 30
```

👉 **只看直接光**

```glsl
directDiffuse + directSpecular
```

---

### 用途

* 判断 IBL 是否过强

---

## ⭐ DEBUG_FINAL_NO_DIRECT

```glsl
#define DEBUG_FINAL_NO_DIRECT 31
```

👉 **只看环境光**

```glsl
ibl + emissive
```

---

### 用途

* 判断环境是否主导

---

# 🌈 六、Heatmap（数值可视化）

---

## ⭐ DEBUG_HEAT_LIGHT_INTENSITY

```glsl
#define DEBUG_HEAT_LIGHT_INTENSITY 50
```

👉 **光照强度分布**

---

### 看什么？

```glsl
length(direct lighting)
```

---

### 用途

* 过曝检测
* 能量分布

---

---

## ⭐ DEBUG_HEAT_NDOTL

```glsl
#define DEBUG_HEAT_NDOTL 51
```

👉 **N·L 分布**

---

### 看什么？

```glsl
dot(N, L)
```

---

### 用途

* 法线是否正确
* 光方向是否对

---

---

## ⭐ DEBUG_HEAT_LIGHT_COUNT

```glsl
#define DEBUG_HEAT_LIGHT_COUNT 52
```

👉 **每像素光源数量**

---

### 看什么？

```glsl
lightCount / MAX_LIGHTS
```

---

### 用途

* 性能分析（Forward 非常重要）

---

# 🎯 七、一个整体理解（非常重要）

---

## 这些模式本质是在回答不同问题：

---

### 🧩 Geometry

```text
模型有没有问题？
```

---

### 🎨 Material

```text
材质输入对不对？
```

---

### 💡 Lighting

```text
光照计算对不对？
```

---

### 🔀 Combined

```text
哪一部分贡献过大？
```

---

### 🌈 Heatmap

```text
数值分布是否合理？
```

---

# 🧠 八、一句话总结

👉 这些 debug mode 本质是：

> **把“渲染拆成每一层”，让你逐层验证**

---

# 🚀 最后一个实战建议（非常关键）

你以后调 bug 按这个顺序：

```text
1. NORMAL → 几何 OK？
2. ALBEDO → 材质 OK？
3. DIRECT_DIFFUSE → 光方向 OK？
4. SPECULAR → BRDF OK？
5. IBL → 环境 OK？
6. HEATMAP → 数值 OK？
```

---

如果你愿意，我可以下一步帮你做一个：

👉 **“一键诊断流程（像医生问诊一样定位 PBR bug）”**

这个会非常实用，能让你快速定位任何渲染问题 🚀
