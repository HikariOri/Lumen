# Git 提交规范（Conventional Commits）

本项目使用 [Conventional Commits](https://www.conventionalcommits.org/) 管理提交信息，便于生成 CHANGELOG、语义化版本、回溯历史与代码审查。推荐使用 VSCode / Cursor 扩展 **vscode-conventional-commits** 辅助填写。

---

## 1. 提交信息格式

```
<type>(<scope>): <subject>

[optional body]

[optional footer]
```

| 部分 | 必须 | 说明 |
|------|:----:|------|
| type | 必填 | 提交类型 |
| scope | 必填 | 影响范围（**本项目要求填写**；跨模块或难以归类时用 `core`、`chore` 等） |
| subject | 必填 | 简短描述（祈使语气，**推荐英文**：小写开头、句末不加句号，约 50 字内） |
| body | 不必 | 详细说明（可选，约 72 字符换行；中英文均可） |
| footer | 不必 | 关联 issue、`BREAKING CHANGE` 等 |

与标准 Conventional Commits 的差异：标准中 `scope` 为可选，本仓库为便于定位问题，**约定每次提交都带 scope**。

---

## 2. 类型（type）

| type | 含义 | 示例 |
|------|------|------|
| `feat` | 新功能 | `feat(renderer): add forward rendering path` |
| `fix` | 修复 bug | `fix(memory): resolve staging buffer leak` |
| `docs` | 仅文档 | `docs(renderer): update pipeline usage guide` |
| `style` | 格式/风格（不影响逻辑） | `style: align clang-format with project` |
| `refactor` | 重构（不新增功能、不修 bug） | `refactor(rendergraph): simplify pass scheduling` |
| `perf` | 性能优化 | `perf(renderer): reduce draw call overhead` |
| `test` | 测试相关 | `test(buffer): add allocator unit tests` |
| `build` | 构建系统、CMake、依赖 | `build: remove GLFW dependency` |
| `ci` | CI/CD 配置 | `ci: add Windows build workflow` |
| `chore` | 杂项（脚本、配置等，不改变业务逻辑） | `chore: update .gitignore` |

`feat` 与 `fix` 最常用于版本推导（通常对应 minor / patch）。说明参考 [Conventional Commits 说明](https://www.conventionalcommits.org/)。

---

## 3. 范围（scope）

### 3.1 与工程模块对应（目录/领域）

改动主要落在下列模块之一时，优先选用对应 scope：

| scope | 说明 |
|-------|------|
| `renderer` | 渲染路径、上下文、Swapchain、与「整体渲染」相关且未落到下表细项时 |
| `platform` | 窗口、输入、事件、平台抽象 |
| `scene` | 场景图、实体、Transform 等 |
| `asset` | 模型、纹理、着色器加载与资源管线 |
| `ui` | ImGui 等 UI 集成 |
| `sandbox` | 示例 / sandbox 应用 |
| `math` | 数学库 |
| `core` | 核心框架、跨模块基础设施 |
| `tools` | 工具链、离线工具 |
| `build` / `ci` | 若提交主体就是构建或 CI，可与 type 呼应（也可用 `chore`） |

历史提交若使用 `render` 与 `renderer` 混用，新提交建议以 **`renderer`** 表示「渲染相关总称」，细粒度见下节。

### 3.2 渲染子系统（细粒度，推荐）

当改动集中在渲染管线内部某一子系统时，**优先**使用更细的 scope，便于检索与 review：

| scope | 说明 |
|-------|------|
| `vulkan` | Vulkan API 封装 |
| `rendergraph` | RenderGraph / pass 调度 |
| `pipeline` | 管线、PSO |
| `shader` | Shader 与编译 |
| `resource` | Buffer、Image 等资源对象 |
| `memory` | 内存分配、VMA 等 |
| `descriptor` | 描述符布局、绑定 |
| `sync` | Barrier、Semaphore、Fence 等同步 |
| `framegraph` | 帧级结构与调度（若与 rendergraph 区分使用） |

---

## 4. subject 与正文约定

**subject（首行描述）**

- 说明「做了什么」，而不是「为什么」或泛泛的「改了代码」。
- 推荐英文、小写开头、句末无句号；与 commitlint、changelog 工具兼容最好。
- 使用祈使语气；避免重复 type 含义（不宜写 `fix: fix leak`，应写 `fix(memory): resolve buffer leak`）。

**body（可选）**

- 可补充背景、实现要点或列表项。

**footer（可选）**

- 不兼容变更使用 `BREAKING CHANGE:` 开头说明迁移方式。
- 可关闭或引用 issue（视团队钩子配置而定）。

**破坏性变更示例：**

```
feat(platform): migrate to SDL3-only windowing

BREAKING CHANGE: USE_GLFW is removed; use SDL3 backend only
```

**多行 body 示例：**

```
refactor(vulkan): adopt Vulkan 1.4 Properties2 chain

- Use vkGetPhysicalDeviceProperties2 with pNext
- Pass Features2 via pNext at device creation
```

---

## 5. 提交原则

1. **原子提交**：一次提交只做一件事，便于 `git bisect` 与回滚。不要将「新功能 + 无关修复」写在同一 subject 里。
2. **描述结果**：写清问题域与行为，避免 `fix: change code`、`chore: update` 等模糊信息。
3. **带齐 scope**：与第 1 节一致，强制 scope，利于按模块过滤历史。

**反例与正例：**

```
# 不好
feat: add renderer + fix bug

# 更好
feat(renderer): add deferred shading path
fix(memory): fix buffer leak in allocator
```

---

## 6. 推荐工作流（VSCode / Cursor）

1. 安装扩展：**vscode-conventional-commits**
2. `Ctrl + Shift + P`（macOS：`Cmd + Shift + P`）
3. 执行 **Conventional Commits**，按向导填写 type、scope、subject

其他可选工具：**GitLens**、**Commitizen**（需项目配置）。

---

## 7. 与工具集成

- **commitlint**：可在 CI 或 `commit-msg` 钩子中校验格式与本仓库的 scope 约定。
- **conventional-changelog**：按本规范生成 CHANGELOG。
- 若 subject 统一为英文，规则与社区预设最接近，配置成本最低。

---

## 8. 参考

- [Conventional Commits](https://www.conventionalcommits.org/)
- [Angular Commit Guidelines](https://github.com/angular/angular/blob/main/CONTRIBUTING.md#commit)

---

## 9. 速查示例（本项目风格）

```
feat(rendergraph): add pass dependency tracking
fix(memory): resolve staging buffer leak
perf(renderer): reduce pipeline state switches
docs(renderer): document transient resource usage
```
