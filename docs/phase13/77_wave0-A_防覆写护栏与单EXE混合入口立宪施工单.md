# **77\_wave0-A\_基线回退0beda87\_防覆写护栏与单EXE混合入口立宪施工单**

*Superpowers 原子施工规约 · 严格依据 CineRTX Phase 13 76 号终审裁决书演进*

---

## **1\. 施工元数据与唯一基线**

本施工单为 CineRTX Phase 13 架构收敛工程的底层净化指令，由 Superpowers 引擎自动生成并执行。

### **1.1 施工元数据**

* **施工波次**：Wave 0 \- Stage A（前置止损、防覆写护栏与单 EXE 混合入口立宪，与 Wave 0-B 共同组成 Wave 0 基线净化阶段）  
* **前置依赖**：`76_phase13_74号返工终审验收报告与单EXE极限Modern C++重构演进方案.md`  
* **目标交付物**：回退并固化的 `0beda87` 代码基线、机械化防覆写 CMake 门禁守卫、单 EXE `/SUBSYSTEM:WINDOWS` 控制台安全绑定机制。

### **1.2 强制回退基线指标**

工作区必须严格对齐 Commit `0beda87`，构建与审计指标如下表所示：

| 评估维度 | 指标约束值 | 验证命令/状态 |
| :---- | :---- | :---- |
| **编译质量** | MSVC `/W4 /WX` 零错误、零警告 | `cmake --build build --config Release` |
| **单元测试** | `rtx_tests.exe` 157/157 PASS | `./build/bin/Release/rtx_tests.exe` |
| **隐私审计** | `AuditPrivacy` 0 违规 | `cmake -P cmake/AuditPrivacy.cmake` |
| **许可审计** | `AuditSpdx` PASS | `cmake -P cmake/AuditSpdx.cmake` |
| **代码规模** | Core SLOC \= 12,496 | `cloc src/core include/rtx` |

### **1.3 宪法级硬红线**

1. **严禁整文件覆盖（No Full-File Overwrite Rule）**  
   自本裁决起，工程严禁接收任何全量文件覆盖式 Patch。所有代码修改必须基于当前封板基线（Commit `0beda87`）生成带准确上下文的最小函数级 Unified Diff。违者自动阻断 CI 流程。  
2. **单 EXE 铁律（Single Executable Architecture Rule）**  
   彻底废除保留多 EXE 的历史条款，物理拔除 `rtx_headless_cli.exe` 目标。全项目最终产物彻底收敛为单一 `/SUBSYSTEM:WINDOWS` 可执行文件 `CineRTX.exe`。除开发期测试宿主 `rtx_tests.exe` 外，最终交付目录仅允许包含 `CineRTX.exe`、`rtx_ngx_bridge.dll` 及必备引擎权重文件。  
3. **零新增具名类型（Zero Named Types Constraint）**  
   入口控制台绑定与句柄恢复逻辑必须严格以无状态自由函数（Free Functions）或局部内部 Lambda 表达式实现，严禁创建 `ConsoleGuard`、`CliHost`、`ConsoleManager` 等无意义的抽象包装类。

---

## **2\. Task 1：植入防覆写机械化三道护栏**

> **注**：基线回退至 `0beda87`（含 `consumeNvofSceneCut` 与 `isPreviewEnabled()` 完整保留）及 `73_` / `74_` `AuditPrivacy` 违规路径清零已由协调负责人完成，工作区现已处于干净 `0beda87` 状态，执行工程师直接从本节护栏植入开始施工。

### **Step 1.1：植入防覆写机械化三道护栏**

为彻底杜绝后续 Patch 静默破坏关键契约，在 CMake 机制中引入硬性校验。

#### **护栏 1：受保护契约符号守卫（Contract Symbol Guard）**

在 `cmake/ContractGuard.cmake` 中增加源码正则表达式扫描，断言核心接口的声明与调用存在性：

```
# cmake/ContractGuard.cmake
file(READ "${CMAKE_SOURCE_DIR}/include/rtx/ngx_remaster.h" HEADER_CONTENT)
if(NOT HEADER_CONTENT MATCHES "consumeNvofSceneCut")
    message(FATAL_ERROR "[ContractGuard] CRITICAL: 'consumeNvofSceneCut' signature missing from ngx_remaster.h! Aborting build.")
endif()

file(READ "${CMAKE_SOURCE_DIR}/src/core/job_queue.cpp" SOURCE_CONTENT)
if(NOT SOURCE_CONTENT MATCHES "isPreviewEnabled")
    message(FATAL_ERROR "[ContractGuard] CRITICAL: 'isPreviewEnabled()' check missing from job_queue.cpp! Aborting build.")
endif()
```

#### **护栏 2：门禁脚本独立变更锁（Gatekeeper Lock）**

任何对 `cmake/Audit*.cmake` 或 `cmake/RtxFfmpegStatic.cmake` 的修改必须单独提交，禁止在功能 Patch 中顺带放宽审计指标。

#### **护栏 3：构建与审计强串行化 Pipeline**

修改 `build.bat` 脚本，将审计门禁作为构建的前置与后置硬依赖：

```
@echo off
setlocal enabledelayedexpansion

:: 1. 前置静态审计
cmake -P cmake/AuditPrivacy.cmake || exit /b 1
cmake -P cmake/AuditSpdx.cmake || exit /b 1
cmake -P cmake/ContractGuard.cmake || exit /b 1

:: 2. 编译构建
cmake --build build --config Release --parallel || exit /b 1

:: 3. 后置二进制与架构审计
cmake -P cmake/AuditImports.cmake || exit /b 1
cmake -P cmake/AuditTypes.cmake || exit /b 1

:: 4. 自动化测试回归
.\build\bin\Release\rtx_tests.exe || exit /b 1

echo [BUILD SUCCESS] All quality gates passed cleanly.
```

---

## **3\. Task 2：彻底拔除 `rtx_headless_cli.exe` 与 `/SUBSYSTEM:WINDOWS` 安全混合入口实现**

### **Step 2.1：物理拔除 `rtx_headless_cli` 与 Help 文本收口**

1. **清理 CMake 构建目标**：  
   在 `CMakeLists.txt` 中物理删除 `rtx_headless_cli` 可执行文件定义，并在 `PackageRelease.cmake` 中移除相关打包规则。

```
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ -120,8 +120,20 @@ add_executable(CineRTX WIN32
     src/main.cpp
+    src/cli/console_bind.cpp
 )

-add_executable(rtx_headless_cli
-    src/cli/main.cpp
-)
```

2. **收口 CLI Help 帮助文本**：  
   删除 `src/cli/main.cpp:75-116` 中重复的 `printHelp()` 实现，统一收口至 `param_schema.cpp::formatCliHelp()`。无头模式启动逻辑直接调用该标准入口。

### **Step 2.2：`bind_parent_console_if_needed()` 安全控制台绑定实现**

为了实现单一 `CineRTX.exe` 既能在 GUI 双击下零窗口闪烁，又能在命令行中正常输出日志并支持标准 I/O 重定向，采用下述 Modern Win32 无状态绑定算法。

#### **核心控制台绑定逻辑 (`src/cli/console_bind.cpp`)**

```
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <cstdio>
#include <iostream>

namespace cinertx::cli {

void bind_parent_console_if_needed() {
    // 检查标准输出与标准错误句柄的原生状态
    HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hStdErr = GetStdHandle(STD_ERROR_HANDLE);

    DWORD outType = (hStdOut != NULL && hStdOut != INVALID_HANDLE_VALUE) ? GetFileType(hStdOut) : FILE_TYPE_UNKNOWN;
    DWORD errType = (hStdErr != NULL && hStdErr != INVALID_HANDLE_VALUE) ? GetFileType(hStdErr) : FILE_TYPE_UNKNOWN;

    bool isOutRedirected = (outType == FILE_TYPE_DISK || outType == FILE_TYPE_PIPE);
    bool isErrRedirected = (errType == FILE_TYPE_DISK || errType == FILE_TYPE_PIPE);

    // 如果未发生重定向，且尝试附加到父进程控制台成功（例如从 cmd.exe 或 PowerShell 启动）
    if ((!isOutRedirected || !isErrRedirected) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        
        // 针对未重定向的 stdout 进行 CONOUT$ 绑定
        if (!isOutRedirected) {
            FILE* fpOut = nullptr;
            if (freopen_s(&fpOut, "CONOUT$", "w", stdout) == 0) {
                setvbuf(stdout, nullptr, _IONBF, 0);
            }
        } else {
            // 已重定向流：强行恢复被 AttachConsole 篡改的系统句柄，严禁使用 freopen_s
            SetStdHandle(STD_OUTPUT_HANDLE, hStdOut);
        }

        // 针对未重定向的 stderr 进行 CONOUT$ 绑定
        if (!isErrRedirected) {
            FILE* fpErr = nullptr;
            if (freopen_s(&fpErr, "CONOUT$", "w", stderr) == 0) {
                setvbuf(stderr, nullptr, _IONBF, 0);
            }
        } else {
            SetStdHandle(STD_ERROR_HANDLE, hStdErr);
        }

        // 强制终端输出采用 UTF-8 编码，解决中文乱码
        SetConsoleOutputCP(CP_UTF8);
        
        // 使 C++ std::cout / std::cerr 与 C stdout / stderr 保持同步
        std::ios::sync_with_stdio(true);
    }
}

} // namespace cinertx::cli
```

#### **系统入口点集成 (`src/main.cpp`)**

```
#include <windows.h>

namespace cinertx::cli { void bind_parent_console_if_needed(); }

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 1. 安全按需绑定控制台（若从终端调用）
    cinertx::cli::bind_parent_console_if_needed();

    // 2. 解析命令行参数
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    
    // 3. 路由至 GUI 或 Headless 模式...
    
    LocalFree(argvW);
    return 0;
}
```

#### **Windows Shell 管道与重定向行为规范**

在 `/SUBSYSTEM:WINDOWS` 模式下，不同 Shell 的输出重定向机制存在差异。运维与测试必须遵循以下调用规范：

1. **`cmd.exe` 环境**：  
   直接支持标准重定向。  
   `CineRTX.exe --help > out.txt 2>&1`  
2. **`PowerShell` 环境**：  
   由于交互式 PowerShell 处理 Win32 Windowed 子系统程序的管道机制会在后台进程返回前提前关闭管道，直接使用 `>` 可能会导致输出拦截不完整。**必须**使用以下两种命令之一：  
   * 管道输出：`.\CineRTX.exe --help | Out-File -Encoding utf8 out.txt`  
   * 显式等待：`Start-Process -FilePath ".\CineRTX.exe" -ArgumentList "--help" -Wait -RedirectStandardOutput "out.txt"`

---

## **4\. 验收 Gate (Checklist)**

施工完成后，必须逐项通过以下验收门禁方可交付：

- [x] **基线完整性**：工作区已彻底回退至 `0beda87`，且 `consumeNvofSceneCut` 与 `job_queue.cpp:L359` 预览注入守卫代码处于激活状态。  
- [x] **架构收敛**：`rtx_headless_cli.exe` 项目目标已彻底清除，交付目录仅生成单一 `CineRTX.exe`。  
- [x] **GUI 零闪烁**：在资源管理器中直接双击 `CineRTX.exe`，`GetStdHandle` 返回 `NULL`，直接跳过控制台附加，无任何黑色 CMD 窗口闪烁。  
- [x] **CLI 输出正常**：在 `cmd.exe` 下执行 `CineRTX.exe --help > out.txt`，文本完整写入，无乱码，句柄无泄漏。  
- [x] **自动化测试全过**：执行 `./rtx_tests.exe`，结果显示 `157/157 PASS`。  
- [x] **静态合规**：运行 `cmake -P cmake/AuditPrivacy.cmake` 输出 `0 violations`。

