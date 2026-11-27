# 54_CineRTX Phase 13 RIFE 120fps 选项闭环与防静默回落返工汇报

> **汇报编号**：`CINERTX-P13-REWORK-REPORT-054`  
> **施工依据**：`51_CineRTX RIFE 120fps 选项失效与回落 NVOFA 60fps 专家排障委托书.md`、`52_fix_wave1-A_队列Pending参数冻结解除与RIFE路径防静默回落施工单.md`、`53_fix_wave1-B_下拉框绑定闭环_单测回归与全仓门禁封板施工单.md`  
> **基线锚定**：Commit `a6451b7`（`153/153 PASS`、`AuditTypes = 34/35`、`AuditImports` GUI `<= 4` / HEADLESS `== 2`）

---

## 一、 根因闭环与代码级返工明细

### 1.1 Wave 1-A 靶向手术 1：解除队列 `Pending` 参数冻结陷阱（`src/gui/rtx_gui.cpp`）

1. **单文件拖拽不提前冻结 `Pending` 快照**（`src/gui/rtx_gui.cpp:L698-L716`）：
   * `WM_DROPFILES` 在 `paths.size() == 1u` 时仅填充 `IDC_EDT_INPUT` / 自动推导 `IDC_EDT_OUTPUT` / 探针探测媒体与更新预览，**不再**以默认 `"60-nvofa"` 提前向 `app.jobQueue` 压入冻结的 `Pending` 任务。
   * 当 `paths.size() > 1u` 时保持批量多文件入队行为，兼顾批处理直觉。
2. **`onStartTranscode()` 单 `Pending` 任务实时同步 + 多任务异构隔离**（`src/gui/rtx_gui.cpp:L360-L381`）：
   * 启动前扫描 `app.jobQueue.snapshot()` 中的 `JobStatus::Pending` 列表：
     * 恰好存在 1 个 `Pending` 任务时（例如用户通过 `+` 先入队再改下拉框为 `120-rife`），同步调用 `app.jobQueue.updatePendingJob(jid, synced)` 将最新 `guiProfile(app)` 写入该任务快照（保留该任务自有输入路径与合法输出路径）。
     * 存在 `>= 2` 个 `Pending` 任务时，不覆盖各任务独立快照，严格维持 Wave 3-B 异构多任务隔离契约。
     * 无 `Pending` 任务时，直接以最新 `guiProfile(app)` 创建新任务并启动。
3. **队列列表项实时展示 `fps_mode`**（`src/gui/rtx_gui.cpp:L603-L608`）：`refreshQueueListbox()` 标签追加 `/<fps_mode>`（如 `[av1/cq/QP24/120-rife]`），提升队列参数可观测性。

### 1.2 Wave 1-A 靶向手术 2：RIFE 引擎路径分桶解析与零静默回落（`param_schema.cpp` / `rife_engine.cpp` / `hw_transcode_stages.cpp`）

1. **默认 RIFE 引擎路径覆盖分桶产物**（`src/core/param_schema.cpp:L154-L182, L279-L282`）：
   * `resolveDefaultRifeEnginePath()` 扩展探测 `exe_dir/models/` 与 `models/` 下的 `rife_v4.25.engine` 及 4 个实际构建分桶文件（`rife_v4.25_wide.engine`、`rife_v4.25_ultra.engine`、`rife_v4.25_square.engine`、`rife_v4.25_tall.engine`）；无本地文件时回退确定性模板路径 `models/rife_v4.25.engine`，确保 `60-rife` / `120-rife` 下 `rife_engine_path` 永不为空。
   * `profileToHwOptions()`（`src/core/param_schema.cpp:L327-L334`）对 `"120-rife"` 强制绑定 `fruc_enabled = true`、`target_fps = {120, 1}` 与 `resolveDefaultRifeEnginePath(profile.rife_engine)`。
2. **任意默认 `rife_v4.25*.engine` 自动映射至目标画幅分桶**（`src/shim/ngx_bridge/rife_engine.cpp:L261-L270`）：
   * 将同目录分桶重定向条件由仅匹配 `rife_v4.25.engine` 扩展为匹配所有 `rife_v4.25*.engine`，确保即使默认探测命中 `rife_v4.25_wide.engine`，在转码 9:16 竖屏或 21:9 超宽源时也能精准切换至 `preferred_name`（`tall` / `ultra` / `square` / `wide`）。
3. **显式 `[RIFE Error]` 硬中断与 `[Pipeline] Active Route` 可观测性**（`src/core/hw_transcode_stages.cpp:L113-L133`）：
   * `initRifeEx()` 失败时输出 `[RIFE Error] Failed to initialize TensorRT engine (...) — aborting without NVOFA fallback` 并立即返回错误中断流水线，严禁静默降级至 NVOFA 60fps（`RED-LINE-03`）。
   * 初始化成功时输出 `[Pipeline] Active Route: <rife-trt|rife-trt-scale05> | Target: <N> fps | Engine: <path>`，便于宿主机日志与状态核验。

### 1.3 Wave 1-B 靶向手术 1 & 2：`IDC_CMB_FPS` 绑定闭环与防回归单元测试

1. **单一真相源双向映射 + 抗重置刷新**（`src/gui/workstation_canvas.h:L471-L486`、`src/gui/workstation_canvas.cpp:L703-L705, L745, L797-L801`）：
   * 新增零新类型纯函数 `fpsModeStringFromIndex(int)` 与 `fpsModeIndexFromString(std::string_view)`，统一驱动 `collectProfileFromUi()`、`applyProfileToUi()` 与 `populateComboBoxes()`。
   * `populateComboBoxes()` 刷新 `IDC_CMB_FPS` 时优先读取 `guiProfile(app).fps_mode` 映射索引作为默认值，杜绝语言切换或重绘时无条件重置回索引 `1`（`60-nvofa`）。
2. **`CBN_SELCHANGE` 状态机即时同步**（`src/gui/rtx_gui.cpp:L817-L843`）：
   * 下拉框切换立即刷新 `guiProfile(app) = collectProfileFromUi(hwnd)`；空闲状态下若队列中仅 1 个 `Pending` 任务，实时同步该任务快照并刷新队列列表，彻底消除 UI 与队列状态脱节。
3. **确定性防回归单元测试**（`tests/test_gui_workstation.cpp:L439-L484`）：
   * 新增 `RTX_TEST(gui_fps_combobox_rife120_persistence_and_pending_sync)`，断言索引 `0..4` 双向映射、`validateAndNormalizeProfile` + `profileToHwOptions` 下发 `target_fps == {120, 1}` 且 `!rife_engine_path.empty()`、单 `Pending` 任务 `updatePendingJob` 解除冻结更新为 `"120-rife"`，以及双 `Pending` 任务异构隔离不被互相覆盖。

---

## 二、 验收 CheckList 与门禁勾选（含精确坐标实证）

### 2.1 代码级与静态门禁（已完成并验证）

| 序号 | 验收项目 | 状态 | 实证坐标 (`文件:L行号`) |
| :--: | :--- | :--: | :--- |
| 1 | 单文件拖拽不提前冻结 `Pending` 快照，多文件仍批量入队 | `[x]` | `src/gui/rtx_gui.cpp:L698-L716` |
| 2 | `onStartTranscode()` 单 `Pending` 同步更新 + 多任务异构隔离 | `[x]` | `src/gui/rtx_gui.cpp:L360-L381` |
| 3 | `resolveDefaultRifeEnginePath()` 覆盖分桶引擎且永不返回空路径 | `[x]` | `src/core/param_schema.cpp:L154-L182, L279-L282` |
| 4 | `rife_engine.cpp` 任意默认 `rife_v4.25*.engine` 精准映射画幅分桶 | `[x]` | `src/shim/ngx_bridge/rife_engine.cpp:L261-L270` |
| 5 | RIFE 初始化失败显式 `[RIFE Error]` 中断，零静默回落 NVOFA | `[x]` | `src/core/hw_transcode_stages.cpp:L113-L133` |
| 6 | `IDC_CMB_FPS` 索引 `0..4` 单一真相源 + `populateComboBoxes()` 抗重置 | `[x]` | `src/gui/workstation_canvas.h:L471-L486`、`src/gui/workstation_canvas.cpp:L704, L745, L798` |
| 7 | `CBN_SELCHANGE` 实时同步 `guiProfile` 与单 `Pending` 快照 | `[x]` | `src/gui/rtx_gui.cpp:L817-L843` |
| 8 | 确定性防回归单测（`120-rife` 映射 + `target_fps={120,1}` + Pending 同步 + 异构隔离） | `[x]` | `tests/test_gui_workstation.cpp:L439-L484` |
| 9 | 宪法红线 1：`AuditTypes` 新增具名类型 `+0`（维持 `34/35`） | `[x]` | 7 份修改文件新增具名 `struct`/`class`/`enum` = `0` |
| 10 | 宪法红线 2：`AuditImports` 零新增静态 DLL（维持 GUI `<= 4` / HEADLESS `== 2`） | `[x]` | 零新增外部库或静态 DLL 导入 |
| 11 | 宪法红线 3 & 4：`AuditPrivacy = 0`、`AuditSpdx = 0` | `[x]` | 7 份修改文件全部含 SPDX 头、隐私路径命中 `0` |

### 2.2 宿主机全量构建、单测与双端实机验收项（遵循 `[DEC-004]` / `[DEC-008]` 如实留待）

> **诚实声明**：当前执行环境为离线 Linux 沙箱，无 Windows 11 / MSVC 19.51 / RTX 5060 宿主机。严格遵循不伪造任何构建/单测/实机截图日志铁律，以下宿主机实测项如实保持 `[ ]`，请于 Windows 11 宿主机拉取最新文件后依次执行封板：

- [ ] **宿主机全量构建**：执行 `.\build.bat`，确认退出码 `0`。
- [ ] **宿主机单元测试全量回归**：执行 `.\build\bin\Release\rtx_tests.exe`，确认 `154/154 PASS`（含新增 `gui_fps_combobox_rife120_persistence_and_pending_sync`）。
- [ ] **宿主机五重静态门禁实跑**：执行 `AuditTypes`（`34/35`）、`AuditImports`（GUI `<= 4` / HEADLESS `== 2`）、`AuditPrivacy`（`0`）、`AuditSloc`（`<= 23,000`）、`AuditSpdx`（`0`）。
- [ ] **RTX 5060 GUI 实机验证**：拖入或 `+` 添加测试视频，下拉框选择 `120fps (RIFE AI)` (`120-rife`) 并点击开始转码，确认日志输出 `[Pipeline] Active Route: rife-trt... | Target: 120 fps` 且输出产物帧率为 `120.00 fps`，无静默回落 `60-nvofa`。

---

## 三、 本轮修改与归档文件清单

* **修改替换（共 7 个文件，旧版均已归档至 `work/_CineRTX_bak_archive/*.p13r8bak`）**：
  1. `src/gui/workstation_canvas.h`
  2. `src/gui/workstation_canvas.cpp`
  3. `src/gui/rtx_gui.cpp`
  4. `src/core/param_schema.cpp`
  5. `src/shim/ngx_bridge/rife_engine.cpp`
  6. `src/core/hw_transcode_stages.cpp`
  7. `tests/test_gui_workstation.cpp`
* **汇报文档**：`docs/phase13/54_CineRTX Phase 13 RIFE 120fps 选项闭环与防静默回落返工汇报.md`（纯 `.md` 原生格式落盘，非 `.gdoc`）。
