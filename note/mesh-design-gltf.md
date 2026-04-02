# Mesh 系统设计（glTF 对齐版）

本文在 `glftMesh系统.md`、`Mesh Buffer.md`、`submesh.md` 三份笔记基础上合并、去重，并**按 glTF 2.0 语义**给出可落地的运行时模型；命名与职责尽量与仓库中 `gltf/mesh_asset.hpp`、`scene/submesh.hpp`、`gltf/gltf_scene_mesh.hpp` 一致。

---

## 1. 设计目标

- **一一映射**：glTF 的 `mesh` / `primitive` / `material` / accessor 语义在资源层有明确对应，导入后不依赖 Node 树做绘制决策。
- **绘制粒度正确**：一次 `vkCmdDrawIndexed` 对应一个 **Primitive**（≈ glTF primitive），而不是整个 `Mesh`。
- **共享几何缓冲**：多个 `Mesh` / `Primitive` 可驻留在同一块大 **VertexBuffer** / **IndexBuffer** 中，通过字节偏移与索引范围引用。
- **场景表达可选两种模式**：简单实例用「整 Mesh 渲染」；需要按 primitive 独立变换、显隐、材质覆盖或 Picking 时用 **SubMesh 实体**。

---

## 2. 概念分层

| 层级 | 含义 | glTF 关系 |
|------|------|-----------|
| **Asset（资源）** | `Model` / `Mesh` / `Primitive` / `Material` / 纹理 | 由 glTF 加载一次，可被多处引用 |
| **Scene（场景 / ECS）** | `Transform`、父节点、`MeshRenderer` 或 `SubMeshRendererComponent` | glTF `Node` 映射为实体与层级，**不**把 glTF JSON 结构当运行时权威 |
| **Render（渲染）** | 扁平 `RenderItem` 列表 | 每帧或每阶段由 ECS + `MeshBuffer` 展开为 draw |

核心句式：

- **Mesh 存拓扑与对缓冲的引用区间；Primitive 决定怎么画；Entity 决定在哪画（及是否按子图元拆开）；RenderItem 决定管线与 draw 顺序。**
- **Material 属于 Primitive**（默认），而非「整个 Entity 一个材质」——否则多 primitive 多材质会错。

---

## 3. glTF → 运行时映射表

### 3.1 资源侧

| glTF | 运行时（Lumen 对齐） | 说明 |
|------|----------------------|------|
| `asset.meshes[i]` | `Model[i]` 或 `Mesh` 池中的第 `i` 个 | 保持与 glTF `meshes` 数组下标一致便于调试 |
| `mesh.primitives[j]` | `Mesh::primitives[j]` | 顺序一致；跳过无索引或空 layout 的项时需在加载器与 `is_drawable()` 上统一 |
| primitive 的 `attributes` / `indices` | `Primitive::layout` + `vertexByteOffset` / `firstIndex` / `indexCount` / `baseVertex` | 顶点格式用动态 `VertexLayout`，避免固定 C++ struct 绑死 glTF |
| primitive 的 `material` | `Primitive::material` | 指针指向共享 `Material` |
| primitive 的 `mode` | `Primitive::topology`（如 `TRIANGLE_LIST`） | 须与对应 `GraphicsPipeline` 一致 |
| accessors / bufferViews | 上传到大缓冲后的**子范围** | 不在 `Primitive` 上持有 `VkBuffer`，只保留偏移与计数 |

### 3.2 材质与扩展

- **PBR Metallic-Roughness**：`baseColorFactor`、`metallicFactor`、`roughnessFactor`、各纹理与 `alphaMode`、`doubleSided` 等，集中在 `Material`（与 primitive 引用配合）。
- **Morph / Skin**：本文聚焦几何与 draw 粒度；蒙皮与 morph 权重可作为**同一 `Mesh` 的附加顶点流或 per-instance 数据**，在 `VertexLayout` 与 shader 中扩展，不改变「一 primitive 一 draw」的主线。

### 3.3 场景侧（Node）

- glTF：`Node` →（可选）`mesh` 索引 + 局部变换 + 子节点。
- 引擎：**一个 Node 对应一个（或一棵）实体**；若该 Node 引用 `mesh`：
  - **模式 A**：父实体挂 `MeshRenderer`（或等价组件），指向共享 `Mesh*`，渲染时遍历 `mesh->primitives` 展开多条 `RenderItem`。
  - **模式 B**：父实体仅作变换与层级；为每个**可绘制** `primitive` 创建一个**子实体**，挂 `SubMeshRendererComponent`（`mesh` + `primitiveIndex`），便于 per-primitive 的局部矩阵、材质覆盖、显隐与 Picking。

---

## 4. 核心类型（逻辑模型）

以下与实现细节无关的「字段含义」说明；具体类型名以代码为准。

### 4.1 `MeshBuffer`（共享缓冲视图）

- 保存对**全局**（或场景级）`VertexBuffer` / `IndexBuffer` 的非拥有指针。
- **作用**：绘制路径先 `vkCmdBindVertexBuffers` / `vkCmdBindIndexBuffer` 绑定大缓冲，再对每个 `Primitive` 使用其偏移与 `vkCmdDrawIndexed`。
- **生命周期**：由加载器或资源持有者创建与销毁；`Mesh` / `Primitive` 不拥有 GPU 缓冲对象。

### 4.2 `Primitive`（最小 draw 单元）

应表达 glTF primitive 在 GPU 上的**视图**，而非重复存储几何：

- **位置**：`vertexByteOffset`（绑定时的字节偏移）、`firstIndex`、`indexCount`、`baseVertex`（`vkCmdDrawIndexed` 的 vertex offset）。
- **格式**：`VertexLayout`（多 attribute、多 format，对齐 glTF 动态 attribute）。
- **材质**：`material` 指针（默认来自 glTF primitive）。
- **拓扑**：`topology` 与管线一致。
- **可选**：`localPivot` / `localAabbHalfExtent` 等 mesh 空间包围信息，供编辑器与 Gizmo 使用。

**禁止**：在 `Primitive` 内长期存放独立 `VkBuffer` 句柄作为「唯一几何来源」（与共享缓冲策略冲突）。

### 4.3 `Mesh`

- `std::vector<Primitive> primitives`；语义等同于 glTF 的一个 `mesh`。
- 不包含「世界矩阵」；变换只属于场景 / ECS。

### 4.4 `Model`

- `std::vector<Mesh>`（`lumen::gltf::Model`），与 glTF `meshes` 数组对齐（见 `gltf/mesh_asset.hpp`）。

### 4.5 `Material`

- 由 glTF `material` 填充；被多个 `Primitive` 共享引用。

---

## 5. 几何上传与共享缓冲流程（加载器）

与 `Mesh Buffer.md` 一致，并与 glTF 数据流结合：

1. **解析 glTF**：按 `mesh` → `primitive` 读取 accessor（`POSITION`、`NORMAL`、`TEXCOORD_0`、切线等）与 `indices`，确定每个 primitive 的顶点个数、索引个数、索引类型（`ushort` / `uint`）。
2. **交错或分块写入 staging**：按引擎 `VertexLayout` 规则打包顶点；注意 **stride 对齐** 与 attribute offset，与 shader `location` 一致。
3. **分配子范围**：在全局 VB / IB 上分配连续区间，记录每个 primitive 的 `vertexByteOffset`、`firstIndex`、`indexCount`、`baseVertex`（若使用「整 mesh 一段 VB + 索引引用局部顶点」的约定，与加载器实现保持一致）。
4. **拷贝到 device-local**：`staging → device local`，静态网格**不要每帧重建**。
5. **索引类型**：若 glTF 使用 `UNSIGNED_SHORT` 而全局 IB 为 32-bit，加载器需统一约定（例如在 IB 中仍用 32-bit 存储，或在绑定时使用对应 `VkIndexType`）；**同一 draw 内**索引类型必须与 `vkCmdBindIndexBuffer` 一致。

加载完成后：**`MeshBuffer` 与所有相关 `Mesh` 的几何来源一致**（例如同一 `GltfSceneMesh::geometry()`），避免 bind 与 offset 错位。

---

## 6. ECS 与 SubMesh（`submesh.md` 与引擎对齐）

### 6.1 `SubMeshRendererComponent` 语义

- `mesh`：共享的 `Mesh*`（**不要** per-entity 复制 `Mesh` 数据）。
- `primitiveIndex`：指向 `mesh->primitives[primitiveIndex]`。
- `materialOverride`：可选；非空时覆盖 `Primitive::material`（编辑器、换皮、调试）。

子实体默认局部变换为单位矩阵；**世界矩阵 = 父链 × 局部**，由场景图系统计算。

### 6.2 何时用「子实体 per primitive」

- 需要对**单个 glTF primitive** 做独立变换、显隐、材质覆盖或 Picking。
- 需要 Inspector 树与「子网格」一一对应。

### 6.3 何时用「单实体 + 整 Mesh」

- 静态模型、无 per-primitive 需求时，**更少实体、更省遍历**；渲染阶段仍应展开为多个 `RenderItem`（每个 primitive 一条）。

### 6.4 常见错误

- 在 **Entity** 上挂「唯一 `Material*`** 作为该 Node 的唯一材质，且该 Node 引用多 primitive mesh → 与 glTF 语义冲突。
- 在 `Primitive` 上挂「子图元 Transform」→ 变换应落在 **Entity**（子实体或父实体策略），保持 ECS 单一数据源。
- **复制 Mesh 资源** 而非共享指针 → 浪费内存且破坏与全局缓冲 offset 的一致性。

---

## 7. `RenderItem` 与渲染循环

每帧（或可见性剔除后）将 ECS 压平为：

- 指向或拷贝绘制所需：`Primitive` 引用、`Material`（含 override 解析结果）、`MeshBuffer`、世界矩阵、`pipelineKey`、可选 `entity`（Picking）。
- 排序键建议包含：**透明 / 不透明**、管线状态、`Material`、深度等，避免过度切换。

绘制伪流程：

1. 绑定当前 primitive 对应管线；
2. `vkCmdBindVertexBuffers(..., meshBuffer.vertexBuffer, offsets = primitive.vertexByteOffset)`；
3. `vkCmdBindIndexBuffer(meshBuffer.indexBuffer, ...)`；
4. `vkCmdBindDescriptorSets`（材质与帧常量等）；
5. `vkCmdDrawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance)`。

**不要在渲染循环里遍历 glTF Node 树**；只遍历 `RenderItem` 或用于生成它的 ECS 视图。

---

## 8. glTF 特有注意点（「完美适配」检查清单）

- **多 primitive 多材质**：每个 primitive 独立 `material` 索引 → 独立 `Primitive::material`。
- **动态顶点格式**：`morphTargets`、多种 `TEXCOORD`、`COLOR`、`JOINTS`/`WEIGHTS` 等 → `VertexLayout` 必须可配置；无法兼容的管线应分 **pipeline variant**（或拆分 primitive）。
- **`doubleSided` / `alphaMode`**：影响管线与排序，属于 `Material` + RenderQueue 策略，不改变「primitive = draw 单元」。
- **无索引 primitive**：glTF 允许 `mode` 无 indices；若引擎仅支持 indexed draw，加载器需生成索引或标记为 `!is_drawable()` 并明确日志。
- **Node 与 Mesh 实例**：同一 `mesh` 可被多个 Node 引用 → 运行时多实体共享同一 `Mesh*`，变换各自独立。

---

## 9. 演进路径（与三份原笔记对应）

| 阶段 | 内容 |
|------|------|
| 基础 | `Mesh` / `Primitive` / `VertexLayout` + glTF 加载 → 共享 VB/IB + offset |
| 场景 | `Transform` + `MeshRenderer` 或 `SubMeshRendererComponent` + 父实体 |
| 渲染 | `RenderItem` 扁平化 + 排序 + `MeshBuffer` bind |
| 进阶 | RenderQueue（opaque / transparent）、PipelineKey、Descriptor 策略；再往后 bindless / GPU-driven 仍保持「逻辑上」primitive 边界清晰 |

---

## 10. 一句话总结

**glTF 只在导入阶段权威；运行时以 `Mesh` / `Primitive` + 共享 `MeshBuffer` 表达几何，以 ECS 表达实例与可选子图元，以 `RenderItem` 表达实际 draw——这样与 glTF 的 Node → Mesh → Primitive → Material 链条一致，且满足 Vulkan 批量化与编辑器需求。**

---

## 11. 实现状态（引擎）

- **资源类型**：`lumen::gltf::Mesh` / `Primitive` / `Model` / `MeshBuffer` 定义于 `gltf/mesh_asset.hpp`。
- **加载**：`load_gltf_scene_mesh` 已按 `meshes[]` 在 mesh 局部空间打包几何，并填充 `GltfSceneMesh::scene_nodes`。
- **场景**：`spawn_gltf_scene`（`scene/gltf_spawn.hpp`）将节点写入 ECS；默认对含 mesh 的节点调用 `attach_submesh_children`；`GltfSpawnOptions::attach_submesh_children == false` 时在节点上挂 `MeshRendererComponent`（整网 + `append_mesh_render_items` 按 primitive 展开）。
- **渲染收集**：`lumen::scene::RenderItem`（`scene/render_item.hpp`）与 `MeshRendererComponent` / `SubMeshRendererComponent`。

已移除旧的 `core::load_gltf` / `CpuMesh` / `PrimitiveSlice` 管线。
