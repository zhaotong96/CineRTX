# CineRTX 媒体底层架构决断：FFmpeg 7.1.1 锁定依据与 FFmpeg 8.x 未来演进技术评估

> **文档标识**：`SPEC-TODO-FFMPEG-EVOLUTION-003`  
> **所属目录**：[`docs/TODO/ffmpeg7_vs_ffmpeg8_selection_and_evolution.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/TODO/ffmpeg7_vs_ffmpeg8_selection_and_evolution.md)  
> **起草日期**：2026-10-26  
> **状态**：归档记录 / 未来演进路线储备 (Archived Deliberation & Future RFC)  
> **关联规范**：[`docs/phase3/CineRTX FFmpeg全量静态链接专家方案.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/phase3/CineRTX%20FFmpeg全量静态链接专家方案.md)、[`docs/phase3/CineRTX_FFmpeg全量静态链接全栈施工方案.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/phase3/CineRTX_FFmpeg全量静态链接全栈施工方案.md)

---

## 1. 决策背景与讨论实录 (Context & Deliberation History)

### 1.1 核心议题呈现
在 CineRTX 阶段三（Phase 3: 双路智能插帧合流、DirectComposition 实时视口与 Flicks 纳秒时基）前置重构中，团队确立了**“废除所有动态 DLL 依赖、全量静态内联 FFmpeg、构建单 EXE 自包含极简架构”**的 P0 级攻坚目标。

在方案实施过程中，产品负责人与系统架构团队展开了深入的技术论证：
> **核心讨论**：*“业界最新主线已推进至 FFmpeg 8.x（FFmpeg 8.0 ‘Huffman’ 于 2025 年 8 月发布，FFmpeg 8.1 ‘Hoare’ 于 2026 年 3 月发布），为何 CineRTX 阶段三全栈方案坚决锁定 FFmpeg 7.1.1（‘Péter’）而非直接跟进最新的 8.x？”*

本文件完整归档这一系统级决断的技术证据链、因果推导过程及未来演进触发预案，以备后续大版本迭代参考。

---

## 2. 深度技术对比：FFmpeg 7.1.1 锁定依据 (Technical Justification)

```
CineRTX 纯 GPU 异构数据流流水线：
┌──────────────┐     ┌───────────────┐     ┌────────────────┐     ┌───────────────┐     ┌──────────────┐
│  MP4 / MKV   │────►│ NVDEC 硬件解码 │────►│ NvOFFRUC/RIFE  │────►│ NVENC 硬件编码 │────►│  MP4 / MKV   │
│ Demux (静态) │     │ (纯显存NV12)  │     │ 显存插帧与处理 │     │ (纯显存AV1)   │     │  Mux (静态)  │
└──────────────┘     └───────────────┘     └────────────────┘     └───────────────┘     └──────────────┘
       ▲                                                                                       ▲
       └────────────────────────────── FFmpeg 职责严格受限在此两端 ─────────────────────────────┘
                                  (中间 0 字节 Host RAM，0 CPU 像素计算)
```

### 2.1 纯 GPU 架构职责边界与 8.x 新特性剪除率
在 CineRTX 的系统哲学中，所有重度密集型计算（像素级解码、超分、插帧、色彩转换、视频编码、视口渲染）**100% 由 NVIDIA Blackwell/Ada 硬件及 CUDA 核心接管**：
* **解复用与复用**：`FfmpegDemuxer` 与 `FfmpegMuxer` 仅负责读取压缩包并解析 NALU/OBU 头，将数据直接送入 NVDEC；
* **像素计算**：绝不允许任何一行 CPU 像素解码（禁用 `libavcodec` 软解码）、软缩放（禁用 `libswscale`）、滤镜图（禁用 `libavfilter`）。

**FFmpeg 8.x 核心新特性与 CineRTX 需求映射矩阵**：

| FFmpeg 8.x 核心更新特性 | 特性技术本质 | CineRTX 实际需求评估 | 裁剪策略 |
| :--- | :--- | :--- | :--- |
| **OpenAI Whisper Filter** | 基于 CPU/DNN 的自动语音识别转文字滤镜 | CineRTX 当前为离线/实时视频转码重制引擎，暂无端侧离线语音听翻功能 | **100% 裁剪剔除** (`--disable-avfilter`) |
| **Vulkan Compute Codecs** | 基于 Vulkan 计算着色器的 FFV1/ProRes 软编解码 | CineRTX 专精于 RTX Blackwell 架构原生第 9 代 NVENC/NVDEC | **100% 裁剪剔除** (`--disable-everything`) |
| **D3D12/VAAPI 软硬管线重构** | 跨平台硬件抽象层增强 | CineRTX 通过纯 C 垫片与 DirectComposition/DXGI 直接打通底层硬件，不使用 FFmpeg 内部的 hwcontext | **100% 裁剪剔除** (`--disable-d3d11va --disable-dxva2`) |
| **APV / RealVideo 6.0** | 极小众及过时软解码格式 | 与消费级 4K HDR10 / AV1 / HEVC 主流生产流程无关 | **100% 裁剪剔除** |

**结论**：FFmpeg 8.x 带来的所有大型功能升级，在 CineRTX 的手术刀级白名单下被 **100% 物理剥离**，盲目升级无法为引擎带来哪怕 0.1 FPS 的吞吐提升或任何画面增益。

---

### 2.2 破坏性变更 (Breaking Changes) 与工程稳定性风险
FFmpeg 8.x 是重大的主版本跃升（Major Version Bump），对库的 ABI（`libavcodec 62` 等）与 API 进行了结构性重构：
1. **老旧 API 物理清除**：
   - 彻底移除了旧版通道布局标志（Channel Layout Masks），全面强制使用 `AVChannelLayout`；
   - 调整了部分编解码器上下文（`AVCodecContext`）生命周期标志与参数解析钩子。
2. **构建脚本与隐式依赖变更**：
   - FFmpeg 8.x 源码树增加了对 Vulkan SDK 头文件、高级线程同步原语的隐式编译期探测；
   - 在 MSVC 工具链（`cl.exe`）配合 `--disable-everything` 时，若白名单配置稍有不慎，极易在 `libavformat` 内部产生未定义外部符号，破坏静态链接的确定性。
3. **已验证基线的传承价值**：
   - CineRTX 在阶段一、二全量验收（包含 128.7 FPS 吞吐、Gate 2.4 两小时高负荷 Soak 测试）中，全部代码与内存流实现均以 **FFmpeg 7.1.1** 作为底层基准完成严格校准，稳定性已获生产级验证。

---

### 2.3 黄金体积包络的毫厘级精算结果 (Size Envelope)
通过对 FFmpeg 7.1.1 的深度静态裁剪，CineRTX 实现了多媒体底层库的极致收敛：

* **静态 `.lib` 物理体积**：
  - `libavformat.lib`: ~1.15 MB
  - `libavcodec.lib`: ~0.95 MB
  - `libavutil.lib`: ~0.42 MB
  - `libswresample.lib`: ~0.18 MB
  - **静态库合计**：**~2.70 MB**
* **内联至 EXE 的机器码增量**：经 MSVC `link.exe /OPT:REF /OPT:ICF` 消除未引用符号后，实际主程序增量仅为 **~1.85 MB**；
* **单 EXE 终态体积**：`cinertx.exe` 稳定收敛于 **8.45 MB**，完美落在 **8.0 MB ~ 12.0 MB** 的工业级黄金包络区间内。

若在 Phase 3 研发中期切换至 FFmpeg 8.x，需要重新对 8.x 的数百个目标文件重新进行符号依赖树逆向审计，极易导致二进制体积突破黄金包络。

---

### 2.4 交付优先级守则 (Project Deliverables Priority)
在软件工程管理中，首要目标是**“控制变量，推进实机闭环”**：
* 阶段三当前处于工程攻坚的关键收敛期（执行工程师刚打上 6315 行补丁，需搭建轻量环境跑通本地物理编译与 137 项 CTest 门禁）；
* 此时保持外部第三方库（FFmpeg）版本处于绝对已知、绝对受控状态，是保障项目按时闭环交付的根本防线。

---

## 3. 未来向 FFmpeg 8.x 演进的技术路线与触发条件 (Evolution Roadmap)

### 3.1 演进触发条件 (Evolution Triggers)
在 CineRTX 演进至更高级版本（如 v0.3.0+ 或阶段四）时，若满足以下任一条件，团队将正式启动对 FFmpeg 8.x 或更高版本的升级工作：

1. **容器层规范演进**：
   - 国际主流工业界确立了基于 FFmpeg 8.x 原生解析的新型 HDR / 杜比视界元数据容器扩展，且 7.1.1 无法无缝承载；
2. **多模态音频与 AI 辅助演进**：
   - CineRTX 未来规划原生离线多轨语音分离、智能时间轴听翻（需启用 Whisper 或新一代音频分析滤镜，并作为独立动态子系统引入）；
3. **长期支持版本稳定性**：
   - FFmpeg 8.x 系列进入成熟期（如发布 8.2 或对应长期维护点版本），且其 MSVC 原生静态构建与 `--disable-everything` 裁剪在社区获得全面实测背书。

---

### 3.2 架构隔离与无痛平滑升级 SOP (Zero-Impact Migration SOP)

得益于 CineRTX 现有的高度内聚与抽象封装架构，未来升级媒体底层无需改动任何核心 GPU 流水线：

```
[系统调度层] ──── (纯 C++ 抽象 include/rtx/io.h) ────► [媒体适配层 src/io/ffmpeg_io.cpp]
                                                               │
                                         (底层仅替换此目录下的构建与源码)
                                                               ▼
                                               [FFmpeg 8.x Static .lib]
```

#### 升级实施步骤：
1. **外部构建脚本升级**：
   - 将 [`tools/build_ffmpeg_static_msvc.sh`](file:///C:/PUBLIC/[REDACTED_PROJ]/tools/build_ffmpeg_static_msvc.sh) 中的版本定义更新为目标版本：
     ```bash
     FFMPEG_VERSION="${FFMPEG_VERSION:-8.1.0}"
     ```
   - 在 MSYS2 + MSVC 命令行下编译产出新版 4 个核心静态库；
2. **符号表与白名单校验**：
   - 运行 dumpbin 审计新版 `.lib`，确认不包含网络（`ws2_32`）、加密（`schannel`）等污染符号；
   - 验证静态库总体积仍然稳定控制在 $\le 3.5\text{ MB}$；
3. **更新 CMake 供应链清单**：
   - 计算新生成的 `.zip` 静态归档包的 SHA-256，更新 [`cmake/RtxFfmpegStatic.cmake`](file:///C:/PUBLIC/[REDACTED_PROJ]/cmake/RtxFfmpegStatic.cmake)；
4. **门禁回归验证**：
   - 执行 `ctest --output-on-failure`：确保 137+ 项单元测试 100% PASS；
   - 执行 [`tools/audit_imports.ps1`](file:///C:/PUBLIC/[REDACTED_PROJ]/tools/audit_imports.ps1)：确保生成的单 EXE 导入表中 FFmpeg 动态库导入计数严格为 0；
   - 执行 1080p@60 离线与实时转码闭环压测。

---

## 4. 结论与备忘 (Summary & Sign-off)

1. **当前决断**：阶段三（v0.2.0）严格冻结在 **FFmpeg 7.1.1 手术刀级静态内联版本**，全力确保 Gate 3.0 与 Gate 3 验收达标；
2. **未来规划**：本技术备忘录作为 `docs/TODO/` 第 41 号规范存档，为未来 CineRTX 媒体层向 FFmpeg 8.x / 9.x 演进提供完整的决策基线与实施指引。
