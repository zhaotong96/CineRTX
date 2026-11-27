# **49\_rework\_wave2-A\_HLSL字节码重编译\_宿主机全量构建与双端实机封板施工单**

**执行标准规约**：Superpowers 原子执行与终审竣工封板规约  
**裁决依凭**：`CINERTX-P13-EXPERT-AUDIT-046` 穿透复验裁决（硬伤 2 深度闭环与宿主机真机实测全量检验）

---

## **一、 施工元数据与程序性一票否决戒律**

### **1.1 施工元数据**

| 元数据项目 | 规范定义与参数 |
| :---- | :---- |
| **施工波次** | Rework Wave 2 \- Stage A |
| **前置代码依赖** | 依赖 `47_rework_wave1-A` 与 `48_rework_wave1-B` 代码库修改已 100% 落地合并 |
| **本波次目标** | 刷新预编译 HLSL 头文件、执行目标宿主机全量编译与单测、完成五重静态门禁与双端实机封板取证 |
| **目标宿主机配置** | **OS**: Windows 11 Workstation (23H2+)**Compiler**: MSVC 19.51 (Visual Studio 2026/ Build Tools 17.14+)**GPU**: NVIDIA GeForce RTX 5060 (Blackwell 架构 / Driver 572.xx+) |
| **执行者** | Person |
| **施工日期** | Date |

### **1.2 程序性一票否决戒律 (`[DEC-004]` / `[DEC-008]`)**

1. **绝对禁令**：严禁再次提交未经 Windows 11 \+ MSVC 19.51 \+ RTX 5060 目标宿主机真机编译、未运行 `fxc`、未运行 `rtx_tests.exe`、未启动 `CineRTX.exe` 实测的纸面汇报！  
2. **法定递交要求**：工程师在提交下一份施工竣工汇报（50 号文档）时，必须完整粘贴本施工单要求的全部命令行真实 `stdout` / `stderr` 输出与真机像素实测结果，缺一不可。任何缺漏或使用伪造/未在指定硬件环境下运行的输出，直接判定为 **施工违规（REJECTED）**。

---

## **二、 Task 1：DirectX SDK `fxc` 重编译与字节码头文件更新**

### **2.1 穿透事实与硬伤 2 溯源**

`src/gui/preview_viewport.cpp:22` 通过 `#include "preview_tone_map_cs.h"` 直接嵌入预编译 Compute Shader 字节码数组 `g_preview_tone_map_cs`。  
45 号返工修改了源码 `src/gui/shaders/preview_tone_map_cs.hlsl`，却未在 Windows 宿主机运行 `fxc` 刷新 src/gui/preview\_tone\_map\_cs.h！若直接编译运行，C++ 端传递新的 12 DWORD 根常量而 GPU 运行旧字节码，必将导致根常量偏移错位、内存越界与色彩渲染崩溃。

### **2.2 精确执行指令**

在 Windows 11 宿主机 PowerShell 终端中，于仓库根目录运行以下编译命令：

```
fxc /T cs_5_1 /E CSMain /Vn g_preview_tone_map_cs /Fh src/gui/preview_tone_map_cs.h src/gui/shaders/preview_tone_map_cs.hlsl
```

### **2.3 门禁核验与准入要点**

1. **编译过程**：`fxc` 执行退出码必须严格为 `0`，终端输出 0 Warnings, 0 Errors。  
2. **代码变更核验**：运行 `git diff src/gui/preview_tone_map_cs.h`，确认字节码数组 `g_preview_tone_map_cs[]` 已包含更新：  
   * 根常量长度对齐至 12 DWORD；  
   * 包含 `leftMaxNits`, `colorFlags`, `rightMaxNits` 结构字段及 Aspect-Fit `drawRect` 逻辑字节码。

---

## **三、 Task 2：宿主机全量编译与单元测试实跑**

### **3.1 精确执行指令**

在宿主机 PowerShell 终端顺序执行项目构建与全量测试：

```
.\build.bat
.\build\bin\rtx_tests.exe
```

*(注：若 CMake 构建 preset 使用子目录，如 `.\build\windows-msvc-ninja\bin\rtx_tests.exe`，需贴出实际构建脚本输出的准确产物路径并执行)*

### **3.2 硬性通过判据**

1. **编译构建**：`.\build.bat` 退出码 `0`。目标 `CineRTX.exe`、`rtx_headless_cli.exe`、`rtx_tests.exe` 及 `rtx_ngx_bridge.dll` 均完成增量/全量链接，0 Error。  
2. **单元测试**：`rtx_tests.exe` 测试套件运行完成，**0 FAILED**。  
   * 原有 `150/150` 测试用例全数通过；  
   * 47 号新增测试：播放环底部行像素非零断言通过；  
   * 48 号新增测试：Aspect-Fit 计算边界与 HDR10/SDR 双侧色彩标志位断言全数通过。

---

## **四、 Task 3：宿主机五重静态门禁全绿核验**

在宿主机 PowerShell 终端中依次执行 CMake 审计脚本，并捕获完整输出日志：

```
cmake -P cmake/AuditTypes.cmake
cmake -P cmake/AuditImports.cmake
cmake -P cmake/AuditPrivacy.cmake
cmake -P cmake/AuditSloc.cmake
cmake -P cmake/AuditSpdx.cmake
```

### **4.1 五重静态门禁硬性标准表**

| 门禁脚本 | 校验项与指标要求 | 硬性通过判定文本 |
| :---- | :---- | :---- |
| `AuditTypes.cmake` | 命名类型总数管控 | `OK: 34/35 named types`（严格 `<= 35`） |
| `AuditImports.cmake` | GUI 与 CLI 依赖项解耦 | `CineRTX.exe` 静态 DLL 依赖 `<= 4` (`bcrypt, gdi32, kernel32, user32`)`rtx_headless_cli.exe` 静态依赖 `== 2` (`bcrypt, kernel32`) |
| `AuditPrivacy.cmake` | 隐私与数据零外流 | `PASS: 0 violations` |
| `AuditSloc.cmake` | 代码行数上限管控 | `PASS: SLOC constraints verified` |
| `AuditSpdx.cmake` | 开源协议许可证完整性 | `PASS: All files contain valid SPDX tags` |

---

## **五、 Task 4：`CineRTX.exe` 双端真机端到端实测取证**

启动目标产物 `.\build\bin\CineRTX.exe`，在 RTX 5060 硬件平台上对 Player 与 Encoder 双端工作区执行验收测试：

### **5.1 实测场景 A：Player 工作区多画幅满框正常播放 (验证问题 1 根除)**

```
[实测操作流程]
1. 启动 CineRTX.exe，默认或手动切换至 Player 工作区。
2. 依次加载并播放测试样例：
   a. 16:9 标清/高清/超高清 (1080p, 2160p) 视频；
   b. 9:16 竖屏短视频 (720x1280)；
   c. 21:9 变形宽银幕电影片段。
3. 执行 Seek（随机拖动时间轴）、Pause（暂停）、Frame-Step（单帧步进）。
4. 在 Player <-> Encoder 工作区之间进行 5 次往返快速切换。
5. 打开顶栏下拉菜单与模态弹窗。
```

* **AC-1 满框与黑边断言**：视口全程保持 Correct Aspect Ratio（等比 Aspect-Fit），上下或左右自动补齐纯黑边（RGB 0,0,0）。顶部数像素单线截断/白边现象 100% 消除。  
* **AC-2 DComp 混合断言**：菜单与弹窗展开时，无任何被 DComp 视觉盖压或穿透擦除现象（验证 `CreateTargetForHwnd(..., FALSE)` 顶级渲染生效）。

### **5.2 实测场景 B：Encoder 转码预览双侧色彩与画布零溢出 (验证问题 2A & 2B 根除)**

```
[实测操作流程]
1. 切换至 Encoder 工作区，配置分屏对比模式 (Split Preview)。
2. 测试用例 1：SDR (BT.709 Gamma 2.4) 输入 + 关闭 TrueHDR。
3. 测试用例 2：SDR (BT.709 Gamma 2.4) 输入 + 开启 TrueHDR ( Target MaxNits = 1000 )。
4. 测试用例 3：HDR10 (BT.2020 PQ) 原生素材输入转码。
5. 压力测试：
   a. 启动程序即在 Encoder 发起转码（验证第 1 帧渲染）；
   b. 连续拖拽拉伸窗口尺寸、最小化窗口再还原；
   c. 按 F11 进出全屏模式，按 Esc 退出全屏；
   d. 关闭程序。
```

* **AC-3 色彩映射与分屏断言**：  
  * *用例 1*：左右两侧画面亮度、对比度、饱和度 1:1 完全一致，无灰雾、无偏色；  
  * *用例 2*：左侧保持标准 SDR 观感，右侧呈现 TrueHDR 高动态提升，高光区平滑滚降（完美对齐 `frame_max_nits`）；  
  * *用例 3*：左侧按 MaxCLL（或 `1000.0f` 默认值）精准执行 Tone Mapping，彻底消除传统 HDR 视频在 SDR 视口的 Log 灰片感。  
* **AC-4 几何边界与稳定性断言**：  
  * 从第 1 帧起，预览画面严格贴合中间 Canvas 矩形区域，左右两侧无任何 1 像素跨界溢出至检查器 (Inspector) 或队列面板 (Queue Panel)；  
  * 缩放与全屏切换过程中，渲染无拉伸形变、无闪烁，极简退出无 D3D12 Device Lost / Device Removed 错误，程序退出码为 `0`。

---

## **六、 50 号施工竣工汇报法定附件清单**

工程师在提交 **50 号施工竣工汇报（`50_rework_wave2-A_completion_report.md`）** 时，必须附带以下完整原始证据。缺漏任何一项，审查自动化门禁将直接拒绝归档：

1. **核心修改摘要**：  
   * 运行 `git diff --stat` 的终端完整输出；  
   * 显式列出对 `src/shim/ngx_bridge/bridge_exports.cpp`、`src/gui/preview_tone_map_cs.h`、`src/gui/workstation_canvas.cpp` 及 `src/gui/preview_viewport.cpp` 的修改点摘要。  
2. **终端文本日志（Raw Output）**：  
   * 宿主机 `fxc` 编译更新 `preview_tone_map_cs.h` 的完整命令行与输出；  
   * `.\build.bat` 全量编译构建的完整输出文本；  
   * `rtx_tests.exe` 执行完毕后的终端测试结果报告；  
   * 宿主机五重静态门禁 (`AuditTypes.cmake` \~ `AuditSpdx.cmake`) 的 5 个完整终端文本块。  
3. **真机运行凭证与诊断日志**：  
   * `CineRTX.exe` 真机运行 Player 多画幅 (16:9 / 9:16 / 21:9) 满框播放实测截图/日志；  
   * Encoder HDR10 / SDR 分屏预览与 Aspect-Fit 边界无溢出实测截图/诊断日志证据；  
   * 程序正常退出码 (`Exit Code: 0`) 的运行记录。

