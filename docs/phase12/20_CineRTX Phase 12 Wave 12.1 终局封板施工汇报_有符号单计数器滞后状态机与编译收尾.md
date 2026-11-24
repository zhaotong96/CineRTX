# CineRTX Phase 12 Wave 12.1 终局封板施工汇报：有符号单计数器滞后状态机与编译收尾

- **文档编号**：`CINERTX-P12-REPORT-020`
- **归档路径**：`docs/phase12/20_CineRTX Phase 12 Wave 12.1 终局封板施工汇报_有符号单计数器滞后状态机与编译收尾.md`
- **依据指令**：[`19_CineRTX Phase 12 Wave 12.1 调度器滞后状态换向与编译收尾终审裁决书.md`](./19_CineRTX%20Phase%2012%20Wave%2012.1%20调度器滞后状态换向与编译收尾终审裁决书.md)（`CINERTX-P12-RULING-019`）
- **交付口径声明（严守诚实性红线）**：本执行环境为无 Windows SDK / MSVC / CUDA / DirectX 12 的 Linux 沙箱，**本次交付提交全部 4 个文件的终局修复补丁与 `VramScheduler` C++23 滞后换向实测；Windows 宿主机 85/85 MSVC `/W4 /WX` 编译与双目录 147/147 `rtx_tests.exe` 终验留待宿主机执行放行**。

## 一、 19 号终审裁决逐项收尾清单（4 个更新文件）

| 裁决议题 | 变更文件（仓库相对路径） | 精准修正内容 |
| :--- | :--- | :--- |
| **议题一：有符号单计数器 `int32_t dwell_frames_`** | [`include/rtx/vram_scheduler.h`](../../include/rtx/vram_scheduler.h) | 严守奥卡姆剃刀原则，不新增 `PressureDirection` 枚举；将 `dwell_frames_` 改为有符号 `int32_t dwell_frames_ = 0`，`framesInCurrentTier()` 返回绝对值以保持公开接口兼容。 |
| **议题一：双向滞后与死区/换向归零** | [`src/core/vram_scheduler.cpp`](../../src/core/vram_scheduler.cpp) | 按 19 号裁决 §1.2 重构 `VramScheduler::observeFrame`：高压区（`>= 82%`）在 `PassThrough_NoFruc` 置 `0`，其余正向累积（`dwell_frames_ > 0 ? dwell_frames_ + 1 : 1`），达 `+target_dwell` 降级归零；低压区（`<= 68%`）在 `VsrFirst_RifeFull` 置 `0`，其余负向累积（`dwell_frames_ < 0 ? dwell_frames_ - 1 : -1`），达 `-target_dwell` 升档归零；死区（`68% < usage < 82%`）立即归零。 |
| **议题二：`nv_encoder.cpp(542)` 清除同名重复副本并全新落盘** | [`src/shim/loader/nv_encoder.cpp`](../../src/shim/loader/nv_encoder.cpp) | 查明云盘 `src/shim/loader/` 存在桌面端同步回写的旧同名副本与 `.bak` 残留（导致宿主机读到旧版 `RTX_ERR_STATE`）；现已将全部历史副本及 `.bak` 移出 `CineRTX/` 至外部归档目录 `work/_CineRTX_bak_archive/`，仅保留唯一一份全新落盘的 `nv_encoder.cpp`（L542 为 `RTX_ERR_INTERNAL`，`RTX_ERR_STATE` 0 命中）。 |
| **议题二：物理删除 `rtx_gui.cpp(516)` 死局部变量** | [`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp) | 严禁使用 `[[maybe_unused]]` 掩盖，直接物理删除第 516 行未引用声明 `rtx::gui::WorkstationApp& app = *app_ptr;`，保留 `if (!app_ptr) return DefWindowProcW(...);` 边界守护，彻底消除 `C4189`。 |

## 二、 沙箱实测与静态核验记录

1. **`VramScheduler` C++23 `-Wall -Wextra -Wpedantic -Werror` 实测**：完整镜像 `tests/test_vram_scheduler.cpp`（5 级降级 + `PassThrough_NoFruc` 125 帧高压滞留 + 119 帧低压不触发 + 第 120 帧精准恢复至 `FgFirst_RifeFull` + 死区归零测试），输出 `VRAM_SIGNED_DWELL_TEST_PASSED`，0 警告。
2. **云盘同目录唯一性与零垃圾核验**：`src/shim/loader/`、`src/gui/`、`include/rtx/`、`src/core/` 均已移出旧版与同名重复副本至 `work/_CineRTX_bak_archive/`，源码树内 0 个 `.bak` 文件、0 处 `RTX_ERR_STATE`。

## 三、 Windows 宿主机 Wave 12.1 终局验收命令

请在 Windows 构建机确认 workspace 同步完成（并核验 `nv_encoder.cpp` 的 `LastWriteTime`）后执行终验：

```powershell
Get-Item src\shim\loader\nv_encoder.cpp | Select-Object LastWriteTime
rg "RTX_ERR_STATE" include/ src/
cmake --build --preset windows-msvc-ninja
.\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe
Push-Location [REDACTED_PATH]; .\.vs\build\cmake\windows-msvc-ninja\rtx_tests.exe; Pop-Location
```

确认 **85/85 目标 `0 Error(s), 0 Warning(s)`** 与双目录 **147/147 PASS** 后，即可正式封板 Wave 12.1 并解封 Wave 12.2。
