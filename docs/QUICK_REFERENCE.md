# 快速参考

## 项目与可执行文件

| 项目 | 说明 |
|------|------|
| sandbox | 2D 纹理矩形，WASD 移动 / QE 旋转 |
| shadertoy | 全屏 Shadertoy 效果，拖鼠标控制相机 |
| demo3d | 3D 纹理立方体，WASD 轨道相机 |

构建后对应可执行文件位于 `build/<Config>/` 下。

## Vulkan 3D 相关

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

- [NOTES.md](NOTES.md) — 开发注意事项
- [LOGGER.md](LOGGER.md) — 日志系统
- [COMMIT_CONVENTION.md](COMMIT_CONVENTION.md) — 提交规范
