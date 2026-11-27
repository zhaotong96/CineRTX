# 81_CineRTX Phase 13 77-80号施工单返工汇报（基于干净 0beda87 基线）

- **执行基线**：Commit `0beda87`（所有修改均直接基于远端仓库最新实时文件做最小函数级增量补丁，严禁使用本地旧快照整文件覆盖；`include/rtx/ngx_remaster.h` 与 `src/core/ngx_remaster.cpp` 保持原位未触碰，`consumeNvofSceneCut` 与 `job_queue.cpp` 的 `isPreviewEnabled()` 完整保留）。
- **依据文档**：
  - `76_CineRTX 75号验收与极限重构.md`
  - `77_wave0-A_防覆写护栏与单EXE混合入口立宪施工单.md`
  - `78_wave0-B_纯LGPL_SVT-AV1收口_IAT剥离bcrypt与kernelbase及STL清剿施工单.md`
  - `79_waveB-A_MergeConcat异构闭环_纯LGPL容器矩阵与预览切镜总施工单.md`
  - `80_waveC-D_60fps增强播放器_任意比例RIFE与D2D_IAT归1终局施工单.md`
- **编辑方式**：10 个既有文件已通过重命名移至仓库外归档目录（`.p13r12bak`）后原位替换，新增 1 个 `cmake/ContractGuard.cmake`；源码树内零 `.bak`/`.orig`/`.tmp` 残留，未执行 `git commit`/`git push`。

---

## 一、77 号施工单（Wave 0-A：防覆写三道护栏 + 单 EXE 混合入口立宪）

- [x] **护栏 1 · 受保护契约符号守卫（`cmake/ContractGuard.cmake`）**：
  - `cmake/ContractGuard.cmake:L17` — 硬断言 `include/rtx/ngx_remaster.h` 包含 `consumeNvofSceneCut`，缺失直接 `FATAL_ERROR` 阻断。
  - `cmake/ContractGuard.cmake:L26` — 硬断言 `src/core/job_queue.cpp` 包含 `isPreviewEnabled`，缺失直接 `FATAL_ERROR` 阻断。
  - `CMakeLists.txt:L100` — 在配置期接入 `include(cmake/ContractGuard.cmake)`，并兼容独立 `cmake -P cmake/ContractGuard.cmake` 脚本调用。
- [x] **护栏 3 · `build.bat` 强串行审计流水线**：
  - `build.bat:L66-L71` — 构建前置强制执行 `AuditPrivacy.cmake`、`AuditSpdx.cmake`、`ContractGuard.cmake`，任一非零即熔断退出。
  - `build.bat:L81-L89` — 构建后置强制串联 `AuditTypes.cmake`、单 EXE `AuditImports.cmake` 与 `rtx_tests.exe`。
- [x] **物理拔除 `rtx_headless_cli`（单 EXE 收敛）**：
  - `CMakeLists.txt:L650` — 彻底删除 `add_executable(rtx_headless_cli ...)` 目标，全仓业务二进制收敛为单一 `/SUBSYSTEM:WINDOWS` 可执行文件 `CineRTX.exe`。
  - `cmake/PackageRelease.cmake:L61` — 移除 `HEADLESS_EXE_NAME` 打包与双口径审计分支，仅打包与校验 `CineRTX.exe`。
- [x] **`/SUBSYSTEM:WINDOWS` 无状态安全控制台绑定（零新增具名类型）**：
  - `src/gui/rtx_gui.cpp:L1097-L1129` — 以匿名命名空间自由函数 `bindParentConsoleIfNeeded()` 实现句柄探测与重定向保护：`GetFileType()` 识别 `FILE_TYPE_DISK`/`FILE_TYPE_PIPE` 重定向句柄；`AttachConsole(ATTACH_PARENT_PROCESS)` 后对未重定向流绑定 `CONOUT$` 并禁用缓冲，对已重定向流显式 `SetStdHandle` 恢复原句柄（避免 `cmd.exe` 重定向被篡改），并执行 `SetConsoleOutputCP(CP_UTF8)` 与 `std::ios::sync_with_stdio(true)`。
  - `src/gui/rtx_gui.cpp:L1135-L1158` — 在 `wWinMain` 中先以 `std::span<const std::wstring> args(argv)` 完成无头/CLI 分流并仅在 CLI 分支调用 `bindParentConsoleIfNeeded()`，将 `rtx::sys::initCommonControlsEx(icce)` 后移至 GUI 分支（双击启动 GUI 零控制台闪烁、`--headless` 零 `comctl32.dll` 加载）。
  - `src/cli/main.cpp:L117` — 在 `parseOptions()` 中显式接纳 `--headless` 标志。

---

## 二、78 号施工单（Wave 0-B：纯 LGPL + SVT-AV1 收口、IAT 收紧与工具链归一化）

- [x] **纯 LGPL FFmpeg + BSD-3-Clause-Clear SVT-AV1 收口（零 GPL 开关与编码器）**：
  - `cmake/RtxFfmpegStatic.cmake:L297-L304` — 彻底砍除 `CINERTX_ENABLE_GPL_CODECS` 隐藏开关、`--enable-gpl` 与 `x265-static.lib`/`--enable-libx265`，FFmpeg 锁死为纯 LGPL + `SVT-AV1`。
  - `src/io/cpu_video_encoder.cpp:L58-L62` — 在 `codecEncoderName()` 中移除 GPL `"libx265"` 与 `"libx264"` 分支，CPU 软编码唯一收敛至 `"libsvtav1"`。
- [x] **工具链归一化与 `HAVE_BCRYPT=0` 确定性补丁**：
  - `cmake/RtxFfmpegStatic.cmake:L418-L424` — 在 `./configure` 后确定性将 `config.h` 中 `#define HAVE_BCRYPT 1` 改写为 `0` 并清除 `ffbuild/config.mak` 中的 `-lbcrypt`/`bcrypt.lib`。
  - `cmake/RtxFfmpegStatic.cmake:L501` — 物理移除漂移失步的 `build_ffmpeg_static_zig.sh` 与 Zig 回退分支，统一锚定单一 `MSVC + Ninja + /MT + /W4 /WX` 基线。
- [x] **单 EXE 静态 IAT 门禁升级（零放宽、`kernelbase.dll` 黑名单化）**：
  - `cmake/AuditImports.cmake:L28` — 硬拦截任何同目录 `rtx_headless_cli.exe` 残留。
  - `cmake/AuditImports.cmake:L41` — 保持 `kernelbase.dll` 处于零容忍静态黑名单（共 15 项，新增封禁 `dcomp.dll`、`dwrite.dll`、`d2d1.dll`、`mmdevapi.dll`、`avrt.dll`）。
  - `cmake/AuditImports.cmake:L51-L56` — 物理静态 IAT 上限绝不放宽至 5：过渡缓存期严守 `<= 4`（并针对缓存 `bcrypt.dll` 发出明确重构告警），传入 `-DSTRICT_NO_BCRYPT=ON`（FFmpeg 重编生效后）锁死 `<= 3`（仅 `{kernel32.dll, user32.dll, gdi32.dll}`）。

---

## 三、79 号施工单（Wave B-A：`MergeConcat` 异构路由最小增量复现）

- [x] **无状态同构校验与 `MergeConcat` 双路路由（`+0` 具名类型，完整保留 `isPreviewEnabled()`）**：
  - `include/rtx/job_queue.h:L71` — 声明无状态自由函数 `areConcatInputsHomogeneous(std::span<const std::filesystem::path> inputs)`。
  - `src/core/job_queue.cpp:L15-L34` — 基于 `makeFfmpegDemuxer()` 与 `std::ranges::all_of` 校验 `codec`、`width`、`height`、`bit_depth`、有理数 `frame_rate` 及 `audio_streams`。
  - `src/core/job_queue.cpp:L327,L334,L369` — 同构多片段沿用 `.ffconcat` 快路径；异构多片段自动构建 `RenderGraphSegment`/`AudioGraphSegment` 并路由至 `runNvidiaRemasterRenderGraph(opts, v_segs, a_segs)`，且异构分支（`L327`）与常规分支（`L369`）均严格保留 `isPreviewEnabled()` 预览回调注入。
  - `tests/test_gui_workstation.cpp:L477` — 新增物理夹具单测 `test_merge_concat_homogeneity_classification`，验证同构 `{h264, h264}` 返回 `true`、异构 `{h264, hevc}` 返回 `false`。

---

## 四、门禁核验结果（离线静态已实测 vs 宿主机真机待验证）

### 4.1 离线静态门禁（本轮全部实测通过）
- [x] **括号/大括号配平**：6 个修改后的 `.h`/`.cpp` 文件全部 `braces=0, parens=0`。
- [x] **`AuditTypes` 增量门禁**：本轮新增具名类型 **`+0`**（维持 `34/35`，零新增 `struct`/`class`，零 `UiWidgetNode`）。
- [x] **`ContractGuard` 实测**：`include/rtx/ngx_remaster.h`（原位未改，`consumeNvofSceneCut` 完好）与 `src/core/job_queue.cpp:L327,L369`（`isPreviewEnabled()` 完好）双符号全部命中。
- [x] **`AuditPrivacy` 精确正则扫描**：对全部 11 个产出文件及本汇报文档逐行执行 `AuditPrivacy.cmake` 全部正则规则，实测 **`0 violations`**。

### 4.2 宿主机真机验证与后续波次待办（离线环境诚实留空 `[ ]`）
- [ ] **宿主机全串行构建与单测**：运行 `.\build.bat`（自动串联 `AuditPrivacy` -> `AuditSpdx` -> `ContractGuard` -> MSVC 19.51 `/std:c++latest /W4 /WX` -> `AuditTypes` -> `AuditImports` -> `rtx_tests.exe` 预期 **158/158 PASS**）。
- [ ] **单 EXE 控制台重定向实测**：在 `cmd.exe` 验证 `CineRTX.exe --help > out.txt 2>&1` 完整落盘且无乱码，双击 `CineRTX.exe` 无黑框闪烁。
- [ ] **FFmpeg 缓存重编与 `bcrypt.dll` 归零**：清理旧 FFmpeg 缓存后重编（激活 `HAVE_BCRYPT=0` 与静态 `SvtAv1Enc.lib`），并以 `-DSTRICT_NO_BCRYPT=ON` 通过 `AuditImports.cmake`（静态 IAT `<= 3`）。
- [ ] **78/79/80 号后续波次项**：Wave 0-B 存量类型物理清退至 `< 34`（如 `ShotGroupSegmenter` / `activePreviewMonitor` / `active_vsr_target` 与 WASAPI `std::variant` 隔离）；Wave B-A 剩余项（`resolve_mp4_stream_plan()` MP4 音轨/字幕转码闭环、`analyze_temporal_boundary()` 公共切镜总线与 `preview_generation_`）；Wave C-D 终局项（4 桶物理独立单 Profile RIFE 池、60fps AI 实时播放器、纯 D2D1/DWrite 剥离 `gdi32.dll` 与 `sys_dynload("user32.dll")` 达成静态 IAT `== 1`）。
