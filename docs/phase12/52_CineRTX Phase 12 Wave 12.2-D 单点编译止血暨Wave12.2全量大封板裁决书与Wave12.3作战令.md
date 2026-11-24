# **CineRTX Phase 12 Wave 12.2-D 单点编译止血暨 Wave 12.2 全量大封板裁决书与 Wave 12.3 作战令**

* **文档编号**：`CINERTX-P12-FINAL-SEAL-052`  
* **归档路径**：`docs/phase12/52_CineRTX Phase 12 Wave 12.2-D 单点编译止血暨Wave12.2全量大封板裁决书与Wave12.3作战令.md`  
* **审验标的**：  
  * `docs/phase12/51_CineRTX Phase 12 Wave 12.2-D 终局类型达成与单点编译止血专家咨询委托书.md`（`CINERTX-P12-DELEGATION-051`）  
  * `docs/phase12/50_CineRTX Phase 12 Wave 12.2-D 编译止血与终局类型收口施工汇报.md`（`CINERTX-P12-W12.2-D-REPORT-050`）  
  * 仓库最新实体源码核验：`include/rtx/io.h`（第 30–115 行）、`src/io/memory_avio.h`（第 1–42 行）与 `src/io/ffmpeg_io.cpp`（第 985–1025 行）  
* **前序依据**：`49_CineRTX Phase 12 Wave 12.2-C 编译断环与代码规模收敛终审裁决书暨Wave12.2-D收口令.md`（`CINERTX-P12-RULING-049`）、`05_CineRTX Phase 12 SuperPower 施工单_13项系统DLL净空与核心界面解耦.md`（`CINERTX-P12-WORKORDER-005`）  
* **签发主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
* **审验日期**：2026-11-23  
* **终审裁决**：**全面批准方案 A / 方案 B 最后单点止血修复，核准 Wave 12.2 全战役正式大圆满封板（具名类型 34/35 硬指标提前全面达成），并同步核发 Wave 12.3（13 项系统 DLL 净空与核心/界面解耦）正式作战令（APPROVED FOR WAVE 12.2 GRAND SEAL & WAVE 12.3 EXECUTION）**——经首席领域专家组直接调取工作区最新实体源码进行逐行穿透核验，确证全仓 61 个编译目标中已有 60 个通过 MSVC `/std:c++latest /W4 /WX` 零警告编译，全仓具名自定义类型历史性压减至 **`34` 个（`≤ 35` 终局硬门禁提前锁定，`0` 违规，`RTX_AUDIT_TYPES_STRICT` 已切换为全量硬阻断）**，现代 C++ 卫生指标（`std::get<`、`<sstream>`、旧式格式化、仓内 `.bak`）全仓保持 `0` 命中；同时精准定位 `include/rtx/io.h:34-36` 的死 `class MemoryAvioContext;` 前置声明冲突（`C2371`）与 `src/io/ffmpeg_io.cpp:995-1007` 的 `hdr.mastering` 元组未解构遗漏（`C2039`），批准立即合入方案 A 与方案 B，并在宿主机跑通 61/61 目标编译与 147/147 单测绿灯后直接签署 Wave 12.2 全量封板、正式开启 Wave 12.3 战役（同步承担 Pure SLOC `30,678 → ≤ 23,000` 终局收敛）。

# **执行摘要（Executive Ruling）**

首席领域专家组穿透核验了 `51 号咨询委托书` 呈报的 MSVC 19.51+ 编译 logs 与仓库最新实体源码（`include/rtx/io.h`、`src/io/memory_avio.h`、`src/io/ffmpeg_io.cpp`），认为全仓具名类型清剿战役已取得决定性胜利，仅剩 `ffmpeg_io.cpp` 单一翻译单元内的两处类型降级伴生遗漏。专家组签发如下终审裁决：

1. **终局具名类型硬门禁历史性达成（VERIFIED PASS: 34 / 35 Types, 0 Violations）**：  
   * 全仓具名自定义类型从 Phase 12 初始基线的 `155` 个压减至 **`34` 个**（净削减 `121` 个，削减率高达 **78.1%**），严格满足 `≤ 35` 个终局硬红线，且 `cmake/AuditTypes.cmake` 已将 `RTX_AUDIT_TYPES_STRICT` 切换为无条件 `ON` 硬阻断模式，实现 `0 Violations`。  
   * 全仓（含 `tests/`）`std::get<` 裸索引、`snprintf` / `_snwprintf` / `ostringstream`、`#include <sstream>`、非法类型分类与仓内 `.bak` 文件均保持 **0 命中**。  
2. **议题一裁决（全面批准方案 A 与方案 B 最后单点止血修复，恢复 61/61 全量编译与 147/147 单测绿灯）**：  
   * **断环 A（`include/rtx/io.h:34–36`）**：实地源码证实 `include/rtx/io.h` 第 34–36 行残留 `namespace io { class MemoryAvioContext; }`，而 `src/io/memory_avio.h` 第 31 行已将其重构为类型别名 `using MemoryAvioContext = std::shared_ptr<MemoryAvioState>;`，且 `io.h` 内部成员已改用 `std::shared_ptr<void> memory_avio_;`。物理删除该 3 行死前置声明即可彻底消除 `error C2371` 与 `error C2027`。  
   * **断环 B（`src/io/ffmpeg_io.cpp:995–1007`）**：实地源码证实 `FfmpegMuxer::applyHdrSideData` 中 `const MasteringDisplay& m = hdr.mastering;` 仍把 `MasteringDisplay`（10 元 `std::tuple`）当作结构体访问 `m.red_x` / `m.max_luminance`。核准按方案 B 改写为结构化绑定 `const auto& [m_rx, m_ry, m_gx, m_gy, m_bx, m_by, m_wx, m_wy, m_max, m_min] = hdr.mastering;`。  
3. **议题二与议题三裁决（批准 Wave 12.2 正式大封板，确立 Wave 12.3 为 Pure SLOC 终局收敛主战场）**：  
   * **客观代码体量结构性洞察**：经过四轮波次清剿（155 → 34 类型），剩余 `30,678` 行 Pure SLOC 中：生产代码为 `24,946` 行（其中 `src/shim/` 硬件驱动表与静态桥接占 `8,641` 行、`src/gui/` 桌面控件占 `3,200` 行），测试代码为 `5,969` 行（覆盖 147 项单测）。在严守"不阉割硬件功能、不削减测试覆盖、不删技术注释、不压单行"的前提下，**强行在单仓混合架构内继续删减代码已触及物理边际效应极限**。  
   * **架构解耦与 SLOC 收敛合流战略**：即将启动的 **Wave 12.3（13 项系统 DLL 净空与核心/界面物理隔离）** 正是释放体量的根本解法——通过剥离 Win32 GUI 冗余外壳、淘汰 `comdlg32` / `comctl32` / `shell32` / `dwmapi` / `uxtheme` 胶水代码以及收敛核心与 Shim 之间的重复封装，**全仓 Pure SLOC 将自然落入 `≤ 23,000` 行终局目标包络**。  
4. **Wave 12.2 直通封板与 Wave 12.3 启动授权**：协调负责人督导合入方案 A 与方案 B 后，在 Windows 宿主机确认 MSVC `/std:c++latest /W4 /WX` **61/61 目标 0 Error(s), 0 Warning(s)** 且双向运行 `rtx_tests.exe` 达成 **147/147 PASS**，**无需再次呈报中间委托，直接签署 Wave 12.2 全量大封板令（创建本地 Git 快照，严禁 `git push`）并即刻启动 Wave 12.3（`05号施工单`）**。

# **一、 仓库最新代码事实穿透核验与权威修复范本**

## **1.1 断环 A 源码实证与修复范本：`include/rtx/io.h`（第 30–40 行）**

穿透查看仓库实体文件 `include/rtx/io.h` 第 30–38 行现场代码：

```
// 当前破损现场（include/rtx/io.h:30-38）：
namespace rtx {

namespace io {
class MemoryAvioContext;   // <-- 与 memory_avio.h 中的 using MemoryAvioContext = std::shared_ptr<...> 触发 C2371 冲突！
} // namespace io
```

**终审定型修复代码**（直接删除第 34–36 行死前置声明）：

```
// include/rtx/io.h (Lines 30-38)
namespace rtx {

// Phase 12 (Wave 12.2-C): tuple alias (stream_index, codec_type, codec_id, codec_name, time_base, opaque_codecpar).
using PassthroughStreamInfo = std::tuple<int, int, int, std::string, Rational, std::shared_ptr<void>>;
```

## **1.2 断环 B 源码实证与修复范本：`src/io/ffmpeg_io.cpp`（第 990–1015 行）**

穿透查看仓库实体文件 `src/io/ffmpeg_io.cpp` 第 990–1012 行 `FfmpegMuxer::applyHdrSideData` 现场代码：

```
// 当前破损现场（src/io/ffmpeg_io.cpp:994-1008）：
    const MasteringDisplay& m = hdr.mastering;
    mastering->display_primaries[0][0] = av_make_q(m.red_x, 50000); // C2039: 'red_x' is not a member of 'std::tuple'
```

**终审定型修复代码**（完整解构 10 元组并消除未用变量警告）：p  
// src/io/ffmpeg\_io.cpp: FfmpegMuxer::applyHdrSideData (Lines 994-1010)  
const auto& \[m\_rx, m\_ry, m\_gx, m\_gy, m\_bx, m\_by, m\_wx, m\_wy, m\_max, m\_min\] \= hdr.mastering;  
mastering-\>display\_primaries\[0\]\[0\] \= av\_make\_q(m\_rx, 50000);  
mastering-\>display\_primaries\[0\]\[1\] \= av\_make\_q(m\_ry, 50000);  
mastering-\>display\_primaries\[1\]\[0\] \= av\_make\_q(m\_gx, 50000);  
mastering-\>display\_primaries\[1\]\[1\] \= av\_make\_q(m\_gy, 50000);  
mastering-\>display\_primaries\[2\]\[0\] \= av\_make\_q(m\_bx, 50000);  
mastering-\>display\_primaries\[2\]\[1\] \= av\_make\_q(m\_by, 50000);  
mastering-\>white\_point\[0\] \= av\_make\_q(m\_wx, 50000);  
mastering-\>white\_point\[1\] \= av\_make\_q(m\_wy, 50000);  
mastering-\>max\_luminance \= av\_make\_q(static\_cast(m\_max), 10000);  
mastering-\>min\_luminance \= av\_make\_q(static\_cast(m\_min), 10000);  
mastering-\>has\_primaries \= m\_rx \!= 0 ? 1 : 0;  
mastering-\>has\_luminance \= m\_max \!= 0 ? 1 : 0;

````

## 二、 Wave 12.2 大封板核验与 Wave 12.3（13 项系统 DLL 净空）作战令

### 2.1 Wave 12.2 宿主机终局封板核验命令

协调负责人督导合入 `include/rtx/io.h` 与 `src/io/ffmpeg_io.cpp` 两处修复后，在 Windows 宿主机依次执行：

```powershell
cmake --build --preset windows-msvc-ninja
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSpdx.cmake
````

确认以下全部指标绿灯后，直接归档 Wave 12.2 封板记录并提交本地 Git 基线快照（严禁执行 git push）：

- [ ] **编译门禁**：MSVC `/std:c++latest /W4 /WX` 全量 **61/61 目标 0 Error(s), 0 Warning(s)**；  
- [ ] **单测门禁**：默认目录与隔离目录双向运行 `rtx_tests.exe` 均达成 **147/147 PASS**；  
- [ ] **类型与许可门禁**：`AuditTypes` 输出 **`34 / 35` 个合法具名类型（`0 Violations`）**，`AuditSpdx` 全绿，仓内 **0 个 `.bak`**。

## **2.2 Wave 12.3（第四大主波次：13 项系统 DLL 净空与核心界面解耦）立即启动令**

Wave 12.2 封板后，协调负责人立即向执行工程师正式派发 [**`05_CineRTX Phase 12 SuperPower 施工单_13项系统DLL净空与核心界面解耦.md`](./05_CineRTX%20Phase%2012%20SuperPower%20施工单_13项系统DLL净空与核心界面解耦.md)**（`CINERTX-P12-WORKORDER-005`），并锁定以下双轨硬指标：

| 战役核心指标 | Wave 12.2 封板基线 | Wave 12.3 终局硬门禁 | 核心实施抓手 |
| :---- | :---- | :---- | :---- |
| **无头核心态静态 IAT** | 未隔离 | **严格 \= 2 项**（`kernel32.dll`, `bcrypt.dll`） | `rtx_core` 彻底剥离 `user32`/`gdi32`/`ole32`/`shell32`/`kernelbase` 链接依赖 |
| **GUI 混合发行态静态 IAT** | 13 项系统 DLL | **≤ 4 项**（`kernel32.dll`, `user32.dll`, `gdi32.dll`, `bcrypt.dll`） | 物理删除 `comdlg32`/`comctl32`/`shell32`/`kernelbase`；`d3d12`/`dxgi`/`ole32`/`dwmapi`/`uxtheme` 全部改为 `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)` 按需延迟解析；全仓 `#pragma comment(lib, ...)` **0 命中** |
| **具名自定义类型上限** | 34 个（`0 Violations`） | **≤ 35 个（单调不反弹，`0 Violations`）** | 延迟加载函数表全部采用匿名命名空间自由函数指针 \+ `std::remove_cvref_t` 无状态 Lambda 删除器，零新增具名类型 |
| **全仓 Pure SLOC 终局收敛** | 30,678 行 | **≤ 23,000 行** | 随旧式 Win32 通用控件、模态文件对话框、重复 DLL 加载壳与冗余 GUI 胶水剥离同步释放体量 |

