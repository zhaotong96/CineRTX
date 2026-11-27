# **41\_fix\_wave1-A\_播放器单线垂直截断定界与三段链路修复施工单**

## **1\. 施工元数据与基线硬红线**

### **施工元数据**

* **施工波次**：`Fix Wave 1 - Stage A`（与 `Fix Wave 1-B` 并行推进：根治问题 1——Player 工作区视频预览区仅顶部数像素高度呈单线状、其余区域不可见或异常）。  
* **基线锁定**：Commit `d18b469`（`150/150` 单测 PASS、`AuditTypes = 34/35`、`AuditImports` GUI `<= 4` / HEADLESS `== 2`）。  
* **依据文件**：39 号委托书与 40 号专家方案。

### **宪法级硬红线**

| 维度 | 红线指标 | 约束实现方式 |
| :---- | :---- | :---- |
| **类型收敛** | **零新增具名类型** | 严格保持 `AuditTypes == 34/35`（`+0` 具名 `struct`/`class`/`enum`）。所有新增元数据通过 `uint32_t` 位掩码、标量出参及既有类内私有 POD 成员传递。 |
| **依赖依赖** | **零新增静态 DLL** | 严格保持 `AuditImports` GUI `<= 4` / HEADLESS `== 2`（`+0` 静态导入库）。 |
| **性能防线** | **零 GPU-\>CPU 像素回读** | 彻底禁止在渲染/播放主帧路径上调用任何同步回读函数。 |

---

## **2\. 渲染机理辨析与三段链路故障坐标**

### **渲染机理物理判据**

当预览区出现"仅顶部数像素高度显示正常比例画面，其余区域全黑或异常消失"现象时，可通过以下物理判据区分故障性质：

* **反例（非采样坐标拉伸）**：若仅是 `srcHeight` 被错误传入 1\~8 的小整数，而 GPU 采样阶段仍按照全高度 UV 展开，屏幕将呈现第 0\~7 行像素在垂直方向被拉伸填满整个视口（即"垂帘条纹"）。  
* **正例（两类垂直截断）**：若画面仅占顶部数像素且下方完全不显示或呈纯黑色，物理上必归结为以下两类截断机制之一：  
  1. **数据源 / 着色器边界截断**：下方像素行未被 CUDA 实际写入、由于 D3D12 SRV `Buffer.NumElements` 不足导致 DXIL 硬件读取越界自动补零（`0x00000000` 纯黑），或 `Dispatch(dx, dy, 1)` / `drawSize` 计算陷入无效小区域。  
  2. **桌面合成 / 窗口树截断**：Win32 兄弟控件未设置隐藏，导致 DirectComposition 第 3 层子窗口跨层物理遮挡第 2 层视觉树；或者 SwapChain / `SetClip` 的实际渲染高度停留在数像素尺寸。

### **三段链路精确代码坐标**

* **段 1（无头播放环生产端）**：`src/shim/ngx_bridge/bridge_exports.cpp`  
  * `MediaPlaybackSession` 在 `reserved[0] = 1` 时调用 `rtx_ngx_bridge_create` 建立 3-slot 共享预览环。  
  * 每帧由 `rtx_ngx_bridge_playback_pack()` 调用 `rtx_ngx::launchPreprocessBuffer` 写入 `previewRingDevicePtr(slot)`，传参包含 `previewRingRowBytes()`、`width`、`height`。  
* **段 2（视口借用与 D3D12 CS 消费端）**：`src/gui/workstation_canvas.cpp` & `src/gui/preview_viewport.cpp` & `src/gui/shaders/preview_tone_map_cs.hlsl`  
  * 调用路径：`presentPlaybackFrame()` / `presentSlotToViewport()` \$\\rightarrow\$ `PreviewViewport::presentShared()` \$\\rightarrow\$ `preview_tone_map_cs.hlsl`（`gPreview[clamped.y * pitchWords + clamped.x]`）。  
* **段 3（Win32 布局与 DComp 合成端）**：`src/gui/rtx_gui.cpp` & `src/gui/preview_viewport.cpp`  
  * `applyWorkspaceLayout()` 内 `isPlayer` 分支中 `previewViewport->setBounds(0, bodyTop, width, std::max(u(80), vpH))`、`WM_CREATE` 窗口样式分配，以及子窗口 Z-Order 层级。

---

## **3\. Task 1：5 分钟三段探针定界（TDD 故障锁定）**

在进行实际代码修复前，按以下三段注入临时诊断探针，定位垂直截断的具体环节：

### **Probe-1A（数据源生产端 \- `src/shim/ngx_bridge/bridge_exports.cpp`）**

在 `rtx_ngx_bridge_playback_pack()` 与 `borrow_preview()` 入口及出口添加日志探针：

```
// 在 rtx_ngx_bridge_playback_pack 内
uint32_t ring_row_bytes = session->previewRingRowBytes();
uint32_t grid_x = (width + 15) / 16;
uint32_t grid_y = (height + 15) / 16;
printf("[PROBE-1A-PACK] frame=%llu w=%u h=%u ring_bytes=%zu row_bytes=%u grid=(%u,%u)\n",
       (unsigned long long)session->frameCount(), width, height,
       session->previewRingSizeBytes(), ring_row_bytes, grid_x, grid_y);

// 在 rtx_ngx_bridge_borrow_preview 内
printf("[PROBE-1A-BORROW] slot=%u ptr=%p out_w=%u out_h=%u out_pitch=%u\n",
       slot_idx, *out_dev_ptr, *out_width, *out_height, *out_pitch);
```

* **核验目标**：检测是否存在 `grid_y == 1`、`height < 16`、环总字节数小于 `row_bytes * height`，或字节跨度（byte pitch）与字跨度（word pitch）混淆问题。

### **Probe-1B（消费端与 SRV 绑定 \- `src/gui/preview_viewport.cpp`）**

在 `PreviewViewport::presentShared()` 中重建 SRV 及发起 `Dispatch` 前注入探针：

```
uint32_t pitchWords = out_pitch / 4u;
uint32_t requiredElements = pitchWords * srcH;
printf("[PROBE-1B-CS] srcW=%u srcH=%u out_pitch=%u pitchWords=%u NumElements=%u swap=(%u,%u) Dispatch=(%u,%u)\n",
       srcW, srcH, out_pitch, pitchWords, requiredElements, swapWidth_, swapHeight_,
       (swapWidth_ + 15) / 16, (swapHeight_ + 15) / 16);
```

* **核验目标**：检测是否存在 SRV 的 `Buffer.NumElements < pitchWords * srcH`（导致第 1 行以下的坐标读取越界被 DXIL 自动填零）或 `Dispatch` 组数 `dy == 1`。

### **Probe-1C（Win32 窗口树与 DComp 合成 \- `src/gui/rtx_gui.cpp` & `preview_tone_map_cs.hlsl`）**

在 `applyWorkspaceLayout()` 触发后，打印窗口物理尺寸，并在着色器中注入调色板校验：

```
// rtx_gui.cpp
RECT wr, cr;
GetWindowRect(child_hwnd_, &wr);
GetClientRect(child_hwnd_, &cr);
printf("[PROBE-1C-WIN32] HWND=%p WindowRect=(%ld,%ld,%ld,%ld) ClientRect=(%ld,%ld)\n",
       child_hwnd_, wr.left, wr.top, wr.right, wr.bottom, cr.right - cr.left, cr.bottom - cr.top);
```

**色块探针试验**：  
临时在 `preview_tone_map_cs.hlsl` 输出末尾将最终颜色强制写为纯青色 `float4(0.0, 1.0, 1.0, 1.0)`：

* **判定 1**：若视口画面**依然仅有顶部数像素呈纯青色**，其余区域依然黑屏或缺失 \$\\rightarrow\$ **100% 锁定段 3**（Win32 兄弟控件物理遮挡或 DirectComposition/SwapChain 高度截断）。  
* **判定 2**：若视口画面**整体变为全框纯青色** \$\\rightarrow\$ **100% 锁定段 1 或段 2**（CUDA 未写入有效行或 SRV `NumElements` 越界填零）。

---

## **4\. Task 2：三段链路全覆盖防御性契约修复**

### **Step 2.1（统一无头播放环分配与 2D CUDA 写入网格）**

修改 `src/shim/ngx_bridge/bridge_exports.cpp`，确保 3-slot 共享预览环按真实动态尺寸扩展，且 CUDA Preprocess 逻辑严格应用 2D 线程网格。

```
// 1. 确保环缓冲区按 256 字节对齐计算 row_bytes 并动态重建
uint32_t alignUp(uint32_t val, uint32_t align) {
    return (val + align - 1) & ~(align - 1);
}

// 2. 播放打包函数中的环缓冲区校验与 2D Kernel 调度
NVSDK_NGX_Result rtx_ngx_bridge_playback_pack(...) {
    // ...
    uint32_t row_bytes = alignUp(width * 4u, 256u);
    size_t single_frame_bytes = static_cast<size_t>(row_bytes) * height;

    // 若当前帧要求尺寸大于已分配槽位大小，先进行 GPU 同步并重分配
    if (session->previewRingSizeBytes() < single_frame_bytes) {
        cudaStreamSynchronize(session->stream());
        session->reallocatePreviewRing(single_frame_bytes, width, height, row_bytes);
    }

    void* dev_ptr = session->previewRingDevicePtr(current_slot);
    
    // 强制按 2D 网格启动 CUDA 核函数，杜绝 grid.y == 1
    dim3 block(16, 16, 1);
    dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y, 1);

    rtx_ngx::launchPreprocessBuffer(
        session->stream(),
        input_tensor,
        dev_ptr,
        width,
        height,
        row_bytes, // 显式传递 256 字节对齐的行字节数
        grid,
        block
    );
    // ...
}
```

### **Step 2.2（统一 `out_pitch` 行字节量纲与 D3D12 SRV `Buffer.NumElements` 边界）**

修改 `src/gui/preview_viewport.cpp`，统一 `out_pitch` 的含义为**字节跨度（Row Bytes）**，并修复 SRV 与 CS Dispatch 维度。

```
void PreviewViewport::presentShared(void* devPtr, uint32_t srcW, uint32_t srcH, uint32_t out_pitch) {
    if (!devPtr || srcW == 0 || srcH == 0) return;

    // 契约断言：out_pitch 必须为字节跨度，且 4 字节对齐
    assert(out_pitch >= srcW * 4u);
    assert((out_pitch % 4u) == 0);

    uint32_t pitchWords = out_pitch / 4u;
    uint32_t totalElements = pitchWords * srcH;

    // 重建共享 SRV 资源视图
    if (srvElementCount_ < totalElements || !srvHeap_) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = totalElements; // 完整覆盖 pitchWords * srcH，防止越界读零
        srvDesc.Buffer.StructureByteStride = 4;     // R8G8B8A8Packed (32-bit word)

        device_->CreateShaderResourceView(sharedBufferResource_.Get(), &srvDesc, srvCpuHandle_);
        srvElementCount_ = totalElements;
    }

    // 更新 CBV 常量缓冲区
    PreviewCB cbData = {};
    cbData.srcWidth = srcW;
    cbData.srcHeight = srcH;
    cbData.pitchWords = pitchWords;
    cbData.dstWidth = swapWidth_;
    cbData.dstHeight = swapHeight_;
    updateConstantBuffer(&cbData, sizeof(cbData));

    // 计算 Dispatch 组数：按 Render Target (swapWidth_, swapHeight_) 满屏覆盖
    const uint32_t TG_X = 16;
    const uint32_t TG_Y = 16;
    uint32_t dispatchX = (swapWidth_ + TG_X - 1) / TG_X;
    uint32_t dispatchY = (swapHeight_ + TG_Y - 1) / TG_Y;

    commandList_->Dispatch(dispatchX, dispatchY, 1);
}
```

在 `src/gui/shaders/preview_tone_map_cs.hlsl` 中核验坐标换算关系：l  
cbuffer PreviewConfig : register(b0) {  
uint gSrcWidth;  
uint gSrcHeight;  
uint gPitchWords;  
uint gDstWidth;  
uint gDstHeight;  
};

StructuredBuffer gPreview : register(t0);  
RWTexture2D gOutput : register(u0);

\[numthreads(16, 16, 1)\]  
void main(uint3 dispatchThreadID : SV\_DispatchThreadID) {  
if (dispatchThreadID.x \>= gDstWidth \|\| dispatchThreadID.y \>= gDstHeight) {  
return;  
}

```
// 计算屏幕坐标到源视频帧坐标的 Aspect-Fit 映射
float2 uv = (float2(dispatchThreadID.xy) + 0.5) / float2(gDstWidth, gDstHeight);
uint srcX = uint(clamp(uv.x * gSrcWidth, 0.0, float(gSrcWidth - 1)));
uint srcY = uint(clamp(uv.y * gSrcHeight, 0.0, float(gSrcHeight - 1)));

// 严禁使用 gSrcWidth 计算索引，必须使用 gPitchWords！
uint bufferIndex = srcY * gPitchWords + srcX;
uint rawColor = gPreview[bufferIndex];

float4 color = unpackUnorm4x8(rawColor);
gOutput[dispatchThreadID.xy] = color
```

;  
}

````

### Step 2.3（清理 Player 工作区兄弟窗口 Z-Order 与样式隔离）
修改 `src/gui/rtx_gui.cpp`，治理窗口层级与样式隔离，消除 DirectComposition / Win32 子窗口跨层遮挡。

```cpp
// 1. 在主窗口及 Canvas 创建阶段确立窗口裁剪样式
void RtxGui::initWindowStyles(HWND mainHwnd, HWND childViewportHwnd) {
    // 主窗口启用 WS_CLIPCHILDREN，防止主窗口重绘覆盖子窗口
    LONG_PTR mainStyle = GetWindowLongPtr(mainHwnd, GWL_STYLE);
    SetWindowLongPtr(mainHwnd, GWL_STYLE, mainStyle | WS_CLIPCHILDREN);

    // 预览子窗口启用 WS_CLIPSIBLINGS，防止同级窗口重叠
    LONG_PTR childStyle = GetWindowLongPtr(childViewportHwnd, GWL_STYLE);
    SetWindowLongPtr(childViewportHwnd, GWL_STYLE, childStyle | WS_CLIPSIBLINGS | WS_CHILD | WS_VISIBLE);
}

// 2. 在 applyWorkspaceLayout 的 isPlayer 分支执行严格样式与显隐隔离
void RtxGui::applyWorkspaceLayout(bool isPlayer, int width, int height) {
    if (isPlayer) {
        // 显式隐藏 Encoder 工作区独有的左侧 Inspector 和右侧 Queue Panel
        if (hwndLeftInspector_) ShowWindow(hwndLeftInspector_, SW_HIDE);
        if (hwndRightQueue_)    ShowWindow(hwndRightQueue_, SW_HIDE);

        int bodyTop = getTopBarHeight();
        int controlBarHeight = getPlayerControlBarHeight();
        int vpH = height - bodyTop - controlBarHeight;
        if (vpH < 80) vpH = 80;

        // 重新设定 Player 视口位置
        SetWindowPos(child_hwnd_, HWND_TOP, 0, bodyTop, width, vpH, SWP_NOACTIVATE | SWP_SHOWWINDOW);

        // 显示 Player 底部控制条，并确保其布局在视口下方，绝对不相交
        if (hwndPlayerControlBar_) {
            SetWindowPos(hwndPlayerControlBar_, HWND_TOP, 0, bodyTop + vpH, width, controlBarHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }

        // 刷新 PreviewViewport 内 SwapChain 与 DComp 尺寸
        if (previewViewport_) {
            previewViewport_->setBounds(0, bodyTop, width, vpH);
        }
    } else {
        // Encoder 模式逻辑
        // ...
    }
}
````

---

# **5\. 验收 Gate（本波次退出判据）**

## **自动化与架构约束检查**

1. **类型零新增校验**：编译并运行审计脚本，校验 `AuditTypes == 34/35`（`+0` 具名 `struct`/`class`/`enum`）。  
2. **静态库零新增校验**：校验 `AuditImports` GUI `<= 4` / HEADLESS `== 2`（`+0` 静态导入库）。  
3. **单测回归**：全套 `150/150` C++ 单元测试全数 PASS。

## **功能与视觉验收标准**

1. **多分辨率与宽高比支持**：  
   * 在 Player 工作区中分别加载并播放 `16:9 (1080p)`、`9:16 竖屏 (720x1280)` 以及 `21:9 超宽屏` 媒体流。  
   * **结果**：视频预览视口全程保持满高/满宽居中呈现，画面清晰且动态连续，顶部单线截断现象 \$100%\$ 消失。  
2. **交互稳定性测试**：  
   * 连续执行拖动进度条（`Seek`）、快速暂停/播放切换、单帧步进（`Step`），以及在 Encoder 与 Player 工作区来回频繁切换。  
   * **结果**：预览视口始终无黑屏、无画面残留、无画面截断，控件遮挡彻底消除。

