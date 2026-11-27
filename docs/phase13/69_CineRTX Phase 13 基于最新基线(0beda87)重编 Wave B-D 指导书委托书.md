# 69_CineRTX Phase 13 基于最新基线(0beda87)重编 Wave B-D 指导书委托书

- **唯一基线锚点**：Commit `0beda87`（`feat(phase13): RIFE FG-First geometry lock, stream isolation, PTX full-range fix, and GUI drop sync (157/157 PASS)`）
- **基线实测门禁**：
  - `.\build.bat` (MSVC 19.51 `/W4 /WX`)：`0 Error, 0 Warning`
  - `rtx_tests.exe`：**`157/157 PASS`**（无非预期跳过）
  - `AuditTypes`：**`34/35`**（仅余最后 `1` 个名额，须留给 `UiWidgetNode`）
  - `AuditImports`：**GUI `4 physical DLL(s)`** / **HEADLESS `2 physical DLL(s)`**
  - `AuditSloc`：**Core `12,496 / 23,000`**（全仓 `32,733`）
  - `AuditPrivacy`：**`0 violations`** / `AuditSpdx`：**`PASS (GPL=114 MIT=39)`**
- **委托原因**：原 `32_` / `33_` / `34_` 施工单起草于旧基线，自 `9355120` -> `d18b469` -> `a6451b7` -> `0beda87` 期间，仓库已完成双端预览 BT.2390 重构、DComp 矩形硬裁剪、播放器 2D 环 PTX 修复、RIFE FG-First 几何统一、`rife_stream_` 专属非阻塞流隔离、PTX 7.5 有限值与全范围修补，以及 GUI 即拖即入队 + `activeSyncIds` 靶向联动。**旧版 `32_`–`34_` 的部分行号、API 签名与假设已过期**，故特委托领域专家基于 `0beda87` 最新代码事实状态，重编全新的 Wave B / Wave C / Wave D 指导书与施工单。

---

## 一、`0beda87` 最新代码事实状态盘点（经宿主机只读核验）

### 1.1 Wave B 现状（对应原 `32_` 范围）
1. **`cmake/RtxFfmpegStatic.cmake`**：
   - MSVC 分支第 297–306 行已预留条件探测 `SvtAv1Enc.lib`（`--enable-libsvtav1`）与 `CINERTX_ENABLE_GPL_CODECS` 门控下的 `x265-static.lib`（`--enable-gpl --enable-libx265`），第 390–396 行已启用 `concat` 解复用器、`aac`、`mov_text` 编码器；
   - **缺口**：`RtxFfmpegStatic.cmake` 尚未自动源码构建或拉取 `SvtAv1Enc.lib` / `x265-static.lib`（仅 `if [[ -f ... ]]` 条件探测）；且第 643–648 行 Zig 交叉分支仍遗漏 `--enable-encoder=mov_text`（并需对齐 `libsvtav1` / `libx265`）。
2. **`src/io/cpu_video_encoder.cpp`**：
   - 已包含 `"libsvtav1"` 与 `"libx265"` 编码器名称映射及 `preset` 设置，但依赖底层 FFmpeg 静态库实际编入对应编码器后方可端到端生效。
3. **`src/io/ffmpeg_io.cpp`**：
   - `FfmpegDemuxer` 已将音频流及 `MOV_TEXT`/`SUBRIP`/`ASS`/`WEBVTT` 字幕流加入 `passthrough_streams`；
   - **缺口**：`FfmpegMuxer::open()`（第 834 行）在 MP4 模式下仍仅对 `AV_CODEC_ID_MOV_TEXT` 直通，尚未实现非兼容音轨（DTS/FLAC 等）经 `swresample` 转码 AAC-LC 及 SRT/ASS 转码 `mov_text` 闭环。
4. **`src/core/job_queue.cpp`**：
   - `EncoderJobQueue::workerLoop()`（第 242–251 行）在 `JobOutputMode::MergeConcat` 下仍仅生成 `.ffconcat` 列表（同构路径），**尚未实现**先探测多文件流参数是否同构、并在异构时自动路由 `runNvidiaRemasterRenderGraph()` 的双路智能合并。

### 1.2 Wave C 现状（对应原 `33_` 范围）
1. **`src/gui/preview_viewport.h/.cpp` & `preview_tone_map_cs.hlsl`（`0beda87` 已升级）**：
   - 视口已升级为 **12-DWORD 双侧独立色彩空间契约**（`kPreviewLeftPq/Hlg/Bt2020`、`kPreviewRightPq/Hlg/Bt2020` + `left_max_nits` / `right_max_nits` + BT.2390 Hermite EETF + DComp `IDCompositionRectangleClip` + `CreateTargetForHwnd(..., FALSE)`）；
   - 原 `33_` 若仍按旧版单比特 `input_is_pq` 传参将与现行渲染契约脱节，**必须基于新版 `presentShared(..., color_flags, left_max_nits, right_max_nits, ticket)` 设计**。
2. **`src/core/playback_session.cpp` & `rtx_ngx_bridge`**：
   - 无头播放环已修复 PTX 11 参数对齐（`out_pitch` 移至 `param_10`）与 `playback_pack` 动态扩容，`RifeEngine` 已具备 `rife_stream_` 非阻塞流隔离；
   - **缺口**：`createHeadlessPreviewSink()` 仍为原始解码帧直通 `playbackPack`，尚未接入统一 GPU AI 增强管线（切镜守卫 -> RIFE/NVOFA 60fps -> VSR -> TrueHDR -> 3D LUT -> A/B 分屏）及 WASAPI 时钟锁相 / 8.0 ms 推理超时熔断。

### 1.3 Wave D 现状（对应原 `34_` 范围）
1. **`src/gui/workstation_canvas.h/.cpp` & `src/gui/rtx_gui.cpp`（`0beda87` 已升级）**：
   - 已落地 `activeSyncIds` / `selectedJobId`、即拖即入队、`fpsModeStringFromIndex` 单一映射、`WS_CLIPCHILDREN`、`commitViewportVsrScale()`；
   - **缺口**：`chromeText()` 私藏词条表仍存在（`workstation_canvas.cpp:L207-L282`），`d2d1.dll` / `dwrite.dll` 动态组件库、`UiWidgetNode`（`35/35` 封顶唯一类型名额）、横向可拖拽 DAG 画布及零算力预览门控开关尚未实现。

---

## 二、对专家新指导书的硬性要求

1. **严格基于 `0beda87` 现状重编 Wave B -> Wave C -> Wave D 全套指导书与施工单**：
   - 每波次必须明确：前置依赖、修改文件清单（增量补丁，**严禁整文件覆写**）、精确接口/类型预算（Wave B/C 严格 `+0` 具名类型维持 `34/35`，仅 Wave D 使用第 `35` 个名额 `UiWidgetNode`）、以及零静态新 DLL 约束（`AuditImports` GUI=4 / HEADLESS=2）。
2. **禁止保留已过时前提**：
   - 对接 `0beda87` 的 `PreviewViewport` 双侧色彩掩码、`rife_stream_`、`FG-First` 原生分辨率锁定、`activeSyncIds` 队列联动与 `writeWebuiPreviewBmpIfDue()` 预览落盘。
3. **每波次必须给出可宿主机实测的确定性验收门禁与新增单元测试清单**，确保单波次收口全绿后再推进下一波次，直至 Phase 13（`v0.5.0`）正式封板。
