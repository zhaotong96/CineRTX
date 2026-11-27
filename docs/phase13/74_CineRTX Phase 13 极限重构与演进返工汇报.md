# 74_CineRTX Phase 13 极限重构与演进返工汇报（基于 0beda87 基线与 70–73 号文档）

- **执行基线**：`0beda87`（宿主机 Windows 11 + RTX 5060 已封板基线，MSVC 19.51 `/std:c++latest /W4 /WX` 0 Error 0 Warning、`rtx_tests.exe` 157/157 PASS、`AuditTypes` 34/35、`AuditPrivacy` 0、`AuditSpdx` PASS）
- **依据文档**：
  - `69_CineRTX Phase 13 基于最新基线(0beda87)重编 Wave B-D 指导书委托书.md`
  - `70_CineRTX极限重构与演进方案.md`
  - `71_wave1-A_双端预览真机封板与RIFE全链路增量落地施工单.md`
  - `72_wave1-B_GUI拖放即入队_Pending靶向联动与120-rife防回落施工单.md`
  - `73_wave2-A_RIFE真机端到端非dup与NV12色度单测及全仓门禁封板施工单.md`
- **编辑方式**：全部通过工作区重命名归档至 `work/_CineRTX_bak_archive/*.p13r11bak` 并原位上传纯净新文件（`CineRTX/` 树内零 `.bak` 残留，未执行 `git commit`/`git push`）。

---

## 一、71–73 号施工单基线核验实证（已在 0beda87 全部落地并锁死）

经同步核对 `0beda87` 最新源码，71、72、73 号文档所列双端预览真机封板、RIFE FG-First 与非阻塞流隔离、PTX 11 参数对齐与满量程缩放、GUI 拖放即入队与 `activeSyncIds` 靶向联动、以及 4 项纯 C ABI 端到端单测均已在 `0beda87` 完整闭环且零回退：

- [x] **71 号施工单 · 双端预览 12-DWORD 色彩契约与 DComp 硬裁剪**：`src/gui/preview_viewport.h` / `src/gui/preview_viewport.cpp` 维持 `presentShared(..., color_flags, left_max_nits, right_max_nits, ticket)` 与 `CreateTargetForHwnd(..., FALSE)` + `IDCompositionRectangleClip`；`src/core/hw_transcode_stages.cpp` 维持动态预览几何与 HDR10/HLG 双侧掩码。
- [x] **71 号施工单 · RIFE FG-First 原生分辨率锁定 + `rife_stream_` 非阻塞流隔离 + `rife_pack.ptx` 12×`testp.finite.f32` / 3×`255.0f`**：`src/shim/ngx_bridge/rife_engine.h:L74`、`src/shim/ngx_bridge/rife_pack.ptx` 保持 `0beda87` 封板状态。
- [x] **72 号施工单 · GUI 拖放即入队、`activeSyncIds` 靶向联动与 `120-rife` 防回落**：`src/gui/rtx_gui.cpp`、`src/gui/workstation_canvas.h`、`src/core/param_schema.cpp` 保持 `0beda87` 封板状态。
- [x] **73 号施工单 · 纯 C ABI RIFE E2E 单测与 157/157 绿灯基线**：`tests/test_rife.cpp` 通过 `NgxRemasterBridge` + `TestCudaDriver` 纯 C ABI 验证 Case 1–3，零私有头文件跨界污染。

---

## 二、70 号文档（Wave 0 + Wave B/C/D 增量落地）执行清单与代码实证

本轮严格遵循 70 号文档的“**先排空 Wave 0 架构红线，再按增量补丁推进 Wave B–D，全程 `+0` 新增具名类型（彻底否决 `UiWidgetNode`）**”原则，完成 14 个文件的精准外科手术：

### 2.1 Wave 0：单可执行文件收拢、红线清理与隐蔽开关拔除

- [x] **彻底拔除 `rtx_headless_cli` 独立构建目标（单 EXE 收拢）**：
  - `CMakeLists.txt:L649` — 完整删除 `add_executable(rtx_headless_cli ...)` 及其 alias/链接/属性块，全仓业务二进制收敛为单一 `CineRTX.exe`。
  - `cmake/AuditImports.cmake:L28` — 升级为单 EXE 统一审计脚本，硬拦截 `rtx_headless_cli.exe` / `rtx_headless_cli` 残留，同时将 `mmdevapi.dll` 与 `avrt.dll` 纳入静态导入黑名单（`cmake/AuditImports.cmake:L44`）。
  - `cmake/PackageRelease.cmake:L61` — 移除 `HEADLESS_EXE_NAME` 与双二进制打包/审计分支，仅打包与审计单一 `CineRTX.exe`。
- [x] **`wWinMain` 前端 `std::span` 无头分流 + `GetFileType` 守卫 `AttachConsole`/`freopen_s`**：
  - `src/gui/rtx_gui.cpp:L1096` — 在触及 `initCommonControlsEx` 及任何窗口/图形组件加载之前，先以 `std::span<const std::wstring> args(argv)` 完成命令行分流。
  - `src/gui/rtx_gui.cpp:L1108` — 进入 CLI/无头分支时，先通过 `GetFileType(GetStdHandle(STD_OUTPUT_HANDLE/STD_ERROR_HANDLE))` 探测句柄：保留 `FILE_TYPE_PIPE` / `FILE_TYPE_DISK` 重定向管道不受破坏；仅在句柄为 `FILE_TYPE_UNKNOWN` 且 `AttachConsole(ATTACH_PARENT_PROCESS)` 成功时通过 `freopen_s` 重绑 `CONOUT$`/`CONIN$`，严禁调用 `AllocConsole()`。
  - `src/gui/rtx_gui.cpp:L1135` — 将 `rtx::sys::initCommonControlsEx(icce)` 后移至确认 GUI 模式之后，杜绝 `--headless` 路径触发 `comctl32.dll` 加载。
  - `src/cli/main.cpp:L117` — 在 `parseOptions()` 中显式接纳 `--headless` 参数，使 `CineRTX.exe --headless --probe` 等调用无缝透传。
- [x] **铲除 `CINERTX_ENABLE_GPL_CODECS` 隐藏开关、拔除失步 Zig 分支、补齐 `HAVE_BCRYPT=0` 确定性补丁**：
  - `cmake/RtxFfmpegStatic.cmake:L297` — 删除 `CINERTX_ENABLE_GPL_CODECS` 门控开关，默认开启 `--enable-gpl` 并无条件接纳静态 `SvtAv1Enc.lib` / `x265-static.lib`。
  - `cmake/RtxFfmpegStatic.cmake:L421` — 在 FFmpeg `./configure` 之后加入确定性补丁：将 `config.h` 中 `#define HAVE_BCRYPT 1` 强制改写为 `0`，并从 `ffbuild/config.mak` 清除 `-lbcrypt` / `bcrypt.lib`（注：保留 `rtx_ffmpeg_static` 接口链接兼容现有宿主机缓存 `avutil.lib`；一旦宿主机重编 FFmpeg 缓存，MSVC `/OPT:REF` 将自动实现 `bcrypt.dll` 静态导入清零，零 `LNK2019` 破坏风险）。
  - `cmake/RtxFfmpegStatic.cmake:L504` — 整块删除与主线漂移（遗漏 `mov_text` 与编码器对齐）的 `build_ffmpeg_static_zig.sh` 及 `.bat` 中的 Zig 回退分支，全线收敛为唯一受控 MSVC 工具链。
- [x] **彻底删除 `chromeText()` 私藏词条表与 `ChromeText` 枚举**：
  - `src/gui/workstation_canvas.h:L170-L180` — 删除 `enum class ChromeText`、`chromeText()` 与 `goldenPresetList()` 声明。
  - `src/gui/workstation_canvas.cpp:L213` — `playbackCaption()` 直接原地返回双语字面量。
  - `src/gui/workstation_canvas.cpp:L259` — 标题副文案改用编译期 `static constexpr std::wstring_view kWorkstationSubtitle`。
  - `src/gui/workstation_canvas.cpp:L810-L845` 与 `L904` — 全部按钮与提示文本原地内联，彻底消除二次跳转词条表。

### 2.2 Wave B / C / D 增量落地（严格 `+0` 具名类型）

- [x] **Wave B · 智能合并同构/异构双路路由（`+0` 具名类型）**：
  - `include/rtx/job_queue.h:L71` — 新增自由函数声明 `bool areConcatInputsHomogeneous(std::span<const std::filesystem::path> inputs)`。
  - `src/core/job_queue.cpp:L15` — 基于 `makeFfmpegDemuxer()` 与 `std::ranges::all_of` 实现严格同构校验（比对 `codec`、`width`、`height`、`bit_depth`、`frame_rate` 及 `audio_streams`）。
  - `src/core/job_queue.cpp:L327` — `JobOutputMode::MergeConcat` 多片段任务自动分流：严格同构走高效 `.ffconcat` 直出路径；异构特征自动构建 `RenderGraphSegment` / `AudioGraphSegment` 并路由至 `runNvidiaRemasterRenderGraph(opts, v_segs, a_segs)` 统一 GPU 渲染图合并。
  - `tests/test_gui_workstation.cpp:L477` — 新增单元测试 `test_merge_concat_homogeneity_classification`，基于现存物理夹具 `fixtures/h264_25fps_gop12.mkv` 与 `fixtures/hevc_25fps_gop12.mkv` 验证同构返回 `true`、异构返回 `false`。
- [x] **Wave C · 实时预览入口规范重命名**：
  - `include/rtx/ngx_remaster.h:L30`、`src/core/ngx_remaster.cpp:L111`、`src/gui/workstation_canvas.cpp:L948` — 将 `createHeadlessPreviewSink` 统一重命名为 `createRealtimePreviewSink`，消除语义歧义。
- [x] **Wave D 前置 · `writeWebuiPreviewBmpIfDue()` 栈上 54 字节 BMP 头**：
  - `src/core/hw_transcode_stages.cpp:L55` — 使用 `std::array<uint8_t, 54> hdr{}` 在栈上无分配构造 54 字节标准 BMP 文件头并经 `std::ofstream` 直接落盘，零 GDI 依赖。

---

## 三、全仓 34 个存量具名类型全量静态审计清单（Wave 0-4）

经复刻 `cmake/AuditTypes.cmake` 规则对全仓 `include/` 与 `src/` 扫描，本轮净增具名类型严格为 **`+0`**（`34/35`，严禁引入 `UiWidgetNode`）：

| 编号 | 文件:行号 | 类型名 | 分类/留存初审 |
|---:|---|---|---|
| 1 | `include/rtx/cpu_video_encoder.h:24` | `class CpuVideoEncoder` | 编码会话状态机（FFmpeg `AVCodecContext` 生命周期） |
| 2 | `include/rtx/hw_transcode.h:38` | `struct HwTranscodeOptions` | 领域参数聚合 |
| 3 | `include/rtx/hw_transcode.h:99` | `struct HwTranscodeReport` | 领域结果聚合 |
| 4 | `include/rtx/io.h:41` | `struct MediaInfo` | 媒体探测模型 |
| 5 | `include/rtx/io.h:71` | `class FfmpegDemuxer` | 硬件/容器状态机（待 Wave 0 后续评估拆解） |
| 6 | `include/rtx/io.h:126` | `class FfmpegMuxer` | 硬件/容器状态机（跨包 DTS 单调性与延迟 extradata） |
| 7 | `include/rtx/job_queue.h:91` | `class EncoderJobQueue` | 必要保留（UI/Worker 并发队列与停机同步不变式） |
| 8 | `include/rtx/ngx_remaster.h:37` | `class NgxRemasterBridge` | 必要保留（纯 C ABI 动态函数表与 3 槽预览注册） |
| 9 | `include/rtx/nle_project.h:88` | `struct NleProject` | 领域时间轴模型 |
| 10 | `include/rtx/nvidia_shim.h:22` | `class NvidiaShim` | 必要保留（动态 CUDA/NVDEC/NVENC 垫片宿主） |
| 11 | `include/rtx/param_schema.h:66` | `struct TranscodeProfile` | SSOT 参数反射聚合 |
| 12 | `include/rtx/playback_session.h:142` | `class WasapiAudioOutput` | 必要保留（WASAPI 音频时钟与动态 COM 宿主） |
| 13 | `include/rtx/playback_session.h:183` | `class MediaPlaybackSession` | 必要保留（音视频解复用/解码/呈现多线程协调） |
| 14 | `include/rtx/rational.h:19` | `struct Rational` | 数学有理数原语 |
| 15 | `include/rtx/status.h:55` | `struct Error` | 错误值载荷 |
| 16 | `include/rtx/types.h:271` | `struct Hdr10Metadata` | HDR10 元数据布局 |
| 17 | `include/rtx/types.h:323` | `struct Frame` | 视频帧元数据与缓冲载体 |
| 18 | `include/rtx/types.h:400` | `struct Packet` | 压缩包载体 |
| 19 | `src/core/hw_transcode_internal.h:104` | `class NvidiaTranscodeSession` | 必要保留（NVDEC/NGX/NVENC 硬件流水线会话） |
| 20 | `src/core/hw_transcode_internal.h:214` | `class RemasterPipeline` | 必要保留（GPU 增强阶段拓扑宿主） |
| 21 | `src/core/playback_session_impl.h:46` | `struct MediaPlaybackSession::Impl` | Pimpl 私有状态机 |
| 22 | `src/core/wasapi_audio_output.cpp:76` | `struct WasapiAudioOutput::Impl` | Pimpl 私有音频状态机 |
| 23 | `src/gui/preview_viewport.h:62` | `class PreviewViewport` | 必要保留（12-DWORD 色彩契约 + DComp 硬裁剪 + SwapChain） |
| 24 | `src/gui/workstation_canvas.h:322` | `struct WorkstationApp` | 主窗口会话状态宿主 |
| 25–28 | `src/shim/loader/*.{cpp,h}` | `RtxBackend`, `RtxDecoder`, `RtxEncoder`, `DriverTable` | C ABI 硬件驱动垫片不透明句柄与函数表 |
| 29–34 | `src/shim/ngx_bridge/*.{cpp,h}` | `RtxNgxBridge`, `D3D12Interop`, `NgxD3D12Bridge`, `NvofEngine`, `RifeTrtLogger`, `RifeEngine` | 必要保留（D3D12/NGX/NVOF/TensorRT RIFE 硬件并发不变式） |

---

## 四、门禁核验状态（静态已核验 vs 宿主机待真机验证）

### 4.1 离线静态门禁（本轮已实测通过）
- [x] **括号/大括号语法配平**：10 个修改后的 `.h`/`.cpp` 文件全部 `braces=0, parens=0`。
- [x] **`AuditTypes` 增量门禁**：全仓具名类型 **`34 / 35`（增量 `+0`）**，零 `UiWidgetNode`，零孤儿 `@rtx-type-justification`。
- [x] **红线残留零命中审计**：`add_executable(rtx_headless_cli`、`ChromeText`/`chromeText`/`goldenPresetList`、`CINERTX_ENABLE_GPL_CODECS`、`build_ffmpeg_static_zig.sh`、`createHeadlessPreviewSink` 全仓匹配数均为 **`0`**。
- [x] **零 `.bak` 卫生门禁**：`CineRTX/` 源码树内无任何 `.bak`/`.orig`/`.tmp` 文件，旧版本均已移至 `work/_CineRTX_bak_archive/`。

### 4.2 宿主机真机门禁与后续波次待办（离线环境诚实留空 `[ ]`）
> 说明：当前执行环境为离线 Linux 沙箱，无 Windows 11 / MSVC 19.51 / `dumpbin` / RTX 5060 GPU，以下项严禁伪造日志，须在 Windows 宿主机实测勾核：

- [ ] **宿主机编译与单测**：运行 `.\build.bat` 验证 MSVC 19.51 `/std:c++latest /W4 /WX` 0 Error 0 Warning；运行 `rtx_tests.exe` 验证 **158/158 PASS**（含新增 `test_merge_concat_homogeneity_classification`）。
- [ ] **单 EXE 无头与 IAT 实测**：运行 `CineRTX.exe --headless --probe`（交互终端与管道重定向均正常输出），并运行 `cmake -DTARGET_BIN=.../CineRTX.exe -P cmake/AuditImports.cmake` 确认无 `rtx_headless_cli.exe` 残留。
- [ ] **FFmpeg 缓存重编（清零 `bcrypt.dll` + 静态 SVT-AV1/x265）**：清理 `.vs/build/external/ffmpeg-static` 后重新触发 `build_ffmpeg_static_msvc.sh`（使 `HAVE_BCRYPT=0` 生效），并用 `dumpbin /imports CineRTX.exe` 确认零 `bcrypt.dll`。
- [ ] **Wave B 余项**：在 `src/io/ffmpeg_io.cpp:L822-L841` 补齐 MP4 非直通音轨（`SwrContext` + `AVAudioFifo` -> AAC-LC）与 SRT/ASS/WEBVTT 字幕 -> `mov_text` 转码闭环及对应单测。
- [ ] **Wave C 余项**：将 `createRealtimePreviewSink` / `src/core/playback_session.cpp` 从直通升级为实时 GPU AI 增强环（12-DWORD 掩码直达 `PreviewViewport::presentShared`）、WASAPI 锁相与 8.0 ms 熔断单测。
- [ ] **Wave D 余项**：全量切换 D2D/DWrite 摘除 `gdi32.lib`、`LoadLibraryExW` 动态化 `user32.dll` 摘除 `user32.lib`、DOD 横向拖拽 DAG 画布（`+0` 类型）及 v0.5.0 封板。
