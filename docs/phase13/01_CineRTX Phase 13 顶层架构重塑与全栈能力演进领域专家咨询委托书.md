# CineRTX Phase 13 顶层架构重塑与全栈能力演进领域专家咨询委托书

- **文档编号**：`CINERTX-P13-DELEGATION-001`
- **归档路径**：[`docs/phase13/01_CineRTX Phase 13 顶层架构重塑与全栈能力演进领域专家咨询委托书.md`](./01_CineRTX%20Phase%2013%20顶层架构重塑与全栈能力演进领域专家咨询委托书.md)
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）
- **受呈报方**：多媒体系统工程、实时图形合成与异构计算首席领域专家组（Chief Domain Experts & Architecture Board）
- **当前基线**：Commit `0ee04fd` / Tag `v0.4.0`（Phase 12 极限现代 C++26 重构与无头 DLL 净空封板态）
- **实测门禁状态**：`AuditTypes = 34/35`、`AuditSloc Core = 12,157 / Full = 31,160`（`150` 文件）、`AuditPrivacy = 0 violations`（`394` 文件）、`AuditImports` GUI 静态物理 DLL `<= 4` / HEADLESS `== 2`
- **最高委托原则**：**严禁先拍脑袋局部打补丁；必须先完成最高层级系统调研与全局程序架构设计（Unified Top-Level Architecture），再下沉展开各子系统具体设计与分波次施工落地。**

---

## 零、 委托背景与不可逾越的架构宪法约束

在 Phase 12（`v0.4.0`）收官后，CineRTX 在核心类型收口、纯 CMake 静态门禁与静态导入表净空方面已建成极端严苛的工程防线。然而，围绕从“离线单功能转码演示工具”迈向“完整桌面/无头全媒体创作与实时增强工作站”的 Phase 13 核心需求，项目团队在真实使用与审查中集中暴露并梳理出 **13 项互相耦合的深层架构诉求与缺陷**。

这些诉求覆盖媒体画幅泛化、实时视口与播放器链路崩坏、60fps 实时 AI 增强、自研 UI 组件库边界、全栈 i18n、多容器+CPU CRF 软编（固定输出 `.mp4`）、异构合并任务队列、可拖拽自定义管线图、光流切镜果冻、测试异常蜂鸣定因、历史开关卫生清剿以及 `docs/TODO` 收编。

为避免“头痛医头、各修各的”导致 `AuditTypes`、`AuditImports` 与“零开关”铁律瞬间崩溃，现委托首席领域专家组遵循以下刚性约束启动**顶层调研 + 总体架构设计**：

| 约束维度 | 当前锁定红线（只读核验基线） | Phase 13 顶层设计面对的张力 |
| :--- | :--- | :--- |
| **具名类型预算 (`cmake/AuditTypes.cmake`)** | 全仓 `include/`+`src/` 具名类/结构体上限 **35 个**，实测已用 **`34 / 35`**（仅余 **1 个**名额，`[DEC-009]`） | 新增 UI 组件模型、DAG 管线图、合并队列模型、增强播放会话若无顶层表驱动/值语义治理或经裁决的规范重划，必然撞穿门禁 |
| **PE 静态 IAT 净空 (`cmake/AuditImports.cmake`)** | GUI 静态导入物理 DLL `<= 4`（`kernel32, user32, gdi32, bcrypt`）；**黑名单 10 项物理 DLL 禁止静态导入**（`shell32, dwmapi, comdlg32, comctl32, uxtheme, d3d12, dxgi, ole32, kernelbase, advapi32`）；HEADLESS `== 2`（`[DEC-018]`） | 现代化 Win32 组件库如何“能复用 Windows UI 则复用，复用不了则高水准定制”，且绝不破坏静态 IAT 与无头双模架构 |
| **TRT 单 Profile 解耦铁律 (`[DEC-011]` / `[ISSUE-005]`)** | 严禁在单 `.engine` 内搞多 Optimization Profile 动态切换（Phase 11.4 实证曾致 8.89 GB 显存暴涨与 `<1fps` 坍塌）；缩放须配合真实 CUDA 核函数（`[DEC-012]`） | RIFE 适配 21:9/4:3/1:1 等任意宽高比及播放器多片切换时，严禁走回多 Profile 共享引擎老路，必须构建合规替代拓扑 |
| **代码规模护栏 (`cmake/AuditSloc.cmake`)** | Core Engine Pure SLOC 上限 **`23,000`**（当前 `12,157`，尚余约 `10.8k` 空间） | 禁止膨胀无用的 OOP 样板代码；须用紧凑数据表与正交自由函数维持高信息密度 |
| **许可证与专有隔离 (`docs/spec/compliance.md`)** | 主程序 `GPL-3.0-or-later WITH Plugin-Exception`；专有 NGX/TensorRT 严格物理隔离在 `rtx_ngx_bridge.dll` 动态边界后（`[DEC-010]`） | 引入 CPU 软编码库（如 `libsvtav1` / `libx265` 等）与更多容器解复用器/解码器时，须严审许可证兼容性、静态体积包络与零动态 DLL 泄露 |
| **奥卡姆剃刀与“零开关三反思” (`docs/TODO/README.md`)** | 严禁新增环境变量后门、幽灵编译开关、隐藏降级旁路 | 对现有 `CMakeLists.txt`、编译选项、全局状态、废弃字段及新需求的显式开关做一次彻底收编 |
| **隐私安全门禁 (`cmake/AuditPrivacy.cmake`)** | 全仓 0 违规，全部脱敏严格遵循 `[REDACTED_*]` | 后续全套文档与源码严禁出现本机路径、盘符、账号或测试主机痕迹 |

---

## 一、 13 项核心问题的只读代码取证与因果剖析

以下全部结论均来自对当前 `v0.4.0`（HEAD）仓库源码与构建脚本的直接只读核查，供专家组作为顶层调研的事实底座。

### 议题 1：RIFE 固定 16:9 尺寸锚定与任意宽高比视频适配失效

- **实证代码坐标**：
  - `cmake/EnsureRifeEngine.cmake:85-86`：构建期硬编码生成 `rife_v4.25_landscape.engine (1920x1152)` 与 `rife_v4.25_portrait.engine (1152x1920)`。
  - `src/shim/ngx_bridge/rife_builder.cpp:444-458`：采用单一 TensorRT Optimization Profile；仅按 `is_portrait = (height > width)` 二分，`OPT` 锁死在 `1920x1152` 或 `1152x1920`，`MAX` 锁死在 `3840x2176`（横屏）与 `2176x3840`（竖屏）。
  - `src/shim/ngx_bridge/rife_engine.cpp:161-162, 366-404`：运行时仅按 `height > width` 匹配上述两个 16:9 引擎；对齐步长为 `align_mod = 128` 并仅在右侧/下侧镜像填充（`reflect101i`）。
  - `src/core/hw_transcode_stages.cpp:92-101`：`scale=0.5f` 仅在 `!use_fg_first && (rife_w >= 2560u || rife_h >= 2160u)` 触发；若存在放大（`is_spatial_upscale`），强制走 `fg-first` 并锁定 `scale=1.0f`。
- **深度因果问题**：
  1. **硬报错拒止**：对 `4:3`（如 `3072x2304` -> `aligned_h = 2304 > max_h 2176`）、高分辨率 `1:1`（如 `2560x2560` -> 无论选横屏还是竖屏均超出 `2176` 上界）、以及超宽带鱼屏 `21:9` / `32:9`（如 `5120x1440` -> `> max_w 3840`），单 Profile `MAX` 范围直接越界失败。
  2. **TensorRT 算子调优退化**：Single Profile 的 `OPT` 被强锚定在 16:9，非 16:9（如 4:3、1:1、21:9、4:5、异比竖屏）张量形状下，Myelin 编译出的 Kernel 战术与工作空间并非最优，可能导致时延飙升或显存峰值暴增。
  3. **多分辨率/变画幅切换僵化**：同一会话/队列合并/播放器切换不同画幅视频时，缺乏自适应多档 Profile、内部降采样倍率自适应及分块（Tiling）/等比内缩放保护策略。

### 议题 2：单测 `[ RUN ] playback_session_native_decode_h264_hevc_av1` 附近“嘟嘟嘟”异响的法医定因

- **实证代码坐标**：
  - `tests/fixtures/README.md:8-14`：仓内三份测试媒体（`h264_25fps_gop12.mkv`、`hevc_25fps_gop12.mkv`、`av1_25fps_gop12.mkv`）全部内含一条真实 `440 Hz AAC mono` 测试音轨。
  - `tests/test_playback_session.cpp:486-487`：紧邻的前一个用例 `playback_wasapi_shared_mode_master_clock` 在 Phase 11.3-R 曾加注注释“*写入 0.0f 全静音缓冲（-inf dB），完全消除单测执行时的 440 Hz 扬声器嘟嘟声*”。
  - `tests/test_playback_session.cpp:544-646`：然而 `playback_session_native_decode_h264_hevc_av1` 在第 563 行构造了 `PlaybackOptions{Rational{25, 1}, 1.0, 5u, 0ull, 0, true}`（`enable_audio = true`！），并对 H.264/HEVC/AV1 三份夹具高速连续执行 `play() -> Ended -> play() -> pause() -> seek() -> stepFrame(+1/-1) -> setSpeed(2.0/0.25) -> seek()`。
  - `src/core/playback_session_audio.cpp:194-197`：`mixAvx2` 增益写死 `1.0f`，直接打开 Windows 默认 WASAPI 共享输出设备将真实解码出的 `440 Hz` 音频泵向系统扬声器。
- **因果定论与复核要求**：
  - 所谓“嘟嘟嘟”并非底层内核输入缓冲区溢出，而是单元测试真实驱动 WASAPI 硬件声卡循环播放并被频繁 `seek/flush` 截断的 `440 Hz AAC` 正弦波物理鸣响；同时也暴露出 `PlaybackOptions` 与 `WasapiAudioOutput` 缺少**虚拟静音渲染沉没器（Null Audio Sink / Volume 0 / 测试隔离通道）**的架构缺失。
  - 请专家在方案中同时列出物理主机验证核查点，确认除 `test_playback_session.cpp` 外，GUI/消息循环单测（`test_gui_workstation.cpp`）等是否还存在触发 Windows 系统 `MessageBeep` 的隐蔽路径。

### 议题 3：转码预览窗口完全不显示与播放器功能彻底瘫痪的多重断链

只读穿透排查揭示这不是单点 Bug，而是 Phase 11/12 重构遗留的 **四重系统性断链**：

1. **断链 1（转码预览在队列入口即被物理掐断）**：
   - `src/core/job_queue.cpp:258-264`：`EncoderJobQueue` 构造 `HwTranscodeOptions opts = profileToHwOptions(prof);` 时**从未赋值 `opts.preview`**（全仓搜索 `opts.preview` 为 0 命中，`opts.preview` 默认空 `std::function`）。
   - `src/core/hw_transcode_stages.cpp:344`：`if (options_.preview && session_->previewReady())` 仍旧沿用废弃回调开关做门控，导致条件恒为 `false`，`in.preview_rgba10_ptr` 恒为 `0`，整个转码链路一帧都未写入 `preview_ring_`！
2. **断链 2（预览消费者空读未写槽位 + 尺寸/色度硬编码错位 + 首帧尺寸失步）**：
   - `src/core/ngx_remaster.cpp:204-212`：`publishActivePreviewBridge` 在首帧产出前就把 `preview_latest_slot_` 设为 `0`，GUI 30Hz `WM_TIMER`（`rtx_gui.cpp:878` -> `workstation_canvas.cpp:1051-1066`）无从区分“真实发布帧”与“未写入初始状态”，产生虚假借阅后停滞。
   - `src/shim/ngx_bridge/bridge_exports.cpp:250` 硬分配 `960x540` 共享缓冲；`workstation_canvas.cpp:1098` 硬编码 `960, 540, input_is_pq=true, max_nits=100.0f`（忽略竖屏、21:9 及真实 `peak_nits`）。
   - `src/gui/shaders/preview_tone_map_cs.hlsl:109-110` 直接将整个视口 UV 拉伸采样填满交换链 `dstSize`，缺失 Letterbox/Pillarbox 等比维持逻辑。
   - `src/gui/rtx_gui.cpp:558-569` + `src/gui/preview_viewport.cpp:449-452`：`WM_CREATE` 期间建立 `PreviewViewport`（初始 `960x540`，尚无 D3D12 设备与 `swap_chain_`），随后 `applyWorkspaceLayout`/`WM_SIZE` 触发 `resizeSwapChain` 时因 `!swap_chain_` 直接 `return`；直到首次 `presentShared -> adoptDevice` 时才按过期的 `960x540` 重建交换链，导致布局几何与底层交换链长期失步。
   - `src/gui/rtx_gui.cpp:876-878`：30Hz 定时器不区分当前活跃工作区或任务态，同时盲目轮询播放与转码呈现。
3. **断链 3（Win32 播放器 UI 无接线、无入口、“假装播放”）**：
   - `src/gui/rtx_gui.cpp:166-174` & `workstation_canvas.cpp:910-964`：Player 工作区根本没有“打开媒体”按钮，仅在非 Encoder 页拖放文件（`rtx_gui.cpp:635`）才触发 `startPlaybackSession`；从 Encoder 切到 Player 页（`line 227-238, 719`）或通过浏览按钮（`line 456-472`）选片均**不会**初始化播放会话。
   - `src/gui/workstation_canvas.cpp:1021-1033`：`togglePlayback` 在 `playbackSession == nullptr` 时仍然翻转 `isPlaying` 并改写按钮文本/日志，纯属空壳假播；且 NLE Bin 选片（`rtx_gui.cpp:734-750`）同样未绑定视口回放解码。
4. **断链 4（WebUI 预览与播放器协议割裂）**：
   - `webui/index.html:246, 326-327` & `webui/app.js:1429-1448`：Encoder 与 Player 视口均挂接原生 HTML5 `<video>` 元素（且 `encoder-video` 在转码过程中无任何实时帧数据源），现代浏览器本身无法原生解码 10-bit 4:2:0 HDR HEVC/AV1（尤以 MKV/TS/MXF 及未封装完输出为甚），也完全无法呈现后端实时的 RTX VSR + TrueHDR + RIFE A/B 分屏对比画面。

### 议题 4：播放器正式化、60fps 实时播与 RIFE 动态协同的深层架构挑战

- **实证代码坐标**：
  - `src/core/playback_session_video.cpp:151-167` & `src/core/ngx_remaster.cpp:111-203`：`createHeadlessPreviewSink` 仅将 NVDEC 解码出的原始帧直接交由 `rtx_yuv_to_rgb10_buf` 打包为 R10G10B10A2 共享缓冲送呈视口。**当前播放路径 0% 接入 VSR、0% 接入 TrueHDR、0% 接入 3D LUT、0% 接入 NVOFA/RIFE 帧率倍增**。
  - `src/gui/workstation_canvas.h:79-88` & `rtx_gui.cpp:266-286`：GUI 计算出的 `active_vsr_target()` 仅打印在 OSD 文本上，从未生效于播放管线。
- **核心设计命题**：
  - 如何把播放器从“玩具解码演示”升格为**硬实时 60fps AI 增强播放器**：保证在 `16.67 ms` 帧周期内稳定跑完 `NVDEC -> ShotCut Guard + FRUC(60fps NVOFA/RIFE) -> VSR(跟随视口缩放自适应) -> TrueHDR + 3D LUT -> D3D12 Compute ToneMap -> DComp SwapChain`；
  - 如何解决固定优化尺寸 RIFE 模型在实时播片中的冷启动/切分辨率卡顿、Seek/正逆步进（`StepForward`/`StepBackward`）后的流式上下文失效重建、以及音频 `WasapiAudioOutput` 主时钟与 FRUC 插值帧时间轴（尤其 23.976/24/25 -> 60fps 分数倍率 `alpha` 与抖动抑制）的精密同步。

### 议题 5：Win32 UI 组件库重写与 Windows 原生复用边界

- **实证代码坐标**：
  - `src/gui/rtx_gui.cpp:186-218`：右侧检查器使用固定 `lblW = u(120), rowH = u(26), gap = u(2)` 的扁平坐标循环；`workstation_canvas.cpp:891-964` 全靠标准 `STATIC`/`EDIT`/`BUTTON`/`COMBOBOX`/`LISTBOX` 拼装，导致中文/英文稍长文本（如动态切换语言后标签）截断，完全无 `DT_WORDBREAK` 多行高度测算、无垂直滚动容器（小屏或增项必溢出裁切）、无矢量圆角卡片/开关/高级进度与拖拽反馈。
- **架构要求**：
  - 在严守 `AuditImports <= 4`（禁用静态链入 `comctl32/uxtheme/d3d12/dxgi/ole32`）和类型门禁的前提下，由专家组制定一套清晰的分层组件决策矩阵：**哪几类交互必须通过 `sys_dynload` 复用 Windows 原生体系**（如系统文件选择器、Taskbar 进度、系统字体/DPI/ClearType、DirectWrite/Direct2D、DWM 暗色/圆角、原生编辑框与 IME 输入法宿主），**哪几类必须脱离老旧 Win32 控件彻底定制**（支持自动换行与内容撑开高度的 `MeasureText` 文本体系、带滚动/折叠分组的 Inspector、现代 Toggle/Combo/Slider、批处理队列项视图、横向流水线节点编辑器），并彻底重构布局管线。

### 议题 6：全栈 i18n（国际化/本地化）系统性设计缺口

- **实证代码坐标**：
  - `include/rtx/i18n.h` & `src/core/i18n.cpp`（125 词条 `.rdata` 表），但 `src/gui/workstation_canvas.cpp:206-241` 另行私藏第二套 27 词条的 `chromeText()` 硬编码英文/中文双分支表，造成维护双裂。
  - 状态栏与列表标签仍充斥大量硬编码英文拼接：`rtx_gui.cpp:814-817`（`Batch .../Progress/Speed`）、`workstation_canvas.cpp:477`（`"NVIDIA shim probe"`）、`519-531`（`[Pending]/[Running]/[Done]/[Failed]`）、`591-615`（NLE 导出失败日志）。
  - `src/cli/main.cpp:75-116` 的 `printHelp()` 硬编码英文并与 `param_schema.cpp::formatCliHelp()` 并行漂移；`webui/app.js:21-330` 另行写死一套庞大字典并与后端 `GET /api/schema` 合并补丁。
  - **结构性缺失**：缺乏位置参数插值模板（中/英/日/德等语序不同）、复数与度量单位本地化、无语言切换后的控件文本动态重测算+重布局回流（导致换语言后控件切字）、无解耦语言包扩展机制。

### 议题 7：全格式容器输入支持 + 恢复 CPU 软编码（CRF 等）+ 物理锁死输出 `.mp4`

- **实证代码坐标**：
  - `cmake/RtxFfmpegStatic.cmake:379`：静态 FFmpeg 目前仅启用 `demuxer=mov,matroska,mpegts,flv,avi,mxf,aac,mp3,flac,ogg,wav,w64`，未覆盖 `asf`（WMV）、`mpegps`（VOB/MPG）、`rm`、`ivf`、`webm` 等常见输入，且解码器与 Parser 白名单需核查对老旧编码（MPEG-2, VC-1, VP8 等）的硬解（NVDEC 支持列表）或必要轻量化软解回落映射（`src/shim/loader/nv_decoder.cpp:47-58` 当前仅映射 H264/HEVC/AV1）。
  - `src/gui/rtx_gui.cpp:357, 475`：文件筛选器写死 `*.mp4;*.mkv;*.mov;*.ts`。
  - `src/core/param_schema.cpp:184-209, 291`：**现行代码中物理阻断了 CPU 软编**：显式拦截 `av1_cpu` / `hevc_cpu` 返回 `Unsupported`，将 `crf` 偷换映射为 `cq`，并在 `profileToHwOptions` 写死 `const bool is_cpu = false;`；尽管仓内保留 `src/io/cpu_video_encoder.h/.cpp`，但 `RtxFfmpegStatic.cmake` 已禁用所有 CPU 视频编码器。
- **确定性产物边界**：
  - 按产品定案：**输出封装容器一律固定为 `.mp4`**。专家须深度解决：**当输入含非 MP4 原生兼容流（如 PGS/ASS 图像/特效字幕、FLAC/Opus/PCM/DTS 音轨、VFR 动态帧率、HDR10+ 元数据与 Dolby/BT.2020 盒子封装）时，如何制定确定性的封装兼容性矩阵（直通 vs 精确转码 AudioEncoder vs 合规拒绝提示），并确立 CPU 软编码库（`libsvtav1` / `libx265` 10-bit）在许可证、静态编联体积与 GPU->CPU 安全下传（唯一允许明确语义的 D2H 编码出口）上的完整架构**。

### 议题 8：Encoder 异构任务队列与多文件合并（Batch vs Merge）彻底重塑

- **实证代码坐标**：
  - `include/rtx/job_queue.h`：`EncoderJob` 仅记录单份 `TranscodeProfile` 与一维 `std::vector<path> inputs`，语义被死绑为“同一套参数顺次批处理生成多份 `_remaster_<codec>.mp4`”。
  - `src/gui/rtx_gui.cpp:312-323`（**灾难性设计缺陷**）：`onStartTranscode` 会在点击“开始转码”时遍历**所有 `Pending` 任务，用当前右侧检查器 UI 状态暴力覆盖整条队列的独立参数**，直接毁掉异构参数隔离性！
  - Win32 Queue UI 仅为单行字符串 `LISTBOX`，既不能为单一任务增减局部文件与调整顺序，也不能把选中任务的参数独立回显进检查器修改而不影响草稿/其他任务。
- **新模型核心需求**：
  - 每个任务单元须独立拥有：独立文件列表（数量 `>= 1` 且支持动态增删、排序）、独立完整编码/管线配置、以及核心二选一动作模型——**「独立逐一导出」vs「无缝拼接合并为单一 `.mp4`（Concat/Merge）」**。
  - 当同任务下多文件合并且视频分辨率/帧率/色彩/声道配置不一致时（异构片段合并），如何依托现有的 `RenderGraph` / 多段不断流引擎实现确定性的归一化与无缝时间轴拼接（音频采样率/声道重采样对齐、GOP/IDR 强制边界、NTSC/CFR 时间戳无漂移连续合成）。

### 议题 9：转码预览控制复选框（按需开启 / 暂停节省算力）

- **现状**：目前因断链彻底不可预览，一旦恢复后也必须可控。
- **要求**：在转码界面/会话参数中提供瞬态可控的预览开关（Checkbox），勾选时实时零回读呈现，取消勾选时从 GPU Pack Kernel、共享环槽位占用到 30Hz Compute Dispatch 与 DComp 提交**全链路真暂停/旁路释放资源**，同时画面/状态给予明确静默冻结或占位反馈，且不影响并行的编码吞吐。

### 议题 10：类 PR 管线横向可拖拽排序工作流卡片 UI 与可控 DAG 执行引擎

- **实证代码坐标**：
  - 目前仅靠静态枚举 `pipelineOrder = "vsr-first" | "fg-first"`，且 `src/core/hw_transcode_stages.cpp:92-95` 在放大场景会**悄悄无视用户选项**强制改为 `fg-first`。
  - 现有 3D LUT、TrueHDR（SDR->HDR10 PQ）、VSR（超分）、FRUC（NVOFA / RIFE 插帧）被硬编码死锁在有限几个 C++ 函数与 CUDA 内核先后顺序中。
- **设计诉求**：
  - UI 层：打造类 Premiere 效果管线（横向排列的方块节点卡片流），支持**拖拽重排算子执行顺序**、单节点旁路禁用、卡片上直观呈现该节点处的张量分辨率与帧率变换（如 `1080p@24 -> [RIFE 60] -> 1080p@60 -> [VSR L4] -> 4K@60 -> [TrueHDR] -> [3D LUT]`）。
  - 内核与合法性约束：并不是所有排列都具物理意义或高效（例如：在 SDR 输入先跑 TrueHDR 转 BT.2020 PQ 再送入只受 SDR 输入假设或不同色域期望的 VSR/RIFE 算子会发生什么？放大之后再跑 NVOFA/RIFE 与先插帧再超分的显存/算力倍率差几倍？），专家须给出**节点契约表（前置色域/格式要求 + 自动插拔转换核 + 非法拓扑吸附/防呆阻断规则）**，将现有“隐式覆盖逻辑”改造为公开透明、物理自洽的管线编排架构。

### 议题 11：光流（NVOFA）场景切换/黑场果冻效应重灾区 vs RIFE 切镜保护鸿沟

- **实证代码坐标**：
  - `src/shim/ngx_bridge/rife_engine.cpp:640-671` & `src/core/hw_transcode_stages.cpp:423-428`：RIFE 路径已接入 8x8 分块 SAD 检测（`launchEvalBlockSad8x8` + `evalBlockSadSceneCut` 自适应 EMA），切镜时立即走近端原帧硬切拷贝并向后级输出 `force_reset_temporal=true`（重置 VSR 时域历史并强制 NVENC IDR 关键帧）。
  - `src/core/hw_transcode_stages.cpp:446-459` & `src/shim/ngx_bridge/bridge_exports.cpp:674-723`：**NVOFA 路径零切镜检测、零黑场检测、零回调信号**！更讽刺的是，`hw_transcode_internal.h:242` 声明了 `ShotGroupSegmenter segmenter_` 且在 `hw_transcode_stages.cpp:79` 执行了一次 `resetShotSegmenter`，**随后整条转码主循环中一次也未调用过！**
- **深度诉求**：
  - 彻底将切镜检测、黑场渐变识别（Fade-to-Black / Dissolve 区分于硬切 Cut！）上升为 **RIFE、NVOFA 与未来播放器的公共前置/旁路 GPU 检测原语**，打通“多阶切镜/闪帧/黑场判别 -> 双路统一硬切退避 + 时间采样点重锚定 + VSR 时域彻底 Reset + 编码器 IDR 注入”的闭环，彻底根治光流场景切换果冻拖影。

### 议题 12：编译开关、隐藏幽灵开关与项目卫生体系全面清查

只读扫描已抓取下列违背“奥卡姆剃刀/统一确定路径”的典型卫生隐患（提请专家做全量地毯式扫除规划）：
1. **构建链工具预设分裂**：`build.bat:68` 硬编码走 `cmake/toolchain-zig.cmake`，而 `README.md` / `PackageRelease.cmake` 主推 `windows-msvc-ninja`（MSVC `/std:c++latest`）。
2. **源码残余死开关/占位**：`src/shim/loader/nv_encoder.cpp:241, 277` 残留 `constexpr const char* uhq_env = nullptr; / bframes_env = nullptr;` 死分支。
3. **CMake 双重逻辑与冗余编译接线**：`CMakeLists.txt:200-244` 同时在配置期与构建期存在 PTX 生成规则；`rtx_tests` 与 `CineRTX` 在头文件路径与源文件直包边界混杂（如直接把内部 `.cpp` 塞入 `rtx_tests` 而非干净库边界）。
4. **双源帮助文本漂移与私藏表**：`src/cli/main.cpp:75-116` 的 `printHelp()` 硬编码独立于 `param_schema.cpp::formatCliHelp()`；GUI 存在第二份 i18n 表 `chromeText()` 与硬编码 `OptCodecH264` 废旧词条残余。
5. **跨层全局可变状态泄漏**：`ngx_remaster.cpp:105` `activePreviewMonitor()` 全局原子单例指针、`workstation_canvas.h:79-88` 全局静态 `active_vsr_target()`（且计算结果悬空未接入播放器）。
6. **局部告警掩蔽与悬浮字段**：`src/core/wasapi_audio_output.cpp:74` `#pragma warning(disable : 4324)`、`PlaybackOptions` 与 `TranscodeProfile` 多处保留无效/未接线字段、`hw_transcode_stages.cpp:79` 孤悬未用成员 `segmenter_`。

### 议题 13：`docs/TODO` 残留项全面盘点与 Phase 13 收编裁决

对照 `docs/TODO/README.md` §3 备案的 7 项 `v0.3+` 远期展望：
- **第 7 项（CPU 软编 + CRF 恒定质量码控）**：已被用户直接提升为 Phase 13 第 7 项必选硬需求。
- **其余 6 项（NTSC `1001` 时基因子扩展、FFmpeg 8.x 演进、Nsight Systems 线级证据、HDR10 物理徽标、2 小时 Soak 长稳、Linux 无头形态）**：提请专家逐项审定在 Phase 13 的处置去留——尤其**第 1 项（NTSC `1001` 时基因子扩展）**与本次第 4 项（播放器 60fps 帧对齐）及第 8 项（多片合并时间轴无累积误差）强相关，需决定是否在 Phase 13 架构层一并解决；并重修 `docs/TODO/README.md` 消除陈旧与漂移。

---

## 二、 提请首席专家组给出的顶层调研与架构设计核心考题

**【特别指令】**：请专家组按**“最高层级调研 $\rightarrow$ 总体程序架构设计 $\rightarrow$ 各模块具体实现方案”**的三层金字塔逻辑作答，严禁直接堆砌零散补丁代码。重点围绕以下六大架构级考题给出带有深度推演、定量取舍与严密边界的终极设计：

### 考题 A：最高层级行业调研与架构范式选型（Phase 13 底座对齐）
1. **具名类型与现代 C++ 数据流范式裁决**：面对 `34/35` 类型上限与 Phase 13 的巨大扩张量（UI 组件体系、DAG 管线、合并队列、增强播放、i18n），是坚持在严苛的 `<= 35`（通过全面采用 POD 切片表、`std::tuple`、`std::variant`、无状态自由函数 + Handle/Context 表驱动，甚至 UI 控件描述符全表化）框架下零扩张完成，还是建立基于物理分层规则的《Phase 13 类型配额重核标准》？请论证最优均衡解并给出硬审计指标。
2. **Win32 原生渲染与现代组件库技术路线选型（合规破局）**：在黑名单禁用 `comctl32/d3d12/dxgi/ole32/uxtheme` 静态导入前提下，调研并选定最高工程性价比与表现力的 Win32 界面实现架构：
   - 方案对比：**纯 GDI+ClearType/双缓冲** vs **通过 `sys_dynload` 按需动态加载 `d2d1.dll + dwrite.dll`（与既有 DComp `PreviewViewport` 共享 DXGI 合成表面或独立宿主）** vs **混合方案（原生宿主处理文字编辑/系统框 + D2D/DWrite 自绘高精度流式工作区与横向管线图）**。
   - 明确回答文本自动换行测量（中英文混排 `DT_WORDBREAK` / `IDWriteTextLayout`）、高 DPI 无模糊、滚动检查器及未来主题一致性如何一劳永逸解决。
3. **WebUI 架构定位再审视**：面对原生工作站具备零回读 GPU HDR10/10-bit 实时能力而浏览器 HTML5 `<video>` 能力残缺（见议题 3 断链 4），WebUI 应当定位为“轻量级无头控制台 + 轻量代理预览流（如低延时 WebCodecs / JPEG-MJPEG / HLS 节流预览或远程状态面板）”，还是彻底明确三端（Win32 Native / Headless CLI / WebUI Remote）的功能对齐界限？

### 考题 B：统一视频增强流水线 DAG 与自适应 FRUC/RIFE 架构（解决 #1, #10, #11）
1. **任意比例 / 动态画幅 RIFE 破局架构**：在严格遵守 `[DEC-011]`（严禁单一引擎内嵌多 Optimization Profile 共享切换，防 `8.89 GB` 显存/算子坍塌）前提下，如何设计脱离 `16:9` 牢笼的统一 RIFE 推理引擎治理方案？（例如：物理分立的画幅桶专用单 Profile 引擎 vs 按像素面积上限自适应动态下采样倍率 `infer_scale` / 对称镜像居中填充 / 分块 Tile 合成；以及在 `Player` 切换片源或预览窗口重设缩放时如何做到毫秒级无卡顿切换与显存可控？请给出显存/时延/质量定量推演模型。）
2. **通用切镜 / 淡入淡出 / 黑场防呆总线**：统一设计一个位于解码输出与一切插帧/超分之间的 **Temporal Boundary Analyzer**——结合 8x8 分块 SAD、直流亮度方差、色彩直方图及时间离散梯度，精确区分：**硬切（Hard Cut）**、**全黑场/渐隐渐显（Fade）**、**高速运动（High Motion）**，并统一下发调度给 **NVOFA、RIFE 与 VSR**，一举根除 NVOFA 果冻并同步修复 `ShotGroupSegmenter` 悬空死代码。
3. **可自由重排管线图（Pipeline DAG）抽象与物理自洽规则**：
   - 将所有视频算子（Decode -> Geometry -> Temporal/FRUC -> SuperRes/VSR -> ToneMap/TrueHDR -> 3D LUT -> Encode/Display）抽离为显式带输入/输出契约标签的算子结点。
   - 清晰界定：**哪些排列允许用户横向自由拖拽生效**（含节点参数绑定与单节点启用开关），**哪些顺序变更必须在色彩域/精度层自动插入/合并转换核或通过 UI 拓扑锁规避物理错误**（例如 TrueHDR 转换后若送入只认识 SDR 的模块该如何处理？）。

### 考题 C：真·实时 60fps AI 增强播放器与预览视口体系重构（解决 #2, #3, #4, #9）
1. **零断链 D3D12 + DirectComposition 统一视口状态机**：从源码根治 `PreviewViewport` 首帧尺寸失步、无源虚占 `slot 0`、`opts.preview` 误门控、硬编码 `960x540` 与视口非等比拉伸五大症状；明确**编码旁路预览**与**播放器渲染呈现**共享同一个严密生命周期与信号机制，以及**预览暂停 Checkbox** 在各个环节（CUDA 核旁路、D3D12 描述符/Dispatch 休眠、冻结画面保留）的行为契约。
2. **播放器 60fps AI 实时增强架构与性能预算拆解**：
   - 当媒体源为 `24/25/30fps`（如 1080p 或 4K）而目标为 60fps 实时播放时，构建完整的异步生产者-帧重定时-AI 增强管线（含解码预读队列、RIFE/NVOFA 实时倍帧、跟随视口尺寸的自适应 VSR 目标、TrueHDR 实时映射、A/B 实时物理分屏）。
   - 详述 `Seek / Pause / StepFrame(+/-1) / Speed(0.25x~2.0x)` 如何与时域缓存（prev/curr 帧对、RIFE 缓存、VSR 历史状态）无缝重置对齐，确保拖拽进度条无残影闪帧且 `A/V Drift` 收敛在亚毫秒/单帧级。
3. **WASAPI 音量/静音治理与测试隔离方案**：解决单测 `440Hz` “嘟嘟嘟”蜂鸣声的根本架构措施（引入标准音频增益与虚拟渲染测试模式 / 静音默认选项，严禁单测直敲物理声卡）。

### 考题 D：全格式容器输入、CPU CRF 软编轨道与固定 MP4 交付矩阵（解决 #7）
1. **全容器解析与混合解码链路**：分析在 `cmake/RtxFfmpegStatic.cmake` 手术刀白名单约束下扩展容器/解复用器/解码器（NVDEC 硬解优先 vs 仅针对硬件不支持的老格式/ProRes 等的轻量化软解 + 规范上传 GPU VRAM 的统一帧接口），列出推荐白名单增量及内存开销边界。
2. **`.mp4` 单一输出容器兼容矩阵**：系统梳理常见输入中可能含有的非 MP4 兼容音轨（PCM, DTS, FLAC, TrueHD, Vorbis 等）与字幕轨（SRT/MOV_TEXT vs ASS/PGS/VobSub），提出明确无歧义的转码/封装/提示政策（杜绝静默丢音或写出损坏 MP4）。
3. **CPU 软编码轨道（SVT-AV1 / x265 CRF）架构落地**：选定许可证与体积最优集成方案（尤其审核 x265 GPLv2+ 与 SVT-AV1 BSD 对本仓 GPLv3 及静态归档尺寸的影响），重新打通 `param_schema`、`GpuToCpuStaging` 与 `CpuVideoEncoder`，并设计 10-bit 线性/PQ 到软编输入的高保真交接及速度/质量契约。

### 考题 E：异构多任务队列、合并模式（Merge/Concat）与全栈 i18n/UI 体系（解决 #5, #6, #8, #9, #10）
1. **新一代编码队列架构**：彻底推翻 `onStartTranscode` 覆写队列与单行列表缺陷，给出支持**独立参数隔离、任务内文件动态增删/调序、同任务「独立多输出」与「合并输出单一 `.mp4`」切换、异构片段合并对齐引擎（分辨率/FPS/色彩/音频统一归一化）**的数据结构与状态流转模型，以及三端交互设计。
2. **原生 UI 组件库与布局引擎详细蓝图**：严守 `[DEC-015]`（显式上下文传递、ComboBox 包络高度防裁切等经验）且不触发黑名单 DLL 的前提下，建立现代 Win32 控件与自绘容器规范（自适应高度文本与自动折行、弹性栈/网格/滚动面板、可拖拽横向管线方块卡片、文件队列树/卡片、平滑滑块/开关）。
3. **次世代 i18n 单源架构**：淘汰所有 `chromeText`、`printHelp` 与孤立脚本词典，统一收敛为基于编译期 ID + 参数化占位符（支持语序重排与复数）+ GUI 切换语言自动触发布局重算重绘的全栈国际化引擎（评估内置静态表 vs 外部可扩展语言文件方案）。

### 考题 F：项目卫生大肃清、`docs/TODO` 收编与两阶段施工路线（解决 #12, #13）
1. 给出全部历史编译开关、死分支、双份 Help、全局单例状态、`build.bat` vs Preset 冲突、`CMakeLists.txt` 重复规则的清剿清单。
2. 逐项裁决 `docs/TODO/README.md` 剩余 6 项（尤其 **NTSC `1001` 时基因子扩展**是否借 Phase 13 统一落盘，使播放与合并不再积累亚毫秒余数误差）。
3. 制定严谨的 **Phase 13 分波次演进路线图（Wave 拆分、模块边界、单测/真机端到端验收 Gate）**。

---

## 三、 期望专家组交付物标准（严格两步走）

请首席领域专家组严格按以下顺序批复并落盘至 `docs/phase13/`，在**总体程序架构设计未正式获批定案前，禁止任何执行工程师动笔改写 `src/` 或 `include/` 代码**：

1. **第一交付物（最高优先）：`02_CineRTX Phase 13 顶层调研与总体程序架构设计终审报告.md`**
   - 包含：6 大架构考题的深度调研对比与选型裁决、Phase 13 全系统总架构图（Mermaid/ASCII）、具名类型/DLL/许可/SLOC 四大红线影响预测与控制规则、各子系统间接口契约、13 项诉求与 `TODO` 全景映射表。
2. **第二交付物（架构定案后展开）：分域深度架构详设与可执行分波次规范（`03_...` 等）**
   - 包含：每个核心子系统（媒体 DAG 与 RIFE 泛化引擎、实时增强播放器与 D3D12 视口、Win32 自研 UI 与布局/i18n 体系、异构合并队列、CPU CRF 与扩展容器 I/O、卫生清剿清单）的数据结构定义、CUDA/HLSL/DComp 关键算法与同步时序图、完整单测及端到端实测验证矩阵。

---

**呈报完毕，恭请首席领域专家组展开顶层系统调研并出具总体架构裁决。**
