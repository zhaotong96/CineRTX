# 12 CineRTX Phase 13 重启版 Wave 0-4 全链路施工汇报

- **汇报编号**：Phase 13-12（重启版，基于 `v0.4.0` / `0ee04fd` 恢复基线）
- **依据施工单**：`docs/phase13/02`–`11` 号文档（Wave 0-A 至 Wave 4-B）
- **变更文件数**：33 个（旧版均已外置归档至 `work/_CineRTX_bak_archive/*.p13r2bak`，仓库内零 `.bak` 残留）
- **Git 状态**：严格遵守红线，未执行任何 `git commit` 或 `git push`

---

## 一、宪法四重门禁与离线实测实证

- [x] **零新增命名类型红线（`AuditTypes <= 34/35`）**：全仓 `include/` + `src/` 命名 `struct`/`class`（不含 `FnDeleter`、`enum class` 与前向声明）实测 **34 / 35**，严格 **0 新增命名类型**（所有新增数据结构均采用 `enum class` + `using ... = std::tuple<...>` 零类元组别名，见 `include/rtx/playback_session.h:L173`、`src/shim/ngx_bridge/rife_engine.h:L20`、`src/shim/ngx_bridge/rife_engine.h:L49`）。
- [x] **Core Engine Pure SLOC 红线（`<= 13,500`）**：`include/` + `src/core/` + `src/io/` + `src/cli/` 实测 **13,260 行**（余量 240 行，严格满足 `AuditSloc.cmake` 上限 `<= 13,500`）。
- [x] **SPDX 与隐私卫生红线（`AuditSpdx == 0` / `AuditPrivacy == 0`）**：全仓 `include/` + `src/` 缺失 SPDX 头 **0 处**；外部网盘链接、本地用户路径及沙盒路径泄露 **0 处**。
- [x] **静态 DLL 边界红线**：未向 `CMakeLists.txt` 或任何源码新增 `d3d12.lib`、`dxgi.lib`、`dcomp.lib`、`d2d1.lib`、`dwrite.lib` 静态链接依赖；GUI 与 Headless CLI 静态导入边界保持不变。
- [x] **离线 GCC C++23 全核心编译 + 10 套件 71 项单元测试真机实跑**：
  - 核心源文件全量 `-std=c++23 -Wall -Wextra` 语法校验 `rc=0` 通过。
  - 实机编译运行 10 个核心单元测试套件（`test_flicks_timebase`、`test_flicks_semantics`、`test_i18n`、`test_param_schema_reflection`、`test_geometry`、`test_hdr`、`test_lut3d`、`test_nle_vertical_slice`、`test_vram_scheduler`、`test_types`）：**71/71 PASS, 0 FAILED (370 ms)**。
  - 实测期间定位并彻底修复了 `1001` 倍 Flicks 时基下 `formatSmpteTimecode` 的 64 位乘法溢出缺陷（`include/rtx/time_model.h:L119`）。

---

## 二、逐波次施工清单与精确行号实证（`文件:L行号`）

### Wave 0-A（03 号文档）：宪法纠偏、卫生清剿与测试静音隔离

- [x] **构建脚本收口至 MSVC + Ninja**：移除 `cmake/toolchain-zig.cmake` 残留引用，统一收口为 `cmake --preset windows-msvc-ninja` 与 `cmake --build --preset windows-msvc-ninja`（`build.bat:L11`）。
- [x] **死分支清剿**：清理 `RtxEncoder` 中不可达的 `uhq_env` / `bframes_env` 死分支代码（`src/shim/loader/nv_encoder.cpp:L143`）。
- [x] **消除 C4324 警告抑制**：移除 `#pragma warning(disable : 4324)` 与触发警告的 `alignas(64)` 修饰，保留显式 64 字节缓存行填充数组（`src/core/wasapi_audio_output.cpp:L78`）。
- [x] **测试静音隔离（零新增命名类型）**：将 `PlaybackOptions` 元组扩展为 8 元素，新增 `bool null_sink` 与 `float audio_gain`（`include/rtx/playback_session.h:L173`）；`null_sink=true` 跳过物理 WASAPI 设备激活（`src/core/playback_session.cpp:L96`），`audio_gain <= 0.0f` 输出静音帧（`src/core/playback_session_audio.cpp:L194`）；全量同步 `test_playback_session.cpp` 测试用例为静音隔离配置（`tests/test_playback_session.cpp:L486`）。
- [x] **CLI 帮助文案单源收口**：替换手写 `printHelp()`，统一调用 `rtx::formatCliHelp()` 单源生成（`src/cli/main.cpp:L78`）。
- [x] **TODO 宪法裁决台账**：在 `docs/TODO/README.md` 追加 Wave 0-A 7 项历史遗留裁决表（#1 归入 Wave 3-A/B、#7 归入 Wave 1-B、#2–#6 归入 Phase 14）（`docs/TODO/README.md:L78`）。

### Wave 1-A（04 号文档）：D3D12-CUDA 互操作视口与预览总线修复

- [x] **根治 `CreateSwapChainForComposition` `0x887A0001` 黑屏故障**：
  - 净化 `DXGI_SWAP_CHAIN_DESC1` 为纯 `DXGI_USAGE_RENDER_TARGET_OUTPUT` + `DXGI_SCALING_STRETCH` + `DXGI_SWAP_EFFECT_FLIP_DISCARD`，并加入 `DXGI_ALPHA_MODE_IGNORE -> PREMULTIPLIED` 回退及十六进制 `HRESULT` 诊断日志（`src/gui/preview_viewport.cpp:L377`）。
  - 新增离屏 UAV 纹理 `ComPtr<ID3D12Resource> compute_target_` 与 `createComputeTarget()`（`src/gui/preview_viewport.h:L98`、`src/gui/preview_viewport.cpp:L328`），在 `presentShared()` 中执行 `Dispatch -> CopyResource -> Present` 完整资源屏障与拷贝管线（`src/gui/preview_viewport.cpp:L564`、`src/gui/preview_viewport.cpp:L660`）。
  - 加入零类 `std::unique_ptr` ticket 释放守卫与 `16ms` 延迟等待，保证 `resizeSwapChain()` 原子重建与 `dcomp_device_->Commit()` 同步提交（`src/gui/preview_viewport.cpp:L518`）。
- [x] **视口等比缩放与黑边渲染**：在 Compute Shader 中补齐源/目标宽高比 Letterbox/Pillarbox 映射数学与纯黑边填充（`src/gui/shaders/preview_tone_map_cs.hlsl:L109`）。
- [x] **Win32 视口闪烁、蜂鸣音与播放总线修复**：
  - 新增 `WM_SETCURSOR`（`HTERROR -> TRUE`）与 `WM_ERASEBKGND`/`WM_PAINT` 碳黑 `#0B0D0B` 背景绘制（`src/gui/preview_viewport.cpp:L63`、`src/gui/rtx_gui.cpp:L953`）。
  - 修复 `togglePlayback()` 在会话为空时自动从 `IDC_EDT_INPUT` 拉起会话且不错误翻转 `isPlaying` 状态（`src/gui/workstation_canvas.cpp:L1021`）。
  - 修复 `presentSlotToViewport()` 仅在 `!lastPresentSkipped()` 时推进 `last_slot` 游标，防止丢帧卡死（`src/gui/workstation_canvas.cpp:L1077`）。
- [x] **Live D3D12 视口实测用例**：追加 `gui_preview_viewport_composition_swapchain_live` 实时集成测试（`tests/test_gui_workstation.cpp:L270`）。

### Wave 1-B（05 号文档）：全容器输入扩展、SVT-AV1/x265 软编 CRF 与 MP4 兼容交付

- [x] **FFmpeg 静态裁剪全容器扩展**：扩展 `--enable-demuxer`（`asf,mpegps,rm,ivf,webm,dv,gif`）、`--enable-parser`（`vp8,mpegvideo,vc1`）与 `--enable-decoder`（`h264,hevc,av1,vp8,vp9,mpeg2video,mpeg4,vc1,wmv3`）（`cmake/RtxFfmpegStatic.cmake:L379`）。
- [x] **MP4/MOV 边下边播兼容性**：在 `avformat_write_header` 对 `.mp4`/`.mov` 输出自动注入 `movflags=+faststart`（`src/io/ffmpeg_io.cpp:L867`）。
- [x] **SVT-AV1 与 x265 软编正交合并**：在 `CpuVideoEncoder` 中支持 `VideoCodec::AV1`（`libsvtav1`，`rc=0`，`crf 0..63`，`preset 1..12`）与 `VideoCodec::HEVC`（`libx265`，`crf 0..51`）双路正交路由（`src/io/cpu_video_encoder.cpp:L59`）。
- [x] **参数 Schema 与 GUI 文件选择器打通**：在 `param_schema.h` / `param_schema.cpp` 解锁 `av1_cpu`/`hevc_cpu` 及 `crf` 参数（NVENC 归一化 `crf -> cq`，CPU 保留原生 `crf`，`is_cpu` 正确下传至 `profileToHwOptions`）（`include/rtx/param_schema.h:L99`、`src/core/param_schema.cpp:L184`）；GUI `kBrowseSpecs` 扩展至全部 16 种受支持容器（`src/gui/rtx_gui.cpp:L407`）。

### Wave 2-A & 2-B（06–07 号文档）：四画幅桶分立单 Profile RIFE 引擎规范与对称反射

- [x] **四画幅桶物理分立单 Profile 规范（`[DEC-011]` / `[ISSUE-005]`）**：零新增命名类型，新增 `enum class RifeAspectBucket`（`16x9` / `9x16` / `21x9` / `1x1` 四分立桶）、`classifyRifeBucket()`、`rifeBucketEngineFilename()` 与对称 Pad-to-128 的 `RifeGeometryDesc` / `calculateRifeGeometry()`（`src/shim/ngx_bridge/rife_engine.h:L20`、`src/shim/ngx_bridge/rife_engine.h:L49`）。
- [x] **CUDA 四向对称 `BORDER_REFLECT_101` 边界反射**：更新 `reflect101i()` 支持左/上负索引与右/下越界索引的完整对称镜像反射，消除补帧黑边伪影（`src/shim/ngx_bridge/rife_pack.cu:L37`）。

### Wave 3-A & 3-B（08–09 号文档）：1001 因子精确时基与异构多任务队列隔离

- [x] **1001 扩展 Flicks 时基（`706,305,600,000 Hz`）**：升级 `kFlicksPerSecond = kBaseFlicksPerSecond * 1001ULL`，补齐精确 NTSC 常量 `kFlicksNtsc23976` / `kFlicksNtsc2997` / `kFlicksNtsc5994` 与零余数校验函数 `isZeroRemainderFlicks()`（`include/rtx/flicks.h:L17`、`include/rtx/flicks.h:L20`）。
- [x] **64 位无溢出时间重标定与 SMPTE 格式化**：
  - `rescaleQ()` 升级为 64 位 `std::gcd` + `av_rescale_rnd`，消除 32 位 `AVRational` 截断（`src/core/rational.cpp:L145`）。
  - `formatSmpteTimecode()` 改为商余拆分的无溢出 64 位精确整除，24 小时及 1000 年级时长均零溢出、零浮点误差（`include/rtx/time_model.h:L119`）。
  - 替换 `hw_transcode_stages.cpp` 中硬编码 `7840` 为 `kFlicksPerSecond / 90000ULL`（`src/core/hw_transcode_stages.cpp:L471`）。
- [x] **异构多任务队列保护与错误闭环**：重构 `onStartTranscode()` 保留既有 `Pending` 异构任务快照（不再覆写已有待执行任务），支持空输出路径自动补全、输入文件存在性校验与 `enqueueJob` 返回值 `MessageBoxW` + `logError` 显式报错（`src/gui/rtx_gui.cpp:L338`）；追加 `gui_job_queue_enqueue_and_worker_real_transcode` 测试（`tests/test_gui_workstation.cpp:L321`）。

### Wave 4-A & 4-B（10–11 号文档）：无 DLL 污染布局排版、系统语言检测与测试同步

- [x] **自动系统语言检测**：新增 `detectSystemLanguage()` 通过 `GetUserDefaultUILanguage()` 自动识别中文/英文系统环境（`src/core/i18n.cpp:L434`）。
- [x] **纯 GDI 零 DLL 污染折行与防裁切布局**：新增 `kWrapDrawFlags` 与 `measureWrappedTextHeight()`（`src/gui/workstation_canvas.h:L439`）；采用 `BeginDeferWindowPos` 批量重排控件，限制 `dynRowH <= u(40)` 并设置页脚安全下限 `maxFooterTop`，杜绝高 DPI 与中文宽文本裁切重叠（`src/gui/rtx_gui.cpp:L196`、`src/gui/rtx_gui.cpp:L215`、`src/gui/rtx_gui.cpp:L223`）。
- [x] **全套单测基线对齐**：同步 `test_flicks_timebase.cpp`、`test_flicks_semantics.cpp`、`test_i18n.cpp`（125 项 `StrId`）与 `test_param_schema_reflection.cpp`（20 项 SSOT 字段 + CPU CRF 解锁），离线实跑 71/71 全绿通过。

---

## 三、待真机验证与后续深水区诚实披露

以下项目受限于当前 Linux 无 GPU 离线沙盒环境或属于需独立硬件实测的深水区模块，**未勾选 `[x]`**，需在 Windows 11 + MSVC + RTX 5060 真机环境继续验证或分期收口：

- [ ] **Windows 11 MSVC + Ninja 全仓真机构建与 CTest 全量回归**：在宿主机执行 `build.bat` 验证 `rtx_gui.exe` / `rtx_headless_cli.exe` 静态 IAT 导入表（GUI `<= 4` DLLs、Headless `== 2` DLLs），并实跑 `gui_preview_viewport_composition_swapchain_live`、`gui_job_queue_enqueue_and_worker_real_transcode` 与 `test_playback_session`。
- [ ] **Wave 2-A/2-B 宿主机 GPU 深水区**：在 RTX 5060 上实际离线构建 4 桶单 Profile TensorRT 引擎（`rife_16x9.engine` / `rife_9x16.engine` / `rife_21x9.engine` / `rife_1x1.engine`），并完成 NVOFA 双向光流抗果冻 CUDA Kernel 与 `shot_segmenter` 切镜总线全链路实片验收。
- [ ] **Wave 4-B 交互式横向可拖拽 DAG 画布与动态 Direct2D/DirectWrite 渲染**：本次优先保证了零新增命名类型与零新增静态 DLL 边界下的基础布局与预览稳态；完整的动态加载 D2D1/DWrite 节点拖拽 DAG 画布及预览算力门控交互建议作为 Phase 13 后续独立迭代专项推进并配齐真机 UI 录屏验收。
