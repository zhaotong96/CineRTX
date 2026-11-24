# CineRTX Phase 12 Wave 12.1 终局收敛施工汇报：GUI 上下文显式传参与 ABI 错误码修正

- **文档编号**：`CINERTX-P12-REPORT-017`
- **归档路径**：`docs/phase12/17_CineRTX Phase 12 Wave 12.1 终局收敛施工汇报_GUI上下文显式传参与ABI错误码修正.md`
- **依据指令**：[`16_CineRTX Phase 12 Wave 12.1 GUI上下文传递与ABI错误码终审裁决书.md`](./16_CineRTX%20Phase%2012%20Wave%2012.1%20GUI上下文传递与ABI错误码终审裁决书.md)（`CINERTX-P12-RULING-016`）
- **交付口径声明（严守诚实性红线）**：当前执行环境为无 Windows SDK / MSVC / CUDA / DirectX 12 的 Linux 沙箱，**本次交付仅提交 4 个文件的精准源码补丁与静态/作用域/契约镜像审计，Windows 宿主机 85/85 MSVC `/W4 /WX` 编译、四重 CMake 审计、双目录 146/146 `rtx_tests.exe` 及 WTS 活跃桌面验活留待宿主机实测放行**。

## 一、 16 号终审裁决逐项修正清单（4 个更新文件）

| 裁决议题 | 变更文件（仓库相对路径） | 精准修正内容 |
| :--- | :--- | :--- |
| **议题一：ABI 错误码收敛** | [`src/shim/loader/nv_encoder.cpp`](../../src/shim/loader/nv_encoder.cpp) | 将 L542 未注册枚举 `RTX_ERR_STATE` 修正为 `include/shim/rtx_shim_abi.h` 中既有的 `RTX_ERR_INTERNAL`；全文件 `RTX_ERR_*` 枚举经白名单扫描 100% 合法，未触碰任何 C-ABI 头文件。 |
| **议题二：前置声明与显式引用签名** | [`src/gui/workstation_canvas.h`](../../src/gui/workstation_canvas.h) | 在 `namespace rtx::gui` 顶部补齐 `struct WorkstationApp;` 前置声明；将 `guiProfile`（`const` / 非 `const` 双重载）、`setActiveWorkspace`、`playbackCaption`、`installTranscodeBorrow`、`stopPlaybackSession` 全部规范化为显式非空引用 `WorkstationApp&` / `const WorkstationApp&`；补充 `getWindowApp(HWND)` 入口提取辅助函数。 |
| **议题二：消除畸形函数头与悬垂 `hwnd`** | [`src/gui/workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp) | 1. 彻底修复 L962 畸形函数头为 `void stopPlaybackSession(WorkstationApp& app) noexcept`；<br>2. 将 `effectiveTimelineSpan`、`timelineXToFlicks`、`timelineFlicksToX`、`playbackCaption`、`guiProfile`、`setActiveWorkspace`、`installTranscodeBorrow` 全部改造为显式接收 `WorkstationApp&` / `const WorkstationApp&` 并同步更新全部内部调用点（`timelineCanvasProc`、`paintTimelineCanvas`、`updateNleTimecode`、`collectProfileFromUi`、`applyProfileToUi`、`applyGoldenPresetById`、`paintCommandButton`、`createWorkstationControls`）；<br>3. 创建时间轴子窗口 `kTimelineCanvasClass` 时传入 `&requireApp(hwnd)` 至 `lpCreateParams`，确保 `WM_NCCREATE` 立即绑定 `GWLP_USERDATA`；移除 L1065 冗余 `(void)hwnd;`。 |
| **议题二：同步 GUI 宿主调用点** | [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) | 将 L262 `setActiveWorkspace(mode)` 更新为 `setActiveWorkspace(rtx::gui::requireApp(hwnd), mode)`；将 `wWinMain` L957 收口为 `rtx::gui::setGuiPlaying(*app_owner, false)`。 |

## 二、 沙箱静态与作用域审计记录（非宿主机替代证明）

1. **工作区零垃圾保障**：本次替换的 4 个旧文件已移出 `CineRTX/` 源码树至外部归档目录（`work/_CineRTX_bak_archive/`），源码树内 `.bak` / `.orig` / `.tmp` 残留为 **0**。
2. **`AuditTypes.cmake` 逐行规则镜像**：4 个文件内 5 个具名类型全部通过 `@rtx-type-justification` 校验，**0 违规**。
3. **ABI 错误码 / 负向正则 / 作用域核验**：`nv_encoder.cpp` 全部 `RTX_ERR_*` 100% 属于 `rtx_shim_abi.h` 既有枚举；`std::getenv|_dupenv_s|GetEnvironmentVariable`、`\bg_app\b`、`src/gui/` 裸 GDI 创建/释放均为 **0 命中**；无畸形函数头或悬垂 `hwnd` 引用。

## 三、 Windows 宿主机 Wave 12.1 终局验收命令

请在 Windows 构建机同步后执行以下完整收网验证，绿灯后即可正式签署 Wave 12.1 封板并解封 Wave 12.2：

```powershell
cmake --preset windows-msvc-ninja
cmake --build --preset windows-msvc-ninja
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSpdx.cmake
cmake -P cmake/AuditSloc.cmake
cmake -P cmake/AuditImports.cmake
rg "std::getenv|_dupenv_s|GetEnvironmentVariable" include/ src/ tests/
rg "\bg_app\b" include/ src/
rg "CreateSolidBrush|DeleteObject" src/gui/
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location
```

随后请在 WTS Active Console Session（`SessionId > 0`）启动 `CineRTX.exe` 完成窗口交互、时间轴拖拽、播放启停及正常退出的实呈验活。
