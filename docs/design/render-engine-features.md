# 渲染引擎 — 功能特性清单与开发阶段速查

本文档是 [render-engine-roadmap.md](render-engine-roadmap.md) 的补充，提供按模块的勾选清单与按阶段的实现顺序，便于排期与验收。总体规划、架构与风险见 PLAN。

**导航**：一、功能总览（0–17） → 二、阶段顺序（Phase 0–6） → 三、模块与接口对应 → 四、推荐阅读

---

## 一、功能特性总览（按模块）

### 0. 平台（窗口与输入，SDL3）

- [ ] 窗口抽象接口（创建、尺寸、全屏、获取 Vulkan Surface 用句柄）
- [ ] 输入抽象接口（键/鼠/手柄状态或事件）
- [ ] SDL3 后端实现（SDL3 窗口与 Vulkan Surface 创建等）
- [ ] 统一事件/状态模型（应用层不直接依赖 SDL3 类型）

### 1. Vulkan 底层（Vulkan 1.4）

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

### 4a. 场景序列化

- [ ] **保存**：场景图（GameObject 层级、UUID、名称、父子关系）+ Transform（本地 TRS）+ 组件数据（Camera、Light、MeshRenderer 等）
- [ ] **资产引用**：Mesh、Material 等用资产路径或 ID 存储，加载时通过 AssetManager 解析
- [ ] **格式**：JSON（可读、版本管理）或二进制（体积小、加载快）；文件带版本号
- [ ] **加载**：解析文件 → 创建节点与组件 → 解析并加载资产引用 → 绑定渲染组件、更新世界矩阵
- [ ] **兼容**：版本号与迁移策略，旧版本可读或提示升级
- [ ] 可选：增量保存、仅保存选中子树、预制体共用格式

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
- [ ] **噪声纹理**：预烘焙或程序化生成（SSAO 旋转噪声、蓝噪声、抖动用）
- [ ] 噪声纹理在 SSAO Pass 中绑定（随机方向/偏移）
- [ ] 噪声纹理在 TAA/抖动等后处理中可选使用
- [ ] **程序化噪声（柏林/Simplex/Worley）**：CPU 或 Compute 实现，2D/3D，多 octave（FBM）
- [ ] 柏林噪声（Perlin）：地形高度、云、有机形态
- [ ] Simplex 噪声：同上，性能与质量更均衡
- [ ] Worley/Voronoi：胞元噪声，可选用于细节层
- [ ] 工具：`generate_noise` 生成预烘焙噪声图（含柏林/Simplex 等）到 `assets/textures/noise/`

### 7. PBR 渲染

- [ ] 金属/粗糙度着色器（Cook-Torrance）
- [ ] BaseColor / Metallic / Roughness / Normal / AO 贴图
- [ ] 方向光 + 多光源（点光/聚光）
- [ ] IBL：辐照度 + 预滤波镜面 + BRDF LUT
- [ ] 材质参数与 PBR 着色器、UBO/Descriptor 对接

### 7a. 材质系统

- [ ] **材质资产**：PBR 参数（BaseColor、Metallic、Roughness、AO、Emissive）+ 贴图槽，标量/贴图二选一与默认值
- [ ] **贴图槽**：BaseColor、MR/Roughness、Normal、AO、Emissive；无贴图时使用标量
- [ ] **Alpha 模式**：Opaque / Mask（alpha cutoff）/ Blend，驱动深度与混合
- [ ] **渲染状态**：背面剔除/双面、深度写、混合模式（Blend 时）
- [ ] **材质实例**：由资产派生，可覆盖部分参数（如颜色、粗糙度）
- [ ] **SubMesh 绑定**：每子网格指定一个材质实例
- [ ] **GPU 绑定**：材质 UBO 或 Push Constant；DescriptorSet 贴图+Sampler；按 Alpha/双面等选择管线变体
- [ ] **材质序列化与加载**：JSON/二进制，加载与缓存
- [ ] **同材质合批**：减少 Draw Call 与状态切换
- [ ] 可选：Clearcoat/Sheen/Transmission、材质热重载

### 8. 阴影

- [ ] 方向光阴影贴图（单张或 CSM）
- [ ] 阴影 Pass（深度写入）
- [ ] PCF 软阴影
- [ ] 点光/聚光阴影（可选）

### 9. 后处理

- [ ] 全屏四边形 Pass
- [ ] 色调映射（Tone Mapping）
- [ ] Bloom（阈值 + 模糊）
- [ ] FXAA / TAA（可选；TAA 可配合蓝噪声/抖动纹理）
- [ ] SSAO（可选；依赖随机旋转噪声纹理）

### 10. UI

- [ ] ImGui 初始化与 Vulkan 后端
- [ ] Font Atlas 纹理与 Descriptor
- [ ] UI RenderPass 与绘制
- [ ] 调试面板（FPS、开关）
- [ ] 场景树、选中、Gizmo（ImGuizmo）
- [ ] 材质编辑面板
- [ ] 拾取：点击场景视图选中物体，与场景树、Gizmo 联动

### 11. 性能与工具

- [ ] GPU 时间戳 / 统计
- [ ] Debug Utils（命名对象）
- [ ] 多线程 Command Buffer 录制（可选）
- [ ] 实例化绘制
- [ ] LOD（可选）
- [ ] **调试绘制**：线框、AABB、法线、坐标轴、Debug 线；与正式渲染可切换
- [ ] 验证层开关（配置或运行时）

### 12. 程序化地形

- [ ] 高度场：柏林或 Simplex 多 octave（frequency、persistence、lacunarity、seed）
- [ ] 高度图 → 网格：顶点 Position、法线、UV；块内无缝
- [ ] 分块（Chunk）：按世界坐标分块，按需生成与卸载
- [ ] 地形 LOD：按与相机距离降低块分辨率或密度（可选）
- [ ] 地形材质：单材质或按高度/斜率混合（splat/程序化）（可选）
- [ ] 可选：GPU Compute 生成高度、曲面细分、离线工具 `generate_terrain_heightmap`

### 13. 配置、启动与运维

- [ ] **配置文件**：分辨率、全屏、质量档位等（JSON/YAML/INI）
- [ ] 命令行参数（可选）：覆盖配置、场景路径、验证层
- [ ] **VSync / Present 模式**：垂直同步、FIFO/MAILBOX
- [ ] 帧率限制（可选）
- [ ] **日志系统**：级别、控制台与文件输出
- [ ] 崩溃与错误处理：友好提示、可选转储

### 14. 场景与编辑器扩展

- [ ] **预制体（Prefab）**：可复用场景子树、实例化与覆盖、与场景序列化兼容
- [ ] **图层/标签（Layer、Tag）**：相机裁剪层、光照层、拾取过滤、按层显示/隐藏
- [ ] **拾取（Picking）**：射线与场景相交或 ID Buffer，编辑器选中
- [ ] 多窗口/多视口（可选）

### 15. 色彩与 HDR

- [ ] 线性空间计算；sRGB 贴图采样转线性
- [ ] HDR 渲染目标（主 Color/后处理）
- [ ] 色调映射 + 可调曝光，输出 sRGB
- [ ] 高 DPI / 分辨率缩放（窗口与 UI）

### 16. 可选扩展（按需）

- [ ] 粒子系统（CPU/GPU 粒子）
- [ ] 骨骼动画与蒙皮
- [ ] 反射探针 / 平面反射
- [ ] 文本渲染（SDF 或 Mesh 文本）
- [ ] 纹理压缩（BC/ASTC）与流式加载

### 17. 离屏渲染与导出（图像/视频）

- [ ] 离屏渲染目标：VkImage + VkFramebuffer + RenderPass，可配置分辨率与格式
- [ ] 主管线输出到离屏目标（可选路径，与 Swapchain 二选一或并存）
- [ ] 无窗口 / Headless 模式（可选）
- [ ] 多视口/多相机输出到多张离屏图（可选）
- [ ] 截图：Color 读回 CPU（CopyImageToBuffer），保存 PNG/JPG/HDR
- [ ] 高分辨率或排除 UI 的截图（可选）
- [ ] 视频录制：连续帧读回并编码为 MP4/WebM（FFmpeg 或 OpenCV）
- [ ] 导出 FPS、分辨率可配置；可选异步编码与 UI 状态/报错反馈

---

## 二、按阶段的实现顺序（建议）

### Phase 0：Vulkan 1.4 最小可运行

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

1. 平台层：窗口与输入抽象 + SDL3 后端，能创建 Vulkan Surface。  
2. 基础配置与日志：配置文件（或硬编码默认）、日志级别与输出。  
3. 顶点/索引 Buffer（含 Staging 上传）  
4. Uniform Buffer（MVP）  
5. DescriptorSetLayout + DescriptorPool + 绑定 MVP  
6. 简单顶点/片段着色器（带纹理采样）  
7. 相机与 Transform 计算 View/Projection  
8. 模型加载（OBJ）+ 纹理加载  
9. 深度缓冲 + 深度测试  
10. 简单光照（Blinn-Phong 或最简 PBR 单光）  
**验收**：加载一个带纹理的 OBJ，有基础光照，可旋转视角；SDL3 窗口与 Vulkan Surface；有日志与基本配置。

---

### Phase 2：完整 PBR + IBL

1. PBR 着色器（金属/粗糙度、多光源）  
2. 法线贴图、AO 贴图  
3. IBL 资源：辐照度图、预滤波环境图、BRDF LUT  
4. 在 PBR 中接入 IBL 漫反射与镜面  
5. 材质系统与 PBR 着色器、UBO/Descriptor 对接  
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
5. 拾取：射线或 ID Buffer 实现点击选中，与场景树、Gizmo 联动  
6. ImGuizmo：平移/旋转/缩放选中对象  
7. 材质面板：材质参数滑动条、贴图槽、实例覆盖  
**验收**：可通过 UI 查看统计、点击选中物体、改 Transform 与材质。

---

### Phase 5：扩展与优化

- 实例化绘制  
- 视锥剔除  
- LOD  
- 更多后处理（SSAO、DOF 等）  
- 程序化噪声（柏林/Simplex）库与噪声纹理管线  
- 管线缓存持久化  
- GPU 时间统计  
- 资源热重载（可选）

### Phase 5a：程序化地形（可选）

- 柏林/Simplex 高度场生成（seed、多 octave 参数）  
- 单块高度图 → 网格（顶点/法线）  
- 分块与按需加载、块边界无缝  
- 地形 LOD、地形材质（可选）  
- **验收**：可配置 seed 与尺度，生成连续地形并渲染  

---

### Phase 6：离屏渲染与导出（图像/视频）

1. 离屏目标：创建离屏 VkImage（Color/Depth）、Framebuffer、与主 RenderPass 兼容的附件。  
2. 渲染路径：支持“渲染到离屏”模式（不依赖 Swapchain 或与 Present 并存）。  
3. 读回：CopyImageToBuffer，Host 可见 Buffer，按行/格式转成 RGB/RGBA。  
4. 导出图像：stb_image_write 或 libpng 写出 PNG/JPG；可选 HDR 写出。  
5. 导出视频：按固定 FPS 收集帧，调用 FFmpeg/OpenCV 编码为 MP4/WebM。  
6. 可选：异步编码、导出进度与错误在 UI 中显示。  
**验收**：可指定分辨率离屏渲染，支持单帧保存为图片、多帧录制为视频；导出失败时有明确提示。

---

## 三、核心模块与接口对应

| 模块/类 | 用途 |
|--------|------|
| Material / MaterialAsset / MaterialInstance | PBR 参数与贴图槽、资产与实例、用于 UBO/Descriptor 与着色器 |
| Transform | 层级与世界矩阵，驱动 MVP 与光照位置 |
| Camera | 相机参数与 View/Projection |
| Light | 光源类型与参数，驱动 UBO 与 PBR 光照计算 |
| Mesh | 网格数据与顶点格式，对应 Vulkan Buffer |
| Scene / GameObject | 场景图与渲染遍历 |
| SceneSerializer | 场景序列化/反序列化（保存与加载、资产引用、版本） |
| Prefab / Layer、Tag | 预制体实例化与覆盖；图层/标签用于裁剪、光照、拾取过滤 |
| Picking | 射线或 ID Buffer 拾取，编辑器选中物体 |
| Config / Logger | 配置加载、日志级别与输出、错误处理 |
| Pipeline | Vulkan 管线封装（创建、绑定、销毁） |
| PBR 着色器 | 顶点/片段着色器，金属/粗糙度工作流 |

---

## 四、推荐阅读与参考

- Vulkan 规范与 SDK 样例  
- [Vulkan Guide](https://vkguide.dev/)  
- PBR：LearnOpenGL PBR 章节、Filament/Unreal 文档  
- ImGui：ImGui 仓库 Vulkan 示例  

完成某阶段后，在本清单中勾选对应项，并同步更新 [render-engine-roadmap.md](render-engine-roadmap.md) 第 8 节「开发阶段与里程碑」。

