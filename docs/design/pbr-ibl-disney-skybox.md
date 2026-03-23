# PBR、IBL、天空盒与迪士尼风格 BRDF（Demo3D）

本文说明 **Lumen** 中在 `examples/demo3d` 落地的 **金属–粗糙度 PBR**、**基于图像的光照（IBL）**、**天空盒**，以及着色器里采用的 **迪士尼原则性 BRDF 中的漫反射项 + 工业界常用的 GGX 镜面项** 的组合。正文排版遵循 [guides/chinese-typography.md](../guides/chinese-typography.md)。

**扩展规划**（每物体材质、环境贴图文件加载、Descriptor 拆分、Inspector 材质块等）见 [material-system-ibl-pbr.md](material-system-ibl-pbr.md)。

---

## 1. 概述

| 能力 | 说明 |
|------|------|
| 直接光照 | 多盏方向光 / 点光 / 聚光，与 `GPULight` UBO 一致 |
| 迪士尼漫反射 | Burley 风格的 `Fd` 项，依赖 `N·L`、`N·V`、`V·H` 与粗糙度 |
| 镜面反射 | GGX 法线分布（Trowbridge–Reitz）、**高度相关**的 Smith 几何项（Heitz）、带粗糙度的 Schlick Fresnel |
| IBL | Epic **split-sum** 近似：预滤波环境立方体贴图 Mipmap + **BRDF 积分 LUT** |
| 天空盒 | 独立管线：深度写入关闭、`LESS_OR_EQUAL`、正面剔除，片段用世界方向采样同一环境贴图 |

**未实现（可后续扩展）**：迪士尼的次表面、清漆、Sheen、各向异性、透射；离线辐照度贴图与镜面卷积管线（当前用 **程序化天空 RGBA8 立方体 + Blit Mipmap** 作近似）。

---

## 2. 架构

### 2.1 数据流与 Pass 顺序

```text
Scene RenderPass（颜色 + 深度）
  ├─ Clear
  ├─ [Lit / Wireframe] 天空盒 DrawIndexed（深度不写，≤ 远平面）
  ├─ 主模型 DrawIndexed（PBR 片段着色器）
  └─ 光源 Gizmo（原有）
```

法线 / 深度调试视口仍只画模型，不画天空盒，便于观察 GBuffer 类调试。

### 2.2 CPU / GPU 资源

| 组件 | 路径或类型 |
|------|------------|
| 环境立方体贴图 | `lumen::render::Texture::create_cubemap_from_rgba8_faces`（引擎） |
| 程序化天空像素 | `examples/demo3d/src/pbr_resources.cpp` → `fill_procedural_sky_faces` |
| BRDF LUT | `generate_brdf_lut_rgba8` → `Texture::create_from_memory`（256² RGBA8，无 Mipmap） |
| 天空几何 | `main.cpp` 中单位立方体 8 顶点 + 36 索引，独立 `VertexBuffer` / `IndexBuffer` |
| 统一 UBO | `struct UBO`：`model`、`mvp`、`normalMatrix`、`cameraWorld`、`lights[]`、`sceneParams`、`skyMvp`、`skyOrientInv`、`pbrParams`、`envParams` |
| 描述符集（set = 0） | binding 0 UBO；1 `sampler2D` 反照率；2 `samplerCube` 环境；3 `sampler2D` BRDF LUT |

天空与主材质 **共用同一 `PipelineLayout` 与 DescriptorSet**，以便绑定一致；天空片元仅使用 binding 0 与 2。

### 2.3 引擎侧立方体贴图

`engine/src/render/resource/texture.cpp`：

- `transition_image_subresource`：支持指定 `baseArrayLayer` / `layerCount`，供立方体各面 Mipmap Blit 使用。
- `generate_mipmaps_cube`：对 6 个面分别做与 2D 纹理类似的 `TRANSFER_DST → SRC → Blit → SHADER_READ_ONLY` 链。
- `Texture::create_cubemap_from_rgba8_faces`：Staging 一次上传 6 面，再生成 Mipmap。

面顺序与 Vulkan 一致：**+X, -X, +Y, -Y, +Z, -Z**。

---

## 3. 原理摘要

### 3.1 金属–粗糙度工作流

- **反照率 `baseColor`**：绝缘体为漫反射色；金属吸收漫反射，镜面着色使用 `baseColor` 作为 `F0`（与 `metallic` 插值）。
- **金属度 `metallic`**：`F0 = mix(0.04, baseColor, metallic)`；漫反射系数 `kD ∝ (1 - F)(1 - metallic)`。
- **粗糙度 `roughness`**：映射到 GGX 的 `α = roughness²`（片元中用于 NDF 与几何项）。

### 3.2 迪士尼漫反射项（实时子集）

采用 Burley 论文中常见的 **Schlick 型 Fresnel 混合**形式：

- `FL = (1 - N·L)^5` 的变体（实现里为 `schlick_fresnel_u(N·L)`）；
- `FV` 同理对 `N·V`；
- `Fd90 = 0.5 + 2·roughness·(V·H)²`；
- `Fd = mix(1, Fd90, FL) · mix(1, Fd90, FV)`；
- 漫反射辐射亮度 ∝ `baseColor / π · Fd · kD · (N·L)`（再乘光源辐射度与衰减）。

这与 **完全版迪士尼 BSDF**（清漆、Sheen、次表面等）不同；镜面支路使用下面 GGX 模型而非 Disney 的 GTR 族，以便与现有 IBL 工具链一致。

### 3.3 镜面项（GGX + 高度相关遮蔽）

- **法线分布**：Trowbridge–Reitz / GGX。
- **几何项**：`V_SmithGGXCorrelated(N·L, N·V, α)`（Filament / Heitz 形式），避免过度暗的独立 `G_Schlick` 乘积在部分角度下的偏差。
- **Fresnel**：`F_SchlickRoughness`，在 Schlick 基础上用 `roughness` 抬升漫反射侧的 `F90`，减少高粗糙时的高光过暗。

### 3.4 IBL（Split Sum）

参考 Epic Games 与 **Learn OpenGL** 等公开资料：

1. **漫反射**：`textureLod(envMap, N, diffuseMip)` 近似 **辐照度**（此处用较高 Mip，非真实 SH / 卷积贴图）。
2. **镜面**：`prefilteredColor = textureLod(envMap, R, roughness * maxMip)`。
3. **BRDF 尺度**：`BRDF_LUT(N·V, roughness)` 存 `(A, B)`，使  
   `specularIBL ≈ prefiltered · (F0 * A + B)`。

LUT 在 `pbr_resources.cpp` 中用 **重要性采样的 GGX** 与 **1024 样本**在 CPU 端预积分（启动时一次）。

### 3.5 天空盒

- 顶点着色器：`gl_Position = (skyMvp * vec4(pos,1)).xyww`，将深度固定在远平面，便于后续物体正常深度测试。
- `skyMvp = projection * mat4(mat3(view))`，去掉平移，盒子始终包围相机。
- `skyOrientInv`：将立方体局部方向变到 **世界方向** 以采样世界空间环境贴图（正交旋转的逆）。

---

## 4. 使用说明

### 4.1 运行 Demo3D

构建目标 `demo3d`，在 **Demo3D** 窗口中：

- **Metallic / Roughness / AO**：`pbrParams`；
- **IBL Strength**：漫反射 + 镜面 IBL 总增益；
- **Env Exposure**：环境贴图与天空盒亮度（`envParams.x`）。

**Lit / Wireframe** 下会先画天空盒再画模型；**Normal / Depth** 下主场景不画天空盒，仅清屏色。

### 4.2 替换为 HDR 或资产立方体贴图

1. 在 CPU 侧准备 **6 面线性颜色** 或 **等距柱状展开**（需先转 6 面）。
2. 调用 `create_cubemap_from_rgba8_faces` 上传；若为 HDR，可扩展为 `RGBA16_SFLOAT` 并同步修改上传路径（当前 API 为 RGBA8）。
3. 若要做 **正确镜面 IBL**，建议增加离线或运行时 **镜面预滤波**（按粗糙度过滤），而不是仅依赖 Box Blit 的 Mipmap（当前为轻量近似）。

### 4.3 与引擎其他模块的边界

- **渲染核心**仅增加 **通用立方体贴图上传**；游戏 / 编辑器逻辑仍在示例与 `scene` 中。
- 材质系统、延迟渲染、自动曝光等见 [render-engine-roadmap.md](render-engine-roadmap.md)。

---

## 5. 关键文件索引

| 文件 | 作用 |
|------|------|
| `engine/include/render/resource/texture.hpp` | `create_cubemap_from_rgba8_faces` 声明 |
| `engine/src/render/resource/texture.cpp` | 立方体上传与 Mipmap |
| `examples/demo3d/src/pbr_resources.hpp/.cpp` | 程序化天空、BRDF LUT |
| `examples/demo3d/shaders/cube.frag` | PBR + IBL 主片元 |
| `examples/demo3d/shaders/skybox.vert/.frag` | 天空盒 |
| `examples/demo3d/src/main.cpp` | UBO、描述符、管线、绘制顺序、ImGui |
| `examples/demo3d/CMakeLists.txt` | 编译 `pbr_resources.cpp` 与 `skybox` 着色器 |

---

## 6. 参考

- Burley, *Physically-Based Shading at Disney*（迪士尼原则性模型与漫反射项）。
- Walter et al., *Microfacet Models for Refraction through Rough Surfaces*（GGX / Smith 几何）。
- Karis, *Real Shading in Unreal Engine 4*（split-sum IBL 与 BRDF 积分表）。

以上文献与开源实现（Filament、Learn OpenGL IBL 章节）可与本仓库着色器对照阅读。
