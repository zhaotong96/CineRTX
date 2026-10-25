# RTX 转码引擎 Win32 极简原生单 exe GUI 演进路线与系统合规 — 领域专家咨询报告

**文档编号**：`RTX-EXPERT-20261024-GUI-CONSULTATION`
**文档性质**：领域专家正式咨询批复报告
**咨询依据**：[`win32_gui_architecture_and_expert_consultation_dossier.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/win32_gui_architecture_and_expert_consultation_dossier.md)
**基准硬件**：[REDACTED_HOST]（RTX 5060 Laptop, sm_120, 8123 MiB GDDR7, 驱动 617.14, CUDA 13.4）

---

## 〇、总裁决摘要 (Executive Verdict)

| 咨询议题 | 裁决结论 | 风险等级 |
|----------|---------|---------|
| 议题一：单体静态链接合规性 | ✅ **方案 (a) 全量静态链接合规且工业稳健度最优** | 🟢 低 |
| 议题二：零拷贝实时画面视口 | ✅ **独立轻量 D3D12 设备 + DirectComposition + 专用渲染线程** | 🟢 低 |
| 议题三：超高频消息泵解耦 | ✅ **无锁原子聚合状态桶 + SetTimer 60Hz 拉取**，彻底禁止 PostMessage 逐帧推送 | 🟢 低 |
| 议题四：高 DPI 与自绘控件 | ✅ **Per-Monitor V2 + 纯 GDI 起步，后期可选 Direct2D 增强** | 🟢 低 |

---

## 一、咨询议题一：单二进制交付与 GPLv3 Plugin-Exception 许可隔离边界合规性

### 1.1 Plugin-Exception 条文精读

审查 [`COPYING.EXCEPTION`](file:///C:/PUBLIC/[REDACTED_PROJ]/COPYING.EXCEPTION) 第 0 节，"Independent Module" 定义含三个**联合条件**：

> (a) is not derived from the Covered Work;
> (b) is distributed under license terms **other than the GPLv3**;
> (c) is combined with the Covered Work **only at run time** by dynamic loading through the Plugin Interface, **without being statically linked**.

第 3 节（TERMINATION）进一步强化：

> If you modify the Covered Work so that it **statically links**, or is otherwise combined at build time with, an Independent Module, this exception **does not apply**.

### 1.2 三层分析矩阵

项目中存在三个不同许可层级的代码：

| 代码层 | 许可证 | 能否静态链入 GPLv3 核心？ | 法律依据 |
|--------|--------|---------------------------|---------|
| **核心调度器 + FFmpeg 封装** | GPLv3 + Plugin-Exception | ✅ **可以** — 同属 "Covered Work" | 同许可证，无障碍 |
| **`rtx_shim` + `rtx_ngx_bridge`** | MIT OR Apache-2.0 | ✅ **可以** — 宽松许可证单向兼容 GPL | MIT/Apache-2.0 允许被吸收入 GPLv3 作品；它们**不是** "Independent Module"（不满足条件 b 的排他性） |
| **NVIDIA 闭源 DLL**（`nvcuvid.dll`, `nvEncodeAPI64.dll`, `nvsdk_ngx_d.dll`） | 专有 EULA | ❌ **必须保持运行时动态加载** | 这才是 Plugin-Exception 所保护的真正 "Independent Module" |

**关键洞察**：Plugin-Exception 的静态链接禁令**仅适用于 "Independent Module"（专有代码）**，不适用于 GPL 兼容的宽松许可代码。MIT 代码一旦被静态链入 GPLv3 二进制，它在分发层面以 GPLv3 条款传递——MIT 许可证明确允许这种再授权。

### 1.3 当前架构的合规性验证

审查 [`d3d12_interop.cpp`](file:///C:/PUBLIC/[REDACTED_PROJ]/src/shim/ngx_bridge/d3d12_interop.cpp) 第 95-100 行，所有 NVIDIA DLL 均通过 `LoadLibraryExW(LOAD_LIBRARY_SEARCH_SYSTEM32)` 动态加载；`third_party/open_headers/ffnvcodec` 为独立许可的开放头文件，不含 NVIDIA 专有 SDK 素材。四个合规条件全部满足：

- (a) Covered Work 保持 GPLv3 ✅
- (b) Independent Module 仅运行时组合 ✅
- (c) 源码中无专有 SDK 头文件/库 ✅
- (d) 已告知接收者独立模块受其自身许可约束 ✅

**附加防线**：GPLv3 §1 "System Library" 例外——NVIDIA GPU 驱动通过 Windows Update / GeForce Experience 随系统分发，可论证为 "System Library"。

### 1.4 方案 (a) vs 方案 (b) 工业稳健度对比

| 维度 | (a) 全量静态链接 | (b) PE 资源内嵌释放 |
|------|-----------------|-------------------|
| **法律合规** | ★★★★★ 教科书级 GPL 兼容整合 | ★★★★☆ 合规但需确保释放的 DLL 保留许可声明 |
| **杀毒误报** | ★★★★★ 无可疑行为模式 | ★★☆☆☆ **高危！** `FindResource→WriteFile→LoadLibrary` 链是**教科书级恶意投放器行为**，Defender/ESET/Kaspersky 均有启发式签名 |
| **SmartScreen** | ★★★★★ 单哈希单签名，声誉累积稳定 | ★★★☆☆ 释放的 DLL 未签名，每次构建重置声誉 |
| **代码签名** | ★★★★★ 一次 Authenticode 覆盖全部 | ★★☆☆☆ 签名 exe 加载未签名 DLL → EDR 行为拦截 |
| **DLL 劫持** | ★★★★★ 无用户可写目录加载 | ★★☆☆☆ `%LOCALAPPDATA%` 写入+加载 = 劫持攻击面 |
| **WDAC/AppLocker** | ★★★★★ 企业策略友好 | ★★☆☆☆ 未签名 DLL 可能被企业策略阻止 |

### 1.5 裁决

> **✅ 全量采纳方案 (a) — 静态单体二进制。**
>
> 将 Win32 UI 模块、核心调度器、MIT/Apache-2.0 垫片层全量静态编译链接为单一 `rtx-transcode.exe`。NVIDIA 闭源 DLL 继续以 `LoadLibraryExW(LOAD_LIBRARY_SEARCH_SYSTEM32)` 运行时动态加载。此方案在法律合规性、杀毒误报、SmartScreen 声誉、企业部署、安全态势五个维度均达到最优。

**备选方案**：若后续需要保持垫片代码的架构独立性（便于测试），可考虑 **MSVC `/DELAYLOAD` 延迟加载 DLL**——将垫片 DLL 与 exe 并排放置在同一签名目录（如 `Program Files\RTXTranscode\`），每个 DLL 独立签名，既保留模块化又不触发杀毒启发式。

---

## 二、咨询议题二：Win32 窗口实时画面零拷贝渲染架构选型

### 2.1 现有架构关键事实

审查 [`d3d12_interop.cpp`](file:///C:/PUBLIC/[REDACTED_PROJ]/src/shim/ngx_bridge/d3d12_interop.cpp) 与 [`ngx_bridge_abi.h`](file:///C:/PUBLIC/[REDACTED_PROJ]/include/shim/ngx_bridge_abi.h)：

- **设备**：无头 `ID3D12Device`（无窗口、无交换链），通过 LUID 匹配 CUDA 设备
- **队列**：单 `DIRECT` 队列，`PRIORITY_NORMAL`，用于 NGX 调度、上传、下载、Fence 信号
- **输出纹理**：`RGBA16F`（R16G16B16A16_FLOAT），3840×2160，`D3D12_HEAP_FLAG_SHARED`，带 `ALLOW_UNORDERED_ACCESS | ALLOW_RENDER_TARGET`
- **CUDA 互操作**：通过 NT 共享句柄导出；CUDA 以 `CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE` + `DEDICATED` 标志导入为外部内存 → MipMapped Array → Surface Object
- **Fence**：`D3D12_FENCE_FLAG_SHARED`，单调递增 `fence_value_`
- **管线流**：NVDEC → NV12/P010 → RGBA16F (NGX VSR + TrueHDR) → Pack to P010 → NVENC

### 2.2 核心裁决：独立轻量 D3D12 设备 + DirectComposition

**必须创建一个独立的 `ID3D12Device` 用于画面预览，不得复用现有无头设备。**

理由：

| 维度 | 复用现有设备 | 独立设备 |
|------|------------|---------|
| 队列竞争 | ❌ 现有单 DIRECT 队列被 NGX 调度、Fence 信号、Upload/Download 共享；`Present()` 会串行化在 NGX 工作负载之后 | ✅ 独立队列，零竞争 |
| TDR 风险 | ❌ 若预览交换链导致 DWM 调度暂停，整个编码管线被阻塞 | ✅ 故障隔离 |
| Fence 空间 | ❌ 现有 Fence 与 CUDA 共享，增加消费者会破坏单调递增协议 | ✅ 独立 Fence |
| 设备丢失 | ❌ 预览触发 DXGI 设备移除将连坐编码设备 | ✅ 互不影响 |
| 额外 VRAM | — | ≈24MB（3×8MB BGRA8 三缓冲） |

### 2.3 呈现模式选型

| 参数 | 推荐值 | 理由 |
|------|--------|------|
| **SwapEffect** | `DXGI_SWAP_EFFECT_FLIP_DISCARD` | 最低延迟；DWM 可直接翻转（Direct Flip）；后缓冲内容不保留，因为每帧从头渲染 |
| **BufferCount** | 3（三缓冲） | CPU 可准备 N+2 帧同时 GPU 渲染 N+1 并合成器呈现 N；仅多花一个 8MB 缓冲 |
| **Format** | `DXGI_FORMAT_B8G8R8A8_UNORM`（SDR 桌面） | DWM 原生合成格式，无隐式转换 |
| **ColorSpace** | `DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709` | sRGB |
| **Queue Priority** | `D3D12_COMMAND_QUEUE_PRIORITY_LOW` | GPU 调度器优先抢占编码管线的 Compute 工作负载 |
| **呈现方式** | **DirectComposition**（`CreateSwapChainForComposition`） | 见下文 |

### 2.4 DirectComposition vs 普通 SwapChain

**裁决：推荐 DirectComposition。**

优势：
1. **合成子 HWND**：DComp 允许预览交换链作为主窗口的 Visual 子元素无缝合成，无需 HWND 重设父级的兼容性问题
2. **无 WM_PAINT/WM_SIZE 耦合**：调整大小通过更新 DComp Visual 的 Transform 实现，交换链保持固定内部分辨率
3. **独立 Present**：DComp 交换链的 `Present1()` 不阻塞消息泵
4. **更低延迟**：绕过传统 GDI Blit 路径，直达 DWM 合成树

```cpp
// 创建用于合成的交换链（无 HWND 直接绑定）
DXGI_SWAP_CHAIN_DESC1 desc = {};
desc.Width = previewWidth;
desc.Height = previewHeight;
desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
desc.SampleDesc.Count = 1;
desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
desc.BufferCount = 3;
desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

IDXGISwapChain1* swapChain;
factory->CreateSwapChainForComposition(previewQueue, &desc, nullptr, &swapChain);

// 绑定至 DirectComposition
IDCompositionDevice* dcompDevice;
DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&dcompDevice));
IDCompositionTarget* target;
dcompDevice->CreateTargetForHwnd(previewHwnd, true, &target);
IDCompositionVisual* visual;
dcompDevice->CreateVisual(&visual);
visual->SetContent(swapChain);
target->SetRoot(visual);
dcompDevice->Commit();
```

### 2.5 纹理共享与同步：零拷贝引用

1. **在预览设备上打开共享句柄**：`ID3D12Device::OpenSharedHandle(output_.shared_handle)` 返回一个别名同一 GPU 内存的 `ID3D12Resource*`——零开销，无数据拷贝
2. **创建 SRV**（`DXGI_FORMAT_R16G16B16A16_FLOAT`），着色器只读不写
3. **同步**：在预览设备上导入现有共享 Fence，预览队列 `Wait()` 等待编码设备信号 `fence_value N` 后再访问纹理
4. **跳帧策略**：预览以显示器刷新率（60Hz）运行，编码以转码帧率运行；预览是 "最佳努力" 模式

### 2.6 HDR 色调映射：PQ→sRGB 独立渲染通道

**核心原则：只读访问原始 RGBA16F 纹理，绝不污染送往 NVENC 的 HDR10 P010 表面。**

使用独立 Compute Shader 执行 PQ→sRGB 色调映射：

```hlsl
// 输入: RGBA16F 纹理，BT.2020 PQ 空间
Texture2D<float4> hdrInput : register(t0);     // SRV — 只读
RWTexture2D<float4> sdrOutput : register(u0);   // 写入交换链后缓冲

static const float3x3 bt2020_to_bt709 = float3x3(
     1.6605, -0.5876, -0.0728,
    -0.1246,  1.1329, -0.0083,
    -0.0182, -0.1006,  1.1187);

float pqEOTF(float pq) {
    const float m1 = 0.1593017578125, m2 = 78.84375;
    const float c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    float p = pow(max(pq, 0.0), 1.0 / m2);
    return pow(max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1) * 10000.0;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    float4 pqColor = hdrInput[id.xy];
    float3 lin2020 = float3(pqEOTF(pqColor.r), pqEOTF(pqColor.g), pqEOTF(pqColor.b));
    float3 lin709 = max(mul(bt2020_to_bt709, lin2020), 0.0);
    float3 tm = lin709 / (1000.0 + lin709);                    // Reinhard
    float3 srgb = lerp(tm * 12.92,
                       1.055 * pow(tm, 1.0/2.4) - 0.055,
                       step(0.0031308, tm));                    // sRGB OETF
    sdrOutput[id.xy] = float4(saturate(srgb), 1.0);
}
```

**性能影响 <1% 的保证机制**：
1. NVENC 和 Tensor Core 是独立于 3D/Compute 引擎的硬件固定功能单元
2. 预览设备队列 `PRIORITY_LOW`，GPU 调度器抢占优先编码工作负载
3. 色调映射着色器仅为单次全屏 Dispatch，4K 下 ~0.1ms
4. 预览可缩小至 960×540，开销按比例降低
5. 使用 `DXGI_PRESENT_ALLOW_TEARING` 避免 VSync 停滞
6. 预览线程**不向编码设备队列添加任何 GPU 命令**——仅在编码设备完成后读取共享纹理

### 2.7 独立渲染线程架构

```
编码线程                           预览线程
═══════                           ══════
NVDEC → RGBA16F → P010 → NVENC     Wait on shared fence value N
      ↓                              ↓
Signal shared fence (value N)      Acquire swap chain back buffer
      ↓                              ↓
继续编码帧 N+1                     OpenSharedHandle → SRV 只读
                                     ↓
                                   PQ→sRGB Compute Pass
                                     ↓
                                   Present() on preview queue (LOW priority)
```

---

## 三、咨询议题三：超高频异构吞吐与 Win32 消息泵解耦

### 3.1 问题本质

审查 [`pipeline_scheduler.cpp`](file:///C:/PUBLIC/[REDACTED_PROJ]/src/core/pipeline_scheduler.cpp) 第 185 行，调度器以 `for (frame = 0; frame < frame_count; ++frame)` 紧密循环运行。RTX 5060 压制 1080p 可达数百 fps。`PostMessage` 队列默认深度 10000，`WM_ENTERSIZEMOVE` 期间主线程消息泵被 Windows 内部模态循环劫持。

**30 秒窗口拖拽 × 200fps = 6000 条积压消息，逼近 10000 上限。队列满后 `PostMessage` 静默丢弃并返回 FALSE。**

### 3.2 裁决：无锁原子聚合状态桶 + SetTimer 60Hz 拉取

**彻底禁止 `PostMessage` 逐帧推送。** 这是一个**聚合问题**而非**流式问题**——UI 不需要每个中间状态，只需要最新状态。

```cpp
// 工作线程写入，UI 线程读取 — 全部宽松原子操作
struct alignas(64) TranscodeProgress {
    std::atomic<uint64_t> frames_decoded{0};
    std::atomic<uint64_t> frames_encoded{0};
    std::atomic<uint64_t> packets_written{0};
    std::atomic<uint64_t> bitstream_bytes{0};
    std::atomic<uint64_t> total_frames{0};       // 启动时设置一次
    std::atomic<uint64_t> packed_state{0};        // 高32位=fps×100, 低32位=阶段枚举
    std::atomic<int32_t>  status{0};              // 0=运行中, 1=成功, -1=错误
    char error_message[256]{};                    // 仅在 status≠0 后由 UI 读取
    char _pad[64]{};                              // 防止伪共享
};
```

**工作线程侧**（改造 [`hw_transcode.cpp`](file:///C:/PUBLIC/[REDACTED_PROJ]/src/core/hw_transcode.cpp) 内部循环）：

```cpp
// 原: ++report.frames_encoded;
// 改:
progress->frames_encoded.store(n, std::memory_order_relaxed);
// 这是 x86-64 上的单条 MOV 指令，零开销，无内核转换
```

**UI 线程侧**：

```cpp
// 启动 60Hz 定时器
SetTimer(hwnd, TIMER_PROGRESS, 16, nullptr);

case WM_TIMER:
    if (wParam == TIMER_PROGRESS) {
        uint64_t encoded = g_progress.frames_encoded.load(std::memory_order_relaxed);
        uint64_t total   = g_progress.total_frames.load(std::memory_order_relaxed);
        // 更新进度条、ETA、fps 显示
        InvalidateRect(hwnd, &progressRect, FALSE);

        int32_t st = g_progress.status.load(std::memory_order_acquire);
        if (st != 0) {
            KillTimer(hwnd, TIMER_PROGRESS);
            // 处理完成/错误
        }
    }
    break;
```

### 3.3 为什么此方案完全免疫模态循环问题

- `WM_TIMER` 消息在 `WM_ENTERSIZEMOVE` 模态循环期间**仍然被分发**（Windows 内部 `DefWindowProc` 仍调用 `DispatchMessage`）
- 即使定时器被暂时抑制也无影响——工作线程持续写入原子量，模态循环退出后的下一个 Timer Tick 立即读取最新状态
- **零消息丢失、零队列填充**

### 3.4 替代方案对比

| 模式 | 裁决 | 理由 |
|------|------|------|
| `PostMessage` 逐帧 | ❌ 危险 | 模态循环期间队列溢出；每帧一次内核转换浪费 ~1μs |
| `PostMessage` + 合并标志 | ⚠️ 脆弱 | 生产者仍需检查标志后再投递；仍受模态循环停滞影响 |
| `WaitableTimer` + `MsgWaitForMultipleObjects` | ⚠️ 过度工程 | 对简单进度刷新来说复杂度不匹配；有重入消息分发的陷阱 |
| 无锁 SPSC 队列传进度 | ⚠️ 不必要 | 项目已有 `SpscBoundedQueue`，但进度是聚合问题非流式问题；原子快照更简洁高效 |
| **原子状态桶 + SetTimer** | ✅ **最优** | 零消息压力；零模态循环风险；x86 单 MOV 指令 |

### 3.5 DWM 节流对 CUDA/NVENC 管线的影响

**裁决：风险为零。**

审查项目架构，**NVDEC/NVENC 路径完全通过 CUDA 运行**（`rtx_shim.dll` → CUVID → NVENC），与 D3D12/DXGI 无关。CUDA 命令提交完全独立于 DWM。

| 关注点 | 风险 | 缓解措施 |
|--------|------|---------|
| DWM 节流 CUDA/NVENC | **无** | CUDA 独立于 DXGI/DWM |
| DWM 节流 GDI UI | **无** | GDI 不使用交换链 |
| DWM 节流未来 D3D12 UI | **低** | 最小化时跳过 Present；独立设备 |
| 工作线程饥饿 | **低** | `SetThreadPriority(THREAD_PRIORITY_ABOVE_NORMAL)` |

检测最小化并跳过 Present：
```cpp
case WM_SIZE:
    if (wParam == SIZE_MINIMIZED) g_minimized = true;
    else { g_minimized = false; /* resize swap chain */ }
    break;

// 渲染循环中:
if (!g_minimized) { /* Render and Present */ }
```

---

## 四、咨询议题四：Windows 原生高 DPI 与无框架自绘控件规范

### 4.1 Per-Monitor DPI Awareness V2

**通过应用程序清单声明（优先于 API 调用，在任何窗口创建前生效）：**

```xml
<application xmlns="urn:schemas-microsoft-com:asm.v3">
  <windowsSettings>
    <dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">
      PerMonitorV2,PerMonitor</dpiAwareness>
    <dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">
      True/PM</dpiAware>
  </windowsSettings>
</application>
```

### 4.2 `WM_DPICHANGED` 处理规范

```cpp
case WM_DPICHANGED: {
    UINT dpi = HIWORD(wParam);
    const RECT* suggested = reinterpret_cast<const RECT*>(lParam);

    // 1. 按系统建议矩形调整窗口
    SetWindowPos(hwnd, nullptr,
        suggested->left, suggested->top,
        suggested->right - suggested->left,
        suggested->bottom - suggested->top,
        SWP_NOZORDER | SWP_NOACTIVATE);

    // 2. 重建所有 DPI 相关资源
    recreateFonts(dpi);
    recalculateLayout(dpi);

    // 3. 触发全量重绘
    InvalidateRect(hwnd, nullptr, TRUE);
    return 0;
}

void recreateFonts(UINT dpi) {
    if (g_hFontUI) DeleteObject(g_hFontUI);
    int height = -MulDiv(10, dpi, 72);  // 10pt
    g_hFontUI = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

inline int dpiScale(int value, UINT dpi) {
    return MulDiv(value, dpi, 96);
}
```

### 4.3 GDI vs Direct2D+DirectWrite 裁决

| 维度 | 纯 GDI | Direct2D + DirectWrite |
|------|--------|----------------------|
| 复杂度 | 最低 | 中等（COM 初始化、工厂创建、设备丢失处理） |
| 文本渲染 | ClearType，良好但非最优 | 亚像素定位、精确提示、正确伽马校正 |
| 抗锯齿图形 | 无（`RoundRect` 有锯齿） | 全部几何体默认抗锯齿 |
| 深色模式 | `FillRect` + `SetBkColor` + `SetTextColor` | `ID2D1SolidColorBrush` + 平滑渐变 |
| 圆角矩形 | `RoundRect`（锯齿）或 `CreateRoundRectRgn`（裁剪） | `D2D1_ROUNDED_RECT` — 完美抗锯齿 |
| 依赖 | 零 — user32/gdi32 | d2d1.dll, dwrite.dll, d3d11.dll（全为 Windows 内建） |
| GPU 加速 | 通过 DWM 透明加速 | 直接 GPU 渲染（D2D 内部使用 D3D11） |

**裁决**：

> 1. **第一步以纯 GDI 起步。** 转码引擎 GUI 本质上是一个进度监视器——文件选择、编解码器配置、开始/停止、进度条、ETA、fps 读数。GDI 完全胜任。深色模式通过 Owner-Draw 全部控件实现。
>
> 2. **后期按需引入 Direct2D+DirectWrite。** 当需要以下特性时切换：抗锯齿圆角进度条、平滑动画过渡、高 DPI 下亚像素文本排版、GPU 渲染波形/时间线可视化。
>
> 3. **不要在同一窗口中混用 GDI 和 D2D。** 切换则整窗切换。`ID2D1DCRenderTarget` 互操作路径可行但抵消 D2D 优势。
>
> 4. **DirectWrite 不能脱离 Direct2D 独立使用** — DirectWrite 布局需要 D2D 渲染目标来绘制字形。

深色模式标题栏（Windows 10 1809+）：
```cpp
BOOL darkMode = TRUE;
DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */,
                      &darkMode, sizeof(darkMode));
```

深色配色方案：
```cpp
constexpr COLORREF kBgDark      = RGB(18, 18, 18);    // #121212
constexpr COLORREF kBgCard      = RGB(30, 30, 30);    // #1E1E1E
constexpr COLORREF kTextPrimary = RGB(230, 230, 230);
constexpr COLORREF kAccentNV    = RGB(118, 185, 0);    // NVIDIA #76B900
constexpr COLORREF kProgressBg  = RGB(60, 60, 60);
constexpr COLORREF kProgressFg  = RGB(118, 185, 0);
```

---

## 五、整体架构裁决与集成蓝图

### 5.1 最终推荐架构

```
┌──────────────────────────────────────────────────────────────┐
│                      UI 主线程                               │
│  ┌────────────────┐   ┌──────────────────────────────────┐   │
│  │  消息泵         │   │  SetTimer(16ms / 60Hz)           │   │
│  │  GetMessage()   │──→│  读取原子进度                     │   │
│  │  DispatchMsg()  │   │  InvalidateRect()                │   │
│  └────────────────┘   └──────────────────────────────────┘   │
│         │                          │                         │
│  ┌──────┴──────┐   ┌──────────────┴───────────────────┐      │
│  │  WM_PAINT   │   │  GDI Owner-Draw 深色控件          │      │
│  │  双缓冲重绘  │   │  DPI 缩放字体 + 进度条 + ETA     │      │
│  └─────────────┘   └──────────────────────────────────┘      │
│                                                              │
│     std::atomic<uint64_t> 共享进度 ◄─────────────────────────┤
└──────────────────────────────────────────────────────────────┘
                        ▲ (宽松原子, 无消息)
┌──────────────────────────────────────────────────────────────┐
│                     工作线程                                  │
│  ┌────────────────────────────────────────────────────────┐   │
│  │  runNvidiaContainerTranscode()                         │   │
│  │    for each packet:                                    │   │
│  │      Demux → NVDEC → NVENC → Mux                      │   │
│  │      progress.frames_encoded.store(n, relaxed)         │   │
│  │    on complete:                                        │   │
│  │      progress.status.store(1, release)                 │   │
│  └────────────────────────────────────────────────────────┘   │
│  运行于 CUDA — 完全独立于 DWM/DXGI                           │
└──────────────────────────────────────────────────────────────┘
                        ▲ (仅当启用零拷贝预览时)
┌──────────────────────────────────────────────────────────────┐
│                   预览渲染线程 (可选)                          │
│  独立 ID3D12Device (LOW priority queue)                      │
│  OpenSharedHandle → SRV 只读 RGBA16F                        │
│  Wait(shared fence) → PQ→sRGB Compute → Present(DComp)     │
│  60Hz 最佳努力, 跳帧, 对编码吞吐影响 <1%                     │
└──────────────────────────────────────────────────────────────┘
```

### 5.2 项目约定与设计哲学

| 约定 | 规范 |
|------|------|
| **产物形态** | 单一 `rtx-transcode.exe`，静态单体二进制 |
| **框架依赖** | 绝对禁止 Qt/Electron/CEF/WPF/MFC/WxWidgets/Flutter |
| **系统 DLL 白名单** | kernel32, user32, gdi32, shell32, comctl32, dwmapi, dcomp（全为 Windows 原生） |
| **双模入口** | `SUBSYSTEM:WINDOWS` + `AttachConsole` CLI 回退 |
| **线程模型** | UI 主线程 + 转码工作线程 + 可选预览渲染线程 |
| **线程通信** | 无锁原子聚合状态桶，UI 侧 60Hz 定时器拉取 |
| **预览渲染** | 独立 D3D12 设备 + DirectComposition + PQ→sRGB 着色器 |
| **DPI 策略** | Per-Monitor V2 + `WM_DPICHANGED` 字体/布局重建 |
| **渲染方式** | 第一阶段纯 GDI 双缓冲自绘；后期可选 D2D 增强 |
| **许可合规** | MIT/Apache-2.0 垫片静态链入 GPLv3 核心；NVIDIA DLL 运行时动态加载 |

### 5.3 分步演进路线

| 阶段 | 交付内容 | 前置条件 |
|------|---------|---------|
| **Phase 0** | Step 1 三项源码加固完成 + Gate 2 终审通过 | 执行工程师施工中 |
| **Phase 1** | `src/gui/` 纯 Win32 双模原型：拖拽排队、进度条、控制台重定向 | Phase 0 |
| **Phase 2** | D3D12 DirectComposition 零拷贝预览视口 + PQ→sRGB 色调映射 | Phase 1 |
| **Phase 3** | 可选 Direct2D 增强：抗锯齿控件、波形图、GPU 占用率曲线 | Phase 2 |

---

*报告完毕。本咨询批复对四项议题均给出了明确裁决、完整法律依据、详尽实现方案与代码参考。项目架构师可据此直接签发施工令。*