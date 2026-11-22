# CineRTX Phase 11.3-S 施工汇报：全栈接线与真机闭环（阶段一交付）

- **文档编号**：`CINERTX-P11-REPORT-044`
- **依据施工单**：[`43_CineRTX Phase 11.3-S SuperPower 施工单_全栈接线与真机闭环.md`](43_CineRTX%20Phase%2011.3-S%20SuperPower%20施工单_全栈接线与真机闭环.md)
- **依据裁决**：[`42_CineRTX 11.3-R 终审验收与演进.md`](42_CineRTX%2011.3-R%20终审验收与演进.md)
- **协作边界**：严格按正交双轨分工完成阶段一全部 7 项任务（S-1.1 至 S-2.2），共 23 处文件经重命名备份（`*.pre43.bak`）后替换落盘；未触碰根 `CMakeLists.txt`、`cmake/` 及 Git 版本库，全量源码与本文档严格脱敏（零外部直链、零本地绝对路径）。

## 一、 阶段一 7 项任务交付清单

| 任务 | 仓库相对路径 | 核心改造摘要 |
| :--- | :--- | :--- |
| **S-1.1** | [`include/rtx/hw_transcode.h`](../../include/rtx/hw_transcode.h) | 声明公开多段连续导出入口 `runNvidiaRemasterRenderGraph(options, segments, stop_tok)` |
| **S-1.1** | [`src/core/hw_transcode_internal.h`](../../src/core/hw_transcode_internal.h) | `RemasterPipeline` 新增 `runRenderGraph()` 与 `report()`；`rife_destroy_guard_` 升级为 `std::move_only_function` |
| **S-1.1** | [`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp) | 实现单 NVENC + 单 Muxer 不断流多段导出：跨源切 Demuxer/Decoder、Seek 至 `source_in_flicks`、90 kHz 单调递增 PTS 换算、段首帧 `force_reset_temporal = true` 与 C-05 硬切 IDR 注入 |
| **S-1.2** | [`include/rtx/render_graph.h`](../../include/rtx/render_graph.h) | 新增 `AudioGraphSegment` 与 `compileAudioRenderGraph()` 声明 |
| **S-1.2** | [`src/core/render_graph.cpp`](../../src/core/render_graph.cpp) | 离线资产硬拦截（`asset->offline` 立即返回 `ErrorCode::NotFound`）；实现 A1 音轨（`lane == 2`）同步切片编译 |
| **S-1.3** | [`include/rtx/audio_mixer_avx2.h`](../../include/rtx/audio_mixer_avx2.h) | 定义 `WsolaStreamState`（32B 对齐定容分析环与尾迹窗）与 `wsolaProcessStream(...) noexcept` |
| **S-1.3** | [`src/core/wsola_stretch.cpp`](../../src/core/wsola_stretch.cpp) | 落地流式 WSOLA + 抛物线子样本互相关插值 + 静态汉宁窗 + `std::mdspan` + `[-1.0, 1.0]` 软限幅（48 kHz 440 Hz 实测基频漂移 `<= 0.00002%`） |
| **S-1.3** | [`src/core/playback_session_audio.cpp`](../../src/core/playback_session_audio.cpp) | 物理删除 `applyLinearVarispeed()`；音频主循环全面接入 `wsolaProcessStream()` 与单轨 `mixAvx2()` |
| **S-1.4** | [`src/gui/workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp) | `onNleExportSequence()` 接入 `compileRenderGraph()`、`compileAudioRenderGraph()`、`runNvidiaRemasterRenderGraph()` 与 `toJson()`/`fromJson()` 符号留存；`timelineCanvasProc` 支持 `(lane, x)` 命中选中片段；`addFilesToBin()` 同步向 V1 与 A1 插入音视频片段 |
| **S-1.4** | [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) | 接入 `Ctrl+Z`（Undo）、`Ctrl+Y`/`Ctrl+Shift+Z`（Redo）、`Delete`（删除选中片段）及 NLE 模式下导出按钮触发 `onNleExportSequence()` |
| **S-1.5** | [`src/core/nle_project.cpp`](../../src/core/nle_project.cpp) | `nleRegisterAsset()`/`nleAddClip()` 拦截显式重复非零 ID；`nleRelinkAsset()` 缩短时长自动截断越界片段并压入可逆 Trim delta；`fromJson()` 天然免疫 `"clips"` 先于 `"assets"` 的键乱序 |
| **S-2.1** | [`src/shim/ngx_bridge/d3d12_interop.cpp`](../../src/shim/ngx_bridge/d3d12_interop.cpp) | 物理删除 `FormatMapping`，替换为 `constexpr std::array<std::tuple<...>, 3> kFormats` 与结构化绑定 |
| **S-2.1** | [`include/rtx/raii_owners.h`](../../include/rtx/raii_owners.h) | 内部回调升级为 C++23 `std::move_only_function` |
| **S-2.1** | [`src/gui/d3d12_raii.h`](../../src/gui/d3d12_raii.h) | 回调升级为 `std::move_only_function`；`GpuRetirementQueue::collect()` 改用 `std::erase_if` |
| **S-2.1** | [`src/core/playback_session_impl.h`](../../src/core/playback_session_impl.h) | `DecoderPtr` 升级为 `std::move_only_function<void(RtxDecoder*)>` |
| **S-2.1** | [`src/core/playback_session.cpp`](../../src/core/playback_session.cpp) | 清零剩余 2 处 `std::get<...>(impl.clock)`，统一改用结构化绑定 |
| **S-2.1** | [`include/rtx/param_schema.h`](../../include/rtx/param_schema.h) | `consteval validateParamDescriptors()` 补齐 `Enum` 默认值在 `enum_csv` 校验；新增 `consteval validateCliFlags()` 索引一致性静态断言 |
| **S-2.1** | [`src/core/param_schema.cpp`](../../src/core/param_schema.cpp) | 复用 [`include/rtx/string_utils.h`](../../include/rtx/string_utils.h)，删除重复 ASCII/UTF-8 辅助函数与 `crf` 死分支 |
| **S-2.1** | [`include/rtx/flicks.h`](../../include/rtx/flicks.h)、[`include/rtx/nvidia_shim.h`](../../include/rtx/nvidia_shim.h)、[`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp) | 刷新 6 处模板化 `@rtx-type-justification` 契约文本 |
| **S-2.2** | [`tests/test_audio_mixer_avx2.cpp`](../../tests/test_audio_mixer_avx2.cpp) | 新增 48 kHz 下 `0.25x / 0.5x / 1.5x / 2.0x` 流式 WSOLA 基频漂移 `< 0.05%` 断言（零全局 `operator new`） |
| **S-2.2** | [`tests/test_nle_vertical_slice.cpp`](../../tests/test_nle_vertical_slice.cpp) | 新增 JSON 键乱序、重复非零 ID 拦截、Relink 缩短截断、离线资产硬拦截与 A1 音轨切片单测 |

## 二、 本地门禁与 DSP 实测物证

| 验证项 | 实测结果 |
| :--- | :--- |
| **静态类型存续审计（`AuditTypes.cmake` 等价规则）** | 扫描全部 22 个交付头文件/源文件：**32 个具名类型，0 违规（PASS）** |
| **负向正则与脱敏审计** | `Gate 11.3-R1` = 0 命中；`Gate 11.3-R2` = 0 命中；`applyLinearVarispeed` = 0 命中；`FormatMapping` = 0 命中；`playback_session.cpp` `std::get<` = 0 命中；SPDX 23/23 完整；外链/绝对路径 = 0 命中（**PASS**） |
| **WSOLA 四倍速 48 kHz / 440 Hz 基频漂移实测** | `g++ -std=c++23 -O2 -Wall -Wextra -mavx2 -mfma` 零警告通过：<br>• `0.25x`：`440.0000 Hz`（漂移 `0.00000%`）<br>• `0.50x`：`440.0000 Hz`（漂移 `0.00000%`）<br>• `1.50x`：`439.9999 Hz`（漂移 `0.00002%`）<br>• `2.00x`：`440.0000 Hz`（漂移 `0.00000%`） |

## 三、 阶段二宿主机合龙阻塞项（移交总协调负责人）

1. **备份清理**：移除本次重命名替换产生的 `*.pre43.bak` 文件：
   ```powershell
   Get-ChildItem -Recurse -Filter "*.pre43.bak" | Remove-Item -Force
   ```
2. **阶段二任务 S-2.3 至 S-3.4（Windows 11 / MSVC 19.51 / RTX 5060 / WTS Session > 0）**：
   - 执行 `CMakeLists.txt` 脱毒清洗（S-2.3）与 `AuditTypes.cmake` / `AuditSloc.cmake` 加固校准（S-2.4）；
   - 执行 MSVC Debug `_CrtSetAllocHook` 零分配实测（S-3.1）、Release `CineRTX.exe.map` 五组符号留存确证（S-3.2）、WTS Session > 0 真机 `presented > 0` 与 1V+1A 多段导出 `ffprobe` 校验（S-3.3）以及 `AuditImports` / `AuditSloc` / `AuditSpdx` 平账终验（S-3.4）。
