# **CineRTX Phase 12 UI 下拉列表展开失效与交互修复专家咨询委托书**

**工程协调推进组 · 受控呈报工件**

---

## **基础元数据**

| 属性项 | 内容详细 |
| :---- | :---- |
| **委托书编号** | `CINERTX-P12-DELEGATION-070` |
| **归档路径** | `docs/phase12/70_CineRTX Phase 12 UI下拉列表展开失效与交互修复专家咨询委托书.md` |
| **呈报主体** | 工程协调推进组 (Engineering Coordination Group) |
| **受呈主体** | 多媒体系统工程与 GPU 异构计算首席领域专家组 (Extreme Modern C++ Purist & Architecture Board) |
| **呈报依据** | 用户在 Windows 交互桌面现场实测最高驳回指令（“目前的UI下拉列表无法点击drop出来，这个问题要写委托，phase12内必须解决”） |
| **排查标的** | `src/gui/rtx_gui.cpp`（`applyWorkspaceLayout` 布局机制）、`src/gui/workstation_canvas.cpp`（`createWorkstationControls` / `populateComboBoxes`） |
| **日期** | 2026-11-24 |

---

## **一、 缺陷现象与用户最高驳回裁定**

在阶段 2 实机验证就绪、准备签发 `v0.4.0` 发版前夕，项目负责人（用户）在 Windows 11 桌面（`WTS SessionId: 1, Active`）启动 `CineRTX.exe` 执行真机交互验收时，发现严重交互缺陷：
- **缺陷现象**：右侧 Inspector 工作区面板中的全部下拉列表（包括 **Codec** `IDC_CMB_CODEC`、**Preset** `IDC_CMB_PRESET`、**Rate Control** `IDC_CMB_RC`、**Resolution** `IDC_CMB_RES`、**Aspect** `IDC_CMB_GEOM`、**Target FPS** `IDC_CMB_FPS`、**VSR Lvl** `IDC_CMB_VSR_QUAL`），鼠标点击右侧下拉箭头或控件本体时，**均完全无法弹出（drop down）选项列表**，控件处于“死锁/点不开”的不可交互状态。
- **用户最高指令**：**立即暂停发版，就此问题正式出具专家咨询委托书，此缺陷必须在 Phase 12 内彻底修复并通过实测验收，方可发版！**

---

## **二、 只读检查与物理根因分析（Root Cause Analysis）**

工程协调推进组遵照“只读检查先于结论”铁律，深入穿透排查了 `src/gui/rtx_gui.cpp` 与 `src/gui/workstation_canvas.cpp`，精准锁定了导致下拉列表无法展开的物理根因：

### **2.1 物理根因：Win32 ComboBox 尺寸机制误区致使下拉列表可用高度为 0**
1. **源码事实排查（`src/gui/rtx_gui.cpp` 第 187–198 行）**：
   ```cpp
   // src/gui/rtx_gui.cpp:187-198
   const int panelW = u(380), panelX = width - panelW, lblW = u(120), rowH = u(26), gap = u(2);
   int y = bodyTop + u(10);
   for (const auto& [labelId, ctrlId, extraId, extraW_raw] : kInspectorRows) {
       const int extraW = extraW_raw != 0 ? u(extraW_raw) : 0;
       place(labelId, panelX + u(8), y + u(4), lblW, rowH);
       place(ctrlId, panelX + u(8) + lblW, y, panelW - lblW - u(24) - extraW, rowH);
       if (extraId >= 0) place(extraId, panelX + panelW - u(16) - extraW, y, extraW, rowH);
       y += rowH + gap;
   }
   // ...
   place(IDC_CMB_VSR_QUAL, panelX + u(8) + u(160), y, panelW - u(160) - u(24), rowH);
   ```
   其中 `place` 的底层实现为：
   ```cpp
   const auto place = [hwnd](int id, int x, int y, int w, int h) {
       MoveWindow(GetDlgItem(hwnd, id), x, y, w, h, TRUE);
   };
   ```

2. **Win32 经典机制与因果断裂**：
   - 在微软标准 Win32 控件体系中，对于具有 `CBS_DROPDOWN` 或 `CBS_DROPDOWNLIST` 样式的 ComboBox，调用 `MoveWindow` / `SetWindowPos` 时传入的 `height`（`cy`）参数，**代表的是该控件在包含其下拉弹出列表框（Drop-down List Box）在内的整体最大矩形高度**，而**绝非闭合时的单行文本框高度**！
   - Windows 操作系统内部会自动将闭合态单行编辑框的高度锁定为当前字体高度加上边框（约 24~26 像素），并用传入的 `height` 减去闭合高度，**差值作为下拉列表框的有效展开高度**；
   - 当前布局代码将传入 `MoveWindow` 的高度硬编码赋值为单行高度 `rowH = u(26)`（26 像素）。系统计算得到的下拉高度为：
     $$\text{Dropdown Height} = 26\text{px} - 26\text{px} = 0\text{px}$$
   - **直接因果后果**：当用户用鼠标点击 ComboBox 箭头时，操作系统确实响应了弹出事件，但由于其列表矩形高度被裁切为 0 像素，导致下拉列表在物理层面上“完全隐形/展开高度为零”，造成用户肉眼所见“无法点击 drop 出来”的假死现象！

### **2.2 附加检查项：Z-Order 遮挡与消息路由复核**
推进组同步排查了以下关联因素，需专家组一并终审定型：
1. **Z-Order 层级与 `WS_CLIPSIBLINGS`**：在 `createWorkstationControls` 中创建的各类静态标签、按钮及 D3D12/DirectComposition 画布，是否存在 Z 序覆盖在 ComboBox 下拉列表区域上方的情形；
2. **`DarkMode_Explorer` 主题影响**：`rtx::sys::setWindowTheme(h, L"DarkMode_Explorer", nullptr)` 是否影响系统默认 ComboBox 的展开度量或边框重绘；
3. **`CB_SETMINVISIBLE` / `CB_SETDROPPEDWIDTH`**：是否需要显式发送 `CB_SETMINVISIBLE` 强制限制最少可见项（例如 8 项）避免系统自动折叠。

---

## **三、 专家咨询与裁决请求**

针对上述排查事实与用户“Phase 12 内必须彻底解决”的最高指令，工程协调推进组特向多媒体系统工程与 GPU 异构计算首席领域专家组提出如下三项紧急咨询与裁决请求：

1. **ComboBox 布局尺寸权威修复方案定型**：
   - 裁决在 `applyWorkspaceLayout` 中是否采用控件类型自适应判定，若目标控件为 ComboBox（或在 `isComboBoxId(id)` 范围内），将其传入 `MoveWindow` 的高度扩展为包含下拉列表的完整展开高度（例如 `u(240)` 或 `u(280)`，且闭合态在 Windows 内部自适应保持为 26px），从而根治下拉高度为 0 的问题；
2. **全仓 ComboBox 交互健壮性加固标准**：
   - 确认是否需在 `populateComboBoxes` 中追加 `SendMessageW(h, CB_SETMINVISIBLE, 8, 0)` 等消息指令，以及是否需要 `SetWindowPos(..., HWND_TOP, ...)` 确保 Z 序处于最顶层；
3. **Phase 12 终局闭环推进节奏重排**：
   - 请专家组签发 **《Phase 12 Wave 12.4 UI 下拉列表交互消缺作战令》**，明确标准修复代码范本，指令执行工程师在 0.5 工作日内完成源码原位替换，并在 Windows 宿主机由用户亲自复验下拉展开正常后，再行正式发布 `v0.4.0`！
