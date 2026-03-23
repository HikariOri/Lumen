# Demo3D：PBR 与材质系统（实现说明）

本文描述 **`examples/demo3d`** 中与 **`render-engine-features.md`** §7（PBR 渲染）、§7a（材质系统）对齐的**当前实现**：双 Descriptor Set、材质 UBO、贴图槽与标量二选一、Alpha 模式、管线变体、ECS 与 Inspector。正文排版遵循 [guides/chinese-typography.md](../guides/chinese-typography.md)。

**相关文档**：[pbr-ibl-disney-skybox.md](pbr-ibl-disney-skybox.md)（BRDF / IBL / 天空盒原理）、[material-system-ibl-pbr.md](material-system-ibl-pbr.md)（规划与演进）、[render-engine-features.md](render-engine-features.md)（全引擎功能清单）。

---

## 1. 范围与非目标

### 1.1 已实现（与 §7 / §7a 对应）

| 能力 | 说明 |
|------|------|
| 金属–粗糙度主光照 | GGX + 迪士尼风格漫反射子集 + 多光源（方向 / 点 / 聚光） |
| 贴图槽 | BaseColor（sRGB）、Normal、Metallic–Roughness（glTF：G=粗糙，B=金属）、AO（R）、Emissive |
| 标量 / 贴图二选一 | 路径非空则该槽启用贴图并与因子相乘；空则片元仅用 `MaterialUBO` 标量 |
| 线性工作流 | 反照率贴图采样后经 **sRGB → 线性** 再参与光照；MR / 法线 / AO / 自发光为线性 UNORM |
| IBL | 与既有 `envMap` Mipmap + `brdfLUT`、split-sum 一致；强度等在 `SceneUBO.envParams` |
| Alpha | **Opaque**（默认）、**Mask**（`discard` + `alpha_cutoff`）、**Blend**（预乘典型 SrcAlpha / OneMinusSrcAlpha，独立管线） |
| 渲染状态 | **双面**：`VK_CULL_MODE_NONE` 管线；**Blend**：关闭深度写入的透明管线（单 mesh 简化路径） |
| GPU 绑定 | Set 0 场景；Set 1 材质 UBO + 5 张 combined image sampler；Push Constant：`mode` + `modelColor` |
| ECS | `MaterialComponent` + `DrawableTag`；`reload` 从路径加载 `Texture` 并写 Descriptor |
| UI | `SceneInspectorPanel` 材质块：路径、标量、Alpha mode、Cutoff、Double sided |

### 1.2 明确未实现（不要求与本文同步）

- 程序化噪声纹理、噪声在 SSAO / TAA 中的绑定（§6 噪声子项）。
- **SubMesh** 与每子网格材质实例、**同材质合批**。
- **材质序列化 / 反序列化**、资产 ID、磁盘缓存与去重哈希表（规划见 `material-system-ibl-pbr.md` §4.3）。
- **材质资产** 与 **材质实例** 的正式分层（当前以 `MaterialComponent` 为单一数据源）。
- Clearcoat / Sheen / Transmission、切线属性（顶点无 **Tangent**，法线贴图用屏幕空间导数构 TBN）。
- HDR 主色目标、色调映射链路（主 Pass 仍为 LDR 交换链路径下的实现）。

---

## 2. Descriptor Set 布局

### 2.1 Set 0 — `SceneUBO` + 环境

| Binding | 类型 | 说明 |
|---------|------|------|
| 0 | UBO | `SceneUbo`：`model`、`mvp`、`normalMatrix`、`cameraWorld`、`lights[]`、`sceneParams`、`skyMvp`、`skyOrientInv`、`envParams`（`x` 曝光、`y` maxMip、`z` diffMip、`w` IBL 强度） |
| 1 | `samplerCube` | `envMap` |
| 2 | `sampler2D` | `brdfLUT` |

天空盒绘制：绑定 **Set 0**，片元使用 UBO + `envMap`；**不绑定 Set 1**。

### 2.2 Set 1 — 材质

| Binding | 类型 | 说明 |
|---------|------|------|
| 0 | UBO | `PbrMaterialUbo`（见 §3） |
| 1 | `sampler2D` | `albedoMap`（sRGB 格式纹理） |
| 2 | `sampler2D` | `normalMap`（UNORM） |
| 3 | `sampler2D` | `metallicRoughnessMap`（UNORM） |
| 4 | `sampler2D` | `aoMap`（UNORM） |
| 5 | `sampler2D` | `emissiveMap`（UNORM） |

缺贴图时由 **`PbrPlaceholderTextures`**（`engine`）提供 **1×1** 占位图，Descriptor 始终合法；是否**真正采样**由 **材质掩码**（§3）决定。

---

## 3. `PbrMaterialUbo` 与贴图槽掩码

定义见 `engine/include/render/pbr_material_ubo.hpp`，与 `cube.frag` 中 `MaterialUBO` **std140** 对齐（共 **64** 字节）。

| 字段（GLSL 名） | 含义 |
|-----------------|------|
| `baseColorFactor` | 与反照率相乘；无反照率贴图时作为 **线性 RGB** 基础色（`a` 参与 Alpha） |
| `mrAoFactors` | `x` 金属度因子、`y` 粗糙度因子、`z` AO 因子 |
| `emissiveFactor` | 自发光 `rgb` |
| `shaderParams` | `x`：`MaterialAlphaMode`（0 Opaque / 1 Mask / 2 Blend）；`y`：`alpha_cutoff`；`z`：贴图槽 **位掩码**（以 `float` 位拷贝写入 `uint32_t`）；`w` 保留 |

掩码常量与 CPU 侧由路径推导的函数见 `engine/include/render/material_texture_mask.hpp`：

- `kMatTexBitAlbedo`、`kMatTexBitNormal`、`kMatTexBitMetallicRoughness`、`kMatTexBitOcclusion`、`kMatTexBitEmissive`。

片元着色器使用 `floatBitsToUint(shaderParams.z)` 解析；各位为 **1** 时对该槽 **采样并乘因子**，否则仅用标量分支。

---

## 4. `MaterialComponent`（ECS）

定义见 `engine/include/scene/components.hpp`。

除 `base_color_factor`、`metallic_factor`、`roughness_factor`、`ao_factor`、`emissive_factor` 与各 `*_path` 外，另有：

- `MaterialAlphaMode alpha_mode`
- `float alpha_cutoff`
- `bool double_sided`

**约定**：`albedo_path` 为空时不从磁盘加载反照率纹理，材质 UBO 掩码 **不含** Albedo 位，显示依赖 `base_color_factor`（与 Inspector 文案一致）。

---

## 5. 管线变体（`main.cpp`）

| 管线 | 用途 |
|------|------|
| 默认填充 | 背面剔除、无混合、深度写 |
| `pipeline_nocull` | `double_sided == true` 且非 Blend 模式 |
| `pipeline_blend` | `alpha_mode == Blend`：无剔除、**alpha 混合**、**深度不写**（简化透明） |
| 线框 | 独立 `wireframePipeline`，逻辑不变 |

**说明**：透明物体未排序；多透明 mesh 时顺序可能不正确，需后续按深度排序或独立透明 Pass。

---

## 6. 资源与代码索引

| 项目 | 路径 |
|------|------|
| 片元着色（PBR + 掩码 + Alpha） | `examples/demo3d/shaders/cube.frag` |
| 顶点着色 | `examples/demo3d/shaders/cube.vert` |
| 场景 / 材质描述符、管线、每帧 UBO | `examples/demo3d/src/main.cpp` |
| 占位纹理 | `engine/include/render/resource/pbr_placeholder_textures.hpp`、`.cpp` |
| 材质 UBO | `engine/include/render/pbr_material_ubo.hpp` |
| 掩码 | `engine/include/render/material_texture_mask.hpp` |
| Inspector 材质 UI | `engine/src/ui/scene_inspector_panel.cpp` |
| 环境加载（六面 / HDR 等） | `engine/include/render/resource/cubemap_file_loader.hpp` 及实现 |

---

## 7. 与旧文档的差异说明

- [pbr-ibl-disney-skybox.md](pbr-ibl-disney-skybox.md) §2.2 中「单 UBO + 单 Set + 反照率在 binding 1」的描述已过时；**以本文 §2 为准**。
- [material-system-ibl-pbr.md](material-system-ibl-pbr.md) §2「现状摘要」以**规划成文时**为起点；落地结果以**本文**与仓库代码为准。

---

## 8. 后续可衔接工作（不在本文实现范围内）

- 引擎级 **Material 资产 / 实例** 与 **Descriptor 缓存**（路径哈希去重）。
- **Tangent** 顶点流与 MikkTSpace，提升法线贴图质量。
- **透明排序**、可选 **独立 forward / OIT**。
- 与 [render-engine-features.md](render-engine-features.md) 全表对齐的引擎默认渲染器（非仅 Demo3D）。
