# **CineRTX Phase 12 Wave 12.3 动态加载断环与导入表审计终审裁决书暨封板授权令**

**多媒体系统工程与 GPU 异构计算首席领域专家组** \| 终审裁决与封板授权

---

## **基础元数据**

| 属性项 | 内容详细 |
| :---- | :---- |
| **文档编号** | `CINERTX-P12-FINAL-SEAL-055` |
| **归档路径** | `docs/phase12/55_CineRTX Phase 12 Wave 12.3 动态加载断环与导入表审计终审裁决书暨封板授权令.md` |
| **审验标的** | • `docs/phase12/54_CineRTX Phase 12 Wave 12.3 动态加载断环与导入表审计专家咨询委托书.md` (`CINERTX-P12-DELEGATION-054`)• `docs/phase12/53_CineRTX Phase 12 Wave 12.2 终局止血暨Wave12.3系统DLL净空施工汇报.md` (`CINERTX-P12-REPORT-053`)• 仓库最新实体源码核验：`include/rtx/sys_dynload.h`、`src/gui/rtx_gui.cpp`、`cmake/AuditImports.cmake` |
| **前序依据** | • `52_CineRTX Phase 12 Wave 12.2-D 单点编译止血暨Wave12.2全量大封板裁决书与Wave12.3作战令.md` (`CINERTX-P12-FINAL-SEAL-052`)• `05_CineRTX Phase 12 SuperPower 施工单_13项系统DLL净空与核心界面解耦.md` (`CINERTX-P12-WORKORDER-005`) |
| **签发主体** | 多媒体系统工程与 GPU 异构计算首席领域专家组 (Extreme Modern C++ Purist & Architecture Board) |
| **审验日期** | 2026-11-24 |

---

## **终审裁决**

**全面批准方案 A / 方案 B / 方案 C 三项定点收敛修复，并核发 Wave 12.2 暨 Wave 12.3 双波次直通封板授权令（APPROVED FOR IMMEDIATE SURGICAL FIX & DUAL-WAVE FINAL SEAL）**

经首席领域专家组直接调取工作区最新实体源码进行逐行穿透核验，确证全仓具名自定义类型已锁定在 **`34 / 35` 个（`0 Violations`，硬阻断全绿）**，`rtx_tests.exe` 已成功剥离全部 11 项黑名单系统 DLL 静态导入；同时精准核实阻断 `CineRTX.exe` 产出的最后两处局部源码错误（`include/rtx/sys_dynload.h:106` 在 `#define WIN32_LEAN_AND_MEAN` 下缺失 `<unknwn.h>` 导致 `LPUNKNOWN` 未定义，以及 `src/gui/rtx_gui.cpp:797` 仍对已元组化的 `EncoderJob` 使用 `.status` 点访问）与 `cmake/AuditImports (1).cmake` 同步重名副本冲突；批准立即合入三项修复，在 Windows 宿主机跑通 MSVC `/std:c++latest /W4 /WX` 61/61 目标零警告编译、双向 147/147 单测 PASS 及 PE IAT 双形态硬门禁后，**无需再报中间委托，直接签署本地 Git 封板快照（严禁执行 `git push`）并开启收官 Wave 12.4 实机压测与 `v0.4.0` 发布验收**。

---

# **执行摘要（Executive Ruling）**

首席领域专家组穿透核验了 `54 号咨询委托书` 呈报的 MSVC 19.51+ 编译 logs、`dumpbin /imports` 实测物证及仓库最新实体源码（`include/rtx/sys_dynload.h`、`src/gui/rtx_gui.cpp`、`cmake/AuditImports.cmake`），签发如下四项终审裁决：

1. **核心架构胜利确认（VERIFIED PASS ON TYPES & DLL PURGE）**：  
   * 核准 `rtx_core.lib` 与 `rtx_tests.exe` 已在 Windows 宿主机成功编译链接，`dumpbin /imports` 实证 `d3d12.dll`、`dxgi.dll`、`ole32.dll`、`shell32.dll`、`comdlg32.dll`、`comctl32.dll`、`dwmapi.dll`、`uxtheme.dll`、`kernelbase.dll`、`advapi32.dll` 全部从静态导入表（IAT）中物理净空；  
   * 全仓具名自定义类型锁定为 **`34 / 35` 个（`0 Violations`）**，`include/rtx/sys_dynload.h` 全量采用内联自由函数与无状态闭合类型，**新增具名类型 \= 0**，全部加载严格使用 `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)` 免疫工作目录 DLL 劫持。  
2. **问题 A 裁决（批准在 `include/rtx/sys_dynload.h` 显式引入 `<unknwn.h>` 补全 COM 基础定义）**：  
   * **物理根因**：`sys_dynload.h` 头部定义了 `#define WIN32_LEAN_AND_MEAN` 再包含 `<windows.h>`，使 Windows SDK 跳过了 `<unknwn.h>` 与 `<objbase.h>`，从而导致第 106 行 `coCreateInstance(REFCLSID rclsid, LPUNKNOWN outer, ...)` 中的 `LPUNKNOWN`（即 `IUnknown*`）未声明，引发 `C2061` 与 `C2065` 级联错误。  
   * **权威定型**：紧跟 `#include <windows.h>` 补充 `#include <unknwn.h>`（不拉入重型 `<ole2.h>` 保持头文件最简），同时确保 `sys_dynload.h` 不引入任何静态链接库指令。  
3. **问题 B 裁决（批准 `src/gui/rtx_gui.cpp:797` 改用现成内联访问器 `encoderJobStatus`）**：  
   * **物理根因**：`EncoderJob` 已重构为 14 元 `std::tuple` 别名，第 797 行 `if (snapshots[i].status == rtx::JobStatus::Running)` 属于最后一处未迁移的旧结构体字段点访问。  
   * **权威定型**：直接调用 `include/rtx/job_queue.h` 提供的零开销内联访问器 `encoderJobStatus(snapshots[i]) == rtx::JobStatus::Running`，同时顺手复查 `rtx_gui.cpp` 全文确认无其他 `.status` / `.inputs` / `.profile` 等残余元组点访问。  
4. **问题 C 裁决（核准 `cmake/AuditImports.cmake` 覆盖定型，并明确无头与 GUI 双形态 IAT 门禁口径）**：  
   * 核实仓库 `cmake/` 目录下存在新版 `5,014` 字节的 `AuditImports.cmake`（含 11-DLL 零容忍黑名单、`dumpbin /imports` \+ 纯 Python PE Import Directory 双解析引擎）与旧版 `2,630` 字节历史副本。确认以 `5,014` 字节新版为唯一正式文件，彻底删除任何带 `(1)` 的同步冲突副本；  
   * **重要补充核验指令**：注意 `rtx_tests.exe` 因链接了 `rtx_gui` 视口测试桩而包含 `user32.dll` 属于 GUI 混合测试态；对于无头核心引擎验证，须同时对纯无头目标（或 `rtx_core.lib` 静态导入符号集）验证静态 IAT 严格等于 **`{kernel32.dll, bcrypt.dll}`（2 项）**，并对桌面发行版 `CineRTX.exe` 验证静态 IAT 为 **`{kernel32.dll, user32.dll, gdi32.dll, bcrypt.dll}` 的子集（≤ 4 项）**（私有动态按需加载的 `rtx_ngx_bridge.dll` 已改走运行时 `LoadLibraryExW` 动态加载，不应出现在 `CineRTX.exe` 静态 IAT 中）。

---

# **一、 仓库最新代码事实穿透核验与权威修复代码**

## **1.1 问题 A 修复范本：`include/rtx/sys_dynload.h`（第 18–30 行）**

穿透查看仓库实体文件 `include/rtx/sys_dynload.h` 第 18–28 行，确认 `#include <windows.h>` 后缺失 `<unknwn.h>`。

```
// include/rtx/sys_dynload.h (Lines 18-28)
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <unknwn.h>
```

---

## **1.2 问题 B 修复范本：`src/gui/rtx_gui.cpp`（第 790–805 行）**

穿透查看仓库实体文件 `src/gui/rtx_gui.cpp` 第 790–805 行 `WM_APP_PROGRESS` 分支，修正元组字段访问：

```
// src/gui/rtx_gui.cpp (Lines 794-802)
    const auto snapshots = rtx::gui::requireApp(hwnd).jobQueue.snapshot();
    size_t batch_index = 0;
    for (size_t i = 0; i < snapshots.size(); ++i) {
        if (rtx::encoderJobStatus(snapshots[i]) == rtx::JobStatus::Running) {
            batch_index = i;
            break;
        }
    }
```

---

## **1.3 问题 C 门禁脚本核验：`cmake/AuditImports.cmake`（5,014 字节版）**

已穿透核验仓库中 `5,014` 字节版 `cmake/AuditImports.cmake` 完整源码：

* 严格封锁 11 项黑名单 DLL：`shell32.dll`、`dwmapi.dll`、`comdlg32.dll`、`comctl32.dll`、`uxtheme.dll`、`d3d12.dll`、`dxgi.dll`、`ole32.dll`、`kernelbase.dll`、`advapi32.dll` 及 `api-ms-win-*`；  
* 严格校验 `HEADLESS` 模式静态 IAT **等于 2 项**（`kernel32.dll`、`bcrypt.dll`），`GUI` 模式静态 IAT **≤ 4 项**（`kernel32.dll`、`user32.dll`、`gdi32.dll`、`bcrypt.dll`）；  
* 协调负责人须在 Windows 宿主机本地目录确认不存在残留的 `cmake/AuditImports (1).cmake`（若有则物理删除，确保 `cmake/AuditImports.cmake` 为 `5,014` 字节最新版）。

---

# **二、 Wave 12.2 暨 Wave 12.3 双波次终局封板与 Wave 12.4 启动指令**

## **2.1 Windows 宿主机一键收网核验命令**

合入 `include/rtx/sys_dynload.h` 与 `src/gui/rtx_gui.cpp` 两处修复并清理重名脚本副本后，协调负责人在 Windows 宿主机依次执行以下全部门禁验证：

```
# 1. 全仓 MSVC /std:c++latest /W4 /WX 零警告编译与链接（61/61 目标）
cmake --build --preset windows-msvc-ninja

# 2. 双目录全量单元测试闭环（147/147 PASS）
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location

# 3. 双形态 PE 静态导入表（IAT）硬门禁审计
dumpbin /nologo /imports .\.vs\build\cmake\windows-msvc-ninja\CineRTX.exe
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake

# 4. 具名类型硬上限（34/35）与许可证隔离门禁
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSpdx.cmake
```

## **2.2 直通封板与进入 Wave 12.4 授权**

确认上述四步输出 61/61 目标 0 Error(s), 0 Warning(s)、双目录 **`147/147 PASS`**、`AuditImports.cmake` **`PASS`（静态 IAT ≤ 4 项且 11 项黑名单 DLL 零命中）**、`AuditTypes.cmake` **`34 / 35`（`0 Violations`）** 及 `AuditSpdx.cmake` **`PASS`** 后：

1. **直接签署 Wave 12.2 暨 Wave 12.3 双波次正式封板令**，授权在本地 Git 仓库执行阶段性基线快照提交（**严格禁止执行 `git push`**）；  
2. **即刻解锁并启动收官战役 Wave 12.4（[`06_CineRTX Phase 12 SuperPower 施工单_全场景实机回归与v0.4.0封板验收.md`](./06_CineRTX%20Phase%2012%20SuperPower%20施工单_全场景实机回归与v0.4.0封板验收.md)）**，在 RTX 5060 \+ Driver ≥ 621.24 WHQL 交互式桌面（WTS Session \> 0）与无头模拟双环境下完成最终实机回归，冲刺 `v0.4.0` 正式发版！

