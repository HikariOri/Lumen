# 快速参考

正文排版与代码示例分别遵循 [中文文案排版指北](chinese-typography.md) 与 [C++ 编码风格参考](../reference/cpp-style.md)。

## 项目与可执行文件

| 项目 | 说明 |
|------|------|
| sandbox | 2D 纹理矩形，WASD 移动 / QE 旋转 |
| shadertoy | 全屏 Shadertoy 效果，拖鼠标控制相机 |
| demo3d | 3D 纹理立方体，WASD 轨道相机 |

构建后对应可执行文件位于 `build/<Config>/` 下。

## Vulkan 1.4 与 3D 相关

- **投影 Y 翻转**：`glm::perspective` 后需 `proj[1][1] *= -1` 适配 Vulkan NDC
- **frontFace**：使用 Y 翻转时，管线应设 `frontFace = VK_FRONT_FACE_CLOCKWISE`，否则会显示背面

## 常用 API

| 操作 | 调用 |
|------|------|
| 路径 | `lumen::core::get_resource_path("shaders/xx.spv")` |
| 时间 | `lumen::core::get_time_seconds()` |
| 日志 | `LUMEN_APP_LOG_*`（应用层）|
| 设备等待 | `ctx.wait_idle()` |
| Swapchain 重建 | `recreate_swapchain_resources(ctx, swapchain, framebuffers, frameSync, renderPass.handle(), w, h, framesInFlight, depthImageView)` |

## 文档索引

完整目录见 [文档首页](../README.md)。

- [ImGui 集成](../design/imgui-integration.md) — Vulkan + SDL3 后端、SetMinImageCount、注意事项
- [项目注意事项](project-notes.md) — 初始化顺序、Vulkan 同步、常见坑
- [日志系统](../reference/logging.md) — spdlog 封装与宏
- [Git 提交规范](git-commit-convention.md) — Conventional Commits
- [AI 协作开发](ai-assisted-workflow.md) — ChatGPT 规格 → Cursor 实现
- [SDL3 时间 API](../reference/sdl3-time.md) — 与 `get_time_seconds()` 对照
