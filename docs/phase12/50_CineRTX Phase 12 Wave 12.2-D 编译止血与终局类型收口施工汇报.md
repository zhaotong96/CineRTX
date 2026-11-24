# 50_CineRTX Phase 12 Wave 12.2-D 编译止血与终局类型收口施工汇报

- **对应裁决与作战令**：`docs/phase12/49_CineRTX Phase 12 Wave 12.2-C 编译断环与代码规模收敛终审裁决书暨Wave12.2-D收口令.md`（`CINERTX-P12-RULING-049`）
- **关联委托书**：`docs/phase12/48_CineRTX Phase 12 Wave 12.2-C 编译断环与代码规模收敛专家咨询委托书.md`（`CINERTX-P12-DELEGATION-048`）
- **施工日期**：2026-11-23
- **变更规模**：原位替换 41 个文件（含 `cmake/AuditTypes.cmake`），全部 41 份旧版本已以 `*.dbak` 迁出 `CineRTX/` 至外置归档目录 `work/_CineRTX_bak_archive/`，仓库内零 `.bak`/`.dbak` 残留

---

## 1. 终局门禁验收总表

| 门禁 | 指标项 | 49 号裁决书目标 | 实测结果 | 判定 |
| --- | --- | --- | --- | --- |
| **止血补丁 A/B** | `src/io/ffmpeg_io.cpp:218-235` & `src/gui/preview_viewport.cpp` | 修复 `hp/ht/hm` + 多余 `}`；补齐 `<bit>`/`<array>`/`<cstdint>` | **已完成**（全量花括号平衡校验 = 0 失配，`<bit>` 已显式补齐） | **PASS** |
| **Gate 1（终局类型）** | `AuditTypes` 合法具名类型总数 | `<= 35` 个，0 违规，0 孤立声明，`AuditTypes.cmake` 全量硬阻断 | **34 / 35**（净减 13 个，`47 -> 34`），**Violations = 0**，`RTX_AUDIT_TYPES_STRICT` 已默认置 `ON` | **PASS** |
| **Gate 2（Pure SLOC）** | `AuditSloc` 全仓纯代码行 | `<= 23,000` 行（严禁删注释、严禁压单行） | **30,013 行**（Comments=3,649，Files=134；净减 92 行） | **未达 23,000（诚实披露客观体量分布，见 §4）** |
| **Gate 3（卫生）** | `std::get<` / `OldFormat` / `AuditSpdx` / 仓内 `.bak` | 全仓 0 / 0 / 0 缺失 / 0 残留 | **0（含 `tests/`） / 0 / 0 / 0** | **PASS** |
| **Gate 4（编译验证）** | Linux GCC 12 便携 TU 实编（`-std=c++23 -Wall -Wextra -Werror -Wno-interference-size`） | 可编便携 TU 0E 0W | **20 / 20 PASS（0E 0W）** | **PASS（便携子集）** |
| **Gate 4（实机复核）** | Windows MSVC `/std:c++latest /W4 /WX` & 双目录 `rtx_tests.exe` | 0E 0W + 147/147 PASS | 沙箱无 MSVC/WinSDK，已完成全量字段清零与花括号静态审计 | **待实机 MSVC 复核** |

---

## 2. 两项编译止血补丁核验（Wave 12.2-C 遗留阻断）

1. **阻断 A（`src/io/ffmpeg_io.cpp:218-235`）**：严格按 49 号裁决书定型方案，在 `if (info.hdr.enabled)` 块内引入 `auto& [c_pri, c_trc, c_mat, c_rng] = info.color; (void)c_rng;`，彻底消除未声明标识符 `hp`/`ht`/`hm` 及第 227 行多余闭合花括号 `}`。全文件词法级花括号深度校验为 `0`。
2. **阻断 B（`src/gui/preview_viewport.cpp:11-16`）**：显式补齐 `#include <array>`、`#include <bit>`、`#include <cstdint>`，根治 `std::bit_cast` 缺失与传递包含隐患。

---

## 3. 本轮清剿的 13 个具名自定义类型与 7 处理由去重（47 -> 34，0 Violations）

### 3.1 清剿的 13 个具名类型明细

| # | 原具名类型 | 原属文件 | 现代 C++23/26 替代方案 |
| --- | --- | --- | --- |
| 1 | `TaggedHandle` | `include/rtx/types.h` | 确认为死代码（仅头文件自引用），连同 5 个未用句柄别名彻底删除 |
| 2 | `WsolaStreamState` | `include/rtx/audio_mixer_avx2.h` | `std::tuple<array, array, size_t, size_t, size_t, double, float>` 别名 + 结构化绑定 |
| 3 | `ParamMeta` | `include/rtx/param_schema.h` | 11 元 `std::tuple` 别名 + `constexpr makeParamMeta(...)` 工厂 |
| 4 | `TelemetrySnapshot` | `include/rtx/telemetry_bus.h` | `std::array<uint64_t, 16>`（严格 128B、平凡可拷贝）+ `TelemetrySlot` 枚举与内联读写器 |
| 5 | `AtomicTelemetryBus` | `include/rtx/telemetry_bus.h` | `std::array<std::atomic<uint64_t>, 24>`（严格 192B，词 0 与词 8..23 跨缓存行隔离）+ `telemetryPublish`/`telemetryRead` |
| 6 | `GpuRetirementQueue` | `src/gui/d3d12_raii.h` | 6 元 `std::tuple` 别名 + `gpuRetire`/`gpuCollect`/`gpuReleaseAll` 等自由函数 |
| 7 | `NvidiaRuntime` | `src/shim/loader/dyn_library.h` | 6 元 `std::tuple` 别名 + `rtLoadCuda`/`rtLoadNvenc`/`rtLoadNvcuvid`/`rtLoadNgx` 等自由函数 |
| 8 | `DynLibrary` | `src/shim/loader/dyn_library.h` | `std::tuple<std::shared_ptr<void>, std::string, std::string>`（`dynUnloadHandle` 自定义删除器）+ `dynOpenSystem`/`dynOpenExplicit`/`dynSymbol`/`dynClose` |
| 9 | `cuda::Context` | `src/shim/loader/nv_cuda_context.h` | `std::tuple<DynLibrary, DriverTable, CUcontext, CUdevice, std::shared_ptr<void>>`（RAII 主上下文释放守卫）+ `cudaCtxOpen`/`cudaCtxClose` 等自由函数 |
| 10 | `EncoderJob` | `include/rtx/job_queue.h` | 14 元 `std::tuple` 别名 + `encoderJobId`/`encoderJobStatus`/`formatEncoderJobSummary`/`encoderJobToJson` |
| 11 | `MemoryAvioContext` | `src/io/memory_avio.h` | `std::shared_ptr<MemoryAvioState>`（自定义 `releaseAvioState` 删除器）+ `makeMemoryAvioRead`/`makeMemoryAvioWrite`/`memoryAvioRaw` 等自由函数 |
| 12 | `NleMediaAsset` | `include/rtx/nle_project.h` | 8 元 `std::tuple` 别名 + `makeNleAsset`/`nleAssetId` |
| 13 | `TimelineClip` | `include/rtx/nle_project.h` | 8 元 `std::tuple` 别名 + `makeTimelineClip`/`nleClipId`/`nleClipDuration` |

### 3.2 剩余 7 处重复 `@rtx-type-justification` 去重与硬阻断切换
- 为 `HwTranscodeReport`、`Rational`、`Hdr10Metadata`、`Frame`、`Packet`、`NvidiaTranscodeSession`、`RemasterPipeline` 全部撰写了类型专属的 `Why-Std-Fails` 与 `Invariant`，使 `AuditTypes` 违规数归零（**34 / 35 Valid, 0 Violations**）。
- 已更新 `cmake/AuditTypes.cmake`，将 `RTX_AUDIT_TYPES_STRICT` 默认值切换为无条件 `ON`（全量硬阻断模式）。
- 同步清零了 `tests/test_playback_session.cpp` 中遗留的 6 处 `std::get<`，全仓（含 `tests/`）`std::get<` 命中数为 **0**。

---

## 4. Pure SLOC 现状的客观数据披露（Gate 2）

本轮实测 `AuditSloc` 为 **30,013**（严守不删语义注释、不压单行红线），未能达到 `<= 23,000` 目标。向专家组如实呈报按目录拆分的物理实测分布（仅源码 `.h/.hpp/.cpp`）：

| 目录 | Pure SLOC（实测） | 占比 | 说明 |
| --- | ---: | ---: | --- |
| `src/shim/` | 8,641 | 28.0% | NVDEC/NVENC/D3D12/NGX/RIFE 硬件桥接实现 |
| `src/core/` | 7,335 | 23.7% | 转码编排、播放会话、NLE、参数 Schema、i18n |
| `tests/` | 5,969 | 19.3% | 22 个测试文件（并非 49 号预估的 13,000 Pure SLOC） |
| `src/gui/` | 3,157 | 10.2% | Win32 工作站画布、D3D12 预览视口、主控窗体 |
| `include/` | 2,877 | 9.3% | 公共接口与内联 C++23 算法头 |
| `src/io/` | 1,996 | 6.5% | FFmpeg 封装/解复用、码流分析、内存 AVIO |
| `src/cli/` | 940 | 3.0% | CLI 入口与会话 stdio 协议 |

**客观数学事实**：即使将 `tests/` 全部 5,969 行归零，仅生产代码（`include/` + `src/` = **24,946** Pure SLOC）已高于 23,000 阈值；其中 `src/shim/` 单目录即占 8,641 行。因此，在不动摇硬件编解码/NGX/RIFE 生产功能与 147 项单测覆盖的前提下，单靠 `tests/` 折叠与 `src/gui/` 表驱动在数学上无法净削 7,013 行。建议专家组在确认实机编译全绿与 Gate 1（34/35 硬阻断）达标后，结合 Wave 12.3（DLL 净空与核心/界面解耦）重新校准 Pure SLOC 阶段水位，或明确授权裁剪特定功能模块。

---

## 5. 编译与静态验证边界声明（Gate 4）

### 5.1 Linux GCC 12 实编通过清单（20 / 20 PASS，0 Error / 0 Warning）
编译参数：`-std=c++23 -Wall -Wextra -Werror -Wno-interference-size`（使用沙箱临时 `<format>` 桩头，未写入仓库）：
1. `src/core/gop_seek_scheduler.cpp`
2. `src/core/lut3d.cpp`
3. `src/core/ngx_remaster.cpp`
4. `src/core/nvidia_shim.cpp`
5. `src/core/sei_synthesizer.cpp`
6. `src/core/shot_segmenter.cpp`
7. `src/core/vram_scheduler.cpp`
8. `src/core/wsola_stretch.cpp`
9. `src/shim/loader/dyn_library.cpp`
10. `src/shim/loader/nv_backend_shim.cpp`
11. `src/shim/loader/nv_cuda_context.cpp`
12. `src/shim/loader/nv_func_tables.cpp`
13. `src/shim/loader/nv_runtime.cpp`
14. `src/shim/loader/nv_shim_static_table.cpp`
15. `tests/test_audio_mixer_avx2.cpp`
16. `tests/test_flicks_semantics.cpp`
17. `tests/test_flicks_timebase.cpp`
18. `tests/test_hdr.cpp`
19. `tests/test_lut3d.cpp`
20. `tests/test_vram_scheduler.cpp`

### 5.2 Windows / FFmpeg / D3D12 / C++23 Deducing-this 专属 TU 静态审计
其余受影响的 21 个文件（含 `src/io/ffmpeg_io.cpp`、`src/io/memory_avio.cpp`、`src/gui/*`、`src/core/job_queue.cpp`、`src/core/nle_project.cpp`、`src/core/param_schema.cpp`、`src/core/playback_session*.cpp`、`src/core/render_graph.cpp`、`src/shim/loader/dyn_library_win32.cpp`、`nv_decoder.cpp`、`nv_encoder.cpp` 及对应测试文件）已完成全仓旧字段名 grep 清零与词法级花括号平衡校验，待 Windows 宿主机 MSVC 实编终验。

---

## 6. 实机 Windows MSVC 复核命令

```powershell
cmake --build --preset windows-msvc-ninja
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSpdx.cmake
cmake -P cmake/AuditSloc.cmake
```
