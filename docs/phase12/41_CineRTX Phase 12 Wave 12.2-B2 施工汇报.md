# CineRTX Phase 12 Wave 12.2-B2 施工汇报

- **文档编号**：`CINERTX-P12-W12.2-B2-REPORT-041`
- **对应作战令**：`docs/phase12/40_CineRTX Phase 12 Wave 12.2-B1 终审封板裁决书暨Wave12.2-B2第二战场作战令.md`（`CINERTX-P12-FINAL-SEAL-040`）
- **汇报日期**：2026-11-23
- **执行范围**：`cmake/AuditTypes.cmake` 正则修复 + 战场 B2-1（I/O 虚壳与编码器配置清剿）+ 战场 B2-2（播放会话伪并发结构与回调虚壳清剿）+ 战场 B2-3（全仓被动数据包清剿）+ 战场 B2-4（`std::get<` 与 `<sstream>` 全仓清零）

---

## 1. Gate 12.2-B2 门禁验收总览

| 门禁项 | Wave 12.2-B1 封板基线 | Wave 12.2-B2 实测值 | 门禁要求 | 状态 |
| :--- | :---: | :---: | :---: | :---: |
| 全仓命名类型总量（`AuditTypes`） | 104 | **73**（净削减 **-31**） | `<= 75`（净削减 `>= 29`） | **PASS** |
| 非法分类 `Polymorphic-Boundary` | 4 | **0** | `= 0` | **PASS** |
| 非法分类 `Concurrency-Invariant` | 1 | **0** | `= 0` | **PASS** |
| 全仓 `std::get<` 裸索引访问 | 75 | **0** | `= 0` | **PASS** |
| 全仓 `#include <sstream>` | 2 | **0** | `= 0` | **PASS** |
| 全仓 `snprintf`/`_snwprintf`/`ostringstream` | 0 | **0** | `= 0` | **PASS** |
| `AuditSpdx` 缺失计数 | 0 | **0** | `= 0` | **PASS** |
| `cmake/AuditTypes.cmake` L110 `\bfinal\b` 修复 | 未修（TRE 不支持 `\b`） | **已修复** | CMake TRE 兼容正则 | **PASS** |
| `CineRTX/` 内 `.bak` 残留 | 0 | **0**（45 份旧文件已迁至外部归档目录） | `= 0` | **PASS** |
| 全仓 Pure SLOC（`AuditSloc`） | 30,950 | **30,356**（净减 **-594**） | `<= 26,500`（严禁假压缩） | **诚实披露（见 §4）** |
| Linux GCC 12 实编（11 个可移植修改 TU） | — | **11/11 PASS（0E 0W）** | 零告警 | **PASS（详见 §5）** |
| Windows MSVC `/std:c++latest /W4 /WX` 全量 85 目标 & `rtx_tests.exe` | 85/85 + 147/147 | **待 Windows 宿主机终审复测** | 0E 0W + 全测绿灯 | **待宿主机验证** |

---

## 2. 逐项施工明细

### 2.1 `cmake/AuditTypes.cmake` 正则缺陷修复（作战令 §1.2）
- 将 `cmake/AuditTypes.cmake` 第 110 行 CMake TRE 不支持的 `\bfinal\b` 替换为严格兼容写法：
  ```cmake
  string(REGEX REPLACE "(^|[ \t])final([ \t]+|:[ \t]*|\\{|$)" "\\1\\2" _clean "${_line}")
  ```
- 已通过本地 Python 镜像与逐行正则核验，确保 `class FfmpegDemuxer final` / `class FfmpegMuxer final` 均被正确识别为对应具名类型而非 `final`。

### 2.2 战场 B2-1：I/O 虚壳与编码器包装清剿（-6 类型）
1. **`include/rtx/io.h` & `src/io/ffmpeg_io.cpp` & `src/io/memory_avio.h`（-4 类型）**：
   - 物理删除单实现虚基类 `IDemuxer`、`IMuxer`、死代码结构体 `DecodedVideoFrame` 与无意义传输壳 `PassthroughTarget`。
   - 直接在 `include/rtx/io.h` 暴露具体类 `class FfmpegDemuxer final` 与 `class FfmpegMuxer final`，保留 `makeFfmpegDemuxer()` / `makeFfmpegMuxer()` 工厂函数并更新返回类型为 `std::unique_ptr<FfmpegDemuxer>` / `std::unique_ptr<FfmpegMuxer>`。
   - 清理 `src/io/ffmpeg_io.cpp` 中未使用的 `#include <sstream>`，同步更新 `MemoryAvioContext` 合法性注释与 `tests/test_io.cpp`。
2. **`include/rtx/cpu_video_encoder.h` & `src/io/cpu_video_encoder.cpp`（-2 类型）**：
   - 物理删除被动配置包 `CpuEncoderConfig` 与多余 PIMPL `CpuVideoEncoder::Impl`，将 `CpuVideoEncoder` 归类为 `NonTrivial-StateMachine`，扁平化工厂签名 `CpuVideoEncoder::create(codec, width, height, frame_rate, crf, preset, threads, hdr)`。

### 2.3 战场 B2-2：播放会话与无头接收器虚壳/伪并发结构清剿（-4 类型）
1. **`include/rtx/playback_session.h` & `src/core/playback_session_impl.h` & `src/core/playback_session*.cpp` & `src/core/gop_seek_scheduler.cpp`（-3 类型）**：
   - 物理删除非法分类 `Concurrency-Invariant` 的 `MediaPlaybackSession::Impl::Control` 嵌套结构，将同步原语与状态位直接内联于 `MediaPlaybackSession::Impl`。
   - 物理删除虚基类 `IVideoFrameSink` 与 `IAudioMasterClock`，替换为标准库函数组合别名：
     - `using VideoFrameSink = std::pair<std::function<Result<uint64_t>(const Frame&)>, std::function<uint64_t()>>;`
     - `using AudioMasterClockFn = std::function<std::optional<int64_t>()>;`
   - 将 `src/core/playback_session_video.cpp` 中全部 10 处 `std::get<0/1/2>` 替换为结构化绑定。
2. **`include/rtx/ngx_remaster.h` & `src/core/ngx_remaster.cpp`（-1 类型）**：
   - 物理删除 `HeadlessPlaybackSink`（`Polymorphic-Boundary`），改为返回携带 `std::shared_ptr` 状态的 `std::optional<VideoFrameSink>` 闭包。
3. 同步更新 `src/gui/workstation_canvas.h/.cpp` 与 `tests/test_playback_session.cpp`。

### 2.4 战场 B2-3：全仓被动数据包清剿（-21 类型）

| 模块 | 物理拔除的命名类型（共 21 个） | 现代 C++23 替代方案 |
| :--- | :--- | :--- |
| `src/io/bitstream.h/.cpp` | `BitReader`, `HevcSpsInfo`, `HevcVuiColor`, `HevcSeiHdr10`（-4） | 自由函数游标参数 + `std::tuple` 别名与结构化绑定 |
| `src/core/hw_transcode_internal.h/.cpp` & `hw_transcode_stages.cpp` | `CudaDriverApi`, `DecoderDeleter`, `EncoderDeleter`, `BridgeDeleter`, `PendingEncodeItem`（-5） | `namespace cuda_drv` 自由函数；`std::unique_ptr<T, std::function<void(T*)>>`；`PendingEncodeItem` 改为 `std::tuple` 别名 |
| `include/rtx/hdr.h` & `src/core/sei_synthesizer.cpp` | `Hdr10PlusBitWriter`, `TrueHdrParameters`, `HevcVuiParameters`, `NvencHevcHdrSettings`, `Hdr10PlusFrameStats`（-5） | 局部 lambda 位写入器 + `std::tuple`/`std::array` 别名与 `defaultTrueHdrParameters()` / `SeiSynthesizer::validateTrueHdr()` |
| `include/rtx/shot_segmenter.h` & `src/core/shot_segmenter.cpp` | `ShotTransitionDecision`, `FrameDiffMetrics`（-2） | `std::tuple` / `std::array<float, 4>` 别名与结构化绑定 |
| `include/rtx/geometry.h` & `src/core/geometry.cpp` | `GeometryResult`（-1） | 9 元 `std::tuple` 别名与结构化绑定 |
| `include/rtx/time_model.h` | `TimecodeFormat`（-1） | `std::pair<Rational, TimecodeDropMode>` + `kDefaultTimecodeFormat` |
| `src/gui/workstation_canvas.h/.cpp` & `src/gui/rtx_gui.cpp` | `DynamicVsrTarget`, `ProbedMedia`（-2） | `std::tuple` 别名与结构化绑定 |
| `src/cli/shim_probe.h/.cpp` & `src/cli/main.cpp` | `ShimProbeResult`（-1） | 6 元 `std::tuple` 别名与结构化绑定 |

> 全仓命名类型净削减：`6 (B2-1) + 4 (B2-2) + 21 (B2-3) = -31`，总量从 **104 压降至 73**（低于 `<= 75` 门禁线 2 个余量）。

### 2.5 战场 B2-4：`std::get<` 与 `<sstream>` 全仓清零
- **`std::get<` 全仓清零（75 -> 0）**：
  - `src/core/playback_session_video.cpp`（10 -> 0）
  - `src/gui/d3d12_raii.h`（2 -> 0）
  - `src/gui/rtx_gui.cpp`（1 -> 0）
  - `src/shim/loader/nv_backend_shim.cpp`（8 -> 0）
  - `src/shim/loader/nv_runtime.cpp`（20 -> 0）
  - `src/shim/loader/nv_decoder.cpp`（34 -> 0）
- **`#include <sstream>` 全仓清零（2 -> 0）**：移除 `src/io/ffmpeg_io.cpp` 与 `src/core/param_schema.cpp` 中冗余的 `<sstream>` 头文件包含。

---

## 3. 修改文件清单（共 45 个文件，全部外部归档替换）

- **CMake（1）**：`cmake/AuditTypes.cmake`
- **Public Headers（8）**：`include/rtx/cpu_video_encoder.h`, `include/rtx/geometry.h`, `include/rtx/hdr.h`, `include/rtx/io.h`, `include/rtx/ngx_remaster.h`, `include/rtx/playback_session.h`, `include/rtx/shot_segmenter.h`, `include/rtx/time_model.h`
- **IO（5）**：`src/io/bitstream.h`, `src/io/bitstream.cpp`, `src/io/cpu_video_encoder.cpp`, `src/io/ffmpeg_io.cpp`, `src/io/memory_avio.h`
- **Core（13）**：`src/core/geometry.cpp`, `src/core/gop_seek_scheduler.cpp`, `src/core/hw_transcode.cpp`, `src/core/hw_transcode_internal.h`, `src/core/hw_transcode_stages.cpp`, `src/core/ngx_remaster.cpp`, `src/core/param_schema.cpp`, `src/core/playback_session.cpp`, `src/core/playback_session_audio.cpp`, `src/core/playback_session_impl.h`, `src/core/playback_session_video.cpp`, `src/core/sei_synthesizer.cpp`, `src/core/shot_segmenter.cpp`
- **CLI（3）**：`src/cli/main.cpp`, `src/cli/shim_probe.h`, `src/cli/shim_probe.cpp`
- **GUI（4）**：`src/gui/d3d12_raii.h`, `src/gui/rtx_gui.cpp`, `src/gui/workstation_canvas.h`, `src/gui/workstation_canvas.cpp`
- **Shim Loader（3）**：`src/shim/loader/nv_backend_shim.cpp`, `src/shim/loader/nv_decoder.cpp`, `src/shim/loader/nv_runtime.cpp`
- **Tests（8）**：`tests/test_geometry.cpp`, `tests/test_gui_workstation.cpp`, `tests/test_hdr.cpp`, `tests/test_io.cpp`, `tests/test_playback_session.cpp`, `tests/test_rife.cpp`, `tests/test_shim.cpp`, `tests/test_types.cpp`

> 全部 45 份旧文件已预先移出 `CineRTX/` 至 `work/_CineRTX_bak_archive/`（后缀 `.b2bak`），随后上传洁净新版本，`CineRTX/` 目录内 `.bak` 残留严格为 0。

---

## 4. Pure SLOC 诚实披露

- 本轮完成 31 个命名类型清剿及样板精简后，全仓 Pure SLOC 由 **30,950 降至 30,356**（净减 **594 行**，Comment 3,872 行，134 文件）。
- 严格遵守作战令红线：**未通过合并单行代码、删除语义注释或破坏可读性来伪造 SLOC 压降**。
- 当前 30,356 行主要由 `src/gui/` Win32 控件布局、FFmpeg/NVDEC/NVENC/NGX 跨语言桥接代码以及 147 项单元测试构成；若后续波次（Wave 12.2-C）需进一步将 Pure SLOC 压向 26,500，需在独立架构评审下对重复的 Win32 控件注册表、测试夹具或 FFmpeg 选项映射表进行数据驱动重构，而非在 Wave 12.2-B2 中强行压缩。

---

## 5. 编译与静态审计边界声明

### 5.1 沙箱 Linux GCC 12 实编通过清单（11/11 PASS，0 Error，0 Warning）
编译参数：`g++ -std=c++23 -Wall -Wextra -Werror -Wno-interference-size`（仅使用 `/tmp/gcc12_shim` 提供最小 `<format>` 头文件垫片，未污染 `repo/`）：
1. `src/io/bitstream.cpp` — **PASS**
2. `src/core/gop_seek_scheduler.cpp` — **PASS**
3. `src/core/ngx_remaster.cpp` — **PASS**
4. `src/core/sei_synthesizer.cpp` — **PASS**
5. `src/core/shot_segmenter.cpp` — **PASS**
6. `src/core/geometry.cpp` — **PASS**
7. `src/cli/shim_probe.cpp` — **PASS**
8. `src/shim/loader/nv_runtime.cpp` — **PASS**
9. `src/shim/loader/nv_backend_shim.cpp` — **PASS**
10. `tests/test_hdr.cpp` — **PASS**
11. `tests/test_flicks_semantics.cpp` — **PASS**

### 5.2 仅静态审计（待 Windows MSVC `/std:c++latest /W4 /WX` 实编复测）
以下文件因依赖 Windows SDK（Win32/D3D12/WASAPI）、FFmpeg/ffnvcodec 头文件或 GCC 12 尚未支持的 C++23 特性（`raii_owners.h` 中 `static operator()`、`nle_project.h` 中 deducing `this`），在 Linux 沙箱中已完成全量交叉引用、签名一致性与零残留字段扫描（`0 stale field accesses`），但**尚未经真实 MSVC 编译与 `rtx_tests.exe` 运行验证**：
- `src/io/ffmpeg_io.cpp`, `src/io/cpu_video_encoder.cpp`
- `src/core/param_schema.cpp`, `src/core/playback_session.cpp`, `src/core/playback_session_video.cpp`, `src/core/playback_session_audio.cpp`, `src/core/hw_transcode.cpp`, `src/core/hw_transcode_stages.cpp`
- `src/cli/main.cpp`
- `src/gui/rtx_gui.cpp`, `src/gui/workstation_canvas.cpp`
- `src/shim/loader/nv_decoder.cpp`
- `tests/test_geometry.cpp`, `tests/test_gui_workstation.cpp`, `tests/test_io.cpp`, `tests/test_playback_session.cpp`, `tests/test_rife.cpp`, `tests/test_shim.cpp`, `tests/test_types.cpp`

---

## 6. 建议 Windows 宿主机终审复测命令

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake -DROOT_DIR=. -P cmake/AuditTypes.cmake
cmake -DROOT_DIR=. -P cmake/AuditSloc.cmake
cmake -DROOT_DIR=. -P cmake/AuditSpdx.cmake
```
