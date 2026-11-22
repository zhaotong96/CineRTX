# CineRTX Phase 11.3 SuperPower 施工单：统一 NLE 引擎与零分配 AVX2 音频总线合龙（终审修订版）

- **施工单号**：`CINERTX-P11-WORKORDER-019-REV2`
- **归档路径**：`docs/phase11/19_CineRTX Phase 11.3 SuperPower 施工单_统一NLE引擎与零分配AVX2音频总线.md`
- **承接角色**：NLE 剪辑架构与高性能 AVX2 音频执行工程师（外部执行工程师）
- **前置基线**：Phase 11.2 视口与实时内核合龙基线（CineRTX.exe 具备实时视听与 DComp 视口呈现能力）
- **依据母本**：[`16_CineRTX Phase 11 演进计划.md`](./16_CineRTX%20Phase%2011%20演进计划.md)、[`21_CineRTX Phase11 施工方案深度审查.md`](./21_CineRTX%20Phase11%20施工方案深度审查.md)
- **建议工期**：3–4 个有效工作日
- **核心目标**：统一 705.6 MHz Flicks 时间基、落地 `NleProject` 与 7 类可逆剪辑命令（含 Relink）、修复 NVENC 硬切 IDR 漏传断链（C-05）、实现 `mixAvx2()` 零堆分配与流式 WSOLA 接入。

---

## 一、 施工背景与输入资产

在 Phase 11.2 完成实时视听播放内核与 DirectComposition 零回读视口接线后，系统的底层多媒体运行时已建立。21 号穿透审查指出，原计划在 NVENC 硬切关键帧传递上存在漏传断点（C-05），且缺少 Gate 11D-2 要求的资产重链接命令。本施工单已全量收敛上述缺陷。

### 核心参考与输入文件
- 历史提纯参考源：`archive/phase11-attempt-1` 中的 `src/core/nle_project.cpp`、`src/core/render_graph.cpp`、`src/core/audio_mixer_avx2.cpp`、`src/core/wsola_stretch.cpp`
- 架构约束与技术裁决：[`21_CineRTX Phase11 施工方案深度审查.md`](./21_CineRTX%20Phase11%20施工方案深度审查.md) 第四节

---

## 二、 核心开发任务与实施规范

### 任务 1：基于 `json_scanner.h` 落地 `NleProject` 与 7 类可逆命令
修改/新建文件：`include/rtx/nle_project.h`、`src/core/nle_project.cpp`、`tests/test_nle_vertical_slice.cpp`

1. **复用公共 JSON 解析基础设施**：
   严禁引入外部第三方库，全量基于 `include/rtx/json_scanner.h` 实现工程 JSON 序列化与反序列化（`toJson()` / `fromJson()`）。
2. **原生 705,600,000 Hz Flicks 单一时间基**：
   彻底废除任何 `*_ms` 浮点或整型时间单位，所有片段（`Clip`）、过渡、播放头与工程总长严格以 64 位无符号整数 Flicks 计量。
3. **7 类可逆编辑命令栈（`EditHistory`）**：
   实现 `AddClipCommand`、`TrimClipCommand`、`SplitClipCommand`、`MoveClipCommand`、`DeleteClipCommand`、`ReorderTracksCommand` 与 **`RelinkAssetCommand`**。
   - **绝对红线**：命令对象内严禁持有任何 GPU / COM / CUDA 句柄或大块内存，纯净记录工程元数据 Delta，具备确定性 `undo()` / `redo()` 可逆性。

---

### 任务 2：`src/gui/workstation_canvas.cpp` 剪辑主干拔旧接新
修改文件：`src/gui/workstation_canvas.h/.cpp`

1. **物理剔除全局粗粒度时间线**：
   彻底删除全局静态 `sharedNleTimeline()`、`NleTimeline`、`NleClip`、`timelineXToMs`、`timelineMsToX` 以及 `onNleRazor` 中残留的 `msToFlicks(100)` 假定与 `*_ms` 残留变量。
2. **安全 RAII 化管理**：
   将原 `nleTimelineProc` 通过 `GWLP_USERDATA` 手工管理裸指针的 `NleWindowState` 收归安全 RAII 持有。
3. **全面接入 `NleProject` + `EditHistory`**：
   画布时间轴的缩放、标尺刻度、吸附定位、入出点标记与 Ctrl+Z / Ctrl+Y 撤销重做全面切至 `NleProject` 的 64 位整数 Flicks 计量与 `formatSmpteTimecode()`。

---

### 任务 3：`CompiledRenderGraph` 驱动与 NVENC 硬切 IDR 修复（C-05）
修改/新建文件：`include/rtx/render_graph.h`、`src/core/render_graph.cpp`、`src/core/hw_transcode_stages.cpp`

1. **修复 NVENC 关键帧漏传断点（C-05）**：
   在 `src/core/hw_transcode_stages.cpp` 的 `encodeWorkerBody()` 中，GPU NVENC 分支显式补入关键帧赋值：
   ```cpp
   encode_frame.keyframe = item.force_keyframe ? 1 : 0;
   ```
   使 `nv_encoder.cpp` 在硬切点精准打入 `NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS`。
2. **多段硬切容器导出合龙**：
   保持单一 `encoder_` 与 `muxer_` 实例在整个导出生命周期内不断流（维持以时间线 Flicks 换算 90 kHz 的单调连续 PTS）。按图切换或 Seek 源素材，在跨素材硬切边界自动下发 `force_reset_temporal = true`（驱动 `NVSDK_NGX_Parameter_Reset` 清除 VSR 时域历史）与 `force_key = true`。
3. **输出校验唯一铁证**：
   导出的 MP4/MKV 文件经解复用验证其实际帧数、切点 IDR 关键帧位置与时间线理论值逐帧一致。

---

### 任务 4：`mixAvx2()` 零堆分配重构与流式 `WsolaStreamState`
修改文件：`src/core/audio_mixer_avx2.h/.cpp`、`src/core/wsola_stretch.cpp`、`tests/test_audio_mixer_avx2.cpp`

1. **消除音频热循环堆分配**：
   彻底消除每次调用构造 `std::vector<VoicePrep>` 的行为，改用由调用方长期持有的固定容量 `AudioMixerScratchpad`（或 `std::array<VoicePrep, 32>` 栈上分块处理超多轨）。
   - 严守实时音频回调 4 条红线：零锁（Zero Locks）、零分配（Zero Allocations）、零 I/O（Zero I/O）、零无界循环（Zero Unbounded Loops）。
   - 在 `tests/test_audio_mixer_avx2.cpp` 中通过 Debug 构建重载 `operator new` 断言分配次数严格为 0。
2. **流式 WSOLA 状态机接入**：
   围绕 `wsolaStretch` 封装带预设环形分析窗与汉宁重叠相加尾迹区的 `WsolaStreamState`（所有缓冲在会话创建期一次性预分配），全面替换变调失真的 `VarispeedCursor`。在 0.25x~2.0x 调速播放下确保基频漂移 $< 0.05\%$。

---

## 三、 外部工程师交付验收检查清单（Gates）

- [ ] **Check 1：单测全绿无回归**：`rtx_tests.exe` 中 `test_nle_vertical_slice` 与 `test_audio_mixer_avx2` 100% PASS。
- [ ] **Check 2：音频回调零分配实证**：Debug 构建下断言音频混音热循环内堆分配为 **0 次**。
- [ ] **Check 3：硬切导出逐帧校验**：多段 1V+1A 硬切导出文件解复用帧数与理论 Flicks 换算帧数完全一致，切点处包含合法 IDR 帧。
- [ ] **Check 4：Linker .map 核心修饰符号留存**：
  - `NleProject::toJson` / `fromJson`
  - `CompiledRenderGraph::compile`
  - `mixAvx2`
  - `wsolaStretch`
