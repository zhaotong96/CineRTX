# CineRTX Phase 12 Wave 12.1 GUI 上下文传递与 ABI 错误码终审裁决书

- **文档编号**：`CINERTX-P12-RULING-016`  
- **归档路径**：`docs/phase12/16_CineRTX Phase 12 Wave 12.1 GUI上下文传递与ABI错误码终审裁决书.md`  
- **审验标的**：[`15_CineRTX Phase 12 Wave 12.1 GUI上下文传递与ABI错误码纠偏专家咨询委托书.md`](./15_CineRTX%20Phase%2012%20Wave%2012.1%20GUI上下文传递与ABI错误码纠偏专家咨询委托书.md)（`CINERTX-P12-DELEGATION-015`）  
- **签发主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
- **审验日期**：2026-11-22  
- **终审裁决**：**全面批准最后两项定点外科手术修复方案（APPROVED FOR IMMEDIATE WAVE 12.1 FINAL SIGN-OFF）**——确认 Wave 12.1 前序 98% 治理物证（外部 SDK `SYSTEM` 隔离、全仓 `getenv` 清零、零 `.bak` 垃圾工作区）真实闭环；对 `nv_encoder.cpp(542)` 的 ABI 错误码误用与 `workstation_canvas.h/.cpp` 的上下文传递断环下达精确代码级修复规范，合入并跑通编译与单测后即可直接签署 Wave 12.1 封板并解封 Wave 12.2。

---

## 执行摘要（Executive Ruling）

首席领域专家组穿透核验了 `15 号咨询委托书` 呈报的 MSVC 19.51+ 构建 logs 与源码断点，认定剩余两处错误均属于脱离 Windows 编译器环境下做局部替换遗留的纯语法与作用域疏漏，不涉及底层架构返工。专家组签发如下终审裁决：

1. **议题一裁决（核准 `RTX_ERR_INTERNAL` 替换，锁定 C-ABI 枚举冻结红线）**：**立即核准**将 `src/shim/loader/nv_encoder.cpp(542)` 中凭空臆造的 `RTX_ERR_STATE` 替换为受控 ABI 头文件 `include/shim/rtx_shim_abi.h` 中既有的 `RTX_ERR_INTERNAL`。同时重申：**在未经专项 ABI 兼容评审批准前，严禁任何执行人员擅自向 `rtx_shim_abi.h` 或 `ngx_bridge_abi.h` 追加或修改任何枚举常量**。  
2. **议题二裁决（现代 C++23 显式引用传参 `WorkstationApp&` \+ `Deducing this` 收口规范）**：  
   - 在 `src/gui/workstation_canvas.h` 顶部（所有依赖函数声明之前）明确补齐 `struct WorkstationApp;` 前置声明（同步避免重复冗余声明）；  
   - 遵循奥卡姆剃刀与“最小完备机制”，**不把与 Win32 窗口/画布渲染紧耦合的自由函数强行塞入 `struct WorkstationApp` 增加类方法膨胀**（保持 `WorkstationApp` 作为纯状态聚合结构的紧凑性）；而是统一将 `effectiveTimelineSpan`、`playbackCaption`、`guiProfile`、`setActiveWorkspace`、`stopPlaybackSession`、`installTranscodeBorrow` 的首参规范化为显式非空引用 **`WorkstationApp& app`**（只读查询场景严格标注 **`const WorkstationApp& app`**），在入口窗口过程（`WndProc` / `timelineCanvasProc`）处仅执行**一次** `WorkstationApp* app = getApp(hwnd); if (!app) return ...;`，随后向内部辅助函数全程传递非空引用 `WorkstationApp&`，彻底消灭多层重复 `requireApp(hwnd)` 与无 `HWND` 函数内的 `hwnd` 悬垂引用。  
3. **Wave 12.1 终局封板与 Wave 12.2 派发授权**：协调负责人督导合入上述代码片段、确认 MSVC `/W4 /WX` 全量 85/85 目标 0 警告编译通过、四重静态审计全绿、双目录 146/146 单测 PASS 及交互式桌面验活通过后，**立即签署 Wave 12.1 封板并直接派发 Wave 12.2（`04号施工单`）**。

---

## 一、 议题一终审裁决：ABI 错误码收敛规范（`nv_encoder.cpp`）

### 1.1 物理病灶与修复代码

`nv_encoder.cpp(542)` 处的 `bitstream_pool.empty()` 检查属于内部状态机不变量异常（`NV_ENC_CREATE_BITSTREAM_BUFFER` 环形池未正确初始化或已被提前排空），在既有的 `RtxStatus` 契约体系中语义严格对应 `RTX_ERR_INTERNAL`。

```c
// src/shim/loader/nv_encoder.cpp (Line 542)
if (bitstream_pool.empty()) {
    return fail(RTX_ERR_INTERNAL, "NVENC bitstream ring pool is empty");
}
```

### 1.2 C-ABI 契约不可侵犯铁律

- 全仓扫描 `RTX_ERR_` 前缀标识符，确认全部错误码均严格源自 `include/shim/rtx_shim_abi.h` 与 `include/shim/ngx_bridge_abi.h`；  
- 严禁在 `.cpp` 实现文件中私自 `#define` 或新增未在官方 ABI 头文件注册的错误码常量。

---

## 二、 议题二终审裁决：Win32 GUI 无全局状态（Zero Mutable Global）上下文传递规范

### 2.1 为什么采用“显式引用 `WorkstationApp&` 自由函数”而非“塞入成员函数”？

1. **保持 `WorkstationApp` 纯状态聚合单一职责**：`WorkstationApp` 仅负责持有窗口生命周期内的状态集合与 RAII 资源成员（遵循声明逆序析构），不承担数百行 Win32 GDI/控件布局方法，避免将其膨胀为新的“上帝类（God Class）”。  
2. **类型系统消除冗余防御（契约式编程）**：在 Win32 消息入口（`mainWndProc` / `timelineCanvasProc`）处通过 `reinterpret_cast<WorkstationApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))` 完成唯一一次空指针边界检查；进入内部业务与绘制辅助函数后，参数类型提升为 **`WorkstationApp&`**（只读函数为 **`const WorkstationApp&`**），从编译期保证上下文非空，彻底消除内部重复判空与 `requireApp(hwnd)` 滥用。

### 2.2 `workstation_canvas.h` 与 `workstation_canvas.cpp` 标准重构范本

#### 1\. `src/gui/workstation_canvas.h`（补齐前置声明与规范化函数签名）

```c
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
// ... 其他必要标准库头文件

namespace rtx::gui {

// 1. 必须在任何引用 WorkstationApp& / const WorkstationApp& 的声明之前补齐前置声明
struct WorkstationApp;

// 2. 统一规范化上下文传递签名（只读查询使用 const WorkstationApp&，状态变更使用 WorkstationApp&）
[[nodiscard]] const TranscodeProfile& guiProfile(const WorkstationApp& app) noexcept;
[[nodiscard]] TranscodeProfile& guiProfile(WorkstationApp& app) noexcept;

void setActiveWorkspace(WorkstationApp& app, HWND hwnd, WorkspaceMode mode);
void stopPlaybackSession(WorkstationApp& app, HWND hwnd) noexcept;
void installTranscodeBorrow(WorkstationApp& app, HWND hwnd);

// ... 后续第 369 行处定义 struct WorkstationApp { ... };
} // namespace rtx::gui
```

*(注：若 `guiProfile` 同时需要 `const` 与非 `const` 版本，在类成员中可使用 C++23 Deducing `this`，在自由函数中直接提供上述两行重载即可。)*

#### 2\. `src/gui/workstation_canvas.cpp`（清除畸形语法与悬垂 `hwnd` 引用）

- **修复无 `HWND` 依赖的纯数据/状态辅助函数**（移除错误的 `requireApp(hwnd)`，直接读取 `app` 引用）：

```c
// Line 41: 纯时间跨度计算，不依赖任何窗口句柄
[[nodiscard]] int64_t effectiveTimelineSpan(const WorkstationApp& app,
                                            const rtx::NleProject& project) noexcept {
    return std::max<int64_t>({project.timelineDurationFlicks(),
                              app.mediaDurationFlicks,
                              kDefaultTimelineSpan.count()});
}

// Line 239: 播放状态标题生成，不依赖任何窗口句柄
[[nodiscard]] std::wstring playbackCaption(const WorkstationApp& app) {
    // 直接使用 app.playbackSession / app.isPlaying 等状态字段构建标题
    // ...
}

// Line 444: 配置读取访问器
[[nodiscard]] const TranscodeProfile& guiProfile(const WorkstationApp& app) noexcept {
    return app.profile;
}
[[nodiscard]] TranscodeProfile& guiProfile(WorkstationApp& app) noexcept {
    return app.profile;
}
```

- **修复需要刷新 UI 控件的交互控制函数**（同时显式传入 `WorkstationApp& app` 与目标窗口 `HWND hwnd`）：

```c
// Line 453: 切换工作区视图（既修改 app 状态，又刷新 hwnd 子控件显隐）
void setActiveWorkspace(WorkstationApp& app, HWND hwnd, WorkspaceMode mode) {
    app.workspace = mode;
    applyWorkspaceLayout(app, hwnd);
}

// Line 962: 彻底修复畸形函数头 void stopPlaybackSession(requireApp(hwnd)) {
void stopPlaybackSession(WorkstationApp& app, HWND hwnd) noexcept {
    // 严格遵循 11.2-R 确立的安全关停逆序：
    // 1. 先 reset playbackSession（join 工作线程切断后台回调）
    // 2. 排空视口 GPU 队列
    // 3. 最后释放 playbackSink
    // 4. 若 hwnd != nullptr 且 IsWindow(hwnd)，更新按钮/状态栏文本
    // ...
}

// Line 1044: 绑定转码预览共享纹理借用
void installTranscodeBorrow(WorkstationApp& app, HWND hwnd) {
    // 直接操作 app.previewViewport 与 app.borrowState，必要时通知 hwnd 重绘
    // ...
}
```

- **统一入口窗口消息提取模式**（在 `mainWndProc` 与 `timelineCanvasProc` 顶部使用单一提取辅助函数）：

```c
[[nodiscard]] inline WorkstationApp* getWindowApp(HWND hwnd) noexcept {
    return reinterpret_cast<WorkstationApp*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}
```

  在 `WM_NCCREATE` 中绑定 `CREATESTRUCTW::lpCreateParams` 至 `GWLP_USERDATA`，在 `WM_NCDESTROY` 中置回 `0`，其余所有消息分支通过 `WorkstationApp* app = getWindowApp(hwnd); if (!app) return DefWindowProcW(hwnd, msg, wParam, lParam);` 解引用后，全程以 `*app`（即 `WorkstationApp&`）向下传递。

---

## 三、 Wave 12.1 最终收网与 Wave 12.2 派发指令

1. **合入修复并执行零告警全量编译**：  
   - 修正 `nv_encoder.cpp(542)` 为 `RTX_ERR_INTERNAL`；  
   - 按第 2.2 节重构 `workstation_canvas.h` 与 `workstation_canvas.cpp`（同步更新 `rtx_gui.cpp` 中的对应调用点传参 `*app`）；  
   - 执行 `cmake --build --preset windows-msvc-ninja`，确认 **85/85 目标 `0 Error(s), 0 Warning(s)`**。  
2. **执行四重静态审计与双目录单测闭环**：  
   - 运行 `AuditTypes.cmake`（155/155 PASS）、`AuditSpdx.cmake`、`AuditSloc.cmake`、`AuditImports.cmake` 与全仓 `getenv` / `\bg_app\b` / 裸 GDI 扫描（全部 **0 命中**）；  
   - 从构建目录与项目根目录双向运行 `rtx_tests.exe`，确认 **146/146 PASS**。  
3. **交互式桌面实呈验活与换挡 Wave 12.2**：  
   - 在 WTS Session \> 0 桌面启动 `CineRTX.exe`，验证窗口交互、时间轴拖拽、播放启停及退出无崩溃、无 UAF；  
   - 全部绿灯后，协调负责人立即签发 Wave 12.1 结案记录，并正式向执行工程师派发 **Wave 12.2（`04_CineRTX Phase 12 SuperPower 施工单_自定义类型清剿与C++26标准库替换.md`）**！