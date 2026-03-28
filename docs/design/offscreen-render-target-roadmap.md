# OffscreenRenderTarget 大改路线图（预留）

本文记录与 `OffscreenRenderTarget` / 离屏渲染相关的**远期架构方向**，供多视口、GBuffer、与 RenderGraph 深度整合时参考。实施前请与 [`render-graph.md`](./render-graph.md) 及当前引擎模块边界对齐。

## 目标

- 离屏资源与 **屏障 / layout 调度** 由统一图或调度器负责，减少手写 `cmdPipelineBarrier` 分叉。
- **多离屏目标** 共享等价 `VkRenderPass` 时由**缓存或显式注册表**管理，避免调用方重复配置。
- **对象生命周期** 表达更清晰（如 `std::expected` / 工厂函数、`SwapchainRenderTarget` 非裸指针）。

## 建议阶段

### 阶段 A：Pass 与 Framebuffer 分离（中改已落地）

- 调用方可注入共享 `RenderPass`：`create(ctx, config, const RenderPass *shared_render_pass)`；`resize(w, h)` 使用 `create` 时记录的 `Context`，不再传 `Context&`。
- 示例：`examples/demo3d`、`examples/cube3d_imgui` 对离屏目标共用一份 `offscreenRenderPass`。
- 后续可选：`RenderPassCache`（key = format + useDepth + finalLayout），对无注入场景自动去重。

### 阶段 B：与 RenderGraph 对齐

- 离屏 `Image` / view 作为 **Graph 资源节点**；pass 内 `initialLayout` / `finalLayout` 与图的状态跟踪一致（见 `render_graph` 模块）。
- `OffscreenRenderTarget` 退化为 **资源句柄 + 尺寸**，或合并为 Graph 的 `RenderTarget` 描述子。

### 阶段 C：API 与类型安全

- `create` / `resize` 错误路径：`std::expected` 或专用 `Error` 类型，减少「返回 `false` + 半初始化」状态。
- `resize` 仅依赖 `create` 记录的 `Context`（中改已采用无 `Context` 参数的 `resize`）。
- `SwapchainRenderTarget`：`Swapchain` + `span<Framebuffer>` 或引用成员，文档化寿命。

### 阶段 D：多附件 / MSAA / MRT

- 配置从单一 color + 可选 depth 扩展为附件列表；与延迟渲染、多 RT 一致。

## 参考文件

- [`engine/include/render/pass/render_target.hpp`](../../engine/include/render/pass/render_target.hpp)
- [`docs/design/render-graph.md`](./render-graph.md)
