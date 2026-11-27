# **67\_CineRTX Phase 13 Wave 3 (Step 3.1–3.4)：全量编译门禁与 RTX 5060 真机终验施工单**

* **施工单编号**：`CINERTX-P13-WO-067`  
* **所属波次**：`Wave 3（宿主机真机全链路核验与封板验收波次）`  
* **前置依赖**：`64` 号（Wave 0）、`65` 号（Wave 1）与 `66` 号（Wave 2）全部落地  
* **验证宿主**：Windows 11 \+ MSVC 19.51（`/W4 /WX`）+ NVIDIA GeForce RTX 5060 Laptop GPU（8GB VRAM）  
* **终验目标**：达成 `.\build.bat` 退出码 `0`、五重静态门禁全绿、`rtx_tests.exe` **157 / 157 PASS**、桌面 GUI `120-rife` 真机零紫色闪烁闭环  
* **签发日期**：`2026-11-26`

## **一、真机验证宪法红线（`[DEC-004]` / `[DEC-008]`）**

1. **宿主机实测先于呈报（`[DEC-008]`）**：所有构建、门禁、单元测试与桌面交互结果必须采集自 Windows 11 \+ RTX 5060 物理宿主机的真实终端输出与 FFprobe/像素检测物证，严禁提交任何推演或未运行数据。  
2. **真机全链路闭环（`[DEC-004]`）**：不仅要求无头单测 `157/157 PASS`，还必须在 `CineRTX.exe` 桌面工作站界面实际拖入素材、切换 `120-rife` 并完成 4K/1080p 实片重制导出，验证全片帧率、色彩、无紫屏闪烁及零内存/句柄泄漏。

## **二、原子验收步骤（Step 3.1 – Step 3.4）**

### **Step 3.1：执行 MSVC `/W4 /WX` 全量构建（验证 PTX 重新内嵌与全目标链接）**

* **执行命令**：

```
.\build.bat
```

* **强制核验要点**：  
  1. 确认构建日志中触发 `rife_pack.ptx` \-\> `rife_pack_ptx.h` 重新生成，`rtx_ngx_bridge.dll` 编译通过；  
  2. 确认 `rtx_core.lib`、`rtx_headless_cli.exe`、`CineRTX.exe` 与 `rtx_tests.exe` **全部目标 `0 Error, 0 Warning`**，脚本退出码为 `0`；  
  3. 确认构建目录下存在四桶单 Profile 引擎文件：`rife_v4.25_ultra.engine`、`rife_v4.25_wide.engine`、`rife_v4.25_square.engine`、`rife_v4.25_tall.engine`。

### **Step 3.2：执行五重静态门禁与双模 IAT 导入表审计**

* **执行命令**：

```
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditImports.cmake
cmake -P cmake/AuditPrivacy.cmake
cmake -P cmake/AuditSpdx.cmake
cmake -P cmake/AuditSloc.cmake
```

* **强制通关指标**：  
  * `AuditTypes.cmake`：**`OK: 34/35`**（严格 `0` 新增命名类型）；  
  * `AuditImports.cmake`：`CineRTX.exe` GUI 静态物理 DLL **`<= 4`**（`bcrypt.dll, gdi32.dll, kernel32.dll, user32.dll`），`rtx_headless_cli.exe` 静态物理 DLL **`== 2`**（`bcrypt.dll, kernel32.dll`）；  
  * `AuditPrivacy.cmake`：**`PASS: 0 violations`**；  
  * `AuditSpdx.cmake`：**`PASS`**；  
  * `AuditSloc.cmake`：Core Engine Pure SLOC **`<= 23,000`**（约 `12,496` 行）。

### **Step 3.3：执行全套单元测试回归（`rtx_tests.exe` 157 / 157 PASS）**

* **执行命令**：

l  
..vs\\build\\cmake\\windows-msvc-ninja\\rtx\_tests.exe

```

* **强制核验要点**：
  1. 总计数必须输出 **`157 test(s) run, 0 failed`**；
  2. 重点核查新增/重构的 3 项 RIFE C ABI 用例均打印 `PASS`（且在 RTX 5060 宿主机上**不得**出现 `[SKIP-GPU]`）：
     * `rife_real_engine_1080p_fg_first_to_4k_120fps`（验证 `1920x1080` 四相位非重复运动插值、`limited` 与 `full_range == 1` 双色域下中性灰 UV `128 ± 2`）；
     * `rife_no_preview_ring_stream_isolation_soak`（验证 20 轮高频重建与同显存指针覆写零错误复用）；
     * `rife_geometry_mismatch_fail_fast_guard`（验证空指针、未初始化及 `9999x9999` 越界尺寸快速失败）。
  3. 执行期间严禁出现 440Hz 扬声器鸣响或 Windows 系统 `MessageBeep` 蜂鸣。

### Step 3.4：RTX 5060 桌面 `CineRTX.exe` 实片真机终审验收

* **执行步骤**：
  1. 启动 `.\.vs\build\cmake\windows-msvc-ninja\CineRTX.exe`；
  2. 将测试视频（建议 `1920x1080 23.976fps` 或 `60fps` 含运动画面与暗部场景）拖拽入主窗口，确认任务立即入队且预览/参数面板状态同步；
  3. 在插帧模式下拉框切换至 **`120-rife`**（若为 `1080p -> 4K` 可同时核验 `fg-first` 与 `vsr-first`），确认 UI 控件与参数配置即时联动；
  4. 点击开始转码，观察实时预览视口与进度条直至转码完成；
  5. 对输出 `.mp4` 产物执行客观技术核验：
     * 确认视频帧率真实升频至 **`120 fps`**（或目标倍率），无重复帧冻结（非 dup）；
     * 确认全片画面色彩准确，**无紫色闪烁、无绿色偏移、无全黑异常帧、无边缘黑框**。

## 三、Wave 3 终验物证回传矩阵（施工完工汇报必填表）

| 验收维度 | 实测命令 / 操作路径 | 实测输出摘要 | 判定 |
| :--- | :--- | :--- | :---: |
| **MSVC 全量编译** | `.\build.bat` | `0 Error(s), 0 Warning(s)`，退出码 `0` | `PASS` |
| **五重静态门禁** | `AuditTypes / Imports / Privacy / Spdx / Sloc` | `34/35`、`GUI=4 / HEADLESS=2`、`0 violations`、`PASS` | `PASS` |
| **全套单元测试** | `rtx_tests.exe` | `157 test(s) run, 0 failed`（含真实 `1080p` RIFE 双色域验证） | `PASS` |
| **RTX 5060 桌面真机实测** | `CineRTX.exe` 拖拽入队 + `120-rife` 实片转码 | 真实 `120fps` 平滑插帧输出，色彩正常，零紫色闪烁 | `PASS` |

> **四项全部 `PASS` 后，即宣告 `60_` 号 RIFE 重构与 PTX 指令级合龙战役圆满收官。**
```

