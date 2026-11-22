# CineRTX Phase 11（v0.3.0）演进方向调研：领域专家评审裁决与修订终稿

- **文档编号**：`CINERTX-P11-RESEARCH-REV-001`
- **归档路径**：`docs/phase11/01_CineRTX_Phase11_演进方向调研_专家修订终稿.md`
- **评审基线**：CineRTX `v0.2.0` 已发版 GitHub 主干（Commit `52103a7`，Phase 10 全栈收官，78/78 GPU 单测 PASS）
- **原调研审阅对象**：[`docs/phase11/CineRTX 演进方向调研.md`](./CineRTX 演进方向调研.md)
- **评审角色**：多媒体系统工程与 GPU 异构计算首席领域专家（Chief Domain Expert）
- **评审日期**：2026-11-18
- **总体裁决**：**不予全文照搬，执行 8 项核心纠偏修订；待项目负责人确认本修订终稿后，再据此拆解并落盘 Phase 11 多份 SuperPower 施工文档。**

---

## 0. 领域专家逐项评审裁决与修订对照表

经同步 GitHub 最新 `v0.2.0`（Commit `52103a7`）代码仓并逐段穿透核验原调研稿，原调研的**三大宏观战略方向**（代码精简提纯、播放器视口体验完善、工业级 NLE 底座正式开发）完全正确；但文档在**版本基线、编译器可行性、架构红线、显存拓扑与 SDK 适用边界**上存在 **8 处重大事实偏差或技术倒退**，若直接据此下发 SuperPower 施工单将导致编译中断、架构回退或无效返工。

| 序号 | 原调研观点（待修订点） | 首席领域专家评审意见与实证依据 | 修订后确立的 Phase 11 架构准则 |
| :---: | :--- | :--- | :--- |
| **R-01** | **版本时空倒错**：以 `v0.1.0` 为现状、以 `Stage 3 (v0.2.0)` 为目标，要求修复 `tools/package_release.py`、QP 默认值 `0/24/26` 漂移、`server.py` 正则刮削、双轨管线与 `--session-stdio` 缺失。 | **不同意（严重过期）**。仓库已发版 **`v0.2.0`**（Commit `52103a7`）：`tools/` 已由 [`AuditSpdx.cmake`](./AuditSpdx.cmake) 禁绝；20 项 SSOT 参数（统一 `cq` / `qp=22`）、`--schema-json`、`--session-stdio` 异步取消与 `NvidiaTranscodeSession` 均已落地。**真实遗留漂移**反而是 [`CMakeLists.txt`](../../CMakeLists.txt) 仍标 `VERSION 0.1.0`、[`README.md`](../../README.md) / [`README_zh.md`](./README_zh.md) 仍写旧的“19 项参数 / 113 词条 / 75 单测 / `rtx_ngx_bridge.dll`”。 | 确立基线为 **`v0.2.0` $\rightarrow$ `v0.3.0`**；清理聚焦真实技术债：版本号与 README 同步、历史 `docs/phase1~10` 压缩归档、残留无用文件清扫。 |
| **R-02** | **强推 C++26 静态反射（P2996 + P3394）**：要求立即用 `^^`、`std::meta::info`、`[: :]`、`[[=annotation]]` 重写 `param_schema.h`。 | **坚决反对（编译硬阻断 + 过度工程）**。CineRTX 主力工具链为 Windows MSVC + 便携 Zig 0.13 回退。截至 2026 年，**MSVC 尚无任何公开的 P2996/P3394 静态反射支持**（仅 GCC 16 与 Bloomberg 实验性 Clang 分支部分支持，见 [Whole Tomato C++26 Reflection Report](https://www.wholetomato.com/blog/cpp26-reflection-large-codebases/) 与 [Stream HPC](https://streamhpc.com/blog/2026-07-08/kernel-development-c26-reflection-%F0%9F%A7%A1/)）。强行引入会直接炸毁 `windows-msvc-ninja`、`build.bat` 与 VS IDE 集成。当前 [`param_schema.h`](./param_schema.h) 的 `constexpr` 表驱动架构已完美实现三端 SSOT。 | **否决 P2996 强制改造**；在标准 C++20/23/MSVC `/std:c++latest` 可编译前提下，用极简 `FieldDescriptor`（成员指针表）进一步消除 `param_schema.cpp` 内部手写重复；将 P2996 移入未来编译器成熟后的远期观察项。 |
| **R-03** | **退回三段式显式生命周期**：要求微内核贯彻 `initialize()` / `execute()` / `shutdown()`。 | **坚决反对（违背核心 RAII 铁律）**。CineRTX 自 Phase 6 起已确立 **100% 绝对 RAII**（私有构造 + `static Result<T> create(...)` 工厂 + 析构自动逆序释放），彻底消灭易泄漏的 `initialize/shutdown`。 | **严守 100% 绝对 RAII**；资源销毁顺序（`encoder_` 先于 `bridge_` 释放）通过类成员声明顺序在编译期强制约束，绝不引入手动 `shutdown()`。 |
| **R-04** | **一刀切否决 `vsr-first` 并强制锁死 `fg-first`**：断言超分先行必然 OOM；并声称用“CUDA 13.4 VMM `cuMemCreate`”将 NvOFFRUC 与 TRT 显存别名化。 | **部分同意、核心机制纠偏**：<br>1. Phase 10 已确立 `vsr-first`（避免 VSR 放大插帧伪影）并在 `TranscodeProfile` 保留 `vsr-first \| fg-first` 选项，对 NVOFA 或 2K 源 `vsr-first` 极稳；仅 `4K + RIFE + 8GB VRAM` 存在显存与大位移风险，应做**显存感知自适应调度 + RIFE `scale=0.5` 多尺度光流**，而非一棍子打死 `vsr-first`。<br>2. `cuMemCreate` 早在 CUDA 11+ 即已存在（并非 CUDA 13.4 新特性，见 [NVIDIA Forum](https://forums.developer.nvidia.com/t/mpi-scatter-hits-cuda-error-not-permitted/354766)），且 NvOFFRUC 内部工作区由驱动黑盒管理，无法与 TRT `IGpuAllocator` 强行做物理页别名化。 | 保留 `vsr-first \| fg-first` 双拓扑并引入 **VRAM 预算感知自适应切换**（8GB 卡遇 4K RIFE 自动启用 `scale=0.5` 或降级 `fg-first`）；FRUC 显存复用继续贯彻**互斥生命周期按需初始化**，辅以显式 VRAM 预算监控。 |
| **R-05** | **视口现状误判与 `dcomp.dll` 红线未声明**：声称当前是“4K RGBA16F D2H 1.5 GB/s + GDI StretchDIBits”，且忽略了 Player 模式的核心缺口。 | **事实纠偏 + 架构补全**。[`preview_viewport.cpp`](./preview_viewport.cpp) 已是 960×540 `R10G10B10A2` D3D12 `FLIP_DISCARD` 非阻塞交换链（仅 4.14 MB 显存，15fps D2H ~30 MB/s），但确实存在：① 独立 `ID3D12Device` + 仍走小尺寸 D2H；② [`preview_viewport.h`](./preview_viewport.h) 曾规定零 `dcomp.dll` 依赖；③ `Player` 模式缺真正的**实时音画同步播放引擎（Audio-Master Clock + 预解环 + 帧精确 Seek）**，且队列模式未接预览回调。 | 批准演进至 **动态加载 `dcomp.dll` + 共享 D3D12 Device 低优先级队列 + HDR10/scRGB 自适应 + 零拷贝纹理直连**（同步更新 `AuditImports.cmake` 白名单）；并将 **独立实时播放器引擎（WASAPI 音频主时钟 + NVDEC 预解环 + 帧精确 Seek）** 确立为方向二核心。 |
| **R-06** | **Flicks 降采样至 `14,112,000 Hz`**：提议核心层锚定 `14,112,000 Hz`（Flicks 的 1/50），却在表格中自承 44.1kHz 与 NTSC 不整除。 | **坚决反对（数学自废武功）**。原生 `705,600,000 Hz` Flicks 在 `int64_t` 下可表达 **414 年**，且 [`src/core/rational.cpp`](../../src/core/rational.cpp) 与 `flicks.h` 已完整实现 705,600,000 Hz + `_umul128`/`_udiv128`。降采样到 14.112 MHz 毫无意义且破坏无损整除性。 | **全局刚性锁定原生 `705,600,000 Hz` Flicks**；Phase 11 重点是将 [`workstation_canvas.cpp`](./workstation_canvas.cpp) 中遗留的 `duration_ms` 毫秒时间轴全面迁移至 `rtx::Flicks`，并构建真正的 NLE 多轨编辑数据结构。 |
| **R-07** | **Video Codec SDK 13.1 `AppTransZeroCopy` 误用**：声称 VSR 4K 输出通过 PTX 写入 NVDEC/NVENC 共享的 `CUarray` 消除“跨 CUDA Context 搬移”。 | **技术纠偏**。根据 [NVIDIA Video Codec SDK 13.1 官方博客](https://developer.nvidia.com/blog/nvidia-video-codec-sdk-13-1-zero-copy-transcode-av1-b-frames-and-frame-accurate-seek/)，`AppTransZeroCopy` 用于 NVDEC $\rightarrow$ NVENC **直通转码**（同分辨率同 `CUarray`）；CineRTX 的 1080p $\rightarrow$ 4K NGX VSR 管线本就在同一 CUDA Context 内且分辨率变化，无法直连输入输出 `CUarray`。SDK 13.1 对 CineRTX Phase 11 真正最具颠覆性的是 **GOP-Aware Frame-Accurate Seek**（赋能 Player/NLE 丝滑拖条）、**AV1 31 层级 B 帧 + UHQ** 与 **NVDEC 宏块统计零开销直出**（赋能切镜检测）。 | 精准引入 SDK 13.1 三大高价值能力：**① GOP-Aware 帧精确 Seek（Player/NLE 核心）**、**② AV1 分层 B 帧（默认 7 帧平衡 / 可选 15-31 帧 UHQ 母版档，含 8GB 显存保护）**、**③ 可选 `CUarray` 编码输入零拷贝优化（仅用于直通/同分辨率编码段）与宏块 MV 先验**。 |
| **R-08** | **文档正文残留 AI 乱码标记与过期引用**：正文中夹杂 `[span_28](start_span)...` 等断字乱码，引文混杂已淘汰的旧阶段文档。 | **已全面清洗**。 | 本修订终稿已彻底剔除所有乱码与过期引文，全部锚定至 `v0.2.0` 真实代码与权威外部技术文档。 |

---

## 1. 项目现状基线与 Phase 11（v0.3.0）演进总纲

截至 `v0.2.0`（Commit `52103a7`）发版至 GitHub 主线，CineRTX 已圆满完成 **Phase 10 全栈架构提纯与转码链路闭环**：
- **核心能力已固化**：静态内联 `rtx_ngx_bridge`（零附属自研 DLL）、PTX `.version 7.5 / sm_75` 驱动兼容钳位、全模态编码（NVENC CQ/QP/VBR/CBR + 10-bit P010 CPU CRF）、NVOFA 硬件光流 + TensorRT RIFE v4.25 双路补帧、`ShotGroupSegmenter` 切镜保护、音频流无损透传冲刷、`EncoderJobQueue` 动态多任务队列，以及 Win32 / CLI `--session-stdio` / WebUI v3 三端 20 项 SSOT 参数对齐；78/78 GPU 单元测试与 4K HDR10 AV1 + NVOFA 真机端到端转码 100% 通过。
- **Phase 11（目标版本：`v0.3.0`）的核心跃迁**：从**“高稳态纯 GPU 离线重制转码器”**正式升维为**“具备真零拷贝实时监看播放能力与多轨时间轴引擎的工业级 GPU 非线性编辑（NLE）工作站”**。

本阶段围绕三大正交战略方向展开：
1. **方向一（工程底座提纯与文档收口）**：对齐 `v0.2.0` $\rightarrow$ `v0.3.0` 元数据漂移，压缩归档 Phase 1–10 过程文档，升级 `constexpr` 成员指针表驱动 SSOT（零破坏 MSVC 兼容性），严守 100% RAII 与零泄漏红线。
2. **方向二（真零拷贝播放器与 DirectComposition 视口重塑）**：将当前仅用于转码抽帧预览的 `PreviewViewport` 升级为真正的 **D3D12 + DirectComposition 零拷贝 HDR/SDR 自适应播放与监看视口**，补齐 **WASAPI 音频主时钟同步引擎**、**NVDEC 预解帧环** 与 **Video Codec SDK 13.1 GOP-Aware 帧精确 Seek**，让 Win32 `Player` 与 `+1` 关联全屏快播达到专业播放器水准。
3. **方向三（工业级 NLE 多轨底座与高阶视频/音频引擎）**：全面启用原生 `705,600,000 Hz` Flicks 构建多轨非线编工程模型（Project / Track / Clip / Cut / Transition / Export Graph），落地 **AVX2 SIMD + 32-bit Float Planar + SPSC 无锁环** 多轨混音引擎（支持 WSOLA 保调变速），引入 **显存预算感知的 VSR/FRUC 自适应调度** 与 **Video Codec SDK 13.1 AV1 分层 B 帧 / UHQ 母版编码**。

---

## 2. 第一演进方向：代码精简、真实文档漂移清零与编译期 SSOT 提纯

### 2.1 真实漂移清零与历史文档物理压缩
- **修正仓库显性元数据漂移**：
  - 将 [`CMakeLists.txt`](../../CMakeLists.txt) 中的 `project(CineRTX VERSION 0.1.0 ...)` 升级并同步至当前真实发版号，确保 [`PackageRelease.cmake`](./PackageRelease.cmake) 产出的发行包版本号准确无误；
  - 全面更新 [`README.md`](../../README.md) 与 [`README_zh.md`](./README_zh.md)，将过时的“19 项参数 / 113 词条 / 75 项单测 / 独立 `rtx_ngx_bridge.dll`”同步为真实基线（20 项参数 / 125 词条 / 78+ 项单测 / 静态内联 `RTX_EMBEDDED_NGX_BRIDGE` / 全模态 CQ+CPU CRF / WebUI v3）。
- **历史上下文瘦身**：将 `docs/phase1` 至 `docs/phase10` 中数百份过程性返工单与草稿归档压缩至 `docs/archive/HISTORY_SUMMARY_P1_P10.md`（仅保留存活架构契约与已被推翻的反模式黑名单），消除开发者与 AI 辅助编程工具的上下文污染。

### 2.2 务实编译期 SSOT 提纯（兼容 MSVC `/std:c++latest` 与 Zig Clang）
- **明确否决现阶段引入 C++26 P2996 静态反射**：鉴于 [MSVC 在 2026 年尚未提供公开的 P2996/P3394 支持](https://www.wholetomato.com/blog/cpp26-reflection-large-codebases/)，强行引入 `^^` / `std::meta::info` 将破坏 Windows 主力构建链。
- **高内聚替代方案（Member-Pointer Descriptor Table）**：在 [`include/rtx/param_schema.h`](../../include/rtx/param_schema.h) 现有 `kParamSchema` 基础上，引入零依赖的编译期字段描述符（`FieldBinding<TranscodeProfile, T>`），将 `parseProfileFromJson`、`profileToJson`、CLI 参数解析与 `validateAndNormalizeProfile` 统一收敛到同一份成员指针映射表，消除 `param_schema.cpp` 中 4 处重复手写字段名的维护负担，同时 100% 兼容 MSVC 19.44+、Clang 与 Zig 0.13。

### 2.3 严守 100% 绝对 RAII 与单一微内核收口
- **禁止任何形式的 `initialize()` / `shutdown()` 倒退**：所有新建子系统（DirectComposition 视口、WASAPI 音频引擎、NLE 工程与渲染图）一律严格遵守 `private 构造 + static Result<T> create(...) + ~T() noexcept` 绝对 RAII 规范。
- **编译期销毁序不变量**：在 [`NvidiaTranscodeSession`](#) 等核心会话类中，显式以成员声明顺序保证 `bridge_` 先于 `encoder_` 构造、后于 `encoder_` 析构，确保 NVENC 在 NGX 释放底层 P010 环形显存前已彻底解绑全部 CUDA 资源。

---

## 3. 第二演进方向：真零拷贝播放器引擎与 DirectComposition 视口重塑

### 3.1 DirectComposition 视觉树与共享 D3D12 Device 低优先级队列
- **消除现有 `PreviewViewport` 的两大瓶颈**：
  1. 淘汰 [`src/gui/preview_viewport.cpp`](../../src/gui/preview_viewport.cpp) 中“独立 `ID3D12Device` + 子 `HWND` + 960×540 D2H 拷贝”的过渡设计；
  2. 复用核心管线的无头 `ID3D12Device`，开辟独立的 `D3D12_COMMAND_QUEUE_PRIORITY_LOW` 图形/呈现命令队列，通过 **CUDA-D3D12 共享纹理（Zero-Copy Shared Texture）** 直接消费 VSR/TrueHDR 输出表面，实现真正意义上的 **0 B/s D2H 零拷贝实时监看**；
  3. 采用 **动态加载 `dcomp.dll`**（`LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)`，并同步更新 [`cmake/AuditImports.cmake`](../../cmake/AuditImports.cmake) 白名单与文档说明），以 `IDCompositionDevice` + `CreateSwapChainForComposition` 将交换链作为独立 Visual 精准镶嵌于主窗口，根除拖拽缩放时的空域撕裂（Airspace Tearing）与黑边闪烁，并为悬浮 OSD / A/B 分屏线提供硬件级 Alpha 合成。
- **非阻塞抽帧节流与 HDR/SDR 动态色彩空间自适应**：
  - 结合 `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`、`DXGI_PRESENT_DO_NOT_WAIT | DXGI_PRESENT_ALLOW_TEARING` 与 `DXGI_STATUS_OCCLUDED` 探针，将监看刷新率解耦锁定至显示器刷新率（如 60Hz），确保 120–240 FPS 的离线压制吞吐不受 DWM 拖累（干扰率 < 0.5%）；
  - 窗口初始化及跨屏移动时调用 `IDXGISwapChain4::CheckColorSpaceSupport`：Windows 11 HDR 屏幕直出 `R16G16B16A16_FLOAT`（scRGB 线性空间）原汁原味呈现 TrueHDR；SDR 屏幕自动切至 `R8G8B8A8_UNORM` 并在独立低优先级 Compute Shader 下采样阶段执行 ACES Filmic 色调映射，绝不污染送往 NVENC 的原始 4K 显存表面。
- **补齐队列模式预览接线**：将 `opts.preview` / 零拷贝纹理句柄与 `split_ratio` 正式接入 [`EncoderJobQueue`](#)，使 Win32 队列模式与单任务模式均享有实时 A/B 分屏监看。

### 3.2 独立实时播放器内核（补齐 Player 与 `+1` 全屏快播体验）
原调研仅讨论了渲染交换链，忽略了“好用播放器”的核心在于**播放调度内核**。Phase 11 需正式落地 `rtx::RealtimePlayer`：
1. **音频主时钟（Audio-Master Sync Clock）**：基于 Windows WASAPI 共享模式（低延迟事件驱动）输出 32-bit Float PCM，以音频硬件已播放采样数换算原生 Flicks 作为全局主时钟；无音轨素材自动退化至 `std::chrono::steady_clock` 高精度视频时钟；
2. **NVDEC 预解码缓冲环（Decode-Ahead Ring, 4–6 Frames）**：解码线程提前填充 GPU 显存帧环，按主时钟精准节拍投递至 VSR/TrueHDR（视口自适应动态倍率）与 DirectComposition 交换链，实现 4K 60/120fps 零卡顿回放与倍速播放（0.25x / 0.5x / 1x / 1.5x / 2x）；
3. **GOP-Aware 帧精确 Seek（接入 Video Codec SDK 13.1 范式）**：参照 [NVIDIA Video Codec SDK 13.1 帧精确 Seek 架构](https://developer.nvidia.com/blog/nvidia-video-codec-sdk-13-1-zero-copy-transcode-av1-b-frames-and-frame-accurate-seek/)，定位任意时间戳时自动回溯至最近 IDR（兼容 Open GOP）、跳过非参考帧、仅对目标帧执行映射与 VSR 后处理，实现进度条拖拽与逐帧步进（`,` / `.` 键）的毫秒级响应；
4. **无锁原子遥测总线**：核心层建立 `alignas(64) TelemetrySnapshot`（解码/编码帧数、实时 FPS、VRAM 水位、音画偏差 ms、丢帧计数），工作线程 `memory_order_release` 写入，GUI 30Hz `SetTimer` `memory_order_acquire` 拉取，彻底杜绝高频 `PostMessageW` 阻塞消息队列。

---

## 4. 第三演进方向：工业级 NLE 多轨底座与高阶视频/音频引擎

### 4.1 全局原生 `705,600,000 Hz` Flicks 时基与多轨工程模型（NLE Core）
- **全面弃用毫秒近似与 `14,112,000 Hz` 降采样**：基于已验证的 `include/rtx/flicks.h` 与 [`src/core/rational.cpp`](../../src/core/rational.cpp)（128 位 `_umul128` / `_udiv128` + Bresenham 余数结转），全系统刚性锁定原生 **`1 Flick = 1/705,600,000 s`**，同时精确整除 24/25/30/48/50/60/120 fps、NTSC 30000/1001 及 44.1/48/96/192 kHz 音频采样率（`int64_t` 跨度超 414 年，零累积漂移）。
- **迁移 `workstation_canvas.cpp` 与构建 NLE 数据契约**：
  - 定义跨端（Win32 NLE / WebUI NLE / CLI `--project`）同构的 JSON 工程文件契约 `NleProject`：包含 `Timeline`（原生 Flicks 标尺）、多轨道 `VideoTrack (V1..Vn)` / `AudioTrack (A1..An)` / `SubtitleTrack (S1)`、`Clip`（`source_path`, `in_flicks`, `out_flicks`, `timeline_start_flicks`, `speed_ratio`, `lut_path`, `gain_db`）、`Transition`（硬切 / 交叉叠化 Cross-Dissolve）与非破坏性撤销栈（Command Pattern Undo/Redo）；
  - 将 NLE 导出正式接入现有的 `RemasterPipeline` 算子图，实现**多片段无缝拼接 + 片段边界自动重置 VSR 时域与强制 IDR + 统一 HDR10/AV1 10-bit 零拷贝渲染导出**。

### 4.2 显存预算感知的 VSR / FRUC 自适应拓扑调度（守住 8GB GDDR7 红线）
- **保留双拓扑，引入智能显存保护**：
  - 维持 `pipeline_order: "vsr-first" | "fg-first"` 契约；
  - 默认策略：**NVOFA 硬件光流**或 **目标分辨率 $\le$ 2K** 时优先 `vsr-first`（避免 VSR 放大插帧伪影）；当 **目标 4K + RIFE 深度学习 + 显卡可用 VRAM $\le$ 8GB** 时，自动启用 RIFE v4.25 原生 **`scale=0.5` 多尺度光流估计**（既解决 4K 大像素位移超出感受野问题，又将激活显存压缩 75%）或平滑回退至 `fg-first`，严防触碰 88% Critical 显存红线；
  - 继续贯彻 **NVOFA 与 RIFE 互斥按需初始化**，不尝试对 NvOFFRUC 驱动黑盒显存强行做不安全的 `cuMemMap` 别名化；
  - 复用并增强 [`ShotGroupSegmenter`](#)（8×8 分块 SAD + EMA 基线），并可选结合 SDK 13.1 NVDEC 宏块运动矢量统计，在硬切镜时 100% 触发插帧复制旁路、VSR 时域重置与 TrueHDR 曝光重置。

### 4.3 AVX2 SIMD 多轨音频处理矩阵（Fairlight 级内核）
- **统一内部格式**：所有 NLE 轨道音频经静态 `libswresample` 解码归一化为 **32-bit Float Planar PCM @ 48 kHz / 96 kHz**；
- **无锁 SPSC 环形缓冲**：播放与导出音频管线均采用 `alignas(64)` 缓存行隔离读写指针的 SPSC Ring Buffer，杜绝 MESI 伪共享与音频爆音；
- **AVX2 SIMD 混音与 DSP**：使用 `_mm256_fmadd_ps` 实现 8 路并行浮点增益、声相（Pan）、线性/等功率淡入淡出（Fade In/Out）与简易总线限幅器（Limiter）；
- **WSOLA 保调变速（Pitch-Preserving Time-Stretch）**：当 NLE 片段启用慢动作（如 0.5x）或加速（2.0x）时，同步对伴音执行轻量级纯 C++ WSOLA 重叠相加变速，维持原生人声音调不变。

### 4.4 Video Codec SDK 13.1 高阶能力精准接入
依据 [NVIDIA Video Codec SDK 13.1 官方发布规格](https://developer.nvidia.com/blog/nvidia-video-codec-sdk-13-1-zero-copy-transcode-av1-b-frames-and-frame-accurate-seek/)，按实际架构收益分三级精准接入：
1. **P0 必接 — GOP-Aware Frame-Accurate Seek**：用于 `RealtimePlayer` 与 NLE 多片段随机寻址解码；
2. **P1 必接 — AV1 Hierarchical Reference Mode（分层 B 帧）与 UHQ 迭代编码**：在 Blackwell GPU（RTX 50 系列）上开放 AV1 分层 B 帧（平衡档 7 B-frames；极限母版档 15/31 B-frames + UHQ Lookahead & Temporal Filtering，且在 8GB 显存卡上对 Lookahead 深度做上限保护），在实时硬件吞吐下逼近 `libsvtav1` 极慢预设压缩率；
3. **P2 选接 — `CUarray` 直通零拷贝与 NVDEC 宏块统计**：仅在“无 VSR 缩放的原生分辨率直通转码”分支启用 `CUDA_ARRAY3D_VIDEO_ENCODE_DECODE` 共享 `CUarray` 降低 SM 占用；探索 H.264/HEVC 宏块运动矢量零开销直出辅助切镜检测。

---

## 5. 下一步行动建议（待项目负责人拍板）

由于本次对原调研文档做出了 **8 项实质性架构修订**（特别是**否决 C++26 P2996 强推、否决 `initialize/shutdown` 倒退、保留 `vsr-first` 自适应调度、坚持原生 705.6 MHz Flicks、补齐实时播放器音频主时钟与 SDK 13.1 精准落点**），暂未直接批量落盘 Phase 11 SuperPower 施工文档。

**请项目负责人确认以下 2 项架构拍板意见后，我即可立即按 SuperPower 规范（总纲 Plan + 分轨 Tasks + 量化 Gate Checklists）在 `docs/phase11/` 下落盘全套施工文档**：
1. 是否同意上述 **R-01 ~ R-08** 全部 8 项修订裁决？
2. Phase 11（`v0.3.0`）是否按 **4 条可并行工程分轨** 拆解下发施工单：
   - **Track 11A**：基线收口（版本号/README 同步、历史文档归档、成员指针表驱动 SSOT 提纯）；
   - **Track 11B**：DirectComposition 零拷贝视口 + HDR/SDR 自适应 + `RealtimePlayer`（WASAPI 主时钟 + GOP-Aware 帧精确 Seek + 无锁遥测）；
   - **Track 11C**：原生 Flicks 多轨 NLE 工程引擎 + AVX2 SIMD 多轨音频矩阵（SPSC + WSOLA）+ 多片段导出图；
   - **Track 11D**：显存感知 VSR/FRUC 自适应调度（RIFE `scale=0.5`）+ Video Codec SDK 13.1 AV1 分层 B 帧 / UHQ 编码接入。
