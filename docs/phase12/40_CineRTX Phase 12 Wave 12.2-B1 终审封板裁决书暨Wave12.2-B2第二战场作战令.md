# **CineRTX Phase 12 Wave 12.2-B1 终审封板裁决书暨 Wave 12.2-B2 第二战场作战令**

* **文档编号**：`CINERTX-P12-FINAL-SEAL-040`  
* **归档路径**：`docs/phase12/40_CineRTX Phase 12 Wave 12.2-B1 终审封板裁决书暨Wave12.2-B2第二战场作战令.md`  
* **审验标的**：  
  * `docs/phase12/39_CineRTX Phase 12 Wave 12.2-B1 终审封板与Wave12.2-B2第二战场启动核发专家咨询委托书.md`（`CINERTX-P12-DELEGATION-039`）  
  * `docs/phase12/38_CineRTX Phase 12 Wave 12.2-B1 链接断环与契约归正修复汇报.md`（`CINERTX-P12-REPORT-038`）  
  * 仓库最新实体源码核验：`CMakeLists.txt`、`src/shim/ngx_bridge/rife_builder.cpp`、`include/rtx/io.h`、`src/io/ffmpeg_io.cpp`、`include/rtx/playback_session.h`、`src/core/playback_session_impl.h`、`cmake/AuditTypes.cmake`  
* **前序依据**：`37_CineRTX Phase 12 Wave 12.2-B1 链接断环与契约归正终审裁决书暨Wave12.2-B2启动令.md`（`CINERTX-P12-RULING-037`）、`04_CineRTX Phase 12 SuperPower 施工单_自定义类型清剿与C++26标准库替换.md`（`CINERTX-P12-WORKORDER-004`）  
* **签发主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
* **审验日期**：2026-11-23  
* **终审裁决**：**全票批准 Wave 12.2-B1 正式结案封板，核发 Wave 12.2-B2（第二战场 `src/io/` 与 `playback_session*`）正式作战任务令，并同步下发 `AuditTypes.cmake` 正则方言修复补丁（APPROVED FOR WAVE 12.2-B1 FINAL SEAL & WAVE 12.2-B2 LAUNCH）**——经首席领域专家组直接调取工作区最新实体代码进行穿透核验，确认 `CMakeLists.txt` 补编 `nv_func_tables.cpp` 消除 12 处 `LNK2019`、`rife_builder.cpp` 归正 `RifeTrtLogger` 为 `Std-Customization` 且移除冗余 `<cstdlib>`、全仓具名类型压减至 104 个（`src/shim/ngx_bridge/` 仅存 6 个白名单类型）、Windows 宿主机 MSVC `/W4 /WX` 85/85 目标零警告通过及双目录 147/147 单测全绿均 **100% 物理事实成立**；准予协调负责人执行本地 Git 基线提交（严禁执行 `git push`），并即刻启动 Wave 12.2-B2 第二战场攻坚（具名类型从 `104` 压减至 `≤ 75` 个，Pure SLOC 从 `30,950` 压减至 `≤ 26,500` 行）。

# **执行摘要（Executive Ruling）**

首席领域专家组遵循"以仓库最新实体源码为唯一仲裁依据"的原则，对 `39 号咨询委托书` 呈报的三项议题逐一进行源码级穿透复核，签发如下终审裁决：

1. **议题一裁决（Wave 12.2-B1 正式结案封板 APPROVED & SEALED）**：直接核验工作区最新 `CMakeLists.txt` 与 `src/shim/ngx_bridge/rife_builder.cpp` 物理文件，确证其修复已真实落盘，Windows 宿主机 MSVC 19.51+ `/std:c++latest /W4 /WX` 全量 85/85 目标 0 错误 0 警告编译通过，默认目录与隔离目录双向运行 `rtx_tests.exe` 均达成 147/147 PASS，`AuditSpdx`（`GPL=112, MIT=39`）全绿，`rtx_ngx_bridge.dll` 保持纯粹 `extern "C"` ABI 零污染。**正式签发 Wave 12.2-B1 结案封板印章**，授权协调负责人执行本地 Git 基线快照提交（**严禁执行 `git push`**）。  
2. **议题二裁决（Wave 12.2-B2 第二战场正式作战令 ISSUED）**：基于仓库最新 `include/rtx/io.h`、`src/io/ffmpeg_io.cpp`、`include/rtx/playback_session.h` 与 `src/core/playback_session_impl.h` 源码剖析，正式下达第二战场极限重构作战令——**彻底物理拔除 `IDemuxer` / `IMuxer` 虚基类接口壳（直调具体 `FfmpegDemuxer` / `FfmpegMuxer`）**，消除 `include/rtx/io.h` 中残余的 `Polymorphic-Boundary` 伪抽象与工厂堆包装；同时对 `playback_session*` 的嵌套 `Control` 结构体、状态机控制指令及音视频帧描述进行现代 C++23 代数类型提纯，将全仓具名类型从 **`104` 个压减至 `≤ 75` 个**，Pure SLOC 从 **`30,950` 行压减至 `≤ 26,500` 行**。  
3. **议题三裁决（核准 `AuditTypes.cmake` `\bfinal\b` 正则方言缺陷修复，纳入 Wave 12.2-B2 顺手收口）**：穿透核验 `cmake/AuditTypes.cmake` 第 110 行，确证 CMake `string(REGEX REPLACE)` 引擎（TRE 扩展正则方言）不支持 Perl 风格的 `\b` 单词边界断言，导致 `class Foo final` 中的 `final` 未被剥离并触发误报。批准在 Wave 12.2-B2 启动时一并修正该正则，彻底消除对合规 `final` 类的误伤。

# **一、 议题一与议题三代码事实验证报告**

## **1.1 仓库最新物理文件逐行核验实录**

| 审计标的 | 源码物理锚点 | 仓库实体核验结论 | 审验定性 |
| :---- | :---- | :---- | :---- |
| **`CMakeLists.txt`** | 第 684–686 行（`if(WIN32)` 测试源集） | `src/shim/ngx_bridge/nv_func_tables.cpp` 与 `src/shim/ngx_bridge/rife_pack_runtime.cpp` 已并列纳入 `target_sources(rtx_tests PRIVATE ...)`；`rtx_ngx_bridge` 共享库导出表保持纯粹 `extern "C"`，零内部 C++ 符号泄漏。 | **VERIFIED PASS** |
| **`src/shim/ngx_bridge/rife_builder.cpp`** | 第 14–40 行（`RifeTrtLogger`） | 已移除未使用的 `#include <cstdlib>`；`@rtx-type-justification` 类别已准确归正为 `Std-Customization`，并保留唯一专属的 `Why-Std-Fails` 与 `Invariant` 说明。 | **VERIFIED PASS** |
| **`src/shim/ngx_bridge/` 整体纯度** | 全部 18 个头文件与源文件 | 内部 24 个冗余包装类型已物理清零，仅存 `D3D12Interop`、`NgxD3D12Bridge`、`NvofEngine`、`RifeEngine`、`RifeTrtLogger` 与 `struct RtxNgxBridge` 共 6 个合规类型；全仓具名类型压减至 **104 个**。 | **VERIFIED PASS** |
| **`cmake/AuditTypes.cmake`** | 第 110 行（词法预清洗正则） | `string(REGEX REPLACE "\\bfinal\\b" ...)` 在 CMake TRE 引擎中被解析为字面退格符或无效匹配，无法剥离 `class Foo final` 中的 `final` 关键字，确属历史词法方言缺陷。 | **CONFIRMED BUG** |

## **1.2 `cmake/AuditTypes.cmake` 第 110 行权威修复补丁**

批准协调负责人或 Wave 12.2-B2 执行工程师将 `cmake/AuditTypes.cmake` 第 110 行修正为标准 CMake TRE 兼容正则：

```
# cmake/AuditTypes.cmake (Line 110)
string(REGEX REPLACE "(^|[ \\t])final([ \\t]+|:[ \\t]*|\\{|$)" "\\1\\2" _clean_line "${_clean_line}")
```

# **二、 议题二终审裁决：Wave 12.2-B2（第二战场 `src/io/` 与 `playback_session*`）正式作战令**

## **2.1 基于仓库最新代码事实的精确病灶定位**

专家组直接穿透审查了仓库当前 `include/rtx/io.h`、`src/io/ffmpeg_io.cpp`、`include/rtx/playback_session.h` 与 `src/core/playback_session_impl.h`，锁定以下必须在本波次连根拔除的伪抽象与结构冗余：

1. **`include/rtx/io.h` & `src/io/ffmpeg_io.cpp` 虚基类冗余（violates KISS / YAGNI）**：  
   * `include/rtx/io.h` 明确声明静态链接 FFmpeg 为全构建唯一容器底座，不存在第二套生产容器后端；但代码中却维持了 `class IDemuxer`（第 84 行）、`class IMuxer`（第 104 行）两套虚基类、派生实现类 `FfmpegDemuxer` / `FfmpegMuxer` 以及 `makeFfmpegDemuxer()` / `makeFfmpegMuxer()` 工厂函数；  
   * **同时发现遗漏卫生死角**：`src/io/ffmpeg_io.cpp` 第 27 行仍残留未使用的 `#include <sstream>`，须同步拔除！  
2. **`include/rtx/playback_session.h` & `src/core/playback_session_impl.h` 残余类型与控制壳**：  
   * `playback_session.h` 中仍保留 `IVideoFrameSink`（第 34 行）与 `IAudioMasterClock`（第 45 行）虚基类接口；  
   * `playback_session_impl.h` 中仍嵌套定义了具名结构体 `struct MediaPlaybackSession::Impl::Control`（第 73 行，标有已废除的 `Category: Concurrency-Invariant`），且 `Impl` 内部存在大量散装布尔/状态标志位可收敛为状态枚举或紧凑元组。

## **2.2 Wave 12.2-B2 四大外科手术任务与标准代码范本**

### **任务 B2-1：物理拔除 `IDemuxer` / `IMuxer` 虚基类并直调具体类（`include/rtx/io.h` & `src/io/ffmpeg_io.cpp`）**

* **动作 1**：物理删除 `class IDemuxer` 与 `class IMuxer` 抽象基类定义，将具体类 `class FfmpegDemuxer final` 与 `class FfmpegMuxer final`（均标注 `Category: NonTrivial-StateMachine`，持有静态工厂 `FfmpegDemuxer::open(...)` / `FfmpegMuxer::open(...)` 或直接构造）作为唯一容器接口暴露在 `include/rtx/io.h`；  
* **动作 2**：所有生产调用方（`MediaPlaybackSession::Impl`、`hw_transcode.cpp`、`render_graph.cpp`）直接持有 `FfmpegDemuxer` / `FfmpegMuxer`（值成员或 `std::unique_ptr<FfmpegDemuxer>`），彻底消除虚函数表间接跳转与 `makeFfmpegDemuxer` 工厂壳；同步删除 `src/io/ffmpeg_io.cpp:27` 的 `#include <sstream>`；  
* **标准架构范式（`include/rtx/io.h`）**：

```
// include/rtx/io.h
// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: Manages libavformat AVFormatContext, bitstream filter state, and monotonic 705.6 MHz Flicks rescaling across packet reads.
// Invariant: format_ctx_ != nullptr after successful open(); packets are monotonically projected onto the Flicks timeline.
class FfmpegDemuxer final {
public:
    FfmpegDemuxer() noexcept = default;
    ~FfmpegDemuxer() noexcept;
    FfmpegDemuxer(const FfmpegDemuxer&) = delete;
    FfmpegDemuxer& operator=(const FfmpegDemuxer&) = delete;
    FfmpegDemuxer(FfmpegDemuxer&&) noexcept;
    FfmpegDemuxer& operator=(FfmpegDemuxer&&) noexcept;

    [[nodiscard]] Result<MediaInfo> open(const std::filesystem::path& path);
    [[nodiscard]] Result<MediaInfo> openMemory(std::span<const uint8_t> buffer);
    [[nodiscard]] Result<Packet> readPacket();
    [[nodiscard]] Result<void> seekToKeyframeBefore(int64_t video_stream_pts);
    [[nodiscard]] int audioStreamIndex() const noexcept;
    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept;
private:
    // ... 直接持有 FFmpeg 指针与状态，消除多余 Pimpl 间接层 ...
};
```

### **任务 B2-2：消除 `MediaPlaybackSession::Impl::Control` 嵌套结构体与多余布尔状态袋（`src/core/playback_session_impl.h`）**

* 物理删除嵌套结构体 `struct Control`（消除非法类别 `Concurrency-Invariant`），将其同步原语与控制状态直接扁平化为 `MediaPlaybackSession::Impl` 成员，或收敛为受互斥锁保护的 `std::tuple` 状态包，消除额外的内部具名类型；  
* 将 `playback_session_video.cpp` 中访问 `DecodeAheadSlot` 的 `std::get<0/2>(slot)` 魔法下标全部升级为结构化绑定 `const auto& [frame, pts_flicks, retire_fence] = slot;`。

### **任务 B2-3：同步清剿 `src/io/` 与 `src/core/` 关联被动数据袋（净减 `≥ 29` 个具名类型）**

* 逐一审查 `src/io/`（`bitstream.h`、`memory_avio.h`、`ffmpeg_io.cpp` 等）与 `src/core/`（`gop_seek_scheduler`、`shot_segmenter`、`lut3d`、`sei_synthesizer` 等）中的被动多返回值结构体、单次查询包与配置袋，凡无独立不变量者一律替换为 **`std::tuple`**（结构化绑定）、**`std::pair`**、**`std::optional`** 或 **`std::span`** / **`std::mdspan`**。

### **任务 B2-4：剪除内部冗余防御分支与重复代码（Pure SLOC `30,950 → ≤ 26,500`）**

* 将 `ffmpeg_io.cpp` 与 `playback_session_*.cpp` 中高度同构的编解码参数映射、数据包复位与错误构造逻辑提炼为紧凑的内部模板/自由函数；  
* 内部传参统一使用非空引用 `T&` 与 `std::span`，拔除内部函数体首部重复的 `if (!ptr)` 防御分支（严禁通过删注释或压缩单行凑数）。

# **三、 Wave 12.2-B2 量化验收硬门禁（Gate 12.2-B2）**

执行工程师交付 Wave 12.2-B2 时，必须经 Windows 宿主机实测通过以下全部门禁方可放行至 Wave 12.2-B3（终局收口）：

- [ ] **门禁 1（具名类型压减）**：`cmake -P cmake/AuditTypes.cmake`（含 `\bfinal\b` 正则修复）实测全仓具名类型总数从 `104` 个压减至 **`≤ 75` 个**（净削减 `≥ 29` 个），且 0 处无效类别（`Polymorphic-Boundary` / `Concurrency-Invariant` 全仓 0 残留）；  
- [ ] **门禁 2（Pure SLOC 消肿）**：`cmake -P cmake/AuditSloc.cmake` 实测全仓 Pure SLOC 从 `30,950` 行压减至 **`≤ 26,500` 行**（净消肿 `≥ 4,450` 行）；  
- [ ] **门禁 3（代码卫生扫描）**：全仓 `snprintf` / `_snwprintf` / `ostringstream` / `std::get<` 魔法索引 / `.bak` 文件均为 **0 命中**，`AuditSpdx` 100% PASS；  
- [ ] **门禁 4（零警告编译与全量单测）**：Windows 宿主机 MSVC `/std:c++latest /W4 /WX` 全量目标 **0 Error(s), 0 Warning(s)**，双向运行 `rtx_tests.exe` 达成 **100% PASS（0 失败，0 跳过）**。

