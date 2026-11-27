# 51_CineRTX RIFE 120fps 选项失效与回落 NVOFA 60fps 专家排障委托书

- **基线版本**：Commit `a6451b7`（双端预览与播放器 2D 环本地基线：`153/153 PASS`、`AuditTypes = 34/35`、`AuditImports` GUI=4 / HEADLESS=2）
- **故障级别**：致命 BUG（用户操作与实际转码链路严重背离）
- **委托目标**：请领域专家对“GUI 下拉框明确选择 `rife120fps`，实际转码却仍执行 NVOFA 60fps”问题进行全链路定界与裁决，给出最小充分修复方案与防回归单测要求。

---

## 一、实测现象与已定位根因坐标

- **实测现象**：在 Encoder 工作区将 `Target FPS` 下拉列表（`IDC_CMB_FPS`）明确选为 `120fps (RIFE AI)`（`120-rife`）并执行转码后，实际输出产物与执行流水线仍为 **NVOFA 60fps**（`60-nvofa`）。
- **已排查代码路径与关键发现**：
  1. **队列 `Pending` 任务参数冻结陷阱（高置信主要根因）**：
     - 拖拽文件入窗（`WM_DROPFILES`，`src/gui/rtx_gui.cpp:L676-L695`）或点击 `+` 入队（`onQueueAdd()`，`src/gui/rtx_gui.cpp:L530-L546`）时，程序会以**当时界面默认参数（`fps_mode = "60-nvofa"`）**立即调用 `app.jobQueue.enqueueJob(...)` 生成 `JobStatus::Pending` 任务。
     - 当用户随后在下拉框中把 `IDC_CMB_FPS` 改为 `120-rife` 并点击 `Start Transcode` 时，`onStartTranscode()`（`src/gui/rtx_gui.cpp:L358-L398`）虽然调用 `collectProfileFromUi(hwnd)` 更新了 `guiProfile(app)`，但紧接着检测到 `hasPending == true`，于是**直接跳过新参数入队，且未对队列中已有 `Pending` 任务调用 `updatePendingJob()`**，最终 `app.jobQueue.startWorker()` 仍按入队时刻冻结的 `"60-nvofa"` 快照执行！
  2. **RIFE 默认引擎路径与分桶解析链路（需同步核查防二次踩坑）**：
     - `src/core/param_schema.cpp:L154-L174` `resolveDefaultRifeEnginePath()` 在 `IDC_EDT_RIFE_ENGINE` 为空时返回 `models/rife_v4.25.engine`（但构建输出目录 `models/` 下实际仅存在 `rife_v4.25_wide.engine` 等 6 个分桶引擎文件，并无 `rife_v4.25.engine` 本体）。
     - 虽然 `src/shim/ngx_bridge/rife_engine.cpp:L233` 会把 `rife_v4.25*` 自动切换至同目录 `preferred_name`，但仍需确认 `initRifeEx()`、`validateAndNormalizeProfile()`（`src/core/param_schema.cpp:L271-L274`）及 `RemasterPipeline::initRemasterAndFgStage()`（`src/core/hw_transcode_stages.cpp:L172-L227`）在未显式填写 RIFE 路径时不会出现静默回落或路径校验失败。
  3. **UI 下拉框与语言刷新/控件同步链路**：
     - 核查 `workstation_canvas.cpp:L703-L707, L748-L758, L810-L812` 中 `IDC_CMB_FPS` 索引 `0..4` 与 `kFpsModes`（`original`, `60-nvofa`, `120-nvofa`, `60-rife`, `120-rife`）映射、以及 `CBN_SELCHANGE`（`rtx_gui.cpp:L799`）与 `populateComboBoxes()` 在各交互路径下是否存在状态不同步。

---

## 二、需专家裁决与输出内容

1. **多任务队列 vs 单文件即时转码的参数覆盖契约**：
   - 明确 `WM_DROPFILES` / `onQueueAdd` / `onStartTranscode` / `CBN_SELCHANGE` 的参数绑定语义：
     - 方案 A：仅 1 个 `Pending` 任务（或当前在面板修改参数）且尚未启动时，点击 `Start` 或修改控件是否同步更新该 `Pending` 任务快照（`updatePendingJob`）；抑或 `WM_DROPFILES` 单文件仅填入 `IDC_EDT_INPUT` 而不提前冻结 `Pending` 队列任务（多文件才批量入队），从而兼顾 Wave 3-B“多任务异构参数不互相覆盖”与单文件“先选文件后改参数再点开始”的直觉？
2. **RIFE 引擎解析与失败语义契约**：
   - 确认 `rife_v4.25.engine` 缺省路径与 `rife-trt` / `rife-trt-scale05` 路由日志/状态栏可观测性（当用户选择 `60-rife` / `120-rife` 时，禁止在无明确报错的情况下静默退化为 `nvof 60fps`）。
3. **施工约束与验收标准**：
   - 保持 `AuditTypes == 34/35`（零新增具名类型）、`AuditImports`（GUI `<= 4` / HEADLESS `== 2`）；
   - 补充确定性单元测试，验证 GUI 下拉框选择 `120-rife` -> 入队/启动全链路后实际下发 `target_fps == {120, 1}` 且 `!rife_engine_path.empty()`，绝不回落为 `nvof 60fps`。
