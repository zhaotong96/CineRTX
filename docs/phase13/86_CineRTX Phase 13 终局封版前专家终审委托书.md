# 86_CineRTX Phase 13 终局封版前专家终审委托书

- **委托方**：CineRTX Phase 13 协调负责人
- **受托方**：CineRTX 领域专家评审组
- **当前本地基线**：`a1832bc`（`feat(phase13): accept batch 2 wave C-D (Static IAT 2/2, 60fps sync, DAG, ffmpeg bat fix)`）
- **演进链路**：`df6ae0b`（81 号里程碑）→ `84006c2`（批 1 / 84 号 + `+faststart` 保护）→ `a1832bc`（批 2 / 85 号 Wave C-D + 协调者 3 处编译修复 + `RtxFfmpegStatic.cmake` 批处理语法根治）

---

## 1. 截至 `a1832bc` 已完成并实测通过的内容

### 1.1 批 1（84 号，已封存于 `84006c2`）
- **`83_` 控制台 stdout 修复**：`src/gui/rtx_gui.cpp` `bindParentConsoleIfNeeded()` 补充 `SetStdHandle` 与 `std::cout.clear()` / `std::cerr.clear()`，实测 `CineRTX.exe --headless --probe` 管道输出完整 GPU / NGX 探测信息。
- **`78_` 冗余类型清理**：内联移除 `WasapiAudioOutput::Impl`，`AuditTypes` 从 `34/35` 降至 `33/35`。
- **`79_` 遗留项补全**：补齐 MP4 非透传音频（`SwrContext` + `AVAudioFifo` -> AAC-LC）与字幕（`mov_text`）转码、`analyze_temporal_boundary()`、`preview_generation_` 及 3 项单测；协调者同步在 `src/io/ffmpeg_io.cpp` 为 `+faststart` 补充 `(format_->flags & AVFMT_FLAG_CUSTOM_IO) == 0` 保护，彻底修复内存流转码失败。

### 1.2 批 2（85 号 Wave C-D，已封存于 `a1832bc`）
- **Task C-1 / C-2**：`src/core/hw_transcode_stages.cpp` 增加 RIFE NV12 偶数几何硬守卫；`include/rtx/playback_session.h`、`src/core/playback_session_impl.h`、`src/core/playback_session_video.cpp` 落地 `<chrono>` 编译期有理数 `23.976 -> 60.000` 相位计算、`PlaybackSyncState`（8ms `AudioDrift` / 32ms `MeltdownBypass`）及 Seek/单步时域状态重置（`+0` 具名结构体）。
- **Task D-1 / D-2 / D-3**：
  - `include/rtx/sys_dynload.h`、`include/rtx/raii_owners.h`、`src/gui/rtx_gui.cpp`、`src/gui/workstation_canvas.h/.cpp`、`src/gui/preview_viewport.cpp` 与 `CMakeLists.txt` 完成 `user32.dll` / `gdi32.dll` 全量按需动态载入与 `<windowsx.h>` 宏剥离，`CineRTX.exe` 静态导入表已彻底消除 `gdi32.dll` 与 `user32.dll`。
  - `include/rtx/i18n.h`、`src/core/i18n.cpp` 新增单源 `std::vformat` 与语言切换 `g_relayout_required` 脏标；`src/gui/workstation_canvas.h` 以无状态函数与枚举落地 `hitTestRect()` 及 `validateDagEdge()`（`HDR BT.2020 -> SDR BT.709` 自动标记 `Auto ToneMap`），零 `UiWidgetNode` 膨胀。
- **协调者同步修复**：
  1. `src/core/i18n.cpp`：`setLanguage()` 补全缺失右花括号（解决 `C2601`）。
  2. `include/rtx/playback_session.h` 等 4 文件：新枚举 `PlaybackClockState` 重命名为 `PlaybackSyncState`（解决与第 46 行既有 `using PlaybackClockState` 重名冲突 `C2371`）。
  3. `tests/test_raii_retirement.cpp`：调整 `d3d12_raii.h` 至 `rtx/raii_owners.h` 之前，保证 `__d3d12_h__` 宏先于 `sys_dynload.h` 展开。
  4. `cmake/RtxFfmpegStatic.cmake`：根治历史多轮复发的 `build_ffmpeg_static.bat` `not was unexpected at this time.`（退出码 `255`）——移除 `if not defined USE_MSVC (...)` 块内 `echo ... (MSVC x64) not found.` 的裸括号并将 `vcvars64.bat` 移至 `setlocal enabledelayedexpansion` 之前；已在洁净 `cmd.exe`（无 `cl.exe` 环境）实测退出码 `0`。

---

## 2. 宿主机实测门禁汇总（`a1832bc` 对应构建）

| 门禁 / 检查项 | 实测结果 | 备注 |
| :--- | :--- | :--- |
| `AuditPrivacy` | **PASS** | `0 violations across 480 file(s)` |
| `AuditSpdx` | **PASS** | `GPL=115 MIT=39` |
| `ContractGuard` | **PASS** | `consumeNvofSceneCut`, `isPreviewEnabled` 完整保留 |
| `AuditTypes` | **PASS** | `33/35 named types, all uniquely justified` |
| `AuditSloc` | **PASS** | Core Engine `13760`（阈值 `<= 23000`），Full Repo `34220` |
| `MSVC /W4 /WX` | **PASS** | `.\build.bat` 0 Error、0 Warning |
| `AuditImports` | **PASS (2/2 过渡)** | `[bcrypt.dll, kernel32.dll]`；`gdi32.dll` / `user32.dll` 已清零，仅余旧缓存 FFmpeg 的 `bcrypt.dll` |
| `--headless --probe` | **PASS** | 管道输出完整 GPU (`RTX 5060`) 与 `rtx_ngx_bridge.dll` 探测信息 |
| `rtx_tests.exe`（全量串行） | **164/165 PASS** | 耗时 `68332 ms`；批 2 新增 4 项单测全过；仅 `rife_four_phase_monotonic_progression` 失败（`tests/test_rife.cpp:444`） |
| `rife_four_phase_monotonic_progression`（独立） | **1/1 PASS** | 耗时 `26976 ms`，单独运行稳定通过 |
| `build_ffmpeg_static.bat`（洁净 `cmd.exe`） | **PASS** | 退出码 `0`，自动完成 `vcvars64.bat` 初始化并定位 `cl.exe` 与 `bash.exe` |

---

## 3. 终局封版前请专家裁决的 3 项确认点

1. **FFmpeg 缓存清理重建与 `Static IAT == 1/1` 收口**：
   - 目前仅余旧缓存 FFmpeg 带来的过渡性 `bcrypt.dll`。我们计划在宿主机清理 `.vs/build/external/ffmpeg-static` 后重新触发 `RtxFfmpegStatic.cmake` 全量编译（同步激活 `HAVE_BCRYPT=0` 与 `SvtAv1Enc`），并使用 `-DSTRICT_NO_BCRYPT=ON` 将 `AuditImports` 实测收口至 `1/1 (kernel32.dll)`。请专家确认该步通过后是否即满足静态导入表终局要求。
2. **`rife_four_phase_monotonic_progression` 串行偶发波动裁决**：
   - 该用例在全量串行中偶发失败（`tests/test_rife.cpp:444`），但独立运行 `1/1 PASS`（`26976 ms`），且批 1 / 批 2 均未改动 `rife_engine.cpp`。请专家裁决：封版时是以「全量串行 + 失败项独立复测通过」放行，还是要求在本轮封版前根治 `test_rife.cpp` 前置用例遗留的 GPU 默认流同步问题？
3. **实机性能与长稳压测边界**：
   - `85_` §4 提及的 RTX 5060 Nsight Systems 60fps 帧预算（`< 12.5 ms`）与 2 小时 Soak 压测尚未在自动化流水线执行。请专家明确其属于「Phase 13 代码封版前置硬门禁」，还是「封版后真机验收阶段」执行项。
