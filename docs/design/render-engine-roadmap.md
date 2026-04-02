# Vulkan 渲染引擎 — 总体规划与功能文档

本文档为基于 Vulkan 的渲染引擎提供整体规划、架构设计、功能特性清单与开发阶段建议，便于按阶段实现 UI、PBR 及更多高级特性。配套清单与阶段拆解见 [render-engine-features.md](render-engine-features.md)。

**目录**：1 概述与目标 → 2 技术栈 → 3 架构设计 → 4 渲染管线 → 5 UI → 6 功能特性清单 → 7 目录结构 → 8 开发阶段 → 9 风险与注意事项 → 10 文档与后续

---

## 1. 项目概述与目标

### 1.1 定位

- **类型**：实时 3D 渲染引擎（可嵌入游戏/工具/编辑器）
- **图形 API**：Vulkan 1.4（使用 Properties2/Features2 链，支持 descriptor indexing、timeline semaphore 等）
- **目标平台**：Windows（主）、可选 Linux / macOS
- **语言**：**C++23**（与根目录 `CMakeLists.txt` 中 `CMAKE_CXX_STANDARD` 一致），着色器 GLSL 或 HLSL（经 glslang/glslc 或 DXC 编译）

### 1.2 核心目标

| 目标 | 说明 |
|------|------|
| **PBR 渲染** | 基于物理的材质（金属/粗糙度工作流），支持 IBL、多光源 |
| **可扩展 UI** | 编辑器/调试 UI（如 ImGui），与 Vulkan 后端深度集成 |
| **功能丰富** | 阴影、后处理、抗锯齿、LOD、实例化、GPU 粒子等 |
| **可维护** | 清晰分层、模块化、便于后续加 Vulkan 2 / 扩展 |

---

## 2. 技术栈与依赖

### 2.1 建议依赖

| 类别 | 库/工具 | 用途 |
|------|---------|------|
| 图形 API | Vulkan SDK (1.4) | 实例、设备、管线、描述符等 |
| 窗口与输入 | SDL3 | 窗口、输入、Vulkan Surface 创建 |
| 数学 | glm | 向量、矩阵、四元数 |
| UI | Dear ImGui + ImGui Vulkan 后端 | 编辑器/调试 UI、面板、拾取 |
| 模型加载 | tinyobjloader / Assimp | OBJ、FBX、glTF 等 |
| 图片 | stb_image / libpng | 纹理加载、HDR |
| 噪声纹理 | 程序化生成（CPU/GPU）或预烘焙 PNG | SSAO、TAA、抖动、蓝噪声；柏林/Simplex 等用于地形与程序化效果 |
| 图片写出 | stb_image_write / libpng | 导出 PNG/JPG、HDR 截图 |
| 视频编码 | FFmpeg (libavcodec/libavformat) 或 OpenCV VideoWriter | 帧序列编码为 MP4/WebM 等 |
| 着色器编译 | glslang / glslc (Vulkan SDK) 或 DXC | GLSL → SPIR-V，可选 HLSL |
| 序列化/配置 | nlohmann/json 或 yaml-cpp | 场景/材质/配置 |
| 字体 | stb_truetype + ImGui 或 FreeType | UI 与文本渲染 |
| 可选 | ImGuizmo | 场景内 Gizmo（平移/旋转/缩放） |
| 可选 | ImGui Node Editor | 节点式材质/蓝图编辑器 |

---

## 3. 架构设计

### 3.1 分层结构（自上而下）

```
┌─────────────────────────────────────────────────────────────┐
│  应用层 (Application)                                        │
│  - 编辑器 / 游戏主循环 / 场景加载 / 配置                      │
├─────────────────────────────────────────────────────────────┤
│  场景与逻辑层 (Scene / Gameplay)                             │
│  - Scene, GameObject, Component, Transform, Camera, Light   │
│  - 材质/网格/渲染器组件、脚本（可选）                         │
├─────────────────────────────────────────────────────────────┤
│  渲染抽象层 (Render Abstraction)                             │
│  - Renderer, RenderPass, MeshBatch, MaterialInstance         │
│  - 不直接依赖 Vulkan 句柄，便于以后换 API                    │
├─────────────────────────────────────────────────────────────┤
│  Vulkan 实现层 (Vulkan Backend)                              │
│  - VkContext, VkPipeline, VkBuffer, VkImage, VkDescriptor   │
│  - 命令缓冲、同步、内存管理、描述符集                        │
├─────────────────────────────────────────────────────────────┤
│  平台层 (Platform)                                           │
│  - Window/Input 抽象 + SDL3 后端, 文件系统, 线程/任务         │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 平台抽象：SDL3

窗口与输入采用**抽象接口 + SDL3 后端**。

- **抽象接口（应用只依赖此层）**
  - **窗口**：创建/销毁、尺寸与分辨率、是否全屏、标题；获取供 Vulkan 创建 Surface 的句柄（如 `VkSurfaceKHR` 所需平台句柄）。
  - **输入**：轮询或事件回调、键/鼠/手柄状态、窗口焦点与关闭请求。
  - **Surface 创建**：Vulkan 的 `vkCreateXXXSurfaceKHR` 在 SDL3 下用 `SDL_GetVulkanInstanceExtensions`、`SDL Vulkan_CreateSurface` 等 API 创建 Surface；接口层提供“创建当前窗口的 VkSurfaceKHR”。
- **SDL3 后端**
  - `Window`/`Input` 的 SDL3 实现，内部用 SDL3 窗口与事件 API；支持音频、游戏手柄、多平台一致性。

### 3.3 核心模块划分

| 模块 | 职责 | 主要类/组件 |
|------|------|-------------|
| **Core** | 引擎生命周期、配置、日志、全局服务定位 | Engine, Config, Logger |
| **Platform** | 窗口与输入抽象；SDL3 后端，创建 Vulkan Surface | IWindow, IInput, WindowConfig；SDL3Backend |
| **VkContext** | Instance、PhysicalDevice、Device、Queue、Swapchain | `Context`（`context.hpp`）、Swapchain |
| **VkMemory** | GPU 内存（VMA）、Buffer/Image/Texture 封装、staging、上传 | `VmaAllocator`（`Context::vma_allocator()`）、`Buffer`、`Image`、`Texture` |
| **VkPipeline** | 管线布局、Graphics/Compute 管线、缓存 | Pipeline (重写), PipelineCache |
| **VkDescriptor** | 描述符池、布局、集、绑定 | DescriptorPool, DescriptorSetLayout |
| **RenderPass** | 渲染通道定义、子通道、附件、依赖 | RenderPass, Framebuffer |
| **Scene** | 场景图、GameObject、Transform、Camera、Light | Scene, GameObject, Transform, Camera, Light |
| **Mesh/Material** | 网格数据、材质参数、贴图句柄 | Mesh, Material, TextureManager |
| **Material System** | 材质资产、实例、Alpha/渲染状态、GPU 绑定与管线变体 | MaterialAsset, MaterialInstance, 材质 UBO/Descriptor |
| **PBR** | PBR 着色器、UBO、贴图绑定、IBL | PBRPipeline, IBL (cubemap/BRDF LUT) |
| **Shadow** | 阴影图、CSM/Spot/Point 阴影 | ShadowMap, ShadowPass |
| **PostProcess** | 后处理链（Bloom、TAA、色调映射等） | PostProcessPass, PostStack |
| **Offscreen** | 离屏渲染目标、无窗口渲染、多视口/多分辨率 | OffscreenTarget, HeadlessContext |
| **Export** | 帧缓冲读回、导出为图像文件与视频文件 | FrameExporter, ImageExporter, VideoRecorder |
| **UI** | ImGui 初始化、Vulkan 后端、字体、渲染 | UIRenderer, ImGuiBackend |
| **Asset** | 模型/纹理/着色器加载与热重载（可选） | AssetManager, Loaders |

---

## 4. 渲染管线设计

### 4.1 帧流程概览

```
[ 同步：等待上一帧 GPU 完成 ]
    ↓
[ 获取下一帧 Swapchain 图像、Fence/Semaphore ]
    ↓
[ 1. Shadow Pass(es) ]          → 阴影图 (depth / variance 等)
    ↓
[ 2. GBuffer Pass (可选) ]      → Albedo, Normal, PBR (M/R/AO), Depth
    ↓
[ 3. Main Deferred / Forward ]  → 光照计算、PBR 着色
    ↓ 或
[ 3'. Forward Opaque ]          → 不透明物体 PBR
[ 3''. Sky / IBL ]              → 天空盒、环境反射
[ 3'''. Transparent ]           → 透明物体（按深度排序）
    ↓
[ 4. Post-Process ]             → Bloom, Tone Mapping, FXAA/TAA, 等
    ↓
[ 5. UI Pass ]                  → ImGui 等
    ↓
[ Present ]
```

### 4.2 PBR 管线要点

- **工作流**：金属/粗糙度（Metallic-Roughness），材质含 BaseColor、Metallic、Roughness、贴图槽等。
- **贴图**：BaseColor, Metallic, Roughness, Normal, AO，可选 Emissive；支持无贴图时用 uniform 参数。
- **光照**：方向光 + 多盏点光/聚光；BRDF：Cook-Torrance（GGX NDF、Smith G、Fresnel-Schlick）。
- **IBL**：预滤波环境贴图（镜面）+ 辐照度贴图（漫反射）+ BRDF LUT 二维贴图。
- **实现层次**：先单方向光 + 简单环境光，再加 IBL 与多光源。

### 4.2a 材质系统（Material System）

材质系统负责**资产定义、运行时实例、GPU 绑定与管线选择**，与 PBR 管线配合使用。

- **材质资产（Material Asset）**
  - **PBR 参数**：BaseColor（vec4）、Metallic、Roughness、AO、Emissive（color + intensity）；每项可为**标量/向量**或**贴图槽**，无贴图时使用标量（含默认值）。
  - **贴图槽**：BaseColor、Metallic、Roughness（可与 AO 合并为 MR/AO 图）、Normal、AO、Emissive；每槽可选贴图资源 + 采样选项（UV 缩放/偏移、法线 Y 翻转等）。
  - **Alpha 模式**：Opaque（不透明）、Mask（Alpha Test，可配置 cutoff）、Blend（透明混合）；决定渲染顺序与深度/混合状态。
  - **渲染状态**：背面剔除/双面、深度写/深度测试、混合模式（Blend 时）、可选 Stencil；由材质类型或显式配置驱动 Pipeline 的 Vulkan 状态。
  - **着色模型/类型**：默认 PBR（金属/粗糙度）；可选 Unlit、Toon、自定义等，对应不同 Shader 或变体。
  - **序列化**：材质定义可存为 JSON/二进制，便于加载、版本管理与热重载（可选）。
- **材质实例（Material Instance）**
  - 运行时由资产派生的实例，可**覆盖部分参数**（如只改 BaseColor、Roughness）而不复制整份资产；同一资产可有多实例（如不同颜色的球）。
  - 与**网格/子网格**绑定：每 SubMesh 指定一个 Material Instance；Draw Call 按材质/实例分组便于合批。
- **GPU 侧对接**
  - **UBO**：材质参数（BaseColor、Metallic、Roughness 等）打包为 per-draw 或 per-material UBO，或使用 Push Constant（小数据）。
  - **DescriptorSet**：每材质或每实例绑定一套贴图 + Sampler；可池化、按帧或按批次更新；若贴图多可考虑纹理数组或 Bindless（可选）。
  - **管线选择**：按 Alpha 模式、双面、是否用法线贴图等生成或选择 Pipeline/Shader 变体（Specialization Constant 或预编译多版本）。
- **批处理与性能**
  - 同材质/同实例的物体可合并 Draw Call（instancing 或 merge buffer）；贴图尽量复用，减少 DescriptorSet 与 Pipeline 切换。
  - 材质变更时正确更新 Descriptor 与 UBO，避免每帧全量更新可静态部分。
- **扩展（可选）**
  - **Clearcoat / Sheen / Transmission**：PBR 扩展，增加参数与贴图槽及对应 Shader 逻辑。
  - **材质图/节点编辑器**：用节点连接贴图与参数，再烘焙为材质资产或运行时解析（工作量大，可选）。
  - **LOD 材质**：不同 LOD 使用不同材质或贴图分辨率，由 LOD 系统选择。

### 4.2b 色彩管线与 HDR

- **线性空间**：光照与 PBR 在线性空间计算；贴图若为 sRGB（如 BaseColor）则采样时转线性，输出到显示前再转回 sRGB。
- **HDR 渲染目标**：主 Color 与后处理链使用 HDR 格式（如 R16G16B16A16_SFLOAT），便于 Bloom 与正确色调映射。
- **色调映射与曝光**：后处理中 Tone Mapping（Reinhard/ACES 等）+ 可调曝光，再输出到 sRGB Swapchain 或 8bit 离屏图。
- **一致性**：贴图 sRGB 约定、Swapchain 格式（sRGB 或非 sRGB）与 Present 时是否需要线性→sRGB 转换需统一，避免过曝或发灰。

### 4.3 阴影

- **方向光**：级联阴影贴图（CSM）或单张 large shadow map。
- **点光/聚光**：可选 cubemap 或 2D 阴影图。
- **技术**：PCF、VSM、ESM 等可选，先实现硬阴影 + PCF。

### 4.4 抗锯齿与后处理

- **抗锯齿**：先 MSAA（若用 Forward），或 TAA/FXAA 在后处理中。
- **后处理链**：Bloom、色调映射（Reinhard / ACES）、可选 SSAO、泛光、色差、vignette 等。

### 4.4a 噪声纹理与程序化噪声（Noise Textures & Procedural Noise）

- **用途**：
  - **SSAO**：随机旋转/偏移噪声（如 4×4 小图），在 SSAO 中用于采样方向随机化，避免带状伪影。
  - **TAA / 时序抗锯齿**：抖动采样、蓝噪声（Blue Noise）贴图，用于子像素偏移或采样序列。
  - **抖动（Dithering）**：低精度下减少色带，如 Bayer 矩阵或程序化噪声。
  - **程序化效果**：云、雾、**地形高度/细节**、法线扰动、胶片颗粒等，可作为 2D 或 3D 噪声。
- **噪声类型与算法**：
  - **柏林噪声（Perlin Noise）**：经典梯度噪声，平滑、可重复；适合地形高度场、云层、有机形态；2D/3D 常用，可多 octave 叠加（FBM）控制细节层次。
  - **Simplex 噪声**：Perlin 的改进版，计算更省、方向性更均匀；同样适合地形、云、材质细节；2D/3D/4D 均有实现。
  - **Worley / Voronoi**：胞元噪声，适合细胞、岩石裂纹、水面波纹、某些材质细节。
  - **Value 噪声**：比 Perlin 简单，可用于混合或低成本效果。
  - **蓝噪声（Blue Noise）**：低差异序列，用于采样、TAA、路径追踪等，通常预烘焙为贴图。
- **实现方式**：
  - **预烘焙**：构建时或工具（如 `tools/generate_noise.cpp`）生成 PNG/R32G32B32A32 等，放入 `assets/textures/noise/`；适合 SSAO 旋转噪声、蓝噪声、或离线地形高度图。
  - **运行时 CPU**：Perlin/Simplex/Worley 等库在 CPU 生成像素或高度值，再上传 VkImage 或写入顶点/Heightmap；适合地形块按需生成、可复现（固定 seed）。
  - **运行时 GPU（Compute Shader）**：在 GPU 上实现 Perlin/Simplex 等，直接写入 Texture 或 Buffer；适合大尺寸、每帧变化或与地形/粒子等管线统一。
- **格式与尺寸**：SSAO 常用小图（如 4×4 RG8 或 16×16）；蓝噪声 64×64～256×256；地形/程序化效果可按 chunk 或全局尺寸（如 256×256～1024×1024）生成 R16/R32 高度或 RGBA。
- **与管线对接**：作为只读贴图在对应 Pass 的 DescriptorSet 中绑定；地形生成则多与 Heightmap → Mesh 或 Compute 管线配合（见下节）。

### 4.4b 程序化地形生成（Procedural Terrain）

- **定位**：基于噪声（以柏林/Simplex 为主）生成高度场，再生成网格、法线、可选 LOD 与分块，用于开放场景、关卡原型、预览等。
- **核心流程**：
  - **高度场**：2D 噪声（Perlin/Simplex 多 octave、可选 Worley 混合）按世界 XZ 采样，得到高度 Y；可加噪声层（细节层、侵蚀感、平坦区）与曲线重映射（如 smoothstep）控制形状。
  - **种子与尺度**：使用 seed 保证同一配置可复现；frequency/scale、octave 数、persistence、lacunarity 等参数可配置，便于美术调参或运行时切换“世界”。
  - **从高度到网格**：由高度图生成规则网格顶点（Position + Normal + UV）；法线可由高度差近似或从高度图导数计算。可选：切线空间、顶点色（如按高度着色）。
  - **分块（Chunk）**：大世界按块（如 64×64 或 128×128 顶点）生成与加载，仅保留相机附近块，远处卸载或降 LOD；块边界需无缝（共享边、高度一致）。
  - **LOD**：按与相机距离切换块分辨率（如 3～4 级）或网格密度，减少三角形数；可配合 HLOD 或简单距离剔除。
- **与渲染对接**：
  - 生成结果：每块为独立 Mesh（顶点/索引 Buffer），或合并为一大 Buffer 多 Draw；材质可用单张贴图或程序化（噪声驱动颜色/粗糙度）。
  - 阴影与碰撞：同一高度数据可用于生成阴影贴图采样或简单碰撞/射线检测（可选）。
- **实现层次**：
  - **最小**：单块固定尺寸高度图（CPU Perlin/Simplex）→ 网格 → 渲染；无分块、无 LOD。
  - **推荐**：分块 + 按需生成 + 参数化（seed、scale、octave）；噪声在 CPU 或 Compute 中生成。
  - **可选**：GPU 地形（Compute 生成高度 → 顶点缓冲）、曲面细分（Tessellation）按视距细分、纹理混合（多套 splat 贴图按高度/斜率混合）。

### 4.5 离屏渲染（Off-Screen Rendering）

- **用途**：无窗口渲染（批处理/服务器）、缩略图/预览、多视口、固定分辨率输出、导出图像/视频的稳定帧源。
- **实现要点**：
  - **离屏目标**：使用 VkImage（非 Swapchain）作为 Color/Depth 附件，搭配 VkFramebuffer 与 RenderPass；分辨率、格式（如 R8G8B8A8_UNORM、R16G16B16A16_SFLOAT）可独立于窗口。
  - **无窗口/无 Surface**：若需完全无窗口（headless），可用 `VK_KHR_display` 或创建不可见窗口 + 常规 Surface，或使用 `VK_EXT_headless_surface`（若可用）；多数情况下“离屏”指渲染到自有 Image 而非 Swapchain，窗口仍可存在。
  - **与主管线复用**：Shadow、GBuffer、主场景、后处理等 Pass 可先渲染到离屏目标，再根据需要 Copy/Resolve 到 Swapchain 或读回 CPU。
- **同步**：离屏 Pass 与读回之间需正确 Fence/Semaphore，读回前 vkQueueWaitIdle 或使用带 Fence 的提交，确保 GPU 写入完成。

### 4.6 导出为图像与视频

- **导出为图像**：
  - **截图**：将当前帧的 Color 附件（Swapchain 或离屏 Image）通过 `vkCmdCopyImageToBuffer` 复制到 Host 可见的 Buffer，再按行/格式转换为 PNG/JPG/HDR 等，用 stb_image_write 或 libpng 写出。
  - **高分辨率/无 UI 截图**：使用离屏渲染到指定分辨率，不包含 UI Pass，再读回并保存。
  - **格式**：PNG（无损）、JPEG（有损）、HDR（如 .hdr）用于高动态范围截图。
- **导出为视频**：
  - **流程**：每帧（或按固定 FPS）将 Color 输出（同上）读回 CPU，送入视频编码器（如 FFmpeg libavcodec/libavformat 或 OpenCV `VideoWriter`），编码为 MP4、WebM 等。
  - **帧率与分辨率**：可固定导出 FPS（如 30/60）和分辨率（如 1920×1080），与窗口/离屏分辨率可不同（需缩放时可用 Compute 或 Blit）。
  - **性能**：读回与编码建议放独立线程/异步，避免阻塞渲染循环；可先写入内存或临时文件，再在后台编码。
- **错误与状态**：导出失败（如磁盘满、编码错误）应通过日志或 UI 提示反馈，避免静默失败；可提供“导出中/队列中”状态显示。

### 4.7 调试与开发支持

- **调试绘制（Debug Draw）**：线框模式（Wireframe）、AABB/包围盒、法线可视化、坐标轴、简单线段/射线；用于编辑器与排查渲染问题；可用独立 Pass 或叠加到主 Pass，与正式渲染开关隔离。
- **验证层**：Debug 构建默认开启 Vulkan Validation Layer；Release 可关闭；支持运行时或配置开关，便于抓帧与回归。
- **调试标签（Debug Utils）**：为 CommandBuffer、Pipeline、Image、Buffer 等设置名称（`VK_EXT_debug_utils`），便于 RenderDoc、Nsight 中识别。
- **统计与面板**：FPS、帧时间、各 Pass GPU 时间、Draw Call 数、三角数、显存占用（若实现查询）；在调试面板中展示。

### 4.8 拾取（Picking）

- **用途**：编辑器内鼠标点击选中物体（与场景树、Gizmo 配合）；可选游戏内点击检测。
- **实现**：从相机发射射线，与场景几何做相交检测（AABB 或三角形）；可用 GPU 读回（ID Buffer：将物体/实例 ID 写入离屏图再读回点击像素）或 CPU 射线与 BVH/简单遍历。
- **与 UI 对接**：点击坐标转换到视口/NDC，触发拾取查询，将选中结果反馈给场景树与 Gizmo。

---

## 5. UI 系统

### 5.1 方案

- **Dear ImGui + Vulkan 后端**：成熟、与 Vulkan 集成方案明确（Descriptor Set、Pipeline、Font Atlas 纹理）。
- **集成点**：
  - 在 Vulkan 设备上创建 ImGui 所需的 DescriptorPool、RenderPass、Pipeline、Font Texture。
  - 每帧：ImGui::NewFrame → 应用侧 ImGui 调用 → ImGui::Render → 将 ImGui 的 DrawData 转为 Vulkan 绘制（顶点/索引 buffer、draw call）。
  - 在 Present 前增加一层 “UI RenderPass”，渲染到当前 Swapchain 图像（或叠加在后处理输出上）。

### 5.2 UI 功能建议

- 性能与调试：FPS、GPU 时间、三角数、Draw Call 数。
- 场景：层级树、GameObject 选中、Transform 编辑（可配合 ImGuizmo）。
- 材质：当前材质参数滑动条、贴图预览、切换材质。
- 渲染：开关阴影、后处理、MSAA、曝光等。
- 资源：已加载纹理/模型列表、显存占用概览（若实现统计）。
- 拾取：点击场景视图选中物体，与场景树、Gizmo 联动。

---

## 6. 功能特性清单

以下按「必须 / 推荐 / 可选」分级，便于排期；设计细节见第 4、5 节。

### 6.1 核心渲染

| 特性 | 优先级 | 说明 |
|------|--------|------|
| Vulkan 初始化与 Swapchain | 必须 | Instance、Device、Queue、Swapchain、双/三缓冲 |
| 基础管线 | 必须 | Graphics Pipeline、顶点/索引绘制、Uniform/Descriptor |
| 深度测试与深度缓冲 | 必须 | Depth attachment、深度清除与测试 |
| 多采样 (MSAA) | 推荐 | 2x/4x，与 RenderPass 和 Resolve 配合 |
| PBR 材质（金属/粗糙度） | 必须 | 金属/粗糙度工作流，至少单光 + 环境项 |
| 纹理采样 | 必须 | 2D 纹理、Sampler、Mipmap、各向异性过滤（可选） |
| 法线贴图 | 推荐 | 切线空间法线，在 PBR 中参与计算 |
| IBL（环境光） | 推荐 | 辐照度 + 预滤波镜面 + BRDF LUT |
| 方向光阴影 | 推荐 | 单张或 CSM |
| 点光/聚光阴影 | 可选 | Cubemap 或 2D shadow map |
| 天空盒 / Skybox | 推荐 | Cubemap 或 equirectangular 投影 |
| 透明与混合 | 推荐 | 按深度排序、Alpha Blend |
| 实例化绘制 | 推荐 | 同一网格多实例，instanced draw |
| LOD | 可选 | 按距离切换网格/简化网格 |
| 视锥剔除 | 推荐 | CPU 或 GPU 端剔除不可见物体 |

### 6.2 后处理

| 特性 | 优先级 | 说明 |
|------|--------|------|
| 全屏四边形 Pass | 必须 | 后处理基础 |
| 色调映射 (Tone Mapping) | 推荐 | Reinhard / ACES / Uncharted2 |
| Bloom | 推荐 | 高斯或 Dual Filter、阈值、强度 |
| FXAA / TAA | 推荐 | 抗锯齿 |
| SSAO | 可选 | 环境光遮蔽；依赖随机旋转噪声纹理 |
| 噪声纹理（SSAO/TAA/抖动） | 推荐 | 预烘焙或程序化；小图旋转噪声、蓝噪声等 |
| 泛光 / 色差 / Vignette | 可选 | 艺术向效果 |
| 景深 (DOF) | 可选 | 基于深度模糊 |

### 6.3 UI 与交互

| 特性 | 优先级 | 说明 |
|------|--------|------|
| ImGui Vulkan 集成 | 必须 | 完整后端：Descriptor、Pipeline、Font |
| 调试面板 | 推荐 | FPS、帧时间、基础统计 |
| 场景树与选中 | 推荐 | 列表/树、高亮选中对象 |
| Gizmo 变换 | 推荐 | ImGuizmo 平移/旋转/缩放 |
| 材质编辑器 | 推荐 | 滑动条、贴图槽、实时预览 |
| 节点编辑器 | 可选 | 材质/逻辑节点图 |
| 控制台/日志窗口 | 可选 | 引擎日志输出到 ImGui |

### 6.3a 材质系统

| 特性 | 优先级 | 说明 |
|------|--------|------|
| 材质资产（参数 + 贴图槽） | 必须 | BaseColor/Metallic/Roughness/AO/Normal/Emissive；标量或贴图，默认值 |
| 贴图槽回退（无贴图用标量） | 必须 | 每槽可选贴图或常量，着色器分支或统一采样 |
| Alpha 模式（Opaque / Mask / Blend） | 推荐 | 决定深度、混合与渲染顺序；Mask 需 cutoff |
| 渲染状态（剔除、深度、混合） | 推荐 | 双面/单面、深度写、Blend 模式，驱动 Pipeline 状态 |
| 材质实例（覆盖参数） | 推荐 | 由资产派生，可覆盖部分参数，多实例共享资产 |
| 材质 UBO / Push Constant | 必须 | 参数上传 GPU，per-draw 或 per-material |
| 材质 DescriptorSet（贴图+Sampler） | 必须 | 按材质/实例绑定，池化或按批更新 |
| 管线/Shader 变体（Alpha、双面等） | 推荐 | Specialization Constant 或预编译多版本 |
| 材质序列化与加载 | 推荐 | JSON/二进制资产，加载与缓存 |
| 同材质合批 / Instancing | 推荐 | 减少 Draw Call 与状态切换 |
| Clearcoat/Sheen/Transmission | 可选 | PBR 扩展参数与着色 |
| 材质热重载 | 可选 | 资产变更后重新加载并更新 GPU 资源 |

### 6.4 资源与管线

| 特性 | 优先级 | 说明 |
|------|--------|------|
| 模型加载 (OBJ/glTF) | 必须 | 顶点、法线、UV、索引、子网格 |
| 纹理加载 (PNG/JPG/HDR) | 必须 | 2D、Cubemap、Mip 生成 |
| 噪声纹理 | 推荐 | 程序化生成或预烘焙；供 SSAO/TAA/抖动/蓝噪声等使用 |
| 程序化噪声（柏林/Simplex/Worley） | 推荐 | CPU 或 Compute 实现；地形、云、材质细节、FBM 多 octave |
| 着色器编译 (GLSL→SPIR-V) | 必须 | 构建时或运行时，管线缓存 |
| 描述符集管理 | 必须 | 池化、按帧/按材质绑定 |
| 统一内存/缓冲策略 | 推荐 | 大 UBO、动态偏移或 push constant |
| 异步加载 | 可选 | 后台加载纹理/模型，完成后上传 GPU |
| 热重载 | 可选 | 着色器/配置热重载 |

### 6.5 场景与逻辑

| 特性 | 优先级 | 说明 |
|------|--------|------|
| 场景图与 Transform 层级 | 必须 | GameObject/Transform 层级，世界矩阵更新 |
| 相机组件 | 必须 | 透视、视口、与 Renderer 对接 |
| 光源组件 | 必须 | 方向光/点光/聚光，与 PBR 一致 |
| 网格/材质组件 | 必须 | 绑定 Mesh + Material，驱动绘制 |
| 场景序列化/反序列化 | 推荐 | 场景保存与加载，见 6.5a |
| 脚本/行为 | 可选 | Lua 或 C++ 组件逻辑 |

### 6.5a 场景序列化（Scene Serialization）

- **目标**：将当前场景（层级、组件、引用）保存为文件，并支持加载还原，便于关卡编辑、存档、协作。
- **序列化内容**：
  - **场景图**：根节点列表；每个 GameObject 的 ID（UUID）、名称、父节点引用、子节点列表。
  - **Transform**：本地位置、旋转、缩放（或 TRS 矩阵）；可选世界矩阵缓存不序列化，加载时从本地重建。
  - **组件**：按类型序列化——Camera（FOV、近远裁剪、视口）、Light（类型、颜色、强度、范围等）、MeshRenderer（Mesh 引用、Material 实例引用）、自定义组件（若支持则按字段写出）。
  - **资产引用**：Mesh、Material、纹理等用**资产路径**或**资产 ID** 引用，不内嵌数据；加载时通过 AssetManager 解析并加载。
- **格式**：
  - **JSON**：可读、易 diff、便于版本管理；可用 nlohmann/json 或 yaml-cpp；体积较大、解析稍慢。
  - **二进制**：自定义或现成格式（如 glTF 场景扩展）；体积小、加载快，需版本号与兼容策略。
  - 可选：编辑器用 JSON，发布用二进制或打包进资源包。
- **版本与兼容**：文件头或根节点带**版本号**；反序列化时根据版本做字段迁移或忽略未知字段，避免旧版本无法加载。
- **加载流程**：读文件 → 解析为场景图 + 组件数据 → 创建 GameObject/Transform/组件 → 解析资产引用并异步/同步加载 Mesh、材质等 → 绑定到渲染组件 → 触发依赖更新（如世界矩阵）。
- **保存流程**：遍历场景图 → 按节点与组件类型写出 JSON/二进制；资产仅写路径或 ID，不写贴图/网格数据。
- **可选**：增量保存、仅保存选中子树、与预制体（Prefab）共用序列化格式。

### 6.5b 程序化地形

| 特性 | 优先级 | 说明 |
|------|--------|------|
| 高度场生成（柏林/Simplex 多 octave） | 推荐 | 由噪声生成高度图，seed 可复现 |
| 高度图 → 网格（顶点/法线/UV） | 推荐 | 规则网格生成，法线由高度差或导数计算 |
| 分块（Chunk）与按需加载 | 推荐 | 大世界分块，仅保留相机附近块 |
| 地形 LOD（按距离降分辨率） | 可选 | 减少远处三角形，块级或顶点级 LOD |
| GPU 地形（Compute 生成） | 可选 | 噪声在 Compute 中生成，直接写顶点/Buffer |
| 地形材质（splat/程序化着色） | 可选 | 按高度/斜率混合贴图或噪声着色 |

### 6.6 性能与质量

| 特性 | 优先级 | 说明 |
|------|--------|------|
| 多线程命令录制 | 推荐 | 多线程生成 Command Buffer |
| 管线缓存 | 推荐 | VkPipelineCache 持久化 |
| GPU 时间戳/统计 | 推荐 | 各 Pass 耗时、瓶颈分析 |
| 调试标签 (Debug Utils) | 推荐 | 便于 RenderDoc/Nsight 分析 |
| 验证层与错误处理 | 必须 | Debug 构建开启 Validation，友好错误信息 |

### 6.7 离屏渲染与导出（图像/视频）

| 特性 | 优先级 | 说明 |
|------|--------|------|
| 离屏渲染目标 (FBO) | 推荐 | 渲染到自有 VkImage/VkFramebuffer，分辨率与格式可配置 |
| 无窗口/Headless 模式 | 可选 | 无可见窗口的渲染（批处理、服务器端渲染） |
| 多视口/多相机输出 | 可选 | 同一帧多相机渲染到多张离屏图 |
| 截图（当前帧） | 推荐 | 将 Color 读回 CPU，保存为 PNG/JPG/HDR |
| 高分辨率/无 UI 截图 | 可选 | 离屏渲染指定分辨率并排除 UI 层后导出 |
| 视频录制 | 推荐 | 连续帧读回并编码为 MP4/WebM 等，可配置 FPS 与分辨率 |
| 导出队列与异步编码 | 可选 | 读回与编码放后台线程，避免卡顿；导出状态/错误反馈到 UI |

### 6.8 配置、启动与运维

| 特性 | 优先级 | 说明 |
|------|--------|------|
| 配置文件 | 推荐 | 分辨率、全屏、质量档位、窗口标题等；JSON/YAML/INI 加载 |
| 命令行参数 | 可选 | 覆盖配置（如 -fullscreen、-width）、场景路径、验证层开关 |
| VSync / Present 模式 | 推荐 | 垂直同步开关、FIFO/MAILBOX 等 Present 模式 |
| 帧率限制 | 可选 | 最大 FPS 上限（无 VSync 时） |
| 日志系统 | 必须 | 日志级别（Debug/Info/Warn/Error）、控制台与文件输出 |
| 崩溃与错误处理 | 推荐 | 友好错误提示、可选崩溃转储或日志回溯 |

### 6.9 场景与编辑器扩展

| 特性 | 优先级 | 说明 |
|------|--------|------|
| 预制体（Prefab） | 推荐 | 可复用场景子树、实例化与覆盖参数、与场景序列化格式兼容 |
| 图层 / 标签（Layer、Tag） | 推荐 | 相机裁剪层、光照影响层、拾取/射线过滤；按层显示/隐藏 |
| 拾取（Picking） | 推荐 | 射线与场景相交或 ID Buffer，用于编辑器选中物体 |
| 调试绘制 | 推荐 | 线框、AABB、法线、坐标轴、Debug 线；与正式渲染可切换 |
| 多窗口 / 多视口 | 可选 | 编辑器多视口或独立窗口，每视口独立相机与渲染 |

### 6.10 可选扩展（按需排期）

| 特性 | 优先级 | 说明 |
|------|--------|------|
| HDR 与色彩管线 | 推荐 | 线性空间、sRGB 贴图、HDR 渲染目标、曝光与色调映射一致性 |
| 粒子系统 | 可选 | CPU 或 GPU 粒子、Billboard/网格粒子、与主 Pass 混合 |
| 骨骼动画与蒙皮 | 可选 | 骨骼、蒙皮网格、动画剪辑与播放、与 Mesh 组件对接 |
| 反射探针 / 平面反射 | 可选 | 环境 Cubemap 或平面反射贴图，供 PBR 镜面采样 |
| 文本渲染 | 可选 | SDF 字体或 Mesh 文本，用于 UI 与 3D 文本 |
| 高 DPI / 分辨率缩放 | 推荐 | 窗口与 UI 在高 DPI 下的缩放、帧缓冲分辨率与窗口比例 |
| 纹理压缩与流式 | 可选 | BC/ASTC 等压缩格式、按需流式加载与卸载 |

---

## 7. 建议目录结构

以下为按领域划分、头源分离的目录布局，便于扩展与测试；根目录以 `engine/` 为例，可替换为实际工程名。对应文件级清单与阶段拆解见 [render-engine-features.md](render-engine-features.md)。

### 7.1 设计原则

- **include/**：对外 API，按模块分目录；实现细节不暴露。
- **src/**：与 include 一一对应的实现，同一模块内可再分子目录。
- **assets/**：运行时资源（着色器源码、默认纹理/模型/环境贴图），与代码分离便于打包。
- **third_party/**：第三方源码或 vcpkg 外的依赖，统一管理。
- **tools/**：着色器编译、资源处理、管线缓存等离线工具。
- **tests/**：单元测试与小型集成测试，不依赖完整应用。

### 7.2 推荐树形结构

```
engine/
├── CMakeLists.txt
├── include/                          # 公开头文件
│   ├── engine/
│   │   ├── core.hpp                 # 引擎入口、生命周期、配置、日志
│   │   └── types.hpp                # 公共类型、枚举、常量
│   ├── platform/
│   │   ├── window.hpp               # 窗口抽象（尺寸、Surface 句柄、全屏等）
│   │   ├── input.hpp                # 输入抽象（键/鼠/手柄、事件或轮询）
│   │   ├── platform_config.hpp      # 平台配置
│   │   └── backend/
│   │       └── sdl3_window.hpp      # SDL3 窗口/输入实现
│   ├── render/
│   │   ├── context.hpp              # Vulkan 实例、设备、队列；VMA（`vma_allocator()`）
│   │   ├── swapchain.hpp            # Swapchain、帧同步
│   │   ├── resource/
│   │   │   ├── buffer.hpp           # VkBuffer + VMA 分配
│   │   │   ├── image.hpp            # VkImage、View、VMA 分配
│   │   │   ├── sampler.hpp
│   │   │   └── descriptor.hpp       # 描述符池、布局、集
│   │   ├── pipeline.hpp             # 管线布局、Graphics/Compute 管线
│   │   ├── pass/
│   │   │   ├── render_pass.hpp      # 渲染通道、Framebuffer 抽象
│   │   │   ├── shadow_pass.hpp      # 阴影图 Pass
│   │   │   ├── main_pass.hpp        # 主场景/GBuffer/前向
│   │   │   ├── post_process.hpp     # 后处理链
│   │   │   └── offscreen.hpp        # 离屏目标、多视口
│   │   ├── pbr.hpp                  # PBR 管线与 IBL 绑定
│   │   ├── renderer.hpp             # 高层帧流程编排
│   │   └── export.hpp               # 截图、视频录制（读回与编码接口）
│   ├── gltf/
│   │   ├── mesh_asset.hpp           # glTF 对齐 Mesh / Primitive / Model / MeshBuffer（lumen::gltf）
│   │   └── gltf_scene_mesh.hpp      # load_gltf_scene_mesh，GPU 几何与材质
│   ├── scene/
│   │   ├── scene.hpp
│   │   ├── scene_serializer.hpp      # 场景序列化/反序列化（保存与加载）
│   │   ├── game_object.hpp
│   │   ├── transform.hpp
│   │   ├── camera.hpp
│   │   ├── light.hpp
│   │   ├── gltf_spawn.hpp           # glTF 场景写入 ECS（SubMesh / MeshRenderer）
│   │   ├── material.hpp              # 材质参数与贴图槽定义
│   │   ├── material_asset.hpp       # 材质资产（序列化、加载）
│   │   ├── material_instance.hpp    # 材质实例（运行时覆盖）
│   │   ├── component.hpp
│   │   ├── uuid.hpp
│   │   ├── terrain_chunk.hpp        # 地形块、高度图与网格
│   │   └── terrain_generator.hpp    # 程序化地形：噪声参数、分块、LOD
│   ├── asset/
│   │   ├── asset_manager.hpp        # 统一加载与缓存
│   │   ├── model_loader.hpp         # OBJ/glTF 等
│   │   ├── texture_loader.hpp
│   │   ├── noise_texture.hpp        # 噪声纹理生成/加载（程序化或预烘焙）
│   │   ├── procedural_noise/       # 柏林、Simplex、Worley 等
│   │   │   ├── perlin.hpp
│   │   │   ├── simplex.hpp
│   │   │   └── worley.hpp           # 可选
│   │   └── shader_compiler.hpp     # GLSL→SPIR-V、管线创建辅助
│   └── ui/
│       ├── imgui_backend.hpp        # ImGui Vulkan 后端封装
│       ├── panels.hpp               # 调试/场景/材质等面板
│       └── gizmo.hpp                # ImGuizmo 等
├── src/
│   ├── main.cpp                     # 应用入口（可选，若引擎为库则放在示例里）
│   ├── engine/
│   │   ├── core.cpp
│   │   └── ...
│   ├── platform/
│   │   ├── window.cpp               # 抽象层或工厂
│   │   ├── input.cpp
│   │   └── backend/
│   │       └── sdl3/                # SDL3 后端
│   │       │   ├── sdl3_window.cpp
│   │       │   └── sdl3_input.cpp
│   ├── render/
│   │   ├── context.cpp
│   │   ├── swapchain.cpp
│   │   ├── resource/
│   │   ├── pipeline.cpp
│   │   ├── pass/
│   │   ├── pbr.cpp
│   │   ├── renderer.cpp
│   │   └── export.cpp
│   ├── scene/
│   ├── asset/
│   └── ui/
├── assets/                          # 引擎或示例使用的默认资源
│   ├── shaders/
│   │   ├── glsl/
│   │   │   ├── pbr.vert
│   │   │   ├── pbr.frag
│   │   │   ├── shadow.vert
│   │   │   ├── shadow.frag
│   │   │   ├── post_process.vert
│   │   │   ├── post_process.frag
│   │   │   └── ui.vert / ui.frag
│   │   └── (可选) hlsl/
│   ├── textures/                    # 默认/测试纹理
│   │   └── noise/                   # 预烘焙噪声图（SSAO 旋转、蓝噪声等）
│   ├── models/
│   └── env/                         # IBL：HDR、辐照度、预滤波、BRDF LUT
├── third_party/                     # 非 vcpkg 的第三方代码
│   ├── imgui/
│   ├── imguizmo/
│   └── (按需)
├── tools/                           # 离线工具
│   ├── compile_shaders.cpp          # 批量编译 GLSL→SPIR-V
│   ├── bake_ibl.cpp                 # 预计算 IBL 贴图
│   ├── generate_noise.cpp           # 生成噪声纹理（SSAO 旋转、蓝噪声、柏林/Simplex 预烘焙等）
│   ├── generate_terrain_heightmap.cpp  # 可选：离线生成地形高度图（柏林/Simplex）
│   └── CMakeLists.txt
├── tests/
│   ├── test_math.cpp
│   ├── test_asset_loading.cpp
│   └── CMakeLists.txt
└── docs/                            # 设计文档、API 说明
    └── ...
```

### 7.3 模块与目录对应

| 目录 | 职责 |
|------|------|
| `include/engine` | 引擎核心、配置、日志、类型 |
| `include/platform` | 窗口与输入抽象；`backend/` 下 SDL3 实现 |
| `include/render` | 全部渲染相关：Vulkan 上下文、资源、管线、各 Pass、PBR、导出 |
| `include/scene` | 场景图、物体、变换、相机、光源、网格、材质；程序化地形（terrain_chunk、terrain_generator） |
| `include/asset` | 资源加载、编译、缓存；程序化噪声（Perlin、Simplex、Worley） |
| `include/ui` | ImGui 集成、面板、Gizmo |
| `assets/` | 着色器源码与默认资源，部署时可单独打包 |
| `tools/` | 不参与运行时的构建/烘焙工具 |
| `tests/` | 独立可执行测试，便于 CI |

---

## 8. 开发阶段与里程碑

### Phase 0：基础 Vulkan 1.4（若尚未完成）

- Vulkan 1.4 Instance、Device、Queue、Surface、Swapchain（含 `VK_API_VERSION_1_4` 及所需扩展）。
- 双缓冲/三缓冲、Fence/Semaphore 同步。
- 最小 RenderPass：单色或三角形，能 Present。

### Phase 1：可渲染场景（MVP）

- 窗口与输入抽象与 SDL3 后端；基础配置与日志（分辨率、全屏等；日志级别与输出）。
- 网格：顶点/索引 Buffer、简单 Uniform（MVP）；相机与 Transform 驱动 MVP。
- 单 Pass 前向渲染：简单光照（如 Blinn-Phong）或最简 PBR（单方向光）；纹理与 Sampler、Mipmap。
- **交付**：能加载一个 OBJ，带纹理与光照的静态场景；具备基本配置与日志。

### Phase 2：PBR 与 IBL

- 完整 PBR 着色器（金属/粗糙度、多光源）。
- 材质系统：材质资产（参数 + 贴图槽、标量回退）、UBO/Descriptor 绑定；可选材质实例与 Alpha 模式。
- 法线贴图、AO 贴图。
- IBL：辐照度 + 预滤波 + BRDF LUT，或先用简化环境项。
- **交付**：PBR 材质 + IBL 的视觉效果达标；材质可配置参数与贴图。

### Phase 3：阴影与后处理

- 方向光阴影（单张或 CSM）。
- 后处理：全屏 Pass、色调映射、Bloom、FXAA/TAA 选做。
- **交付**：带阴影与基础后处理的完整画面。

### Phase 4：UI 与编辑器基础

- ImGui Vulkan 后端集成；调试面板（FPS、GPU 时间、开关）。
- 场景树、拾取（射线或 ID Buffer 选中）、Gizmo（ImGuizmo）；材质面板（滑动条、贴图槽）。
- 配置与日志（若 Phase 1 未做可在此补全）。
- **交付**：可交互编辑的编辑器式界面，支持点击选中与 Transform/材质编辑。

### Phase 5：扩展与优化

- 实例化、LOD、视锥剔除。
- 更多光源类型与阴影。
- SSAO、更多后处理。
- 程序化噪声（柏林/Simplex/Worley）库与噪声纹理管线（含工具 `generate_noise`）。
- 管线缓存、多线程录制、GPU 统计。
- 资源热重载、异步加载（可选）。

### Phase 5a：程序化地形（可选）

- 柏林/Simplex 多 octave 高度场生成（seed、frequency、persistence、lacunarity）。
- 高度图 → 网格（顶点、法线、UV）；分块（Chunk）与按需加载，块边界无缝。
- 地形 LOD、地形材质（splat 或程序化着色）可选。
- **交付**：可配置参数生成连续地形并渲染。

### Phase 6：离屏渲染与导出（图像/视频）

- **离屏渲染**：离屏 VkImage + Framebuffer + RenderPass，支持自定义分辨率与格式；主管线可先渲染到离屏目标再 Present 或读回。
- **导出为图像**：从 Color 附件（Swapchain 或离屏）CopyImageToBuffer → CPU 内存 → PNG/JPG/HDR（stb_image_write 等）；支持“当前帧截图”与“高分辨率/无 UI 截图”。
- **导出为视频**：按固定 FPS 将帧读回并送入 FFmpeg/OpenCV 等编码为 MP4/WebM；可选异步编码与导出状态/错误反馈到 UI。
- **交付**：可无窗口或带窗口下进行离屏渲染，并支持单帧导出为图片、多帧导出为视频。

---

## 9. 风险与注意事项

- **驱动与验证层**：不同厂商 Vulkan 行为差异；开发期务必开启 Validation，发布前可关闭并做回归测试。
- **同步**：每帧正确使用 Fence/Semaphore，避免资源写读冲突（Shadow、GBuffer、Post、UI 之间）。
- **描述符与绑定**：提前规划 UBO/贴图数量与更新频率，避免每物体一描述符集导致池耗尽；可多用动态 UBO 或 push constant。
- **内存**：引擎侧 `Buffer` / `Image` / `Texture` 已通过 **VMA**（`vulkan-memory-allocator`）做子分配；Swapchain 图像等仍由 KHR swapchain / 驱动路径管理，不经过 VMA。自定义大量离屏资源时仍注意 `maxMemoryAllocationCount` 与碎片。
- **色彩与线性空间**：贴图 sRGB 约定、HDR 中间目标与最终 sRGB 输出需统一，避免过曝或发灰。
- **扩展**：按需启用 extension（如 VK_KHR_dynamic_rendering、VK_EXT_descriptor_indexing），并做 fallback 或特性检测。

---

## 10. 文档与后续

- **本文档**：总体规划与功能清单；随实现进展可增删优先级与阶段。**配套**：[render-engine-features.md](render-engine-features.md) 提供按模块的勾选清单与按阶段的实现顺序。
- **建议补充文档**：
  - API 设计：各模块对外接口（类、方法、参数）。
  - 着色器规范：UBO 布局、Descriptor Set 绑定、Push Constant 布局。
  - 资源格式约定：模型坐标系、纹理约定（Y 翻转、sRGB）、命名规范。
- 可按需再拆出更细的「第一步实现清单」（如 VkContext + Forward PBR Pass + ImGui 窗口），便于按任务拆 PR/Commit。

