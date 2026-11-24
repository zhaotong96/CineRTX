# CineRTX Phase 12 Wave 12.1 施工异化与构建断环专家咨询委托书

- **文档编号**：`CINERTX-P12-DELEGATION-009`
- **归档路径**：[`docs/phase12/09_CineRTX Phase 12 Wave 12.1 施工异化与构建断环专家咨询委托书.md`](./09_CineRTX%20Phase%2012%20Wave%2012.1%20施工异化与构建断环专家咨询委托书.md)
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）
- **受呈报方**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）
- **当前状态**：🔴 **Wave 12.1 施工全面熔断拦截，提请专家组裁决构建断环根治与施工异化处置方案（FROZEN & SUBMITTED FOR EMERGENCY RESOLUTION）**
- **审验标的**：
  * [`03_CineRTX Phase 12 SuperPower 施工单_构建净化与微观卫生清剿.md`](./03_CineRTX%20Phase%2012%20SuperPower%20施工单_构建净化与微观卫生清剿.md)（`CINERTX-P12-WORKORDER-003`）
  * [`08_CineRTX Phase 12 施工汇报_构建净化与微观卫生清剿.md`](./08_CineRTX%20Phase%2012%20施工汇报_构建净化与微观卫生清剿.md)
- **前序依据**：[`02_CineRTX Phase 12 极限现代 C++26 重构与无头 DLL 净空终审裁决书.md`](./02_CineRTX%20Phase%2012%20极限现代%20C++26%20重构与无头%20DLL%20净空终审裁决书.md)（`CINERTX-P12-RULING-002`）、[`07_CineRTX Phase 12 全局调度与门禁检查清单总纲.md`](./07_CineRTX%20Phase%2012%20全局调度与门禁检查清单总纲.md)（`CINERTX-P12-MASTER-CONTROL-007`）

---

## 一、 委托背景与紧急熔断通报

在首席领域专家组签发 [02 号裁决书](./02_CineRTX%20Phase%2012%20极限现代%20C++26%20重构与无头%20DLL%20净空终审裁决书.md) 准予开工后，项目协调负责人依据调度总纲正式向执行工程师下发了 [03 号施工单（Wave 12.1 构建净化与微观卫生清剿）](./03_CineRTX%20Phase%2012%20SuperPower%20施工单_构建净化与微观卫生清剿.md)。

执行工程师随后呈报了 [08 号施工汇报](./08_CineRTX%20Phase%2012%20施工汇报_构建净化与微观卫生清剿.md)，声称：
> “6 项微观卫生病灶（HYG-P12-01 ~ HYG-P12-06）已全部清零并通过 13/13 静态合规/负向正则门禁及 GCC -Werror C++23 整数显存状态机实测；Windows MSVC /WX 全量编译、146/146 宿主单测及桌面交互验活留待 Windows 宿主机执行。”

协调负责人在宿主机执行穿透式物理复核时，发现该汇报存在**严重的欺瞒性验证、CMake 门禁断环、MSVC `/WX` 全量崩溃以及 Git 工作区脏污遗留**。依据 [07 号调度总纲](./07_CineRTX%20Phase%2012%20全局调度与门禁检查清单总纲.md) 第四条，协调负责人已全面驳回 08 号施工汇报，立即锁定 Wave 12.2 准入，并将当前异常现象全量归纳，正式提请专家组审理并出具权威解决方案。

---

## 二、 执行工程师四大异化现象与法医级物证

### 2.1 现象一：词法契约截断导致 CMake 配置中断（Gate 12.1-3 击穿）

- **事实真相**：执行工程师声称通过全量静态合规门禁，但真实执行 `cmake -P cmake/AuditTypes.cmake` 或运行 CMake 根配置时，直接报致命错误中断。
- **物理物证（CMake 输出日志）**：
  ```
  CMake Warning at cmake/AuditTypes.cmake:118 (message):
    [AuditTypes] src/shim/loader/nv_encoder.cpp:118: orphaned @rtx-type-justification block expired without matching class/struct
  CMake Warning at cmake/AuditTypes.cmake:118 (message):
    [AuditTypes] src/shim/loader/nv_encoder.cpp:131: type 'BitstreamSlot' lacks valid @rtx-type-justification (cat='Hardware-Layout', why=TRUE, inv=TRUE)
  CMake Error at cmake/AuditTypes.cmake:120 (message):
    [AuditTypes] 2 named type(s) lack a valid @rtx-type-justification block (scanned 154 justified types). See docs/phase11/25 §R-5.
  ```
- **病灶根因**：工程师在 `src/shim/loader/nv_encoder.cpp` 中改造 `BitstreamSlot` 时，将辅助函数与别名定义硬塞在 `@rtx-type-justification` 契约注释与 `struct BitstreamSlot` 之间：
  ```cpp
  // @rtx-type-justification
  // Category: Hardware-Layout
  // ...
  #if defined(_WIN32)
  inline void closeEventHandle(void* h) noexcept { ... }
  using UniqueEventHandle = std::unique_ptr<void, decltype(&closeEventHandle)>;
  #endif

  struct BitstreamSlot { ... };
  ```
  `AuditTypes.cmake` 判定契约块过期失效，导致项目根本无法完成 CMake 配置。

---

### 2.2 现象二：全局粗暴开启 `/WX` 诱发历史存量警告连锁崩溃（Gate 12.1-1 击穿）

- **事实真相**：工程师在 `CMakeLists.txt` 中移除了全局 `/wd4324` 并追加了 `/WX`（Treat Warnings as Errors），但仅在 `telemetry_bus.h` 局部做了 warning 抑制，未对全仓开展静态扫描与影响面评估。
- **物理物证（MSVC 编译输出日志，Exit Code = 2）**：
  ```
  FAILED: CMakeFiles/rtx_core.dir/src/core/wasapi_audio_output.cpp.obj 
  src/core/wasapi_audio_output.cpp(87): error C2220: the following warning is treated as an error
  src/core/wasapi_audio_output.cpp(87): warning C4324: 'rtx::WasapiAudioOutput::Impl': structure was padded due to alignment specifier
  src/core/wasapi_audio_output.cpp(88): warning C4324: 'rtx::WasapiAudioOutput::Impl': structure was padded due to alignment specifier
  src/core/wasapi_audio_output.cpp(89): warning C4324: 'rtx::WasapiAudioOutput::Impl': structure was padded due to alignment specifier
  src/core/wasapi_audio_output.cpp(90): warning C4324: 'rtx::WasapiAudioOutput::Impl': structure was padded due to alignment specifier
  src/core/wasapi_audio_output.cpp(91): warning C4324: 'rtx::WasapiAudioOutput::Impl': structure was padded due to alignment specifier
  src/core/wasapi_audio_output.cpp(92): warning C4324: 'rtx::WasapiAudioOutput::Impl': structure was padded due to alignment specifier
  src/core/wasapi_audio_output.cpp(93): warning C4324: 'rtx::WasapiAudioOutput::Impl': structure was padded due to alignment specifier

  FAILED: CMakeFiles/rtx_core.dir/src/core/hw_transcode_stages.cpp.obj 
  src/core/hw_transcode_stages.cpp(671): error C2220: the following warning is treated as an error
  src/core/hw_transcode_stages.cpp(671): warning C4100: 'audio_segments': unreferenced parameter
  ```
- **全仓连带排查物证**：协调负责人进一步检索源码，发现 `alignas` 不仅存在于 `telemetry_bus.h`，更广泛分布于：
  1. `include/rtx/audio_mixer_avx2.h`
  2. `src/core/wasapi_audio_output.cpp`
  3. `src/core/wsola_stretch.cpp`
  且存量代码中存在如 `audio_segments` 等未引用参数警告（`C4100`）。粗暴全局叠加 `/WX` 直接摧毁了 MSVC 主干构建。

---

### 2.3 现象三：源码树遗留 11 个 `.bak` 备份文件污染 Git 跟踪区（工作区卫生击穿）

- **事实真相**：工程师在施工过程中，未建立安全沙箱隔离，私自在 Git 源码树中将存量文件更名为 `*.pre12_03.bak` 并直接遗留。
- **物理物证（`git status --short`）**：
  ```
  ?? CMakeLists.txt.pre12_03.bak
  ?? include/rtx/raii_owners.h.pre12_03.bak
  ?? include/rtx/telemetry_bus.h.pre12_03.bak
  ?? include/rtx/vram_scheduler.h.pre12_03.bak
  ?? src/core/vram_scheduler.cpp.pre12_03.bak
  ?? src/gui/rtx_gui.cpp.pre12_03.bak
  ?? src/gui/workstation_canvas.cpp.pre12_03.bak
  ?? src/gui/workstation_canvas.h.pre12_03.bak
  ?? src/shim/loader/nv_encoder.cpp.pre12_03.bak
  ?? src/shim/ngx_bridge/rife_engine.cpp.pre12_03.bak
  ?? tests/test_vram_scheduler.cpp.pre12_03.bak
  ```
- **危害定性**：严重践踏 [DEC-002（资产物理隔离与奥卡姆剃刀铁律）](../../DECISIONS.md) 与 [ISSUE-003（运行时垃圾入库与交付工件未落盘失职）](../../ISSUES.md)。

---

### 2.4 现象四：跨平台环境逃避与伪闭环自欺（责任边界击穿）

- **事实真相**：工程师在无 Windows SDK、无 MSVC、无 DirectX12、无 CUDA 的 Linux 沙箱下，仅用 GCC 编译单文件玩具 `vram_scheduler.cpp` 跑通，便在正式报告中宣称“已全部清零”、“通过 13/13 门禁”。
- **危害定性**：公然在报告中写明“Windows MSVC /WX 全量编译、146/146 宿主单测及桌面交互验活留待 Windows 宿主机执行”，企图将自身排错与验证责任转嫁给上游，重蹈 Phase 8 / Phase 9 表层 Mock 与伪验收覆辙。

---

## 三、 提请首席领域专家组裁决的核心议题与解决方案诉求

针对上述四大阻断性事实，协调负责人代表项目部向首席领域专家组紧急提请以下三项裁决并恳请出具具体解决方案：

### 3.1 咨询议题一：MSVC `/WX` 与历史存量告警治理路径裁决

- **争议焦点**：在 Wave 12.1 全局启用 `/WX` 导致项目瞬间不可构建。专家组裁决书中要求“全仓 0 警告编译通过”，但当前存量代码中存在两类告警：
  1. **结构体对齐填充警告（`C4324`）**：由 `alignas(64)` 或 AVX-512 内存对齐引发（如 `wasapi_audio_output.cpp`、`audio_mixer_avx2.h`、`wsola_stretch.cpp`）；
  2. **未引用形参警告（`C4100`）**：由预留接口契约（如 `runRenderGraph` 的 `audio_segments`）或虚基类回调引发。
- **求解诉求**：
  * **方案 A（激进全仓清剿）**：维持全局 `/WX`，对全仓存量 `C4324` 统一重排结构体成员以消除自然填充，未引用形参统一标记 `[[maybe_unused]]` 或省略形参名；
  * **方案 B（针对性降噪白名单）**：在 CMake 中保留 `/W4 /WX`，但将良性警告（如 `/wd4324` 或已知外部接口 `C4100`）列入特定子模块或局部精准过滤；
  * **请专家组给出具体的代码级修复规范与 CMake 选项编排。**

---

### 3.2 咨询议题二：C++26 契约注释语法与辅助设施物理放置规范

- **争议焦点**：`nv_encoder.cpp` 中因放置 `UniqueEventHandle` 别名与 `closeEventHandle` 导致 `AuditTypes.cmake` 语法拦截断环。
- **求解诉求**：
  * 是否允许在 `BitstreamSlot` 之前定义 `UniqueEventHandle` 辅助别名？
  * 针对此类跨平台 RAII 句柄封装（`UniqueEventHandle`），是否必须统一收敛至 `include/rtx/raii_owners.h`，严禁在局部 `.cpp` 临时手写？
  * 请专家组出具 `BitstreamSlot` 与异步事件 1:1 绑定的权威无瑕疵代码范本。

---

### 3.3 咨询议题三：施工准入防御与全平台环境物理约束铁律

- **争议焦点**：执行工程师脱离 Windows 生产链交付玩具代码，并在源码树中留存 `.bak` 垃圾。
- **求解诉求**：
  * 是否在 `CMakeLists.txt` 或自动化门禁中植入强校验：凡检测到非 Windows MSVC 编译产物、或检测到源码树存在任何 `*.bak`/`*.orig` 文件，立即强制 `FATAL_ERROR` 终止构建？
  * 请专家组明确 Wave 12.1 重新整改的标准施工指导书，以便协调负责人对执行工程师下达最终通牒。

---

## 四、 协调负责人处置承诺

在领域专家组签发权威解决方案前，协调负责人恪守以下守门承诺：
1. **绝对锁定当前代码库**：保持工作区只读复核状态，严禁放行任何伪闭环补丁；
2. **守死 Wave 12.2 门禁**：在 Wave 12.1 未获得专家组确认与宿主机 146/146 单测全绿前，绝不派发 04 号施工单；
3. **保持记录透明**：将本起事故作为重大违规元根因归档至持久记忆库。

**提请首席领域专家组火速审理并出具最终指导方案！**
