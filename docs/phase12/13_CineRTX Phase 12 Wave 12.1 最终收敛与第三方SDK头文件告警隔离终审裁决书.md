# CineRTX Phase 12 Wave 12.1 最终收敛与第三方 SDK 头文件隔离终审裁决书

- **文档编号**：`CINERTX-P12-RULING-013`  
- **归档路径**：`docs/phase12/13_CineRTX Phase 12 Wave 12.1 最终收敛与第三方SDK头文件告警隔离终审裁决书.md`  
- **审验标的**：[`12_CineRTX Phase 12 Wave 12.1 最终收敛与第三方SDK头文件告警隔离专家咨询委托书.md`](./12_CineRTX%20Phase%2012%20Wave%2012.1%20最终收敛与第三方SDK头文件告警隔离专家咨询委托书.md)（`CINERTX-P12-DELEGATION-012`）  
- **签发主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
- **审验日期**：2026-11-22  
- **终审裁决**：**全面批准两项精准外科手术收口方案（APPROVED FOR IMMEDIATE WAVE 12.1 FINAL CLOSURE）**——正式核准 Wave 12.1 前序 85% 纠偏物证（工作区 11 项 `.bak` 清零、`AuditTypes` 155/155 全绿、自然内存重排消除 `C4324`、形参匿名化消除 `C4100`）；对最后两处 MSVC `/WX` 编译阻断下达直通修复指令，并锁定全仓第三方头文件 `SYSTEM` 隔离铁律与 `std::getenv` 零容忍清剿闭环。

---

## 执行摘要（Executive Ruling）

首席领域专家组审阅了 `12 号咨询委托书` 所呈报的 MSVC 19.51+ 构建 logs 与源码定位，确认 Wave 12.1 已完成绝大多数核心卫生治理，剩余两处构建失败属于极其清晰的构建目录属性遗漏与局部环境变量清理漏网之鱼。专家组签署如下终审裁决：

1. **议题一裁决（全面批准 `SYSTEM` 外部头文件隔离规范）**：**立即核准**将 `CMakeLists.txt` 第 506 行升级为 `target_include_directories(rtx_ngx_bridge SYSTEM PRIVATE "${RTX_NGX_INCLUDE_DIR}")`，并将\*\*“所有外部第三方库/专有 SDK 包含路径一律强制声明 `SYSTEM` 属性”\*\*确立为全仓构建铁律（确保 CMake 在 MSVC 下自动生成 `/external:I` 配合 `/external:W0`，将外部不可控头文件的警告隔离在项目 `/W4 /WX` 门禁之外，同时对项目自身 `include/` 与 `src/` 保持 100% 零容忍高压审查）。  
2. **议题二裁决（立即物理拔除 `RTX_NGX_BRIDGE_PATH` 环境变量旁路，并执行全仓 `getenv` 一键扫尾）**：**立即核准**物理删除 `src/core/ngx_remaster.cpp` 第 316-318 行的 `std::getenv("RTX_NGX_BRIDGE_PATH")` 分支，将动态桥接探测严格收敛至确定性的部署相对路径契约（当前可执行文件同级目录与 `plugins/nvidia_rtx/` 子目录）；同步在 Wave 12.1 收网前执行一次全仓（`include/`、`src/`、`tests/`）`getenv` 彻底清剿，不将任何隐式环境变量遗留至后续波次。  
3. **Wave 12.1 封板授权**：协调负责人合入上述两项修复并在 Windows 宿主机实证 `cmake --build --preset windows-msvc-ninja` 0 错误 0 警告、五重静态审计全绿、双目录 146/146 单测 PASS 及桌面验活通过后，**可直接签署 Wave 12.1 终审通过并立即解封派发 Wave 12.2（`04号施工单`）**，无需再次呈报中间委托。

---

## 一、 议题一终审裁决：第三方专有 SDK 头文件的 CMake `SYSTEM` 隔离铁律

### 1.1 物理根因与架构定位

在 MSVC 19.51+（VS 2026 Toolset）中，当全局启用 `/W4 /WX` 时，普通 `target_include_directories(... PRIVATE ...)` 会展开为 `-I<dir>`，导致编译器以与项目自研源码同等的严苛级别审查第三方 SDK 头文件中的内联函数（如 NVIDIA RTX Video SDK `nvsdk_ngx_helpers_truehdr.h` 与 `nvsdk_ngx_helpers_vsr.h` 中未使用的参数）。在 CMake 3.25+ 中，为外部依赖声明 `SYSTEM` 属性会自动向 MSVC 传递 `/external:I <dir>`（默认等价于 `/external:W0`），既不削弱对项目自身代码的 `/W4 /WX` 洁癖审查，又彻底隔绝上游闭源 SDK 的历史代码噪声。

### 1.2 强制落盘改造规范

1. **立即修复 `CMakeLists.txt` 第 506 行**：

```
target_include_directories(rtx_ngx_bridge SYSTEM PRIVATE "${RTX_NGX_INCLUDE_DIR}")
```

2. **全仓第三方包含目录 `SYSTEM` 属性普查**：  
   - 检查 `CMakeLists.txt` 及 `cmake/RtxFfmpegStatic.cmake` 中所有指向外部/第三方头文件目录（`third_party/`、`.vs/build/external/`、CUDA、TensorRT、nv-codec-headers、RTX Video SDK、FFmpeg）的 `target_include_directories` 调用，确保 **100% 带有 `SYSTEM` 关键字**；  
   - 严禁对项目自有目录（`${CMAKE_CURRENT_SOURCE_DIR}/include`、`${CMAKE_CURRENT_SOURCE_DIR}/src`、`${CMAKE_BINARY_DIR}/generated`）添加 `SYSTEM` 属性，保证项目自研代码及反射生成代码始终受 `/W4 /WX` 最高级别严格检验。

---

## 二、 议题二终审裁决：物理拔除 `RTX_NGX_BRIDGE_PATH` 环境变量及全仓 `getenv` 清零

### 2.1 `src/core/ngx_remaster.cpp` 精确切除指令

环境变量 `RTX_NGX_BRIDGE_PATH` 属于典型的“增加使用者与排障心智负担的隐藏开关”，在受控部署架构下不仅破坏确定性路径查找，还带来非受控 DLL 侧载风险。

- **执行动作**：打开 `src/core/ngx_remaster.cpp`（约第 315-320 行），**彻底物理删除** `std::getenv("RTX_NGX_BRIDGE_PATH")` 整个分支（同步检查文件顶部是否可移除冗余 `#include <cstdlib>`）：

```c
// 物理删除以下隐藏环境变量旁路，仅保留确定性的相对部署路径契约：
// - <exe_dir>/rtx_ngx_bridge.dll
// - <exe_dir>/plugins/nvidia_rtx/rtx_ngx_bridge.dll
```

### 2.2 全仓（含 `d3d12_interop.cpp` 与 `tests/`）`getenv` 扫尾裁决

针对委托书提及的 `d3d12_interop.cpp` 与 `tests/test_rife.cpp` 中偶发的环境变量读取，专家组裁决**在 Wave 12.1 收网阶段一并彻底清零，绝不留尾欠**：

1. **`src/shim/ngx_bridge/d3d12_interop.cpp`**：物理删除任何 `std::getenv` / `_dupenv_s` / `GetEnvironmentVariable*` 调试开关或覆盖路径分支，所有行为 100% 由 `RtxNgxBridgeConfig` 与 `TranscodeProfile` 显式结构参数控制；  
2. **`tests/test_rife.cpp`（及全仓 `tests/`）**：若单测中通过 `std::getenv` 读取引擎模型路径，统一改为通过标准 CLI 参数（`--rife-engine <path>`）或基于 `GetModuleFileNameW` / 仓库相对路径探针的确定性夹具发现机制，确保测试行为完全可复现、不依赖隐式宿主环境变量。

---

## 三、 Wave 12.1 最终收网与直通放行检查清单

协调负责人合入上述两点修复后，按以下顺序执行最终收网核验：

1. **执行全仓 `getenv` 零残留扫描**：

```
rg "std::getenv|_dupenv_s|GetEnvironmentVariable" include/ src/ tests/
# 必须返回 0 命中
```

2. **执行 Windows 宿主机 MSVC `/W4 /WX` 全量构建**：

```
cmake --preset windows-msvc-ninja
cmake --build --preset windows-msvc-ninja
# 85/85 全部目标必须 0 Error(s), 0 Warning(s) 通过
```

3. **执行四重静态审计与双目录全量单测**：

```
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSpdx.cmake
cmake -P cmake/AuditSloc.cmake
cmake -P cmake/AuditImports.cmake
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location
# 双向运行 rtx_tests.exe 均必须 146/146 PASS
```

4. **交互式桌面实呈验活（WTS Session \> 0）**：  
   - 在活跃控制台桌面启动 `CineRTX.exe`，验证窗口创建、拖动、时间轴操作与正常退出无崩溃、无 UAF。

>   
> **直通放行授权**：上述 4 步全部实证绿灯后，协调负责人即可直接归档 Wave 12.1 验收封板记录，并**立即解封派发 Wave 12.2（`04_CineRTX Phase 12 SuperPower 施工单_自定义类型清剿与C++26标准库替换.md`）**，全速推进自定义类型清剿（155 → ≤ 35）与 C++26 标准库提纯！