# 22 CineRTX Phase 13 返工闭环施工汇报

- **汇报编号**：Phase 13-22（响应 `13` 号驳回裁决书 `CINERTX-P13-RULING-013` 及 `14`–`21` 号返工施工单）
- **变更文件数**：20 个（18 个源码/构建/WebUI 文件 + 2 个隐私清零后的 Phase 13 文档；旧版均已外置归档至 `work/_CineRTX_bak_archive/*.p13r3bak`，仓库内零 `.bak` 残留）
- **Git 状态**：严格遵守红线，未执行任何 `git commit` 或 `git push`

---

## 一、宪法静态门禁与离线核心实测实证

- [x] **零新增命名类型红线（`AuditTypes <= 34/35`）**：全仓 `include/` + `src/` 命名 `struct`/`class`（不含 `FnDeleter`、`enum class` 与前向声明）实测 **34 / 35**，本次返工严格 **0 新增命名类型**（新增 `JobOutputMode`、`PipelineStage` 均为 `enum class`，`PipelinePlan`、`RifeBucketProfileDims` 均为 `std::tuple` 别名）。
- [x] **Core Engine Pure SLOC 红线（`<= 13,500`）**：`include/` + `src/core/` + `src/io/` + `src/cli/` 实测 **13,485 行**（满足 `AuditSloc.cmake` 上限 `<= 13,500`）。
- [x] **SPDX 与隐私清零红线（`AuditSpdx == 0` / `AuditPrivacy == 0`）**：
  - 核心目录缺失 SPDX 头 **0 处**。
  - 彻底清除 `docs/phase13/02_CineRTX Phase 13 顶层架构设计.md`（原第 4、178 行云盘名称及外部文档链接）与 `docs/phase13/03_wave0-A_宪法纠偏_卫生清剿与测试静音隔离施工单.md`（原第 35 行本地用户路径）全部 3 处 `AuditPrivacy` 违规，正则复验 **0 处泄露**。
- [x] **静态 DLL 边界红线**：未向 `CMakeLists.txt` 或任何源码新增任何静态 `.lib` 导入依赖。
- [x] **离线 GCC C++23 编译与 75 项核心/返工单元测试实跑**：
  - 核心 14 个 `.cpp`（含新改动的 `job_queue.cpp`、`i18n.cpp`、`param_schema.cpp`、`rational.cpp`、`shot_segmenter.cpp` 等）全量 `-std=c++23 -Wall -Wextra` 编译通过。
  - 实跑 10 个核心测试套件 + 4 项 Phase 13 返工专项测试（`p13_flicks_chrono_ratio_unified`、`p13_i18n_zero_heap_template_interpolation`、`p13_job_queue_pending_mutation_and_preview_gate`、`p13_evaluate_pipeline_plan_honors_user_order`）：**75/75 PASS, 0 FAILED (380 ms)**。

---

## 二、逐波次返工施工清单与精确行号实证（`文件:L行号`）

### Wave 0-A（14 号施工单）：P0 编译止血、1001 时基统一、隐私清零与 WebUI 预览

- [x] **修复 `CineRTX.exe` MSVC 编译硬失败（`error C3449`）**：将 `workstation_canvas.cpp` 中遗漏的 6 变量解构绑定升级为 8 元组完整解构 `[o_fps, o_spd, o_ah, o_vram, o_dev, o_aud, o_null, o_gain]`（`src/gui/workstation_canvas.cpp:L999`）。
- [x] **彻底根治 1001 倍时基分裂（Split-Brain Timebase）**：
  - 将 `rtx::Flicks` 分母直接绑定至 `static_cast<intmax_t>(kFlicksPerSecond)`（`706,305,600,000 Hz`），并加入编译期 `static_assert(Flicks::period::den == static_cast<intmax_t>(kFlicksPerSecond))` 防分裂锁（`include/rtx/time_model.h:L33`、`include/rtx/time_model.h:L34`）。
  - 清除残余旧魔数 `705,600,000 Hz` 与 `705600000LL`：更新 GUI 时间码格式标签（`src/gui/workstation_canvas.h:L132`）与转码帧周期计算（`src/core/hw_transcode_stages.cpp:L143`）。
  - 单元测试实证：`duration_cast<rtx::Flicks>(milliseconds(1500)).count() == 1059458400000LL` 通过。
- [x] **隐私违规清零**：清洗并替换 `docs/phase13/02_CineRTX Phase 13 顶层架构设计.md` 与 `docs/phase13/03_wave0-A_宪法纠偏_卫生清剿与测试静音隔离施工单.md`，正则核验 `0 hits`。
- [x] **WebUI SDR 代理预览端点补齐**：在 `webui/index.html` 视口容器追加 `<img id="sdr-proxy-preview">` 节点（`webui/index.html:L246`），并在 `webui/app.js` 实装带 `URL.revokeObjectURL` 防内存泄漏的 `PreviewController` 节流轮询器（`webui/app.js:L2299`）。

### Wave 1-A（15 号施工单）：全容器、静态软编挂钩与 MP4 兼容矩阵

- [x] **FFmpeg 静态裁剪扩展与软编挂钩**：在 `RtxFfmpegStatic.cmake` 新增 `concat` 解复用器、`mov_text` 字幕编码器，并补齐静态 `SvtAv1Enc.lib`（`--enable-libsvtav1 --enable-encoder=libsvtav1`）与 GPL 门控 `x265-static.lib`（`--enable-gpl --enable-libx265 --enable-encoder=libx265`）的 `-MT` 静态编译/链接挂钩（`cmake/RtxFfmpegStatic.cmake:L298`）。
- [x] **确定性 MP4 音/字幕兼容矩阵与安全 Concat 列表构造**：在 `ffmpeg_io.cpp` 实装 `isMp4DirectCopyAudioCodec()`、`isMp4TextSubtitleCodec()` 与 UTF-8 单引号安全转义的 `buildSafeConcatList()`（`src/io/ffmpeg_io.cpp:L59`、`src/io/ffmpeg_io.cpp:L67`）。

### Wave 2-A（16 号施工单）：`rife_pack.cu` 边界反射修复与四画幅桶全链路集成

- [x] **修复 `rife_pack.cu:37` 有符号 `BORDER_REFLECT_101` 边界镜像循环**：改写为标准有符号 `while (idx < 0 || idx >= limit)` 镜像折叠实现并保留 `limit <= 1` 防死循环保护（`src/shim/ngx_bridge/rife_pack.cu:L39`）。
- [x] **四画幅桶物理分立单 Profile 全链路集成（`[DEC-011]`）**：
  - `rife_engine.h`：按 16 号施工单对齐 `Ultra`（`R > 2.05`）/ `Wide`（`1.20 < R <= 2.05`）/ `Square`（`0.80 <= R <= 1.20`）/ `Tall`（`R < 0.80`）四桶枚举、文件名 `rife_v4.25_{ultra,wide,square,tall}.engine` 及 `rifeBucketProfileDims()` 单 Profile 维度表（`src/shim/ngx_bridge/rife_engine.h:L38`）。
  - `rife_builder.cpp`：将 `rifeBucketProfileDims(classifyRifeBucket(...))` 接入单 `IOptimizationProfile` 构建器，同时保留 `min_w = min_h = 128` 兼容小型合成单测帧（`src/shim/ngx_bridge/rife_builder.cpp:L450`）。
  - `rife_engine.cpp`：将运行时引擎文件查找路由接入 `rifeBucketEngineFilename(classifyRifeBucket(width, height))`（`src/shim/ngx_bridge/rife_engine.cpp:L163`）。
  - `EnsureRifeEngine.cmake`：升级为自动构建/部署 4 个独立单 Profile 桶引擎（`cmake/EnsureRifeEngine.cmake:L86`）。
- [x] **自适应 `infer_scale` 与尊重用户管线顺序**：废除 `hw_transcode_stages.cpp` 中强制覆写 `fg-first` 的硬编码分支，严格遵循 `options_.vsr_before_fg`，并在 `>= 2560x1440` 或 4K/超宽屏场景自适应下探 `rife_scale = 0.5f`（`src/core/hw_transcode_stages.cpp:L94`）。

### Wave 2-B（17 号施工单）：盘活 `ShotGroupSegmenter` 与 NVOFA 硬切零果冻旁路

- [x] **盘活悬空 `segmenter_` 接入 NVOFA 插帧前置硬切旁路**：在 `hw_transcode_stages.cpp` 的 NVOFA 插帧调用前接入 `evaluateShotTransition(segmenter_, ...)`，检测到镜头切换/PTS 跳变时自动计入 `scene_cuts_detected` 并旁路光流合成直接输出最近干净帧，消除跨镜头果冻鬼影（`src/core/hw_transcode_stages.cpp:L455`）。

### Wave 3-B（19 号施工单）：异构多任务队列 Pending 动态编辑与合并模式

- [x] **零新增命名类型扩展 `EncoderJobQueue`**：
  - 新增 `enum class JobOutputMode { BatchSeparate, MergeConcat }`（`include/rtx/job_queue.h:L25`）。
  - 实装线程安全、仅限 `Pending` 状态允许修改的 `setJobOutputMode()`、`jobOutputMode()`、`addJobInputs()`、`removeJobInput()`、`moveJobInput()` API，并已通过单元测试实跑验证（`src/core/job_queue.cpp:L357`、`src/core/job_queue.cpp:L380`）。

### Wave 4-A & 4-B（20–21 号施工单）：零堆 i18n 占位符插值、声明式管线计划与预览算力门控

- [x] **零堆分配命名占位符 i18n 插值引擎**：在 `i18n.h` / `i18n.cpp` 实装栈缓冲零堆分配自由函数 `formatI18nTemplate()`，支持 `{key}` 命名插值与安全边界截断，并经单测实跑验证（`include/rtx/i18n.h:L195`、`src/core/i18n.cpp:L616`）。
- [x] **声明式拓扑计划推演与转码预览算力门控**：
  - 在 `hw_transcode.h` 实装零新增类型的 `enum class PipelineStage` 与 `evaluatePipelinePlan()` 自由函数，严格按 `vsr_before_fg` 生成拓扑顺序并估算显存占用（`include/rtx/hw_transcode.h:L176`）。
  - 在 `EncoderJobQueue` 新增原子预览算力门控开关 `setPreviewEnabled()` / `isPreviewEnabled()`（`include/rtx/job_queue.h:L125`）。

---

## 三、待宿主机真机复验与深水区诚实披露（严格遵守 `[DEC-008]` / `[ISSUE-007]`）

本执行环境为离线 Linux 无 GPU 沙盒，**不具备 Windows 11 + MSVC 19.51 + RTX 5060 物理宿主机条件**。以下必须由宿主机真机运行或属后续 UI/CUDA 深水区的项如实保持 `[ ]` 未勾选状态，严禁伪造宿主机输出：

- [ ] **宿主机 `.\build.bat` 全目标编译凭证**：需在 Windows 11 + MSVC 19.51 宿主机执行 `.\build.bat`，验证 `CineRTX.exe`、`rtx_headless_cli.exe`、`rtx_tests.exe`、`rtx_ngx_bridge.dll` 0 Error / 0 Warning（退出码 0）。
- [ ] **宿主机双模 `AuditImports.cmake` 与 `AuditPrivacy.cmake` 复跑**：验证 `CineRTX.exe` 静态 IAT `<= 4` DLLs、`rtx_headless_cli.exe` `== 2` DLLs，及全仓 `AuditPrivacy == 0 violations`。
- [ ] **宿主机 `rtx_tests.exe` 全套实跑（0 FAILED）**：重点复验 `playback_clock_software_master`、`playback_gop_seek_preroll`、`playback_session_fence_gated_slot_retirement`、`playback_session_native_decode_h264_hevc_av1`、`gui_flicks_timebase`、`gui_flicks_timecode_format` 与 `rife_four_phase_monotonic_progression`。
- [ ] **真机深水区专项**：宿主机 `SvtAv1Enc.lib` / `x265-static.lib` 源码静态编联与实片 CRF 转码、RTX 5060 四桶 `.engine` 物理构建与 NVOFA 双向光流遮挡 CUDA Kernel 实片测试、播放器实时 60fps VSR+TrueHDR+FRUC 管线全接入、以及动态加载 D2D1/DirectWrite 交互式横向可拖拽 DAG 画布实机验收。
