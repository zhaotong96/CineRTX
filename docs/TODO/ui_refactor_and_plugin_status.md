# CineRTX GUI 演进重构设计：底部状态栏与插件运行态诊断体系

> **文档标识**：`SPEC-GUI-TODO-001`  
> **所属目录**：[`docs/TODO/ui_refactor_and_plugin_status.md`](file:///[REDACTED_PROJ]/docs/TODO/ui_refactor_and_plugin_status.md)  
> **起草日期**：2026-10-25  
> **状态**：待评审 / 演进路线规划 (Draft / RFC)  
> **适用目标**：[`src/gui/rtx_gui.cpp`](file:///[REDACTED_PROJ]/src/gui/rtx_gui.cpp) 原生 Win32 GDI 即时态单 exe GUI

> **2026-10-29 更新：** 产品契约已统一为单一重制链路。GUI 现在直接调用
> `rtx::runNvidiaRemasterTranscode`，不再提供“极速转码 / AI 重制”双模式选择，
> 也不允许在缺少 NGX DLL 时退回另一种像素路径。本文档中状态栏、异步探针与
> 诊断引导仍然有效；涉及双模式切换的描述仅作历史设计参考。

---

## 1. 现状审查与核心痛点 (Problem Statement)

### 1.1 现状盲区实测
在当前 CineRTX GUI 实现（[`src/gui/rtx_gui.cpp`](file:///[REDACTED_PROJ]/src/gui/rtx_gui.cpp)）中，界面设计仅包含顶栏设备名、视频拖拽卡片、编解码参数卡片与下方任务吞吐进度条：
1. **完全缺失状态栏 (Status Bar)**：主窗口底部缺乏持久化硬件与环境诊断状态栏，用户无法感知当前系统底层能力；
2. **插件装载为静默失败 (Silent Fallback)**：
   - 依赖目录 `plugins/nvidia_rtx/` 下缺少 `nvngx_vsr.dll` 或 `nvngx_truehdr.dll` 时，GUI 没有任何显式视觉提示（如置灰、警告角标、缺件说明）；
   - 用户无法直观获知：当前运行的是纯硬件零拷贝裸转码（Gate 1），还是具备画质重制能力的超分/HDR 流水线（Gate 2）；
3. **功能入口已统一**：GUI 现在调用 `rtx::runNvidiaRemasterTranscode`；本文
   余下的双模式选择设计已被单一产品契约取代，仅保留状态栏与诊断探针部分。

---

## 2. 重构设计目标与架构原则 (Architectural Principles)

1. **延续极简原生单 exe 约束 (KISS & Zero External Runtime)**：
   - 坚决杜绝引入 Qt、Electron、WebView2 或 .NET/WPF；
   - 保持 Win32 GDI 纯即时态自绘，零子控件句柄（No HWND child controls），全窗保持单一双缓冲 `BitBlt` 渲染；
   - 保持 60 FPS 响应，冷启动时间稳定保持在 $\le 50\text{ ms}$。
2. **显式状态暴露 (Explicit Status Over Implicit Fallback)**：
   - 插件状态透明化：健康（🟢）、缺失（🟡）、硬件不兼容（🔴）三种状态一目了然；
   - 缺失依赖时提供友好的自愈引导，杜绝“无反应”或“静默退化”。
3. **零开销异步探针 (Non-blocking Asynchronous Probing)**：
   - 启动与动态检测严禁在主 UI 消息循环（Message Pump）中执行阻塞式 `LoadLibraryExW` 或 DX12 枚举；
   - 采用轻量后台线程完成探测，通过 `PostMessageW(hwnd, WM_APP + 4, ...)` 异步回调刷脏。

---

## 3. UI 界面布局与视觉规范重构 (Layout & Visual Spec)

### 3.1 窗口几何拓扑演进

```
┌─────────────────────────────────────────────────────────────┐
│ ▌ RTX 零拷贝转码引擎                                         │
│   NVIDIA GeForce RTX 5060 Laptop · SM 12.0 · Driver 617.14  │
├─────────────────────────────────────────────────────────────┤
│ ┌─ 输入与工作流模式 ──────────────────────────────────────┐ │
│ │  拖拽视频文件至此 (MP4 / MKV / MOV)                      │ │
│ │  [ ● 极速转码 (Gate 1) ]    [ ○ AI画质重制 (Gate 2) ⚠ ]  │ │
│ └─────────────────────────────────────────────────────────┘ │
│ ┌─ 编码配置 ──────┐ ┌─ 画质预设 ───┐ ┌─ 操作 ────────────┐ │
│ │  编码: HEVC/AV1 │ │  预设: P7/质量│ │  开始转码 (Space)│ │
│ └─────────────────┘ └──────────────┘ └───────────────────┘ │
│ ┌─ 任务遥测与流水线监控 ──────────────────────────────────┐ │
│ │  进度条 [=======================>             ]  58.4%   │ │
│ │  吞吐: 128.7 FPS │ 帧数: 1420/2430 │ 耗时: 11.0s │ 零拷贝 │ │
│ └─────────────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────┤
│ 底部状态栏 (Persistent Status Bar, 高度 24px)               │
│ [🟢 GPU: 正常] │ [🟡 VSR: 缺少dll] │ [🟡 TrueHDR: 缺少dll] ││
└─────────────────────────────────────────────────────────────┘
```

### 3.2 底部状态栏 (Bottom Status Bar) 槽位设计

状态栏位于窗口底部，固定高度为 `S(24)`（随 DPI 缩放），采用 `kCard2` 背景与 `kBorder` 上边框分割线：

| 槽位 | 标识 | 正常就绪态 (Green) | 缺失依赖态 (Yellow) | 硬件不支持态 (Red) | 交互响应 (Click / Hover) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Slot 1** | **硬件与驱动** | `🟢 RTX 5060 (617.14)` | `🟡 未检测到 NV 显卡` | `🔴 驱动版本过低 (<550)` | 点击弹出驱动升级建议与完整 GPU 规格 |
| **Slot 2** | **VSR 超分插件** | `🟢 VSR v1.1 已就绪` | `🟡 VSR 缺失 (点击配置)` | `🔴 当前 GPU 架构不支持` | 点击弹出缺少文件路径与官方 SDK 获取向导 |
| **Slot 3** | **TrueHDR 重制** | `🟢 TrueHDR 已就绪` | `🟡 TrueHDR 缺失` | `🔴 当前显卡/驱动不支持` | 点击弹出缺少文件路径与官方 SDK 获取向导 |
| **Slot 4** | **零拷贝管线** | `⚡ 硬件零拷贝链路` | `⚠ 存在主机内存回传` | `✖ 回退纯 CPU 模式` | 显示显存池命中率与 PCIe 传输开销分析 |

### 3.3 模式选择联动机制 (Mode Selector Interlocking)

1. **当 `plugins/nvidia_rtx/` 齐全（包含 `nvngx_vsr.dll` 与 `nvngx_truehdr.dll`）**：
   - `AI画质重制 (Gate 2)` 按钮完全激活，支持单选切换；
   - 切换后，下方参数卡自适应展开“超分倍率（1.5x / 2.0x）”与“HDR10 峰值亮度（1000 nits）”调节滑块。
2. **当缺少任何一个 NGX DLL 时**：
   - `AI画质重制 (Gate 2)` 单选按钮以 `kMuted` 灰显，不可勾选；
   - 按钮右上角绘制黄色角标 `[⚠ 缺少插件]`；
   - 当用户鼠标点击该按钮时，状态栏闪烁黄色高亮，并弹出轻量诊断提示卡（Tooltip/Bubble Card）：
     ```
     ┌──────────────────────────────────────────────────────────┐
     │ ⚠ 无法启用 AI 画质重制功能                                 │
     │ 未在插件目录检测到 NVIDIA NGX 运行时库:                   │
     │ 缺失: plugins/nvidia_rtx/nvngx_vsr.dll                   │
     │       plugins/nvidia_rtx/nvngx_truehdr.dll               │
     │                                                          │
     │ 请将 RTX Video SDK v1.1.0 对应动态库放入上述目录后重试。   │
     │ [打开插件目录]                         [获取 SDK 说明]   │
     └──────────────────────────────────────────────────────────┘
     ```

---

## 4. 运行时技术实现与核心数据结构 (Implementation Specification)

### 4.1 插件健康度状态枚举与快照

```cpp
enum class PluginHealthState : uint32_t {
    Probing = 0,        // 探测中
    Ready = 1,          // 插件完整且驱动签名校验通过
    MissingDll = 2,     // 缺少核心 DLL
    DriverUnsupported = 3, // 驱动版本低于 NGX 要求
    DeviceIncompatible = 4, // GPU 算力不满足 (需 SM 7.0+)
    LoadFailed = 5      // 动态装载发生 Win32 异常
};

struct PluginDiagnosticInfo {
    PluginHealthState vsrState = PluginHealthState::Probing;
    PluginHealthState truehdrState = PluginHealthState::Probing;
    std::wstring missingVsrPath;
    std::wstring missingTrueHdrPath;
    std::wstring failureReason;
    uint32_t vsrVersion = 0;
    uint32_t trueHdrVersion = 0;
};
```

### 4.2 异步探针心跳与消息规约

```cpp
constexpr UINT kMsgPluginDiagnostics = WM_APP + 4;

void startPluginDiagnosticProbe(HWND hwnd) {
    std::thread([hwnd]() {
        PluginDiagnosticInfo info;
        
        // 1. 探测 plugins/nvidia_rtx/nvngx_vsr.dll 物理文件
        const std::wstring pluginDir = getExeDirectory() + L"\\plugins\\nvidia_rtx\\";
        const std::wstring vsrPath = pluginDir + L"nvngx_vsr.dll";
        const std::wstring hdrPath = pluginDir + L"nvngx_truehdr.dll";

        const DWORD vsrAttr = GetFileAttributesW(vsrPath.c_str());
        if (vsrAttr == INVALID_FILE_ATTRIBUTES) {
            info.vsrState = PluginHealthState::MissingDll;
            info.missingVsrPath = vsrPath;
        } else {
            // 尝试以安全模式探针载入，杜绝任意代码执行
            HMODULE hVsr = LoadLibraryExW(vsrPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (hVsr != nullptr) {
                info.vsrState = PluginHealthState::Ready;
                FreeLibrary(hVsr);
            } else {
                info.vsrState = PluginHealthState::LoadFailed;
            }
        }

        // 2. 探测 plugins/nvidia_rtx/nvngx_truehdr.dll 物理文件
        const DWORD hdrAttr = GetFileAttributesW(hdrPath.c_str());
        if (hdrAttr == INVALID_FILE_ATTRIBUTES) {
            info.truehdrState = PluginHealthState::MissingDll;
            info.missingTrueHdrPath = hdrPath;
        } else {
            HMODULE hHdr = LoadLibraryExW(hdrPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (hHdr != nullptr) {
                info.truehdrState = PluginHealthState::Ready;
                FreeLibrary(hHdr);
            } else {
                info.truehdrState = PluginHealthState::LoadFailed;
            }
        }

        // 3. 投递主线程安全更新 UI
        {
            std::lock_guard<std::mutex> lock(g_pluginMutex);
            g_pluginDiagnostics = info;
        }
        PostMessageW(hwnd, kMsgPluginDiagnostics, 0, 0);
    }).detach();
}
```

### 4.3 即时态渲染（GDI Draw Frame）增强

在 `drawFrame(HDC dc, int width, int height)` 中追加状态栏绘制：
```cpp
void drawStatusBar(HDC dc, int width, int height) {
    const int barHeight = S(24);
    const RECT barRect{0, height - barHeight, width, height};
    
    // 绘制状态栏底色与顶部 1px 细分割线
    fillRect(dc, barRect, g_theme.card2);
    HPEN oldPen = SelectPen(dc, g_theme.border);
    MoveToEx(dc, 0, barRect.top, nullptr);
    LineTo(dc, width, barRect.top);
    SelectPen(dc, oldPen);

    // 绘制 4 个槽位状态图标与文本 (Green: 118,185,0; Yellow: 230,160,30; Red: 190,46,38)
    // ...
}
```

---

## 5. 演进路线图与实施计划 (Milestones)

| 阶段 | 实施内容 | 交付标准 | 关联 issue / 模块 |
| :---: | :--- | :--- | :--- |
| **Phase 3.1** | **状态栏基础设施与异步探针** | 窗口底部渲染 4 槽位状态栏；实现无锁后台探测线程与 `kMsgPluginDiagnostics` 消息处理； | [`src/gui/rtx_gui.cpp`](file:///[REDACTED_PROJ]/src/gui/rtx_gui.cpp) |
| **Phase 3.2** | **Gate 2 重制模式交互控件** | 增加工作模式单选控件（极速裸转码 vs AI重制）；缺少 DLL 时自动置灰并展示角标； | [`src/gui/rtx_gui.cpp`](file:///[REDACTED_PROJ]/src/gui/rtx_gui.cpp) |
| **Phase 3.3** | **诊断弹窗与排障向导** | 点击置灰卡片或状态栏槽位，弹出自愈引导（一键打开 `plugins/nvidia_rtx` 目录）； | `ShellExecuteW(..., L"explore", ...)` |
| **Phase 3.4** | **端到端测试与自检** | 添加 `--gui-self-test-missing-plugins` 仿真测试，验证无 DLL 时的健壮性与无崩溃保障。 | [`tests/unit/gui_smoke_test.cpp`](file:///[REDACTED_PROJ]/tests/unit/gui_smoke_test.cpp) |
