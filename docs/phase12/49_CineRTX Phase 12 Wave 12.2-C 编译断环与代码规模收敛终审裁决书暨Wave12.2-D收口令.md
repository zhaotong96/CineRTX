# **CineRTX Phase 12 Wave 12.2-C 编译断环与代码规模收敛终审裁决书暨 Wave 12.2-D 收口令**

* **文档编号**：`CINERTX-P12-RULING-049`  
* **归档路径**：`docs/phase12/49_CineRTX Phase 12 Wave 12.2-C 编译断环与代码规模收敛终审裁决书暨Wave12.2-D收口令.md`  
* **审验标的**：  
  * `docs/phase12/48_CineRTX Phase 12 Wave 12.2-C 编译断环与代码规模收敛专家咨询委托书.md`（`CINERTX-P12-DELEGATION-048`）  
  * `docs/phase12/47_CineRTX Phase 12 Wave 12.2-C 施工汇报.md`（`CINERTX-P12-W12.2-C-REPORT-047`）  
  * 仓库最新实体源码核验：`src/io/ffmpeg_io.cpp`（第 180–238 行）与 `src/gui/preview_viewport.cpp`（第 1–20 行、第 545–565 行）  
* **前序依据**：`46_CineRTX Phase 12 Wave 12.2-B2 终审封板裁决书暨Wave12.2-C第三战场作战令.md`（`CINERTX-P12-FINAL-SEAL-046`）、`04_CineRTX Phase 12 SuperPower 施工单_自定义类型清剿与C++26标准库替换.md`（`CINERTX-P12-WORKORDER-004`）  
* **签发主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
* **审验日期**：2026-11-23  
* **终审裁决**：**全面批准方案 A / 方案 B 两项编译止血修复，并针对 Pure SLOC 规模收敛下达"半顺延 \+ 专项结构性消肿（Wave 12.2-D）"双轨裁决（APPROVED FOR IMMEDIATE COMPILE FIX & STRUCTURAL SLOC REDUCTION）**——经首席领域专家组直接调取工作区最新实体源码进行逐行穿透核验，确证 `src/io/ffmpeg_io.cpp` 第 220–231 行存在未声明标识符 `hp` / `ht` / `hm` 及第 227 行多余闭合花括号 `}` 导致的级联编译崩溃，且 `src/gui/preview_viewport.cpp` 顶部确实缺失 `#include <bit>`；批准立即合入方案 A 与方案 B 恢复 Windows MSVC `/std:c++latest /W4 /WX` 零警告构建与 147/147 单测绿灯；同时针对 Pure SLOC（实测 `30,105` 行，距 `≤ 26,500` 门禁差额 `3,605` 行），**否决无条件全额顺延（选项甲）**，确立\*\*"先止血封板 Wave 12.2-C 类型战果（47 个具名类型，超额达成 ≤ 50 目标），随即设立短平快的 Wave 12.2-D 结构性消肿冲刺（通过折叠重复测试夹具与表驱动化 GUI/FFmpeg 样板真实削减 ≥ 7,105 行，直达 ≤ 23,000 行与 ≤ 35 类型终局红线）"\*\*，为进入 Wave 12.3（13 项系统 DLL 净空）铺平道路。

---

# **执行摘要（Executive Ruling）**

首席领域专家组穿透核验了 `48 号咨询委托书` 呈报的 MSVC 19.51+ 构建 logs 与仓库最新实体源码（src/io/ffmpeg\_io.cpp 与 src/gui/preview\_viewport.cpp），签发如下四项终审裁决：

1. **Wave 12.2-C 类型清剿成果核准（VERIFIED PASS ON TYPES）**：核准全仓具名自定义类型由 `73` 个进一步压减至 **`47` 个**（净削减 26 个，超额达成 `≤ 50` 阶段硬指标），`Polymorphic-Boundary` 与 `Concurrency-Invariant` 非法分类保持 `0` 项，`std::get<` 裸索引、`snprintf` / `_snwprintf` / `ostringstream` 与 `<sstream>` 全仓保持 `0` 命中，仓内零 `.bak` 残留。  
2. **议题一裁决（全面批准方案 A 与方案 B 定点止血补丁，立即恢复全仓编译与 147/147 单测）**：  
   * **阻断 A（`src/io/ffmpeg_io.cpp:219–232`）**：实地源码查明 `mergeHevcBitstreamMetadata` 中第 192 行解构 `info.color` 的作用域仅限 `if (!container_primaries && vui_primaries)` 块内，后续 `if (info.hdr.enabled)` 块内不仅遗漏重新解构 `info.color` 并误写了不存在的 `hp` / `ht` / `hm`，还在第 227 行 `hm = MatrixCoefficients::BT2020NC; }` 处多写了一个闭合花括号 `}` 提前终结了整个函数。核准按方案 A 完整修复并清理缩进。  
   * **阻断 B（`src/gui/preview_viewport.cpp`）**：实地源码查明顶部仅包含 `"preview_viewport.h"`、`<utility>` 与 `"preview_tone_map_cs.h"`，确未包含 C++20 `<bit>`（也未显式包含 `<array>` / `<cstdint>`，依赖间接传递）。核准立即显式补齐 `#include <bit>`（并建议显式补齐 `#include <array>` 与 `#include <cstdint>` 消除传递依赖隐患）。  
3. **议题二裁决（否决无条件全额顺延，采纳"先止血封板 12.2-C \+ 设立 Wave 12.2-D 结构性消肿专项"双轨策略）**：  
   * **为什么不能无条件顺延至 Wave 12.3？** Wave 12.3 的核心使命是 **13 项 Windows 系统 DLL 净空与核心/界面物理隔离**（将静态 IAT 压减至 2 项 / ≤ 4 项），若带着 `30,105` 行体量与剩余 `47` 个类型（距终局 `≤ 23,000` 行相差 `7,105` 行、距 `≤ 35` 类型相差 `12` 个）直接跨入 Wave 12.3，将导致 Wave 12.3 再次陷入范围过载。  
   * **结构性消肿抓手（严禁删注释或压单行）**：协调组精准定位出三大体量高地——① `tests/` 下 24 个文件（约 `13,000` 行）存在大量重复的音视频缓冲构造与断言样板；② `src/gui/rtx_gui.cpp` & `workstation_canvas.cpp`（约 `1,800` 行）存在大量重复 Win32 控件注册与布局调用；③ `src/io/ffmpeg_io.cpp` & `src/core/`（约 `1,100+` 行）存在重复错误分支与残留过度抽象。通过**泛型测试夹具工厂**、**全表驱动 GUI 控件创建器**与**剩余 12+ 个类型归并**，可在保持 100% 注释与可读性的前提下真实削减 **`7,200+` 行 Pure SLOC**。  
4. **Wave 12.2-C 封板与 Wave 12.2-D 启动授权**：合入方案 A 与方案 B 并在 Windows 宿主机确认 MSVC `/std:c++latest /W4 /WX` 全量构建 `0 Error(s), 0 Warning(s)` 及双目录 `rtx_tests.exe` `147/147 PASS` 后，**无需再报中间委托，直接签署 Wave 12.2-C 封板（提交本地 Git 快照，严禁 `git push`）并立即启动 Wave 12.2-D 终局收口冲刺**。

---

# **一、 仓库最新代码事实穿透核验与权威修复代码**

## **1.1 阻断 A 源码实证与修复范本：`src/io/ffmpeg_io.cpp`（第 218–235 行）**

穿透查看仓库实体文件 `src/io/ffmpeg_io.cpp` 第 218–233 行现场代码：

```
// 当前破损现场（src/io/ffmpeg_io.cpp:218-233）：
    if (info.hdr.enabled) {
        if (!has_primaries) {
            hp = ColorPrimaries::BT2020;       // 错误1：hp 未声明
        }
        if (!has_transfer) {
            ht = TransferFunction::SMPTE2084;  // 错误2：ht 未声明
        }
        if (!has_matrix) {
            hm = MatrixCoefficients::BT2020NC; } // 错误3：hm 未声明 + 多余闭合花括号 '}' 提前结束函数！
    }
    info.hdr.color = info.color;
    }                                          // 此处才是原函数闭合括号，已变成孤立语法错误
    info.hdr_source = info.hdr.enabled;
}
```

**终审定型修复代码**（直接结构化绑定 `info.color` 四元组并移除多余闭合花括号）：

```
// src/io/ffmpeg_io.cpp (Lines 218-235)
    if (info.hdr.enabled) {
        auto& [c_pri, c_trc, c_mat, c_rng] = info.color;
        (void)c_rng;
        if (!has_primaries) {
            c_pri = ColorPrimaries::BT2020;
        }
        if (!has_transfer) {
            c_trc = TransferFunction::SMPTE2084;
        }
        if (!has_matrix) {
            c_mat = MatrixCoefficients::BT2020NC;
        }
        info.hdr.color = info.color;
        // HDR10 signals that omit colour metadata still require the PQ
        // container description; fill only the gaps so an explicit HLG or
        // BT.2020 declaration survives untouched.
    }
    info.hdr_source = info.hdr.enabled;
}
```

---

## **1.2 阻断 B 源码实证与修复范本：`src/gui/preview_viewport.cpp`（第 1–16 行）**

穿透查看仓库实体文件 `src/gui/preview_viewport.cpp` 第 1–15 行，确认其仅包含了 `"preview_viewport.h"` 与 `<utility>`，缺少 `<bit>`（及显式 `<array>` / `<cstdint>`）。

**终审定型修复代码**：

```
// src/gui/preview_viewport.cpp (Lines 10-16)
#include "preview_viewport.h"

#include <array>
#include <bit>
#include <cstdint>
#include <utility>

#include "preview_tone_map_cs.h"
```

---

# **二、 Wave 12.2-C 封板核验与 Wave 12.2-D（终局类型与 Pure SLOC 收口）作战令**

## **2.1 Wave 12.2-C 宿主机封板核验命令**

协调负责人督导合入上述两处修复后，在 Windows 宿主机依次执行：

```
cmake --build --preset windows-msvc-ninja
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSpdx.cmake
```

确认 MSVC `/std:c++latest /W4 /WX` **0 Error(s), 0 Warning(s)**、双目录 **`147/147 PASS`** 且 `AuditTypes` 合法类型为 **47 个（`≤ 50` 阶段达标）** 后，直接签署 Wave 12.2-C 封板并在本地执行 `git commit` 快照（**严禁执行 `git push`**）。

---

## **2.2 Wave 12.2-D（短平快专项：结构性代码消肿与终局门禁收口）作战指令**

Wave 12.2-C 封板后，协调负责人立即下发 **Wave 12.2-D 终局收口任务**（严禁删注释、严禁压成单行），通过三大结构性重构杠杆一举击穿 Phase 12 核心源码指标红线：

| 消肿杠杆 | 靶向目录与文件 | 具体重构规范（现代 C++23/26 范式） | 预期消肿收益 |
| :---- | :---- | :---- | :---- |
| **1\. 测试夹具与重复断言模板化折叠** | `tests/test_*.cpp`（24 个测试文件，约 `13,000` 行） | 提取共享测试辅助头文件 `tests/test_helpers.h`：将重复的 NV12/P010 测试帧生成器、模拟媒体流封装器、DXGI/VRAM 遥测快照构造器与 `Result<T>` 断言宏折叠为泛型助手模板，消除 24 个测试文件间高度雷同的百行级 setup 样板代码。 | **`-4,200 ~ -4,800` Pure SLOC** |
| **2\. Win32 GUI 控件创建与布局表驱动化** | `src/gui/rtx_gui.cpp` & `src/gui/workstation_canvas.cpp`（约 `1,800` 行） | 将重复的 `CreateWindowExW` 控件创建调用收敛为 `constexpr std::array` 控件描述符表 \+ 单一循环创建器；统一 `GetDlgItem` \+ `MoveWindow` / `SetDlgItemTextW` 泛型助手模板，消除冗长重复 UI 样板。 | **`-1,200 ~ -1,500` Pure SLOC** |
| **3\. 剩余 12+ 个类型归并与内部防御减枝** | `include/rtx/`、`src/core/`、`src/io/ffmpeg_io.cpp` | 将剩余 `47` 个具名类型中仍可合并的同构领域结构（如 `RenderGraphSegment` 与 `AudioGraphSegment` 等）及内部非空引用边界下的冗余判空/重复错误分支进一步提纯，并将 `cmake/AuditTypes.cmake` 切换为全量硬阻断模式。 | **`-1,500 ~ -1,800` Pure SLOC具名类型 `47 → ≤ 35` 个** |

## **2.3 Wave 12.2-D 终局验收硬门禁（准入 Wave 12.3 前置条件）**

- [ ] **门禁 1（具名自定义类型终局达标）**：`cmake -P cmake/AuditTypes.cmake` 实测全仓合法具名类型 **`≤ 35` 个**（0 违规、0 孤立声明、0 非法分类）；  
- [ ] **门禁 2（Pure SLOC 终局达标）**：`cmake -P cmake/AuditSloc.cmake` 实测全仓 Pure SLOC **`≤ 23,000` 行**（保持完整契约与技术注释，无假压缩）；  
- [ ] **门禁 3（零警告编译与双向单测全绿）**：Windows 宿主机 MSVC `/std:c++latest /W4 /WX` **0 Error(s), 0 Warning(s)**，双目录 `rtx_tests.exe` **147/147 PASS**，`AuditSpdx` 全绿，仓内 **0 个 `.bak`**；  
- [ ] **门禁 4（直通解锁 Wave 12.3）**：上述三项达标后，立即解锁并派发 **`05_CineRTX Phase 12 SuperPower 施工单_13项系统DLL净空与核心界面解耦.md`**（冲击无头静态 IAT \= 2 项 / 桌面静态 IAT ≤ 4 项）。

