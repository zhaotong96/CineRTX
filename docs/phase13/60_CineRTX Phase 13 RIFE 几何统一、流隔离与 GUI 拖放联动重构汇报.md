# 60_CineRTX Phase 13 RIFE 几何统一、流隔离与 GUI 拖放联动重构汇报

> **汇报编号**：`CINERTX-P13-REWORK-REPORT-060`  
> **唯一锚定基线**：Commit `a6451b7`（已同步 Drive 最新回滚后基线，基于原子增量补丁实施，零旧快照整文件覆写）  
> **施工依据**：`55_CineRTX RIFE 链路报废与窗体拖放失效根因排查及新方案委托书.md`、`57_wave1-A_RIFE管线几何统一_PTX双平面钳位与流隔离施工单.md`、`58_wave1-B_GUI拖放即入队_Pending靶向联动与120-rife防回落施工单.md`、`59_wave2-A_RIFE真机端到端非dup与NV12色度单测及门禁封板施工单.md`

---

## 一、 Wave 1-A：RIFE 管线几何统一、色度钳位、防指针别名与流隔离（Doc 57）

1. **根除初始化与推理几何分裂（消除纯 dup 与紫绿闪烁根因）**：
   * `src/core/hw_transcode_stages.cpp:L183-L190`：由于 `synthesizeAndRemasterWindow()` 始终先将原生解码 NV12 帧（`session_->info().width x height`）送入 RIFE 插帧、再交由 VSR 超分至目标尺寸，现将 `initRemasterAndFgStage()` 中的 `rife_w` / `rife_h` 严格锁定为解码原生尺寸 `session_->info().width` x `session_->info().height`（默认 FG-First），彻底消除 `1080p -> 4K` 下按 `3840x2160` 越界读取 `1920x1080` 显存造成的 `U=0, V=0` 紫绿闪与 SAD 误判纯 dup。
   * `src/core/hw_transcode_stages.cpp:L513-L520`、`src/shim/ngx_bridge/bridge_exports.cpp:L824-L832`、`src/shim/ngx_bridge/rife_engine.cpp:L570-L575`：三层入口加装 Fail-Fast 几何与步长硬断言（校验 `pitch >= engine_w`、偶数对齐、非空指针与尺寸匹配），不匹配时立即返回 `RTX_ERR_INVALID_ARGUMENT` 并递增 `fruc_failures`，零显存越界、零静默 dup。
2. **专属非阻塞 CUDA 流与代次防指针别名缓存键**：
   * `src/shim/ngx_bridge/nv_func_tables.h:L32-L34,L54-L56` 与 `src/shim/ngx_bridge/nv_func_tables.cpp:L32-L34,L52-L54,L85-L102,L120-L157`：动态加载 `cuStreamCreate`、`cuStreamDestroy_v2`、`cuCtxPushCurrent_v2`、`cuCtxPopCurrent_v2`（零新增具名类型、零新增静态 DLL）。
   * `src/shim/ngx_bridge/rife_engine.h:L146-L151` 与 `src/shim/ngx_bridge/rife_engine.cpp:L77-L107,L554-L698`：在 `loadEngineEx()` 中创建专属 `CU_STREAM_NON_BLOCKING`（`flags=1`）流 `rife_stream_` 并在 `releaseBuffers()` 中安全销毁；当调用方传入 `stream == nullptr`（无预览环模式）时自动隔离至 `rife_stream_` 并同步，彻底消灭默认流 `0` 竞态；缓存键引入 `cache_generation_` + `full_range` + `alpha > last_alpha_`，`resetFramePairCache()` 递增代次并复位实例级 `ema_motion_`，杜绝显存指针复用导致的伪缓存命中。
3. **分桶引擎全家族探测、零静默回落与后处理钳位**：
   * `src/core/param_schema.cpp:L160-L176`：扩展 `resolveDefaultRifeEnginePath()` 探测 `models/` 下全部 7 种 `rife_v4.25*.engine` 分桶文件（`wide`/`ultra`/`square`/`tall`/`landscape`/`portrait`），配合 `src/shim/ngx_bridge/rife_engine.cpp:L233-L242` 自动重定向至目标画幅分桶。
   * `src/core/hw_transcode_stages.cpp:L202-L210,L224-L225`：`initRifeEx()` 失败时输出 `[RIFE Error] ... — aborting without NVOFA fallback` 硬报错中断，成功时输出 `[Pipeline] Active Route: <rife-trt|rife-trt-scale05> | Target: <N> fps`。
   * `src/shim/ngx_bridge/rife_pack.cu:L49-L52,L241-L255`：升级 `clamp01` 增加 `isnan`/`isinf` 硬拦截，并修复 `full_range != 0` 分支缺少 `* 255.0f` 缩放的缺陷（注：现有 `rife_pack.ptx` 保持 ABI 兼容，宿主机如需重编 PTX 可由 `nvcc` 从修复后的 `rife_pack.cu` 重新生成）。

---

## 二、 Wave 1-B：GUI 拖放即入队、`activeSyncIds` 靶向联动与 `120-rife` 防回落（Doc 58）

1. **即拖即入队 + `activeSyncIds` 靶向联动状态机**：
   * `src/gui/workstation_canvas.h:L372-L374`：在既有 `WorkstationApp` 上追加 `uint64_t selectedJobId` 与 `std::unordered_set<uint64_t> activeSyncIds`（**+0 具名类型**）。
   * `src/gui/rtx_gui.cpp:L753-L779`（`WM_DROPFILES`）：
     * Encoder 工作区：无论单文件还是多文件拖入，**立即**按当前 UI 配置创建 `Pending` 任务入队，将本次拖入任务 ID 写入 `activeSyncIds`，`selectedJobId` 锁定首项并高亮刷新队列列表（彻底修复 55 号问题 B 拖放无队列反馈问题）；全路径通过 `rtx::sys::dragQueryFileW` / `dragFinish` 动态调用，保持 `shell32.dll` 零静态导入。
     * 非 Encoder 工作区（Player / NLE）：直接路由至播放/素材箱，绝不污染 Encoder 队列。
   * `src/gui/rtx_gui.cpp:L346-L368`（`syncActivePendingJobs`）与 `L876-L901`（`CBN_SELCHANGE` & `IDC_LST_QUEUE` `LBN_SELCHANGE`）：
     * 修改下拉框（含 `IDC_CMB_FPS` 切换 `120-rife`）时仅更新 `activeSyncIds` 中状态为 `Pending` 的任务快照并刷新队列列表摘要（展示 `fps_mode`），历史任务与运行中/已完成任务严格隔离不被改写。
     * 点击队列列表项 `J_i` 时自动切换 `selectedJobId`、`activeSyncIds`（`Pending` 为 `{J_i}`，否则清空）并回填面板控件。
   * `src/gui/rtx_gui.cpp:L381-L454`（`onStartTranscode`）与 `L580-L586`（`onQueueAdd`）：启动前先执行 `syncActivePendingJobs`，对未入队的 `IDC_EDT_INPUT` 自动补入队，并对 `60-rife`/`120-rife` 任务执行启动前契约校验（确认 `target_fps == {120, 1}`/`{60, 1}` 且 `!rife_engine_path.empty()`），状态栏输出 `Engine: RIFE | Target FPS: 120 | Status: Ready`，校验失败硬弹窗拦截绝不静默回落 `60-nvofa`。
2. **规避 Win32 `CB_SETCURSEL` 不触发 `CBN_SELCHANGE` 隐性不同步**：
   * `src/gui/workstation_canvas.h:L474-L489` 与 `src/gui/workstation_canvas.cpp:L704,L744-L749,L801-L809`：统一 `0..4` 索引双向映射函数 `fpsModeStringFromIndex` / `fpsModeIndexFromString`，并在 `applyProfileToUi()` 与 `populateComboBoxes()` 发送 `CB_SETCURSEL` 后立即调用 `ComboBox_GetCurSel()` 反向校验并同步 `guiProfile(app).fps_mode`。

---

## 三、 Wave 2-A：4 项强制端到端与回归单元测试（Doc 59）

| 用例编号 | 测试名称 | 实证坐标 (`文件:L行号`) | 验证要点 |
| :---: | :--- | :--- | :--- |
| Case 1 | `rife_real_engine_1080p_fg_first_to_4k_120fps` | `tests/test_rife.cpp:L747-L820` | 真实 `1920x1080` NV12 + `models/rife_v4.25_wide.engine` 4 相位插帧：断言 `scene_cut == 0`、相邻帧 `MAE > 0.5`、平移块质心严格单调递增、中性灰背景 `|U-128| <= 2` 且 `|V-128| <= 2` |
| Case 2 | `rife_no_preview_ring_stream_isolation_soak` | `tests/test_rife.cpp:L822-L860` | 20 轮 `stream == nullptr` 无预览环专属非阻塞流压测 + 同设备指针内容复用防别名验证 |
| Case 3 | `rife_geometry_mismatch_fail_fast_guard` | `tests/test_rife.cpp:L862-L877` | `3840x2160` 引擎注入 `1920` 非法步长，立即返回 `RTX_ERR_INVALID_ARGUMENT`（零越界、零崩溃、零静默 dup） |
| Case 4 | `gui_drop_then_switch_120rife_queue_isolation` | `tests/test_gui_workstation.cpp:L439-L475` | 模拟拖放 `clipA` 即入队 -> 切换索引 4 (`120-rife`) 靶向同步 `target_fps={120,1}` + `!rife_engine_path.empty()` -> 追加 `clipB` (`60-nvofa`) 验证多任务隔离 |

---

## 四、 门禁 CheckList 勾选与宿主机封板项

### 4.1 代码级与离线静态审计（已实证勾选）

- [x] **Gate 1（几何统一 & Fail-Fast）**：`src/core/hw_transcode_stages.cpp:L183-L190,L513-L520`、`src/shim/ngx_bridge/bridge_exports.cpp:L824-L832`、`src/shim/ngx_bridge/rife_engine.cpp:L570-L575`
- [x] **Gate 2（色度钳位 & 全范围缩放）**：`src/shim/ngx_bridge/rife_pack.cu:L49-L52,L241-L255`
- [x] **Gate 3（分桶全族校验 & 零静默回落）**：`src/core/param_schema.cpp:L160-L176`、`src/shim/ngx_bridge/rife_engine.cpp:L233-L242`、`src/core/hw_transcode_stages.cpp:L202-L210`、`src/gui/rtx_gui.cpp:L434-L453`
- [x] **Gate 4（代次防指针别名 & 专属非阻塞流隔离）**：`src/shim/ngx_bridge/nv_func_tables.h:L32-L34,L54-L56`、`src/shim/ngx_bridge/rife_engine.h:L146-L151`、`src/shim/ngx_bridge/rife_engine.cpp:L77-L107,L554-L698`
- [x] **Gate 5（GUI 即拖即入队 + `activeSyncIds` 靶向联动 + `CB_SETCURSEL` 校验）**：`src/gui/workstation_canvas.h:L372-L374,L474-L489`、`src/gui/workstation_canvas.cpp:L704,L744-L749,L801-L809`、`src/gui/rtx_gui.cpp:L346-L368,L753-L779,L876-L901`
- [x] **Gate 6（4 项强制单测代码落盘）**：`tests/test_rife.cpp:L747-L877`、`tests/test_gui_workstation.cpp:L439-L475`
- [x] **Gate 7（离线静态红线自检）**：13 份修改文件新增具名类型 `+0`（维持 `AuditTypes == 34/35`）、新增静态 DLL `+0`（零 `shell32.dll`）、`AuditPrivacy == 0`、`AuditSpdx == PASS`

### 4.2 Windows 11 + RTX 5060 宿主机实跑封板项（严格遵循 `[DEC-004]` / `[DEC-008]` 如实留待）

> **诚实声明**：当前执行环境为离线 Linux 沙箱，无 Windows 11 / MSVC 19.51 / CUDA `nvcc` / RTX 5060 宿主机。严禁伪造编译、单测、导入表或实机运行日志，以下项如实保持 `[ ]`，请于宿主机同步后执行验证：

- [ ] **宿主机全量编译**：执行 `.\build.bat`（确认退出码 `0`）。
- [ ] **宿主机单元测试全量回归**：执行 `.\build\bin\Release\rtx_tests.exe`，确认原有 `153` 项 + 本轮新增 `4` 项（总计 `157/157`）全部 `PASS`。
- [ ] **宿主机五重静态门禁实跑**：执行 `AuditTypes`（`34/35`）、`AuditImports`（GUI `<= 4` 且无 `shell32.dll` / HEADLESS `== 2`）、`AuditPrivacy`（`0`）、`AuditSpdx`（`0`）、`AuditSloc`（预算内）。
- [ ] **RTX 5060 真机实测验收**：验证 Encoder 拖入视频立即显示于队列 -> 下拉切换 `120-rife` 队列摘要同步刷新 -> 开始转码输出 `120.00 fps` 真实运动插帧且全程无紫绿闪烁。

---

## 五、 本轮修改与归档文件清单（共 13 份源码/单测，旧版归档于 `work/_CineRTX_bak_archive/*.p13r9bak`）

1. `src/shim/ngx_bridge/nv_func_tables.h`
2. `src/shim/ngx_bridge/nv_func_tables.cpp`
3. `src/shim/ngx_bridge/rife_engine.h`
4. `src/shim/ngx_bridge/rife_engine.cpp`
5. `src/shim/ngx_bridge/bridge_exports.cpp`
6. `src/shim/ngx_bridge/rife_pack.cu`
7. `src/core/hw_transcode_stages.cpp`
8. `src/core/param_schema.cpp`
9. `src/gui/workstation_canvas.h`
10. `src/gui/workstation_canvas.cpp`
11. `src/gui/rtx_gui.cpp`
12. `tests/test_rife.cpp`
13. `tests/test_gui_workstation.cpp`
14. `docs/phase13/60_CineRTX Phase 13 RIFE 几何统一、流隔离与 GUI 拖放联动重构汇报.md`（原生 `.md`，非 `.gdoc`）
