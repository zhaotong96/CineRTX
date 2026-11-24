# CineRTX Phase 12 Wave 12.1 最终收敛施工汇报：外部 SDK 隔离与环境变量清零

- **文档编号**：`CINERTX-P12-REPORT-014`
- **归档路径**：`docs/phase12/14_CineRTX Phase 12 Wave 12.1 最终收敛施工汇报_外部SDK隔离与环境变量清零.md`
- **依据指令**：[`13_CineRTX Phase 12 Wave 12.1 最终收敛与第三方SDK头文件告警隔离终审裁决书.md`](./13_CineRTX%20Phase%2012%20Wave%2012.1%20最终收敛与第三方SDK头文件告警隔离终审裁决书.md)（`CINERTX-P12-RULING-013`）
- **交付口径声明（严守 10 号与 13 号裁决书诚实性红线）**：本执行环境为无 Windows SDK / MSVC / CUDA / DirectX 12 的 Linux 沙箱，**本次交付仅提交源码最终收敛补丁与静态/契约镜像审计，Windows 宿主机 85/85 MSVC `/W4 /WX` 全量构建、四重 CMake 审计、双目录 146/146 `rtx_tests.exe` 及 WTS 活跃桌面验活均待宿主机实测放行**。

## 一、 13 号裁决书逐项收敛落盘清单（6 个更新文件）

| 裁决议题 | 变更文件（仓库相对路径） | 精准外科手术内容 |
| :--- | :--- | :--- |
| **议题一：第三方 SDK `SYSTEM` 隔离铁律** | [`CMakeLists.txt`](../../CMakeLists.txt) | 1. 第 506 行升级为 `target_include_directories(rtx_ngx_bridge SYSTEM PRIVATE "${RTX_NGX_INCLUDE_DIR}")`；<br>2. 普查全仓外部包含目录，将 `rtx_shim_static`（L180）与 `rtx_ngx_bridge`（L406）中的 `"${RTX_NVCODEC_INCLUDE_DIR}"` 一并迁入 `SYSTEM PRIVATE`，确保全部外部 SDK（nv-codec-headers、CUDA、TensorRT、RTX Video SDK；`cmake/RtxFfmpegStatic.cmake` 已带 `SYSTEM INTERFACE`）均走 `/external:I` + `/external:W0`，同时所有项目自有目录（`include/`、`src/`、`${RTX_GENERATED_DIR}`）保持非 `SYSTEM` 以接受 `/W4 /WX` 最高级别审查。 |
| **议题二：拔除 `RTX_NGX_BRIDGE_PATH` 旁路** | [`src/core/ngx_remaster.cpp`](../../src/core/ngx_remaster.cpp) | 物理删除第 316–318 行 `std::getenv("RTX_NGX_BRIDGE_PATH")` 分支及冗余 `#include <cstdlib>`，动态桥接探测严格收敛至 `<exe_dir>/rtx_ngx_bridge.dll` 与 `<exe_dir>/plugins/nvidia_rtx/rtx_ngx_bridge.dll` 确定性部署契约。 |
| **议题二：全仓 `getenv` / `GetEnvironmentVariable` 扫尾** | [`src/shim/ngx_bridge/d3d12_interop.cpp`](../../src/shim/ngx_bridge/d3d12_interop.cpp) | 物理删除 `plugin_directory()` 内 `GetEnvironmentVariableW(L"RTX_NGX_PLUGIN_DIR", ...)` 覆盖分支，统一走 `GetModuleHandleExW` / `GetModuleFileNameW` 确定性模块相对路径探测。 |
| **议题二：全仓 `GetEnvironmentVariable` 扫尾** | [`src/shim/ngx_bridge/ngx_d3d12_bridge.cpp`](../../src/shim/ngx_bridge/ngx_d3d12_bridge.cpp) | 将 `makeAppDataDirectory()` 内两处 `GetEnvironmentVariableW(L"LOCALAPPDATA" / L"TEMP", ...)` 替换为标准 `kernel32.dll` 原语 `GetTempPathW`（保留 `fallback` 目录回退），消除环境变量直接读取且零新增 DLL 依赖。 |
| **议题二：全仓 `GetEnvironmentVariable` 扫尾** | [`src/shim/ngx_bridge/trt_loader.cpp`](../../src/shim/ngx_bridge/trt_loader.cpp) | 物理删除 `getCandidateDirectories()` 内 `RTX_TENSORRT_ROOT` / `TENSORRT_DIR` / `TRT_ROOT` 的 `GetEnvironmentVariableW` 遍历，保留基于 `getModuleDirectory()` 的确定性相对路径探测（并补充 `.vs/build/external/tensorrt/TensorRT-10.13.3.9/lib` 相对路径）。 |
| **议题二：单测 `getenv` 扫尾** | [`tests/test_rife.cpp`](../../tests/test_rife.cpp) | 物理删除 `setTestEnvVar`、`unsetTestEnvVar`、`readEnvValue` 及两处针对已移除开关 `CINERTX_ALLOW_BUILTIN_RIFE_SYNTH` 的读写/恢复样板代码；两项 GPU 回归测试直接经显式 `"__builtin__"` 参数构造独立隔离的 `rife_builtin_synth.engine` 夹具运行。 |

## 二、 沙箱静态与合规核验记录（非宿主机替代证明）

1. **工作区零垃圾保障**：本次替换的 6 个旧文件均已移出 `CineRTX/` 源码树至外部归档目录（`work/_CineRTX_bak_archive/`），源码树内 `.bak` / `.orig` / `.tmp` 残留严格为 **0**。
2. **环境变量清零扫描**：全部 6 个更新文件内 `std::getenv|_dupenv_s|GetEnvironmentVariable|_putenv|::setenv|::unsetenv` 命中 **0**。
3. **`AuditTypes.cmake` 镜像与 SPDX / 脱敏门禁**：4 个 `src/` 修改文件通过 `@rtx-type-justification` 逐行规则校验（0 违规）；`src/shim/` 全部 `MIT OR Apache-2.0`，其余 `GPL-3.0-or-later WITH Plugin-Exception`；无外部网盘或本地路径泄漏。

## 三、 Windows 宿主机 Wave 12.1 最终收网验收命令

请在 Windows 构建机同步后依次执行 13 号裁决书 §三 规定的全部 4 步收网核验，全部绿灯即可直接封板 Wave 12.1 并解封 Wave 12.2：

```powershell
# 1. 全仓环境变量零残留扫描（必须 0 命中）
rg "std::getenv|_dupenv_s|GetEnvironmentVariable" include/ src/ tests/

# 2. Windows 宿主机 MSVC /W4 /WX 全量构建（85/85 必须 0 Error(s), 0 Warning(s)）
cmake --preset windows-msvc-ninja
cmake --build --preset windows-msvc-ninja

# 3. 四重静态审计与双目录 146/146 单元测试
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSpdx.cmake
cmake -P cmake/AuditSloc.cmake
cmake -P cmake/AuditImports.cmake
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location
```

完成后请在 WTS Active Console Session（`SessionId > 0`）启动 `CineRTX.exe` 完成窗口创建、拖动、时间轴交互与退出的实呈验活。
