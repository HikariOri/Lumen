# 材质系统、IBL 与材质/环境面板 — 设计与实现计划

本文规划 **Lumen** 在 **金属–粗糙度 PBR**、**IBL（基于图像的光照）** 与 **ImGui 材质/环境编辑面板** 上的数据模型、GPU 布局、着色器扩展与分阶段落地顺序，作为实现前的单一事实来源。正文排版遵循 [guides/chinese-typography.md](../guides/chinese-typography.md)。

**相关文档**：[pbr-ibl-disney-skybox.md](pbr-ibl-disney-skybox.md)（当前 Demo3D 着色模型与 IBL 近似）、[vulkan-materials.md](vulkan-materials.md)（Effect / Material / Descriptor 分层）、[ui-panels.md](ui-panels.md)（面板架构）、[render-engine-roadmap.md](render-engine-roadmap.md)。

---

## 1. 目标与范围

### 1.1 目标

| 能力 | 说明 |
|------|------|
| **材质系统（数据 + GPU）** | 每物体可配置 PBR 参数与贴图槽；与 Scene / ECS 组件对齐；描述符与管线布局可扩展、可缓存。 |
| **IBL** | 环境用 **立方体贴图**（与现有 `samplerCube envMap` 一致）；支持 **从文件加载**（六面目录 **或** 单张 **等距柱状 `.hdr`**，见 §7）；保留 **程序化天空** 作默认/回退。 |
| **PBR 光照** | 延续现有 **迪士尼风格漫反射（Burley 子集）+ GGX 镜面 + 多光源**；扩展 **法线贴图、金属/粗糙度/AO（及可选 ORM 打包）、自发光** 等工业界常见工作流。 |
| **UI** | **材质面板**：编辑选中物体的材质参数与贴图路径/预览；**环境（IBL）面板**：加载/切换环境立方体贴图、曝光、IBL 强度等与 `envParams` / 全局环境一致。 |

### 1.2 非目标（当前阶段明确不做或后置）

- 迪士尼完整 BSDF（清漆、Sheen、次表面、各向异性等）。
- 延迟渲染 / GBuffer 材质系统（可与本文并行规划，实现阶段独立）。
- 运行时 **HDR 立方体 + 正确镜面预滤波卷积**（可作为 Phase C，见 §9.3）。
- Bindless / GPU-driven 材质（见 [vulkan-materials.md](vulkan-materials.md) 演进路线）。

---

## 2. 现状摘要（Demo3D）

以下描述实现本文档时的起点，便于 diff 心智模型。

- **着色器**（`examples/demo3d/shaders/cube.frag`）：单张 **反照率** `sampler2D`；`pbrParams`（metallic, roughness, ao, iblStrength）与 **全局** `envParams`（exposure, maxMip, diffMip）在 **UBO**；IBL 为 **split-sum** + `samplerCube` + `brdfLUT`。
- **CPU**：`main.cpp` 中全局 `pbr_metallic` 等与 ImGui 调试滑条绑定；**无** `MaterialComponent`；**无** 每物体描述符。
- **环境贴图**：`pbr_resources.cpp` 程序化 RGBA8 立方体 + `Texture::create_cubemap_from_rgba8_faces`；BRDF LUT CPU 预积分。
- **Inspector**：`SceneInspectorPanel` 含 Transform / Light / Drawable 等，**无** 材质块。

**差距**：材质与 IBL 均为「全局一份」，无法按物体换贴图/参数，也无法从资产路径加载环境图。

---

## 3. 设计原则

1. **与 [vulkan-materials.md](vulkan-materials.md) 对齐**：**Effect（管线 + Layout）** 少实例；**Material 实例** 持贴图引用与参数；Draw 路径使用解析后的 **pipeline + descriptor set + buffer**。
2. **按更新频率拆 Descriptor Set**：**Set 0** 每帧 / 全局（相机、灯光、**环境 + BRDF LUT**、可选场景 UBO）；**Set 1** 每材质（反照率、法线、PBR 遮罩、自发光等）。避免单 Set 过大，且与 Validation 的绑定上限兼容。
3. **占位资源**：缺贴图时绑定 **1×1 默认纹理**（白反照率、平直法线、金属 0 / 粗糙 1 等），避免未绑定采样器。
4. **引擎 vs 示例边界**：**通用能力**（纹理加载、cube 上传 API、默认贴图）宜在 `engine`；**Demo3D 专用**（具体 UI 布局、资产路径）留在 `examples/demo3d`，必要时再抽「编辑器模块」。

---

## 4. 数据模型（CPU / ECS）

### 4.1 `MaterialComponent`（建议）

挂在可绘制实体上，与 `DrawableTag` 共存。字段可分阶段落地：

| 字段 | 类型（建议） | 说明 |
|------|----------------|------|
| `base_color_factor` | `glm::vec4` | 乘到反照率贴图；默认 `(1,1,1,1)`。 |
| `metallic_factor` | `float` | 与贴图通道相乘；默认 `1`。 |
| `roughness_factor` | `float` | 同上；默认 `1`。 |
| `emissive_factor` | `glm::vec3` | 自发光强度；默认 `0`。 |
| `albedo_path` | `std::string` | 反照率贴图路径（sRGB）；空则用占位。 |
| `normal_path` | `std::string` | 切线空间法线；空则平直。 |
| `metallic_roughness_path` | `std::string` | 可选单张 **B=金属, G=粗糙**（glTF 风格）；空则用常数或下栏 `ao_path`。 |
| `ao_path` | `std::string` | 可选 Ambient Occlusion；若与 MR 合并为 ORM 可逐步废弃独立 AO 贴图。 |
| `emissive_path` | `std::string` | 可选。 |

**注意**：字符串路径适合编辑器与序列化；运行时 Draw 前应解析为 `Texture*` 或材质缓存中的句柄，避免每帧读盘。

### 4.2 全局环境 `SceneEnvironment` / `IBLSettings`（建议）

不绑定单一实体，由 `Demo3D` 或后续 `Renderer` 持有：

| 字段 | 说明 |
|------|------|
| `env_cubemap_path` | 可选；标识 6 面资源或目录约定（见 §7）。空则程序化天空。 |
| `exposure` | 对应现有 `envParams.x`。 |
| `ibl_strength` | 可与全局 `pbrParams.w` 合并或保留在环境块中（二选一做单一数据源，避免重复）。 |
| `brdf_lut` | 仍为全局一张；与材质无关。 |

### 4.3 材质缓存（建议）

- `MaterialGpuHandle`：`VkDescriptorSet`（set 1）+ 可选 `Texture` 拥有者或共享池索引。
- 按 **哈希（路径集合 + sampler 配置）** 去重，减少重复上传与 descriptor 分配。

---

## 5. GPU 资源与 Descriptor 布局

### 5.1 目标布局（与 §3 一致）

**Set 0 — Frame / Scene（与现有 UBO 对齐并瘦身）**

| Binding | 类型 | 内容 |
|---------|------|------|
| 0 | UBO | `model`, `mvp`, `normalMatrix`, `cameraWorld`, `lights[]`, `sceneParams`, `skyMvp`, `skyOrientInv`；**每材质** scalar 建议迁出至 Set 1 或 Push Constant（见下）。 |
| 1 | `samplerCube` | `envMap` |
| 2 | `sampler2D` | `brdfLUT` |

**Set 1 — Material**

| Binding | 类型 | 说明 |
|---------|------|------|
| 0 | UBO 或 Push Constants | `baseColorFactor`, `metallic`, `roughness`, `ao`, `emissive` 等；若体积小可用 push constant，否则用小型 UBO。 |
| 1 | `sampler2D` | 反照率（sRGB） |
| 2 | `sampler2D` | 法线（线性，UNORM） |
| 3 | `sampler2D` | 金属粗糙（线性）；未使用时占位 |
| 4 | `sampler2D` | AO 或 ORM 第三通道；可阶段二再分绑 |
| 5 | `sampler2D` | 自发光（可选） |

实际 binding 数量可按 **MVP（反照率 + 法线 + 合并 MR）** 先减后增，但应在文档中固定一版 **「PBR 完整槽位」** 再实现，避免反复改 layout。

### 5.2 管线布局

- `VkPipelineLayout`：`setLayouts = { set0_layout, set1_layout }`，`pushConstantRange` 保留 **渲染模式**（Lit / Normal / Depth 等）与 **tint** 等每 Draw 极小数据。
- **天空盒**：继续仅用 Set 0 的 binding 0 + 1（与现有一致），Set 1 可不绑（shader 不访问）。

### 5.3 与多帧飞行（frames in flight）

- Set 0 的 UBO 保持 **每帧一份**（现有 `uniformBuffers[i]`）。
- Set 1 若仅随材质变、不每帧改，可 **每材质一份** descriptor，多帧 **共享** 同一 `VkDescriptorSet`（注意：仅当材质数据不在该 set 的 UBO 里每帧被覆盖；若每材质 UBO 每帧更新，则需每帧 × 每材质或动态 offset）。

**推荐 MVP**：材质参数放 **push constant 或小 UBO 单缓冲 + dynamic offset**；贴图用静态 `VkDescriptorSet`，多帧共享。

---

## 6. 着色器（PBR 特性集）

### 6.1 法线贴图

- 顶点阶段输出 **切线空间** 基（T, B, N）或在片元用 `dFdx/dFdy` 构造 TBN（MVP 可先选一种）。
- 法线贴图采样后 `normal = normalize(TBN * (texture * 2 - 1))`，再参与现有 `NdotL` / IBL 方向。

### 6.2 金属度 / 粗糙度 / AO

- 从 **合并纹理** 或 **独立纹理** 读通道，与 **scalar factor** 相乘。
- 与现有 `roughness` clamp 下限（如 `0.04`）保持一致，避免除零。

### 6.3 自发光

- `emissive = texture(emissiveMap).rgb * emissiveFactor`，**加到** 直接光 + IBL 之后（或仅 Lit 模式）。

### 6.4 直接光与 IBL

- **直接光**：沿用 `accum_light_pbr` 与 `GPULight` 布局。
- **IBL**：沿用 `pbr-ibl-disney-skybox.md` 的 split-sum；`envMap` 与 `brdfLUT` 来自 Set 0。

---

## 7. IBL / 环境光贴图加载

### 7.1 格式与 API（分阶段）

| 阶段 | 内容 |
|------|------|
| **Phase A** | 继续 **RGBA8 / SRGB** 六面加载：`Texture::create_cubemap_from_rgba8_faces`；**从磁盘 6 张图** 或 **单目录约定** 的封装为 `load_cubemap_from_face_files`（引擎）。 |
| **Phase B（部分已落地）** | **等距柱状 Radiance `.hdr`**：`stbi_loadf` → CPU 双线性采样 → **`Texture::create_cubemap_from_rgba32f_faces`**（`VK_FORMAT_R32G32B32A32_SFLOAT` + Mipmap Blit）。API：`load_cubemap_from_hdr_equirectangular_file`。**未做**：EXR、GPU 立方体卷积、RGBA16F 以省带宽。 |
| **Phase C** | 镜面 **预滤波环境贴图**（替代纯 Mipmap Blit），与粗糙度 mip 严格对应。 |

### 7.2 文件约定（建议文档化到 README 或资源目录）

- **六面 LDR**：`assets/environments/<name>/px.png, nx.png, …, nz.png`（或 `.jpg`；名称与 [pbr-ibl-disney-skybox.md](pbr-ibl-disney-skybox.md) 中 +X,-X,+Y,-Y,+Z,-Z 一致）。
- **单张 HDR**：`SceneEnvironment::cubemap_directory` 可填 **单个 `.hdr` 文件路径**（等距柱状，Y 为上、方位与上述 CPU 转换一致）。扩展名大小写不敏感。`.exr` 当前未支持（非 stb 默认能力）。

### 7.3 切换环境时的 GPU 行为

- 释放旧 `Texture` 前 **`device wait idle`** 或依赖帧同步，避免仍在采样的 image 被销毁。
- 更新 **所有帧** 的 Set 0 binding 1（`envMap`），或统一走 **单一全局 descriptor** 仅更新一次（若与多帧 UBO 分离更清晰）。

---

## 8. UI：材质面板与环境面板

### 8.1 放置位置

- **方案 A（推荐 MVP）**：扩展 `SceneInspectorPanel`：选中实体且含 `MaterialComponent` 时显示 **「Material」** 折叠块（与 Light / Transform 并列）。
- **方案 B**：新增 `MaterialPanel` / `EnvironmentPanel` 实现 `IPanel`，由 `PanelManager` 注册；适合环境为「无选中实体」的全局设置。

环境（IBL）与场景全局相关，**至少一处**应在无实体选中时也可编辑 → **环境面板**独立窗口或与 **Scene / Settings** 合并。

### 8.2 控件清单（建议）

**材质（Inspector 或 Material 窗口）**

- 反照率：`ColorEdit3`（factor）+ 文件路径 `InputText` + 「加载」按钮；可选缩略图（`ImGui::Image` + `imgui_backend_add_texture`）。
- 金属度、粗糙度、AO：滑条 + factor。
- 法线、MR、AO、自发光：路径 + 加载。
- 「重置为默认贴图」。

**环境**

- 当前环境名 / 路径；「从目录加载六面图」；曝光、IBL 强度（与 shader 统一字段）。
- 「恢复程序化天空」。

### 8.3 与 [ui-panels.md](ui-panels.md) 一致

- 所有绘制在 `imgui_backend_new_frame` 之后、`imgui_backend_render` 之前；需要 `Dock` 时沿用 `PanelManager.set_default_dock_id`。

---

## 9. 分阶段实现清单

### Phase 0 — 文档与常量

- [x] 本文档入库；`docs/README.md` 增加索引链接。
- [ ] 在 `pbr-ibl-disney-skybox.md` 顶部增加「扩展实现见 material-system-ibl-pbr.md」互链（可选）。

### Phase 1 — ECS 与全局环境加载

- [ ] 新增 `MaterialComponent`（最小：albedo 路径 + metallic/roughness/ao scalar）。
- [ ] 新增 `SceneEnvironment`（或 Demo3D 内结构体）+ 从 **六面 PNG** 加载立方体；ImGui **环境** 区块切换程序化 / 文件。
- [ ] 资源生命周期与 `wait_idle` 约定写进代码注释（对齐 §7.3）。

### Phase 2 — Descriptor 拆分与 Set 1

- [ ] 新建 Set 0 / Set 1 layout；`cube.frag` / `cube.vert` 更新 `layout(set=…)`。
- [ ] 每材质分配 Set 1；默认占位纹理。
- [ ] Draw 时 `vkCmdBindDescriptorSets` 两次（或使用 `firstSet` 偏移）。

### Phase 3 — 着色器 PBR 扩展

- [ ] 法线贴图 + TBN。
- [ ] 金属粗糙贴图（+ AO 通道约定）。
- [ ] 自发光（可选）。

### Phase 4 — Inspector / 面板整合

- [ ] Inspector 材质块与文件对话框（若引入原生文件对话框需平台抽象；MVP 可手输路径）。
- [ ] 可选独立 **Environment** 窗口。

### Phase 5 — 缓存与 polish

- [ ] 材质 GPU 缓存去重；重复路径不重复创建 `Texture`。
- [ ] 错误路径日志与 ImGui 提示。

---

## 10. 风险与注意事项

- **Descriptor 池容量**：Set 1 每材质一套时，池大小需按「最大材质数 × 绑定数」估算，避免 `VK_ERROR_OUT_OF_POOL_MEMORY`。
- **sRGB**：反照率、环境用 **SRGB** 格式；法线、金属粗糙、LUT 用 **UNORM 线性**。
- **同步**：切换环境或替换材质大贴图时遵守 GPU 空闲或帧栅栏。
- **与 RenderGraph**：当前 Demo3D 若用 RenderGraph 执行主 Pass，材质绑定应在 **记录命令缓冲** 前完成，与 Pass 节点职责划分清楚。

---

## 11. 参考

- 仓库内：[pbr-ibl-disney-skybox.md](pbr-ibl-disney-skybox.md)、[vulkan-materials.md](vulkan-materials.md)、[ui-panels.md](ui-panels.md)。
- 外部：glTF 2.0 金属粗糙度材质；Learn OpenGL PBR；Filament 材质文档。

实现完成后，应更新本文 **§2 现状** 与 **§9 勾选**，并视情况在 [render-engine-features.md](render-engine-features.md) 中勾选对应条目。
