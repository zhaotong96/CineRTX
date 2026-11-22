# CineRTX Phase 11.3 SuperPower 施工单：统一 NLE 引擎与零分配 AVX2 音频总线合龙（终审执行版）

- **文档编号**：`CINERTX-P11-WORKORDER-036`（全面替代并废止 `CINERTX-P11-WORKORDER-019-REV2`）
- **归档路径**：[`docs/phase11/36_CineRTX Phase 11.3 SuperPower 施工单_统一NLE引擎与零分配AVX2音频总线.md`](36_CineRTX%20Phase%2011.3%20SuperPower%20施工单_统一NLE引擎与零分配AVX2音频总线.md)
- **承接角色**：NLE 剪辑架构与高性能 AVX2 音频执行工程师（外部执行工程师）
- **发单主体**：CineRTX 项目总协调负责人（Coordinator）
- **前置基线**：
  1. [`34 号专家终审裁决书`](34_CineRTX%20Phase%2011.2-R%20项目卫生达标与整改收口专家终审验收裁决书.md)（PASS 结案封板，解除 Phase 11.3 冻结令）；
  2. [`35 号终审方案`](35_CineRTX%20Phase%2011.2-R%20终审方案.md) 第三节 Day-0 显微修补已由总协调负责人 **100% 修复闭环**；
  3. `cmake/AuditSloc.cmake` 确立同口径入场基线：规范集合 135 文件、38,324 物理行、4,732 语义注释行、31,064 规范纯净 SLOC（全仓基准 38,562 行）；
  4. MSVC `/MT` 14.51 (`C++26`) 30/30 构建目标 **0 错误 0 警告**；
  5. `rtx_tests.exe` **128/128 PASS（0 failed, 0 skip）** 双目录实测绿灯；
  6. `AuditTypes.cmake`（150 具名类型 0 违规）与 `AuditSpdx.cmake`（GPL=98 MIT=37）全绿。
- **依据母本**：[`16_CineRTX Phase 11 演进计划.md`](16_CineRTX%20Phase%2011%20演进计划.md) · [`21_CineRTX Phase11 施工方案深度审查.md`](21_CineRTX%20Phase11%20施工方案深度审查.md) · [`29_CineRTX 11.2-R 专家裁决方案.md`](29_CineRTX%2011.2-R%20专家裁决方案.md) · [`35_CineRTX Phase 11.2-R 终审方案.md`](35_CineRTX%20Phase%2011.2-R%20终审方案.md)
- **建议工期**：3–4 个有效工作日
- **核心目标**：统一 705.6 MHz Flicks 时间基、落地 `NleProject` 与 7 类可逆剪辑命令（含 Relink）、修复 NVENC 硬切 IDR 漏传断链（C-05）、实现 `mixAvx2()` 零堆分配与流式 `WsolaStreamState` 接入，并刚性兑现 **-400 至 -600 Pure SLOC 负增长追偿预算**。

---

## 〇、 外部工程师前置纪律与 19 号单勘误声明

为杜绝重演 Phase 11.2 施工中发生的“古德哈特定律破坏代码卫生（私删 4,827 行注释伪造行数）”与“单测孤岛”问题，外部执行工程师必须严格知悉并遵守以下四项刚性边界：

1. **版本库权限与参考资产边界**：
   外部执行工程师**严禁且无法访问 Git 版本库**。施工单中提及的历史提纯参考材料均由项目负责人在仓内工作区物理就位，严禁私自尝试执行任何 `git` 检出或分支操作。
2. **过时符号纠偏（勘误 19 号单 Task 4）**：
   原 19 号单提及的“全面替换变调失真的 `VarispeedCursor`”已在 Phase 11.2-R 中被彻底消灭。本阶段实际替换目标为 [`src/core/playback_session_audio.cpp`](../../src/core/playback_session_audio.cpp) 中的自由函数 `applyLinearVarispeed()`。
3. **基础设施复用律（严禁重复造轮子）**：
   `NleProject` 的 JSON 序列化与反序列化**必须 100% 复用既有 [`include/rtx/json_scanner.h`](../../include/rtx/json_scanner.h)**。严禁重演此前在 `nle_project.cpp` 私自复制代码复制上千行 JSON 扫描器与 UTF-8 代理对解码器的恶劣膨胀行为。
4. **代码注释零删减与 `AuditTypes.cmake` 刚性约束**：
   延续双轨度量口径，所有真实业务注释、SPDX 许可证头与 `@rtx-type-justification` 契约块均受保护，**严禁删减注释或压缩空行凑数**。所有新增具名类/结构体必须在声明上方 8 行内标注合规的存续契约，严禁复制粘贴模板文案。

---

## 一、 核心开发任务与实施规范

### 任务 1：开工首日全量拔旧清债（预计立刻收回 ~200 行 SLOC）
**修改文件**：[`src/gui/workstation_canvas.h`](../../src/gui/workstation_canvas.h)、[`src/gui/workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp)

1. **物理剔除画布过渡性毫秒级时间线遗产**：
   在开始接入新架构前，必须在 `workstation_canvas.h/.cpp` 中**物理删除**全部过渡性模拟遗产：
   - 全局静态与类型：`sharedNleTimeline()`、`NleTimeline`、`NleClip`；
   - 换算函数：`kFlicksPerMs`、`msToFlicks()`、`formatFlicksTimecode()`、`timelineXToMs()`、`timelineMsToX()`；
   - 粗粒度逻辑：`onNleRazor` 中残留的 `100` ms 硬编码偏移及任何 `*_ms` 局部变量。
2. **验收铁证门禁**：
   阶段验收时，对 `src/gui/workstation_canvas.h` 与 `src/gui/workstation_canvas.cpp` 执行负向正则扫描：
   ```
   NleClip|NleTimeline|sharedNleTimeline|kFlicksPerMs|msToFlicks|formatFlicksTimecode|timelineXToMs|timelineMsToX|_ms
   ```
   **必须返回 0 命中（0 matches）**。

---

### 任务 2：基于 `json_scanner.h` 落地 `NleProject` 与 7 类可逆命令
**新建/修改文件**：[`include/rtx/nle_project.h`](../../include/rtx/nle_project.h)、[`src/core/nle_project.cpp`](../../src/core/nle_project.cpp)、[`tests/test_nle_vertical_slice.cpp`](../../tests/test_nle_vertical_slice.cpp)

1. **原生 705,600,000 Hz Flicks 单一时间基**：
   所有片段（`Clip`）、轨道（`Track`）、过渡、播放头与工程总长严格以 64 位整数 Flicks 计量（`include/rtx/time_model.h`），与播放内核绝对对齐。
2. **复用 `json_scanner.h` 实现工程序列化**：
   `NleProject::toJson()` 与 `NleProject::fromJson()` 全量基于 `include/rtx/json_scanner.h` 实现，支持工程元数据、资产表（`MediaAsset`）、多轨结构（V2/V1/A1/S1）及片段入出点的精确无损持久化。
3. **7 类可逆编辑命令栈（`EditHistory`）**：
   实现以下 7 类编辑命令：
   - `AddClipCommand`
   - `TrimClipCommand`
   - `SplitClipCommand`
   - `MoveClipCommand`
   - `DeleteClipCommand`
   - `ReorderTracksCommand`
   - **`RelinkAssetCommand`**（支持离线资产重新关联、元数据验证与时间线安全重绑定）
   - **绝对红线**：命令对象仅记录工程元数据 Delta，**严禁持有任何 GPU / COM / CUDA 句柄或大块内存**，确保任意深度的 `undo()` / `redo()` 零泄漏、确定性可逆。

---

### 任务 3：`src/gui/workstation_canvas.cpp` 全量接合新剪辑内核
**修改文件**：[`src/gui/workstation_canvas.h`](../../src/gui/workstation_canvas.h)、[`src/gui/workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp)、[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)

1. **安全状态接线**：
   原通过 `GWLP_USERDATA` 挂载的 `NleWindowState` 全面绑定 `NleProject` 与 `EditHistory` 实例；
2. **交互全量切入 SMPTE 时间码**：
   画布时间轴的缩放比例、标尺刻度绘制、磁吸对齐（Snap）、入出点标记（`MarkIn`/`MarkOut`）、剃刀切割（Razor）及 Ctrl+Z / Ctrl+Y 撤销重做，全面切至原生 Flicks 与 `formatSmpteTimecode()`，杜绝浮点累计漂移；
3. **素材箱与时间轴联动**：
   从素材箱拖拽、右键添加或双击素材时，生成对应的 `AddClipCommand` 并下发至工程，立即驱动时间轴重绘与播放内核定位。

---

### 任务 4：`CompiledRenderGraph` 驱动与 NVENC 硬切 IDR 修复（C-05）
**新建/修改文件**：[`include/rtx/render_graph.h`](../../include/rtx/render_graph.h)、[`src/core/render_graph.cpp`](../../src/core/render_graph.cpp)、[`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp)

1. **补齐 NVENC 强制 IDR 链路（C-05 闭环）**：
   在 [`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp) 的 `encodeWorkerBody()` 中，针对 GPU NVENC 编码分支显式补齐关键帧标记赋值：
   ```cpp
   encode_frame.keyframe = item.force_keyframe ? 1 : 0;
   ```
   使 `nv_encoder.cpp` 在硬切切点处能精准下发 `NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS`。
2. **多段硬切容器导出合龙**：
   由 `CompiledRenderGraph` 编译时间线片段为执行图。保持单一 `encoder_` 与 `muxer_` 实例在整个导出生命周期内不断流（维持以时间线 Flicks 换算 90 kHz 的单调连续 PTS）。按图切换或 Seek 源素材的解复用器与解码器，并在跨素材切点处下发 `force_reset_temporal = true`（驱动 `NVSDK_NGX_Parameter_Reset` 清空 VSR 时域历史）与强制关键帧。
3. **输出校验唯一铁证**：
   导出的 MP4/MKV 文件经容器解复用工具逐帧验证其实际总帧数、切点 IDR 关键帧位置与时间线理论值 100% 吻合。

---

### 任务 5：`mixAvx2()` 零堆分配重构与流式 `WsolaStreamState`
**新建/修改文件**：[`include/rtx/audio_mixer_avx2.h`](../../include/rtx/audio_mixer_avx2.h)、[`src/core/audio_mixer_avx2.cpp`](../../src/core/audio_mixer_avx2.cpp)、[`src/core/wsola_stretch.cpp`](../../src/core/wsola_stretch.cpp)、[`src/core/playback_session_audio.cpp`](../../src/core/playback_session_audio.cpp)、[`tests/test_audio_mixer_avx2.cpp`](../../tests/test_audio_mixer_avx2.cpp)

1. **消除音频热循环堆分配**：
   彻底消除每次调用构造 `std::vector<VoicePrep>` 的行为，改用由调用方预分配并长期持有的 `AudioMixerScratchpad`（或在栈上以 `std::array<VoicePrep, 32>` 局部处理分块）。
   - **实时音频热循环四条红线**：零锁（Zero Locks）、零堆分配（Zero Allocations）、零 I/O（Zero I/O）、零无界循环（Zero Unbounded Loops）；
   - 在 [`tests/test_audio_mixer_avx2.cpp`](../../tests/test_audio_mixer_avx2.cpp) 中通过 Debug 构建重载全局 `operator new`，断言混音热循环执行期间堆分配次数**严格为 0**。
2. **流式 WSOLA 状态机接入**：
   围绕 `wsolaStretch` 封装带预设环形分析窗与汉宁重叠相加尾迹区的 `WsolaStreamState`（所有缓冲区在会话创建期一次性预分配），全面替换 `playback_session_audio.cpp` 中的 `applyLinearVarispeed()`。在 0.25x–2.0x 变速播放下确保基频漂移 $< 0.05\%$，无破音、无相位抵消。

---

## 二、 双轨体量刚性核算与赤字平账预算

延续 Phase 11.2-R 确立的科学度量基准，Phase 11.3 结项时必须直接以 `cmake/AuditSloc.cmake` 检验净效果：

```
+----------------------------------------------------------------------------------+
| CineRTX Phase 11.3 SLOC 预算平账表（Entry to Exit Ledger）                        |
+----------------------------------------------------------------------------------+
| 1. 入场纯净 SLOC 水位（AuditSloc.cmake 锁定）:   31,064 行（全仓基准 38,562 行）   |
| 2. 保护语义注释行（SPDX + 契约注释，严禁删减）:   4,732 行（全仓基准  4,827 行）   |
|                                                                                  |
| 3. Phase 11.3 施工预算增减控制：                                                 |
|    - 任务 1 拔除 workstation_canvas 毫秒级旧代码:      回收 ~200 行               |
|    - 任务 2~5 新增 NleProject、Graph、AVX2 核心代码:   新增 ~450 行               |
|    - 复用 json_scanner 与标准库泛型替代结构:           节约 ~400 行               |
|                                                                                  |
| 4. Phase 11.3 结项刚性指标要求：                                                 |
|    - 全仓 Pure SLOC 必须实现净减少:                    -400 至 -600 行            |
|    - 结项 Pure SLOC 红线上限（AuditSloc 读数）:        <= 30,664 行               |
|    - 冲抵 Phase 11.2-R 结转赤字（~1,200 行）额度:      已平账 400~600 行          |
+----------------------------------------------------------------------------------+
```

---

## 三、 外部工程师交付验收检查清单（Gates）

外部执行工程师提交代码前，必须在宿主机环境（Session 1 + RTX GPU）全量跑通以下 6 道验收门禁并附上物理物证：

| 门禁项 | 验证命令与检查焦点 | 达标合格线 |
| :--- | :--- | :---: |
| **Gate 11.3-1（旧码清零）** | 对 `src/gui/workstation_canvas.*` 正则扫描 `NleClip\|NleTimeline\|kFlicksPerMs\|msToFlicks\|_ms` | **0 命中** |
| **Gate 11.3-2（单测全绿）** | `rtx_tests.exe`（含新增 `test_nle_vertical_slice` 与 `test_audio_mixer_avx2`） | **100% PASS (0 fail)** |
| **Gate 11.3-3（音频零分配）** | Debug 构建下执行 `test_audio_mixer_avx2`，全局 `operator new` 计数器验证 | **0 次堆分配** |
| **Gate 11.3-4（硬切 IDR 闭环）** | 运行多段 1V+1A 导出，以 FFmpeg 解复用核验导出的 MP4/MKV 总帧数与切点 IDR 帧位置 | **逐帧完全吻合** |
| **Gate 11.3-5（符号留存）** | `CineRTX.exe.map` 符号审计：`NleProject::toJson/fromJson`、`CompiledRenderGraph::compile`、`mixAvx2`、`wsolaStretch` | **全部物理留存** |
| **Gate 11.3-6（门禁与 SLOC）** | `cmake -P cmake/AuditTypes.cmake` (0 违规)<br>`cmake -P cmake/AuditSpdx.cmake` (PASS)<br>`cmake -P cmake/AuditImports.cmake` (PASS)<br>`cmake -P cmake/AuditSloc.cmake` (Pure SLOC 净减 400~600 行) | **四项全绿** |

---

**发单落款**：CineRTX 项目总协调负责人  
**签发日期**：2026 年 11 月 21 日
