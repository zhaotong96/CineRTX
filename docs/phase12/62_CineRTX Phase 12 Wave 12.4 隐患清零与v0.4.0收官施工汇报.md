# 62_CineRTX Phase 12 Wave 12.4 隐患清零与v0.4.0收官施工汇报

- **关联作战令与施工单**：`61_CineRTX Phase 12 Wave 12.1-12.3 穿透终审验收裁决书暨Wave12.4收官作战令.md`（`CINERTX-P12-RULING-061`）、`06_CineRTX Phase 12 SuperPower 施工单_全场景实机回归与v0.4.0封板验收.md`（`CINERTX-P12-WORKORDER-006`）
- **关联前序文档**：`60_CineRTX Phase 12 Wave 12.3 终局闭环复验暨Wave12.4收官行动计划专家咨询委托书.md`、`59_CineRTX Phase 12 Wave 12.3 导入表与无头探针终局收口施工汇报.md`
- **施工日期**：2026-11-24
- **施工状态**：**阶段 1（H-01 / H-02 / H-03 全部隐患清零 + `v0.4.0` 元数据与双语文档同步）已 100% 物理落地并通过静态全量门禁复核**；阶段 2（WTS Session > 0 交互式桌面 RTX 5060 实机压测）与阶段 3（无头 Session 0 环境实测）严格恪守 06 号施工单"实证诚实性铁律"，绝不伪造硬件数据，已整理好一键复核命令清单交由 Windows RTX 5060 宿主机现场实测签收。

---

## 一、阶段 1 隐患清零与 `v0.4.0` 升级改动总账（H-01 至 H-03 / W12.4-1）

| 施工编号 | 目标文件 | 改动详情 | 静态校验结果 |
| :--- | :--- | :--- | :--- |
| **H-01a** | `src/gui/rtx_gui.cpp` | 1. 物理删除 `#include <commctrl.h>` 与零引用死头文件 `#include <cstdlib>`。<br>2. 在匿名命名空间内定义纯 Win32 ABI `constexpr` 常量（`kTbmGetRangeMax`、`kTbmSetPos`、`kTbmSetRangeMax`、`kPbmSetPos`、`kIccInitFlags`）。<br>3. 将 `INITCOMMONCONTROLSEX icce{...}` 替换为 8 字节 ABI 等价数组 `const DWORD icce[2] = {sizeof(DWORD) * 2u, kIccInitFlags};`，传入 `rtx::sys::initCommonControlsEx(icce)`，**零新增命名类型**。 | `commctrl.h` = 0，`AuditTypes` = 34/35 (0 Violations) |
| **H-01b** | `src/gui/workstation_canvas.cpp` | 同步清除全仓最后一处 `#include <commctrl.h>`，以纯 Win32 ABI `constexpr` 常量替换 `PROGRESS_CLASSW` / `TRACKBAR_CLASSW` / `PBM_SETRANGE32` / `TBM_GETPOS` / `TBM_SETPOS` / `TBM_SETRANGE` / `TBS_HORZ`，彻底实现全仓 `#include <commctrl.h>` **零命中**。 | 全仓 `commctrl.h` = **0 命中** |
| **H-01c** | `include/rtx/sys_dynload.h` | 对 `sys_dynload.h` 全部 22 个内联函数逐一扫描全仓调用点：确认 `initCommonControlsEx`（`rtx_gui.cpp:914`）与 `setWindowTheme`（`workstation_canvas.cpp`）均有真实活跃调用；精准揪出唯一零调用公开封装 **`createDxgiFactory2`**（0 调用点）并遵循奥卡姆剃刀原则物理删除。 | 剩余全部动态加载封装调用点 ≥ 1 |
| **H-02** | `cmake/AuditSloc.cmake` | 升级为默认 `FATAL_ERROR` 硬阻断门禁：当未显式传入 `CEILING_CORE_SLOC` 时默认锁定 `CEILING_CORE_SLOC = 23000`，只要核心无头引擎 Pure SLOC（`include/` + `src/core/` + `src/io/` + `src/cli/`）超过 `23,000` 行，无参运行 `cmake -P cmake/AuditSloc.cmake` 即刻返回非零退出码阻断。 | `if/endif=10` 成对平衡；Core Engine Pure SLOC 远低于 `23,000` |
| **H-03 / W12.4-1** | `CMakeLists.txt` | 将 `project(CineRTX VERSION 0.3.0 ...)` 升级至 **`VERSION 0.4.0`**，确保 `CineRTX.exe --version`、`rtx_headless_cli.exe --version` 与 `--schema-json` 统一输出 `0.4.0`。 | 全仓 `0.3.0` 残留 = **0 命中**；`if/endif=78` 成对平衡 |
| **H-03 / W12.4-1** | `README.md`、`README_zh.md`、`webui/README.md` | 同步升级至 `v0.4.0`：如实写入 Phase 12 架构成果（C++26 ≤ 35 具名类型 [34/35]、核心引擎 ≤ 23,000 Pure SLOC、无头 `rtx_headless_cli.exe` 2 项物理 IAT / 桌面 `CineRTX.exe` ≤ 4 项物理 IAT、20 项 SSOT 参数契约、147 项单测、705.6 MHz Flicks 时基、DirectComposition 零回读管线与 `CineRTX-v0.4.0-windows-x64.zip` 发布包口径）。 | 3 份文档均以原始 `.md` 落盘，旧版已外置归档至 `work/_CineRTX_bak_archive/*.r61bak` |

> **历史归档说明（`docs/archive/`）**：核查 `docs/archive/` 现有 `HISTORY_SUMMARY_P1_P10.md` 与 `Phase1_to_Phase7_Evolution_Digest.md`，未擅自删除或篡改 Phase 11 原始目录；待宿主机完成阶段 2/3 实机回归并由专家组签署 `v0.4.0` 封板后，可统一并入历史摘要。

---

## 二、阶段 1 静态卫生与自动化门禁实测汇总

| 审计项 | 目标红线 | 实测结果 | 判定 |
| :--- | :--- | :--- | :---: |
| **`AuditTypes`（具名类型预算）** | ≤ 35，0 Violations | **34 / 35（0 Violations）** | **PASS** |
| **`AuditSpdx`（双许可头）** | 缺口 = 0 | **Missing = 0** | **PASS** |
| **`OldFormat`（旧式格式化）** | 0 命中 | **0 命中** | **PASS** |
| **`#include <commctrl.h>`** | 0 命中 | **0 命中** | **PASS** |
| **`0.3.0` 旧版本号残留** | 0 命中 | **0 命中** | **PASS** |
| **`std::getenv` / `g_app` / `#pragma comment(lib)` / `LoadLibrary[AW]`** | 全部 0 命中 | **全部 0 命中** | **PASS** |
| **仓内 `.bak` 残留** | 0 命中 | **`CineRTX/` 仓内 0 命中**（8 个旧版已外置归档至 `work/_CineRTX_bak_archive/*.r61bak`） | **PASS** |

---

## 三、待 Windows RTX 5060 宿主机现场执行的阶段 2–4 实机回归与封板清单

> **实证诚实性声明**：当前执行环境为无 NVIDIA RTX 5060 硬件与无 WTS 交互式桌面的沙箱容器，依据 06 号施工单军规 1 & 2，下列宿主机实机构建、五重门禁日志采集、WTS Session > 0 桌面压测与无头 Session 0 模块快照均需在物理宿主机现场执行并留存真实日志（证据目录建议落盘于仓外 `$env:TEMP\CineRTX\evidence\phase12\`，严禁污染源码树）。

### 3.1 阶段 1 宿主机 Clean Build 与五重门禁一键采集（Gate 12.4-1 ~ 12.4-4）

```powershell
$EVID = "$env:TEMP\CineRTX\evidence\phase12"
New-Item -ItemType Directory -Force -Path $EVID | Out-Null

# 1. Clean Build 全量目标（MSVC /std:c++latest /W4 /WX）
cmake --preset windows-msvc-relwithdebinfo
cmake --build --preset windows-msvc-relwithdebinfo --clean-first --target CineRTX rtx_headless_cli rtx_tests *>&1 | Tee-Object "$EVID\build_w12_4.log"

# 2. 四重静态门禁（全部必须返回退出码 0）
cmake -P cmake/AuditTypes.cmake   *>&1 | Tee-Object "$EVID\audit_types.log"
cmake -P cmake/AuditSloc.cmake    *>&1 | Tee-Object "$EVID\audit_sloc.log"
cmake -P cmake/AuditSpdx.cmake    *>&1 | Tee-Object "$EVID\audit_spdx.log"
cmake -DTARGET_BIN=build/windows-msvc-relwithdebinfo/RelWithDebInfo/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake *>&1 | Tee-Object "$EVID\audit_imports_gui.log"
cmake -DTARGET_BIN=build/windows-msvc-relwithdebinfo/RelWithDebInfo/rtx_headless_cli.exe -DMODE=HEADLESS -P cmake/AuditImports.cmake *>&1 | Tee-Object "$EVID\audit_imports_headless.log"

# 3. 双向运行 rtx_tests.exe（构建目录 + 仓库根目录，确认 147/147 PASS）
.\build\windows-msvc-relwithdebinfo\RelWithDebInfo\rtx_tests.exe *>&1 | Tee-Object "$EVID\rtx_tests_root.log"
Push-Location build\windows-msvc-relwithdebinfo\RelWithDebInfo
.\rtx_tests.exe *>&1 | Tee-Object "$EVID\rtx_tests_build.log"
Pop-Location
```

### 3.2 阶段 3 无头探针全功能验证（H-03 + W12.4-3）

```powershell
$CLI = ".\build\windows-msvc-relwithdebinfo\RelWithDebInfo\rtx_headless_cli.exe"
& $CLI --version      *>&1 | Tee-Object "$EVID\headless_version.log"     # 预期输出: CineRTX 0.4.0
& $CLI --schema-json  *>&1 | Tee-Object "$EVID\headless_schema.json"     # 预期退出码 0，含 20 项 SSOT 参数
& $CLI --probe --json *>&1 | Tee-Object "$EVID\headless_probe.json"      # 预期正常探测硬件信息
& $CLI --invalid-flag *>&1 | Tee-Object "$EVID\headless_badarg.log"      # 预期非零退出码、无弹窗崩溃
```

### 3.3 阶段 2 交互式桌面实机压测（WTS Session > 0，W12.4-2）

1. 先执行 `quser` 确认当前处于 `Active` 且 `SessionId > 0` 的真实交互式桌面会话，记录 RTX 5060 Laptop GPU 驱动版本（≥ 621.24 WHQL）。
2. **4K AV1 HDR10 全链路零拷贝重制**：输入 1080p SDR 素材，启用 `vsr-first`（VSR 4 + TrueHDR 1000 nits + RIFE `scale=0.5` 60fps + AV1 10-bit CQ22），核验 `D2H == 0 B`、显存峰值 < 5,986 MiB（7,300 MiB 的 82%）、`ffprobe` 验证 10-bit AV1 PQ HDR10 与音画同步。
3. **DirectComposition + WASAPI 稳定性**：播放 H.264 / HEVC / AV1 测试流并执行 100 次连续 Resize、全屏切换、GOP-Aware Seek 与音频设备切换，验证 `presented > 0`、零崩溃、零句柄/GDI 泄漏。
4. **NLE 多段硬切导出与零分配音频总线**：执行 `1V+1A` 多段硬切导出，核验导出总帧数与 705.6 MHz Flicks 理论值逐帧一致且切点为 IDR 帧。

---

## 四、`v0.4.0` 终局封板 Checklist 进度追踪

- [x] **Stage 1（代码与门禁收口）**：H-01（`commctrl.h` 全仓清零、`rtx_gui.cpp` 死 `<cstdlib>` 清除、`sys_dynload.h` 零调用 `createDxgiFactory2` 删除）、H-02（`AuditSloc.cmake` 默认 `23,000` `FATAL_ERROR` 硬阻断）、H-03 / W12.4-1（`VERSION 0.4.0` + `README.md` / `README_zh.md` / `webui/README.md` 同步）全部完成。
- [ ] **Stage 2 & 3（宿主机实机物证采集）**：待 Windows RTX 5060 宿主机执行 §3.1–§3.3 命令并归档真实日志。
- [ ] **Stage 4（`v0.4.0` 正式封板签发）**：待宿主机实机物证回传后，汇总呈报专家组与用户签署 `v0.4.0` 终审封板并由用户授权打标。
