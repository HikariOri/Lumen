# 渲染引擎 — 功能特性清单与开发阶段速查

本文档是 [RENDER_ENGINE_PLAN.md](RENDER_ENGINE_PLAN.md) 的补充，提供按阶段拆分的功能清单与实现顺序速查，便于排期与验收。

---

## 一、功能特性总览（按模块）

### 1. Vulkan 底层

- [ ] Instance 创建（Layer、Extension、Validation）
- [ ] Physical Device 选择与 Queue 族
- [ ] Logical Device 与 Queue
- [ ] Surface 与 Swapchain（含 Resize 重建）
- [ ] 双缓冲/三缓冲与 Fence/Semaphore 同步
- [ ] Command Pool 与 Command Buffer（每帧录制）
- [ ] RenderPass 与 Framebuffer
- [ ] 管线缓存（VkPipelineCache）持久化

### 2. 资源与内存

- [ ] Buffer 封装（Vertex/Index/Uniform/Staging）
- [ ] Image 封装（2D/Cube、Mip、Format）
- [ ] 内存分配策略（可选 VMA）
- [ ] Staging 上传（纹理、顶点数据）
- [ ] Sampler（Filter、AddressMode、Anisotropy）

### 3. 描述符与绑定

- [ ] DescriptorSetLayout 定义
- [ ] DescriptorPool 与 DescriptorSet 分配
- [ ] UBO 绑定（MVP、光照、材质等）
- [ ] 纹理与 Sampler 绑定
- [ ] Push Constant（小数据、per-draw）

### 4. 场景与相机

- [ ] Transform 层级与世界矩阵更新
- [ ] Camera 组件（透视、View/Projection）
- [ ] 视锥与视口
- [ ] 简单视锥剔除（可选）

### 5. 网格与模型

- [ ] 顶点格式（Position、Normal、UV、Tangent）
- [ ] 索引绘制
- [ ] OBJ 加载（或 Assimp 多格式）
- [ ] 子网格与材质索引
- [ ] glTF 加载（可选）

### 6. 纹理

- [ ] 2D 纹理加载（PNG/JPG）
- [ ] HDR 纹理（环境、IBL）
- [ ] Mipmap 生成
- [ ] Cubemap（天空、IBL）
- [ ] 纹理缓存/复用

### 7. PBR 渲染

- [ ] 金属/粗糙度着色器（Cook-Torrance）
- [ ] BaseColor / Metallic / Roughness / Normal / AO 贴图
- [ ] 方向光 + 多光源（点光/聚光）
- [ ] IBL：辐照度 + 预滤波镜面 + BRDF LUT
- [ ] 与现有 `Material` 结构对接

### 8. 阴影

- [ ] 方向光阴影贴图（单张或 CSM）
- [ ] 阴影 Pass（深度写入）
- [ ] PCF 软阴影
- [ ] 点光/聚光阴影（可选）

### 9. 后处理

- [ ] 全屏四边形 Pass
- [ ] 色调映射（Tone Mapping）
- [ ] Bloom（阈值 + 模糊）
- [ ] FXAA / TAA（可选）
- [ ] SSAO（可选）

### 10. UI

- [ ] ImGui 初始化与 Vulkan 后端
- [ ] Font Atlas 纹理与 Descriptor
- [ ] UI RenderPass 与绘制
- [ ] 调试面板（FPS、开关）
- [ ] 场景树、选中、Gizmo（ImGuizmo）
- [ ] 材质编辑面板

### 11. 性能与工具

- [ ] GPU 时间戳 / 统计
- [ ] Debug Utils（命名对象）
- [ ] 多线程 Command Buffer 录制（可选）
- [ ] 实例化绘制
- [ ] LOD（可选）

---

## 二、按阶段的实现顺序（建议）

### Phase 0：Vulkan 最小可运行

1. Instance + Validation Layer  
2. Physical Device + Logical Device + Queue  
3. Window + Surface  
4. Swapchain + Image Views  
5. RenderPass（单色或三角形）+ Framebuffer  
6. Command Buffer 录制 + 同步  
7. Present  
**验收**：窗口内显示单色或三角形，无报错。

---

### Phase 1：可渲染场景（MVP）

1. 顶点/索引 Buffer（含 Staging 上传）  
2. Uniform Buffer（MVP）  
3. DescriptorSetLayout + DescriptorPool + 绑定 MVP  
4. 简单顶点/片段着色器（带纹理采样）  
5. 相机与 Transform 计算 View/Projection  
6. 模型加载（OBJ）+ 纹理加载  
7. 深度缓冲 + 深度测试  
8. 简单光照（Blinn-Phong 或最简 PBR 单光）  
**验收**：加载一个带纹理的 OBJ，有基础光照，可旋转视角。

---

### Phase 2：完整 PBR + IBL

1. PBR 着色器（金属/粗糙度、多光源）  
2. 法线贴图、AO 贴图  
3. IBL 资源：辐照度图、预滤波环境图、BRDF LUT  
4. 在 PBR 中接入 IBL 漫反射与镜面  
5. 与 `engine/include/scene/material.h` 的 Material 字段对接  
**验收**：PBR 材质 + IBL 视觉效果正确，可调金属度/粗糙度。

---

### Phase 3：阴影 + 后处理

1. 阴影贴图（方向光）  
2. 阴影 Pass（深度）  
3. 主 Pass 中采样阴影、PCF  
4. 后处理：全屏 Pass、色调映射  
5. Bloom（可选）  
6. FXAA/TAA（可选）  
**验收**：有方向光阴影，画面经过色调映射与可选 Bloom。

---

### Phase 4：UI 与编辑器基础

1. ImGui Vulkan 后端（Descriptor、Pipeline、Font）  
2. 每帧 ImGui  NewFrame / Render / 绘制  
3. 调试面板：FPS、渲染开关  
4. 场景树：列出 GameObject，选中高亮  
5. ImGuizmo：平移/旋转/缩放选中对象  
6. 材质面板：Material 参数滑动条、贴图槽  
**验收**：可通过 UI 查看统计、选择物体、改 Transform 与材质。

---

### Phase 5：扩展与优化

- 实例化绘制  
- 视锥剔除  
- LOD  
- 更多后处理（SSAO、DOF 等）  
- 管线缓存持久化  
- GPU 时间统计  
- 资源热重载（可选）  

---

## 三、与现有 engine 的对接点

| 现有项 | 用途 |
|--------|------|
| `scene/material.h` (Material) | PBR 参数与贴图槽，直接用于 UBO/Descriptor 与着色器 |
| `scene/transform.h` | 层级与世界矩阵，驱动 MVP 与光照位置 |
| `scene/camera.h` / `scenecamera.hpp` | 相机参数与 View/Projection |
| `scene/light.h` | 光源类型与参数，驱动 UBO 与 PBR 光照计算 |
| `scene/mesh.h` | 网格数据与顶点格式，对应 Vulkan Buffer |
| `scene/gameobject.h`, `scene.h` | 场景图与渲染遍历 |
| `render/pipeline.h` | 重写为真正的 VkPipeline 封装（创建、绑定、销毁） |
| `engine/assets/shader/glsl/pbr.*.glsl` | 实现完整 PBR 顶点/片段着色器 |

---

## 四、推荐阅读与参考

- Vulkan 规范与 SDK 样例  
- [Vulkan Guide](https://vkguide.dev/)（与 examples/vkguide 对应）  
- LearnVulkan 内 examples/VulkanTutorial、sdl_cpp_VulkanTutorial  
- PBR：LearnOpenGL PBR 章节、Filament/Unreal 文档  
- ImGui：ImGui 仓库的 `examples/example_win32_dx12`、Vulkan 示例  

完成某一阶段后，可在本清单中勾选对应项，并更新 [RENDER_ENGINE_PLAN.md](RENDER_ENGINE_PLAN.md) 中的“开发阶段”小节。
