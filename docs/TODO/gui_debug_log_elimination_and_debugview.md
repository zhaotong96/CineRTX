# CineRTX GUI 临时调试日志消解与 DebugView 机制演进规范 (RFC)

> **文档标识**：`TASK-TODO-DEBUG-LOG-OCCAM-001`  
> **所属目录**：[`docs/TODO/gui_debug_log_elimination_and_debugview.md`](./gui_debug_log_elimination_and_debugview.md)  
> **起草日期**：2026-11-01  
> **状态**：已闭环就绪 / 施工验证完成 (Completed & Verified — 2026-11-01)  
> **适用目标**：[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)（`writeStartupLog` 与 `writeResultLog`）  
> **核心宪章基准**：[`docs/spec/architecture.md`](../spec/architecture.md) §1.3（奥卡姆剃刀与禁绝开关铁律）

---

## 1. 现状审查与架构冲突 (Problem Statement & Architectural Conflict)

在当前 Win32 原生 GDI GUI 实现（[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)）中，存在两处用于冷启动性能分析与单任务转码结果回传的诊断逻辑：
1. `writeStartupLog`（第 582–614 行）：读取环境变量 `RTX_GUI_STARTUP_LOG`，通过 `CreateFileW` 强行落盘启动耗时、DPI 及自绘阶段基准数据；
2. `writeResultLog`（第 909–935 行）：读取环境变量 `RTX_GUI_RESULT_LOG`，通过 `CreateFileW` 强行落盘作业耗时、帧数、显存传输量（`d2h`/`h2d`）及输入输出路径。

### 1.1 违背的核心设计哲学

1. **违背《奥卡姆剃刀与禁绝冗余开关铁律》**：
   依据 [`docs/spec/architecture.md`](../spec/architecture.md) §1.3 与 [`docs/TODO/README.md`](./README.md) §1，CineRTX 坚决禁绝一切随意引入的环境变量开关（`RTX_XXX`）。`RTX_GUI_RESULT_LOG` 与 `RTX_GUI_STARTUP_LOG` 属于历史开发测试阶段遗留的隐蔽配置分支，将原本应确定性闭环或经由标准诊断流暴露的能力转嫁为外部环境感知，严重侵蚀了系统的极简纯粹性。
2. **违背 KISS 原则与磁盘卫生（Disk Hygiene）**：
   此类日志本质上属于**瞬态非持久化调试信息（Ephemeral Debug Information）**。强行通过同步 Win32 文件 I/O（`CreateFileW` + `WriteFile`）落盘，不仅徒增 UI 线程/工作线程的 I/O 抖动风险，且引申出诸如路径有效性校验、目录创建权限、UNC 网络共享映射乃至崩溃清理等一系列本不该存在的偶发复杂性（正如外部审计所纠缠的 UNC 路径争议）。

---

## 2. 演进重构设计方案 (Target Architecture: OutputDebugString & DebugView)

### 2.1 告别磁盘落盘，拥抱 Windows 原生 DebugView 监听流
对于仅在联调、排障、UI 性能剖析时使用的瞬态诊断数据，**彻底废除磁盘文件创建与落盘逻辑**，全面切换至 Windows 工业级标准调试输出通道：
* **原生 API**：使用 Win32 系统的 `::OutputDebugStringW`；
* **监听工具**：开发者或测试工程师使用微软官方 Sysinternals **DebugView (DbgView.exe)**，即可在无需任何文件落盘、无需任何环境变量配置的情况下，零侵入、实时捕获毫秒级高精度日志；
* **环境免疫**：在生产发行版或未挂载调试器的环境下，`::OutputDebugStringW` 开销几乎为零（仅进行一次内核态上下文探测），绝不产生任何磁盘残留垃圾或文件权限纠纷。

### 2.2 彻底物理切除环境变量开关
与 [`docs/phase4/CineRTX 代码与架构深度协查.md`](../phase4/CineRTX%20代码与架构深度协查.md) 维度七裁决保持一致：
* 从 [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) 中彻底切除 `GetEnvironmentVariableW(L"RTX_GUI_RESULT_LOG", ...)` 与 `GetEnvironmentVariableW(L"RTX_GUI_STARTUP_LOG", ...)`；
* 彻底拔除无意义的分支判断，将诊断输出直接以统一格式化前缀（如 `[CineRTX:GUI]` / `[CineRTX:Bench]`）无条件推送至 `::OutputDebugStringW`。

---

## 3. 施工任务拆解卡片 (Granular Task Card)

### 任务卡片 ACT-TODO-GUI-LOG-01: GUI 诊断流原生化与环境变量拔除
- **执行文件**：[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)
- **整改步骤**：
  1. 定义轻量格式化辅助内联函数：
     ```cpp
     inline void debugTrace(const wchar_t* format, ...) {
     #if defined(_WIN32)
         wchar_t buffer[1024];
         va_list args;
         va_start(args, format);
         _vsnwprintf_s(buffer, sizeof(buffer) / sizeof(wchar_t), _TRUNCATE, format, args);
         va_end(args);
         ::OutputDebugStringW(buffer);
     #endif
     }
     ```
  2. 重构 `writeStartupLog`：移除 `GetEnvironmentVariableW` 与 `CreateFileW`，将启动统计（`startup_ms`、`dpi`、`paint_ms`）直推 `debugTrace`；
  3. 重构 `writeResultLog`：移除 `GetEnvironmentVariableW` 与 `CreateFileW`，将作业结果（`frames`、`elapsed_ms`、`zero_copy`、`d2h`/`h2d`）直推 `debugTrace`；
  4. 消除所有涉及该日志的磁盘清理与权限假设，达成 100% 内存驻留与无害瞬态输出。

---

## 4. 验收判据 (Acceptance Criteria)

- [x] **准则 1 (零文件残留)**：GUI 启动、绘制、转码、关闭的全生命周期中，工作目录与临时目录均不产生任何附加调试 `.log` 或 `.txt` 文件；
- [x] **准则 2 (零环境变量)**：`src/gui/rtx_gui.cpp` 中检索 `RTX_GUI_RESULT_LOG` 与 `RTX_GUI_STARTUP_LOG` 的匹配数严格为 0；
- [x] **准则 3 (DebugView 实时直连)**：在宿主机启动 `DbgView.exe`，启动 `CineRTX.exe` 时能清晰、实时看到格式规整的 `[CineRTX]` 前缀启动与转码指标。
