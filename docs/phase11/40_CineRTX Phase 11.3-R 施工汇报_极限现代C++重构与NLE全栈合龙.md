# CineRTX Phase 11.3-R 施工汇报：极限现代 C++20/23/26 重构与 NLE 全栈合龙

- **文档编号**：`CINERTX-P11-REPORT-040`
- **依据施工单**：[`39_CineRTX Phase 11.3-R SuperPower 施工单_极限现代C++重构与NLE全栈合龙.md`](39_CineRTX%20Phase%2011.3-R%20SuperPower%20施工单_极限现代C++重构与NLE全栈合龙.md)
- **母本审计**：[`38_CineRTX 现代 C++ 极限重构审计.md`](38_CineRTX%20现代%20C++%20极限重构审计.md)
- **执行基线**：Commit `749fe0d`（未触碰 `cmake/`、`docs/` 既有文档及根 `CMakeLists.txt`）
- **文件编辑方式**：现有 14 处修改文件均已通过重命名备份（`*.pre39.bak`）+ 替换上传方式落盘，9 处新建文件已直接上传至对应目录。

## 一、 7 项核心任务交付清单

| 任务 | 文件路径 | 操作 | 核心重构摘要 | Drive 链接 |
| :--- | :--- | :--- | :--- | :--- |
| **任务 1** | `include/rtx/param_schema.h` | 替换 | 拔除 `schema/generated/*` 包含；落地 C++23 `ProfileMemberPtr`、`kParamDescriptors`（20 项）、`consteval validateParamDescriptors()`、`static_assert`、`forEachProfileField` 及兼容 `kParamSchema`/`kCliFlags`/`findCliFieldBinding` | `param_schema.h` |
| **任务 1** | `src/core/param_schema.cpp` | 替换 | `exportSchemaJson()` 直接消费 `kParamDescriptors` | `param_schema.cpp` |
| **任务 1** | `tests/test_param_schema_reflection.cpp` | 替换 | 移除 `schema/generated/` 依赖，校验 `kParamDescriptors` 与 `validateParamDescriptors()` 及 20 字段 JSON 往返 | `test_param_schema_reflection.cpp` |
| **任务 2** | `include/rtx/raii_owners.h` | 替换 | 升级 `FnDeleter` 为 C++23 `static void operator()(auto* p) noexcept` + `if constexpr (requires { Fn(p); })` 统一分派 `T*`/`T**` | `raii_owners.h` |
| **任务 2** | `src/core/playback_session_audio.cpp` | 替换 | 物理删除 `IndirectDeleter`，全部 4 个 FFmpeg 智能指针别名切至 `FnDeleter` | `playback_session_audio.cpp` |
| **任务 2** | `include/rtx/playback_session.h` | 替换 | `PlaybackClockState` 压缩为 4 元组（`std::optional<steady_clock::time_point>` 编码暂停态）并全面改用结构化绑定 | `playback_session.h` |
| **任务 2** | `src/core/playback_session_impl.h` | 替换 | 适配 4 元组 `PlaybackClockState` 初始状态 | `playback_session_impl.h` |
| **任务 2** | `src/core/playback_session.cpp` | 替换 | `create()` / `setClockPaused()` 改用 4 元组结构化绑定 | `playback_session.cpp` |
| **任务 2** | `src/core/playback_session_video.cpp` | 替换 | `performSeek()` / `videoLoop()` 改用 4 元组结构化绑定 | `playback_session_video.cpp` |
| **任务 2** | `tests/test_playback_session.cpp` | 替换 | 同步更新 2 项软件/音频主时钟单测至 4 元组与结构化绑定（保留声学静音任务归宿主机） | `test_playback_session.cpp` |
| **任务 3** | `include/rtx/nle_project.h` | 新建 | `NleMediaAsset`、`TimelineClip`、6 支 `std::variant` 代数和 `NleEditDelta`、`EditStacks`、`NleProject`（C++23 Deducing `this`，零 `const_cast`）及 7 类纯编辑函数 | `nle_project.h` |
| **任务 3** | `src/core/nle_project.cpp` | 新建 | 实现 7 类可逆编辑操作与 Undo/Redo（深度 256）、零尾逗号 RFC 8259 `toJson()` 与无异常 `Result<NleProject> fromJson()` | `nle_project.cpp` |
| **任务 4** | `include/rtx/render_graph.h` | 新建 | 仅声明 `RenderGraphSegment` 与 `compileRenderGraph()`（零 `VideoClaim` / `CompiledRenderGraph`） | `render_graph.h` |
| **任务 4** | `src/core/render_graph.cpp` | 新建 | 单遍离散事件扫描线（`std::ranges::sort/unique` + `std::views::filter` + `min_element`），同源连续段合并与跨源硬切 `hard_cut`/`force_keyframe` 置位 | `render_graph.cpp` |
| **任务 4** | `src/core/hw_transcode_stages.cpp` | 替换 | GPU 编码分支补齐 `encode_frame.keyframe = item.force_keyframe ? 1 : 0;`（C-05 闭环） | `hw_transcode_stages.cpp` |
| **任务 5** | `include/rtx/audio_mixer_avx2.h` | 新建 | `using AudioVoice = std::pair<std::span<const float>, float>;` + `mixAvx2(...) noexcept` + `wsolaStretch(...) noexcept`（零 `VoicePrep` / `AudioMixerScratchpad`） | `audio_mixer_avx2.h` |
| **任务 5** | `src/core/audio_mixer_avx2.cpp` | 新建 | AVX2+FMA 单指令 `_mm256_fmadd_ps` 混音主循环 + 标量尾迹，零堆分配 | `audio_mixer_avx2.cpp` |
| **任务 5** | `src/core/wsola_stretch.cpp` | 新建 | 静态 32 字节对齐只读共享汉宁窗 `kHannTable` + C++23 `std::mdspan<float, std::dextents<size_t, 2>>` 二维视图 | `wsola_stretch.cpp` |
| **任务 6** | `src/gui/workstation_canvas.h` | 替换 | 彻底拔除旧时间轴符号与裸毫秒宏；`WorkstationApp` 按值持有 `project`、`history`、`exportThread` 与 `std::atomic<bool>` 状态 | `workstation_canvas.h` |
| **任务 6** | `src/gui/workstation_canvas.cpp` | 替换 | 统一使用 `TimelineClip`/`paintTimelineCanvas`/`registerTimelineCanvasWindowClass`；零判空防御；`seekPlaybackFromSlider` 改用 `duration_cast<Flicks>`；`onNleExportSequence` 使用 `std::jthread` + 堆所有权消息投递 | `workstation_canvas.cpp` |
| **任务 6** | `src/gui/rtx_gui.cpp` | 替换 | 适配 `registerTimelineCanvasWindowClass`、`g_app.project`、`WM_APP_NLE_EXPORT_DONE` 并在 `WM_DESTROY` 中显式 `exportThread.request_stop()` / `join()` | `rtx_gui.cpp` |
| **任务 7** | `tests/test_nle_vertical_slice.cpp` | 新建 | 覆盖 7 类编辑操作、Undo/Redo、RelinkAsset、无尾逗号 RFC 8259 JSON 往返与多段硬切 IDR 扫描线断言 | `test_nle_vertical_slice.cpp` |
| **任务 7** | `tests/test_audio_mixer_avx2.cpp` | 新建 | 栈定容 `std::array<AudioVoice, 32>` + 局部 Debug CRT `_CrtSetAllocHook` 验证，**零全局 `operator new/delete`** | `test_audio_mixer_avx2.cpp` |

## 二、 本地门禁与冒烟验证结果

| 检查项 | 执行方式 | 实测结果 |
| :--- | :--- | :--- |
| **Gate 11.3-R1（旧符号零残留）** | 正则扫描 `src/gui/workstation_canvas.*` 与 `tests/` | **0 命中（PASS）** |
| **Gate 11.3-R2（全局 `operator new` 零劫持）** | 正则扫描 `tests/test_audio_mixer_avx2.cpp` | **0 命中（PASS）** |
| **Gate 11.3-R3（`AuditTypes.cmake` 等价审计）** | 1:1 复现 `cmake/AuditTypes.cmake` 规则扫描全部 19 处交付头文件/源文件 | **19 个具名类型，0 违规（PASS）** |
| **SPDX / 无异常 / GUI 遗留符号** | 扫描全部 23 份交付文件 | **SPDX 23/23 完整；`nle_project.cpp` 零 `throw/catch`；GUI 遗留符号 0 命中（PASS）** |
| **AVX2+FMA & WSOLA 实机编译与运行** | Debian 12 `g++ 12.2 -std=c++23 -O2 -Wall -Wextra -mavx2 -mfma` 编译并执行 | **0 警告；FMA 误差 `< 1e-5f`；WSOLA 1.5x/0.75x/1.0x 断言全通过（PASS）** |

## 三、 阻塞项与宿主机合龙收尾指令

1. **物理删除 `scripts/` 与 `schema/` 目录（Drive API 无删除权限）**：
   - 本离线环境无 workspace 删除工具与 Git 写权限，且严禁触碰根 `CMakeLists.txt` 与 `cmake/`。
   - 请协调负责人在本地宿主机同步后执行以下清理与 CMake 合龙命令：
     ```powershell
     # 1. 移除废弃目录与 .pre39.bak 替换备份
     git rm -r scripts schema
     Get-ChildItem -Recurse -Filter "*.pre39.bak" | Remove-Item -Force

     # 2. 在 CMakeLists.txt 中移除 schema/ 规则并挂接新增的 4 个核心 .cpp 与 2 个单测 .cpp：
     #    src/core/nle_project.cpp, src/core/render_graph.cpp,
     #    src/core/audio_mixer_avx2.cpp, src/core/wsola_stretch.cpp,
     #    tests/test_nle_vertical_slice.cpp, tests/test_audio_mixer_avx2.cpp
     ```
2. **MSVC 19.51 全量编译与四大硬门禁终验（沙箱为 Linux GCC 12.2，缺 Win32/D3D12/NVENC 及 C++23 `<format>` + Deducing `this`）**：
   - 请在 Windows 宿主机（MSVC 19.51 `/std:c++latest`）执行全量构建、CTest 与四门禁终验：
     ```powershell
     cmake -P cmake/AuditTypes.cmake
     cmake -P cmake/AuditSloc.cmake
     cmake -P cmake/AuditSpdx.cmake
     cmake --build build --config Release
     ctest --test-dir build -C Release --output-on-failure
     ```
