# CineRTX Phase 11 施工单 Track 11C：Flicks 语义重构与 WASAPI 实时播放内核

- **施工单编号**：`CINERTX-P11-TASK-11C`
- **优先级与依赖**：**P1（强依赖 Track 11A 的五域 RAII 底座合龙后启动）**
- **承接工程师**：媒体运行时与实时音视频同步工程师（Media Runtime & Real-Time Audio/Playback Engineer）
- **母本依据**：[`04_CineRTX Phase 11 冲突评判与方案.md`](./04_CineRTX Phase 11 冲突评判与方案.md) §3（第二、三点）、§4（Track 11C）、§5（Gate 11C-1/2）
- **独占文件边界**：
  - 修改：`include/rtx/flicks.h`、`include/rtx/rational.h`、`src/core/rational.cpp`、`src/io/ffmpeg_io.cpp`、`src/gui/workstation_canvas.h`、`src/gui/workstation_canvas.cpp`
  - 新建：`include/rtx/time_model.h`、`include/rtx/telemetry_bus.h`、`include/rtx/playback_session.h`、`src/core/playback_session.cpp`、`src/core/wasapi_audio_output.cpp`、`src/core/gop_seek_scheduler.cpp`、`tests/test_flicks_semantics.cpp`、`tests/test_playback_session.cpp`
  - **严禁触碰**：`schema/*`、`src/shim/ngx_bridge/*`、`src/gui/preview_viewport.*`（由 11B/11D/11E 独占）

---

## 一、 施工目标与核心架构约束

1. **修复 Flicks 语义混用并彻底消灭 `*_ms` 近似**：
   - 全仓刚性锁定原生 `kFlicksPerSecond = 705,600,000ULL`（`1 Flick ≈ 1.41723356 ns`，`int64_t` 跨度约 414.22 年），严禁任何 `14,112,000 Hz` 降采样；
   - 修复 [`src/io/ffmpeg_io.cpp`](../../src/io/ffmpeg_io.cpp) 将 B 帧乱序绝对 PTS 送入有状态 `FlicksDriftCompensator::rescaleToFlicks()` 的语义漏洞；
   - 将 [`src/gui/workstation_canvas.h/.cpp`](../../src/gui/workstation_canvas.h/.cpp) 中的 `start_ms` / `duration_ms` / `playhead_ms` / `in_ms` / `out_ms` 与写死 24fps 的 `formatFlicksTimecode()` 全面升级为原生 Flicks 与全制式 SMPTE 时间码。
2. **交付独立垂直切片 `MediaPlaybackSession`（先原生解码回放，绝不改造 `RemasterPipeline`）**：
   - 实时播放按呈现截止时间（Deadline）调度，过期视频帧可丢弃，解码环必须有界，**音频硬件时钟绝不允许阻塞等待 VSR 推理**；
   - 必须落地事件驱动 WASAPI 共享模式 `IAudioClient` / `IAudioClock` 主时钟、热拔插自动重连、GOP-Aware 帧精确 Seek 以及 `alignas(64)` 无锁原子遥测总线。

---

## 二、 原子 TDD 施工任务（Atomic Tasks）

### Task 11C-1：建立严谨时间模型（`include/rtx/time_model.h`）与分离 Flicks 换算语义
1. 在 `include/rtx/time_model.h` 中定义 6 个正交值类型：
   ```cpp
   // include/rtx/time_model.h (SPDX: GPL-3.0-or-later WITH Plugin-Exception)
   #pragma once
   #include <cstdint>
   #include <string>
   #include "rtx/flicks.h"
   #include "rtx/rational.h"

   namespace rtx {

   struct SourceTimestamp {
       int64_t pts = 0;
       int64_t dts = 0;
       Rational time_base{1, 90000};
   };

   struct TimelinePosition { int64_t flicks = 0; };
   struct AudioSamplePosition { int64_t sample_index = 0; uint32_t sample_rate = 48000; };

   enum class TimecodeDropMode : uint8_t { NonDropFrame = 0, DropFrame = 1 };
   struct TimecodeFormat {
       Rational frame_rate{24, 1};
       TimecodeDropMode drop_mode = TimecodeDropMode::NonDropFrame;
   };

   // 1. 无状态绝对时间戳换算：安全处理 B 帧解码乱序、Seek 跳变与负 PTS，保留原始 SourceTimestamp
   [[nodiscard]] int64_t absoluteTimestampToFlicks(int64_t pts, Rational time_base) noexcept;

   // 2. 有状态增量余数结转器：仅用于恒定步长增量累加（如音频采样/恒定帧率合成步进）
   class IncrementalFlicksAccumulator {
   public:
       explicit IncrementalFlicksAccumulator(Rational step_seconds) noexcept;
       void reset(int64_t base_flicks = 0) noexcept;
       [[nodiscard]] int64_t advanceOneStep() noexcept; // 128-bit Bresenham 结转
   private:
       Rational step_;
       int64_t current_flicks_ = 0;
       int64_t remainder_ = 0;
   };

   // 全制式 SMPTE Timecode（23.976 / 24 / 25 / 29.97 DF&NDF / 30 / 50 / 59.94 / 60 fps）
   [[nodiscard]] std::string formatSmpteTimecode(int64_t flicks, const TimecodeFormat& fmt);

   } // namespace rtx
   ```
2. 修改 `src/io/ffmpeg_io.cpp`：对解复用器产出的任意绝对 PTS/DTS 改调 `absoluteTimestampToFlicks()`，并同步在 `Packet` 中保留原始 `SourceTimestamp`。
3. 迁移 `src/gui/workstation_canvas.h/.cpp`：将 `NleClip` / `NleTimeline` 核心时间字段改为 `int64_t *_flicks`，坐标映射升级为 `timelineXToFlicks()` / `timelineFlicksToX()`，时间码格式化切至 `formatSmpteTimecode()`。

### Task 11C-2：无锁原子遥测总线（`include/rtx/telemetry_bus.h`）
- 定义 `alignas(64) struct TelemetrySnapshot`（包含 `frames_decoded`, `frames_rendered`, `frames_dropped`, `current_fps_x100`, `av_drift_us`, `vram_used_bytes`, `vram_budget_bytes`, `playhead_flicks`, `sequence_epoch`）；
- 实现 `AtomicTelemetryBus`：工作线程通过 `std::memory_order_release` 无阻塞发布（seqlock 或双缓冲原子槽），GUI 主线程以 `30 Hz SetTimer` 通过 `std::memory_order_acquire` 读取，彻底杜绝高频 `PostMessageW` 塞满 Win32 消息队列。

### Task 11C-3：事件驱动 WASAPI 音频主时钟与热拔插自愈（`src/core/wasapi_audio_output.cpp`）
1. 严格遵守 Track 11A RAII：使用 `static Result<std::unique_ptr<WasapiAudioOutput>> create(...)` 工厂与 `ComOwner<T>` / `UniqueHandle`，零 `initialize()/shutdown()`。
2. 共享模式 + 事件驱动 `IAudioClient` 输出 32-bit Float PCM，通过 [`IAudioClock::GetPosition()`](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition) 与 `GetFrequency()` 配合 QPC 时间戳外推换算为原生 Flicks 主时钟；无音轨素材自动退化至 `std::chrono::steady_clock` 主时钟。
3. **实时音频回调线程 4 条绝对红线**：零文件 I/O、零动态堆分配（`new`/`malloc`/`std::vector` 扩容）、零长锁等待、零同步 GPU Fence Wait。
4. 捕获 `AUDCLNT_E_DEVICE_INVALIDATED`：触发非阻塞重连状态机，经 `IMMDeviceEnumerator::GetDefaultAudioEndpoint` 自动重新打开新默认设备并平滑续播。

### Task 11C-4：GOP-Aware 帧精确 Seek 调度器（`src/core/gop_seek_scheduler.cpp`）
参照 [NVIDIA Video Codec SDK 13.1 GOP-Aware Seek 范式](https://developer.nvidia.com/blog/nvidia-video-codec-sdk-13-1-zero-copy-transcode-av1-b-frames-and-frame-accurate-seek/)，在现有 `IDemuxer` + NVDEC 之上实现独立算法组件（不依赖 R610 驱动）：
1. 由 `IDemuxer` 索引定位至目标 `target_flicks` 之前最近的 IDR 关键帧（兼容 Open GOP）；
2. Seek 解复用器并向 NVDEC 下发 Flush / Discontinuity；
3. 解码参考帧以恢复完整参考状态；
4. 对非目标中间帧**跳过**高开销显存映射与后处理；
5. 命中目标帧（`frame_flicks >= target_flicks`）后执行映射与呈现，支撑拖动进度条与 `,` / `.` 逐帧步进。

### Task 11C-5：组装独立实时播放会话 `MediaPlaybackSession`（`include/rtx/playback_session.h` & `src/core/playback_session.cpp`）
- 内部明确分层：`DemuxScheduler` $\rightarrow$ `DecodeAheadQueue`（有界队列，初始 4–6 帧，可按帧间隔/解码耗时/VRAM 预算/倍速 `0.25x–2x` 动态调整，每槽位绑定 GPU Fence 退役值） $\rightarrow$ `PlaybackClock`（WASAPI 主时钟） $\rightarrow$ `FrameDeadlineScheduler`（过期帧丢弃策略） $\rightarrow$ `IVideoFrameSink`（解耦视口渲染，11C 阶段可接现有 `PreviewViewport` 或无头验证 Sink，11D 无缝切至 DirectComposition）。
- 编写 `tests/test_flicks_semantics.cpp` 与 `tests/test_playback_session.cpp`。

---

## 三、 量化验收门禁 Checklist（Gate 11C-1 & Gate 11C-2）

- [ ] **静态扫描**：
  - `rg "14112000|14,112,000" include/ src/` 返回 0 匹配；
  - `rg "start_ms|duration_ms|playhead_ms|in_ms|out_ms" src/gui/workstation_canvas.*` 核心时间模型字段为 0 残留；
  - 静态审查 `wasapi_audio_output.cpp` 音频回调路径确认零文件 I/O、零 `new/malloc`、零 GPU Fence 等待。
- [ ] **Gate 11C-1（Flicks 精度与乱序门禁）**：
  - `test_flicks_semantics` 覆盖 `23.976 / 29.97 DF&NDF / 59.94 fps`、`44.1 / 48 / 96 / 192 kHz`、非整除任意 `time_base`（如 `1/1000`, `1/90000`, `1/15360`）、VFR、B 帧重排 PTS、Seek 跳变与负 PTS，100% PASS。
- [ ] **Gate 11C-2（WASAPI 实时播放内核门禁）**：
  - 本地 H.264 / HEVC / AV1 素材原生解码回放，`IAudioClock` 主时钟稳定跟随；
  - 暂停、倍速（`0.25x–2x`）、GOP-Aware Seek 与 `,`/`.` 逐帧步进无死锁；
  - 模拟或物理拔插音频设备触发 `AUDCLNT_E_DEVICE_INVALIDATED` 后自动重连恢复；
  - `AuditSpdx.cmake`、`AuditImports.cmake` 与全量 `rtx_tests.exe` 100% PASS。
