# CineRTX Phase 12 SuperPower 施工单（Wave 12.3）：13 项系统 DLL 净空与核心界面解耦

- **施工单编号**：`CINERTX-P12-WORKORDER-005`  
- **归档路径**：`docs/phase12/05_CineRTX Phase 12 SuperPower 施工单_13项系统DLL净空与核心界面解耦.md`  
- **承接角色**：Windows 系统底层 ABI、链接器导入表与图形解耦执行工程师（External Execution Engineer）  
- **签发主管**：多媒体系统工程与 GPU 异构计算首席领域专家组 / 项目总协调负责人  
- **前置基线**：Wave 12.2 封板基线（`AuditTypes` 合法具名类型 ≤ 35 个，`AuditSloc` Pure SLOC ≤ 23,000 行，`/WX` 0 警告，单测 100% PASS）  
- **依据母本**：[`02_CineRTX Phase 12 极限现代 C++26 重构与无头 DLL 净空终审裁决书.md`](./02_CineRTX%20Phase%2012%20极限现代%20C++26%20重构与无头%20DLL%20净空终审裁决书.md)（`CINERTX-P12-RULING-002` §三）  
- **建议工期**：2 个有效工作日  
- **核心目标**：对 `{shell32.dll, dwmapi.dll, gdi32.dll, user32.dll, comdlg32.dll, comctl32.dll, uxtheme.dll, d3d12.dll, dxgi.dll, bcrypt.dll, ole32.dll, kernel32.dll, kernelbase.dll}` 13 项系统 DLL 实施彻底剥离与核心/界面物理分层解耦；升级 `cmake/AuditImports.cmake` 执行双形态静态导入表（IAT）强拦截，实现**核心无头引擎态静态 IAT \= 2 项（`kernel32.dll`、`bcrypt.dll`）**，**Win32 GUI 混合发行态静态 IAT ≤ 4 项（`kernel32.dll`、`user32.dll`、`gdi32.dll`、`bcrypt.dll`）**。

---

## 一、 施工军规与系统加载安全红线

1. **禁止隐式链接污染铁律**：全仓任何 `.cpp` / `.h` 文件内**严禁出现 `#pragma comment(lib, "...")`**（彻底清零 `comctl32.lib`、`uxtheme.lib`、`dwmapi.lib`、`shell32.lib`、`comdlg32.lib`、`ole32.lib`、`d3d12.lib`、`dxgi.lib` 等内联链接指令）。所有库依赖仅允许在 `CMakeLists.txt` 中受控声明。  
2. **强制系统目录限定加载（DLL 劫持免疫）**：所有动态延迟解析的系统 DLL 必须严格调用 `LoadLibraryExW(L"<name>.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)`，**严禁调用不带路径限定的 `LoadLibraryW` / `LoadLibraryA`**，防止当前工作目录恶意 DLL 劫持。  
3. **无头降级零崩溃不变量**：在 Windows Server Core、Docker 容器或无显示器 Session 0 环境下执行无头转码/CLI 任务时，即便 `d3d12.dll`、`dxgi.dll`、`dwmapi.dll`、`uxtheme.dll` 或 `ole32.dll` 不可用，核心引擎仍须正常初始化并完成 CPU/CUDA 路径或优雅返回 `std::unexpected(Error::Unsupported)`，绝不触发系统缺库弹窗或进程崩溃。  
4. **物理净空优先于延迟加载**：凡可通过现有轻量化设施完全替代的 DLL（`comdlg32.dll`、`comctl32.dll`、`shell32.dll`、`kernelbase.dll`），必须从源码与构建脚本中**彻底物理删除**，不保留死代码延迟加载壳。

---

## 二、 核心外科手术任务（Tasks W12.3-1 至 W12.3-5）

### 任务 W12.3-1：物理分离 `rtx_core` 无头内核与 `rtx_gui` 桌面宿主边界

**涉及文件**：`CMakeLists.txt`、`include/rtx/*`、`src/core/*`、`src/cli/main.cpp`

1. **清除核心层 Win32 图形/壳层头文件污染**：  
   - 核查并移除 `src/core/` 与 `src/io/` 中任何包含 `<windowsx.h>`、`<commctrl.h>`、`<commdlg.h>`、`<dwmapi.h>`、`<shellapi.h>`、`<uxtheme.h>` 或直接调用 `MessageBoxW`、`SendMessageW`、`PostMessageW` 的代码。  
   - 通知事件统一通过 C++23 `std::move_only_function` 回调或 `AtomicTelemetryBus` 无锁原子槽向外发布，由 GUI 宿主自行订阅。  
2. **构建目标正交拆分**：  
   - 在 `CMakeLists.txt` 中确保 `rtx_core` 与纯无头测试/CLI 目标（如 `rtx_headless_cli` / `rtx_tests` 无头核心集）**仅链接 `kernel32.lib` 与 `bcrypt.lib`**（以及静态 FFmpeg 所需基础内核符号），严禁链接 `user32`、`gdi32`、`ole32`、`shell32` 等桌面库。  
   - 确认链接命令中显式排除 `kernelbase.lib` 直接引用，统一走标准 `kernel32.lib` umbrella 导入库，消除 `kernelbase.dll` 静态 IAT 条目。

---

### 任务 W12.3-2：`d3d12.dll` 与 `dxgi.dll` 移出静态导入表并落地受控延迟解析

**涉及文件**：`CMakeLists.txt`、`src/shim/ngx_bridge/d3d12_interop.h/.cpp`、`src/gui/preview_viewport.h/.cpp`、`src/core/vram_scheduler.cpp`

1. **移除 `d3d12.lib` 与 `dxgi.lib` 静态链接**：  
   - 从 `CMakeLists.txt` 所有目标的 `target_link_libraries` 中彻底删除 `d3d12`、`dxgi` 与 `d3dcompiler`。  
2. **建立单一延迟符号解析器**：  
   - 利用 `rtx::UniqueModule`（`LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)`）按需加载 `dxgi.dll` 与 `d3d12.dll`，仅解析实际使用的最小符号集：  
     - `dxgi.dll`：`CreateDXGIFactory1`、`CreateDXGIFactory2`；  
     - `d3d12.dll`：`D3D12CreateDevice`、`D3D12SerializeRootSignature`（仅在需要动态编译根签名时）。  
   - `VramAdaptiveScheduler` 查询 `IDXGIAdapter3::QueryVideoMemoryInfo` 时，若当前无头环境无法创建 DXGI 工厂，优雅回退至 `cuMemGetInfo` 显存统计路径，保持调度器 100% 正常工作。

---

### 任务 W12.3-3：`ole32.dll` 动态隔离与 WASAPI / 文件对话框按需初始化

**涉及文件**：`CMakeLists.txt`、`src/core/wasapi_audio_output.cpp`、`src/gui/rtx_gui.cpp`

1. **移除 `ole32.lib` 静态链接**：  
   - 从 `CMakeLists.txt` 删除 `ole32` 静态依赖。  
2. **动态解析 `ole32.dll` 最小原语**：  
   - 在 `wasapi_audio_output.cpp` 与交互式文件对话框触发点，按需加载 `ole32.dll`（`LOAD_LIBRARY_SEARCH_SYSTEM32`）并解析 `CoInitializeEx`、`CoUninitialize`、`CoCreateInstance` 与 `CoTaskMemFree`。  
   - 封装基于 `UniqueModule` 与 `FnDeleter` 的 COM 套间作用域哨兵，确保音频播放或文件对话框退出后零句柄与套间状态泄漏；纯批处理转码全程零加载 `ole32.dll`。

---

### 任务 W12.3-4：彻底物理拔除 `comdlg32`、`comctl32`、`shell32` 并可选化 `dwmapi` / `uxtheme`

**涉及文件**：`src/gui/rtx_gui.cpp`、`src/gui/workstation_canvas.cpp`、`CMakeLists.txt`

1. **删除 `#pragma comment(lib, ...)` 与清单强制耦合**：  
   - 物理删除 `rtx_gui.cpp` 顶部的 `#pragma comment(lib, "comctl32.lib")`、`#pragma comment(lib, "uxtheme.lib")`、`#pragma comment(lib, "dwmapi.lib")` 以及依赖 `comctl32.dll` v6 的清单链接指令。  
2. **替换传统壳层与通用控件（彻底净空 `comdlg32`、`comctl32`、`shell32`）**：  
   - **替代 `comdlg32.dll`**：删除所有 `GetOpenFileNameW` / `GetSaveFileNameW` 调用，统一改用基于动态 `ole32.dll` 的现代轻量 COM `IFileOpenDialog` / `IFileSaveDialog`（由 `rtx::ComPtr` 管理），在无桌面壳层环境下返回 `Error::Unsupported`。  
   - **替代 `comctl32.dll`**：移除 `InitCommonControlsEx` 及通用控件依赖，进度条与队列列表全面采用已有原生 `user32` 子窗口与 `gdi32` 双缓冲自绘（或直接引导至 WebUI v3 完整工作站界面）。  
   - **替代 `shell32.dll`**：移除 `<shellapi.h>` 静态导入；文件拖拽若保留，通过 `GetModuleHandleW(L"shell32.dll")` / `LoadLibraryExW` 动态解析 `DragAcceptFiles`、`DragQueryFileW`、`DragFinish` 三函数，静态 IAT 零出现。  
3. **`dwmapi.dll` 与 `uxtheme.dll` 可选增强探测**：  
   - 暗色标题栏（`DwmSetWindowAttribute` `DWMWA_USE_IMMERSIVE_DARK_MODE`）与滚动条主题（`SetWindowTheme`）改为 `LoadLibraryExW(..., LOAD_LIBRARY_SEARCH_SYSTEM32)` 可选探测，缺失时静默跳过，绝不写入静态 IAT。

---

### 任务 W12.3-5：升级 `cmake/AuditImports.cmake` 执行双形态 IAT 硬拦截

**涉及文件**：`cmake/AuditImports.cmake`

1. **黑名单零容忍扫描**：  
   - 在 `cmake/AuditImports.cmake` 中通过 `dumpbin /imports`（或纯 CMake/Python PE Import Directory 解析器）扫描目标二进制的静态导入表（IAT）。  
   - 将以下 11 项系统 DLL 列入**全局静态 IAT 零容忍黑名单**： `shell32.dll`、`dwmapi.dll`、`comdlg32.dll`、`comctl32.dll`、`uxtheme.dll`、`d3d12.dll`、`dxgi.dll`、`ole32.dll`、`kernelbase.dll`、`advapi32.dll`、`api-ms-win-*`。  
2. **双形态白名单硬校验**：  
   - **无头引擎核心目标**：静态 IAT 集合必须严格等于 `{kernel32.dll, bcrypt.dll}`（**总计 2 项**）；  
   - **Win32 GUI 混合目标（`CineRTX.exe`）**：静态 IAT 集合必须为 `{kernel32.dll, user32.dll, gdi32.dll, bcrypt.dll}` 的子集（**总计 ≤ 4 项**），且仍严禁任何 FFmpeg / NVIDIA / MSVC 动态伴生 DLL。

---

## 三、 Wave 12.3 自动化验收门禁清单（Hard Gates）

外部执行工程师交付前，必须在本地宿主机完成以下全部门禁验证并落盘实证日志：

- [ ] **Gate 12.3-1（双形态静态 IAT 实证）**：  
  - `dumpbin /imports`（及 `cmake -P cmake/AuditImports.cmake`）输出确认：  
    - 无头核心模块静态导入 DLL **仅 2 项**（`kernel32.dll`、`bcrypt.dll`）；  
    - `CineRTX.exe` 混合发行版静态导入 DLL **≤ 4 项**（`kernel32.dll`、`user32.dll`、`gdi32.dll`、`bcrypt.dll`）；  
    - 黑名单 11 项 DLL 静态导入命中数为 **0**。  
- [ ] **Gate 12.3-2（源码零隐式链接扫描）**：  
  - `rg "#pragma\s+comment\s*\(\s*lib" include/ src/` → **0 命中**；  
  - `rg "\bLoadLibrary[AW]\b" include/ src/` → **0 命中**（全部使用 `LoadLibraryExW` \+ `LOAD_LIBRARY_SEARCH_SYSTEM32`）。  
- [ ] **Gate 12.3-3（无头环境与桌面环境双模启动验证）**：  
  - 在无显卡/无头测试台架下执行纯 CPU/CLI 探测与参数校验，进程冷启动零缺失 DLL 错误、零弹窗、正常退出；  
  - 在交互式桌面（WTS Session \> 0）启动 `CineRTX.exe`，验证文件选择对话框、暗色标题栏、DirectComposition 预览与 WASAPI 音频播放全部按需动态加载并流畅运行。  
- [ ] **Gate 12.3-4（构建、类型、体量与单测全绿）**：  
  - MSVC `/WX` **0 错误 0 警告**；`AuditTypes`（≤ 35）、`AuditSloc`（≤ 23,000）、`AuditSpdx` 全绿；`rtx_tests.exe` **100% PASS**。