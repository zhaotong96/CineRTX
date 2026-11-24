# 72_CineRTX Phase 12 UI下拉列表展开失效修复施工汇报

- **关联裁决书与委托书**：`71_CineRTX Phase 12 UI下拉列表展开失效法医级根因裁决书暨修复施工令.md`（`CINERTX-P12-RULING-071`）、`70_CineRTX Phase 12 UI下拉列表展开失效与交互修复专家咨询委托书.md`（`CINERTX-P12-DELEGATION-070`）
- **关联前序文档**：`69_CineRTX Phase 12 发版前最后一关穿透终审裁决书暨v0.4.0正式发版核准令.md`
- **施工日期**：2026-11-24
- **施工状态**：已严格按照 71 号裁决书标准代码范本完成 `src/gui/rtx_gui.cpp` 与 `src/gui/workstation_canvas.cpp` 定点根治修复，旧版文件已外置归档至 `work/_CineRTX_bak_archive/*.r71bak`（`CineRTX/` 仓内 `.bak` 残留为 `0`），全部静态门禁零退化；待 Windows 11 宿主机增量构建并经用户亲测 7 项下拉交互及刷新发布包后即可解冻 `v0.4.0` 发版。

---

## 一、施工改动清单（严格对齐 71 号裁决书 §2）

| 序号 | 目标文件 | 修复定位 | 核心改动内容 | 门禁与卫生影响 |
| :---: | :--- | :--- | :--- | :--- |
| 1 | `src/gui/rtx_gui.cpp` | `applyWorkspaceLayout`（`showInspector` 分支） | 1. 定义 `comboDropH = u(260)` 与零开销内联 Lambda `isComboBox(int id) noexcept`，完整覆盖全仓全部 **7 个 `COMBOBOX` 控件**（`IDC_CMB_CODEC`、`IDC_CMB_PRESET`、`IDC_CMB_RC`、`IDC_CMB_RES`、`IDC_CMB_GEOM`、`IDC_CMB_FPS`、`IDC_CMB_VSR_QUAL`）。<br>2. 在 `kInspectorRows` 循环中对全部 6 个下拉框传入 `ctrlH = comboDropH`（闭合态由 Win32 自动维持单行 26px 外观，展开态获得约 234px / 10–12 行下拉列表高度）；并对独立布局的 `IDC_CMB_VSR_QUAL` 同步传入 `comboDropH`。<br>3. 复核 `CBN_SELCHANGE` 联动路由（`IDC_CMB_CODEC` / `IDC_CMB_RC` / `IDC_CMB_FPS` 触发 `syncControlStates(hwnd)`），逻辑完整无缺陷。 | 新增具名类型 = **0**；静态 IAT = **0 变化**；Core Engine Pure SLOC = **0 变化** |
| 2 | `src/gui/workstation_canvas.cpp` | `populateComboBoxes` | 1. 定义纯 Win32 标准 ABI 常量 `constexpr UINT kCbSetMinVisible = 0x1701u;`（`CB_SETMINVISIBLE`，**不引入 `<commctrl.h>`**，维持零死头文件卫生）。<br>2. 在 `refill` Lambda 填充完选项后发送 `SendMessageW(h, kCbSetMinVisible, 10, 0);`，确保高 DPI（150%/200%）缩放下稳定展示 10 个可见选项而不意外折叠。 | `#include <commctrl.h>` 全仓保持 **0 命中**；新增具名类型 = **0** |

---

## 二、全仓静态门禁复核（本轮修复后）

| 审计项 | 目标红线 | 实测结果 | 判定 |
| :--- | :--- | :--- | :---: |
| **`AuditTypes`（具名类型预算）** | ≤ 35，0 Violations | **34 / 35（0 Violations）** | **PASS** |
| **`AuditSpdx`（双许可头）** | 缺口 = 0 | **Missing = 0** | **PASS** |
| **`OldFormat`（旧式格式化）** | 0 命中 | **0 命中** | **PASS** |
| **`#include <commctrl.h>`** | 0 命中 | **0 命中** | **PASS** |
| **Core Engine Pure SLOC 变动** | 0 行变动（仅修改 `src/gui/`） | **0 行变动**（维持 12,157 行 ≤ 23,000） | **PASS** |
| **仓内 `.bak` 残留** | 0 命中 | **`CineRTX/` 仓内 0 命中**（2 个旧版已归档至 `work/_CineRTX_bak_archive/*.r71bak`） | **PASS** |

---

## 三、Windows 11 宿主机终局验活与 `v0.4.0` 解冻发版步骤

同步上述 2 个文件至 Windows 11 宿主机后，按 71 号裁决书 §3 依次执行：

```powershell
# 1. 增量构建与门禁零退化复核
cmake --build --preset windows-msvc-relwithdebinfo --target CineRTX rtx_headless_cli rtx_tests
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditSloc.cmake
cmake -DTARGET_BIN=build/windows-msvc-relwithdebinfo/RelWithDebInfo/CineRTX.exe -DMODE=GUI -P cmake/AuditImports.cmake

# 2. 启动 CineRTX.exe，由项目负责人亲测全部 7 项下拉列表展开、Ctrl+L 双语热切换及联动启用/禁用
.\build\windows-msvc-relwithdebinfo\RelWithDebInfo\CineRTX.exe

# 3. 人工验收通过后，刷新 CineRTX-v0.4.0-windows-x64.zip 及 SHA-256 哈希
powershell -ExecutionPolicy Bypass -File scripts\verify_wave12_4_release.ps1 `
    -BuildDir "build\windows-msvc-relwithdebinfo\RelWithDebInfo" `
    -Mode Package
```

经用户亲测全部 7 个下拉列表平滑展开且下方控件点击无遮挡后，即可解冻并正式执行 `git commit` 与 `git tag -a v0.4.0` 发版。
