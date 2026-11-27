# 55_CineRTX RIFE 链路报废与窗体拖放失效根因排查及新方案委托书

- **基线版本**：已完整回退至 Commit `a6451b7`（双端预览与播放器 2D 环本地基线；`54_` 问题改动已全部回滚清除）。
- **故障级别**：P0 阻塞级（RIFE 插帧核心能力完全不可用 + GUI 拖放交互失效）。
- **委托目标**：请领域专家彻底重新设计 **RIFE 管线几何/色彩空间全链路方案** 与 **GUI 拖放及参数同步方案**，禁止局部打补丁或旧快照整文件覆盖。

---

## 一、实测致命现象与已取证根因

### 问题 A：RIFE 链路彻底报废（无实际插帧、纯 dup、紫色闪烁）

- **实测现象**：在 RTX 5060 真机开启 RIFE 插帧后，输出视频无任何真实运动插帧（纯复制帧 dup），并伴随全屏/大面积紫色闪烁。
- **已定位的硬伤坐标**：
  1. **输入/初始化几何尺寸严重错配（越界读取 + UV 错位 + 误判场景切换）**：
     - `src/core/hw_transcode_stages.cpp:L183-L188` 当 `vsr_before_fg == true`（默认模式，例如 `1080p -> 4K`）时，`initRifeEx()` 按 `targetWidth() x targetHeight()`（`3840x2160`）初始化 `RifeEngine`；
     - 但 `synthesizeAndRemasterWindow()`（`src/core/hw_transcode_stages.cpp:L417-L436`）实际却将**解码原分辨率的 `prev_f` / `curr_f`（`1920x1080` NV12）**直接传入 `interpolateRife()`，且 UV 平面偏移按 `pitch * prev_f.height` 计算。
     - 结果：`RifeEngine` 按 `3840x2160` 越界读取 `1920x1080` 显存，产生脏数据并触发 `evalBlockSadSceneCut()` 误判硬切（退化为纯 dup 复制帧），未拦截时因越界/UV 错位（`U=0, V=0`）直接产生紫色闪烁。
  2. **RIFE NV12 输入/输出格式与 TensorRT 分桶引擎稳定性问题**：
     - `models/` 下仅构建了 `rife_v4.25_{wide,ultra,square,tall,landscape,portrait}.engine` 6 个分桶文件而无 `rife_v4.25.engine` 本体；
     - `test_rife.cpp` 中 `rife_four_phase_monotonic_progression` 亦在同进程前序 GPU 测试后偶发 `scene cut` 误报与像素偏差（`pack_stream()` 在无预览环时退化为默认流 `0` 与上下文切换竞态）。
  3. **既有单测盲区**：现有单测仅验证 `256x256` 合成交叉淡入或 `profileToHwOptions` 字段映射，从未覆盖真实 `rife_v4.25_*.engine` 端到端插帧（含 `1080p -> 4K`、`vsr_before_fg`、L1 运动像素差异及 UV 色度合法性校验）。

### 问题 B：窗体拖入 Drop 失效

- **实测现象**：`54_` 修改后，将媒体文件拖入主窗体无有效入队/列表反馈，表现为拖放失效。
- **已定位的根因**：
  - `54_` 在 `WM_DROPFILES` 中把单文件拖放改写为“不入队列、不刷新队列列表”，叠加 `IDC_EDT_INPUT` / `applyProbedMedia` 与 `Pending` 同步逻辑改动，破坏了用户拖入文件即在队列可见并可直接启动/调整参数的交互契约。

---

## 二、请专家出具的新方案要求

1. **RIFE 全链路重构方案（彻底根治纯 dup 与紫色闪烁）**：
   - 明确 **RIFE 永远在解码原生分辨率（`info.width x info.height`）插帧后再送 VSR**，还是 **先 VSR 放大后再送 RIFE**（若支持后者，必须给出中间显存缓冲几何契约，严禁 `initRifeEx` 与 `interpolateRife` 分辨率不一致）；
   - 核查 `rife_pack.ptx`（preprocess/postprocess NV12 Y/UV 偏移、`pitch`、Full/Limited range、FP16/FP32）与 `rife_v4.25_*.engine` 实际张量输入输出契约，根除紫色闪烁（UV 异常）与 `evalBlockSadSceneCut` 误杀（纯 dup）；
   - 修复 `rtx_ngx_bridge_interpolate_rife` CUDA 流与上下文隔离（禁止无预览环时退化为默认流 `0` 引发的异步竞态）。
2. **GUI 拖放（`WM_DROPFILES`）+ 参数同步（`IDC_CMB_FPS`）统一交互方案**：
   - 保持拖放文件（单文件/多文件）立即可见、可用且反馈明确；
   - 彻底解决“先选/拖入文件，再切换 `120-rife` 或其他参数，点击 Start 却按旧快照运行”的问题，同时不破坏多任务异构队列隔离。
3. **强制端到端真机可验证单测**：
   - 必须新增针对真实 `rife_v4.25_*.engine`（或真实 `RemasterPipeline`）的端到端测试：验证输出帧数翻倍/目标帧率达成、**帧间存在真实运动插值（非纯 dup）**、**NV12 UV 色度处于合法非紫屏范围**。
4. **工程红线**：
   - 严格保持 `AuditTypes == 34/35`、`AuditImports`（GUI `<= 4` / HEADLESS `== 2`）、`AuditPrivacy = 0`、`AuditSpdx = PASS`；
   - 禁止基于旧快照整文件覆写代码，所有施工单必须基于最新基线 `a6451b7` 增量定义。
