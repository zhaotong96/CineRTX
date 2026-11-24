# CineRTX Phase 12 Wave 12.3 动态加载断环与导入表审计专家咨询委托书

- **文档编号**：`CINERTX-P12-DELEGATION-054`  
- **归档路径**：`docs/phase12/54_CineRTX Phase 12 Wave 12.3 动态加载断环与导入表审计专家咨询委托书.md`  
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）  
- **接收主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
- **前置依据**：  
  - [`52_CineRTX Phase 12 Wave 12.2-D 单点编译止血暨Wave12.2全量大封板裁决书与Wave12.3作战令.md`](./52_CineRTX%20Phase%2012%20Wave%2012.2-D%20单点编译止血暨Wave12.2全量大封板裁决书与Wave12.3作战令.md)（`CINERTX-P12-FINAL-SEAL-052`）  
  - [`05_CineRTX Phase 12 SuperPower 施工单_13项系统DLL净空与核心界面解耦.md`](./05_CineRTX%20Phase%2012%20SuperPower%20施工单_13项系统DLL净空与核心界面解耦.md)（`CINERTX-P12-WORKORDER-005`）  
  - [`53_CineRTX Phase 12 Wave 12.2 终局止血暨Wave12.3系统DLL净空施工汇报.md`](./53_CineRTX%20Phase%2012%20Wave%2012.2%20终局止血暨Wave12.3系统DLL净空施工汇报.md)（`CINERTX-P12-REPORT-053`）  
- **审验标的**：  
  - 动态加载与 GUI 阻断源文件：`include/rtx/sys_dynload.h`、`src/gui/rtx_gui.cpp`  
  - 门禁脚本命名冲突：`cmake/AuditImports.cmake` 与 `cmake/AuditImports (1).cmake`  
- **呈报日期**：2026-11-24  
- **委托主题**：呈报 Wave 12.2 终局止血通过、`rtx_tests.exe` 成功剥离 11 项黑名单系统 DLL 静态导入之重大战果；深度剖析阻断 `CineRTX.exe` 最终编译链接的两处语法断环及门禁脚本冲突缺陷，请求专家组出具权威定型解决方案并签署 55 号裁决令。

---

## 一、 咨询背景与已确证重大战果

依据 52 号裁决令与 05 号施工单，施工单位提交了 53 号汇报。协调组在 Windows 宿主机 MSVC 环境执行了物理复验，以下核心指标已**100% 物理确证达成**：

1. **Wave 12.2 终局具名类型指标历史性达成**：  
   `cmake -P cmake/AuditTypes.cmake` 实测全仓合法具名类型严格为 **34 / 35 个（0 Violations）**，硬阻断（`RTX_AUDIT_TYPES_STRICT=ON`）全绿锁定。
2. **前序断环完全排除**：  
   `include/rtx/io.h` 死声明清除与 `src/io/ffmpeg_io.cpp:995` 元组解构修复成功，`rtx_core.lib` 顺利生成，`rtx_tests.exe` 成功完成物理链接。
3. **11 项黑名单系统 DLL 静态导入物理清零**：  
   在宿主机对 `rtx_tests.exe` 执行 `dumpbin /imports` 实测证实：`d3d12.dll`、`dxgi.dll`、`ole32.dll`、`shell32.dll`、`comdlg32.dll`、`comctl32.dll`、`dwmapi.dll`、`uxtheme.dll` **全部已从静态 IAT 中净空剥离**！仅依赖 `USER32.dll`、`bcrypt.dll`、`KERNEL32.dll` 及私有 `rtx_ngx_bridge.dll`。

---

## 二、 物理编译阻断缺陷深度剖析（明确问题）

当前阻断整个工程交付、导致 `CineRTX.exe` 无法产出的缺陷精确锁定于以下两处源码及一处构建脚本冲突：

### 2.1 问题 A：`include/rtx/sys_dynload.h:106` 缺失 COM 基础头文件导致 `LPUNKNOWN` 语法错误（MSVC C2061）

#### 宿主机物理错误日志
```text
FAILED: CMakeFiles/CineRTX.dir/src/cli/main.cpp.obj
C:\PROGRA~1\MICROS~4\18\COMMUN~1\VC\Tools\MSVC\1451~1.362\bin\Hostx64\x64\cl.exe ... -c "src\cli\main.cpp"
[REDACTED_PATH]\include\rtx/sys_dynload.h(106): error C2061: syntax error: identifier 'LPUNKNOWN'
[REDACTED_PATH]\include\rtx/sys_dynload.h(108): error C2061: syntax error: identifier 'LPUNKNOWN'
[REDACTED_PATH]\include\rtx/sys_dynload.h(113): error C2065: 'outer': undeclared identifier
[REDACTED_PATH]\include\rtx/sys_dynload.h(113): error C2065: 'ctx': undeclared identifier
[REDACTED_PATH]\include\rtx/sys_dynload.h(113): error C2065: 'riid': undeclared identifier
[REDACTED_PATH]\include\rtx/sys_dynload.h(113): error C2065: 'ppv': undeclared identifier
```

#### 根因剖析
在 `include/rtx/sys_dynload.h` 头部（第 22–25 行）定义了 `#define WIN32_LEAN_AND_MEAN` 并包含了 `<windows.h>`。在此宏定义下，Windows SDK 会剔除 COM 相关子系统头文件。当 `src/cli/main.cpp` 引入 `sys_dynload.h` 时，第 106 行 `coCreateInstance` 声明中的形参类型 `LPUNKNOWN` 未定义，导致 MSVC 抛出 `error C2061` 及级联形参未声明错误。

---

### 2.2 问题 B：`src/gui/rtx_gui.cpp:797` 对元组类型 `EncoderJob` 非法访问成员字段（MSVC C2039）

#### 宿主机物理错误日志
```text
FAILED: CMakeFiles/CineRTX.dir/src/gui/rtx_gui.cpp.obj
C:\PROGRA~1\MICROS~4\18\COMMUN~1\VC\Tools\MSVC\1451~1.362\bin\Hostx64\x64\cl.exe ... -c "src\gui\rtx_gui.cpp"
[REDACTED_PATH]\src\gui\rtx_gui.cpp(797): error C2039: 'status': is not a member of 'std::tuple<uint64_t,std::string,std::vector<std::filesystem::path,...>,...,rtx::JobStatus,...>'
```

#### 根因剖析
在 Wave 12.2-D 中，`EncoderJob` 已被降级为 14 元 `std::tuple` 别名；在 `src/gui/rtx_gui.cpp` 第 797 行的 `WM_APP_PROGRESS` 消息处理分支中，代码仍残存点操作符旧访问：
```cpp
// 破损点（src/gui/rtx_gui.cpp:797）：
if (snapshots[i].status == rtx::JobStatus::Running) {
```
由于元组不存在 `.status` 成员变量，MSVC 抛出致命 `error C2039` 阻断编译。

---

### 2.3 问题 C：构建审计脚本命名冲突（`cmake/AuditImports (1).cmake`）

#### 现象与影响
施工单位提交的新版 PE 导入表审计脚本在落盘时与历史旧版 `cmake/AuditImports.cmake` 发生盘符冲突，导致新版脚本被保存为 `cmake/AuditImports (1).cmake`（5,014 字节，支持纯 Python/dumpbin 双引擎），而工程根目录 `cmake/AuditImports.cmake`（2,630 字节）仍为未更新的旧版。

---

## 三、 协调组预研外科手术修复方案

协调组已完成定点极简补丁预研，呈报专家组技术裁定：

### 3.1 方案 A：`include/rtx/sys_dynload.h` 补全 COM 接口定义
在 `include/rtx/sys_dynload.h` 第 25 行 `<windows.h>` 之后，显式引入 COM 核心头文件 `<unknwn.h>`（或 `<objbase.h>`）：
```cpp
// include/rtx/sys_dynload.h 第 25 行起：
#include <windows.h>
#include <unknwn.h>
```

### 3.2 方案 B：`src/gui/rtx_gui.cpp:797` 调用现成内联访问器
调用 `include/rtx/job_queue.h` 第 57 行已声明并实现的极简内联函数 `encoderJobStatus`：
```cpp
// 修复后（src/gui/rtx_gui.cpp:797）：
if (encoderJobStatus(snapshots[i]) == rtx::JobStatus::Running) {
```

### 3.3 方案 C：覆盖整合导入表审计脚本
将 `cmake/AuditImports (1).cmake` 覆盖写入 `cmake/AuditImports.cmake`，并物理删除冲突的 `(1)` 临时文件。

---

## 四、 请求专家委员会裁决与授权事项

为确保 Phase 12 Wave 12.3 彻底闭环并完成 Wave 12.2 的总结算，协调组提请首席领域专家组签署 **《55 号终审裁决书》**，出具如下法定决议：

1. **审定两处编译断环定型代码**：对上述 `sys_dynload.h` 与 `rtx_gui.cpp` 定点补丁给予技术审查与方案核准；
2. **裁定导入表门禁规范与脚本覆盖**：核准 `AuditImports.cmake` 覆盖并确认 PE IAT 门禁判定准则（无头态严格 `= 2`，GUI 桌面态 `≤ 4`）；
3. **下达双波次大圆满封板授权令**：授权施工单位立即合入止血补丁；在宿主机实测达成 **61/61 目标 0E 0W、双向 147/147 PASS、PE IAT 审计全绿** 后，直接签署本地 Git 封板快照（**严格禁止执行 `git push`**）。

---
*CineRTX 项目总协调负责人（Coordinator）*  
*呈报：多媒体系统工程与 GPU 异构计算首席领域专家组*
