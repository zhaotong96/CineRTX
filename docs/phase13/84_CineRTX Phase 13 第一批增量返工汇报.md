# 84_CineRTX Phase 13 第一批增量返工汇报（83_ 补丁 + 78_ 类型清退 + 79_ 闭环）

- **执行基线**：Commit `df6ae0b`（`158/158 PASS`，`AuditPrivacy=0/476`，`AuditSpdx=PASS`，`ContractGuard=PASS`）。
- **防覆写声明**：本批全部 10 个修改文件均直接拉取 `df6ae0b` 最新实时版本后做最小函数级增量补丁，严禁使用旧快照整文件覆盖；`include/rtx/ngx_remaster.h:L90` 的 `consumeNvofSceneCut` 与 `src/core/job_queue.cpp` 的 `isPreviewEnabled()` 均完整保留。
- **编辑方式**：10 个既有文件旧版已重命名移至仓库外归档目录（`.p13r13bak`）并原位替换；源码树内零 `.bak`/`.orig`/`.tmp` 残留，未执行 `git commit`/`git push`。

---

## 一、83_ 第二节微创修复：`bindParentConsoleIfNeeded()` 终端直出与重定向双通

- [x] **同步 Win32 标准句柄与重置 C++ 流状态（`+0` 具名类型）**：
  - `src/gui/rtx_gui.cpp:L1114-L1119` — 未重定向 `stdout` 分支在 `freopen_s("CONOUT$", "w", stdout)` 后，通过 `CreateFileW(L"CONOUT$", ...)` 打开有效控制台句柄并调用 `SetStdHandle(STD_OUTPUT_HANDLE, hConOut)`；已重定向分支保留 `SetStdHandle(STD_OUTPUT_HANDLE, hStdOut)`。
  - `src/gui/rtx_gui.cpp:L1127-L1133` — 未重定向 `stderr` 分支同步执行 `CreateFileW(L"CONOUT$", ...)` 与 `SetStdHandle(STD_ERROR_HANDLE, hConErr)`；已重定向分支保留 `SetStdHandle(STD_ERROR_HANDLE, hStdErr)`。
  - `src/gui/rtx_gui.cpp:L1140-L1141` — 在 `std::ios::sync_with_stdio(true)` 后显式调用 `std::cout.clear()` 与 `std::cerr.clear()`，清除流初始化阶段可能置位的 `badbit`，确保 `CineRTX.exe --headless --probe` 终端直出与管道重定向双通。

---

## 二、78_ Task 3：冗余存量类型物理清退（`AuditTypes` `34/35` -> `33/35`，`< 34`）

- [x] **物理清退冗余 Pimpl 类型 `struct WasapiAudioOutput::Impl`（类型总数净减 1）**：
  - `include/rtx/playback_session.h:L146-L212` — 将原先双层 Pimpl 拆分的 `WasapiAudioOutput::Impl` 状态与私有方法直接内联归位至 `class WasapiAudioOutput`；5 个 COM 接口句柄与 2 个事件句柄以不透明 `void*` 持有并在 `src/core/wasapi_audio_output.cpp` 中统一释放，保持 `playback_session.h` 零 Windows/COM 头文件渗入；同时移除多余 `alignas(64)`（保留显式 56 字节缓存行填充数组 `pad_*`），杜绝 `/W4 /WX` 下的 `C4324` 警告。
  - `src/core/wasapi_audio_output.cpp:L1-L396` — 物理删除 `struct WasapiAudioOutput::Impl` 定义（全文件具名类型数归 `0`），在 `WasapiAudioOutput::~WasapiAudioOutput()`（`L314`）中严格按“先 `worker_.request_stop()` / `join()` -> 再 `releaseComHandles()` -> 最后关闭 `stop_event_`”逆序析构，对外公开接口与 `tests/test_playback_session.cpp` 100% 兼容零改动。
  - **全仓类型统计**：`AuditTypes` 从 `34/35` 真实压降至 **`33/35`（`< 34`）**，本批所有其他功能增量严格保持 `+0` 具名类型。

---

## 三、79_ 余项闭环：MP4 音轨/字幕转码、公共切镜总线与 `preview_generation_` 同步

- [x] **MP4 非直通音轨（`SwrContext` + `AVAudioFifo` -> AAC-LC）与文本字幕（SRT/ASS/SSA/WEBVTT -> `mov_text`）转码闭环（`+0` 具名类型）**：
  - `include/rtx/io.h:L154-L155` — 在 `FfmpegMuxer` 新增纯 `std::tuple` 成员 `audio_transcoders_` 与 `subtitle_transcoders_`（零新增具名类型）。
  - `src/io/ffmpeg_io.cpp:L77-L273` — 以 `using AudioTranscodeState = std::tuple<...>` 与 `using SubtitleTranscodeState = std::tuple<...>` 配合 `std::shared_ptr<void>` 自定义释放器，实现 `createAudioAacTranscoder()`（解码器 + 48kHz/FLTP AAC-LC 编码器 + `SwrContext` + `AVAudioFifo`）、`createSubtitleMovTextTranscoder()`（字幕解码器 + `AV_CODEC_ID_MOV_TEXT` 编码器）及 `encodeAudioFifoFrames()` / `drainEncodedAudioPackets()`。
  - `src/io/ffmpeg_io.cpp:L1034,L1067` — 在 `FfmpegMuxer::open()` 的 MP4 分支中，将非直通兼容音轨（PCM/FLAC/DTS 等，若静态 FFmpeg 含对应解码器）接入 AAC-LC 转码流，将 `SUBRIP`/`ASS`/`SSA`/`WEBVTT` 字幕接入 `mov_text` 转码流并记录样式裁切提示。
  - `src/io/ffmpeg_io.cpp:L1125-L1228` 与 `L1344` — 在 `FfmpegMuxer::writePacket()` 中实现音频解码 -> `swr_convert` -> `AVAudioFifo` -> AAC-LC 编码写包与字幕 `avcodec_decode_subtitle2`（带 RAII `avsubtitle_free` 守卫）-> `avcodec_encode_subtitle` -> `mov_text` 写包；在 `FfmpegMuxer::finalize()`（`L1344`）写入容器尾部前刷空 FIFO 残留采样与编码器延迟帧。
- [x] **无状态公共切镜与黑场总线 `analyze_temporal_boundary()`（`+0` 具名类型）**：
  - `include/rtx/shot_segmenter.h:L38-L49` — 声明 `enum class TemporalBoundaryType` 与无状态自由函数 `analyze_temporal_boundary(const uint8_t* prev_y, const uint8_t* curr_y, int width, int height, int pitch)`。
  - `src/core/shot_segmenter.cpp:L77-L132` — 实现 8x8 分块 SAD + 亮度均值/方差融合判定（`FadeToBlack` / `HardCut` / `HighMotion` / `None`），不恢复已退化的 `ShotGroupSegmenter` 类。
- [x] **预览槽位单调代数计数器 `preview_generation_` 同步**：
  - `include/rtx/ngx_remaster.h:L70,L78,L112` — 在 `NgxRemasterBridge` 增加 `std::atomic<std::uint64_t> preview_generation_{0}`、`previewGeneration()` 与 `latestPreviewGeneration()`，同时完整保留 `consumeNvofSceneCut`（`L90`）。
  - `src/core/ngx_remaster.cpp:L211,L228,L252,L260` — `publishActivePreviewBridge()` 重置代数为 `0`，每次 `publishPreviewSlot()` 单调递增 `preview_generation_.fetch_add(1u, std::memory_order_acq_rel)`（有效首帧从 `1` 开始）。
- [x] **补充 3 项确定性单元测试**：
  - `tests/test_io.cpp:L534` — `io_mp4_pcm_audio_transcode_to_aac`（内存 MP4 封装 `pcm_s16le` 音轨经 `SwrContext` + `AVAudioFifo` 转码为 `AV_CODEC_ID_AAC` 并回读验证）。
  - `tests/test_io.cpp:L614` — `shot_analyze_temporal_boundary_bus`（验证 `None` / `FadeToBlack` / `HardCut` 三态判定）。
  - `tests/test_io.cpp:L630` — `ngx_preview_generation_monotonic_sync`（验证 `previewGeneration()` 从 `0` 单调递增至 `1, 2, 3`）。

---

## 四、静态自检与宿主机验收清单

### 4.1 离线静态自检（本轮全部实测通过）
- [x] **括号/大括号平衡**：全部 10 个修改的 `.h`/`.cpp` 文件均实测 `braces=0, parens=0`。
- [x] **`AuditTypes` 类型净降**：`WasapiAudioOutput::Impl` 物理清退，全仓具名类型由 `34/35` 降至 **`33/35`（`< 34`）**，其余改动新增 `+0`。
- [x] **`ContractGuard` 符号保留**：`include/rtx/ngx_remaster.h:L90` 保留 `consumeNvofSceneCut`；`src/core/job_queue.cpp` 未改动，`isPreviewEnabled()` 双路径完好。
- [x] **`AuditPrivacy` 精确正则扫描**：全部 10 个修改源码文件及本 84 号汇报文档实测 **`0 violations`**。

### 4.2 宿主机统一验收项（离线环境诚实留空 `[ ]`）
- [ ] **FFmpeg 缓存清理重编与 `-DSTRICT_NO_BCRYPT=ON`**：清理 `.vs/build/external/ffmpeg-static` 重编纯 LGPL + `SVT-AV1` 静态 FFmpeg，验证 `AuditImports -DSTRICT_NO_BCRYPT=ON` 达成 **`PASS: 3/3`**（`bcrypt.dll` 归零）。
- [ ] **宿主机 MSVC `/W4 /WX` 与全套单测**：运行 `.\build.bat` 校验 0 Error 0 Warning、`AuditTypes = 33/35`、`rtx_tests.exe` 预期 **`161/161 PASS`**（原 158 + 本批新增 3 项）。
- [ ] **单 EXE 无头直出实测**：验证 `CineRTX.exe --headless --probe` 在终端直出与管道重定向下均完整输出探测报告。
