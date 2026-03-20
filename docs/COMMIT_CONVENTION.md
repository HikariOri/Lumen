# Git 提交规范指南

> 统一提交信息格式，便于生成 changelog、回溯历史、协作与代码审查。

---

## 1. 提交信息格式

```
<type>(<scope>): <subject>

[optional body]

[optional footer]
```

- **type**：提交类型（必填）
- **scope**：影响范围（可选，小写）
- **subject**：简短描述（必填，50 字内，祈使句，不加句号）
- **body**：详细说明（可选，72 字换行）
- **footer**：关联 issue、破坏性变更说明（可选）

---

## 2. 类型（type）

| 类型 | 说明 | 示例 |
|------|------|------|
| `feat` | 新功能 | `feat(render): 添加 PBR 金属/粗糙度管线` |
| `fix` | Bug 修复 | `fix(swapchain): 窗口 resize 后正确重建` |
| `docs` | 仅文档 | `docs: 添加 Vulkan 1.4 与 SDL3 说明` |
| `style` | 代码风格（不影响逻辑） | `style: 统一 clang-format` |
| `refactor` | 重构（不新增功能、不修 bug） | `refactor(context): 使用 Properties2 链` |
| `perf` | 性能优化 | `perf(buffer): 合并小 Buffer 上传` |
| `test` | 测试相关 | `test: 添加 Buffer 单元测试` |
| `build` | 构建、依赖、CMake | `build: 移除 GLFW 依赖` |
| `ci` | CI/CD 配置 | `ci: 添加 Windows 构建流程` |
| `chore` | 杂项（脚本、配置等） | `chore: 更新 .gitignore` |

---

## 3. 范围（scope）

建议按模块划分，与代码目录对应：

| scope | 说明 |
|-------|------|
| `render` | Vulkan 上下文、管线、Swapchain、资源 |
| `platform` | 窗口、输入、事件 |
| `scene` | 场景图、GameObject、Transform |
| `asset` | 模型、纹理、着色器加载 |
| `ui` | ImGui 集成 |
| `sandbox` | 示例应用 |
| *(空)* | 跨模块或无法归入单一范围 |

---

## 4. 示例

**功能**
```
feat(render): 添加各向异性采样与 Sampler 配置
```

**修复**
```
fix(platform): 高 DPI 下鼠标 delta 计算错误
```

**破坏性变更（BREAKING CHANGE）**
```
feat(platform): 移除 GLFW，仅保留 SDL3

BREAKING CHANGE: 不再支持 USE_GLFW，需使用 SDL3 后端
```

**多行说明**
```
refactor(context): 升级至 Vulkan 1.4 Properties2/Features2

- 使用 vkGetPhysicalDeviceProperties2 与 pNext 链
- 设备创建通过 pNext 传递 Features2
- 保留 physical_device_properties() 兼容接口
```

---

## 5. 约定与建议

1. **使用祈使句**：用「添加」「修复」「更新」等，不用「添加了」「修复了」。
2. **首字母小写**：subject 首字母小写（除非专有名词）。
3. **不重复 type 含义**：避免 `fix: 修复 xxx`，直接用 `fix: xxx`。
4. **原子提交**：一次提交只做一件事，便于 bisect 与 revert。
5. **复杂变更**：可在 body 中分条列出，或拆成多个小提交。

---

## 6. 与工具集成

- **commitlint**：可配置校验提交格式。
- **conventional-changelog**：按本规范生成 changelog。
- **VSCode / Cursor**：使用扩展（如 GitLens、Commitizen）辅助规范提交。

---

## 7. 参考

- [Conventional Commits](https://www.conventionalcommits.org/)
- [Angular Commit Guidelines](https://github.com/angular/angular/blob/main/CONTRIBUTING.md#commit)
