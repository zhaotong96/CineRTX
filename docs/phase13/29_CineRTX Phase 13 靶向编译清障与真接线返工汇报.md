# 29 CineRTX Phase 13 靶向编译清障与真接线返工汇报

- **汇报编号**：Phase 13-29（响应 `23` 号驳回裁决书 `CINERTX-P13-RULING-023` 及 `24`–`28` 号靶向施工单）
- **变更文件数**：9 个（8 个源码/WebUI 文件 + 1 个隐私清零后的 `14_` 施工单；旧版均已外置归档至 `work/_CineRTX_bak_archive/*.p13r4bak`，仓库内零 `.bak` 残留）
- **Git 状态**：严格遵守红线，未执行任何 `git commit` 或 `git push`

---

## 一、P0 三处 MSVC 编译阻断、隐私清零与死代码拔除实证（`文件:L行号`）

### 1.1 3 处 P0 MSVC 编译硬失败全部清障（24 号靶向修复 1）

- [x] **错误 1（`rife_builder.cpp` `error C2039` / `C3861`）**：在顶部显式补全 `#include "rife_engine.h"`，使第 447–450 行的 `rtx_ngx::classifyRifeBucket` 与 `rtx_ngx::rifeBucketProfileDims` 符号完整解析（`src/shim/ngx_bridge/rife_builder.cpp:L12`）。
- [x] **错误 2（`rife_engine.cpp:210` `error C2065: 'is_portrait'`）**：彻底清除第 210 行遗留的 `!is_portrait` 引用，重构为基于四桶枚举的 `aspect_bucket == RifeAspectBucket::Wide` 回落条件，全文件 `is_portrait` 残留为 0（`src/shim/ngx_bridge/rife_engine.cpp:L210`）。
- [x] **错误 3（`hw_transcode_stages.cpp:458` `error C2039: 'scene_cuts_detected'`）**：修正为 `rtx::HwTranscodeReport` 真实成员 `++report_.fruc_scene_cuts;`（`src/core/hw_transcode_stages.cpp:L424` / `L458`）。

### 1.2 `AuditPrivacy` 剩余 1 处违规清零（24 号靶向修复 2）

- [x] **清洗 `14_rework_wave0-A_P0编译修复_1001时基统一与隐私清零施工单.md:L167`**：将第 167 行硬编码 Windows 用户绝对路径替换为 `[REDACTED_PATH]`，正则复验全文件 `0 violations`。

### 1.3 拔除 `[DEC-016]` 违宪 `[[maybe_unused]]` 死代码并完成主链路真接线（24/25/27 号）

- [x] **全面移除 `src/io/ffmpeg_io.cpp` 中全部 3 处 `[[maybe_unused]]` 并真接线**：
  - `isMp4DirectCopyAudioCodec()` 正式接入 `FfmpegMuxer::open()` 的 MP4 音轨直通兼容性校验（不兼容音轨记录告警并安全跳过，防止写头崩溃）（`src/io/ffmpeg_io.cpp:L59`、`src/io/ffmpeg_io.cpp:L822`）。
  - `isMp4TextSubtitleCodec()` 正式接入 `FfmpegDemuxer::open()` 字幕流筛选分支（`src/io/ffmpeg_io.cpp:L501`）。
  - `rtx::buildSafeConcatList()` 提升为公开 API（`include/rtx/io.h:L178`、`src/io/ffmpeg_io.cpp:L1099`），并在 `EncoderJobQueue::workerLoop()` 的 `JobOutputMode::MergeConcat` 分支真实落地：多片段任务自动生成 UTF-8 安全转义的临时 `.ffconcat` 清单送入单次执行链路，结束后自动清理临时清单（零磁盘中间视频大文件）（`src/core/job_queue.cpp:L242`）。
  - 单元测试实证：`p13r4_merge_concat_executes_ffconcat_manifest` 实跑 PASS（5 ms），验证多输入 `MergeConcat` 真实生成 `.ffconcat` 清单并完成交付。
- [x] **补齐 WebUI SDR 预览前后端完整闭环**：
  - `webui/server.py` 新增 `GET /api/v1/stream/latest_frame.jpg` 路由，有缓存帧时返回 `200 image/jpeg`（`Cache-Control: no-store`），空闲时返回 `204 No Content`，并通过 `python3 -m py_compile` 语法检查（`webui/server.py:L1066`）。
  - `webui/app.js` 在 `DOMContentLoaded` / `beforeunload` 生命周期正式接通 `window.rtxPreviewCtrl.startSdrProxyStream(15)` 与 `stopSdrProxyStream()`（`webui/app.js:L2346`）。

---

## 二、宪法门禁自检结果

- [x] **`AuditTypes` 零新增命名类型**：`34 / 35`（严格 0 新增 `struct`/`class`）。
- [x] **`[DEC-016]` 死代码零容忍**：`src/io/ffmpeg_io.cpp` 中 `[[maybe_unused]]` 归零，所有新增辅助函数均已接入真实执行主路。
- [x] **静态 DLL 导入边界**：未新增任何静态 `.lib` 依赖。
- [x] **离线单测实跑**：`p13r4_merge_concat_executes_ffconcat_manifest` PASS（5 ms），`webui/server.py` `py_compile` PASS。

---

## 三、待宿主机真机复验与深水区诚实披露（严格遵守 `[DEC-008]` / `[ISSUE-007]`）

本执行环境为离线 Linux 无 GPU 沙盒，不具备 Windows 11 + MSVC 19.51 + RTX 5060 物理宿主机条件。以下项目如实保持 `[ ]` 未勾选状态，待宿主机真机复核：

- [ ] **Windows 11 + MSVC 19.51 宿主机 `.\build.bat` 全目标编译**：验证 3 处 P0 错误清零后 `rtx_ngx_bridge.dll`、`rtx_core.lib`、`CineRTX.exe`、`rtx_headless_cli.exe`、`rtx_tests.exe` 全部 0 Error 产出。
- [ ] **宿主机 `AuditPrivacy.cmake`、`AuditImports.cmake` 与 `rtx_tests.exe` 全量实跑**：确认 0 violations、GUI `<= 4` / HEADLESS `== 2` DLLs 及全套单元测试 0 FAILED。
- [ ] **25–28 号深水区实装专项**：宿主机 `SvtAv1Enc.lib` / `x265-static.lib` 静态编联实片压测、RTX 5060 四桶 `.engine` 物理构建与 NVOFA 双向光流 CUDA Kernel 实片验证、播放器实时 60fps VSR+TrueHDR+FRUC 管线全接入，以及动态加载 D2D1/DirectWrite 交互式横向可拖拽 DAG 卡片画布。
