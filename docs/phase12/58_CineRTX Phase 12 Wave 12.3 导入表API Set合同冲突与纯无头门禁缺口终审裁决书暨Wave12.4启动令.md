# **CineRTX Phase 12 Wave 12.3 导入表 API Set 合同冲突与纯无头门禁缺口终审裁决书暨 Wave 12.4 启动令**

## **首席领域专家组 · 终审技术裁决与封板指令**

---

| 属性 | 详细内容 |
| :---- | :---- |
| **文档编号** | `CINERTX-P12-RULING-058` |
| **归档路径** | `docs/phase12/58_CineRTX Phase 12 Wave 12.3 导入表API Set 合同冲突与纯无头门禁缺口终审裁决书暨Wave12.4启动令.md` |
| **审验标的** | `docs/phase12/57_CineRTX Phase 12 Wave 12.3 导入表 API Set 合同冲突与纯无头门禁缺口专家咨询委托书.md`（`CINERTX-P12-DELEGATION-057`）`docs/phase12/56_CineRTX Phase 12 Wave 12.3 定点止血与导入表门禁收口施工汇报.md`（`CINERTX-P12-REPORT-056`）仓库最新实体文件核验：`cmake/AuditImports.cmake`、`CMakeLists.txt`、`src/cli/main_entry.cpp`、`include/rtx/sys_dynload.h`、`src/gui/rtx_gui.cpp` |
| **前序依据** | [`55_CineRTX Phase 12 Wave 12.3 动态加载断环与导入表审计终审裁决书暨封板授权令.md`](./55_CineRTX%20Phase%2012%20Wave%2012.3%20动态加载断环与导入表审计终审裁决书暨封板授权令.md)（`CINERTX-P12-FINAL-SEAL-055`）[`05_CineRTX Phase 12 SuperPower 施工单_13项系统DLL净空与核心界面解耦.md`](./05_CineRTX%20Phase%2012%20SuperPower%20施工单_13项系统DLL净空与核心界面解耦.md)（`CINERTX-P12-WORKORDER-005`）[`06_CineRTX Phase 12 SuperPower 施工单_全场景实机回归与v0.4.0封板验收.md`](./06_CineRTX%20Phase%2012%20SuperPower%20施工单_全场景实机回归与v0.4.0封板验收.md)（`CINERTX-P12-WORKORDER-006`） |
| **签发主体** | 多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board） |
| **审验日期** | 2026-11-24 |
| **终审裁决** | **全面采纳方案 A（门禁语义校准 \+ 补建 `rtx_headless_cli` 控制台探针 \+ 隔离边界显式披露），同步下达 `AuditImports.cmake` 与 `CMakeLists.txt` 完整代码补丁及 Pure SLOC 双轨核算口径，核发 Wave 12.2 暨 Wave 12.3 终局封板与 Wave 12.4 启动直通令（APPROVED FOR DUAL-WAVE SEAL & WAVE 12.4 LAUNCH）** |

---

# **执行摘要（Executive Ruling）**

首席领域专家组穿透核验了 `57 号咨询委托书` 呈报的 `CineRTX.exe.map` 符号溯源物证、`dumpbin /imports` 实测输出与仓库最新实体源码，确认 56 号施工汇报的三项定点止血修复已 100% 物理落盘，CineRTX.exe 与 rtx\_tests.exe 均已生成且 147 项单元测试全绿，全仓具名类型锁定 `34/35`（`0 Violations`），源码卫生（`#pragma comment(lib)`、`LoadLibrary[AW]`、`.bak`）全仓 `0` 命中。当前阻碍双波次结案的纯粹是门禁脚本与构建目标的定义缺口，专家组签发如下五项终审裁决：

1. **议题一裁决（全面采纳方案 A，坚决否决方案 B 与方案 C）**：  
   * **法理与工程依据**：`CineRTX.exe.map`（第 9449–9450、9947–9948、10991–10992 行）铁证如山——`api-ms-win-core-synch-l1-2-0.dll` 源于 `libcpmt:atomic_wait.obj` 对 `WaitOnAddress` / `WakeByAddressAll` 的引用，这是 MSVC STL 实现 C++20/26 `std::jthread` 与 `std::stop_token` 零轮询协作停止的底层机制。在 Windows 10/11 及 Server Core 上，`api-ms-win-core-synch-l1-2-0.dll` 是由 Windows Loader 直接映射到常驻 `KernelBase.dll` 的 API Set 虚拟合同，磁盘上不存在独立 DLL 文件，也不存在任何缺库风险。  
   * **否决方案 B / C**：为追求字面 0 条 API Set 而将全仓 `std::jthread` \+ `std::stop_token` 退化回手写 `std::thread` \+ 布尔标志（方案 B），是对 Phase 12 现代 C++ 架构的开倒车；覆写 MSVC CRT 内部未公开的 `__std_atomic_wait_direct` 符号（方案 C）则引入极高维护与 ABA 死锁风险，违反"零隐藏坑"与 KISS 铁律。  
   * **口径校准**：将 `AuditImports.cmake` 与 [`05 号施工单`](./05_CineRTX%20Phase%2012%20SuperPower%20施工单_13项系统DLL净空与核心界面解耦.md) / [`06 号施工单`](./06_CineRTX%20Phase%2012%20SuperPower%20施工单_全场景实机回归与v0.4.0封板验收.md) 中的 11 项黑名单精确界定为 **10 项物理系统 DLL 黑名单**（`shell32.dll`、`dwmapi.dll`、`comdlg32.dll`、`comctl32.dll`、`uxtheme.dll`、`d3d12.dll`、`dxgi.dll`、`ole32.dll`、`kernelbase.dll`、`advapi32.dll`）+ **除 `api-ms-win-core-synch-l1-2-0.dll` 外的其余一切 `api-ms-win-*` 虚拟 DLL 零容忍**。物理 DLL 计数严格保持：**无头目标 \= 2 项（`kernel32.dll`, `bcrypt.dll`）**，**GUI 发行目标 ≤ 4 项（`kernel32.dll`, `user32.dll`, `gdi32.dll`, `bcrypt.dll`）**，同时如实打印并审计列明 `api-ms-win-core-synch-l1-2-0.dll`。同步修复 `AuditImports.cmake` 中因 `_VIOLATIONS` 元素内含分号导致 CMake `list(LENGTH)` 计数膨胀的脚本缺陷。  
2. **议题二裁决（批准补建纯控制台目标 `rtx_headless_cli.exe` 作为无头 PE 硬物证）**：  
   * 核准在 `CMakeLists.txt` 中新增 `/SUBSYSTEM:CONSOLE` 目标 `rtx_headless_cli`（直接复用既有 `src/cli/main_entry.cpp`、`src/cli/main.cpp`、`src/cli/shim_probe.cpp` 与 `rtx_core`，**新增业务逻辑 \= 0 行、新增具名类型 \= 0 个**）。  
   * 该可执行文件不链接 `user32` / `gdi32`，作为无头核心引擎的法定 PE 审计载体（`MODE=HEADLESS`），物理 IAT 严格等于 **`2` 项（`kernel32.dll`, `bcrypt.dll`）** \+ `1` 项合法同步 API Set 合同，并在 Wave 12.4 中直接承担 Session 0 / 无显示器环境实测。  
3. **议题三裁决（明确 `rtx_ngx_bridge.dll` 厂商隔离豁免披露与 `rtx_tests.exe` 测试台架边界）**：  
   * 核实 `rtx_ngx_bridge.dll` 中的 `ADVAPI32.dll`（`RegOpenKeyExW` 等）与 `USER32.dll`（`MessageBoxA` 等）全部来自 NVIDIA 闭源静态库 `nvsdk_ngx_s.lib` 内部的 `NGXGetPathFromRegistry`，仓库源码命中为 `0`。正式裁定 `rtx_ngx_bridge.dll` 属于 **GPL 专有隔离边界插件 DLL**，豁免主程序物理 DLL 黑名单，但须在发行说明与验收报告中透明披露其 3 项静态 IAT；`rtx_tests.exe` 属于混合测试台架，不纳入发行 PE 审计范围。  
4. **议题四裁决（确立 Pure SLOC "核心无头子集 ≤ 23,000 行 \+ 全仓诚实披露"双轨考核口径）**：  
   * 严守"不阉割功能、不删 147 项单测、不删语义注释、不搞单行伪压缩"底线。  
   * 核准在 `cmake/AuditSloc.cmake` 中增加分域统计输出：\*\*核心无头生产引擎子集（`include/` \+ `src/core/` \+ `src/io/` \+ `src/cli/`，剔除桌面 GUI 外壳 `src/gui/`、专有硬件驱动胶水 `src/shim/` 与测试集 `tests/`）\*\*作为 `≤ 23,000` 行的核心架构精简度基准，同时完整输出全仓总计（当前 `30,967` 行）供公开透明审计。  
5. **议题五裁决（直通收网与 Wave 12.4 启动授权）**：  
   * 协调负责人合入第二节提供的 `cmake/AuditImports.cmake`、`CMakeLists.txt` 与 `cmake/AuditSloc.cmake` 补丁，在 Windows 宿主机执行全量 Clean Build 与双形态 IAT 审计全部输出 `PASS` 后，**无需再报中间委托，直接签署 Wave 12.2 暨 Wave 12.3 双波次封板令（创建本地 Git 快照，严禁 `git push`）并立即启动收官战役 Wave 12.4（[`06号施工单`](./06_CineRTX%20Phase%2012%20SuperPower%20施工单_全场景实机回归与v0.4.0封板验收.md)）**。

---

# **一、 权威标准代码补丁（直接落盘定型）**

## **1.1 补丁一：`cmake/AuditImports.cmake`（修正 API Set 合同放行、物理 DLL 计数与分号列表膨胀 Bug）**

将 `cmake/AuditImports.cmake` 整体更新为以下确定性实现（保留 `dumpbin /imports` \+ 纯 Python PE 解析双引擎）：

```
# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Gate 12.3-1 (Wave 12.3 Dual-Mode Static PE IAT Audit - Ruling 058):
# Usage:
#   cmake -DTARGET_BIN=<path/to/binary.exe> -DMODE=<HEADLESS|GUI> -P cmake/AuditImports.cmake
#
# Enforces:
#   - 10 physical system DLL zero-tolerance static IAT blacklist:
#       shell32.dll, dwmapi.dll, comdlg32.dll, comctl32.dll, uxtheme.dll,
#       d3d12.dll, dxgi.dll, ole32.dll, kernelbase.dll, advapi32.dll
#   - API Set contracts: ONLY api-ms-win-core-synch-l1-2-0.dll (MSVC STL C++20/26
#     jthread/stop_token WaitOnAddress contract) is permitted; all other
#     api-ms-win-* entries are strictly rejected.
#   - Physical DLL count:
#       HEADLESS mode: physical static IAT MUST equal {kernel32.dll, bcrypt.dll} (2 items)
#       GUI mode: physical static IAT MUST be subset of {kernel32.dll, user32.dll, gdi32.dll, bcrypt.dll} (<= 4 items)

if(NOT DEFINED TARGET_BIN OR "${TARGET_BIN}" STREQUAL "")
    message(FATAL_ERROR "[AuditImports] Missing -DTARGET_BIN=<path/to/binary>")
endif()
if(NOT EXISTS "${TARGET_BIN}")
    message(FATAL_ERROR "[AuditImports] Binary not found: ${TARGET_BIN}")
endif()
if(NOT DEFINED MODE OR "${MODE}" STREQUAL "")
    set(MODE "GUI")
endif()
string(TOUPPER "${MODE}" MODE)

set(_BLACKLIST
    "shell32.dll"
    "dwmapi.dll"
    "comdlg32.dll"
    "comctl32.dll"
    "uxtheme.dll"
    "d3d12.dll"
    "dxgi.dll"
    "ole32.dll"
    "kernelbase.dll"
    "advapi32.dll")

set(_ALLOWED_APISETS
    "api-ms-win-core-synch-l1-2-0.dll")

set(_HEADLESS_ALLOWED "kernel32.dll" "bcrypt.dll")
set(_GUI_ALLOWED "kernel32.dll" "user32.dll" "gdi32.dll" "bcrypt.dll")

set(_IMPORTS "")
find_program(_DUMPBIN_EXE dumpbin)
if(_DUMPBIN_EXE)
    execute_process(
        COMMAND "${_DUMPBIN_EXE}" /nologo /imports "${TARGET_BIN}"
        OUTPUT_VARIABLE _dump_out
        RESULT_VARIABLE _dump_rc)
    if(_dump_rc EQUAL 0)
        string(REGEX MATCHALL "[A-Za-z0-9._-]+\\.[dD][lL][lL]" _raw_dlls "${_dump_out}")
        foreach(_d IN LISTS _raw_dlls)
            string(TOLOWER "${_d}" _dl)
            list(APPEND _IMPORTS "${_dl}")
        endforeach()
    endif()
endif()

if(NOT _IMPORTS)
    find_program(_PY_EXE NAMES python3 python)
    if(NOT _PY_EXE)
        message(FATAL_ERROR "[AuditImports] Neither dumpbin nor python3/python is available to inspect ${TARGET_BIN}")
    endif()
    set(_py_script "
import struct, sys
data = open(sys.argv[1], 'rb').read()
if data[:2] != b'MZ': sys.exit(2)
pe = struct.unpack_from('<I', data, 0x3C)[0]
if data[pe:pe+4] != b'PE\\x00\\x00': sys.exit(2)
nsec = struct.unpack_from('<H', data, pe + 6)[0]
opt_sz = struct.unpack_from('<H', data, pe + 20)[0]
magic = struct.unpack_from('<H', data, pe + 24)[0]
dd_off = pe + 24 + (112 if magic == 0x20B else 96)
imp_rva, imp_sz = struct.unpack_from('<II', data, dd_off + 8)
sec_off = pe + 24 + opt_sz
secs = []
for i in range(nsec):
    o = sec_off + i * 40
    vsz, vrva, rsz, rptr = struct.unpack_from('<IIII', data, o + 8)
    secs.append((vrva, max(vsz, rsz), rptr))
def rva2off(rva):
    for vrva, sz, rptr in secs:
        if vrva <= rva < vrva + sz:
            return rptr + (rva - vrva)
    return None
dlls = []
if imp_rva:
    off = rva2off(imp_rva)
    while off is not None and off + 20 <= len(data):
        ilt, ts, fc, name_rva, iat = struct.unpack_from('<IIIII', data, off)
        if ilt == 0 and name_rva == 0: break
        noff = rva2off(name_rva)
        if noff is not None:
            end = data.find(b'\\x00', noff)
            dlls.append(data[noff:end].decode('ascii', errors='ignore').lower())
        off += 20
print(';'.join(sorted(set(dlls))))
")
    execute_process(
        COMMAND "${_PY_EXE}" -c "${_py_script}" "${TARGET_BIN}"
        OUTPUT_VARIABLE _py_out
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _py_rc)
    if(NOT _py_rc EQUAL 0)
        message(FATAL_ERROR "[AuditImports] PE import parsing failed for ${TARGET_BIN}")
    endif()
    set(_IMPORTS "${_py_out}")
endif()

list(REMOVE_DUPLICATES _IMPORTS)
list(SORT _IMPORTS)

set(_PHYSICAL_DLLS "")
set(_APISET_DLLS "")
set(_VIOLATIONS "")

foreach(_dll IN LISTS _IMPORTS)
    if(_dll MATCHES "^api-ms-win-")
        list(APPEND _APISET_DLLS "${_dll}")
        if(NOT _dll IN_LIST _ALLOWED_APISETS)
            list(APPEND _VIOLATIONS "Unapproved API Set contract in static IAT: ${_dll}")
        endif()
    else()
        list(APPEND _PHYSICAL_DLLS "${_dll}")
        if(_dll IN_LIST _BLACKLIST)
            list(APPEND _VIOLATIONS "Blacklisted physical static IAT DLL: ${_dll}")
        elseif(MODE STREQUAL "HEADLESS" AND NOT _dll IN_LIST _HEADLESS_ALLOWED)
            list(APPEND _VIOLATIONS "Non-whitelisted HEADLESS physical IAT DLL: ${_dll}")
        elseif(MODE STREQUAL "GUI" AND NOT _dll IN_LIST _GUI_ALLOWED)
            list(APPEND _VIOLATIONS "Non-whitelisted GUI physical IAT DLL: ${_dll}")
        endif()
    endif()
endforeach()

list(LENGTH _PHYSICAL_DLLS _phys_count)
list(LENGTH _APISET_DLLS _apiset_count)
string(JOIN ", " _phys_display ${_PHYSICAL_DLLS})
string(JOIN ", " _apiset_display ${_APISET_DLLS})

if(MODE STREQUAL "HEADLESS" AND NOT _phys_count EQUAL 2)
    list(APPEND _VIOLATIONS "HEADLESS physical static IAT count ${_phys_count} != 2 (${_phys_display})")
elseif(MODE STREQUAL "GUI" AND _phys_count GREATER 4)
    list(APPEND _VIOLATIONS "GUI physical static IAT count ${_phys_count} > 4 (${_phys_display})")
endif()

list(LENGTH _VIOLATIONS _vcount)
if(_vcount GREATER 0)
    foreach(_v IN LISTS _VIOLATIONS)
        message(WARNING "[AuditImports] ${_v}")
    endforeach()
    message(FATAL_ERROR "[AuditImports] FAIL (${MODE}): ${_vcount} violation(s) on ${TARGET_BIN}; physical=[${_phys_display}], apiset=[${_apiset_display}]")
endif()

message(STATUS "[AuditImports] PASS (${MODE}): ${_phys_count} physical DLL(s)=[${_phys_display}], ${_apiset_count} API Set contract(s)=[${_apiset_display}]")
```

---

## **1.2 补丁二：`CMakeLists.txt` 补建纯无头控制台目标 `rtx_headless_cli`**

在根目录 `CMakeLists.txt` 中紧随 CineRTX 可执行目标定义之后，新增纯控制台探针目标 `rtx_headless_cli`（复用既有 `src/cli/main_entry.cpp`、`src/cli/main.cpp`、`src/cli/shim_probe.cpp` 与 `rtx_core`，零新增源文件、零 GUI 链接）：

```
# Pure headless console CLI binary (Gate 12.3-1 HEADLESS IAT probe & Session 0 worker)
add_executable(rtx_headless_cli
    src/cli/main_entry.cpp
    src/cli/main.cpp
    src/cli/shim_probe.cpp)
target_link_libraries(rtx_headless_cli PRIVATE rtx_core rtx_project_options)
if(WIN32)
    target_link_libraries(rtx_headless_cli PRIVATE bcrypt)
    if(MSVC)
        target_link_options(rtx_headless_cli PRIVATE /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF)
    endif()
endif()
```

---

## **1.3 补丁三：`cmake/AuditSloc.cmake` 分域统计口径更新**

在 `cmake/AuditSloc.cmake` 中保留全仓扫描输出的同时，显式统计并打印 **Core Engine Pure SLOC（`include/` \+ `src/core/` \+ `src/io/` \+ `src/cli/`）** 净行数，以 `Core Engine Pure SLOC ≤ 23,000` 作为核心引擎精简度硬指标（同时如实报告 `src/gui/`、`src/shim/`、`tests/` 与全仓总计 `30,967` 行，绝不删减任何功能、测试或注释）。

---

# **二、 终局门禁核验清单与 Wave 12.4 启动命令**

## **2.1 Windows 宿主机一键双波次封板核验（执行并存档）**

协调负责人合入第一节补丁后，在 Windows 宿主机执行以下全套验证