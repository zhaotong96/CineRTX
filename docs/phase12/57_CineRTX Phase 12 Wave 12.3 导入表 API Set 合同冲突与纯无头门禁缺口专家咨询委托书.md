# CineRTX Phase 12 Wave 12.3 导入表 API Set 合同冲突与纯无头门禁缺口专家咨询委托书

- **文档编号**：`CINERTX-P12-DELEGATION-057`
- **归档路径**：`docs/phase12/57_CineRTX Phase 12 Wave 12.3 导入表 API Set 合同冲突与纯无头门禁缺口专家咨询委托书.md`
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）
- **接收主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）
- **前置依据**：
  - [`55_CineRTX Phase 12 Wave 12.3 动态加载断环与导入表审计终审裁决书暨封板授权令.md`](./55_CineRTX%20Phase%2012%20Wave%2012.3%20动态加载断环与导入表审计终审裁决书暨封板授权令.md)（`CINERTX-P12-FINAL-SEAL-055`）
  - [`56_CineRTX Phase 12 Wave 12.3 定点止血与导入表门禁收口施工汇报.md`](./56_CineRTX%20Phase%2012%20Wave%2012.3%20定点止血与导入表门禁收口施工汇报.md)（`CINERTX-P12-REPORT-056`）
  - [`05_CineRTX Phase 12 SuperPower 施工单_13项系统DLL净空与核心界面解耦.md`](./05_CineRTX%20Phase%2012%20SuperPower%20施工单_13项系统DLL净空与核心界面解耦.md)（`CINERTX-P12-WORKORDER-005`）
  - [`06_CineRTX Phase 12 SuperPower 施工单_全场景实机回归与v0.4.0封板验收.md`](./06_CineRTX%20Phase%2012%20SuperPower%20施工单_全场景实机回归与v0.4.0封板验收.md)（`CINERTX-P12-WORKORDER-006`）
- **审验标的**：
  - 门禁脚本：`cmake/AuditImports.cmake`（5,014 字节正式版）
  - 发行与测试 PE：`.vs/build/cmake/windows-msvc-ninja/CineRTX.exe`、`rtx_tests.exe`、`rtx_ngx_bridge.dll`
  - 线程与等待源码：`src/core/playback_session.cpp`、`playback_session_video.cpp`、`hw_transcode_stages.cpp`、`wasapi_audio_output.cpp`、`src/gui/workstation_canvas.cpp`
  - 构建入口：`CMakeLists.txt`、`src/cli/main.cpp`、`src/cli/main_entry.cpp`
- **呈报日期**：2026-11-24
- **委托主题**：呈报 56 号三项定点修复已物理落地、147/147 单测与类型/许可门禁全绿；但宿主机 PE IAT 实测发现 `CineRTX.exe` 命中 MSVC STL 的 `synchronization.lib` API Set 合同（`api-ms-win-core-synch-l1-2-0.dll`），且工程内不存在可审计的纯无头 PE 目标、私有 NGX 桥 DLL 的厂商 IAT 无法套用全局黑名单；请求专家组裁定导入表门禁语义、纯无头目标补建方案与 SLOC 口径，并签发 58 号裁决令。

---

## 一、 咨询背景与 56 号施工成果的宿主物理复验

依据 56 号汇报，协调组在 Windows 宿主机对三项定点修复、构建产物与全部门禁进行了逐项复验。除导入表门禁与 SLOC 外，其余指标均已物理达成：

| 验收项 | 56 号呈报 | 协调组宿主机复验（2026-11-24） | 判定 |
| --- | --- | --- | --- |
| **方案 A** `sys_dynload.h` | 已补 `<unknwn.h>` | `include/rtx/sys_dynload.h:25-26` 实体确认：`#include <windows.h>` 后紧随 `#include <unknwn.h>` | **PASS** |
| **方案 B** `rtx_gui.cpp` | 已改 `encoderJobStatus` | `src/gui/rtx_gui.cpp:797` 实体确认；全文 `snapshots[i].status` 残余 = 0 | **PASS** |
| **方案 C** 审计脚本 | 仅存 5,014 字节新版 | `cmake/AuditImports.cmake` = 5,014 字节；`cmake/` 内无 `(1)` 冲突副本 | **PASS** |
| **构建产物** | 待宿主机一键收网 | `CineRTX.exe` = 5,034,496 字节 @ 02:42:52；`rtx_tests.exe` = 5,755,904 字节 @ 02:42:51；增量构建返回 0，仅执行插件暂存 | **PASS（增量复核）** |
| **单元测试** | 待宿主机执行 | `147 test(s) run, 0 failed (46,693 ms)` | **PASS** |
| **`AuditTypes`** | 34 / 35 | `OK: 34/35 named types, all uniquely justified` | **PASS** |
| **`AuditSpdx`** | 缺失 = 0 | `GPL=113 MIT=39`，`result: PASS` | **PASS** |
| **源码卫生** | `#pragma comment(lib)` / `LoadLibrary[AW]` / `.bak` 全 0 | 实测 `0 / 0 / 0` | **PASS** |
| **`AuditImports`（GUI）** | 待宿主机 PE IAT 复核 | **FAIL**：5 项静态 IAT，含 `api-ms-win-core-synch-l1-2-0.dll` | **FAIL** |
| **HEADLESS（= 2 项）** | 待宿主机 PE IAT 复核 | 工程内**不存在**纯无头 PE 目标，无法出具实证 | **BLOCKED** |
| **`AuditSloc`** | 53 号披露 30,354 | 实测 **30,967**，高于 23,000 目标 7,967 行 | **未达** |

> 结论：56 号的三项外科修复本身已全部成功，`CineRTX.exe` 与 `rtx_tests.exe` 均已物理产出，147 项单测全绿。当前阻断双波次封板的不是源码断环，而是**导入表门禁口径与 MSVC STL 实现的物理冲突**，以及**纯无头目标与 SLOC 口径两项门禁设计缺口**。

---

## 二、 阻断项 A：GUI 静态 IAT 命中 API Set 合同（当前硬阻断）

### 2.1 宿主机实测输出

```text
[AuditImports] Blacklisted static IAT DLL: api-ms-win-core-synch-l1-2-0.dll
[AuditImports] GUI static IAT count 5 > 4
               (api-ms-win-core-synch-l1-2-0.dll; bcrypt.dll; gdi32.dll; kernel32.dll; user32.dll)
[AuditImports] FAIL (GUI): 6 violation(s) on .../CineRTX.exe
```

实际静态 IAT 为 5 项：

| # | DLL | 性质 |
| --- | --- | --- |
| 1 | `KERNEL32.dll` | 物理系统 DLL（白名单） |
| 2 | `USER32.dll` | 物理系统 DLL（GUI 白名单） |
| 3 | `GDI32.dll` | 物理系统 DLL（GUI 白名单） |
| 4 | `bcrypt.dll` | 物理系统 DLL（白名单） |
| 5 | `api-ms-win-core-synch-l1-2-0.dll` | **API Set 加载器合同，非物理 DLL 文件** |

> 脚本因 `_VIOLATIONS` 列表元素内含分号而发生 CMake 列表膨胀，将 2 项实际违规显示为 6 项（`rtx_tests.exe` 显示为 7 项）。此为计数显示缺陷，不改变 FAIL 结论，但应在 58 号裁决令中一并修正。

### 2.2 因果链：从 `std::jthread` 到 API Set 静态导入

| 层级 | 实体证据 | 说明 |
| --- | --- | --- |
| **业务源码** | `src/core/playback_session.cpp:145-146`（audio/video 双 `std::jthread`）、`src/core/hw_transcode_stages.cpp:170`（encode worker `std::jthread`）、`src/core/wasapi_audio_output.cpp:217`（callback `std::jthread`）、`src/gui/workstation_canvas.cpp:580`（export `std::jthread`） | 工程大量使用 C++20 `std::jthread` + `std::stop_token` |
| **等待语义** | `src/core/playback_session_video.cpp`、`src/core/hw_transcode_stages.cpp` 中的 `condition_variable_any::wait(lock, stop_token, pred)` | stop_token 集成等待，触发 STL 的原子等待路径 |
| **MSVC STL** | `<stop_token>:73-74`：`atomic<const _Stop_callback_base*> _Current_callback = nullptr; // atomic just to get wait/notify support` | 只要使用 `stop_token`，STL 即拉入原子等待设施 |
| **CRT 预编译对象** | `CineRTX.exe.map:9449-9450`：`__std_atomic_notify_all_direct` / `__std_atomic_wait_direct` ← `libcpmt:atomic_wait.obj` | 来自 MSVC 预编译静态 CRT，不在本仓库源码内 |
| **CRT 链接指令** | MSVC `atomic_wait.cpp:14`：`#pragma comment(lib, "synchronization")` | 该 pragma 位于 MSVC CRT 源码，非仓库代码 |
| **导入符号** | `CineRTX.exe.map:9947-9948`：`WaitOnAddress` / `WakeByAddressAll` ← `synchronization:api-ms-win-core-synch-l1-2-0.dll`；`:10991-10992`：`__imp_WaitOnAddress` / `__imp_WakeByAddressAll` | 最终固化进 `CineRTX.exe` 静态 IAT |
| **OS 解析层** | `KernelBase.dll` 导出 `WaitOnAddress`、`WakeByAddressAll`、`WakeByAddressSingle`；`kernel32.dll` 不导出这三个符号 | API Set 由加载器解析到 `KernelBase`/`ntdll`，不是可部署的物理文件 |

### 2.3 为什么不能靠 `_WIN32_WINNT` 或目标平台宏消除

`__std_atomic_wait_direct` 与 `__std_atomic_notify_all_direct` 位于 MSVC 预编译的 `libcpmt:atomic_wait.obj`，其 `#pragma comment(lib, "synchronization")` 在 CRT 构建时即已固化。消费方工程降低 `_WIN32_WINNT` 或改写目标版本宏，不会重新编译该对象，也不会改变其导入合同。因此“把目标系统降到 Windows 7 以走旧同步回退”的方案在物理上不成立。

### 2.4 性质判定

- 本项**不是**仓库卫生回退：全仓 `#pragma comment(lib)` 实测为 **0**，`LoadLibrary[AW]` 实测为 **0**；命中的 `synchronization.lib` 指令在 MSVC CRT 内部。
- 本项**是** 05 号施工单“11 项黑名单含 `api-ms-win-*`”条款与 C++20/26 标准线程模型之间的**门禁语义冲突**。只要保留 `std::jthread` / `std::stop_token`，该 API Set 合同就无法通过纯源码卫生手段消除。
- API Set 合同在 Windows 10/11 上由加载器无条件解析到始终存在的 `KernelBase`，无独立文件、无独立部署单元、无“缺库”可能；其风险等级与 `shell32.dll`、`d3d12.dll`、`ole32.dll` 等可独立缺失的物理 DLL 不同。

---

## 三、 阻断项 B：HEADLESS“严格 = 2 项”门禁缺少可审计 PE 目标

### 3.1 现有构建目标的事实

| 目标 | 源码与链接事实 | 实测静态 IAT | 能否作为 HEADLESS 证据 |
| --- | --- | --- | --- |
| `CineRTX.exe` | `src/gui/rtx_gui.cpp` + `preview_viewport.cpp` + `workstation_canvas.cpp` + `src/cli/main.cpp`；`/SUBSYSTEM:WINDOWS`；链接 `gdi32 user32` | 5 项（含 API Set） | **否**，GUI 混合发行态 |
| `rtx_tests.exe` | 链接 `rtx_core`、`preview_viewport.cpp`、`user32`、`rtx_ngx_bridge` | 5 项：`rtx_ngx_bridge.dll`、`USER32.dll`、`bcrypt.dll`、`KERNEL32.dll`、`api-ms-win-core-synch-l1-2-0.dll` | **否**，GUI/测试混合态 |
| `rtx_ngx_bridge.dll` | 私有 NGX 桥，按需加载 | 3 项：`KERNEL32.dll`、`USER32.dll`、`ADVAPI32.dll` | **否**，厂商隔离 DLL |
| `rtx_headless_cli` | **不存在** | — | **缺失** |

### 3.2 缺口分析

05 号施工单任务 W12.3-1 明确要求“纯无头测试/CLI 目标（如 `rtx_headless_cli` / `rtx_tests` 无头核心集）仅链接 `kernel32.lib` 与 `bcrypt.lib`”，Gate 12.3-1 亦要求无头核心静态 IAT 严格等于 2 项。但当前工程从未创建该目标：

1. `CineRTX.exe` 是 `/SUBSYSTEM:WINDOWS` 的 GUI 宿主，静态导入 `user32`/`gdi32`，以 `MODE=HEADLESS` 审计必然得到 5 项，不能作为无头证据；
2. `rtx_tests.exe` 为 GUI 视口测试桩与私有桥的混合态，同样不具备无头证据资格；
3. 对静态库 `rtx_core.lib` 无法直接执行 PE IAT 解析，只能做未解析符号/`.drectve` 间接审计，证据强度不足。

因此，当前“无头引擎静态 IAT = 2 项”没有任何可执行的 PE 物证，属于**门禁目标缺失**，而非施工失败。

### 3.3 低成本补齐路径

`src/cli/main_entry.cpp` 与 `src/cli/main.cpp` 已构成完整的纯 CLI 入口：前者定义控制台 `main` 并调用 `rtxCliMain`，后者实现 `rtxCliMain` 且不包含任何 GUI 头文件或 GUI 符号。复用这两个既有 TU，加 `src/cli/shim_probe.cpp` 与 `rtx_core`，即可新增一个 `/SUBSYSTEM:CONSOLE` 的 `rtx_headless_cli` 目标：

- **新增业务逻辑 = 0 行**，仅复用既有源码；
- **新增具名类型 = 0 个**，`AuditTypes` 仍为 34/35；
- **预期静态 IAT = 3 项**：`KERNEL32.dll`、`bcrypt.dll`、`api-ms-win-core-synch-l1-2-0.dll`（2 项物理 DLL + 1 项 API Set 合同）；
- 可在无显示器 Session 0 下直接执行 `--version`、`--schema-json` 与纯 CPU 参数校验，为 Wave 12.4 的无头降级实测提供真实载体。

---

## 四、 阻断项 C：私有 NGX 桥 DLL 的厂商 IAT 不能套用全局黑名单

### 4.1 实测导入

`rtx_ngx_bridge.dll` 静态 IAT 为 3 项，其中 `ADVAPI32.dll` 属物理 DLL 黑名单命中，`USER32.dll` 属 HEADLESS 白名单越界：

| DLL | 导入符号 | 归属 |
| --- | --- | --- |
| `KERNEL32.dll` | 基础内核符号 | 项目代码 + 厂商库 |
| `USER32.dll` | `MessageBoxA`、`GetWindowThreadProcessId` | **NVIDIA 静态库** |
| `ADVAPI32.dll` | `RegOpenKeyExW`、`RegQueryValueExW`、`RegCloseKey` | **NVIDIA 静态库**（`NGXGetPathFromRegistry`） |

### 4.2 来源确证

- 仓库源码扫描 `RegOpenKeyEx|RegQueryValueEx|RegCloseKey` = **0 命中**；`MessageBoxA` 仅出现在 `src/gui/workstation_canvas.cpp:471`，不属于桥 DLL 源码；
- `rtx_ngx_bridge.dll` 内嵌字符串明确包含 `NGXGetPathFromRegistry`、`SOFTWARE\NVIDIA Corporation\Global\NGXCore` 与 `.../nvsdk_ngx_lib_windows.cpp`，证明注册表探测来自 NVIDIA `nvsdk_ngx_s.lib`；
- 该 DLL 是 GPL 隔离边界：专有 `nvsdk_ngx_s.lib` 仅允许私有链接进此 DLL，主程序 `CineRTX.exe` 不静态导入它。

### 4.3 门禁语义问题

05 号施工单将 11 项黑名单表述为“全局静态 IAT 零容忍”，但该表述无法适用于厂商私有桥 DLL：NVIDIA 静态库的 `ADVAPI32`/`USER32` 导入无法在不修改厂商二进制的前提下清除。若对 `rtx_ngx_bridge.dll` 执行 `MODE=HEADLESS` 审计，当前脚本会同时报出 `advapi32.dll` 黑名单命中、`user32.dll` 非白名单与 `3 != 2` 计数失败，形成**结构性永红门禁**。

> 建议在 58 号裁决令中将审计范围明确为“发行可执行目标（`CineRTX.exe` + 纯无头 CLI）”，并将 `rtx_ngx_bridge.dll` 列为“厂商隔离边界，豁免原 11 项黑名单（10 项物理 DLL + `api-ms-win-*`），但须在发布说明中披露其厂商 IAT”；`rtx_tests.exe` 列为“测试台架，不纳入发行门禁”。

---

## 五、 附带门禁风险：Pure SLOC 30,967 与 23,000 目标

`AuditSloc` 宿主机实测：

```text
Canonical Files Scanned: 149 files (include, src, tests, cmake)
Total Physical Lines:    37,850
Blank Lines:             2,950
Semantic Comments:       3,933
Pure SLOC (Code only):   30,967
```

06 号施工单 Gate 12.4-2 仍要求 `AuditSloc ≤ 23,000`，当前超出 **7,967 行**。53 号汇报已诚实披露 30,354 行并建议通过物理拆分 `src/gui/` 或独立 `rtx_shim` 仓/子模块边界达成，但 55 号裁决令未对该项作出裁定。若不在封板前明确口径，Wave 12.4 的 Gate 12.4-2 将必然失败。

可选路径：

| 路径 | 做法 | 代价 | 评价 |
| --- | --- | --- | --- |
| **A. 口径修正** | 公开披露 30,967 行，将 23,000 改为“核心无头子集”指标，GUI/测试单独计账 | 需修订 06 号 Gate，但不删功能、不伪造压缩 | 诚实、低风险 |
| **B. 物理拆分** | 将 `src/gui/` 拆为独立 target/仓/子模块，仅对核心引擎计 SLOC | 构建与发布流程变复杂，但可真实达成 23,000 | 若 23,000 为硬性发布 KPI，优先 |
| **C. 延后豁免** | 在 Wave 12.4 申请一次性豁免 | 与 06 号 Gate 冲突，需专家组明文授权 | 不推荐作为默认 |

---

## 六、 协调组预研方案 A / B / C

### 6.1 方案 A（推荐）：门禁语义修正 + 纯无头探针 + 桥边界排除

1. **`cmake/AuditImports.cmake` 语义修正**：
   - 保留原 11 项黑名单中的 10 项**物理 DLL** 零容忍；
   - 新增唯一 API Set 允许项 `api-ms-win-core-synch-l1-2-0.dll`，其余 `api-ms-win-*` 仍零容忍；
   - 计数口径改为“物理 DLL 计数”：`HEADLESS` 物理集合严格等于 `{kernel32.dll, bcrypt.dll}`（2 项），`GUI` 物理集合为 `{kernel32.dll, user32.dll, gdi32.dll, bcrypt.dll}` 的子集（≤ 4 项）；API Set 合同单独列出、不计入物理计数，但总数与明细必须打印；
   - 修正 `_VIOLATIONS` 分号列表膨胀导致的错误计数。
2. **新增 `rtx_headless_cli` 控制台目标**：
   - 复用 `src/cli/main.cpp`、`src/cli/main_entry.cpp`、`src/cli/shim_probe.cpp`；
   - 仅链接 `rtx_core`，不链接 `user32`/`gdi32`；
   - 预期物理 IAT = 2 项 + API Set 1 项。
3. **审计范围收口**：
   - 发行门禁：`CineRTX.exe`（GUI，≤ 4 物理 + 1 API Set）、`rtx_headless_cli.exe`（HEADLESS，= 2 物理 + 1 API Set）；
   - 豁免并披露：`rtx_ngx_bridge.dll`（厂商隔离边界，3 项 IAT）、`rtx_tests.exe`（测试台架，5 项 IAT）。
4. **修订 05/06 号措辞**：将“静态 IAT ≤ 4 / = 2”明确为“物理系统 DLL ≤ 4 / = 2，另允许 1 项 `api-ms-win-core-synch-l1-2-0.dll` 加载器合同”，避免后续门禁再次误判。

**预期结果**：`CineRTX.exe` GUI PASS（4 物理 + 1 API Set），`rtx_headless_cli.exe` HEADLESS PASS（2 物理 + 1 API Set），10 项物理 DLL 黑名单命中 = 0，非白名单 API Set 命中 = 0。
**风险**：极低；无运行时代码改动，无新增业务逻辑，API Set 在 Windows 10/11 上始终由加载器解析到 `KernelBase`，无头 Session 0 行为不变。

### 6.2 方案 B：源码级去 `jthread` / `stop_token`

将 `playback_session.cpp`、`playback_session_audio.cpp`、`playback_session_video.cpp`、`hw_transcode_stages.cpp`、`wasapi_audio_output.cpp`、`workstation_canvas.cpp` 中的 `std::jthread`、`std::stop_token` 与 `condition_variable_any` 集成等待，替换为 `std::thread` + `std::atomic<bool>` + `std::condition_variable`，以维持字面上的 2 项 IAT。

**代价与风险**：
- 触及核心播放、转码、音频回调与导出线程的取消/唤醒语义，存在竞态、死锁与关闭时序回归风险；
- 需重新验证 147 项单测与 100 次 Resize / 播放 Seek 等实机场景；
- 放弃 C++20/26 标准停止语义，与 Phase 12 “极限现代 C++” 的架构目标相悖；
- 即使改完，仍需全量扫描其他 STL 原子等待消费者（`std::atomic::wait`、`std::latch`、`std::barrier`、`std::semaphore`），不能保证一次性归零。

**结论**：仅在专家组认定“2 项物理 + API Set”不可接受、必须追求字面 2 项时启用；不建议作为首选。

### 6.3 方案 C：自定义 `synchronization` ABI 垫片

在工程内提供 `__std_atomic_wait_direct`、`__std_atomic_notify_one_direct`、`__std_atomic_notify_all_direct` 的强符号覆盖，内部通过 `GetModuleHandleW(L"KernelBase.dll")` + `GetProcAddress` 动态解析 `WaitOnAddress`/`WakeByAddress*`，或用 SRW + `CONDITION_VARIABLE` 自行模拟。

**优点**：保留 `std::jthread`/`stop_token`，且静态 IAT 可维持 2 项。
**缺点**：接管 MSVC CRT 的内部 ABI，需覆盖 `atomic_wait.obj` 的全部导出符号或使用 `/ALTERNATENAME` 规避重复定义，并精确匹配 `_Remaining_timeout`、`_DEBUG` 断言、唤醒丢失与 ABA 语义；一旦实现偏差，将在播放/转码高频路径引入极难定位的挂起。**风险高于收益，不推荐**。

### 6.4 方案对比

| 方案 | 源码改动 | 预期 IAT | 封板风险 | 推荐度 |
| --- | --- | --- | --- | --- |
| **A. 门禁语义修正 + 无头探针** | 脚本 + 少量 CMake（复用既有 TU），无业务逻辑 | 2 物理 + 1 API Set | 极低 | **首选** |
| **B. 去 jthread/stop_token** | 6 个核心并发文件重写 | 字面 2 项 | 高 | 备选 |
| **C. synchronization ABI 垫片** | 新增 CRT 覆盖层 | 字面 2 项 | 高 | 不推荐 |
| **D. 默认接受 5 项 GUI IAT** | 无 | 5 项 | 违反 05/06 字面门禁 | 不可接受 |

---

## 七、 请求专家委员会裁决与授权事项

为确保 Wave 12.2 暨 Wave 12.3 双波次封板不再被门禁语义问题反复阻断，协调组提请首席领域专家组签署 **《58 号终审裁决书》**，出具如下法定决议：

1. **裁定导入表门禁语义**：在方案 A / B / C 中择一定型。若采纳方案 A，请明文核准“物理 DLL 计数 + 唯一 API Set 合同 `api-ms-win-core-synch-l1-2-0.dll`”的新口径，并授权同步修订 05 号施工单 Gate 12.3-1 与 06 号施工单 Gate 12.4-3 的措辞。
2. **核准纯无头目标补建**：授权新增 `rtx_headless_cli` 控制台目标（复用 `src/cli/main.cpp` + `src/cli/main_entry.cpp` + `src/cli/shim_probe.cpp`），并将其纳入 HEADLESS 静态 IAT 审计；明确 `rtx_ngx_bridge.dll` 与 `rtx_tests.exe` 的审计豁免/披露边界。
3. **裁定 Pure SLOC 口径**：对 30,967 行与 23,000 目标的差额作出裁决——采用“核心无头子集计账 + 诚实披露”、执行 `src/gui/` 物理拆分，或给予 Wave 12.4 明文豁免；不接受删测试、删功能或伪造压缩。
4. **授权封板与解锁 Wave 12.4**：在 58 号裁决令落地后，授权协调组在宿主机重跑最终门禁；全绿后直接签署 Wave 12.2 暨 Wave 12.3 双波次封板令，创建本地 Git 基线快照（**严禁执行 `git push`**），并即刻启动 Wave 12.4 全场景实机回归与 `v0.4.0` 发布验收。
5. **补齐封板物证**：58 号裁决令须附带两形态（HEADLESS / GUI）审计命令、预期输出与测试/桥豁免清单，确保后续任何一次复验都能得到唯一、可复现的 PASS/FAIL 结论。

---

## 八、 附录：宿主机复验命令与实测摘要

```powershell
# 1. 增量构建复核（当前产物已存在；封板前应再执行一次 clean build 取 0E0W 全量日志）
cmake --build --preset windows-msvc-ninja

# 2. 单元测试
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
# 实测：147 test(s) run, 0 failed (46,693 ms)

# 3. 类型与许可门禁
cmake -P cmake/AuditTypes.cmake
# 实测：OK: 34/35 named types, all uniquely justified
cmake -P cmake/AuditSpdx.cmake
# 实测：GPL=113 MIT=39, result: PASS

# 4. 体量门禁
cmake -P cmake/AuditSloc.cmake
# 实测：Pure SLOC 30,967（目标 ≤ 23,000）

# 5. GUI 静态 IAT（当前 FAIL）
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake
# 实测：5 项 = KERNEL32, USER32, GDI32, bcrypt, api-ms-win-core-synch-l1-2-0
#       FAIL: Blacklisted static IAT DLL + GUI count 5 > 4

# 6. 测试态与桥 DLL 的越界审计（用于证明范围需要收口）
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/rtx_tests.exe -DMODE=GUI -P cmake/AuditImports.cmake
# 实测：5 项，FAIL（含 rtx_ngx_bridge.dll 与 API Set）
cmake -DTARGET_BIN=.vs/build/cmake/windows-msvc-ninja/rtx_ngx_bridge.dll -DMODE=HEADLESS -P cmake/AuditImports.cmake
# 实测：3 项，FAIL（ADVAPI32 黑名单 + USER32 非白名单 + count 3 != 2）
```

关键物证索引：

| 证据 | 位置 |
| --- | --- |
| `libcpmt:atomic_wait.obj` | `CineRTX.exe.map:9449-9450` |
| `WaitOnAddress` / `WakeByAddressAll` 导入 | `CineRTX.exe.map:9947-9948` |
| `__imp_WaitOnAddress` / `__imp_WakeByAddressAll` | `CineRTX.exe.map:10991-10992` |
| CRT `synchronization.lib` 指令 | MSVC `crt/src/stl/atomic_wait.cpp:14` |
| `stop_token` 原子等待注释 | MSVC `include/stop_token:73-74` |
| `jthread` 使用点 | `playback_session.cpp:145-146`、`hw_transcode_stages.cpp:170`、`wasapi_audio_output.cpp:217`、`workstation_canvas.cpp:580` |
| 桥 DLL 厂商注册表探测 | `rtx_ngx_bridge.dll` 内嵌 `NGXGetPathFromRegistry` / `nvsdk_ngx_lib_windows.cpp` |

---

*CineRTX 项目总协调负责人（Coordinator）*  
*呈报：多媒体系统工程与 GPU 异构计算首席领域专家组*
