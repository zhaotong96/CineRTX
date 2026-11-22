# CineRTX Phase 11.3-S SuperPower 施工单：全栈接线与真机闭环

- **文档编号**：`CINERTX-P11-WORK-ORDER-043`
- **归档路径**：[`docs/phase11/43_CineRTX Phase 11.3-S SuperPower 施工单_全栈接线与真机闭环.md`](43_CineRTX%20Phase%2011.3-S%20SuperPower%20施工单_全栈接线与真机闭环.md)
- **签发主体**：CineRTX 项目总协调负责人（Coordinator）
- **依据工件**：
  * [`docs/phase11/42_CineRTX 11.3-R 终审验收与演进.md`](42_CineRTX%2011.3-R%20终审验收与演进.md)
  * [`docs/phase11/38_CineRTX 现代 C++ 极限重构审计.md`](38_CineRTX%20现代%20C%2B%2B%20极限重构审计.md)
  * [`docs/phase11/40_CineRTX Phase 11.3-R 施工汇报_极限现代C++重构与NLE全栈合龙.md`](40_CineRTX%20Phase%2011.3-R%20施工汇报_极限现代C%2B%2B重构与NLE全栈合龙.md)
  * [`docs/phase11/41_CineRTX Phase 11.3-R 极限现代C++重构与NLE全栈合龙专家终审验收委托书.md`](41_CineRTX%20Phase%2011.3-R%20极限现代C%2B%2B重构与NLE全栈合龙专家终审验收委托书.md)
- **施工目标**：依据专家组 42 号终审裁决（`CONDITIONAL PASS`），保留 11.3-R 全部优质现代 C++ 成果，打通核心算子向 `CineRTX.exe` 终端调用的“最后一公里”，物理根治死代码剥离风险、多段导出断链、音频变调失真与 GUI 交互脱节，完成四大门禁实机闭环，正式兑现 NLE 全栈战略封板。
- **预定工期**：2 个工作日。

---

## 一、 正交分工与协作边界

为确保施工零踩踏、零延误，本次冲刺严格维持正交双轨分工：

| 职责角色 | 运行环境 | 责任边界与交付工件 |
| :--- | :--- | :--- |
| **Round 3 高级执行工程师** | 离线无头 Linux 沙箱（G++ 12+ / Clang 18+，无 Win32/GPU） | **认领阶段一（纯 C++ 算法与产品接线）**：<br>1. `hw_transcode_stages.cpp` 多段执行图导出流水线（S-1.1）；<br>2. `render_graph.cpp` 离线资产拦截与 A1 音轨切片（S-1.2）；<br>3. `wsola_stretch.cpp` 落地 `WsolaStreamState` 并于 `playback_session_audio.cpp` 拔除 `applyLinearVarispeed`（S-1.3）；<br>4. `rtx_gui.cpp` 与 `workstation_canvas.cpp` 交互接线（S-1.4）；<br>5. `nle_project.cpp` 乱序解析与重复 ID 防御（S-1.5）；<br>6. 源码洁癖尾差清零（S-2.1）；<br>7. 单测扩展用例（S-2.2）。 |
| **项目总协调负责人** | 本地 Windows 11 宿主机（MSVC 19.51 / RTX 5060 / WTS Session > 0） | **认领阶段二（构建脱毒、真机复测与门禁平账）**：<br>1. `CMakeLists.txt` 深度脱毒与属性单点收拢（S-2.3）；<br>2. `AuditTypes.cmake` 升级为 `file(READ)` 根除方括号解析漏洞（S-2.4）；<br>3. MSVC Debug 编译与 `_CrtSetAllocHook` 零分配物证捕获（S-3.1）；<br>4. `CineRTX.exe.map` 符号留存确证（防 `/OPT:REF` 剥离）（S-3.2）；<br>5. WTS Session > 0 桌面实测 `presented > 0` 与 1V+1A 真实多段导出（S-3.3）；<br>6. `AuditImports.cmake` 与 `AuditSloc.cmake` 账本归一平账（S-3.4）。 |

---

## 二、 阶段一：高级执行工程师任务清单（纯 C++ 语言与业务接线）

### 任务 S-1.1：`RemasterPipeline` 多段连续导出流水线接线
- **修改文件**：[`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h)、[`src/core/hw_transcode_internal.h`](../../src/core/hw_transcode_internal.h)、[`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp)
- **技术要求**：
  1. 在 `RemasterPipeline` 中增加接收执行图多段切片的导出方法：
     ```cpp
     [[nodiscard]] Result<void> runRenderGraph(std::span<const RenderGraphSegment> segments,
                                               std::stop_token stop_tok = {});
     ```
  2. **单实例连续编码流**：在整个多段导出过程中，保持单一 `nvenc_` 硬件编码器与单一 `muxer_` 容器封装实例不断流，避免段间重建引发文件截断；
  3. **单调递增 90 kHz PTS 换算**：按段在时间线上的绝对 Flicks 坐标换算输出 PTS：
     $$\text{pts\_90k} = \text{duration\_cast}\left\langle\text{std::chrono::duration}\langle\text{int64\_t}, \text{std::ratio}\langle 1, 90000\rangle\rangle\right\rangle(\text{segment.timeline\_start\_flicks} + \text{frame\_flicks\_offset}).\text{count}()$$
  4. **源切换与时域重置**：当跨越段边界或素材路径切换时，复用或重建 `demuxer_` 与 `decoder_`，精准 Seek 至 `segment.source_in_flicks`；每个段的首帧强制置位：
     ```cpp
     encode_frame.force_reset_temporal = true;                     // 驱动 VSR 重置历史帧缓存
     encode_frame.keyframe = segment.force_keyframe ? 1 : 0;       // 闭环 C-05：硬切点强制注入 IDR 关键帧
     ```

### 任务 S-1.2：`render_graph.cpp` 离线资产拦截与 A1 音轨切片
- **修改文件**：[`include/rtx/render_graph.h`](../../include/rtx/render_graph.h)、[`src/core/render_graph.cpp`](../../src/core/render_graph.cpp)
- **技术要求**：
  1. **离线资产硬拦截**：扫描线在处理片段时，若发现 `asset->offline == true`，立即返回 `makeError(ErrorCode::NotFound, "timeline references offline asset: ...")`，严禁静默 `continue` 产生黑帧；
  2. **A1 音轨（lane == 2）同步切片**：扩展 `compileRenderGraph` 返回结构或提供 `compileAudioRenderGraph`，统一提取 A1 轨音频片段的时间线区间、源入出点与片段音量增益（`gain`），输出 `std::vector<AudioGraphSegment>` 供多段导出和音频总线消费。

### 任务 S-1.3：流式 WSOLA 状态机与音频主循环彻底接入
- **修改文件**：[`include/rtx/audio_mixer_avx2.h`](../../include/rtx/audio_mixer_avx2.h)、[`src/core/wsola_stretch.cpp`](../../src/core/wsola_stretch.cpp)、[`src/core/playback_session_audio.cpp`](../../src/core/playback_session_audio.cpp)
- **技术要求**：
  1. 在 `wsola_stretch.cpp` 中定义具备完整契约的流式状态机：
     ```cpp
     // @rtx-type-justification
     // Category: NonTrivial-StateMachine
     // Why-Std-Fails: 跨缓冲块保持重叠相加尾迹进位与历史互相关分析窗，std 容器缺乏步长对齐与定容环形历史进位语义。
     // Invariant: speed_ratio > 0.0f; ring_head < kAnalysisWindow; tail_frames <= kOverlapFrames.
     struct WsolaStreamState {
         alignas(32) std::array<float, 2048> analysis_ring{};
         alignas(32) std::array<float, 512> overlap_tail{};
         size_t tail_frames{0};
         float last_speed{1.0f};
     };
     ```
  2. 实现流式时间伸缩函数：
     ```cpp
     [[nodiscard]] size_t wsolaProcessStream(WsolaStreamState& state,
                                             std::span<const float> input,
                                             std::span<float> output,
                                             float speed_ratio) noexcept;
     ```
     在 48 kHz 采样率下保持基频漂移 $< 0.05\%$；并在混音输出端按需接入 $[-1.0, 1.0]$ 软限幅保护；
  3. **物理拔除变调旧函数**：在 [`src/core/playback_session_audio.cpp`](../../src/core/playback_session_audio.cpp) 中彻底物理删除 `applyLinearVarispeed()`，音频主时钟全速切至 `wsolaProcessStream` 与单轨 `mixAvx2`，彻底杜绝调速时的音调畸变。

### 任务 S-1.4：GUI 交互与 NLE 操作全链路闭环接线
- **修改文件**：[`src/gui/workstation_canvas.h`](../../src/gui/workstation_canvas.h)、[`src/gui/workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp)、[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)
- **技术要求**：
  1. **快捷键与动作接入**：在 `rtx_gui.cpp` 窗口消息循环与菜单命令中接入：
     - `Ctrl+Z` $\rightarrow$ `onNleUndo()`；
     - `Ctrl+Y` / `Ctrl+Shift+Z` $\rightarrow$ `onNleRedo()`；
     - `Delete` $\rightarrow$ `onNleDeleteSelected()`；
  2. **序列导出真机触发**：修改 `onNleExportSequence()`，调用 `compileRenderGraph()` 生成切片序列后，启动后台 `std::jthread` 调用 `RemasterPipeline::runRenderGraph` 执行实际多段导出，并在导出完毕后通过 `PostMessageW(hwnd, WM_APP_NLE_EXPORT_DONE, ...)` 回传状态；
  3. **画布操作增强**：
     - `timelineCanvasProc` 补充根据鼠标 `(lane, x)` 命中选中对应片段并设置 `project.selected_clip_id`；
     - `addFilesToBin()` 在向时间线放置媒体时，同时向 V1 插入视频段、向 A1 插入对应音频段，严禁 A1 留空。

### 任务 S-1.5：`nle_project.cpp` 边界防御与鲁棒性加固
- **修改文件**：[`src/core/nle_project.cpp`](../../src/core/nle_project.cpp)
- **技术要求**：
  1. **JSON 键乱序防御**：重构 `NleProject::fromJson()` 解析流程，支持 `"clips"` 数组出现在 `"assets"` 数组之前的合规 JSON，先解析原始 AST 再二次解析引用，杜绝误报未知素材；
  2. **显式重复 ID 拦截**：在 `nleRegisterAsset()` 与 `nleAddClip()` 中，若输入显式带有非零 `id`，必须先检查 `project` 中是否存在重复 `id`，存在冲突时返回错误，杜绝静默跳过插入导致 Undo 栈撤销误删原片段；
  3. **Relink 缩短时长校验**：在 `nleRelinkAsset()` 中，若重新链接的新素材时长短于旧素材，自动遍历校验所有引用该素材的片段，若 `out_flicks > new_duration` 则自动截断至新时长并记录 delta，防止越界解码。

### 任务 S-2.1：源码洁癖尾差清零与现代 C++ 规范收口
- **修改文件**：[`src/shim/ngx_bridge/d3d12_interop.cpp`](../../src/shim/ngx_bridge/d3d12_interop.cpp)、[`include/rtx/raii_owners.h`](../../include/rtx/raii_owners.h)、[`include/rtx/d3d12_raii.h`](../../include/rtx/d3d12_raii.h)、[`src/core/playback_session_impl.h`](../../src/core/playback_session_impl.h)、[`src/core/playback_session.cpp`](../../src/core/playback_session.cpp)、[`src/core/param_schema.cpp`](../../src/core/param_schema.cpp)、[`include/rtx/param_schema.h`](../../include/rtx/param_schema.h)、[`include/rtx/flicks.h`](../../include/rtx/flicks.h)、[`include/rtx/nvidia_shim.h`](../../include/rtx/nvidia_shim.h)、[`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp)
- **技术要求**：
  1. 将 `d3d12_interop.cpp` 中的 `FormatMapping` 彻底替换为 `constexpr std::array<std::tuple<...>, 3>`；
  2. 将 `raii_owners.h`、`d3d12_raii.h`、`playback_session_impl.h` 中的 `std::function` 全面升级为 C++23 `std::move_only_function`，并在 `GpuRetirementQueue::collect()` 中采用 `std::erase_if`；
  3. 消除 `playback_session.cpp` 中剩余的 2 处 `std::get<...>(impl.clock)`，改用结构化绑定；
  4. 让 `param_schema.cpp` 直接复用 [`include/rtx/string_utils.h`](../../include/rtx/string_utils.h)，删除重复的 ASCII 辅助函数与 `crf` 死分支；在 `param_schema.h` 的 `consteval validateParamDescriptors()` 中补齐对 `Enum` 默认值在 `enum_csv` 内以及 `kCliFlags` 索引一致性的断言；
  5. 刷新 `flicks.h`、`nvidia_shim.h`、`ffmpeg_io.cpp` 中模板化失真的 `@rtx-type-justification` 契约文本。

### 任务 S-2.2：单元测试用例扩展
- **修改文件**：[`tests/test_audio_mixer_avx2.cpp`](../../tests/test_audio_mixer_avx2.cpp)、[`tests/test_nle_vertical_slice.cpp`](../../tests/test_nle_vertical_slice.cpp)
- **技术要求**：
  1. 在 `test_audio_mixer_avx2.cpp` 中新增 WSOLA 0.25x / 0.5x / 1.5x / 2.0x 调速基频漂移测试，断言音高漂移 $< 0.05\%$；
  2. 在 `test_nle_vertical_slice.cpp` 中新增 JSON 键乱序反序列化成功用例、重复非零 ID 插入拦截用例、以及多段扫描线导出切片结构完整性用例。

---

## 三、 阶段二：项目总协调负责人收尾清单（宿主机集成与真机闭环）

### 任务 S-2.3：`CMakeLists.txt` 深度脱毒清洗
- **修改文件**：[`CMakeLists.txt`](../../CMakeLists.txt)
- **技术要求**：
  1. 清理重复定义的 `set(RTX_EXTERNAL_DIR ...)`；
  2. 提取公共编译选项（`CXX_STANDARD 23`, `CXX_STANDARD_REQUIRED ON`, `CXX_EXTENSIONS OFF`），统一收拢至 `rtx_project_options`，删除 5 个目标的重复属性硬编码；
  3. 将 `string(REPLACE "/O2" "/O1" ...)` 严格收窄至 `CineRTX` 目标本身，杜绝污染全局子库编译标志；
  4. 清理 `rtx_ngx_bridge_static` 重复声明的 `SYSTEM PRIVATE` 头路径、末尾孤立的 `if(WIN32) endif()` 僵尸块、以及针对旧版 Zig 的阴影库残留。

### 任务 S-2.4：`cmake/AuditTypes.cmake` 词法解析加固
- **修改文件**：[`cmake/AuditTypes.cmake`](../../cmake/AuditTypes.cmake)、[`cmake/AuditSloc.cmake`](../../cmake/AuditSloc.cmake)
- **技术要求**：
  1. 将 `AuditTypes.cmake` 从 `file(STRINGS ... ENCODING UTF-8)` 升级为与 `AuditSloc.cmake` 相同的 `file(READ)` + 字符转义切分行方案，从根本上免疫注释中的半开区间方括号 `[` 或分号 `;` 对 CMake 词法解析器的干扰；
  2. 校准 `AuditSloc.cmake` 提示横幅，将其入场基线明确修正为 Commit `749fe0d` 对应的 **31,064 Pure SLOC**。

### 任务 S-3.1：MSVC Debug 构建与 `_CrtSetAllocHook` 零分配物证捕获
- **操作要求**：
  1. 执行 `cmake --build .vs/build/cmake/windows-msvc-ninja --config Debug`；
  2. 执行 `rtx_tests.exe --filter audio_mixer`，在包含 `_DEBUG` 的环境下实测并捕获 Debug CRT `_CrtSetAllocHook` 堆分配计数严格等于 0 的终端实测日志，固化为零堆分配物理物证。

### 任务 S-3.2：Linker `.map` 核心符号留存确证
- **操作要求**：
  1. 在全开 `/OPT:REF /OPT:ICF` 优化的 Release 构建下生成 `CineRTX.exe.map`；
  2. 使用 PowerShell 正则检索 `CineRTX.exe.map` 的 `Publics by Value` 节区：
     ```powershell
     Select-String -Path .vs/build/cmake/windows-msvc-ninja/CineRTX.exe.map -Pattern "toJson|fromJson|compileRenderGraph|mixAvx2|wsola"
     ```
  3. 确证上述 5 组核心非内联符号全部被终端产品物理引用并留存，零死代码剥离。

### 任务 S-3.3：WTS Session > 0 交互式桌面全口径实测
- **操作要求**：
  1. 在交互式桌面（WTS Session ID > 0，NVIDIA 驱动 $\ge 621.24$ WHQL）下运行全量单测：
     - 验证 `preview_viewport_100x` 实测呈现指标：$\text{presented} > 0$ 且 $\text{D2H} == 0\text{ B}$；
  2. 启动 `CineRTX.exe`，执行一次包含 1V + 1A 的真实时间线编辑，执行撤销/重做，并触发序列导出为多段 `.mkv`；
  3. 使用 `ffprobe` 逐帧校验输出视频，验证段间切点处 IDR 关键帧存在且 90 kHz 时间戳单调无回跳。

### 任务 S-3.4：二进制体积与 SLOC 账本归一平账
- **操作要求**：
  1. 运行 `cmake -P cmake/AuditImports.cmake`，确证 `CineRTX.exe` 体积受控于 1.2–12.0 MB 包络且零驱动/第三方 DLL 泄漏；
  2. 运行 `cmake -P cmake/AuditSloc.cmake`，在剔除 CMakeLists 冗余与完成尾差收口后，如实核算全仓 Pure SLOC 最终落位，在审计报告中完成透明平账；
  3. 运行 `cmake -P cmake/AuditSpdx.cmake`，确证 100% 许可证合规。

---

## 四、 施工交付约束与验收硬门禁

1. **绝对脱敏约束**：全仓新增或修改的任何源码、注释与文档中，**严禁出现任何 外部私有云盘 外部直链或本地 本地绝对URI 绝对路径**，统一采用仓库内相对路径。
2. **零提交与不推送纪律**：执行工程师与协调负责人在未获得用户最终终审封板授权前，**严禁执行任何 `git commit` 或 `git push`**。
3. **准出验收矩阵**：
   - Gate S-1（编译全绿）：MSVC 19.51 Debug 与 Release 双模式构建 0 错误 0 警告；
   - Gate S-2（单测全绿）：CTest 全量 133+ 项单测 100% PASS，全程 0 蜂鸣；
   - Gate S-3（四门禁全绿）：`AuditTypes`（152+ 类型合规）、`AuditSpdx`（PASS）、`AuditImports`（PASS）、`AuditSloc`（平账记录落盘）；
   - Gate S-4（产品接线闭环）：`CineRTX.exe.map` 核心符号全部留存，多段硬切容器导出通过逐帧一致性复核。
