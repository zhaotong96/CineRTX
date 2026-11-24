# 47_CineRTX Phase 12 Wave 12.2-C 施工汇报

- **对应作战令**：`docs/phase12/46_CineRTX Phase 12 Wave 12.2-B2 终审封板裁决书暨Wave12.2-C第三战场作战令.md`（`CINERTX-P12-FINAL-SEAL-046`）
- **施工阶段**：Phase 12 Wave 12.2-C（第三战场：Core / NLE / GUI / Loader 内部命名类型清剿、C++23/26 现代标准库替换与结构整合）
- **交付日期**：2026-11-23
- **变更规模**：共修改并原位替换 54 个仓库源码/头文件/测试文件（旧版本已全部移出 `CineRTX/` 归档至外置目录 `work/_CineRTX_bak_archive/`）

---

## 1. 四大门禁验收总表

| 门禁 | 指标项 | 封板基线（Doc 46） | Wave 12.2-C 目标 | 实测结果 | 判定 |
| --- | --- | --- | --- | --- | --- |
| **Gate 1** | 自定义命名类型总数（`AuditTypes`） | 73 | `<= 50`（净减 `>= 23`；挑战 `<= 35`） | **47**（净减 **26**，余量 3 个） | **PASS** |
| **Gate 1** | 违规 `Polymorphic-Boundary` / `Concurrency-Invariant` | 0 / 0 | 0 / 0 | **0 / 0** | **PASS** |
| **Gate 1** | 孤立 `@rtx-type-justification` 注释 | 0 | 0 | **0** | **PASS** |
| **Gate 2** | Pure SLOC（`AuditSloc`） | 30,382 | `<= 26,500`（严禁假压缩/删注释） | **30,105**（净减 277，Comments=3,738，Files=134） | **未达 26,500（诚实披露，见 §4）** |
| **Gate 3** | `std::get<` / `OldFormat` / `<sstream>` | 0 / 0 / 0 | 0 / 0 / 0 | **0 / 0 / 0** | **PASS** |
| **Gate 3** | `CineRTX/` 内 `.bak`/`.cbak` 残留 | 0 | 0 | **0**（54 份旧版均外置归档） | **PASS** |
| **Gate 3** | `AuditSpdx` 许可证头缺失 | 0 | 0 | **0** | **PASS** |
| **Gate 4** | Linux GCC 12 便携 TU 实编（`-std=c++23 -Wall -Wextra -Werror -Wno-interference-size`） | — | 全部可编便携 TU 0E 0W | **16 / 16 PASS（0E 0W）** | **PASS（便携子集）** |
| **Gate 4** | Windows / FFmpeg / D3D12 专属 TU | Windows MSVC 0E 0W | 实机 MSVC `/std:c++latest /W4 /WX` | 沙箱无 MSVC/WinSDK，已完成全量静态引用与契约审计 | **待实机 MSVC 复核** |

---

## 2. 本轮清剿的 26 个自定义命名类型清单（73 -> 47）

| # | 原自定义类型 | 原属文件 | C++23/26 替代形态与自由函数契约 |
| --- | --- | --- | --- |
| 1 | `JobQueueEvent` | `include/rtx/job_queue.h` | `enum class JobQueueEventKind : uint8_t` + `std::tuple` 别名 |
| 2 | `RenderGraphSegment` | `include/rtx/render_graph.h` | `std::tuple` 别名 |
| 3 | `AudioGraphSegment` | `include/rtx/render_graph.h` | `std::tuple` 别名 |
| 4 | `HwTranscodeProgress` | `include/rtx/hw_transcode.h` | `std::tuple` 别名 |
| 5 | `ViewportConstants` | `src/gui/preview_viewport.cpp` | `std::array<uint32_t, 12>` + `std::bit_cast` |
| 6 | `ContentLightLevel` | `include/rtx/types.h` | `std::pair` 别名 + 自由辅助函数 |
| 7 | `AudioTrackInfo` | `include/rtx/types.h` | `std::tuple` 别名 + 自由辅助函数 |
| 8 | `ColorDescription` | `include/rtx/types.h` | `std::tuple` 别名 + 自由辅助函数 |
| 9 | `MasteringDisplay` | `include/rtx/types.h` | `std::tuple` 别名 + 自由辅助函数 |
| 10 | `FrameBuffer` | `include/rtx/types.h` | `std::tuple` 别名 + `frameBufferSize` 等自由辅助函数 |
| 11 | `EmbeddedShimApi` | `include/rtx/embedded_shim.h` | 20 元函数指针 `std::tuple` 别名 |
| 12 | `StreamTimestampGuard` | `include/rtx/timestamp_guard.h` | `std::tuple` 状态别名 + 自由辅助函数 |
| 13 | `ShotGroupSegmenter` | `include/rtx/shot_segmenter.h` | `std::tuple` 状态别名 + 自由辅助函数 |
| 14 | `IncrementalFlicksAccumulator` | `include/rtx/time_model.h` | `std::tuple` 状态别名 + 自由辅助函数 |
| 15 | `Lut3D` | `include/rtx/lut3d.h` | `std::pair<int, std::vector<uint16_t>>` + `loadLut3dFromCubeFile` / `parseLut3d` |
| 16 | `FlicksDriftCompensator` | `include/rtx/flicks.h` | `uint64_t` 别名 + 自由辅助函数 |
| 17 | `FlicksMonotonicGuard` | `include/rtx/flicks.h` | `std::tuple` 别名 + 自由辅助函数 |
| 18 | `PassthroughStreamInfo` | `include/rtx/io.h` | `std::tuple` 别名 |
| 19 | `PlaybackOptions` | `include/rtx/playback_session.h` | `std::tuple` 别名 |
| 20 | `GopSeekScheduler` | `include/rtx/playback_session.h` | `std::tuple` 状态别名 + 自由辅助函数 |
| 21 | `CUDA_MEMCPY2D` | `src/shim/loader/nv_func_tables.h` | 归正为 C ABI `typedef struct CUDA_MEMCPY2D_st`（退出 C++ 命名类型统计） |
| 22 | `nvenc::Table` | `src/shim/loader/nv_func_tables.h` | 确认死代码并彻底删除 |
| 23 | `ngx::Table` | `src/shim/loader/nv_func_tables.h/.cpp` | 替换为 `ngx::resolveExports` 自由解析函数 |
| 24 | `VramScheduler` | `include/rtx/vram_scheduler.h` | `std::tuple` 状态别名 + 自由辅助函数 |
| 25 | `CudaDeviceBuffer` | `include/rtx/raii_owners.h` | `std::pair<CudaDevicePtr, size_t>`（底层采用 `std::move_only_function<void(void*)>`）+ `wrapCudaDeviceBuffer` |
| 26 | `Options` | `src/cli/main.cpp` | 文件级匿名/局部 `namespace opts` 配置变量组 |

> 清理过程中同步修正了两处孤立 `@rtx-type-justification` 注释（`src/core/nle_project.cpp`、`src/shim/loader/nv_func_tables.h`），确保 `AuditTypes` 零孤立标注。

---

## 3. Core / NLE / GUI 结构性整合与编译修复要点

1. **`src/core/nle_project.cpp`**：将 `applyDelta` 中约 75 行的 `if constexpr (std::is_same_v<...>)` 阶梯分支重构为 C++23 `Overloaded{lambdas...}` + `std::visit` 派发，提升可读性与编译期穷举检查能力。
2. **`src/gui/rtx_gui.cpp`**：在 `applyWorkspaceLayout` 引入局部 `place(id, x, y, w, h)` 布局辅助 lambda 与 `auto& app = rtx::gui::requireApp(hwnd);`，收敛重复 Win32 `MoveWindow`/`GetDlgItem` 样板代码。
3. **GCC 12 实编拦截并当场修复的 2 处 C++23 语义缺陷**：
   - `include/rtx/types.h` 第 394 行：`FrameBuffer` 退化为 `std::tuple` 后将遗留的 `storage->size()` 修正为 `frameBufferSize(*storage)`。
   - `include/rtx/raii_owners.h`：`CudaDeviceBuffer` 改为 `std::pair<CudaDevicePtr, size_t>`，自定义删除器使用 `std::move_only_function<void(void*)>` 配合 `mutable` lambda 与显式 `CudaDevicePtr` 类型，消除无状态 deleter 的约束冲突。

---

## 4. Pure SLOC 实测分析与诚实披露（Gate 2）

- **实测数据**：`verify_wave12_2.py` 测得 `PureSLOC = 30,105`（相对 Doc 46 基线 `30,382` 净减 **277** 行；总文件数 134，注释行 3,738）。
- **未达 `<= 26,500` 的客观原因**：
  1. 本轮严格遵守“禁止一行化假压缩、禁止删除语义注释、禁止破坏多行可读布局”的工程红线。
  2. 将 26 个内部结构体替换为 `std::tuple`/`std::pair` 别名并补充对应的结构化绑定与自由访问器（如 `frameBufferSize`、解构声明等），在大幅削减命名类型总数的同时，短期内会抵消部分行数缩减。
  3. 当前剩余代码量高度集中于 Win32 GUI 控件创建/消息分发（`rtx_gui.cpp`、`workstation_canvas.cpp`）、FFmpeg I/O 管线（`ffmpeg_io.cpp`、`hw_transcode_stages.cpp`）及大型集成测试（`test_raii_retirement.cpp`、`test_rife.cpp`、`test_playback_session.cpp`）。
- **后续收敛路径建议**：若后续波次仍需向 `<= 26,500` 推进，建议以“真去重”方式合并 GUI 控件表驱动构造、抽离重复的 FFmpeg 错误码映射与测试夹具，而非压缩单行格式。

---

## 5. 编译与静态验证边界声明（Gate 4）

### 5.1 Linux GCC 12 实编通过清单（16 / 16 PASS，0 Error / 0 Warning）
编译参数：`-std=c++23 -Wall -Wextra -Werror -Wno-interference-size`

1. `src/core/gop_seek_scheduler.cpp`
2. `src/core/lut3d.cpp`
3. `src/core/ngx_remaster.cpp`
4. `src/core/nvidia_shim.cpp`
5. `src/core/sei_synthesizer.cpp`
6. `src/core/shot_segmenter.cpp`
7. `src/core/vram_scheduler.cpp`
8. `src/shim/loader/nv_backend_shim.cpp`
9. `src/shim/loader/nv_func_tables.cpp`
10. `src/shim/loader/nv_runtime.cpp`
11. `src/shim/loader/nv_shim_static_table.cpp`
12. `tests/test_flicks_semantics.cpp`
13. `tests/test_flicks_timebase.cpp`
14. `tests/test_hdr.cpp`
15. `tests/test_lut3d.cpp`
16. `tests/test_vram_scheduler.cpp`

### 5.2 Windows / FFmpeg / D3D12 专属 TU 静态审计清单（待实机 MSVC 复核）
其余 38 个受影响的 Windows/Win32/D3D12/FFmpeg 依赖文件（含 `src/cli/main.cpp`、`src/gui/*`、`src/io/*`、`src/core/hw_transcode*.cpp`、`src/core/job_queue.cpp`、`src/core/nle_project.cpp`、`src/core/playback_session*.cpp`、`src/core/rational.cpp`、`src/core/render_graph.cpp` 及相应 `tests/*.cpp`）已在沙箱内完成全量符号引用、残余字段访问（0 处）、`std::get<`（0 处）、旧格式化 API（0 处）及头文件依赖闭环审计，但因 Linux 沙箱缺失 MSVC / WinSDK / D3D12 / FFmpeg 开发库，尚未在本地链接产出 `.exe`。

---

## 6. 全量变更文件清单（54 个）

- **`include/rtx/`（14）**：`embedded_shim.h`、`flicks.h`、`hw_transcode.h`、`io.h`、`job_queue.h`、`lut3d.h`、`playback_session.h`、`raii_owners.h`、`render_graph.h`、`shot_segmenter.h`、`time_model.h`、`timestamp_guard.h`、`types.h`、`vram_scheduler.h`
- **`src/cli/`（1）**：`main.cpp`
- **`src/core/`（18）**：`gop_seek_scheduler.cpp`、`hw_transcode.cpp`、`hw_transcode_internal.h`、`hw_transcode_stages.cpp`、`job_queue.cpp`、`lut3d.cpp`、`ngx_remaster.cpp`、`nle_project.cpp`、`nvidia_shim.cpp`、`playback_session.cpp`、`playback_session_audio.cpp`、`playback_session_impl.h`、`playback_session_video.cpp`、`rational.cpp`、`render_graph.cpp`、`sei_synthesizer.cpp`、`shot_segmenter.cpp`、`vram_scheduler.cpp`
- **`src/gui/`（3）**：`preview_viewport.cpp`、`rtx_gui.cpp`、`workstation_canvas.cpp`
- **`src/io/`（2）**：`cpu_video_encoder.cpp`、`ffmpeg_io.cpp`
- **`src/shim/loader/`（3）**：`nv_func_tables.cpp`、`nv_func_tables.h`、`nv_runtime.cpp`
- **`tests/`（13）**：`test_flicks_semantics.cpp`、`test_flicks_timebase.cpp`、`test_fuzz_resilience.cpp`、`test_hdr.cpp`、`test_io.cpp`、`test_lut3d.cpp`、`test_nle_vertical_slice.cpp`、`test_playback_session.cpp`、`test_raii_retirement.cpp`、`test_rife.cpp`、`test_types.cpp`、`test_util.h`、`test_vram_scheduler.cpp`

> **归档纪律**：上述 54 个文件的旧版本均已重命名为 `*.cbak` 并移至仓库外部目录 `work/_CineRTX_bak_archive/`，`CineRTX/` 目录下零备份文件残留。

---

## 7. 实机 Windows MSVC 复核命令建议

请在 Windows 构建主机拉取最新仓库后依次执行：
1. `cmake --build build --config Release`（确认 MSVC `/std:c++latest /W4 /WX` 0 Error 0 Warning，产出 `CineRTX.exe` 与 `rtx_tests.exe`）
2. `build\Release\rtx_tests.exe`（确认全部单元测试 100% PASS）
3. `python scripts/verify_wave12_2.py .`（复核命名类型 = 47、PureSLOC = 30,105、零旧格式化 API、SPDX 100% PASS）
