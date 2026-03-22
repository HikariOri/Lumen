# Lumen 文档索引

**编写规范**：正文中文排版遵循 [guides/chinese-typography.md](guides/chinese-typography.md)；文内 C++ 示例遵循 [reference/cpp-style.md](reference/cpp-style.md)。**技术栈表述**应与工程一致：**C++23**、**Vulkan 1.4**（见根目录 `CMakeLists.txt` 与 `engine/include/render/context.hpp`）。

文档按用途分为三类目录：

| 目录 | 用途 |
|------|------|
| [guides/](guides/) | 日常开发：快速参考、注意事项、提交规范、协作流程、中文排版 |
| [reference/](reference/) | 与代码直接对照：事件/输入、日志、GLM↔Vulkan、C++ 风格、SDL3 时间 |
| [design/](design/) | 子系统设计与路线图：渲染、RenderGraph、UI/ImGui、Scene/ECS、材质等 |

## 新手入门（推荐阅读顺序）

1. [guides/quick-reference.md](guides/quick-reference.md) — 可执行目标与常用 API  
2. [guides/project-notes.md](guides/project-notes.md) — 初始化与 Vulkan 坑点  
3. [reference/glm-vulkan.md](reference/glm-vulkan.md) — 投影、frontFace、UBO  
4. [design/imgui-integration.md](design/imgui-integration.md) — ImGui + Vulkan + SDL3  

## guides

| 文档 | 说明 |
|------|------|
| [quick-reference.md](guides/quick-reference.md) | 项目列表、3D 要点、API 速查 |
| [project-notes.md](guides/project-notes.md) | 工程注意事项 |
| [git-commit-convention.md](guides/git-commit-convention.md) | Conventional Commits |
| [ai-assisted-workflow.md](guides/ai-assisted-workflow.md) | ChatGPT 出规格 → Cursor 落地 |
| [cursor-end-to-end-workflow.md](guides/cursor-end-to-end-workflow.md) | 全程在 Cursor：设计 → 实现 → 验证 → 提交 |
| [chinese-typography.md](guides/chinese-typography.md) | 中文文案排版指北 |

## reference

| 文档 | 说明 |
|------|------|
| [event-input.md](reference/event-input.md) | 事件泵、Input、ImGui 分层 |
| [logging.md](reference/logging.md) | 日志实现与扩展路线 |
| [glm-vulkan.md](reference/glm-vulkan.md) | GLM 与 Vulkan 适配 |
| [cpp-style.md](reference/cpp-style.md) | C++ 编码风格参考 |
| [sdl3-time.md](reference/sdl3-time.md) | SDL3 时间与引擎 `get_time_seconds()` |

## design

| 文档 | 说明 |
|------|------|
| [render-engine-roadmap.md](design/render-engine-roadmap.md) | 渲染引擎总体规划 |
| [render-engine-features.md](design/render-engine-features.md) | 功能清单与阶段勾选 |
| [render-graph.md](design/render-graph.md) | RenderGraph 概念、与引擎对照、系统设计 |
| [scene-ecs.md](design/scene-ecs.md) | Scene 与 ECS |
| [vulkan-materials.md](design/vulkan-materials.md) | Vulkan 材质系统 |
| [imgui-integration.md](design/imgui-integration.md) | ImGui 后端与 3D 纹理绘制 |
| [ui-panels.md](design/ui-panels.md) | UI 面板与资源面板约定 |
| [editor-ui.md](design/editor-ui.md) | Editor UI（Console、Command 等）递进设计 |
