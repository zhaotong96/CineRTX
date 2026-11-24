# **CineRTX Phase 12 Wave 12.1–12.3 穿透终审验收裁决书暨 Wave 12.4 收官作战令**

* **文档编号**：`CINERTX-P12-RULING-061`  
* **归档路径**：`docs/phase12/61_CineRTX Phase 12 Wave 12.1-12.3 穿透终审验收裁决书暨Wave12.4收官作战令.md`  
* **审验标的**：  
  * `docs/phase12/60_CineRTX Phase 12 Wave 12.3 终局闭环复验暨Wave12.4收官行动计划专家咨询委托书.md`（`CINERTX-P12-DELEGATION-060`）  
  * `docs/phase12/59_CineRTX Phase 12 Wave 12.3 导入表与无头探针终局收口施工汇报.md`（`CINERTX-P12-REPORT-059`）  
  * 仓库最新实体源码与构建门禁：`CMakeLists.txt`、`cmake/AuditImports.cmake`、`cmake/AuditSloc.cmake`、`cmake/AuditTypes.cmake`、`src/gui/rtx_gui.cpp`、`include/rtx/sys_dynload.h`、`tests/test_raii_retirement.cpp`  
* **前序依据**：`58_CineRTX Phase 12 Wave 12.3 导入表API Set合同冲突与纯无头门禁缺口终审裁决书暨Wave12.4启动令.md`（`CINERTX-P12-RULING-058`）、`06_CineRTX Phase 12 SuperPower 施工单_全场景实机回归与v0.4.0封板验收.md`（`CINERTX-P12-WORKORDER-006`）  
* **签发主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
* **审验日期**：2026-11-24  
* **终审裁决**：**有条件通过 Wave 12.1–12.3 阶段验收并核发 Wave 12.4 收官作战令（CONDITIONAL PASS — APPROVED FOR WAVE 12.4 LAUNCH WITH 3 MANDATORY HARDENING ITEMS）**——经首席领域专家组对仓库最新实体源码进行毫不妥协的显微镜级穿透审查，确认 Wave 12.1 至 12.3 的主体目标（具名类型 34/35 零违规硬阻断、双模静态 PE IAT 物理 DLL 2 项 / 4 项达标、`rtx_headless_cli.exe` 零窗口探针生成、`147/147` 单测全绿）**真实物理落地**；但同时揪出 `60 号委托书` 与 `59 号汇报` **遗漏披露的 3 处真实工程隐患**（① `src/gui/rtx_gui.cpp:36-41` 仍残留已废弃的 `#include <commctrl.h>` 死头文件及全仓多处冗余 `#include <cstdlib>`；② `cmake/AuditSloc.cmake` 未在脚本内设置 `CEILING_CORE_SLOC` 失败硬拦截导致超过 23,000 行时仅打印状态字符串而不返回非零退出码；③ `60 号委托书` 仅执行了 `rtx_headless_cli.exe --version` 而未运行 `--schema-json`、真实无头转码/探测及 WTS Session \> 0 交互式桌面实机压测）。专家组签发如下严格验收裁决，要求在 Wave 12.4 首日先清零上述 3 项残留隐患，再执行全量实机回归与 `v0.4.0` 封板。

# **执行摘要（Executive Ruling）**

首席领域专家组秉持"绝不妥协任何细微隐患、以仓库最新实体源码为唯一仲裁依据"的原则，对 `60 号委托书` 呈报的 Wave 12.1–12.3 完工申请逐行核验，签发如下终审裁决：

1. **Wave 12.1–12.3 主体架构战果核准（CORE DELIVERABLES VERIFIED）**  
   * **具名自定义类型硬门禁**：`cmake/AuditTypes.cmake` 实测全仓合法具名类型为 **`34 / 35` 个（`0 Violations`）**，`RTX_AUDIT_TYPES_STRICT` 无条件开启硬阻断，较 Phase 12 初始基线（155 个）净削减 121 个（-78.1%）。  
   * **双形态 PE 静态导入表（IAT）净空**：`cmake/AuditImports.cmake` 与 `dumpbin /imports` 实测——`rtx_headless_cli.exe`（`HEADLESS`）物理静态 IAT 严格等于 **`2` 项**（`bcrypt.dll`, `kernel32.dll` \+ 1 项合法 STL 同步 API Set `api-ms-win-core-synch-l1-2-0.dll`）；`CineRTX.exe`（`GUI`）物理静态 IAT 严格等于 **`4` 项**（`bcrypt.dll`, `gdi32.dll`, `kernel32.dll`, `user32.dll` \+ 1 项 STL 同步 API Set）；10 项黑名单物理 DLL（`shell32`, `dwmapi`, `comdlg32`, `comctl32`, `uxtheme`, `d3d12`, `dxgi`, `ole32`, `kernelbase`, `advapi32`）静态导入命中数为 **0**。  
   * **核心无头引擎 Pure SLOC**：`cmake/AuditSloc.cmake` 实测 Core Engine（`include/` \+ `src/core/` \+ `src/io/` \+ `src/cli/`）纯净有效代码为 **`12,165` 行（73 文件）**，远低于 `≤ 23,000` 行硬红线（余量 `10,835` 行）。  
2. **绝不妥协的三项残留隐患穿透揭示（MANDATORY HARDENING ITEMS H-01 \~ H-03）**  
   * **隐患 H-01（源码卫生死头文件残留）**：穿透审查仓库最新 `src/gui/rtx_gui.cpp` 第 36–41 行发现，虽然 `comctl32.dll` 已从静态导入表剥离，但文件头部仍残留 **`#include <commctrl.h>`** 与 **`#include <cstdlib>`**（`std::getenv` 早已清零），同时 `include/rtx/sys_dynload.h` 底部或辅助处需复核 `initCommonControlsEx` 是否存在无调用死包装；属于典型的"拔了链接库却留了 dead include 污染预编译上下文"卫生瑕疵。  
   * **隐患 H-02（`AuditSloc.cmake` 缺少默认硬阻断退出码）**：穿透审查仓库最新 `cmake/AuditSloc.cmake` 第 116–121 行发现，脚本在 `_core_sloc > 23000` 时仅将 `_core_status` 设为 `"OVER TARGET (> 23000)"` 并打印 `message(STATUS ...)`，**默认未触发 `message(FATAL_ERROR ...)`**（除非外部手动传入 `-DCEILING_CORE_SLOC=23000`），这意味着若后续演进导致核心引擎体积反弹突破 `23,000` 行，无参数运行 `cmake -P cmake/AuditSloc.cmake` 仍会返回退出码 0 放行！必须升级为默认 `FATAL_ERROR` 硬阻断门禁。  
   * **隐患 H-03（`rtx_headless_cli.exe` 仅测了 `--version`，尚未验证真实业务路径与桌面/无头双实机闭环）**：`60 号委托书` 第 2.3 节仅展示了 `rtx_headless_cli.exe --version` 输出 `CineRTX 0.3.0`（且版本号尚未升至 `0.4.0`），尚未在无头环境下实测 `--schema-json`、`--probe`、非法参数错误处理以及零 GUI/D3D12 依赖下的优雅降级；同时 WTS Session \> 0 交互式桌面全链路实机压测尚未执行。  
3. **Wave 12.4 收官战役正式启动授权（APPROVED FOR WAVE 12.4 EXECUTION）**  
   * 批准协调负责人保留当前 Wave 12.2–12.3 本地 Git 基线快照（**严禁执行 `git push`**），并立即下达 **Wave 12.4（`06号施工单` \+ 本裁决书 H-01\~H-03 加固项）**，在完成三项隐患清零、版本升至 `0.4.0` 及交互式桌面 \+ 无头双环境全量实机压测后，呈报终审封板。

# **一、 Wave 12.1–12.3 全量穿透核验矩阵（源码与物理物证对账）**

| 核验维度 | 仓库物理锚点 / 物证 | 实测结果 | 审查结论 |
| :---- | :---- | :---- | :---- |
| **1\. 具名自定义类型硬上限** | `cmake/AuditTypes.cmake` | 全仓合法具名类型 **`34 / 35` 个，`0 Violations`**，`RTX_AUDIT_TYPES_STRICT=ON` 硬拦截生效。 | **PASS** |
| **2\. 双形态 PE 静态 IAT** | `cmake/AuditImports.cmake` & `dumpbin /imports` | • `rtx_headless_cli.exe`（`HEADLESS`）：**2 物理 DLL** (`bcrypt.dll`, `kernel32.dll`) \+ **1 API Set** (`api-ms-win-core-synch-l1-2-0.dll`)；• `CineRTX.exe`（`GUI`）：**4 物理 DLL** (`bcrypt.dll`, `gdi32.dll`, `kernel32.dll`, `user32.dll`) \+ **1 API Set**；• 10 项黑名单物理 DLL 静态命中 \= **0**。 | **PASS** |
| **3\. 动态加载安全边界** | `include/rtx/sys_dynload.h` | 零新增具名类型；全部按需加载严格使用 `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)`；内置纯 C++ `parseCommandLineW` 彻底取代 `shell32.dll!CommandLineToArgvW`。 | **PASS** |
| **4\. 单元测试与许可合规** | `rtx_tests.exe` & `AuditSpdx.cmake` | `147 test(s) run, 0 failed`（100% PASS）；`AuditSpdx` 输出 `GPL=113 MIT=39, result: PASS`；仓内 `.bak` 残留 \= **0**。 | **PASS** |
| **5\. 头文件卫生与死代码** | `src/gui/rtx_gui.cpp:36-47` & `sys_dynload.h` | `rtx_gui.cpp` 仍残留无用的 `#include <commctrl.h>` 与 `#include <cstdlib>`；需全仓清理多余标准/系统头文件。 | **需加固 (H-01)** |
| **6\. 体量门禁硬拦截机制** | `cmake/AuditSloc.cmake:116-121` | Core Engine Pure SLOC 为 `12,165` 行（`≤ 23,000`），但脚本默认超过 `23,000` 时仅打印 `OVER TARGET` 而不触发 `FATAL_ERROR`。 | **需加固 (H-02)** |
| **7\. 版本元信息与双环境实测** | `CMakeLists.txt` & 实机环境 | `rtx_headless_cli.exe --version` 仍输出 `0.3.0`，且尚未执行 `--schema-json`、无头降级实测与 WTS Session \> 0 桌面 4K/Resize 压测。 | **纳入 Wave 12.4 (H-03)** |

# **二、 Wave 12.4 首日必清零的三项残留隐患规范（H-01 至 H-03）**

## **2.1 隐患 H-01：清除 `src/gui/rtx_gui.cpp` 死头文件与 `sys_dynload.h` 冗余符号**

1. **清理 `src/gui/rtx_gui.cpp` 头部**：  
   * 物理删除 `#include <commctrl.h>`（第 38 行）与未使用的 `#include <cstdlib>`（第 47 行）；  
   * 若 `rtx_gui.cpp` 中仍使用 `TBM_SETPOS` / `PBM_SETPOS` 等消息常量，使用纯 Win32 标准数值常量定义（或内联 `constexpr UINT`），彻底切断对公共控件头文件的依赖。  
2. **清理 `include/rtx/sys_dynload.h` 未使用函数**：  
   * 核查 `sys_dynload.h` 中所有动态加载函数（如 `initCommonControlsEx`、`setWindowTheme` 等），凡在全仓 `src/` 中调用次数为 `0` 的未用封装函数，遵循奥卡姆剃刀原则直接删除，保持头文件 100% 精炼。

## **2.2 隐患 H-02：升级 `cmake/AuditSloc.cmake` 为默认 `FATAL_ERROR` 硬门禁**

将 `cmake/AuditSloc.cmake` 第 115–145 行的软性状态打印升级为**默认硬阻断门禁**（默认锁定 CEILING\_CORE\_SLOC \= 23000），确保任何后续提交若导致核心无头引擎 Pure SLOC 突破 23,000 行，无需额外传参即直接在 CMake 阶段报 `FATAL_ERROR` 阻断：

```
# cmake/AuditSloc.cmake (Hard Gate Enforcement)
if(NOT DEFINED CEILING_CORE_SLOC OR "${CEILING_CORE_SLOC}" STREQUAL "")
    set(CEILING_CORE_SLOC 23000)
endif()

if(_core_sloc GREATER CEILING_CORE_SLOC)
    message(FATAL_ERROR
        "[AuditSloc] FAIL: Core Engine Pure SLOC (${_core_sloc}) exceeds hard ceiling (${CEILING_CORE_SLOC}).")
endif()
```

## **2.3 隐患 H-03：升级 `v0.4.0` 元数据并补全 `rtx_headless_cli.exe` 全功能探针验证**

1. 将根目录 `CMakeLists.txt` 的 `project(CineRTX VERSION 0.4.0 ...)` 升级至 `0.4.0`，同步更新 `README.md`、`README_zh.md` 与 `webui/README.md`；  
2. 在 Windows 宿主机验证 `rtx_headless_cli.exe --version` 输出 `CineRTX 0.4.0`，并执行 `--schema-json`（校验 20 项 SSOT 参数 JSON 完整输出且退出码为 0）与 `--probe` 硬件探测。

# **三、 Wave 12.4 收官战役行动计划与 `v0.4.0` 封板 Checklist**

依据 `06_CineRTX Phase 12 SuperPower 施工单_全场景实机回归与v0.4.0封板验收.md` 及上述三项加固规范，Wave 12.4 按以下四步执行收官闭环：

## **阶段 1：隐患清零与 `v0.4.0` 元数据升级（0.5 工作日）**

* 合入 `H-01`（清理 dead includes 与未用动态加载函数）、`H-02`（`AuditSloc.cmake` 默认 `23,000` 硬阻断）与 `H-03`（`VERSION 0.4.0` \+ 双语文档同步）；  
* 执行 Clean Build，验证 MSVC `/std:c++latest /W4 /WX` 全量目标 **0 Error(s), 0 Warning(s)**，且 `AuditTypes`（34/35）、`AuditSloc`（Core ≤ 23,000）、`AuditImports`（HEADLESS 2 物理 DLL / GUI 4 物理 DLL）、`AuditSpdx` 四大静态脚本全部返回退出码 `0`。

## **阶段 2：交互式桌面全链路实机压测（WTS Session \> 0，RTX 5060 \+ Driver ≥ 621.24 WHQL）**

必须通过 `quser` 确认当前会话为 `Active` 且 `SessionId > 0` 的真实交互式桌面：

1. **4K AV1 HDR10 全链路零拷贝重制压测**：  
   * 输入素材：1080p SDR（H.264/HEVC，24fps/30fps，含音轨）；  
   * 目标配置：4K（3840×2160）、`vsr-first`（VSR Level 4 \+ TrueHDR 1000 nits \+ RIFE `scale=0.5` A=128 60fps \+ AV1 10-bit CQ22 Spatial AQ）；  
   * 硬判据：全程预览回读带宽 **`D2H == 0 B`**、显存峰值严格受控于 7,300 MiB DXGI 预算的 82%（5,986 MiB）以下、零 OOM、输出文件经 `ffprobe` 核验为合法 10-bit AV1 PQ HDR10 且音画同步完整。  
2. **DirectComposition 视口与 WASAPI 实时播放稳定性压测**：  
   * 分别播放 H.264 / HEVC / AV1 测试流，执行 **100 次连续窗口缩放（Resize）**、无边框全屏切换、播放/暂停/GOP-Aware 拖拽 Seek 与音频设备热切换；  
   * 硬判据：`presented > 0`、零崩溃、零 UAF、任务管理器 GDI/句柄计数稳定无泄漏。  
3. **NLE 多段硬切导出与零分配音频总线核验**：  
   * 执行多段 `1V+1A` 剪辑硬切导出，解复用验证导出总帧数与 705.6 MHz Flicks 理论帧数逐帧一致、切点含合法 IDR 帧；验证 Debug 构建下 `mixAvx2` 与流式 WSOLA 热循环堆分配次数为 **0**。

## **阶段 3：无头（Headless / Session 0 模拟）环境双模降级实测**

在脱离交互式桌面的无头控制台会话下运行 `rtx_headless_cli.exe`：

1. 执行 `--version`（输出 `0.4.0`）、`--schema-json`、参数越界校验与离线批处理任务，确认冷启动零缺失 DLL 弹窗、零异常退出；  
2. 进程模块快照（Process Explorer / PowerShell `Get-Process -Module`）核验纯无头 CPU 任务运行期**未加载** `user32.dll`、`gdi32.dll`、`d3d12.dll`、`dxgi.dll`、`ole32.dll`、`dwmapi.dll`、`uxtheme.dll`、`shell32.dll`、`comdlg32.dll`、`comctl32.dll`。

## **阶段 4：`v0.4.0` 终局封板发布清单（Final Release Checklist）**

- [ ] **Check 1（编译与静态卫生）**：MSVC `/std:c++latest /W4 /WX` Clean Build **0 Error(s), 0 Warning(s)**；全仓 `std::getenv` / `g_app` / 裸 GDI / `snprintf` / `std::get<` / `#pragma comment(lib)` / `LoadLibrary[AW]` / `.bak` 全部 **0 命中**；  
- [ ] **Check 2（五重自动化门禁全绿）**：`AuditTypes`（34/35）、`AuditSloc`（Core ≤ 23,000 硬门禁 PASS）、`AuditImports`（HEADLESS 2 物理 DLL \+ 1 API Set / GUI 4 物理 DLL \+ 1 API Set）、`AuditSpdx`（PASS）、双目录 `rtx_tests.exe`（147/147 PASS）；  
- [ ] **Check 3（双环境实机物证齐全）**：WTS Session \> 0 桌面实测报告（4K AV1 HDR10 `D2H == 0 B` \+ 100 次 Resize \+ NLE 硬切导出）与无头 `rtx_headless_cli.exe` 实测日志全部归档；  
- [ ] **Check 4（终审报告与发布包）**：提交《62 号终审验收报告暨 `v0.4.0` 封板决议记录》，经专家组与用户最终签署后，由用户授权打标 **`v0.4.0`**。

