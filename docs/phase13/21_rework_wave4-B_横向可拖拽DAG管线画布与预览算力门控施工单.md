# **21\_rework\_wave4-B\_横向可拖拽DAG管线画布与预览算力门控施工单**

## **1\. 元信息与驳回定性复盘**

### **1.1 元信息**

* **波次定位**：Rework Wave 4-B（与 Rework Wave 4-A 并行，Phase 13 终局合龙波次：彻底补实原 Wave 4-B 未开工缺口，交付类 Premiere 横向可拖拽排序管线卡片画布、显式拓扑契约与显存推演编译器、以及真正节省 GPU 算力的转码预览门控 CheckBox）。  
* **依据裁决**：`CINERTX-P13-RULING-013` 第 13 号文档（协调负责人驳回裁决书）。  
* **硬性架构红线**：  
  * **零新增具名类型**：`AuditTypes <= 35/35` 极限界限保持不动。管线节点序列使用定长 `std::array<uint8_t, 8>` 算子 ID 数组 \+ `uint16_t` 旁路掩码嵌入既有 `TranscodeProfile` 结构中；局部栈上返回值使用 `std::tuple` 或现有状态槽位，**绝不新增任何具名 struct/class**。  
  * **静态 IAT 零新增 DLL**：禁止引入外置拖拽 GUI 库或 D3D 框架，画布全部依托 Rework Wave 4-A 的 Direct2D / DirectWrite / DXGI 自绘基础设施实现。

### **1.2 驳回定性复盘**

原 Wave 4-B（11 号文档）被协调负责人裁决为 **"未开工（NOT STARTED）"**，其根本缺陷复盘如下表所示：

| 裁决维标 | 原 11 号文档（被驳回版本）缺陷 | Rework Wave 4-B 纠偏重构方案 |
| :---- | :---- | :---- |
| **拓扑编译器** | `src/core/hw_transcode_stages.cpp:92-95` 存在硬编码的"覆盖分支"，在放大场景下悄悄无视用户选择，强制将顺序篡改重写为 `fg-first`。 | 彻底废除 `hw_transcode_stages.cpp:92-95` 硬编码覆盖；实现 `evaluatePipelinePlan` 拓扑编译器，严格遵循用户自定义算子序列与旁路开关。 |
| **画布交互** | 无可拖拽画布，仅在 UI 放置静态下拉框，完全无法表现 DAG 拓扑节点流转与张量规格变化。 | 交付 `PipelineCardCanvas` 控件，包含固定源/输出端点、中间横向拖拽平滑让位卡片、实时张量规格（分辨率/帧率/色域）标签与 Bypass 切换角标。 |
| **预览算力门控** | GUI 的"预览开关"仅改变视口渲染控件的隐藏/显示状态，后台 CUDA/NGX/TRT 算子与 `pack_preview_rgba10` Kernel、共享内存同步仍全速运行，未节省任何 GPU 算力。 | 贯通 L4 UI 至 L1 GPU 调度全链路：取消勾选时立即熔断 `pack_preview_rgba10` Kernel 执行、暂停 D3D12 Fence 同步并释放 preview 临时显存，GPU 算力 100% 倾斜供给后台编码。 |

---

## **2\. Task 1：废除隐式顺序覆盖，实装泛型算子调度与显存/色域拓扑编译器**

精确代码坐标：`src/core/hw_transcode_stages.cpp`、`include/rtx/param_schema.h`、`src/core/playback_session_video.cpp`。

### **Step 1.1 扩充 `TranscodeProfile` 定长管线描述符（零新增类型）**

在 `include/rtx/param_schema.h` 内既有的 `TranscodeProfile` 结构体结尾扩充字段，保持静态类型总量不变：

```
// 算子常量枚举标识（占用 uint8_t）
constexpr uint8_t OP_NONE     = 0;
constexpr uint8_t OP_FRUC     = 1; // AI 插帧 (RIFE)
constexpr uint8_t OP_VSR      = 2; // AI 超分 (VSR)
constexpr uint8_t OP_TRUE_HDR = 3; // SDR 转 HDR10 PQ
constexpr uint8_t OP_LUT3D    = 4; // 3D LUT 色彩校正

// 修改 TranscodeProfile（仅在既有结构体内增加定长成员，不增加新 struct/class）
struct TranscodeProfile {
    // ... 既有字段保持不变 ...

    // Rework Wave 4-B 新增：定长管线节点序列（最大支持 8 个节点）
    std::array<uint8_t, 8> pipeline_nodes = { OP_FRUC, OP_VSR, OP_TRUE_HDR, OP_LUT3D, 0, 0, 0, 0 };
    // 按位旁路掩码（bit 0 对应 pipeline_nodes[0]，1 表示 bypass，0 表示 active）
    uint16_t pipeline_bypass_mask = 0x0000;
};
```

JSON/CLI 序列化机制无缝映射 `pipeline_nodes` 为整数数组 `"pipeline_nodes": [1, 2, 3, 4]`，`pipeline_bypass_mask` 序列化为整数值。

### **Step 1.2 实装公开拓扑契约编译器 `evaluatePipelinePlan`**

1. **废除硬编码**：彻底从 `src/core/hw_transcode_stages.cpp` 中删除 `92-95` 行隐式重写代码：

```
// 彻底废除以下错误逻辑：
// if (scale_factor > 1.0f && profile.pipeline_order != PipelineOrder::FG_FIRST) {
//     profile.pipeline_order = PipelineOrder::FG_FIRST; // BAD: 悄悄覆盖用户决策
// }
```

2. **拓扑契约与色域/显存推演算法**：  
   在 `src/core/hw_transcode_stages.cpp` 中实现无状态自由推演函数 `evaluatePipelinePlan`（返回值使用 `std::tuple` 或已有状态变量，确保不触发 `AuditTypes`）：

```
std::tuple<bool, uint32_t, uint32_t, float, uint16_t> evaluatePipelinePlan(
    const TranscodeProfile& profile,
    uint32_t src_w, uint32_t src_h, float src_fps, bool src_is_hdr) 
{
    uint32_t cur_w = src_w;
    uint32_t cur_h = src_h;
    float cur_fps = src_fps;
    bool cur_is_hdr = src_is_hdr;
    
    uint16_t effective_bypass_mask = profile.pipeline_bypass_mask;
    size_t peak_vram_bytes = 0;
    
    // 遍历管线节点推演状态
    for (size_t i = 0; i < profile.pipeline_nodes.size(); ++i) {
        uint8_t op = profile.pipeline_nodes[i];
        if (op == OP_NONE) break;
        
        bool is_bypassed = (effective_bypass_mask & (1 << i)) != 0;
        
        switch (op) {
            case OP_FRUC: {
                if (!is_bypassed) {
                    float factor = profile.rife_target_fps / cur_fps;
                    cur_fps = profile.rife_target_fps;
                    // 推演显存：FRUC 在当前分辨率下的纹理与中间缓存
                    size_t frame_mem = cur_w * cur_h * 4 * 2; // NV12/RGBA 帧缓冲
                    peak_vram_bytes = (std::max)(peak_vram_bytes, frame_mem * 4);
                }
                break;
            }
            case OP_VSR: {
                if (!is_bypassed) {
                    float scale = profile.vsr_scale_factor;
                    cur_w = static_cast<uint32_t>(cur_w * scale);
                    cur_h = static_cast<uint32_t>(cur_h * scale);
                    // 放大后算子显存需求呈 s^2 暴涨
                    size_t vsr_workspace = cur_w * cur_h * 4 * 6; // TensorRT Workspace 估算
                    peak_vram_bytes = (std::max)(peak_vram_bytes, vsr_workspace);
                }
                break;
            }
            case OP_TRUE_HDR: {
                // 色域/HDR 自动衔接规则：若输入源本身已是 HDR10 PQ，自动激活 Auto-Bypass
                if (cur_is_hdr) {
                    effective_bypass_mask |= (1 << i); // 自动屏蔽冗余 TrueHDR 节点
                } else if (!is_bypassed) {
                    cur_is_hdr = true; // 转化为 PQ 线性色彩空间
                }
                break;
            }
            case OP_LUT3D: {
                // 3D LUT 色彩转换处理
                break;
            }
        }
    }

    // 显存水位校验与安全降级推演
    constexpr size_t VRAM_SAFE_WATERMARK = 6ULL * 1024 * 1024 * 1024; // 6GB 安全水位
    bool vram_warning = (peak_vram_bytes > VRAM_SAFE_WATERMARK);

    return { vram_warning, cur_w, cur_h, cur_fps, effective_bypass_mask };
}
```

### **Step 1.3 泛型循环算子调度器**

重构 `src/core/hw_transcode_stages.cpp` 中的转码主循环，摒弃固化的 `if (use_fruc) ... if (use_vsr) ...` 结构，改为基于拓扑序列的动态分发调度：

```
void executePipelineStages(TranscodeExecutionContext* ctx) {
    auto& nodes = ctx->profile.pipeline_nodes;
    uint16_t mask = ctx->effective_bypass_mask;

    for (size_t i = 0; i < nodes.size(); ++i) {
        uint8_t op = nodes[i];
        if (op == OP_NONE) break;
        if ((mask & (1 << i)) != 0) continue; // 节点被 Bypass，跳过执行

        switch (op) {
            case OP_FRUC:
                dispatchRIFEKernel(ctx);
                break;
            case OP_VSR:
                dispatchVSRKernel(ctx);
                break;
            case OP_TRUE_HDR:
                dispatchTrueHDRKernel(ctx);
                break;
            case OP_LUT3D:
                dispatch3DLUTKernel(ctx);
                break;
        }
    }
}
```

---

## **3\. Task 2：交付类 Premiere 横向可拖拽排序管线卡片画布（`PipelineCardCanvas`）**

基于 Rework Wave 4-A 的 Direct2D / DirectWrite / DXGI 零依赖基础设施自绘 UI 控件。

```
+-------------------------------------------------------------------------------------------------------------------------+
| [ Source: 1080p@24 SDR ]  --->  [ AI 插帧 (RIFE) ]  --->  [ AI 超分 (VSR) ]  --->  [ TrueHDR ]  --->  [ Encoder: SVT-AV1 ] |
| (Locked Start Node)             (Bypass Corner)           (Selected Node)         (Auto-Bypassed)      (Locked End Node)   |
|                                 Out: 1080p@60             Out: 4K@60              Out: 4K@60 PQ                         |
+-------------------------------------------------------------------------------------------------------------------------+
```

### **3.1 渲染与布局数据流**

`PipelineCardCanvas` 控件维护内部布局描述，不引入新 class，利用数组与结构化绘制逻辑在 Windows `WM_PAINT` / `ID2D1RenderTarget` 上呈现：

1. **锚点节点渲染**：  
   * **左端锁定节点**：绘制带有淡蓝色边框的 `Source` 卡片，文本显示输入源物理规格（如 `1080p@23.976 SDR`）。不接受拖拽指令。  
   * **右端锁定节点**：绘制带有淡绿色边框的 `Encoder` 卡片，文本显示输出编码配置（如 `SVT-AV1 CRF 28`）。不接受拖拽指令。  
2. **中间可拖拽算子卡片渲染**：  
   * 遍历 `pipeline_nodes` 中非 0 的算子，依次绘制包含背景矩形、算子标题、`Bypass` 复选角标、底部张量规格（如 `1080p@60 -> 4K@60`）的卡片。  
   * **动态矢量箭头**：在相邻卡片之间使用 Direct2D 绘制连线箭头 `ID2D1GeometrySink`，并标示当前线段上的像素流速。  
   * **Auto-Bypass 徽标**：被 `evaluatePipelinePlan` 推演为自动旁路的卡片（如输入已是 HDR 时的 `TrueHDR` 卡片），叠加半透明琥珀色 `Auto-Bypassed` 状态标签。

### **3.2 拖拽交互与平滑让位算法**

在 Win32 消息循环中响应鼠标事件 (`WM_LBUTTONDOWN`, `WM_MOUSEMOVE`, `WM_LBUTTONUP`)：

```
// 拖拽逻辑伪代码实现 (嵌入既有 GUI 窗口过程 WndProc)
static int g_dragged_index = -1;
static POINT g_drag_offset = {0, 0};
static int g_hover_insert_index = -1;

void HandlePipelineCanvasLButtonDown(HWND hwnd, POINT pt, TranscodeProfile& profile) {
    // 检查是否击中 Bypass 角标
    for (int i = 0; i < GetActiveNodeCount(profile); ++i) {
        RECT badge_rect = GetNodeBypassBadgeRect(i);
        if (PtInRect(&badge_rect, pt)) {
            profile.pipeline_bypass_mask ^= (1 << i); // 切换 Bypass 状态
            InvalidateRect(hwnd, NULL, FALSE);
            return;
        }
    }
    
    // 检查是否击中中间可拖拽节点
    int hit_index = HitTestPipelineNodes(pt);
    if (hit_index >= 0) {
        g_dragged_index = hit_index;
        g_drag_offset.x = pt.x - GetNodeRect(hit_index).left;
        SetCapture(hwnd);
    }
}

void HandlePipelineCanvasMouseMove(HWND hwnd, POINT pt, TranscodeProfile& profile) {
    if (g_dragged_index >= 0) {
        // 计算当前鼠标位置对应的目标插入索引 (0 ~ active_count - 1)
        g_hover_insert_index = CalculateInsertIndex(pt.x);
        InvalidateRect(hwnd, NULL, FALSE); // 触发重绘，绘制虚线槽与卡片让位动画
    }
}

void HandlePipelineCanvasLButtonUp(HWND hwnd, POINT pt, TranscodeProfile& profile) {
    if (g_dragged_index >= 0) {
        ReleaseCapture();
        if (g_hover_insert_index >= 0 && g_hover_insert_index != g_dragged_index) {
            // 重构 pipeline_nodes 数组元素顺序
            uint8_t moved_op = profile.pipeline_nodes[g_dragged_index];
            // 移出原位置
            for (int i = g_dragged_index; i < 7; ++i) {
                profile.pipeline_nodes[i] = profile.pipeline_nodes[i + 1];
            }
            // 插入新位置
            for (int i = 7; i > g_hover_insert_index; --i) {
                profile.pipeline_nodes[i] = profile.pipeline_nodes[i - 1];
            }
            profile.pipeline_nodes[g_hover_insert_index] = moved_op;
        }
        g_dragged_index = -1;
        g_hover_insert_index = -1;
        InvalidateRect(hwnd, NULL, FALSE);
    }
}
```

---

## **4\. Task 3：落地真·省算力转码预览门控 CheckBox**

### **4.1 全链路算力与显存控制架构**

彻底摒弃"仅隐藏 UI 视口"的虚假开关，构建从 L4 UI CheckBox 直达 L1 GPU Kernel 与 DirectX12 / CUDA 算力执行层的真实控制链路：

```
[ L4 GUI: Live Preview CheckBox ]
                |
                v (std::atomic<bool> preview_enabled)
+-----------------------------------------------------------------------+
| L1 Transcode Engine Pipeline Loop                                    |
|                                                                       |
|  [ Decode ] --> [ Dynamic DAG Operators ] --> [ Encode Push ]         |
|                                   |                                   |
|                                   +---> if (preview_enabled.load())   |
|                                   |         |                         |
|                                   |         +-- YES: [ pack_preview ] |
|                                   |         |        [ Copy Shared ]  |
|                                   |         |        [ Present ]      |
|                                   |         |                         |
|                                   |         +-- NO:  [ SKIP ALL ]     |
|                                   |                  [ Free Buffer ]  |
+-----------------------------------------------------------------------+
```

### **4.2 L1 / L2 算子层熔断实现**

在 `src/core/playback_session_video.cpp` 与 CUDA 渲染管线中实现门控逻辑：

```
void ProcessTranscodeFrame(TranscodeSessionContext* session, CUdeviceptr gpu_output_surface) {
    // 1. 执行编码器帧推送 (核心转码任务，不受预览门控影响)
    PushFrameToEncoder(session->encoder_handle, gpu_output_surface);

    // 2. 检查预览门控 CheckBox 状态 (零锁原子操作)
    if (!session->preview_enabled.load(std::memory_order_relaxed)) {
        // --- 预览已关闭：100% 熔断预览开销 ---
        if (session->preview_gpu_buffer != 0) {
            // 立即释放临时 RGBA10/NV12 预览显存缓冲，降低 GPU 显存占用
            cudaFree((void*)session->preview_gpu_buffer);
            session->preview_gpu_buffer = 0;
        }
        // 跳过 pack_preview_rgba10 CUDA Kernel 调度
        // 跳过 cudaMemcpy2DAsync / 共享句柄跨进程复制
        // 跳过 D3D12 External Fence 信号量同步与 D3D12Presenter::Present
        return; 
    }

    // --- 预览已开启：按 <= 30Hz 节流分发预览 ---
    if (session->preview_gpu_buffer == 0) {
        // 重新按需分配预览显存缓冲
        cudaMalloc((void**)&session->preview_gpu_buffer, session->preview_pitch * session->height);
    }

    if (ShouldRenderPreviewFrame(session->last_preview_timestamp, 30 /* Hz Limit */)) {
        // 执行 CUDA pack_preview_rgba10 Kernel 打包
        launchPackPreviewRGBA10Kernel(gpu_output_surface, session->preview_gpu_buffer, session->width, session->height);
        // 执行 D3D12 共享句柄 Presents 呈现
        SubmitPreviewToD3D12Presenter(session);
    }
}
```

### **4.3 视口 UI 状态联动**

当 `preview_enabled == false` 时，UI 视口渲染线程暂停刷新，保持呈现最后一帧静态图像，并在视口中央使用 Direct2D 叠加半透明磨砂遮罩与文本提示：

```
// 视口绘制逻辑
if (!profile.preview_enabled) {
    DrawStaticLastFrame(rt);
    DrawOverlayText(rt, L"预览已暂停（GPU 算力 100% 供给后台全速编码）", COLOR_AMBER_WARNING);
}
```

---

## **5\. Phase 13 终局全仓静态门禁与真机全链路封板清单**

完成上述代码重构后，必须严格依次执行以下构建与验证步骤，确保全仓静态门禁无瑕疵通过。

### **5.1 构建与单元测试验证**

在终端中依次执行脚本：

```
:: 1. 编译全仓代码
.\build.bat
:: 预期输出：退出码 0，无 Warning/Error

:: 2. 运行单元测试套件
.\bin\rtx_tests.exe
:: 预期输出：[PASSED] ALL TESTS (0 FAILED)
```

### **5.2 CMake 静态五重门禁真机全链路执行与验证结果**

执行 CMake 静态检查目标：

```
cmake --build build --target check_all_static_rules
```

真机现场验证原文输出必须严格符合以下要求（缺一不可）：

```
[CHECK 1/5] AuditTypes Rule Check...
  -> Total explicit struct/class count: 35
  -> Limit: <= 35
  [PASSED] AuditTypes <= 35/35 satisfied. (Zero new types introduced in Wave 4-B)

[CHECK 2/5] AuditImports Rule Check...
  -> GUI Binary IAT Imports: 4 (KERNEL32.dll, USER32.dll, D2D1.dll, D3D12.dll)
  -> Headless Binary IAT Imports: 2 (KERNEL32.dll, CUDA_DRIVER.dll)
  [PASSED] AuditImports satisfied.

[CHECK 3/5] AuditSloc Rule Check...
  -> Total Source Lines of Code (SLOC): 22,410
  -> Limit: < 23,000
  [PASSED] AuditSloc < 23,000 satisfied.

[CHECK 4/5] AuditSpdx Rule Check...
  -> Missing SPDX Headers: 0
  [PASSED] AuditSpdx == 0 satisfied.

[CHECK 5/5] AuditPrivacy Rule Check...
  -> Unauthorized Telemetry / Network Calls: 0
  [PASSED] AuditPrivacy == 0 satisfied.

===============================================================================
[RESULT] ALL 5 STATIC CHECKS PASSED SUCCESSFULLY. READY FOR P13 MERGE.
===============================================================================
```

