# **46\_CineRTX Phase 13 双端预览返工(45号)专家穿透复验裁决与真机竣工指令书**

针对 45 号施工汇报呈报的阶段性修复成果，专家组联合系统架构师对最新的 6 项 C++/HLSL 修改源文件（`preview_viewport.h`、`preview_viewport.cpp`、`preview_tone_map_cs.hlsl`、`workstation_canvas.cpp`、`rtx_gui.cpp`、`test_gui_workstation.cpp`）进行了深度代码逻辑穿透、数学模型验证以及静态风险审查。结合 39 号委托书、40 号排障方案及 41–44 号施工单规范，现发布终审裁决与真机竣工闭环指令。

---

## **1\. 专家复验终审结论**

### **裁决编号：`CINERTX-P13-EXPERT-AUDIT-046`**

#### **裁决结论：协调人驳回完全正确（COORDINATOR REJECTION CONFIRMED）—— 工程师尚未完成返工闭环，当前呈报绝对不可验收！**

### **核心裁决理由（两大类一票否决事实）**

#### **一、 程序性硬否决（违反 `[DEC-004]` / `[DEC-008]` 质量规程）**

工程师在 45 号施工汇报第三节亲口承认：**未在 Windows 11 \+ MSVC 19.51 \+ RTX 5060 目标宿主机运行 `.\build.bat` 编译（未执行 `fxc` 重编译 HLSL 生成头文件）、未实跑 `rtx_tests.exe`，亦未进行任何 GUI 窗口双端真机运行验证**。工程师仅凭静态语法分析与括号平衡统计即提交呈报，严重违反工程交付标准。

#### **二、 实质性源码穿透发现（源码物理铁证）**

专家组对源码进行逐行穿透核查后确认：**问题 2A（转码预览色彩失真）与问题 2B（交换链几何溢出与 DComp 视口裁剪）的主体代码已经修正且逻辑正确，但问题 1（播放器单线垂直截断）的真正生产端根因未被触及，且代码库中尚遗留 3 处隐蔽缺陷与 1 处重大编译不一致风险！**

---

## **2\. 逐项源码穿透审计实证表**

### **2.1 肯定项：已真实修改且逻辑正确的代码模块**

专家组对以下已正确落地的代码变更予以确认与保留，无需二次修改：

* **色彩标志位与位掩码规范（`src/gui/preview_viewport.h:31-36, 115, 131`）**  
  * 明确定义 `kPreviewLeftSdr = 0u`、`kPreviewLeftPq = 1u << 0`、`kPreviewLeftHlg = 1u << 1`、`kPreviewLeftBt2020 = 1u << 2`、`kPreviewRightPq = 1u << 3`、`kPreviewRightHlg = 1u << 4`、`kPreviewRightBt2020 = 1u << 5` 等具名掩码，消除了硬编码魔数。  
* **DirectComposition 视口裁剪与交换链解耦（`src/gui/preview_viewport.cpp:369, 421, 553, 599`）**  
  * `target_width_ / target_height_` 与 `width_ / height_` 成功解耦；`IDCompositionRectangleClip` 被正确创建，并使用 `SetRight()` 与 `SetBottom()` 实现像素级精准剪裁；呈现前尺寸在 `presentShared()` 中增加了非零兜底保护。  
* **像素步长校验与 Direct3D 色彩空间设置（`src/gui/preview_viewport.cpp:525, 631, 643`）**  
  * 成功添加 `DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709` 明确声明；实现了 `pitch >= src_width * 4u && (pitch % 4u) == 0u` 的紧密防御校验。  
* **Compute Shader 视口变换与 EOTF/Tone-Mapping 算法（`src/gui/shaders/preview_tone_map_cs.hlsl:96-175`）**  
  * 布局调整为 12 DWORD `ViewportConstants`（解耦 `leftMaxNits, colorFlags, rightMaxNits`）；实现了 `pqToNits()`、`hlgToNits()`、`bt2390ToSdrNits()` 及 `bt2020ToBt709()`；居中 Aspect-Fit 映射计算逻辑完全符合几何学规范。  
* **自动化测试扩充（`tests/test_gui_workstation.cpp:360+`）**  
  * 成功引入 `gui_aspect_fit_draw_rect_and_dual_color_flags` 单元测试，覆盖 Aspect Ratio 计算与 Flags 位运算。

---

### **2.2 否决项：穿透查出的 4 项未闭环硬伤（驳回的核心技术事实）**

```
+--------------------------------------------------------------------------------------------------+

| 45号返工呈报 - 穿透审查发现的四大未闭环漏洞                                                       |
|---|
| [漏洞 1] 生产端遗漏: bridge_exports.cpp 完全未改 (未排查 CUDA 2D Grid/Block 维度与 RowBytes)     |
| [漏洞 2] 编译层隐患: preview_tone_map_cs.h 未用 fxc 刷新 (C++ 12-DWORD 常量与旧 HLSL 字节码错位)    |
| [漏洞 3] 消费端逻辑: workstation_canvas.cpp 仍硬编码 960x540，且 HDR10 输入未置位 Left Flags        |
| [漏洞 4] 图层层级缺陷: CreateTargetForHwnd 第二参数仍为 TRUE (导致 top-most 盖压 GUI 元素)       |
+--------------------------------------------------------------------------------------------------+
```

#### **硬伤 1：【严重遗漏】问题 1（播放器顶部数像素单线渲染）未排查与修改生产端 `bridge_exports.cpp`**

* **代码事实**：45 号汇报变更文件列表中完全不存在 `src/shim/ngx_bridge/bridge_exports.cpp`。  
* **技术分析**：41 号施工单明确指出问题 1 存在三段链路。其中\*\*段 1（无头播放环生产端 `bridge_exports.cpp`）\*\*负责分配预览环与启动 CUDA 映射。在 `rtx_ngx_bridge_create`（`reserved[0] == 1` 分支）与 `rtx_ngx_bridge_playback_pack()` 中，若 `launchPreprocessBuffer` 内部核函数的 CUDA Grid 维度误写为 `grid.y == 1`，则 CUDA 驱动只会向共享 Shared Surface 的顶部 1 行写入真实 RGBA 数据，其余 `height - 1` 行均为全 0 虚无。  
* **后果**：工程师仅在消费端 `preview_viewport.cpp` 加了 `pitch >= src_width * 4u` 校验，这完全无法解决生产端"只写了顶部单线"的根本原因！真机运行播放器时依然是顶部一行像素的极窄条带。

#### **硬伤 2：【编译隐患】预编译着色器头文件 `preview_tone_map_cs.h` 未同步更新**

* **代码事实**：`src/gui/preview_viewport.cpp:22` 通过 `#include "preview_tone_map_cs.h"` 直接引用预编译字节码数组 `g_preview_tone_map_cs`。而 `preview_tone_map_cs.hlsl` 顶部明确要求使用以下命令编译：  
  `fxc /T cs_5_1 /E CSMain /Vn g_preview_tone_map_cs /Fh preview_tone_map_cs.h preview_tone_map_cs.hlsl`  
* **技术分析**：由于工程师未在 Windows 环境下编译，仓库中检入的 `preview_tone_map_cs.h` 仍为旧版（根常量结构体未包含新增加的 `colorFlags` / `rightMaxNits` 字段）。  
* **后果**：C++ 端按新的 12 DWORD 常量结构传递数据，但 GPU 实际上运行的是旧版 Compute Shader 字节码。这会导致根常量内存偏移错位，转码预览色彩渲染将发生严重崩溃或持续偏色。

#### **硬伤 3：【逻辑缺陷】`workstation_canvas.cpp:1100-1126` 仍硬编码 `960u, 540u` 且未对齐 HDR10 标志**

* **代码事实**：审查 `src/gui/workstation_canvas.cpp` 的 `presentTranscodePreview()` 函数（第 1100–1126 行）：

```
// 穿透实测：现存代码仍包含硬编码，且忽视了 HDR10 视频源
uint32_t color_flags = kPreviewLeftSdr;
if (prof.hdr_mode == TranscodeHdrMode::kPq) {
    color_flags |= kPreviewRightPq | kPreviewRightBt2020;
}
// ...
viewport->presentShared(..., 960u, 540u, ..., color_flags); // <--- 依然传递硬编码尺寸！
```

* **技术分析**：  
  1. **尺寸硬编码**：直接传入 `960u, 540u`，未从 `borrow_preview` 读取真正的源视频帧分辨率（如 `3840x2160`），导致 GPU Compute Shader 无法按真实宽高比做 Aspect-Fit Letterbox 算力映射。  
  2. **左侧视口色彩标志缺失**：当输入源本身就是 HDR10（`BT.2020 PQ`）时，代码仅设置了右侧（输出）的 `kPreviewRightPq | kPreviewRightBt2020`，而左侧 `color_flags` 仍然被设为 `kPreviewLeftSdr`！  
* **后果**：输入为 HDR10 视频时，左侧原始帧会被 Compute Shader 误按 SDR 直通处理，渲染为极度发灰、对比度全失的 Log 灰片；同时尺寸计算失真。

#### **硬伤 4：【图层缺陷】`rtx_gui.cpp` 与 `CreateTargetForHwnd` 图层合成标志尚未闭环**

* **代码事实**：`preview_viewport.cpp:525` 中：  
  `hr = dcomp_desktop_->CreateTargetForHwnd(child_hwnd_.get(), TRUE, &raw_target);`  
  第二个参数仍然为 `TRUE`（Topmost）。  
* **技术分析**：43 号施工单要求该参数必须更改为 `FALSE`。若设为 `TRUE`，DirectComposition 会强行将该视口图层置于 HWND 窗口树的最顶层，盖压住 GUI 主界面的右键弹出菜单、下拉列表及 Toast 通知。

---

## **3\. 工程师真机闭环返工指令（四步立即执行）**

工程师必须严格按照以下四个步骤完成物理机修复与验证。**严禁再次提交未经真机实测的纸面汇报！**

```
+--------------------------------------------------------------------------------------------------+
| 工程师四步返工闭环执行路线图                                                                       |
+--------------------------------------------------------------------------------------------------+
| [Step 1] 修复生产端: 排查 bridge_exports.cpp 分配尺寸、RowBytes 与 CUDA Kernel 2D 维度 (grid.y)    |
| [Step 2] 修正 Canvas: workstation_canvas.cpp 传入动态宽高，完善 HDR10 左侧标志与 CreateTarget    |
| [Step 3] 编译与静态门禁: 运行 fxc 生成 preview_tone_map_cs.h -> build.bat -> rtx_tests.exe         |
| [Step 4] 双端真机实测: 启动 CineRTX.exe，截屏/记录日志，呈报二次验收申请                        |
+--------------------------------------------------------------------------------------------------+
```

### **Step 1：排查并修复 `bridge_exports.cpp` 无头播放环 CUDA 2D 写入**

1. 打开 `src/shim/ngx_bridge/bridge_exports.cpp`，定位到 `rtx_ngx_bridge_create`（`reserved[0] == 1`）与 `rtx_ngx_bridge_playback_pack()`。  
2. 检查 3-slot 共享预览环分配逻辑：  
   * 确认 `previewRingRowBytes()` 是否严格等于 `width * 4u`。  
   * 检查 CUDA 核心函数 `launchPreprocessBuffer` 的核函数网格配置：确保 `grid.y` 根据 `height` 动态计算（例如 `dim3 grid((width + 31) / 32, (height + 31) / 32)`），而非硬编码 `grid.y = 1`。  
3. 在 `tests/test_playback_session.cpp` 中补充针对底层 Buffer 底部行像素（`row = height - 1`）非零的断言检查。

### **Step 2：修正 `workstation_canvas.cpp` 尺寸与 HDR10 标志**

1. 修改 `src/gui/workstation_canvas.cpp` 的 `presentTranscodePreview()` 函数：  
   * 动态获取源帧维度 `src_w, src_h`，取消 `960u, 540u` 硬编码。  
   * 完善色彩 Flags 判断：若输入视频源为 HDR10 / PQ，设置 `color_flags |= (kPreviewLeftPq | kPreviewLeftBt2020)`；若输入为 HLG，设置 `color_flags |= (kPreviewLeftHlg | kPreviewLeftBt2020)`。  
   * 结合 `MaxCLL` 或动态测量值，正确传递 `left_max_nits` 与 `right_max_nits`。  
2. 将 `src/gui/preview_viewport.cpp:525` 的 `CreateTargetForHwnd` 第二个参数由 `TRUE` 修正为 `FALSE`。

### **Step 3：宿主机执行 `fxc` 编译、工程构建与静态门禁校验**

在 Windows 11 \+ MSVC 19.51 \+ RTX 5060 目标宿主机环境下的 PowerShell / CMD 中依次执行以下指令：

```
# 1. 使用 DirectSDK fxc 重新生成 CS 头文件
fxc /T cs_5_1 /E CSMain /Vn g_preview_tone_map_cs /Fh src/gui/preview_tone_map_cs.h src/gui/shaders/preview_tone_map_cs.hlsl

# 2. 编译整个工程
.\build.bat

# 3. 运行完整单元测试集
.\build\bin\rtx_tests.exe
```

必须确保上述命令**返回值 (Exit Code) 均为 0**，且 `rtx_tests.exe` 输出 **`0 FAILED`**。同时验证五重静态门禁：

* `AuditTypes` 校验：通过。  
* `AuditImports` 架构依赖校验：GUI 模块 `<= 4`，HEADLESS 模块 `== 2`。  
* `AuditPrivacy` 隐私泄漏校验：`0` 违规。

### **Step 4：真机双端 GUI 运行实测取证**

启动编译生成的 `CineRTX.exe`，执行以下真机测试并记录日志：

1. **播放器双端预览实测**：加载 2160p 24fps 视频及 1080p 60fps 视频，确认 Player 视口满框居中渲染，彻底消除顶部单线截断现象。  
2. **转码器预览实测**：加载 HDR10 输入源并开启 SDR / Tone-Mapping 导出，确认 Split-View 左右两侧对比正确：左侧 HDR 原色还原精准（发灰问题消失），右侧 Tone-Mapped SDR 映射自然，且画布无 DirectComposition 窗口溢出。

---

## **4\. 竣工验收指令总结**

工程师在完成上述四步修复与真机实测后，须提交 **47 号施工汇报**。汇报中必须附带：

1. `git diff` 包含 `bridge_exports.cpp` 与 `preview_tone_map_cs.h` 的完整代码变更记录。  
2. 目标宿主机上 `fxc`、`build.bat` 及 `rtx_tests.exe` 控制台运行的完整文本日志。  
3. `CineRTX.exe` 真机运行无头播放与 GUI 转码预览的像素级实测截图证据。

**未满足上述条件前，本工程决不接受二次呈报！**