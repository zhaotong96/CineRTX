# **CineRTX 纯 GPU 转码与非编引擎：FFmpeg 全量静态链接与单 EXE 自包含架构重构协助调查与专家咨询请求书**

**文档标识**：`DOSSIER-FFMPEG-STATIC-CONSOLIDATION-20261026`  
**呈报对象**：CineRTX 领域专家委员会（异构系统架构组 / 核心流水线工委会 / 开源法务与供应链审查组）  
**调查负责人**：CineRTX 工程组与系统架构团队  
**基准工程版本**：`CineRTX v0.1.0` (Git Commit: `32a396f`, Branch: `main`)  
**基准运行环境**：RTX 5060 Laptop GPU (sm_120, 8GB GDDR7, Driver 617.14, CUDA 13.4, Windows 11 24H2)  
**涉及核心源码与技术规范**：
- 构建控制中心：[`CMakeLists.txt`](file:///C:/PUBLIC/[REDACTED_PROJ]/CMakeLists.txt)
- 媒体解复用/复用层：[`src/io/ffmpeg_io.cpp`](file:///C:/PUBLIC/[REDACTED_PROJ]/src/io/ffmpeg_io.cpp)、[`include/rtx/io.h`](file:///C:/PUBLIC/[REDACTED_PROJ]/include/rtx/io.h)
- 导入表合规审计门禁：[`tools/audit_imports.ps1`](file:///C:/PUBLIC/[REDACTED_PROJ]/tools/audit_imports.ps1)
- 极简单 EXE 演进路线：[`docs/gui/RTX 转码引擎 Win32 极简原生单 exe GUI 演进路线.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/gui/RTX%20转码引擎%20Win32%20极简原生单 exe%20GUI%20演进路线.md)
- GUI 零依赖方案评估：[`docs/gui/RTX转码引擎GUI演进方案评估.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/gui/RTX转码引擎GUI演进方案评估.md)
- 开源合规声明：[`docs/spec/compliance.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/spec/compliance.md)

---

## 一、 架构缺陷定性与用户质疑深度复盘

### 1.1 用户核心批评与架构哲学审视

在 CineRTX 进入阶段三（Phase 3）工程筹备与端到端实机试用过程中，工程组在进行实际 MP4 视频转码测试时遭遇了 `ErrorCode::FfmpegMissing` 拦截，并试图通过引入本地外部 `ffmpeg-7.1.1-full_build-shared` 动态库及拷贝运行时 DLL（`rtx_copy_ffmpeg_runtime`）来绕过阻碍。对此，系统产品负责人与最终用户发出了极其严肃且一针见血的技术质问：

> *“就不该存在 `RTX_HAVE_FFMPEG` 这个变量，而是将 ffmpeg 引入静态链接，这才符合高内聚、自包含的单 exe 程序。如果当前 spec 哲学体现得不够充分，那说明文档亟需完善。那么，现在的程序架构是错误实现。出一个协查请求，请领域专家出一套静态链接 ffmpeg 的解决方案。”*

工程组经系统级深度反思与全量架构溯源后一致认为：**用户的批评完全正确，击中了当前代码库存在的根本性架构债务（Architectural Debt）与规范脱节（Doc-Code Drift）**。

### 1.2 当前实现的“三宗罪”：为什么说是错误实现？

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                               当前架构实现缺陷 vs 既定设计哲学对比                           │
├───────────────────────────────┬─────────────────────────────┬───────────────────────────────┤
│           架构维度            │      当前错误实现 (As-Is)    │     既定架构哲学 (To-Be)      │
├───────────────────────────────┼─────────────────────────────┼───────────────────────────────┤
│ 1. 核心能力的定位             │ • 将 FFmpeg 视为可选组件     │ • 媒体容器 I/O 是核心第一公民 │
│                               │ • 引入 RTX_HAVE_FFMPEG 选配宏│ • 严禁任何形式的降级存根/Stub │
│                               │ • 暴露 FfmpegMissing 运行时错│ • 引擎交付即具备完整媒体解封装│
├───────────────────────────────┼─────────────────────────────┼───────────────────────────────┤
│ 2. 二进制交付形态             │ • 依赖外部 Shared 动态库构建 │ • 100% 全量静态内联归档 (.lib)│
│                               │ • CMake 自动拷贝 8 个外挂 DLL│ • 双击即跑，零第三方 DLL 依赖 │
│                               │ • 体积碎片化，散落运行时目录 │ • 纯正单 EXE，高内聚、自包含  │
├───────────────────────────────┼─────────────────────────────┼───────────────────────────────┤
│ 3. 规范一致性 (Spec Drift)   │ • 代码充斥动态库链接兼容分支 │ • 严格遵循 docs/gui/ 专家裁决 │
│                               │ • 审计脚本漏检 FFmpeg 动态库 │ • audit_imports.ps1 强力拦截  │
└───────────────────────────────┴─────────────────────────────┴───────────────────────────────┘
```

1. **媒体核心能力的“选配化降级”错误**：
   CineRTX 是一台面向专业领域的“纯 GPU 视频重制与全 GPU 非线性编辑（NLE）引擎”。在多媒体处理流水线中，容器解封装（Demuxing）、音视频复用（Muxing）与音轨处理是**整个管线最基础的数据吞吐底座**。将其设计为 `option(RTX_WITH_FFMPEG)` 并在未找到库时退化为只返回错误的存根类 [`FfmpegNotCompiledDemuxer`](file:///C:/PUBLIC/[REDACTED_PROJ]/src/io/ffmpeg_io.cpp#L783)，是在架构抽象上本末倒置，将核心强依赖错误地降级为了可选插件。
2. **动态库拷贝对“单 EXE 自包含”哲学的背离**：
   在 [`CMakeLists.txt`](file:///C:/PUBLIC/[REDACTED_PROJ]/CMakeLists.txt#L189-L203) 中，存在一套所谓的 `rtx_copy_ffmpeg_runtime` 机制，在编译后强行将 `avcodec-61.dll`（89 MB）、`avformat-61.dll`（18.7 MB）、`avutil-59.dll`（2.8 MB）等一整套动态链接库拷贝到目标可执行文件旁边。这种设计彻底破坏了原生 Win32 单 exe 的便携性、健壮性与纯洁性，引入了 DLL 劫持、路径污染与运行环境缺失风险。
3. **工程实现与既定专家裁决的严重背离**：
   在已获批准的 [`docs/gui/RTX 转码引擎 Win32 极简原生单 exe GUI 演进路线.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/gui/RTX%20转码引擎%20Win32%20极简原生单 exe%20GUI%20演进路线.md) 与 [`docs/gui/RTX转码引擎GUI演进方案评估.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/gui/RTX转码引擎GUI演进方案评估.md) 中，专家委员会早已全票裁定：“**全量采纳方案 (a) — 静态单体二进制**”，并明确要求“**手术刀级裁剪的静态 FFmpeg 容器层，静态链接后单 EXE 仅依赖 Windows 自带核心系统 DLL（KERNEL32、USER32、GDI32、DWMAPI）**”。然而现存工程代码却因初期单元测试走便捷通道，残留了动态导入桩，导致实际代码未跟上设计规范。

---

## 二、 当前底层技术现状深度剖析

### 2.1 PE 导入表实测证据

针对当前已生成的发布二进制 [`build/RelWithDebInfo/rtx-transcode-gui.exe`](file:///C:/PUBLIC/[REDACTED_PROJ]/build/RelWithDebInfo/rtx-transcode-gui.exe)，通过执行底层的 [`tools/audit_imports.ps1`](file:///C:/PUBLIC/[REDACTED_PROJ]/tools/audit_imports.ps1) 静态导入分析器，捕获其真实导入表数据如下：

```text
Import-table audit
  [PE] C:\PUBLIC\[REDACTED_PROJ]\build\RelWithDebInfo\rtx-transcode-gui.exe
         dependencies: dwmapi.dll, SHELL32.dll, GDI32.dll, USER32.dll, KERNEL32.dll, 
                       MSVCP140.dll, VCRUNTIME140.dll, VCRUNTIME140_1.dll, 
                       api-ms-win-crt-runtime-l1-1-0.dll, api-ms-win-crt-heap-l1-1-0.dll...
         imported symbols: 257
         result: no NVIDIA static imports
```

**深层因果解构**：
- 当前 GUI 在外观上确实呈现为“纯单 exe”，其 PE 导入表没有任何第三方 DLL（如 Qt、WebView2 或 FFmpeg）。
- **然而这种“纯粹”是牺牲了媒体处理能力换来的假象**：因为在配置时 `RTX_HAVE_FFMPEG` 为 `OFF`，导致底层实际链接的是存根代码；一旦执行真正的容器输入，程序就会由于缺乏底层编解码器与解复用器代码而直接中断。

### 2.2 本地物料与链接模式错配

经核查，开发机当前安装的外部 FFmpeg 为 [`[REDACTED_USER]\program\ffmpeg-7.1.1-full_build-shared`](file:///C:/Users/[REDACTED_USER]/program/ffmpeg-7.1.1-full_build-shared)：
- 其 `lib/` 目录中的 `avcodec.lib` 仅 137 KB，`avformat.lib` 仅 127 KB。它们不是全量内联归档（Static Archive），而是动态导入桩（Import Stubs）。
- 若在 CMake 中粗暴指定该目录，链接器生成的 `.exe` 头部将不可避免地被打入 8 个 FFmpeg 动态 DLL 符号，迫使分发物料退化为散落的多文件文件夹。

### 2.3 开源法务边界再次穿透澄清

依据 [`docs/spec/compliance.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/spec/compliance.md) 与 [`COPYING.EXCEPTION`](file:///C:/PUBLIC/[REDACTED_PROJ]/COPYING.EXCEPTION) 条文：
- **Covered Work 区域**：CineRTX 核心调度器与 FFmpeg 容器层均属于 GPLv3 + Plugin-Exception 下的 Covered Work。FFmpeg（LGPLv2.1 或 GPLv3）作为开源库，**静态链接进入主可执行文件在法务上具有完全的合法性**。
- **Independent Module 区域**：根据例外条款第 0 节与第 3 节，**仅有 NVIDIA 的闭源专有驱动与 SDK**（`nvcuvid.dll`、`nvEncodeAPI64.dll`、`_nvngx.dll`）才必须严格遵循 `LoadLibraryExW` 运行时动态绑定，严禁静态链接。
- **结论**：将 FFmpeg 改为全量静态内联，不仅完全不违背 GPLv3 Plugin-Exception，反而是最纯粹地履行了 GPL 自由软件的完整自包含分发契约！

---

## 三、 提请领域专家委员会裁决与设计的五大核心议题

为了从根源上彻底铲除这一架构缺陷，使 CineRTX 的工程代码与 Spec 哲学实现 100% 的坚固自洽，系统架构团队特此向专家委员会呈报以下五大核心工程议题，恳请委员会出具规范级指导方案与可落地的实施代码。

### 3.1 议题一：彻底废除 `RTX_HAVE_FFMPEG` 条件宏与解封装存根的代码重构规约

- **现状痛点**：
  在 [`src/io/ffmpeg_io.cpp`](file:///C:/PUBLIC/[REDACTED_PROJ]/src/io/ffmpeg_io.cpp) 中存在巨幅的 `#ifdef RTX_HAVE_FFMPEG` 条件编译，并在 `#else` 分支维护了 `FfmpegNotCompiledDemuxer` 和 `FfmpegNotCompiledMuxer` 两个空桩，同时在 [`include/rtx/status.h`](file:///C:/PUBLIC/[REDACTED_PROJ]/include/rtx/status.h) 中定义了无意义的运行时错误码 `ErrorCode::FfmpegMissing`。
- **请示裁定**：
  1. 委员会是否核准**彻底删除 `RTX_HAVE_FFMPEG` 条件宏**？
  2. 是否核准**完全移除所有 `NotCompiled` 存根类及 `ErrorCode::FfmpegMissing` 枚举**，将 FFmpeg 媒体 I/O 提升为 `rtx_core` 的无条件强内联静态组件？
  3. 对于无多媒体封装的纯算法单元测试（如纯显存池或合成帧压测），如何重构测试入口，使其在保持静态内嵌 FFmpeg 的前提下优雅运行？

### 3.2 议题二：Windows 平台 MSVC 工具链下 FFmpeg 静态库工程化接入与深层依赖解析方案

- **现状痛点**：
  在 Windows 平台采用 Visual Studio 2022 (MSVC / C++20) 编译全静态多媒体工程时，静态链接由 MinGW 或 MSVC 构建的 FFmpeg 静态归档库（`.a` 或 `.lib`）面临严重的符号冲突与系统依赖暗坑：
  - **C 运行时库 (CRT) 冲突**：FFmpeg 静态编译时的 CRT 模式（`/MT` vs `/MD`）必须与 CineRTX 保持严丝合缝；
  - **庞大的 Windows 系统底层静态依赖**：FFmpeg 的 `libavformat`、`libavcodec`、`libavutil` 静态库在解析网络流、加密、多媒体框架时，深层依赖 `bcrypt.lib`、`ws2_32.lib`、`secur32.lib`、`shlwapi.lib`、`strmiids.lib`、`mfuuid.lib`、`ole32.lib`、`user32.lib` 等。缺少任何一个导入都将引发长达数百行的 `LNK2019` 未解析外部符号。
- **请示裁定**：
  1. 针对 Windows MSVC 19.40+ 编译器，推荐采用何种经过工业级验证的 FFmpeg 静态库构建产物形态（MSVC-compatible `.lib` 还是 GNU `.a`）？
  2. 请提供一份标准的 CMake 静态依赖链接靶标声明（如 `rtx_ffmpeg_static`），系统化解决所有深层 Win32 依赖库的传递与跨符号绑定。

### 3.3 议题三：手术刀级 FFmpeg 极简静态裁剪（Minimal Static Stripping）白名单与体积规约

- **现状痛点**：
  如果将未经裁剪的完整 FFmpeg 全量静态链接进工程，不仅编译极其耗时，而且单个 `rtx-transcode.exe` 体积将急剧膨胀至 80MB~120MB 以上，严重背离了 Win32 极简原生单 exe（< 10MB）的设计美学。
  事实上，由于 CineRTX 所有的视频重制、超分以及转码编解码全部由 GPU 原生硬件接口（NVDEC 与 NVENC）100% 承担，**引擎根本不需要任何 FFmpeg 的 CPU 视频解码器（如 libx264, libx265, libvpx, dav1d）！**
- **需求范围白名单定义**：
  - **解复用容器（Demuxers）**：仅需 MP4/MOV, Matroska (MKV/WebM), MPEG-TS, FLV, AVI；
  - **复用容器（Muxers）**：仅需 MP4, Matroska (MKV), MPEG-TS；
  - **码流过滤器（BSFs）**：仅需 H.264 (`h264_mp4toannexb`), HEVC (`hevc_mp4toannexb`), AV1 (`av1_metadata`)；
  - **音频处理**：基础音频解码（AAC, Opus, MP3, FLAC, PCM）与重采样（`libswresample`）；
  - **协议层**：仅保留本地文件 `file` 协议与管道 `pipe` 协议。
- **请示裁定**：
  请专家委员会出具一份权威的、经过生产环境验证的 FFmpeg `./configure` 手术刀级裁剪白名单编译指令（包含 `--disable-everything`、`--enable-small` 及各项白名单开启开关），确保编译产出的静态库总体积压缩在 2MB~3MB，最终生成的单 EXE 体积严格控制在 **8 MB – 12 MB** 黄金包络之内。

### 3.4 议题四：自动化静态物料供应链与构建系统（CMake）一体化改造

- **现状痛点**：
  目前构建如果本地没有配置静态库，工程便无法构建或退化为存根，这对开源社区协作者与本地开发造成了巨大的认知负担。
  在二阶段中，针对 NVIDIA RTX Video SDK，工程团队成功设计了基于 official CDN 的自动下载、SHA256 哈希校验与解压入构建树的自动化供应链（参见 `CMakeLists.txt` 301–330 行）。
- **请示裁定**：
  1. 委员会是否批准在 CMake 中建立统一的 `RTX_STATIC_FFMPEG` 供应链？（支持两种模式：① 用户指定本地裁剪静态库路径 `RTX_FFMPEG_SUBMODULE (third_party/ffmpeg)`；② 当未检测到本地静态库时，自动从预构建的 CineRTX 官方裁剪静态包归档源安全下载并校验接入）。
  2. 彻底拔除并永久删除 `CMakeLists.txt` 中的 `rtx_copy_ffmpeg_runtime` 函数与 `RTX_FFMPEG_DLL_DIR` 缓存变量。
  3. 如何升级 [`tools/audit_imports.ps1`](file:///C:/PUBLIC/[REDACTED_PROJ]/tools/audit_imports.ps1) 审计门禁，将任何形式的 FFmpeg 动态 DLL 符号（`avcodec-*.dll`, `avformat-*.dll` 等）加入强制失败黑名单，实现 CI 自动化拦截任何试图破坏单 exe 纯洁性的代码变更？

### 3.5 议题五：核心规范体系 (Spec) 的全局修正与阶段三收敛规划

- **现状痛点**：
  当前的部分技术文档存在表述含糊之处，未将“自包含单 EXE”上升为第一宪章级原则，致使后续开发中发生了妥协与反复。
- **请示裁定**：
  1. 请专家委员会指导对 [`docs/spec/architecture.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/spec/architecture.md)、[`docs/spec/compliance.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/spec/compliance.md) 及 [`docs/spec/gate3.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/spec/gate3.md) 进行针对性修订，将“**强制静态全量内联 FFmpeg 容器层，彻底杜绝外挂 DLL，构建绝对自包含单二进制**”正式确立为不可动摇的架构原则。
  2. 将该重构任务正式编列为阶段三（Phase 3）的前置 P0 级攻坚工序（工序代号：`ACT-PHASE3-PRE-STATIC-01`），必须全量验收通过后，方可启动后续双路补帧合流代码编写。

---

## 四、 推荐的施工实施纲要（草案）

系统架构团队初步拟定的整改工作分解结构（WBS）如下，呈报委员会审阅：

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                            FFmpeg 静态全量内联重构实施路线 (WBS)                             │
├───────────────────────┬─────────────────────────────────────────────────────────────────────┤
│ 工序代号              │ 核心施工任务与交付标准                                              │
├───────────────────────┼─────────────────────────────────────────────────────────────────────┤
│ ACT-STATIC-01         │ 静态裁剪库获取与预构建归档体系搭建：                                │
│ (物料与供应链)        │ • 运用委员会裁决的白名单脚本编译手术刀级静态库 (libavformat/codec)  │
│                       │ • 生成 SHA-256 签名，建立 CMake 自动探测与拉取校验通道              │
├───────────────────────┼─────────────────────────────────────────────────────────────────────┤
│ ACT-STATIC-02         │ CMakeLists.txt 架构脱胎重构：                                       │
│ (构建体系拔除妥协)    │ • 彻底删除 RTX_WITH_FFMPEG / RTX_HAVE_FFMPEG 宏开关                 │
│                       │ • 彻底删除 rtx_copy_ffmpeg_runtime 及所有动态 DLL 拷贝逻辑          │
│                       │ • 建立静态库靶标，规范挂载 bcrypt/ws2_32 等底层 Win32 依赖         │
├───────────────────────┼─────────────────────────────────────────────────────────────────────┤
│ ACT-STATIC-03         │ 源码解封装层纯化与存根清除：                                        │
│ (代码层清除存根)      │ • 彻底清除 src/io/ffmpeg_io.cpp 中的 #ifdef 存根与 NotCompiled 类   │
│                       │ • 剔除 ErrorCode::FfmpegMissing 错误定义，恢复媒体层强类型直通      │
├───────────────────────┼─────────────────────────────────────────────────────────────────────┤
│ ACT-STATIC-04         │ 导入表严苛审计与端到端实操验收：                                    │
│ (验证与合规锁定)      │ • 升级 tools/audit_imports.ps1，将 FFmpeg 动态库设为必挂黑名单      │
│                       │ • 独立裸机双击 rtx-transcode-gui.exe 跑通 4K MP4 实机转码           │
│                       │ • 修订 docs/spec/ 核心架构文档，全量锁定单 EXE 规范                 │
└───────────────────────┴─────────────────────────────────────────────────────────────────────┘
```

---

## 五、 请求决议事项清单

工程团队谨提请领域专家委员会针对本请求书签发正式批复，裁决要点包含：

1. **架构原则裁决**：正式确认将“FFmpeg 全量静态链接、单 EXE 绝对自包含、零外部媒体 DLL”确立为 CineRTX 不可逾越的核心架构规范；
2. **裁剪参数裁定**：提供生产级、最小化的 FFmpeg `--disable-everything` 静态裁剪编译参数白名单；
3. **构建体系设计**：提供在 MSVC 环境下优雅链接静态 FFmpeg 且消灭所有 Win32 深层依赖符号缺失的完整 CMake 规约；
4. **门禁指标核准**：核准将 FFmpeg 动态符号列入 `tools/audit_imports.ps1` 阻断黑名单，并设定单 EXE 最终体积上限门禁（建议 $\le 12$ MB）。

---
*呈报签署：CineRTX 系统架构团队与全栈工程组*  
*呈报时间：2026 年 10 月 26 日*
