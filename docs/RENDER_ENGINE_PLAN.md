# Vulkan 渲染引擎 — 总体规划与功能文档

本文档为基于 Vulkan 的渲染引擎提供整体规划、架构设计、功能特性清单与开发阶段建议，便于按阶段实现 UI、PBR 及更多高级特性。

---

## 1. 项目概述与目标

### 1.1 定位

- **类型**：实时 3D 渲染引擎（可嵌入游戏/工具/编辑器）
- **图形 API**：Vulkan 1.x（优先 Vulkan 1.2+，便于使用 descriptor indexing、timeline semaphore 等）
- **目标平台**：Windows（主）、可选 Linux / macOS
- **语言**：C++17/20，引擎核心 C++，着色器 GLSL 或 HLSL（经 glslang/glslc 或 DXC 编译）

### 1.2 核心目标

| 目标 | 说明 |
|------|------|
| **PBR 渲染** | 基于物理的材质（金属/粗糙度工作流），支持 IBL、多光源 |
| **可扩展 UI** | 编辑器/调试 UI（如 ImGui），与 Vulkan 后端深度集成 |
| **功能丰富** | 阴影、后处理、抗锯齿、LOD、实例化、GPU 粒子等 |
| **可维护** | 清晰分层、模块化、便于后续加 Vulkan 2 / 扩展 |

---

## 2. 技术栈与依赖

### 2.1 已有 / 建议依赖

| 类别 | 库/工具 | 用途 |
|------|---------|------|
| 图形 API | Vulkan SDK (1.2+) | 实例、设备、管线、描述符等 |
| 窗口与输入 | GLFW 或 SDL2 | 窗口、键盘、鼠标、手柄 |
| 数学 | glm | 向量、矩阵、四元数 |
| UI | Dear ImGui + ImGui Vulkan 后端 | 编辑器/调试 UI、面板、拾取 |
| 模型加载 | tinyobjloader / Assimp | OBJ、FBX、glTF 等 |
| 图片 | stb_image / libpng | 纹理加载、HDR |
| 着色器编译 | glslang / glslc (Vulkan SDK) 或 DXC | GLSL → SPIR-V，可选 HLSL |
| 序列化/配置 | nlohmann/json 或 yaml-cpp | 场景/材质/配置 |
| 字体 | stb_truetype + ImGui 或 FreeType | UI 与文本渲染 |
| 可选 | ImGuizmo | 场景内 Gizmo（平移/旋转/缩放） |
| 可选 | ImGui Node Editor | 节点式材质/蓝图编辑器 |

### 2.2 与现有工程的关系

- 复用 **engine** 下已有：`scene`（GameObject、Transform、Camera、Light、Material、Mesh）、`Material`（PBR 字段与贴图槽）、`pipeline.h` 占位。
- 将 **Pipeline** 实现为真正的 Vulkan 管线封装（Graphics/Compute），并接入 PBR 与 UI。
- 示例可参考 **examples** 中的 VulkanTutorial、vkguide、EasyVulkan、sdl_cpp_VulkanTutorial 等。

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
│  - Window, Input, 文件系统, 线程/任务                        │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 核心模块划分

| 模块 | 职责 | 主要类/组件 |
|------|------|-------------|
| **Core** | 引擎生命周期、配置、日志、全局服务定位 | Engine, Config, Logger |
| **Window** | 窗口创建、表面、与 Vulkan Surface 对接 | Window, WindowConfig |
| **Input** | 键盘/鼠标/手柄事件，输入映射 | InputManager, InputMapping |
| **VkContext** | Instance、PhysicalDevice、Device、Queue、Swapchain | VkContext, VkSwapchain |
| **VkMemory** | 分配器、Buffer/Image 封装、 staging、上传 | VkAllocator, VkBuffer, VkImage |
| **VkPipeline** | 管线布局、Graphics/Compute 管线、缓存 | Pipeline (重写), PipelineCache |
| **VkDescriptor** | 描述符池、布局、集、绑定 | DescriptorPool, DescriptorSetLayout |
| **RenderPass** | 渲染通道定义、子通道、附件、依赖 | RenderPass, Framebuffer |
| **Scene** | 场景图、GameObject、Transform、Camera、Light | 已有 scene/* 扩展 |
| **Mesh/Material** | 网格数据、材质参数、贴图句柄 | Mesh, Material（已有）, TextureManager |
| **PBR** | PBR 着色器、UBO、贴图绑定、IBL | PBRPipeline, IBL (cubemap/BRDF LUT) |
| **Shadow** | 阴影图、CSM/Spot/Point 阴影 | ShadowMap, ShadowPass |
| **PostProcess** | 后处理链（Bloom、TAA、色调映射等） | PostProcessPass, PostStack |
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

- **工作流**：金属/粗糙度（Metallic-Roughness），与现有 `Material` 一致。
- **贴图**：BaseColor, Metallic, Roughness, Normal, AO，可选 Emissive；支持无贴图时用 uniform 参数。
- **光照**：方向光 + 多盏点光/聚光；BRDF：Cook-Torrance（GGX NDF、Smith G、Fresnel-Schlick）。
- **IBL**：预滤波环境贴图（镜面）+ 辐照度贴图（漫反射）+ BRDF LUT 二维贴图。
- **实现层次**：先单方向光 + 简单环境光，再加 IBL 与多光源。

### 4.3 阴影

- **方向光**：级联阴影贴图（CSM）或单张 large shadow map。
- **点光/聚光**：可选 cubemap 或 2D 阴影图。
- **技术**：PCF、VSM、ESM 等可选，先实现硬阴影 + PCF。

### 4.4 抗锯齿与后处理

- **抗锯齿**：先 MSAA（若用 Forward），或 TAA/FXAA 在后处理中。
- **后处理链**：Bloom、色调映射（Reinhard / ACES）、可选 SSAO、泛光、色差、 vignette 等。

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

---

## 6. 功能特性清单（尽可能全面）

以下按“必须 / 推荐 / 可选”分级，便于排期。

### 6.1 核心渲染

| 特性 | 优先级 | 说明 |
|------|--------|------|
| Vulkan 初始化与 Swapchain | 必须 | Instance、Device、Queue、Swapchain、双/三缓冲 |
| 基础管线 | 必须 | Graphics Pipeline、顶点/索引绘制、Uniform/Descriptor |
| 深度测试与深度缓冲 | 必须 | Depth attachment、深度清除与测试 |
| 多采样 (MSAA) | 推荐 | 2x/4x，与 RenderPass 和 Resolve 配合 |
| PBR 材质（金属/粗糙度） | 必须 | 与现有 Material 一致，至少单光 + 环境项 |
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
| SSAO | 可选 | 环境光遮蔽 |
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

### 6.4 资源与管线

| 特性 | 优先级 | 说明 |
|------|--------|------|
| 模型加载 (OBJ/glTF) | 必须 | 顶点、法线、UV、索引、子网格 |
| 纹理加载 (PNG/JPG/HDR) | 必须 | 2D、Cubemap、Mip 生成 |
| 着色器编译 (GLSL→SPIR-V) | 必须 | 构建时或运行时，管线缓存 |
| 描述符集管理 | 必须 | 池化、按帧/按材质绑定 |
| 统一内存/缓冲策略 | 推荐 | 大 UBO、动态偏移或 push constant |
| 异步加载 | 可选 | 后台加载纹理/模型，完成后上传 GPU |
| 热重载 | 可选 | 着色器/配置热重载 |

### 6.5 场景与逻辑

| 特性 | 优先级 | 说明 |
|------|--------|------|
| 场景图与 Transform 层级 | 必须 | 已有 GameObject/Transform，需世界矩阵更新 |
| 相机组件 | 必须 | 透视、视口、与 Renderer 对接 |
| 光源组件 | 必须 | 方向光/点光/聚光，与 PBR 一致 |
| 网格/材质组件 | 必须 | 绑定 Mesh + Material，驱动绘制 |
| 序列化/反序列化 | 推荐 | 场景保存与加载（JSON/自定义格式） |
| 脚本/行为 | 可选 | Lua 或 C++ 组件逻辑 |

### 6.6 性能与质量

| 特性 | 优先级 | 说明 |
|------|--------|------|
| 多线程命令录制 | 推荐 | 多线程生成 Command Buffer |
| 管线缓存 | 推荐 | VkPipelineCache 持久化 |
| GPU 时间戳/统计 | 推荐 | 各 Pass 耗时、瓶颈分析 |
| 调试标签 (Debug Utils) | 推荐 | 便于 RenderDoc/Nsight 分析 |
| 验证层与错误处理 | 必须 | Debug 构建开启 Validation，友好错误信息 |

---

## 7. 建议目录结构

在现有 **engine** 基础上，可逐步演化为类似结构（仅作参考，可按需合并/拆分）：

```
engine/
├── include/
│   ├── core/           # 已有 core.h，可扩展 Engine、Config、Logger
│   ├── window/         # Window 抽象与 GLFW/SDL 实现
│   ├── input/          # InputManager
│   ├── render/
│   │   ├── pipeline.h           # 重写为 Vulkan 管线封装
│   │   ├── render_pass.h
│   │   ├── framebuffer.h
│   │   ├── buffer.h
│   │   ├── image.h
│   │   ├── descriptor.h
│   │   ├── context.h             # VkContext
│   │   ├── renderer.h            # 高层渲染器
│   │   ├── pbr_pipeline.h
│   │   ├── shadow_pass.h
│   │   ├── post_process.h
│   │   └── ui_renderer.h
│   ├── scene/          # 已有 camera, component, gameobject, light, material, mesh, scene, transform, uuid
│   ├── asset/          # AssetManager, ModelLoader, TextureLoader
│   ├── shader/         # ShaderCompiler, 管线创建辅助
│   └── ui/             # ImGui 封装、面板、Gizmo 等
├── src/
│   ├── engine.cpp
│   ├── window/
│   ├── input/
│   ├── render/         # context.cpp, pipeline.cpp, renderer.cpp, pbr_pipeline.cpp, ...
│   ├── scene/
│   ├── asset/
│   └── ui/
├── assets/
│   ├── shader/glsl/    # 已有 pbr.vert/frag，扩展 shadow、post、ui
│   ├── textures/
│   ├── model/
│   └── env/            # IBL 用 HDR 或预计算贴图
└── CMakeLists.txt
```

---

## 8. 开发阶段与里程碑

### Phase 0：基础 Vulkan（若尚未完成）

- Vulkan Instance、Device、Queue、Surface、Swapchain。
- 双缓冲/三缓冲、Fence/Semaphore 同步。
- 最小 RenderPass：单色或三角形，能 Present。

### Phase 1：可渲染场景（MVP）

- 窗口与输入（复用或封装 GLFW/SDL）。
- 网格：顶点/索引 Buffer、简单 Uniform（MVP）。
- 相机与 Transform 驱动 MVP。
- 单 Pass 前向渲染：简单光照（如 Blinn-Phong）或最简 PBR（单方向光）。
- 纹理与 Sampler、Mipmap。
- **交付**：能加载一个 OBJ，带纹理与光照的静态场景。

### Phase 2：PBR 与 IBL

- 完整 PBR 着色器（金属/粗糙度、多光源）。
- 法线贴图、AO 贴图。
- IBL：辐照度 + 预滤波 + BRDF LUT，或先用简化环境项。
- **交付**：PBR 材质 + IBL 的视觉效果达标。

### Phase 3：阴影与后处理

- 方向光阴影（单张或 CSM）。
- 后处理：全屏 Pass、色调映射、Bloom、FXAA/TAA 选做。
- **交付**：带阴影与基础后处理的完整画面。

### Phase 4：UI 与编辑器基础

- ImGui Vulkan 后端集成。
- 调试面板（FPS、开关）。
- 场景树、选中、Gizmo（ImGuizmo）。
- 材质面板（滑动条、贴图槽）。
- **交付**：可交互编辑的编辑器式界面。

### Phase 5：扩展与优化

- 实例化、LOD、视锥剔除。
- 更多光源类型与阴影。
- SSAO、更多后处理。
- 管线缓存、多线程录制、GPU 统计。
- 资源热重载、异步加载（可选）。

---

## 9. 风险与注意事项

- **驱动与验证层**：不同厂商 Vulkan 行为差异；开发期务必开 Validation，发布前可关闭并做回归测试。
- **同步**：每帧正确使用 Fence/Semaphore，避免资源写读冲突（尤其 Shadow、GBuffer、Post、UI 之间）。
- **描述符与绑定**：提前规划 UBO/贴图数量与更新频率，避免每物体一描述符集导致池耗尽；可多用动态 UBO 或 push constant。
- **内存**：大 Buffer/Image 建议用专用分配器（如 VMA）减少碎片与分配次数。
- **扩展**：需要时再启用 extension（如 VK_KHR_dynamic_rendering、VK_EXT_descriptor_indexing），并做 fallback 或特性检测。

---

## 10. 文档与后续

- **本文档**：作为总体规划与功能清单，随实现进展可增删“优先级”和“阶段”。
- **建议补充**：
  - **API 设计文档**：各模块对外接口（类、方法、参数）。
  - **着色器规范**：UBO 布局、Descriptor Set 绑定、Push Constant 布局。
  - **资源格式约定**：模型坐标系、纹理约定（Y 翻转、sRGB）、命名规范。

若你希望，我可以基于当前 **engine** 的 `scene`、`Material`、`pipeline.h` 再写一份更细的「第一步实现清单」（例如：先实现 VkContext + 一个 Forward PBR Pass + 一个简单 ImGui 窗口），便于直接按任务拆 PR/Commit。
