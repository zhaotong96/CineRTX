# 13_CineRTX Phase 13 重启版 Wave 0-4 验收驳回裁决暨真机闭环返工指令书

- **裁决编号**：`CINERTX-P13-RULING-013`
- **裁决主体**：CineRTX 项目总协调负责人（Coordinator）
- **被审汇报**：[`12_CineRTX Phase 13 重启版 Wave 0-4 全链路施工汇报.md`](./12_CineRTX%20Phase%2013%20重启版%20Wave%200-4%20全链路施工汇报.md)
- **依据红线**：`[DEC-003]`（文档落盘）、`[DEC-004]`（真机全链路闭环）、`[DEC-008]`（宿主机实测先于呈报）、`[DEC-011]`（单 Profile 引擎）、`[ISSUE-007]`（禁止跨端局部自测虚报全绿）
- **终审裁决**：🔴 **验收驳回（REJECTED FOR REWORK）—— 主程序无法编译、真机核心单测大面积挂红、多条核心交付为空壳状态，严禁封板或转入后续阶段。**

---

## 一、 宿主机真机编译、单测与门禁穿透核验实证

协调负责人拒绝采信“Linux 无 GPU 离线环境 10 套件 `71/71`”作为全链路验收凭证，已在 Windows 11 + MSVC 19.51 + RTX 5060 宿主机执行全量复核，实测结果如下：

| 验证维度 | 12 号汇报声称 | 宿主机实测结果 | 判定 |
| :--- | :--- | :--- | :---: |
| **MSVC 全量编译 (`build.bat`)** | 未执行真机全量编译，标记待验 | **`CineRTX.exe` 编译硬失败（`[85/91]`）**：`src/gui/workstation_canvas.cpp(999): error C3449: the number of identifiers must match the value of 'std::tuple_size<rtx::PlaybackOptions>::value'`。将 `PlaybackOptions` 扩充为 8 元组却遗漏 GUI 调用点。 | **FAIL (P0)** |
| **全套单元测试 (`rtx_tests.exe`)** | 离线 GCC 仅跑 10 套件 `71/71 PASS` | 真机运行出现 **7 项明确失败** 并因多次 `15s` 超时阻塞导致整体运行超时（`>180s` 中止）：<br>1. `playback_clock_software_master`（`test_playback_session.cpp:239`）<br>2. `playback_gop_seek_preroll`（`test_playback_session.cpp:371`，`actual 0, expected 1`）<br>3. `playback_session_fence_gated_slot_retirement`（`test_playback_session.cpp:727`）<br>4. `playback_session_native_decode_h264_hevc_av1`（`test_playback_session.cpp:577,590,593,605,638,671`：解码 4 帧渲染 1 帧后死等超时）<br>5. `gui_flicks_timebase`（`test_gui_workstation.cpp`）<br>6. `gui_flicks_timecode_format`（`test_gui_workstation.cpp:105,109,110,114`）<br>7. `rife_four_phase_monotonic_progression`（`test_rife.cpp:442`） | **FAIL (P0)** |
| **`AuditSloc.cmake`** | 汇报写为 Core Engine `13,260` 行 | 实测 Core Engine **`12,175` 行**（`<= 23,000`），Full Repo `31,470` 行。汇报数据与门禁实测不符。 | **PASS (数据失真)** |
| **`AuditTypes.cmake`** | `34 / 35` | `34 / 35`（`OK`） | **PASS** |
| **`AuditSpdx.cmake`** | `0` 缺失 | `GPL=114 MIT=39`，`PASS` | **PASS** |
| **`AuditImports.cmake`** | 声称保持不变，未跑 | `rtx_headless_cli.exe` PASS（`2 physical DLLs = [bcrypt.dll, kernel32.dll]`）；`CineRTX.exe` 因编译失败未能产出审计对象。 | **PARTIAL** |
| **`AuditPrivacy.cmake`** | 声称 `0` 处泄露 | **FAIL（3 处违规 / 406 文件）**：`docs/phase13/02_...md` 第 4、178 行（Cloud drive name leak）与 `docs/phase13/03_...md` 第 35 行（Windows user path leak）。 | **FAIL (P1)** |

### 致命元根因解剖：1001 倍时基分裂（Split-Brain Timebase）
工程师在 `include/rtx/flicks.h:17` 将 `kFlicksPerSecond` 改写为 `kBaseFlicksPerSecond * 1001ULL`（`706,305,600,000 Hz`），但 `include/rtx/time_model.h:34` 的标准时钟类型 `using Flicks = std::chrono::duration<int64_t, std::ratio<1, 705600000>>;` **完全未同步更新**，仍为 `705,600,000 Hz`！导致所有基于 `Flicks` 的 `clockNow()` 与 `waitFor(frame_interval)` 产生 **1001 倍尺度错位**（每帧等待被放大为 ~40 秒而非 40 毫秒），直接拖垮整个 `MediaPlaybackSession` 与 GUI 时间码测试。

---

## 二、 逐波次施工单（`03`–`11`）兑现度穿透审计

对照 `git diff --stat`（42 文件变动，实际业务源码仅局部微调）与 12 号汇报的 `[x]` 勾选情况，核查出严重的**“局部签名占位当全链路闭环”**现象：

| 波次 | 核心目标 | 实际源码落地核查 | 验收裁定 |
| :--- | :--- | :--- | :---: |
| **Wave 0-A** (`03`) | 卫生清剿、测试静音、`TODO` 收编 | `build.bat`、`nv_encoder.cpp`、`wasapi_audio_output.cpp`、`main.cpp`、`docs/TODO/README.md` 已改；但 `PlaybackOptions` 改完未改 `workstation_canvas.cpp:999` 导致主程序编译失败；`03` 文档仍存隐私违规。 | **返工** |
| **Wave 1-A** (`04`) | D3D12 视口与四重预览/播放断链 | `preview_viewport.cpp`、`preview_tone_map_cs.hlsl`、`workstation_canvas.cpp` 修复了部分 DComp 路径与 `WM_SETCURSOR`（且 `gui_preview_viewport_composition_swapchain_live` 与 `preview_tone_map_compute_matches_reference_pixels` 实跑 PASS）；但 **WebUI 断链 4 未动**，且 `job_queue.cpp` 仍未介入预览门控。 | **部分通过 / 缺口待补** |
| **Wave 1-B** (`05`) | 全容器扩展、CPU CRF 软编与 `.mp4` 交付 | **严重空壳**：`param_schema.cpp` 解锁了 `av1_cpu`/`hevc_cpu`/`crf`，`ffmpeg_io.cpp` 仅补了 `+faststart`；但 `cmake/RtxFfmpegStatic.cmake` **未启用 `--enable-libsvtav1` / `--enable-libx265` 也未引入对应静态库构建**，`CpuVideoEncoder` 在静态 FFmpeg 中无编码器可用！同时非 MP4 兼容音轨/字幕处理矩阵未实现。 | **驳回（空壳）** |
| **Wave 2-A** (`06`) | 任意画幅 RIFE 4 桶分立引擎 | **严重空壳**：仅在 `rife_engine.h` 添加了枚举/几何计算函数并修改 `rife_pack.cu:37`（改后直接导致 `rife_four_phase_monotonic_progression` 失败！），`rife_engine.cpp`、`rife_builder.cpp`、`EnsureRifeEngine.cmake` **零集成**。 | **驳回（空壳+回归）** |
| **Wave 2-B** (`07`) | 统一切镜黑场总线与 NVOFA 抗果冻 | **零实现**：`nvof_engine.cpp` 与 `hw_transcode_stages.cpp:446-459` 未做任何修改，`shot_segmenter` 依然悬空未接入光流。 | **未开工** |
| **Wave 3-A** (`08`) | 60fps AI 增强播放器与 1001 时基 | **破坏主干**：1001 时基改半截打挂全套播放测试；`MediaPlaybackSession` 仍零接入 VSR / TrueHDR / FRUC 60fps 实时增强。 | **驳回（回归+空壳）** |
| **Wave 3-B** (`09`) | 异构多任务队列与三路无缝合并 | **严重空壳**：`include/rtx/job_queue.h` 与 `src/core/job_queue.cpp` **一行未改**，仅 `rtx_gui.cpp` 停止覆盖 Pending 队列；多文件动态增删与 Merge/Concat 三路合并引擎完全缺失。 | **驳回（空壳）** |
| **Wave 4-A** (`10`) | DirectWrite 组件库与全栈 i18n | **严重缩水**：仅加了 10 行 GDI `measureWrappedTextHeight` 与 `detectSystemLanguage()`，无 D2D1/DWrite 动态加载组件库、无检查器滚动容器、无 `chromeText` 收敛与参数化插值引擎。 | **未达标** |
| **Wave 4-B** (`11`) | 横向可拖拽 DAG 画布与预览开关 | **零实现**：无 DAG 拓扑编译器、无 `PipelineCardCanvas`、无转码预览算力门控 CheckBox。 | **未开工** |

---

## 三、 强制返工指令（严禁跳步，按序闭环）

1. **P0 止血门禁（必须最先完成并宿主机全绿验证）**：
   - 修复 `src/gui/workstation_canvas.cpp:999` 8 元组结构化绑定，确保 `.\build.bat` 全目标（`CineRTX.exe`、`rtx_headless_cli.exe`、`rtx_tests.exe`、`rtx_ngx_bridge.dll`）0 错误 0 警告编译通过。
   - 彻底统一 `include/rtx/time_model.h` 中 `rtx::Flicks` 分母与 `include/rtx/flicks.h` 的 `kFlicksPerSecond` 定义（或严格统一单位换算），保证 `playback_session`、`gui_flicks_*` 与时间码全绿，无任何 1001 倍延迟。
   - 排查并修复 `rife_pack.cu` 改动导致的 `rife_four_phase_monotonic_progression` 失败。
   - 运行 `rtx_tests.exe` 全套 **0 FAILED**，并补跑 `CineRTX.exe` 与 `rtx_headless_cli.exe` 双模 `AuditImports.cmake`。
2. **P1 核心空壳补实（逐单真实现，禁只改头文件/枚举）**：
   - **Wave 1-B**：实装 `libsvtav1`/`libx265`（或明确并验证可用静态归档构建链路）及 `.mp4` 音/字幕兼容转码/拒绝矩阵，打通端到端 CPU CRF 实片转码测试。
   - **Wave 2-A / 2-B**：将 4 桶单 Profile 接入 `rife_engine.cpp`/`rife_builder.cpp`/`EnsureRifeEngine.cmake`；将 `ShotGroupSegmenter`/8x8 SAD + 黑场检测接入 `nvof_engine` 与 `hw_transcode_stages.cpp`。
   - **Wave 3-B**：真正重构 `job_queue.h/.cpp` 落地每任务文件列表编辑与「独立批出 vs Merge/Concat 单 `.mp4`」引擎。
3. **P2 交互与终局合规收口**：
   - 完成 Wave 3-A 播放器 60fps AI 实时管线、Wave 4-A 动态 D2D1/DWrite 与单源 i18n、Wave 4-B 可拖拽 DAG 与预览 CheckBox。
   - 执行 `cmake -DFIX=ON -P cmake/AuditPrivacy.cmake` 清洗 `02_`、`03_` 等全部文档隐私泄露，复跑确认 `0 violations`。

---

## 四、 下次送审法定准入凭证（缺一不可）

工程师再次提交施工汇报前，**必须在本 Windows 11 + RTX 5060 宿主机上亲自跑通并贴出实测完整输出**：
1. `.\build.bat` 退出码 `0`；
2. 双模 `AuditImports.cmake`（`GUI` + `HEADLESS`）全部 `PASS`；
3. `rtx_tests.exe` 全套 `0 FAILED`（含 `playback_session_*`、`gui_*`、`rife_*`）；
4. 五重静态门禁（`AuditSpdx`、`AuditTypes`、`AuditSloc`、`AuditPrivacy`、`AuditImports`）全绿。
