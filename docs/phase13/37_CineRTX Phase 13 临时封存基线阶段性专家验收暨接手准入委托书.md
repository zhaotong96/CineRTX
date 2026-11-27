# 37_CineRTX Phase 13 临时封存基线阶段性专家验收暨接手准入委托书

- **文档编号**：`CINERTX-P13-DELEGATION-037`
- **归档路径**：[`docs/phase13/37_CineRTX Phase 13 临时封存基线阶段性专家验收暨接手准入委托书.md`](./37_CineRTX%20Phase%2013%20临时封存基线阶段性专家验收暨接手准入委托书.md)
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）
- **受呈报方**：多媒体系统工程、实时图形合成与异构计算首席领域专家组（Chief Domain Experts & Architecture Board）
- **审计基线**：Commit `9355120`（`wip(phase13): temporary checkpoint - MSVC full build, 5 static gates, and 150/150 tests green`）
- **委托目的**：在协调负责人正式接手本机实装 `31`（WebUI 写帧尾项）与 `32`–`34` 号深水区施工单之前，提请首席领域专家组对当前 `9355120` 阶段成果开展**穿透式阶段性验收与排雷审计**——提前拦截前期代码中的显性错误、伪闭环与潜在架构风险，若有硬伤先退回工程师返工清零，确认无误后再签发后续波次接手准入令。

---

## 一、 当前阶段封存基线实证状态（Commit `9355120`）

| 验证维度 | 当前实测事实 |
| :--- | :--- |
| **MSVC 全量编译 (`.\build.bat`)** | 退出码 `0`：`rtx_ngx_bridge.dll`、`rtx_core.lib`、`rtx_headless_cli.exe`、`rtx_tests.exe`、`CineRTX.exe` 全绿编过；RTX 5060 构建产出四桶单 Profile 引擎（`ultra`/`wide`/`square`/`tall`）。 |
| **五重静态门禁 & 双模 IAT** | `AuditPrivacy`: `0 violations`（`430` 文件）；`AuditTypes`: `34/35`；`AuditSpdx`: `PASS`（`GPL=114 MIT=39`）；`AuditSloc`: Core `12,385`（`<= 23,000`）；`AuditImports`: GUI `4 DLLs` / HEADLESS `2 DLLs` 全绿。 |
| **单元测试 (`rtx_tests.exe`)** | **`150 test(s) run, 0 failed (69892 ms)`**（含 `rife_four_phase_monotonic_progression`、`playback_session_native_decode_h264_hevc_av1`、`nle_*`、`gui_*`、`p13r4_merge_concat_*` 全绿）。 |
| **尚未实装范围** | `31` 号单中的 C++ 后端写帧落盘（`cinertx_latest_preview.jpg`），以及 `32`（SVT-AV1/x265 实编）、`33`（60fps AI 增强播放器）、`34`（动态 D2D1/DirectWrite 组件库与可拖拽 DAG 画布）全部尚未开始。 |

---

## 二、 协调负责人预检锁定的五大高危嫌疑点（重点提请专家穿透审查）

在静态编过与单测全绿的背后，协调负责人基于只读代码审查已识别出以下 **5 项极可能存在静默失效、逻辑漏洞或崩坏风险的实现**，请专家组逐一深挖裁决是否必须退回工程师返工：

### 嫌疑 1：MP4 音轨兼容性处理存在“静默丢音”重大产品违约风险
- **代码坐标**：`src/io/ffmpeg_io.cpp:59-61, 822-827`
- **现状**：`isMp4DirectCopyAudioCodec()` 仅放行 `AAC / MP3 / OPUS / ALAC`；在 `FfmpegMuxer::open()` 中，遇到其余音轨（如影视常见的 `AC3`、`EAC3`、`DTS`、`FLAC`、`PCM`）时仅记录一条 `warnings_` 并直接 `continue` 跳过，**导致输出 `.mp4` 静默无声**！
- **提请裁决**：这是否违背“不静默丢音、非直通音轨应自动转码为 AAC/合规音频或显式拒止”的架构契约？

### 嫌疑 2：`MergeConcat` 强依赖 `.ffconcat` 解复用器，对异构片源与安全限制存在崩溃/失效隐患
- **代码坐标**：`src/core/job_queue.cpp:242-270`、`src/io/ffmpeg_io.cpp:1099-1114`、`cmake/RtxFfmpegStatic.cmake:390`
- **现状**：`EncoderJobQueue::workerLoop()` 对 `MergeConcat` 仅简单生成一份 `.ffconcat` 清单文件送入单次 `runNvidiaRemasterTranscode()`。
- **风险点**：
  1. FFmpeg `concat` demuxer 默认启用 `safe=1`，对带盘符或绝对路径往往报 `Unsafe file name` 拒绝打开；
  2. 即使打开，当多段输入具备**不同分辨率、不同编码格式（如 H.264 + HEVC）、不同帧率或不同色彩空间**时，单解码器实例 + 单 `RtxNgxBridge` 实例无法无缝切换，将直接解码失败或花屏——与 `19`/`27` 号施工单设计的“三路智能合并（同构直拼 vs 异构 RenderGraph 重编码归一化）”严重不符。

### 嫌疑 3：NVOFA 像素 SAD 切镜检测在 `bridge_exports.cpp` 的内存安全与精度隐患
- **代码坐标**：`src/shim/ngx_bridge/bridge_exports.cpp:707-740`
- **现状**：在 `rtx_ngx_bridge_interpolate_nv12()` 内使用 `rtx_ngx::nv_cuda::memcpyDtoH` 仅抽样 Y 平面 8 行（且假定 `prev_nv12`/`curr_nv12` 必为 8-bit `NV12`、未做前序异步 CUDA Stream 同步即直接 Host 读显存，也未回传 `scene_cut` 给后级重置 VSR 时域历史 `force_reset_temporal` 与 NVENC IDR）。
- **提请裁决**：是否存在异步竞态/未同步读取、局部大运动/字幕跳变误判、以及未重置后级 VSR 仍留残影的问题？

### 嫌疑 4：`1001` 扩展时基与 `hw_transcode_stages.cpp` 兜底常量残留
- **代码坐标**：`include/rtx/flicks.h:17`、`include/rtx/time_model.h:33`、`src/core/hw_transcode_stages.cpp:144`
- **现状**：主时基已统一为 `706,305,600,000 Hz`，但 `hw_transcode_stages.cpp:144` 仍留有旧时代 `60fps` 兜底常量 `11760000LL`（应为 `kFlicksPerSecond / 60ULL = 11,771,760,000`）；同时请专家复核全仓在 `706,305,600,000 Hz` 下 `int64_t` 乘法、`duration_cast` 与长片时长边界是否仍有溢出隐患。

### 嫌疑 5：移除 `is_spatial_upscale` 强制 `fg-first` 后的 4K VSR->RIFE 显存与四桶上界风险
- **代码坐标**：`src/core/hw_transcode_stages.cpp:92-104`、`src/shim/ngx_bridge/rife_engine.h:38`、`src/shim/ngx_bridge/rife_builder.cpp:444-458`
- **现状**：现已允许用户在放大场景（如 1080p -> 4K）选择 `vsr-first`（先 VSR 放大至 4K，再跑 RIFE `scale=0.5f`），请专家核查：
  1. `rife_builder` 四桶 `MAX` 范围与 `rife_engine.cpp:366-404` 对超宽/方形/竖屏 4K 的实际兼容边界；
  2. `1080p -> 4K vsr-first` 在 8GB RTX 5060 上的实际显存占用与吞吐是否安全；
  3. 当前 `Wave 1-A` 修复后的桌面 `CineRTX.exe` 真实转码预览与播放器界面是否还存在未测出的 UI/DComp 断链。

---

## 三、 提请专家组出具的阶段验收裁决事项

请首席领域专家组基于 Commit `9355120` 代码基线开展全面代码审查与风险评估，并落盘《阶段性验收与风险排雷终审意见书》：

1. **阶段性验收判定**：对 `9355120` 给出明确裁决（哪些部分已达标放行、哪些隐藏缺陷必须在接手前打回工程师修正）。
2. **显性错误与潜在风险清单**：逐一审定上述 5 项嫌疑点及专家额外发现的全部 Bug、边界漏洞、性能/并发/内存/许可合规风险（含 `32` 号引入 `--enable-gpl --enable-libx265` 对主仓分发与静态构建链的影响）。
3. **接手 `31` 尾项与 `32`–`34` 号施工单的准入批复**：
   - 核准 `AuditTypes` 仅余 `1` 个名额（`34/35`）下，`32`–`34` 施工单采用零新增类型（`tuple`/`variant`/表驱动自由函数）的可行性与注意事项；
   - 明确排雷清理完毕后，正式批准由协调负责人接手本机实装 `31(尾项) -> 32 -> 33 -> 34` 的执行路线与每波次真机验收标准。

---

**呈报完毕，恭请首席领域专家组展开阶段性全面验收与风险排雷审查。**
